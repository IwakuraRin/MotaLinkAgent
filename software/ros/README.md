# ROS（OmniOS）

本目录存放 **ROS 1 Noetic** 的 Catkin 工作区，与同级的 `frontend/`、`backend/`、`database/` 等目录并列（均在仓库的 `software/` 下）。

## 子目录

- **`catkin_ws/`** — 标准 Catkin 工作区；源码在 `catkin_ws/src/`，编译产物在 `build/`、`devel/`（通常不提交 Git）。

## 常用命令

```bash
cd catkin_ws
source /opt/ros/noetic/setup.bash
catkin_make
source devel/setup.bash
```

或使用仓库根的 `setup_ros1.bash` 一次加载 Noetic + 本工作区。

## 与一键脚本的关系

仓库根目录的 `./omniroam.sh` 会按布局 source 对应 `catkin_ws/devel/setup.bash`（若已编译，例如 `software/ros/catkin_ws`）。未编译时在相应 `catkin_ws` 内执行 `catkin_make` 即可。

若工作区**刚从其他路径移动过来**，旧的 `build/`、`devel/` 里可能残留绝对路径，建议在 `catkin_ws` 下执行 `catkin_make clean` 后重新 `catkin_make`，或删除 `build`、`devel` 再全量编译。
