#!/usr/bin/env bash
# 作用：自动检测 Ubuntu 环境、安装缺失依赖、生成本机配置，并构建 AmseokBot-Milo。

set -Eeuo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
[[ -f "${SCRIPT_DIR}/lib/common.sh" ]] || { printf '[AmseokBot][ERROR] 缺少脚本库：%s\n' "${SCRIPT_DIR}/lib/common.sh" >&2; exit 1; }
source "${SCRIPT_DIR}/lib/common.sh"

SKIP_APT=0
for arg in "$@"; do
  case "${arg}" in
    --no-apt) SKIP_APT=1 ;;
    *) die "未知参数：${arg}" ;;
  esac
done

# ==================== 系统环境检测 ====================
# 作用：确认脚本运行在 Debian/Ubuntu 系，并补齐编译与前端构建依赖。
# ======================================================
install_apt_packages() {
  if [[ "${SKIP_APT}" -eq 1 ]]; then
    log "跳过 apt 依赖安装"
    return
  fi
  have_cmd apt-get || die "当前系统没有 apt-get；此脚本优先支持 Ubuntu/Debian"
  log "安装基础依赖：git curl ca-certificates build-essential gcc make golang-go nodejs npm"
  as_root apt-get update
  as_root env DEBIAN_FRONTEND=noninteractive apt-get install -y git curl ca-certificates build-essential gcc make golang-go nodejs npm openssl
}

install_pnpm() {
  if have_cmd pnpm; then
    log "pnpm 已存在：$(pnpm --version)"
    return
  fi
  if have_cmd corepack; then
    log "通过 corepack 启用 pnpm"
    corepack enable
    corepack prepare pnpm@9.15.0 --activate
    return
  fi
  have_cmd npm || die "缺少 npm，无法安装 pnpm"
  log "通过 npm 安装 pnpm@9.15.0"
  as_root npm install -g pnpm@9.15.0
}

# ==================== 本机配置生成 ====================
# 作用：创建只存在于本机的环境文件，保存运行路径和首次默认登录密码。
# ======================================================
DEFAULT_HOSTPC_USER="user"
# 说明：这是可提交到 Git 的首次默认密码，只用于第一次生成账号；登录后仍会提示用户改密。
DEFAULT_HOSTPC_PASSWORD="Lain0706"

write_env_file() {
  ensure_runtime_dirs
  if [[ -f "${AMSEOKBOT_ENV_FILE}" ]]; then
    log "环境文件已存在：${AMSEOKBOT_ENV_FILE}"
    return
  fi

  local tmp
  tmp="$(make_temp_file)"
  cat >"${tmp}" <<EOF_ENV
# 作用：AmseokBot-Milo 本机运行配置。HOSTPC_PASSWORD 为仓库默认首次密码，首次登录后请修改。
AMSEOKBOT_REPO_DIR=${AMSEOKBOT_REPO_DIR}
AMSEOKBOT_MASTER_DIR=${AMSEOKBOT_MASTER_DIR}
AMSEOKBOT_API_ADDR=0.0.0.0:8080
AMSEOKBOT_CONFIG_DIR=/etc/amseokbot
AMSEOKBOT_DATA_DIR=/var/lib/amseokbot
AMSEOKBOT_LOG_DIR=/var/log/amseokbot
HOSTPC_USER=${DEFAULT_HOSTPC_USER}
HOSTPC_PASSWORD=${DEFAULT_HOSTPC_PASSWORD}
EOF_ENV
  as_root install -m 0600 "${tmp}" "${AMSEOKBOT_ENV_FILE}"
  rm -f "${tmp}"
  log "已生成环境文件：${AMSEOKBOT_ENV_FILE}"
  log "首次登录账号为 ${DEFAULT_HOSTPC_USER}；默认密码为 ${DEFAULT_HOSTPC_PASSWORD}，登录后请立即修改。"
}

# ==================== 安装入口 ====================
# 作用：串联依赖安装、配置生成和项目构建。
# ================================================
main() {
  require_repo
  install_apt_packages
  install_pnpm
  write_env_file
  "${SCRIPT_DIR}/build.sh"
  log "安装完成。启动服务：MasterComputer/scripts/start.sh；注册开机自启：MasterComputer/scripts/service-install.sh"
}

main
