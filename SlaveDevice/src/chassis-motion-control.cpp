/*
|--------------------------------------------------------------------------
| 底盘速度控制实现
|--------------------------------------------------------------------------
| 将目标底盘速度限幅并按加速度斜坡平滑，再调用运动学模块生成三轮速度命令。
|--------------------------------------------------------------------------
*/
#include "chassis-motion-control.h"

#include "iic-hall-motor-driver.h"
#include "wheel-speed-pid.h"

// ==================== 初始化 ====================
// 作用：设置默认速度上限、加速度上限和控制周期。
// =================================================
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
      forceOutput_(true),
      active_(false) {}

void ChassisMotionControl::configureKinematics(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale) {
    kinematics_.configure(wheelBaseRadiusM, wheelRadiusM, maxWheelOmegaRadps, wheelCommandScale);
}

void ChassisMotionControl::configureLimits(float maxLinearMps, float maxAngularRadps) {
    maxLinearMps_ = maxLinearMps;
    maxAngularRadps_ = maxAngularRadps;
}

void ChassisMotionControl::configureSmoothing(float maxLinearAccelMps2, float maxAngularAccelRadps2, uint16_t updateIntervalMs) {
    maxLinearAccelMps2_ = maxLinearAccelMps2;
    maxAngularAccelRadps2_ = maxAngularAccelRadps2;
    updateIntervalMs_ = updateIntervalMs;
}

void ChassisMotionControl::configureCommandTimeout(uint32_t commandTimeoutMs) {
    commandTimeoutMs_ = commandTimeoutMs;
}

// ==================== 目标速度 ====================
// 作用：接收上位机底盘整体速度，并立即限幅到本地下位机允许范围。
// ==================================================
void ChassisMotionControl::setTargetTwist(const ChassisTwist& twist) {
    target_.vxMps = clampFloat(twist.vxMps, -maxLinearMps_, maxLinearMps_);
    target_.vyMps = clampFloat(twist.vyMps, -maxLinearMps_, maxLinearMps_);
    target_.wzRadps = clampFloat(twist.wzRadps, -maxAngularRadps_, maxAngularRadps_);
    forceOutput_ = true;
}

void ChassisMotionControl::setTargetTwist(const ChassisTwist& twist, uint32_t nowMs) {
    setTargetTwist(twist);
    lastCommandMs_ = nowMs;
    active_ = true;
}

void ChassisMotionControl::stop() {
    target_.vxMps = 0.0f;
    target_.vyMps = 0.0f;
    target_.wzRadps = 0.0f;
    current_ = target_;
    forceOutput_ = true;
}

void ChassisMotionControl::stopMotion(WheelSpeedPid& wheelSpeedPid) {
    stop();
    wheelSpeedPid.reset();
    active_ = false;
}

void ChassisMotionControl::deactivate() {
    active_ = false;
}

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

// ==================== 周期更新 ====================
// 作用：按固定周期推进速度斜坡，输出可直接发送给 IIC 驱动板的三轮命令。
// ==================================================
bool ChassisMotionControl::update(uint32_t nowMs, ChassisWheelCommand& output) {
    if (!forceOutput_ && nowMs - lastUpdateMs_ < updateIntervalMs_) {
        return false;
    }

    const uint32_t elapsedMs = lastUpdateMs_ == 0 ? updateIntervalMs_ : nowMs - lastUpdateMs_;  // 本次速度斜坡积分使用的时间间隔。
    lastUpdateMs_ = nowMs;
    forceOutput_ = false;

    const float dt = elapsedMs / 1000.0f;  // 斜坡计算使用的秒级时间步长。
    current_.vxMps = approachFloat(current_.vxMps, target_.vxMps, maxLinearAccelMps2_ * dt);
    current_.vyMps = approachFloat(current_.vyMps, target_.vyMps, maxLinearAccelMps2_ * dt);
    current_.wzRadps = approachFloat(current_.wzRadps, target_.wzRadps, maxAngularAccelRadps2_ * dt);

    kinematics_.wheelCommandFromTwist(current_, output);
    return true;
}

// ==================== 驱动板输出 ====================
// 作用：维护 CHASSIS 命令超时、速度平滑、霍尔反馈 PID 修正和 IIC 轮速写入。
// ====================================================
ChassisMotionUpdateResult ChassisMotionControl::updateDriver(uint32_t nowMs, IICHallMotorDriver& motorDriver, WheelSpeedPid& wheelSpeedPid) {
    if (active_ && nowMs - lastCommandMs_ > commandTimeoutMs_) {
        stopMotion(wheelSpeedPid);
        return kChassisMotionTimedOut;
    }

    if (!active_) {
        return kChassisMotionNoOutput;
    }

    ChassisWheelCommand wheelCommand;  // 运动学输出的三轮目标命令和对应物理角速度。
    if (!update(nowMs, wheelCommand)) {
        return kChassisMotionNoOutput;
    }

    int16_t command[WheelSpeedPid::kWheelCount] = {wheelCommand.command[0], wheelCommand.command[1], wheelCommand.command[2]};  // 实际发送给驱动板的三轮命令，可能被 PID 修正。
    HallMotorStatus status;  // 驱动板回传的反馈速度、使能状态和故障码。
    if (motorDriver.readStatus(status)) {
        wheelSpeedPid.update(wheelCommand.command, status.wheelSpeed, nowMs, command);
    }

    if (!motorDriver.setWheelSpeed(command[0], command[1], command[2])) {
        return kChassisMotionDriverError;
    }
    return kChassisMotionOutputSent;
}

bool ChassisMotionControl::active() const {
    return active_;
}

// ==================== 数学工具 ====================
// 作用：提供限幅和斜坡逼近函数，让速度变化连续可控。
// ==================================================
float ChassisMotionControl::clampFloat(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

float ChassisMotionControl::approachFloat(float current, float target, float maxDelta) {
    if (target > current + maxDelta) {
        return current + maxDelta;
    }
    if (target < current - maxDelta) {
        return current - maxDelta;
    }
    return target;
}
