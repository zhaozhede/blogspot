#include "user_db.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PBKDF2_ITER 10000
#define SALT_LEN 16
#define HASH_LEN 32
#define HEX_SALT_LEN (SALT_LEN * 2)
#define HEX_HASH_LEN (HASH_LEN * 2)

/**
 * exec_simple — 执行无结果集 SQL（user_db 内部）
 *
 * @param db  用户库连接
 * @param sql SQL 字符串
 * @return SQLITE_OK 成功，否则错误码
 */
static int exec_simple(sqlite3 *db, const char *sql) {
    char *errmsg = NULL;
    int rc = sqlite3_exec(db, sql, NULL, NULL, &errmsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "user_db SQLite error: %s\n", errmsg ? errmsg : "unknown");
        sqlite3_free(errmsg);
    }
    return rc;
}

/**
 * bin_to_hex — 将二进制数据转为十六进制字符串
 *
 * @param bin 二进制数据
 * @param len 长度
 * @param hex 输出缓冲区（至少 len*2+1）
 * 返回值：无
 */
static void bin_to_hex(const unsigned char *bin, size_t len, char *hex) {
    for (size_t i = 0; i < len; i++) {
        sprintf(hex + (i * 2), "%02x", bin[i]);
    }
    hex[len * 2] = '\0';
}

/**
 * hex_to_bin — 将十六进制字符串转为二进制
 *
 * @param hex     十六进制字符串
 * @param hex_len 长度（应为偶数）
 * @param bin     输出缓冲区
 * @param bin_size 缓冲区大小（至少 hex_len/2）
 * @return 1 成功，0 失败（长度或字符非法）
 */
static int hex_to_bin(const char *hex, size_t hex_len, unsigned char *bin, size_t bin_size) {
    if (hex_len % 2 != 0 || hex_len / 2 > bin_size) return 0;
    for (size_t i = 0; i < hex_len / 2; i++) {
        char two[3] = { hex[i*2], hex[i*2+1], '\0' };
        unsigned long v = strtoul(two, NULL, 16);
        if (v > 255) return 0;
        bin[i] = (unsigned char)v;
    }
    return 1;
}

/**
 * user_db_open — 打开用户数据库
 *
 * 功能：path 为 NULL 或空时使用 USER_DB_PATH_DEFAULT（data/users.db）；并开启外键。
 *
 * @param db_out 输出：已打开的 sqlite3*
 * @param path  数据库路径，NULL 表示默认
 * @return SQLITE_OK 成功，否则错误码
 */
int user_db_open(sqlite3 **db_out, const char *path) {
    if (!db_out) return SQLITE_ERROR;
    const char *p = (path && path[0]) ? path : USER_DB_PATH_DEFAULT;
    int rc = sqlite3_open(p, db_out);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open user database: %s\n", sqlite3_errmsg(*db_out));
        return rc;
    }
    exec_simple(*db_out, "PRAGMA foreign_keys = ON;");
    return SQLITE_OK;
}

/**
 * user_db_close — 关闭用户数据库连接
 *
 * @param db 由 user_db_open 打开的连接
 * 返回值：无
 */
void user_db_close(sqlite3 *db) {
    if (db) sqlite3_close(db);
}

/**
 * user_db_init — 初始化用户表（users：id, username, salt, password_hash, created_at）
 *
 * @param db 已打开的用户库连接
 * @return SQLITE_OK 成功
 */
int user_db_init(sqlite3 *db) {
    const char *sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  username TEXT UNIQUE NOT NULL,"
        "  salt TEXT NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  created_at INTEGER NOT NULL"
        ");";
    return exec_simple(db, sql);
}

/**
 * user_register — 注册新用户（PBKDF2 盐+哈希存储）
 *
 * 功能：禁止用户名为 "admin"；用户名唯一；密码用 PBKDF2-HMAC-SHA256 存储。
 *
 * @param db       用户库连接
 * @param username 用户名
 * @param password 明文密码
 * @return 0 成功，-1 用户名已存在或为 admin，-2 内部错误
 */
