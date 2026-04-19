#!/usr/bin/env python3
"""
OmniRoam 三角全向轮底盘：等边三角形顶点布置三颗万向轮时的运动学 + 简单速度运动控制。

与普通汽车轮子的区别
------------------
- **普通汽车（两轮/四轮转向）**：在平面内是**非完整约束**，瞬时速度只能沿**轮子的滚动方向**
  （再加转向产生的转弯），**不能**同时任意指定「横向滑移」；要侧移必须先打方向、倒车等组合机动。
- **万向轮（全向轮）**：滚子允许在垂直于主轮轴方向有附加自由度，整车在平面内可近似实现
  **v_x、v_y、ω 独立**（三全向轮典型为**完整约束**），因此可以**直接横移、斜移、旋转**，
  这是相对普通车轮多出来的「左右平移」能力。

几何约定
--------
- 车体坐标系 B：原点在车体质心（等边三角形形心），x 前、y 左、z 上（右手系）。
- 三个轮心位于半径 R 的圆上，方位角 θ_k = θ_offset + k·2π/3，k∈{0,1,2}。
- 等边三角形边长 a 与 R 关系：R = a / √3（形心到顶点距离）。
- 默认滚动方向 u_k 为从轮心指向形心方向的**逆时针 90°**（切向）。若某轮装反，把 `wheel_signs[k]` 设为 -1。

运动学（平面）
------------
  v_roll_k = -vx·sin θ_k + vy·cos θ_k + ω·R
矩阵 v_wheels = M · [vx, vy, ω]^T；正解 twist = M^{-1} · v_wheels。
轮角速度 ω_wheel = v_roll / r_wheel（r_wheel = 直径/2）。

默认物理参数（可按减速比等修改）
------------------------------
- 轮直径 85 mm → r = 0.0425 m
- 电机额定转速 282 r/min（空载参考），配合 `motor_gear_ratio` 得到轮轴转速；
  轮缘线速度上限约 v ≈ (2π/60)·RPM_geared·r（留裕量用 `motor_speed_safety`）。

依赖：numpy
"""
from __future__ import print_function

import math
from typing import Optional, Tuple

import numpy as np

# --- OmniRoam 默认实车参数（可改）---
DEFAULT_SIDE_M = 0.1602147
DEFAULT_WHEEL_DIAMETER_M = 0.085
DEFAULT_MOTOR_RATED_RPM = 282.0
DEFAULT_MOTOR_GEAR_RATIO = 1.0
DEFAULT_MOTOR_SPEED_SAFETY = 0.85


def equilateral_centroid_to_vertex(side_m: float) -> float:
    """等边三角形边长 a → 形心到顶点距离 R = a/√3。"""
    return float(side_m) / math.sqrt(3.0)


def rpm_to_rad_s(rpm: float) -> float:
    """转每分 → rad/s。"""
    return float(rpm) * (2.0 * math.pi / 60.0)


def wheel_linear_speed_max_m_s(
    motor_rpm: float,
    wheel_radius_m: float,
    gear_ratio: float = 1.0,
    safety: float = 1.0,
) -> float:
    """
    由电机额定转速（到轮轴）估算轮缘最大线速度 (m/s)。
    gear_ratio：电机转速 × ratio = 轮轴转速（减速电机则 ratio < 1 若定义为「电机/轮轴」）。
    若定义为「轮轴/电机」则传入 1/减速比，请与实物一致。
    """
    omega_wheel = rpm_to_rad_s(motor_rpm * gear_ratio)
    return safety * omega_wheel * wheel_radius_m


def suggest_twist_limits_from_wheel_speed(
    kin: "OmniTriangleKinematics", v_wheel_max_m_s: float
) -> Tuple[float, float, float]:
    """
    由「单轮最大线速度」保守估算车体系速度箱型上限（分别沿纯 vx、纯 vy、纯 ω 缩放）。
    组合运动时仍可能在控制器里再做一轮轮速统一缩放，避免饱和。
    """
    vlim = float(v_wheel_max_m_s)
    if vlim <= 0:
        return 0.0, 0.0, 0.0
    colx = np.abs(kin.M @ np.array([1.0, 0.0, 0.0]))
    coly = np.abs(kin.M @ np.array([0.0, 1.0, 0.0]))
    colw = np.abs(kin.M @ np.array([0.0, 0.0, 1.0]))
    vx_max = vlim / max(float(np.max(colx)), 1e-9)
    vy_max = vlim / max(float(np.max(coly)), 1e-9)
    omega_max = vlim / max(float(np.max(colw)), 1e-9)
    return vx_max, vy_max, omega_max


