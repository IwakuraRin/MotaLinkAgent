/** @file include/core/chassis/kinematics.h
 *  @brief 底盘运动学接口定义。
 */
#ifndef CORE_CHASSIS_KINEMATICS_H
#define CORE_CHASSIS_KINEMATICS_H

#include <Arduino.h>

/** @brief 机体系底盘速度，vx 向前、vy 向左、wz 逆时针。 */
struct ChassisTwist {
    float vxMps;   ///< 车体 X 轴线速度，单位 m/s，正方向为前进。
    float vyMps;   ///< 车体 Y 轴线速度，单位 m/s，正方向为左移。
    float wzRadps; ///< 车体 Z 轴角速度，单位 rad/s，正方向为逆时针。
};

/** @brief 三轮驱动命令及对应物理角速度。 */
struct ChassisWheelCommand {
    int16_t command[3];  ///< 发给 IIC 驱动板的三轮目标值，顺序与驱动板协议一致。
    float omegaRadps[3]; ///< 三轮物理角速度，单位 rad/s，用于调试和状态输出。
};

/** @brief 根据三轮安装角、底盘半径和轮半径执行正逆运动学。 */
class OmniTriangleKinematics {
public:
    static const uint8_t kWheelCount = 3; ///< 底盘固定为三颗全向轮。

    OmniTriangleKinematics();

    /**
     * @brief 配置底盘几何参数和驱动板命令缩放比例。
     * @param wheelBaseRadiusM 底盘中心到轮接地点距离，单位 m。
     * @param wheelRadiusM 全向轮半径，单位 m。
     * @param maxWheelOmegaRadps 单轮最大目标角速度，单位 rad/s。
     * @param wheelCommandScale 轮角速度 rad/s 转驱动板命令值的比例。
     */
    void configure(float wheelBaseRadiusM, float wheelRadiusM, float maxWheelOmegaRadps, float wheelCommandScale);
    /**
     * @brief 把底盘速度转换为三轮角速度和驱动板命令。
     * @param twist 输入底盘速度。
     * @param output 写入三轮命令和物理角速度。
     */
    void wheelCommandFromTwist(const ChassisTwist& twist, ChassisWheelCommand& output) const;
    /**
     * @brief 把三轮驱动板命令近似还原为底盘速度。
     * @param wheelCommand 三轮驱动板命令。
     * @param twist 写入还原得到的底盘速度。
     */
    void twistFromWheelCommand(const int16_t wheelCommand[kWheelCount], ChassisTwist& twist) const;

private:
    float wheelBaseRadiusM_;   ///< 底盘中心到轮接地点距离，单位 m。
    float wheelRadiusM_;       ///< 全向轮半径，单位 m。
    float maxWheelOmegaRadps_; ///< 单轮最大允许角速度，单位 rad/s，用于等比例限幅。
    float wheelCommandScale_;  ///< rad/s 到驱动板 int16 命令值的比例。

    /**
     * @brief 把底盘速度转换为三轮物理角速度。
     * @param twist 输入底盘速度。
     * @param omega 写入三轮角速度，单位 rad/s。
     */
    void wheelOmegaFromTwist(const ChassisTwist& twist, float omega[kWheelCount]) const;
    /**
     * @brief 将浮点值限制在闭区间内。
     * @param value 输入值。
     * @param low 下限。
     * @param high 上限。
     * @return 限幅后的值。
     */
    static float clampFloat(float value, float low, float high);
};

#endif
