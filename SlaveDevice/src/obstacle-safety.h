/*
|--------------------------------------------------------------------------
| 超声波障碍物安全状态机
|--------------------------------------------------------------------------
| 负责把 HC-SR04 原始距离过滤成稳定距离，并判断是否进入本地下位机急停。
| 只使用整数和少量状态，适合 ATmega2560 的内存与实时性限制。
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
// 作用：返回原始距离、过滤距离、阻挡状态和状态变化事件。
// ======================================================
struct ObstacleSafetyResult {
    uint16_t rawDistanceMm;
    uint16_t filteredDistanceMm;
    bool valid;
    bool blocked;
    ObstacleSafetyEvent event;
};

// ==================== 障碍物安全状态机 ====================
// 作用：用停止阈值、解除阈值和连续计数实现灵敏但不抖动的急停判断。
// ==========================================================
class ObstacleSafety {
public:
    ObstacleSafety();

    void configure(uint16_t stopDistanceMm, uint16_t clearDistanceMm, uint8_t stopSamples, uint8_t clearSamples);
    ObstacleSafetyResult update(uint16_t rawDistanceMm);
    bool blocked() const;
    uint16_t filteredDistanceMm() const;
    uint16_t stopDistanceMm() const;
    uint16_t clearDistanceMm() const;

private:
    uint16_t stopDistanceMm_;
    uint16_t clearDistanceMm_;
    uint8_t stopSamples_;
    uint8_t clearSamples_;
    uint8_t nearCount_;
    uint8_t clearCount_;
    uint16_t filteredDistanceMm_;
    bool hasFilter_;
    bool blocked_;
};

#endif
