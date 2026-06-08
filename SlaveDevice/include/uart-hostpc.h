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
    static const uint8_t kLineBufferSize = 96;  // 单行 UART 命令最大缓存长度，含结尾 0。

    explicit UARTHostPC(HardwareSerial& port = Serial);

    void init(uint32_t baudRate = 115200UL);
    bool readLine(char* output, uint8_t outputSize);
    void sendLine(const char* data);
    void sendLine(const __FlashStringHelper* data);
    bool overflowed() const;
    void clearOverflow();

private:
    HardwareSerial* port_;             // 实际使用的 Arduino 硬件串口对象。
    char rxBuffer_[kLineBufferSize];   // 正在接收但尚未遇到换行符的命令缓冲区。
    uint8_t rxLength_;                 // rxBuffer_ 当前已写入的字符数量。
    bool overflowed_;                  // 上一条命令是否超过缓冲区并触发溢出。
    bool droppingLine_;                // 溢出后是否正在丢弃当前行直到换行符。
};

#endif
