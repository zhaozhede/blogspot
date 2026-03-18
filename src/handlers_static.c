/**
 * handlers_static.c — 静态资源与关于页
 *
 * - send_static_favicon: 网站图标 /favicon.ico
 * - send_about_page: 静态个人介绍页 individual.html，动态注入访问统计与运行时长
 * - send_static_file: /static/ 前缀的通用静态文件
 * - send_static_jsencrypt: 本地 jsencrypt.min.js
 */
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "conn.h"
#include "http.h"
#include "metrics.h"
#include "handlers.h"

/**
 * send_static_favicon — 发送网站图标 /favicon.ico
 *
 * 功能：读取 static/favicon.png，以 image/png 返回，最大 128KB。
 *
 * @param ctx 连接上下文
 * 返回值：无（失败时 send_404 或 send_500）
 */
// 提供网站图标：/favicon.ico（返回 static/favicon.png，PNG 格式）
void send_static_favicon(conn_ctx_t *ctx) {
    int fd = open("static/favicon.png", O_RDONLY);
    if (fd < 0) {
        send_404(ctx);
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > 128 * 1024) {
        close(fd);
        send_404(ctx);
        return;
    }
    size_t fsize = (size_t)st.st_size;
    char *body = (char *)malloc(fsize);
    if (!body) {
        close(fd);
        send_500(ctx, "Out of memory");
        return;
    }
    size_t nread = 0;
    while (nread < fsize) {
        ssize_t r = read(fd, body + nread, fsize - nread);
        if (r <= 0) break;
        nread += (size_t)r;
    }
    close(fd);
    if (nread != fsize) {
        free(body);
        send_500(ctx, "Read error");
        return;
    }
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: image/png\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "Cache-Control: public, max-age=86400\r\n"
                        "\r\n",
                        fsize);
    conn_send(ctx, hdr, (size_t)hlen);
    conn_send(ctx, body, fsize);
    free(body);
}

/**
 * send_about_page — 发送关于页 GET /about
 *
 * 功能：读取 static/individual.html，在页脚注入累积访问人次与基于服务器启动时间的运行时长脚本。
 *
 * @param ctx 连接上下文
 * 返回值：无
 */
// GET /about：直接返回 static/individual.html，并注入访问统计与运行时长脚本
void send_about_page(conn_ctx_t *ctx) {
    int fd = open("static/individual.html", O_RDONLY);
    if (fd < 0) { send_404(ctx); return; }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > 256 * 1024) {
        close(fd); send_404(ctx); return;
    }
    size_t fsize = (size_t)st.st_size;
    char *body = (char *)malloc(fsize + 1);
    if (!body) { close(fd); send_500(ctx, "Out of memory"); return; }
    size_t nread = 0;
    while (nread < fsize) {
        ssize_t r = read(fd, body + nread, fsize - nread);
        if (r <= 0) break;
        nread += (size_t)r;
    }
    close(fd);
    if (nread != fsize) { free(body); send_500(ctx, "Read error"); return; }
    body[fsize] = '\0';

    /* 在页脚最底部插入累积访问人次（与首页/blog 等统一：在运行时间之后、</footer> 之前） */
    {
        const char *fmarker = "id=\"run-days-footer\"";
        char *fpos = strstr(body, fmarker);
        if (fpos) {
            /* 找到该段后的 </p>，在其后插入 */
            char *p_end = strstr(fpos, "</p>");
            if (p_end) {
                p_end += 4; /* 指向 </p> 之后 */
                char visit_inject[200];
                int vilen = snprintf(visit_inject, sizeof(visit_inject),
                    "\n    <p style=\"margin:0.35rem 0 0;font-size:0.8rem;color:var(--text-light);opacity:0.95;\">累积访问人次：%lld</p>",
                    (long long)metrics_get_total_visits());
                if (vilen > 0 && vilen < (int)sizeof(visit_inject)) {
                    size_t before = (size_t)(p_end - body);
                    size_t rest = fsize - before;
                    char *nb = (char *)malloc(fsize + (size_t)vilen + 1);
                    if (nb) {
                        memcpy(nb, body, before);
                        memcpy(nb + before, visit_inject, (size_t)vilen);
                        memcpy(nb + before + (size_t)vilen, p_end, rest + 1);
                        fsize += (size_t)vilen;
                        free(body);
                        body = nb;
                    }
                }
            }
        }
    }

    /* 在 </body> 前插入基于服务启动时间的运行时长脚本 */
    const char *marker = "</body>";
    char *pos = strstr(body, marker);
    char extra[512];
    int extra_len = snprintf(
        extra,
        sizeof(extra),
        "<script>(function(){ var startSec=%ld; function pad(n){ return n<10?'0'+n:n; } function update(){ var now=Math.floor(Date.now()/1000); var d=Math.floor((now-startSec)/86400); var h=pad(Math.floor((now-startSec)%%86400/3600)); var m=pad(Math.floor((now-startSec)%%3600/60)); var s=pad((now-startSec)%%60); var el=document.getElementById('run-days-footer'); if(el) el.textContent='已运行 '+d+' 天 '+h+' 时 '+m+' 分 '+s+' 秒'; } update(); setInterval(update,1000); })();</script>",
        (long)metrics_get_server_start_time());
    if (extra_len < 0 || (size_t)extra_len >= sizeof(extra)) {
        free(body);
        send_500(ctx, "Internal error");
        return;
    }

    size_t prefix_len, suffix_len;
    if (pos) {
        prefix_len = (size_t)(pos - body);
        suffix_len = fsize - prefix_len;
    } else {
        prefix_len = fsize;
        suffix_len = 0;
    }

    size_t total_len = prefix_len + (size_t)extra_len + suffix_len;

    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
        total_len);
    conn_send(ctx, hdr, (size_t)hlen);

    if (prefix_len > 0)
        conn_send(ctx, body, prefix_len);
    conn_send(ctx, extra, (size_t)extra_len);
    if (suffix_len > 0)
        conn_send(ctx, body + prefix_len, suffix_len);

    free(body);
}

