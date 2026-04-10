#!/usr/bin/env python3
"""
OmniRoam 机械臂：5 关节几何模型与正 / 逆运动学（位置 + 平面内朝向 + 腰 / 腕转角）。

几何约定（与实物一致时请用 joint_offset / 限位校准）
----------------------------------------
- 世界系：原点放在关节 1（腰转）轴线上，Z 竖直向上，X、Y 水平（右手系）。
- 关节 1：绕 +Z 旋转 q1，无连杆（关节 1 与 2 同轴线上一点，无标注长度）。
- 关节 2–4：转轴均平行于 Y' = Z × X'，其中 X' 为水平面内由 q1 旋转得到的径向：
    X' = [cos(q1), sin(q1), 0]，Y' = [-sin(q1), cos(q1), 0]，竖直面由 (X', Z) 张成。
- 连杆：关节 2→3 长度 L1=220 mm；关节 3→4 长度 L2（默认 120 mm，可设 115–125 mm）。
- 关节 4 与 5 无连杆间隔：末端位置由关节 4 决定；q4 为绕 Y' 的转角，不改变末端点位置，
  只改变末端在竖直面内的朝向；关节 5 绕世界 +Z 再转 q5。

角度定义（弧度）
----------------------------------------
- q2：第一段连杆（L1）与 +X' 的夹角，从 +X' 转向 +Z 为正（在竖直面内的仰角）。
- q3：第二段连杆相对第一段的方向角增量（肘关节，与常见 2R 平面臂一致）。
- 初始“摆臂在 ZX 面内与 Z、X 均成 45°”对应可取 q2 = π/4、q3 = 0（第一段沿 (X'+Z)/√2）。

末端位置（关节 4 / TCP 无工具偏置时）
----------------------------------------
  p = L1*(cos(q2) X' + sin(q2) Z) + L2*(cos(q2+q3) X' + sin(q2+q3) Z)

依赖：numpy（ROS Noetic 下通常已装 python3-numpy）
"""
from __future__ import print_function

import math
from typing import List, Optional, Tuple

import numpy as np


def _clamp(x: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, x))


