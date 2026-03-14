/**
 * dispatch.h — 请求分发
 *
 * handle_client：解析 METHOD/PATH/body，按路径调用各 handler，并做页面访问统计。
 */
#ifndef BLOG_DISPATCH_H
#define BLOG_DISPATCH_H

#include "conn.h"
#include <sqlite3.h>

/**
 * 解析 ctx 读缓冲中的请求，记录 PV（若为页面访问），按路径分发到 landing/blog/admin/login/comment 等。
 * @param ctx      连接上下文
 * @param db       博客库
 * @param user_db  用户库
 * @param client_ip 客户端 IP 字符串（用于评论等）
 */
void handle_client(conn_ctx_t *ctx, sqlite3 *db, sqlite3 *user_db, const char *client_ip);

#endif /* BLOG_DISPATCH_H */
