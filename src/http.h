/**
 * http.h — HTTP 响应与工具
 *
 * - 响应：send_response_header, send_404, send_500, send_redirect*, send_json
 * - 工具：html_escape（堆分配需 free）, url_decode（原地）, query_get（堆分配需 free）
 */
#ifndef BLOG_HTTP_H
#define BLOG_HTTP_H

#include "conn.h"

/** 发送响应行与 Content-Type、Connection: close。status/status_text/content_type 为响应头内容。 */
void send_response_header(conn_ctx_t *ctx, int status, const char *status_text, const char *content_type);
/** 发送 404 页面，正文含累积访问人次。 */
void send_404(conn_ctx_t *ctx);
/** 发送 500 页面，msg 为错误说明（可 NULL）。 */
void send_500(conn_ctx_t *ctx, const char *msg);
/** 303 重定向并 Set-Cookie（后台 session）。cookie_name/value 为 Cookie 键值。 */
void send_redirect_with_cookie(conn_ctx_t *ctx, const char *location, const char *cookie_name, const char *cookie_value);
/** 303 重定向，不设置 Cookie。 */
void send_redirect(conn_ctx_t *ctx, const char *location);
/** 303 重定向并清除后台 session Cookie。 */
void send_redirect_clear_cookie(conn_ctx_t *ctx, const char *location);
/** 303 重定向并清除用户 user_session Cookie。 */
void send_redirect_clear_user_cookie(conn_ctx_t *ctx, const char *location);
/** 303 重定向并设置 user_session=session_id（Path=/）。 */
void send_redirect_with_user_cookie(conn_ctx_t *ctx, const char *location, const char *session_id);
/** 发送 JSON 响应，status 为 HTTP 状态码，json_body 为已格式化的 JSON 字符串。 */
void send_json(conn_ctx_t *ctx, int status, const char *json_body);

/** & < > " ' 转 HTML 实体，返回堆内存，调用者 free。src 为 NULL 时返回空串或 NULL。 */
char *html_escape(const char *src);
/** URL 解码 %XX 与 +，原地修改 s。 */
void url_decode(char *s);
/** 从 query 字符串中取 key 对应的值，返回堆内存并已 url_decode，调用者 free。不存在返回 NULL。 */
char *query_get(const char *query, const char *key);

#endif /* BLOG_HTTP_H */
