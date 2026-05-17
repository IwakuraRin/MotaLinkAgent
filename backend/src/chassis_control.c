/*
|--------------------------------------------------------------------------
| 三全向轮底盘运动控制模块
|--------------------------------------------------------------------------
| 根据机体系速度计算三个全向轮目标角速度，输出给串口协议模块组包。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
|--------------------------------------------------------------------------
| 轮组安装角
|--------------------------------------------------------------------------
| 来自三角底盘 CAD 参数：右前、左前、后轮的滚动方向角。
|--------------------------------------------------------------------------
*/
static const double WHEEL_ANGLE_DEG[AMSEOKBOT_WHEEL_COUNT] = {29.9459, 149.6229, -90.8207};

/*
|--------------------------------------------------------------------------
| 单轮速度限幅
|--------------------------------------------------------------------------
| 保证输出角速度不超过默认安全上限，避免下位机收到过大目标值。
|--------------------------------------------------------------------------
*/
static double clamp_wheel_speed(double value) {
    if (value > AMSEOKBOT_MAX_WHEEL_RADPS) {
        return AMSEOKBOT_MAX_WHEEL_RADPS;
    }
    if (value < -AMSEOKBOT_MAX_WHEEL_RADPS) {
        return -AMSEOKBOT_MAX_WHEEL_RADPS;
    }
    return value;
}

/*
|--------------------------------------------------------------------------
| 底盘逆运动学
|--------------------------------------------------------------------------
| 把 vx、vy、wz 转为三个轮子的目标角速度，单位为 rad/s。
|--------------------------------------------------------------------------
*/
amseokbot_wheel_speed_t amseokbot_compute_wheel_speed(const amseokbot_chassis_command_t *command) {
    double wheel_speed[AMSEOKBOT_WHEEL_COUNT] = {0.0, 0.0, 0.0};
    for (int i = 0; i < AMSEOKBOT_WHEEL_COUNT; i++) {
        double angle_rad = WHEEL_ANGLE_DEG[i] * M_PI / 180.0;
        double tangent_x = -sin(angle_rad);
        double tangent_y = cos(angle_rad);
        double linear = tangent_x * command->vx_mps + tangent_y * command->vy_mps;
        double rotate = AMSEOKBOT_CHASSIS_RADIUS_M * command->wz_radps;
        wheel_speed[i] = clamp_wheel_speed((linear + rotate) / AMSEOKBOT_WHEEL_RADIUS_M);
    }
    return (amseokbot_wheel_speed_t){
        .right_front_radps = wheel_speed[0],
        .left_front_radps = wheel_speed[1],
        .rear_radps = wheel_speed[2],
    };
}
