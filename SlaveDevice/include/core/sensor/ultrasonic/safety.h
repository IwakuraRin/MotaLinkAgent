/** @file include/core/sensor/ultrasonic/safety.h
 *  @brief 障碍物安全状态机定义。
 */
#ifndef CORE_SENSOR_ULTRASONIC_SAFETY_H
#define CORE_SENSOR_ULTRASONIC_SAFETY_H

#include <Arduino.h>
#include <stdint.h>

/** @brief 一次测距更新触发的安全状态变化。 */
enum ObstacleSafetyEvent : uint8_t {
    kObstacleSafetyNone = 0,  ///< 本次更新没有发生安全状态变化。
    kObstacleSafetyStop = 1,  ///< 本次更新触发进入障碍物急停状态。
    kObstacleSafetyClear = 2  ///< 本次更新触发解除障碍物急停状态。
};

/** @brief 原始距离经过滤波和迟滞判断后的安全更新结果。 */
struct ObstacleSafetyResult {
    uint16_t rawDistanceMm;      ///< 本次输入的 HC-SR04 原始距离，单位 mm。
    uint16_t medianDistanceMm;   ///< 最近 3 次有效距离的中值结果，单位 mm。
    uint16_t filteredDistanceMm; ///< 低通滤波后的稳定距离，单位 mm。
    bool valid;                  ///< 本次原始距离是否有效，0 距离视为无效。
    bool blocked;                ///< 更新后的障碍物阻挡状态。
    ObstacleSafetyEvent event;   ///< 本次更新产生的状态变化事件。
};

/** @brief 距离和阈值快照。 */
struct ObstacleSafetyState {
    uint16_t medianDistanceMm;   ///< 最近 3 次有效距离的中值结果，单位 mm。
    uint16_t filteredDistanceMm; ///< 低通滤波后的稳定距离，单位 mm。
    uint16_t hardStopDistanceMm; ///< 原始距离触发立即急停的阈值，单位 mm。
    uint16_t stopDistanceMm;     ///< 进入阻挡状态的距离阈值，单位 mm。
    uint16_t clearDistanceMm;    ///< 解除阻挡状态的距离阈值，单位 mm。
};

/** @brief 用滤波、迟滞阈值和连续计数实现稳定的障碍物急停判断。 */
class ObstacleSafety {
public:
    ObstacleSafety();

    /**
     * @brief 配置进入阻挡、解除阻挡和极近立即急停的阈值及连续样本数。
     * @param stopDistanceMm 进入阻挡状态的距离阈值，单位 mm。
     * @param clearDistanceMm 解除阻挡状态的距离阈值，单位 mm。
     * @param stopSamples 进入阻挡需要连续满足的采样次数。
     * @param clearSamples 解除阻挡需要连续满足的采样次数。
     * @param hardStopDistanceMm 原始距离立即急停阈值，单位 mm。
     */
    void configure(
        uint16_t stopDistanceMm,
        uint16_t clearDistanceMm,
        uint8_t stopSamples,
        uint8_t clearSamples,
        uint16_t hardStopDistanceMm);
    /**
     * @brief 输入一次原始距离，更新滤波状态并返回安全事件。
     * @param rawDistanceMm 原始测距，单位 mm；0 表示无效。
     * @return 本次安全状态更新结果。
     */
    ObstacleSafetyResult update(uint16_t rawDistanceMm);
    /**
     * @brief 当前是否处于障碍物阻挡状态。
     * @return 阻挡中返回 true。
     */
    bool blocked() const;
    /**
     * @brief 当前距离和阈值快照。
     * @return 当前滤波距离和阈值。
     */
    ObstacleSafetyState state() const;

private:
    static const uint8_t kMedianSampleCount = 3; ///< 中值滤波窗口大小。

    uint16_t stopDistanceMm_;      ///< 进入阻挡状态的距离阈值，单位 mm。
    uint16_t clearDistanceMm_;     ///< 解除阻挡状态的距离阈值，单位 mm。
    uint16_t hardStopDistanceMm_;  ///< 立即急停的原始距离阈值，单位 mm。
    uint8_t stopSamples_;          ///< 稳定近距离需要连续满足的采样次数。
    uint8_t clearSamples_;         ///< 稳定远距离需要连续满足的采样次数。
    uint8_t nearCount_;            ///< 当前连续近距离采样计数。
    uint8_t clearCount_;           ///< 当前连续远距离采样计数。
    uint16_t samples_[kMedianSampleCount]; ///< 最近有效距离的环形采样窗口，单位 mm。
    uint8_t sampleCount_;          ///< 采样窗口内已有的有效样本数量。
    uint8_t sampleIndex_;          ///< 下一次写入 samples_ 的环形下标。
    uint16_t medianDistanceMm_;    ///< 最近一次中值滤波结果，单位 mm。
    uint16_t filteredDistanceMm_;  ///< 最近一次低通滤波结果，单位 mm。
    bool hasFilter_;               ///< 是否已经有可用的低通滤波初值。
    bool blocked_;                 ///< 当前是否处于障碍物急停阻挡状态。

    /**
     * @brief 保存有效距离并返回当前中值滤波结果。
     * @param rawDistanceMm 原始有效测距，单位 mm。
     * @return 当前中值滤波距离，单位 mm。
     */
    uint16_t pushSampleAndMedian(uint16_t rawDistanceMm);
    /**
     * @brief 返回三个 uint16 样本的中值。
     * @param a 样本 a。
     * @param b 样本 b。
     * @param c 样本 c。
     * @return 三个样本的中值。
     */
    static uint16_t median3(uint16_t a, uint16_t b, uint16_t c);
};

#endif
