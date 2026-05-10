#!/usr/bin/env bash
# 作用：构建前端和 C 控制核心，然后启动 Go API 层绑定 0.0.0.0:8080 供局域网访问。

set -euo pipefail

# ==================== 路径配置 ====================
# 作用：定位 software 目录，并让前端和后端命令都在稳定路径下执行。
# ==================================================
ROOT="$(cd "$(dirname "$0")" && pwd)"

# ==================== 前端构建 ====================
# 作用：安装 pnpm 依赖并生成可由 Go API 层托管的静态资源。
# ==================================================
cd "$ROOT/frontend"
pnpm install
pnpm build

# ==================== C 控制核心构建 ====================
# 作用：编译 C 语言控制核心，供 Go API 层后续通过进程或本地 socket 调用。
# ======================================================
cd "$ROOT/backend"
make

# ==================== Go API 层启动 ====================
# 作用：由 Go 负责 HTTP API、鉴权、静态前端托管和轻量系统接口。
# =====================================================
cd "$ROOT/backend-go"
make
echo "Starting Go HostPC API server... (Ctrl+C to stop)"
exec ./hostpc-api -addr 0.0.0.0:8080 -static ../frontend/dist -settings ../backend/hostpc-settings.json -users ../backend/hostpc-users.cauth -control-core ../backend/hostpc-c
