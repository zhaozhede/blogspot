#include "auth.h"

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>

#define SESSION_ID_LEN 32
#define SESSION_TTL_SEC  (24 * 3600)
#define MAX_SESSIONS     256
#define KEY_DIR          "data"
#define KEY_PRIVATE      KEY_DIR "/admin_private.pem"
#define KEY_PUBLIC       KEY_DIR "/admin_public.pem"
#define RSA_BITS         2048
#define PUBLIC_KEY_PEM_MAX 4096

typedef struct {
    char id[65];
    time_t expiry;
} SessionEntry;

typedef struct {
    char id[65];
    time_t expiry;
    int user_id;
    char username[65];
} UserSessionEntry;

static RSA *g_rsa_private = NULL;
static char g_public_key_pem[PUBLIC_KEY_PEM_MAX];
static int g_public_key_pem_len = 0;
static SessionEntry g_sessions[MAX_SESSIONS];
static int g_n_sessions = 0;
static UserSessionEntry g_user_sessions[MAX_SESSIONS];
static int g_n_user_sessions = 0;

/**
 * get_password — 获取管理员密码（环境变量或默认）
 *
 * @return 指向 BLOG_ADMIN_TOKEN 或 "admin123" 的只读字符串
 */
static const char *get_password(void) {
    const char *env = getenv("BLOG_ADMIN_TOKEN");
    return (env && env[0]) ? env : "admin123";
}

/**
 * auth_get_session_from_cookie — 从请求头中解析 Cookie: session=xxx
 *
 * @param headers        完整请求头（含 "Cookie: ..."）
 * @param session_id_out 输出缓冲区
 * @param out_size       缓冲区大小（建议至少 65）
 * @return 1 找到并写入，0 未找到或参数无效
 */
int auth_get_session_from_cookie(const char *headers, char *session_id_out, size_t out_size) {
    if (!headers || !session_id_out || out_size < 65) return 0;
    const char *p = strstr(headers, "Cookie:");
    if (!p) return 0;
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    const char *key = "session=";
    if (strncasecmp(p, key, 8) != 0) {
        const char *q = strstr(p, "; session=");
        if (!q) return 0;
        p = q + 2;
        if (strncasecmp(p, key, 8) != 0) return 0;
        p += 8;
    } else {
        p += 8;
    }
    size_t i = 0;
    while (i < out_size - 1 && p[i] && p[i] != ';' && p[i] != ' ') {
        session_id_out[i] = p[i];
        i++;
    }
    session_id_out[i] = '\0';
    return i > 0 ? 1 : 0;
}

/**
 * auth_session_valid — 检查后台 session 是否有效（存在且未过期）
 *
 * @param session_id 64 字符 hex 的 session ID
 * @return 1 有效，0 无效或已过期
 */
int auth_session_valid(const char *session_id) {
    if (!session_id || strlen(session_id) != 64) return 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_n_sessions; i++) {
        if (strcmp(g_sessions[i].id, session_id) == 0) {
            if (g_sessions[i].expiry > now) return 1;
            g_sessions[i] = g_sessions[g_n_sessions - 1];
            g_n_sessions--;
            return 0;
        }
    }
    return 0;
}

/**
 * auth_session_create — 创建新后台 session
 *
 * 功能：生成 32 字节随机数转 64 字符 hex 作为 session_id，写入表并设置 24 小时过期。
 *
 * @param session_id_out 输出缓冲区（至少 65 字节）
 * @param out_size       缓冲区大小
 * @return 1 成功，0 失败（缓冲区过小或 session 表满）
 */
