/** @file monitor.cpp
 *  @brief 超声波采样调度和避障运行结果生成。
 */
#include "core/sensor/ultrasonic/monitor.h"

/** @brief 初始化采样调度依赖和默认周期。 */
ObstacleMonitor::ObstacleMonitor(
    SR04Sensor& rangeSensor,
    ObstacleSafety& obstacleSafety,
    uint16_t& lastRawDistanceMm)
    : rangeSensor_(rangeSensor),
      obstacleSafety_(obstacleSafety),
      lastRawDistanceMm_(lastRawDistanceMm),
      sampleIntervalMs_(25),
      reportIntervalMs_(100),
      stopRefreshMs_(80),
      lastSampleMs_(0),
      lastReportMs_(0),
      lastStopRefreshMs_(0) {}

void ObstacleMonitor::configure(uint32_t sampleIntervalMs, uint32_t reportIntervalMs, uint32_t stopRefreshMs) {
    sampleIntervalMs_ = sampleIntervalMs;
    reportIntervalMs_ = reportIntervalMs;
    stopRefreshMs_ = stopRefreshMs;
}

/** @brief 到达采样周期时读取距离并更新安全状态；未到周期时返回当前状态快照。 */
ObstacleMonitorResult ObstacleMonitor::update(uint32_t nowMs) {
    ObstacleMonitorResult output = { ///< 默认返回当前安全状态；未到采样周期时不触发事件或上报。
        false,
        false,
        false,
        lastRawDistanceMm_,
        {lastRawDistanceMm_, obstacleSafety_.medianDistanceMm(), obstacleSafety_.filteredDistanceMm(), lastRawDistanceMm_ > 0, obstacleSafety_.blocked(), kObstacleSafetyNone}
    };

    if (nowMs - lastSampleMs_ < sampleIntervalMs_) {
        return output;
    }
    lastSampleMs_ = nowMs;

    lastRawDistanceMm_ = rangeSensor_.readDistanceMm();
    output.sampled = true;
    output.rawDistanceMm = lastRawDistanceMm_;
    output.safety = obstacleSafety_.update(lastRawDistanceMm_);

    if (output.safety.event == kObstacleSafetyStop) {
        /// 新进入阻挡状态时重置 stop 刷新时间，立即急停由调用者处理。
        lastStopRefreshMs_ = nowMs;
    }

    if (obstacleSafety_.blocked() && nowMs - lastStopRefreshMs_ >= stopRefreshMs_) {
        /// 阻挡持续期间周期性请求调用者重新发送 stop。
        output.shouldRefreshStop = true;
        lastStopRefreshMs_ = nowMs;
    }

    if (nowMs - lastReportMs_ >= reportIntervalMs_) {
        output.shouldReport = true;
        lastReportMs_ = nowMs;
    }

    return output;
}
