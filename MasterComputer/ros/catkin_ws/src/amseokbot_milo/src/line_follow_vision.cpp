/*
|--------------------------------------------------------------------------
| OpenCV 巡线视觉节点
|--------------------------------------------------------------------------
| 负责从 ROS 图像中提取白色线条位置，发布 /cmd_vel；可选发布调试图像。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <geometry_msgs/Twist.h>
#include <opencv2/imgproc.hpp>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>

namespace amseokbot_milo {

double clampValue(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

/*
|--------------------------------------------------------------------------
| 巡线节点
|--------------------------------------------------------------------------
| 订阅摄像头图像，识别下方 ROI 中的白色线条，并转换为线速度和角速度。
|--------------------------------------------------------------------------
*/
class LineFollowVisionNode {
 public:
  LineFollowVisionNode() : private_nh_("~") {
    private_nh_.param<std::string>("image_topic", image_topic_, "/usb_cam/image_raw");
    private_nh_.param<std::string>("cmd_vel_topic", cmd_topic_, "/cmd_vel");
    private_nh_.param<std::string>("debug_image_topic", debug_topic_, "debug");
    private_nh_.param("publish_debug_image", publish_debug_, false);
    private_nh_.param("line_roi_height_ratio", line_roi_height_ratio_, 0.35);
    private_nh_.param("blur_ksize", blur_ksize_, 5);
    private_nh_.param("white_thresh", white_thresh_, 200);
    private_nh_.param("min_line_area", min_line_area_, 500.0);
    private_nh_.param("v_max", v_max_, 0.12);
    private_nh_.param("k_angular", k_angular_, 1.8);
    private_nh_.param("obstacle_enable", obstacle_enable_, false);
    private_nh_.param("obs_roi_height_ratio", obs_roi_height_ratio_, 0.30);
    private_nh_.param("obs_dark_thresh", obs_dark_thresh_, 80);
    private_nh_.param("obs_dark_ratio", obs_dark_ratio_, 0.35);

    twist_pub_ = nh_.advertise<geometry_msgs::Twist>(cmd_topic_, 1);
    if (publish_debug_) {
      debug_pub_ = nh_.advertise<sensor_msgs::Image>(debug_topic_, 1);
    }
    image_sub_ = nh_.subscribe(image_topic_, 1, &LineFollowVisionNode::handleImage, this);
    ROS_INFO("line_follow_vision_cpp: image=%s cmd=%s debug=%s", image_topic_.c_str(), cmd_topic_.c_str(), publish_debug_ ? "true" : "false");
  }

 private:
  void handleImage(const sensor_msgs::Image::ConstPtr& msg) {
    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(msg, "bgr8");
    } catch (const cv_bridge::Exception& ex) {
      ROS_WARN("line_follow_vision cv_bridge: %s", ex.what());
      return;
    }

    cv::Mat bgr = cv_ptr->image.clone();
    cv::Mat gray;
    cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
    if (blur_ksize_ >= 3 && blur_ksize_ % 2 == 1) {
      cv::GaussianBlur(gray, gray, cv::Size(blur_ksize_, blur_ksize_), 0);
    }

    const int width = gray.cols;
    const int height = gray.rows;
    geometry_msgs::Twist twist;
    const bool obstacle = detectDarkObstacle(gray);
    const int line_y0 = static_cast<int>(height * (1.0 - line_roi_height_ratio_));
    cv::Mat roi = gray(cv::Range(line_y0, height), cv::Range::all());
    cv::Mat mask;
    cv::threshold(roi, mask, white_thresh_, 255, cv::THRESH_BINARY);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(mask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    bool found = false;
    double cx_img = width * 0.5;
    double best_area = 0.0;
    std::vector<cv::Point> best_contour;
    for (const auto& contour : contours) {
      const double area = cv::contourArea(contour);
      if (area > best_area) {
        best_area = area;
        best_contour = contour;
      }
    }
    if (best_area >= min_line_area_) {
      const cv::Moments moments = cv::moments(best_contour);
      if (moments.m00 > 1e-6) {
        cx_img = moments.m10 / moments.m00;
        found = true;
      }
    }

    const double error = clampValue((cx_img - width * 0.5) / std::max(width * 0.5, 1.0), -1.0, 1.0);
    if (obstacle) {
      twist.linear.x = 0.0;
      twist.angular.z = 0.0;
    } else if (found) {
      twist.linear.x = v_max_;
      twist.angular.z = clampValue(-k_angular_ * error, -1.5, 1.5);
    } else {
      ROS_WARN_THROTTLE(3.0, "line_follow_vision_cpp: no line contour in ROI");
    }
    twist_pub_.publish(twist);

    if (publish_debug_) {
      publishDebug(msg->header, bgr, line_y0, obstacle, found, cx_img);
    }
  }

  bool detectDarkObstacle(const cv::Mat& gray) const {
    if (!obstacle_enable_) {
      return false;
    }
    const int obs_h = std::max(1, static_cast<int>(gray.rows * obs_roi_height_ratio_));
    cv::Mat roi = gray(cv::Range(0, obs_h), cv::Range::all());
    cv::Mat dark = roi < obs_dark_thresh_;
    const double ratio = static_cast<double>(cv::countNonZero(dark)) / std::max(1, dark.rows * dark.cols);
    if (ratio >= obs_dark_ratio_) {
      ROS_WARN_THROTTLE(2.0, "line_follow_vision_cpp: dark obstacle ratio=%.2f", ratio);
      return true;
    }
    return false;
  }

  void publishDebug(const std_msgs::Header& header, cv::Mat& bgr, int line_y0, bool obstacle, bool found, double cx) {
    cv::rectangle(bgr, cv::Point(0, line_y0), cv::Point(bgr.cols - 1, bgr.rows - 1), cv::Scalar(255, 128, 0), 1);
    if (obstacle) {
      cv::rectangle(bgr, cv::Point(0, 0), cv::Point(bgr.cols - 1, static_cast<int>(bgr.rows * obs_roi_height_ratio_)), cv::Scalar(0, 0, 255), 1);
    }
    if (found) {
      cv::circle(bgr, cv::Point(static_cast<int>(cx), line_y0 + 20), 8, cv::Scalar(0, 255, 0), 2);
    }
    cv_bridge::CvImage out(header, "bgr8", bgr);
    debug_pub_.publish(out.toImageMsg());
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher twist_pub_;
  ros::Publisher debug_pub_;
  ros::Subscriber image_sub_;
  std::string image_topic_;
  std::string cmd_topic_;
  std::string debug_topic_;
  bool publish_debug_ = false;
  double line_roi_height_ratio_ = 0.35;
  int blur_ksize_ = 5;
  int white_thresh_ = 200;
  double min_line_area_ = 500.0;
  double v_max_ = 0.12;
  double k_angular_ = 1.8;
  bool obstacle_enable_ = false;
  double obs_roi_height_ratio_ = 0.30;
  int obs_dark_thresh_ = 80;
  double obs_dark_ratio_ = 0.35;
};

}  // namespace amseokbot_milo

int main(int argc, char** argv) {
  ros::init(argc, argv, "line_follow_vision");
  amseokbot_milo::LineFollowVisionNode node;
  ros::spin();
  return 0;
}
