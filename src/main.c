#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "db.h"
#include "auth.h"
#include "user_db.h"
#include "conn.h"
#include "http.h"
#include "metrics.h"
#include "dispatch.h"

#define SERVER_PORT 80

static volatile sig_atomic_t g_running = 1;

/**
 * handle_sigint — SIGINT 信号处理（Ctrl+C）
 *
 * 功能：将 g_running 置 0，使 main 循环退出。
 *
 * @param sig 信号编号（未使用）
 * 返回值：无
 */
static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/**
 * main — 程序入口：初始化认证与数据库、创建监听 socket、epoll 事件循环
 *
 * 功能：加载 RSA 密钥、打开 blog.db 与 users.db、创建 IPv6/IPv4 监听、conn_init、epoll 循环中
 *       accept/read/request_complete/handle_client/write；SIGINT 时退出并关闭资源。
 *
 * 参数：无
 * @return 0 正常退出，1 初始化或监听失败
 */
int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, SIG_IGN);

    if (auth_init() != 0) {
        fprintf(stderr, "Failed to init auth (RSA keys)\n");
        return 1;
    }

    const char *data_dir = "data";
    mkdir(data_dir, 0755);

    char blog_path[256];
    char user_path[256];
    snprintf(blog_path, sizeof(blog_path), "%s/blog.db", data_dir);
    snprintf(user_path, sizeof(user_path), "%s/users.db", data_dir);

    sqlite3 *db = NULL;
    if (db_open(&db, blog_path) != SQLITE_OK) {
        fprintf(stderr, "Failed to open DB: %s\n", blog_path);
        return 1;
    }
    if (db_init(db) != SQLITE_OK) {
        fprintf(stderr, "Failed to init DB\n");
        db_close(db);
        return 1;
    }
    metrics_set_visits(db_visit_get_total(db), db_visit_get_today(db));

    sqlite3 *user_db = NULL;
    if (user_db_open(&user_db, user_path) != SQLITE_OK) {
        fprintf(stderr, "Failed to open user DB: %s\n", user_path);
        db_close(db);
        return 1;
    }
    if (user_db_init(user_db) != SQLITE_OK) {
        fprintf(stderr, "Failed to init user DB\n");
        user_db_close(user_db);
        db_close(db);
        return 1;
    }

    int server_fd = -1;
    int use_ipv6 = 0;

    /* 优先尝试 IPv6，失败再回退 IPv4 */
    server_fd = socket(AF_INET6, SOCK_STREAM, 0);
    if (server_fd >= 0) {
        int off = 0;
        setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in6 addr6;
        memset(&addr6, 0, sizeof(addr6));
        addr6.sin6_family = AF_INET6;
        addr6.sin6_addr = in6addr_any;
        addr6.sin6_port = htons(SERVER_PORT);

        if (bind(server_fd, (struct sockaddr *)&addr6, sizeof(addr6)) < 0) {
            perror("bind IPv6");
            close(server_fd);
            server_fd = -1;
        } else {
            use_ipv6 = 1;
        }
    }

    if (server_fd < 0) {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd < 0) {
            perror("socket IPv4");
            user_db_close(user_db);
            db_close(db);
            return 1;
        }
        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in addr4;
        memset(&addr4, 0, sizeof(addr4));
        addr4.sin_family = AF_INET;
        addr4.sin_addr.s_addr = htonl(INADDR_ANY);
        addr4.sin_port = htons(SERVER_PORT);

        if (bind(server_fd, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
            perror("bind IPv4");
            close(server_fd);
            user_db_close(user_db);
            db_close(db);
            return 1;
        }
    }

    if (listen(server_fd, 128) < 0) {
        perror("listen");
        close(server_fd);
        user_db_close(user_db);
        db_close(db);
        return 1;
    }

    metrics_set_server_start_time(time(NULL));

    conn_init();

    if (set_nonblocking(server_fd) < 0) {
        perror("set_nonblocking server");
        close(server_fd);
        user_db_close(user_db);
        db_close(db);
        return 1;
    }

    int epfd = epoll_create1(EPOLL_CLOEXEC);
    if (epfd < 0) {
        perror("epoll_create1");
        close(server_fd);
        user_db_close(user_db);
        db_close(db);
        return 1;
    }

    struct epoll_event ev = {0};
    ev.events = EPOLLIN;
    ev.data.fd = server_fd;
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, server_fd, &ev) < 0) {
        perror("epoll_ctl add server");
        close(epfd);
        close(server_fd);
        user_db_close(user_db);
        db_close(db);
        return 1;
    }

    printf("Blog server listening on port %d (%s), epoll concurrency...\n",
           SERVER_PORT,
           use_ipv6 ? "IPv6/IPv4" : "IPv4");

    struct epoll_event events[MAX_EPOLL_CONN];

    while (g_running) {
        int nfds = epoll_wait(epfd, events, MAX_EPOLL_CONN, 2000);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_fd) {
                /* 接受新连接 */
                while (1) {
                    struct sockaddr_storage client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
                    if (client_fd < 0) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        if (errno == EINTR) continue;
                        perror("accept");
                        break;
                    }

                    int slot = -1;
                    for (int k = 0; k < MAX_EPOLL_CONN; k++) {
                        if (g_conns[k].fd < 0) { slot = k; break; }
                    }
                    if (slot < 0) {
                        close(client_fd);
                        continue;
                    }
                    if (set_nonblocking(client_fd) < 0) {
                        close(client_fd);
                        continue;
                    }

                    conn_ctx_t *c = &g_conns[slot];
                    memset(c, 0, sizeof(*c));
                    c->fd = client_fd;
                    c->state = CONN_READING;
                    c->read_len = 0;

                    void *addr_ptr = NULL;
                    if (client_addr.ss_family == AF_INET) {
                        addr_ptr = &((struct sockaddr_in *)&client_addr)->sin_addr;
                    } else if (client_addr.ss_family == AF_INET6) {
                        addr_ptr = &((struct sockaddr_in6 *)&client_addr)->sin6_addr;
                    }
                    if (addr_ptr)
                        inet_ntop(client_addr.ss_family, addr_ptr, c->ipstr, sizeof(c->ipstr));
                    else
                        snprintf(c->ipstr, sizeof(c->ipstr), "unknown");

                    int one = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

                    struct epoll_event cev = {0};
                    cev.events = EPOLLIN;
                    cev.data.ptr = c;
                    if (epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &cev) < 0) {
                        close(client_fd);
                        c->fd = -1;
                    }
                }
                continue;
            }

            conn_ctx_t *c = (conn_ctx_t *)events[i].data.ptr;
            if (!c || c->fd < 0) continue;

            if (c->state == CONN_READING) {
                if (events[i].events & (EPOLLERR | EPOLLHUP)) {
                    int fd = c->fd;
                    conn_free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    c->fd = -1;
                    continue;
                }
                ssize_t r = recv(c->fd, c->read_buf + c->read_len, RECV_BUF_SIZE - 1 - c->read_len, 0);
                if (r <= 0) {
                    if (r == 0 || errno != EAGAIN) {
                        int fd = c->fd;
                        conn_free(c);
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        c->fd = -1;
                    }
                    continue;
                }
                c->read_len += (size_t)r;
                int header_len = 0;
                size_t content_length = 0;
                int rc = request_complete(c, &header_len, &content_length);
                if (rc == -1) {
                    send_500(c, "请求体过大");
                    c->state = CONN_WRITING;
                    struct epoll_event wev = { .events = EPOLLOUT, .data.ptr = c };
                    epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &wev);
                    continue;
                }
                if (rc == 0) continue;

                handle_client(c, db, user_db, c->ipstr);
                c->state = CONN_WRITING;
                struct epoll_event wev = { .events = EPOLLOUT, .data.ptr = c };
                epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &wev);
                continue;
            }

            if (c->state == CONN_WRITING && (events[i].events & EPOLLOUT)) {
                size_t left = c->write_len - c->write_sent;
                if (left == 0) {
                    int fd = c->fd;
                    conn_free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    c->fd = -1;
                    continue;
                }
                ssize_t sent = send(c->fd, c->write_buf + c->write_sent, left, 0);
                if (sent <= 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    int fd = c->fd;
                    conn_free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    c->fd = -1;
                    continue;
                }
                c->write_sent += (size_t)sent;
                if (c->write_sent >= c->write_len) {
                    int fd = c->fd;
                    conn_free(c);
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    c->fd = -1;
                }
            }
        }
    }

    printf("Shutting down...\n");
    for (int i = 0; i < MAX_EPOLL_CONN; i++) {
        if (g_conns[i].fd >= 0) {
            conn_free(&g_conns[i]);
        }
    }

    close(epfd);
    close(server_fd);
    user_db_close(user_db);
    db_close(db);
    auth_cleanup();

    return 0;
}

