/*
|--------------------------------------------------------------------------
| HC-SR04 超声波测距实现
|--------------------------------------------------------------------------
| 触发 10us 脉冲并读取 Echo 高电平时间，使用整数公式换算毫米距离。
|--------------------------------------------------------------------------
*/
#include "SR04.h"

// ==================== 初始化 ====================
// 作用：配置 Trig/Echo 引脚，默认 18ms 超时约对应 3m，有利于提高主循环响应。
// =================================================
SR04Sensor::SR04Sensor()
    : triggerPin_(0), echoPin_(0), timeoutUs_(18000UL) {}

void SR04Sensor::init(uint8_t triggerPin, uint8_t echoPin, uint32_t timeoutUs) {
    triggerPin_ = triggerPin;
    echoPin_ = echoPin;
    timeoutUs_ = timeoutUs;

    pinMode(triggerPin_, OUTPUT);
    pinMode(echoPin_, INPUT);
    digitalWrite(triggerPin_, LOW);
}

// ==================== 距离读取 ====================
// 作用：读取单次距离，声音速度按 343m/s 计算，除以 2 得到单程距离。
// ==================================================
uint16_t SR04Sensor::readDistanceMm() const {
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    const uint32_t durationUs = pulseIn(echoPin_, HIGH, timeoutUs_);  // Echo 高电平持续时间，单位 us，0 表示超时。
    if (durationUs == 0) {
        return kInvalidDistanceMm;
    }

    const uint32_t distanceMm = (durationUs * 343UL) / 2000UL;  // 按声速 343m/s 换算出的单程距离，单位 mm。
    if (distanceMm > UINT16_MAX) {
        return UINT16_MAX;
    }
    return static_cast<uint16_t>(distanceMm);
}