/**
 * send_static_file — 发送 /static/ 前缀的静态文件
 *
 * 功能：path 须以 /static/ 开头；从 static/ 目录读取，禁止 ..；按扩展名设置 Content-Type；
 *       单文件最大 2MB。
 *
 * @param ctx  连接上下文
 * @param path 请求路径（如 /static/xxx.jpg）
 * 返回值：无（失败时 send_404 或 send_500）
 */
// 通用静态文件：GET /static/xxx 从 static/ 目录读取（仅允许图片等安全类型，便于国内部署自托管图）
void send_static_file(conn_ctx_t *ctx, const char *path) {
    if (!path || strncmp(path, "/static/", 8) != 0) { send_404(ctx); return; }
    const char *name = path + 8;
    if (!name[0]) { send_404(ctx); return; }
    for (const char *p = name; p[0]; p++) if (p[0] == '.' && p[1] == '.' && (p[2] == '/' || p[2] == '\0')) { send_404(ctx); return; }
    char filepath[512];
    int n = snprintf(filepath, sizeof(filepath), "static/%s", name);
    if (n <= 0 || n >= (int)sizeof(filepath)) { send_404(ctx); return; }
    const char *ext = strrchr(name, '.');
    const char *ct = "application/octet-stream";
    if (ext && ext[1]) {
        char e[8];
        size_t i = 0;
        for (const char *s = ext + 1; s[0] && i < sizeof(e) - 1; s++) e[i++] = (char)((s[0] >= 'A' && s[0] <= 'Z') ? s[0] + 32 : s[0]);
        e[i] = '\0';
        if (strcmp(e, "jpg") == 0 || strcmp(e, "jpeg") == 0) ct = "image/jpeg";
        else if (strcmp(e, "png") == 0) ct = "image/png";
        else if (strcmp(e, "webp") == 0) ct = "image/webp";
        else if (strcmp(e, "gif") == 0) ct = "image/gif";
        else if (strcmp(e, "svg") == 0) ct = "image/svg+xml";
        else if (strcmp(e, "ico") == 0) ct = "image/x-icon";
        else if (strcmp(e, "js") == 0) ct = "application/javascript; charset=utf-8";
        else if (strcmp(e, "json") == 0) ct = "application/json; charset=utf-8";
    }
    int fd = open(filepath, O_RDONLY);
    if (fd < 0) { send_404(ctx); return; }
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || (size_t)st.st_size > 2 * 1024 * 1024) {
        close(fd); send_404(ctx); return;
    }
    size_t fsize = (size_t)st.st_size;
    char *body = (char *)malloc(fsize);
    if (!body) { close(fd); send_500(ctx, "Out of memory"); return; }
    size_t nread = 0;
    while (nread < fsize) {
        ssize_t r = read(fd, body + nread, fsize - nread);
        if (r <= 0) break;
        nread += (size_t)r;
    }
    close(fd);
    if (nread != fsize) { free(body); send_500(ctx, "Read error"); return; }
    char hdr[320];
    int hlen = snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\nContent-Type: %s\r\nContent-Length: %zu\r\nConnection: close\r\nCache-Control: public, max-age=86400\r\n\r\n",
        ct, fsize);
    conn_send(ctx, hdr, (size_t)hlen);
    conn_send(ctx, body, fsize);
    free(body);
}

