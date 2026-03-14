/**
 * conn.h — 连接与请求缓冲
 *
 * 提供 epoll 下每个客户端连接的状态与读写缓冲：
 * - conn_ctx_t：单连接上下文（fd、读缓冲、写缓冲、状态）
 * - conn_send / conn_free：响应写入与连接释放
 * - request_complete：判断是否已收齐 HTTP 请求（含 POST body）
 * - set_nonblocking：将 fd 设为非阻塞
 * - 连接池 g_conns 供 main 的 epoll 循环使用
 */
#ifndef BLOG_CONN_H
#define BLOG_CONN_H

#include <stddef.h>

/* 单连接读缓冲大小（同时也是允许的最大 HTTP 请求体 + 请求头总大小）。*/
/* 之前是 64 KiB，长文章提交时容易触发「请求体过大」，这里适当调大到 256 KiB。*/
/* 单连接读缓冲大小（请求头 + 请求体）。 */
/* 设为 1 MiB，应用层再对正文做 512 KiB 限制，避免长文触发底层「请求体过大」。 */
#define RECV_BUF_SIZE (1024 * 1024)
#define MAX_EPOLL_CONN 512
#define WRITE_BUF_INIT_SIZE (2 * 1024 * 1024)

/* 连接状态：读请求 → 写响应 → 关闭 */
enum conn_state {
    CONN_READING,
    CONN_WRITING,
    CONN_CLOSING
};

typedef struct conn_ctx conn_ctx_t;

struct conn_ctx {
    int fd;
    char read_buf[RECV_BUF_SIZE];
    size_t read_len;
    char *write_buf;
    size_t write_cap;
    size_t write_len;
    size_t write_sent;
    enum conn_state state;
    char ipstr[64]; /* INET6_ADDRSTRLEN */
};

/* 连接池，main 中 epoll 使用；conn_init 初始化 fd=-1 */
extern conn_ctx_t g_conns[];

/** 初始化连接池，所有槽位 fd=-1。无参数无返回值。 */
void conn_init(void);
/** 向连接写缓冲追加 data[0..len-1]。ctx/data 可为 NULL 则忽略。无返回值。 */
void conn_send(conn_ctx_t *c, const void *data, size_t len);
/** 关闭 fd、释放 write_buf，置 CONN_CLOSING。无返回值。 */
void conn_free(conn_ctx_t *c);

/**
 * 判断是否已收到完整 HTTP 请求（含 POST body）。
 * @param c 连接上下文
 * @param out_header_len 输出请求头长度（含 \\r\\n\\r\\n）
 * @param out_content_length 输出 Content-Length
 * @return 1=完整可处理，-1=错误/断开，0=继续读
 */
int request_complete(conn_ctx_t *c, int *out_header_len, size_t *out_content_length);

/** 将 fd 设为非阻塞。返回 0 成功 -1 失败。 */
int set_nonblocking(int fd);

#endif /* BLOG_CONN_H */
