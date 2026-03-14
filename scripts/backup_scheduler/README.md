# 备份调度器（backup_scheduler）

根据博文是否变动，自动在「每日 / 每周 / 每月」三种备份频率间切换，由 crontab 每天调用一次即可。

## 逻辑

- **默认**：每天执行一次备份（由 cron 每天调用本程序一次）。
- **连续 3 天博文无变动**：自动改为**每周**执行一次备份。
- **连续 3 周博文无变动**：自动改为**每月**执行一次备份。
- **一旦有变动**（后台新建/编辑/删除文章）：下次调度时恢复为**每日**备份。

博文变动由主站后台在保存或删除文章时写入 `data/last_blog_change` 的时间戳（或 mtime）判定；调度状态保存在 `data/schedule_state`。

## 编译

在项目根目录执行：

```bash
make backup-scheduler
```

或在当前目录：

```bash
make -C scripts/backup_scheduler
```

生成可执行文件 `scripts/backup_scheduler/backup_scheduler`。

## 安装 crontab（每天 2:00 运行）

在项目根目录（需先编译过 backup_scheduler）：

```bash
make install-backup-crontab
```

或在 scripts/backup_scheduler 目录下：

```bash
make install-crontab
```

会自动把「每天 2:00 运行备份调度器」加入当前用户的 crontab；若已有同一条会先删再添，避免重复。路径按当前项目根目录自动生成。

## 运行

建议通过 crontab 每天固定时间（如凌晨 2 点）运行一次。需设置环境变量（或保证当前工作目录为项目根目录且使用默认 `data`）：

- **BLOG_DATA_DIR**：数据目录，内含 `last_blog_change`、`schedule_state`，默认 `data`。
- **BLOG_PROJECT_ROOT**：项目根目录绝对路径，用于定位 `scripts/backup_scheduler/backup.sh`。不设时尝试从可执行路径推导。

示例（项目根为 `/home/ubuntu/blogpost`）：

```bash
BLOG_DATA_DIR=/home/ubuntu/blogpost/data \
BLOG_PROJECT_ROOT=/home/ubuntu/blogpost \
/home/ubuntu/blogpost/scripts/backup_scheduler/backup_scheduler
```

Crontab 示例（每天 2:00 执行）：

```cron
0 2 * * * BLOG_DATA_DIR=/home/ubuntu/blogpost/data BLOG_PROJECT_ROOT=/home/ubuntu/blogpost /home/ubuntu/blogpost/scripts/backup_scheduler/backup_scheduler
```

若已 `cd` 到项目根目录再执行，可简化为：

```cron
0 2 * * * cd /home/ubuntu/blogpost && BLOG_PROJECT_ROOT=/home/ubuntu/blogpost ./scripts/backup_scheduler/backup_scheduler
```

## 文件说明

| 文件 | 说明 |
|------|------|
| `main.c` | 调度逻辑：读 state / last_blog_change，决定是否执行备份并更新档位。 |
| `Makefile` | 编译 backup_scheduler，供外层 `make backup-scheduler` 调用。 |
| `backup.sh` | 实际执行增量备份的脚本（同步到家目录、U 盘等）。 |
| `README.md` | 本说明。 |

备份目标路径与主项目 `scripts/backup.sh` 一致，可通过环境变量 `BLOG_BACKUP_HOME`、`BLOG_BACKUP_USB` 等覆盖。
