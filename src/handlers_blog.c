/**
 * handlers_blog.c — 博客列表与文章详情
 *
 * - handle_index：文章列表页（分页，带预览）
 * - handle_post_detail：文章详情 + 评论展示与表单
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "conn.h"
#include "http.h"
#include "auth.h"
#include "metrics.h"
#include "db.h"
#include "handlers.h"

/**
 * handle_index — 博客文章列表页 GET /blog 或 /blog/
 *
 * 功能：分页（每页 10 篇）、卡片网格、顶栏用户/登录注册、分页导航、页脚访问人次与运行时长。
 *
 * @param ctx    连接上下文
 * @param db     博客库
 * @param headers 完整请求头（用于解析 user_session）
 * @param query  查询字符串（含 page）
 * 返回值：无
 */
// 输出博客文章列表页：卡片网格 + 分页（每页 10 篇）
void handle_index(conn_ctx_t *ctx, sqlite3 *db, const char *headers, const char *query) {
    Post **posts = NULL;
    int count = 0;
    if (db_get_all_posts(db, &posts, &count) != SQLITE_OK) {
        send_500(ctx, "无法读取文章列表");
        return;
    }

    int page = 1;
    const int per_page = 10;
    if (query) {
        char *page_str = query_get(query, "page");
        if (page_str && page_str[0]) {
            int tmp = atoi(page_str);
            if (tmp > 0) page = tmp;
        }
        free(page_str);
    }
    if (count <= 0) {
        page = 1;
    }

    char user_sid[65] = {0};
    int user_id = 0;
    char user_name[65] = {0};
    int user_logged_in = headers && auth_get_user_session_from_cookie(headers, user_sid, sizeof(user_sid))
                        && auth_user_session_valid(user_sid, &user_id, user_name, sizeof(user_name));

    send_response_header(ctx, 200, "OK", "text/html");

    /* 统一顶栏样式（与首页一致） */
    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/png\">"
        "<title>博客 · zhaozzz.top</title>"
        "<style>"
        ":root{--primary:#2563eb;--text:#1a1a2e;--text-muted:#6b7280;--border:#e5e7eb;--bg:#f8fafc;--card:#fff;--accent:#2563eb;--chip-bg:rgba(255,255,255,0.78);}"
        "*{box-sizing:border-box;}"
        "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif;color:var(--text);line-height:1.6;min-height:100vh;display:flex;flex-direction:column;background:var(--bg);overflow-x:hidden;}"
        ".header{position:sticky;top:0;z-index:1000;height:60px;display:flex;align-items:center;justify-content:space-between;padding:0 24px;"
        "background:rgba(255,255,255,0.28);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);box-shadow:0 1px 0 rgba(0,0,0,0.05);}"
        ".header > a{flex-shrink:0;display:inline-block;color:var(--text);text-decoration:none;font-weight:600;font-size:18px;padding:4px 10px;border-radius:999px;background:var(--chip-bg);} .header > a:hover{color:var(--primary);}"
        ".nav{display:flex;align-items:center;}"
        ".nav > a{display:inline-block;margin-left:24px;color:var(--text);text-decoration:none;font-size:14px;font-weight:500;line-height:1.6;padding:4px 10px;border-radius:999px;background:var(--chip-bg);vertical-align:middle;} .nav > a:hover{color:var(--primary);}"
        ".nav-dropdown{position:relative;margin-right:16px;flex-shrink:0;}"
        ".nav-dropdown-btn{margin:0;background:none;border:none;cursor:pointer;color:var(--text);font-size:14px;font-weight:500;font-family:inherit;padding:4px 10px;line-height:1.6;display:inline-block;border-radius:999px;background:var(--chip-bg);vertical-align:middle;} .nav-dropdown-btn:hover{color:var(--primary);}"
        ".nav-dropdown-panel{display:none;position:absolute;top:100%;left:0;margin-top:4px;min-width:140px;padding:8px 0;background:rgba(255,255,255,0.95);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);border:1px solid var(--border);border-radius:8px;box-shadow:0 4px 16px rgba(0,0,0,0.1);z-index:1001;}"
        ".nav-dropdown-panel.open{display:block;}"
        ".nav-dropdown-panel a,.nav-dropdown-panel .menu-user{display:block;padding:8px 16px;color:var(--text);text-decoration:none;font-size:14px;} .nav-dropdown-panel .menu-user{color:var(--text-muted);}"
        ".nav-dropdown-panel a:hover{background:rgba(0,0,0,0.05);color:var(--primary);}"
        ".blog-menu-bar{display:flex;justify-content:flex-start;padding:6px 24px 0;box-sizing:border-box;width:100%%;}"
        ".main{flex:1 0 auto;display:flex;align-items:center;justify-content:center;padding:24px 16px;box-sizing:border-box;}"
        ".main-inner{display:block;width:100%;max-width:900px;margin:0 auto;background:rgba(255,255,255,0.9);border-radius:24px;padding:20px 16px 18px;box-shadow:0 18px 45px rgba(15,23,42,0.12);border:1px solid rgba(226,232,240,0.9);box-sizing:border-box;}"
        ".main-inner h2{font-size:1.25rem;font-weight:600;color:var(--text);margin:0 0 1rem;padding-bottom:0.5rem;border-bottom:1px solid var(--border);text-align:center;}"
        ".post-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;width:100%;box-sizing:border-box;justify-items:center;}"
        ".post-item{display:block;width:100%;max-width:420px;padding:14px 16px;border-radius:16px;border:1px solid var(--border);background:#fff;box-shadow:0 10px 25px rgba(15,23,42,0.08);box-sizing:border-box;text-align:center;text-decoration:none;color:var(--text);}"
        ".post-item:hover{color:var(--primary);border-color:var(--primary);}"
        ".post-item-title{display:block;font-size:1.15rem;font-weight:600;margin-bottom:0.35rem;}"
        ".post-date{display:block;color:var(--text-muted);font-size:0.875rem;margin-top:0.25rem;}"
        ".post-updated{display:block;color:var(--text-muted);font-size:0.78rem;margin-top:0.1rem;}"
        ".post-preview{display:block;color:var(--text-muted);font-size:0.9rem;margin-top:0.35rem;line-height:1.5;}"
        ".pager{margin-top:18px;display:flex;justify-content:center;gap:8px;font-size:0.85rem;color:var(--text-muted);}"
        ".pager-info{margin:0 4px;}"
        ".pager-link{display:inline-block;padding:4px 10px;border-radius:999px;border:1px solid var(--border);text-decoration:none;color:var(--text);background:rgba(255,255,255,0.9);} .pager-link:hover{color:var(--primary);border-color:var(--primary);}"
        ".footer{text-align:center;padding:24px;color:var(--text-muted);font-size:14px;"
        "background:rgba(255,255,255,0.25);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);border-top:1px solid rgba(255,255,255,0.2);}"
        ".footer a{display:inline-block;color:var(--text-muted);text-decoration:none;font-weight:500;padding:4px 10px;border-radius:999px;background:var(--chip-bg);} .footer a:hover{color:var(--primary);}"
        ".footer p span{display:inline-block;padding:4px 10px;border-radius:999px;background:var(--chip-bg);}"
        "@media(prefers-color-scheme:dark){"
        ":root{--text:#e2e8f0;--text-muted:#94a3b8;--primary:#60a5fa;--chip-bg:rgba(15,23,42,0.78);--bg:#020617;--card:rgba(15,23,42,0.95);--border:rgba(148,163,184,0.35);}"
        ".header{background:rgba(15,23,42,0.35);box-shadow:0 1px 0 rgba(255,255,255,0.06);} .footer{background:rgba(15,23,42,0.3);}"
        ".nav-dropdown-panel{background:rgba(15,23,42,0.95);border-color:rgba(255,255,255,0.1);} .nav-dropdown-panel a:hover{background:rgba(255,255,255,0.08);}"
        ".main-inner{background:rgba(15,23,42,0.9);border-color:rgba(148,163,184,0.4);box-shadow:0 18px 45px rgba(0,0,0,0.5);}"
        ".post-item{background:rgba(15,23,42,0.85);border-color:rgba(148,163,184,0.5);box-shadow:0 10px 25px rgba(0,0,0,0.4);}"
        ".pager-link{background:rgba(15,23,42,0.9);border-color:rgba(148,163,184,0.5);color:var(--text);}"
        "}"
        "@media(max-width:768px){.main-inner{padding:18px 10px 14px;border-radius:20px;} .post-grid{grid-template-columns:1fr;width:100%;justify-items:stretch;} .post-item{max-width:none;} .nav > a{margin-left:16px;font-size:13px;padding:3px 8px;} .blog-menu-bar{padding:4px 16px 0;} .nav-dropdown{margin-right:8px;} .nav-dropdown-btn{padding:3px 8px;}}"
        "</style></head><body>"
        "<header class=\"header\"><a href=\"/\">zhaozzz.top</a><nav class=\"nav\"><a href=\"/\">首页</a><a href=\"/blog\">博客</a><a href=\"/about\">关于</a></nav></header>"
        "<div class=\"blog-menu-bar\"><div class=\"nav-dropdown\"><button type=\"button\" class=\"nav-dropdown-btn\" id=\"blogMenuBtn\" aria-expanded=\"false\">菜单</button><div class=\"nav-dropdown-panel\" id=\"blogMenuPanel\">";
    conn_send(ctx, head, strlen(head));
    char line[2048];
    int n;
    if (user_logged_in) {
        char *uname_esc = html_escape(user_name);
        n = snprintf(line, sizeof(line),
                     "<span class=\"menu-user\">%s</span><a href=\"/blog/user/logout?redir=%%2Fblog\">退出</a>",
                     uname_esc);
        conn_send(ctx, line, (size_t)n);
        free(uname_esc);
    } else {
        const char *auth_links = "<a href=\"/blog/login\">登录</a><a href=\"/blog/register\">注册</a>";
        conn_send(ctx, auth_links, strlen(auth_links));
    }
    const char *nav_tail_index = "</div></div></div><main class=\"main\"><div class=\"main-inner\"><h2>文章列表</h2><div class=\"post-grid\">";
    conn_send(ctx, nav_tail_index, strlen(nav_tail_index));

    int total_pages = (count + per_page - 1) / per_page;
    if (total_pages <= 0) total_pages = 1;
    if (page > total_pages) page = total_pages;
    if (page < 1) page = 1;

    int start = (page - 1) * per_page;
    int end = start + per_page;
    if (start < 0) start = 0;
    if (end > count) end = count;

    if (count == 0) {
        const char *empty_html = "<p class=\"post-preview\">暂无文章。</p>";
        conn_send(ctx, empty_html, strlen(empty_html));
    }

    for (int i = start; i < end; ++i) {
        Post *p = posts[i];
        char *title_esc = html_escape(p->title ? p->title : "(无标题)");
        char created_buf[64];
        char updated_buf[64];
        struct tm *tm_created = localtime(&p->created_at);
        if (tm_created) {
            strftime(created_buf, sizeof(created_buf), "%Y-%m-%d %H:%M", tm_created);
        } else {
            snprintf(created_buf, sizeof(created_buf), "未知时间");
        }
        int show_updated = (p->updated_at > p->created_at);
        if (show_updated) {
            struct tm *tm_updated = localtime(&p->updated_at);
            if (tm_updated) {
                strftime(updated_buf, sizeof(updated_buf), "%Y-%m-%d %H:%M", tm_updated);
            } else {
                snprintf(updated_buf, sizeof(updated_buf), "未知时间");
            }
        } else {
            updated_buf[0] = '\0';
        }

        if (show_updated) {
            n = snprintf(line, sizeof(line),
                         "<a class=\"post-item\" href=\"/blog/post?id=%d\">"
                         "<span class=\"post-item-title\">%s</span>"
                         "<div class=\"post-date\">创建于 %s</div>"
                         "<div class=\"post-updated\">最后修改：%s</div>"
                         "</a>",
                         p->id, title_esc, created_buf, updated_buf);
        } else {
            n = snprintf(line, sizeof(line),
                         "<a class=\"post-item\" href=\"/blog/post?id=%d\">"
                         "<span class=\"post-item-title\">%s</span>"
                         "<div class=\"post-date\">创建于 %s</div>"
                         "</a>",
                         p->id, title_esc, created_buf);
        }
        conn_send(ctx, line, (size_t)n);
        free(title_esc);
    }

    conn_send(ctx, "</div>", 6); /* close post-grid */

    /* 分页导航（每页 10 篇） */
    int total_pages_disp = (count + per_page - 1) / per_page;
    if (total_pages_disp <= 0) total_pages_disp = 1;
    if (total_pages_disp > 1) {
        n = snprintf(line, sizeof(line), "<div class=\"pager\">");
        conn_send(ctx, line, (size_t)n);

        if (page > 1) {
            n = snprintf(line, sizeof(line),
                         "<a class=\"pager-link\" href=\"/blog?page=%d\">上一页</a>", page - 1);
            conn_send(ctx, line, (size_t)n);
        }

        n = snprintf(line, sizeof(line),
                     "<span class=\"pager-info\">第 %d / %d 页</span>", page, total_pages_disp);
        conn_send(ctx, line, (size_t)n);

        if (page < total_pages_disp) {
            n = snprintf(line, sizeof(line),
                         "<a class=\"pager-link\" href=\"/blog?page=%d\">下一页</a>", page + 1);
            conn_send(ctx, line, (size_t)n);
        }

        conn_send(ctx, "</div>", 6);
    }

    n = snprintf(line, sizeof(line),
                 "</div></main><footer class=\"footer\"><p>© 2026 zhaozzz.top</p>"
                 "<p><span id=\"run-days-footer\">已运行 0 天 00 时 00 分 00 秒</span></p>"
                 "<p style=\"margin:0.35rem 0 0;font-size:0.8rem;color:var(--text-muted);\">累积访问人次：%lld</p></footer>"
                 "<script>(function(){ var startSec=%ld; function pad(n){ return n<10?'0'+n:n; } function update(){ var now=Math.floor(Date.now()/1000); var d=Math.floor((now-startSec)/86400); var h=pad(Math.floor((now-startSec)%%86400/3600)); var m=pad(Math.floor((now-startSec)%%3600/60)); var s=pad((now-startSec)%%60); var el=document.getElementById('run-days-footer'); if(el) el.textContent='已运行 '+d+' 天 '+h+' 时 '+m+' 分 '+s+' 秒'; } update(); setInterval(update,1000); })();</script>"
                 "<script>(function(){ var btn=document.getElementById('blogMenuBtn'); var panel=document.getElementById('blogMenuPanel'); if(btn&&panel){ btn.addEventListener('click',function(e){ e.stopPropagation(); panel.classList.toggle('open'); btn.setAttribute('aria-expanded',panel.classList.contains('open')); }); document.addEventListener('click',function(){ panel.classList.remove('open'); btn.setAttribute('aria-expanded','false'); }); } })();</script></body></html>",
                 (long long)metrics_get_total_visits(), (long)metrics_get_server_start_time());
    conn_send(ctx, line, (size_t)n);

    db_free_posts(posts, count);
}

