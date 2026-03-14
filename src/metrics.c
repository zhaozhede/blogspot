/**
 * metrics.c — 服务器指标与访问统计实现
 *
 * 启动时间、/proc/meminfo、statvfs、/proc/msp/pm_cpu（Hi3798）、今日访问数。
 */
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <sys/statvfs.h>
#include <time.h>

static time_t g_server_start_time = 0;
static long long g_total_visits = 0;
static long long g_visits_today = 0;

/**
 * metrics_set_server_start_time — 设置服务器启动时间
 *
 * 功能：供 main 在 listen 成功后调用，用于首页/关于页「已运行 X 天」计算。
 *
 * @param t Unix 时间戳
 * 返回值：无
 */
void metrics_set_server_start_time(time_t t) {
    g_server_start_time = t;
}

/**
 * metrics_get_server_start_time — 获取服务器启动时间
 *
 * @return 启动时的 Unix 时间戳，未设置时为 0
 */
time_t metrics_get_server_start_time(void) {
    return g_server_start_time;
}

/**
 * metrics_set_visits — 设置访问人次缓存（与数据库同步）
 *
 * 功能：dispatch 在记录 PV 后从 db 读取并调用，供各 handler 页脚展示。
 *
 * @param total 全站累积访问人次
 * @param today 今日访问人次
 * 返回值：无
 */
void metrics_set_visits(long long total, long long today) {
    g_total_visits = total;
    g_visits_today = today;
}

/**
 * metrics_get_total_visits — 获取累积访问人次
 *
 * @return 当前缓存的 total_visits
 */
long long metrics_get_total_visits(void) {
    return g_total_visits;
}

/**
 * metrics_get_visits_today — 获取今日访问人次
 *
 * @return 当前缓存的 visits_today
 */
long long metrics_get_visits_today(void) {
    return g_visits_today;
}

/* ---------------------------------------------------------------------------
 * get_server_metrics — 填充 server_metrics_t：启动时间、内存、磁盘、CPU、今日访问
 *
 * 功能：从 /proc/meminfo、statvfs("/")、/proc/msp/pm_cpu（Hi3798）等读取并格式化。
 *
 * @param m 输出结构体，各字段会被写入；可为 NULL（不操作）
 * 返回值：无
 * --------------------------------------------------------------------------- */
void get_server_metrics(server_metrics_t *m) {
    if (!m) return;
    memset(m, 0, sizeof(*m));
    time_t now = time(NULL);

    /* 服务器启动时间 */
    struct tm *tm_ptr = localtime(&g_server_start_time);
    if (tm_ptr)
        strftime(m->boot_time, sizeof(m->boot_time), "%Y-%m-%d %H:%M:%S", tm_ptr);
    else
        snprintf(m->boot_time, sizeof(m->boot_time), "未知");

    /* 最后更新时间 */
    tm_ptr = localtime(&now);
    if (tm_ptr)
        strftime(m->last_update, sizeof(m->last_update), "%Y-%m-%d %H:%M:%S", tm_ptr);
    else
        snprintf(m->last_update, sizeof(m->last_update), "未知");

    /* 内存：/proc/meminfo MemTotal / MemAvailable */
    {
        FILE *f = fopen("/proc/meminfo", "r");
        long total_kb = 0, avail_kb = 0;
        if (f) {
            char key[64], unit[32];
            long value;
            while (fscanf(f, "%63s %ld %31s", key, &value, unit) == 3) {
                if (strcmp(key, "MemTotal:") == 0) total_kb = value;
                else if (strcmp(key, "MemAvailable:") == 0) avail_kb = value;
                if (total_kb > 0 && avail_kb > 0) break;
            }
            fclose(f);
        }
        if (total_kb > 0 && avail_kb >= 0 && avail_kb <= total_kb) {
            long used_kb = total_kb - avail_kb;
            double used_pct = (double)used_kb * 100.0 / (double)total_kb;
            double total_gb = (double)total_kb / (1024.0 * 1024.0);
            double used_gb = (double)used_kb / (1024.0 * 1024.0);
            snprintf(m->mem_usage, sizeof(m->mem_usage),
                     "%.1f GB / %.1f GB (%.0f%%)", used_gb, total_gb, used_pct);
        } else {
            snprintf(m->mem_usage, sizeof(m->mem_usage), "未知");
        }
    }

    /* 磁盘：statvfs 根分区 */
    {
        struct statvfs vfs;
        if (statvfs("/", &vfs) == 0 && vfs.f_blocks > 0) {
            unsigned long long total = (unsigned long long)vfs.f_blocks * vfs.f_frsize;
            unsigned long long avail = (unsigned long long)vfs.f_bavail * vfs.f_frsize;
            unsigned long long used = total - avail;
            double used_pct = (double)used * 100.0 / (double)total;
            double total_gb = (double)total / (1024.0 * 1024.0 * 1024.0);
            double used_gb = (double)used / (1024.0 * 1024.0 * 1024.0);
            snprintf(m->disk_usage, sizeof(m->disk_usage),
                     "%.1f GB / %.1f GB (%.0f%%)", used_gb, total_gb, used_pct);
        } else {
            snprintf(m->disk_usage, sizeof(m->disk_usage), "未知");
        }
    }

    /* CPU 温度：/proc/msp/pm_cpu（Hi3798）或未知 */
    {
        FILE *f = fopen("/proc/msp/pm_cpu", "r");
        if (f) {
            char line[128];
            long temp_deg = -1;
            while (fgets(line, sizeof(line), f)) {
                long v = 0;
                if (sscanf(line, "Tsensor: temperature = %ld degree", &v) == 1) {
                    temp_deg = v;
                    break;
                }
            }
            fclose(f);
            if (temp_deg >= 0)
                snprintf(m->cpu_temp, sizeof(m->cpu_temp), "%ld ℃", temp_deg);
            else
                snprintf(m->cpu_temp, sizeof(m->cpu_temp), "未知");
        } else {
            snprintf(m->cpu_temp, sizeof(m->cpu_temp), "未知");
        }
    }

    snprintf(m->visits_today_str, sizeof(m->visits_today_str), "%lld", (long long)g_visits_today);
}
