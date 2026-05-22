#!/usr/bin/env bash
# 作用：启动 AmseokBot-Milo 本机服务；启动前自动检查更新、环境、构建产物和访问状态。

set -Eeuo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
[[ -f "${SCRIPT_DIR}/lib/common.sh" ]] || { printf '[AmseokBot][ERROR] 缺少脚本库：%s\n' "${SCRIPT_DIR}/lib/common.sh" >&2; exit 1; }
source "${SCRIPT_DIR}/lib/common.sh"

FOREGROUND=0
AUTO_UPDATE=1
AUTO_INSTALL=1
START_ROS=1
for arg in "$@"; do
  case "${arg}" in
    --foreground) FOREGROUND=1 ;;
    --no-update) AUTO_UPDATE=0 ;;
    --no-install) AUTO_INSTALL=0 ;;
    --no-ros) START_ROS=0 ;;
    *) die "未知参数：${arg}" ;;
  esac
done

# ==================== 更新检测 ====================
# 作用：启动前尝试同步远端快进更新；网络失败或本地有改动时不中断启动。
# ==================================================
auto_update_from_git() {
  [[ "${AUTO_UPDATE}" -eq 1 ]] || {
    log "已跳过自动更新检测"
    return
  }
  [[ -d "${AMSEOKBOT_REPO_DIR}/.git" ]] || {
    log "当前不是 Git 仓库，跳过自动更新检测"
    return
  }
  have_cmd git || {
    log "缺少 git，跳过自动更新检测"
    return
  }

  cd "${AMSEOKBOT_REPO_DIR}"
  local branch local_sha remote_sha
  branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || true)"
  if [[ -z "${branch}" || "${branch}" == "HEAD" ]]; then
    log "当前不在普通 Git 分支，跳过自动更新检测"
    return
  fi
  if [[ -n "$(git status --porcelain)" ]]; then
    log "工作区有本地改动，跳过自动更新检测"
    return
  fi

  log "检测远端更新：origin/${branch}"
  if ! git fetch --quiet origin "${branch}"; then
    log "远端更新检测失败，继续使用本地版本启动"
    return
  fi
  local_sha="$(git rev-parse HEAD)"
  remote_sha="$(git rev-parse "origin/${branch}")"
  if [[ "${local_sha}" == "${remote_sha}" ]]; then
    log "当前已经是最新版本：${local_sha:0:8}"
    return
  fi

  log "发现新版本，快进同步到：${remote_sha:0:8}"
  if git merge --ff-only --quiet "origin/${branch}"; then
    log "代码更新完成"
  else
    log "无法快进合并远端版本，继续使用本地版本启动"
  fi
}

# ==================== 环境检查 ====================
# 作用：确认当前机器具备启动、构建和访问前后端所需的基础条件。
# ==================================================
check_environment() {
  log "检查运行环境"
  require_repo
  ensure_runtime_dirs
  [[ -r "${AMSEOKBOT_MASTER_DIR}/backend-go/go.mod" ]] || die "缺少 backend-go/go.mod，Go API 无法构建"
  [[ -r "${AMSEOKBOT_MASTER_DIR}/frontend/package.json" ]] || die "缺少 frontend/package.json，前端无法构建"
  [[ -r "${AMSEOKBOT_MASTER_DIR}/frontend/pnpm-lock.yaml" ]] || die "缺少 frontend/pnpm-lock.yaml，前端依赖无法锁定"
  [[ -r "${AMSEOKBOT_MASTER_DIR}/backend/Makefile" ]] || die "缺少 backend/Makefile，C 控制核心无法构建"
  [[ -d "${AMSEOKBOT_ROS_WS}/src" ]] || log "缺少 ROS 工作区源码目录，将只启动前后端：${AMSEOKBOT_ROS_WS}/src"

  local missing=()
  have_cmd make || missing+=("make")
  have_cmd gcc || missing+=("gcc")
  have_cmd go || missing+=("go")
  have_cmd pnpm || missing+=("pnpm")
  if [[ "${#missing[@]}" -gt 0 ]]; then
    log "发现缺失环境：${missing[*]}"
    if [[ "${AUTO_INSTALL}" -eq 1 ]]; then
      log "尝试自动安装缺失环境"
      "${AMSEOKBOT_MASTER_DIR}/scripts/install.sh"
    else
      die "缺少 ${missing[*]}，请先运行 scripts/install.sh"
    fi
  fi

  have_cmd make || die "自动安装后仍缺少 make"
  have_cmd gcc || die "自动安装后仍缺少 gcc"
  have_cmd go || die "自动安装后仍缺少 go"
  have_cmd pnpm || die "自动安装后仍缺少 pnpm"
  have_cmd curl || log "缺少 curl，启动后将跳过 HTTP 访问验证"

  if [[ ! -w "${AMSEOKBOT_DATA_DIR}" || ! -w "${AMSEOKBOT_LOG_DIR}" ]]; then
    die "运行目录不可写：${AMSEOKBOT_DATA_DIR} 或 ${AMSEOKBOT_LOG_DIR}"
  fi
}

