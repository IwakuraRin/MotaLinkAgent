/** @file include/driver/motor.h
 *  @brief IIC 电机驱动接口定义。
 */
#ifndef DRIVER_MOTOR_H
#define DRIVER_MOTOR_H

#include <Arduino.h>
#include <Wire.h>

/** @brief 从 IIC 驱动板读取到的三轮反馈速度和故障状态。 */
struct HallMotorStatus {
    int16_t wheelSpeed[3]; ///< 三轮反馈速度，顺序与 setWheelSpeed 参数一致。
    uint8_t enabled;       ///< 驱动板使能状态，0 表示关闭，非 0 表示使能。
    uint8_t faultCode;     ///< 驱动板故障码，具体含义由驱动板固件协议定义。
};

/** @brief 通过紧凑二进制协议向 IIC 驱动板发送速度、使能和状态读取命令。 */
class IICHallMotorDriver {
public:
    static const uint8_t kDefaultAddress = 0x10;      ///< 默认 IIC 从机地址。
    static const uint32_t kDefaultClockHz = 100000UL; ///< 默认 IIC 时钟频率，单位 Hz。

    IICHallMotorDriver();

    /**
     * @brief 初始化 Wire 总线并设置驱动板地址和 IIC 时钟。
     * @param address IIC 从机地址。
     * @param clockHz IIC 总线频率，单位 Hz。
     */
    void init(uint8_t address = kDefaultAddress, uint32_t clockHz = kDefaultClockHz);
    /**
     * @brief 设置驱动板使能状态。
     * @param enabled true 为使能，false 为关闭。
     * @return IIC 命令写入成功返回 true。
     */
    bool setEnabled(bool enabled);
    /**
     * @brief 设置三轮速度命令。
     * @param wheel0 0 号轮速度命令。
     * @param wheel1 1 号轮速度命令。
     * @param wheel2 2 号轮速度命令。
     * @return IIC 命令写入成功返回 true。
     */
    bool setWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2);
    /**
     * @brief 请求驱动板立即停止三轮输出。
     * @return IIC 命令写入成功返回 true。
     */
    bool stop();
    /**
     * @brief 读取驱动板反馈速度、使能状态和故障码。
     * @param status 写入读取到的状态。
     * @return 状态读取成功返回 true。
     */
    bool readStatus(HallMotorStatus& status);
    /**
     * @brief 返回最近一次 Wire.endTransmission 的错误码。
     * @return Wire 错误码，0 表示成功。
     */
    uint8_t lastError() const;

private:
    static const uint8_t kCmdSetEnabled = 0x01;    ///< 协议命令字：设置驱动板使能。
    static const uint8_t kCmdSetWheelSpeed = 0x02; ///< 协议命令字：设置三轮速度。
    static const uint8_t kCmdStop = 0x03;          ///< 协议命令字：立即停止三轮输出。
    static const uint8_t kCmdReadStatus = 0x10;    ///< 协议命令字：读取驱动板状态。
    static const uint8_t kStatusPayloadSize = 8;   ///< 状态 payload 字节数：3 个 int16 速度 + enabled + faultCode。

    uint8_t address_;   ///< 当前通信使用的 IIC 从机地址。
    uint8_t lastError_; ///< 最近一次 Wire.endTransmission 返回的错误码，0 表示成功。

    /**
     * @brief 写入一帧 IIC 命令，首字节为命令号，后续为 payload。
     * @param command 协议命令字。
     * @param payload 命令 payload，可为 nullptr。
     * @param payloadSize payload 字节数。
     * @param sendStop 是否在 endTransmission 时发送 stop 条件。
     * @return IIC 写入成功返回 true。
     */
    bool writeCommand(uint8_t command, const uint8_t* payload, uint8_t payloadSize, bool sendStop = true);
    /**
     * @brief 将 int16 写入小端字节缓冲区。
     * @param buffer 目标缓冲区。
     * @param index 写入起始下标。
     * @param value 要写入的 int16 值。
     */
    static void writeInt16LE(uint8_t* buffer, uint8_t index, int16_t value);
    /**
     * @brief 从两个小端字节还原 int16。
     * @param lowByte 低字节。
     * @param highByte 高字节。
     * @return 还原后的 int16 值。
     */
    static int16_t readInt16LE(uint8_t lowByte, uint8_t highByte);
};

#endif
