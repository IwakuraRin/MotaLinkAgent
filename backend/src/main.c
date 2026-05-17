/*
|--------------------------------------------------------------------------
| AmseokBot C 控制核心入口
|--------------------------------------------------------------------------
| 提供底盘、机械臂、急停和健康检查命令。Go API 通过本地进程调用它，
| 后续也可以替换成本地 socket 或静态库接口。
|--------------------------------------------------------------------------
*/

#include "control_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
|--------------------------------------------------------------------------
| JSON 输出模块
|--------------------------------------------------------------------------
| 统一输出 Go API 可解析的 JSON，避免 Go 层解析文本协议细节。
|--------------------------------------------------------------------------
*/
void amseokbot_print_health_json(void) {
    printf("{\"ok\":true,\"service\":\"amseokbot-control-core\",\"roles\":[\"motor\",\"serial\",\"chassis\",\"arm\",\"safety\"]}\n");
}

void amseokbot_print_chassis_json(const amseokbot_chassis_command_t *command, const amseokbot_wheel_speed_t *speed, const amseokbot_serial_frame_t *frame) {
    printf("{\"ok\":true,\"type\":\"chassis\",\"command\":{\"vx_mps\":%.6f,\"vy_mps\":%.6f,\"wz_radps\":%.6f},\"wheel_radps\":{\"right_front\":%.6f,\"left_front\":%.6f,\"rear\":%.6f},\"serial_frame\":\"%s\"}\n",
        command->vx_mps,
        command->vy_mps,
        command->wz_radps,
        speed->right_front_radps,
        speed->left_front_radps,
        speed->rear_radps,
        frame->text);
}

void amseokbot_print_arm_json(const amseokbot_arm_command_t *command, const amseokbot_serial_frame_t *frame) {
    printf("{\"ok\":true,\"type\":\"arm\",\"joint_deg\":{\"shoulder_yaw\":%.3f,\"shoulder_pitch\":%.3f,\"elbow\":%.3f,\"wrist\":%.3f},\"serial_frame\":\"%s\"}\n",
        command->shoulder_yaw_deg,
        command->shoulder_pitch_deg,
        command->elbow_deg,
        command->wrist_deg,
        frame->text);
}

void amseokbot_print_stop_json(const amseokbot_serial_frame_t *frame) {
    printf("{\"ok\":true,\"type\":\"stop\",\"serial_frame\":\"%s\"}\n", frame->text);
}

/*
|--------------------------------------------------------------------------
| 参数解析工具
|--------------------------------------------------------------------------
| 从命令行读取浮点参数，保持接口足够简单，方便 Go API 调用。
|--------------------------------------------------------------------------
*/
static double read_number_arg(int argc, char **argv, const char *name, double fallback) {
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return strtod(argv[i + 1], NULL);
        }
    }
    return fallback;
}

static int print_usage(void) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  amseokbot-control-core health\n");
    fprintf(stderr, "  amseokbot-control-core chassis --vx 0.1 --vy 0 --wz 0\n");
    fprintf(stderr, "  amseokbot-control-core arm --shoulder-yaw 0 --shoulder-pitch 20 --elbow 30 --wrist 0\n");
    fprintf(stderr, "  amseokbot-control-core stop\n");
    return 2;
}

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 分发健康检查、底盘控制、机械臂控制和停止命令。
|--------------------------------------------------------------------------
*/
int main(int argc, char **argv) {
    if (argc < 2) {
        return print_usage();
    }

    if (strcmp(argv[1], "health") == 0) {
        amseokbot_print_health_json();
        return 0;
    }

    if (strcmp(argv[1], "chassis") == 0) {
        char error[128] = {0};
        amseokbot_chassis_command_t command = {
            .vx_mps = read_number_arg(argc, argv, "--vx", 0.0),
            .vy_mps = read_number_arg(argc, argv, "--vy", 0.0),
            .wz_radps = read_number_arg(argc, argv, "--wz", 0.0),
        };
        if (!amseokbot_check_chassis_command(&command, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        amseokbot_wheel_speed_t speed = amseokbot_compute_wheel_speed(&command);
        amseokbot_serial_frame_t frame = {0};
        amseokbot_build_chassis_frame(&speed, &frame);
        amseokbot_print_chassis_json(&command, &speed, &frame);
        return 0;
    }

    if (strcmp(argv[1], "arm") == 0) {
        char error[128] = {0};
        amseokbot_arm_command_t command = {
            .shoulder_yaw_deg = read_number_arg(argc, argv, "--shoulder-yaw", 0.0),
            .shoulder_pitch_deg = read_number_arg(argc, argv, "--shoulder-pitch", 0.0),
            .elbow_deg = read_number_arg(argc, argv, "--elbow", 0.0),
            .wrist_deg = read_number_arg(argc, argv, "--wrist", 0.0),
        };
        if (!amseokbot_check_arm_command(&command, error, sizeof(error))) {
            fprintf(stderr, "%s\n", error);
            return 1;
        }
        amseokbot_serial_frame_t frame = {0};
        amseokbot_build_arm_frame(&command, &frame);
        amseokbot_print_arm_json(&command, &frame);
        return 0;
    }

    if (strcmp(argv[1], "stop") == 0) {
        amseokbot_serial_frame_t frame = {0};
        amseokbot_build_stop_frame(&frame);
        amseokbot_print_stop_json(&frame);
        return 0;
    }

    return print_usage();
}
