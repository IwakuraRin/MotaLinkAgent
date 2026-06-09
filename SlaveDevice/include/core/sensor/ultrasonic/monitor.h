/** @file monitor.h
 *  @brief 超声波避障运行循环，负责采样调度并返回安全事件和上报节拍。
 */
#ifndef CORE_SENSOR_ULTRASONIC_MONITOR_H
#define CORE_SENSOR_ULTRASONIC_MONITOR_H

#include <Arduino.h>
#include <stdint.h>

#include "core/sensor/ultrasonic/safety.h"
#include "driver/sr04.h"

/** @brief 一次超声波运行循环的输出结果。 */
struct ObstacleMonitorResult {
    bool sampled;            ///< 本次是否实际读取了 HC-SR04。
    bool shouldReport;       ///< 本次是否需要向上位机上报遥测。
    bool shouldRefreshStop;  ///< 阻挡期间是否需要刷新急停输出。
    uint16_t rawDistanceMm;  ///< 最近一次原始距离，单位 mm。
    ObstacleSafetyResult safety; ///< 本次测距更新后的避障状态。
};

/** @brief 周期读取超声波传感器，并维护遥测和急停刷新节拍。 */
class ObstacleMonitor {
public:
    /**
     * @brief 绑定前向测距传感器、安全状态机和共享的最近原始距离。
     * @param rangeSensor 前向 HC-SR04 传感器。
     * @param obstacleSafety 超声波障碍物安全状态机。
     * @param lastRawDistanceMm 最近一次原始测距引用。
     */
    ObstacleMonitor(
        SR04Sensor& rangeSensor,
        ObstacleSafety& obstacleSafety,
        uint16_t& lastRawDistanceMm);

    /**
     * @brief 配置采样周期、遥测上报周期和阻挡期间 stop 刷新周期。
     * @param sampleIntervalMs 两次实际测距之间的最小间隔，单位 ms。
     * @param reportIntervalMs 遥测上报请求间隔，单位 ms。
     * @param stopRefreshMs 阻挡期间刷新 stop 请求的间隔，单位 ms。
     */
    void configure(uint32_t sampleIntervalMs, uint32_t reportIntervalMs, uint32_t stopRefreshMs);

    /**
     * @brief 按当前时间推进一次超声波采样调度。
     * @param nowMs 当前 millis 时间戳。
     * @return 本次运行循环的采样、安全事件和上报请求。
     */
    ObstacleMonitorResult update(uint32_t nowMs);

private:
    SR04Sensor& rangeSensor_;           ///< 前向 HC-SR04 传感器。
    ObstacleSafety& obstacleSafety_;    ///< 原始距离到阻挡状态的安全状态机。
    uint16_t& lastRawDistanceMm_;       ///< 与命令处理共享的最近一次原始距离。
    uint32_t sampleIntervalMs_;         ///< 两次实际测距之间的最小间隔。
    uint32_t reportIntervalMs_;         ///< 遥测上报请求间隔。
    uint32_t stopRefreshMs_;            ///< 阻挡期间刷新 stop 请求的间隔。
    uint32_t lastSampleMs_;             ///< 上一次实际测距的 millis 时间戳。
    uint32_t lastReportMs_;             ///< 上一次请求遥测上报的 millis 时间戳。
    uint32_t lastStopRefreshMs_;        ///< 上一次请求刷新 stop 的 millis 时间戳。
};

#endif
