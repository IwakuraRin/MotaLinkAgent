/*
|--------------------------------------------------------------------------
| 安全保护模块
|--------------------------------------------------------------------------
| 校验并限制底盘速度和机械臂角度，避免 Go API 传入危险或无效命令。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <math.h>
#include <stdio.h>

/*
|--------------------------------------------------------------------------
| 通用限幅工具
|--------------------------------------------------------------------------
| 把输入值限制在指定范围内，供底盘和机械臂模块复用。
|--------------------------------------------------------------------------
*/
static double clamp_value(double value, double min_value, double max_value) {
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

/*
|--------------------------------------------------------------------------
| 底盘速度安全检查
|--------------------------------------------------------------------------
| 拒绝 NaN/Inf，并对线速度和角速度做保守限幅。
|--------------------------------------------------------------------------
*/
bool amseokbot_check_chassis_command(amseokbot_chassis_command_t *command, char *error, size_t error_size) {
    if (command == NULL) {
        snprintf(error, error_size, "missing chassis command");
        return false;
    }
    if (!isfinite(command->vx_mps) || !isfinite(command->vy_mps) || !isfinite(command->wz_radps)) {
        snprintf(error, error_size, "invalid chassis command number");
        return false;
    }
    command->vx_mps = clamp_value(command->vx_mps, -AMSEOKBOT_MAX_LINEAR_MPS, AMSEOKBOT_MAX_LINEAR_MPS);
    command->vy_mps = clamp_value(command->vy_mps, -AMSEOKBOT_MAX_LINEAR_MPS, AMSEOKBOT_MAX_LINEAR_MPS);
    command->wz_radps = clamp_value(command->wz_radps, -AMSEOKBOT_MAX_ANGULAR_RADPS, AMSEOKBOT_MAX_ANGULAR_RADPS);
    return true;
}

/*
|--------------------------------------------------------------------------
| 机械臂角度安全检查
|--------------------------------------------------------------------------
| 使用第一版保守角度范围，后续根据结构限位和电机减速比继续收紧。
|--------------------------------------------------------------------------
*/
bool amseokbot_check_arm_command(amseokbot_arm_command_t *command, char *error, size_t error_size) {
    if (command == NULL) {
        snprintf(error, error_size, "missing arm command");
        return false;
    }
    if (!isfinite(command->shoulder_yaw_deg) || !isfinite(command->shoulder_pitch_deg) || !isfinite(command->elbow_deg) || !isfinite(command->wrist_deg)) {
        snprintf(error, error_size, "invalid arm command number");
        return false;
    }
    command->shoulder_yaw_deg = clamp_value(command->shoulder_yaw_deg, -120.0, 120.0);
    command->shoulder_pitch_deg = clamp_value(command->shoulder_pitch_deg, -20.0, 95.0);
    command->elbow_deg = clamp_value(command->elbow_deg, -135.0, 135.0);
    command->wrist_deg = clamp_value(command->wrist_deg, -120.0, 120.0);
    return true;
}
