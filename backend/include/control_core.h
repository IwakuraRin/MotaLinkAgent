/*
|--------------------------------------------------------------------------
| AmseokBot C 控制核心公共接口
|--------------------------------------------------------------------------
| 声明电机控制、串口协议、底盘运动、机械臂控制和安全保护使用的数据结构。
| C 层只处理低延迟硬件相关逻辑，不再承载 HTTP、登录和前端文件服务。
|--------------------------------------------------------------------------
*/

#ifndef AMSEOKBOT_CONTROL_CORE_H
#define AMSEOKBOT_CONTROL_CORE_H

#include <stdbool.h>
#include <stddef.h>

/*
|--------------------------------------------------------------------------
| 控制核心常量
|--------------------------------------------------------------------------
| 保存三全向轮底盘和安全限幅使用的默认机械参数。
|--------------------------------------------------------------------------
*/
#define AMSEOKBOT_WHEEL_COUNT 3
#define AMSEOKBOT_WHEEL_RADIUS_M 0.0425
#define AMSEOKBOT_CHASSIS_RADIUS_M 0.1337147
#define AMSEOKBOT_MAX_LINEAR_MPS 0.80
#define AMSEOKBOT_MAX_ANGULAR_RADPS 2.50
#define AMSEOKBOT_MAX_WHEEL_RADPS 35.0

/*
|--------------------------------------------------------------------------
| 底盘速度命令
|--------------------------------------------------------------------------
| vx 向前速度，vy 横向速度，wz 绕机体中心旋转角速度。
|--------------------------------------------------------------------------
*/
typedef struct {
    double vx_mps;
    double vy_mps;
    double wz_radps;
} amseokbot_chassis_command_t;

/*
|--------------------------------------------------------------------------
| 底盘轮速输出
|--------------------------------------------------------------------------
| 保存右前轮、左前轮和后轮目标角速度。
|--------------------------------------------------------------------------
*/
typedef struct {
    double right_front_radps;
    double left_front_radps;
    double rear_radps;
} amseokbot_wheel_speed_t;

/*
|--------------------------------------------------------------------------
| 机械臂关节命令
|--------------------------------------------------------------------------
| 保存肩部双电机、肘部电机和腕部电机的目标角度。
|--------------------------------------------------------------------------
*/
typedef struct {
    double shoulder_yaw_deg;
    double shoulder_pitch_deg;
    double elbow_deg;
    double wrist_deg;
} amseokbot_arm_command_t;

/*
|--------------------------------------------------------------------------
| 串口协议帧
|--------------------------------------------------------------------------
| 保存要发送给下位机的文本协议帧，Go 层后续可以把它写入串口。
|--------------------------------------------------------------------------
*/
typedef struct {
    char text[256];
} amseokbot_serial_frame_t;

bool amseokbot_check_chassis_command(amseokbot_chassis_command_t *command, char *error, size_t error_size);
bool amseokbot_check_arm_command(amseokbot_arm_command_t *command, char *error, size_t error_size);
amseokbot_wheel_speed_t amseokbot_compute_wheel_speed(const amseokbot_chassis_command_t *command);
void amseokbot_build_chassis_frame(const amseokbot_wheel_speed_t *speed, amseokbot_serial_frame_t *frame);
void amseokbot_build_arm_frame(const amseokbot_arm_command_t *command, amseokbot_serial_frame_t *frame);
void amseokbot_build_stop_frame(amseokbot_serial_frame_t *frame);
void amseokbot_print_health_json(void);
void amseokbot_print_chassis_json(const amseokbot_chassis_command_t *command, const amseokbot_wheel_speed_t *speed, const amseokbot_serial_frame_t *frame);
void amseokbot_print_arm_json(const amseokbot_arm_command_t *command, const amseokbot_serial_frame_t *frame);
void amseokbot_print_stop_json(const amseokbot_serial_frame_t *frame);

#endif
