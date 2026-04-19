#include "uart-hostpc.h"

void UARTHostPC::init(long baudRate) {
    Serial.begin(baudRate);
    while (!Serial) {
        ; // 等待串口连接
    }
}

void UARTHostPC::sendData(const char* data) {
    Serial.println(data);
}

String UARTHostPC::receiveData() {
    if (Serial.available() > 0) {
        return Serial.readStringUntil('\n');
    }
    return "";
}