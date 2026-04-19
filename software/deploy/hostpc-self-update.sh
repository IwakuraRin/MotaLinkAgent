#!/usr/bin/env bash
# Self-update: git pull repo root, rebuild frontend + Go binary, reinstall systemd service.
# Usage: bash hostpc-self-update.sh /path/to/OmniRoam
# Requires: git, pnpm, go, sudo (for install-hostpc.sh).
set -euo pipefail

REPO_ROOT="${1:?Pass repository root as first argument (e.g. /opt/OmniRoam)}"
PANEL=""
FRONT=""
BACK=""

if [[ -d "$REPO_ROOT/software/frontend" ]] && [[ -d "$REPO_ROOT/software/backend" ]]; then
  PANEL="$REPO_ROOT/software"
  FRONT="$PANEL/frontend"
  BACK="$PANEL/backend"
elif [[ -d "$REPO_ROOT/OmniControlPanel/frontend" ]] && [[ -d "$REPO_ROOT/OmniControlPanel/backend" ]]; then
  PANEL="$REPO_ROOT/OmniControlPanel"
  FRONT="$PANEL/frontend"
  BACK="$PANEL/backend"
elif [[ -d "$REPO_ROOT/OmniOS/frontend" ]] && [[ -d "$REPO_ROOT/OmniOS/backend" ]]; then
  PANEL="$REPO_ROOT/OmniOS"
  FRONT="$PANEL/frontend"
  BACK="$PANEL/backend"
elif [[ -d "$REPO_ROOT/OmniOS/hostpc/web" ]] && [[ -d "$REPO_ROOT/OmniOS/hostpc/server" ]]; then
  PANEL="$REPO_ROOT/OmniOS/hostpc"
  FRONT="$PANEL/web"
  BACK="$PANEL/server"
elif [[ -d "$REPO_ROOT/HostPC/web" ]] && [[ -d "$REPO_ROOT/HostPC/server" ]]; then
  PANEL="$REPO_ROOT/HostPC"
  FRONT="$PANEL/web"
  BACK="$PANEL/server"
else
  echo "未找到上位机目录（期望 software/、OmniControlPanel、OmniOS 或 HostPC 下的 frontend+backend 或 web+server）: $REPO_ROOT" >&2
  exit 1
fi

INSTALL="$PANEL/deploy/install-hostpc.sh"
if [[ ! -f "$INSTALL" ]]; then
  echo "缺少 install-hostpc.sh: $INSTALL" >&2
  exit 1
fi

cd "$REPO_ROOT"
echo "==> git pull (ff-only)"
git pull --ff-only

echo "==> pnpm install + build ($FRONT)"
if ! command -v pnpm >/dev/null 2>&1; then
  echo "pnpm not found in PATH" >&2
  exit 1
fi
(cd "$FRONT" && pnpm install && pnpm run build)

echo "==> go build hostpc ($BACK)"
(cd "$BACK" && go build -o hostpc .)

if [[ "${OMNIROAM_SKIP_SYSTEMD_INSTALL:-0}" == "1" ]]; then
  echo "==> OMNIROAM_SKIP_SYSTEMD_INSTALL=1，跳过 install-hostpc"
else
  echo "==> install-hostpc (needs sudo)"
  sudo "$INSTALL"
fi

echo "==> self-update done"
