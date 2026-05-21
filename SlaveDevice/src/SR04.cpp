// 作用：实现 HC-SR04 超声波模块的触发、回波读取和距离换算。
#include "SR04.h"

// ==================== 初始化 ====================
// 作用：配置 Trig/Echo 引脚，默认 25ms 超时约对应 4m 有效测距范围。
// =================================================
SR04Sensor::SR04Sensor()
    : triggerPin_(0), echoPin_(0), timeoutUs_(25000UL) {}

void SR04Sensor::init(uint8_t triggerPin, uint8_t echoPin, uint32_t timeoutUs) {
    triggerPin_ = triggerPin;
    echoPin_ = echoPin;
    timeoutUs_ = timeoutUs;

    pinMode(triggerPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
    digitalWrite(triggerPin_, LOW);
}

// ==================== 距离读取 ====================
// 作用：发出 10us 触发脉冲并读取 Echo 高电平时间，使用整数公式换算毫米距离。
// ==================================================
uint16_t SR04Sensor::readDistanceMm() const {
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    const uint32_t durationUs = pulseIn(echoPin_, HIGH, timeoutUs_);
    if (durationUs == 0) {
        return kInvalidDistanceMm;
    }

    const uint32_t distanceMm = (durationUs * 343UL) / 2000UL;
    if (distanceMm > UINT16_MAX) {
        return UINT16_MAX;
    }
    return static_cast<uint16_t>(distanceMm);
}
