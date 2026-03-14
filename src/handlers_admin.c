/**
 * handlers_admin.c — 后台管理页面与操作
 *
 * - 列表页 /admin
 * - 新建 / 编辑文章表单
 * - 创建 / 更新 / 删除文章
 * - 评论管理与删除
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "conn.h"
#include "http.h"
#include "db.h"
#include "handlers.h"

/* Markdown 正文最大体积（按 UTF-8 字节近似）：512 KiB */
#define STR_HELPER(x) #x
#define STRINGIFY(x) STR_HELPER(x)
#define CONTENT_MD_MAX_BYTES (512 * 1024)

/** 博文有变动时写入 data/last_blog_change，供定时备份调度器判断是否恢复为每日备份 */
static void admin_touch_last_blog_change(void) {
    const char *data_dir = getenv("BLOG_DATA_DIR");
    if (!data_dir || !data_dir[0]) data_dir = "data";
    char path[512];
    int n = snprintf(path, sizeof(path), "%s/last_blog_change", data_dir);
    if (n <= 0 || n >= (int)sizeof(path)) return;
    FILE *f = fopen(path, "w");
    if (f) {
        time_t t = time(NULL);
        fprintf(f, "%ld\n", (long)t);
        fclose(f);
    }
}

/**
 * handle_admin_index — 后台文章列表页 GET /blog/admin
 *
 * 功能：已鉴权；分页（page、每页 10 条）、圆角卡片、编辑/删除、点击卡片跳转详情。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param query 查询字符串（含 page）
 * 返回值：无
 */
