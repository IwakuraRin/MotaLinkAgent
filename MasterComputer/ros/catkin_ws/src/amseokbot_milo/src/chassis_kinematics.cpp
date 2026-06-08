/*
|--------------------------------------------------------------------------
| 底盘速度串口节点
|--------------------------------------------------------------------------
| 把 /cmd_vel 车体速度直接转发给 ATmega，下位机负责逆运动学、限幅和平滑。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <sstream>
#include <string>

#include <geometry_msgs/Twist.h>
#include <ros/ros.h>
#include <std_msgs/Bool.h>
#include <std_msgs/Float32MultiArray.h>
#include <std_msgs/String.h>

namespace amseokbot_milo {

struct ChassisCommandConfig {
  double max_linear_mps = 0.80;
  double max_angular_radps = 2.50;
  double command_timeout_s = 0.35;
  double control_rate_hz = 30.0;
  bool publish_serial = true;
  std::string serial_prefix = "CHASSIS";
};

double clampValue(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

/*
|--------------------------------------------------------------------------
| ROS 底盘节点
|--------------------------------------------------------------------------
| 订阅速度指令和下位机急停状态，定频输出底盘整体速度命令。
|--------------------------------------------------------------------------
*/
class ChassisKinematicsNode {
 public:
  ChassisKinematicsNode() : private_nh_("~"), config_(loadConfig()) {
    std::string cmd_vel_topic;
    std::string body_twist_topic;
    std::string serial_tx_topic;
    std::string obstacle_stop_topic;

    private_nh_.param<std::string>("cmd_vel_topic", cmd_vel_topic, "/cmd_vel");
    private_nh_.param<std::string>("body_twist_topic", body_twist_topic, "/chassis/body_twist");
    private_nh_.param<std::string>("serial_tx_topic", serial_tx_topic, "/atmega_serial_bridge/tx");
    private_nh_.param<std::string>("obstacle_stop_topic", obstacle_stop_topic, "/safety/obstacle_stop");

    body_twist_pub_ = nh_.advertise<std_msgs::Float32MultiArray>(body_twist_topic, 10);
    serial_pub_ = nh_.advertise<std_msgs::String>(serial_tx_topic, 10);
    cmd_sub_ = nh_.subscribe(cmd_vel_topic, 10, &ChassisKinematicsNode::handleTwist, this);
    obstacle_sub_ = nh_.subscribe(obstacle_stop_topic, 10, &ChassisKinematicsNode::handleObstacleStop, this);

    last_cmd_time_ = ros::Time(0);
    timer_ = nh_.createTimer(ros::Duration(1.0 / std::max(1.0, config_.control_rate_hz)), &ChassisKinematicsNode::handleTimer, this);

    ROS_INFO("chassis_command_cpp: cmd=%s serial=%s obstacle=%s prefix=%s",
             cmd_vel_topic.c_str(),
             serial_tx_topic.c_str(),
             obstacle_stop_topic.c_str(),
             config_.serial_prefix.c_str());
  }

 private:
  ChassisCommandConfig loadConfig() {
    ChassisCommandConfig config;
    private_nh_.param("max_linear_mps", config.max_linear_mps, config.max_linear_mps);
    private_nh_.param("max_angular_radps", config.max_angular_radps, config.max_angular_radps);
    private_nh_.param("command_timeout_s", config.command_timeout_s, config.command_timeout_s);
    private_nh_.param("control_rate_hz", config.control_rate_hz, config.control_rate_hz);
    private_nh_.param("publish_serial", config.publish_serial, config.publish_serial);
    private_nh_.param<std::string>("serial_command_prefix", config.serial_prefix, config.serial_prefix);
    return config;
  }

  void handleTwist(const geometry_msgs::Twist::ConstPtr& msg) {
    target_twist_ = limitTwist(*msg);
    last_cmd_time_ = ros::Time::now();
  }

  void handleObstacleStop(const std_msgs::Bool::ConstPtr& msg) {
    obstacle_stop_ = msg->data;
    if (obstacle_stop_) {
      target_twist_ = geometry_msgs::Twist();
      last_cmd_time_ = ros::Time::now();
      ROS_WARN_THROTTLE(1.0, "chassis_command_cpp: obstacle stop active, output zero chassis speed");
    }
  }

  geometry_msgs::Twist limitTwist(const geometry_msgs::Twist& twist) const {
    geometry_msgs::Twist out = twist;
    out.linear.x = clampValue(out.linear.x, -config_.max_linear_mps, config_.max_linear_mps);
    out.linear.y = clampValue(out.linear.y, -config_.max_linear_mps, config_.max_linear_mps);
    out.angular.z = clampValue(out.angular.z, -config_.max_angular_radps, config_.max_angular_radps);
    return out;
  }

  geometry_msgs::Twist activeTwist() const {
    if (obstacle_stop_ || last_cmd_time_.isZero()) {
      return geometry_msgs::Twist();
    }
    const double age = (ros::Time::now() - last_cmd_time_).toSec();
    if (age > config_.command_timeout_s) {
      return geometry_msgs::Twist();
    }
    return target_twist_;
  }

  std::string formatSerialCommand(const geometry_msgs::Twist& twist) const {
    std::ostringstream out;
    out.setf(std::ios::fixed);
    out.precision(6);
    out << config_.serial_prefix << " " << twist.linear.x << " " << twist.linear.y << " " << twist.angular.z;
    return out.str();
  }

  void handleTimer(const ros::TimerEvent&) {
    const geometry_msgs::Twist twist = activeTwist();

    std_msgs::Float32MultiArray body_msg;
    body_msg.data = {
      static_cast<float>(twist.linear.x),
      static_cast<float>(twist.linear.y),
      static_cast<float>(twist.angular.z)
    };
    body_twist_pub_.publish(body_msg);

    if (config_.publish_serial) {
      std_msgs::String serial_msg;
      serial_msg.data = formatSerialCommand(twist);
      serial_pub_.publish(serial_msg);
    }
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher body_twist_pub_;
  ros::Publisher serial_pub_;
  ros::Subscriber cmd_sub_;
  ros::Subscriber obstacle_sub_;
  ros::Timer timer_;
  ChassisCommandConfig config_;
  geometry_msgs::Twist target_twist_;
  ros::Time last_cmd_time_;
  bool obstacle_stop_ = false;
};

}  // namespace amseokbot_milo

int main(int argc, char** argv) {
  ros::init(argc, argv, "chassis_kinematics");
  amseokbot_milo::ChassisKinematicsNode node;
  ros::spin();
  return 0;
}
