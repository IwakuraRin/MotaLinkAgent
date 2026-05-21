// 作用：封装 ATmega2560 到 32F103 霍尔电机驱动板的 IIC 通信，控制三个全向轮速度。
#ifndef IIC_HALL_MOTOR_DRIVER_H
#define IIC_HALL_MOTOR_DRIVER_H

#include <Arduino.h>
#include <Wire.h>

// ==================== 驱动板状态 ====================
// 作用：保存从 32F103 驱动板读取到的三轮反馈速度和故障状态。
// ====================================================
struct HallMotorStatus {
    int16_t wheelSpeed[3];
    uint8_t enabled;
    uint8_t faultCode;
};

// ==================== IIC 霍尔电机驱动 ====================
// 作用：通过紧凑二进制协议向 32F103 驱动板发送三轮速度和使能命令。
// ==========================================================
class IICHallMotorDriver {
public:
    static const uint8_t kDefaultAddress = 0x10;
    static const uint32_t kDefaultClockHz = 100000UL;

    IICHallMotorDriver();

    void init(uint8_t address = kDefaultAddress, uint32_t clockHz = kDefaultClockHz);
    bool setEnabled(bool enabled);
    bool setWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2);
    bool stop();
    bool readStatus(HallMotorStatus& status);
    uint8_t lastError() const;

private:
    static const uint8_t kCmdSetEnabled = 0x01;
    static const uint8_t kCmdSetWheelSpeed = 0x02;
    static const uint8_t kCmdStop = 0x03;
    static const uint8_t kCmdReadStatus = 0x10;
    static const uint8_t kStatusPayloadSize = 8;

    uint8_t address_;
    uint8_t lastError_;

    bool writeCommand(uint8_t command, const uint8_t* payload, uint8_t payloadSize, bool sendStop = true);
    static void writeInt16LE(uint8_t* buffer, uint8_t index, int16_t value);
    static int16_t readInt16LE(uint8_t lowByte, uint8_t highByte);
};

#endif
