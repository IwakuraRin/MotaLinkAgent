/** @file uart.h
 *  @brief 上位机 UART 行协议收发封装。
 */
#ifndef DRIVER_UART_H
#define DRIVER_UART_H

#include <Arduino.h>

/** @brief 使用固定缓冲区非阻塞收发上位机文本命令。 */
class UARTHostPC {
public:
    static const uint8_t kLineBufferSize = 96; ///< 单行 UART 命令最大缓存长度，含结尾 0。

    /**
     * @brief 绑定实际使用的硬件串口对象。
     * @param port Arduino 硬件串口实例。
     */
    explicit UARTHostPC(HardwareSerial& port = Serial);

    /**
     * @brief 初始化串口和接收状态。
     * @param baudRate 串口波特率。
     */
    void init(uint32_t baudRate = 115200UL);
    /**
     * @brief 非阻塞读取一条换行结尾的命令。
     * @param output 输出命令行的缓冲区。
     * @param outputSize 输出缓冲区长度。
     * @return 读到完整命令行返回 true，否则返回 false。
     */
    bool readLine(char* output, uint8_t outputSize);
    /**
     * @brief 发送 RAM 中的普通字符串并追加换行。
     * @param data 要发送的空结尾字符串。
     */
    void sendLine(const char* data);
    /**
     * @brief 发送 Flash 字符串并追加换行。
     * @param data 要发送的 Flash 字符串。
     */
    void sendLine(const __FlashStringHelper* data);
    /**
     * @brief 上一条输入是否因为超过缓冲区而被丢弃。
     * @return 发生过输入溢出返回 true。
     */
    bool overflowed() const;
    /** @brief 清除输入溢出标志。 */
    void clearOverflow();

private:
    HardwareSerial* port_;           ///< 实际使用的 Arduino 硬件串口对象。
    char rxBuffer_[kLineBufferSize]; ///< 正在接收但尚未遇到换行符的命令缓冲区。
    uint8_t rxLength_;               ///< rxBuffer_ 当前已写入的字符数量。
    bool overflowed_;                ///< 上一条命令是否超过缓冲区并触发溢出。
    bool droppingLine_;              ///< 溢出后是否正在丢弃当前行直到换行符。
};

#endif