class OmniTriangleKinematics:
    """
    三全向轮（三角顶点布局）平面运动学。
    """

    def __init__(
        self,
        side_m: float = DEFAULT_SIDE_M,
        wheel_radius_m: Optional[float] = None,
        wheel_diameter_m: float = DEFAULT_WHEEL_DIAMETER_M,
        theta_offset_rad: float = 0.0,
        wheel_signs: Optional[np.ndarray] = None,
    ):
        """
        :param side_m: 等边三角形边长 (m)
        :param wheel_radius_m: 轮半径 (m)；若 None 则用 wheel_diameter_m/2
        :param wheel_diameter_m: 轮直径 (m)，默认 0.085
        :param theta_offset_rad: 整体绕 z 旋转布局（使某一轮对准车头）
        :param wheel_signs: (3,) 每轮 ±1，装反时取 -1
        """
        self.side_m = float(side_m)
        self.R = equilateral_centroid_to_vertex(side_m)
        if wheel_radius_m is not None:
            self.wheel_radius_m = float(wheel_radius_m)
        else:
            self.wheel_radius_m = float(wheel_diameter_m) * 0.5
        self.theta_offset_rad = float(theta_offset_rad)
        self.wheel_signs = (
            np.ones(3, dtype=float)
            if wheel_signs is None
            else np.asarray(wheel_signs, dtype=float).reshape(3)
        )
        self._build_M()

    def max_wheel_omega_rad_s(
        self,
        motor_rpm: float = DEFAULT_MOTOR_RATED_RPM,
        gear_ratio: float = DEFAULT_MOTOR_GEAR_RATIO,
        safety: float = DEFAULT_MOTOR_SPEED_SAFETY,
    ) -> float:
        """由电机 RPM 得到轮轴 rad/s 上限（× safety）。"""
        return rpm_to_rad_s(motor_rpm * gear_ratio) * safety

    def _wheel_thetas(self) -> np.ndarray:
        return self.theta_offset_rad + np.array(
            [0.0, 2.0 * math.pi / 3.0, 4.0 * math.pi / 3.0], dtype=float
        )

    def _build_M(self) -> None:
        """v_wheels = M @ [vx, vy, omega]^T，单位 m/s 与 rad/s。"""
        thetas = self._wheel_thetas()
        M = np.zeros((3, 3), dtype=float)
        for k in range(3):
            t = thetas[k]
            M[k, 0] = -math.sin(t)
            M[k, 1] = math.cos(t)
            M[k, 2] = self.R
            M[k, :] *= self.wheel_signs[k]
        self.M = M
        self.M_inv = np.linalg.inv(M)

    def fk_twist(self, wheel_linear_m_s: np.ndarray) -> np.ndarray:
        """正运动学：三轮线速度 (m/s) → 车体系 twist [vx, vy, omega]。"""
        v = np.asarray(wheel_linear_m_s, dtype=float).reshape(3)
        return self.M_inv @ v

    def ik_wheel_linear(self, twist_body: np.ndarray) -> np.ndarray:
        """逆运动学：[vx, vy, omega] → 三轮线速度 (m/s)。"""
        xi = np.asarray(twist_body, dtype=float).reshape(3)
        return self.M @ xi

    def ik_wheel_omega_rad_s(self, twist_body: np.ndarray) -> np.ndarray:
        """逆运动学：车体 twist → 各轮角速度 rad/s。"""
        v = self.ik_wheel_linear(twist_body)
        return v / self.wheel_radius_m

    def fk_twist_from_wheel_omega(self, wheel_omega_rad_s: np.ndarray) -> np.ndarray:
        """由轮角速度 (rad/s) 推算车体系 twist。"""
        w = np.asarray(wheel_omega_rad_s, dtype=float).reshape(3)
        return self.fk_twist(w * self.wheel_radius_m)

    def wheel_positions_body(self) -> np.ndarray:
        """(3,2) 各轮心在车体系 x,y。"""
        thetas = self._wheel_thetas()
        return self.R * np.stack([np.cos(thetas), np.sin(thetas)], axis=1)


def world_twist_to_body(
    vx_w: float, vy_w: float, omega: float, yaw_rad: float
) -> np.ndarray:
    """世界系水平速度转到车体系（只绕 z 旋转 yaw，omega 相同）。"""
    c, s = math.cos(yaw_rad), math.sin(yaw_rad)
    vx_b = c * vx_w + s * vy_w
    vy_b = -s * vx_w + c * vy_w
    return np.array([vx_b, vy_b, omega], dtype=float)


def saturate_twist_box(
    twist: np.ndarray, vx_max: float, vy_max: float, omega_max: float
) -> Tuple[np.ndarray, float]:
    """独立轴限幅；若任一超限则整体按比例缩小，保持方向。"""
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


