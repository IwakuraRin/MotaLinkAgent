#!/usr/bin/env bash
# 作用：把 HostPC 的 Go API 层、C 控制核心和 Vue 静态资源安装为 systemd 服务。
# 用法（需 root）：
#   pnpm -C software/frontend install && pnpm -C software/frontend run build
#   make -C software/backend
#   make -C software/backend-go
#   sudo software/deploy/install-hostpc.sh
set -euo pipefail

# ==================== 路径配置 ====================
# 作用：定位仓库内已经构建好的 Go API、C 控制核心和前端产物。
# ==================================================
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
API_BIN_SRC="$ROOT/backend-go/hostpc-api"
CONTROL_BIN_SRC="$ROOT/backend/hostpc-c"
DIST_SRC="$ROOT/frontend/dist"
UNIT_SRC="$ROOT/deploy/omniroam-hostpc.service"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "请用 root 运行: sudo $0" >&2
  exit 1
fi
if [[ ! -x "$API_BIN_SRC" ]]; then
  echo "缺少 Go API 可执行文件: $API_BIN_SRC — 请先执行: (cd $ROOT/backend-go && make)" >&2
  exit 1
fi
if [[ ! -x "$CONTROL_BIN_SRC" ]]; then
  echo "缺少 C 控制核心: $CONTROL_BIN_SRC — 请先执行: (cd $ROOT/backend && make)" >&2
  exit 1
fi
if [[ ! -d "$DIST_SRC" ]] || [[ ! -f "$DIST_SRC/index.html" ]]; then
  echo "缺少前端构建: $DIST_SRC — 请先执行: (cd $ROOT/frontend && pnpm install && pnpm run build)" >&2
  exit 1
fi

# ==================== 系统用户 ====================
# 作用：创建低权限运行用户，并准备持久化数据目录。
# ==================================================
echo "==> 系统用户 omniroam"
if ! id -u omniroam &>/dev/null; then
  useradd --system --home-dir /var/lib/omniroam --create-home --shell /usr/sbin/nologin omniroam
fi
install -d -m 0750 -o omniroam -g omniroam /var/lib/omniroam
install -d -m 0755 /usr/lib/omniroam

# ==================== 文件安装 ====================
# 作用：安装 Go API、C 控制核心和前端静态资源。
# ==================================================
echo "==> 安装二进制与静态资源"
install -m 0755 "$API_BIN_SRC" /usr/sbin/hostpc-api
install -m 0755 "$CONTROL_BIN_SRC" /usr/lib/omniroam/hostpc-c
install -d -m 0755 /usr/share/omniroam/web
rsync -a --delete "$DIST_SRC/" /usr/share/omniroam/web/dist/

# ==================== systemd 服务 ====================
# 作用：安装并重启 HostPC 服务。
# =====================================================
echo "==> systemd"
install -m 0644 "$UNIT_SRC" /etc/systemd/system/omniroam-hostpc.service
systemctl daemon-reload
systemctl enable omniroam-hostpc.service
systemctl restart omniroam-hostpc.service

echo "==> 完成。服务: systemctl status omniroam-hostpc"
echo "    浏览器: http://$(hostname -I | awk '{print $1}'):8080/"
