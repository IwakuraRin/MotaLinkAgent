/** @file include/driver/sr04.h
 *  @brief HC-SR04 驱动接口定义。
 */
#ifndef DRIVER_SR04_H
#define DRIVER_SR04_H

#include <Arduino.h>
#include <stdint.h>

/** @brief 管理 Trig/Echo 引脚并返回毫米级单次测距结果。 */
class SR04Sensor {
public:
    static const uint16_t kInvalidDistanceMm = 0; ///< 无效距离返回值，表示 Echo 超时或没有可靠回波。

    SR04Sensor();

    /**
     * @brief 配置 Trig/Echo 引脚和 pulseIn 超时时间。
     * @param triggerPin Trig 输出引脚。
     * @param echoPin Echo 输入引脚。
     * @param timeoutUs pulseIn 等待 Echo 的超时时间，单位 us。
     */
    void init(uint8_t triggerPin, uint8_t echoPin, uint32_t timeoutUs = 18000UL);
    /**
     * @brief 读取一次距离。
     * @return 毫米距离；0 表示超时或无效回波。
     */
    uint16_t readDistanceMm() const;

private:
    uint8_t triggerPin_; ///< Trig 输出引脚，用于发出 10us 触发脉冲。
    uint8_t echoPin_;    ///< Echo 输入引脚，用于测量回波高电平宽度。
    uint32_t timeoutUs_; ///< pulseIn 等待 Echo 的超时时间，单位 us。
};

#endif
