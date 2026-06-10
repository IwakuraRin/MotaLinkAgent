/** @file src/core/chassis/kinematics.cpp
 *  @brief 底盘运动学接口实现。
 */
#include "core/chassis/kinematics.h"

#include <math.h>

namespace {
const int16_t kWheelAngleDeg[OmniTriangleKinematics::kWheelCount] = {0, 120, -121}; ///< 三轮滚动切向角，单位 degree，来自底盘几何标定。
const float kPi = 3.1415927f;                                                       ///< 角度和弧度换算使用的 float 精度圆周率。
}

OmniTriangleKinematics::OmniTriangleKinematics()
    : wheelBaseRadiusM_(0.1337147f),
      wheelRadiusM_(0.0425f),
      maxWheelOmegaRadps_(25.10f),
      wheelCommandScale_(100.0f) {}

void OmniTriangleKinematics::configure(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale) {
    wheelBaseRadiusM_ = wheelBaseRadiusM;
    wheelRadiusM_ = wheelRadiusM;
    maxWheelOmegaRadps_ = maxWheelOmegaRadps;
    wheelCommandScale_ = wheelCommandScale;
}

void OmniTriangleKinematics::wheelCommandFromTwist(const ChassisTwist& twist, ChassisWheelCommand& output) const {
    float omega[kWheelCount] = {0.0f, 0.0f, 0.0f}; ///< 逆运动学得到的三轮角速度，单位 rad/s。
    wheelOmegaFromTwist(twist, omega);
    for (uint8_t i = 0; i < kWheelCount; ++i) {
        output.omegaRadps[i] = omega[i];
        const float scaled = clampFloat(roundf(omega[i] * wheelCommandScale_), -32767.0f, 32767.0f); ///< IIC 驱动板协议使用 int16，保留 -32768 作为异常空间。
        output.command[i] = static_cast<int16_t>(scaled);
    }
}

void OmniTriangleKinematics::wheelOmegaFromTwist(const ChassisTwist& twist, float omega[kWheelCount]) const {
    float largest = 0.0f; ///< 当前三轮角速度绝对值最大值，用于保持运动方向的等比例限幅。
    for (uint8_t i = 0; i < kWheelCount; ++i) {
        const float theta = static_cast<float>(kWheelAngleDeg[i]) * kPi / 180.0f; ///< 当前轮滚动切向角，单位 rad。
        const float linearSpeed = (
            -sinf(theta) * twist.vxMps +
            cosf(theta) * twist.vyMps +
            wheelBaseRadiusM_ * twist.wzRadps); ///< 当前轮接地点沿滚动方向的线速度，单位 m/s。
        omega[i] = linearSpeed / wheelRadiusM_;
        largest = max(largest, fabs(omega[i]));
    }

    if (largest > maxWheelOmegaRadps_ && largest > 1.0e-6f) {
        const float scale = maxWheelOmegaRadps_ / largest; ///< 三轮共同缩放比例，避免单轮超过速度上限。
        for (uint8_t i = 0; i < kWheelCount; ++i) {
            omega[i] *= scale;
        }
    }
}

void OmniTriangleKinematics::twistFromWheelCommand(const int16_t wheelCommand[kWheelCount], ChassisTwist& twist) const {
    const float w0 = (wheelCommand[0] / wheelCommandScale_) * wheelRadiusM_;  // 0 号轮滚动方向线速度，单位 m/s。
    const float w1 = (wheelCommand[1] / wheelCommandScale_) * wheelRadiusM_;  // 1 号轮滚动方向线速度，单位 m/s。
    const float w2 = (wheelCommand[2] / wheelCommandScale_) * wheelRadiusM_;  // 2 号轮滚动方向线速度，单位 m/s。
    const float theta0 = static_cast<float>(kWheelAngleDeg[0]) * kPi / 180.0f;  // 0 号轮滚动切向角，单位 rad。
    const float theta1 = static_cast<float>(kWheelAngleDeg[1]) * kPi / 180.0f;  // 1 号轮滚动切向角，单位 rad。

    const float a0 = -sinf(theta0);      // 0 号轮方程中 vx 的系数。
    const float b0 = cosf(theta0);       // 0 号轮方程中 vy 的系数。
    const float a1 = -sinf(theta1);      // 1 号轮方程中 vx 的系数。
    const float b1 = cosf(theta1);       // 1 号轮方程中 vy 的系数。
    const float c = wheelBaseRadiusM_;   // 角速度 wz 映射到轮接地点线速度的力臂。

    const float det = a0 * b1 - a1 * b0;             // 二元线速度方程组行列式，过小表示几何矩阵退化。
    const float avgRotate = (w0 + w1 + w2) / 3.0f;   // 三轮公共速度分量，近似对应原地旋转速度。
    twist.wzRadps = c > 1.0e-6f ? avgRotate / c : 0.0f;

    const float l0 = w0 - c * twist.wzRadps;  // 去掉旋转分量后的 0 号轮平移约束。
    const float l1 = w1 - c * twist.wzRadps;  // 去掉旋转分量后的 1 号轮平移约束。
    if (fabs(det) < 1.0e-6f) {
        twist.vxMps = 0.0f;
        twist.vyMps = 0.0f;
        return;
    }
    twist.vxMps = (l0 * b1 - l1 * b0) / det;
    twist.vyMps = (a0 * l1 - a1 * l0) / det;
}

float OmniTriangleKinematics::clampFloat(float value, float low, float high) {
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}
