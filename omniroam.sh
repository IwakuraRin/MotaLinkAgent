#!/usr/bin/env bash
# OmniRoam 统一控制台：环境检测 → 启动前后端 → 终端图形菜单
#
# 环境变量（可选）：
#   OMNIROAM_SKIP_ROS=1        不启动 / 不检测 roscore
#   OMNIROAM_USE_SYSTEMD=1     使用 systemctl 管理 hostpc（需已 install-hostpc）
#   OMNIROAM_NO_VITE=1         不自动启动 Vite 开发服务器（仅用 8080 静态站）
#   OMNIROAM_MENU=0            启动服务后退出，不进入交互菜单（供脚本调用）
#   OMNIROAM_ROSLAUNCH         roslaunch，格式: "pkg file.launch"
#   OMNIROAM_ROSLAUNCH_ARGS    传给 roslaunch 的额外参数
#   HOSTPC_GITHUB_REPO         传给后端的 -github-repo
#   OMNIROAM_GITHUB_BRANCH     默认 main
#   OMNIROAM_HOSTPC_EXTRA_ARGS 追加到 C 后端 hostpc-c 的参数
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATEDIR="$ROOT/.omniroam"
LOGDIR="${OMNIROAM_LOG_DIR:-$STATEDIR/logs}"

FRONTEND_DIR=""
BACKEND_DIR=""
CATKIN_WS=""
DEPLOY_DIR=""
PANEL_ROOT="" # software/ 或 OmniControlPanel、OmniOS 等「一体包」根目录（含 frontend/backend）

log() { echo "[omniroam] $*"; }

resolve_layout() {
  FRONTEND_DIR=""; BACKEND_DIR=""; CATKIN_WS=""; DEPLOY_DIR=""; PANEL_ROOT=""
  if [[ -f "$ROOT/software/backend/Makefile" ]]; then
    PANEL_ROOT="$ROOT/software"
    FRONTEND_DIR="$PANEL_ROOT/frontend"
    BACKEND_DIR="$PANEL_ROOT/backend"
    CATKIN_WS="$PANEL_ROOT/ros/catkin_ws"
    DEPLOY_DIR="$PANEL_ROOT/deploy"
  elif [[ -f "$ROOT/OmniControlPanel/backend/Makefile" ]]; then
    PANEL_ROOT="$ROOT/OmniControlPanel"
    FRONTEND_DIR="$PANEL_ROOT/frontend"
    BACKEND_DIR="$PANEL_ROOT/backend"
    CATKIN_WS="$PANEL_ROOT/ros/catkin_ws"
    DEPLOY_DIR="$PANEL_ROOT/deploy"
  elif [[ -f "$ROOT/OmniOS/backend/Makefile" ]]; then
    PANEL_ROOT="$ROOT/OmniOS"
    FRONTEND_DIR="$PANEL_ROOT/frontend"
    BACKEND_DIR="$PANEL_ROOT/backend"
    CATKIN_WS="$PANEL_ROOT/ros/catkin_ws"
    DEPLOY_DIR="$PANEL_ROOT/deploy"
  elif [[ -d "$ROOT/OmniOS/hostpc/server" ]]; then
    PANEL_ROOT="$ROOT/OmniOS/hostpc"
    FRONTEND_DIR="$PANEL_ROOT/web"
    BACKEND_DIR="$PANEL_ROOT/server"
    CATKIN_WS="$ROOT/OmniOS/ros/catkin_ws"
    DEPLOY_DIR="$PANEL_ROOT/deploy"
  elif [[ -d "$ROOT/HostPC/server" ]]; then
    PANEL_ROOT="$ROOT/HostPC"
    FRONTEND_DIR="$PANEL_ROOT/web"
    BACKEND_DIR="$PANEL_ROOT/server"
    CATKIN_WS="$ROOT/catkin_ws"
    DEPLOY_DIR="$PANEL_ROOT/deploy"
  else
    echo "未找到上位机布局（期望 software/、OmniControlPanel、OmniOS 或 HostPC）。" >&2
    exit 1
  fi
}

