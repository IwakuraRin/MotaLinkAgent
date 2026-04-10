#!/usr/bin/env bash
# OmniRoam 一键重启：先停掉本脚本管理的进程与服务，再调用 start.sh
#
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
STATEDIR="$ROOT/.omniroam"
LOGDIR="${OMNIROAM_LOG_DIR:-$STATEDIR/logs}"

log() { echo "[omniroam-restart] $*"; }

#-------- systemd HostPC --------
if systemctl is-active omniroam-hostpc.service &>/dev/null; then
  log "停止 omniroam-hostpc.service"
  sudo systemctl stop omniroam-hostpc.service || true
fi

#-------- 开发模式 hostpc（PID 文件）--------
if [[ -f "$STATEDIR/hostpc.pid" ]]; then
  pid="$(cat "$STATEDIR/hostpc.pid" 2>/dev/null || true)"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    log "停止 hostpc pid $pid"
    kill "$pid" 2>/dev/null || true
    sleep 1
    kill -9 "$pid" 2>/dev/null || true
  fi
  rm -f "$STATEDIR/hostpc.pid"
fi

#-------- roslaunch（若 start.sh 写过 pid）--------
if [[ -f "$STATEDIR/roslaunch.pid" ]]; then
  pid="$(cat "$STATEDIR/roslaunch.pid" 2>/dev/null || true)"
  if [[ -n "$pid" ]] && kill -0 "$pid" 2>/dev/null; then
    log "停止 roslaunch pid $pid"
    kill "$pid" 2>/dev/null || true
  fi
  rm -f "$STATEDIR/roslaunch.pid"
fi
pkill -f "roslaunch.*simple_robotic_arm" 2>/dev/null || true

#-------- roscore --------
if pgrep -f rosmaster >/dev/null 2>&1; then
  log "停止 roscore / rosmaster"
  pkill -f rosmaster 2>/dev/null || true
  sleep 1
  pkill -9 -f rosmaster 2>/dev/null || true
fi

sleep 2
log "重新启动…"
exec bash "$ROOT/start.sh"
