// 作用：实现上位机串口通信，非阻塞读取一行命令并按行返回结果。
#include "uart-hostpc.h"
#include <string.h>

// ==================== 初始化 ====================
// 作用：初始化串口，不阻塞等待 USB 串口连接，保证机器人上电后能直接运行。
// =================================================
UARTHostPC::UARTHostPC(HardwareSerial& port)
    : port_(&port), rxLength_(0), overflowed_(false), droppingLine_(false) {
    rxBuffer_[0] = 0;
}

void UARTHostPC::init(uint32_t baudRate) {
    port_->begin(baudRate);
    rxLength_ = 0;
    overflowed_ = false;
    droppingLine_ = false;
}

// ==================== 接收命令 ====================
// 作用：从串口读取到换行符为止，返回一条完整命令；没有完整命令时立即返回 false。
// ==================================================
bool UARTHostPC::readLine(char* output, uint8_t outputSize) {
    if (output == nullptr || outputSize == 0) {
        return false;
    }

    while (port_->available() > 0) {
        const char ch = static_cast<char>(port_->read());

        if (ch == 13) {
            continue;
        }

        if (droppingLine_) {
            if (ch == 10) {
                droppingLine_ = false;
                rxLength_ = 0;
            }
            continue;
        }

        if (ch == 10) {
            rxBuffer_[rxLength_] = 0;
            strncpy(output, rxBuffer_, outputSize - 1);
            output[outputSize - 1] = 0;
            rxLength_ = 0;
            return true;
        }

        if (rxLength_ < kLineBufferSize - 1) {
            rxBuffer_[rxLength_++] = ch;
        } else {
            rxLength_ = 0;
            overflowed_ = true;
            droppingLine_ = true;
        }
    }

    return false;
}

// ==================== 发送响应 ====================
// 作用：统一以换行结尾返回响应，方便上位机按行解析。
// ==================================================
void UARTHostPC::sendLine(const char* data) {
    if (data == nullptr) {
        return;
    }
    port_->println(data);
}

void UARTHostPC::sendLine(const __FlashStringHelper* data) {
    if (data == nullptr) {
        return;
    }
    port_->println(data);
}

// ==================== 异常状态 ====================
// 作用：暴露接收缓冲区溢出状态，主循环可以向上位机报告协议错误。
// ==================================================
bool UARTHostPC::overflowed() const {
    return overflowed_;
}

void UARTHostPC::clearOverflow() {
    overflowed_ = false;
}
