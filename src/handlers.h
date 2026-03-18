/**
 * handlers.h — 各 handler 与 send_* 页面函数声明
 *
 * 供 dispatch.c 调用：首页/博客/静态/登录注册/后台/用户评论。
 */
#ifndef BLOG_HANDLERS_H
#define BLOG_HANDLERS_H

#include "conn.h"
#include <sqlite3.h>

/* 首页 landing：根路径 / 或 /index，随机台词+背景图+服务器指标 */
void handle_landing(conn_ctx_t *ctx, sqlite3 *db);

/* 博客列表与文章详情：ctx, db, headers/query 用于 session 与分页 */
void handle_index(conn_ctx_t *ctx, sqlite3 *db, const char *headers, const char *query);
void handle_post_detail(conn_ctx_t *ctx, sqlite3 *db, const char *query, const char *headers);

/* 静态与关于页 */
void send_about_page(conn_ctx_t *ctx);
void send_static_file(conn_ctx_t *ctx, const char *path);
void send_static_favicon(conn_ctx_t *ctx);
void send_static_jsencrypt(conn_ctx_t *ctx);
/** 将 jsencrypt 内容内联发送（用于登录/注册页，避免二次请求导致加密库加载失败） */
void send_jsencrypt_inline(conn_ctx_t *ctx);

/* 登录/注册页（后台与前台统一登录）：show_error 是否显示错误，redir/post_id 为可选参数 */
void send_login_page(conn_ctx_t *ctx, int show_error);
void send_admin_redirect_to_login(conn_ctx_t *ctx);
void send_unified_login_page(conn_ctx_t *ctx, int show_error, const char *redir);
void send_register_page(conn_ctx_t *ctx, int show_error, int post_id, const char *redir);

/* 后台管理：query 含 page 或 id，body 为表单 body */
void handle_admin_index(conn_ctx_t *ctx, sqlite3 *db, const char *query);
void handle_admin_new_post(conn_ctx_t *ctx);
void handle_admin_edit_post(conn_ctx_t *ctx, sqlite3 *db, const char *query);
void handle_admin_post_create(conn_ctx_t *ctx, sqlite3 *db, const char *body);
void handle_admin_post_update(conn_ctx_t *ctx, sqlite3 *db, const char *body);
void handle_admin_post_delete(conn_ctx_t *ctx, sqlite3 *db, const char *body);
void handle_admin_comment_delete(conn_ctx_t *ctx, sqlite3 *db, const char *body);

/* 用户注册/登录/评论：body 为表单，client_ip/headers 用于评论与 session */
void handle_user_register(conn_ctx_t *ctx, sqlite3 *user_db, const char *body);
void handle_unified_login(conn_ctx_t *ctx, sqlite3 *user_db, const char *body);
void handle_user_login(conn_ctx_t *ctx, sqlite3 *user_db, const char *body, const char *headers);
void handle_comment_post(conn_ctx_t *ctx, sqlite3 *db, const char *body, const char *client_ip, const char *headers);

#endif /* BLOG_HANDLERS_H */
