/*
|--------------------------------------------------------------------------
| 超声波障碍物安全状态机实现
|--------------------------------------------------------------------------
| 中值滤波抗单点跳变，整数低通平滑距离，迟滞阈值防抖，极近距离立即急停。
|--------------------------------------------------------------------------
*/
#include "obstacle-safety.h"

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
    ObstacleSafetyResult result {rawDistanceMm, medianDistanceMm_, filteredDistanceMm_, rawDistanceMm > 0, blocked_, kObstacleSafetyNone};
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

    const bool hardNear = rawDistanceMm <= hardStopDistanceMm_;
    const bool stableNear = medianDistanceMm_ <= stopDistanceMm_ || filteredDistanceMm_ <= stopDistanceMm_;
    const bool stableClear = medianDistanceMm_ >= clearDistanceMm_ && filteredDistanceMm_ >= clearDistanceMm_;

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
        const uint16_t tmp = a;
        a = b;
        b = tmp;
    }
    if (b > c) {
        const uint16_t tmp = b;
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
