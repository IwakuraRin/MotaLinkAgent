#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
上位机视觉巡线 + 简易前方遮挡启发式（ROS 1 + OpenCV）。

巡线：在图像下方 ROI 内做灰度阈值，取最大连通域质心，与画面中心偏差 → angular.z。
障碍物（可选）：在图像上方 ROI 内统计「偏暗」像素占比；超过阈值则停车。
  这仅在「地面偏亮、障碍物明显更暗」时勉强可用；可靠方案见节点顶部说明。

依赖（apt / rosdep）：ros-noetic-cv-bridge、python3-opencv、sensor_msgs、geometry_msgs。
"""
from __future__ import print_function

import sys

import cv2
import numpy as np
import rospy
from cv_bridge import CvBridge, CvBridgeError
from geometry_msgs.msg import Twist
from sensor_msgs.msg import Image


def _clamp(x, lo, hi):
    return max(lo, min(hi, x))


class LineFollowVision(object):
    def __init__(self):
        self.bridge = CvBridge()
        self.image_topic = rospy.get_param("~image_topic", "/usb_cam/image_raw")
        self.cmd_topic = rospy.get_param("~cmd_vel_topic", "/cmd_vel")
        self.publish_debug = rospy.get_param("~publish_debug_image", False)
        self.debug_topic = rospy.get_param("~debug_image_topic", "~debug")

        # 巡线：白线 → 高阈值保留亮像素
        self.line_roi_height_ratio = rospy.get_param("~line_roi_height_ratio", 0.35)
        self.blur_ksize = int(rospy.get_param("~blur_ksize", 5))
        self.white_thresh = int(rospy.get_param("~white_thresh", 200))
        self.min_line_area = float(rospy.get_param("~min_line_area", 500.0))

        self.v_max = float(rospy.get_param("~v_max", 0.12))
        self.k_angular = float(rospy.get_param("~k_angular", 1.8))

        # 简易障碍物：上方 ROI 暗像素比例
        self.obstacle_enable = rospy.get_param("~obstacle_enable", False)
        self.obs_roi_height_ratio = rospy.get_param("~obs_roi_height_ratio", 0.30)
        self.obs_dark_thresh = int(rospy.get_param("~obs_dark_thresh", 80))
        self.obs_dark_ratio = float(rospy.get_param("~obs_dark_ratio", 0.35))

        self.twist_pub = rospy.Publisher(self.cmd_topic, Twist, queue_size=1)
        self.debug_pub = None
        if self.publish_debug:
            self.debug_pub = rospy.Publisher(self.debug_topic, Image, queue_size=1)

        self.sub = rospy.Subscriber(self.image_topic, Image, self._on_image, queue_size=1)
        rospy.loginfo(
            "line_follow_vision: image=%s cmd=%s debug=%s obstacle=%s",
            self.image_topic,
            self.cmd_topic,
            self.publish_debug,
            self.obstacle_enable,
        )

    def _on_image(self, msg):
        try:
            bgr = self.bridge.imgmsg_to_cv2(msg, desired_encoding="bgr8")
        except CvBridgeError as e:
            rospy.logwarn("cv_bridge: %s", e)
            return

        h, w = bgr.shape[:2]
        gray = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
        if self.blur_ksize >= 3 and self.blur_ksize % 2 == 1:
            gray = cv2.GaussianBlur(gray, (self.blur_ksize, self.blur_ksize), 0)

        twist = Twist()
        obstacle = False

        if self.obstacle_enable:
            obs_h = max(1, int(h * self.obs_roi_height_ratio))
            roi_obs = gray[:obs_h, :]
            dark = roi_obs < self.obs_dark_thresh
            ratio = float(np.count_nonzero(dark)) / float(dark.size) if dark.size else 0.0
            obstacle = ratio >= self.obs_dark_ratio
            if obstacle:
                rospy.logwarn_throttle(2.0, "obstacle heuristic: dark_ratio=%.2f", ratio)

        line_h0 = int(h * (1.0 - self.line_roi_height_ratio))
        roi = gray[line_h0:h, :]
        _, mask = cv2.threshold(roi, self.white_thresh, 255, cv2.THRESH_BINARY)
        contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

        cx_img = w * 0.5
        found = False
        if contours:
            c = max(contours, key=cv2.contourArea)
            area = cv2.contourArea(c)
            if area >= self.min_line_area:
                m = cv2.moments(c)
                if m["m00"] > 1e-6:
                    cx = m["m10"] / m["m00"]
                    cy = m["m01"] / m["m00"]
                    cx_img = cx
                    found = True
                    if self.publish_debug and self.debug_pub is not None:
                        cv2.circle(
                            bgr,
                            (int(cx), int(cy + line_h0)),
                            8,
                            (0, 255, 0),
                            2,
                        )

        err = (cx_img - w * 0.5) / max(w * 0.5, 1.0)
        err = _clamp(err, -1.0, 1.0)

        if obstacle:
            twist.linear.x = 0.0
            twist.angular.z = 0.0
        elif found:
            twist.linear.x = self.v_max
            twist.angular.z = _clamp(-self.k_angular * err, -1.5, 1.5)
        else:
            twist.linear.x = 0.0
            twist.angular.z = 0.0
            rospy.logwarn_throttle(3.0, "line_follow: no line contour in ROI")

        self.twist_pub.publish(twist)

        if self.publish_debug and self.debug_pub is not None:
            dbg = bgr.copy()
            cv2.rectangle(dbg, (0, line_h0), (w - 1, h - 1), (255, 128, 0), 1)
            if self.obstacle_enable:
                cv2.rectangle(dbg, (0, 0), (w - 1, int(h * self.obs_roi_height_ratio)), (0, 0, 255), 1)
            try:
                out = self.bridge.cv2_to_imgmsg(dbg, encoding="bgr8")
                out.header = msg.header
                self.debug_pub.publish(out)
            except CvBridgeError as e:
                rospy.logwarn("cv_bridge debug: %s", e)


def main():
    rospy.init_node("line_follow_vision", anonymous=False)
    try:
        LineFollowVision()
        rospy.spin()
    except rospy.ROSInterruptException:
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
