/*
|--------------------------------------------------------------------------
| 三全向轮底盘运动学节点
|--------------------------------------------------------------------------
| 负责把 /cmd_vel 车体速度转换为三颗 85mm 全向轮角速度，并按 ATmega 串口
| 文本协议发布到底层串口桥。几何参数来自 2026-05-10 的三角底盘 STEP 文件。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <string>
#include <vector>

#include <geometry_msgs/Twist.h>
#include <ros/ros.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/String.h>

namespace simple_robotic_arm {

/*
|--------------------------------------------------------------------------
| 底盘几何参数
|--------------------------------------------------------------------------
| 保存三颗全向轮相对底盘中心的角度、半径、轮半径和安全速度上限。
|--------------------------------------------------------------------------
*/
struct ChassisConfig {
  double wheel_base_radius_m     = 0.1337147;
  double wheel_radius_m          = 0.0425;
  double max_wheel_omega_rad_s   = 25.10;
  double command_timeout_s       = 0.35;
  double control_rate_hz         = 30.0;
  double twist_filter_tau_s      = 0.08;
  bool   publish_serial          = true;
  std::string serial_prefix      = "WHEEL";
  std::vector<double> wheel_angles_deg {29.9459, 149.6229, -90.8207};
  std::vector<double> wheel_signs      {1.0, 1.0, 1.0};
};

