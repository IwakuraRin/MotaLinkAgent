# OmniRoam HostPC Go API Layer

本目录是 HostPC 的 Go API 层。C 后端保留为底层控制核心，Go 负责 HTTP API、鉴权、静态前端托管和轻量系统接口。

## 构建与运行

```bash
make
./hostpc-api -addr 0.0.0.0:8080 -static ../frontend/dist -settings ../backend/hostpc-settings.json -users ../backend/hostpc-users.cauth -control-core ../backend/hostpc-c
```

## 分层约定

- C：硬件控制、串口/机器人底层能力，后续可通过进程或本地 socket 暴露。
- Go：登录、会话、Web API、文件浏览、串口枚举、前端静态资源。
- 前端：继续请求原有 `/api/*` 路径，避免大改 UI。
