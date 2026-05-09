#!/usr/bin/env bash
# 作用：构建前端并启动 C 语言版 HostPC 后端，绑定 0.0.0.0:8080 供局域网访问。

set -euo pipefail

# ==================== 路径配置 ====================
# 作用：定位 software 目录，并让前端和后端命令都在稳定路径下执行。
# ==================================================
ROOT="$(cd "$(dirname "$0")" && pwd)"

# ==================== 前端构建 ====================
# 作用：安装 pnpm 依赖并生成可由 C 后端托管的静态资源。
# ==================================================
cd "$ROOT/frontend"
pnpm install
pnpm build

# ==================== C 后端启动 ====================
# 作用：编译 C 服务并以原来的 8080 端口提供 HTTP、WebSocket 和静态前端。
# ==================================================
cd "$ROOT/backend"
make
echo "Starting C HostPC server... (Ctrl+C to stop)"
exec ./hostpc-c -addr 0.0.0.0:8080 -static ../frontend/dist -settings hostpc-settings.json -users hostpc-users.cauth
