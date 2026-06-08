/*
|--------------------------------------------------------------------------
| 底盘速度控制
|--------------------------------------------------------------------------
| 管理上位机给出的底盘目标速度，完成安全限幅、加速度平滑和周期轮速输出。
|--------------------------------------------------------------------------
*/
#ifndef CHASSIS_MOTION_CONTROL_H
#define CHASSIS_MOTION_CONTROL_H

#include <Arduino.h>

#include "chassis-kinematics.h"

class IICHallMotorDriver;
class WheelSpeedPid;

// ==================== 底盘输出结果 ====================
// 作用：描述一次底盘周期输出是否实际下发，或者是否遇到驱动板错误。
// ======================================================
enum ChassisMotionUpdateResult : uint8_t {
    kChassisMotionNoOutput = 0,      // 本周期无需输出，可能未激活或未到控制周期。
    kChassisMotionOutputSent = 1,    // 本周期已经向 IIC 驱动板写入三轮速度。
    kChassisMotionDriverError = 2,   // 本周期写入 IIC 驱动板失败。
    kChassisMotionTimedOut = 3       // CHASSIS 命令超时，底盘自动停止。
};

// ==================== 底盘速度控制器 ====================
// 作用：把离散串口目标速度变成平滑的三轮驱动板命令。
// ========================================================
class ChassisMotionControl {
public:
    ChassisMotionControl();

    void configureKinematics(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale);
    void configureLimits(float maxLinearMps, float maxAngularRadps);
    void configureSmoothing(float maxLinearAccelMps2, float maxAngularAccelRadps2, uint16_t updateIntervalMs);
    void configureCommandTimeout(uint32_t commandTimeoutMs);
    void setTargetTwist(const ChassisTwist& twist);
    void setTargetTwist(const ChassisTwist& twist, uint32_t nowMs);
    void stop();
    void stopMotion(WheelSpeedPid& wheelSpeedPid);
    void deactivate();
    void reset(uint32_t nowMs);
    bool update(uint32_t nowMs, ChassisWheelCommand& output);
    ChassisMotionUpdateResult updateDriver(uint32_t nowMs, IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid);

    bool active() const;
    ChassisTwist targetTwist() const;
    ChassisTwist currentTwist() const;

private:
    OmniTriangleKinematics kinematics_;  // 底层三轮运动学模型，负责把底盘速度转换成轮速。
    float maxLinearMps_;                 // vx/vy 线速度上限，单位 m/s。
    float maxAngularRadps_;              // wz 角速度上限，单位 rad/s。
    float maxLinearAccelMps2_;           // vx/vy 加速度斜坡上限，单位 m/s^2。
    float maxAngularAccelRadps2_;        // wz 角加速度斜坡上限，单位 rad/s^2。
    uint16_t updateIntervalMs_;          // 控制器最小输出周期，单位 ms。
    uint32_t commandTimeoutMs_;          // 上位机底盘速度命令超时时间，单位 ms。
    uint32_t lastUpdateMs_;              // 上一次输出轮速命令的 millis 时间戳。
    uint32_t lastCommandMs_;             // 上一次接收有效 CHASSIS 命令的 millis 时间戳。
    ChassisTwist target_;                // 上位机最新目标速度，已经做过安全限幅。
    ChassisTwist current_;               // 平滑后的当前执行速度，用于生成轮速命令。
    bool forceOutput_;                   // 强制下一次 update 输出，目标变化或 stop 后置位。
    bool active_;                        // 当前是否允许周期性生成并下发轮速命令。

    static float clampFloat(float value, float low, float high);
    static float approachFloat(float current, float target, float maxDelta);
};

#endif
