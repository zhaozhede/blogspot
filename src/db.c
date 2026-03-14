#include "db.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * exec_simple — 执行无结果集的 SQL
 *
 * @param db  已打开的 SQLite 连接
 * @param sql SQL 字符串
 * @return SQLITE_OK 成功，否则错误码；错误时向 stderr 打印信息
 */
static int exec_simple(sqlite3 *db, const char *sql) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQLite error: %s\nSQL: %s\n", errmsg ? errmsg : "unknown", sql);
        sqlite3_free(errmsg);
    }
    return rc;
}

/**
 * db_open — 打开博客数据库
 *
 * 功能：打开 path 指向的 SQLite 数据库；path 为 NULL 或空时使用 BLOG_DB_PATH_DEFAULT。
 *       并开启 PRAGMA foreign_keys = ON。
 *
 * @param db   输出：成功时指向已打开的 sqlite3*
 * @param path 数据库文件路径，NULL 表示使用默认 data/blog.db
 * @return SQLITE_OK 成功，否则 SQLite 错误码
 */
int db_open(sqlite3 **db, const char *path) {
    if (!db) return SQLITE_ERROR;
    const char *p = (path && path[0]) ? path : BLOG_DB_PATH_DEFAULT;
    int rc = sqlite3_open(p, db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(*db));
        return rc;
    }
    // 打开外键支持
    exec_simple(*db, "PRAGMA foreign_keys = ON;");
    return SQLITE_OK;
}

/**
 * db_close — 关闭数据库连接
 *
 * @param db 由 db_open 打开的连接，可为 NULL（不操作）
 * 返回值：无
 */
void db_close(sqlite3 *db) {
    if (db) {
        sqlite3_close(db);
    }
}

/**
 * db_init — 初始化数据库表结构
 *
 * 功能：创建 posts、comments、site_stats 表（若不存在）；为 comments 添加 parent_id/location 列；
 *       若无文章则插入一篇示例文章；初始化 site_stats 中的 total_visits、visits_today 等。
 *
 * @param db 已打开的博客库连接
 * @return SQLITE_OK 成功，否则错误码
 */