/**
 * handle_post_detail — 文章详情页 GET /blog/post?id=...
 *
 * 功能：根据 id 取文章与评论；正文以 Markdown 原文通过 JSON 传给前端，由 marked.js + KaTeX 渲染；
 *       评论区展示 post_id/parent_id，可发表评论或回复。
 *
 * @param ctx    连接上下文
 * @param db    博客库
 * @param query  查询字符串（含 id）
 * @param headers 请求头（用于 user_session）
 * 返回值：无
 */
void handle_post_detail(conn_ctx_t *ctx, sqlite3 *db, const char *query, const char *headers) {
    char line[1024];
    int n;
    char *id_str = query_get(query, "id");
    if (!id_str) {
        send_404(ctx);
        return;
    }
    int id = atoi(id_str);
    free(id_str);
    if (id <= 0) {
        send_404(ctx);
        return;
    }

    Post post;
    memset(&post, 0, sizeof(Post));
    if (db_get_post_by_id(db, id, &post) != SQLITE_OK) {
        send_404(ctx);
        return;
    }

    Comment **comments = NULL;
    int comment_count = 0;
    db_get_comments_by_post(db, id, &comments, &comment_count);

    char *title_esc = html_escape(post.title);
    // Markdown 原文不做 HTML 转义，交由客户端的 marked.js 渲染，
    // 通过 <script> 中 JSON.stringify 进行安全转义

    send_response_header(ctx, 200, "OK", "text/html");

    const char *head1 =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>";
    conn_send(ctx, head1, strlen(head1));
    conn_send(ctx, title_esc, strlen(title_esc));
    char user_sid[65] = {0};
    int user_id = 0;
    char user_name[65] = {0};
    int user_logged_in = headers && auth_get_user_session_from_cookie(headers, user_sid, sizeof(user_sid))
                        && auth_user_session_valid(user_sid, &user_id, user_name, sizeof(user_name));

    const char *head2 =
        "</title>"
        "<script src=\"https://cdn.jsdelivr.net/npm/marked@9.1.6/marked.min.js\"></script>"
        "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.css\">"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/katex.min.js\"></script>"
        "<script defer src=\"https://cdn.jsdelivr.net/npm/katex@0.16.9/dist/contrib/auto-render.min.js\"></script>"
        "<style>"
        ":root{--primary:#2563eb;--text:#1a1a2e;--text-muted:#64748b;--accent:#2563eb;--border:#e2e8f0;--bg:#f8fafc;--card:#fff;--chip-bg:rgba(255,255,255,0.78);}"
        "body{font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);line-height:1.7;display:flex;flex-direction:column;min-height:100vh;}"
        ".header-bar{position:fixed;top:0;left:0;right:0;z-index:50;background:var(--header-bg);color:#fff;padding:0.6rem 1.25rem;display:flex;align-items:center;gap:1rem;box-shadow:0 1px 3px rgba(0,0,0,0.1);}"
        ".header-bar a{color:#fff;text-decoration:none;font-weight:500;}"
        ".menu-dropdown{position:absolute;top:100%;left:0;background:#1e293b;min-width:180px;box-shadow:0 4px 14px rgba(0,0,0,0.2);z-index:100;display:none;padding:0.5rem;border-radius:6px;}"
        ".menu-dropdown.open{display:block !important;}"
        ".modal{position:fixed;inset:0;z-index:200;display:none;align-items:center;justify-content:center;}"
        ".modal.show{display:flex !important;}"
        ".header{position:sticky;top:0;z-index:1000;height:60px;display:flex;align-items:center;justify-content:space-between;padding:0 24px;background:rgba(255,255,255,0.28);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);box-shadow:0 1px 0 rgba(0,0,0,0.05);}"
        ".header > a{flex-shrink:0;display:inline-block;color:var(--text);text-decoration:none;font-weight:600;font-size:18px;padding:4px 10px;border-radius:999px;background:var(--chip-bg);} .header > a:hover{color:var(--primary);}"
        ".nav{display:flex;align-items:center;}"
        ".nav > a{display:inline-block;margin-left:24px;color:var(--text);text-decoration:none;font-size:14px;font-weight:500;line-height:1.6;padding:4px 10px;border-radius:999px;background:var(--chip-bg);vertical-align:middle;} .nav > a:hover{color:var(--primary);}"
        ".blog-menu-bar{display:flex;justify-content:flex-start;padding:6px 24px 0;box-sizing:border-box;width:100%%;}"
        "main{flex:1;max-width:960px;width:100%;margin:0 auto;padding:0;background:var(--card);box-shadow:0 1px 3px rgba(0,0,0,0.06);}"
        ".post-body{max-width:960px;margin:24px auto 16px;padding:1.75rem 1.5rem;background:var(--card);border-radius:24px;box-shadow:0 18px 45px rgba(15,23,42,0.12);border:1px solid var(--border);}"
        ".post-body img{max-width:100%;height:auto;display:block;margin:0.85rem auto;border-radius:12px;box-shadow:0 10px 30px rgba(15,23,42,0.18);}"
        ".post-title{margin:0 0 0.5rem;font-size:2.25rem;font-weight:700;line-height:1.3;font-family:-apple-system,sans-serif;text-align:center;display:block;}"
        ".meta{color:var(--text-muted);font-size:0.9rem;margin-bottom:1.5rem;text-align:center;}"
        "#post-content{font-size:1rem;line-height:1.8;color:var(--text);text-align:left;}"
        "#post-content h1,#post-content h2,#post-content h3,#post-content h4,#post-content h5,#post-content h6{margin:1.6rem 0 0.75rem;font-weight:600;line-height:1.4;}"
        "#post-content h1{font-size:1.7rem;}"
        "#post-content h2{font-size:1.5rem;}"
        "#post-content h3{font-size:1.3rem;}"
        "#post-content p{margin:0.5rem 0;}"
        "#post-content code{font-family:SFMono-Regular,Menlo,Monaco,Consolas,monospace;font-size:0.9em;background:rgba(15,23,42,0.04);padding:0.1rem 0.35rem;border-radius:4px;}"
        "#post-content pre{background:#0b1120;color:#e5e7eb;padding:0.85rem 1rem;border-radius:10px;overflow:auto;font-size:0.9rem;}"
        "#post-content pre code{background:transparent;padding:0;}"
        "#post-content blockquote{margin:0.9rem 0;padding:0.5rem 0.9rem;border-left:4px solid var(--accent);background:rgba(37,99,235,0.04);color:var(--text-muted);border-radius:6px;}"
        "#post-content ul,#post-content ol{margin:0.5rem 0 0.5rem 1.3rem;}"
        "#post-content li{margin:0.2rem 0;}"
        "#post-content table{border-collapse:collapse;width:100%;margin:0.9rem 0;font-size:0.95rem;}"
        "#post-content th,#post-content td{border:1px solid var(--border);padding:0.4rem 0.6rem;text-align:left;}"
        "#post-content th{background:rgba(148,163,184,0.15);}"
        "#post-content img{max-width:100%;height:auto;display:block;margin:0.85rem auto;border-radius:12px;box-shadow:0 10px 30px rgba(15,23,42,0.18);}"
        ".comment-meta{color:var(--text-muted);font-size:0.8rem;margin-bottom:0.25rem;}"
        ".comment-content{display:block;margin:0.4rem 0;white-space:pre-wrap;word-break:break-word;}"
        ".comment.reply .comment-meta{font-size:0.7rem;}"
        ".comment.reply .comment-nick{font-size:0.9rem;}"
        ".replies-box{margin-top:0.35rem;margin-left:0.5rem;padding-left:0.75rem;border-left:2px solid var(--border);}"
        ".reply-btn,.reply-btn-no-login{font-size:0.85rem;color:var(--accent);background:none;border:none;cursor:pointer;padding:0;} .reply-form-wrap{margin-top:0.5rem;display:none;}"
        ".toast-overlay{position:fixed;inset:0;display:none;align-items:center;justify-content:center;background:rgba(15,23,42,0.35);z-index:1200;}"
        ".toast-box{min-width:220px;max-width:280px;padding:0.85rem 1rem;border-radius:16px;background:rgba(255,255,255,0.96);box-shadow:0 18px 40px rgba(15,23,42,0.35);color:var(--text);font-size:0.95rem;text-align:center;}"
        ".comments{max-width:820px;margin:0 auto 24px;padding:0 1.5rem 1.75rem;}"
        ".comments h3{margin:1.5rem 0 0.75rem;font-size:1.1rem;font-weight:600;}"
        "textarea,input[type=text],input[type=password]{width:100%;padding:0.6rem;font-size:1rem;box-sizing:border-box;border:1px solid var(--border);border-radius:10px;}"
        "button{margin-top:0.5rem;padding:0.5rem 1.1rem;border:none;background:var(--accent);color:#fff;border-radius:10px;cursor:pointer;} button:hover{opacity:0.9;}"
        ".footer{text-align:center;padding:24px;color:var(--text-muted);font-size:14px;background:rgba(255,255,255,0.25);backdrop-filter:blur(10px);border-top:1px solid rgba(255,255,255,0.2);}"
        ".footer a{display:inline-block;color:var(--text-muted);text-decoration:none;font-weight:500;padding:4px 10px;border-radius:999px;background:var(--chip-bg);} .footer a:hover{color:var(--primary);}"
        ".footer p span{display:inline-block;padding:4px 10px;border-radius:999px;background:var(--chip-bg);}"
        "@media(prefers-color-scheme:dark){:root{--text:#e2e8f0;--text-muted:#94a3b8;--primary:#60a5fa;--chip-bg:rgba(15,23,42,0.78);--bg:#020617;--card:rgba(15,23,42,0.95);--border:rgba(148,163,184,0.35);--accent:#60a5fa;} .header{background:rgba(15,23,42,0.35);box-shadow:0 1px 0 rgba(255,255,255,0.06);} .footer{background:rgba(15,23,42,0.3);} main{background:var(--card);} .post-body{background:var(--card);border-color:var(--border);box-shadow:0 18px 45px rgba(0,0,0,0.5);} .toast-box{background:rgba(15,23,42,0.96);border:1px solid var(--border);} #post-content code{background:rgba(148,163,184,0.2);} #post-content blockquote{background:rgba(96,165,250,0.1);border-left-color:var(--accent);} #post-content th{background:rgba(148,163,184,0.25);} textarea,input[type=text],input[type=password]{background:rgba(15,23,42,0.6);border-color:var(--border);color:var(--text);}}"
        "@media(max-width:768px){.nav > a{margin-left:16px;font-size:13px;padding:3px 8px;} .blog-menu-bar{padding:4px 16px 0;}}"
        "@media(max-width:600px){.post-body{margin:16px 12px 12px;padding:1.25rem;} .post-title{font-size:1.5rem;text-align:center;}}"
        "</style></head><body>"
        "<header class=\"header\"><a href=\"/\">zhaozzz.top</a><nav class=\"nav\"><a href=\"/\">首页</a><a href=\"/blog\">博客</a><a href=\"/about\">关于</a></nav></header>"
        "<div class=\"blog-menu-bar\"></div>";
    conn_send(ctx, head2, strlen(head2));
    const char *nav_tail_detail = "</div></div></div><main>";
    conn_send(ctx, nav_tail_detail, strlen(nav_tail_detail));

    // 标题与时间（包在 post-body 内）
    conn_send(ctx, "<div class=\"post-body\">", 22);
    char created_buf[64];
    char updated_buf[64];
    struct tm *tm_created = localtime(&post.created_at);
    if (tm_created) {
        strftime(created_buf, sizeof(created_buf), "%Y-%m-%d %H:%M:%S", tm_created);
    } else {
        snprintf(created_buf, sizeof(created_buf), "未知时间");
    }
    int show_updated = (post.updated_at > post.created_at);
    if (show_updated) {
        struct tm *tm_updated = localtime(&post.updated_at);
        if (tm_updated) {
            strftime(updated_buf, sizeof(updated_buf), "%Y-%m-%d %H:%M:%S", tm_updated);
        } else {
            snprintf(updated_buf, sizeof(updated_buf), "未知时间");
        }
    } else {
        updated_buf[0] = '\0';
    }
    char meta_buf[160];
    if (show_updated) {
        snprintf(meta_buf, sizeof(meta_buf), "哲 创建于 %s · 最后修改 %s", created_buf, updated_buf);
    } else {
        snprintf(meta_buf, sizeof(meta_buf), "哲 创建于 %s", created_buf);
    }
    n = snprintf(line, sizeof(line),
                     "<h2 class=\"post-title\" style=\"text-align:center;\">%s</h2>"
                     "<div class=\"meta\">%s</div>"
                     "<article id=\"post-content\"></article>",
                     title_esc, meta_buf);
    conn_send(ctx, line, (size_t)n);

    // 评论列表（树形：顶级 + 回复，id 用于跳转锚点）
    conn_send(ctx, "</div>", 6);
    const char *comments_head = "<section class=\"comments\"><h3>评论</h3>";
    conn_send(ctx, comments_head, strlen(comments_head));

    for (int i = 0; i < comment_count; ++i) {
        Comment *c = comments[i];
        if (c->parent_id != 0) continue;
        char *nick_esc = html_escape(c->nickname);
        char *ip_esc = html_escape(c->ip);
        char *content_esc = html_escape(c->content);
        char ctime_buf[64];
        struct tm *ctm_ptr = localtime(&c->created_at);
        if (ctm_ptr) strftime(ctime_buf, sizeof(ctime_buf), "%Y-%m-%d %H:%M:%S", ctm_ptr);
        else snprintf(ctime_buf, sizeof(ctime_buf), "未知时间");
        n = snprintf(line, sizeof(line),
                     "<div class=\"comment\" id=\"comment-%d\">"
                     "<div class=\"comment-meta\"><span class=\"comment-nick\">%s</span><span class=\"comment-meta-extra\"> · %s · IP: %s</span></div>"
                     "<div class=\"comment-content\">%s</div>",
                     c->id, nick_esc, ctime_buf, ip_esc ? ip_esc : "", content_esc);
        conn_send(ctx, line, (size_t)n);
        free(ip_esc);
        if (user_logged_in) {
            n = snprintf(line, sizeof(line),
                         "<button type=\"button\" class=\"reply-btn\" data-reply-to=\"%d\">回复</button>"
                         "<div class=\"reply-form-wrap\" id=\"reply-wrap-%d\">"
                         "<form method=\"POST\" action=\"/blog/comment\" class=\"reply-form\">"
                         "<input type=\"hidden\" name=\"post_id\" value=\"%d\"><input type=\"hidden\" name=\"reply_to_id\" value=\"%d\">"
                         "<textarea name=\"content\" required placeholder=\"请输入评论内容\"></textarea><br>"
                         "<button type=\"submit\">发送回复</button></form></div>",
                         c->id, c->id, id, c->id);
            conn_send(ctx, line, (size_t)n);
        } else {
            n = snprintf(line, sizeof(line), "<button type=\"button\" class=\"reply-btn-no-login\">回复</button>");
            conn_send(ctx, line, (size_t)n);
        }
        conn_send(ctx, "</div>", 5);
        int reply_count = 0;
        for (int j = 0; j < comment_count; ++j)
            if (comments[j]->parent_id == c->id) reply_count++;
        if (reply_count > 0) {
            n = snprintf(line, sizeof(line), "<div class=\"replies-box\" id=\"replies-%d\">", c->id);
            conn_send(ctx, line, (size_t)n);
            for (int j = 0; j < comment_count; ++j) {
                Comment *r = comments[j];
                if (r->parent_id != c->id) continue;
                char *rnick = html_escape(r->nickname);
                char *rip = html_escape(r->ip);
                char *rcont = html_escape(r->content);
                char rtime[64];
                struct tm *rtm = localtime(&r->created_at);
                if (rtm) strftime(rtime, sizeof(rtime), "%Y-%m-%d %H:%M:%S", rtm);
                else snprintf(rtime, sizeof(rtime), "未知时间");
                n = snprintf(line, sizeof(line),
                             "<div class=\"comment reply reply-item\" id=\"comment-%d\">"
                             "<div class=\"comment-meta\"><span class=\"comment-nick\">%s</span><span class=\"comment-meta-extra\"> · %s · IP: %s</span></div>"
                             "<div class=\"comment-content\">%s</div></div>",
                             r->id, rnick, rtime, rip ? rip : "", rcont);
                conn_send(ctx, line, (size_t)n);
                free(rnick); free(rip); free(rcont);
            }
            conn_send(ctx, "</div>", 6);
        }
        free(nick_esc); free(content_esc);
    }

    conn_send(ctx, "</section>", 10);

    if (user_logged_in) {
        n = snprintf(line, sizeof(line),
                     "<section class=\"comments\"><h3>发表评论</h3>"
                     "<form method=\"POST\" action=\"/blog/comment\" id=\"comment-form\">"
                     "<input type=\"hidden\" name=\"post_id\" value=\"%d\">"
                     "<label for=\"comment-content\">评论内容：</label><br>"
                     "<textarea id=\"comment-content\" name=\"content\" required placeholder=\"请输入评论内容\"></textarea><br>"
                     "<button type=\"submit\">提交评论</button>"
                     "</form></section>",
                     id);
        conn_send(ctx, line, (size_t)n);
    } else {
        const char *comment_btn_section = "<section class=\"comments\"><h3>发表评论</h3><button type=\"button\" id=\"btnCommentNoLogin\">发表评论</button></section>";
        conn_send(ctx, comment_btn_section, strlen(comment_btn_section));
        const char *toast_html = "<div id=\"toastOverlay\" class=\"toast-overlay\"><div class=\"toast-box\">请点击左上角菜单登录或者注册</div></div>";
        conn_send(ctx, toast_html, strlen(toast_html));
        const char *toast_script = "<script>document.getElementById('btnCommentNoLogin').onclick=function(){ var t=document.getElementById('toastOverlay'); if(t) t.style.setProperty('display','flex','important'); }; document.getElementById('toastOverlay').onclick=function(){ this.style.display='none'; }; document.querySelectorAll('.reply-btn-no-login').forEach(function(btn){ btn.onclick=function(){ var t=document.getElementById('toastOverlay'); if(t) t.style.setProperty('display','flex','important'); }; });</script>";
        conn_send(ctx, toast_script, strlen(toast_script));
    }

    const char *post_script =
        "<script>"
        "document.querySelectorAll('.reply-btn').forEach(function(btn){ btn.onclick=function(){ var wrap=document.getElementById('reply-wrap-'+btn.getAttribute('data-reply-to')); if(wrap) wrap.style.display=wrap.style.display==='block'?'none':'block'; }; });"
        "function scrollToComment(){ if(location.hash&&location.hash.indexOf('comment-')!==-1){ var el=document.getElementById(location.hash.slice(1)); if(el) el.scrollIntoView({behavior:'smooth',block:'start'}); } };"
        "if(document.readyState==='loading'){ document.addEventListener('DOMContentLoaded',function(){ setTimeout(scrollToComment,80); }); } else { setTimeout(scrollToComment,80); };"
        "if(location.hash==='#login'){ location.href='/blog/login?redir='+encodeURIComponent(location.pathname+location.search); }"
        "</script>";
    conn_send(ctx, post_script, strlen(post_script));

    // 在页面底部注入脚本，将 Markdown 渲染为 HTML
    const char *script1 =
        "<script>"
        "const rawMd = ";
    conn_send(ctx, script1, strlen(script1));

    // 用 JSON.stringify 的方式输出 Markdown 字符串
    // 简单实现：把换行和引号等做转义
    const char *md = post.content_md ? post.content_md : "";
    conn_send(ctx, "\"", 1);
    for (const char *s = md; *s; ++s) {
        char ch = *s;
        switch (ch) {
            case '\\':
                conn_send(ctx, "\\\\", 2);
                break;
            case '\"':
                conn_send(ctx, "\\\"", 2);
                break;
            case '<':
                conn_send(ctx, "\\x3C", 4);
                break;
            case '\n':
                conn_send(ctx, "\\n", 2);
                break;
            case '\r':
                /* 跳过 CR，统一用 \n */
                break;
            default:
                conn_send(ctx, &ch, 1);
                break;
        }
    }
    conn_send(ctx, "\";", 2);

    const char *script2 =
        "const html = marked.parse(rawMd);"
        "window.addEventListener('load',function(){"
        "  const pc = document.getElementById('post-content');"
        "  if(!pc) return;"
        "  pc.innerHTML = html;"
        "  if(window.renderMathInElement){"
        "    try{ renderMathInElement(pc,{"
        "      delimiters:["
        "        {left:\"$$\",right:\"$$\",display:true},"
        "        {left:\"\\\\[\",right:\"\\\\]\",display:true},"
        "        {left:\"$\",right:\"$\",display:false},"
        "        {left:\"\\\\(\",right:\"\\\\)\",display:false}"
        "      ],"
        "      throwOnError:false"
        "    }); }catch(e){}"
        "  }"
        "});"
        "</script>";
    conn_send(ctx, script2, strlen(script2));

    const char *footer =
        "<footer class=\"footer\">"
        "<p>© 2026 zhaozzz.top</p>"
        "<p><span id=\"run-days-footer\">已运行 0 天 00 时 00 分 00 秒</span></p>"
        "</footer>"
        "<script>(function(){ var startSec="
        ;
    conn_send(ctx, footer, strlen(footer));

    char buf[640];
    n = snprintf(buf, sizeof(buf),
                 "%ld; function pad(n){ return n<10?'0'+n:n; } function update(){ var now=Math.floor(Date.now()/1000); var d=Math.floor((now-startSec)/86400); var h=pad(Math.floor((now-startSec)%%86400/3600)); var m=pad(Math.floor((now-startSec)%%3600/60)); var s=pad((now-startSec)%%60); var el=document.getElementById('run-days-footer'); if(el) el.textContent='已运行 '+d+' 天 '+h+' 时 '+m+' 分 '+s+' 秒'; } update(); setInterval(update,1000); })();</script></body></html>",
                 (long)metrics_get_server_start_time());
    conn_send(ctx, buf, (size_t)n);

    free(title_esc);
    db_free_comments(comments, comment_count);
    free(post.title);
    free(post.content_md);
}

