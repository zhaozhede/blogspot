/**
 * handlers_auth.c — 登录 / 注册相关页面
 *
 * 包含：
 * - 后台登录页（RSA 公钥 + JSEncrypt）
 * - 统一登录页（前台 / 后台共用）
 * - 注册页（加密账户:密码）
 * 仅负责页面与重定向，不直接操作数据库。
 */
#include <stdio.h>
#include <string.h>

#include "conn.h"
#include "http.h"
#include "metrics.h"
#include "auth.h"
#include "handlers.h"

/**
 * send_login_page — 后台登录页（RSA 公钥 + JSEncrypt）
 *
 * 功能：将公钥嵌入页面，表单提交时客户端用 JSEncrypt 加密「账户:密码」后 POST 到 /blog/admin/login。
 *
 * @param ctx       连接上下文
 * @param show_error 1 显示「账户或密码错误」，0 不显示
 * 返回值：无
 */
// 后台登录页：公钥嵌入页面，客户端用 JSEncrypt 加密「账户:密码」后 POST（与修改前一致）
void send_login_page(conn_ctx_t *ctx, int show_error) {
    const char *pem = auth_get_public_key_pem();
    if (!pem) {
        send_500(ctx, "RSA 公钥未就绪");
        return;
    }
    send_response_header(ctx, 200, "OK", "text/html");
    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>后台登录</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#2563eb;--shadow:0 18px 40px rgba(15,23,42,0.12);}"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;}"
        "main{width:100%;max-width:420px;margin:0 16px;padding:1.9rem 1.6rem;background:var(--card-bg);border-radius:24px;box-shadow:var(--shadow);border:1px solid var(--card-border);box-sizing:border-box;}"
        "h1{text-align:center;margin:0 0 0.9rem;font-size:1.5rem;letter-spacing:0.06em;}"
        "p#loadHint{margin:0 0 0.75rem;font-size:0.85rem;color:var(--text-muted);text-align:center;}"
        "input{width:100%;padding:0.6rem 0.75rem;margin-bottom:0.9rem;font-size:1rem;box-sizing:border-box;border:1px solid var(--card-border);border-radius:12px;background:rgba(255,255,255,0.96);color:var(--text);}"
        "button{width:100%;padding:0.65rem 0.75rem;background:var(--accent);color:#f9fafb;border:none;border-radius:999px;font-size:1rem;font-weight:500;cursor:pointer;box-shadow:0 10px 25px rgba(37,99,235,0.28);} button:hover{filter:brightness(0.97);}"
        ".err{color:#dc3545;font-size:0.9rem;margin-bottom:0.5rem;text-align:center;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#60a5fa;--shadow:0 20px 50px rgba(0,0,0,0.9);}"
        "input{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        "}"
        "@media(max-width:480px){main{margin:0 12px;padding:1.65rem 1.35rem;border-radius:20px;}}"
        "</style>"
        "<script>";
    conn_send(ctx, head, strlen(head));
    send_jsencrypt_inline(ctx);
    {
        const char *tail = "</script></head><body><main><h1>博客后台登录</h1><p id=\"loadHint\"></p>";
        conn_send(ctx, tail, strlen(tail));
    }
    if (show_error) {
        const char *err_msg = "<p class=\"err\">账户或密码错误，请重试。</p>";
        conn_send(ctx, err_msg, strlen(err_msg));
    }
    const char *form =
         "<form id=\"loginForm\" method=\"POST\" action=\"/blog/admin/login\">"
         "<label>账户：</label><br><input type=\"text\" name=\"username\" value=\"admin\" required><br>"
         "<label>密码：</label><br><input type=\"password\" name=\"password\" required><br>"
         "<input type=\"hidden\" name=\"encrypted\" id=\"encrypted\" value=\"\">"
         "<button type=\"submit\" id=\"loginBtn\">登录</button></form>"
         "<script>"
         "(function(){ var hint=document.getElementById('loadHint'); var formEl=document.getElementById('loginForm');"
         "if(typeof JSEncrypt==='undefined'){ if(hint) hint.textContent='加密库异常，请刷新页面'; return; }"
         "formEl.onsubmit=function(e){ e.preventDefault();"
         "try{ var u=document.querySelector('input[name=username]').value,p=document.querySelector('input[name=password]').value;"
         "var pem=(document.getElementById('publicKey').textContent||'').trim();"
         "if(!pem){ alert('公钥未就绪'); return; }"
         "var enc=new JSEncrypt(); enc.setPublicKey(pem); var encStr=enc.encrypt(u+':'+p);"
         "if(!encStr){ alert('加密失败，请重试'); return; }"
         "document.getElementById('encrypted').value=encStr; document.querySelector('input[name=password]').value='';"
         "formEl.submit(); }catch(err){ alert('登录出错: '+err.message); } }; })();"
         "</script>"
         "<script type=\"text/plain\" id=\"publicKey\">";
    conn_send(ctx, form, strlen(form));
    conn_send(ctx, pem, strlen(pem));
    conn_send(ctx, "</script></main></body></html>", 32);
}

