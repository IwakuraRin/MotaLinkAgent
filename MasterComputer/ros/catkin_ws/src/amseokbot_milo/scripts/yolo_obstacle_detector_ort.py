#!/usr/bin/env python3
# 作用：使用 ONNX Runtime 运行 YOLO 模型，发布带检测框的 ROS 调试图像。

import time
from typing import List, Tuple

import cv2
import numpy as np
import onnxruntime as ort
import rospy
from cv_bridge import CvBridge, CvBridgeError
from rospkg import RosPack
from sensor_msgs.msg import Image


# ==================== 图像预处理 ====================
# 作用：等比例缩放到方形输入，记录缩放比例与填充量，用于还原检测框。
# ====================================================
def letterbox(bgr: np.ndarray, size: int) -> Tuple[np.ndarray, float, int, int]:
    h, w = bgr.shape[:2]
    scale = min(float(size) / float(w), float(size) / float(h))
    new_w = int(round(w * scale))
    new_h = int(round(h * scale))
    resized = cv2.resize(bgr, (new_w, new_h), interpolation=cv2.INTER_AREA)
    canvas = np.full((size, size, 3), 114, dtype=np.uint8)
    pad_x = (size - new_w) // 2
    pad_y = (size - new_h) // 2
    canvas[pad_y : pad_y + new_h, pad_x : pad_x + new_w] = resized
    return canvas, scale, pad_x, pad_y


