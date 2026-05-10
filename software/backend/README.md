# OmniRoam HostPC C Control Core

本目录保留 C 语言版 HostPC 控制核心。运行时推荐由 `software/backend-go` 的 Go API 层对外提供 HTTP/API，本目录的 `hostpc-c` 继续作为底层控制能力和兼容实现。

## 构建与运行

```bash
make
./hostpc-c -addr 0.0.0.0:8080 -static ../frontend/dist
```

完整启动推荐从 `software/` 目录执行，脚本会先构建 C 控制核心，再启动 Go API 层：

```bash
bash start-hostpc.sh
```

## 主要功能

- 保留 C 语言底层控制和兼容 HTTP 实现。
- Go API 层负责对外 HTTP、登录会话、设置、串口、文件列表和静态前端托管。
- 后续新增机器人硬件控制时，优先把 C 能力封装成进程命令、本地 socket 或库接口，再由 Go API 调用。

## 默认账号

首次运行会生成 `hostpc-users.cauth`：

```text
user / change-me-on-first-login
```

可以用环境变量覆盖首次初始化账号：

```bash
HOSTPC_USER=admin HOSTPC_PASSWORD='your-password' ./hostpc-c
```
