#!/usr/bin/env bash
# 作用：按 C 控制核心、Go API、Vue 前端的顺序构建 AmseokBot-Milo。

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

# ==================== 依赖检查 ====================
# 作用：在构建前确认关键命令存在，避免中途报难懂错误。
# ================================================
check_build_tools() {
  have_cmd make || die "缺少 make，请先运行 scripts/install.sh"
  have_cmd gcc || die "缺少 gcc，请先运行 scripts/install.sh"
  have_cmd go || die "缺少 go，请先运行 scripts/install.sh"
  have_cmd pnpm || die "缺少 pnpm，请先运行 scripts/install.sh"
}

# ==================== C 控制核心 ====================
# 作用：编译底盘、机械臂、串口协议和低延迟硬件控制入口。
# ==================================================
build_control_core() {
  log "构建 C 控制核心"
  make -C "${AMSEOKBOT_REPO_DIR}/backend"
}

# ==================== Go API 层 ====================
# 作用：编译 HTTP API、鉴权、配置和前端通信服务。
# ================================================
build_go_api() {
  log "构建 Go API"
  make -C "${AMSEOKBOT_REPO_DIR}/backend-go"
}

# ==================== 前端界面 ====================
# 作用：安装前端依赖并生成由 Go API 托管的静态页面。
# ==================================================
build_frontend() {
  log "构建 Vue 前端"
  cd "${AMSEOKBOT_REPO_DIR}/frontend"
  pnpm install --frozen-lockfile || pnpm install
  pnpm run build
}

main() {
  require_repo
  check_build_tools
  ensure_runtime_dirs
  build_control_core
  build_go_api
  build_frontend
  log "构建完成"
}

main
