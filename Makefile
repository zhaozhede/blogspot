# 编译器与选项
CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11
LDFLAGS := -lsqlite3 -lcrypto

SRC_DIR := src
OBJ_DIR := build

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SRCS))

TARGET := blog

.PHONY: all clean dirs quotes backup backup-scheduler install-autostart install-backup-crontab help restart

# 显示可用目标说明
help:
	@echo "用法: make [目标]"
	@echo ""
	@echo "目标:"
	@echo "  all              默认：清理、编译并生成 blog 可执行文件"
	@echo "  clean            删除 build/ 与 blog"
	@echo "  backup           运行增量备份脚本（同步 data/ 到家目录、U 盘等）"
	@echo "  backup-scheduler 编译定时备份调度器（scripts/backup_scheduler/）"
	@echo "  install-backup-crontab 安装备份调度器的 crontab（每天 2:00；勿用 sudo）"
	@echo "  install-autostart 安装 systemd 自启动服务（需 root：sudo make install-autostart）"
	@echo "  restart          编译并重启 blog 系统服务（make && sudo systemctl restart blog）"
	@echo "  help             显示本帮助"

# 默认目标：清理后完整编译并生成可执行文件 blog
all: clean dirs quotes $(TARGET)

# 创建 build、data、static 等目录
dirs:
	mkdir -p $(OBJ_DIR)
	mkdir -p data
	mkdir -p static

# 根据 static/landing-quotes.json 生成 src/landing_quotes_embed.h（首页台词 C 数组）
quotes:
	python3 tools/gen_landing_quotes_h.py

# 链接生成最终可执行文件
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 将每个 .c 编译成 build 下的 .o
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# main.c 包含 landing_quotes_embed.h，JSON 变更后需重编 main.o
$(OBJ_DIR)/main.o: $(SRC_DIR)/landing_quotes_embed.h

# 删除 build 目录与 blog 可执行文件
clean:
	rm -rf $(OBJ_DIR) $(TARGET)

# 运行增量备份脚本：将 BLOG_DATA_DIR（默认 data/）同步到
# - /mnt/emmc/blog_backup（或 BLOG_BACKUP_EMMC）
# - \$HOME/blog_backup（或 BLOG_BACKUP_HOME）
# - /mnt/usb1/blog_backup（或 BLOG_BACKUP_USB）
backup:
	sh scripts/backup.sh

# 编译定时备份调度器（scripts/backup_scheduler/），供 crontab 每日调用
# 先 clean 再编译，避免在 ARM 上误用 x64 残留产物
backup-scheduler:
	$(MAKE) -C scripts/backup_scheduler clean
	$(MAKE) -C scripts/backup_scheduler

# 安装备份调度器 crontab（需先 make backup-scheduler；会追加每天 2:00 任务）
install-backup-crontab:
	$(MAKE) -C scripts/backup_scheduler install-crontab

# 安装 systemd 自启动服务（需要在目标设备上以 root 运行）
install-autostart:
	sh scripts/install_autostart.sh

# 编译并重启 blog 系统服务（先 make 再 systemctl restart blog）
restart: all
	sudo systemctl restart blog