/**
 * send_admin_redirect_to_login — 未登录访问后台时重定向到登录页
 *
 * @param ctx 连接上下文
 * 返回值：无
 */
void send_admin_redirect_to_login(conn_ctx_t *ctx) {
    send_redirect(ctx, "/blog/admin");
}

/**
 * send_unified_login_page — 前台统一登录页（与后台同逻辑，可带 redir）
 *
 * 功能：公钥嵌入，JSEncrypt 加密后 POST 到 /blog/login；支持 hidden 参数 redir 登录后跳转。
 *
 * @param ctx       连接上下文
 * @param show_error 1 显示错误提示，0 不显示
 * @param redir     登录成功后的跳转 URL，可为 NULL
 * 返回值：无
 */
/* 统一登录页：与 admin 相同，公钥嵌入，客户端用 JSEncrypt 加密后 POST */
void send_unified_login_page(conn_ctx_t *ctx, int show_error, const char *redir) {
    const char *pem = auth_get_public_key_pem();
    if (!pem) {
        send_500(ctx, "RSA 公钥未就绪");
        return;
    }
    send_response_header(ctx, 200, "OK", "text/html");
    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>登录</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#2563eb;--shadow:0 18px 40px rgba(15,23,42,0.12);}"
        "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;}"
        "main{width:100%;max-width:420px;margin:0 16px;padding:1.9rem 1.6rem;background:var(--card-bg);"
        "border-radius:24px;box-shadow:var(--shadow);border:1px solid var(--card-border);box-sizing:border-box;}"
        "h1{text-align:center;margin:0 0 0.9rem;font-size:1.5rem;letter-spacing:0.05em;}"
        "p#loadHint{margin:0 0 0.75rem;font-size:0.85rem;color:var(--text-muted);text-align:center;}"
        "input{width:100%;padding:0.6rem 0.75rem;margin-bottom:0.9rem;font-size:1rem;box-sizing:border-box;border:1px solid var(--card-border);border-radius:12px;background:rgba(255,255,255,0.96);color:var(--text);}"
        "button{width:100%;padding:0.65rem 0.75rem;background:var(--accent);color:#fff;border:none;border-radius:999px;font-size:1rem;font-weight:500;cursor:pointer;box-shadow:0 10px 25px rgba(37,99,235,0.25);} button:hover{filter:brightness(0.97);}"
        "a{color:var(--accent);text-decoration:none;} a:hover{text-decoration:underline;}"
        ".err{color:#dc3545;font-size:0.9rem;margin-bottom:0.5rem;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#60a5fa;--shadow:0 20px 50px rgba(0,0,0,0.9);}"
        "input{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        "}"
        "@media(max-width:480px){main{margin:0 12px;padding:1.65rem 1.35rem;border-radius:20px;}}"
        "</style>"
        "<script>";
    conn_send(ctx, head, strlen(head));
    send_jsencrypt_inline(ctx);
    {
        const char *tail = "</script></head><body><main><h1>登录</h1><p id=\"loadHint\"></p>";
        conn_send(ctx, tail, strlen(tail));
    }
    if (show_error) {
        const char *err_msg = "<p class=\"err\">账户或密码错误，请重试。</p>";
        conn_send(ctx, err_msg, strlen(err_msg));
    }
    const char *form_start =
         "<form id=\"loginForm\" method=\"POST\" action=\"/blog/login\">"
         "<label>账户：</label><br><input type=\"text\" name=\"username\" required><br>"
         "<label>密码：</label><br><input type=\"password\" name=\"password\" required><br>"
         "<input type=\"hidden\" name=\"encrypted\" id=\"encrypted\" value=\"\">";
    conn_send(ctx, form_start, strlen(form_start));
    if (redir && redir[0]) {
        char redir_line[512];
        int rn = snprintf(redir_line, sizeof(redir_line), "<input type=\"hidden\" name=\"redir\" value=\"%s\">", redir);
        if (rn > 0 && rn < (int)sizeof(redir_line))
            conn_send(ctx, redir_line, (size_t)rn);
    }
    const char *form_end =
         "<button type=\"submit\" id=\"loginBtn\">登录</button>"
         "</form><p><a href=\"/blog/register\">注册新用户</a></p>"
         "<script>"
         "(function(){ var hint=document.getElementById('loadHint'); var formEl=document.getElementById('loginForm');"
         "if(typeof JSEncrypt==='undefined'){ if(hint) hint.textContent='加密库异常，请刷新页面'; return; }"
         "formEl.onsubmit=function(e){ e.preventDefault();"
         "try{ var u=document.querySelector('input[name=username]').value,p=document.querySelector('input[name=password]').value;"
         "var pem=(document.getElementById('publicKey').textContent||'').trim();"
         "if(!pem){ alert('公钥未就绪'); return; }"
         "var enc=new JSEncrypt(); enc.setPublicKey(pem); var encStr=enc.encrypt(u+':'+p);"
         "if(!encStr){ alert('加密失败，请重试'); return; }"
         "document.getElementById('encrypted').value=encStr; document.querySelector('input[name=password]').value='';"
         "formEl.submit(); }catch(err){ alert('登录出错: '+err.message); } }; })();"
         "</script>"
         "<script type=\"text/plain\" id=\"publicKey\">";
    conn_send(ctx, form_end, strlen(form_end));
    conn_send(ctx, pem, strlen(pem));
    {
        char tbuf[160];
        int tn = snprintf(tbuf, sizeof(tbuf),
            "</script><p style=\"text-align:center;margin-top:1rem;font-size:0.8rem;color:#6b7280;\">累积访问人次：%lld</p></main></body></html>",
            (long long)metrics_get_total_visits());
        if (tn > 0 && tn < (int)sizeof(tbuf)) conn_send(ctx, tbuf, (size_t)tn);
        else
            conn_send(ctx, "</script></main></body></html>", 32);
    }
}