#-------- 环境检测（缺项仅提示，不中断）--------//
check_environment() {
  local issues=0
  echo ""
  echo "  ┌─────────────────────────────────────────────────────────┐"
  echo "  │  环境检测                                                │"
  echo "  └─────────────────────────────────────────────────────────┘"
  if ! command -v gcc >/dev/null 2>&1; then
    echo "  · 缺少: gcc（C 后端无法编译运行）"
    issues=1
  else
    echo "  · GCC:  $(command -v gcc) ($(gcc --version 2>/dev/null | head -1))"
    if [[ -n "${BACKEND_DIR:-}" ]] && [[ -d "$BACKEND_DIR" ]]; then
      if [[ ! -f "$BACKEND_DIR/Makefile" ]]; then
        echo "  · 缺少: $BACKEND_DIR/Makefile（C 后端无法 make）"
        issues=1
      else
        echo "  · C 后端: Makefile 已存在"
      fi
    fi
  fi
  if command -v make >/dev/null 2>&1; then
    echo "  · make: $(command -v make)"
  else
    echo "  · 缺少: make（C 后端无法编译）"
    issues=1
  fi
  if command -v pnpm >/dev/null 2>&1; then
    echo "  · pnpm: $(command -v pnpm) ($("pnpm" --version 2>/dev/null || true))"
  elif command -v npm >/dev/null 2>&1; then
    echo "  · 未找到 pnpm，有 npm: $(command -v npm)（建议: corepack enable && corepack prepare pnpm@9 --activate）"
  else
    echo "  · 缺少: pnpm / npm（前端依赖安装与构建受限）"
    issues=1
  fi
  if command -v python3 >/dev/null 2>&1; then
    echo "  · Python3: $(python3 --version 2>&1)"
  else
    echo "  · 缺少: python3（部分 ROS 脚本 / 工具链可能需要）"
    issues=1
  fi
  if [[ -f /opt/ros/noetic/setup.bash ]]; then
    echo "  · ROS:   Noetic 已安装 (/opt/ros/noetic)"
    if [[ -f "$CATKIN_WS/devel/setup.bash" ]]; then
      echo "  · Catkin: 已编译 → $CATKIN_WS/devel"
    else
      echo "  · Catkin: 未编译 devel（需要节点时: cd $CATKIN_WS && catkin_make）"
    fi
  else
    echo "  · ROS:   未检测到 Noetic（将跳过 roscore / roslaunch）"
  fi
  echo ""
  if [[ "$issues" -eq 1 ]]; then
    echo "  提示: 部分环境缺失，仍尝试继续启动已有能力范围内的服务。"
  else
    echo "  核心命令行工具检测完成。"
  fi
  echo ""
}

draw_banner_ok() {
  local ip
  ip="$(hostname -I 2>/dev/null | awk '{print $1}')"
  cat <<EOF

    ╔═══════════════════════════════════════════════════════════╗
    ║                                                           ║
    ║     ██████╗ ███╗   ███╗███╗   ██╗██╗██████╗  ██████╗      ║
    ║    ██╔═══██╗████╗ ████║████╗  ██║██║██╔══██╗██╔═══██╗     ║
    ║    ██║   ██║██╔████╔██║██╔██╗ ██║██║██████╔╝██║   ██║     ║
    ║    ██║   ██║██║╚██╔╝██║██║╚██╗██║██║██╔══██╗██║   ██║     ║
    ║    ╚██████╔╝██║ ╚═╝ ██║██║ ╚████║██║██║  ██║╚██████╔╝     ║
    ║     ╚═════╝ ╚═╝     ╚═╝╚═╝  ╚═══╝╚═╝╚═╝  ╚═╝ ╚═════╝      ║
    ║                                                           ║
    ║              控制台 · 服务已尝试加载                       ║
    ╚═══════════════════════════════════════════════════════════╝

    后端 (静态 + API):  http://127.0.0.1:8080/
EOF
  if [[ "${OMNIROAM_NO_VITE:-0}" != "1" ]] && [[ -f "$STATEDIR/vite.pid" ]] && kill -0 "$(cat "$STATEDIR/vite.pid")" 2>/dev/null; then
    cat <<EOF
    前端开发 (Vite):    http://127.0.0.1:5173/
EOF
  fi
  if [[ -n "$ip" ]]; then
    echo "    局域网后端:         http://${ip}:8080/"
  fi
  echo ""
  echo "    日志目录: $LOGDIR"
  echo ""
}

