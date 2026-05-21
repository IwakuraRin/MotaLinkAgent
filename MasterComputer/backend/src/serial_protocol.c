/*
|--------------------------------------------------------------------------
| 串口协议模块
|--------------------------------------------------------------------------
| 把底盘、机械臂和急停命令组装成下位机可以解析的文本协议帧。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <stdio.h>

/*
|--------------------------------------------------------------------------
| 底盘协议帧
|--------------------------------------------------------------------------
| 输出三轮角速度，后续由 Go 层或 C 串口驱动写入 ATmega/ESP32。
|--------------------------------------------------------------------------
*/
void amseokbot_build_chassis_frame(const amseokbot_wheel_speed_t *speed, amseokbot_serial_frame_t *frame) {
    snprintf(frame->text, sizeof(frame->text), "CHASSIS_WHEEL_OMEGA %.6f %.6f %.6f", speed->right_front_radps, speed->left_front_radps, speed->rear_radps);
}

/*
|--------------------------------------------------------------------------
| 机械臂协议帧
|--------------------------------------------------------------------------
| 输出四个关节目标角度，具体闭环控制由下位机和驱动器负责。
|--------------------------------------------------------------------------
*/
void amseokbot_build_arm_frame(const amseokbot_arm_command_t *command, amseokbot_serial_frame_t *frame) {
    snprintf(frame->text, sizeof(frame->text), "ARM_JOINT_DEG %.3f %.3f %.3f %.3f", command->shoulder_yaw_deg, command->shoulder_pitch_deg, command->elbow_deg, command->wrist_deg);
}

/*
|--------------------------------------------------------------------------
| 停止协议帧
|--------------------------------------------------------------------------
| 输出急停/停止命令，供 Go API、ROS 或安全监控统一调用。
|--------------------------------------------------------------------------
*/
void amseokbot_build_stop_frame(amseokbot_serial_frame_t *frame) {
    snprintf(frame->text, sizeof(frame->text), "STOP_ALL");
}
