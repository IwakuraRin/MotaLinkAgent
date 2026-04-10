#!/usr/bin/env bash
# OmniRoam 一键更新（由 HostPC「检测到更新」后调用，也可手动执行）
#
# 流程概要：
#   1) 将 GitHub 源码包下载到 Ubuntu 默认「下载」目录（~/Downloads，可用 OMNIROAM_DOWNLOAD_DIR 覆盖）
#   2) 在下载目录内解压
#   3) 将解压出的顶层目录内容 rsync 到本仓库根（保留 .git/，排除常见构建产物以免误删本地缓存可选）
#   4) git fetch + reset --hard 与远端分支对齐（若存在 .git）
#   5) 构建 HostPC 前端 + Go，并可选执行 install-hostpc.sh（systemd）
#   6) 调用同目录下的 restart.sh 重启全部服务
#
# 用法：
#   bash /path/to/OmniRoam/update.sh [/path/to/repo]
#
# 环境变量（可选）：
#   OMNIROAM_GITHUB_SLUG   owner/repo，默认从 git remote origin 解析
#   OMNIROAM_GITHUB_BRANCH 分支名，默认 main
#   OMNIROAM_DOWNLOAD_DIR  下载与解压目录，默认 $HOME/Downloads
#   OMNIROAM_ARCHIVE       若已手动下载好压缩包，设为绝对路径则跳过 wget（仍解压到 Downloads 下临时目录）
#   OMNIROAM_SKIP_SYSTEMD_INSTALL  设为 1 则跳过 sudo install-hostpc.sh（仅本地 go run 场景）
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "${1:-}" != "" ]]; then
  REPO_ROOT="$(cd "$1" && pwd)"
fi

DOWNLOAD_DIR="${OMNIROAM_DOWNLOAD_DIR:-$HOME/Downloads}"
BRANCH="${OMNIROAM_GITHUB_BRANCH:-main}"
SLUG="${OMNIROAM_GITHUB_SLUG:-}"

log() { echo "[omniroam-update] $*"; }
die() { log "ERROR: $*"; exit 1; }

mkdir -p "$DOWNLOAD_DIR"

#-------- 解析 owner/repo --------
if [[ -z "$SLUG" ]] && [[ -d "$REPO_ROOT/.git" ]]; then
  url="$(git -C "$REPO_ROOT" remote get-url origin 2>/dev/null || true)"
  if [[ "$url" =~ github\.com[:/]([^/]+)/([^/.]+)(\.git)?$ ]]; then
    SLUG="${BASH_REMATCH[1]}/${BASH_REMATCH[2]}"
  fi
fi
[[ -n "$SLUG" ]] || die "无法得到 GitHub slug，请设置 OMNIROAM_GITHUB_SLUG=owner/repo 或确保 .git 与 origin 指向 github.com"

OWNER="${SLUG%%/*}"
NAME="${SLUG#*/}"
[[ -n "$OWNER" && -n "$NAME" ]] || die "无效的 OMNIROAM_GITHUB_SLUG: $SLUG"

WORKDIR="$(mktemp -d "$DOWNLOAD_DIR/omniroam-update.XXXXXX")"
cleanup() { rm -rf "$WORKDIR"; }
trap cleanup EXIT

TGZ_LOCAL=""
if [[ -n "${OMNIROAM_ARCHIVE:-}" ]] && [[ -f "$OMNIROAM_ARCHIVE" ]]; then
  TGZ_LOCAL="$OMNIROAM_ARCHIVE"
  log "使用已有压缩包: $TGZ_LOCAL"
else
  ZIP_URL="https://codeload.github.com/${OWNER}/${NAME}/tar.gz/${BRANCH}"
  TGZ_LOCAL="$WORKDIR/source.tar.gz"
  log "下载: $ZIP_URL"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$ZIP_URL" -o "$TGZ_LOCAL"
  elif command -v wget >/dev/null 2>&1; then
    wget -q -O "$TGZ_LOCAL" "$ZIP_URL"
  else
    die "需要 curl 或 wget"
  fi
fi

EXTRACT="$WORKDIR/extract"
mkdir -p "$EXTRACT"
tar -xzf "$TGZ_LOCAL" -C "$EXTRACT"
TOP="$(find "$EXTRACT" -mindepth 1 -maxdepth 1 -type d | head -1)"
[[ -d "$TOP" ]] || die "解压后未找到顶层目录"

log "同步源码到: $REPO_ROOT（保留 .git）"
rsync -a --delete \
  --exclude='.git/' \
  --exclude='catkin_ws/build/' \
  --exclude='catkin_ws/devel/' \
  --exclude='catkin_ws/install/' \
  --exclude='catkin_ws/logs/' \
  --exclude='HostPC/web/node_modules/' \
  --exclude='HostPC/server/hostpc' \
  "$TOP/" "$REPO_ROOT/"

if [[ -d "$REPO_ROOT/.git" ]]; then
  log "git fetch + reset --hard origin/$BRANCH"
  git -C "$REPO_ROOT" fetch origin "$BRANCH" || log "WARN: git fetch 失败（可检查网络或 remote 名称）"
  if git -C "$REPO_ROOT" show-ref --verify --quiet "refs/remotes/origin/${BRANCH}"; then
    git -C "$REPO_ROOT" reset --hard "origin/${BRANCH}"
  elif git -C "$REPO_ROOT" show-ref --verify --quiet "refs/remotes/orgin/${BRANCH}"; then
    git -C "$REPO_ROOT" reset --hard "orgin/${BRANCH}"
  else
    log "WARN: 未找到 origin/$BRANCH，跳过 reset --hard"
  fi
fi

HOSTPC="$REPO_ROOT/HostPC"
[[ -d "$HOSTPC/web" ]] || die "未找到 HostPC/web"

log "构建前端"
if ! command -v pnpm >/dev/null 2>&1; then
  die "未找到 pnpm，请先安装 Node/pnpm（见 setup_hostpc_ubuntu20.sh）"
fi
(cd "$HOSTPC/web" && pnpm install && pnpm run build)

log "构建 hostpc 二进制"
(cd "$HOSTPC/server" && go build -o hostpc .)

if [[ "${OMNIROAM_SKIP_SYSTEMD_INSTALL:-0}" != "1" ]] && [[ -x "$HOSTPC/deploy/install-hostpc.sh" ]] && command -v sudo >/dev/null 2>&1; then
  if [[ "${EUID}" -eq 0 ]]; then
    "$HOSTPC/deploy/install-hostpc.sh"
  else
    log "安装 systemd 服务（需要 sudo）"
    sudo "$HOSTPC/deploy/install-hostpc.sh" || log "WARN: install-hostpc.sh 失败，请手动安装或仅用 start.sh 开发启动"
  fi
fi

log "执行 restart.sh"
trap - EXIT
rm -rf "$WORKDIR"
exec bash "$REPO_ROOT/restart.sh"