draw_menu() {
  cat <<'EOF'
  ┌──────────────────────────────────────────────────────────────┐
  │  主菜单 · 输入数字后回车                                      │
  ├──────────────────────────────────────────────────────────────┤
  │  1) 检查更新（git fetch，显示与远端差异）                     │
  │  2) 应用更新（git pull + 构建；生产环境会跑 install-hostpc）  │
  │  3) 重启 HostPC（C 后端；按需重启 Vite）                      │
  │  4) 停止本脚本管理的全部服务（后端 / Vite / roslaunch）       │
  │  5) 启动 roscore（需已安装 ROS Noetic）                       │
  │  6) 停止 roscore / 本仓库记录的 roslaunch                     │
  │  7) 仅重新构建前端 dist（pnpm build）                         │
  │  8) 查看 hostpc 日志（Ctrl+C 返回菜单）                       │
  │  9) 启动 Vite 开发服务器（5173，代理到 8080）                 │
  │ 10) 停止 Vite                                                 │
  │ 11) 用系统浏览器打开本机控制台（xdg-open，若可用）            │
  │  0) 退出（不自动停止已启动的服务）                            │
  └──────────────────────────────────────────────────────────────┘
EOF
}

stop_hostpc_dev() {
  if [[ -f "$STATEDIR/hostpc.pid" ]]; then
    local pid
    pid="$(cat "$STATEDIR/hostpc.pid" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      log "停止 hostpc (pid $pid)"
      kill "$pid" 2>/dev/null || true
      sleep 1
      kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$STATEDIR/hostpc.pid"
  fi
}

stop_vite() {
  if [[ -f "$STATEDIR/vite.pid" ]]; then
    local pid
    pid="$(cat "$STATEDIR/vite.pid" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      log "停止 Vite (pid $pid)"
      kill "$pid" 2>/dev/null || true
      sleep 1
      kill -9 "$pid" 2>/dev/null || true
    fi
    rm -f "$STATEDIR/vite.pid"
  fi
}

stop_roslaunch_only() {
  if [[ -f "$STATEDIR/roslaunch.pid" ]]; then
    local pid
    pid="$(cat "$STATEDIR/roslaunch.pid" 2>/dev/null || true)"
    if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
      log "停止 roslaunch pid $pid"
      kill "$pid" 2>/dev/null || true
    fi
    rm -f "$STATEDIR/roslaunch.pid"
  fi
}

stop_roscore() {
  if pgrep -f rosmaster >/dev/null 2>&1; then
    log "停止 rosmaster / roscore"
    pkill -f rosmaster 2>/dev/null || true
    sleep 1
    pkill -9 -f rosmaster 2>/dev/null || true
  fi
}

start_ros_layer() {
  if [[ "${OMNIROAM_SKIP_ROS:-0}" == "1" ]]; then
    return 0
  fi
  if [[ ! -f /opt/ros/noetic/setup.bash ]]; then
    return 0
  fi
  # shellcheck source=/dev/null
  set +u; source /opt/ros/noetic/setup.bash; set -u
  if [[ -f "$CATKIN_WS/devel/setup.bash" ]]; then
    # shellcheck source=/dev/null
    set +u; source "$CATKIN_WS/devel/setup.bash"; set -u
  fi
  if ! pgrep -f rosmaster >/dev/null 2>&1; then
    log "启动 roscore → $LOGDIR/roscore.log"
    nohup roscore >"$LOGDIR/roscore.log" 2>&1 &
    sleep 2
  else
    log "roscore 已在运行"
  fi
  if [[ -n "${OMNIROAM_ROSLAUNCH:-}" ]]; then
    read -r -a RL <<< "${OMNIROAM_ROSLAUNCH}"
    if [[ "${#RL[@]}" -ge 2 ]]; then
      log "启动 roslaunch ${RL[*]} ${OMNIROAM_ROSLAUNCH_ARGS:-}"
      # shellcheck disable=SC2086
      nohup roslaunch "${RL[0]}" "${RL[1]}" ${OMNIROAM_ROSLAUNCH_ARGS:-} >"$LOGDIR/roslaunch.log" 2>&1 &
      echo $! >"$STATEDIR/roslaunch.pid"
    fi
  fi
}

ensure_frontend_dist() {
  if [[ -f "$FRONTEND_DIR/dist/index.html" ]]; then
    return 0
  fi
  if ! command -v pnpm >/dev/null 2>&1; then
    log "WARN: 无 dist 且无 pnpm，跳过前端构建"
    return 1
  fi
  log "未找到前端 dist，执行 pnpm install && build…"
  (cd "$FRONTEND_DIR" && pnpm install && pnpm run build)
}

