#!/usr/bin/env bash
# 包装：实际逻辑在 scripts/setup_ros1.bash（仓库根 = 脚本目录的上一级）
_REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=/dev/null
source "${_REPO}/scripts/setup_ros1.bash"
