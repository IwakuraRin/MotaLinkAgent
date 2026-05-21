/*
|--------------------------------------------------------------------------
| 超声波障碍物安全状态机
|--------------------------------------------------------------------------
| 负责把 HC-SR04 原始距离过滤成稳定距离，并判断是否进入本地下位机急停。
| 使用 3 点中值滤波、整数低通、迟滞阈值和极近距离立即急停，适合 ATmega2560。
|--------------------------------------------------------------------------
*/
#ifndef OBSTACLE_SAFETY_H
#define OBSTACLE_SAFETY_H

#include <Arduino.h>
#include <stdint.h>

// ==================== 安全事件类型 ====================
// 作用：描述本次测距更新是否触发急停或解除急停。
// ======================================================
enum ObstacleSafetyEvent : uint8_t {
    kObstacleSafetyNone = 0,
    kObstacleSafetyStop = 1,
    kObstacleSafetyClear = 2
};

// ==================== 安全更新结果 ====================
// 作用：返回原始距离、中值距离、过滤距离、阻挡状态和状态变化事件。
// ======================================================
struct ObstacleSafetyResult {
    uint16_t rawDistanceMm;
    uint16_t medianDistanceMm;
    uint16_t filteredDistanceMm;
    bool valid;
    bool blocked;
    ObstacleSafetyEvent event;
};

// ==================== 障碍物安全状态机 ====================
// 作用：用滤波、停止阈值、解除阈值和连续计数实现灵敏但不抖动的急停判断。
// ==========================================================
class ObstacleSafety {
public:
    ObstacleSafety();

    void configure(
        uint16_t stopDistanceMm,
        uint16_t clearDistanceMm,
        uint8_t stopSamples,
        uint8_t clearSamples,
        uint16_t hardStopDistanceMm);
    ObstacleSafetyResult update(uint16_t rawDistanceMm);
    bool blocked() const;
    uint16_t filteredDistanceMm() const;
    uint16_t medianDistanceMm() const;
    uint16_t stopDistanceMm() const;
    uint16_t clearDistanceMm() const;
    uint16_t hardStopDistanceMm() const;

private:
    static const uint8_t kMedianSampleCount = 3;

    uint16_t stopDistanceMm_;
    uint16_t clearDistanceMm_;
    uint16_t hardStopDistanceMm_;
    uint8_t stopSamples_;
    uint8_t clearSamples_;
    uint8_t nearCount_;
    uint8_t clearCount_;
    uint16_t samples_[kMedianSampleCount];
    uint8_t sampleCount_;
    uint8_t sampleIndex_;
    uint16_t medianDistanceMm_;
    uint16_t filteredDistanceMm_;
    bool hasFilter_;
    bool blocked_;

    uint16_t pushSampleAndMedian(uint16_t rawDistanceMm);
    static uint16_t median3(uint16_t a, uint16_t b, uint16_t c);
};

#endif