start_hostpc_backend() {
  if [[ "${OMNIROAM_USE_SYSTEMD:-0}" == "1" ]] || systemctl is-enabled omniroam-hostpc.service &>/dev/null; then
    log "使用 systemd 启动 omniroam-hostpc"
    sudo systemctl start omniroam-hostpc.service
    return 0
  fi
  if [[ -f "$STATEDIR/hostpc.pid" ]] && kill -0 "$(cat "$STATEDIR/hostpc.pid")" 2>/dev/null; then
    log "hostpc 已在运行 (pid $(cat "$STATEDIR/hostpc.pid"))"
    return 0
  fi
  if ! command -v gcc >/dev/null 2>&1; then
    log "ERROR: 未安装 gcc，无法编译 C 后端"
    return 1
  fi
  if ! command -v make >/dev/null 2>&1; then
    log "ERROR: 未安装 make，无法编译 C 后端"
    return 1
  fi
  if [[ ! -f "$BACKEND_DIR/Makefile" ]]; then
    log "ERROR: 缺少 $BACKEND_DIR/Makefile — 无法构建 C 后端"
    return 1
  fi
  ensure_frontend_dist || true
  mkdir -p "$ROOT"
  local SETTINGS USERS STATIC_DIST EXTRA
  SETTINGS="$ROOT/hostpc-settings.json"
  USERS="$ROOT/hostpc-users.cauth"
  STATIC_DIST="$(cd "$FRONTEND_DIR" && pwd)/dist"
  EXTRA="${OMNIROAM_HOSTPC_EXTRA_ARGS:-}"
  (cd "$BACKEND_DIR" && make)
  local RUN=("$BACKEND_DIR/hostpc-c" -addr 0.0.0.0:8080 -static "$STATIC_DIST" -repo-root "$ROOT" -settings "$SETTINGS" -users "$USERS")
  # shellcheck disable=SC2206
  [[ -n "$EXTRA" ]] && RUN+=($EXTRA)
  log "启动 HostPC（C 后端）→ $LOGDIR/hostpc.log"
  cd "$BACKEND_DIR"
  nohup "${RUN[@]}" >"$LOGDIR/hostpc.log" 2>&1 &
  echo $! >"$STATEDIR/hostpc.pid"
  log "hostpc pid $(cat "$STATEDIR/hostpc.pid")"
  for _ in {1..20}; do
    if curl -sf -o /dev/null "http://127.0.0.1:8080/"; then
      log "后端 HTTP 探测成功"
      return 0
    fi
    sleep 1
  done
  log "WARN: 后端暂未响应 8080，请查看 $LOGDIR/hostpc.log"
  return 1
}

start_vite_if_enabled() {
  if [[ "${OMNIROAM_NO_VITE:-0}" == "1" ]]; then
    return 0
  fi
  if ! command -v pnpm >/dev/null 2>&1; then
    log "未安装 pnpm，跳过 Vite"
    return 0
  fi
  if [[ -f "$STATEDIR/vite.pid" ]] && kill -0 "$(cat "$STATEDIR/vite.pid")" 2>/dev/null; then
    log "Vite 已在运行"
    return 0
  fi
  log "启动 Vite → $LOGDIR/vite.log"
  (
    cd "$FRONTEND_DIR"
    nohup pnpm dev >"$LOGDIR/vite.log" 2>&1 &
    echo $! >"$STATEDIR/vite.pid"
  )
  sleep 2
}

cmd_check_update() {
  if [[ ! -d "$ROOT/.git" ]]; then
    echo "  非 git 仓库，跳过。"
    return
  fi
  echo "  正在 git fetch…"
  git -C "$ROOT" fetch origin 2>&1 || echo "  fetch 失败（检查网络与 remote）"
  local local_h remote_h branch
  branch="$(git -C "$ROOT" rev-parse --abbrev-ref HEAD 2>/dev/null || echo "?")"
  local_h="$(git -C "$ROOT" rev-parse --short HEAD 2>/dev/null || echo "?")"
  if git -C "$ROOT" rev-parse @{u} >/dev/null 2>&1; then
    remote_h="$(git -C "$ROOT" rev-parse --short @{u} 2>/dev/null || echo "?")"
    echo "  分支: $branch  本地: $local_h  上游: $remote_h"
    git -C "$ROOT" log --oneline "HEAD..@{u}" 2>/dev/null | head -5 || true
    git -C "$ROOT" status -sb
  else
    echo "  分支: $branch  本地: $local_h（未配置上游跟踪分支）"
  fi
}

