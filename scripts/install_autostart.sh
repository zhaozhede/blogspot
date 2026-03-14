#!/usr/bin/env sh
# 在支持 systemd 的 Linux 上为 blog 服务创建自启动（systemd service）。
# 需要以 root 身份运行：
#   sudo scripts/install_autostart.sh

set -eu

if ! command -v systemctl >/dev/null 2>&1; then
  echo "当前系统不支持 systemd（未找到 systemctl），无法自动注册服务。" >&2
  exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

BLOG_BIN="${PROJECT_ROOT}/blog"

if [ ! -x "${BLOG_BIN}" ]; then
  echo "未找到可执行文件：${BLOG_BIN}，请先在此目录执行 make 生成 blog。" >&2
  exit 1
fi

# 默认数据目录可根据需要修改为 EMMC 上的固定路径
DATA_DIR_DEFAULT="${BLOG_DATA_DIR:-${PROJECT_ROOT}/data}"

SERVICE_NAME="blog.service"
UNIT_PATH="/etc/systemd/system/${SERVICE_NAME}"

echo "即将在 ${UNIT_PATH} 写入 systemd 服务单元，使用："
echo "  ExecStart=${BLOG_BIN}"
echo "  WorkingDirectory=${PROJECT_ROOT}"
echo "  BLOG_DATA_DIR=${DATA_DIR_DEFAULT}"

cat > "${UNIT_PATH}" <<EOF
[Unit]
Description=Personal blog HTTP server (blogpost)
After=network.target

[Service]
Type=simple
WorkingDirectory=${PROJECT_ROOT}
ExecStart=${BLOG_BIN}
Restart=on-failure
Environment=BLOG_DATA_DIR=${DATA_DIR_DEFAULT}

[Install]
WantedBy=multi-user.target
EOF

echo "已写入 ${UNIT_PATH}"

systemctl daemon-reload
systemctl enable --now "${SERVICE_NAME}"

echo "已启用并启动服务：${SERVICE_NAME}"
echo "查看状态：systemctl status ${SERVICE_NAME}"

