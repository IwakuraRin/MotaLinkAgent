# AmseokBot Milo

AmseokBot Milo 是机器人上位机软件仓库，按 C 控制核心、Go API 层、ROS 能力层和前端界面分开维护。

## 分层架构

```text
frontend / mobile app
        |
        | HTTP / WebSocket
        v
MasterComputer/backend-go
        |
        | local process / future Unix socket
        v
MasterComputer/backend
        |
        | serial protocol / low latency control
        v
SlaveDevice/ATmega2560 / motor drivers

MasterComputer/ros
        |
        | topics / launch / perception / kinematics
        v
camera / YOLO / navigation / sensor nodes
```

## 目录职责

| 目录 | 职责 |
|------|------|
| `SlaveDevice/ATmega2560/` | 下位机固件：串口通信、电机执行、传感器采集和实时控制 |
| `MasterComputer/backend/` | C 语言控制核心：电机控制、串口协议、底盘运动、机械臂控制、安全限幅、本地命令接口 |
| `MasterComputer/backend-go/` | Go API 层：HTTP API、登录鉴权、前端/手机通信、配置管理、文件管理、调用 C 控制核心 |
| `MasterComputer/ros/` | ROS 层：运动学、传感器节点、相机、YOLO、导航和机器人实验节点 |
| `MasterComputer/frontend/` | 前端界面，通过 Go API 控制机器人和查看状态 |
| `MasterComputer/database/` | 可选数据库实验配置 |
| `MasterComputer/deploy/` | 后续 deb/systemd/镜像部署文件 |
| `MasterComputer/systeminfo/` | 可选系统信息小工具 |

## 构建控制核心

```bash
cd MasterComputer/backend
make
./amseokbot-control-core health
```

## 验证 Go API

```bash
cd MasterComputer/backend-go
go test ./...
go run ./cmd/hostpc-api -control-core ../backend/amseokbot-control-core
```

## 运行数据原则

真实密钥、用户数据库和机器人本机数据不提交到 Git。以后打包成 deb 时，由安装脚本或首次启动流程生成到 `/etc/amseokbot/` 和 `/var/lib/amseokbot/`。
