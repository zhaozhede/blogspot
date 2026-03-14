# 备份调度器：编译 backup_scheduler 可执行文件
# 可由项目根目录 Makefile 通过 make backup-scheduler 或 make -C scripts/backup_scheduler 调用

CC := gcc
CFLAGS := -Wall -Wextra -O2 -std=c11
TARGET := backup_scheduler

# 项目根目录（当前目录的上级的上级），用于生成 crontab 绝对路径
PROJECT_ROOT := $(abspath $(CURDIR)/../..)
SCHEDULER_BIN := $(PROJECT_ROOT)/scripts/backup_scheduler/$(TARGET)
DATA_DIR := $(PROJECT_ROOT)/data

.PHONY: all clean install-crontab

all: $(TARGET)

$(TARGET): main.c
	$(CC) $(CFLAGS) -o $(TARGET) main.c

clean:
	rm -f $(TARGET)

# 安装 crontab：每天 2:00 运行备份调度器（先删除已有同条再追加，避免重复）
# 请勿使用 sudo，否则会装到 root 的 crontab，当前用户 crontab -l 看不到
install-crontab: $(TARGET)
	@echo "项目根目录: $(PROJECT_ROOT)"
	@echo "将添加: 0 2 * * * BLOG_DATA_DIR=$(DATA_DIR) BLOG_PROJECT_ROOT=$(PROJECT_ROOT) $(SCHEDULER_BIN)"
	@(crontab -l 2>/dev/null | grep -v 'backup_scheduler' || true; \
	  echo "0 2 * * * BLOG_DATA_DIR=$(DATA_DIR) BLOG_PROJECT_ROOT=$(PROJECT_ROOT) $(SCHEDULER_BIN)") | crontab -
	@echo "crontab 已更新（当前用户）。查看: crontab -l"
	@crontab -l
