/** @file control.h
 *  @brief 底盘速度控制器，负责目标速度限幅、加速度平滑、PID 修正和驱动板输出。
 */
#ifndef CORE_CHASSIS_CONTROL_H
#define CORE_CHASSIS_CONTROL_H

#include <Arduino.h>

#include "core/chassis/kinematics.h"

class IICHallMotorDriver;
struct HallMotorStatus;
class WheelSpeedPid;

/** @brief 一次底盘周期输出的结果。 */
enum ChassisMotionUpdateResult : uint8_t {
    kChassisMotionNoOutput = 0,    ///< 本周期无需输出，可能未激活或未到控制周期。
    kChassisMotionOutputSent = 1,  ///< 本周期已经向 IIC 驱动板写入三轮速度。
    kChassisMotionDriverError = 2, ///< 本周期写入 IIC 驱动板失败。
    kChassisMotionTimedOut = 3     ///< CHASSIS 命令超时，底盘自动停止。
};

/** @brief 上位机或安全逻辑触发的底盘命令执行结果。 */
enum ChassisCommandResult : uint8_t {
    kChassisCommandOk = 0,             ///< 命令已执行。
    kChassisCommandRejectedSafety = 1, ///< 当前安全阻挡，不允许输出运动命令。
    kChassisCommandDriverError = 2,    ///< 驱动板通信失败。
    kChassisCommandNotReady = 3        ///< 尚未绑定驱动板或 PID。
};

/** @brief 把离散串口目标速度变成平滑的三轮驱动板命令。 */
class ChassisMotionControl {
public:
    ChassisMotionControl();

    /**
     * @brief 绑定实际输出轮速的驱动板和用于反馈修正的 PID 控制器。
     * @param motorDriver IIC 三轮电机驱动板接口。
     * @param wheelSpeedPid 三轮速度外环 PID。
     */
    void attachDriver(IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid);

    /**
     * @brief 配置三轮底盘几何参数和轮速命令缩放比例。
     * @param wheelBaseRadiusM 底盘中心到轮接地点距离，单位 m。
     * @param wheelRadiusM 全向轮半径，单位 m。
     * @param maxWheelOmegaRadps 单轮最大目标角速度，单位 rad/s。
     * @param wheelCommandScale 轮角速度 rad/s 转驱动板命令值的比例。
     */
    void configureKinematics(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale);

    /**
     * @brief 配置上位机底盘速度命令的安全限幅。
     * @param maxLinearMps vx/vy 最大线速度，单位 m/s。
     * @param maxAngularRadps wz 最大角速度，单位 rad/s。
     */
    void configureLimits(float maxLinearMps, float maxAngularRadps);

    /**
     * @brief 配置速度斜坡上限和底盘控制输出周期。
     * @param maxLinearAccelMps2 vx/vy 加速度上限，单位 m/s^2。
     * @param maxAngularAccelRadps2 wz 角加速度上限，单位 rad/s^2。
     * @param updateIntervalMs 底盘控制输出周期，单位 ms。
     */
    void configureSmoothing(float maxLinearAccelMps2, float maxAngularAccelRadps2, uint16_t updateIntervalMs);

    /**
     * @brief 配置 CHASSIS 命令丢失后的自动停车超时时间。
     * @param commandTimeoutMs 超时时间，单位 ms。
     */
    void configureCommandTimeout(uint32_t commandTimeoutMs);

    /**
     * @brief 写入目标速度并做限幅，不改变命令时间戳或激活状态。
     * @param twist 目标底盘速度。
     */
    void setTargetTwist(const ChassisTwist& twist);

    /**
     * @brief 写入目标速度、刷新命令时间戳，并激活周期输出。
     * @param twist 目标底盘速度。
     * @param nowMs 当前 millis 时间戳。
     */
    void setTargetTwist(const ChassisTwist& twist, uint32_t nowMs);

    /** @brief 清零目标速度和当前速度，使下一次周期更新输出停止命令。 */
    void stop();

    /** @brief 停用周期底盘输出，用于手动轮速控制接管。 */
    void deactivate();

    /**
     * @brief 重置控制器时间基准，避免启动时产生异常斜坡积分。
     * @param nowMs 当前 millis 时间戳。
     */
    void reset(uint32_t nowMs);

    /**
     * @brief 推进速度斜坡并生成三轮命令，但不直接写入驱动板。
     * @param nowMs 当前 millis 时间戳。
     * @param output 写入生成的三轮命令。
     * @return 本周期生成了新输出返回 true。
     */
    bool update(uint32_t nowMs, ChassisWheelCommand& output);

    /**
     * @brief 执行完整周期输出，包括超时、PID 修正和驱动板写入。
     * @param nowMs 当前 millis 时间戳。
     * @return 周期输出结果。
     */
    ChassisMotionUpdateResult updateDriver(uint32_t nowMs);

    /**
     * @brief 处理 CHASSIS 命令；安全阻挡时拒绝运动目标。
     * @param twist 目标底盘速度。
     * @param nowMs 当前 millis 时间戳。
     * @return 命令执行结果。
     */
    ChassisCommandResult setTargetTwistCommand(const ChassisTwist& twist, uint32_t nowMs);

