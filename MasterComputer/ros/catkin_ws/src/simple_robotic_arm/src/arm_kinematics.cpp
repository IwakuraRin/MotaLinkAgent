/*
|--------------------------------------------------------------------------
| 机械臂运动学节点
|--------------------------------------------------------------------------
| 提供 5 关节机械臂的基础正运动学计算；订阅关节角度数组并发布末端位置。
|--------------------------------------------------------------------------
*/

#include <array>
#include <cmath>
#include <string>

#include <ros/ros.h>
#include <std_msgs/Float32MultiArray.h>

namespace simple_robotic_arm {

/*
|--------------------------------------------------------------------------
| 机械臂几何模型
|--------------------------------------------------------------------------
| 第一段和第二段连杆决定末端位置，腕部关节只影响姿态，不改变此处的位置输出。
|--------------------------------------------------------------------------
*/
class ArmKinematicsModel {
 public:
  ArmKinematicsModel(double link_1_m, double link_2_m) : link_1_m_(link_1_m), link_2_m_(link_2_m) {}

  std::array<double, 3> forwardPosition(const std::array<double, 5>& q) const {
    const double q1 = q[0];
    const double q2 = q[1];
    const double q3 = q[2];
    const double radial = link_1_m_ * std::cos(q2) + link_2_m_ * std::cos(q2 + q3);
    const double z = link_1_m_ * std::sin(q2) + link_2_m_ * std::sin(q2 + q3);
    return {radial * std::cos(q1), radial * std::sin(q1), z};
  }

 private:
  double link_1_m_ = 0.170;
  double link_2_m_ = 0.110;
};

/*
|--------------------------------------------------------------------------
| ROS 机械臂节点
|--------------------------------------------------------------------------
| 订阅 ~joint_target_rad，发布 ~tcp_position_m，方便上层验证机械臂模型。
|--------------------------------------------------------------------------
*/
class ArmKinematicsNode {
 public:
  ArmKinematicsNode() : private_nh_("~"), model_(loadLink1(), loadLink2()) {
    std::string joint_topic;
    std::string position_topic;
    private_nh_.param<std::string>("joint_target_topic", joint_topic, "joint_target_rad");
    private_nh_.param<std::string>("tcp_position_topic", position_topic, "tcp_position_m");
    position_pub_ = private_nh_.advertise<std_msgs::Float32MultiArray>(position_topic, 10);
    joint_sub_ = private_nh_.subscribe(joint_topic, 10, &ArmKinematicsNode::handleJoints, this);
    ROS_INFO("arm_kinematics_cpp: joint=%s tcp=%s", joint_topic.c_str(), position_topic.c_str());
  }

 private:
  double loadLink1() {
    double value = 0.170;
    private_nh_.param("link_1_m", value, value);
    return value;
  }

  double loadLink2() {
    double value = 0.110;
    private_nh_.param("link_2_m", value, value);
    return value;
  }

  void handleJoints(const std_msgs::Float32MultiArray::ConstPtr& msg) {
    std::array<double, 5> q {0.0, 0.0, 0.0, 0.0, 0.0};
    for (std::size_t index = 0; index < q.size() && index < msg->data.size(); ++index) {
      q[index] = msg->data[index];
    }
    const auto position = model_.forwardPosition(q);
    std_msgs::Float32MultiArray out;
    out.data = {
      static_cast<float>(position[0]),
      static_cast<float>(position[1]),
      static_cast<float>(position[2])
    };
    position_pub_.publish(out);
  }

  ros::NodeHandle private_nh_;
  ros::Publisher position_pub_;
  ros::Subscriber joint_sub_;
  ArmKinematicsModel model_;
};

}  // namespace simple_robotic_arm

int main(int argc, char** argv) {
  ros::init(argc, argv, "arm_kinematics");
  simple_robotic_arm::ArmKinematicsNode node;
  ros::spin();
  return 0;
}
