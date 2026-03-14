/**
 * http.c — HTTP 响应与工具实现
 *
 * 响应行/头、404/500 正文、重定向、JSON；HTML 转义、URL 解码、query 解析。
 */
#include "http.h"
#include "metrics.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * 响应行与头
 * --------------------------------------------------------------------------- */

/**
 * send_response_header — 发送 HTTP 响应行与常用头
 *
 * 功能：向 ctx 写缓冲写入 "HTTP/1.1 status status_text" 及 Content-Type、Connection: close。
 *
 * @param ctx          连接上下文
 * @param status      状态码（如 200、404）
 * @param status_text 状态文本（如 "OK"、"Not Found"）
 * @param content_type 响应体 MIME 类型（如 "text/html"）
 * 返回值：无
 */
void send_response_header(conn_ctx_t *ctx, int status, const char *status_text, const char *content_type) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 %d %s\r\n"
                       "Content-Type: %s; charset=utf-8\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       status, status_text, content_type);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_404 — 发送 404 页面
 *
 * 功能：发送 HTTP 404 与 HTML 正文「404 未找到」，并附带累积访问人次。
 *
 * @param ctx 连接上下文
 * 返回值：无
 */
void send_404(conn_ctx_t *ctx) {
    send_response_header(ctx, 404, "Not Found", "text/html");
    long long n = metrics_get_total_visits();
    char body[384];
    int len = snprintf(body, sizeof(body),
        "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><title>404 未找到</title></head>"
        "<body><h1>404 未找到</h1><p>页面不存在。</p>"
        "<p style=\"margin-top:1rem;font-size:13px;color:#6b7280;\">累积访问人次：%lld</p></body></html>",
        (long long)n);
    if (len > 0 && len < (int)sizeof(body))
        conn_send(ctx, body, (size_t)len);
    else
        conn_send(ctx, "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"></head><body>404</body></html>", 78);
}

/**
 * send_500 — 发送 500 错误页
 *
 * 功能：发送 HTTP 500 与 HTML 正文，展示 msg 及累积访问人次。
 *
 * @param ctx 连接上下文
 * @param msg 错误说明文字，可为 NULL（显示「未知错误」）
 * 返回值：无
 */
void send_500(conn_ctx_t *ctx, const char *msg) {
    send_response_header(ctx, 500, "Internal Server Error", "text/html");
    long long n = metrics_get_total_visits();
    char body[640];
    int len = snprintf(body, sizeof(body),
                       "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"><title>服务器错误</title></head>"
                       "<body><h1>服务器错误</h1><p>%s</p>"
                       "<p style=\"margin-top:1rem;font-size:13px;color:#6b7280;\">累积访问人次：%lld</p></body></html>",
                       msg ? msg : "未知错误", (long long)n);
    if (len > 0 && len < (int)sizeof(body))
        conn_send(ctx, body, (size_t)len);
    else
        conn_send(ctx, "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"></head><body>500</body></html>", 78);
}

/**
 * send_redirect_with_cookie — 303 重定向并设置 Cookie（后台 session）
 *
 * 功能：发送 303 See Other、Location 与 Set-Cookie（Path=/blog/admin, HttpOnly, Max-Age=86400）。
 *
 * @param ctx         连接上下文
 * @param location    重定向 URL
 * @param cookie_name Cookie 名（如 "session"）
 * @param cookie_value Cookie 值（如 session_id）
 * 返回值：无
 */
void send_redirect_with_cookie(conn_ctx_t *ctx, const char *location, const char *cookie_name, const char *cookie_value) {
    char buf[1024];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 303 See Other\r\n"
                       "Location: %s\r\n"
                       "Set-Cookie: %s=%s; Path=/blog/admin; HttpOnly; Max-Age=86400\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location, cookie_name, cookie_value);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_redirect — 303 重定向，不设置 Cookie
 *
 * 功能：发送 303 See Other 与 Location，Content-Length: 0。
 *
 * @param ctx      连接上下文
 * @param location 重定向 URL
 * 返回值：无
 */
void send_redirect(conn_ctx_t *ctx, const char *location) {
    char buf[512];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 303 See Other\r\n"
                       "Location: %s\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_redirect_clear_cookie — 303 重定向并清除后台 session Cookie
 *
 * 功能：发送 303、Location，并 Set-Cookie session=; Max-Age=0 以清除 session。
 *
 * @param ctx      连接上下文
 * @param location 重定向 URL
 * 返回值：无
 */
void send_redirect_clear_cookie(conn_ctx_t *ctx, const char *location) {
    char buf[640];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 303 See Other\r\n"
                       "Location: %s\r\n"
                       "Set-Cookie: session=; Path=/blog/admin; HttpOnly; Max-Age=0\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_redirect_clear_user_cookie — 303 重定向并清除前台用户 session Cookie
 *
 * 功能：发送 303、Location，并清除 user_session Cookie（Path=/）。
 *
 * @param ctx      连接上下文
 * @param location 重定向 URL
 * 返回值：无
 */
void send_redirect_clear_user_cookie(conn_ctx_t *ctx, const char *location) {
    char buf[640];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 303 See Other\r\n"
                       "Location: %s\r\n"
                       "Set-Cookie: user_session=; Path=/; HttpOnly; Max-Age=0\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_redirect_with_user_cookie — 303 重定向并设置用户 session Cookie
 *
 * 功能：发送 303、Location，并 Set-Cookie user_session=session_id（Path=/, HttpOnly, SameSite=Lax）。
 *
 * @param ctx        连接上下文
 * @param location   重定向 URL
 * @param session_id 用户 session 字符串（64 字符 hex）
 * 返回值：无
 */
void send_redirect_with_user_cookie(conn_ctx_t *ctx, const char *location, const char *session_id) {
    char buf[640];
    int len = snprintf(buf, sizeof(buf),
                       "HTTP/1.1 303 See Other\r\n"
                       "Location: %s\r\n"
                       "Set-Cookie: user_session=%s; Path=/; HttpOnly; Max-Age=86400; SameSite=Lax\r\n"
                       "Content-Length: 0\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       location, session_id);
    conn_send(ctx, buf, (size_t)len);
}

/**
 * send_json — 发送 JSON 响应
 *
 * 功能：发送 HTTP status、Content-Type: application/json，以及 json_body 作为正文。
 *
 * @param ctx       连接上下文
 * @param status   HTTP 状态码（200 时状态文本为 "OK"，否则 "Error"）
 * @param json_body 已格式化的 JSON 字符串
 * 返回值：无
 */
void send_json(conn_ctx_t *ctx, int status, const char *json_body) {
    char hdr[256];
    size_t body_len = strlen(json_body);
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 %d %s\r\n"
                        "Content-Type: application/json; charset=utf-8\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        status, status == 200 ? "OK" : "Error", body_len);
    conn_send(ctx, hdr, (size_t)hlen);
    conn_send(ctx, json_body, body_len);
}

/* ---------------------------------------------------------------------------
 * html_escape — 将 & < > " ' 转为 HTML 实体，返回堆内存，调用者需 free
 *
 * @param src 源字符串
 * @return 新字符串指针，失败或 src 为 NULL 时返回空串或 NULL（malloc 失败）
 * --------------------------------------------------------------------------- */
char *html_escape(const char *src) {
    if (!src) {
        char *empty = (char *)malloc(1);
        if (empty) empty[0] = '\0';
        return empty;
    }
    size_t len = strlen(src);
    size_t max_len = len * 6 + 1;
    char *out = (char *)malloc(max_len);
    if (!out) return NULL;
    char *p = out;
    for (const char *s = src; *s; ++s) {
        switch (*s) {
            case '&': memcpy(p, "&amp;", 5); p += 5; break;
            case '<': memcpy(p, "&lt;", 4); p += 4; break;
            case '>': memcpy(p, "&gt;", 4); p += 4; break;
            case '"': memcpy(p, "&quot;", 6); p += 6; break;
            case '\'': memcpy(p, "&#39;", 5); p += 5; break;
            default: *p++ = *s; break;
        }
    }
    *p = '\0';
    return out;
}

/* ---------------------------------------------------------------------------
 * url_decode — URL 解码：%XX 与 + → 空格，原地修改 s
 *
 * @param s 以 NUL 结尾的字符串，会被原地改写
 * 返回值：无
 * --------------------------------------------------------------------------- */
void url_decode(char *s) {
    char *src = s, *dst = s;
    while (*src) {
        if (*src == '%' && src[1] && src[2]) {
            char hex[3] = { src[1], src[2], '\0' };
            *dst++ = (char)strtol(hex, NULL, 16);
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            src++;
        } else {
            *dst++ = *src++;
        }
    }
    *dst = '\0';
}

/* ---------------------------------------------------------------------------
 * query_get — 从查询字符串中取 key 对应的值
 *
 * 功能：在 query 中查找 key= 并解码值；返回堆内存，调用者需 free。
 *
 * @param query 查询字符串（如 "a=1&b=2"）
 * @param key   键名
 * @return 键对应的值，不存在或失败返回 NULL
 * --------------------------------------------------------------------------- */
char *query_get(const char *query, const char *key) {
    if (!query || !key) return NULL;
    size_t key_len = strlen(key);
    const char *p = query;
    while (*p) {
        if ((p == query || *(p - 1) == '&') &&
            strncmp(p, key, key_len) == 0 &&
            p[key_len] == '=') {
            p += key_len + 1;
            const char *end = strchr(p, '&');
            size_t len = end ? (size_t)(end - p) : strlen(p);
            char *value = (char *)malloc(len + 1);
            if (!value) return NULL;
            memcpy(value, p, len);
            value[len] = '\0';
            url_decode(value);
            return value;
        }
        p++;
    }
    return NULL;
}