class ChassisMotionController:
    """
    期望 twist → 滤波 + 车体箱型限幅 + 轮速统一缩放（不超电机/轮速）→ 各轮 rad/s。
    """

    def __init__(
        self,
        kin: OmniTriangleKinematics,
        vx_max: float,
        vy_max: float,
        omega_max: float,
        wheel_omega_max_rad_s: float,
        twist_filter_tau_s: float = 0.0,
    ):
        self.kin = kin
        self.vx_max = float(vx_max)
        self.vy_max = float(vy_max)
        self.omega_max = float(omega_max)
        self.wheel_omega_max_rad_s = float(wheel_omega_max_rad_s)
        self.tau = float(twist_filter_tau_s)
        self._twist_filt = np.zeros(3, dtype=float)
        self._initialized = False

    @classmethod
    def from_vehicle_specs(
        cls,
        side_m: float = DEFAULT_SIDE_M,
        wheel_diameter_m: float = DEFAULT_WHEEL_DIAMETER_M,
        motor_rated_rpm: float = DEFAULT_MOTOR_RATED_RPM,
        motor_gear_ratio: float = DEFAULT_MOTOR_GEAR_RATIO,
        motor_speed_safety: float = DEFAULT_MOTOR_SPEED_SAFETY,
        theta_offset_rad: float = 0.0,
        wheel_signs: Optional[np.ndarray] = None,
        twist_filter_tau_s: float = 0.08,
    ) -> "ChassisMotionController":
        """
        用边长、轮径、电机额定转速一键生成运动学与保守速度限幅。
        motor_gear_ratio：电机转速乘以该系数得到轮轴转速（按你的减速器定义调整）。
        """
        kin = OmniTriangleKinematics(
            side_m=side_m,
            wheel_diameter_m=wheel_diameter_m,
            theta_offset_rad=theta_offset_rad,
            wheel_signs=wheel_signs,
        )
        r = kin.wheel_radius_m
        v_wheel_max = wheel_linear_speed_max_m_s(
            motor_rated_rpm, r, gear_ratio=motor_gear_ratio, safety=motor_speed_safety
        )
        vx_max, vy_max, omega_max = suggest_twist_limits_from_wheel_speed(
            kin, v_wheel_max
        )
        w_max = kin.max_wheel_omega_rad_s(
            motor_rated_rpm, gear_ratio=motor_gear_ratio, safety=motor_speed_safety
        )
        return cls(
            kin,
            vx_max=vx_max,
            vy_max=vy_max,
            omega_max=omega_max,
            wheel_omega_max_rad_s=w_max,
            twist_filter_tau_s=twist_filter_tau_s,
        )

    def reset(self) -> None:
        self._twist_filt[:] = 0.0
        self._initialized = False

    def step(
        self,
        vx_cmd: float,
        vy_cmd: float,
        omega_cmd: float,
        dt: float,
        yaw_rad: Optional[float] = None,
        use_world_frame: bool = False,
    ) -> np.ndarray:
        """
        单步更新，返回各轮目标角速度 (rad/s)。
        """
        if use_world_frame:
            if yaw_rad is None:
                raise ValueError("yaw_rad required when use_world_frame=True")
            twist = world_twist_to_body(vx_cmd, vy_cmd, omega_cmd, yaw_rad)
        else:
            twist = np.array([vx_cmd, vy_cmd, omega_cmd], dtype=float)

        if not self._initialized:
            self._twist_filt = twist.copy()
            self._initialized = True
        elif self.tau > 1e-6 and dt > 0:
            a = math.exp(-dt / self.tau)
            self._twist_filt = a * self._twist_filt + (1.0 - a) * twist
        else:
            self._twist_filt = twist

        xi_sat, _ = saturate_twist_box(
            self._twist_filt, self.vx_max, self.vy_max, self.omega_max
        )
        omega_wheels = self.kin.ik_wheel_omega_rad_s(xi_sat)
        w_abs_max = np.max(np.abs(omega_wheels))
        if w_abs_max > self.wheel_omega_max_rad_s and w_abs_max > 1e-9:
            omega_wheels *= self.wheel_omega_max_rad_s / w_abs_max
        return omega_wheels


def _demo():
    side = DEFAULT_SIDE_M
    print("OmniRoam chassis demo — wheel Ø85 mm, motor 282 r/min (reference)")
    kin = OmniTriangleKinematics(side_m=side, wheel_diameter_m=DEFAULT_WHEEL_DIAMETER_M)
    v_max = wheel_linear_speed_max_m_s(
        DEFAULT_MOTOR_RATED_RPM,
        kin.wheel_radius_m,
        gear_ratio=DEFAULT_MOTOR_GEAR_RATIO,
        safety=DEFAULT_MOTOR_SPEED_SAFETY,
    )
    w_max = kin.max_wheel_omega_rad_s()
    vx_m, vy_m, om_m = suggest_twist_limits_from_wheel_speed(kin, v_max)
    print("  wheel radius (m):", kin.wheel_radius_m)
    print("  est. v_wheel max (m/s):", round(v_max, 4))
    print("  est. omega_wheel max (rad/s):", round(w_max, 3))
    print("  suggested twist limits vx, vy (m/s), omega (rad/s):", round(vx_m, 3), round(vy_m, 3), round(om_m, 3))

    twist = np.array([0.3, 0.0, 0.0], dtype=float)
    twist_back = kin.fk_twist(kin.ik_wheel_linear(twist))
    print("  FK(IK) pos err:", np.linalg.norm(twist_back - twist))

    ctrl = ChassisMotionController.from_vehicle_specs(twist_filter_tau_s=0.05)
    w = ctrl.step(0.3, 0.0, 0.0, dt=0.02)
    print("  controller wheel omega (rad/s):", np.round(w, 3))


if __name__ == "__main__":
    _demo()
