#!/usr/bin/env bash
# 在已安装的 Ubuntu 20.04 Server（或其它 systemd 发行版）上安装 HostPC。
# 用法（需 root）：
#   cd HostPC && pnpm -C web install && pnpm -C web run build && go -C server build -o hostpc .
#   sudo ./deploy/install-hostpc.sh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN_SRC="$ROOT/server/hostpc"
DIST_SRC="$ROOT/web/dist"
UNIT_SRC="$ROOT/deploy/omniroam-hostpc.service"

if [[ "$(id -u)" -ne 0 ]]; then
  echo "请用 root 运行: sudo $0" >&2
  exit 1
fi
if [[ ! -x "$BIN_SRC" ]]; then
  echo "缺少可执行文件: $BIN_SRC — 请先执行: (cd $ROOT/server && go build -o hostpc .)" >&2
  exit 1
fi
if [[ ! -d "$DIST_SRC" ]] || [[ ! -f "$DIST_SRC/index.html" ]]; then
  echo "缺少前端构建: $DIST_SRC — 请先执行: (cd $ROOT/web && pnpm install && pnpm run build)" >&2
  exit 1
fi

echo "==> 系统用户 omniroam"
if ! id -u omniroam &>/dev/null; then
  useradd --system --home-dir /var/lib/omniroam --create-home --shell /usr/sbin/nologin omniroam
fi
install -d -m 0750 -o omniroam -g omniroam /var/lib/omniroam

echo "==> 安装二进制与静态资源"
install -m 0755 "$BIN_SRC" /usr/sbin/hostpc
install -d -m 0755 /usr/share/omniroam/web
rsync -a --delete "$DIST_SRC/" /usr/share/omniroam/web/dist/

echo "==> systemd"
install -m 0644 "$UNIT_SRC" /etc/systemd/system/omniroam-hostpc.service
systemctl daemon-reload
systemctl enable omniroam-hostpc.service
systemctl restart omniroam-hostpc.service

echo "==> 完成。服务: systemctl status omniroam-hostpc"
echo "    浏览器: http://$(hostname -I | awk '{print $1}'):8080/  （默认账号见首次部署说明）"