int auth_session_create(char *session_id_out, size_t out_size) {
    if (out_size < 65 || g_n_sessions >= MAX_SESSIONS) return 0;
    unsigned char buf[SESSION_ID_LEN];
    if (RAND_bytes(buf, SESSION_ID_LEN) != 1) return 0;
    for (int i = 0; i < SESSION_ID_LEN; i++) {
        snprintf(session_id_out + (i * 2), 3, "%02x", buf[i]);
    }
    session_id_out[64] = '\0';
    SessionEntry *e = &g_sessions[g_n_sessions++];
    strncpy(e->id, session_id_out, 64);
    e->id[64] = '\0';
    e->expiry = time(NULL) + SESSION_TTL_SEC;
    return 1;
}

/**
 * auth_session_remove — 删除后台 session（登出）
 *
 * @param session_id 要删除的 session ID
 * 返回值：无
 */
void auth_session_remove(const char *session_id) {
    if (!session_id) return;
    for (int i = 0; i < g_n_sessions; i++) {
        if (strcmp(g_sessions[i].id, session_id) == 0) {
            g_sessions[i] = g_sessions[g_n_sessions - 1];
            g_n_sessions--;
            return;
        }
    }
}

/**
 * get_cookie_value — 从请求头 Cookie 中取出指定 key 的值
 *
 * @param headers  完整请求头
 * @param key     Cookie 键名（如 "user_session"）
 * @param out     输出缓冲区
 * @param out_size 缓冲区大小
 * @return 1 找到并写入，0 未找到或参数无效
 */
static int get_cookie_value(const char *headers, const char *key, char *out, size_t out_size) {
    if (!headers || !key || !out || out_size < 1) return 0;
    size_t key_len = strlen(key);
    const char *p = strstr(headers, "Cookie:");
    if (!p) return 0;
    p += 7;
    while (*p == ' ' || *p == '\t') p++;
    if (strncasecmp(p, key, key_len) != 0) {
        char search[128];
        snprintf(search, sizeof(search), "; %s=", key);
        const char *q = strstr(p, search);
        if (!q) return 0;
        p = q + strlen(search);
    } else {
        p += key_len;
        if (*p != '=') return 0;
        p++;
    }
    size_t i = 0;
    while (i < out_size - 1 && p[i] && p[i] != ';' && p[i] != ' ') {
        out[i] = p[i];
        i++;
    }
    out[i] = '\0';
    return i > 0 ? 1 : 0;
}

/**
 * auth_get_user_session_from_cookie — 从 Cookie 中取 user_session 值
 *
 * @param headers        请求头
 * @param session_id_out 输出缓冲区
 * @param out_size       缓冲区大小（建议至少 65）
 * @return 1 找到，0 未找到
 */
int auth_get_user_session_from_cookie(const char *headers, char *session_id_out, size_t out_size) {
    return get_cookie_value(headers, "user_session", session_id_out, out_size);
}

/**
 * auth_user_session_valid — 检查用户 session 是否有效并返回用户信息
 *
 * @param session_id   64 字符 hex 的 user_session
 * @param user_id_out  输出：用户 ID，可为 NULL
 * @param username_out 输出：用户名，可为 NULL
 * @param username_size 用户名缓冲区大小
 * @return 1 有效，0 无效或已过期
 */
int auth_user_session_valid(const char *session_id, int *user_id_out, char *username_out, size_t username_size) {
    if (!session_id || strlen(session_id) != 64) return 0;
    time_t now = time(NULL);
    for (int i = 0; i < g_n_user_sessions; i++) {
        if (strcmp(g_user_sessions[i].id, session_id) == 0) {
            if (g_user_sessions[i].expiry <= now) {
                g_user_sessions[i] = g_user_sessions[g_n_user_sessions - 1];
                g_n_user_sessions--;
                return 0;
            }
            if (user_id_out) *user_id_out = g_user_sessions[i].user_id;
            if (username_out && username_size > 0) {
                strncpy(username_out, g_user_sessions[i].username, username_size - 1);
                username_out[username_size - 1] = '\0';
            }
            return 1;
        }
    }
    return 0;
}