int db_init(sqlite3 *db) {
    const char *create_posts =
        "CREATE TABLE IF NOT EXISTS posts ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  title TEXT NOT NULL,"
        "  content_md TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  updated_at INTEGER NOT NULL"
        ");";

    const char *create_comments =
        "CREATE TABLE IF NOT EXISTS comments ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  post_id INTEGER NOT NULL,"
        "  parent_id INTEGER,"
        "  nickname TEXT NOT NULL,"
        "  ip TEXT NOT NULL,"
        "  content TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL,"
        "  FOREIGN KEY(post_id) REFERENCES posts(id) ON DELETE CASCADE"
        ");";

    int rc = exec_simple(db, create_posts);
    if (rc != SQLITE_OK) return rc;
    rc = exec_simple(db, create_comments);
    if (rc != SQLITE_OK) return rc;
    /* 旧库可能无 parent_id，尝试添加（若已存在则忽略错误） */
    sqlite3_exec(db, "ALTER TABLE comments ADD COLUMN parent_id INTEGER;", NULL, NULL, NULL);
    sqlite3_exec(db, "ALTER TABLE comments ADD COLUMN location TEXT;", NULL, NULL, NULL);

    // 如果没有任何文章，则插入一篇示例文章，方便首次访问测试
    const char *count_sql = "SELECT COUNT(*) FROM posts;";
    sqlite3_stmt *stmt = NULL;
    int rc2 = sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    if (rc2 == SQLITE_OK) {
        rc2 = sqlite3_step(stmt);
        if (rc2 == SQLITE_ROW) {
            int cnt = sqlite3_column_int(stmt, 0);
            if (cnt == 0) {
                const char *insert_sql =
                    "INSERT INTO posts (title, content_md, created_at, updated_at) "
                    "VALUES (?, ?, ?, ?);";
                sqlite3_stmt *istmt = NULL;
                rc2 = sqlite3_prepare_v2(db, insert_sql, -1, &istmt, NULL);
                if (rc2 == SQLITE_OK) {
                    const char *title = "欢迎来到我的博客";
                    const char *content_md =
                        "# 第一篇示例文章\n\n"
                        "你现在看到的是自动创建的示例文章，用来测试 **列表、详情页和评论功能**。\n\n"
                        "- 这是一个运行在 *HI3798mv100* 电视盒子上的 C + SQLite 博客。\n"
                        "- 正文使用 Markdown 编写，在浏览器端通过 `marked.js` 渲染。\n\n"
                        "可以随便在下面发表评论，测试昵称自动生成以及 IP 记录。";
                    time_t now = time(NULL);
                    sqlite3_bind_text(istmt, 1, title, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_text(istmt, 2, content_md, -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(istmt, 3, (sqlite3_int64)now);
                    sqlite3_bind_int64(istmt, 4, (sqlite3_int64)now);
                    rc2 = sqlite3_step(istmt);
                    if (rc2 != SQLITE_DONE) {
                        fprintf(stderr, "Insert demo post failed: %s\n", sqlite3_errmsg(db));
                    }
                    sqlite3_finalize(istmt);
                } else {
                    fprintf(stderr, "Prepare demo post failed: %s\n", sqlite3_errmsg(db));
                }
            }
        }
        sqlite3_finalize(stmt);
    }

    /* 全站访问人次：单表 key-value，防止 INTEGER 溢出用条件更新 */
    rc = exec_simple(db,
        "CREATE TABLE IF NOT EXISTS site_stats ("
        "  key TEXT PRIMARY KEY,"
        "  value INTEGER NOT NULL"
        ");");
    if (rc != SQLITE_OK) return rc;
    sqlite3_exec(db, "INSERT OR IGNORE INTO site_stats (key, value) VALUES ('total_visits', 0);", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT OR IGNORE INTO site_stats (key, value) VALUES ('visits_today', 0);", NULL, NULL, NULL);
    sqlite3_exec(db, "INSERT OR IGNORE INTO site_stats (key, value) VALUES ('visits_today_date', 0);", NULL, NULL, NULL);

    return SQLITE_OK;
}

#define VISIT_KEY "total_visits"
/* 留余量避免 SQLite 内部与 64 位有符号上限 */
#define VISIT_MAX_SAFE 9223372036854770000LL

/**
 * bump_total_visits — 仅将全站总访问人次 +1（带溢出保护）
 *
 * @param db 博客库连接
 * @return SQLITE_OK 成功
 */
static int bump_total_visits(sqlite3 *db) {
    const char *sql =
        "UPDATE site_stats SET value = value + 1 WHERE key = '" VISIT_KEY "' AND value < ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) return rc;
    sqlite3_bind_int64(stmt, 1, (sqlite3_int64)VISIT_MAX_SAFE);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) return rc == SQLITE_OK ? SQLITE_ERROR : rc;
    if (sqlite3_changes(db) == 0)
        sqlite3_exec(db,
            "INSERT OR IGNORE INTO site_stats (key, value) VALUES ('" VISIT_KEY "', 1);",
            NULL, NULL, NULL);
    return SQLITE_OK;
}

/**
 * db_visit_record_pageview — 记录一次「页面访问」
 *
 * 功能：按打开整页计 1 次；跨自然日时今日计数归零；total_visits 与 visits_today 均 +1。
 *
 * @param db 博客库连接
 * @return SQLITE_OK 成功，否则错误码
 */
int db_visit_record_pageview(sqlite3 *db) {
    if (!db) return SQLITE_ERROR;
    time_t t = time(NULL);
    struct tm *tm_ptr = localtime(&t);
    int today = 0;
    if (tm_ptr)
        today = (tm_ptr->tm_year + 1900) * 10000 + (tm_ptr->tm_mon + 1) * 100 + tm_ptr->tm_mday;

    /* 跨天则今日计数归零 */
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 stored_date = 0;
    if (sqlite3_prepare_v2(db, "SELECT value FROM site_stats WHERE key = 'visits_today_date';", -1, &stmt, NULL) == SQLITE_OK) {
        if (sqlite3_step(stmt) == SQLITE_ROW)
            stored_date = sqlite3_column_int64(stmt, 0);
        sqlite3_finalize(stmt);
    }
    if (today > 0 && stored_date != (sqlite3_int64)today) {
        char sqlbuf[128];
        snprintf(sqlbuf, sizeof(sqlbuf),
            "UPDATE site_stats SET value = %d WHERE key = 'visits_today_date';", today);
        sqlite3_exec(db, sqlbuf, NULL, NULL, NULL);
        sqlite3_exec(db, "UPDATE site_stats SET value = 0 WHERE key = 'visits_today';", NULL, NULL, NULL);
    }

    if (bump_total_visits(db) != SQLITE_OK) return SQLITE_ERROR;

    /* 今日 +1（同样用安全上限避免溢出） */
    sqlite3_exec(db,
        "UPDATE site_stats SET value = value + 1 WHERE key = 'visits_today' AND value < 9223372036854770000;",
        NULL, NULL, NULL);
    if (sqlite3_changes(db) == 0)
        sqlite3_exec(db,
            "INSERT OR REPLACE INTO site_stats (key, value) VALUES ('visits_today', 1);",
            NULL, NULL, NULL);
    return SQLITE_OK;
}

/**
 * db_visit_get_total — 获取全站累积访问人次
 *
 * @param db 博客库连接
 * @return 总人次，db 为 NULL 或查询失败时返回 0
 */
sqlite3_int64 db_visit_get_total(sqlite3 *db) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM site_stats WHERE key = '" VISIT_KEY "';", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_int64 v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        v = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return v;
}

/**
 * db_visit_get_today — 获取今日访问人次
 *
 * @param db 博客库连接
 * @return 今日人次，db 为 NULL 或查询失败时返回 0
 */
sqlite3_int64 db_visit_get_today(sqlite3 *db) {
    if (!db) return 0;
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, "SELECT value FROM site_stats WHERE key = 'visits_today';", -1, &stmt, NULL) != SQLITE_OK)
        return 0;
    sqlite3_int64 v = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        v = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return v;
}

