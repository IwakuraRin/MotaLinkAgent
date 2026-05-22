#!/usr/bin/env bash
# 作用：提供 AmseokBot-Milo 部署脚本共用的路径、日志、权限、仓库定位和运行时目录函数。

set -Eeuo pipefail

# ==================== 基础工具 ====================
# 作用：在路径定位前提供最小工具函数，避免脚本加载顺序依赖。
# ==================================================
have_cmd() {
  command -v "$1" >/dev/null 2>&1
}

# ==================== 路径配置 ====================
# 作用：从脚本真实位置向上定位项目根目录，不依赖用户名、home 目录或固定安装路径。
# ==================================================
COMMON_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SCRIPTS_DIR_DEFAULT="$(cd -P "${COMMON_DIR}/.." && pwd)"

find_repo_root() {
  local candidate="${1:-${SCRIPTS_DIR_DEFAULT}}"
  if have_cmd git; then
    local git_root
    git_root="$(git -C "${candidate}" rev-parse --show-toplevel 2>/dev/null || true)"
    if [[ -n "${git_root}" ]] && [[ -d "${git_root}/scripts" || -d "${git_root}/MasterComputer/scripts" ]]; then
      printf '%s\n' "${git_root}"
      return
    fi
  fi

  while [[ "${candidate}" != "/" ]]; do
    if [[ -d "${candidate}/scripts" && -d "${candidate}/backend" && -d "${candidate}/backend-go" && -d "${candidate}/frontend" ]]; then
      printf '%s\n' "${candidate}"
      return
    fi
    candidate="$(dirname "${candidate}")"
  done

  printf '%s\n' "$(cd -P "${SCRIPTS_DIR_DEFAULT}/.." && pwd)"
}

REPO_DIR_DEFAULT="$(find_repo_root "${SCRIPTS_DIR_DEFAULT}")"
ENV_FILE_DEFAULT="/etc/amseokbot/milo.env"

AMSEOKBOT_REPO_DIR="${AMSEOKBOT_REPO_DIR:-${REPO_DIR_DEFAULT}}"
AMSEOKBOT_MASTER_DIR="${AMSEOKBOT_MASTER_DIR:-${AMSEOKBOT_REPO_DIR}/MasterComputer}"
if [[ ! -d "${AMSEOKBOT_MASTER_DIR}/scripts" && -d "${AMSEOKBOT_REPO_DIR}/scripts" ]]; then
  AMSEOKBOT_MASTER_DIR="${AMSEOKBOT_REPO_DIR}"
fi
AMSEOKBOT_ENV_FILE="${AMSEOKBOT_ENV_FILE:-${ENV_FILE_DEFAULT}}"

if [[ -f "${AMSEOKBOT_ENV_FILE}" ]]; then
  # shellcheck disable=SC1090
  source "${AMSEOKBOT_ENV_FILE}"
fi

AMSEOKBOT_REPO_DIR="${AMSEOKBOT_REPO_DIR:-${REPO_DIR_DEFAULT}}"
AMSEOKBOT_MASTER_DIR="${AMSEOKBOT_MASTER_DIR:-${AMSEOKBOT_REPO_DIR}/MasterComputer}"
if [[ ! -d "${AMSEOKBOT_MASTER_DIR}/scripts" && -d "${AMSEOKBOT_REPO_DIR}/scripts" ]]; then
  AMSEOKBOT_MASTER_DIR="${AMSEOKBOT_REPO_DIR}"
fi
if [[ ! -d "${AMSEOKBOT_MASTER_DIR}/scripts" && -d "${REPO_DIR_DEFAULT}/MasterComputer/scripts" ]]; then
  AMSEOKBOT_REPO_DIR="${REPO_DIR_DEFAULT}"
  AMSEOKBOT_MASTER_DIR="${REPO_DIR_DEFAULT}/MasterComputer"
fi
if [[ ! -d "${AMSEOKBOT_MASTER_DIR}/scripts" && -d "${REPO_DIR_DEFAULT}/scripts" ]]; then
  AMSEOKBOT_REPO_DIR="${REPO_DIR_DEFAULT}"
  AMSEOKBOT_MASTER_DIR="${REPO_DIR_DEFAULT}"
