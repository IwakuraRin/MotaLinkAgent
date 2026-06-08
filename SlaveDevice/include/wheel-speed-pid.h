/*
|--------------------------------------------------------------------------
| 三轮霍尔速度 PID
|--------------------------------------------------------------------------
| 使用 IIC 驱动板回传的三轮霍尔速度，对下发的三轮速度命令做轻量闭环修正。
|--------------------------------------------------------------------------
*/
#ifndef WHEEL_SPEED_PID_H
#define WHEEL_SPEED_PID_H

#include <Arduino.h>

// ==================== PID 参数 ====================
// 作用：保存三轮速度外环使用的比例、积分、微分和输出限幅参数。
// ==================================================
struct WheelSpeedPidConfig {
    float kp;                // 比例项权重，根据当前速度误差直接修正输出。
    float ki;                // 积分项权重，用累计误差补偿长期稳态偏差。
    float kd;                // 微分项权重，根据误差变化趋势抑制过冲。
    int16_t maxCorrection;   // 单次 PID 最大修正量，单位为驱动板速度命令值。
    int16_t integralLimit;   // 积分项限幅，防止长时间堵转后积分过大。
    int16_t deadband;        // 误差死区，单位为驱动板速度命令值，用于抑制小抖动。
};

// ==================== 三轮速度 PID ====================
// 作用：根据目标轮速和霍尔反馈轮速，输出修正后的驱动板速度命令。
// ======================================================
class WheelSpeedPid {
public:
    static const uint8_t kWheelCount = 3;  // 闭环控制的轮子数量。

    WheelSpeedPid();

    void configure(const WheelSpeedPidConfig& config);
    void reset();
    void update(const int16_t target[kWheelCount], const int16_t feedback[kWheelCount], uint32_t nowMs, int16_t output[kWheelCount]);

private:
    WheelSpeedPidConfig config_;       // 当前 PID 参数和限幅配置。
    float integral_[kWheelCount];      // 三轮积分累计误差。
    float lastError_[kWheelCount];     // 三轮上一周期误差，用于计算微分项。
    uint32_t lastUpdateMs_;            // 上一次 PID 计算的 millis 时间戳。
    bool hasLast_;                     // 是否已有上一周期数据，决定 dt 默认值。

    static float clampFloat(float value, float low, float high);
    static int16_t clampCommand(long value);
};

#endif