/**
 * strdup_safe — 安全复制字符串（堆分配）
 *
 * @param src 源字符串（来自 SQLite 列），可为 NULL
 * @return 新字符串，NULL 时返回 NULL，否则调用者不需 free（由 db_free_posts/db_free_comments 等统一释放）
 */
static char *strdup_safe(const unsigned char *src) {
    if (!src) return NULL;
    size_t len = strlen((const char *)src);
    char *dst = (char *)malloc(len + 1);
    if (!dst) return NULL;
    memcpy(dst, src, len + 1);
    return dst;
}

/**
 * db_get_all_posts — 获取所有文章（按创建时间倒序）
 *
 * @param db        博客库连接
 * @param posts_out 输出：文章指针数组，调用者需用 db_free_posts 释放
 * @param count_out 输出：文章数量
 * @return SQLITE_OK 成功，SQLITE_NOMEM 内存不足，其它为 SQLite 错误码
 */
int db_get_all_posts(sqlite3 *db, Post ***posts_out, int *count_out) {
    if (!db || !posts_out || !count_out) return SQLITE_ERROR;

    const char *sql =
        "SELECT id, title, content_md, created_at, updated_at "
        "FROM posts ORDER BY created_at DESC;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    int capacity = 8;
    int count = 0;
    Post **posts = (Post **)malloc(sizeof(Post *) * capacity);
    if (!posts) {
        sqlite3_finalize(stmt);
        return SQLITE_NOMEM;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            Post **tmp = (Post **)realloc(posts, sizeof(Post *) * capacity);
            if (!tmp) {
                rc = SQLITE_NOMEM;
                break;
            }
            posts = tmp;
        }

        Post *p = (Post *)calloc(1, sizeof(Post));
        if (!p) {
            rc = SQLITE_NOMEM;
            break;
        }

        p->id = sqlite3_column_int(stmt, 0);
        p->title = strdup_safe(sqlite3_column_text(stmt, 1));
        p->content_md = strdup_safe(sqlite3_column_text(stmt, 2));
        p->created_at = (time_t)sqlite3_column_int64(stmt, 3);
        p->updated_at = (time_t)sqlite3_column_int64(stmt, 4);

        posts[count++] = p;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_OK) {
        db_free_posts(posts, count);
        return rc;
    }

    *posts_out = posts;
    *count_out = count;
    return SQLITE_OK;
}

/**
 * db_free_posts — 释放 db_get_all_posts 返回的文章数组
 *
 * @param posts 文章指针数组
 * @param count 数量
 * 返回值：无
 */
void db_free_posts(Post **posts, int count) {
    if (!posts) return;
    for (int i = 0; i < count; ++i) {
        if (!posts[i]) continue;
        free(posts[i]->title);
        free(posts[i]->content_md);
        free(posts[i]);
    }
    free(posts);
}

/**
 * db_get_post_by_id — 按 ID 获取单篇文章
 *
 * @param db       博客库连接
 * @param id       文章 ID
 * @param post_out 输出：文章内容（title、content_md 等为堆分配，由调用者或 db_free_posts 释放单篇需单独 free）
 * @return SQLITE_OK 找到，SQLITE_NOTFOUND 无此 ID，其它为错误码
 */
