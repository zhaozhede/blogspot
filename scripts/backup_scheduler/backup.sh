#!/usr/bin/env sh
# 增量备份脚本（供 backup_scheduler 调用）：将 BLOG_DATA_DIR 同步到家目录、U 盘等。
# 本脚本位于 scripts/backup_scheduler/，故 PROJECT_ROOT 为上级的上级。

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"

DATA_DIR="${BLOG_DATA_DIR:-${PROJECT_ROOT}/data}"

if [ ! -d "${DATA_DIR}" ]; then
  echo "数据目录不存在：${DATA_DIR}" >&2
  exit 1
fi

echo "源数据目录：${DATA_DIR}"

HOME_BACKUP_DIR="${BLOG_BACKUP_HOME:-${HOME}/blog_backup}"
USB_BACKUP_DIR="${BLOG_BACKUP_USB:-/mnt/usb1/blog_backup}"

backup_one() {
  src="$1"
  dest="$2"
  label="$3"
  if ! mkdir -p "${dest}" 2>/dev/null; then
    echo "跳过 ${label}（无法创建目录：${dest}）" >&2
    return
  fi
  if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete "${src}/" "${dest}/"
  else
    cp -au "${src}/." "${dest}/"
  fi
  echo "已完成 ${label} 备份：${dest}"
}

backup_one "${DATA_DIR}" "${HOME_BACKUP_DIR}" "家目录"
backup_one "${DATA_DIR}" "${USB_BACKUP_DIR}" "U 盘"
echo "全部备份完成。"
