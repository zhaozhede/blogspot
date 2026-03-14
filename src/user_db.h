#ifndef BLOG_USER_DB_H
#define BLOG_USER_DB_H

#include <sqlite3.h>
#include <stddef.h>

#define USER_DB_PATH_DEFAULT "data/users.db"

/* path 为 NULL 时使用 USER_DB_PATH_DEFAULT；否则使用 path（与博客库同一目录下的 users.db） */
int user_db_open(sqlite3 **db_out, const char *path);
void user_db_close(sqlite3 *db);
int user_db_init(sqlite3 *db);

/* 注册：0 成功，-1 用户名已存在，-2 内部错误 */
int user_register(sqlite3 *db, const char *username, const char *password);

/* 验证登录：1 成功，0 密码错误，-1 用户不存在或错误；成功时写入 user_id_out */
int user_verify(sqlite3 *db, const char *username, const char *password, int *user_id_out);

/* 根据 user_id 取用户名，写入 buf，返回 1 成功 0 失败 */
int user_get_username(sqlite3 *db, int user_id, char *buf, size_t buf_size);

#endif