int db_get_post_by_id(sqlite3 *db, int id, Post *post_out) {
    if (!db || !post_out) return SQLITE_ERROR;

    const char *sql =
        "SELECT id, title, content_md, created_at, updated_at "
        "FROM posts WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        memset(post_out, 0, sizeof(Post));
        post_out->id = sqlite3_column_int(stmt, 0);
        post_out->title = strdup_safe(sqlite3_column_text(stmt, 1));
        post_out->content_md = strdup_safe(sqlite3_column_text(stmt, 2));
        post_out->created_at = (time_t)sqlite3_column_int64(stmt, 3);
        post_out->updated_at = (time_t)sqlite3_column_int64(stmt, 4);
        rc = SQLITE_OK;
    } else if (rc == SQLITE_DONE) {
        rc = SQLITE_NOTFOUND;
    }

    sqlite3_finalize(stmt);
    return rc;
}

/**
 * db_create_post — 创建新文章
 *
 * @param db         博客库连接
 * @param title      标题
 * @param content_md Markdown 正文
 * @param new_id_out 输出：新文章 ID，可为 NULL
 * @return SQLITE_OK 成功，否则错误码
 */
int db_create_post(sqlite3 *db, const char *title, const char *content_md, int *new_id_out) {
    if (!db || !title || !content_md) return SQLITE_ERROR;

    const char *sql =
        "INSERT INTO posts (title, content_md, created_at, updated_at) "
        "VALUES (?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    time_t now = time(NULL);

    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content_md, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)now);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Insert post failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc;
    }
    sqlite3_finalize(stmt);

    if (new_id_out) {
        *new_id_out = (int)sqlite3_last_insert_rowid(db);
    }
    return SQLITE_OK;
}

/**
 * db_update_post — 更新文章标题与正文
 *
 * @param db         博客库连接
 * @param id         文章 ID
 * @param title      新标题
 * @param content_md 新 Markdown 正文
 * @return SQLITE_OK 成功，否则错误码
 */
