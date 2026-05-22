/*
|--------------------------------------------------------------------------
| AmseokBot C 控制核心入口
|--------------------------------------------------------------------------
| 提供底盘、急停、文件管理和健康检查命令。Go API 通过本地进程调用它，
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
    printf("{\"ok\":true,\"service\":\"amseokbot-control-core\",\"roles\":[\"motor\",\"serial\",\"chassis\",\"safety\"]}\n");
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


static const char *read_string_arg(int argc, char **argv, const char *name, const char *fallback) {
    for (int i = 0; i + 1 < argc; i++) {
        if (strcmp(argv[i], name) == 0) {
            return argv[i + 1];
        }
    }
    return fallback;
}

static bool read_bool_arg(int argc, char **argv, const char *name, bool fallback) {
    const char *value = read_string_arg(argc, argv, name, fallback ? "1" : "0");
    return strcmp(value, "1") == 0 || strcmp(value, "true") == 0 || strcmp(value, "yes") == 0;
}

static void print_json_string(const char *value) {
    putchar('"');
    if (value != NULL) {
        for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
            switch (*p) {
                case '\\': printf("\\\\"); break;
                case '"': printf("\\\""); break;
                case '\n': printf("\\n"); break;
                case '\r': printf("\\r"); break;
                case '\t': printf("\\t"); break;
                default:
                    if (*p < 0x20) {
                        printf("\\u%04x", *p);
                    } else {
                        putchar(*p);
                    }
                    break;
            }
        }
    }
    putchar('"');
}

static void print_fs_ok_json(const char *type, const char *path, const char *src, const char *dst) {
    printf("{\"ok\":true,\"type\":");
    print_json_string(type);
    if (path != NULL) {
        printf(",\"path\":");
        print_json_string(path);
    }
    if (src != NULL) {
        printf(",\"src\":");
        print_json_string(src);
    }
    if (dst != NULL) {
        printf(",\"dst\":");
        print_json_string(dst);
    }
    printf("}\n");
}

static void print_fs_stat_json(const amseokbot_fs_info_t *info) {
    printf("{\"ok\":true,\"type\":\"fs_stat\",\"path\":");
    print_json_string(info->path);
    printf(",\"name\":");
    print_json_string(info->name);
    printf(",\"is_dir\":%s,\"size\":%lld,\"mode\":%u,\"mod_time\":", info->is_dir ? "true" : "false", info->size, info->mode);
    print_json_string(info->mod_time);
    printf("}\n");
}

static int print_usage(void) {
    fprintf(stderr, "usage:\n");
    fprintf(stderr, "  amseokbot-control-core health\n");
    fprintf(stderr, "  amseokbot-control-core chassis --vx 0.1 --vy 0 --wz 0\n");
    fprintf(stderr, "  amseokbot-control-core stop\n");
    return 2;
}

/*
|--------------------------------------------------------------------------
| 程序入口
|--------------------------------------------------------------------------
| 分发健康检查、底盘控制、文件管理和停止命令。
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

    if (strcmp(argv[1], "fs") == 0) {
        char error[256] = {0};
        const char *op = read_string_arg(argc, argv, "--op", "");
        const char *path = read_string_arg(argc, argv, "--path", "");
        const char *src = read_string_arg(argc, argv, "--src", "");
        const char *dst = read_string_arg(argc, argv, "--dst", "");

        if (strcmp(op, "stat") == 0) {
            amseokbot_fs_info_t info;
            if (!amseokbot_fs_stat(path, &info, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            print_fs_stat_json(&info);
            return 0;
        }

        if (strcmp(op, "delete") == 0) {
            if (!amseokbot_fs_delete(path, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            print_fs_ok_json("fs_delete", path, NULL, NULL);
            return 0;
        }

        if (strcmp(op, "copy") == 0) {
            if (!amseokbot_fs_copy(src, dst, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            print_fs_ok_json("fs_copy", NULL, src, dst);
            return 0;
        }

        if (strcmp(op, "move") == 0) {
            if (!amseokbot_fs_move(src, dst, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            print_fs_ok_json("fs_move", NULL, src, dst);
            return 0;
        }

        if (strcmp(op, "write") == 0) {
            bool overwrite = read_bool_arg(argc, argv, "--overwrite", true);
            if (!amseokbot_fs_write_stream(path, overwrite, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            print_fs_ok_json("fs_write", path, NULL, NULL);
            return 0;
        }

        if (strcmp(op, "read") == 0) {
            if (!amseokbot_fs_read_stream(path, error, sizeof(error))) {
                fprintf(stderr, "%s\n", error);
                return 1;
            }
            return 0;
        }

        return print_usage();
    }

    if (strcmp(argv[1], "stop") == 0) {
        amseokbot_serial_frame_t frame = {0};
        amseokbot_build_stop_frame(&frame);
        amseokbot_print_stop_json(&frame);
        return 0;
    }

    return print_usage();
}