// 后台：文章列表与入口（已通过 session 鉴权，无需 token），采用圆角卡片布局
// 支持 page 查询参数，每页最多 10 条。
void handle_admin_index(conn_ctx_t *ctx, sqlite3 *db, const char *query) {
    Post **posts = NULL;
    int count = 0;
    if (db_get_all_posts(db, &posts, &count) != SQLITE_OK) {
        send_500(ctx, "无法读取文章列表");
        return;
    }

    int page = 1;
    int per_page = 10;
    int total = count;
    int total_pages = (total + per_page - 1) / per_page;
    if (total_pages < 1) total_pages = 1;
    if (query) {
        char *page_str = query_get(query, "page");
        if (page_str) {
            page = atoi(page_str);
            free(page_str);
            if (page < 1) page = 1;
            if (page > total_pages) page = total_pages;
        }
    }
    int start = (page - 1) * per_page;
    int end = start + per_page;
    if (start < 0) start = 0;
    if (end > count) end = count;

    send_response_header(ctx, 200, "OK", "text/html");

    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>博客后台管理</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#2563eb;--danger:#dc2626;--shadow:0 18px 40px rgba(15,23,42,0.08);}"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);overflow-y:auto;}"
        "header{padding:1.75rem 1rem 0.75rem;text-align:center;}"
        "header h1{margin:0;font-size:1.6rem;font-weight:600;letter-spacing:0.05em;}"
        "main{width:100%;max-width:100%;margin:0 auto 2rem;padding:0 32px;box-sizing:border-box;}"
        ".admin-shell{max-width:960px;margin:0.75rem auto 0;border-radius:22px;background:var(--card-bg);border:1px solid var(--card-border);box-shadow:var(--shadow);padding:1rem 1.25rem;box-sizing:border-box;}"
        ".admin-title{margin:0 0 0.75rem;text-align:center;font-size:1.2rem;font-weight:600;color:var(--text);}"
        ".top-bar{display:flex;flex-wrap:wrap;align-items:center;justify-content:center;margin-bottom:0.9rem;gap:10px;}"
        "a.btn,button.btn{display:inline-block;padding:0.3rem 0.85rem;font-size:0.85rem;line-height:1.4;border-radius:999px;border:1px solid var(--card-border);cursor:pointer;text-decoration:none;font-weight:500;background:#f9fafb;color:var(--text);transition:background 0.15s ease,color 0.15s ease,box-shadow 0.15s ease,border-color 0.15s ease;}"
        "a.btn:hover,button.btn:hover{background:#e5effe;border-color:#93c5fd;box-shadow:0 4px 12px rgba(59,130,246,0.25);}"
        "a.btn-primary,button.btn-primary{background:var(--accent);color:#f9fafb;border-color:var(--accent);box-shadow:0 8px 20px rgba(37,99,235,0.45);}"
        "a.btn-primary:hover,button.btn-primary:hover{filter:brightness(0.97);}"
        ".post-list{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;margin-top:0.5rem;}"
        ".post-card{background:var(--card-bg);border-radius:16px;padding:14px 16px 12px;border:1px solid var(--card-border);box-shadow:0 10px 25px rgba(15,23,42,0.08);cursor:pointer;text-align:center;}"
        ".post-card-header{margin-bottom:10px;}"
        ".post-title{font-size:1.05rem;font-weight:500;margin:0 0 4px;}"
        ".post-meta{font-size:0.8rem;color:var(--text-muted);margin:0;}"
        ".post-meta-sub{font-size:0.74rem;color:var(--text-muted);margin:2px 0 0;}"
        ".post-actions{margin-top:8px;display:flex;flex-wrap:wrap;gap:10px;justify-content:flex-end;}"
        ".post-id-chip{display:none;}"
        ".pager{margin-top:18px;display:flex;justify-content:center;gap:8px;font-size:0.85rem;color:var(--text-muted);}"
        ".pager a{color:var(--accent);text-decoration:none;padding:0.25rem 0.6rem;border-radius:999px;border:1px solid #cbd5f5;background:rgba(255,255,255,0.9);}"
        ".pager a:hover{background:#eff6ff;}"
        ".pager .current{padding:0.25rem 0.6rem;border-radius:999px;background:#1d4ed8;color:#f9fafb;border:1px solid #1d4ed8;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#60a5fa;--danger:#f97373;--shadow:0 20px 50px rgba(0,0,0,0.85);}"
        ".admin-shell{background:var(--card-bg);border-color:var(--card-border);}"
        ".post-card{background:rgba(15,23,42,0.92);border-color:var(--card-border);box-shadow:0 10px 25px rgba(0,0,0,0.7);}"
        "a.btn,button.btn{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        "a.btn-primary,button.btn-primary{background:var(--accent);color:#f9fafb;border-color:var(--accent);}"
        "a.btn-primary:hover,button.btn-primary:hover{filter:brightness(0.97);}"
        ".pager a{background:rgba(15,23,42,0.9);border-color:var(--card-border);}"
        "}"
        "@media(max-width:800px){.post-list{grid-template-columns:1fr;}.admin-shell{border-radius:18px;padding:0.9rem 0.75rem;}}"
        "@media(max-width:600px){main{padding:0 10px;}}"
        "</style>"
        "</head><body>"
        "<header><h1>博客后台管理</h1></header>"
        "<main>"
        "<section class=\"admin-shell\">"
        "<h2 class=\"admin-title\">文章列表</h2>";
    conn_send(ctx, head, strlen(head));

    char line[1024];
    int n = snprintf(line, sizeof(line),
                     "<div class=\"top-bar\">"
                     "<a class=\"btn btn-secondary\" href=\"/\">返回首页</a>"
                     "<a class=\"btn btn-secondary\" href=\"/blog\">前台博客</a>"
                     "<a class=\"btn btn-primary\" href=\"/blog/admin/post/new\">新建文章</a>"
                     "</div>");
    conn_send(ctx, line, (size_t)n);

    if (count == 0) {
        const char *empty_html =
            "<div class=\"post-list\">"
            "<div class=\"post-card\" style=\"background:rgba(15,23,42,0.92);\">"
            "<p class=\"post-title\">暂无文章</p>"
            "<p class=\"post-meta\">点击右上角「新建文章」开始写作。</p>"
            "</div></div>";
        conn_send(ctx, empty_html, strlen(empty_html));
    } else {
        const char *list_open = "<div class=\"post-list\">";
        conn_send(ctx, list_open, strlen(list_open));
        for (int i = start; i < end; ++i) {
            Post *p = posts[i];
            char *title_esc = html_escape(p->title ? p->title : "(无标题)");
            char created_buf[64];
            char updated_buf[64];
            struct tm *tm_created = localtime(&p->created_at);
            if (tm_created) {
                strftime(created_buf, sizeof(created_buf), "%Y-%m-%d %H:%M:%S", tm_created);
            } else {
                snprintf(created_buf, sizeof(created_buf), "未知时间");
            }
            int show_updated = (p->updated_at > p->created_at);
            if (show_updated) {
                struct tm *tm_updated = localtime(&p->updated_at);
                if (tm_updated) {
                    strftime(updated_buf, sizeof(updated_buf), "%Y-%m-%d %H:%M:%S", tm_updated);
                } else {
                    snprintf(updated_buf, sizeof(updated_buf), "未知时间");
                }
            } else {
                updated_buf[0] = '\0';
            }

            /* 文章卡片外层 */
            n = snprintf(line, sizeof(line),
                         "<article class=\"post-card\" onclick=\"location.href='/blog/post?id=%d'\">",
                         p->id);
            if (n > 0 && n < (int)sizeof(line)) conn_send(ctx, line, (size_t)n);

            /* 标题 + 元信息（两行：修改时间 / 创建时间） */
            if (show_updated) {
                n = snprintf(line, sizeof(line),
                             "<header class=\"post-card-header\">"
                             "<div><p class=\"post-title\">%s</p>"
                             "<p class=\"post-meta\">修改时间 %s</p>"
                             "<p class=\"post-meta-sub\">ID %d · 创建时间 %s</p></div>"
                             "</header>",
                             title_esc ? title_esc : "(无标题)",
                             updated_buf, p->id, created_buf);
            } else {
                n = snprintf(line, sizeof(line),
                             "<header class=\"post-card-header\">"
                             "<div><p class=\"post-title\">%s</p>"
                             "<p class=\"post-meta\">ID %d · 创建时间 %s</p></div>"
                             "</header>",
                             title_esc ? title_esc : "(无标题)",
                             p->id, created_buf);
            }
            if (n > 0 && n < (int)sizeof(line)) conn_send(ctx, line, (size_t)n);

            /* 操作按钮：编辑 + 删除（阻止点击冒泡，避免触发卡片的跳转） */
            n = snprintf(line, sizeof(line),
                         "<div class=\"post-actions\" onclick=\"event.stopPropagation()\">"
                         "<a class=\"btn btn-secondary\" href=\"/blog/admin/post/edit?id=%d\">编辑</a>"
                         "<form method=\"POST\" action=\"/blog/admin/post/delete\" style=\"display:inline;\" "
                         "onsubmit=\"return confirm('确定要删除这篇文章及其评论吗？');\">"
                         "<input type=\"hidden\" name=\"id\" value=\"%d\">"
                         "<input type=\"hidden\" name=\"page\" value=\"%d\">"
                         "<button type=\"submit\" class=\"btn btn-danger\">删除</button>"
                         "</form>"
                         "</div>"
                         "</article>",
                         p->id, p->id, page);
            if (n > 0 && n < (int)sizeof(line)) conn_send(ctx, line, (size_t)n);

            free(title_esc);
        }
        conn_send(ctx, "</div>", 6);

        if (total_pages > 1) {
            char pager[512];
            int pn = snprintf(pager, sizeof(pager), "<nav class=\"pager\">");
            if (pn > 0 && pn < (int)sizeof(pager)) conn_send(ctx, pager, (size_t)pn);

            if (page > 1) {
                pn = snprintf(pager, sizeof(pager),
                              "<a href=\"/blog/admin?page=%d\">上一页</a>", page - 1);
                if (pn > 0 && pn < (int)sizeof(pager)) conn_send(ctx, pager, (size_t)pn);
            }

            pn = snprintf(pager, sizeof(pager),
                          "<span class=\"current\">第 %d / %d 页</span>", page, total_pages);
            if (pn > 0 && pn < (int)sizeof(pager)) conn_send(ctx, pager, (size_t)pn);

            if (page < total_pages) {
                pn = snprintf(pager, sizeof(pager),
                              "<a href=\"/blog/admin?page=%d\">下一页</a>", page + 1);
                if (pn > 0 && pn < (int)sizeof(pager)) conn_send(ctx, pager, (size_t)pn);
            }

            conn_send(ctx, "</nav>", 6);
        }
    }

    const char *tail = "</section></main></body></html>";
    conn_send(ctx, tail, strlen(tail));

    db_free_posts(posts, count);
}

/**
 * handle_admin_new_post — 新建文章表单页 GET /blog/admin/post/new
 *
 * @param ctx 连接上下文
 * 返回值：无
 */
// 后台：新建文章表单（已鉴权）
void handle_admin_new_post(conn_ctx_t *ctx) {
    send_response_header(ctx, 200, "OK", "text/html");

    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>新建文章 - 后台</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#2563eb;--shadow:0 18px 40px rgba(15,23,42,0.08);}"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);overflow-y:auto;}"
        "header{padding:1.75rem 1rem 0.75rem;text-align:center;}"
        "header h1{margin:0;font-size:1.6rem;font-weight:600;letter-spacing:0.05em;}"
        "main{width:100%;max-width:100%;margin:0 auto 2rem;padding:0 32px;box-sizing:border-box;}"
        ".progress-card,.editor-card,.preview-card{width:100%;margin:0.75rem 0 0;border-radius:22px;background:var(--card-bg);border:1px solid var(--card-border);box-shadow:var(--shadow);padding:1rem 1.25rem;box-sizing:border-box;}"
        ".progress-card{margin-top:0;text-align:center;}"
        ".progress-form-shell{max-width:520px;margin:0.75rem auto 0;text-align:left;}"
        ".editor-preview-row{width:100%;margin:0.75rem 0 0;display:flex;flex-wrap:wrap;gap:16px;box-sizing:border-box;}"
        ".editor-card,.preview-card{flex:1 1 0;min-width:0;margin:0;}"
        ".editor-card{order:2;}"
        ".preview-card{order:1;}"
        ".progress-header{font-size:1rem;font-weight:600;color:var(--text);margin-bottom:0.35rem;}"
        ".progress-meta{font-size:0.8rem;color:var(--text-muted);margin-bottom:0.55rem;}"
        ".progress-bar-shell{width:100%;height:10px;border-radius:999px;background:rgba(148,163,184,0.25);overflow:hidden;}"
        ".progress-bar-inner{height:100%;width:0;border-radius:999px;background:linear-gradient(90deg,#38bdf8,#4f46e5);transition:width 0.12s ease-out;}"
        ".editor-card>h2,.preview-card>h2{margin:0 0 0.75rem;font-size:1.05rem;font-weight:600;color:var(--text);text-align:center;}"
        ".preview-box{margin-top:0.25rem;border-radius:12px;padding:0.8rem 0.9rem;background:rgba(248,250,252,0.96);border:1px solid rgba(226,232,240,0.9);box-sizing:border-box;overflow-y:auto;}"
        ".field-label{font-size:0.9rem;margin:0.35rem 0 0.25rem;color:var(--text-muted);display:block;}"
        "input[type=text]{width:100%;padding:0.55rem 0.6rem;font-size:1rem;margin-bottom:0.6rem;border-radius:10px;border:1px solid var(--card-border);box-sizing:border-box;background:rgba(255,255,255,0.96);}"
        "textarea{width:100%;padding:0.8rem 0.9rem;font-size:0.95rem;font-family:Menlo,Monaco,Consolas,monospace;border-radius:12px;border:1px solid rgba(226,232,240,0.9);box-sizing:border-box;background:rgba(248,250,252,0.96);resize:none;}"
        ".form-actions{margin-top:0.75rem;display:flex;flex-wrap:wrap;justify-content:center;gap:8px;}"
        "button{padding:0.5rem 1.2rem;border:none;border-radius:999px;background:var(--accent);color:#f9fafb;font-size:0.95rem;font-weight:500;cursor:pointer;box-shadow:0 10px 30px rgba(37,99,235,0.55);}"
        "button:hover{filter:brightness(0.97);}"
        ".preview-empty{font-size:0.85rem;color:var(--text-muted);margin:0 0 0.5rem;}"
        ".preview-content{font-size:0.95rem;line-height:1.8;color:var(--text);}"
        "a{color:var(--accent);text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#60a5fa;--shadow:0 20px 50px rgba(0,0,0,0.85);}"
        "input[type=text]{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        ".preview-box,textarea{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        ".progress-bar-shell{background:rgba(30,64,175,0.55);}"
        "}"
        "@media(max-width:768px){"
        ".editor-preview-row{flex-direction:column;}"
        ".editor-card{order:1;}"
        ".preview-card{order:2;}"
        "}"
        "@media(max-width:640px){main{padding:0 10px;} .progress-card,.editor-card,.preview-card{margin-top:0.75rem;border-radius:18px;padding:0.9rem 0.75rem;}}"
        "</style>"
        "</head><body>"
        "<header><h1>新建文章</h1></header>"
        "<main>";
    conn_send(ctx, head, strlen(head));

    const char *body =
        "<form method=\"POST\" action=\"/blog/admin/post/create\">"
        "<section class=\"progress-card\">"
        "<div class=\"progress-header\">文章信息</div>"
        "<div class=\"progress-meta\" id=\"content-usage-text\">正文体积约 0.0 KB · 0% / 512 KB</div>"
        "<div class=\"progress-bar-shell\"><div class=\"progress-bar-inner\" id=\"content-usage-bar\"></div></div>"
        "<div class=\"progress-form-shell\">"
        "<p class=\"progress-meta\" style=\"text-align:center;margin-top:0.75rem;\"><a href=\"/blog/admin\">返回后台列表</a></p>"
        "<label class=\"field-label\">标题：</label>"
        "<input type=\"text\" name=\"title\" required>"
        "<div class=\"form-actions\"><button type=\"submit\">保存</button></div>"
        "</div>"
        "</section>"
        "<div class=\"editor-preview-row\">"
        "<section class=\"preview-card\" id=\"previewCard\"><h2>预览</h2>"
        "<div class=\"preview-box\" id=\"previewBox\">"
        "<div id=\"preview-empty\" class=\"preview-empty\">开始输入内容后，这里会显示 Markdown 渲染效果。</div>"
        "<article id=\"preview-content\" class=\"preview-content\" style=\"display:none;\"></article>"
        "</div>"
        "</section>"
        "<section class=\"editor-card\">"
        "<h2>正文（Markdown）：</h2>"
        "<textarea name=\"content_md\" required></textarea>"
        "</section>"
        "</div>"
        "</form>";
    conn_send(ctx, body, strlen(body));

    const char *tail =
        "<script src=\"https://cdn.jsdelivr.net/npm/marked@9.1.6/marked.min.js\"></script>"
        "<script>"
        "(function(){"
        "var MAX_BYTES=" STRINGIFY(CONTENT_MD_MAX_BYTES) ";"
        "function utf8Len(str){var bytes=0;for(var i=0;i<str.length;i++){var c=str.charCodeAt(i);"
        "if(c<128){bytes++;}else if(c<2048){bytes+=2;}else if(c>=0xd800&&c<=0xdbff){i++;bytes+=4;}else{bytes+=3;}}return bytes;}"
        "function update(){var ta=document.querySelector('textarea[name=\"content_md\"]');"
        "var bar=document.getElementById('content-usage-bar');"
        "var text=document.getElementById('content-usage-text');"
        "var preview=document.getElementById('preview-content');"
        "var empty=document.getElementById('preview-empty');"
        "var box=document.getElementById('previewBox');"
        "if(!ta)return;var v=ta.value||\"\";var len=utf8Len(v);"
        "if(len>MAX_BYTES){var low=0,high=v.length;while(low<high){var mid=((low+high+1)>>1);"
        "if(utf8Len(v.slice(0,mid))<=MAX_BYTES){low=mid;}else{high=mid-1;}}"
        "v=v.slice(0,low);ta.value=v;len=utf8Len(v);}"
        "var pct=Math.floor(len*100/MAX_BYTES);if(pct>100)pct=100;"
        "if(bar)bar.style.width=pct+'%';"
        "if(text){var kb=(len/1024).toFixed(1);text.textContent='正文体积约 '+kb+' KB · '+pct+'% / 512 KB';}"
        "if(preview){if(!v.trim()){preview.style.display='none';if(empty)empty.style.display='block';}"
        "else{if(typeof marked!=='undefined'){preview.innerHTML=marked.parse(v);}else{preview.textContent=v;}preview.style.display='block';if(empty)empty.style.display='none';}}"
        "if(box){var cardWidth=box.clientWidth||ta.clientWidth;var target=Math.max(Math.round(cardWidth*0.75),320);ta.style.height=target+'px';box.style.height=target+'px';}"
        "}"
        "document.addEventListener('input',function(e){if(e.target&&e.target.name==='content_md'){update();}});"
        "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',update);}else{update();}"
        "})();"
        "</script>"
        "</main></body></html>";
    conn_send(ctx, tail, strlen(tail));
}

/**
 * handle_admin_edit_post — 编辑文章页 GET /blog/admin/post/edit?id=...
 *
 * 功能：展示标题与 Markdown 正文表单、评论列表（含 post_id/parent_id）、删除评论按钮。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param query 查询字符串（含 id）
 * 返回值：无
 */
// 后台：编辑文章及评论管理（已鉴权）
void handle_admin_edit_post(conn_ctx_t *ctx, sqlite3 *db, const char *query) {
    if (!query) {
        send_404(ctx);
        return;
    }
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
    char *content_esc = html_escape(post.content_md ? post.content_md : "");

    send_response_header(ctx, 200, "OK", "text/html");

    const char *head =
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"color-scheme\" content=\"light dark\">"
        "<link rel=\"icon\" href=\"/blog/favicon.ico\" type=\"image/png\">"
        "<title>编辑文章 - 后台</title>"
        "<style>"
        ":root{--bg:#f1f5f9;--text:#0f172a;--text-muted:#64748b;--card-bg:#ffffff;--card-border:#e2e8f0;--accent:#2563eb;--danger:#dc2626;--shadow:0 18px 40px rgba(15,23,42,0.08);}"
        "body{font-family:-apple-system,BlinkMacSystemFont,Segoe UI,Roboto,Helvetica,Arial,sans-serif;margin:0;padding:0;background:var(--bg);color:var(--text);overflow-y:auto;}"
        "header{padding:1.75rem 1rem 0.75rem;text-align:center;}"
        "header h1{margin:0;font-size:1.6rem;font-weight:600;letter-spacing:0.05em;}"
        "main{width:100%;max-width:100%;margin:0 auto 2rem;padding:0 32px;box-sizing:border-box;}"
        ".progress-card,.editor-card,.comments-card,.preview-card{width:100%;margin:0.75rem 0 0;border-radius:22px;background:var(--card-bg);border:1px solid var(--card-border);box-shadow:var(--shadow);padding:1rem 1.25rem;box-sizing:border-box;}"
        ".progress-card{margin-top:0;text-align:center;}"
        ".progress-form-shell{max-width:520px;margin:0.75rem auto 0;text-align:left;}"
        ".editor-preview-row{width:100%;margin:0.75rem 0 0;display:flex;flex-wrap:wrap;gap:16px;box-sizing:border-box;}"
        ".editor-card,.preview-card{flex:1 1 0;min-width:0;margin:0;}"
        ".editor-card{order:2;}"
        ".preview-card{order:1;}"
        ".progress-header{font-size:1rem;font-weight:600;color:var(--text);margin-bottom:0.35rem;}"
        ".progress-meta{font-size:0.8rem;color:var(--text-muted);margin-bottom:0.55rem;}"
        ".progress-bar-shell{width:100%;height:10px;border-radius:999px;background:rgba(148,163,184,0.25);overflow:hidden;}"
        ".progress-bar-inner{height:100%;width:0;border-radius:999px;background:linear-gradient(90deg,#38bdf8,#4f46e5);transition:width 0.12s ease-out;}"
        ".editor-card>h2,.preview-card>h2{margin:0 0 0.75rem;font-size:1.05rem;font-weight:600;color:var(--text);text-align:center;}"
        ".preview-box{margin-top:0.25rem;border-radius:12px;padding:0.8rem 0.9rem;background:rgba(248,250,252,0.96);border:1px solid rgba(226,232,240,0.9);box-sizing:border-box;overflow-y:auto;}"
        ".field-label{font-size:0.9rem;margin:0.35rem 0 0.25rem;color:var(--text-muted);display:block;}"
        "input[type=text]{width:100%;padding:0.55rem 0.6rem;font-size:1rem;margin-bottom:0.6rem;border-radius:10px;border:1px solid var(--card-border);box-sizing:border-box;background:rgba(255,255,255,0.96);}"
        "textarea{width:100%;padding:0.8rem 0.9rem;font-size:0.95rem;font-family:Menlo,Monaco,Consolas,monospace;border-radius:12px;border:1px solid rgba(226,232,240,0.9);box-sizing:border-box;background:rgba(248,250,252,0.96);resize:none;}"
        ".form-actions{margin-top:0.75rem;display:flex;flex-wrap:wrap;justify-content:center;gap:8px;}"
        "button{padding:0.5rem 1.2rem;border:none;border-radius:999px;font-size:0.9rem;font-weight:500;cursor:pointer;}"
        ".btn-primary{background:var(--accent);color:#f9fafb;box-shadow:0 10px 30px rgba(37,99,235,0.55);}"
        ".btn-danger{background:var(--danger);color:#fee2e2;}"
        ".btn-primary:hover,.btn-danger:hover{filter:brightness(0.97);}"
        ".preview-empty{font-size:0.85rem;color:var(--text-muted);margin:0 0 0.5rem;}"
        ".comments{margin-top:0.75rem;}"
        ".comment{border-top:1px solid var(--card-border);padding:0.6rem 0;}"
        ".comment-card{margin:0.45rem 0;padding:0.55rem 0.65rem;border-radius:10px;background:rgba(248,250,252,0.98);border:1px solid var(--card-border);}"
        ".comment-meta{font-size:0.8rem;color:var(--text-muted);margin-bottom:0.2rem;}"
        "a{color:var(--accent);text-decoration:none;}"
        "a:hover{text-decoration:underline;}"
        ".comments{margin-top:0.75rem;}"
        ".site-footer{display:block;width:100%;text-align:center;padding:1rem;color:var(--text-muted);font-size:0.85rem;border-top:1px solid var(--card-border);background:var(--card-bg);}"
        ".site-footer p{margin:0;text-align:center;}"
        "@media(prefers-color-scheme:dark){"
        ":root{--bg:#020617;--text:#e2e8f0;--text-muted:#94a3b8;--card-bg:rgba(15,23,42,0.96);--card-border:rgba(51,65,85,0.9);--accent:#60a5fa;--danger:#f97373;--shadow:0 20px 50px rgba(0,0,0,0.85);}"
        "input[type=text]{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        ".preview-box,textarea{background:rgba(15,23,42,0.9);border-color:var(--card-border);color:var(--text);}"
        ".progress-bar-shell{background:rgba(30,64,175,0.55);}"
        ".comment{border-top-color:rgba(51,65,85,0.9);}"
        ".comment-card{background:rgba(15,23,42,0.9);border-color:var(--card-border);}"
        ".site-footer{background:rgba(15,23,42,0.96);border-top-color:rgba(51,65,85,0.9);}"
        "}"
        "@media(max-width:768px){"
        ".editor-preview-row{flex-direction:column;}"
        ".editor-card{order:1;}"
        ".preview-card{order:2;}"
        "}"
        "@media(max-width:640px){main{padding:0 10px;} .progress-card,.editor-card,.comments-card,.preview-card{margin-top:0.75rem;border-radius:18px;padding:0.9rem 0.75rem;}}"
        "</style>"
        "</head><body>"
        "<header><h1>编辑文章</h1></header>"
        "<main>";
    conn_send(ctx, head, strlen(head));

    char line[1024];

    /* 表单与进度卡片（与新建页布局一致） */
    int n = snprintf(line, sizeof(line),
                     "<form method=\"POST\" action=\"/blog/admin/post/update\">"
                     "<input type=\"hidden\" name=\"id\" value=\"%d\">"
                     "<section class=\"progress-card\">"
                     "<div class=\"progress-header\">文章信息</div>"
                     "<div class=\"progress-meta\" id=\"content-usage-text\">正文体积约 0.0 KB · 0%% / 512 KB</div>"
                     "<div class=\"progress-bar-shell\"><div class=\"progress-bar-inner\" id=\"content-usage-bar\"></div></div>"
                     "<div class=\"progress-form-shell\">",
                     id);
    if (n > 0 && (size_t)n < sizeof(line)) conn_send(ctx, line, (size_t)n);

    n = snprintf(line, sizeof(line),
                 "<p class=\"progress-meta\" style=\"text-align:center;margin-top:0.75rem;\"><a href=\"/blog/admin\">返回后台列表</a> | "
                 "<a href=\"/blog/post?id=%d\">查看前台展示</a></p>",
                 id);
    if (n > 0 && (size_t)n < sizeof(line)) conn_send(ctx, line, (size_t)n);

    n = snprintf(line, sizeof(line),
                 "<label class=\"field-label\">标题：</label>"
                 "<input type=\"text\" name=\"title\" required value=\"%s\">"
                 "<div class=\"form-actions\"><button type=\"submit\" class=\"btn-primary\">保存修改</button></div>"
                 "</div>"  /* progress-form-shell */
                 "</section>",
                 title_esc ? title_esc : "");
    if (n > 0 && (size_t)n < sizeof(line)) conn_send(ctx, line, (size_t)n);

    /* 左右预览 + 编辑区域（纯静态 HTML，一次发送） */
    const char *edit_body_tail =
        "<div class=\"editor-preview-row\">"
        "<section class=\"preview-card\" id=\"previewCard\"><h2>预览</h2>"
        "<div class=\"preview-box\" id=\"previewBox\">"
        "<div id=\"preview-empty\" class=\"preview-empty\">开始输入内容后，这里会显示 Markdown 渲染效果。</div>"
        "<article id=\"preview-content\" class=\"preview-content\" style=\"display:none;\"></article>"
        "</div>"
        "</section>"
        "<section class=\"editor-card\">"
        "<h2>正文（Markdown）：</h2>"
        "<textarea name=\"content_md\" required>";
    conn_send(ctx, edit_body_tail, strlen(edit_body_tail));
    // 正文可能很长，直接发送已转义内容，避免 snprintf 截断导致缺少 </textarea> 和保存按钮
    if (content_esc) {
        conn_send(ctx, content_esc, strlen(content_esc));
    }
    conn_send(ctx, "</textarea></section></div></form>", strlen("</textarea></section></div></form>"));

    const char *comments_head =
        "<section class=\"comments-card\">"
        "<div class=\"comments\">"
        "<h2>评论管理</h2>";
    conn_send(ctx, comments_head, strlen(comments_head));

    if (comment_count == 0) {
        const char *no_comment = "<p>暂无评论。</p>";
        conn_send(ctx, no_comment, strlen(no_comment));
    } else {
        for (int i = 0; i < comment_count; ++i) {
            Comment *c = comments[i];
            char *nick_esc = html_escape(c->nickname);
            char *ip_esc = html_escape(c->ip);
            char *content_c_esc = html_escape(c->content);

            char ctime_buf[64];
            struct tm *ctm_ptr = localtime(&c->created_at);
            if (ctm_ptr) strftime(ctime_buf, sizeof(ctime_buf), "%Y-%m-%d %H:%M:%S", ctm_ptr);
            else snprintf(ctime_buf, sizeof(ctime_buf), "未知时间");

            /* 顶级评论卡片：使用主题变量，显示评论 ID / 文章 ID / 父 ID */
            n = snprintf(line, sizeof(line),
                         "<article class=\"comment comment-card\">"
                         "<div class=\"comment-meta\">"
                         "<strong>%s</strong>"
                         "<span style=\"margin-left:0.35rem;font-size:0.8rem;\">%s · IP: %s</span>"
                         "<span style=\"float:right;font-size:0.75rem;\">"
                         "ID:%d · Post:%d · Parent:%d"
                         "</span>"
                         "</div>"
                         "<div class=\"comment-content\" style=\"font-size:0.9rem;\">%s</div>"
                         "<form method=\"POST\" action=\"/blog/admin/comment/delete\" "
                         "style=\"margin-top:0.25rem;\" "
                         "onsubmit=\"return confirm('确定要删除这条评论吗？');\">"
                         "<input type=\"hidden\" name=\"id\" value=\"%d\">"
                         "<input type=\"hidden\" name=\"post_id\" value=\"%d\">"
                         "<button type=\"submit\" class=\"btn-danger\">删除评论</button>"
                         "</form>"
                         "</article>",
                         nick_esc ? nick_esc : "",
                         ctime_buf,
                         ip_esc ? ip_esc : "",
                         c->id, c->post_id, c->parent_id,
                         content_c_esc ? content_c_esc : "",
                         c->id, id);
            conn_send(ctx, line, (size_t)n);

            free(nick_esc);
            free(ip_esc);
            free(content_c_esc);
        }
    }

    const char *footer = "<footer class=\"site-footer\"></footer>";
    conn_send(ctx, "</div></section>", 14);
    conn_send(ctx, footer, strlen(footer));
    {
        const char *tail =
            "<script src=\"https://cdn.jsdelivr.net/npm/marked@9.1.6/marked.min.js\"></script>"
            "<script>"
            "(function(){"
            "var MAX_BYTES=" STRINGIFY(CONTENT_MD_MAX_BYTES) ";"
            "function utf8Len(str){var bytes=0;for(var i=0;i<str.length;i++){var c=str.charCodeAt(i);"
            "if(c<128){bytes++;}else if(c<2048){bytes+=2;}else if(c>=0xd800&&c<=0xdbff){i++;bytes+=4;}else{bytes+=3;}}return bytes;}"
            "function update(){var ta=document.querySelector('textarea[name=\"content_md\"]');"
            "var bar=document.getElementById('content-usage-bar');"
            "var text=document.getElementById('content-usage-text');"
            "var preview=document.getElementById('preview-content');"
            "var empty=document.getElementById('preview-empty');"
            "var box=document.getElementById('previewBox');"
            "if(!ta)return;var v=ta.value||\"\";var len=utf8Len(v);"
            "if(len>MAX_BYTES){var low=0,high=v.length;while(low<high){var mid=((low+high+1)>>1);"
            "if(utf8Len(v.slice(0,mid))<=MAX_BYTES){low=mid;}else{high=mid-1;}}"
            "v=v.slice(0,low);ta.value=v;len=utf8Len(v);}"
            "var pct=Math.floor(len*100/MAX_BYTES);if(pct>100)pct=100;"
            "if(bar)bar.style.width=pct+'%';"
            "if(text){var kb=(len/1024).toFixed(1);text.textContent='正文体积约 '+kb+' KB · '+pct+'% / 512 KB';}"
            "if(preview){if(!v.trim()){preview.style.display='none';if(empty)empty.style.display='block';}"
            "else{if(typeof marked!=='undefined'){preview.innerHTML=marked.parse(v);}else{preview.textContent=v;}preview.style.display='block';if(empty)empty.style.display='none';}}"
            "if(box){var cardWidth=box.clientWidth||ta.clientWidth;var target=Math.max(Math.round(cardWidth*0.75),320);ta.style.height=target+'px';box.style.height=target+'px';}"
            "}"
            "document.addEventListener('input',function(e){if(e.target&&e.target.name==='content_md'){update();}});"
            "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',update);}else{update();}"
            "})();"
            "</script>";
        conn_send(ctx, tail, strlen(tail));
    }
    conn_send(ctx, "</main></body></html>", 22);

    free(title_esc);
    free(content_esc);
    db_free_comments(comments, comment_count);
    free(post.title);
    free(post.content_md);
}

/**
 * handle_admin_post_create — 创建文章 POST /blog/admin/post/create
 *
 * 功能：从 body 解析 title、content_md，插入文章后重定向到 /blog/admin，并触发备份。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param body 表单 body（application/x-www-form-urlencoded）
 * 返回值：无
 */
// 后台：创建 / 更新 / 删除文章，以及删除评论
void handle_admin_post_create(conn_ctx_t *ctx, sqlite3 *db, const char *body) {
    if (!body) {
        send_500(ctx, "缺少请求体");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_500(ctx, "内存不足");
        return;
    }
    memcpy(copy, body, len + 1);

    char *title = query_get(copy, "title");
    char *content_md = query_get(copy, "content_md");

    if (!title || !content_md || title[0] == '\0' || content_md[0] == '\0') {
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "标题或正文为空");
        return;
    }

    /* 服务器端再次限制正文体积，防止绕过前端校验。 */
    if (strlen(content_md) > CONTENT_MD_MAX_BYTES) {
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "正文超过 512KB 限制");
        return;
    }

    if (db_create_post(db, title, content_md, NULL) != SQLITE_OK) {
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "创建文章失败");
        return;
    }

    free(title);
    free(content_md);
    free(copy);

    admin_touch_last_blog_change();

    send_redirect(ctx, "/blog/admin");
}

