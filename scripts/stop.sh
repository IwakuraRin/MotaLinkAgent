#!/usr/bin/env bash
# 作用：停止由 scripts/start.sh 后台启动的 AmseokBot-Milo 服务。

set -Eeuo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
[[ -f "${SCRIPT_DIR}/lib/common.sh" ]] || { printf '[AmseokBot][ERROR] 缺少脚本库：%s\n' "${SCRIPT_DIR}/lib/common.sh" >&2; exit 1; }
source "${SCRIPT_DIR}/lib/common.sh"

# ==================== 停止后台进程 ====================
# 作用：读取 PID 文件，优雅终止 Go API 进程并清理 PID。
# ======================================================
main() {
  if [[ ! -f "${AMSEOKBOT_PID_FILE}" ]]; then
    log "没有 PID 文件，服务可能未通过 scripts/start.sh 启动"
    return
  fi
  local pid
  pid="$(cat "${AMSEOKBOT_PID_FILE}")"
  if kill -0 "${pid}" >/dev/null 2>&1; then
    log "停止服务，PID=${pid}"
    kill "${pid}"
    for _ in {1..20}; do
      kill -0 "${pid}" >/dev/null 2>&1 || break
      sleep 0.2
    done
    if kill -0 "${pid}" >/dev/null 2>&1; then
      log "进程未退出，强制结束 PID=${pid}"
      kill -9 "${pid}" || true
    fi
  else
    log "PID=${pid} 已不存在"
  fi
  rm -f "${AMSEOKBOT_PID_FILE}"
  log "服务已停止"
}

main
