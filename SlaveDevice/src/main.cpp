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
#include "chassis-motion-control.h"
#include "iic-hall-motor-driver.h"
#include "obstacle-safety.h"
#include "uart-hostpc.h"
#include "wheel-speed-pid.h"

// ==================== 硬件配置 ====================
// 作用：集中管理下位机引脚、串口波特率、IIC 地址和超声波安全阈值。
// ==================================================
namespace {
const uint32_t kHostBaudRate = 115200UL;               // 上位机串口通信波特率。
const uint8_t kSR04TriggerPin = 22;                    // 前向 HC-SR04 Trig 触发引脚。
const uint8_t kSR04EchoPin = 23;                       // 前向 HC-SR04 Echo 回波引脚。
const uint8_t kMotorDriverAddress = 0x10;              // IIC 霍尔电机驱动板从机地址。
const uint32_t kMotorDriverIicClockHz = 100000UL;      // IIC 总线频率，标准模式 100kHz。
const uint16_t kObstacleHardStopDistanceMm = 160;      // 原始测距小于等于该距离时立即急停，单位 mm。
const uint16_t kObstacleStopDistanceMm = 260;          // 滤波距离进入障碍物阻挡状态的阈值，单位 mm。
const uint16_t kObstacleClearDistanceMm = 360;         // 滤波距离解除阻挡状态的阈值，单位 mm，必须大于停止阈值形成迟滞。
const uint8_t kObstacleStopSamples = 2;                // 连续多少次近距离采样后确认进入阻挡状态。
const uint8_t kObstacleClearSamples = 4;               // 连续多少次远距离采样后确认解除阻挡状态。
const uint32_t kRangeSampleIntervalMs = 25;            // HC-SR04 主循环采样周期，单位 ms。
const uint32_t kRangeReportIntervalMs = 100;           // 向上位机主动上报测距遥测的周期，单位 ms。
const uint32_t kBlockedStopRefreshMs = 80;             // 阻挡期间重复下发 stop 的刷新周期，防止驱动板漏停。
const uint32_t kChassisCommandTimeoutMs = 350;         // 超过该时间未收到 CHASSIS 命令时自动停止底盘。
const float kWheelBaseRadiusM = 0.1337147f;            // 底盘中心到轮接地点距离，单位 m。
const float kWheelRadiusM = 0.0425f;                   // 全向轮半径，单位 m。
const float kMaxWheelOmegaRadps = 25.10f;              // 单轮最大目标角速度，单位 rad/s。
const float kWheelCommandScale = 100.0f;               // 轮角速度 rad/s 转驱动板 int16 命令值的比例。
const float kMaxLinearMps = 0.80f;                     // 底盘 vx/vy 最大线速度，单位 m/s。
const float kMaxAngularRadps = 2.50f;                  // 底盘 wz 最大角速度，单位 rad/s。
const float kMaxLinearAccelMps2 = 0.8f;                // 底盘 vx/vy 加速度平滑上限，单位 m/s^2。
const float kMaxAngularAccelRadps2 = 3.0f;             // 底盘 wz 角加速度平滑上限，单位 rad/s^2。
const uint16_t kChassisUpdateIntervalMs = 20;          // 底盘控制器最小输出周期，单位 ms。
const WheelSpeedPidConfig kWheelSpeedPidConfig = {     // 三轮速度外环 PID 参数和输出限幅。
    0.05f,                                             // kp：比例系数，按目标和反馈命令差值修正输出。
    0.0f,                                              // ki：积分项权重，用于累计误差并补偿长期稳态偏差。
    0.0f,                                              // kd：微分项权重，用于根据误差变化趋势抑制过冲。
    300,                                               // maxCorrection：单轮最大 PID 修正命令值。
    800,                                               // integralLimit：积分累计误差限幅。
    2                                                  // deadband：小于等于该命令差值时不修正。
};
const uint8_t kCommandBufferSize = UARTHostPC::kLineBufferSize;  // 接收一行上位机命令的缓冲区长度。
const uint8_t kReplyBufferSize = 112;                            // 格式化 UART 响应和遥测文本的缓冲区长度。

UARTHostPC uartHostPC(Serial);                 // 与上位机通信的硬件串口封装。
SR04Sensor frontRangeSensor;                   // 前向 HC-SR04 测距传感器。
ObstacleSafety obstacleSafety;                 // 超声波障碍物急停状态机。
IICHallMotorDriver motorDriver;                // IIC 三轮霍尔电机驱动板接口。
ChassisMotionControl chassisMotionControl;     // 底盘目标速度限幅、平滑和运动学控制器。
WheelSpeedPid wheelSpeedPid;                   // 基于驱动板反馈的三轮速度外环 PID。

char commandBuffer[kCommandBufferSize];        // 保存从 UART 收到的一整行命令。
char replyBuffer[kReplyBufferSize];            // 保存 snprintf 生成的响应或遥测文本。
uint16_t lastRawDistanceMm = 0;                // 最近一次 HC-SR04 原始距离，0 表示无效或超时。
uint32_t lastRangeSampleMs = 0;                // 上一次执行 HC-SR04 采样的 millis 时间戳。
uint32_t lastRangeReportMs = 0;                // 上一次向上位机上报测距遥测的 millis 时间戳。
uint32_t lastStopRefreshMs = 0;                // 阻挡状态下上一次重复发送 stop 的 millis 时间戳。
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

