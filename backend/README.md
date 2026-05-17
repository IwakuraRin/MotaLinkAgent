# AmseokBot C Control Core

本目录是 AmseokBot 的 C 语言底层控制核心。C 层只负责硬件相关、低延迟、可预测的控制能力，不再负责 HTTP、登录、前端文件托管等上层业务。

## 职责边界

- 电机控制
- 串口协议组包
- 三全向轮底盘运动控制
- 机械臂关节命令
- 安全限幅和急停命令
- 本地进程命令接口，后续可升级为 Unix socket 或静态库接口

## 构建

```bash
make
```

## 命令示例

```bash
./amseokbot-control-core health
./amseokbot-control-core chassis --vx 0.2 --vy 0 --wz 0
./amseokbot-control-core arm --shoulder-yaw 0 --shoulder-pitch 30 --elbow 45 --wrist 0
./amseokbot-control-core stop
```

输出为 JSON，Go API 层负责解析 JSON 并通过 HTTP API 提供给手机 App、前端或 ROS 辅助进程。
