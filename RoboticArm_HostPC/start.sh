#!/usr/bin/env bash
# 构建前端并启动 Go 服务，绑定 0.0.0.0:8080（局域网可访问）
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT/web"
pnpm install
pnpm build
cd "$ROOT/server"
echo "Starting server... (Ctrl+C to stop)"
exec go run . -addr 0.0.0.0:8080 -static ../web/dist
