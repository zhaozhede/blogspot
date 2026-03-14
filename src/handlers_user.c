/**
 * handlers_user.c — 前台用户注册 / 登录 / 评论
 *
 * - handle_user_register：注册并自动登录
 * - handle_unified_login：统一登录（先管理员，再普通用户）
 * - handle_user_login：JSON 接口登录（用于前端交互）
 * - handle_comment_post：发表评论 / 回复
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "conn.h"
#include "http.h"
#include "auth.h"
#include "user_db.h"
#include "db.h"
#include "handlers.h"

/**
 * handle_user_register — 用户注册 POST /blog/user/register
 *
 * 功能：从 body 解析 encrypted（RSA 加密的 用户名:密码）、post_id；解密后 user_register，
 *       成功则创建 user_session 并重定向到文章页或 /blog；失败则带 register_error 重定向。
 *
 * @param ctx     连接上下文
 * @param user_db 用户库
 * @param body    表单 body（encrypted, post_id）
 * 返回值：无
 */
void handle_user_register(conn_ctx_t *ctx, sqlite3 *user_db, const char *body) {
    if (!body || !user_db) {
        send_redirect(ctx, "/?register_error=1");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_redirect(ctx, "/?register_error=2");
        return;
    }
    memcpy(copy, body, len + 1);
    char *encrypted = query_get(copy, "encrypted");
    char *post_id_str = query_get(copy, "post_id");
    free(copy);
    if (!encrypted || !encrypted[0]) {
        free(encrypted);
        free(post_id_str);
        send_redirect(ctx, "/?register_error=1");
        return;
    }
    for (char *p = encrypted; *p; p++) if (*p == ' ') *p = '+';
    char uname[128];
    char pwd[256];
    if (!auth_decrypt_credentials(encrypted, uname, sizeof(uname), pwd, sizeof(pwd))) {
        free(encrypted);
        free(post_id_str);
        send_redirect(ctx, "/?register_error=1");
        return;
    }
    free(encrypted);
    if (uname[0] == '\0' || pwd[0] == '\0') {
        free(post_id_str);
        send_redirect(ctx, "/?register_error=1");
        return;
    }
    int rc = user_register(user_db, uname, pwd);
    char loc[256];
    int pid = (post_id_str && post_id_str[0]) ? atoi(post_id_str) : 0;
    if (post_id_str) free(post_id_str);
    if (pid > 0)
        snprintf(loc, sizeof(loc), "/blog/post?id=%d", pid);
    else
        snprintf(loc, sizeof(loc), "/blog");
    if (rc == -1) {
        snprintf(loc + strlen(loc), sizeof(loc) - (size_t)strlen(loc), "%sregister_error=1", pid > 0 ? "&" : "?");
        send_redirect(ctx, loc);
        return;
    }
    if (rc != 0) {
        snprintf(loc + strlen(loc), sizeof(loc) - (size_t)strlen(loc), "%sregister_error=2", pid > 0 ? "&" : "?");
        send_redirect(ctx, loc);
        return;
    }
    int user_id = 0;
    if (user_verify(user_db, uname, pwd, &user_id) != 1) {
        send_redirect(ctx, loc);
        return;
    }
    char new_sid[65];
    if (!auth_user_session_create(user_id, uname, new_sid, sizeof(new_sid))) {
        send_redirect(ctx, loc);
        return;
    }
    send_redirect_with_user_cookie(ctx, loc, new_sid);
}

/**
 * handle_unified_login — 统一登录 POST /blog/login
 *
 * 功能：先尝试管理员登录（auth_verify_login），成功则写 session Cookie 并重定向到 /blog/admin；
 *       否则用 user_verify 验证普通用户，成功则写 user_session 并重定向到 redir 或 /blog。
 *
 * @param ctx     连接上下文
 * @param user_db 用户库
 * @param body    表单 body（encrypted, redir）
 * 返回值：无
 */
