/*
|--------------------------------------------------------------------------
| AmseokBot C 控制核心公共接口
|--------------------------------------------------------------------------
| 声明电机控制、串口协议、底盘运动和安全保护使用的数据结构。
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
| 串口协议帧
|--------------------------------------------------------------------------
| 保存要发送给下位机的文本协议帧，Go 层后续可以把它写入串口。
|--------------------------------------------------------------------------
*/
typedef struct {
    char text[256];
} amseokbot_serial_frame_t;

bool amseokbot_check_chassis_command(amseokbot_chassis_command_t *command, char *error, size_t error_size);
void amseokbot_build_chassis_frame(const amseokbot_chassis_command_t *command, amseokbot_serial_frame_t *frame);
void amseokbot_build_stop_frame(amseokbot_serial_frame_t *frame);
void amseokbot_print_health_json(void);
void amseokbot_print_chassis_json(const amseokbot_chassis_command_t *command, const amseokbot_serial_frame_t *frame);
void amseokbot_print_stop_json(const amseokbot_serial_frame_t *frame);


/*
|--------------------------------------------------------------------------
| 文件管理命令
|--------------------------------------------------------------------------
| 由 Go API 鉴权后调用，C 层负责实际文件系统操作，避免前端直接碰系统路径。
|--------------------------------------------------------------------------
*/
typedef struct {
    char path[4096];
    char name[256];
    bool is_dir;
    long long size;
    unsigned int mode;
    char mod_time[32];
} amseokbot_fs_info_t;

bool amseokbot_fs_stat(const char *path, amseokbot_fs_info_t *info, char *error, size_t error_size);
bool amseokbot_fs_delete(const char *path, char *error, size_t error_size);
bool amseokbot_fs_move(const char *src, const char *dst, char *error, size_t error_size);
bool amseokbot_fs_copy(const char *src, const char *dst, char *error, size_t error_size);
bool amseokbot_fs_write_stream(const char *path, bool overwrite, char *error, size_t error_size);
bool amseokbot_fs_read_stream(const char *path, char *error, size_t error_size);

#endif
