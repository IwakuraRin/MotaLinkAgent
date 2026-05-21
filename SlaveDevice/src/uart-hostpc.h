/*
|--------------------------------------------------------------------------
| 上位机 UART 通信模块
|--------------------------------------------------------------------------
| 使用固定缓冲区收发文本命令，避免 ATmega2560 上的 String 内存碎片。
|--------------------------------------------------------------------------
*/
#ifndef UART_HOSTPC_H
#define UART_HOSTPC_H

#include <Arduino.h>

// ==================== 上位机 UART 通信 ====================
// 作用：提供非阻塞行读取、普通字符串发送和 Flash 字符串发送。
// ==========================================================
class UARTHostPC {
public:
    static const uint8_t kLineBufferSize = 96;

    explicit UARTHostPC(HardwareSerial& port = Serial);

    void init(uint32_t baudRate = 115200UL);
    bool readLine(char* output, uint8_t outputSize);
    void sendLine(const char* data);
    void sendLine(const __FlashStringHelper* data);
    bool overflowed() const;
    void clearOverflow();

private:
    HardwareSerial* port_;
    char rxBuffer_[kLineBufferSize];
    uint8_t rxLength_;
    bool overflowed_;
    bool droppingLine_;
};

#endif
