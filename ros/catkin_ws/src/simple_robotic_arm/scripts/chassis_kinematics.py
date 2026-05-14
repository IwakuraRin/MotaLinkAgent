#!/usr/bin/env python3
#|--------------------------------------------------------------------------
# 三全向轮底盘运动学节点
#|--------------------------------------------------------------------------
# 这个脚本负责把 ROS /cmd_vel 车体速度转换为三颗 85mm 全向轮的目标角速度，
# 几何参数来自 2026-05-10 的 Shapr3D STEP 底盘文件，可发布调试话题并转发到串口桥。
#|--------------------------------------------------------------------------

from __future__ import print_function

import math
import sys
import threading
from typing import Optional, Tuple

import numpy as np

try:
    import rospy
    from geometry_msgs.msg import Twist
    from std_msgs.msg import Float32MultiArray, String
except ImportError:
    rospy = None
    Twist = None
    Float32MultiArray = None
    String = None


#|--------------------------------------------------------------------------
# CAD 实测底盘几何参数
#|--------------------------------------------------------------------------
# STEP 文件单位为毫米。三轮中心基本构成等边三角形，边长约 231.597mm；
# 车体参考中心取底盘主圆中心，轮方位角用于三全向轮逆运动学。
#|--------------------------------------------------------------------------
CAD_REFERENCE_STEP_FILE = "shapr3d_export_2026-05-10_22h10m.step"
DEFAULT_WHEEL_DIAMETER_M = 0.085
DEFAULT_WHEEL_RADIUS_M = DEFAULT_WHEEL_DIAMETER_M * 0.5
DEFAULT_WHEEL_BASE_RADIUS_M = 0.1337147
DEFAULT_WHEEL_CENTER_DISTANCE_M = 0.231597117921
DEFAULT_WHEEL_ANGLES_DEG = [29.9459, 149.6229, -90.8207]
DEFAULT_WHEEL_NAMES = ["right_front", "left_front", "rear"]
DEFAULT_WHEEL_SIGNS = [1.0, 1.0, 1.0]
DEFAULT_CMD_TIMEOUT_S = 0.35
DEFAULT_CONTROL_RATE_HZ = 30.0
DEFAULT_TWIST_FILTER_TAU_S = 0.08
DEFAULT_MAX_WHEEL_OMEGA_RAD_S = 25.10
DEFAULT_SERIAL_TOPIC = "/esp32_serial_bridge/tx"


#|--------------------------------------------------------------------------
# 通用数学工具
#|--------------------------------------------------------------------------
# 这里放置单位转换、速度限幅、坐标转换等纯函数，不直接依赖 ROS。
#|--------------------------------------------------------------------------
def rpm_to_rad_s(rpm: float) -> float:
    return float(rpm) * (2.0 * math.pi / 60.0)


def wheel_linear_speed_max_m_s(
    wheel_omega_max_rad_s: float,
    wheel_radius_m: float,
) -> float:
    return float(wheel_omega_max_rad_s) * float(wheel_radius_m)


def saturate_twist_box(
    twist: np.ndarray,
    vx_max: float,
    vy_max: float,
    omega_max: float,
) -> Tuple[np.ndarray, float]:
    xi = np.asarray(twist, dtype=float).reshape(3).copy()
    lim = np.array([vx_max, vy_max, omega_max], dtype=float)
    abs_xi = np.abs(xi)
    mask = lim > 1e-9
    ratios = np.ones(3)
    ratios[mask] = abs_xi[mask] / lim[mask]
    rmax = float(np.max(ratios))
    if rmax <= 1.0:
        return xi, 1.0
    scale = 1.0 / rmax
    return xi * scale, scale


def world_twist_to_body(
    vx_w: float,
    vy_w: float,
    omega: float,
    yaw_rad: float,
) -> np.ndarray:
    c = math.cos(yaw_rad)
    s = math.sin(yaw_rad)
    vx_b = c * vx_w + s * vy_w
    vy_b = -s * vx_w + c * vy_w
    return np.array([vx_b, vy_b, omega], dtype=float)


