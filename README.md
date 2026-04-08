# OmniRoam

## Tri-omni mobile manipulator · 三轮全向轮移动机械臂平台

**OmniRoam** is a development platform built around a **three-wheeled omnidirectional (holonomic) chassis** and an **onboard robotic arm**. It supports **omnidirectional translation and yaw** on the plane (subject to wheel layout and control), **arm pick-and-place** with a small cargo bay, **tele-operation**, and higher-level **autonomy** where sensors and software allow. An **x86 Linux host** runs **ROS 1**; an **ESP32-S3** drives actuators via **I2C** to the motor driver board and exchanges data with the host over **USB–UART**.

**OmniRoam** 是一套围绕**三轮全向（全驱）底盘**与**车载机械臂**搭建的实验平台：支持平面**全向平移与旋转**（取决于轮系布局与控制）、**机械臂抓取**与小**货舱**作业、**遥控**，以及在有传感器与软件时的更高层**自主运行**。**x86 Linux 上位机**运行 **ROS 1**；**ESP32-S3** 经 **I2C** 连接电机控制板驱动执行机构，并经 **USB–UART** 与上位机交换数据。

<a id="repo-tree"></a>

## 仓库目录结构

```
simpleRoboticArm/
├── .gitignore
├── LICENSE
├── README.md
├── setup_ros1.bash                 # 加载 Noetic + catkin_ws/devel
├── setup_hostpc_ubuntu20.sh        # Ubuntu 20.04 上位机一键环境
├── 3mf_printFile/                  # 3D 打印 .3mf
├── catkin_ws/                      # ROS 1 Catkin 工作区
│   ├── .catkin_workspace
│   └── src/
│       ├── CMakeLists.txt          # catkin 顶层（符号链接）
│       └── simple_robotic_arm/
│           ├── CMakeLists.txt
│           ├── package.xml
│           ├── include/simple_robotic_arm/
│           ├── launch/
│           │   └── esp32_serial_bridge.launch
│           └── scripts/
│               ├── esp32_serial_bridge.py
│               ├── arm_kinematics.py
│               └── chassis_kinematics.py
├── RoboticArm_ESP32S3/             # ESP32-S3 PlatformIO 固件
│   ├── .gitignore
│   ├── platformio.ini
│   ├── .vscode/
│   ├── include/README
│   ├── lib/README
│   ├── test/README
│   └── src/
│       ├── main.cpp
│       ├── PCA9685_Servo.cpp
│       └── PCA9685_Servo.h
└── RoboticArm_HostPC/              # 上位机 Web + Go
    ├── .gitignore
    ├── README.md
    ├── start.sh
    ├── server/
    │   ├── go.mod
    │   ├── go.sum
    │   └── main.go
    └── web/                        # Vue + Vite + Tailwind（pnpm）
        ├── .env.example
        ├── package.json
        ├── pnpm-lock.yaml
        ├── pnpm-workspace.yaml
        ├── vite.config.ts
        ├── tailwind.config.js
        ├── postcss.config.js
        ├── tsconfig.json
        ├── index.html
        ├── dist/                   # pnpm build 产物
        └── src/
            ├── main.ts
            ├── App.vue
            ├── style.css
            └── vite-env.d.ts
```

未列出：`.git`、`catkin_ws/build`、`catkin_ws/devel`、`RoboticArm_ESP32S3/.pio`、`RoboticArm_HostPC/web/node_modules` 等生成或依赖目录。

---

