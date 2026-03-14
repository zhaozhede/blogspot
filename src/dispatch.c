/**
 * dispatch.c — 请求分发与页面访问统计
 *
 * path_counts_as_pageview：判断 GET 路径是否按「页面访问」计次（排除 favicon/静态/后台等）。
 * handle_client：解析 METHOD/PATH/body，记录 PV 后按路径分发到各 handler。
 */
#include "dispatch.h"
#include "handlers.h"
#include "http.h"
#include "db.h"
#include "auth.h"
#include "metrics.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------------------------------------
 * path_counts_as_pageview — 判断该 GET 请求是否按「页面访问」计次
 *
 * 功能：仅整页 HTML（/、/blog、/blog/post 等）计 1 次，排除 favicon、/static/、后台等。
 *
 * @param method HTTP 方法
 * @param path   URL 路径
 * @return 1 计次，0 不计次
 * --------------------------------------------------------------------------- */
static int path_counts_as_pageview(const char *method, const char *path) {
    if (strcmp(method, "GET") != 0) return 0;
    if (strcmp(path, "/favicon.ico") == 0) return 0;
    if (strncmp(path, "/static/", 8) == 0) return 0;
    if (strcmp(path, "/") == 0 || strcmp(path, "/index") == 0 || strcmp(path, "/about") == 0) return 1;
    if (strncmp(path, "/blog", 5) != 0) return 0;
    if (strncmp(path, "/blog/static/", 13) == 0) return 0;
    if (strcmp(path, "/blog/favicon.ico") == 0) return 0;
    if (strncmp(path, "/blog/admin", 11) == 0) return 0;
    if (strncmp(path, "/blog/user/logout", 17) == 0) return 0;
    /* /blog、/blog/post、/blog/login、/blog/register 及博客前台其它 GET 页面 */
    return 1;
}

/* ---------------------------------------------------------------------------
 * handle_client — 解析请求并分发到对应 handler，写响应到 ctx 写缓冲
 *
 * 功能：从 ctx->read_buf 解析 METHOD、PATH、query、body；若为页面访问则调用 db_visit_record_pageview
 *       并更新 metrics；再按路径调用 handle_landing、handle_index、handle_post_detail、后台/登录/注册/评论等。
 *
 * @param ctx      连接上下文（读缓冲已满、写缓冲待写入）
 * @param db       博客库连接
 * @param user_db  用户库连接
 * @param client_ip 客户端 IP 字符串（用于评论等）
 * 返回值：无
 * --------------------------------------------------------------------------- */
