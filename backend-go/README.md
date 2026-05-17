# AmseokBot Go API Layer

本目录是 AmseokBot 的 Go API 层。Go 负责对手机 App、前端和外部服务提供 HTTP/WebSocket API，并通过本地进程命令调用 C 控制核心。

## 职责边界

- HTTP API
- 登录鉴权
- 手机 App / 前端通信
- 配置管理
- 文件管理
- 串口设备枚举
- 调用 C 控制核心

Go 层不直接实现底盘运动学、电机控制和机械臂底层控制，这些能力由 `../backend/amseokbot-control-core` 提供。

## 控制接口

```text
GET  /api/control/health
POST /api/control/chassis/move
POST /api/control/arm/joints
POST /api/control/stop
```

示例请求：

```json
{"vx_mps":0.2,"vy_mps":0,"wz_radps":0}
```

Go API 会调用 C 控制核心，返回轮速和下位机串口协议帧。
