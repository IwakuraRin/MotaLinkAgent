#ifndef UART_HOSTPC_H
#define UART_HOSTPC_H

#include <Arduino.h>

class UARTHostPC {
public:
    void init(long baudRate = 9600);
    void sendData(const char* data);
    String receiveData();
};

#endif