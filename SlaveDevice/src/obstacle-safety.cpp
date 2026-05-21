/*
|--------------------------------------------------------------------------
| 超声波障碍物安全状态机实现
|--------------------------------------------------------------------------
| 使用二分之一权重的整数低通滤波和迟滞阈值：靠近时快速停，离开后稳定解除。
|--------------------------------------------------------------------------
*/
#include "obstacle-safety.h"

// ==================== 初始化 ====================
// 作用：设置默认急停阈值，stop=260mm，clear=360mm，约 50ms 内可触发急停。
// =================================================
ObstacleSafety::ObstacleSafety()
    : stopDistanceMm_(260),
      clearDistanceMm_(360),
      stopSamples_(2),
      clearSamples_(4),
      nearCount_(0),
      clearCount_(0),
      filteredDistanceMm_(0),
      hasFilter_(false),
      blocked_(false) {}

void ObstacleSafety::configure(uint16_t stopDistanceMm, uint16_t clearDistanceMm, uint8_t stopSamples, uint8_t clearSamples) {
    stopDistanceMm_ = stopDistanceMm;
    clearDistanceMm_ = clearDistanceMm > stopDistanceMm ? clearDistanceMm : static_cast<uint16_t>(stopDistanceMm + 80);
    stopSamples_ = stopSamples == 0 ? 1 : stopSamples;
    clearSamples_ = clearSamples == 0 ? 1 : clearSamples;
}

// ==================== 状态更新 ====================
// 作用：输入一次原始测距，输出过滤后的距离和急停状态变化。
// ==================================================
ObstacleSafetyResult ObstacleSafety::update(uint16_t rawDistanceMm) {
    ObstacleSafetyResult result {rawDistanceMm, filteredDistanceMm_, rawDistanceMm > 0, blocked_, kObstacleSafetyNone};
    if (!result.valid) {
        return result;
    }

    if (!hasFilter_) {
        filteredDistanceMm_ = rawDistanceMm;
        hasFilter_ = true;
    } else {
        filteredDistanceMm_ = static_cast<uint16_t>((static_cast<uint32_t>(filteredDistanceMm_) + rawDistanceMm) / 2U);
    }

    if (!blocked_) {
        if (filteredDistanceMm_ <= stopDistanceMm_) {
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
        if (filteredDistanceMm_ >= clearDistanceMm_) {
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

    result.filteredDistanceMm = filteredDistanceMm_;
    result.blocked = blocked_;
    return result;
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

uint16_t ObstacleSafety::stopDistanceMm() const {
    return stopDistanceMm_;
}

uint16_t ObstacleSafety::clearDistanceMm() const {
    return clearDistanceMm_;
}
