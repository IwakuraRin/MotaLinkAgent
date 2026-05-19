/*
|--------------------------------------------------------------------------
| ATmega 串口桥接节点
|--------------------------------------------------------------------------
| 负责把 ROS 文本命令写入 ATmega USB-UART，并把 ATmega 返回的文本行发布到 ROS。
| 面向 ATmega 控制板命名和实现。
|--------------------------------------------------------------------------
*/

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <termios.h>

#include <ros/ros.h>
#include <std_msgs/String.h>

namespace simple_robotic_arm {

/*
|--------------------------------------------------------------------------
| 串口底层工具
|--------------------------------------------------------------------------
| 使用 Linux termios 打开、配置、读写 USB-UART，避免依赖 Python pyserial。
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
    const ssize_t written = ::write(fd_, line.data(), line.size());
    if (written < 0) {
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
| ROS 串口桥节点
|--------------------------------------------------------------------------
| 订阅 ~tx 写串口，读取串口行后发布 ~rx，供 ATmega 固件联调和底盘控制使用。
|--------------------------------------------------------------------------
*/
class AtmegaSerialBridgeNode {
 public:
  AtmegaSerialBridgeNode() : private_nh_("~") {
    private_nh_.param<std::string>("port", port_, "/dev/ttyUSB0");
    private_nh_.param("baud", baud_, 115200);
    private_nh_.param("append_newline_on_tx", append_newline_, true);

    if (!serial_.openPort(port_, baud_)) {
      ros::shutdown();
      return;
    }

    rx_pub_ = private_nh_.advertise<std_msgs::String>("rx", 100);
    tx_sub_ = private_nh_.subscribe("tx", 50, &AtmegaSerialBridgeNode::handleTx, this);
    running_.store(true);
    read_thread_ = std::thread(&AtmegaSerialBridgeNode::readLoop, this);

    ROS_INFO("atmega_serial_bridge: %s @ %d, topics ~tx ~rx", port_.c_str(), baud_);
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
      } else if (byte != r) {
        line.push_back(byte);
      }
    }
  }

  void publishLine(const std::string& line) {
    if (line.empty()) {
      return;
    }
    std_msgs::String msg;
    msg.data = line;
    rx_pub_.publish(msg);
  }

  ros::NodeHandle private_nh_;
  ros::Publisher rx_pub_;
  ros::Subscriber tx_sub_;
  SerialPort serial_;
  std::thread read_thread_;
  std::atomic<bool> running_ {false};
  std::string port_;
  int baud_ = 115200;
  bool append_newline_ = true;
};

}  // namespace simple_robotic_arm

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 初始化 ATmega 串口桥接节点。
|--------------------------------------------------------------------------
*/
int main(int argc, char** argv) {
  ros::init(argc, argv, "atmega_serial_bridge");
  simple_robotic_arm::AtmegaSerialBridgeNode node;
  ros::spin();
  return 0;
}
