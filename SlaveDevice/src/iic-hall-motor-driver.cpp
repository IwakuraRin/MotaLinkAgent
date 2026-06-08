/*
|--------------------------------------------------------------------------
| IIC 霍尔电机驱动板通信实现
|--------------------------------------------------------------------------
| 使用命令字 + 小端 payload 的紧凑协议，把三轮速度和急停命令发给 IIC 驱动板。
|--------------------------------------------------------------------------
*/
#include "iic-hall-motor-driver.h"

// ==================== 初始化 ====================
// 作用：启动 Wire 总线并设置驱动板地址，默认使用标准 100kHz IIC 频率。
// =================================================
IICHallMotorDriver::IICHallMotorDriver()
    : address_(kDefaultAddress), lastError_(0) {}

void IICHallMotorDriver::init(uint8_t address, uint32_t clockHz) {
    address_ = address;
    lastError_ = 0;
    Wire.begin();
    Wire.setClock(clockHz);
}

// ==================== 控制命令 ====================
// 作用：发送使能、三轮速度和急停命令，速度单位由 IIC 驱动板固件约定。
// ==================================================
bool IICHallMotorDriver::setEnabled(bool enabled) {
    const uint8_t payload[1] = { static_cast<uint8_t>(enabled ? 1 : 0) };  // 使能命令 payload：1 为使能，0 为关闭。
    return writeCommand(kCmdSetEnabled, payload, sizeof(payload));
}

bool IICHallMotorDriver::setWheelSpeed(int16_t wheel0, int16_t wheel1, int16_t wheel2) {
    uint8_t payload[6];  // 三个 int16 轮速命令的小端字节序 payload。
    writeInt16LE(payload, 0, wheel0);
    writeInt16LE(payload, 2, wheel1);
    writeInt16LE(payload, 4, wheel2);
    return writeCommand(kCmdSetWheelSpeed, payload, sizeof(payload));
}

bool IICHallMotorDriver::stop() {
    return writeCommand(kCmdStop, nullptr, 0);
}

// ==================== 状态读取 ====================
// 作用：读取驱动板状态，协议为三路 int16 反馈速度 + enabled + faultCode。
// ==================================================
bool IICHallMotorDriver::readStatus(HallMotorStatus& status) {
    if (!writeCommand(kCmdReadStatus, nullptr, 0, false)) {
        return false;
    }

    const uint8_t received = Wire.requestFrom(address_, kStatusPayloadSize);  // 本次从驱动板实际读到的状态字节数。
    if (received != kStatusPayloadSize) {
        lastError_ = 0xFE;
        while (Wire.available() > 0) {
            Wire.read();
        }
        return false;
    }

    uint8_t payload[kStatusPayloadSize];  // 驱动板状态原始 payload 缓冲区。
    for (uint8_t i = 0; i < kStatusPayloadSize; ++i) {
        payload[i] = static_cast<uint8_t>(Wire.read());
    }

    status.wheelSpeed[0] = readInt16LE(payload[0], payload[1]);
    status.wheelSpeed[1] = readInt16LE(payload[2], payload[3]);
    status.wheelSpeed[2] = readInt16LE(payload[4], payload[5]);
    status.enabled = payload[6];
    status.faultCode = payload[7];
    lastError_ = 0;
    return true;
}

uint8_t IICHallMotorDriver::lastError() const {
    return lastError_;
}

// ==================== 底层 IIC 写入 ====================
// 作用：统一封装 IIC 命令帧，首字节为命令号，后续为小端 payload。
// =======================================================
bool IICHallMotorDriver::writeCommand(uint8_t command, const uint8_t* payload, uint8_t payloadSize, bool sendStop) {
    Wire.beginTransmission(address_);
    Wire.write(command);
    for (uint8_t i = 0; i < payloadSize; ++i) {
        Wire.write(payload[i]);
    }
    lastError_ = Wire.endTransmission(sendStop);
    return lastError_ == 0;
}

void IICHallMotorDriver::writeInt16LE(uint8_t* buffer, uint8_t index, int16_t value) {
    const uint16_t raw = static_cast<uint16_t>(value);  // 保留 int16 二进制补码表示，用于拆成小端字节。
    buffer[index] = static_cast<uint8_t>(raw & 0xFF);
    buffer[index + 1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
}

int16_t IICHallMotorDriver::readInt16LE(uint8_t lowByte, uint8_t highByte) {
    const uint16_t raw = static_cast<uint16_t>(lowByte) | (static_cast<uint16_t>(highByte) << 8);  // 小端两字节合成原始 16 位值。
    return static_cast<int16_t>(raw);
}
