/** @file safety.cpp
 *  @brief 超声波障碍物安全状态机实现。
 */
#include "core/sensor/ultrasonic/safety.h"

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

ObstacleSafetyResult ObstacleSafety::update(uint16_t rawDistanceMm) {
    ObstacleSafetyResult result {rawDistanceMm, medianDistanceMm_, filteredDistanceMm_, rawDistanceMm > 0, blocked_, kObstacleSafetyNone}; ///< 默认返回当前状态，无效读数不改变滤波器。
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

    const bool hardNear = rawDistanceMm <= hardStopDistanceMm_; ///< 原始距离已经极近，需要立即急停。
    const bool stableNear = medianDistanceMm_ <= stopDistanceMm_ || filteredDistanceMm_ <= stopDistanceMm_; ///< 滤波距离稳定处于停止阈值内。
    const bool stableClear = medianDistanceMm_ >= clearDistanceMm_ && filteredDistanceMm_ >= clearDistanceMm_; ///< 滤波距离稳定超过解除阈值。

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
        const uint16_t tmp = a; ///< 交换 a/b 时的临时变量。
        a = b;
        b = tmp;
    }
    if (b > c) {
        const uint16_t tmp = b; ///< 交换 b/c 时的临时变量。
        b = c;
        c = tmp;
    }
    if (a > b) {
        b = a;
    }
    return b;
}

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