/**
 * handle_admin_post_update — 更新文章 POST /blog/admin/post/update
 *
 * 功能：从 body 解析 id、title、content_md，更新后重定向到编辑页。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param body 表单 body
 * 返回值：无
 */
void handle_admin_post_update(conn_ctx_t *ctx, sqlite3 *db, const char *body) {
    if (!body) {
        send_500(ctx, "缺少请求体");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_500(ctx, "内存不足");
        return;
    }
    memcpy(copy, body, len + 1);

    char *id_str = query_get(copy, "id");
    char *title = query_get(copy, "title");
    char *content_md = query_get(copy, "content_md");

    if (!id_str || !title || !content_md) {
        free(id_str);
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "缺少必要字段");
        return;
    }

    int id = atoi(id_str);
    if (id <= 0) {
        free(id_str);
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "文章不存在");
        return;
    }

    if (strlen(content_md) > CONTENT_MD_MAX_BYTES) {
        free(id_str);
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "正文超过 512KB 限制");
        return;
    }

    if (db_update_post(db, id, title, content_md) != SQLITE_OK) {
        free(id_str);
        free(title);
        free(content_md);
        free(copy);
        send_500(ctx, "更新文章失败");
        return;
    }

    free(id_str);
    free(title);
    free(content_md);
    free(copy);

    admin_touch_last_blog_change();

    char location[128];
    snprintf(location, sizeof(location), "/blog/admin/post/edit?id=%d", id);
    send_redirect(ctx, location);
}

