/** @file src/core/chassis/control.cpp
 *  @brief 底盘控制接口实现。
 */
#include "core/chassis/control.h"

#include "core/chassis/pid.h"
#include "driver/motor.h"

ChassisMotionControl::ChassisMotionControl()
    : maxLinearMps_(0.80f),
      maxAngularRadps_(2.50f),
      maxLinearAccelMps2_(0.8f),
      maxAngularAccelRadps2_(3.0f),
      updateIntervalMs_(20),
      commandTimeoutMs_(350),
      lastUpdateMs_(0),
      lastCommandMs_(0),
      target_{0.0f, 0.0f, 0.0f},
      current_{0.0f, 0.0f, 0.0f},
      motorDriver_(nullptr),
      wheelSpeedPid_(nullptr),
      forceOutput_(true),
      active_(false),
      safetyBlocked_(false) {}

/** @brief 绑定底盘控制输出依赖，使 public 命令接口可以独立完成驱动板操作。 */
void ChassisMotionControl::attachDriver(IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid) {
    motorDriver_ = &motorDriver;
    wheelSpeedPid_ = &wheelSpeedPid;
}

/** @brief 把物理几何和命令缩放参数传给运动学模型。 */
void ChassisMotionControl::configureKinematics(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale) {
    kinematics_.configure(wheelBaseRadiusM, wheelRadiusM, maxWheelOmegaRadps, wheelCommandScale);
}

/** @brief 设置底盘整体速度上限，防止上位机命令超过本机能力。 */
void ChassisMotionControl::configureLimits(float maxLinearMps, float maxAngularRadps) {
    maxLinearMps_ = maxLinearMps;
    maxAngularRadps_ = maxAngularRadps;
}

/** @brief 设置加速度平滑参数和固定输出周期。 */
void ChassisMotionControl::configureSmoothing(float maxLinearAccelMps2, float maxAngularAccelRadps2, uint16_t updateIntervalMs) {
    maxLinearAccelMps2_ = maxLinearAccelMps2;
    maxAngularAccelRadps2_ = maxAngularAccelRadps2;
    updateIntervalMs_ = updateIntervalMs;
}

/** @brief 设置 CHASSIS 命令超时时间，避免上位机失联后继续运动。 */
void ChassisMotionControl::configureCommandTimeout(uint32_t commandTimeoutMs) {
    commandTimeoutMs_ = commandTimeoutMs;
}

/** @brief 写入限幅后的底盘目标速度。 */
void ChassisMotionControl::setTargetTwist(const ChassisTwist& twist) {
    target_.vxMps = clampFloat(twist.vxMps, -maxLinearMps_, maxLinearMps_);
    target_.vyMps = clampFloat(twist.vyMps, -maxLinearMps_, maxLinearMps_);
    target_.wzRadps = clampFloat(twist.wzRadps, -maxAngularRadps_, maxAngularRadps_);
    forceOutput_ = true;
}

/** @brief 写入目标速度并记录命令时间，用于周期输出和超时判断。 */
void ChassisMotionControl::setTargetTwist(const ChassisTwist& twist, uint32_t nowMs) {
    setTargetTwist(twist);
    lastCommandMs_ = nowMs;
    active_ = true;
}

/** @brief 处理上位机 CHASSIS 命令，安全阻挡期间拒绝新的运动目标。 */
ChassisCommandResult ChassisMotionControl::setTargetTwistCommand(const ChassisTwist& twist, uint32_t nowMs) {
    if (safetyBlocked_) {
        return kChassisCommandRejectedSafety;
    }

    setTargetTwist(twist, nowMs);
    return kChassisCommandOk;
}

/** @brief 清零目标和当前执行速度，确保下一次输出为停止状态。 */
void ChassisMotionControl::stop() {
    target_.vxMps = 0.0f;
    target_.vyMps = 0.0f;
    target_.wzRadps = 0.0f;
    current_ = target_;
    forceOutput_ = true;
}

/** @brief 停止速度控制状态并清除 PID 历史误差。 */
void ChassisMotionControl::stopMotion(WheelSpeedPid& wheelSpeedPid) {
    stop();
    wheelSpeedPid.reset();
    active_ = false;
}

/** @brief 直接下发三轮手动速度，并停用 CHASSIS 周期控制。 */
ChassisCommandResult ChassisMotionControl::setManualWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2) {
    if (safetyBlocked_) {
        stopDriver();
        return kChassisCommandRejectedSafety;
    }
    if (motorDriver_ == nullptr || wheelSpeedPid_ == nullptr) {
        return kChassisCommandNotReady;
    }

    if (!motorDriver_->setWheelSpeed(wheel0, wheel1, wheel2)) {
        return kChassisCommandDriverError;
    }
    wheelSpeedPid_->reset();
    deactivate();
    return kChassisCommandOk;
}

/** @brief 清零控制状态并向驱动板发送 stop 命令。 */
ChassisCommandResult ChassisMotionControl::stopDriver() {
    if (motorDriver_ == nullptr || wheelSpeedPid_ == nullptr) {
        return kChassisCommandNotReady;
    }

    stopMotion(*wheelSpeedPid_);
    if (!motorDriver_->stop()) {
        return kChassisCommandDriverError;
    }
    return kChassisCommandOk;
}

/** @brief 进入安全阻挡状态并立即停止驱动板输出。 */
ChassisCommandResult ChassisMotionControl::stopForSafety() {
    safetyBlocked_ = true;
    return stopDriver();
}