/**
 * auth_user_session_create — 创建用户 session（评论用）
 *
 * @param user_id       用户 ID
 * @param username      用户名
 * @param session_id_out 输出缓冲区（至少 65 字节）
 * @param out_size      缓冲区大小
 * @return 1 成功，0 失败
 */
int auth_user_session_create(int user_id, const char *username, char *session_id_out, size_t out_size) {
    if (out_size < 65 || !username || g_n_user_sessions >= MAX_SESSIONS) return 0;
    unsigned char buf[SESSION_ID_LEN];
    if (RAND_bytes(buf, SESSION_ID_LEN) != 1) return 0;
    for (int i = 0; i < SESSION_ID_LEN; i++) {
        snprintf(session_id_out + (i * 2), 3, "%02x", buf[i]);
    }
    session_id_out[64] = '\0';
    UserSessionEntry *e = &g_user_sessions[g_n_user_sessions++];
    strncpy(e->id, session_id_out, 64);
    e->id[64] = '\0';
    e->expiry = time(NULL) + SESSION_TTL_SEC;
    e->user_id = user_id;
    strncpy(e->username, username, 63);
    e->username[63] = '\0';
    return 1;
}

/**
 * auth_user_session_remove — 删除用户 session（退出登录）
 *
 * @param session_id 要删除的 user_session 值
 * 返回值：无
 */
void auth_user_session_remove(const char *session_id) {
    if (!session_id) return;
    for (int i = 0; i < g_n_user_sessions; i++) {
        if (strcmp(g_user_sessions[i].id, session_id) == 0) {
            g_user_sessions[i] = g_user_sessions[g_n_user_sessions - 1];
            g_n_user_sessions--;
            return;
        }
    }
}

/**
 * base64_decode — Base64 解码（无换行）
 *
 * @param in      Base64 字符串
 * @param out     输出缓冲区
 * @param out_len 输入时缓冲区大小，输出时写入解码后字节数
 * @return 1 成功，0 失败
 */
static int base64_decode(const char *in, unsigned char *out, size_t *out_len) {
    size_t in_len = strlen(in);
    if (in_len == 0) {
        *out_len = 0;
        return 1;
    }
    BIO *b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO *bmem = BIO_new_mem_buf(in, (int)in_len);
    if (!bmem) {
        BIO_free(b64);
        return 0;
    }
    BIO_push(b64, bmem);
    int n = BIO_read(b64, out, (int)(*out_len));
    BIO_free_all(b64);
    if (n < 0) return 0;
    *out_len = (size_t)n;
    return 1;
}

/**
 * auth_decrypt_credentials — 解密「用户名:密码」并写入输出缓冲区
 *
 * 功能：encrypted_b64 为 RSA 私钥解密后的 Base64；解密得到 "user:pass" 后拆分为 username 与 password。
 *
 * @param encrypted_b64 客户端 RSA 加密后的 Base64 字符串
 * @param username_out  输出：用户名
 * @param username_size 用户名缓冲区大小
 * @param password_out  输出：密码
 * @param password_size 密码缓冲区大小
 * @return 1 成功，0 失败（解密失败或格式错误）
 */
