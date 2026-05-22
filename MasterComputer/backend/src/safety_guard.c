/*
|--------------------------------------------------------------------------
| 安全保护模块
|--------------------------------------------------------------------------
| 校验并限制底盘速度，避免 Go API 传入危险或无效命令。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <math.h>
#include <stdio.h>

/*
|--------------------------------------------------------------------------
| 通用限幅工具
|--------------------------------------------------------------------------
| 把输入值限制在指定范围内，供底盘模块复用。
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