/* 统一登录：先试管理员，成功进 /admin；否则试普通用户，成功进 redir 或 / */
void handle_unified_login(conn_ctx_t *ctx, sqlite3 *user_db, const char *body) {
    if (!body || !user_db) {
        send_unified_login_page(ctx, 1, NULL);
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_unified_login_page(ctx, 1, NULL);
        return;
    }
    memcpy(copy, body, len + 1);
    char *encrypted = query_get(copy, "encrypted");
    char *redir = query_get(copy, "redir");
    free(copy);
    if (!encrypted || !encrypted[0]) {
        free(encrypted);
        if (redir) { send_unified_login_page(ctx, 1, redir); free(redir); } else send_unified_login_page(ctx, 1, NULL);
        return;
    }
    for (char *p = encrypted; *p; p++) if (*p == ' ') *p = '+';
    char uname[128];
    char pwd[256];
    if (!auth_decrypt_credentials(encrypted, uname, sizeof(uname), pwd, sizeof(pwd))) {
        free(encrypted);
        if (redir) { send_unified_login_page(ctx, 1, redir); free(redir); } else send_unified_login_page(ctx, 1, NULL);
        return;
    }
    /* 先试管理员登录（与 admin 完全一致，需要 encrypted） */
    if (auth_verify_login(encrypted)) {
        free(encrypted);
        char new_sid[65];
        if (auth_session_create(new_sid, sizeof(new_sid))) {
            send_redirect_with_cookie(ctx, "/blog/admin", "session", new_sid);
        } else {
            if (redir) { send_unified_login_page(ctx, 1, redir); free(redir); } else send_unified_login_page(ctx, 1, NULL);
        }
        return;
    }
    free(encrypted);
    int user_id = 0;
    int rc = user_verify(user_db, uname, pwd, &user_id);
    if (rc == 1) {
        char new_sid[65];
        if (auth_user_session_create(user_id, uname, new_sid, sizeof(new_sid))) {
            const char *loc = (redir && redir[0]) ? redir : "/blog";
            send_redirect_with_user_cookie(ctx, loc, new_sid);
            if (redir) free(redir);
            return;
        }
    }
    if (redir) { send_unified_login_page(ctx, 1, redir); free(redir); } else send_unified_login_page(ctx, 1, NULL);
}

/**
 * handle_user_login — 用户登录（JSON 接口或表单）POST /blog/user/login
 *
 * 功能：解析 encrypted、post_id；验证成功后写 user_session 并重定向到文章页或 /blog；失败则带 login_error。
 *
 * @param ctx     连接上下文
 * @param user_db 用户库
 * @param body    表单 body
 * @param headers 请求头（未使用，保留接口一致）
 * 返回值：无
 */
void handle_user_login(conn_ctx_t *ctx, sqlite3 *user_db, const char *body, const char *headers) {
    (void)headers;
    if (!body || !user_db) {
        send_json(ctx, 400, "{\"error\":\"请求无效\"}");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_json(ctx, 500, "{\"error\":\"服务器错误\"}");
        return;
    }
    memcpy(copy, body, len + 1);
    char *encrypted = query_get(copy, "encrypted");
    char *post_id_str = query_get(copy, "post_id");
    free(copy);
    if (!encrypted || !encrypted[0]) {
        free(encrypted);
        free(post_id_str);
        send_json(ctx, 400, "{\"error\":\"缺少加密数据\"}");
        return;
    }
    for (char *p = encrypted; *p; p++) if (*p == ' ') *p = '+';
    char uname[128];
    char pwd[256];
    if (!auth_decrypt_credentials(encrypted, uname, sizeof(uname), pwd, sizeof(pwd))) {
        free(encrypted);
        free(post_id_str);
        send_json(ctx, 400, "{\"error\":\"用户名或密码错误\"}");
        return;
    }
    free(encrypted);
    int user_id = 0;
    int rc = user_verify(user_db, uname, pwd, &user_id);
    char loc[256];
    int pid = (post_id_str && post_id_str[0]) ? atoi(post_id_str) : 0;
    if (post_id_str) free(post_id_str);
    if (pid > 0)
        snprintf(loc, sizeof(loc), "/blog/post?id=%d", pid);
    else
        snprintf(loc, sizeof(loc), "/blog");
    if (rc != 1) {
        snprintf(loc + strlen(loc), sizeof(loc) - (size_t)strlen(loc), "%slogin_error=1", pid > 0 ? "&" : "?");
        send_redirect(ctx, loc);
        return;
    }
    char new_sid[65];
    if (!auth_user_session_create(user_id, uname, new_sid, sizeof(new_sid))) {
        snprintf(loc + strlen(loc), sizeof(loc) - (size_t)strlen(loc), "%slogin_error=1", pid > 0 ? "&" : "?");
        send_redirect(ctx, loc);
        return;
    }
    send_redirect_with_user_cookie(ctx, loc, new_sid);
}