# ==================== 构建检查 ====================
# 作用：确认二进制和前端产物存在，不存在时自动构建。
# ==================================================
ensure_built() {
  local rebuild=0
  if [[ ! -x "${AMSEOKBOT_API_BIN}" || ! -x "${AMSEOKBOT_CONTROL_CORE}" || ! -f "${AMSEOKBOT_STATIC_DIR}/index.html" ]]; then
    rebuild=1
  elif ! "${AMSEOKBOT_API_BIN}" -h >/dev/null 2>&1; then
    log "Go API 二进制无法在当前系统运行，将本机重建"
    rebuild=1
  elif ! "${AMSEOKBOT_CONTROL_CORE}" health >/dev/null 2>&1; then
    log "C 控制核心无法在当前系统运行，将本机重建"
    rebuild=1
  fi

  if [[ "${rebuild}" -eq 1 ]]; then
    log "需要本机构建产物，开始自动构建"
    "${AMSEOKBOT_MASTER_DIR}/scripts/build.sh"
  else
    log "构建产物已存在"
  fi
}

is_running() {
  [[ -f "${AMSEOKBOT_PID_FILE}" ]] && kill -0 "$(cat "${AMSEOKBOT_PID_FILE}")" >/dev/null 2>&1
}

api_port() {
  printf '%s\n' "${AMSEOKBOT_API_ADDR##*:}"
}

bind_host() {
  local host
  host="${AMSEOKBOT_API_ADDR%:*}"
  printf '%s\n' "${host:-0.0.0.0}"
}

lan_ips() {
  if have_cmd ip; then
    {
      ip -4 route get 1.1.1.1 2>/dev/null | awk '{ for (i = 1; i <= NF; i++) if ($i == "src") print $(i + 1) }'
      ip -4 -o addr show scope global up 2>/dev/null | awk '$2 !~ /^(docker|br-|veth|lxdbr|virbr)/ { split($4, a, "/"); print a[1] }'
    } | awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ && $0 !~ /^127\\./ && !seen[$0]++'
    return
  fi
  if have_cmd hostname; then
    hostname -I 2>/dev/null | tr ' ' '\n' | awk '/^[0-9]+\.[0-9]+\.[0-9]+\.[0-9]+$/ && $0 !~ /^127\\./ && !seen[$0]++ { print }'
  fi
}

health_urls() {
  local host port ip printed=0
  host="$(bind_host)"
  port="$(api_port)"
  if [[ "${host}" == "0.0.0.0" || "${host}" == "::" || "${host}" == "*" ]]; then
    while IFS= read -r ip; do
      [[ -n "${ip}" ]] || continue
      printf 'http://%s:%s/api/health\n' "${ip}" "${port}"
      printed=1
    done < <(lan_ips)
    [[ "${printed}" -eq 1 ]] && return
    printf 'http://127.0.0.1:%s/api/health\n' "${port}"
    return
  fi
  printf 'http://%s:%s/api/health\n' "${host}" "${port}"
}