**Language / 语言:** [目录结构 ↑](#repo-tree) · [English ↓](#lang-en) · [简体中文 ↓](#lang-zh)

---

<a id="lang-en"></a>

## Documentation (English)

Quick links: [Tree](#repo-tree) · [Overview](#en-overview) · [Architecture](#en-architecture) · [Repository](#en-repo) · [Build & run](#en-dev) · [Status](#en-status) · [简体中文 →](#lang-zh)

<a id="en-overview"></a>

### Overview

**OmniRoam** is an experimental **tri-omni mobile manipulator** platform: tele-operation, basic autonomy, arm pick-and-place with a small cargo bay; split **host PC (x86 Linux, ROS 1) + ESP32-S3**; open firmware and ROS-side code for learning and small demos.

| Area | Description |
|------|-------------|
| Chassis | **Tri-omni** base: planar translation and yaw (depends on wheel layout & control allocation) |
| Arm | Onboard manipulator picks small objects and places them in the cargo bay |
| Mobility | Omnidirectional driving, line/route following, etc. (depends on sensors & stack) |
| Tele-op | Remote chassis and arm commands |

**Design note:** computing vs. real-time control are separated; chassis and lightweight manipulation share one hardware platform.

<a id="en-architecture"></a>

### System architecture

#### Hardware roles

| Role | Stack | Notes |
|------|--------|------|
| Host | x86_64 Linux, **ROS 1 Noetic** | Planning, perception, HMI; **USB camera** for web video (independent of the serial link) |
| Link | **USB ↔ UART** (e.g. **CH340**) | **Cross TX/RX + common GND**, **full-duplex**; on Linux often `/dev/ttyUSB*`; add user to `dialout` |
| MCU | **ESP32-S3** | Firmware in `RoboticArm_ESP32S3/` (PlatformIO + Arduino) |
| Actuation | **I2C → motor driver board** | PWM, encoder motors, etc. depend on your board and firmware |

#### Data flow (conceptual)

```
Web / ROS nodes (x86)
        │  USB-UART (bidirectional)
        ▼
   ESP32-S3
        │  I2C
        ▼
  Driver board ──► PWM / encoder motors, etc.

USB camera (x86) ──► Web video (not through ESP32)
```

#### ROS & serial (current design)

- Catkin workspace: `catkin_ws/`; environment: `source setup_ros1.bash`.
- Package `simple_robotic_arm` provides a **line-oriented text** serial bridge `esp32_serial_bridge.py` (topics `~tx` / `~rx`) for quick bring-up with `Serial.print` style logs.
- A **proper protocol** (joints, encoders, framing, checksums) must be agreed between ESP32 and this node, then implemented in code.

<a id="en-repo"></a>

### Repository layout

| Path | Purpose |
|------|---------|
| `setup_ros1.bash` | Sources `/opt/ros/noetic` and this repo’s `catkin_ws/devel` |
| `catkin_ws/` | ROS 1 Catkin workspace; `build/` & `devel/` are usually local-only |
| `catkin_ws/src/simple_robotic_arm/` | ROS package: `scripts/esp32_serial_bridge.py`, `launch/esp32_serial_bridge.launch`, … |
| `RoboticArm_ESP32S3/` | ESP32-S3 firmware (e.g. PCA9685 / I2C servo demo) |
| `RoboticArm_HostPC/` | Host web UI: Vue + Tailwind (`web/`), Go server + WS (`server/`); see `RoboticArm_HostPC/README.md` |
| `setup_hostpc_ubuntu20.sh` | **Ubuntu 20.04** fresh host: ROS Noetic, Go, Node/pnpm, `catkin_make`, dialout |
| `LICENSE` | License |

<a id="en-dev"></a>

### Development & run

**New Ubuntu 20.04 host (Server/Desktop)**

```bash
cd /path/to/simpleRoboticArm
bash setup_hostpc_ubuntu20.sh
```

**ROS (host)**

```bash
source /path/to/simpleRoboticArm/setup_ros1.bash
cd catkin_ws && catkin_make && source devel/setup.bash
```

**Serial bridge (CH340 / USB-UART device)**

```bash
sudo apt install python3-serial   # if needed
roslaunch simple_robotic_arm esp32_serial_bridge.launch port:=/dev/ttyUSB0
```

Example topics: `/esp32_serial_bridge/tx` (publish `std_msgs/String` to device), `/esp32_serial_bridge/rx` (lines from device).

**ESP32 firmware**

Open `RoboticArm_ESP32S3/` in PlatformIO; **baud rate** must match the bridge (e.g. 115200).

<a id="en-status"></a>

### Implementation status

| Item | State | Notes |
|------|-------|------|
| ROS serial bridge (text lines) | Done | See `roslaunch` above |
| Binary protocol / custom `.msg` | Not done | Align frame format with ESP32 |
| ESP32 UART command + telemetry | Partial | Mostly debug `Serial` + servo I2C today |
| Encoder motors / chassis in firmware | Not done | Extend per your driver board |
| USB camera → web | Not done | Add in `RoboticArm_HostPC` or a separate service |
| Web ↔ ROS | Not done | Typical: `rosbridge_suite` or a small HTTP API |

See `LICENSE` in the repo root.

<p align="right"><a href="#lang-zh">Continue in 简体中文 →</a></p>

---

<a id="lang-zh"></a>

## 简体中文文档（OmniRoam）

**返回英文:** [English documentation ↑](#lang-en)

快速链接: [目录结构](#repo-tree) · [项目简介](#zh-overview) · [系统架构](#zh-architecture) · [仓库结构](#zh-repo) · [开发与运行](#zh-dev) · [实现状态](#zh-status)

<a id="zh-overview"></a>

### 项目简介

**OmniRoam** 是一套围绕**三轮全向（全驱）底盘**与**车载机械臂**搭建的实验平台：支持遥控与基础自主、机械臂抓取及小货舱作业；采用**上位机（x86 Linux + ROS 1）+ ESP32-S3** 分工，固件与 ROS 侧代码开源，适用于学习与小规模演示。

| 方向 | 内容 |
|------|------|
| 底盘 | **三轮全向轮**：平面内平移与旋转（取决于轮距布局与控制分配） |
| 机械臂 | 车载机械臂抓取小物体并放入车身货舱 |
| 移动 | 全向行驶、循迹、路径跟随等（依实际传感器与算法） |
| 遥控 | 遥控底盘与机械臂 |

**设计说明**：算力与实时控制分离；底盘与轻量操作共用同一套硬件平台。

<a id="zh-architecture"></a>

### 系统架构

#### 硬件拓扑

| 角色 | 硬件 / 软件 | 说明 |
|------|----------------|------|
| 上位机 | x86_64，Linux，**ROS 1 Noetic** | 规划、感知、人机交互；USB **摄像头**用于网页视频（与串口控制相互独立） |
| 链路 | **USB → UART**（如 **CH340**） | **TX/RX 交叉 + 共地**，**双向**通信；Linux 上多为 `/dev/ttyUSB*`；用户需加入 `dialout` 组 |
| 下位机 | **ESP32-S3** | 固件见 `RoboticArm_ESP32S3/`（PlatformIO + Arduino） |
| 执行 | **I2C → 控制板** | PWM 驱动、编码电机等由控制板与固件方案决定 |

#### 数据流（示意）

```
网页 / ROS 节点（x86）
        │  USB-UART（双向）
        ▼
   ESP32-S3
        │  I2C
        ▼
     控制板 ──► PWM / 编码电机等

USB 摄像头（x86） ──► 网页视频（不经过 ESP32）
```

#### ROS 与串口（当前设计）

- Catkin 工作区：`catkin_ws/`；进入环境：`source setup_ros1.bash`。
- 包 `simple_robotic_arm` 提供**按行文本**串口桥 `esp32_serial_bridge.py`（话题 `~tx` / `~rx`），便于与固件 `Serial` 打印联调。
- **正式协议**（关节角、编码器回传、帧格式与校验）需在 ESP32 与桥接节点上共同约定后再改代码。

<a id="zh-repo"></a>

### 仓库结构

| 路径 | 说明 |
|------|------|
| `setup_ros1.bash` | 加载 `/opt/ros/noetic` 与本仓库 `catkin_ws/devel` |
| `catkin_ws/` | ROS 1 Catkin 工作区；`build/`、`devel/` 一般为本地生成，通常不提交 |
| `catkin_ws/src/simple_robotic_arm/` | ROS 包：`scripts/esp32_serial_bridge.py`、`launch/esp32_serial_bridge.launch` 等 |
| `RoboticArm_ESP32S3/` | ESP32-S3 固件（含 PCA9685 / I2C 舵机示例） |
| `RoboticArm_HostPC/` | 上位机控制台：Vue + Tailwind（`web/`）、Go + WebSocket（`server/`），见 `RoboticArm_HostPC/README.md` |
| `setup_hostpc_ubuntu20.sh` | **Ubuntu 20.04** 新上位机一键装：Noetic、Go、Node/pnpm、`catkin_make`、串口组 |
| `LICENSE` | 许可证 |

<a id="zh-dev"></a>

### 开发与运行

**全新 Ubuntu 20.04 上位机**

```bash
cd /path/to/simpleRoboticArm
bash setup_hostpc_ubuntu20.sh
```

**ROS（上位机）**

```bash
source /path/to/simpleRoboticArm/setup_ros1.bash
cd catkin_ws && catkin_make && source devel/setup.bash
```

**串口桥（连接 CH340 对应设备）**

```bash
sudo apt install python3-serial   # 若未安装
roslaunch simple_robotic_arm esp32_serial_bridge.launch port:=/dev/ttyUSB0
```

话题示例：`/esp32_serial_bridge/tx`（发字符串）、`/esp32_serial_bridge/rx`（收行）。

**ESP32 固件**

在 `RoboticArm_ESP32S3/` 下用 PlatformIO 打开工程，波特率需与桥接节点一致（如 115200）。

<a id="zh-status"></a>

### 实现状态

| 模块 | 状态 | 备注 |
|------|------|------|
| ROS 串口桥（文本行） | 已有 | 见上文 `roslaunch` |
| 串口二进制协议 / 自定义 `.msg` | 未做 | 需与 ESP32 统一帧格式 |
| ESP32 UART 命令解析与状态上报 | 未完善 | 当前偏 `Serial` 调试与舵机控制 |
| 编码电机 / 底盘在固件中的实现 | 未完善 | 依控制板与硬件继续开发 |
| USB 摄像头 → 网页 | 未做 | 可在 `RoboticArm_HostPC` 或独立服务中实现 |
| 网页 ↔ ROS | 未做 | 常见方案：`rosbridge_suite` 或自建 HTTP API |

许可证见仓库根目录 `LICENSE`。

<p align="right"><a href="#lang-en">Back to English ↑</a></p>
