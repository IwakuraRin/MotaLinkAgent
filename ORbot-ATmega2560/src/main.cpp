#include <Arduino.h>
#include "uart-hostpc.h"

// put function declarations here:
int myFunction(int, int);

UARTHostPC uartHostPC;

void setup() {
  // put your setup code here, to run once:
  uartHostPC.init(9600); // 初始化UART通信，波特率9600
  uartHostPC.sendData("Hello from Arduino!"); // 发送初始消息
  int result = myFunction(2, 3);
}

void loop() {
  // put your main code here, to run repeatedly:
  String received = uartHostPC.receiveData();
  if (received.length() > 0) {
    uartHostPC.sendData(("Received: " + received).c_str());
  }
  delay(100); // 短暂延迟
}

// put function definitions here:
int myFunction(int x, int y) {
  return x + y;
}