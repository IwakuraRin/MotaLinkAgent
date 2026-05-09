# OmniRoam HostPC C Backend

本目录是 C 语言版 HostPC 后端。Go 后端已经移除，运行时只使用 `hostpc-c`。

## 构建与运行

```bash
make
./hostpc-c -addr 0.0.0.0:8080 -static ../frontend/dist
```

或从 `software/` 目录执行：

```bash
bash start-hostpc.sh
```

## 主要功能

- 托管 `frontend/dist` 静态前端，并支持 SPA fallback。
- 提供登录、登出、会话检查和改密接口。
- 读写 `hostpc-settings.json`。
- 枚举 Linux 串口设备。
- 提供只读文件列表接口。
- 提供主 `/ws` WebSocket 日志与按键确认通道。
- 保留更新状态接口，便于前端兼容。

## 默认账号

首次运行会生成 `hostpc-users.cauth`：

```text
user / 123456
```

可以用环境变量覆盖首次初始化账号：

```bash
HOSTPC_USER=admin HOSTPC_PASSWORD='your-password' ./hostpc-c
```