/**
 * handle_admin_post_delete — 删除文章 POST /blog/admin/post/delete
 *
 * 功能：从 body 解析 id，删除文章（级联删除评论）后重定向到 /blog/admin。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param body 表单 body（含 id）
 * 返回值：无
 */
void handle_admin_post_delete(conn_ctx_t *ctx, sqlite3 *db, const char *body) {
    if (!body) {
        send_500(ctx, "缺少请求体");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_500(ctx, "内存不足");
        return;
    }
    memcpy(copy, body, len + 1);

    char *id_str = query_get(copy, "id");
    char *page_str = query_get(copy, "page");
    if (!id_str) {
        free(page_str);
        free(copy);
        send_500(ctx, "缺少文章 ID");
        return;
    }
    int id = atoi(id_str);
    free(id_str);

    if (id <= 0) {
        free(page_str);
        free(copy);
        send_500(ctx, "文章不存在");
        return;
    }

    if (db_delete_post(db, id) != SQLITE_OK) {
        free(page_str);
        free(copy);
        send_500(ctx, "删除文章失败");
        return;
    }

    int page = 0;
    if (page_str) {
        page = atoi(page_str);
        free(page_str);
    }
    free(copy);

    admin_touch_last_blog_change();

    char location[128];
    if (page > 1)
        snprintf(location, sizeof(location), "/blog/admin?page=%d", page);
    else
        snprintf(location, sizeof(location), "/blog/admin");
    send_redirect(ctx, location);
}

