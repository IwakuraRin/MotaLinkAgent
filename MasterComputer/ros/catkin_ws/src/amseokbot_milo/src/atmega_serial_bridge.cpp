/*
|--------------------------------------------------------------------------
| ATmega 串口桥接节点
|--------------------------------------------------------------------------
| 负责把 ROS 文本命令写入 ATmega USB-UART，并把下位机返回的距离、安全事件发布到 ROS。
|--------------------------------------------------------------------------
*/

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <termios.h>

#include <ros/ros.h>
#include <std_msgs/Bool.h>
#include <std_msgs/String.h>
#include <std_msgs/UInt16.h>

namespace amseokbot_milo {

/*
|--------------------------------------------------------------------------
| 串口底层工具
|--------------------------------------------------------------------------
| 使用 Linux termios 打开、配置、读写 USB-UART，并保证一行命令完整写入。
|--------------------------------------------------------------------------
*/
speed_t baudToTermios(int baud) {
  switch (baud) {
    case 9600: return B9600;
    case 19200: return B19200;
    case 38400: return B38400;
    case 57600: return B57600;
    case 115200: return B115200;
    case 230400: return B230400;
    case 460800: return B460800;
    case 921600: return B921600;
    default: return B115200;
  }
}

class SerialPort {
 public:
  ~SerialPort() { closePort(); }

  bool openPort(const std::string& port, int baud) {
    fd_ = ::open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) {
      ROS_ERROR("无法打开 ATmega 串口 %s: %s", port.c_str(), std::strerror(errno));
      return false;
    }

    termios options {};
    if (tcgetattr(fd_, &options) != 0) {
      ROS_ERROR("读取串口配置失败: %s", std::strerror(errno));
      closePort();
      return false;
    }

    cfmakeraw(&options);
    const speed_t speed = baudToTermios(baud);
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_cflag &= ~CRTSCTS;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cc[VMIN] = 0;
    options.c_cc[VTIME] = 2;

    if (tcsetattr(fd_, TCSANOW, &options) != 0) {
      ROS_ERROR("写入串口配置失败: %s", std::strerror(errno));
      closePort();
      return false;
    }
    return true;
  }

  void closePort() {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool writeLine(const std::string& line) {
    if (fd_ < 0) {
      return false;
    }

    std::size_t offset = 0;
    while (offset < line.size()) {
      const ssize_t written = ::write(fd_, line.data() + offset, line.size() - offset);
      if (written > 0) {
        offset += static_cast<std::size_t>(written);
        continue;
      }
      if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        continue;
      }
      ROS_WARN("ATmega 串口写失败: %s", std::strerror(errno));
      return false;
    }
    return true;
  }

  bool readByte(char& byte) {
    if (fd_ < 0) {
      return false;
    }
    const ssize_t n = ::read(fd_, &byte, 1);
    return n == 1;
  }

 private:
  int fd_ = -1;
};

/*
|--------------------------------------------------------------------------
| ATmega 行协议解析
|--------------------------------------------------------------------------
| 解析 TEL SR04 / EVT OBSTACLE / ERR MOTOR obstacle，输出距离和急停状态话题。
|--------------------------------------------------------------------------
*/
bool parseKeyUInt(const std::string& line, const std::string& key, uint32_t& value) {
  const std::size_t begin = line.find(key);
  if (begin == std::string::npos) {
    return false;
  }
  const std::size_t number_begin = begin + key.size();
  std::size_t number_end = number_begin;
  while (number_end < line.size() && line[number_end] >= 0 && line[number_end] <= 9) {
    number_end += 1;
  }
  if (number_end == number_begin) {
    return false;
  }
  try {
    value = static_cast<uint32_t>(std::stoul(line.substr(number_begin, number_end - number_begin)));
    return true;
  } catch (...) {
    return false;
  }
}

bool lineMeansObstacleStop(const std::string& line) {
  return line.find("EVT OBSTACLE STOP") != std::string::npos ||
         line.find("ERR MOTOR obstacle") != std::string::npos ||
         line.find("blocked=1") != std::string::npos;
}

bool lineMeansObstacleClear(const std::string& line) {
  return line.find("EVT OBSTACLE CLEAR") != std::string::npos ||
         line.find("blocked=0") != std::string::npos;
}

