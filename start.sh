#!/usr/bin/env bash
# OmniRoam 一键启动：ROS（可选）、HostPC 前后端、以及你配置的 roslaunch（可选）
#
# 环境变量（可选）：
#   OMNIROAM_SKIP_ROS=1           不启动 roscore / 不 source ROS
#   OMNIROAM_USE_SYSTEMD=1        强制用 systemctl start omniroam-hostpc（需已 install-hostpc）
#   OMNIROAM_ROSLAUNCH            若设置，在 roscore 就绪后执行，例如：
#                                 export OMNIROAM_ROSLAUNCH="simple_robotic_arm esp32_serial_bridge.launch"
#   OMNIROAM_ROSLAUNCH_ARGS       传给 roslaunch 的额外参数，例如：port:=/dev/ttyUSB0
#   HOSTPC_GITHUB_REPO            传给 hostpc 的 -github-repo（与 Web 更新检测一致），如 IwakuraRin/OmniRoam
#   OMNIROAM_GITHUB_BRANCH        默认 main
#   OMNIROAM_HOSTPC_EXTRA_ARGS    追加到 go run/hostpc 的其它参数
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATEDIR="$ROOT/.omniroam"
LOGDIR="${OMNIROAM_LOG_DIR:-$STATEDIR/logs}"
mkdir -p "$STATEDIR" "$LOGDIR"

log() { echo "[omniroam-start] $*"; }

#-------- ROS 环境 --------
if [[ "${OMNIROAM_SKIP_ROS:-0}" != "1" ]]; then
  if [[ -f /opt/ros/noetic/setup.bash ]]; then
    # shellcheck source=/dev/null
    source /opt/ros/noetic/setup.bash
    if [[ -f "$ROOT/catkin_ws/devel/setup.bash" ]]; then
      # shellcheck source=/dev/null
      source "$ROOT/catkin_ws/devel/setup.bash"
    else
      log "提示: 未找到 catkin_ws/devel/setup.bash，若需 ROS 节点请先: cd catkin_ws && catkin_make"
    fi

    if ! pgrep -f rosmaster >/dev/null 2>&1; then
      log "启动 roscore → $LOGDIR/roscore.log"
      nohup roscore >"$LOGDIR/roscore.log" 2>&1 &
      sleep 2
    else
      log "roscore 已在运行"
    fi

    if [[ -n "${OMNIROAM_ROSLAUNCH:-}" ]]; then
      # 格式: "pkg file.launch" 两段
      read -r -a RL <<< "${OMNIROAM_ROSLAUNCH}"
      if [[ "${#RL[@]}" -ge 2 ]]; then
        log "启动 roslaunch ${RL[*]} ${OMNIROAM_ROSLAUNCH_ARGS:-}"
        # shellcheck disable=SC2086
        nohup roslaunch "${RL[0]}" "${RL[1]}" ${OMNIROAM_ROSLAUNCH_ARGS:-} >"$LOGDIR/roslaunch.log" 2>&1 &
        echo $! >"$STATEDIR/roslaunch.pid"
      fi
    fi
  else
    log "未检测到 /opt/ros/noetic/setup.bash，跳过 ROS（可设 OMNIROAM_SKIP_ROS=1 消除本提示）"
  fi
fi

#-------- HostPC --------
GH_REPO="${HOSTPC_GITHUB_REPO:-${OMNIROAM_GITHUB_SLUG:-}}"
GH_BRANCH="${OMNIROAM_GITHUB_BRANCH:-main}"
EXTRA="${OMNIROAM_HOSTPC_EXTRA_ARGS:-}"

if [[ "${OMNIROAM_USE_SYSTEMD:-0}" == "1" ]] || systemctl is-enabled omniroam-hostpc.service &>/dev/null; then
  log "使用 systemd 启动 omniroam-hostpc"
  sudo systemctl start omniroam-hostpc.service
  exit 0
fi

if [[ ! -f "$ROOT/HostPC/web/dist/index.html" ]]; then
  log "未找到前端构建产物，正在 pnpm build…"
  (cd "$ROOT/HostPC/web" && pnpm install && pnpm run build)
fi

if [[ -f "$STATEDIR/hostpc.pid" ]] && kill -0 "$(cat "$STATEDIR/hostpc.pid")" 2>/dev/null; then
  log "hostpc 已在运行 (pid $(cat "$STATEDIR/hostpc.pid"))"
  exit 0
fi

SETTINGS="$ROOT/hostpc-settings.json"
SQLITE="$ROOT/hostpc-users.db"
SECRET="$ROOT/hostpc-auth-secret"
mkdir -p "$ROOT"

RUN=(go run . -addr 0.0.0.0:8080 -static ../web/dist -repo-root "$ROOT" -settings "$SETTINGS" -sqlite-users "$SQLITE" -auth-secret "$SECRET")
[[ -n "$GH_REPO" ]] && RUN+=(-github-repo "$GH_REPO" -github-branch "$GH_BRANCH")
# shellcheck disable=SC2206
[[ -n "$EXTRA" ]] && RUN+=($EXTRA)

log "开发模式启动 HostPC（go run）→ $LOGDIR/hostpc.log"
cd "$ROOT/HostPC/server"
nohup "${RUN[@]}" >"$LOGDIR/hostpc.log" 2>&1 &
echo $! >"$STATEDIR/hostpc.pid"
log "hostpc pid $(cat "$STATEDIR/hostpc.pid") — 浏览器访问 http://$(hostname -I 2>/dev/null | awk '{print $1}'):8080/"
