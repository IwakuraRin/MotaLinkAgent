/*
|--------------------------------------------------------------------------
| HC-SR04 超声波测距模块
|--------------------------------------------------------------------------
| 封装 Trig/Echo 引脚和毫米级整数距离读取，为本地急停和上位机建图提供距离。
|--------------------------------------------------------------------------
*/
#ifndef SR04_H
#define SR04_H

#include <Arduino.h>
#include <stdint.h>

// ==================== HC-SR04 测距模块 ====================
// 作用：管理 Trig/Echo 引脚和单次测距，返回 0 表示超时或无效读数。
// ==========================================================
class SR04Sensor {
public:
    static const uint16_t kInvalidDistanceMm = 0;  // 无效距离返回值，表示 Echo 超时或没有可靠回波。

    SR04Sensor();

    void init(uint8_t triggerPin, uint8_t echoPin, uint32_t timeoutUs = 18000UL);
    uint16_t readDistanceMm() const;

private:
    uint8_t triggerPin_;  // Trig 输出引脚，用于发出 10us 触发脉冲。
    uint8_t echoPin_;     // Echo 输入引脚，用于测量回波高电平宽度。
    uint32_t timeoutUs_;  // pulseIn 等待 Echo 的超时时间，单位 us。
};

#endif