/**
 * send_jsencrypt_inline — 将 static/jsencrypt.min.js 内容内联发送到当前响应体
 *
 * 用于登录/注册页：把加密库直接嵌在 HTML 的 <script> 内，避免第二次 HTTP 请求，
 * 从而消除「加密库加载失败」的随机性（网络/缓存导致 script 请求失败或未就绪）。
 * 会对内容中的 </script> 转义为 <\/script>，避免提前闭合标签。
 *
 * @param ctx 连接上下文（须已发送 HTTP 头，正在发送 body）
 */
void send_jsencrypt_inline(conn_ctx_t *ctx) {
    int fd = open("static/jsencrypt.min.js", O_RDONLY);
    if (fd < 0) return;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > 256 * 1024) {
        close(fd);
        return;
    }
    size_t fsize = (size_t)st.st_size;
    char *in = (char *)malloc(fsize);
    if (!in) { close(fd); return; }
    size_t nread = 0;
    while (nread < fsize) {
        ssize_t r = read(fd, in + nread, fsize - nread);
        if (r <= 0) break;
        nread += (size_t)r;
    }
    close(fd);
    if (nread != fsize) { free(in); return; }
    static const char end_script[] = "</script>";
    const size_t end_len = sizeof(end_script) - 1;
    size_t i = 0;
    while (i < fsize) {
        if (i + end_len <= fsize && memcmp(in + i, end_script, end_len) == 0) {
            conn_send(ctx, "<\\/script>", 10);
            i += end_len;
        } else {
            conn_send(ctx, in + i, 1);
            i++;
        }
    }
    free(in);
}

/**
 * send_static_jsencrypt — 发送 /static/jsencrypt.min.js
 *
 * 功能：从 static/jsencrypt.min.js 读取并以 application/javascript 返回，最大 256KB。
 *
 * @param ctx 连接上下文
 * 返回值：无
 */
// 提供静态 JS：/static/jsencrypt.min.js（不依赖 CDN，由本机读取 static 目录）
void send_static_jsencrypt(conn_ctx_t *ctx) {
    int fd = open("static/jsencrypt.min.js", O_RDONLY);
    if (fd < 0) {
        send_404(ctx);
        return;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0 || (size_t)st.st_size > 256 * 1024) {
        close(fd);
        send_404(ctx);
        return;
    }
    size_t fsize = (size_t)st.st_size;
    char *body = (char *)malloc(fsize);
    if (!body) {
        close(fd);
        send_500(ctx, "Out of memory");
        return;
    }
    size_t nread = 0;
    while (nread < fsize) {
        ssize_t r = read(fd, body + nread, fsize - nread);
        if (r <= 0) break;
        nread += (size_t)r;
    }
    close(fd);
    if (nread != fsize) {
        free(body);
        send_500(ctx, "Read error");
        return;
    }
    char hdr[256];
    int hlen = snprintf(hdr, sizeof(hdr),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/javascript; charset=utf-8\r\n"
                        "Content-Length: %zu\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        fsize);
    conn_send(ctx, hdr, (size_t)hlen);
    conn_send(ctx, body, fsize);
    free(body);
}

