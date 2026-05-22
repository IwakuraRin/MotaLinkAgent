/*
|--------------------------------------------------------------------------
| 单目摄像头 + 超声波局部建图节点
|--------------------------------------------------------------------------
| 使用单个摄像头估计前方障碍物方位，用 HC-SR04 距离确定深度，生成机器人前方局部栅格图。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <nav_msgs/OccupancyGrid.h>
#include <opencv2/imgproc.hpp>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>
#include <std_msgs/UInt16.h>

namespace amseokbot_milo {

/*
|--------------------------------------------------------------------------
| 建图参数
|--------------------------------------------------------------------------
| 保存局部地图尺寸、摄像头视场角、ROI 和距离有效范围。
|--------------------------------------------------------------------------
*/
struct MapperConfig {
  double resolution_m = 0.05;
  double forward_length_m = 3.0;
  double side_width_m = 2.4;
  double camera_fov_deg = 62.0;
  double roi_y0 = 0.45;
  double roi_y1 = 0.92;
  uint16_t min_range_mm = 60;
  uint16_t max_range_mm = 2500;
  double range_timeout_s = 0.5;
  double publish_rate_hz = 5.0;
  int occupied_value = 100;
  int free_value = 0;
  int unknown_value = -1;
};

/*
|--------------------------------------------------------------------------
| 单目方位估计工具
|--------------------------------------------------------------------------
| 在图像下半部分寻找边缘重心，把像素横向偏移换算成相机视场角内的障碍物方位。
|--------------------------------------------------------------------------
*/
double clampValue(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

double degToRad(double deg) {
  return deg * M_PI / 180.0;
}

class CameraBearingEstimator {
 public:
  explicit CameraBearingEstimator(const MapperConfig& config) : config_(config) {}

  double estimateBearingRad(const cv::Mat& bgr) const {
    if (bgr.empty()) {
      return 0.0;
    }

    const int y0 = static_cast<int>(clampValue(config_.roi_y0, 0.0, 1.0) * bgr.rows);
    const int y1 = static_cast<int>(clampValue(config_.roi_y1, 0.0, 1.0) * bgr.rows);
    const cv::Rect roi(0, std::max(0, y0), bgr.cols, std::max(1, y1 - y0));
    cv::Mat gray;
    cv::Mat edges;
    cv::cvtColor(bgr(roi), gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(5, 5), 0.0);
    cv::Canny(gray, edges, 60, 140);

    const cv::Moments moments = cv::moments(edges, true);
    if (moments.m00 < 20.0) {
      return 0.0;
    }

    const double cx = moments.m10 / moments.m00;
    const double normalized = (cx - static_cast<double>(edges.cols) * 0.5) / (static_cast<double>(edges.cols) * 0.5);
    return -clampValue(normalized, -1.0, 1.0) * degToRad(config_.camera_fov_deg) * 0.5;
  }

 private:
  MapperConfig config_;
};

/*
|--------------------------------------------------------------------------
| 局部栅格地图
|--------------------------------------------------------------------------
| 以机器人为原点，x 向前、y 向左；沿超声波射线清空自由区，在测距端点标记占用。
|--------------------------------------------------------------------------
*/
class LocalOccupancyMap {
 public:
  explicit LocalOccupancyMap(const MapperConfig& config) : config_(config) {
    width_ = std::max(1, static_cast<int>(std::round(config_.forward_length_m / config_.resolution_m)));
    height_ = std::max(1, static_cast<int>(std::round(config_.side_width_m / config_.resolution_m)));
    data_.assign(static_cast<std::size_t>(width_ * height_), static_cast<int8_t>(config_.unknown_value));
  }

  void integrate(double range_m, double bearing_rad) {
    const double hit_x = range_m * std::cos(bearing_rad);
    const double hit_y = range_m * std::sin(bearing_rad);
    markRay(hit_x, hit_y);
    markCell(hit_x, hit_y, static_cast<int8_t>(config_.occupied_value));
  }

  nav_msgs::OccupancyGrid toMessage(const ros::Time& stamp, const std::string& frame_id) const {
    nav_msgs::OccupancyGrid grid;
    grid.header.stamp = stamp;
    grid.header.frame_id = frame_id;
    grid.info.resolution = static_cast<float>(config_.resolution_m);
    grid.info.width = static_cast<uint32_t>(width_);
    grid.info.height = static_cast<uint32_t>(height_);
    grid.info.origin.position.x = 0.0;
    grid.info.origin.position.y = -config_.side_width_m * 0.5;
    grid.info.origin.position.z = 0.0;
    grid.info.origin.orientation.w = 1.0;
    grid.data = data_;
    return grid;
  }

 private:
  bool worldToCell(double x, double y, int& ix, int& iy) const {
    ix = static_cast<int>(std::floor(x / config_.resolution_m));
    iy = static_cast<int>(std::floor((y + config_.side_width_m * 0.5) / config_.resolution_m));
    return ix >= 0 && ix < width_ && iy >= 0 && iy < height_;
  }

  void markCell(double x, double y, int8_t value) {
    int ix = 0;
    int iy = 0;
    if (!worldToCell(x, y, ix, iy)) {
      return;
    }
    data_[static_cast<std::size_t>(iy * width_ + ix)] = value;
  }

  void markRay(double hit_x, double hit_y) {
    const double distance = std::sqrt(hit_x * hit_x + hit_y * hit_y);
    const int steps = std::max(1, static_cast<int>(distance / config_.resolution_m));
    for (int step = 0; step < steps; ++step) {
      const double t = static_cast<double>(step) / static_cast<double>(steps);
      markCell(hit_x * t, hit_y * t, static_cast<int8_t>(config_.free_value));
    }
  }

  MapperConfig config_;
  int width_ = 0;
  int height_ = 0;
  std::vector<int8_t> data_;
};

/*
|--------------------------------------------------------------------------
| 单目超声波建图节点
|--------------------------------------------------------------------------
| 订阅摄像头图像和下位机超声波距离，发布 /mapping/local_occupancy 局部栅格图。
|--------------------------------------------------------------------------
*/
class SingleCameraUltrasonicMapperNode {
 public:
  SingleCameraUltrasonicMapperNode()
      : private_nh_("~"), config_(loadConfig()), bearing_estimator_(config_), local_map_(config_) {
    private_nh_.param<std::string>("image_topic", image_topic_, "/usb_cam/image_raw");
    private_nh_.param<std::string>("front_range_topic", front_range_topic_, "/safety/front_range_mm");
    private_nh_.param<std::string>("map_topic", map_topic_, "/mapping/local_occupancy");
    private_nh_.param<std::string>("map_frame", map_frame_, "base_link");

    map_pub_ = nh_.advertise<nav_msgs::OccupancyGrid>(map_topic_, 1, true);
    image_sub_ = nh_.subscribe(image_topic_, 1, &SingleCameraUltrasonicMapperNode::handleImage, this);
    range_sub_ = nh_.subscribe(front_range_topic_, 10, &SingleCameraUltrasonicMapperNode::handleRange, this);
    timer_ = nh_.createTimer(ros::Duration(1.0 / std::max(1.0, config_.publish_rate_hz)), &SingleCameraUltrasonicMapperNode::handleTimer, this);

    ROS_INFO("single_camera_ultrasonic_mapper: image=%s range=%s map=%s", image_topic_.c_str(), front_range_topic_.c_str(), map_topic_.c_str());
  }

 private:
  MapperConfig loadConfig() {
    MapperConfig config;
    private_nh_.param("resolution_m", config.resolution_m, config.resolution_m);
    private_nh_.param("forward_length_m", config.forward_length_m, config.forward_length_m);
    private_nh_.param("side_width_m", config.side_width_m, config.side_width_m);
    private_nh_.param("camera_fov_deg", config.camera_fov_deg, config.camera_fov_deg);
    private_nh_.param("roi_y0", config.roi_y0, config.roi_y0);
    private_nh_.param("roi_y1", config.roi_y1, config.roi_y1);
    int min_range = config.min_range_mm;
    int max_range = config.max_range_mm;
    private_nh_.param("min_range_mm", min_range, min_range);
    private_nh_.param("max_range_mm", max_range, max_range);
    config.min_range_mm = static_cast<uint16_t>(std::max(0, std::min(65535, min_range)));
    config.max_range_mm = static_cast<uint16_t>(std::max(0, std::min(65535, max_range)));
    private_nh_.param("range_timeout_s", config.range_timeout_s, config.range_timeout_s);
    private_nh_.param("publish_rate_hz", config.publish_rate_hz, config.publish_rate_hz);
    return config;
  }

  void handleRange(const std_msgs::UInt16::ConstPtr& msg) {
    last_range_mm_ = msg->data;
    last_range_time_ = ros::Time::now();
  }

  void handleImage(const sensor_msgs::Image::ConstPtr& msg) {
    if (!rangeFresh() || last_range_mm_ < config_.min_range_mm || last_range_mm_ > config_.max_range_mm) {
      return;
    }

    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(msg, "bgr8");
    } catch (const cv_bridge::Exception& ex) {
      ROS_WARN("single_camera_ultrasonic_mapper cv_bridge: %s", ex.what());
      return;
    }

    const double bearing = bearing_estimator_.estimateBearingRad(cv_ptr->image);
    const double range_m = static_cast<double>(last_range_mm_) / 1000.0;
    local_map_.integrate(range_m, bearing);
    last_map_update_ = ros::Time::now();
  }

  void handleTimer(const ros::TimerEvent&) {
    if (last_map_update_.isZero()) {
      return;
    }
    map_pub_.publish(local_map_.toMessage(ros::Time::now(), map_frame_));
  }

  bool rangeFresh() const {
    return !last_range_time_.isZero() && (ros::Time::now() - last_range_time_).toSec() <= config_.range_timeout_s;
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher map_pub_;
  ros::Subscriber image_sub_;
  ros::Subscriber range_sub_;
  ros::Timer timer_;
  MapperConfig config_;
  CameraBearingEstimator bearing_estimator_;
  LocalOccupancyMap local_map_;
  std::string image_topic_;
  std::string front_range_topic_;
  std::string map_topic_;
  std::string map_frame_;
  uint16_t last_range_mm_ = 0;
  ros::Time last_range_time_;
  ros::Time last_map_update_;
};

}  // namespace amseokbot_milo

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 初始化单目摄像头 + 超声波建图节点。
|--------------------------------------------------------------------------
*/
int main(int argc, char** argv) {
  ros::init(argc, argv, "single_camera_ultrasonic_mapper");
  amseokbot_milo::SingleCameraUltrasonicMapperNode node;
  ros::spin();
  return 0;
}
