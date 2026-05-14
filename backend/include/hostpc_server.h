// 作用：声明 C 语言版 HostPC 后端使用的全局配置、HTTP 请求结构和核心模块入口。

#ifndef HOSTPC_SERVER_H
#define HOSTPC_SERVER_H

#include <stddef.h>

// ==================== 服务配置 ====================
// 作用：保存命令行参数解析后的监听地址、静态目录和运行时文件路径。
// ==================================================
typedef struct {
    char listen_host[64];
    int listen_port;
    char static_dir[512];
    char settings_path[512];
    char user_path[512];
    char repo_root[512];
} hostpc_config_t;

// ==================== HTTP 请求 ====================
// 作用：保存单个客户端连接解析出的请求行、头部和请求体。
// ==================================================
typedef struct {
    int client_fd;
    char method[16];
    char path[1024];
    char query[2048];
    char headers[32768];
    char *body;
    size_t body_len;
} http_request_t;

// ==================== 程序入口 ====================
// 作用：启动 C 后端监听循环，并为每个客户端连接分配工作线程。
// ==================================================
int hostpc_run_server(const hostpc_config_t *config);

#endif
