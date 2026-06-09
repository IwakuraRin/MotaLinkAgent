/** @file pid.cpp
 *  @brief 三轮霍尔反馈速度 PID 修正实现。
 */
#include "core/chassis/pid.h"

#include <math.h>
#include <stdlib.h>

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

void WheelSpeedPid::update(const int16_t target[kWheelCount], const int16_t feedback[kWheelCount], uint32_t nowMs, int16_t output[kWheelCount]) {
    const float dt = hasLast_ && nowMs > lastUpdateMs_ ? (nowMs - lastUpdateMs_) / 1000.0f : 0.02f; ///< PID 时间步长，首次按 20ms 控制周期估算。
    lastUpdateMs_ = nowMs;
    hasLast_ = true;

    for (uint8_t i = 0; i < kWheelCount; ++i) {
        float error = static_cast<float>(target[i] - feedback[i]); ///< 目标轮速命令与霍尔反馈轮速命令的差值。
        if (abs(static_cast<int>(error)) <= config_.deadband) {
            error = 0.0f;
        }

        integral_[i] += error * dt;
        integral_[i] = clampFloat(integral_[i], -config_.integralLimit, config_.integralLimit);

        const float derivative = dt > 1.0e-6f ? (error - lastError_[i]) / dt : 0.0f; ///< 误差变化率，用于 D 项。
        lastError_[i] = error;

        float correction = config_.kp * error + config_.ki * integral_[i] + config_.kd * derivative; ///< PID 对目标命令的附加修正量。
        correction = clampFloat(correction, -config_.maxCorrection, config_.maxCorrection);
        output[i] = clampCommand(static_cast<long>(target[i]) + static_cast<long>(round(correction)));
    }
}

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