/**
 * handle_comment_post — 发表评论或回复 POST /blog/comment
 *
 * 功能：从 body 解析 post_id、parent_id、content；可选 nickname；若已登录则用 user_session 昵称，
 *       否则用「访客X」；写入 client_ip；校验 parent_id 属于该文章后插入评论，重定向回文章详情页。
 *
 * @param ctx       连接上下文
 * @param db        博客库
 * @param body      表单 body（post_id, parent_id, content, nickname 等）
 * @param client_ip 客户端 IP
 * @param headers   请求头（用于 user_session）
 * 返回值：无
 */
void handle_comment_post(conn_ctx_t *ctx, sqlite3 *db, const char *body, const char *client_ip, const char *headers) {
    if (!body) {
        send_500(ctx, "缺少请求体");
        return;
    }

    char user_sid[65] = {0};
    int user_id = 0;
    char username[65] = {0};
    if (!headers || !auth_get_user_session_from_cookie(headers, user_sid, sizeof(user_sid))
        || !auth_user_session_valid(user_sid, &user_id, username, sizeof(username))) {
        const char *err = "{\"error\":\"请先登录\"}";
        send_json(ctx, 401, err);
        return;
    }

    size_t body_len = strlen(body);
    char *body_copy = (char *)malloc(body_len + 1);
    if (!body_copy) {
        send_500(ctx, "内存不足");
        return;
    }
    memcpy(body_copy, body, body_len + 1);

    char *post_id_str = query_get(body_copy, "post_id");
    char *content = query_get(body_copy, "content");
    char *reply_to_str = query_get(body_copy, "reply_to_id");

    if (!post_id_str || !content || content[0] == '\0') {
        free(post_id_str);
        free(reply_to_str);
        free(content);
        free(body_copy);
        send_500(ctx, "缺少必要字段");
        return;
    }

    int post_id = atoi(post_id_str);
    free(post_id_str);
    if (post_id <= 0) {
        free(reply_to_str);
        free(content);
        free(body_copy);
        send_500(ctx, "文章不存在");
        return;
    }

    int parent_id = 0;
    if (reply_to_str && reply_to_str[0]) {
        int rt = atoi(reply_to_str);
        free(reply_to_str);
        if (rt > 0 && db_comment_belongs_to_post(db, rt, post_id))
            parent_id = rt;
    } else if (reply_to_str) {
        free(reply_to_str);
    }

    int new_comment_id = 0;
    const char *final_nick = username;
    if (db_insert_comment(db, post_id, parent_id, final_nick, client_ip ? client_ip : "unknown", NULL, content, &new_comment_id) != SQLITE_OK) {
        free(content);
        free(body_copy);
        send_500(ctx, "保存评论失败");
        return;
    }

    free(content);
    free(body_copy);

    char location[256];
    if (new_comment_id > 0)
        snprintf(location, sizeof(location), "/blog/post?id=%d#comment-%d", post_id, new_comment_id);
    else
        snprintf(location, sizeof(location), "/blog/post?id=%d", post_id);
    send_redirect(ctx, location);
}

