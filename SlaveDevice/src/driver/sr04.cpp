/** @file src/driver/sr04.cpp
 *  @brief HC-SR04 驱动接口实现。
 */
#include "driver/sr04.h"

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

uint16_t SR04Sensor::readDistanceMm() const {
    digitalWrite(triggerPin_, LOW);
    delayMicroseconds(2);
    digitalWrite(triggerPin_, HIGH);
    delayMicroseconds(10);
    digitalWrite(triggerPin_, LOW);

    const uint32_t durationUs = pulseIn(echoPin_, HIGH, timeoutUs_); ///< Echo 高电平持续时间，单位 us，0 表示超时。
    if (durationUs == 0) {
        return kInvalidDistanceMm;
    }

    const uint32_t distanceMm = (durationUs * 343UL) / 2000UL; ///< 按声速 343m/s 换算出的单程距离，单位 mm。
    if (distanceMm > UINT16_MAX) {
        return UINT16_MAX;
    }
    return static_cast<uint16_t>(distanceMm);
}