/*
|--------------------------------------------------------------------------
| 通用数学工具
|--------------------------------------------------------------------------
| 放置角度转换、限幅、三维向量缩放等纯工具函数。
|--------------------------------------------------------------------------
*/
double clampValue(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

double degToRad(double deg) {
  return deg * M_PI / 180.0;
}

std::array<double, 3> scaleToWheelLimit(
  const std::array<double, 3>& wheel_omega,
  double max_abs_omega
) {
  double largest = 0.0;
  for (double value : wheel_omega) {
    largest = std::max(largest, std::abs(value));
  }
  if (largest <= max_abs_omega || largest <= 1e-9) {
    return wheel_omega;
  }
  const double scale = max_abs_omega / largest;
  return {wheel_omega[0] * scale, wheel_omega[1] * scale, wheel_omega[2] * scale};
}

/*
|--------------------------------------------------------------------------
| 三全向轮逆运动学模型
|--------------------------------------------------------------------------
| 输入车体坐标系 twist [vx, vy, wz]，输出每颗轮子的角速度 rad/s。
| 车体坐标约定：x 向前、y 向左、z 向上。
|--------------------------------------------------------------------------
*/
class OmniTriangleKinematics {
 public:
  explicit OmniTriangleKinematics(const ChassisConfig& config) : config_(config) {}

  std::array<double, 3> wheelOmegaFromTwist(const geometry_msgs::Twist& twist) const {
    std::array<double, 3> out {0.0, 0.0, 0.0};
    for (std::size_t index = 0; index < 3; ++index) {
      const double theta = degToRad(config_.wheel_angles_deg[index]);
      const double sign  = config_.wheel_signs[index];
      const double linear_speed = sign * (
        -std::sin(theta) * twist.linear.x +
         std::cos(theta) * twist.linear.y +
         config_.wheel_base_radius_m * twist.angular.z
      );
      out[index] = linear_speed / config_.wheel_radius_m;
    }
    return scaleToWheelLimit(out, config_.max_wheel_omega_rad_s);
  }

 private:
  ChassisConfig config_;
};

/*
|--------------------------------------------------------------------------
| ROS 底盘节点
|--------------------------------------------------------------------------
| 订阅速度指令，定频输出轮速调试话题和 ATmega 串口命令。
|--------------------------------------------------------------------------
*/
class ChassisKinematicsNode {
 public:
  ChassisKinematicsNode() : private_nh_("~"), kinematics_(loadConfig()) {
    std::string cmd_vel_topic;
    std::string wheel_omega_topic;
    std::string serial_tx_topic;

    private_nh_.param<std::string>("cmd_vel_topic", cmd_vel_topic, "/cmd_vel");
    private_nh_.param<std::string>("wheel_omega_topic", wheel_omega_topic, "/chassis/wheel_omega");
    private_nh_.param<std::string>("serial_tx_topic", serial_tx_topic, "/atmega_serial_bridge/tx");

    wheel_pub_ = nh_.advertise<std_msgs::Float32MultiArray>(wheel_omega_topic, 10);
    serial_pub_ = nh_.advertise<std_msgs::String>(serial_tx_topic, 10);
    cmd_sub_ = nh_.subscribe(cmd_vel_topic, 10, &ChassisKinematicsNode::handleTwist, this);

    last_cmd_time_ = ros::Time(0);
    timer_ = nh_.createTimer(ros::Duration(1.0 / std::max(1.0, config_.control_rate_hz)), &ChassisKinematicsNode::handleTimer, this);

    ROS_INFO("chassis_kinematics_cpp: cmd=%s serial=%s publish_serial=%s", cmd_vel_topic.c_str(), serial_tx_topic.c_str(), config_.publish_serial ? "true" : "false");
  }

 private:
  ChassisConfig loadConfig() {
    ChassisConfig config;
    private_nh_.param("wheel_base_radius_m", config.wheel_base_radius_m, config.wheel_base_radius_m);
    private_nh_.param("wheel_radius_m", config.wheel_radius_m, config.wheel_radius_m);
    private_nh_.param("wheel_omega_max_rad_s", config.max_wheel_omega_rad_s, config.max_wheel_omega_rad_s);
    private_nh_.param("command_timeout_s", config.command_timeout_s, config.command_timeout_s);
    private_nh_.param("control_rate_hz", config.control_rate_hz, config.control_rate_hz);
    private_nh_.param("twist_filter_tau_s", config.twist_filter_tau_s, config.twist_filter_tau_s);
    private_nh_.param("publish_serial", config.publish_serial, config.publish_serial);
    private_nh_.param<std::string>("serial_command_prefix", config.serial_prefix, config.serial_prefix);
    private_nh_.getParam("wheel_angles_deg", config.wheel_angles_deg);
    private_nh_.getParam("wheel_signs", config.wheel_signs);
    if (config.wheel_angles_deg.size() != 3) {
      config.wheel_angles_deg = {29.9459, 149.6229, -90.8207};
    }
    if (config.wheel_signs.size() != 3) {
      config.wheel_signs = {1.0, 1.0, 1.0};
    }
    config_ = config;
    return config;
  }

  void handleTwist(const geometry_msgs::Twist::ConstPtr& msg) {
    target_twist_ = *msg;
    last_cmd_time_ = ros::Time::now();
  }

  geometry_msgs::Twist activeTwist() const {
    if (last_cmd_time_.isZero()) {
      return geometry_msgs::Twist();
    }
    const double age = (ros::Time::now() - last_cmd_time_).toSec();
    if (age > config_.command_timeout_s) {
      return geometry_msgs::Twist();
    }
    return target_twist_;
  }

  std::string formatSerialCommand(const std::array<double, 3>& wheel_omega) const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(4);
    out << config_.serial_prefix << " " << wheel_omega[0] << " " << wheel_omega[1] << " " << wheel_omega[2];
    return out.str();
  }

  void handleTimer(const ros::TimerEvent&) {
    const auto wheel_omega = kinematics_.wheelOmegaFromTwist(activeTwist());

    std_msgs::Float32MultiArray wheel_msg;
    wheel_msg.data = {
      static_cast<float>(wheel_omega[0]),
      static_cast<float>(wheel_omega[1]),
      static_cast<float>(wheel_omega[2])
    };
    wheel_pub_.publish(wheel_msg);

    if (config_.publish_serial) {
      std_msgs::String serial_msg;
      serial_msg.data = formatSerialCommand(wheel_omega);
      serial_pub_.publish(serial_msg);
    }
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher wheel_pub_;
  ros::Publisher serial_pub_;
  ros::Subscriber cmd_sub_;
  ros::Timer timer_;
  ChassisConfig config_;
  OmniTriangleKinematics kinematics_;
  geometry_msgs::Twist target_twist_;
  ros::Time last_cmd_time_;
};

}  // namespace simple_robotic_arm

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 初始化 ROS 节点并进入事件循环。
|--------------------------------------------------------------------------
*/
int main(int argc, char** argv) {
  ros::init(argc, argv, "chassis_kinematics");
  simple_robotic_arm::ChassisKinematicsNode node;
  ros::spin();
  return 0;
}
