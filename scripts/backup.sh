#!/usr/bin/env sh
# 简单增量备份脚本：将 BLOG_DATA_DIR 下的所有 .db / 密钥等文件
# 同步到 EMMC、家目录、U 盘三个位置。依赖 rsync（如无 rsync，可改为 cp -au）。

set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# 源数据目录（文章 / 评论 / 用户账号等）
DATA_DIR="${BLOG_DATA_DIR:-${PROJECT_ROOT}/data}"

if [ ! -d "${DATA_DIR}" ]; then
  echo "数据目录不存在：${DATA_DIR}" >&2
  exit 1
fi

echo "源数据目录：${DATA_DIR}"

# 目标备份目录（可通过环境变量覆盖）
# 1. 程序所在 data 目录本身已经是主数据存放处
# 2. 家目录备份：默认 /home/ubuntu/blog_backup（若 \$HOME 不是 ubuntu，可通过 BLOG_BACKUP_HOME 覆盖）
HOME_BACKUP_DIR="${BLOG_BACKUP_HOME:-${HOME}/blog_backup}"
# 3. U 盘备份：默认 /mnt/usb1/blog_backup（可用 BLOG_BACKUP_USB 指定实际挂载点）
USB_BACKUP_DIR="${BLOG_BACKUP_USB:-/mnt/usb1/blog_backup}"

backup_one() {
  src="$1"
  dest="$2"
  label="$3"

  # 目标可能不存在，自动创建
  if ! mkdir -p "${dest}" 2>/dev/null; then
    echo "跳过 ${label}（无法创建目录：${dest}）" >&2
    return
  fi

  if command -v rsync >/dev/null 2>&1; then
    # rsync 仅同步变更文件，等价于增量备份
    rsync -a --delete "${src}/" "${dest}/"
  else
    # 回退方案：尽量保持增量语义（不覆盖较新的文件）
    cp -au "${src}/." "${dest}/"
  fi

  echo "已完成 ${label} 备份：${dest}"
}

backup_one "${DATA_DIR}" "${HOME_BACKUP_DIR}" "家目录"
backup_one "${DATA_DIR}" "${USB_BACKUP_DIR}" "U 盘"

echo "全部备份完成。"