# ==================== 检测节点 ====================
# 作用：订阅摄像头图像，调用 ONNX Runtime 推理，并把检测框画到调试图。
# ==================================================
class YoloObstacleDetectorOrt:
    def __init__(self) -> None:
        package_path = RosPack().get_path('amseokbot_milo')
        self.image_topic = rospy.get_param('~image_topic', '/usb_cam/image_raw')
        self.debug_topic = rospy.get_param('~debug_image_topic', '/obstacle_detector/debug')
        self.model_path = rospy.get_param('~model_path', package_path + '/models/obstacle_yolo11n_192.onnx')
        self.input_size = int(rospy.get_param('~input_size', 192))
        self.conf_thresh = float(rospy.get_param('~conf_thresh', 0.05))
        self.nms_thresh = float(rospy.get_param('~nms_thresh', 0.45))
        self.max_det = int(rospy.get_param('~max_det', 30))
        self.output_width = int(rospy.get_param('~output_width', 320))
        self.process_every_n = max(1, int(rospy.get_param('~process_every_n', 1)))
        self.draw_fps = bool(rospy.get_param('~draw_fps', True))
        self.bridge = CvBridge()
        self.frame_index = 0
        self.last_fps = 0.0
        self.model_error = ''
        self.session = None
        self.input_name = ''
        self.output_names: List[str] = []
        self.load_model()
        self.debug_pub = rospy.Publisher(self.debug_topic, Image, queue_size=1)
        self.image_sub = rospy.Subscriber(self.image_topic, Image, self.handle_image, queue_size=1)
        rospy.loginfo('yolo_obstacle_detector_ort: image=%s debug=%s model=%s', self.image_topic, self.debug_topic, self.model_path)

    def load_model(self) -> None:
        try:
            self.session = ort.InferenceSession(self.model_path, providers=['CPUExecutionProvider'])
            self.input_name = self.session.get_inputs()[0].name
            self.output_names = [item.name for item in self.session.get_outputs()]
            self.model_error = ''
        except Exception as exc:  # noqa: BLE001 - ROS 日志需要保留原始异常文本。
            self.session = None
            self.model_error = str(exc)
            rospy.logerr('yolo_obstacle_detector_ort model load failed: %s', self.model_error)

    def handle_image(self, msg: Image) -> None:
        self.frame_index += 1
        if self.frame_index % self.process_every_n != 0:
            return
        try:
            bgr = self.bridge.imgmsg_to_cv2(msg, 'bgr8')
        except CvBridgeError as exc:
            rospy.logwarn('yolo_obstacle_detector_ort cv_bridge: %s', exc)
            return
        bgr = bgr.copy()
        if self.output_width > 0 and bgr.shape[1] > self.output_width:
            out_h = int(round(float(bgr.shape[0]) * float(self.output_width) / float(bgr.shape[1])))
            bgr = cv2.resize(bgr, (self.output_width, out_h), interpolation=cv2.INTER_AREA)
        begin = time.monotonic()
        boxes = []
        if self.session is not None:
            boxes = self.infer(bgr)
        elapsed = time.monotonic() - begin
        self.last_fps = 1.0 / elapsed if elapsed > 1e-6 else 0.0
        self.publish_debug(msg, bgr, boxes)

    def infer(self, bgr: np.ndarray) -> List[Tuple[int, int, int, int, float]]:
        padded, scale, pad_x, pad_y = letterbox(bgr, self.input_size)
        rgb = cv2.cvtColor(padded, cv2.COLOR_BGR2RGB)
        blob = np.expand_dims(np.transpose(rgb, (2, 0, 1)), axis=0).astype(np.float32) / 255.0
        output = self.session.run(self.output_names[:1], {self.input_name: blob})[0]
        return self.postprocess(output, bgr.shape[1], bgr.shape[0], scale, pad_x, pad_y)

    def postprocess(self, output: np.ndarray, image_w: int, image_h: int, scale: float, pad_x: int, pad_y: int) -> List[Tuple[int, int, int, int, float]]:
        rows = np.squeeze(output)
        if rows.ndim != 2:
            return []
        if rows.shape[0] < rows.shape[1]:
            rows = rows.T
        candidate_boxes = []
        scores = []
        for row in rows:
            if row.shape[0] < 5:
                continue
            conf = float(row[4]) if row.shape[0] == 5 else float(np.max(row[4:]))
            if conf < self.conf_thresh:
                continue
            cx, cy, bw, bh = [float(v) for v in row[:4]]
            x1 = int(round((cx - bw * 0.5 - pad_x) / scale))
            y1 = int(round((cy - bh * 0.5 - pad_y) / scale))
            x2 = int(round((cx + bw * 0.5 - pad_x) / scale))
            y2 = int(round((cy + bh * 0.5 - pad_y) / scale))
            x1 = max(0, min(x1, image_w - 1))
            y1 = max(0, min(y1, image_h - 1))
            x2 = max(0, min(x2, image_w - 1))
            y2 = max(0, min(y2, image_h - 1))
            if x2 <= x1 or y2 <= y1:
                continue
            candidate_boxes.append([x1, y1, x2 - x1, y2 - y1])
            scores.append(conf)
        keep = cv2.dnn.NMSBoxes(candidate_boxes, scores, self.conf_thresh, self.nms_thresh)
        if len(keep) == 0:
            return []
        flat_keep = np.array(keep).reshape(-1).tolist()
        detections = []
        for index in flat_keep[: self.max_det]:
            x, y, w, h = candidate_boxes[index]
            detections.append((x, y, w, h, scores[index]))
        return detections

    def publish_debug(self, msg: Image, bgr: np.ndarray, boxes: List[Tuple[int, int, int, int, float]]) -> None:
        for x, y, w, h, conf in boxes:
            cv2.rectangle(bgr, (x, y), (x + w, y + h), (0, 255, 0), 2)
            cv2.putText(bgr, 'obstacle %.2f' % conf, (x, max(16, y - 6)), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 2)
        status = 'obstacles: %d' % len(boxes)
        if self.draw_fps:
            status += ' infer: %.1f fps' % self.last_fps
        if self.session is None:
            status = 'YOLO model unavailable'
            cv2.putText(bgr, 'model error: onnxruntime unavailable', (10, 54), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 0, 255), 2)
        cv2.putText(bgr, status, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        out = self.bridge.cv2_to_imgmsg(bgr, 'bgr8')
        out.header = msg.header
        self.debug_pub.publish(out)


# ==================== 程序入口 ====================
# 作用：初始化 ROS 节点并持续处理摄像头图像。
# ================================================
def main() -> None:
    rospy.init_node('yolo_obstacle_detector')
    YoloObstacleDetectorOrt()
    rospy.spin()


if __name__ == '__main__':
    main()