/** @brief 设置驱动板使能状态。 */
ChassisCommandResult ChassisMotionControl::setDriverEnabled(bool enabled) {
    if (motorDriver_ == nullptr) {
        return kChassisCommandNotReady;
    }
    if (!motorDriver_->setEnabled(enabled)) {
        return kChassisCommandDriverError;
    }
    return kChassisCommandOk;
}

/** @brief 读取驱动板反馈速度、使能状态和故障码。 */
bool ChassisMotionControl::readDriverStatus(HallMotorStatus& status) {
    if (motorDriver_ == nullptr) {
        return false;
    }
    return motorDriver_->readStatus(status);
}

/** @brief 设置安全阻挡标志，用于拒绝后续 CHASSIS/MOTOR 运动命令。 */
void ChassisMotionControl::setSafetyBlocked(bool blocked) {
    safetyBlocked_ = blocked;
}

/** @brief 返回最近一次 IIC 驱动板错误码。 */
uint8_t ChassisMotionControl::lastDriverError() const {
    return motorDriver_ == nullptr ? 0xFF : motorDriver_->lastError();
}

/** @brief 停用周期输出，通常用于手动 MOTOR 命令接管后。 */
void ChassisMotionControl::deactivate() {
    active_ = false;
}

/** @brief 重置速度平滑时间基准，避免初次更新使用异常时间间隔。 */
void ChassisMotionControl::reset(uint32_t nowMs) {
    current_ = target_;
    lastUpdateMs_ = nowMs;
    forceOutput_ = true;
}

ChassisTwist ChassisMotionControl::targetTwist() const {
    return target_;
}

ChassisTwist ChassisMotionControl::currentTwist() const {
    return current_;
}

/** @brief 按固定周期推进速度斜坡，并生成可发送给驱动板的三轮命令。 */
bool ChassisMotionControl::update(uint32_t nowMs, ChassisWheelCommand& output) {
    if (!forceOutput_ && nowMs - lastUpdateMs_ < updateIntervalMs_) {
        return false;
    }

    const uint32_t elapsedMs = lastUpdateMs_ == 0 ? updateIntervalMs_ : nowMs - lastUpdateMs_;  ///< 本次速度斜坡积分使用的时间间隔。
    lastUpdateMs_ = nowMs;
    forceOutput_ = false;

    const float dt = elapsedMs / 1000.0f;  ///< 斜坡计算使用的秒级时间步长。
    current_.vxMps = approachFloat(current_.vxMps, target_.vxMps, maxLinearAccelMps2_ * dt);
    current_.vyMps = approachFloat(current_.vyMps, target_.vyMps, maxLinearAccelMps2_ * dt);
    current_.wzRadps = approachFloat(current_.wzRadps, target_.wzRadps, maxAngularAccelRadps2_ * dt);

    kinematics_.wheelCommandFromTwist(current_, output);
    return true;
}

/** @brief 执行底盘周期输出，包括命令超时、PID 修正和 IIC 轮速写入。 */
ChassisMotionUpdateResult ChassisMotionControl::updateDriver(uint32_t nowMs, IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid) {
    if (safetyBlocked_) {
        return kChassisMotionNoOutput;
    }

    if (active_ && nowMs - lastCommandMs_ > commandTimeoutMs_) {
        stopMotion(wheelSpeedPid);
        return kChassisMotionTimedOut;
    }

    if (!active_) {
        return kChassisMotionNoOutput;
    }

    ChassisWheelCommand wheelCommand;  ///< 运动学输出的三轮目标命令和对应物理角速度。
    if (!update(nowMs, wheelCommand)) {
        return kChassisMotionNoOutput;
    }

    int16_t command[WheelSpeedPid::kWheelCount] = {wheelCommand.command[0], wheelCommand.command[1], wheelCommand.command[2]};  ///< 实际发送给驱动板的三轮命令，可能被 PID 修正。
    HallMotorStatus status;  ///< 驱动板回传的反馈速度、使能状态和故障码。
    if (motorDriver.readStatus(status)) {
        wheelSpeedPid.update(wheelCommand.command, status.wheelSpeed, nowMs, command);
    }

    if (!motorDriver.setWheelSpeed(command[0], command[1], command[2])) {
        return kChassisMotionDriverError;
    }
    return kChassisMotionOutputSent;
}

/** @brief 使用已绑定的驱动板和 PID 执行周期输出。 */
ChassisMotionUpdateResult ChassisMotionControl::updateDriver(uint32_t nowMs) {
    if (motorDriver_ == nullptr || wheelSpeedPid_ == nullptr) {
        return kChassisMotionDriverError;
    }
    return updateDriver(nowMs, *motorDriver_, *wheelSpeedPid_);
}

bool ChassisMotionControl::active() const {
    return active_;
}

bool ChassisMotionControl::safetyBlocked() const {
    return safetyBlocked_;
}

/** @brief 将浮点值限制在闭区间内。 */
float ChassisMotionControl::clampFloat(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

/** @brief 按最大步长从当前值逼近目标值，用于速度斜坡。 */
float ChassisMotionControl::approachFloat(float current, float target, float maxDelta) {
    if (target > current + maxDelta) {
        return current + maxDelta;
    }
    if (target < current - maxDelta) {
        return current - maxDelta;
    }
    return target;
}
