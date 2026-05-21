// 作用：组织 ATmega2560 下位机主循环，处理上位机 UART 命令、HC-SR04 测距和 IIC 三轮电机驱动。
#include <Arduino.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SR04.h"
#include "iic-hall-motor-driver.h"
#include "uart-hostpc.h"

// ==================== 硬件配置 ====================
// 作用：集中管理下位机引脚、串口波特率和 IIC 驱动板地址，方便后续按实物接线调整。
// ==================================================
namespace {
const uint32_t kHostBaudRate = 115200UL;
const uint8_t kSR04TriggerPin = 22;
const uint8_t kSR04EchoPin = 23;
const uint8_t kMotorDriverAddress = 0x10;
const uint32_t kMotorDriverIicClockHz = 100000UL;
const uint8_t kCommandBufferSize = UARTHostPC::kLineBufferSize;
const uint8_t kReplyBufferSize = 96;

UARTHostPC uartHostPC(Serial);
SR04Sensor frontRangeSensor;
IICHallMotorDriver motorDriver;

char commandBuffer[kCommandBufferSize];
char replyBuffer[kReplyBufferSize];
}

// ==================== 通用响应 ====================
// 作用：统一输出 OK/ERR 响应，让上位机协议更容易解析。
// ==================================================
static void sendOk(const __FlashStringHelper* message) {
    uartHostPC.sendLine(message);
}

static void sendError(const __FlashStringHelper* message) {
    uartHostPC.sendLine(message);
}

static void sendDriverError(const char* action) {
    snprintf(replyBuffer, sizeof(replyBuffer), "ERR %s iic=%u", action, motorDriver.lastError());
    uartHostPC.sendLine(replyBuffer);
}

// ==================== 参数解析 ====================
// 作用：把上位机文本参数转换为 ATmega 友好的整数，避免使用动态字符串。
// ==================================================
static bool parseInt16(const char* text, int16_t& value) {
    if (text == nullptr || *text == 0) {
        return false;
    }

    char* end = nullptr;
    const long parsed = strtol(text, &end, 10);
    if (end == text || *end != 0 || parsed < INT16_MIN || parsed > INT16_MAX) {
        return false;
    }

    value = static_cast<int16_t>(parsed);
    return true;
}

static bool parseBool01(const char* text, bool& value) {
    if (text == nullptr) {
        return false;
    }
    if (strcmp(text, "1") == 0 || strcmp(text, "ON") == 0 || strcmp(text, "on") == 0) {
        value = true;
        return true;
    }
    if (strcmp(text, "0") == 0 || strcmp(text, "OFF") == 0 || strcmp(text, "off") == 0) {
        value = false;
        return true;
    }
    return false;
}

// ==================== 功能命令 ====================
// 作用：处理测距、电机速度、使能、急停和状态读取命令。
// ==================================================
static void handleRangeCommand() {
    const uint16_t distanceMm = frontRangeSensor.readDistanceMm();
    snprintf(replyBuffer, sizeof(replyBuffer), "OK SR04 mm=%u", distanceMm);
    uartHostPC.sendLine(replyBuffer);
}

static void handleMotorCommand(char* firstArg) {
    int16_t wheelSpeed[3];
    wheelSpeed[0] = 0;
    wheelSpeed[1] = 0;
    wheelSpeed[2] = 0;

    char* arg = firstArg;
    for (uint8_t i = 0; i < 3; ++i) {
        if (!parseInt16(arg, wheelSpeed[i])) {
            sendError(F("ERR MOTOR args"));
            return;
        }
        arg = strtok(nullptr, " ");
    }

    if (!motorDriver.setWheelSpeed(wheelSpeed[0], wheelSpeed[1], wheelSpeed[2])) {
        sendDriverError("MOTOR");
        return;
    }
    sendOk(F("OK MOTOR"));
}

static void handleEnableCommand(char* arg) {
    bool enabled = false;
    if (!parseBool01(arg, enabled)) {
        sendError(F("ERR ENABLE arg"));
        return;
    }

    if (!motorDriver.setEnabled(enabled)) {
        sendDriverError("ENABLE");
        return;
    }
    sendOk(enabled ? F("OK ENABLE 1") : F("OK ENABLE 0"));
}

static void handleStatusCommand() {
    HallMotorStatus status;
    if (!motorDriver.readStatus(status)) {
        sendDriverError("STATUS");
        return;
    }

    snprintf(
        replyBuffer,
        sizeof(replyBuffer),
        "OK STATUS w0=%d w1=%d w2=%d en=%u fault=%u",
        status.wheelSpeed[0],
        status.wheelSpeed[1],
        status.wheelSpeed[2],
        status.enabled,
        status.faultCode);
    uartHostPC.sendLine(replyBuffer);
}

static void handleStopCommand() {
    if (!motorDriver.stop()) {
        sendDriverError("STOP");
        return;
    }
    sendOk(F("OK STOP"));
}

// ==================== 命令路由 ====================
// 作用：解析上位机按行发送的文本命令，并分发给对应模块。
// ==================================================
static void handleHostCommand(char* line) {
    char* command = strtok(line, " ");
    if (command == nullptr) {
        return;
    }

    if (strcmp(command, "PING") == 0) {
        sendOk(F("OK PONG"));
        return;
    }
    if (strcmp(command, "HELP") == 0) {
        uartHostPC.sendLine(F("OK CMDS PING HELP SR04? MOTOR <w0> <w1> <w2> ENABLE <0|1> STOP STATUS?"));
        return;
    }
    if (strcmp(command, "SR04?") == 0) {
        handleRangeCommand();
        return;
    }
    if (strcmp(command, "MOTOR") == 0) {
        handleMotorCommand(strtok(nullptr, " "));
        return;
    }
    if (strcmp(command, "ENABLE") == 0) {
        handleEnableCommand(strtok(nullptr, " "));
        return;
    }
    if (strcmp(command, "STOP") == 0) {
        handleStopCommand();
        return;
    }
    if (strcmp(command, "STATUS?") == 0) {
        handleStatusCommand();
        return;
    }

    sendError(F("ERR UNKNOWN"));
}

// ==================== Arduino 生命周期 ====================
// 作用：初始化 UART、SR04 和 IIC 电机驱动，并在主循环中持续处理上位机命令。
// ==========================================================
void setup() {
    uartHostPC.init(kHostBaudRate);
    frontRangeSensor.init(kSR04TriggerPin, kSR04EchoPin);
    motorDriver.init(kMotorDriverAddress, kMotorDriverIicClockHz);
    motorDriver.stop();
    uartHostPC.sendLine(F("OK BOOT AmseokBot-Milo SlaveDevice"));
}

void loop() {
    if (uartHostPC.overflowed()) {
        uartHostPC.clearOverflow();
        sendError(F("ERR UART overflow"));
    }

    if (uartHostPC.readLine(commandBuffer, sizeof(commandBuffer))) {
        handleHostCommand(commandBuffer);
    }
}