class ArmKinematics5R:
    """5 关节模型：位置由 (q1,q2,q3) 决定；q4 为平面内末端朝向相对 (q2+q3) 的增量；q5 为绕 Z 的腕转。"""

    def __init__(
        self,
        L1: float = 0.220,
        L2: float = 0.120,
        joint_limit_rad: Optional[np.ndarray] = None,
        joint_offset_rad: Optional[np.ndarray] = None,
    ):
        """
        :param L1: 关节 2→3 连杆长度 (m)
        :param L2: 关节 3→4 连杆长度 (m)
        :param joint_limit_rad: shape (5,2) 每关节 [lower, upper]；默认每关节 ±90°（总行程 180°）
        :param joint_offset_rad: shape (5,) 固件零位与模型零位之差，FK 用 q_model = q_cmd + offset
        """
        self.L1 = float(L1)
        self.L2 = float(L2)
        if joint_limit_rad is None:
            lim = math.pi / 2.0
            self.joint_limit_rad = np.array(
                [[-lim, lim]] * 5, dtype=float
            )
        else:
            self.joint_limit_rad = np.asarray(joint_limit_rad, dtype=float).reshape(5, 2)
        self.joint_offset_rad = (
            np.zeros(5, dtype=float)
            if joint_offset_rad is None
            else np.asarray(joint_offset_rad, dtype=float).reshape(5)
        )

    def _q_model(self, q_cmd: np.ndarray) -> np.ndarray:
        return np.asarray(q_cmd, dtype=float).reshape(5) + self.joint_offset_rad

    def _q_cmd(self, q_model: np.ndarray) -> np.ndarray:
        return np.asarray(q_model, dtype=float).reshape(5) - self.joint_offset_rad

    def axis_yaw(self, q1: float) -> np.ndarray:
        """世界系中竖直轴单位向量（+Z）。"""
        return np.array([0.0, 0.0, 1.0], dtype=float)

    def axis_pitch_plane(self, q1: float) -> np.ndarray:
        """竖直面内关节轴在世界系中的单位向量（Y'）。"""
        return np.array([-math.sin(q1), math.cos(q1), 0.0], dtype=float)

    def radial_horizontal(self, q1: float) -> np.ndarray:
        """竖直面内的水平径向 X'（单位）。"""
        return np.array([math.cos(q1), math.sin(q1), 0.0], dtype=float)

    def fk_position(self, q: np.ndarray) -> np.ndarray:
        """末端位置 (3,) 世界系 m；q 为命令角（会加 offset）。"""
        qm = self._q_model(q)
        q1, q2, q3 = qm[0], qm[1], qm[2]
        xp = self.radial_horizontal(q1)
        zv = np.array([0.0, 0.0, 1.0], dtype=float)
        c2, s2 = math.cos(q2), math.sin(q2)
        ca, sa = math.cos(q2 + q3), math.sin(q2 + q3)
        return self.L1 * (c2 * xp + s2 * zv) + self.L2 * (ca * xp + sa * zv)

    def fk(self, q: np.ndarray) -> Tuple[np.ndarray, np.ndarray, np.ndarray]:
        """
        正运动学。
        :return: (p_world(3,), z_tool_world(3,) 末端接近方向在竖直面内由 q2+q3+q4 决定后在水平面再 q5,
                 简化给出末端系 z 轴近似为竖直面内连杆方向经 q5 绕 Z 旋转)
        """
        qm = self._q_model(q)
        q1, q2, q3, q4, q5 = qm
        p = self.fk_position(q)
        xp = self.radial_horizontal(q1)
        zv = np.array([0.0, 0.0, 1.0], dtype=float)
        ang = q2 + q3 + q4
        x_tool_in_plane = math.cos(ang) * xp + math.sin(ang) * zv
        # 将平面内“前向”绕 Z 旋转 q5（与关节 5 同轴）
        c5, s5 = math.cos(q5), math.sin(q5)
        R_z = np.array([[c5, -s5, 0.0], [s5, c5, 0.0], [0.0, 0.0, 1.0]], dtype=float)
        x_tool = R_z @ x_tool_in_plane
        # 末端 z 轴：取与 x_tool 垂直且在竖直面内的方向（简化夹爪法向朝上分量）
        y_pitch = self.axis_pitch_plane(q1)
        z_tool = np.cross(x_tool, y_pitch)
        zn = np.linalg.norm(z_tool)
        if zn < 1e-9:
            z_tool = zv.copy()
        else:
            z_tool /= zn
        return p, x_tool / (np.linalg.norm(x_tool) + 1e-12), z_tool

    def ik_position(
        self,
        p_des: np.ndarray,
        elbow_sign: int = 1,
    ) -> Optional[Tuple[float, float, float]]:
        """
        仅根据末端位置求 (q1, q2, q3)（模型角）。q4,q5 不影响位置。
        :param p_des: (3,) 世界系 m
        :param elbow_sign: +1 或 -1 选择肘部两种解
        :return: (q1,q2,q3) 或 None（不可达）
        """
        p = np.asarray(p_des, dtype=float).reshape(3)
        q1 = math.atan2(p[1], p[0])
        x_p = math.hypot(p[0], p[1])
        z_p = p[2]
        r = math.hypot(x_p, z_p)
        if r < 1e-9:
            return None
        c3 = (r * r - self.L1 * self.L1 - self.L2 * self.L2) / (2.0 * self.L1 * self.L2)
        c3 = _clamp(c3, -1.0, 1.0)
        s3 = elbow_sign * math.sqrt(max(0.0, 1.0 - c3 * c3))
        q3 = math.atan2(s3, c3)
        q2 = math.atan2(z_p, x_p) - math.atan2(
            self.L2 * math.sin(q3), self.L1 + self.L2 * math.cos(q3)
        )
        return (q1, q2, q3)

    def ik_full(
        self,
        p_des: np.ndarray,
        angle_in_plane_rad: float,
        q5_des: float = 0.0,
        elbow_sign: int = 1,
    ) -> Optional[np.ndarray]:
        """
        位置 + 竖直面内末端朝向角 + 腕转 q5。
        :param angle_in_plane_rad: 末端在 (X',Z) 平面内与 +X' 的夹角（从 X' 转向 Z 为正），即 q2+q3+q4
        :param q5_des: 关节 5 模型角
        """
        sol = self.ik_position(p_des, elbow_sign=elbow_sign)
        if sol is None:
            return None
        q1, q2, q3 = sol
        q4 = angle_in_plane_rad - (q2 + q3)
        q_model = np.array([q1, q2, q3, q4, q5_des], dtype=float)
        if not self._in_limits(q_model):
            return None
        return self._q_cmd(q_model)

    def _in_limits(self, q_model: np.ndarray) -> bool:
        for i in range(5):
            lo, hi = self.joint_limit_rad[i]
            if not (lo - 1e-9 <= q_model[i] <= hi + 1e-9):
                return False
        return True

    def ik_solutions(
        self,
        p_des: np.ndarray,
        angle_in_plane_rad: Optional[float] = None,
        q5_des: float = 0.0,
    ) -> List[np.ndarray]:
        """枚举肘部两种解；若给定平面朝向则补 q4。"""
        out: List[np.ndarray] = []
        for sgn in (+1, -1):
            sol = self.ik_position(p_des, elbow_sign=sgn)
            if sol is None:
                continue
            q1, q2, q3 = sol
            if angle_in_plane_rad is None:
                q4 = 0.0
            else:
                q4 = angle_in_plane_rad - (q2 + q3)
            qm = np.array([q1, q2, q3, q4, q5_des], dtype=float)
            if self._in_limits(qm):
                out.append(self._q_cmd(qm))
        return out


def _demo():
    arm = ArmKinematics5R(L1=0.220, L2=0.120)
    # 初始姿态：q2=45°, q3=0
    q_home = np.deg2rad([0.0, 45.0, 0.0, 0.0, 0.0])
    p0 = arm.fk_position(q_home)
    print("FK home p (m):", p0)
    sols = arm.ik_solutions(p0, angle_in_plane_rad=np.deg2rad(45.0), q5_des=0.0)
    print("IK solutions (cmd q, rad):", len(sols))
    for s in sols:
        err = np.linalg.norm(arm.fk_position(s) - p0)
        print("  q:", s, "pos err:", err)


if __name__ == "__main__":
    _demo()
