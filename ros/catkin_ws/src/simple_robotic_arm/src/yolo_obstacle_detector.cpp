/*
|--------------------------------------------------------------------------
| YOLO ONNX 障碍物检测节点
|--------------------------------------------------------------------------
| 使用 OpenCV DNN 在 C++ 中加载 YOLO ONNX 模型，将检测框绘制到 ROS 调试图像。
|--------------------------------------------------------------------------
*/

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <vector>

#include <cv_bridge/cv_bridge.h>
#include <opencv2/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <ros/package.h>
#include <ros/ros.h>
#include <sensor_msgs/Image.h>

namespace simple_robotic_arm {

/*
|--------------------------------------------------------------------------
| 检测框结构
|--------------------------------------------------------------------------
| 保存 YOLO 后处理后的矩形坐标和置信度。
|--------------------------------------------------------------------------
*/
struct DetectionBox {
  cv::Rect box;
  float confidence = 0.0F;
};

/*
|--------------------------------------------------------------------------
| 图像预处理工具
|--------------------------------------------------------------------------
| 将任意尺寸图像等比例缩放到方形输入，记录缩放比例和填充量供还原坐标。
|--------------------------------------------------------------------------
*/
cv::Mat letterbox(const cv::Mat& bgr, int size, double& scale, int& pad_x, int& pad_y) {
  scale = std::min(static_cast<double>(size) / bgr.cols, static_cast<double>(size) / bgr.rows);
  const int new_w = static_cast<int>(std::round(bgr.cols * scale));
  const int new_h = static_cast<int>(std::round(bgr.rows * scale));
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(new_w, new_h));
  cv::Mat canvas(size, size, CV_8UC3, cv::Scalar(114, 114, 114));
  pad_x = (size - new_w) / 2;
  pad_y = (size - new_h) / 2;
  resized.copyTo(canvas(cv::Rect(pad_x, pad_y, new_w, new_h)));
  return canvas;
}

/*
|--------------------------------------------------------------------------
| YOLO 检测节点
|--------------------------------------------------------------------------
| 订阅摄像头图像，按间隔执行 ONNX 推理，发布带框调试图。
|--------------------------------------------------------------------------
*/
class YoloObstacleDetectorNode {
 public:
  YoloObstacleDetectorNode() : private_nh_("~") {
    loadParams();
    net_ = cv::dnn::readNetFromONNX(model_path_);
    net_.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
    net_.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    debug_pub_ = nh_.advertise<sensor_msgs::Image>(debug_topic_, 1);
    image_sub_ = nh_.subscribe(image_topic_, 1, &YoloObstacleDetectorNode::handleImage, this);
    ROS_INFO("yolo_obstacle_detector_cpp: image=%s debug=%s model=%s", image_topic_.c_str(), debug_topic_.c_str(), model_path_.c_str());
  }

 private:
  void loadParams() {
    const std::string package_path = ros::package::getPath("simple_robotic_arm");
    const std::string default_model = package_path + "/models/obstacle_yolo11n.onnx";
    private_nh_.param<std::string>("image_topic", image_topic_, "/usb_cam/image_raw");
    private_nh_.param<std::string>("debug_image_topic", debug_topic_, "/obstacle_detector/debug");
    private_nh_.param<std::string>("model_path", model_path_, default_model);
    private_nh_.param("input_size", input_size_, 512);
    private_nh_.param("conf_thresh", conf_thresh_, 0.05);
    private_nh_.param("nms_thresh", nms_thresh_, 0.45);
    private_nh_.param("max_det", max_det_, 30);
    private_nh_.param("output_width", output_width_, 320);
    private_nh_.param("process_every_n", process_every_n_, 1);
    private_nh_.param("draw_fps", draw_fps_, true);
    process_every_n_ = std::max(1, process_every_n_);
  }

  void handleImage(const sensor_msgs::Image::ConstPtr& msg) {
    frame_index_ += 1;
    if ((frame_index_ % process_every_n_) != 0) {
      return;
    }

    cv_bridge::CvImageConstPtr cv_ptr;
    try {
      cv_ptr = cv_bridge::toCvShare(msg, "bgr8");
    } catch (const cv_bridge::Exception& ex) {
      ROS_WARN("yolo_obstacle_detector cv_bridge: %s", ex.what());
      return;
    }

    cv::Mat bgr = cv_ptr->image.clone();
    if (output_width_ > 0 && bgr.cols > output_width_) {
      const int out_h = static_cast<int>(std::round(static_cast<double>(bgr.rows) * output_width_ / bgr.cols));
      cv::resize(bgr, bgr, cv::Size(output_width_, out_h), 0, 0, cv::INTER_AREA);
    }

    const auto begin = std::chrono::steady_clock::now();
    std::vector<DetectionBox> boxes = infer(bgr);
    const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    last_fps_ = elapsed > 1e-6 ? 1.0 / elapsed : 0.0;
    publishDebug(msg->header, bgr, boxes);
  }

