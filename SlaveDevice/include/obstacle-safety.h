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
    kObstacleSafetyNone = 0,   // 本次更新没有发生安全状态变化。
    kObstacleSafetyStop = 1,   // 本次更新触发进入障碍物急停状态。
    kObstacleSafetyClear = 2   // 本次更新触发解除障碍物急停状态。
};

// ==================== 安全更新结果 ====================
// 作用：返回原始距离、中值距离、过滤距离、阻挡状态和状态变化事件。
// ======================================================
struct ObstacleSafetyResult {
    uint16_t rawDistanceMm;       // 本次输入的 HC-SR04 原始距离，单位 mm。
    uint16_t medianDistanceMm;    // 最近 3 次有效距离的中值结果，单位 mm。
    uint16_t filteredDistanceMm;  // 低通滤波后的稳定距离，单位 mm。
    bool valid;                   // 本次原始距离是否有效，0 距离视为无效。
    bool blocked;                 // 更新后的障碍物阻挡状态。
    ObstacleSafetyEvent event;    // 本次更新产生的状态变化事件。
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
    void formatTelemetry(char* buffer, uint8_t bufferSize, uint16_t rawDistanceMm) const;
    void formatEvent(char* buffer, uint8_t bufferSize, ObstacleSafetyEvent event) const;
    void formatStatus(char* buffer, uint8_t bufferSize) const;

private:
    static const uint8_t kMedianSampleCount = 3;  // 中值滤波窗口大小。

    uint16_t stopDistanceMm_;       // 进入阻挡状态的距离阈值，单位 mm。
    uint16_t clearDistanceMm_;      // 解除阻挡状态的距离阈值，单位 mm。
    uint16_t hardStopDistanceMm_;   // 立即急停的原始距离阈值，单位 mm。
    uint8_t stopSamples_;           // 稳定近距离需要连续满足的采样次数。
    uint8_t clearSamples_;          // 稳定远距离需要连续满足的采样次数。
    uint8_t nearCount_;             // 当前连续近距离采样计数。
    uint8_t clearCount_;            // 当前连续远距离采样计数。
    uint16_t samples_[kMedianSampleCount];  // 最近有效距离的环形采样窗口，单位 mm。
    uint8_t sampleCount_;           // 采样窗口内已有的有效样本数量。
    uint8_t sampleIndex_;           // 下一次写入 samples_ 的环形下标。
    uint16_t medianDistanceMm_;     // 最近一次中值滤波结果，单位 mm。
    uint16_t filteredDistanceMm_;   // 最近一次低通滤波结果，单位 mm。
    bool hasFilter_;                // 是否已经有可用的低通滤波初值。
    bool blocked_;                  // 当前是否处于障碍物急停阻挡状态。

    uint16_t pushSampleAndMedian(uint16_t rawDistanceMm);
    static uint16_t median3(uint16_t a, uint16_t b, uint16_t c);
};

#endif
