/*
|--------------------------------------------------------------------------
| 霍尔轮速里程计节点
|--------------------------------------------------------------------------
| 订阅 ATmega 串口桥发布的三轮霍尔反馈速度，按三全向轮正运动学积分 /odom。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include <geometry_msgs/Quaternion.h>
#include <nav_msgs/Odometry.h>
#include <ros/ros.h>
#include <std_msgs/Float32MultiArray.h>

namespace amseokbot_milo {

/*
|--------------------------------------------------------------------------
| 里程计参数
|--------------------------------------------------------------------------
| 保存三轮几何、反馈速度单位换算、坐标系名称和发布配置。
|--------------------------------------------------------------------------
*/
struct WheelOdometryConfig {
  double wheel_base_radius_m = 0.1337147;
  double wheel_radius_m = 0.0425;
  double wheel_feedback_scale = 100.0;
  double feedback_timeout_s = 0.5;
  double publish_rate_hz = 30.0;
  std::string odom_frame = "odom";
  std::string base_frame = "base_link";
  std::vector<double> wheel_angles_deg {29.9459, 149.6229, -90.8207};
  std::vector<double> wheel_signs {1.0, 1.0, 1.0};
};

/*
|--------------------------------------------------------------------------
| 通用数学工具
|--------------------------------------------------------------------------
| 提供角度转换、角度归一化和 3x3 线性方程求解，避免引入额外矩阵依赖。
|--------------------------------------------------------------------------
*/
double degToRad(double deg) {
  return deg * M_PI / 180.0;
}

double normalizeAngle(double value) {
  while (value > M_PI) {
    value -= 2.0 * M_PI;
  }
  while (value < -M_PI) {
    value += 2.0 * M_PI;
  }
  return value;
}

geometry_msgs::Quaternion quaternionFromYaw(double yaw) {
  geometry_msgs::Quaternion q;
  q.z = std::sin(yaw * 0.5);
  q.w = std::cos(yaw * 0.5);
  return q;
}

bool solve3x3(double a[3][4], std::array<double, 3>& out) {
  for (int col = 0; col < 3; ++col) {
    int pivot = col;
    for (int row = col + 1; row < 3; ++row) {
      if (std::abs(a[row][col]) > std::abs(a[pivot][col])) {
        pivot = row;
      }
    }
    if (std::abs(a[pivot][col]) < 1e-9) {
      return false;
    }
    if (pivot != col) {
      for (int k = col; k < 4; ++k) {
        std::swap(a[col][k], a[pivot][k]);
      }
    }
    const double div = a[col][col];
    for (int k = col; k < 4; ++k) {
      a[col][k] /= div;
    }
    for (int row = 0; row < 3; ++row) {
      if (row == col) {
        continue;
      }
      const double factor = a[row][col];
      for (int k = col; k < 4; ++k) {
        a[row][k] -= factor * a[col][k];
      }
    }
  }

  out = {a[0][3], a[1][3], a[2][3]};
  return true;
}

/*
|--------------------------------------------------------------------------
| 三全向轮正运动学模型
|--------------------------------------------------------------------------
| 把三轮反馈角速度还原为车体坐标系 vx、vy、wz，供里程计积分使用。
|--------------------------------------------------------------------------
*/
class OmniTriangleForwardKinematics {
 public:
  explicit OmniTriangleForwardKinematics(const WheelOdometryConfig& config) : config_(config) {}

  bool twistFromWheelFeedback(const std::array<double, 3>& wheel_feedback, std::array<double, 3>& body_twist) const {
    double matrix[3][4] {};
    for (std::size_t i = 0; i < 3; ++i) {
      const double theta = degToRad(config_.wheel_angles_deg[i]);
      const double sign = config_.wheel_signs[i];
      matrix[i][0] = -std::sin(theta);
      matrix[i][1] = std::cos(theta);
      matrix[i][2] = config_.wheel_base_radius_m;
      matrix[i][3] = (wheel_feedback[i] / config_.wheel_feedback_scale) * config_.wheel_radius_m / sign;
    }
    return solve3x3(matrix, body_twist);
  }

 private:
  WheelOdometryConfig config_;
};

/*
|--------------------------------------------------------------------------
| 霍尔轮速里程计 ROS 节点
|--------------------------------------------------------------------------
| 接收 /chassis/wheel_feedback，积分机器人平面位姿，发布 /odom 和 odom->base_link。
|--------------------------------------------------------------------------
*/
class WheelOdometryNode {
 public:
  WheelOdometryNode() : private_nh_("~"), config_(loadConfig()), kinematics_(config_) {
    std::string wheel_feedback_topic;
    std::string odom_topic;
    private_nh_.param<std::string>("wheel_feedback_topic", wheel_feedback_topic, "/chassis/wheel_feedback");
    private_nh_.param<std::string>("odom_topic", odom_topic, "/odom");

    odom_pub_ = nh_.advertise<nav_msgs::Odometry>(odom_topic, 20);
    wheel_sub_ = nh_.subscribe(wheel_feedback_topic, 20, &WheelOdometryNode::handleWheelFeedback, this);
    timer_ = nh_.createTimer(
      ros::Duration(1.0 / std::max(1.0, config_.publish_rate_hz)),
      &WheelOdometryNode::handleTimer,
      this);

    ROS_INFO("wheel_odometry: feedback=%s odom=%s frame=%s base=%s",
             wheel_feedback_topic.c_str(),
             odom_topic.c_str(),
             config_.odom_frame.c_str(),
             config_.base_frame.c_str());
  }