/**
 * send_register_page — 注册页（加密用户名:密码后 POST）
 *
 * 功能：公钥嵌入，表单 POST 到 /blog/user/register；支持 post_id 隐藏域用于注册后回文章页。
 *
 * @param ctx       连接上下文
 * @param show_error 1 显示注册失败提示，0 不显示
 * @param post_id   注册后希望跳转的文章 ID，0 表示不指定
 * @param redir     未使用（保留参数）
 * 返回值：无
 */
/* 注册页：与登录页相同，使用 JSEncrypt 加密后 POST */
void send_register_page(conn_ctx_t *ctx, int show_error, int post_id, const char *redir) {
    const char *pem = auth_get_public_key_pem();
    if (!pem) {
        send_500(ctx, "RSA 公钥未就绪");
        return;
    }
    (void)redir;
    send_response_header(ctx, 200, "OK", "text/html");
    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>注册</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#22c55e;--link:#2563eb;--shadow:0 18px 40px rgba(15,23,42,0.12);}"
        "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);"
        "min-height:100vh;display:flex;align-items:center;justify-content:center;}"
        "main{width:100%;max-width:420px;margin:0 16px;padding:1.9rem 1.6rem;background:var(--card-bg);"
        "border-radius:24px;box-shadow:var(--shadow);border:1px solid var(--card-border);box-sizing:border-box;}"
        "h1{text-align:center;margin:0 0 0.9rem;font-size:1.5rem;letter-spacing:0.05em;}"
        "p#loadHint{margin:0 0 0.75rem;font-size:0.85rem;color:var(--text-muted);text-align:center;}"
        "input{width:100%;padding:0.6rem 0.75rem;margin-bottom:0.9rem;font-size:1rem;box-sizing:border-box;border:1px solid var(--card-border);border-radius:12px;background:rgba(255,255,255,0.96);}"
        "button{width:100%;padding:0.65rem 0.75rem;background:var(--accent);color:#fff;border:none;border-radius:999px;font-size:1rem;font-weight:500;cursor:pointer;box-shadow:0 10px 25px rgba(22,163,74,0.25);} button:hover{filter:brightness(0.97);}"
        "a{color:var(--link);text-decoration:none;} a:hover{text-decoration:underline;}"
        ".err{color:#dc3545;font-size:0.9rem;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#22c55e;--link:#60a5fa;--shadow:0 20px 50px rgba(0,0,0,0.9);}"
        "input{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        "}"
        "@media(max-width:480px){main{margin:0 12px;padding:1.65rem 1.35rem;border-radius:20px;}}"
        "</style>"
        "<script>";
    conn_send(ctx, head, strlen(head));
    send_jsencrypt_inline(ctx);
    {
        const char *t = "</script></head><body><main><h1>注册</h1><p id=\"loadHint\"></p>";
        conn_send(ctx, t, strlen(t));
    }
    if (show_error) {
        conn_send(ctx, "<p class=\"err\">注册失败（如用户名已存在），请重试。</p>", 52);
    }
    char line[512];
    int n = snprintf(line, sizeof(line),
        "<form id=\"regForm\" method=\"POST\" action=\"/blog/user/register\">"
        "<input type=\"hidden\" name=\"encrypted\" id=\"encrypted\" value=\"\">"
        "<input type=\"hidden\" name=\"post_id\" value=\"%d\">"
        "<label>用户名：</label><br><input type=\"text\" name=\"username\" required><br>"
        "<label>密码：</label><br><input type=\"password\" name=\"password\" required><br>"
        "<button type=\"submit\">注册</button></form><p><a href=\"/blog/login\">已有账号？去登录</a></p>",
        post_id);
    conn_send(ctx, line, (size_t)n);
    const char *script =
        "<script>"
        "(function(){ var hint=document.getElementById('loadHint'); var formEl=document.getElementById('regForm');"
        "if(typeof JSEncrypt==='undefined'){ if(hint) hint.textContent='加密库异常，请刷新页面'; return; }"
        "formEl.onsubmit=function(e){ e.preventDefault();"
        "try{ var u=document.querySelector('input[name=username]').value,p=document.querySelector('input[name=password]').value;"
        "var pem=(document.getElementById('publicKey').textContent||'').trim();"
        "if(!pem){ alert('公钥未就绪'); return; }"
        "var enc=new JSEncrypt(); enc.setPublicKey(pem); var s=enc.encrypt(u+':'+p);"
        "if(!s){ alert('加密失败'); return; }"
        "document.getElementById('encrypted').value=s; document.querySelector('input[name=password]').value='';"
        "formEl.submit(); }catch(err){ alert('注册出错: '+err.message); } }; })();"
        "</script>"
        "<script type=\"text/plain\" id=\"publicKey\">";
    conn_send(ctx, script, strlen(script));
    conn_send(ctx, pem, strlen(pem));
    {
        char tbuf[160];
        int tn = snprintf(tbuf, sizeof(tbuf),
            "</script><p style=\"text-align:center;margin-top:1rem;font-size:0.8rem;color:#6b7280;\">累积访问人次：%lld</p></main></body></html>",
            (long long)metrics_get_total_visits());
        if (tn > 0 && tn < (int)sizeof(tbuf)) conn_send(ctx, tbuf, (size_t)tn);
        else
            conn_send(ctx, "</script></main></body></html>", 32);
    }
}