/*
|--------------------------------------------------------------------------
| ROS 串口桥节点
|--------------------------------------------------------------------------
| 订阅 ~tx 写串口，读取串口行后发布原始行、前方距离和本地急停状态。
|--------------------------------------------------------------------------
*/
class AtmegaSerialBridgeNode {
 public:
  AtmegaSerialBridgeNode() : private_nh_("~") {
    private_nh_.param<std::string>("port", port_, "/dev/ttyUSB0");
    private_nh_.param("baud", baud_, 115200);
    private_nh_.param("append_newline_on_tx", append_newline_, true);
    private_nh_.param<std::string>("front_range_topic", front_range_topic_, "/safety/front_range_mm");
    private_nh_.param<std::string>("obstacle_stop_topic", obstacle_stop_topic_, "/safety/obstacle_stop");
    private_nh_.param<std::string>("safety_event_topic", safety_event_topic_, "/safety/events");

    if (!serial_.openPort(port_, baud_)) {
      ros::shutdown();
      return;
    }

    rx_pub_ = private_nh_.advertise<std_msgs::String>("rx", 100);
    range_pub_ = nh_.advertise<std_msgs::UInt16>(front_range_topic_, 20);
    obstacle_pub_ = nh_.advertise<std_msgs::Bool>(obstacle_stop_topic_, 20, true);
    safety_event_pub_ = nh_.advertise<std_msgs::String>(safety_event_topic_, 50);
    tx_sub_ = private_nh_.subscribe("tx", 50, &AtmegaSerialBridgeNode::handleTx, this);
    running_.store(true);
    read_thread_ = std::thread(&AtmegaSerialBridgeNode::readLoop, this);

    ROS_INFO("atmega_serial_bridge: %s @ %d, range=%s obstacle=%s", port_.c_str(), baud_, front_range_topic_.c_str(), obstacle_stop_topic_.c_str());
  }

  ~AtmegaSerialBridgeNode() {
    running_.store(false);
    if (read_thread_.joinable()) {
      read_thread_.join();
    }
  }

 private:
  void handleTx(const std_msgs::String::ConstPtr& msg) {
    std::string data = msg->data;
    if (append_newline_ && (data.empty() || data.back() != n)) {
      data.push_back(n);
    }
    serial_.writeLine(data);
  }

  void readLoop() {
    std::string line;
    ros::Rate rate(500.0);
    while (running_.load() && ros::ok()) {
      char byte = 0;
      if (!serial_.readByte(byte)) {
        rate.sleep();
        continue;
      }
      if (byte == n) {
        publishLine(line);
        line.clear();
      } else if (byte != r && line.size() < 160) {
        line.push_back(byte);
      } else if (line.size() >= 160) {
        line.clear();
      }
    }
  }

  void publishLine(const std::string& line) {
    if (line.empty()) {
      return;
    }

    std_msgs::String raw_msg;
    raw_msg.data = line;
    rx_pub_.publish(raw_msg);

    uint32_t mm = 0;
    if (parseKeyUInt(line, "mm=", mm) && mm <= 65535U) {
      std_msgs::UInt16 range_msg;
      range_msg.data = static_cast<uint16_t>(mm);
      range_pub_.publish(range_msg);
    }

    if (lineMeansObstacleStop(line) || lineMeansObstacleClear(line)) {
      const bool blocked = lineMeansObstacleStop(line) && !lineMeansObstacleClear(line);
      std_msgs::Bool obstacle_msg;
      obstacle_msg.data = blocked;
      obstacle_pub_.publish(obstacle_msg);
      safety_event_pub_.publish(raw_msg);
    }
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher rx_pub_;
  ros::Publisher range_pub_;
  ros::Publisher obstacle_pub_;
  ros::Publisher safety_event_pub_;
  ros::Subscriber tx_sub_;
  SerialPort serial_;
  std::thread read_thread_;
  std::atomic<bool> running_ {false};
  std::string port_;
  std::string front_range_topic_;
  std::string obstacle_stop_topic_;
  std::string safety_event_topic_;
  int baud_ = 115200;
  bool append_newline_ = true;
};

}  // namespace amseokbot_milo

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 初始化 ATmega 串口桥接节点。
|--------------------------------------------------------------------------
*/
int main(int argc, char** argv) {
  ros::init(argc, argv, "atmega_serial_bridge");
  amseokbot_milo::AtmegaSerialBridgeNode node;
  ros::spin();
  return 0;
}