    /**
     * @brief 处理 MOTOR 命令；成功后停用周期底盘控制。
     * @param wheel0 0 号轮速度命令。
     * @param wheel1 1 号轮速度命令。
     * @param wheel2 2 号轮速度命令。
     * @return 命令执行结果。
     */
    ChassisCommandResult setManualWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2);

    /**
     * @brief 清零底盘控制状态并向驱动板发送 stop。
     * @return 命令执行结果。
     */
    ChassisCommandResult stopDriver();

    /**
     * @brief 进入安全阻挡状态并立即请求驱动板 stop。
     * @return 命令执行结果。
     */
    ChassisCommandResult stopForSafety();

    /**
     * @brief 设置驱动板使能状态。
     * @param enabled true 为使能，false 为关闭。
     * @return 命令执行结果。
     */
    ChassisCommandResult setDriverEnabled(bool enabled);

    /**
     * @brief 读取驱动板轮速、使能状态和故障码。
     * @param status 写入读取到的驱动板状态。
     * @return 读取成功返回 true。
     */
    bool readDriverStatus(HallMotorStatus& status);

    /**
     * @brief 设置本地安全阻挡标志，用于拦截后续运动命令。
     * @param blocked true 表示阻挡运动输出。
     */
    void setSafetyBlocked(bool blocked);

    /**
     * @brief 返回最近一次驱动板通信错误码；未绑定驱动板时返回 0xFF。
     * @return IIC 驱动错误码。
     */
    uint8_t lastDriverError() const;

    /**
     * @brief 当前是否允许周期性生成并下发轮速命令。
     * @return 激活周期输出返回 true。
     */
    bool active() const;

    /**
     * @brief 当前是否因本地安全状态阻挡运动输出。
     * @return 安全阻挡中返回 true。
     */
    bool safetyBlocked() const;

    /**
     * @brief 返回最近一次写入并限幅后的目标速度。
     * @return 当前目标底盘速度。
     */
    ChassisTwist targetTwist() const;

    /**
     * @brief 返回当前平滑后的执行速度。
     * @return 当前执行底盘速度。
     */
    ChassisTwist currentTwist() const;

private:
    OmniTriangleKinematics kinematics_; ///< 底层三轮运动学模型，负责把底盘速度转换成轮速。
    float maxLinearMps_;                ///< vx/vy 线速度上限，单位 m/s。
    float maxAngularRadps_;             ///< wz 角速度上限，单位 rad/s。
    float maxLinearAccelMps2_;          ///< vx/vy 加速度斜坡上限，单位 m/s^2。
    float maxAngularAccelRadps2_;       ///< wz 角加速度斜坡上限，单位 rad/s^2。
    uint16_t updateIntervalMs_;         ///< 控制器最小输出周期，单位 ms。
    uint32_t commandTimeoutMs_;         ///< 上位机底盘速度命令超时时间，单位 ms。
    uint32_t lastUpdateMs_;             ///< 上一次输出轮速命令的 millis 时间戳。
    uint32_t lastCommandMs_;            ///< 上一次接收有效 CHASSIS 命令的 millis 时间戳。
    ChassisTwist target_;               ///< 上位机最新目标速度，已经做过安全限幅。
    ChassisTwist current_;              ///< 平滑后的当前执行速度，用于生成轮速命令。
    IICHallMotorDriver* motorDriver_;   ///< 绑定的 IIC 三轮电机驱动板接口。
    WheelSpeedPid* wheelSpeedPid_;      ///< 绑定的三轮速度外环 PID。
    bool forceOutput_;                  ///< 强制下一次 update 输出，目标变化或 stop 后置位。
    bool active_;                       ///< 当前是否允许周期性生成并下发轮速命令。
    bool safetyBlocked_;                ///< 当前是否因本地安全状态禁止运动输出。

    /**
     * @brief 停止运动控制状态并重置 PID，不直接访问驱动板。
     * @param wheelSpeedPid 要重置的三轮速度 PID。
     */
    void stopMotion(WheelSpeedPid& wheelSpeedPid);

    /**
     * @brief 使用指定驱动板和 PID 执行周期输出，供绑定式 updateDriver 复用。
     * @param nowMs 当前 millis 时间戳。
     * @param motorDriver 实际输出轮速的驱动板接口。
     * @param wheelSpeedPid 用于反馈修正的 PID 控制器。
     * @return 周期输出结果。
     */
    ChassisMotionUpdateResult updateDriver(uint32_t nowMs, IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid);

    /**
     * @brief 将浮点值限制在闭区间内。
     * @param value 输入值。
     * @param low 下限。
     * @param high 上限。
     * @return 限幅后的值。
     */
    static float clampFloat(float value, float low, float high);

    /**
     * @brief 按最大步长从当前值逼近目标值。
     * @param current 当前值。
     * @param target 目标值。
     * @param maxDelta 单次允许变化的最大绝对值。
     * @return 逼近后的值。
     */
    static float approachFloat(float current, float target, float maxDelta);
};

#endif
