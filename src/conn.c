/**
 * conn.c — 连接与请求缓冲实现
 *
 * 连接池 g_conns、写缓冲扩容、请求完整性判断（含 Content-Length）。
 */
#include "conn.h"
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

conn_ctx_t g_conns[MAX_EPOLL_CONN];

/* ---------------------------------------------------------------------------
 * conn_append — 向连接写缓冲追加数据，容量不足时按 2 倍扩容
 *
 * 功能：将 data 的 len 字节追加到 c 的 write_buf 末尾；若 write_cap 不足则 realloc 扩容。
 *
 * @param c   连接上下文，不可为 NULL
 * @param data 要追加的数据，不可为 NULL
 * @param len  字节数
 * @return 0 成功，-1 失败（c/data 为 NULL 或 realloc 失败）
 * --------------------------------------------------------------------------- */
static int conn_append(conn_ctx_t *c, const void *data, size_t len) {
    if (!c || !data) return -1;
    if (c->write_len + len > c->write_cap) {
        size_t want = c->write_cap ? c->write_cap * 2 : WRITE_BUF_INIT_SIZE;
        while (want < c->write_len + len) want *= 2;
        char *p = (char *)realloc(c->write_buf, want);
        if (!p) return -1;
        c->write_buf = p;
        c->write_cap = want;
    }
    memcpy(c->write_buf + c->write_len, data, len);
    c->write_len += len;
    return 0;
}

/**
 * conn_init — 初始化全局连接池
 *
 * 功能：将 g_conns 中所有槽位的 fd 置为 -1，供 main 中 accept 后复用。
 *
 * 参数：无
 * 返回值：无
 */
void conn_init(void) {
    for (int i = 0; i < MAX_EPOLL_CONN; i++)
        g_conns[i].fd = -1;
}

/**
 * conn_send — 向连接写缓冲追加响应数据
 *
 * 功能：将 data 的 len 字节追加到 ctx 的写缓冲，供 epoll 在 EPOLLOUT 时发送。
 *
 * @param ctx  连接上下文
 * @param data 要发送的数据
 * @param len  字节数
 * 返回值：无（内部失败时仅不追加，不报错）
 */
void conn_send(conn_ctx_t *c, const void *data, size_t len) {
    if (c && data) conn_append(c, data, len);
}

/**
 * conn_free — 释放单连接资源并关闭 socket
 *
 * 功能：关闭 fd、释放 write_buf，并将状态置为 CONN_CLOSING。
 *
 * @param c 连接上下文
 * 返回值：无
 */
void conn_free(conn_ctx_t *c) {
    if (!c) return;
    if (c->fd >= 0) { close(c->fd); c->fd = -1; }
    free(c->write_buf);
    c->write_buf = NULL;
    c->write_cap = c->write_len = c->write_sent = 0;
    c->state = CONN_CLOSING;
}

/**
 * set_nonblocking — 将 fd 设为非阻塞模式
 *
 * 功能：通过 fcntl 在 fd 上增加 O_NONBLOCK，便于 epoll 非阻塞读写。
 *
 * @param fd 文件描述符（如 socket）
 * @return 0 成功，-1 失败（fcntl 错误）
 */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/* ---------------------------------------------------------------------------
 * request_complete — 判断是否已收到完整 HTTP 请求（含 POST body）
 *
 * 功能：在 read_buf 中查找 \r\n\r\n 或 \n\n 作为头结束；若是 POST 则根据
 *       Content-Length 判断 body 是否收齐。
 *
 * @param c                   连接上下文（read_buf、read_len 有效）
 * @param out_header_len      输出：请求头长度（含 \r\n\r\n）
 * @param out_content_length 输出：Content-Length 值（仅 POST 有效）
 * @return 1 完整可处理，-1 错误/需断开（如请求过大），0 继续读
 * --------------------------------------------------------------------------- */
int request_complete(conn_ctx_t *c, int *out_header_len, size_t *out_content_length) {
    char *buf = c->read_buf;
    size_t n = c->read_len;
    if (n < 4) return 0;
    buf[n] = '\0';
    char *header_end = strstr(buf, "\r\n\r\n");
    if (!header_end) header_end = strstr(buf, "\n\n");
    if (!header_end) return (n >= RECV_BUF_SIZE - 1) ? -1 : 0;
    int header_len = (int)(header_end - buf);
    if (header_end[0] == '\r') header_len += 4; else header_len += 2;
    *out_header_len = header_len;
    *out_content_length = 0;
    if (strncmp(buf, "POST ", 5) != 0) return 1;
    const char *cl = "Content-Length:";
    char *cl_line = strstr(buf, cl);
    if (!cl_line || cl_line >= buf + header_len) return 1;
    cl_line += strlen(cl);
    while (*cl_line == ' ' || *cl_line == '\t') cl_line++;
    size_t content_length = (size_t)strtoul(cl_line, NULL, 10);
    *out_content_length = content_length;
    if (content_length == 0) return 1;
    if (header_len + content_length > RECV_BUF_SIZE - 1) return -1;
    if (n >= (size_t)header_len + content_length) return 1;
    return 0;
}