#|--------------------------------------------------------------------------
# 三全向轮逆运动学模型
#|--------------------------------------------------------------------------
# 输入车体坐标系 twist [vx, vy, omega]，输出每个轮子的轮缘线速度或角速度。
# 车体坐标约定：x 向前、y 向左、z 向上；轮滚动方向按切向方向建模。
#|--------------------------------------------------------------------------
class OmniTriangleKinematics:
    def __init__(
        self,
        wheel_base_radius_m: float = DEFAULT_WHEEL_BASE_RADIUS_M,
        wheel_radius_m: float = DEFAULT_WHEEL_RADIUS_M,
        wheel_angles_deg: Optional[list] = None,
        wheel_signs: Optional[list] = None,
    ):
        self.wheel_base_radius_m = float(wheel_base_radius_m)
        self.wheel_radius_m = float(wheel_radius_m)
        self.wheel_angles_deg = (
            list(DEFAULT_WHEEL_ANGLES_DEG)
            if wheel_angles_deg is None
            else [float(v) for v in wheel_angles_deg]
        )
        self.wheel_signs = np.asarray(
            DEFAULT_WHEEL_SIGNS if wheel_signs is None else wheel_signs,
            dtype=float,
        ).reshape(3)
        self._build_matrix()

    @property
    def wheel_angles_rad(self) -> np.ndarray:
        return np.radians(np.asarray(self.wheel_angles_deg, dtype=float))

    def _build_matrix(self) -> None:
        matrix = np.zeros((3, 3), dtype=float)
        for index, theta in enumerate(self.wheel_angles_rad):
            matrix[index, 0] = -math.sin(theta)
            matrix[index, 1] = math.cos(theta)
            matrix[index, 2] = self.wheel_base_radius_m
            matrix[index, :] *= self.wheel_signs[index]

        self.matrix = matrix
        self.inverse_matrix = np.linalg.inv(matrix)

    def wheel_positions_body(self) -> np.ndarray:
        thetas = self.wheel_angles_rad
        return self.wheel_base_radius_m * np.stack(
            [np.cos(thetas), np.sin(thetas)],
            axis=1,
        )

    def ik_wheel_linear_m_s(self, twist_body: np.ndarray) -> np.ndarray:
        twist = np.asarray(twist_body, dtype=float).reshape(3)
        return self.matrix @ twist

    def ik_wheel_omega_rad_s(self, twist_body: np.ndarray) -> np.ndarray:
        return self.ik_wheel_linear_m_s(twist_body) / self.wheel_radius_m

    def fk_twist_from_wheel_linear(self, wheel_linear_m_s: np.ndarray) -> np.ndarray:
        wheels = np.asarray(wheel_linear_m_s, dtype=float).reshape(3)
        return self.inverse_matrix @ wheels

    def fk_twist_from_wheel_omega(self, wheel_omega_rad_s: np.ndarray) -> np.ndarray:
        wheels = np.asarray(wheel_omega_rad_s, dtype=float).reshape(3)
        return self.fk_twist_from_wheel_linear(wheels * self.wheel_radius_m)


#|--------------------------------------------------------------------------
# 底盘控制器
#|--------------------------------------------------------------------------
# 这个模块负责对 /cmd_vel 做平滑、车体速度限幅和轮速统一缩放，避免单轮超出上限。
#|--------------------------------------------------------------------------
class ChassisMotionController:
    def __init__(
        self,
        kinematics: OmniTriangleKinematics,
        vx_max_m_s: float,
        vy_max_m_s: float,
        omega_max_rad_s: float,
        wheel_omega_max_rad_s: float,
        twist_filter_tau_s: float = DEFAULT_TWIST_FILTER_TAU_S,
    ):
        self.kinematics = kinematics
        self.vx_max_m_s = float(vx_max_m_s)
        self.vy_max_m_s = float(vy_max_m_s)
        self.omega_max_rad_s = float(omega_max_rad_s)
        self.wheel_omega_max_rad_s = float(wheel_omega_max_rad_s)
        self.twist_filter_tau_s = float(twist_filter_tau_s)
        self._filtered_twist = np.zeros(3, dtype=float)
        self._initialized = False

    @classmethod
    def from_wheel_speed_limit(
        cls,
        kinematics: OmniTriangleKinematics,
        wheel_omega_max_rad_s: float,
        twist_filter_tau_s: float = DEFAULT_TWIST_FILTER_TAU_S,
    ) -> "ChassisMotionController":
        v_wheel_max = wheel_linear_speed_max_m_s(
            wheel_omega_max_rad_s,
            kinematics.wheel_radius_m,
        )
        unit_x = np.abs(kinematics.matrix @ np.array([1.0, 0.0, 0.0]))
        unit_y = np.abs(kinematics.matrix @ np.array([0.0, 1.0, 0.0]))
        unit_w = np.abs(kinematics.matrix @ np.array([0.0, 0.0, 1.0]))
        vx_max = v_wheel_max / max(float(np.max(unit_x)), 1e-9)
        vy_max = v_wheel_max / max(float(np.max(unit_y)), 1e-9)
        omega_max = v_wheel_max / max(float(np.max(unit_w)), 1e-9)
        return cls(
            kinematics=kinematics,
            vx_max_m_s=vx_max,
            vy_max_m_s=vy_max,
            omega_max_rad_s=omega_max,
            wheel_omega_max_rad_s=wheel_omega_max_rad_s,
            twist_filter_tau_s=twist_filter_tau_s,
        )

    def reset(self) -> None:
        self._filtered_twist[:] = 0.0
        self._initialized = False

    def step(
        self,
        twist_body: np.ndarray,
        dt_s: float,
    ) -> Tuple[np.ndarray, np.ndarray]:
        twist = np.asarray(twist_body, dtype=float).reshape(3)
        if not self._initialized:
            self._filtered_twist = twist.copy()
            self._initialized = True
        elif self.twist_filter_tau_s > 1e-6 and dt_s > 0.0:
            alpha = math.exp(-dt_s / self.twist_filter_tau_s)
            self._filtered_twist = (
                alpha * self._filtered_twist
                + (1.0 - alpha) * twist
            )
        else:
            self._filtered_twist = twist

        saturated_twist, _ = saturate_twist_box(
            self._filtered_twist,
            self.vx_max_m_s,
            self.vy_max_m_s,
            self.omega_max_rad_s,
        )
        wheel_omega = self.kinematics.ik_wheel_omega_rad_s(saturated_twist)
        max_abs_wheel = float(np.max(np.abs(wheel_omega)))
        if (
            self.wheel_omega_max_rad_s > 1e-9
            and max_abs_wheel > self.wheel_omega_max_rad_s
        ):
            wheel_omega *= self.wheel_omega_max_rad_s / max_abs_wheel

        return saturated_twist, wheel_omega


