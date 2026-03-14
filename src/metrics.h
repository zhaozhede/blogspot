/**
 * metrics.h — 服务器指标与访问统计缓存
 *
 * - server_metrics_t：首页「服务器信息」卡片所需字段（启动时间、内存、磁盘、CPU、今日访问）
 * - get_server_metrics：填充上述字段（依赖 metrics_set_server_start_time 已调用）
 * - 访问人次缓存：按页面计次后由 dispatch 更新，供各 handler 页脚展示
 */
#ifndef BLOG_METRICS_H
#define BLOG_METRICS_H

#include <time.h>

typedef struct {
    char boot_time[64];
    char mem_usage[64];
    char disk_usage[64];
    char cpu_temp[96];
    char last_update[64];
    char visits_today_str[32];
} server_metrics_t;

/** 设置服务器启动时间 t，供「已运行 X 天」计算。 */
void metrics_set_server_start_time(time_t t);
/** 返回已设置的启动时间，未设置时为 0。 */
time_t metrics_get_server_start_time(void);
/** 设置访问人次缓存（与数据库同步）。total=全站累积，today=今日。 */
void metrics_set_visits(long long total, long long today);
/** 返回缓存的累积访问人次。 */
long long metrics_get_total_visits(void);
/** 返回缓存的今日访问人次。 */
long long metrics_get_visits_today(void);

/** 填充 m：启动时间、内存、磁盘、CPU 温度、今日访问、最后更新时间。m 可为 NULL。 */
void get_server_metrics(server_metrics_t *m);

#endif /* BLOG_METRICS_H */
