#!/usr/bin/env bash
#
# OmniRoam — 加载 Noetic + 本仓库 Catkin 覆盖层（本文件位于 scripts/，仓库根为其上一级）
#
# 推荐路径：software/ros/catkin_ws/
# 兼容：OmniControlPanel/ros、OmniOS/ros、仓库根 catkin_ws/
#
# 用法：由仓库根的 setup_ros1.bash 包装 source，或:
#   source /本仓库路径/scripts/setup_ros1.bash
#
_WS_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/noetic/setup.bash
if [[ -f "${_WS_ROOT}/software/ros/catkin_ws/devel/setup.bash" ]]; then
  # shellcheck source=/dev/null
  source "${_WS_ROOT}/software/ros/catkin_ws/devel/setup.bash"
elif [[ -f "${_WS_ROOT}/OmniControlPanel/ros/catkin_ws/devel/setup.bash" ]]; then
  # shellcheck source=/dev/null
  source "${_WS_ROOT}/OmniControlPanel/ros/catkin_ws/devel/setup.bash"
elif [[ -f "${_WS_ROOT}/OmniOS/ros/catkin_ws/devel/setup.bash" ]]; then
  # shellcheck source=/dev/null
  source "${_WS_ROOT}/OmniOS/ros/catkin_ws/devel/setup.bash"
elif [[ -f "${_WS_ROOT}/catkin_ws/devel/setup.bash" ]]; then
  # shellcheck source=/dev/null
  source "${_WS_ROOT}/catkin_ws/devel/setup.bash"
else
  echo "[setup_ros1.bash] 未找到 devel/setup.bash，请在对应 catkin_ws 执行 catkin_make" >&2
fi
