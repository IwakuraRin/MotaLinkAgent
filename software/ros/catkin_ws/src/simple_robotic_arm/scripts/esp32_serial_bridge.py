#!/usr/bin/env python3
"""
ESP32-S3 USB-UART 与 ROS 的文本行桥接（占位实现）。

- 订阅 ~tx (std_msgs/String)：写入串口，默认在末尾补 \\n。
- 发布 ~rx (std_msgs/String)：从串口按行读出（decode 为 utf-8，忽略错误）。

与 RoboticArm_ESP32S3 固件对接时，应把双方约定为同一套**二进制或文本协议**；
当前固件大量 Serial.println 调试输出，适合联调；正式控制请定义帧头/校验并改用
std_msgs/UInt8MultiArray 或自定义 .msg。
"""
from __future__ import print_function

import threading

import rospy
from std_msgs.msg import String

try:
    import serial
except ImportError:
    serial = None


def main():
    rospy.init_node("esp32_serial_bridge")

    if serial is None:
        rospy.logfatal("缺少 PySerial：sudo apt install python3-serial")
        return

    port = rospy.get_param("~port", "/dev/ttyUSB0")
    baud = int(rospy.get_param("~baud", 115200))
    append_newline = rospy.get_param("~append_newline_on_tx", True)

    try:
        ser = serial.Serial(port, baud, timeout=0.2)
    except serial.SerialException as e:
        rospy.logfatal("无法打开串口 %s: %s", port, e)
        return

    rx_pub = rospy.Publisher("~rx", String, queue_size=100)
    stop = threading.Event()

    def read_loop():
        while not stop.is_set() and not rospy.is_shutdown():
            try:
                line = ser.readline()
                if not line:
                    continue
                text = line.decode("utf-8", errors="replace").rstrip("\r\n")
                if text:
                    rx_pub.publish(String(data=text))
            except serial.SerialException as ex:
                rospy.logwarn_throttle(5.0, "串口读错误: %s", ex)
                stop.set()
                break

    t = threading.Thread(target=read_loop, daemon=True)
    t.start()

    def on_tx(msg):
        if not ser.is_open:
            return
        data = msg.data
        if append_newline and not data.endswith("\n"):
            data = data + "\n"
        try:
            ser.write(data.encode("utf-8"))
            ser.flush()
        except serial.SerialException as ex:
            rospy.logwarn("串口写失败: %s", ex)

    rospy.Subscriber("~tx", String, on_tx, queue_size=50)
    rospy.loginfo("esp32_serial_bridge: %s @ %d, topics ~tx ~rx", port, baud)

    rospy.on_shutdown(lambda: (stop.set(), ser.close()))
    rospy.spin()


if __name__ == "__main__":
    main()