int db_update_post(sqlite3 *db, int id, const char *title, const char *content_md) {
    if (!db || !title || !content_md) return SQLITE_ERROR;

    const char *sql =
        "UPDATE posts SET title = ?, content_md = ?, updated_at = ? "
        "WHERE id = ?;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    time_t now = time(NULL);

    sqlite3_bind_text(stmt, 1, title, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content_md, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, (sqlite3_int64)now);
    sqlite3_bind_int(stmt, 4, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Update post failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

/**
 * db_delete_post — 删除文章（级联删除评论）
 *
 * @param db 博客库连接
 * @param id 文章 ID
 * @return SQLITE_OK 成功，否则错误码
 */
int db_delete_post(sqlite3 *db, int id) {
    if (!db) return SQLITE_ERROR;

    const char *sql = "DELETE FROM posts WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, id);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Delete post failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

/**
 * db_get_comments_by_post — 获取某文章下的全部评论（按时间正序）
 *
 * @param db           博客库连接
 * @param post_id      文章 ID
 * @param comments_out 输出：评论指针数组，调用者需 db_free_comments 释放
 * @param count_out    输出：评论数量
 * @return SQLITE_OK 成功，SQLITE_NOMEM 内存不足，其它为错误码
 */
int db_get_comments_by_post(sqlite3 *db, int post_id, Comment ***comments_out, int *count_out) {
    if (!db || !comments_out || !count_out) return SQLITE_ERROR;

    const char *sql =
        "SELECT id, post_id, COALESCE(parent_id,0), nickname, ip, COALESCE(location,''), content, created_at "
        "FROM comments WHERE post_id = ? ORDER BY created_at ASC;";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, post_id);

    int capacity = 8;
    int count = 0;
    Comment **comments = (Comment **)malloc(sizeof(Comment *) * capacity);
    if (!comments) {
        sqlite3_finalize(stmt);
        return SQLITE_NOMEM;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        if (count >= capacity) {
            capacity *= 2;
            Comment **tmp = (Comment **)realloc(comments, sizeof(Comment *) * capacity);
            if (!tmp) {
                rc = SQLITE_NOMEM;
                break;
            }
            comments = tmp;
        }

        Comment *c = (Comment *)calloc(1, sizeof(Comment));
        if (!c) {
            rc = SQLITE_NOMEM;
            break;
        }

        c->id = sqlite3_column_int(stmt, 0);
        c->post_id = sqlite3_column_int(stmt, 1);
        c->parent_id = sqlite3_column_int(stmt, 2);
        c->nickname = strdup_safe(sqlite3_column_text(stmt, 3));
        c->ip = strdup_safe(sqlite3_column_text(stmt, 4));
        c->location = strdup_safe(sqlite3_column_text(stmt, 5));
        c->content = strdup_safe(sqlite3_column_text(stmt, 6));
        c->created_at = (time_t)sqlite3_column_int64(stmt, 7);

        comments[count++] = c;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE && rc != SQLITE_OK) {
        db_free_comments(comments, count);
        return rc;
    }

    *comments_out = comments;
    *count_out = count;
    return SQLITE_OK;
}

/**
 * db_free_comments — 释放 db_get_comments_by_post 返回的评论数组
 *
 * @param comments 评论指针数组
 * @param count   数量
 * 返回值：无
 */
void db_free_comments(Comment **comments, int count) {
    if (!comments) return;
    for (int i = 0; i < count; ++i) {
        if (!comments[i]) continue;
        free(comments[i]->nickname);
        free(comments[i]->ip);
        free(comments[i]->location);
        free(comments[i]->content);
        free(comments[i]);
    }
    free(comments);
}

/**
 * db_insert_comment — 插入一条评论
 *
 * @param db         博客库连接
 * @param post_id    文章 ID
 * @param parent_id  0 表示顶级评论，否则为被回复评论的 id
 * @param nickname   昵称
 * @param ip         IP 字符串
 * @param location   归属地，可为 NULL
 * @param content    评论内容
 * @param new_id_out 输出：新评论 ID，可为 NULL
 * @return SQLITE_OK 成功，SQLITE_CONSTRAINT 等错误码
 */
int db_insert_comment(sqlite3 *db,
                      int post_id,
                      int parent_id,
                      const char *nickname,
                      const char *ip,
                      const char *location,
                      const char *content,
                      int *new_id_out) {
    if (!db || !nickname || !ip || !content) return SQLITE_ERROR;

    const char *sql =
        "INSERT INTO comments (post_id, parent_id, nickname, ip, location, content, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, post_id);
    if (parent_id <= 0) sqlite3_bind_null(stmt, 2);
    else sqlite3_bind_int(stmt, 2, parent_id);
    sqlite3_bind_text(stmt, 3, nickname, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, ip, -1, SQLITE_TRANSIENT);
    if (location && location[0])
        sqlite3_bind_text(stmt, 5, location, -1, SQLITE_TRANSIENT);
    else
        sqlite3_bind_null(stmt, 5);
    sqlite3_bind_text(stmt, 6, content, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 7, (sqlite3_int64)time(NULL));

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Insert failed: %s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return rc == SQLITE_CONSTRAINT ? SQLITE_CONSTRAINT : SQLITE_ERROR;
    }
    if (new_id_out)
        *new_id_out = (int)sqlite3_last_insert_rowid(db);
    sqlite3_finalize(stmt);
    return SQLITE_OK;
}

/**
 * db_comment_belongs_to_post — 校验评论是否属于某文章
 *
 * @param db        博客库连接
 * @param comment_id 评论 ID
 * @param post_id   文章 ID
 * @return 1 属于该文章且存在，0 否则
 */
int db_comment_belongs_to_post(sqlite3 *db, int comment_id, int post_id) {
    if (!db || comment_id <= 0 || post_id <= 0) return 0;
    const char *sql = "SELECT 1 FROM comments WHERE id = ? AND post_id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, comment_id);
    sqlite3_bind_int(stmt, 2, post_id);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_ROW) ? 1 : 0;
}

/**
 * db_delete_comment — 删除一条评论
 *
 * @param db        博客库连接
 * @param comment_id 评论 ID
 * @return SQLITE_OK 成功，否则错误码
 */
int db_delete_comment(sqlite3 *db, int comment_id) {
    if (!db) return SQLITE_ERROR;

    const char *sql = "DELETE FROM comments WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    sqlite3_bind_int(stmt, 1, comment_id);
    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        fprintf(stderr, "Delete comment failed: %s\n", sqlite3_errmsg(db));
    }

    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

/**
 * db_get_comment_total — 获取全站评论总数（用于「访客X」编号等）
 *
 * @param db        博客库连接
 * @param total_out 输出：评论总数
 * @return SQLITE_OK 成功，否则错误码
 */
int db_get_comment_total(sqlite3 *db, int *total_out) {
    if (!db || !total_out) return SQLITE_ERROR;

    const char *sql = "SELECT COUNT(*) FROM comments;";
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Prepare failed: %s\n", sqlite3_errmsg(db));
        return rc;
    }

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        *total_out = sqlite3_column_int(stmt, 0);
        rc = SQLITE_OK;
    } else {
        rc = SQLITE_ERROR;
    }

    sqlite3_finalize(stmt);
    return rc;
}

