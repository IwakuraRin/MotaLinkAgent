/*
|--------------------------------------------------------------------------
| 三轮霍尔速度 PID 实现
|--------------------------------------------------------------------------
| 参考 GMega 编码器速度 PID 的保守比例修正方式，作为 IIC 驱动板速度命令外环。
|--------------------------------------------------------------------------
*/
#include "wheel-speed-pid.h"

#include <math.h>
#include <stdlib.h>

// ==================== 初始化 ====================
// 作用：默认使用三角全向轮示例中的速度 PID 参数 P=0.05, I=0, D=0。
// =================================================
WheelSpeedPid::WheelSpeedPid()
    : config_{0.05f, 0.0f, 0.0f, 300, 800, 2},
      integral_{0.0f, 0.0f, 0.0f},
      lastError_{0.0f, 0.0f, 0.0f},
      lastUpdateMs_(0),
      hasLast_(false) {}

void WheelSpeedPid::configure(const WheelSpeedPidConfig& config) {
    config_ = config;
}

void WheelSpeedPid::reset() {
    for (uint8_t i = 0; i < kWheelCount; ++i) {
        integral_[i] = 0.0f;
        lastError_[i] = 0.0f;
    }
    lastUpdateMs_ = 0;
    hasLast_ = false;
}

// ==================== 闭环更新 ====================
// 作用：输入目标和反馈命令单位轮速，输出增加 PID 修正后的驱动板命令。
// ==================================================
void WheelSpeedPid::update(const int16_t target[kWheelCount], const int16_t feedback[kWheelCount], uint32_t nowMs, int16_t output[kWheelCount]) {
    const float dt = hasLast_ && nowMs > lastUpdateMs_ ? (nowMs - lastUpdateMs_) / 1000.0f : 0.02f;  // PID 时间步长，首次按 20ms 控制周期估算。
    lastUpdateMs_ = nowMs;
    hasLast_ = true;

    for (uint8_t i = 0; i < kWheelCount; ++i) {
        float error = static_cast<float>(target[i] - feedback[i]);  // 目标轮速命令与霍尔反馈轮速命令的差值。
        if (abs(static_cast<int>(error)) <= config_.deadband) {
            error = 0.0f;
        }

        integral_[i] += error * dt;
        integral_[i] = clampFloat(integral_[i], -config_.integralLimit, config_.integralLimit);

        const float derivative = dt > 1.0e-6f ? (error - lastError_[i]) / dt : 0.0f;  // 误差变化率，用于 D 项。
        lastError_[i] = error;

        float correction = config_.kp * error + config_.ki * integral_[i] + config_.kd * derivative;  // PID 对目标命令的附加修正量。
        correction = clampFloat(correction, -config_.maxCorrection, config_.maxCorrection);
        output[i] = clampCommand(static_cast<long>(target[i]) + static_cast<long>(round(correction)));
    }
}

// ==================== 数学工具 ====================
// 作用：限制 PID 中间量和最终 int16 命令，避免积分或修正过大。
// ==================================================
float WheelSpeedPid::clampFloat(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

int16_t WheelSpeedPid::clampCommand(long value) {
    if (value > 32767L) {
        return 32767;
    }
    if (value < -32767L) {
        return -32767;
    }
    return static_cast<int16_t>(value);
}