    char* end = nullptr;                                // strtol 写回的解析结束位置，用于判断整串参数是否合法。
    const long parsed = strtol(text, &end, 10);         // 临时使用 long 接收，便于检查是否超出 int16 范围。
    if (end == text || *end != 0 || parsed < INT16_MIN || parsed > INT16_MAX) {
        return false;
    }

    value = static_cast<int16_t>(parsed);
    return true;
}

static bool parseFloat32(const char* text, float& value) {
    if (text == nullptr || *text == 0) {
        return false;
    }

    char* end = nullptr;                                // strtod 写回的解析结束位置，用于判断整串参数是否合法。
    const double parsed = strtod(text, &end);           // 临时使用 double 接收，再转换成 ATmega 控制用 float。
    if (end == text || *end != 0) {
        return false;
    }

    value = static_cast<float>(parsed);
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
    obstacleSafety.formatTelemetry(replyBuffer, sizeof(replyBuffer), rawDistanceMm);
    uartHostPC.sendLine(replyBuffer);
}

static void sendObstacleEvent(ObstacleSafetyEvent event) {
    obstacleSafety.formatEvent(replyBuffer, sizeof(replyBuffer), event);
    uartHostPC.sendLine(replyBuffer);
}

// ==================== 本地急停循环 ====================
// 作用：周期性读取 HC-SR04，距离过近时本地下位机直接让 IIC 驱动板停止。
// ======================================================
static void updateObstacleSafety() {
    const uint32_t now = millis();  // 当前主循环时间戳，用于采样、上报和 stop 刷新调度。
    if (now - lastRangeSampleMs < kRangeSampleIntervalMs) {
        return;
    }
    lastRangeSampleMs = now;

    lastRawDistanceMm = frontRangeSensor.readDistanceMm();
    const ObstacleSafetyResult result = obstacleSafety.update(lastRawDistanceMm);  // 本次测距更新后的滤波距离和安全事件。

    if (result.event == kObstacleSafetyStop) {
        chassisMotionControl.stopMotion(wheelSpeedPid);
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

// ==================== 底盘运动控制 ====================
// 作用：由下位机周期性执行速度平滑和逆运动学，再把三轮目标写给 IIC 驱动板。
// ======================================================
static void updateChassisMotion() {
    if (obstacleSafety.blocked()) {
        return;
    }

    const uint32_t now = millis();  // 当前主循环时间戳，用于底盘命令超时和控制周期调度。
    const ChassisMotionUpdateResult result = chassisMotionControl.updateDriver(now, motorDriver, wheelSpeedPid);
    if (result == kChassisMotionDriverError) {
        sendDriverError("CHASSIS");
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

    int16_t wheelSpeed[3];  // MOTOR 命令直接指定的三轮速度命令值。
    wheelSpeed[0] = 0;
    wheelSpeed[1] = 0;
    wheelSpeed[2] = 0;

    char* arg = firstArg;  // 当前正在解析的 MOTOR 参数指针。
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
    wheelSpeedPid.reset();
    chassisMotionControl.deactivate();
    sendOk(F("OK MOTOR"));
}

static void handleChassisCommand(char* firstArg) {
    if (obstacleSafety.blocked()) {
        chassisMotionControl.stopMotion(wheelSpeedPid);
        motorDriver.stop();
        snprintf(replyBuffer, sizeof(replyBuffer), "ERR CHASSIS obstacle mm=%u", obstacleSafety.filteredDistanceMm());
        uartHostPC.sendLine(replyBuffer);
        return;
    }

    ChassisTwist twist = {0.0f, 0.0f, 0.0f};  // CHASSIS 命令给出的底盘目标速度。
    float* values[3] = {&twist.vxMps, &twist.vyMps, &twist.wzRadps};  // 参数写入目标：vx、vy、wz。
    char* arg = firstArg;  // 当前正在解析的 CHASSIS 参数指针。
    for (uint8_t i = 0; i < 3; ++i) {
        if (!parseFloat32(arg, *values[i])) {
            sendError(F("ERR CHASSIS args"));
            return;
        }
        arg = strtok(nullptr, " ");
    }

    chassisMotionControl.setTargetTwist(twist, millis());
    sendOk(F("OK CHASSIS"));
}

static void handleEnableCommand(char* arg) {
    bool enabled = false;  // ENABLE 命令解析出的驱动板目标使能状态。
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
    HallMotorStatus status;  // STATUS? 命令读取到的驱动板状态快照。
    if (!motorDriver.readStatus(status)) {
        sendDriverError("STATUS");
        return;
    }

    const ChassisTwist currentTwist = chassisMotionControl.currentTwist();  // 当前平滑后的底盘执行速度。
    snprintf(
        replyBuffer,
        sizeof(replyBuffer),
        "OK STATUS w0=%d w1=%d w2=%d en=%u fault=%u obstacle=%u mm=%u vx_mmps=%ld vy_mmps=%ld wz_mradps=%ld",
        status.wheelSpeed[0],
        status.wheelSpeed[1],
        status.wheelSpeed[2],
        status.enabled,
        status.faultCode,
        obstacleSafety.blocked() ? 1 : 0,
        obstacleSafety.filteredDistanceMm(),
        static_cast<long>(currentTwist.vxMps * 1000.0f),
        static_cast<long>(currentTwist.vyMps * 1000.0f),
        static_cast<long>(currentTwist.wzRadps * 1000.0f));
    uartHostPC.sendLine(replyBuffer);
}

static void handleStopCommand() {
    chassisMotionControl.stopMotion(wheelSpeedPid);
    if (!motorDriver.stop()) {
        sendDriverError("STOP");
        return;
    }
    sendOk(F("OK STOP"));
}

static void handleSafetyCommand() {
    obstacleSafety.formatStatus(replyBuffer, sizeof(replyBuffer));
    uartHostPC.sendLine(replyBuffer);
}

// ==================== 命令路由 ====================
// 作用：解析上位机按行发送的文本命令，并分发给对应模块。
// ==================================================
static void handleHostCommand(char* line) {
    char* command = strtok(line, " ");  // 当前命令行的第一个 token，用于路由命令类型。
    if (command == nullptr) {
        return;
    }

    if (strcmp(command, "PING") == 0) {
        sendOk(F("OK PONG"));
        return;
    }
    if (strcmp(command, "HELP") == 0) {
        uartHostPC.sendLine(F("OK CMDS PING HELP SR04? CHASSIS <vx_mps> <vy_mps> <wz_radps> MOTOR <w0> <w1> <w2> ENABLE <0|1> STOP STATUS? SAFETY?"));
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
    if (strcmp(command, "CHASSIS") == 0) {
        handleChassisCommand(strtok(nullptr, " "));
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
    obstacleSafety.configure(kObstacleStopDistanceMm, kObstacleClearDistanceMm, kObstacleStopSamples, kObstacleClearSamples, kObstacleHardStopDistanceMm);
    motorDriver.init(kMotorDriverAddress, kMotorDriverIicClockHz);
    chassisMotionControl.configureKinematics(kWheelBaseRadiusM, kWheelRadiusM, kMaxWheelOmegaRadps, kWheelCommandScale);
    chassisMotionControl.configureLimits(kMaxLinearMps, kMaxAngularRadps);
    chassisMotionControl.configureSmoothing(kMaxLinearAccelMps2, kMaxAngularAccelRadps2, kChassisUpdateIntervalMs);
    chassisMotionControl.configureCommandTimeout(kChassisCommandTimeoutMs);
    chassisMotionControl.reset(millis());
    wheelSpeedPid.configure(kWheelSpeedPidConfig);
    wheelSpeedPid.reset();
    motorDriver.stop();
    uartHostPC.sendLine(F("OK BOOT AmseokBot-Milo SlaveDevice"));
}

void loop() {
    updateObstacleSafety();
    updateChassisMotion();

    if (uartHostPC.overflowed()) {
        uartHostPC.clearOverflow();
        sendError(F("ERR UART overflow"));
    }

    if (uartHostPC.readLine(commandBuffer, sizeof(commandBuffer))) {
        handleHostCommand(commandBuffer);
    }
}