int auth_decrypt_credentials(const char *encrypted_b64,
                             char *username_out, size_t username_size,
                             char *password_out, size_t password_size) {
    if (!g_rsa_private || !encrypted_b64 || !username_out || username_size == 0 || !password_out || password_size == 0)
        return 0;
    size_t enc_len = (size_t)RSA_size(g_rsa_private);
    unsigned char *enc = (unsigned char *)malloc(enc_len);
    if (!enc) return 0;
    if (!base64_decode(encrypted_b64, enc, &enc_len)) {
        free(enc);
        return 0;
    }
    unsigned char *dec = (unsigned char *)malloc((size_t)RSA_size(g_rsa_private));
    if (!dec) {
        free(enc);
        return 0;
    }
    int n = RSA_private_decrypt((int)enc_len, enc, dec, g_rsa_private, RSA_PKCS1_PADDING);
    free(enc);
    if (n <= 0 || n >= RSA_size(g_rsa_private)) {
        free(dec);
        return 0;
    }
    dec[n] = '\0';
    const char *plain = (const char *)dec;
    const char *colon = strchr(plain, ':');
    if (!colon) {
        free(dec);
        return 0;
    }
    size_t ulen = (size_t)(colon - plain);
    if (ulen >= username_size) ulen = username_size - 1;
    memcpy(username_out, plain, ulen);
    username_out[ulen] = '\0';
    const char *pwd = colon + 1;
    size_t plen = strlen(pwd);
    if (plen >= password_size) plen = password_size - 1;
    memcpy(password_out, pwd, plen);
    password_out[plen] = '\0';
    free(dec);
    return 1;
}

/**
 * ensure_key_dir — 确保 data 目录存在（用于存放 RSA 密钥）
 *
 * @return 1 存在或创建成功，0 失败（非 Windows 下 mkdir 失败且非 EEXIST）
 */
static int ensure_key_dir(void) {
#ifdef _WIN32
    (void)KEY_DIR;
    return 0;
#else
    return mkdir(KEY_DIR, 0755) == 0 || errno == EEXIST;
#endif
}

/**
 * auth_init — 初始化认证模块：加载或生成 RSA 密钥对
 *
 * 功能：优先从 data/admin_private.pem、admin_public.pem 加载；若无则生成 2048 位 RSA 并写入；
 *       同时填充 g_public_key_pem 供登录页嵌入。
 *
 * 参数：无
 * @return 0 成功（含已存在密钥），-1 失败（生成或写入失败）
 */
int auth_init(void) {
    g_public_key_pem[0] = '\0';
    g_public_key_pem_len = 0;

    FILE *fp_priv = fopen(KEY_PRIVATE, "r");
    if (fp_priv) {
        g_rsa_private = PEM_read_RSAPrivateKey(fp_priv, NULL, NULL, NULL);
        fclose(fp_priv);
    }
    if (g_rsa_private) {
        FILE *fp_pub = fopen(KEY_PUBLIC, "r");
        if (fp_pub) {
            RSA *pub = PEM_read_RSAPublicKey(fp_pub, NULL, NULL, NULL);
            fclose(fp_pub);
            if (pub) {
                BIO *bio = BIO_new(BIO_s_mem());
                if (bio) {
                    PEM_write_bio_RSAPublicKey(bio, pub);
                    BUF_MEM *bptr = NULL;
                    BIO_get_mem_ptr(bio, &bptr);
                    if (bptr && bptr->length < (size_t)PUBLIC_KEY_PEM_MAX) {
                        memcpy(g_public_key_pem, bptr->data, bptr->length);
                        g_public_key_pem[bptr->length] = '\0';
                        g_public_key_pem_len = (int)bptr->length;
                    }
                    BIO_free(bio);
                }
                RSA_free(pub);
            }
        }
        if (g_public_key_pem_len == 0) {
            BIO *bio = BIO_new(BIO_s_mem());
            if (bio) {
                PEM_write_bio_RSAPublicKey(bio, g_rsa_private);
                BUF_MEM *bptr = NULL;
                BIO_get_mem_ptr(bio, &bptr);
                if (bptr && bptr->length < (size_t)PUBLIC_KEY_PEM_MAX) {
                    memcpy(g_public_key_pem, bptr->data, bptr->length);
                    g_public_key_pem[bptr->length] = '\0';
                    g_public_key_pem_len = (int)bptr->length;
                }
                BIO_free(bio);
            }
        }
        return 0;
    }

    ensure_key_dir();
    BIGNUM *bn = BN_new();
    if (!bn) return -1;
    BN_set_word(bn, RSA_F4);
    g_rsa_private = RSA_new();
    if (!g_rsa_private) {
        BN_free(bn);
        return -1;
    }
    if (RSA_generate_key_ex(g_rsa_private, RSA_BITS, bn, NULL) != 1) {
        RSA_free(g_rsa_private);
        g_rsa_private = NULL;
        BN_free(bn);
        return -1;
    }
    BN_free(bn);

    fp_priv = fopen(KEY_PRIVATE, "w");
    if (fp_priv) {
        PEM_write_RSAPrivateKey(fp_priv, g_rsa_private, NULL, NULL, 0, NULL, NULL);
        fclose(fp_priv);
    }
    FILE *fp_pub = fopen(KEY_PUBLIC, "w");
    if (fp_pub) {
        PEM_write_RSAPublicKey(fp_pub, g_rsa_private);
        fclose(fp_pub);
    }

    BIO *bio = BIO_new(BIO_s_mem());
    if (!bio) return 0;
    PEM_write_bio_RSAPublicKey(bio, g_rsa_private);
    BUF_MEM *bptr = NULL;
    BIO_get_mem_ptr(bio, &bptr);
    if (bptr && bptr->length < (size_t)PUBLIC_KEY_PEM_MAX) {
        memcpy(g_public_key_pem, bptr->data, bptr->length);
        g_public_key_pem[bptr->length] = '\0';
        g_public_key_pem_len = (int)bptr->length;
    }
    BIO_free(bio);
    return 0;
}

