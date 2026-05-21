/*
|--------------------------------------------------------------------------
| ATmega2560 下位机主程序
|--------------------------------------------------------------------------
| 处理上位机 UART 命令、HC-SR04 本地急停、IIC 三轮电机驱动和安全状态上报。
|--------------------------------------------------------------------------
*/
#include <Arduino.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "SR04.h"
#include "iic-hall-motor-driver.h"
#include "obstacle-safety.h"
#include "uart-hostpc.h"

// ==================== 硬件配置 ====================
// 作用：集中管理下位机引脚、串口波特率、IIC 地址和超声波安全阈值。
// ==================================================
namespace {
const uint32_t kHostBaudRate = 115200UL;
const uint8_t kSR04TriggerPin = 22;
const uint8_t kSR04EchoPin = 23;
const uint8_t kMotorDriverAddress = 0x10;
const uint32_t kMotorDriverIicClockHz = 100000UL;
const uint16_t kObstacleStopDistanceMm = 260;
const uint16_t kObstacleClearDistanceMm = 360;
const uint8_t kObstacleStopSamples = 2;
const uint8_t kObstacleClearSamples = 4;
const uint32_t kRangeSampleIntervalMs = 25;
const uint32_t kRangeReportIntervalMs = 100;
const uint32_t kBlockedStopRefreshMs = 80;
const uint8_t kCommandBufferSize = UARTHostPC::kLineBufferSize;
const uint8_t kReplyBufferSize = 112;

UARTHostPC uartHostPC(Serial);
SR04Sensor frontRangeSensor;
ObstacleSafety obstacleSafety;
IICHallMotorDriver motorDriver;

char commandBuffer[kCommandBufferSize];
char replyBuffer[kReplyBufferSize];
uint16_t lastRawDistanceMm = 0;
uint32_t lastRangeSampleMs = 0;
uint32_t lastRangeReportMs = 0;
uint32_t lastStopRefreshMs = 0;
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

// ==================== 安全状态上报 ====================
// 作用：把超声波距离、急停事件和当前安全状态通过 UART 同步给上位机 ROS。
// ======================================================
static void sendRangeTelemetry(uint16_t rawDistanceMm) {
    snprintf(
        replyBuffer,
        sizeof(replyBuffer),
        "TEL SR04 mm=%u raw=%u blocked=%u stop=%u clear=%u",
        obstacleSafety.filteredDistanceMm(),
        rawDistanceMm,
        obstacleSafety.blocked() ? 1 : 0,
        obstacleSafety.stopDistanceMm(),
        obstacleSafety.clearDistanceMm());
    uartHostPC.sendLine(replyBuffer);
}

static void sendObstacleEvent(ObstacleSafetyEvent event) {
    const char* state = event == kObstacleSafetyStop ? "STOP" : "CLEAR";
    snprintf(replyBuffer, sizeof(replyBuffer), "EVT OBSTACLE %s mm=%u", state, obstacleSafety.filteredDistanceMm());
    uartHostPC.sendLine(replyBuffer);
}

// ==================== 本地急停循环 ====================
// 作用：周期性读取 HC-SR04，距离过近时本地下位机直接让 IIC 驱动板停止。
// ======================================================
static void updateObstacleSafety() {
    const uint32_t now = millis();
    if (now - lastRangeSampleMs < kRangeSampleIntervalMs) {
        return;
    }
    lastRangeSampleMs = now;

    lastRawDistanceMm = frontRangeSensor.readDistanceMm();
    const ObstacleSafetyResult result = obstacleSafety.update(lastRawDistanceMm);

    if (result.event == kObstacleSafetyStop) {
        motorDriver.stop();
        lastStopRefreshMs = now;
        sendObstacleEvent(kObstacleSafetyStop);
    } else if (result.event == kObstacleSafetyClear) {
        sendObstacleEvent(kObstacleSafetyClear);
    }

    if (obstacleSafety.blocked() && now - lastStopRefreshMs >= kBlockedStopRefreshMs) {
        motorDriver.stop();
        lastStopRefreshMs = now;
    }

    if (now - lastRangeReportMs >= kRangeReportIntervalMs) {
        lastRangeReportMs = now;
        sendRangeTelemetry(lastRawDistanceMm);
    }
}

// ==================== 功能命令 ====================
// 作用：处理测距、电机速度、使能、急停和状态读取命令。
// ==================================================
static void handleRangeCommand() {
    lastRawDistanceMm = frontRangeSensor.readDistanceMm();
    obstacleSafety.update(lastRawDistanceMm);
    sendRangeTelemetry(lastRawDistanceMm);
}

static void handleMotorCommand(char* firstArg) {
    if (obstacleSafety.blocked()) {
        motorDriver.stop();
        snprintf(replyBuffer, sizeof(replyBuffer), "ERR MOTOR obstacle mm=%u", obstacleSafety.filteredDistanceMm());
        uartHostPC.sendLine(replyBuffer);
        return;
    }

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
        "OK STATUS w0=%d w1=%d w2=%d en=%u fault=%u obstacle=%u mm=%u",
        status.wheelSpeed[0],
        status.wheelSpeed[1],
        status.wheelSpeed[2],
        status.enabled,
        status.faultCode,
        obstacleSafety.blocked() ? 1 : 0,
        obstacleSafety.filteredDistanceMm());
    uartHostPC.sendLine(replyBuffer);
}

static void handleStopCommand() {
    if (!motorDriver.stop()) {
        sendDriverError("STOP");
        return;
    }
    sendOk(F("OK STOP"));
}

static void handleSafetyCommand() {
    snprintf(
        replyBuffer,
        sizeof(replyBuffer),
        "OK SAFETY stop=%u clear=%u blocked=%u mm=%u",
        obstacleSafety.stopDistanceMm(),
        obstacleSafety.clearDistanceMm(),
        obstacleSafety.blocked() ? 1 : 0,
        obstacleSafety.filteredDistanceMm());
    uartHostPC.sendLine(replyBuffer);
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
        uartHostPC.sendLine(F("OK CMDS PING HELP SR04? MOTOR <w0> <w1> <w2> ENABLE <0|1> STOP STATUS? SAFETY?"));
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
    if (strcmp(command, "SAFETY?") == 0) {
        handleSafetyCommand();
        return;
    }

    sendError(F("ERR UNKNOWN"));
}

// ==================== Arduino 生命周期 ====================
// 作用：初始化 UART、SR04、安全状态机和 IIC 电机驱动，并持续处理上位机命令。
// ==========================================================
void setup() {
    uartHostPC.init(kHostBaudRate);
    frontRangeSensor.init(kSR04TriggerPin, kSR04EchoPin);
    obstacleSafety.configure(kObstacleStopDistanceMm, kObstacleClearDistanceMm, kObstacleStopSamples, kObstacleClearSamples);
    motorDriver.init(kMotorDriverAddress, kMotorDriverIicClockHz);
    motorDriver.stop();
    uartHostPC.sendLine(F("OK BOOT AmseokBot-Milo SlaveDevice"));
}

void loop() {
    updateObstacleSafety();

    if (uartHostPC.overflowed()) {
        uartHostPC.clearOverflow();
        sendError(F("ERR UART overflow"));
    }

    if (uartHostPC.readLine(commandBuffer, sizeof(commandBuffer))) {
        handleHostCommand(commandBuffer);
    }
}
