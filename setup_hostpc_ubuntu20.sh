#!/usr/bin/env bash
# 包装：实际逻辑在 scripts/setup_hostpc_ubuntu20.sh
exec bash "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/scripts/setup_hostpc_ubuntu20.sh" "$@"