#|--------------------------------------------------------------------------
# ROS 底盘运动学节点
#|--------------------------------------------------------------------------
# 订阅 /cmd_vel，把速度转换为三轮目标角速度，发布 wheel_omega 调试话题，
# 并可把文本协议转发给 esp32_serial_bridge.py 的串口发送 topic。
#|--------------------------------------------------------------------------
class ChassisKinematicsNode:
    def __init__(self):
        if rospy is None:
            raise RuntimeError("当前环境没有 ROS rospy，不能启动 ROS 节点")

        rospy.init_node("chassis_kinematics")

        self.cmd_vel_topic = rospy.get_param("~cmd_vel_topic", "/cmd_vel")
        self.wheel_omega_topic = rospy.get_param(
            "~wheel_omega_topic",
            "/chassis/wheel_omega",
        )
        self.serial_tx_topic = rospy.get_param(
            "~serial_tx_topic",
            DEFAULT_SERIAL_TOPIC,
        )
        self.publish_serial = bool(rospy.get_param("~publish_serial", True))
        self.command_timeout_s = float(
            rospy.get_param("~command_timeout_s", DEFAULT_CMD_TIMEOUT_S)
        )
        self.control_rate_hz = float(
            rospy.get_param("~control_rate_hz", DEFAULT_CONTROL_RATE_HZ)
        )
        self.serial_command_prefix = rospy.get_param(
            "~serial_command_prefix",
            "CHASSIS_WHEEL_OMEGA",
        )

        wheel_angles_deg = rospy.get_param(
            "~wheel_angles_deg",
            DEFAULT_WHEEL_ANGLES_DEG,
        )
        wheel_signs = rospy.get_param("~wheel_signs", DEFAULT_WHEEL_SIGNS)
        wheel_base_radius_m = float(
            rospy.get_param("~wheel_base_radius_m", DEFAULT_WHEEL_BASE_RADIUS_M)
        )
        wheel_radius_m = float(
            rospy.get_param("~wheel_radius_m", DEFAULT_WHEEL_RADIUS_M)
        )
        wheel_omega_max_rad_s = float(
            rospy.get_param(
                "~wheel_omega_max_rad_s",
                DEFAULT_MAX_WHEEL_OMEGA_RAD_S,
            )
        )
        twist_filter_tau_s = float(
            rospy.get_param("~twist_filter_tau_s", DEFAULT_TWIST_FILTER_TAU_S)
        )

        self.kinematics = OmniTriangleKinematics(
            wheel_base_radius_m=wheel_base_radius_m,
            wheel_radius_m=wheel_radius_m,
            wheel_angles_deg=wheel_angles_deg,
            wheel_signs=wheel_signs,
        )
        self.controller = ChassisMotionController.from_wheel_speed_limit(
            kinematics=self.kinematics,
            wheel_omega_max_rad_s=wheel_omega_max_rad_s,
            twist_filter_tau_s=twist_filter_tau_s,
        )

        self._lock = threading.Lock()
        self._last_cmd_time = rospy.Time(0)
        self._last_step_time = rospy.Time.now()
        self._target_twist = np.zeros(3, dtype=float)

        self.wheel_omega_pub = rospy.Publisher(
            self.wheel_omega_topic,
            Float32MultiArray,
            queue_size=20,
        )
        self.serial_tx_pub = rospy.Publisher(
            self.serial_tx_topic,
            String,
            queue_size=20,
        )
        self.cmd_sub = rospy.Subscriber(
            self.cmd_vel_topic,
            Twist,
            self._handle_cmd_vel,
            queue_size=20,
        )

        rospy.loginfo(
            "chassis_kinematics: CAD=%s base_radius=%.6fm wheel_radius=%.4fm angles=%s signs=%s",
            CAD_REFERENCE_STEP_FILE,
            self.kinematics.wheel_base_radius_m,
            self.kinematics.wheel_radius_m,
            self.kinematics.wheel_angles_deg,
            list(self.kinematics.wheel_signs),
        )
        rospy.loginfo(
            "chassis_kinematics: cmd=%s wheel_pub=%s serial=%s publish_serial=%s",
            self.cmd_vel_topic,
            self.wheel_omega_topic,
            self.serial_tx_topic,
            self.publish_serial,
        )

    def _handle_cmd_vel(self, msg: Twist) -> None:
        with self._lock:
            self._target_twist = np.array(
                [
                    float(msg.linear.x),
                    float(msg.linear.y),
                    float(msg.angular.z),
                ],
                dtype=float,
            )
            self._last_cmd_time = rospy.Time.now()

    def _read_target_twist(self, now: "rospy.Time") -> np.ndarray:
        with self._lock:
            if (
                self._last_cmd_time.to_sec() <= 0.0
                or (now - self._last_cmd_time).to_sec() > self.command_timeout_s
            ):
                return np.zeros(3, dtype=float)
            return self._target_twist.copy()

    def _format_serial_command(self, wheel_omega: np.ndarray) -> str:
        values = " ".join("{:.4f}".format(float(v)) for v in wheel_omega)
        return "{} {}".format(self.serial_command_prefix, values)

    def spin(self) -> None:
        rate = rospy.Rate(self.control_rate_hz)
        while not rospy.is_shutdown():
            now = rospy.Time.now()
            dt_s = max((now - self._last_step_time).to_sec(), 1.0 / 500.0)
            self._last_step_time = now

            twist = self._read_target_twist(now)
            _, wheel_omega = self.controller.step(twist, dt_s)

            self.wheel_omega_pub.publish(
                Float32MultiArray(data=[float(v) for v in wheel_omega])
            )
            if self.publish_serial:
                self.serial_tx_pub.publish(
                    String(data=self._format_serial_command(wheel_omega))
                )

            rate.sleep()


