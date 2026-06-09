/** @file command.h
 *  @brief 上位机 UART 文本命令处理器。
 */
#ifndef CORE_MASTER_COMMAND_H
#define CORE_MASTER_COMMAND_H

#include <Arduino.h>
#include <stdint.h>

#include "core/chassis/control.h"
#include "core/sensor/ultrasonic/safety.h"
#include "driver/sr04.h"
#include "driver/uart.h"

/** @brief 解析上位机文本命令，并调用底盘和传感器模块执行控制协议。 */
class MasterCommand {
public:
    /**
     * @brief 绑定命令处理所需的通信、传感器、安全状态和底盘控制对象。
     * @param uartHostPC 上位机 UART 行协议接口。
     * @param rangeSensor 前向测距传感器。
     * @param obstacleSafety 超声波安全状态机。
     * @param chassisMotionControl 底盘控制器。
     * @param replyBuffer 响应格式化缓冲区。
     * @param replyBufferSize 响应格式化缓冲区长度。
     * @param lastRawDistanceMm 最近一次原始测距引用。
     */
    MasterCommand(
        UARTHostPC& uartHostPC,
        SR04Sensor& rangeSensor,
        ObstacleSafety& obstacleSafety,
        ChassisMotionControl& chassisMotionControl,
        char* replyBuffer,
        uint8_t replyBufferSize,
        uint16_t& lastRawDistanceMm);

    /**
     * @brief 解析并处理一整行命令。
     * @param line 可写命令行缓冲区，函数内部会使用 strtok 修改内容。
     */
    void handle(char* line);
    /**
     * @brief 向上位机发送固定错误文本。
     * @param message Flash 字符串错误文本。
     */
    void sendError(const __FlashStringHelper* message);
    /**
     * @brief 按统一格式发送驱动板错误。
     * @param action 发生错误的动作名。
     */
    void sendDriverError(const char* action);
    /**
     * @brief 发送 SR04 遥测文本。
     * @param rawDistanceMm 原始测距，单位 mm。
     */
    void sendRangeTelemetry(uint16_t rawDistanceMm);
    /**
     * @brief 发送障碍物 stop/clear 事件文本。
     * @param event 障碍物安全事件。
     */
    void sendObstacleEvent(ObstacleSafetyEvent event);

private:
    UARTHostPC& uartHostPC_;                    ///< 上位机 UART 行协议接口。
    SR04Sensor& rangeSensor_;                   ///< SR04? 命令使用的前向测距传感器。
    ObstacleSafety& obstacleSafety_;            ///< 生成安全状态响应和阻挡拒绝原因。
    ChassisMotionControl& chassisMotionControl_; ///< 执行底盘命令和读取驱动板状态。
    char* replyBuffer_;                         ///< 复用的响应格式化缓冲区。
    uint8_t replyBufferSize_;                   ///< 响应格式化缓冲区长度。
    uint16_t& lastRawDistanceMm_;               ///< 最近一次原始测距，供命令读取和遥测共享。

    /**
     * @brief 向上位机发送固定 OK 文本。
     * @param message Flash 字符串 OK 文本。
     */
    void sendOk(const __FlashStringHelper* message);
    /** @brief 处理 SR04? 命令。 */
    void handleRange();
    /**
     * @brief 处理 MOTOR 命令。
     * @param firstArg 第一个轮速参数 token。
     */
    void handleMotor(char* firstArg);
    /**
     * @brief 处理 CHASSIS 命令。
     * @param firstArg 第一个底盘速度参数 token。
     */
    void handleChassis(char* firstArg);
    /**
     * @brief 处理 ENABLE 命令。
     * @param arg 使能参数 token。
     */
    void handleEnable(char* arg);
    /** @brief 处理 STATUS? 命令。 */
    void handleStatus();
    /** @brief 处理 STOP 命令。 */
    void handleStop();
    /** @brief 处理 SAFETY? 命令。 */
    void handleSafety();
};

#endif
