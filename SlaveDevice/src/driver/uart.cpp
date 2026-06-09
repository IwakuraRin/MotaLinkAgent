/** @file uart.cpp
 *  @brief 上位机 UART 行协议收发实现。
 */
#include "driver/uart.h"
#include <string.h>

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

bool UARTHostPC::readLine(char* output, uint8_t outputSize) {
    if (output == nullptr || outputSize == 0) {
        return false;
    }

    while (port_->available() > 0) {
        const char ch = static_cast<char>(port_->read()); ///< 串口当前读到的单个字符。

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

bool UARTHostPC::overflowed() const {
    return overflowed_;
}

void UARTHostPC::clearOverflow() {
    overflowed_ = false;
}