int user_register(sqlite3 *db, const char *username, const char *password) {
    if (!db || !username || !password || username[0] == '\0' || password[0] == '\0')
        return -2;

    /* 禁止使用 admin 作为普通用户名称（保留给后台管理员） */
    if (strcmp(username, "admin") == 0)
        return -1;

    unsigned char salt[SALT_LEN];
    if (RAND_bytes(salt, SALT_LEN) != 1) return -2;

    unsigned char hash[HASH_LEN];
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                          salt, SALT_LEN,
                          PBKDF2_ITER, EVP_sha256(),
                          HASH_LEN, hash) != 1)
        return -2;

    char salt_hex[HEX_SALT_LEN + 1];
    char hash_hex[HEX_HASH_LEN + 1];
    bin_to_hex(salt, SALT_LEN, salt_hex);
    bin_to_hex(hash, HASH_LEN, hash_hex);

    const char *sql = "INSERT INTO users (username, salt, password_hash, created_at) VALUES (?, ?, ?, ?);";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -2;
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, salt_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, hash_hex, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, (sqlite3_int64)time(NULL));
    int step = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (step == SQLITE_CONSTRAINT) return -1;
    if (step != SQLITE_DONE) return -2;
    return 0;
}

/**
 * user_verify — 验证用户名与密码
 *
 * @param db          用户库连接
 * @param username    用户名
 * @param password    明文密码
 * @param user_id_out 输出：用户 ID，可为 NULL
 * @return 1 成功，0 密码错误，-1 用户不存在或参数错误
 */
int user_verify(sqlite3 *db, const char *username, const char *password, int *user_id_out) {
    if (!db || !username || !password) return -1;
    const char *sql = "SELECT id, salt, password_hash FROM users WHERE username = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return -1;
    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_TRANSIENT);
    int step = sqlite3_step(stmt);
    if (step != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int uid = sqlite3_column_int(stmt, 0);
    const unsigned char *salt_hex = sqlite3_column_text(stmt, 1);
    const unsigned char *hash_hex = sqlite3_column_text(stmt, 2);
    if (!salt_hex || !hash_hex) {
        sqlite3_finalize(stmt);
        return -1;
    }
    size_t salt_hex_len = strlen((const char *)salt_hex);
    size_t hash_hex_len = strlen((const char *)hash_hex);
    unsigned char salt[SALT_LEN];
    unsigned char stored_hash[HASH_LEN];
    if (!hex_to_bin((const char *)salt_hex, salt_hex_len, salt, SALT_LEN) ||
        !hex_to_bin((const char *)hash_hex, hash_hex_len, stored_hash, HASH_LEN)) {
        sqlite3_finalize(stmt);
        return -1;
    }
    unsigned char computed[HASH_LEN];
    if (PKCS5_PBKDF2_HMAC(password, (int)strlen(password),
                          salt, SALT_LEN,
                          PBKDF2_ITER, EVP_sha256(),
                          HASH_LEN, computed) != 1) {
        sqlite3_finalize(stmt);
        return -1;
    }
    int ok = (memcmp(computed, stored_hash, HASH_LEN) == 0);
    sqlite3_finalize(stmt);
    if (!ok) return 0;
    if (user_id_out) *user_id_out = uid;
    return 1;
}

/**
 * user_get_username — 根据 user_id 查询用户名
 *
 * @param db       用户库连接
 * @param user_id  用户 ID
 * @param buf      输出缓冲区
 * @param buf_size 缓冲区大小
 * @return 1 成功，0 未找到或参数错误
 */
int user_get_username(sqlite3 *db, int user_id, char *buf, size_t buf_size) {
    if (!db || !buf || buf_size == 0) return 0;
    const char *sql = "SELECT username FROM users WHERE id = ?;";
    sqlite3_stmt *stmt = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) return 0;
    sqlite3_bind_int(stmt, 1, user_id);
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }
    const unsigned char *u = sqlite3_column_text(stmt, 0);
    if (!u) {
        sqlite3_finalize(stmt);
        return 0;
    }
    strncpy(buf, (const char *)u, buf_size - 1);
    buf[buf_size - 1] = '\0';
    sqlite3_finalize(stmt);
    return 1;
}
