/*
|--------------------------------------------------------------------------
| 三全向轮底盘运动学
|--------------------------------------------------------------------------
| 只负责底盘速度和三轮速度之间的数学转换，不处理串口、平滑或 IIC 输出。
|--------------------------------------------------------------------------
*/
#ifndef CHASSIS_KINEMATICS_H
#define CHASSIS_KINEMATICS_H

#include <Arduino.h>

// ==================== 底盘速度 ====================
// 作用：保存机体系整体速度，vx 向前、vy 向左、wz 绕中心逆时针。
// ==================================================
struct ChassisTwist {
    float vxMps;    // 车体 X 轴线速度，单位 m/s，正方向为前进。
    float vyMps;    // 车体 Y 轴线速度，单位 m/s，正方向为左移。
    float wzRadps;  // 车体 Z 轴角速度，单位 rad/s，正方向为逆时针。
};

// ==================== 三轮速度命令 ====================
// 作用：同时保存驱动板 int16 命令值和对应的物理角速度。
// ======================================================
struct ChassisWheelCommand {
    int16_t command[3];    // 发给 IIC 驱动板的三轮目标值，顺序与驱动板协议一致。
    float omegaRadps[3];   // 三轮物理角速度，单位 rad/s，用于调试和状态输出。
};

// ==================== 三角全向轮运动学 ====================
// 作用：根据三轮安装角、底盘半径和轮半径执行逆运动学和正运动学。
// ==========================================================
class OmniTriangleKinematics {
public:
    static const uint8_t kWheelCount = 3;  // 底盘固定为三颗全向轮。

    OmniTriangleKinematics();

    void configure(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale);
    void wheelCommandFromTwist(const ChassisTwist& twist, ChassisWheelCommand& output) const;
    void twistFromWheelCommand(const int16_t wheelCommand[kWheelCount], ChassisTwist& twist) const;

private:
    float wheelBaseRadiusM_;     // 底盘中心到轮接地点距离，单位 m。
    float wheelRadiusM_;         // 全向轮半径，单位 m。
    float maxWheelOmegaRadps_;   // 单轮最大允许角速度，单位 rad/s，用于等比例限幅。
    float wheelCommandScale_;    // rad/s 到驱动板 int16 命令值的比例。

    void wheelOmegaFromTwist(const ChassisTwist& twist, float omega[kWheelCount]) const;
    static float clampFloat(float value, float low, float high);
};

#endif
