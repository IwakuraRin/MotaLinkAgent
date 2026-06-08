/*
|--------------------------------------------------------------------------
| 超声波障碍物安全状态机实现
|--------------------------------------------------------------------------
| 中值滤波抗单点跳变，整数低通平滑距离，迟滞阈值防抖，极近距离立即急停。
|--------------------------------------------------------------------------
*/
#include "obstacle-safety.h"

#include <stdio.h>

// ==================== 初始化 ====================
// 作用：设置默认急停阈值，hard=160mm，stop=260mm，clear=360mm。
// =================================================
ObstacleSafety::ObstacleSafety()
    : stopDistanceMm_(260),
      clearDistanceMm_(360),
      hardStopDistanceMm_(160),
      stopSamples_(2),
      clearSamples_(4),
      nearCount_(0),
      clearCount_(0),
      samples_{0, 0, 0},
      sampleCount_(0),
      sampleIndex_(0),
      medianDistanceMm_(0),
      filteredDistanceMm_(0),
      hasFilter_(false),
      blocked_(false) {}

void ObstacleSafety::configure(
    uint16_t stopDistanceMm,
    uint16_t clearDistanceMm,
    uint8_t stopSamples,
    uint8_t clearSamples,
    uint16_t hardStopDistanceMm) {
    stopDistanceMm_ = stopDistanceMm;
    clearDistanceMm_ = clearDistanceMm > stopDistanceMm ? clearDistanceMm : static_cast<uint16_t>(stopDistanceMm + 80);
    hardStopDistanceMm_ = hardStopDistanceMm < stopDistanceMm ? hardStopDistanceMm : static_cast<uint16_t>(stopDistanceMm / 2U);
    stopSamples_ = stopSamples == 0 ? 1 : stopSamples;
    clearSamples_ = clearSamples == 0 ? 1 : clearSamples;
}

// ==================== 状态更新 ====================
// 作用：输入一次原始测距，输出过滤后的距离和急停状态变化。
// ==================================================
ObstacleSafetyResult ObstacleSafety::update(uint16_t rawDistanceMm) {
    ObstacleSafetyResult result {rawDistanceMm, medianDistanceMm_, filteredDistanceMm_, rawDistanceMm > 0, blocked_, kObstacleSafetyNone};  // 默认返回当前状态，无效读数不改变滤波器。
    if (!result.valid) {
        return result;
    }

    medianDistanceMm_ = pushSampleAndMedian(rawDistanceMm);
    if (!hasFilter_) {
        filteredDistanceMm_ = medianDistanceMm_;
        hasFilter_ = true;
    } else {
        filteredDistanceMm_ = static_cast<uint16_t>((static_cast<uint32_t>(filteredDistanceMm_) * 3U + medianDistanceMm_ + 2U) / 4U);
    }

    const bool hardNear = rawDistanceMm <= hardStopDistanceMm_;  // 原始距离已经极近，需要立即急停。
    const bool stableNear = medianDistanceMm_ <= stopDistanceMm_ || filteredDistanceMm_ <= stopDistanceMm_;  // 滤波距离稳定处于停止阈值内。
    const bool stableClear = medianDistanceMm_ >= clearDistanceMm_ && filteredDistanceMm_ >= clearDistanceMm_;  // 滤波距离稳定超过解除阈值。

    if (!blocked_) {
        if (hardNear) {
            nearCount_ = stopSamples_;
        } else if (stableNear) {
            if (nearCount_ < 255) {
                nearCount_ += 1;
            }
        } else {
            nearCount_ = 0;
        }

        if (nearCount_ >= stopSamples_) {
            blocked_ = true;
            clearCount_ = 0;
            result.event = kObstacleSafetyStop;
        }
    } else {
        if (stableClear) {
            if (clearCount_ < 255) {
                clearCount_ += 1;
            }
        } else {
            clearCount_ = 0;
        }

        if (clearCount_ >= clearSamples_) {
            blocked_ = false;
            nearCount_ = 0;
            result.event = kObstacleSafetyClear;
        }
    }

    result.medianDistanceMm = medianDistanceMm_;
    result.filteredDistanceMm = filteredDistanceMm_;
    result.blocked = blocked_;
    return result;
}

// ==================== 中值滤波 ====================
// 作用：保存最近 3 个有效距离，取中值抑制单次异常跳变。
// ==================================================
uint16_t ObstacleSafety::pushSampleAndMedian(uint16_t rawDistanceMm) {
    samples_[sampleIndex_] = rawDistanceMm;
    sampleIndex_ = static_cast<uint8_t>((sampleIndex_ + 1U) % kMedianSampleCount);
    if (sampleCount_ < kMedianSampleCount) {
        sampleCount_ += 1;
    }

    if (sampleCount_ == 1) {
        return samples_[0];
    }
    if (sampleCount_ == 2) {
        return static_cast<uint16_t>((static_cast<uint32_t>(samples_[0]) + samples_[1]) / 2U);
    }
    return median3(samples_[0], samples_[1], samples_[2]);
}

uint16_t ObstacleSafety::median3(uint16_t a, uint16_t b, uint16_t c) {
    if (a > b) {
        const uint16_t tmp = a;  // 交换 a/b 时的临时变量。
        a = b;
        b = tmp;
    }
    if (b > c) {
        const uint16_t tmp = b;  // 交换 b/c 时的临时变量。
        b = c;
        c = tmp;
    }
    if (a > b) {
        b = a;
    }
    return b;
}

// ==================== 状态查询 ====================
// 作用：提供主循环和命令处理所需的只读安全状态。
// ==================================================
bool ObstacleSafety::blocked() const {
    return blocked_;
}

uint16_t ObstacleSafety::filteredDistanceMm() const {
    return filteredDistanceMm_;
}

uint16_t ObstacleSafety::medianDistanceMm() const {
    return medianDistanceMm_;
}

uint16_t ObstacleSafety::stopDistanceMm() const {
    return stopDistanceMm_;
}

uint16_t ObstacleSafety::clearDistanceMm() const {
    return clearDistanceMm_;
}

uint16_t ObstacleSafety::hardStopDistanceMm() const {
    return hardStopDistanceMm_;
}

// ==================== 文本状态格式化 ====================
// 作用：把安全状态转换为上位机协议文本，主程序只负责发送。
// ========================================================
void ObstacleSafety::formatTelemetry(char* buffer, uint8_t bufferSize, uint16_t rawDistanceMm) const {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    snprintf(
        buffer,
        bufferSize,
        "TEL SR04 mm=%u raw=%u median=%u blocked=%u hard=%u stop=%u clear=%u",
        filteredDistanceMm_,
        rawDistanceMm,
        medianDistanceMm_,
        blocked_ ? 1 : 0,
        hardStopDistanceMm_,
        stopDistanceMm_,
        clearDistanceMm_);
}

void ObstacleSafety::formatEvent(char* buffer, uint8_t bufferSize, ObstacleSafetyEvent event) const {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    const char* state = event == kObstacleSafetyStop ? "STOP" : "CLEAR";  // 上报给上位机的障碍物事件状态文本。
    snprintf(buffer, bufferSize, "EVT OBSTACLE %s mm=%u", state, filteredDistanceMm_);
}

void ObstacleSafety::formatStatus(char* buffer, uint8_t bufferSize) const {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    snprintf(
        buffer,
        bufferSize,
        "OK SAFETY hard=%u stop=%u clear=%u blocked=%u mm=%u median=%u",
        hardStopDistanceMm_,
        stopDistanceMm_,
        clearDistanceMm_,
        blocked_ ? 1 : 0,
        filteredDistanceMm_,
        medianDistanceMm_);
}
