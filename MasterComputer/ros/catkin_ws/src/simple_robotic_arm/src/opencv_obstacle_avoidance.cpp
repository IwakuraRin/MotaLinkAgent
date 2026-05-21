/*
|--------------------------------------------------------------------------
| OpenCV 单目避障节点
|--------------------------------------------------------------------------
| 使用地面颜色采样、边缘和暗区启发式识别前方障碍物，并发布保守速度指令。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <array>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/Twist.h>
#include <opencv2/imgproc.hpp>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>

namespace simple_robotic_arm {

double clampValue(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

/*
|--------------------------------------------------------------------------
| 避障决策节点
|--------------------------------------------------------------------------
| 把前方 ROI 分为左中右三块，中心阻挡则停下转向，侧边阻挡则慢速偏航避开。
|--------------------------------------------------------------------------
*/
class OpenCVObstacleAvoidanceNode {
 public:
  OpenCVObstacleAvoidanceNode() : private_nh_("~") {
    loadParams();
    twist_pub_ = nh_.advertise<geometry_msgs::Twist>(cmd_topic_, 1);
    if (publish_debug_) {
      debug_pub_ = nh_.advertise<sensor_msgs::Image>(debug_topic_, 1);
    }
    image_sub_ = nh_.subscribe(image_topic_, 1, &OpenCVObstacleAvoidanceNode::handleImage, this);
    timer_ = nh_.createTimer(ros::Duration(0.1), &OpenCVObstacleAvoidanceNode::handleTimer, this);
    ROS_INFO("opencv_obstacle_avoidance_cpp: image=%s cmd=%s debug=%s", image_topic_.c_str(), cmd_topic_.c_str(), publish_debug_ ? "true" : "false");
  }

 private:
  void loadParams() {
    private_nh_.param<std::string>("image_topic", image_topic_, "/usb_cam/image_raw");
    private_nh_.param<std::string>("cmd_vel_topic", cmd_topic_, "/cmd_vel");
    private_nh_.param<std::string>("debug_image_topic", debug_topic_, "debug");
    private_nh_.param("publish_debug_image", publish_debug_, false);
    private_nh_.param("forward_speed", forward_speed_, 0.08);
    private_nh_.param("slow_speed", slow_speed_, 0.04);
    private_nh_.param("turn_speed", turn_speed_, 0.45);
    private_nh_.param("side_steer_gain", side_steer_gain_, 0.35);
    private_nh_.param("stop_on_no_image", stop_on_no_image_, true);
    private_nh_.param("no_image_timeout", no_image_timeout_, 1.0);
    private_nh_.param("floor_sample_y0", floor_sample_y0_, 0.82);
    private_nh_.param("floor_sample_y1", floor_sample_y1_, 0.96);
    private_nh_.param("roi_y0", roi_y0_, 0.38);
    private_nh_.param("roi_y1", roi_y1_, 0.88);
    private_nh_.param("roi_x0", roi_x0_, 0.12);
    private_nh_.param("roi_x1", roi_x1_, 0.88);
    private_nh_.param("hsv_floor_dist_thresh", hsv_floor_dist_thresh_, 42.0);
    private_nh_.param("edge_thresh", edge_thresh_, 70);
    private_nh_.param("dark_v_thresh", dark_v_thresh_, 55);
    private_nh_.param("min_saturation", min_saturation_, 35);
    private_nh_.param("center_stop_ratio", center_stop_ratio_, 0.18);
    private_nh_.param("side_slow_ratio", side_slow_ratio_, 0.24);
    private_nh_.param("clear_hysteresis", clear_hysteresis_, 0.04);
    private_nh_.param("min_obstacle_area", min_obstacle_area_, 600);
  }

  void handleTimer(const ros::TimerEvent&) {
    if (!stop_on_no_image_ || last_image_time_.isZero()) {
      return;
    }
    const double age = (ros::Time::now() - last_image_time_).toSec();
    if (age > no_image_timeout_) {
      ROS_WARN_THROTTLE(2.0, "opencv_obstacle_avoidance_cpp: no image %.2fs, stop", age);
      twist_pub_.publish(geometry_msgs::Twist());
    }
  }

