#!/usr/bin/env bash
#
# OmniRoam 上位机一键环境 — 面向 Ubuntu 20.04 (focal) Server/Desktop
#
# 安装：ROS 1 Noetic（ros-base + 常用包）、编译工具、python3-serial/numpy、
#       Go、Node.js + pnpm（HostPC 前后端）、串口 dialout 组。
# 可选：在本仓库内执行 catkin_make、rosdep install。
#
# 用法（本脚本位于仓库根目录）：
#   cd /path/to/simpleRoboticArm
#   bash setup_hostpc_ubuntu20.sh
#
# 需 sudo；建议在全新 Ubuntu 20.04 上首次配置时运行。
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DEBIAN_FRONTEND=noninteractive

log() { echo "[setup] $*"; }
die() { echo "[setup] ERROR: $*" >&2; exit 1; }

if [[ "${EUID}" -eq 0 ]]; then
  die "请不要用 root 直接执行本脚本；请用普通用户运行，脚本会在需要时调用 sudo。"
fi

sudo -v

# --- 校验系统 ---
[[ -r /etc/os-release ]] || die "无法读取 /etc/os-release"
# shellcheck source=/dev/null
. /etc/os-release
[[ "${ID}" == "ubuntu" ]] || die "当前系统不是 Ubuntu（ID=${ID:-?}），本脚本针对 Ubuntu 20.04。"
[[ "${VERSION_ID}" == "20.04" ]] || die "当前版本为 ${VERSION_ID:-?}，请使用 Ubuntu 20.04 (focal) 再运行本脚本。"

log "检测到 Ubuntu ${VERSION_ID} (${VERSION_CODENAME})"

# --- APT 基础 ---
sudo apt-get update -qq
sudo apt-get install -y \
  apt-transport-https \
  ca-certificates \
  curl \
  gnupg \
  lsb-release \
  software-properties-common \
  build-essential \
  git \
  wget \
  udev

sudo add-apt-repository universe -y 2>/dev/null || true
sudo apt-get update -qq

# --- ROS 1 Noetic（官方源，signed-by）---
if [[ ! -f /usr/share/keyrings/ros-archive-keyring.gpg ]]; then
  log "添加 ROS apt 源与密钥…"
  sudo curl -sSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key \
    -o /usr/share/keyrings/ros-archive-keyring.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros/ubuntu ${VERSION_CODENAME} main" \
    | sudo tee /etc/apt/sources.list.d/ros.list > /dev/null
fi

sudo apt-get update -qq
log "安装 ros-noetic-ros-base 与常用组件（无桌面 GUI，适合 Server）…"
sudo apt-get install -y \
  ros-noetic-ros-base \
  ros-noetic-catkin \
  ros-noetic-rospy \
  ros-noetic-roscpp \
  ros-noetic-std-msgs \
  ros-noetic-geometry-msgs \
  ros-noetic-sensor-msgs \
  ros-noetic-nav-msgs \
  ros-noetic-tf \
  ros-noetic-tf2 \
  ros-noetic-tf2-ros \
  ros-noetic-roslaunch \
  ros-noetic-message-runtime \
  python3-rosdep \
  python3-rosinstall \
  python3-rosinstall-generator \
  python3-vcstool \
  python3-pip \
  python3-dev \
  cmake \
  libgtest-dev

# rosdep（仅当未初始化时）
if [[ ! -f /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
  log "初始化 rosdep…"
  sudo rosdep init || true
fi
rosdep update

# 工作区依赖（与 package.xml 对齐）
sudo apt-get install -y python3-serial python3-numpy

# 串口权限
REAL_USER="${SUDO_USER:-$USER}"
log "将用户 ${REAL_USER} 加入 dialout 组（USB 串口）…"
sudo usermod -aG dialout "${REAL_USER}" || true

# --- Go（官方 tarball，不依赖 Ubuntu 自带旧版）---
GO_VERSION="${GO_VERSION:-1.22.10}"
if ! command -v go >/dev/null 2>&1 || [[ "$(go version 2>/dev/null)" != *"go${GO_VERSION}"* ]]; then
  log "安装 Go ${GO_VERSION} 到 /usr/local/go …"
  tmpd="$(mktemp -d)"
  trap 'rm -rf "${tmpd}"' EXIT
  arch="$(uname -m)"
  case "${arch}" in
    x86_64) goarch=amd64 ;;
    aarch64) goarch=arm64 ;;
    *) die "不支持的架构: ${arch}" ;;
  esac
  wget -q "https://go.dev/dl/go${GO_VERSION}.linux-${goarch}.tar.gz" -O "${tmpd}/go.tgz"
  sudo rm -rf /usr/local/go
  sudo tar -C /usr/local -xzf "${tmpd}/go.tgz"
fi
grep -q '/usr/local/go/bin' ~/.profile 2>/dev/null || echo 'export PATH=$PATH:/usr/local/go/bin' >> ~/.profile
export PATH="${PATH}:/usr/local/go/bin"

# --- Node.js 20 LTS + pnpm（NodeSource，适配 focal）---
if ! command -v node >/dev/null 2>&1 || [[ "$(node -v 2>/dev/null || true)" != v20* ]]; then
  log "通过 NodeSource 安装 Node.js 20 …"
  curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
  sudo apt-get install -y nodejs
fi
sudo corepack enable 2>/dev/null || true
corepack prepare pnpm@9 --activate 2>/dev/null || sudo corepack prepare pnpm@9 --activate

# --- 可选：rosdep 安装工作区系统依赖 + catkin_make ---
if [[ -d "${REPO_ROOT}/catkin_ws/src" ]]; then
  log "rosdep 安装 catkin_ws 依赖（--ignore-src）…"
  pushd "${REPO_ROOT}/catkin_ws" >/dev/null
  rosdep install --from-paths src --ignore-src -r -y || log "rosdep 部分包未解析可忽略（若编译失败再补装）"
  log "catkin_make …"
  source /opt/ros/noetic/setup.bash
  catkin_make -DCMAKE_BUILD_TYPE=Release
  popd >/dev/null
else
  log "未找到 ${REPO_ROOT}/catkin_ws/src，跳过 catkin_make。"
fi

# --- HostPC Go 依赖 ---
if [[ -d "${REPO_ROOT}/RoboticArm_HostPC/server" ]]; then
  log "go mod tidy（HostPC server）…"
  ( cd "${REPO_ROOT}/RoboticArm_HostPC/server" && go mod tidy && go build -o /dev/null . )
fi

log "完成。"
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "  请 **重新登录** 或执行: newgrp dialout   （使串口组生效）"
echo "  ROS 使用:"
echo "    source /opt/ros/noetic/setup.bash"
echo "    source ${REPO_ROOT}/catkin_ws/devel/setup.bash"
echo "  或:"
echo "    source ${REPO_ROOT}/setup_ros1.bash"
echo ""
echo "  HostPC 网页控制台:"
echo "    cd ${REPO_ROOT}/RoboticArm_HostPC && ./start.sh"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
