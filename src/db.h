#ifndef BLOG_DB_H
#define BLOG_DB_H

#include <sqlite3.h>
#include <time.h>

/* 默认数据库路径；若 db_open 传入 path 则使用 path（用于与 BLOG_DATA_DIR 统一目录） */
#define BLOG_DB_PATH_DEFAULT "data/blog.db"

typedef struct {
    int id;
    char *title;
    char *content_md;
    time_t created_at;
    time_t updated_at;
} Post;

typedef struct {
    int id;
    int post_id;
    int parent_id;  /* 0 = 顶级评论，否则为被回复评论的 id */
    char *nickname;
    char *ip;
    char *location; /* 客户端获取的归属地，用于显示“来自[location]的网友” */
    char *content;
    time_t created_at;
} Comment;

/* path 为 NULL 时使用 BLOG_DB_PATH_DEFAULT；否则使用 path（如 data_dir/blog.db） */
int db_open(sqlite3 **db, const char *path);
void db_close(sqlite3 *db);
int db_init(sqlite3 *db);

// 文章相关
int db_get_all_posts(sqlite3 *db, Post ***posts_out, int *count_out);
void db_free_posts(Post **posts, int count);

int db_get_post_by_id(sqlite3 *db, int id, Post *post_out);
int db_create_post(sqlite3 *db, const char *title, const char *content_md, int *new_id_out);
int db_update_post(sqlite3 *db, int id, const char *title, const char *content_md);
int db_delete_post(sqlite3 *db, int id);

// 评论相关
int db_get_comments_by_post(sqlite3 *db, int post_id, Comment ***comments_out, int *count_out);
void db_free_comments(Comment **comments, int count);

/* 插入评论，parent_id 为 0 表示顶级评论，否则为回复的评论 id；location 可为 NULL；成功时写入 new_id_out */
int db_insert_comment(sqlite3 *db,
                      int post_id,
                      int parent_id,
                      const char *nickname,
                      const char *ip,
                      const char *location,
                      const char *content,
                      int *new_id_out);

int db_delete_comment(sqlite3 *db, int comment_id);

/* 评论 reply_to 校验：返回 1 表示该评论存在且属于该文章 */
int db_comment_belongs_to_post(sqlite3 *db, int comment_id, int post_id);

// 获取全站评论总数，用于生成“访客X”编号
int db_get_comment_total(sqlite3 *db, int *total_out);

/* 全站累积访问：按「页面访问」计次（非每 TCP 连接），64 位防溢出；今日访问按自然日重置 */
int db_visit_record_pageview(sqlite3 *db);
sqlite3_int64 db_visit_get_total(sqlite3 *db);
sqlite3_int64 db_visit_get_today(sqlite3 *db);

#endif // BLOG_DB_H