frontend_urls() {
  health_urls | sed 's#/api/health$#/#'
}

video_stream_urls() {
  local ip printed=0 host
  host="$(bind_host)"
  if [[ "${host}" == "0.0.0.0" || "${host}" == "::" || "${host}" == "*" ]]; then
    while IFS= read -r ip; do
      [[ -n "${ip}" ]] || continue
      printf 'http://%s:%s/stream?topic=/obstacle_detector/debug\n' "${ip}" "${AMSEOKBOT_WEB_VIDEO_PORT}"
      printed=1
    done < <(lan_ips)
    [[ "${printed}" -eq 1 ]] && return
    printf 'http://127.0.0.1:%s/stream?topic=/obstacle_detector/debug\n' "${AMSEOKBOT_WEB_VIDEO_PORT}"
    return
  fi
  printf 'http://%s:%s/stream?topic=/obstacle_detector/debug\n' "${host}" "${AMSEOKBOT_WEB_VIDEO_PORT}"
}

verify_access() {
  have_cmd curl || return
  local url ok=0
  while IFS= read -r url; do
    [[ -n "${url}" ]] || continue
    for _ in {1..30}; do
      if curl -fsS "${url}" >/dev/null 2>&1; then
        log "局域网服务访问正常：${url}"
        ok=1
        break
      fi
      sleep 0.2
    done
    [[ "${ok}" -eq 1 ]] && break
  done < <(health_urls)

  if [[ "${ok}" -ne 1 ]]; then
    die "服务已启动但局域网健康检查失败，请确认网卡 IP、防火墙和端口 ${AMSEOKBOT_API_ADDR}，日志：${AMSEOKBOT_API_LOG}"
  fi

  log "前端局域网访问地址："
  while IFS= read -r url; do
    [[ -n "${url}" ]] && log "  ${url}"
  done < <(frontend_urls)
}

# ==================== ROS 视觉链路 ====================
# 作用：启动 USB 摄像头、YOLO 带框调试图和 web_video_server 推流服务。
# ======================================================
source_ros_env() {
  local ros_setup="/opt/ros/${AMSEOKBOT_ROS_DISTRO}/setup.bash"
  [[ -r "${ros_setup}" ]] || return 1
  set +u
  # shellcheck disable=SC1090
  source "${ros_setup}"
  if [[ -r "${AMSEOKBOT_ROS_WS}/devel/setup.bash" ]]; then
    # shellcheck disable=SC1090
    source "${AMSEOKBOT_ROS_WS}/devel/setup.bash"
  fi
  set -u
}

ros_is_running() {
  [[ -f "${AMSEOKBOT_ROS_PID_FILE}" ]] && kill -0 "$(cat "${AMSEOKBOT_ROS_PID_FILE}")" >/dev/null 2>&1
}

ensure_ros_workspace_built() {
  [[ "${START_ROS}" -eq 1 ]] || return 0
  [[ -d "${AMSEOKBOT_ROS_WS}/src" ]] || {
    log "未找到 ROS 工作区，跳过 ROS 启动：${AMSEOKBOT_ROS_WS}"
    return
  }
  if ! source_ros_env; then
    log "未找到 ROS ${AMSEOKBOT_ROS_DISTRO} 环境，跳过 ROS 启动"
    return
  fi
  have_cmd roslaunch || {
    log "缺少 roslaunch，跳过 ROS 启动"
    return
  }
  local need_build=0
  if ! rospack find "${AMSEOKBOT_ROS_PACKAGE}" >/dev/null 2>&1; then
    need_build=1
  fi
  if [[ -f "${AMSEOKBOT_ROS_WS}/src/${AMSEOKBOT_ROS_PACKAGE}/scripts/yolo_obstacle_detector_ort.py" ]] && [[ ! -x "${AMSEOKBOT_ROS_WS}/devel/lib/${AMSEOKBOT_ROS_PACKAGE}/yolo_obstacle_detector_ort.py" ]]; then
    need_build=1
  fi
  if [[ "${need_build}" -eq 1 ]]; then
    have_cmd catkin_make || die "缺少 catkin_make，无法构建 ROS 工作区"
    log "构建 ROS 工作区：${AMSEOKBOT_ROS_WS}"
    cd "${AMSEOKBOT_ROS_WS}"
    catkin_make
    source_ros_env || die "ROS 工作区构建后仍无法加载环境"
  fi
}

