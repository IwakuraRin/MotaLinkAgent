/** @file src/core/master/command.cpp
 *  @brief 上位机 UART 文本命令处理实现。
 */
#include "core/master/command.h"

#include "core/master/parser.h"

#include "driver/motor.h"

#include <stdio.h>
#include <string.h>

namespace {
/**
 * @brief 格式化 SR04 遥测响应。
 * @param safety 超声波安全状态机。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区长度。
 * @param rawDistanceMm 原始测距，单位 mm。
 */
void formatSafetyTelemetry(const ObstacleSafety& safety, char* buffer, uint8_t bufferSize, uint16_t rawDistanceMm) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    const ObstacleSafetyState state = safety.state(); ///< 当前滤波距离和安全阈值快照。
    snprintf(
        buffer,
        bufferSize,
        "TEL SR04 mm=%u raw=%u median=%u blocked=%u hard=%u stop=%u clear=%u",
        state.filteredDistanceMm,
        rawDistanceMm,
        state.medianDistanceMm,
        safety.blocked() ? 1 : 0,
        state.hardStopDistanceMm,
        state.stopDistanceMm,
        state.clearDistanceMm);
}

/**
 * @brief 格式化障碍物事件响应。
 * @param safety 超声波安全状态机。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区长度。
 * @param event 障碍物安全事件。
 */
void formatSafetyEvent(const ObstacleSafety& safety, char* buffer, uint8_t bufferSize, ObstacleSafetyEvent event) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    const ObstacleSafetyState safetyState = safety.state(); ///< 障碍物事件携带的当前距离快照。
    const char* eventState = event == kObstacleSafetyStop ? "STOP" : "CLEAR"; ///< 上报给上位机的事件状态文本。
    snprintf(buffer, bufferSize, "EVT OBSTACLE %s mm=%u", eventState, safetyState.filteredDistanceMm);
}

/**
 * @brief 格式化安全状态查询响应。
 * @param safety 超声波安全状态机。
 * @param buffer 输出缓冲区。
 * @param bufferSize 输出缓冲区长度。
 */
void formatSafetyStatus(const ObstacleSafety& safety, char* buffer, uint8_t bufferSize) {
    if (buffer == nullptr || bufferSize == 0) {
        return;
    }
    const ObstacleSafetyState state = safety.state(); ///< 安全状态查询使用的距离和阈值快照。
    snprintf(
        buffer,
        bufferSize,
        "OK SAFETY hard=%u stop=%u clear=%u blocked=%u mm=%u median=%u",
        state.hardStopDistanceMm,
        state.stopDistanceMm,
        state.clearDistanceMm,
        safety.blocked() ? 1 : 0,
        state.filteredDistanceMm,
        state.medianDistanceMm);
}
}

MasterCommand::MasterCommand(
    UARTHostPC& uartHostPC,
    SR04Sensor& rangeSensor,
    ObstacleSafety& obstacleSafety,
    ChassisMotionControl& chassisMotionControl,
    char* replyBuffer,
    uint8_t replyBufferSize,
    uint16_t& lastRawDistanceMm)
    : uartHostPC_(uartHostPC),
      rangeSensor_(rangeSensor),
      obstacleSafety_(obstacleSafety),
      chassisMotionControl_(chassisMotionControl),
      replyBuffer_(replyBuffer),
      replyBufferSize_(replyBufferSize),
      lastRawDistanceMm_(lastRawDistanceMm) {}

void MasterCommand::sendOk(const __FlashStringHelper* message) {
    uartHostPC_.sendLine(message);
}

void MasterCommand::sendError(const __FlashStringHelper* message) {
    uartHostPC_.sendLine(message);
}

void MasterCommand::sendDriverError(const char* action) {
    snprintf(replyBuffer_, replyBufferSize_, "ERR %s iic=%u", action, chassisMotionControl_.lastDriverError());
    uartHostPC_.sendLine(replyBuffer_);
}

void MasterCommand::sendRangeTelemetry(uint16_t rawDistanceMm) {
    formatSafetyTelemetry(obstacleSafety_, replyBuffer_, replyBufferSize_, rawDistanceMm);
    uartHostPC_.sendLine(replyBuffer_);
}

void MasterCommand::sendObstacleEvent(ObstacleSafetyEvent event) {
    formatSafetyEvent(obstacleSafety_, replyBuffer_, replyBufferSize_, event);
    uartHostPC_.sendLine(replyBuffer_);
}

void MasterCommand::handleRange() {
    lastRawDistanceMm_ = rangeSensor_.readDistanceMm();
    obstacleSafety_.update(lastRawDistanceMm_);
    sendRangeTelemetry(lastRawDistanceMm_);
}

void MasterCommand::handleMotor(char* firstArg) {
    int16_t wheelSpeed[3] = {0, 0, 0}; ///< MOTOR 命令解析出的三轮速度命令。
    char* arg = firstArg;              ///< 当前正在解析的命令参数 token。
    for (uint8_t i = 0; i < 3; ++i) {
        if (!MasterParser::parseInt16(arg, wheelSpeed[i])) {
            sendError(F("ERR MOTOR args"));
            return;
        }
        arg = strtok(nullptr, " ");
    }

    const ChassisCommandResult result = chassisMotionControl_.setManualWheelSpeed(wheelSpeed[0], wheelSpeed[1], wheelSpeed[2]); ///< 底盘模块执行 MOTOR 命令的结果。
    if (result == kChassisCommandRejectedSafety) {
        const ObstacleSafetyState safetyState = obstacleSafety_.state(); ///< MOTOR 被安全拒绝时上报的距离快照。
        snprintf(replyBuffer_, replyBufferSize_, "ERR MOTOR obstacle mm=%u", safetyState.filteredDistanceMm);
        uartHostPC_.sendLine(replyBuffer_);
        return;
    }
    if (result != kChassisCommandOk) {
        sendDriverError("MOTOR");
        return;
    }
    sendOk(F("OK MOTOR"));
}

