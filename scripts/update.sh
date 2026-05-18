#!/usr/bin/env bash
# 作用：从 Git 远端检测并拉取新代码，重新构建后快速重启本机 AmseokBot-Milo。

set -Eeuo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "${SCRIPT_DIR}/lib/common.sh"

FORCE=0
for arg in "$@"; do
  case "${arg}" in
    --force) FORCE=1 ;;
    *) die "未知参数：${arg}" ;;
  esac
done

# ==================== 远端更新检测 ====================
# 作用：比较本地 HEAD 与远端分支，只有有更新时才拉取。
# ======================================================
update_from_git() {
  cd "${AMSEOKBOT_REPO_DIR}"
  have_cmd git || die "缺少 git"
  local branch local_sha remote_sha
  branch="$(git rev-parse --abbrev-ref HEAD)"
  [[ "${branch}" != "HEAD" ]] || die "当前不在普通分支上，无法自动更新"

  if [[ -n "$(git status --porcelain)" && "${FORCE}" -ne 1 ]]; then
    die "工作区有未提交修改。确认要覆盖风险时使用 scripts/update.sh --force"
  fi

  log "检测远端更新：origin/${branch}"
  git fetch origin "${branch}"
  local_sha="$(git rev-parse HEAD)"
  remote_sha="$(git rev-parse "origin/${branch}")"
  if [[ "${local_sha}" == "${remote_sha}" ]]; then
    log "当前已经是最新版本：${local_sha:0:8}"
    return
  fi

  if [[ "${FORCE}" -eq 1 ]]; then
    log "强制同步到远端版本：${remote_sha:0:8}"
    git reset --hard "origin/${branch}"
  else
    log "拉取远端版本：${remote_sha:0:8}"
    git pull --ff-only origin "${branch}"
  fi
}

# ==================== 快速重启 ====================
# 作用：重建产物并重启后台服务；systemd 模式下重启 systemd 服务。
# ==================================================
restart_service() {
  "${SCRIPT_DIR}/build.sh"
  if have_cmd systemctl && systemctl list-unit-files amseokbot-milo.service >/dev/null 2>&1; then
    log "重启 systemd 服务：amseokbot-milo.service"
    as_root systemctl restart amseokbot-milo.service
    return
  fi
  "${SCRIPT_DIR}/stop.sh" || true
  "${SCRIPT_DIR}/start.sh"
}

main() {
  require_repo
  ensure_runtime_dirs
  update_from_git
  restart_service
  log "更新完成"
}

main
