// 作用：封装 HC-SR04 超声波测距模块，提供毫米级整数距离读取，适合 ATmega2560 低内存环境。
#ifndef SR04_H
#define SR04_H

#include <Arduino.h>
#include <stdint.h>

// ==================== HC-SR04 测距模块 ====================
// 作用：管理 Trig/Echo 引脚和单次测距，返回 0 表示超时或无效读数。
// ==========================================================
class SR04Sensor {
public:
    static const uint16_t kInvalidDistanceMm = 0;

    SR04Sensor();

    void init(uint8_t triggerPin, uint8_t echoPin, uint32_t timeoutUs = 25000UL);
    uint16_t readDistanceMm() const;

private:
    uint8_t triggerPin_;
    uint8_t echoPin_;
    uint32_t timeoutUs_;
};

#endif