void MasterCommand::handleChassis(char* firstArg) {
    ChassisTwist twist = {0.0f, 0.0f, 0.0f}; ///< CHASSIS 命令解析出的目标底盘速度。
    float* values[3] = {&twist.vxMps, &twist.vyMps, &twist.wzRadps}; ///< 参数写入目标：vx、vy、wz。
    char* arg = firstArg; ///< 当前正在解析的命令参数 token。
    for (uint8_t i = 0; i < 3; ++i) {
        if (!MasterParser::parseFloat32(arg, *values[i])) {
            sendError(F("ERR CHASSIS args"));
            return;
        }
        arg = strtok(nullptr, " ");
    }

    const ChassisCommandResult result = chassisMotionControl_.setTargetTwistCommand(twist, millis()); ///< 底盘模块执行 CHASSIS 命令的结果。
    if (result == kChassisCommandRejectedSafety) {
        const ObstacleSafetyState safetyState = obstacleSafety_.state(); ///< CHASSIS 被安全拒绝时上报的距离快照。
        snprintf(replyBuffer_, replyBufferSize_, "ERR CHASSIS obstacle mm=%u", safetyState.filteredDistanceMm);
        uartHostPC_.sendLine(replyBuffer_);
        return;
    }
    if (result != kChassisCommandOk) {
        sendDriverError("CHASSIS");
        return;
    }
    sendOk(F("OK CHASSIS"));
}

void MasterCommand::handleEnable(char* arg) {
    bool enabled = false; ///< ENABLE 命令解析出的驱动板目标使能状态。
    if (!MasterParser::parseBool01(arg, enabled)) {
        sendError(F("ERR ENABLE arg"));
        return;
    }

    if (chassisMotionControl_.setDriverEnabled(enabled) != kChassisCommandOk) {
        sendDriverError("ENABLE");
        return;
    }
    sendOk(enabled ? F("OK ENABLE 1") : F("OK ENABLE 0"));
}

void MasterCommand::handleStatus() {
    HallMotorStatus status; ///< STATUS? 命令读取到的驱动板状态快照。
    if (!chassisMotionControl_.readDriverStatus(status)) {
        sendDriverError("STATUS");
        return;
    }

    const ChassisTwist currentTwist = chassisMotionControl_.currentTwist(); ///< 当前平滑后的底盘执行速度。
    const ObstacleSafetyState safetyState = obstacleSafety_.state(); ///< STATUS? 响应中的避障距离快照。
    snprintf(
        replyBuffer_,
        replyBufferSize_,
        "OK STATUS w0=%d w1=%d w2=%d en=%u fault=%u obstacle=%u mm=%u vx_mmps=%ld vy_mmps=%ld wz_mradps=%ld",
        status.wheelSpeed[0],
        status.wheelSpeed[1],
        status.wheelSpeed[2],
        status.enabled,
        status.faultCode,
        obstacleSafety_.blocked() ? 1 : 0,
        safetyState.filteredDistanceMm,
        static_cast<long>(currentTwist.vxMps * 1000.0f),
        static_cast<long>(currentTwist.vyMps * 1000.0f),
        static_cast<long>(currentTwist.wzRadps * 1000.0f));
    uartHostPC_.sendLine(replyBuffer_);
}

void MasterCommand::handleStop() {
    if (chassisMotionControl_.stopDriver() != kChassisCommandOk) {
        sendDriverError("STOP");
        return;
    }
    sendOk(F("OK STOP"));
}

void MasterCommand::handleSafety() {
    formatSafetyStatus(obstacleSafety_, replyBuffer_, replyBufferSize_);
    uartHostPC_.sendLine(replyBuffer_);
}

void MasterCommand::handle(char* line) {
    char* command = strtok(line, " ");
    if (command == nullptr) {
        return;
    }

    if (strcmp(command, "PING") == 0) {
        sendOk(F("OK PONG"));
        return;
    }
    if (strcmp(command, "HELP") == 0) {
        uartHostPC_.sendLine(F("OK CMDS PING HELP SR04? CHASSIS <vx_mps> <vy_mps> <wz_radps> MOTOR <w0> <w1> <w2> ENABLE <0|1> STOP STATUS? SAFETY?"));
        return;
    }
    if (strcmp(command, "SR04?") == 0) {
        handleRange();
        return;
    }
    if (strcmp(command, "MOTOR") == 0) {
        handleMotor(strtok(nullptr, " "));
        return;
    }
    if (strcmp(command, "CHASSIS") == 0) {
        handleChassis(strtok(nullptr, " "));
        return;
    }
    if (strcmp(command, "ENABLE") == 0) {
        handleEnable(strtok(nullptr, " "));
        return;
    }
    if (strcmp(command, "STOP") == 0) {
        handleStop();
        return;
    }
    if (strcmp(command, "STATUS?") == 0) {
        handleStatus();
        return;
    }
    if (strcmp(command, "SAFETY?") == 0) {
        handleSafety();
        return;
    }

    sendError(F("ERR UNKNOWN"));
}