  void handleImage(const sensor_msgs::Image::ConstPtr& msg) {
    last_image_time_ = ros::Time::now();
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(msg, "bgr8");
    } catch (const cv_bridge::Exception& ex) {
      ROS_WARN("opencv_obstacle_avoidance cv_bridge: %s", ex.what());
      return;
    }

    cv::Mat bgr = cv_ptr->image.clone();
    cv::Rect roi_box = makeRoiBox(bgr.cols, bgr.rows);
    cv::Mat mask = obstacleMask(bgr);
    cv::Mat roi_mask = mask(roi_box).clone();
    cv::Mat cleaned = cleanMask(roi_mask);
    const auto ratios = sectorRatios(cleaned);
    const geometry_msgs::Twist twist = decide(ratios[0], ratios[1], ratios[2]);
    twist_pub_.publish(twist);

    ROS_INFO_THROTTLE(1.0, "opencv_obstacle_avoidance_cpp: left=%.2f center=%.2f right=%.2f v=%.2f wz=%.2f", ratios[0], ratios[1], ratios[2], twist.linear.x, twist.angular.z);
    if (publish_debug_) {
      publishDebug(msg->header, bgr, roi_box, cleaned);
    }
  }

  cv::Rect makeRoiBox(int width, int height) const {
    int x0 = static_cast<int>(clampValue(roi_x0_, 0.0, 1.0) * width);
    int x1 = static_cast<int>(clampValue(roi_x1_, 0.0, 1.0) * width);
    int y0 = static_cast<int>(clampValue(roi_y0_, 0.0, 1.0) * height);
    int y1 = static_cast<int>(clampValue(roi_y1_, 0.0, 1.0) * height);
    if (x1 <= x0) { x0 = 0; x1 = width; }
    if (y1 <= y0) { y0 = static_cast<int>(0.4 * height); y1 = height; }
    return cv::Rect(x0, y0, x1 - x0, y1 - y0);
  }

  cv::Scalar floorModel(const cv::Mat& hsv) const {
    const int y0 = static_cast<int>(clampValue(floor_sample_y0_, 0.0, 1.0) * hsv.rows);
    const int y1 = static_cast<int>(clampValue(floor_sample_y1_, 0.0, 1.0) * hsv.rows);
    const int x0 = static_cast<int>(0.25 * hsv.cols);
    const int x1 = static_cast<int>(0.75 * hsv.cols);
    cv::Scalar mean_value = cv::mean(hsv(cv::Rect(x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0))));
    return mean_value;
  }

  cv::Mat obstacleMask(const cv::Mat& bgr) const {
    cv::Mat hsv;
    cv::Mat gray;
    cv::cvtColor(bgr, hsv, cv::COLOR_BGR2HSV);
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    const cv::Scalar floor = floorModel(hsv);

    cv::Mat mask(bgr.rows, bgr.cols, CV_8UC1, cv::Scalar(0));
    cv::Mat edges;
    cv::Canny(gray, edges, edge_thresh_, edge_thresh_ * 2);
    for (int y = 0; y < hsv.rows; ++y) {
      for (int x = 0; x < hsv.cols; ++x) {
        const cv::Vec3b p = hsv.at<cv::Vec3b>(y, x);
        double dh = std::abs(static_cast<double>(p[0]) - floor[0]);
        dh = std::min(dh, 180.0 - dh) * 1.4;
        const double ds = std::abs(static_cast<double>(p[1]) - floor[1]) * 0.35;
        const double dv = std::abs(static_cast<double>(p[2]) - floor[2]) * 0.55;
        const bool non_floor = (dh + ds + dv) > hsv_floor_dist_thresh_;
        const bool dark = p[2] < dark_v_thresh_;
        const bool saturated = p[1] > min_saturation_;
        if (non_floor && (dark || saturated || edges.at<unsigned char>(y, x) > 0)) {
          mask.at<unsigned char>(y, x) = 255;
        }
      }
    }
    cv::morphologyEx(mask, mask, cv::MORPH_OPEN, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(3, 3)));
    cv::morphologyEx(mask, mask, cv::MORPH_CLOSE, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(7, 7)));
    return mask;
  }

  cv::Mat cleanMask(const cv::Mat& roi) const {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(roi, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    cv::Mat cleaned = cv::Mat::zeros(roi.size(), CV_8UC1);
    for (const auto& contour : contours) {
      if (cv::contourArea(contour) >= min_obstacle_area_) {
        cv::drawContours(cleaned, std::vector<std::vector<cv::Point>>{contour}, -1, cv::Scalar(255), cv::FILLED);
      }
    }
    return cleaned;
  }

  std::array<double, 3> sectorRatios(const cv::Mat& cleaned) const {
    const int part_w = std::max(1, cleaned.cols / 3);
    std::array<double, 3> ratios {0.0, 0.0, 0.0};
    for (int index = 0; index < 3; ++index) {
      const int x0 = index * part_w;
      const int x1 = (index == 2) ? cleaned.cols : std::min(cleaned.cols, x0 + part_w);
      const cv::Mat part = cleaned(cv::Rect(x0, 0, std::max(1, x1 - x0), cleaned.rows));
      ratios[index] = static_cast<double>(cv::countNonZero(part)) / std::max(1, part.rows * part.cols);
    }
    return ratios;
  }

  geometry_msgs::Twist decide(double left, double center, double right) {
    geometry_msgs::Twist twist;
    double stop_threshold = center_stop_ratio_;
    if (last_blocked_) {
      stop_threshold = std::max(0.01, stop_threshold - clear_hysteresis_);
    }
    const bool blocked = center >= stop_threshold;
    if (blocked) {
      twist.angular.z = (left < right) ? turn_speed_ : -turn_speed_;
    } else if (left >= side_slow_ratio_ || right >= side_slow_ratio_) {
      twist.linear.x = slow_speed_;
      twist.angular.z = -clampValue((left - right) * side_steer_gain_, -turn_speed_, turn_speed_);
    } else {
      twist.linear.x = forward_speed_;
    }
    last_blocked_ = blocked;
    return twist;
  }

  void publishDebug(const std_msgs::Header& header, cv::Mat& bgr, const cv::Rect& roi_box, const cv::Mat& cleaned) {
    cv::rectangle(bgr, roi_box, cv::Scalar(0, 255, 255), 2);
    cv_bridge::CvImage out(header, "bgr8", bgr);
    debug_pub_.publish(out.toImageMsg());
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher twist_pub_;
  ros::Publisher debug_pub_;
  ros::Subscriber image_sub_;
  ros::Timer timer_;
  ros::Time last_image_time_;
  std::string image_topic_;
  std::string cmd_topic_;
  std::string debug_topic_;
  bool publish_debug_ = false;
  double forward_speed_ = 0.08;
  double slow_speed_ = 0.04;
  double turn_speed_ = 0.45;
  double side_steer_gain_ = 0.35;
  bool stop_on_no_image_ = true;
  double no_image_timeout_ = 1.0;
  double floor_sample_y0_ = 0.82;
  double floor_sample_y1_ = 0.96;
  double roi_y0_ = 0.38;
  double roi_y1_ = 0.88;
  double roi_x0_ = 0.12;
  double roi_x1_ = 0.88;
  double hsv_floor_dist_thresh_ = 42.0;
  int edge_thresh_ = 70;
  int dark_v_thresh_ = 55;
  int min_saturation_ = 35;
  double center_stop_ratio_ = 0.18;
  double side_slow_ratio_ = 0.24;
  double clear_hysteresis_ = 0.04;
  int min_obstacle_area_ = 600;
  bool last_blocked_ = false;
};

}  // namespace simple_robotic_arm

int main(int argc, char** argv) {
  ros::init(argc, argv, "opencv_obstacle_avoidance");
  simple_robotic_arm::OpenCVObstacleAvoidanceNode node;
  ros::spin();
  return 0;
}
