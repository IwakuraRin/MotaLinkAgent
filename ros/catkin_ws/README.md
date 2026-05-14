# Catkin 工作区

标准 ROS 1 Catkin 布局。

## 布局说明

- **`src/`** — ROS 包源码；顶层 `CMakeLists.txt` 由 `catkin_init_workspace` 生成。
- **`build/`、`devel/`、`install/`** — `catkin_make` / `catkin build` 生成，一般加入 `.gitignore`。

## 本仓库中的包

- **`src/simple_robotic_arm/`** — 示例包：串口桥、底盘/机械臂运动学、视觉巡线等脚本与 launch 文件。

首次编译（在**本目录** `OmniOS/ros/catkin_ws` 下执行）：

```bash
source /opt/ros/noetic/setup.bash
catkin_make
source devel/setup.bash
```
