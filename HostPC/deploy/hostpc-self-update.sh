#!/usr/bin/env bash
# Self-update: git pull repo root, rebuild HostPC web + Go binary, reinstall systemd service.
# Usage: bash hostpc-self-update.sh /path/to/simpleRoboticArm
# Requires: git, pnpm, go, sudo (for install-hostpc.sh).
set -euo pipefail

REPO_ROOT="${1:?Pass repository root as first argument (e.g. /opt/simpleRoboticArm)}"
HOSTPC="$REPO_ROOT/HostPC"

if [[ ! -d "$HOSTPC" ]]; then
  echo "HostPC directory not found: $HOSTPC" >&2
  exit 1
fi

cd "$REPO_ROOT"
echo "==> git pull (ff-only)"
git pull --ff-only

cd "$HOSTPC/web"
echo "==> pnpm install + build"
if ! command -v pnpm >/dev/null 2>&1; then
  echo "pnpm not found in PATH" >&2
  exit 1
fi
pnpm install
pnpm run build

cd "$HOSTPC/server"
echo "==> go build hostpc"
go build -o hostpc .

echo "==> install-hostpc (needs sudo)"
sudo "$HOSTPC/deploy/install-hostpc.sh"

echo "==> self-update done"