#|--------------------------------------------------------------------------
# 命令行自检入口
#|--------------------------------------------------------------------------
# 不启动 ROS 时执行纯数学自检，方便在 SSH 中验证 CAD 参数和正逆运动学一致性。
#|--------------------------------------------------------------------------
def _demo() -> None:
    kin = OmniTriangleKinematics()
    controller = ChassisMotionController.from_wheel_speed_limit(
        kinematics=kin,
        wheel_omega_max_rad_s=DEFAULT_MAX_WHEEL_OMEGA_RAD_S,
    )
    test_twist = np.array([0.25, 0.0, 0.0], dtype=float)
    wheel_omega = kin.ik_wheel_omega_rad_s(test_twist)
    reconstructed = kin.fk_twist_from_wheel_omega(wheel_omega)

    print("CAD STEP:", CAD_REFERENCE_STEP_FILE)
    print("wheel_base_radius_m:", round(kin.wheel_base_radius_m, 6))
    print("wheel_center_distance_m:", round(DEFAULT_WHEEL_CENTER_DISTANCE_M, 6))
    print("wheel_angles_deg:", [round(v, 4) for v in kin.wheel_angles_deg])
    print("wheel_positions_body_m:")
    print(np.round(kin.wheel_positions_body(), 6))
    print("ik_wheel_omega_rad_s for [0.25,0,0]:", np.round(wheel_omega, 4))
    print("fk_reconstructed_twist:", np.round(reconstructed, 6))
    print(
        "suggested_limits vx vy omega:",
        round(controller.vx_max_m_s, 3),
        round(controller.vy_max_m_s, 3),
        round(controller.omega_max_rad_s, 3),
    )


if __name__ == "__main__":
    if "--demo" in sys.argv or rospy is None:
        _demo()
    else:
        ChassisKinematicsNode().spin()
