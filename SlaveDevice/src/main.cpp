/** @file src/main.cpp
 *  @brief ATMega2560 下位机程序入口。
 */
#include <Arduino.h>
#include <stdint.h>

#include "core/chassis/control.h"
#include "core/chassis/pid.h"
#include "core/master/command.h"
#include "core/sensor/ultrasonic/monitor.h"
#include "core/sensor/ultrasonic/safety.h"
#include "driver/motor.h"
#include "driver/sr04.h"
#include "driver/uart.h"

namespace {
const uint32_t kHostBaudRate = 115200UL;          ///< 上位机 UART 通信波特率。
const uint8_t kSR04TriggerPin = 22;               ///< 前向 HC-SR04 Trig 触发引脚。
const uint8_t kSR04EchoPin = 23;                  ///< 前向 HC-SR04 Echo 回波引脚。
const uint8_t kMotorDriverAddress = 0x10;         ///< IIC 霍尔电机驱动板从机地址。
const uint32_t kMotorDriverIicClockHz = 100000UL; ///< IIC 总线频率，标准模式 100kHz。
const uint16_t kObstacleHardStopDistanceMm = 160; ///< 原始测距小于等于该值时立即进入本地急停，单位 mm。
const uint16_t kObstacleStopDistanceMm = 260;     ///< 滤波距离进入阻挡状态的阈值，单位 mm。
const uint16_t kObstacleClearDistanceMm = 360;    ///< 滤波距离解除阻挡状态的阈值，单位 mm。
const uint8_t kObstacleStopSamples = 2;           ///< 连续近距离采样达到该次数后确认阻挡。
const uint8_t kObstacleClearSamples = 4;          ///< 连续远距离采样达到该次数后解除阻挡。
const uint32_t kRangeSampleIntervalMs = 25;       ///< HC-SR04 主循环采样周期，单位 ms。
const uint32_t kRangeReportIntervalMs = 100;      ///< 向上位机主动上报测距遥测的周期，单位 ms。
const uint32_t kBlockedStopRefreshMs = 80;        ///< 阻挡期间重复下发 stop 的刷新周期，单位 ms。
const uint32_t kChassisCommandTimeoutMs = 350;    ///< 超过该时间未收到 CHASSIS 命令时自动停止底盘，单位 ms。
const float kWheelBaseRadiusM = 0.1337147f;       ///< 底盘中心到轮接地点距离，单位 m。
const float kWheelRadiusM = 0.0425f;              ///< 全向轮半径，单位 m。
const float kMaxWheelOmegaRadps = 25.10f;         ///< 单轮最大目标角速度，单位 rad/s。
const float kWheelCommandScale = 100.0f;          ///< 轮角速度 rad/s 转驱动板 int16 命令值的比例。
const float kMaxLinearMps = 0.80f;                ///< 底盘 vx/vy 最大线速度，单位 m/s。
const float kMaxAngularRadps = 2.50f;             ///< 底盘 wz 最大角速度，单位 rad/s。
const float kMaxLinearAccelMps2 = 0.8f;           ///< 底盘 vx/vy 加速度平滑上限，单位 m/s^2。
const float kMaxAngularAccelRadps2 = 3.0f;        ///< 底盘 wz 角加速度平滑上限，单位 rad/s^2。
const uint16_t kChassisUpdateIntervalMs = 20;     ///< 底盘控制器最小输出周期，单位 ms。

/** @brief 三轮速度外环 PID 参数和输出限幅配置。 */
const WheelSpeedPidConfig kWheelSpeedPidConfig = {
    0.05f, ///< kp：按目标轮速和反馈轮速的偏差生成修正量。
    0.0f,  ///< ki：用于累计误差并补偿长期稳态偏差。
    0.0f,  ///< kd：用于根据误差变化趋势抑制过冲。
    300,   ///< maxCorrection：单轮 PID 修正量上限，单位为驱动板命令值。
    800,   ///< integralLimit：积分累计限幅，防止长时间误差导致过大修正。
    2      ///< deadband：反馈误差死区，小误差不修正以减少抖动。
};
const uint8_t kCommandBufferSize = UARTHostPC::kLineBufferSize; ///< 接收一行上位机命令的缓冲区长度。
const uint8_t kReplyBufferSize = 112;                           ///< 格式化 UART 响应和遥测文本的缓冲区长度。

UARTHostPC uartHostPC(Serial);             ///< 与上位机通信的硬件串口封装。
SR04Sensor frontRangeSensor;               ///< 前向 HC-SR04 测距传感器。
ObstacleSafety obstacleSafety;             ///< 超声波障碍物急停状态机。
IICHallMotorDriver motorDriver;            ///< IIC 三轮霍尔电机驱动板接口。
ChassisMotionControl chassisMotionControl; ///< 底盘目标速度限幅、平滑和运动学控制器。
WheelSpeedPid wheelSpeedPid;               ///< 基于驱动板反馈的三轮速度外环 PID。

char commandBuffer[kCommandBufferSize]; ///< 保存从 UART 收到的一整行命令。
char replyBuffer[kReplyBufferSize];     ///< 保存 snprintf 生成的响应或遥测文本。
uint16_t lastRawDistanceMm = 0;         ///< 最近一次 HC-SR04 原始距离，0 表示无效或超时。

/** @brief 上位机命令协议入口，负责解析命令和发送文本响应。 */
MasterCommand masterCommand(
    uartHostPC,
    frontRangeSensor,
    obstacleSafety,
    chassisMotionControl,
    replyBuffer,
    sizeof(replyBuffer),
    lastRawDistanceMm);

/** @brief 超声波避障运行循环，负责采样调度并返回安全事件。 */
ObstacleMonitor obstacleMonitor(
    frontRangeSensor,
    obstacleSafety,
    lastRawDistanceMm);
}