/**
 * handle_admin_comment_delete — 删除评论 POST /blog/admin/comment/delete
 *
 * 功能：从 body 解析 id、post_id，校验评论属于该文章后删除，重定向回编辑页。
 *
 * @param ctx  连接上下文
 * @param db   博客库
 * @param body 表单 body（含 id、post_id）
 * 返回值：无
 */
void handle_admin_comment_delete(conn_ctx_t *ctx, sqlite3 *db, const char *body) {
    if (!body) {
        send_500(ctx, "缺少请求体");
        return;
    }
    size_t len = strlen(body);
    char *copy = (char *)malloc(len + 1);
    if (!copy) {
        send_500(ctx, "内存不足");
        return;
    }
    memcpy(copy, body, len + 1);

    char *id_str = query_get(copy, "id");
    char *post_id_str = query_get(copy, "post_id");
    if (!id_str || !post_id_str) {
        free(id_str);
        free(post_id_str);
        free(copy);
        send_500(ctx, "缺少必要字段");
        return;
    }

    int id = atoi(id_str);
    int post_id = atoi(post_id_str);
    free(id_str);
    free(post_id_str);

    if (id <= 0 || post_id <= 0) {
        free(copy);
        send_500(ctx, "参数错误");
        return;
    }

    if (db_delete_comment(db, id) != SQLITE_OK) {
        free(copy);
        send_500(ctx, "删除评论失败");
        return;
    }

    free(copy);

    char location[128];
    snprintf(location, sizeof(location), "/blog/admin/post/edit?id=%d", post_id);
    send_redirect(ctx, location);
}