 private:
  WheelOdometryConfig loadConfig() {
    WheelOdometryConfig config;
    private_nh_.param("wheel_base_radius_m", config.wheel_base_radius_m, config.wheel_base_radius_m);
    private_nh_.param("wheel_radius_m", config.wheel_radius_m, config.wheel_radius_m);
    private_nh_.param("wheel_feedback_scale", config.wheel_feedback_scale, config.wheel_feedback_scale);
    private_nh_.param("feedback_timeout_s", config.feedback_timeout_s, config.feedback_timeout_s);
    private_nh_.param("publish_rate_hz", config.publish_rate_hz, config.publish_rate_hz);
    private_nh_.param<std::string>("odom_frame", config.odom_frame, config.odom_frame);
    private_nh_.param<std::string>("base_frame", config.base_frame, config.base_frame);
    private_nh_.getParam("wheel_angles_deg", config.wheel_angles_deg);
    private_nh_.getParam("wheel_signs", config.wheel_signs);
    if (config.wheel_angles_deg.size() != 3) {
      config.wheel_angles_deg = {29.9459, 149.6229, -90.8207};
    }
    if (config.wheel_signs.size() != 3) {
      config.wheel_signs = {1.0, 1.0, 1.0};
    }
    if (std::abs(config.wheel_feedback_scale) < 1e-9) {
      config.wheel_feedback_scale = 100.0;
    }
    return config;
  }

  void handleWheelFeedback(const std_msgs::Float32MultiArray::ConstPtr& msg) {
    if (msg->data.size() < 3) {
      ROS_WARN_THROTTLE(1.0, "wheel_odometry: wheel feedback needs 3 values");
      return;
    }

    const std::array<double, 3> feedback {
      msg->data[0],
      msg->data[1],
      msg->data[2]
    };
    std::array<double, 3> body_twist {0.0, 0.0, 0.0};
    if (!kinematics_.twistFromWheelFeedback(feedback, body_twist)) {
      ROS_WARN_THROTTLE(1.0, "wheel_odometry: kinematics matrix is singular");
      return;
    }

    const ros::Time now = ros::Time::now();
    if (!last_feedback_time_.isZero()) {
      const double dt = (now - last_feedback_time_).toSec();
      if (dt > 0.0 && dt <= config_.feedback_timeout_s) {
        integrate(body_twist, dt);
      }
    }

    last_body_twist_ = body_twist;
    last_feedback_time_ = now;
  }

  void integrate(const std::array<double, 3>& body_twist, double dt) {
    const double cos_yaw = std::cos(yaw_rad_);
    const double sin_yaw = std::sin(yaw_rad_);
    const double world_vx = cos_yaw * body_twist[0] - sin_yaw * body_twist[1];
    const double world_vy = sin_yaw * body_twist[0] + cos_yaw * body_twist[1];
    x_m_ += world_vx * dt;
    y_m_ += world_vy * dt;
    yaw_rad_ = normalizeAngle(yaw_rad_ + body_twist[2] * dt);
  }

  void handleTimer(const ros::TimerEvent&) {
    const ros::Time now = ros::Time::now();
    std::array<double, 3> twist = last_body_twist_;
    if (last_feedback_time_.isZero() || (now - last_feedback_time_).toSec() > config_.feedback_timeout_s) {
      twist = {0.0, 0.0, 0.0};
    }

    const geometry_msgs::Quaternion orientation = quaternionFromYaw(yaw_rad_);
    nav_msgs::Odometry odom;
    odom.header.stamp = now;
    odom.header.frame_id = config_.odom_frame;
    odom.child_frame_id = config_.base_frame;
    odom.pose.pose.position.x = x_m_;
    odom.pose.pose.position.y = y_m_;
    odom.pose.pose.orientation = orientation;
    odom.twist.twist.linear.x = twist[0];
    odom.twist.twist.linear.y = twist[1];
    odom.twist.twist.angular.z = twist[2];
    odom_pub_.publish(odom);

  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher odom_pub_;
  ros::Subscriber wheel_sub_;
  ros::Timer timer_;
  WheelOdometryConfig config_;
  OmniTriangleForwardKinematics kinematics_;
  std::array<double, 3> last_body_twist_ {0.0, 0.0, 0.0};
  ros::Time last_feedback_time_;
  double x_m_ = 0.0;
  double y_m_ = 0.0;
  double yaw_rad_ = 0.0;
};

}  // namespace amseokbot_milo

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 初始化霍尔轮速里程计节点。
|--------------------------------------------------------------------------
*/
int main(int argc, char** argv) {
  ros::init(argc, argv, "wheel_odometry");
  amseokbot_milo::WheelOdometryNode node;
  ros::spin();
  return 0;
}
