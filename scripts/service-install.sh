#!/usr/bin/env bash
# 作用：把 AmseokBot-Milo 注册为 systemd 服务，实现开机自启和崩溃自动重启。

set -Eeuo pipefail

SCRIPT_DIR="$(cd -P "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
[[ -f "${SCRIPT_DIR}/lib/common.sh" ]] || { printf '[AmseokBot][ERROR] 缺少脚本库：%s\n' "${SCRIPT_DIR}/lib/common.sh" >&2; exit 1; }
source "${SCRIPT_DIR}/lib/common.sh"

SERVICE_FILE="/etc/systemd/system/amseokbot-milo.service"

# ==================== systemd 单元生成 ====================
# 作用：使用当前仓库路径和当前用户生成服务文件。
# ========================================================
write_service_file() {
  have_cmd systemctl || die "当前系统没有 systemd"
  ensure_runtime_dirs
  local tmp
  tmp="$(make_temp_file)"
  cat >"${tmp}" <<EOF_SERVICE
[Unit]
Description=AmseokBot-Milo local robot software
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
User=$(id -un)
Group=$(id -gn)
WorkingDirectory=${AMSEOKBOT_REPO_DIR}
EnvironmentFile=-${AMSEOKBOT_ENV_FILE}
ExecStart=/usr/bin/env bash ${AMSEOKBOT_REPO_DIR}/scripts/start.sh --foreground
Restart=on-failure
RestartSec=3
UMask=0027

[Install]
WantedBy=multi-user.target
EOF_SERVICE
  as_root install -m 0644 "${tmp}" "${SERVICE_FILE}"
  rm -f "${tmp}"
}

# ==================== 服务启用 ====================
# 作用：重载 systemd，启用并立即启动 Milo 服务。
# ================================================
main() {
  require_repo
  [[ -f "${AMSEOKBOT_ENV_FILE}" ]] || "${SCRIPT_DIR}/install.sh" --no-apt
  write_service_file
  as_root systemctl daemon-reload
  as_root systemctl enable --now amseokbot-milo.service
  log "systemd 服务已启用：amseokbot-milo.service"
  log "查看状态：systemctl status amseokbot-milo.service"
}

main