cmd_apply_update() {
  local script="$DEPLOY_DIR/hostpc-self-update.sh"
  if [[ -x "$script" ]] || [[ -f "$script" ]]; then
    log "执行 $script"
    bash "$script" "$ROOT" || echo "  更新脚本失败，见上输出。"
  else
    log "未找到 hostpc-self-update.sh，执行简易: pull + build"
    git -C "$ROOT" pull --ff-only || return 1
    (cd "$FRONTEND_DIR" && pnpm install && pnpm run build)
    (cd "$BACKEND_DIR" && go build -o hostpc .)
  fi
}

cmd_restart_hostpc() {
  stop_hostpc_dev
  sleep 1
  start_hostpc_backend || true
  if [[ "${OMNIROAM_NO_VITE:-0}" != "1" ]]; then
    stop_vite
    start_vite_if_enabled
  fi
}

cmd_stop_all_managed() {
  stop_vite
  stop_hostpc_dev
  stop_roslaunch_only
  if systemctl is-active omniroam-hostpc.service &>/dev/null; then
    log "停止 systemd omniroam-hostpc"
    sudo systemctl stop omniroam-hostpc.service || true
  fi
}

menu_loop() {
  set +e
  while true; do
    draw_menu
    read -r -p "  请选择: " choice || exit 0
    echo ""
    case "$choice" in
      1) cmd_check_update ;;
      2) cmd_apply_update ;;
      3) cmd_restart_hostpc ;;
      4) cmd_stop_all_managed ;;
      5)
        OMNIROAM_SKIP_ROS=0
        if [[ -f /opt/ros/noetic/setup.bash ]]; then
          # shellcheck source=/dev/null
          set +u; source /opt/ros/noetic/setup.bash; set -u
          if [[ -f "$CATKIN_WS/devel/setup.bash" ]]; then
            # shellcheck source=/dev/null
            set +u; source "$CATKIN_WS/devel/setup.bash"; set -u
          fi
          if ! pgrep -f rosmaster >/dev/null 2>&1; then
            nohup roscore >"$LOGDIR/roscore.log" 2>&1 &
            sleep 2
            log "roscore 已启动"
          else
            log "roscore 已在运行"
          fi
        else
          echo "  未安装 ROS Noetic。"
        fi
        ;;
      6)
        stop_roslaunch_only
        read -r -p "  是否同时停止 roscore? [y/N] " yn
        if [[ "$yn" =~ ^[yY] ]]; then
          stop_roscore
        fi
        ;;
      7)
        if command -v pnpm >/dev/null 2>&1; then
          (cd "$FRONTEND_DIR" && pnpm install && pnpm run build) && log "前端构建完成"
        else
          echo "  需要 pnpm。"
        fi
        ;;
      8)
        if [[ -f "$LOGDIR/hostpc.log" ]]; then
          tail -n 80 "$LOGDIR/hostpc.log"
          echo ""
          echo "  （跟随日志，Ctrl+C 返回菜单）"
          tail -f "$LOGDIR/hostpc.log"
        else
          echo "  尚无 $LOGDIR/hostpc.log"
        fi
        ;;
      9) stop_vite; start_vite_if_enabled ;;
      10) stop_vite ;;
      11)
        if command -v xdg-open >/dev/null 2>&1; then
          xdg-open "http://127.0.0.1:8080/" 2>/dev/null || true
        else
          echo "  无 xdg-open，请手动打开浏览器。"
        fi
        ;;
      0|"")
        echo "  再见。"
        exit 0
        ;;
      *)
        echo "  无效选项。"
        ;;
    esac
    echo ""
    read -r -p "  回车继续…" _
    echo ""
  done
}

#-------- main --------//
resolve_layout
mkdir -p "$STATEDIR" "$LOGDIR"
check_environment
start_ros_layer
if ! start_hostpc_backend; then
  log "后端启动未完全成功，仍可进入菜单排查。"
fi
start_vite_if_enabled || true
draw_banner_ok

if [[ "${OMNIROAM_MENU:-1}" == "0" ]]; then
  log "OMNIROAM_MENU=0，已退出（服务在后台运行）。"
  exit 0
fi

menu_loop