void handle_client(conn_ctx_t *ctx, sqlite3 *db, sqlite3 *user_db, const char *client_ip) {
    char *buf = ctx->read_buf;
    size_t received = ctx->read_len;
    if (received == 0) return;
    if (received >= RECV_BUF_SIZE) received = RECV_BUF_SIZE - 1;
    buf[received] = '\0';

    char method[8] = {0};
    char path[512] = {0};
    if (sscanf(buf, "%7s %511s", method, path) != 2) {
        send_500(ctx, "请求行解析失败");
        return;
    }

    char *header_end = strstr(buf, "\r\n\r\n");
    int header_len = 0;
    if (header_end) {
        header_len = (int)(header_end + 4 - buf);
    } else {
        header_end = strstr(buf, "\n\n");
        if (header_end) {
            header_len = (int)(header_end + 2 - buf);
        }
    }
    char *body = NULL;
    if (header_len > 0) {
        body = buf + header_len;
    }

    char *query = NULL;
    char *qmark = strchr(path, '?');
    if (qmark) {
        *qmark = '\0';
        query = qmark + 1;
    }

    if (path_counts_as_pageview(method, path)) {
        if (db_visit_record_pageview(db) == SQLITE_OK) {
            metrics_set_visits(db_visit_get_total(db), db_visit_get_today(db));
        }
    }

    if (strcmp(method, "GET") == 0 && (strcmp(path, "/") == 0 || strcmp(path, "/index") == 0)) {
        handle_landing(ctx, db);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/about") == 0) {
        send_about_page(ctx);
        return;
    }

    if (strcmp(method, "GET") == 0 && strncmp(path, "/static/", 8) == 0) {
        send_static_file(ctx, path);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path, "/favicon.ico") == 0) {
        send_static_favicon(ctx);
        return;
    }

    const char *path_in = path;
    if (strncmp(path, "/blog", 5) == 0) {
        path_in = path + 5;
        if (!path_in[0] || (path_in[0] == '/' && !path_in[1]))
            path_in = "/";
    } else {
        send_404(ctx);
        return;
    }

    if (strcmp(method, "GET") == 0 && strcmp(path_in, "/favicon.ico") == 0) {
        send_static_favicon(ctx);
        return;
    }
    if (strcmp(method, "GET") == 0 && strncmp(path_in, "/static/", 8) == 0) {
        if (strcmp(path_in, "/static/jsencrypt.min.js") == 0)
            send_static_jsencrypt(ctx);
        else
            send_static_file(ctx, path_in);
        return;
    }

    int is_admin_path = strncmp(path_in, "/admin", 6) == 0;

    if (is_admin_path) {
        char session_id[65] = {0};
        auth_get_session_from_cookie(buf, session_id, sizeof(session_id));
        int logged_in = auth_session_valid(session_id);

        if (!logged_in) {
            if (strcmp(method, "GET") == 0 && strcmp(path_in, "/admin") == 0) {
                send_login_page(ctx, 0);
                return;
            }
            if (strcmp(method, "POST") == 0 && strcmp(path_in, "/admin/login") == 0) {
                char *encrypted = body ? query_get(body, "encrypted") : NULL;
                char *password_plain = body ? query_get(body, "password") : NULL;
                int ok = 0;
                if (encrypted && encrypted[0]) {
                    for (char *p = encrypted; *p; p++)
                        if (*p == ' ') *p = '+';
                    ok = auth_verify_login(encrypted);
                } else if (password_plain && password_plain[0]) {
                    ok = auth_verify_plain_password(password_plain);
                }
                free(encrypted);
                free(password_plain);
                if (ok) {
                    char new_sid[65];
                    if (auth_session_create(new_sid, sizeof(new_sid))) {
                        send_redirect_with_cookie(ctx, "/blog/admin", "session", new_sid);
                    } else {
                        send_500(ctx, "创建会话失败");
                    }
                } else {
                    send_login_page(ctx, 1);
                }
                return;
            }
            if (strcmp(method, "GET") == 0 && strcmp(path_in, "/admin/logout") == 0) {
                send_redirect(ctx, "/blog/admin");
                return;
            }
            send_admin_redirect_to_login(ctx);
            return;
        }

        if (strcmp(method, "GET") == 0 && strcmp(path_in, "/admin/logout") == 0) {
            auth_session_remove(session_id);
            send_redirect_clear_cookie(ctx, "/blog/admin");
            return;
        }

        if (strcmp(method, "GET") == 0) {
            if (strcmp(path_in, "/admin") == 0) {
                handle_admin_index(ctx, db, query);
            } else if (strcmp(path_in, "/admin/post/new") == 0) {
                handle_admin_new_post(ctx);
            } else if (strcmp(path_in, "/admin/post/edit") == 0) {
                handle_admin_edit_post(ctx, db, query);
            } else {
                send_404(ctx);
            }
        } else if (strcmp(method, "POST") == 0) {
            if (strcmp(path_in, "/admin/post/create") == 0) {
                handle_admin_post_create(ctx, db, body);
            } else if (strcmp(path_in, "/admin/post/update") == 0) {
                handle_admin_post_update(ctx, db, body);
            } else if (strcmp(path_in, "/admin/post/delete") == 0) {
                handle_admin_post_delete(ctx, db, body);
            } else if (strcmp(path_in, "/admin/comment/delete") == 0) {
                handle_admin_comment_delete(ctx, db, body);
            } else {
                send_response_header(ctx, 405, "Method Not Allowed", "text/plain");
                const char *resp = "Unsupported admin POST path.\n";
                conn_send(ctx, resp, strlen(resp));
            }
        } else {
            send_response_header(ctx, 405, "Method Not Allowed", "text/plain");
            const char *resp = "Unsupported method.\n";
            conn_send(ctx, resp, strlen(resp));
        }
    } else {
        if (strcmp(method, "GET") == 0) {
            if (strcmp(path_in, "/user/logout") == 0) {
                char sid[65] = {0};
                if (auth_get_user_session_from_cookie(buf, sid, sizeof(sid)))
                    auth_user_session_remove(sid);
                char *redir = query ? query_get(query, "redir") : NULL;
                if (redir && redir[0]) {
                    send_redirect_clear_user_cookie(ctx, redir);
                    free(redir);
                } else {
                    if (redir) free(redir);
                    send_redirect_clear_user_cookie(ctx, "/blog");
                }
                return;
            }
            if (strcmp(path_in, "/login") == 0) {
                char *redir = query ? query_get(query, "redir") : NULL;
                send_unified_login_page(ctx, 0, redir);
                if (redir) free(redir);
                return;
            }
            if (strcmp(path_in, "/register") == 0) {
                int post_id = 0;
                if (query) {
                    char *pid_str = query_get(query, "post_id");
                    if (pid_str && pid_str[0]) post_id = atoi(pid_str);
                    free(pid_str);
                }
                send_register_page(ctx, 0, post_id, NULL);
                return;
            }
            if (strcmp(path_in, "/") == 0 || strcmp(path_in, "/index") == 0) {
                handle_index(ctx, db, buf, query);
            } else if (strcmp(path_in, "/post") == 0) {
                handle_post_detail(ctx, db, query, buf);
            } else {
                send_404(ctx);
            }
        } else if (strcmp(method, "POST") == 0) {
            if (strcmp(path_in, "/login") == 0) {
                handle_unified_login(ctx, user_db, body);
            } else if (strcmp(path_in, "/user/register") == 0) {
                handle_user_register(ctx, user_db, body);
            } else if (strcmp(path_in, "/user/login") == 0) {
                handle_user_login(ctx, user_db, body, buf);
            } else if (strcmp(path_in, "/comment") == 0) {
                handle_comment_post(ctx, db, body, client_ip, buf);
            } else {
                send_response_header(ctx, 405, "Method Not Allowed", "text/plain");
                const char *resp = "Unsupported POST path.\n";
                conn_send(ctx, resp, strlen(resp));
            }
        } else {
            send_response_header(ctx, 405, "Method Not Allowed", "text/plain");
            const char *resp = "Unsupported method.\n";
            conn_send(ctx, resp, strlen(resp));
        }
    }
}