  std::vector<DetectionBox> infer(const cv::Mat& bgr) {
    double scale = 1.0;
    int pad_x = 0;
    int pad_y = 0;
    cv::Mat padded = letterbox(bgr, input_size_, scale, pad_x, pad_y);
    cv::Mat blob = cv::dnn::blobFromImage(padded, 1.0 / 255.0, cv::Size(input_size_, input_size_), cv::Scalar(), true, false);
    net_.setInput(blob);
    cv::Mat output = net_.forward();
    return postprocess(output, bgr.size(), scale, pad_x, pad_y);
  }

  std::vector<DetectionBox> postprocess(const cv::Mat& output, cv::Size image_size, double scale, int pad_x, int pad_y) const {
    cv::Mat rows = output;
    if (output.dims == 3) {
      const int rows_count = output.size[1];
      const int cols_count = output.size[2];
      rows = cv::Mat(rows_count, cols_count, CV_32F, const_cast<float*>(reinterpret_cast<const float*>(output.ptr<float>())));
      if (rows_count == 5 || rows_count < cols_count) {
        rows = rows.t();
      }
    }

    std::vector<cv::Rect> candidate_boxes;
    std::vector<float> scores;
    for (int i = 0; i < rows.rows; ++i) {
      const float* row = rows.ptr<float>(i);
      const float conf = row[4];
      if (conf < conf_thresh_) {
        continue;
      }
      const float cx = row[0];
      const float cy = row[1];
      const float bw = row[2];
      const float bh = row[3];
      int x1 = static_cast<int>(std::round((cx - bw * 0.5F - pad_x) / scale));
      int y1 = static_cast<int>(std::round((cy - bh * 0.5F - pad_y) / scale));
      int x2 = static_cast<int>(std::round((cx + bw * 0.5F - pad_x) / scale));
      int y2 = static_cast<int>(std::round((cy + bh * 0.5F - pad_y) / scale));
      x1 = std::max(0, std::min(x1, image_size.width - 1));
      y1 = std::max(0, std::min(y1, image_size.height - 1));
      x2 = std::max(0, std::min(x2, image_size.width - 1));
      y2 = std::max(0, std::min(y2, image_size.height - 1));
      if (x2 <= x1 || y2 <= y1) {
        continue;
      }
      candidate_boxes.emplace_back(cv::Rect(cv::Point(x1, y1), cv::Point(x2, y2)));
      scores.push_back(conf);
    }

    std::vector<int> keep;
    cv::dnn::NMSBoxes(candidate_boxes, scores, conf_thresh_, nms_thresh_, keep);
    std::vector<DetectionBox> detections;
    for (int index : keep) {
      if (static_cast<int>(detections.size()) >= max_det_) {
        break;
      }
      detections.push_back({candidate_boxes[index], scores[index]});
    }
    return detections;
  }

  void publishDebug(const std_msgs::Header& header, cv::Mat& bgr, const std::vector<DetectionBox>& boxes) {
    for (const auto& detection : boxes) {
      cv::rectangle(bgr, detection.box, cv::Scalar(0, 255, 0), 2);
      cv::putText(bgr, "obstacle " + std::to_string(detection.confidence).substr(0, 4), detection.box.tl() + cv::Point(0, -6), cv::FONT_HERSHEY_SIMPLEX, 0.55, cv::Scalar(0, 255, 0), 2);
    }
    std::string status = "obstacles: " + std::to_string(boxes.size());
    if (draw_fps_) {
      status += " infer: " + std::to_string(last_fps_).substr(0, 4) + " fps";
    }
    cv::putText(bgr, status, cv::Point(10, 26), cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 255), 2);
    cv_bridge::CvImage out(header, "bgr8", bgr);
    debug_pub_.publish(out.toImageMsg());
  }

  ros::NodeHandle nh_;
  ros::NodeHandle private_nh_;
  ros::Publisher debug_pub_;
  ros::Subscriber image_sub_;
  cv::dnn::Net net_;
  std::string image_topic_;
  std::string debug_topic_;
  std::string model_path_;
  int input_size_ = 512;
  double conf_thresh_ = 0.05;
  double nms_thresh_ = 0.45;
  int max_det_ = 30;
  int output_width_ = 320;
  int process_every_n_ = 1;
  bool draw_fps_ = true;
  int frame_index_ = 0;
  double last_fps_ = 0.0;
};

}  // namespace simple_robotic_arm

int main(int argc, char** argv) {
  ros::init(argc, argv, "yolo_obstacle_detector");
  simple_robotic_arm::YoloObstacleDetectorNode node;
  ros::spin();
  return 0;
}