fi
AMSEOKBOT_API_ADDR="${AMSEOKBOT_API_ADDR:-0.0.0.0:8080}"
AMSEOKBOT_CONFIG_DIR="${AMSEOKBOT_CONFIG_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/etc}"
AMSEOKBOT_DATA_DIR="${AMSEOKBOT_DATA_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/data}"
AMSEOKBOT_LOG_DIR="${AMSEOKBOT_LOG_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/logs}"
AMSEOKBOT_STATIC_DIR="${AMSEOKBOT_STATIC_DIR:-${AMSEOKBOT_MASTER_DIR}/frontend/dist}"
AMSEOKBOT_SETTINGS_FILE="${AMSEOKBOT_SETTINGS_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-settings.json}"
AMSEOKBOT_USERS_FILE="${AMSEOKBOT_USERS_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-users.cauth}"
AMSEOKBOT_CONTROL_CORE="${AMSEOKBOT_CONTROL_CORE:-${AMSEOKBOT_MASTER_DIR}/backend/amseokbot-control-core}"
AMSEOKBOT_API_BIN="${AMSEOKBOT_API_BIN:-${AMSEOKBOT_MASTER_DIR}/backend-go/hostpc-api}"
AMSEOKBOT_PID_FILE="${AMSEOKBOT_PID_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-api.pid}"
AMSEOKBOT_API_LOG="${AMSEOKBOT_API_LOG:-${AMSEOKBOT_LOG_DIR}/hostpc-api.log}"
AMSEOKBOT_ROS_DISTRO="${AMSEOKBOT_ROS_DISTRO:-noetic}"
AMSEOKBOT_ROS_WS="${AMSEOKBOT_ROS_WS:-${AMSEOKBOT_MASTER_DIR}/ros/catkin_ws}"
AMSEOKBOT_ROS_PACKAGE="${AMSEOKBOT_ROS_PACKAGE:-amseokbot_milo}"
AMSEOKBOT_ROS_LAUNCH="${AMSEOKBOT_ROS_LAUNCH:-amseokbot_vision_stream.launch}"
AMSEOKBOT_VIDEO_DEVICE="${AMSEOKBOT_VIDEO_DEVICE:-/dev/video0}"
AMSEOKBOT_WEB_VIDEO_PORT="${AMSEOKBOT_WEB_VIDEO_PORT:-8081}"
AMSEOKBOT_ROS_PID_FILE="${AMSEOKBOT_ROS_PID_FILE:-${AMSEOKBOT_DATA_DIR}/ros-vision.pid}"
AMSEOKBOT_ROS_LOG="${AMSEOKBOT_ROS_LOG:-${AMSEOKBOT_LOG_DIR}/ros-vision.log}"

SCRIPT_DIR="${SCRIPTS_DIR_DEFAULT}"

# ==================== 输出工具 ====================
# 作用：统一脚本日志格式，失败时给出明确错误。
# ==================================================
log() {
  printf '[AmseokBot] %s\n' "$*"
}

die() {
  printf '[AmseokBot][ERROR] %s\n' "$*" >&2
  exit 1
}

# ==================== 权限工具 ====================
# 作用：在普通用户和 root 环境下统一执行需要提权的命令。
# ==================================================
as_root() {
  if [[ "${EUID}" -eq 0 ]]; then
    "$@"
  else
    sudo "$@"
  fi
}

require_repo() {
  [[ -d "${AMSEOKBOT_REPO_DIR}" ]] || die "项目目录不存在：${AMSEOKBOT_REPO_DIR}"
  [[ -d "${AMSEOKBOT_MASTER_DIR}/backend" ]] || die "缺少 backend 目录"
  [[ -d "${AMSEOKBOT_MASTER_DIR}/backend-go" ]] || die "缺少 backend-go 目录"
  [[ -d "${AMSEOKBOT_MASTER_DIR}/frontend" ]] || die "缺少 frontend 目录"
  [[ -d "${AMSEOKBOT_MASTER_DIR}/scripts" ]] || die "缺少 scripts 目录"
}

require_git_repo() {
  [[ -d "${AMSEOKBOT_REPO_DIR}/.git" ]] || die "没有找到 Git 仓库：${AMSEOKBOT_REPO_DIR}；自动更新功能需要通过 git clone 部署"
}

ensure_runtime_dirs() {
  if install -d -m 0755 "${AMSEOKBOT_CONFIG_DIR}" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}" 2>/dev/null; then
    return
  fi
  as_root install -d -m 0755 "${AMSEOKBOT_CONFIG_DIR}" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}"
  as_root chown "$(id -u):$(id -g)" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}"
}

api_args() {
  printf '%s\n' -addr "${AMSEOKBOT_API_ADDR}" -static "${AMSEOKBOT_STATIC_DIR}" -settings "${AMSEOKBOT_SETTINGS_FILE}" -users "${AMSEOKBOT_USERS_FILE}" -control-core "${AMSEOKBOT_CONTROL_CORE}" -repo-root "${AMSEOKBOT_REPO_DIR}" -update-script "${AMSEOKBOT_MASTER_DIR}/scripts/update.sh"
}

make_temp_file() {
  local base="${AMSEOKBOT_DATA_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/data}"
  install -d -m 0755 "${base}" 2>/dev/null || true
  TMPDIR="${base}" mktemp
}
