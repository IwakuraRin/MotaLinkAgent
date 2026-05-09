#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ROS Image -> YOLO ONNX obstacle boxes -> ROS debug Image.

The model is a one-class obstacle detector exported from Ultralytics YOLO.
Inference uses onnxruntime so the node works with ROS Noetic system Python and
old OpenCV 4.2; OpenCV is only used for resize, NMS, and drawing.
"""
from __future__ import print_function

import os
import sys
import time

import cv2
import numpy as np
import onnxruntime as ort
import rospy
from cv_bridge import CvBridge, CvBridgeError
from sensor_msgs.msg import Image


def letterbox(bgr, size):
    h, w = bgr.shape[:2]
    scale = min(float(size) / float(w), float(size) / float(h))
    nw, nh = int(round(w * scale)), int(round(h * scale))
    resized = cv2.resize(bgr, (nw, nh), interpolation=cv2.INTER_LINEAR)
    canvas = np.full((size, size, 3), 114, dtype=np.uint8)
    pad_x = (size - nw) // 2
    pad_y = (size - nh) // 2
    canvas[pad_y:pad_y + nh, pad_x:pad_x + nw] = resized
    return canvas, scale, pad_x, pad_y


def clamp(v, lo, hi):
    return max(lo, min(hi, v))


class YoloObstacleDetector(object):
    def __init__(self):
        self.bridge = CvBridge()
        pkg_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        default_model = os.path.join(pkg_dir, "models", "obstacle_yolo11n.onnx")

        self.image_topic = rospy.get_param("~image_topic", "/usb_cam/image_raw")
        self.debug_topic = rospy.get_param("~debug_image_topic", "/obstacle_detector/debug")
        self.model_path = rospy.get_param("~model_path", default_model)
        self.input_size = int(rospy.get_param("~input_size", 512))
        self.conf_thresh = float(rospy.get_param("~conf_thresh", 0.05))
        self.nms_thresh = float(rospy.get_param("~nms_thresh", 0.45))
        self.max_det = int(rospy.get_param("~max_det", 30))
        self.process_every_n = max(1, int(rospy.get_param("~process_every_n", 1)))
        self.output_width = int(rospy.get_param("~output_width", 320))
        self.draw_fps = bool(rospy.get_param("~draw_fps", True))

        if not os.path.exists(self.model_path):
            raise RuntimeError("model not found: %s" % self.model_path)

        self.session = ort.InferenceSession(self.model_path, providers=["CPUExecutionProvider"])
        self.input_name = self.session.get_inputs()[0].name
        self.pub = rospy.Publisher(self.debug_topic, Image, queue_size=1)
        self.sub = rospy.Subscriber(self.image_topic, Image, self.on_image, queue_size=1, buff_size=2 ** 24)
        self.frame_idx = 0
        self.last_boxes = []
        self.last_fps = 0.0
        rospy.loginfo("yolo_obstacle_detector: image=%s debug=%s model=%s conf=%.3f", self.image_topic, self.debug_topic, self.model_path, self.conf_thresh)

    def preprocess(self, bgr):
        padded, scale, pad_x, pad_y = letterbox(bgr, self.input_size)
        rgb = cv2.cvtColor(padded, cv2.COLOR_BGR2RGB)
        blob = rgb.astype(np.float32) / 255.0
        blob = np.transpose(blob, (2, 0, 1))[None, :, :, :]
        return blob, scale, pad_x, pad_y

    def postprocess(self, output, shape, scale, pad_x, pad_y):
        h, w = shape[:2]
        pred = output[0]
        if pred.shape[0] == 5:
            pred = pred.T
        boxes = []
        scores = []
        for row in pred:
            conf = float(row[4])
            if conf < self.conf_thresh:
                continue
            cx, cy, bw, bh = [float(x) for x in row[:4]]
            x1 = (cx - bw / 2.0 - pad_x) / scale
            y1 = (cy - bh / 2.0 - pad_y) / scale
            x2 = (cx + bw / 2.0 - pad_x) / scale
            y2 = (cy + bh / 2.0 - pad_y) / scale
            x1 = int(clamp(round(x1), 0, w - 1))
            y1 = int(clamp(round(y1), 0, h - 1))
            x2 = int(clamp(round(x2), 0, w - 1))
            y2 = int(clamp(round(y2), 0, h - 1))
            if x2 <= x1 or y2 <= y1:
                continue
            boxes.append([x1, y1, x2 - x1, y2 - y1])
            scores.append(conf)
        if not boxes:
            return []
        idxs = cv2.dnn.NMSBoxes(boxes, scores, self.conf_thresh, self.nms_thresh)
        if len(idxs) == 0:
            return []
        idxs = np.array(idxs).reshape(-1).tolist()[:self.max_det]
        out = []
        for i in idxs:
            x, y, bw, bh = boxes[i]
            out.append((x, y, x + bw, y + bh, scores[i]))
        return out

    def infer(self, bgr):
        blob, scale, pad_x, pad_y = self.preprocess(bgr)
        t0 = time.time()
        output = self.session.run(None, {self.input_name: blob})[0]
        dt = max(time.time() - t0, 1e-6)
        self.last_fps = 1.0 / dt
        return self.postprocess(output, bgr.shape, scale, pad_x, pad_y)

    def draw(self, bgr, boxes):
        out = bgr.copy()
        for x1, y1, x2, y2, conf in boxes:
            cv2.rectangle(out, (x1, y1), (x2, y2), (0, 255, 0), 2)
            label = "obstacle %.2f" % conf
            ty = max(20, y1 - 8)
            cv2.putText(out, label, (x1, ty), cv2.FONT_HERSHEY_SIMPLEX, 0.55, (0, 255, 0), 2, cv2.LINE_AA)
        status = "obstacles: %d" % len(boxes)
        if self.draw_fps:
            status += "  infer: %.1f fps" % self.last_fps
        cv2.putText(out, status, (10, 26), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2, cv2.LINE_AA)
        return out

    def on_image(self, msg):
        self.frame_idx += 1
        try:
            bgr = self.bridge.imgmsg_to_cv2(msg, desired_encoding="bgr8")
        except CvBridgeError as exc:
            rospy.logwarn("yolo_obstacle_detector cv_bridge: %s", exc)
            return

        if self.output_width > 0 and bgr.shape[1] > self.output_width:
            out_h = int(round(float(bgr.shape[0]) * float(self.output_width) / float(bgr.shape[1])))
            bgr = cv2.resize(bgr, (self.output_width, out_h), interpolation=cv2.INTER_AREA)

        if self.frame_idx % self.process_every_n != 0:
            return
        try:
            self.last_boxes = self.infer(bgr)
        except Exception as exc:
            rospy.logwarn_throttle(2.0, "yolo_obstacle_detector infer failed: %s", exc)
            self.last_boxes = []
        dbg = self.draw(bgr, self.last_boxes)
        try:
            out = self.bridge.cv2_to_imgmsg(dbg, encoding="bgr8")
            out.header = msg.header
            self.pub.publish(out)
        except CvBridgeError as exc:
            rospy.logwarn("yolo_obstacle_detector publish: %s", exc)


def main():
    rospy.init_node("yolo_obstacle_detector", anonymous=False)
    try:
        YoloObstacleDetector()
        rospy.spin()
    except rospy.ROSInterruptException:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
