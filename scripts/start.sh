#!/usr/bin/env bash
# 作用：启动 AmseokBot-Milo 本机服务；默认后台运行，--foreground 用于 systemd 托管。

set -Eeuo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
[[ -f "${SCRIPT_DIR}/lib/common.sh" ]] || { printf '[AmseokBot][ERROR] 缺少脚本库：%s\n' "${SCRIPT_DIR}/lib/common.sh" >&2; exit 1; }
source "${SCRIPT_DIR}/lib/common.sh"

FOREGROUND=0
for arg in "$@"; do
  case "${arg}" in
    --foreground) FOREGROUND=1 ;;
    *) die "未知参数：${arg}" ;;
  esac
done

# ==================== 启动前检查 ====================
# 作用：确认二进制和前端产物存在，不存在时自动构建。
# ==================================================
ensure_built() {
  if [[ ! -x "${AMSEOKBOT_API_BIN}" || ! -x "${AMSEOKBOT_CONTROL_CORE}" || ! -f "${AMSEOKBOT_STATIC_DIR}/index.html" ]]; then
    log "缺少构建产物，开始自动构建"
    "${AMSEOKBOT_REPO_DIR}/scripts/build.sh"
  fi
}

is_running() {
  [[ -f "${AMSEOKBOT_PID_FILE}" ]] && kill -0 "$(cat "${AMSEOKBOT_PID_FILE}")" >/dev/null 2>&1
}

# ==================== API 服务启动 ====================
# 作用：启动 Go API；C 控制核心由 API 按请求调用。
# ==================================================
start_foreground() {
  log "前台启动 Go API：${AMSEOKBOT_API_ADDR}"
  cd "${AMSEOKBOT_REPO_DIR}"
  exec "${AMSEOKBOT_API_BIN}" $(api_args)
}

start_background() {
  if is_running; then
    log "服务已在运行，PID=$(cat "${AMSEOKBOT_PID_FILE}")"
    return
  fi
  log "后台启动 Go API：${AMSEOKBOT_API_ADDR}"
  cd "${AMSEOKBOT_REPO_DIR}"
  nohup "${AMSEOKBOT_API_BIN}" $(api_args) >>"${AMSEOKBOT_API_LOG}" 2>&1 &
  printf '%s\n' "$!" >"${AMSEOKBOT_PID_FILE}"
  log "已启动，PID=$(cat "${AMSEOKBOT_PID_FILE}")，日志：${AMSEOKBOT_API_LOG}"
}

main() {
  require_repo
  ensure_runtime_dirs
  ensure_built
  if [[ "${FOREGROUND}" -eq 1 ]]; then
    start_foreground
  else
    start_background
  fi
}

main