/**
 * auth_cleanup — 释放 RSA 私钥（程序退出时调用）
 *
 * 参数：无
 * 返回值：无
 */
void auth_cleanup(void) {
    if (g_rsa_private) {
        RSA_free(g_rsa_private);
        g_rsa_private = NULL;
    }
}

/**
 * auth_get_public_key_pem — 获取公钥 PEM 字符串（供登录页嵌入）
 *
 * @return 只读指针，勿 free；未就绪时返回 NULL
 */
const char *auth_get_public_key_pem(void) {
    return g_public_key_pem_len > 0 ? g_public_key_pem : NULL;
}

/**
 * auth_verify_login — 验证后台登录：解密并校验密码
 *
 * 功能：encrypted_b64 为客户端 RSA 加密的 "用户名:密码" 的 Base64；解密后与 get_password() 比较。
 *
 * @param encrypted_b64 登录表单提交的 encrypted 参数
 * @return 1 密码正确，0 错误或解密失败
 */
int auth_verify_login(const char *encrypted_b64) {
    if (!g_rsa_private || !encrypted_b64) return 0;
    size_t enc_len = (size_t)RSA_size(g_rsa_private);
    unsigned char *enc = (unsigned char *)malloc(enc_len);
    if (!enc) return 0;
    if (!base64_decode(encrypted_b64, enc, &enc_len)) {
        free(enc);
        return 0;
    }
    unsigned char *dec = (unsigned char *)malloc((size_t)RSA_size(g_rsa_private));
    if (!dec) {
        free(enc);
        return 0;
    }
    int n = RSA_private_decrypt((int)enc_len, enc, dec, g_rsa_private, RSA_PKCS1_PADDING);
    free(enc);
    if (n <= 0 || n >= RSA_size(g_rsa_private)) {
        free(dec);
        return 0;
    }
    dec[n] = '\0';
    const char *plain = (const char *)dec;
    const char *colon = strchr(plain, ':');
    const char *password = colon ? colon + 1 : plain;
    const char *expect = get_password();
    int ok = (strcmp(password, expect) == 0);
    free(dec);
    return ok;
}

/**
 * auth_verify_plain_password — 明文密码校验（加密库未加载时的降级）
 *
 * @param password 明文密码
 * @return 1 与配置密码一致，0 否则
 */
int auth_verify_plain_password(const char *password) {
    if (!password) return 0;
    const char *expect = get_password();
    return (strcmp(password, expect) == 0) ? 1 : 0;
}