/**
 * @brief 消费超声波安全事件，并转成底盘动作与上位机消息。
 * @param now 当前 millis 时间戳。
 */
static void updateObstacleSafety(uint32_t now) {
    const ObstacleMonitorResult result = obstacleMonitor.update(now); ///< 本次超声波采样调度产生的安全事件和上报请求。
    if (!result.sampled) {
        return;
    }

    if (result.safety.event == kObstacleSafetyStop) {
        /// 进入阻挡状态时立即让底盘模块执行本地急停，并锁住后续运动命令。
        if (chassisMotionControl.stopForSafety() == kChassisCommandDriverError) {
            masterCommand.sendDriverError("SAFETY");
        }
        masterCommand.sendObstacleEvent(kObstacleSafetyStop);
    } else if (result.safety.event == kObstacleSafetyClear) {
        /// 解除阻挡只清除安全锁，不主动恢复之前的运动目标。
        chassisMotionControl.setSafetyBlocked(false);
        masterCommand.sendObstacleEvent(kObstacleSafetyClear);
    } else if (result.shouldRefreshStop) {
        /// 阻挡期间定期重复 stop，降低单次 IIC 写入失败导致未停稳的风险。
        if (chassisMotionControl.stopForSafety() == kChassisCommandDriverError) {
            masterCommand.sendDriverError("SAFETY");
        }
    }

    if (result.shouldReport) {
        masterCommand.sendRangeTelemetry(result.rawDistanceMm);
    }
}

/**
 * @brief 推进底盘周期输出并报告驱动板错误。
 * @param now 当前 millis 时间戳。
 */
static void updateChassisMotion(uint32_t now) {
    const ChassisMotionUpdateResult result = chassisMotionControl.updateDriver(now); ///< 本次底盘周期输出结果。
    if (result == kChassisMotionDriverError) {
        masterCommand.sendDriverError("CHASSIS");
    }
}

/** @brief 处理 UART 输入溢出和完整命令行。 */
static void updateHostCommand() {
    if (uartHostPC.overflowed()) {
        uartHostPC.clearOverflow();
        masterCommand.sendError(F("ERR UART overflow"));
    }

    if (uartHostPC.readLine(commandBuffer, sizeof(commandBuffer))) {
        masterCommand.handle(commandBuffer);
    }
}

void setup() {
    uartHostPC.init(kHostBaudRate);
    frontRangeSensor.init(kSR04TriggerPin, kSR04EchoPin);
    obstacleSafety.configure(kObstacleStopDistanceMm, kObstacleClearDistanceMm, kObstacleStopSamples, kObstacleClearSamples, kObstacleHardStopDistanceMm);
    obstacleMonitor.configure(kRangeSampleIntervalMs, kRangeReportIntervalMs, kBlockedStopRefreshMs);
    motorDriver.init(kMotorDriverAddress, kMotorDriverIicClockHz);
    chassisMotionControl.attachDriver(motorDriver, wheelSpeedPid);
    chassisMotionControl.configureKinematics(kWheelBaseRadiusM, kWheelRadiusM, kMaxWheelOmegaRadps, kWheelCommandScale);
    chassisMotionControl.configureLimits(kMaxLinearMps, kMaxAngularRadps);
    chassisMotionControl.configureSmoothing(kMaxLinearAccelMps2, kMaxAngularAccelRadps2, kChassisUpdateIntervalMs);
    chassisMotionControl.configureCommandTimeout(kChassisCommandTimeoutMs);
    chassisMotionControl.reset(millis());
    wheelSpeedPid.configure(kWheelSpeedPidConfig);
    wheelSpeedPid.reset();
    chassisMotionControl.stopDriver();
    uartHostPC.sendLine(F("OK BOOT AmseokBot-Milo SlaveDevice"));
}

void loop() {
    const uint32_t now = millis();
    updateObstacleSafety(now);
    updateChassisMotion(now);
    updateHostCommand();
}
