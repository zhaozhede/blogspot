/**
 * backup_scheduler — 定时备份调度
 *
 * 由 crontab 每天调用一次。根据 data/last_blog_change 与当前档位决定今日是否执行备份，
 * 并更新档位：连续 3 天无变动 -> 每周；连续 3 周无变动 -> 每月；一旦有变动 -> 恢复每日。
 *
 * 编译：make -C scripts/backup_scheduler
 * 运行：BLOG_DATA_DIR=./data BLOG_PROJECT_ROOT=/path/to/blogpost ./backup_scheduler
 */

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>

enum { SCHED_DAILY = 0, SCHED_WEEKLY = 1, SCHED_MONTHLY = 2 };

static const char *data_dir;
static const char *project_root;
static char state_path[1024];
static char change_path[1024];
static char backup_sh[1024];

/* 从 state 文件读档位与上次运行日期，返回 0 成功 */
static int read_state(int *schedule_out, time_t *last_run_out) {
    *schedule_out = SCHED_DAILY;
    *last_run_out = 0;
    FILE *f = fopen(state_path, "r");
    if (!f) return 0;
    char line[64];
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (strcmp(line, "weekly") == 0) *schedule_out = SCHED_WEEKLY;
        else if (strcmp(line, "monthly") == 0) *schedule_out = SCHED_MONTHLY;
    }
    if (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0] != '0' || line[1] != '\0') {
            int y, m, d;
            if (sscanf(line, "%d-%d-%d", &y, &m, &d) == 3) {
                struct tm t = {0};
                t.tm_year = y - 1900;
                t.tm_mon = m - 1;
                t.tm_mday = d;
                *last_run_out = mktime(&t);
            }
        }
    }
    fclose(f);
    return 0;
}

static int write_state(int schedule, time_t last_run) {
    FILE *f = fopen(state_path, "w");
    if (!f) return -1;
    const char *s = schedule == SCHED_WEEKLY ? "weekly" : (schedule == SCHED_MONTHLY ? "monthly" : "daily");
    fprintf(f, "%s\n", s);
    if (last_run == 0) {
        fprintf(f, "0\n");
    } else {
        struct tm *tm = localtime(&last_run);
        if (tm)
            fprintf(f, "%04d-%02d-%02d\n", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
        else
            fprintf(f, "0\n");
    }
    fclose(f);
    return 0;
}

/* 取 last_blog_change 文件的 mtime，不存在返回 0 */
static time_t get_last_change_time(void) {
    struct stat st;
    if (stat(change_path, &st) != 0) return 0;
    return st.st_mtime;
}

/* 今日 0 点时间戳（本地） */
static time_t today_start(void) {
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    if (!tm) return t;
    tm->tm_hour = 0;
    tm->tm_min = 0;
    tm->tm_sec = 0;
    return mktime(tm);
}

/* 根据档位与上次运行时间，判断今天是否应执行备份 */
static int should_run_today(int schedule, time_t last_run) {
    time_t today = today_start();
    if (last_run == 0) return 1;
    double days = (double)(today - last_run) / 86400.0;
    if (schedule == SCHED_DAILY) return 1;
    if (schedule == SCHED_WEEKLY) return days >= 7.0;
    if (schedule == SCHED_MONTHLY) return days >= 28.0;
    return 1;
}

/* 执行 backup.sh */
static int run_backup(void) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "BLOG_DATA_DIR=%s sh %s", data_dir, backup_sh);
    int r = system(cmd);
    return (r == -1 || (WIFEXITED(r) && WEXITSTATUS(r) != 0)) ? -1 : 0;
}

/* 根据 last_change 与 last_run 决定下一档位 */
static int next_schedule(time_t last_change, time_t last_run) {
    time_t now = time(NULL);
    double change_days_ago = last_change == 0 ? 1e9 : (double)(now - last_change) / 86400.0;
    int had_change_after_run = (last_change > 0 && last_run > 0 && last_change > last_run);

    if (had_change_after_run) return SCHED_DAILY;
    if (change_days_ago <= 3.0) return SCHED_DAILY;
    if (change_days_ago <= 21.0) return SCHED_WEEKLY;
    return SCHED_MONTHLY;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    data_dir = getenv("BLOG_DATA_DIR");
    if (!data_dir || !data_dir[0]) data_dir = "data";
    project_root = getenv("BLOG_PROJECT_ROOT");
    if (!project_root || !project_root[0]) {
        static char derived_root[PATH_MAX];
        char exe[PATH_MAX];
        ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
        if (n > 0) {
            exe[n] = '\0';
            char *s = strrchr(exe, '/');
            if (s) { *s = '\0'; s = strrchr(exe, '/'); }
            if (s) { *s = '\0'; s = strrchr(exe, '/'); }
            if (s) {
                *s = '\0';
                if (realpath(exe, derived_root))
                    project_root = derived_root;
            }
        }
        if (!project_root || !project_root[0]) project_root = ".";
    }

    snprintf(state_path, sizeof(state_path), "%s/schedule_state", data_dir);
    snprintf(change_path, sizeof(change_path), "%s/last_blog_change", data_dir);
    snprintf(backup_sh, sizeof(backup_sh), "%s/scripts/backup_scheduler/backup.sh", project_root);

    time_t last_change = get_last_change_time();
    int schedule;
    time_t last_run;
    read_state(&schedule, &last_run);

    time_t today = today_start();
    int run_today = should_run_today(schedule, last_run);

    if (run_today) {
        if (run_backup() != 0)
            fprintf(stderr, "backup_scheduler: 备份脚本执行失败\n");
        else
            last_run = today;
    }

    schedule = next_schedule(last_change, last_run);
    write_state(schedule, last_run);
    return 0;
}