start_ros_stack() {
  [[ "${START_ROS}" -eq 1 ]] || {
    log "已跳过 ROS 启动"
    return
  }
  [[ -d "${AMSEOKBOT_ROS_WS}/src" ]] || return
  if ros_is_running; then
    log "ROS 视觉链路已在运行，PID=$(cat "${AMSEOKBOT_ROS_PID_FILE}")"
    return
  fi
  if ! source_ros_env; then
    log "ROS 环境不可用，未启动视觉链路"
    return
  fi
  if ! rospack find "${AMSEOKBOT_ROS_PACKAGE}" >/dev/null 2>&1; then
    log "ROS 包不可用，未启动视觉链路：${AMSEOKBOT_ROS_PACKAGE}"
    return
  fi
  log "后台启动 ROS 视觉链路：${AMSEOKBOT_ROS_PACKAGE} ${AMSEOKBOT_ROS_LAUNCH}"
  cd "${AMSEOKBOT_ROS_WS}"
  nohup roslaunch "${AMSEOKBOT_ROS_PACKAGE}" "${AMSEOKBOT_ROS_LAUNCH}" \
    video_device:="${AMSEOKBOT_VIDEO_DEVICE}" \
    web_video_port:="${AMSEOKBOT_WEB_VIDEO_PORT}" >>"${AMSEOKBOT_ROS_LOG}" 2>&1 &
  printf '%s\n' "$!" >"${AMSEOKBOT_ROS_PID_FILE}"
  log "ROS 已启动，PID=$(cat "${AMSEOKBOT_ROS_PID_FILE}")，日志：${AMSEOKBOT_ROS_LOG}"
  log "YOLO 推流地址："
  while IFS= read -r url; do
    [[ -n "${url}" ]] && log "  ${url}"
  done < <(video_stream_urls)
}

# ==================== API 服务启动 ====================
# 作用：启动 Go API 并托管 frontend/dist；C 控制核心由 API 按请求调用。
# ==================================================
start_foreground() {
  log "前台启动 Go API：${AMSEOKBOT_API_ADDR}"
  cd "${AMSEOKBOT_REPO_DIR}"
  exec "${AMSEOKBOT_API_BIN}" $(api_args)
}

start_background() {
  if is_running; then
    log "服务已在运行，PID=$(cat "${AMSEOKBOT_PID_FILE}")"
    verify_access
    return
  fi
  log "后台启动 Go API：${AMSEOKBOT_API_ADDR}"
  cd "${AMSEOKBOT_REPO_DIR}"
  : >"${AMSEOKBOT_API_LOG}"
  nohup "${AMSEOKBOT_API_BIN}" $(api_args) >>"${AMSEOKBOT_API_LOG}" 2>&1 &
  printf '%s\n' "$!" >"${AMSEOKBOT_PID_FILE}"
  log "已启动，PID=$(cat "${AMSEOKBOT_PID_FILE}")，日志：${AMSEOKBOT_API_LOG}"
  verify_access
}

main() {
  require_repo
  auto_update_from_git
  check_environment
  ensure_built
  ensure_ros_workspace_built
  if [[ "${FOREGROUND}" -eq 1 ]]; then
    start_ros_stack
    start_foreground
  else
    start_background
    start_ros_stack
  fi
}

main
