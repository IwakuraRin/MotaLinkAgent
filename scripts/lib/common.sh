#!/usr/bin/env bash
# 作用：提供 AmseokBot-Milo 部署脚本共用的路径、日志、权限和运行时目录函数。

set -Eeuo pipefail

# ==================== 路径配置 ====================
# 作用：计算仓库根目录，并提供可被 /etc/amseokbot/milo.env 覆盖的默认路径。
# ==================================================
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR_DEFAULT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
ENV_FILE_DEFAULT="/etc/amseokbot/milo.env"

AMSEOKBOT_REPO_DIR="${AMSEOKBOT_REPO_DIR:-${REPO_DIR_DEFAULT}}"
AMSEOKBOT_ENV_FILE="${AMSEOKBOT_ENV_FILE:-${ENV_FILE_DEFAULT}}"

if [[ -f "${AMSEOKBOT_ENV_FILE}" ]]; then
  # shellcheck disable=SC1090
  source "${AMSEOKBOT_ENV_FILE}"
fi

AMSEOKBOT_REPO_DIR="${AMSEOKBOT_REPO_DIR:-${REPO_DIR_DEFAULT}}"
AMSEOKBOT_API_ADDR="${AMSEOKBOT_API_ADDR:-0.0.0.0:8080}"
AMSEOKBOT_CONFIG_DIR="${AMSEOKBOT_CONFIG_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/etc}"
AMSEOKBOT_DATA_DIR="${AMSEOKBOT_DATA_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/data}"
AMSEOKBOT_LOG_DIR="${AMSEOKBOT_LOG_DIR:-${AMSEOKBOT_REPO_DIR}/.amseokbot/logs}"
AMSEOKBOT_STATIC_DIR="${AMSEOKBOT_STATIC_DIR:-${AMSEOKBOT_REPO_DIR}/frontend/dist}"
AMSEOKBOT_SETTINGS_FILE="${AMSEOKBOT_SETTINGS_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-settings.json}"
AMSEOKBOT_USERS_FILE="${AMSEOKBOT_USERS_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-users.cauth}"
AMSEOKBOT_CONTROL_CORE="${AMSEOKBOT_CONTROL_CORE:-${AMSEOKBOT_REPO_DIR}/backend/amseokbot-control-core}"
AMSEOKBOT_API_BIN="${AMSEOKBOT_API_BIN:-${AMSEOKBOT_REPO_DIR}/backend-go/hostpc-api}"
AMSEOKBOT_PID_FILE="${AMSEOKBOT_PID_FILE:-${AMSEOKBOT_DATA_DIR}/hostpc-api.pid}"
AMSEOKBOT_API_LOG="${AMSEOKBOT_API_LOG:-${AMSEOKBOT_LOG_DIR}/hostpc-api.log}"

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

have_cmd() {
  command -v "$1" >/dev/null 2>&1
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
  [[ -d "${AMSEOKBOT_REPO_DIR}/.git" ]] || die "没有找到 Git 仓库：${AMSEOKBOT_REPO_DIR}"
  [[ -d "${AMSEOKBOT_REPO_DIR}/backend" ]] || die "缺少 backend 目录"
  [[ -d "${AMSEOKBOT_REPO_DIR}/backend-go" ]] || die "缺少 backend-go 目录"
  [[ -d "${AMSEOKBOT_REPO_DIR}/frontend" ]] || die "缺少 frontend 目录"
}

ensure_runtime_dirs() {
  if install -d -m 0755 "${AMSEOKBOT_CONFIG_DIR}" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}" 2>/dev/null; then
    return
  fi
  as_root install -d -m 0755 "${AMSEOKBOT_CONFIG_DIR}" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}"
  as_root chown "$(id -u):$(id -g)" "${AMSEOKBOT_DATA_DIR}" "${AMSEOKBOT_LOG_DIR}"
}

api_args() {
  printf '%s\n' -addr "${AMSEOKBOT_API_ADDR}" -static "${AMSEOKBOT_STATIC_DIR}" -settings "${AMSEOKBOT_SETTINGS_FILE}" -users "${AMSEOKBOT_USERS_FILE}" -control-core "${AMSEOKBOT_CONTROL_CORE}"
}
