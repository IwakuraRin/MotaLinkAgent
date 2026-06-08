/*
|--------------------------------------------------------------------------
| IIC 霍尔电机驱动板通信模块
|--------------------------------------------------------------------------
| 封装 ATmega2560 到 IIC 霍尔电机驱动板的 IIC 通信，控制三个全向轮。
|--------------------------------------------------------------------------
*/
#ifndef IIC_HALL_MOTOR_DRIVER_H
#define IIC_HALL_MOTOR_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

// ==================== 驱动板状态 ====================
// 作用：保存从 IIC 驱动板读取到的三轮反馈速度和故障状态。
// ====================================================
struct HallMotorStatus {
    int16_t wheelSpeed[3];  // 驱动板回传的三轮反馈速度，顺序与 setWheelSpeed 参数一致。
    uint8_t enabled;        // 驱动板使能状态，0 表示关闭，非 0 表示使能。
    uint8_t faultCode;      // 驱动板故障码，具体含义由驱动板固件协议定义。
};

// ==================== IIC 霍尔电机驱动 ====================
// 作用：通过紧凑二进制协议向 IIC 驱动板发送三轮速度和使能命令。
// ==========================================================
class IICHallMotorDriver {
public:
    static const uint8_t kDefaultAddress = 0x10;          // 默认 IIC 从机地址。
    static const uint32_t kDefaultClockHz = 100000UL;     // 默认 IIC 时钟频率，单位 Hz。

    IICHallMotorDriver();

    void init(uint8_t address = kDefaultAddress, uint32_t clockHz = kDefaultClockHz);
    bool setEnabled(bool enabled);
    bool setWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2);
    bool stop();
    bool readStatus(HallMotorStatus& status);
    uint8_t lastError() const;

private:
    static const uint8_t kCmdSetEnabled = 0x01;      // 协议命令字：设置驱动板使能。
    static const uint8_t kCmdSetWheelSpeed = 0x02;   // 协议命令字：设置三轮速度。
    static const uint8_t kCmdStop = 0x03;            // 协议命令字：立即停止三轮输出。
    static const uint8_t kCmdReadStatus = 0x10;      // 协议命令字：读取驱动板状态。
    static const uint8_t kStatusPayloadSize = 8;     // 状态 payload 字节数：3 个 int16 速度 + enabled + faultCode。

    uint8_t address_;     // 当前通信使用的 IIC 从机地址。
    uint8_t lastError_;   // 最近一次 Wire.endTransmission 返回的错误码，0 表示成功。

    bool writeCommand(uint8_t command, const uint8_t* payload, uint8_t payloadSize, bool sendStop = true);
    static void writeInt16LE(uint8_t* buffer, uint8_t index, int16_t value);
    static int16_t readInt16LE(uint8_t lowByte, uint8_t highByte);
};

#endif
