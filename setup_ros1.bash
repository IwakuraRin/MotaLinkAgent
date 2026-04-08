#!/usr/bin/env bash
#
# OmniRoam — 仓库目录说明：
#   LICENSE、README.md     — 许可证与项目说明
#   setup_ros1.bash        — 本脚本：加载 Noetic + 本仓库 Catkin 覆盖层
#   catkin_ws/             — ROS 1 Catkin 工作区根（含 .catkin_workspace 标记）
#   catkin_ws/src/         — 所有 ROS 包源码；顶层 CMakeLists.txt 由 catkin_init_workspace 生成
#   catkin_ws/build|devel|install/ — catkin_make 生成目录（已在 .gitignore 中忽略）
#   catkin_ws/src/simple_robotic_arm/ — 示例 ROS 包（节点、launch、msg 等可放于此包内）
#
# 用法：source /本仓库路径/setup_ros1.bash
#
_WS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source /opt/ros/noetic/setup.bash
source "${_WS_ROOT}/catkin_ws/devel/setup.bash"
