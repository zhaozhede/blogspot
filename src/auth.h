#ifndef BLOG_AUTH_H
#define BLOG_AUTH_H

#include <stddef.h>

// 从请求头中解析 Cookie: session=xxx，写入 session_id_out，返回 1 表示找到
int auth_get_session_from_cookie(const char *headers, char *session_id_out, size_t out_size);

// 检查 session_id 是否有效（存在且未过期），返回 1 有效 0 无效
int auth_session_valid(const char *session_id);

// 创建新 session，将 id 写入 session_id_out（至少 65 字节），返回 1 成功
int auth_session_create(char *session_id_out, size_t out_size);

// 删除 session（登出）
void auth_session_remove(const char *session_id);

// 初始化：加载或生成 RSA 密钥对，返回 0 成功 -1 失败
int auth_init(void);

// 释放密钥（程序退出时调用）
void auth_cleanup(void);

// 获取公钥 PEM 字符串，用于登录页嵌入；只读，勿 free
const char *auth_get_public_key_pem(void);

// 验证登录：解密 encrypted_b64（base64），得到 "用户名:密码"，验证密码后返回 1 成功 0 失败
int auth_verify_login(const char *encrypted_b64);

// 明文密码验证（加密库未加载时的降级），返回 1 成功 0 失败
int auth_verify_plain_password(const char *password);

// 解密「用户名:密码」（与登录相同 RSA 传输），写入 username_out 与 password_out，返回 1 成功 0 失败
int auth_decrypt_credentials(const char *encrypted_b64,
                              char *username_out, size_t username_size,
                              char *password_out, size_t password_size);

// 用户态 session（评论用）：Cookie 名 user_session
int auth_get_user_session_from_cookie(const char *headers, char *session_id_out, size_t out_size);
int auth_user_session_valid(const char *session_id, int *user_id_out, char *username_out, size_t username_size);
int auth_user_session_create(int user_id, const char *username, char *session_id_out, size_t out_size);
void auth_user_session_remove(const char *session_id);

#endif
