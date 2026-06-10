/** @file include/core/chassis/pid.h
 *  @brief 轮速 PID 接口定义。
 */
#ifndef CORE_CHASSIS_PID_H
#define CORE_CHASSIS_PID_H

#include <Arduino.h>

/** @brief 三轮速度外环 PID 参数和输出限幅。 */
struct WheelSpeedPidConfig {
    float kp;              ///< 比例项权重，根据当前速度误差直接修正输出。
    float ki;              ///< 积分项权重，用累计误差补偿长期稳态偏差。
    float kd;              ///< 微分项权重，根据误差变化趋势抑制过冲。
    int16_t maxCorrection; ///< 单次 PID 最大修正量，单位为驱动板速度命令值。
    int16_t integralLimit; ///< 积分项限幅，防止长时间堵转后积分过大。
    int16_t deadband;      ///< 误差死区，单位为驱动板速度命令值，用于抑制小抖动。
};

/** @brief 根据目标轮速和霍尔反馈轮速输出修正后的驱动板命令。 */
class WheelSpeedPid {
public:
    static const uint8_t kWheelCount = 3; ///< 闭环控制的轮子数量。

    WheelSpeedPid();

    /**
     * @brief 写入 PID 参数和输出限幅。
     * @param config PID 参数和限幅配置。
     */
    void configure(const WheelSpeedPidConfig& config);
    /** @brief 清空积分、上一周期误差和时间基准。 */
    void reset();
    /**
     * @brief 根据目标和反馈计算三轮修正后命令。
     * @param target 三轮目标速度命令。
     * @param feedback 三轮反馈速度命令。
     * @param nowMs 当前 millis 时间戳。
     * @param output 写入三轮修正后命令。
     */
    void update(const int16_t target[kWheelCount], const int16_t feedback[kWheelCount], uint32_t nowMs, int16_t output[kWheelCount]);

private:
    WheelSpeedPidConfig config_;  ///< 当前 PID 参数和限幅配置。
    float integral_[kWheelCount]; ///< 三轮积分累计误差。
    float lastError_[kWheelCount]; ///< 三轮上一周期误差，用于计算微分项。
    uint32_t lastUpdateMs_;       ///< 上一次 PID 计算的 millis 时间戳。
    bool hasLast_;                ///< 是否已有上一周期数据，决定 dt 默认值。

    /**
     * @brief 将浮点值限制在闭区间内。
     * @param value 输入值。
     * @param low 下限。
     * @param high 上限。
     * @return 限幅后的值。
     */
    static float clampFloat(float value, float low, float high);
    /**
     * @brief 将长整型命令限制到驱动板 int16 命令范围。
     * @param value 输入命令。
     * @return 限幅后的 int16 命令。
     */
    static int16_t clampCommand(long value);
};

#endif
