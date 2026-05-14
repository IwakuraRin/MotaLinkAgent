#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Lightweight monocular obstacle avoidance for ROS 1 + OpenCV.

This is a heuristic, no-training node. It assumes the bottom strip of the image
mostly contains floor, then treats pixels that differ strongly from that local
floor model inside a forward ROI as possible obstacles. The ROI is split into
left/center/right sectors and the node publishes geometry_msgs/Twist:
  - center blocked: stop forward motion and turn toward the clearer side
  - side blocked only: keep moving slowly and bias away from the obstacle
  - clear: move forward

Limitations: monocular RGB does not provide reliable metric distance. For
safety-critical use, pair this with depth/LiDAR/bump sensors and a hardware E-stop.
"""
from __future__ import print_function

import sys

import cv2
import numpy as np
import rospy
from cv_bridge import CvBridge, CvBridgeError
from geometry_msgs.msg import Twist
from sensor_msgs.msg import Image


def clamp(value, low, high):
    return max(low, min(high, value))


class OpenCVObstacleAvoidance(object):
    def __init__(self):
        self.bridge = CvBridge()
        self.image_topic = rospy.get_param("~image_topic", "/usb_cam/image_raw")
        self.cmd_topic = rospy.get_param("~cmd_vel_topic", "/cmd_vel")
        self.debug_topic = rospy.get_param("~debug_image_topic", "~debug")
        self.publish_debug = bool(rospy.get_param("~publish_debug_image", False))

        # Motion. Keep defaults conservative because this is monocular vision only.
        self.forward_speed = float(rospy.get_param("~forward_speed", 0.08))
        self.slow_speed = float(rospy.get_param("~slow_speed", 0.04))
        self.turn_speed = float(rospy.get_param("~turn_speed", 0.45))
        self.side_steer_gain = float(rospy.get_param("~side_steer_gain", 0.35))
        self.stop_on_no_image = bool(rospy.get_param("~stop_on_no_image", True))
        self.no_image_timeout = float(rospy.get_param("~no_image_timeout", 1.0))

        # Regions are normalized image coordinates.
        self.floor_sample_y0 = float(rospy.get_param("~floor_sample_y0", 0.82))
        self.floor_sample_y1 = float(rospy.get_param("~floor_sample_y1", 0.96))
        self.roi_y0 = float(rospy.get_param("~roi_y0", 0.38))
        self.roi_y1 = float(rospy.get_param("~roi_y1", 0.88))
        self.roi_x0 = float(rospy.get_param("~roi_x0", 0.12))
        self.roi_x1 = float(rospy.get_param("~roi_x1", 0.88))

        # Mask thresholds. Tune these per camera/floor lighting.
        self.hsv_floor_dist_thresh = float(rospy.get_param("~hsv_floor_dist_thresh", 42.0))
        self.edge_thresh = int(rospy.get_param("~edge_thresh", 70))
        self.dark_v_thresh = int(rospy.get_param("~dark_v_thresh", 55))
        self.min_saturation = int(rospy.get_param("~min_saturation", 35))
        self.open_kernel = int(rospy.get_param("~open_kernel", 3))
        self.close_kernel = int(rospy.get_param("~close_kernel", 7))

        # Decision thresholds are ratios of obstacle pixels in each sector.
        self.center_stop_ratio = float(rospy.get_param("~center_stop_ratio", 0.18))
        self.side_slow_ratio = float(rospy.get_param("~side_slow_ratio", 0.24))
        self.clear_hysteresis = float(rospy.get_param("~clear_hysteresis", 0.04))
        self.min_obstacle_area = int(rospy.get_param("~min_obstacle_area", 600))

        self.last_image_time = rospy.Time(0)
        self.last_blocked = False

        self.twist_pub = rospy.Publisher(self.cmd_topic, Twist, queue_size=1)
        self.debug_pub = None
        if self.publish_debug:
            self.debug_pub = rospy.Publisher(self.debug_topic, Image, queue_size=1)

        self.sub = rospy.Subscriber(self.image_topic, Image, self.on_image, queue_size=1, buff_size=2 ** 24)
        self.timer = rospy.Timer(rospy.Duration(0.1), self.on_timer)
        rospy.on_shutdown(self.publish_stop)
        rospy.loginfo("opencv_obstacle_avoidance: image=%s cmd=%s debug=%s", self.image_topic, self.cmd_topic, self.publish_debug)

    def publish_stop(self):
        self.twist_pub.publish(Twist())

    def on_timer(self, _event):
        if not self.stop_on_no_image:
            return
        if self.last_image_time == rospy.Time(0):
            return
        age = (rospy.Time.now() - self.last_image_time).to_sec()
        if age > self.no_image_timeout:
            rospy.logwarn_throttle(2.0, "obstacle_avoidance: no image for %.2fs, stopping", age)
            self.publish_stop()

    def clip_box(self, w, h):
        x0 = int(clamp(self.roi_x0, 0.0, 1.0) * w)
        x1 = int(clamp(self.roi_x1, 0.0, 1.0) * w)
        y0 = int(clamp(self.roi_y0, 0.0, 1.0) * h)
        y1 = int(clamp(self.roi_y1, 0.0, 1.0) * h)
        if x1 <= x0:
            x0, x1 = 0, w
        if y1 <= y0:
            y0, y1 = int(0.4 * h), h
        return x0, y0, x1, y1

    def floor_model(self, hsv):
        h, w = hsv.shape[:2]
        y0 = int(clamp(self.floor_sample_y0, 0.0, 1.0) * h)
        y1 = int(clamp(self.floor_sample_y1, 0.0, 1.0) * h)
        x0 = int(0.25 * w)
        x1 = int(0.75 * w)
        sample = hsv[y0:y1, x0:x1]
        if sample.size == 0:
            sample = hsv[int(0.8 * h):h, :]
        return np.median(sample.reshape(-1, 3), axis=0).astype(np.float32)

    def obstacle_mask(self, bgr):
        hsv = cv2.cvtColor(bgr, cv2.COLOR_BGR2HSV)
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        floor = self.floor_model(hsv)

        # Hue wraps at 180 in OpenCV HSV, so use circular hue distance.
        hsv_f = hsv.astype(np.float32)
        dh = np.abs(hsv_f[:, :, 0] - floor[0])
        dh = np.minimum(dh, 180.0 - dh) * 1.4
        ds = np.abs(hsv_f[:, :, 1] - floor[1]) * 0.35
        dv = np.abs(hsv_f[:, :, 2] - floor[2]) * 0.55
        color_dist = dh + ds + dv

        edges = cv2.Canny(gray, self.edge_thresh, self.edge_thresh * 2)
        non_floor = color_dist > self.hsv_floor_dist_thresh
        dark = hsv[:, :, 2] < self.dark_v_thresh
        saturated = hsv[:, :, 1] > self.min_saturation
        mask = (non_floor & (saturated | dark | (edges > 0))).astype(np.uint8) * 255

        if self.open_kernel >= 3:
            k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (self.open_kernel, self.open_kernel))
            mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, k)
        if self.close_kernel >= 3:
            k = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (self.close_kernel, self.close_kernel))
            mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, k)
        return mask

    def sector_ratios(self, mask, box):
        x0, y0, x1, y1 = box
        roi = mask[y0:y1, x0:x1]
        if roi.size == 0:
            return 0.0, 0.0, 0.0, roi

        # Remove tiny speckles before measuring sector occupancy.
        contours, _ = cv2.findContours(roi, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cleaned = np.zeros_like(roi)
        for c in contours:
            if cv2.contourArea(c) >= self.min_obstacle_area:
                cv2.drawContours(cleaned, [c], -1, 255, thickness=cv2.FILLED)

        rw = cleaned.shape[1]
        a = rw // 3
        left = cleaned[:, :a]
        center = cleaned[:, a:2 * a]
        right = cleaned[:, 2 * a:]

        def ratio(part):
            return float(np.count_nonzero(part)) / float(part.size) if part.size else 0.0

        return ratio(left), ratio(center), ratio(right), cleaned

    def decide(self, left, center, right):
        stop_threshold = self.center_stop_ratio
        if self.last_blocked:
            stop_threshold = max(0.01, stop_threshold - self.clear_hysteresis)

        twist = Twist()
        blocked = center >= stop_threshold
        if blocked:
            twist.linear.x = 0.0
            twist.angular.z = self.turn_speed if left < right else -self.turn_speed
            state = "blocked_turn_left" if left < right else "blocked_turn_right"
        elif left >= self.side_slow_ratio or right >= self.side_slow_ratio:
            twist.linear.x = self.slow_speed
            steer = clamp((left - right) * self.side_steer_gain, -self.turn_speed, self.turn_speed)
            twist.angular.z = -steer
            state = "side_avoid"
        else:
            twist.linear.x = self.forward_speed
            twist.angular.z = 0.0
            state = "clear"

        self.last_blocked = blocked
        return twist, state

    def on_image(self, msg):
        self.last_image_time = rospy.Time.now()
        try:
            bgr = self.bridge.imgmsg_to_cv2(msg, desired_encoding="bgr8")
        except CvBridgeError as exc:
            rospy.logwarn("obstacle_avoidance cv_bridge: %s", exc)
            return

        h, w = bgr.shape[:2]
        box = self.clip_box(w, h)
        mask = self.obstacle_mask(bgr)
        left, center, right, cleaned_roi = self.sector_ratios(mask, box)
        twist, state = self.decide(left, center, right)
        self.twist_pub.publish(twist)

        rospy.loginfo_throttle(
            1.0,
            "obstacle_avoidance: %s left=%.2f center=%.2f right=%.2f v=%.2f wz=%.2f",
            state,
            left,
            center,
            right,
            twist.linear.x,
            twist.angular.z,
        )

        if self.publish_debug and self.debug_pub is not None:
            self.publish_debug_image(msg, bgr, box, cleaned_roi, left, center, right, state)

    def publish_debug_image(self, msg, bgr, box, cleaned_roi, left, center, right, state):
        dbg = bgr.copy()
        x0, y0, x1, y1 = box
        overlay = dbg[y0:y1, x0:x1].copy()
        red = np.zeros_like(overlay)
        red[:, :, 2] = cleaned_roi
        overlay = cv2.addWeighted(overlay, 0.65, red, 0.35, 0.0)
        dbg[y0:y1, x0:x1] = overlay
        cv2.rectangle(dbg, (x0, y0), (x1 - 1, y1 - 1), (0, 255, 255), 2)
        sx = (x1 - x0) // 3
        cv2.line(dbg, (x0 + sx, y0), (x0 + sx, y1), (255, 255, 0), 1)
        cv2.line(dbg, (x0 + 2 * sx, y0), (x0 + 2 * sx, y1), (255, 255, 0), 1)
        label = "{} L:{:.2f} C:{:.2f} R:{:.2f}".format(state, left, center, right)
        cv2.putText(dbg, label, (10, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2, cv2.LINE_AA)
        try:
            out = self.bridge.cv2_to_imgmsg(dbg, encoding="bgr8")
            out.header = msg.header
            self.debug_pub.publish(out)
        except CvBridgeError as exc:
            rospy.logwarn("obstacle_avoidance debug cv_bridge: %s", exc)


def main():
    rospy.init_node("opencv_obstacle_avoidance", anonymous=False)
    try:
        OpenCVObstacleAvoidance()
        rospy.spin()
    except rospy.ROSInterruptException:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
