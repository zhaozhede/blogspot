/**
 * handlers_landing.c — 首页 landing 页面
 *
 * - 背景图数组 landing_bg_images / LANDING_BG_COUNT
 * - handle_landing：新首页（根路径），显示随机台词 + 背景图 + 服务器指标
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#include "conn.h"
#include "http.h"
#include "metrics.h"
#include "landing_quotes_embed.h"
#include "handlers.h"

/* 首页背景图列表，每次刷新随机选一张 */
static const char *const landing_bg_images[] = {
    "/static/landing-bg/IMG_20190704_195830.jpg",
    "/static/landing-bg/IMG_20200708_175759.jpg",
    "/static/landing-bg/IMG_20220130_135755_1746254290634.jpg",
    "/static/landing-bg/IMG_20220130_140051_1746315064332.jpg",
    "/static/landing-bg/IMG_20220304_211017.jpg",
    "/static/landing-bg/IMG_20220518_220324.jpg",
    "/static/landing-bg/IMG_20221001_193715.jpg",
    "/static/landing-bg/IMG_20221222_170306.jpg",
    "/static/landing-bg/IMG_20230710_195800.jpg",
    "/static/landing-bg/IMG_20250409_120208.jpg",
    "/static/landing-bg/IMG_20251019_152518.jpg",
    "/static/landing-bg/mmexport1592405180705.jpg",
    "/static/landing-bg/mmexport1639822671033.jpg",
};
#define LANDING_BG_COUNT ((int)(sizeof(landing_bg_images) / sizeof(landing_bg_images[0])))

/**
 * handle_landing — 首页（根路径 / 或 /index）处理
 *
 * 功能：发送 200 与 HTML：顶栏、随机一句台词（可点击弹窗解释）、随机背景图、服务器指标卡片、页脚；
 *       运行时长由客户端 JS 根据 metrics_get_server_start_time 计算。
 *
 * @param ctx 连接上下文
 * @param db  博客库（本 handler 未使用，保留接口一致）
 * 返回值：无
 */
void handle_landing(conn_ctx_t *ctx, sqlite3 *db) {
    (void)db;
    send_response_header(ctx, 200, "OK", "text/html");
    srand((unsigned)time(NULL) ^ (unsigned)(uintptr_t)(void *)ctx);
    int idx = (int)((unsigned)rand() % (unsigned)LANDING_BG_COUNT);
    const char *bg_url = landing_bg_images[idx];

    server_metrics_t m;
    get_server_metrics(&m);

    char page[24576];
    int n = snprintf(page, sizeof(page),
        "<!DOCTYPE html><html lang=\"zh-CN\"><head>"
        "<meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<link rel=\"icon\" href=\"/favicon.ico\" type=\"image/png\">"
        "<title>zhaozzz.top</title>"
        "<style>"
        ":root{--primary:#2563eb;--text:#1a1a2e;--text-muted:#6b7280;--border:#e5e7eb;--bg:#f8fafc;--chip-bg:rgba(255,255,255,0.78);}"
        "*{box-sizing:border-box;}"
        "body{margin:0;font-family:-apple-system,BlinkMacSystemFont,\"Segoe UI\",Roboto,sans-serif;color:var(--text);line-height:1.6;"
        "min-height:100vh;display:flex;flex-direction:column;background:var(--bg);}"
        ".header{position:sticky;top:0;z-index:1000;height:60px;display:flex;align-items:center;justify-content:space-between;padding:0 24px;"
        "background:rgba(255,255,255,0.28);backdrop-filter:blur(14px);-webkit-backdrop-filter:blur(14px);"
        "box-shadow:0 1px 0 rgba(0,0,0,0.05);}"
        ".header a{display:inline-block;color:var(--text);text-decoration:none;font-weight:600;font-size:18px;padding:4px 10px;border-radius:999px;background:var(--chip-bg);}"
        ".header a:hover{color:var(--primary);}"
        ".nav a{display:inline-block;margin-left:24px;font-size:14px;font-weight:500;line-height:1.6;padding:4px 10px;border-radius:999px;background:var(--chip-bg);vertical-align:middle;}"
        ".main{flex:0 0 auto;display:flex;flex-direction:column;align-items:center;text-align:center;padding:24px 16px 32px;gap:20px;}"
        ".quote-box{width:100%%;max-width:960px;margin:0 auto;padding:1.35rem 1.45rem;border-radius:24px;background:var(--chip-bg);"
        "box-shadow:0 10px 30px rgba(15,23,42,0.12);cursor:pointer;text-align:left;position:relative;min-height:96px;border:1px solid rgba(226,232,240,0.6);}"
        ".quote-text{font-size:1.35rem;font-weight:600;line-height:1.65;color:var(--text);margin:0;text-align:left;text-indent:2em;}"
        ".explain-backdrop{display:none;position:fixed;inset:0;background:rgba(15,23,42,0.45);z-index:2000;align-items:center;justify-content:center;padding:20px;}"
        ".explain-backdrop.open{display:flex !important;}"
        ".explain-card{max-width:960px;width:100%%;padding:3rem 3rem 3.2rem;border-radius:32px;background:rgba(255,255,255,0.98);"
        "box-shadow:0 40px 120px rgba(0,0,0,0.45);color:var(--text);display:flex;flex-direction:column;align-items:stretch;text-align:left;font-size:1.4rem;}"
        ".explain-prefix{font-size:0.4rem;line-height:1.4;color:rgba(100,116,139,0.9);margin:0 0 1rem;max-width:100%%;text-align:left;text-indent:2em;}"
        "@media(prefers-color-scheme:dark){.explain-prefix{color:rgba(148,163,184,0.65);}}"
        ".explain-body{font-size:1.5rem;line-height:1.9;margin:0;text-align:left;text-indent:2em;}"
        ".hero-link{display:block;width:100%%;max-width:960px;margin:0 auto;text-decoration:none;outline:none;border-radius:24px;box-shadow:0 18px 45px rgba(15,23,42,0.25);overflow:hidden;}"
        ".hero-link:focus-visible{outline:2px solid var(--primary);outline-offset:4px;}"
        ".hero-img{display:block;width:100%%;height:auto;vertical-align:middle;border-radius:24px;object-fit:contain;background:rgba(0,0,0,0.04);}"
        ".metrics-section-wrapper{background:rgba(248,250,252,0.96);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);padding:32px 16px 40px;}"
        ".metrics-card-outer{width:100%%;max-width:960px;margin:0 auto 24px;padding:18px 18px 16px;border-radius:22px;background:rgba(255,255,255,0.95);"
        "box-shadow:0 16px 40px rgba(15,23,42,0.14);border:1px solid rgba(226,232,240,0.9);}"
        ".metrics-card-title{font-size:1rem;font-weight:600;text-align:center;margin:0 0 14px;color:var(--text);}"
        ".metrics{width:100%%;display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px;margin:0;}"
        ".metric-card{background:rgba(15,23,42,0.03);backdrop-filter:blur(8px);-webkit-backdrop-filter:blur(8px);border-radius:16px;padding:12px 14px;border:1px solid rgba(226,232,240,0.9);text-align:left;box-shadow:0 8px 20px rgba(15,23,42,0.08);}"
        ".metric-title{font-size:0.95rem;font-weight:600;color:var(--text);margin:0 0 4px;}"
        ".metric-value{font-size:0.98rem;font-weight:500;color:var(--text);margin:0 0 2px;}"
        ".metric-desc{font-size:0.8rem;color:var(--text-muted);margin:0;}"
        ".metrics-footer{margin-top:10px;text-align:right;font-size:0.8rem;color:var(--text-muted);}"
        ".footer{text-align:center;padding:24px;color:var(--text-muted);font-size:14px;"
        "background:rgba(255,255,255,0.25);backdrop-filter:blur(10px);-webkit-backdrop-filter:blur(10px);border-top:1px solid rgba(255,255,255,0.2);}"
        ".footer a{display:inline-block;color:var(--text-muted);text-decoration:none;font-weight:500;padding:4px 10px;border-radius:999px;background:var(--chip-bg);}"
        ".footer a:hover{color:var(--primary);}"
        ".footer p span{display:inline-block;padding:4px 10px;border-radius:999px;background:var(--chip-bg);}"
        "@media(prefers-color-scheme:dark){"
        ":root{--text:#e2e8f0;--text-muted:#94a3b8;--primary:#60a5fa;--border:rgba(255,255,255,0.1);--bg:#020617;--chip-bg:rgba(15,23,42,0.78);}"
        ".header{background:rgba(15,23,42,0.35);box-shadow:0 1px 0 rgba(255,255,255,0.06);}"
        ".quote-box{border-color:rgba(51,65,85,0.6);}"
        ".explain-card{background:rgba(15,23,42,0.95);color:var(--text);border:1px solid rgba(148,163,184,0.35);}"
        ".metrics-section-wrapper{background:rgba(15,23,42,0.92);}"
        ".metrics-card-outer{background:rgba(15,23,42,0.96);border-color:rgba(148,163,184,0.4);box-shadow:0 16px 40px rgba(0,0,0,0.7);}"
        ".metric-card{background:rgba(15,23,42,0.85);border-color:rgba(148,163,184,0.7);box-shadow:0 10px 30px rgba(0,0,0,0.6);}"
        ".footer{background:rgba(15,23,42,0.3);border-top:1px solid rgba(255,255,255,0.08);}"
        "}"
        "@media(max-width:768px){.quote-text{font-size:1.15rem;text-indent:2em;} .quote-box{padding:1.1rem 1.15rem;border-radius:20px;min-height:88px;text-align:left;} .main{padding:20px 12px 24px;gap:16px;} .hero-link,.hero-img{border-radius:20px;} .metrics{grid-template-columns:1fr;} .nav a{margin-left:16px;font-size:13px;padding:3px 8px;}}"
        "</style></head><body>"
        "<header class=\"header\">"
        "<a href=\"/\">zhaozzz.top</a>"
        "<nav class=\"nav\"><a href=\"/\">首页</a><a href=\"/blog\">博客</a><a href=\"/about\">关于</a></nav>"
        "</header>"
        "<main class=\"main\">");
    /* 若 snprintf 因 % 截断则整页失败；台词区单独组装后发送，避免内嵌 JSON/JS 解析失败 */
    if (n <= 0 || (size_t)n >= sizeof(page))
        goto landing_err;
    conn_send(ctx, page, (size_t)n);

    /* 每次刷新由服务端随机选一句，直接写入 HTML；点击台词块弹窗显示解释 */
    {
        int qi = 0;
        if (LANDING_QUOTES_COUNT > 0)
            qi = (int)((unsigned)rand() % (unsigned)LANDING_QUOTES_COUNT);
        const landing_quote_t *lq = &LANDING_QUOTES[qi];
        char *eq = html_escape(lq->q);
        char *ef = html_escape(lq->f);
        char *ee = html_escape(lq->e);
        if (!eq) {
            eq = (char *)malloc(1);
            if (eq) eq[0] = '\0';
        }
        if (!ef) {
            ef = (char *)malloc(1);
            if (ef) ef[0] = '\0';
        }
        if (!ee) {
            ee = (char *)malloc(1);
            if (ee) ee[0] = '\0';
        }
        if (!eq || !ef || !ee) {
            if (eq) free(eq);
            if (ef) free(ef);
            if (ee) free(ee);
            const char *fb = "<div class=\"quote-box\" id=\"quoteBox\" title=\"点击查看解释\"><p class=\"quote-text\">时间不在于你拥有多少，而在于你怎样使用。</p></div>"
                "<div id=\"quoteMeta\" style=\"display:none\">时间刺客·艾克</div>"
                "<div id=\"quoteExplain\" style=\"display:none\">生命的价值在于质量和对时间的利用。</div>"
                "<div class=\"explain-backdrop\" id=\"explainBackdrop\"><div class=\"explain-card\" id=\"explainCard\">"
                "<p class=\"explain-prefix\" id=\"explainPrefix\"></p><p class=\"explain-body\" id=\"explainBody\"></p></div></div>";
            conn_send(ctx, fb, strlen(fb));
        } else {
        char quote_part[16384];
        int qn = snprintf(quote_part, sizeof(quote_part),
            "<div class=\"quote-box\" id=\"quoteBox\" title=\"点击查看解释\"><p class=\"quote-text\">%s</p></div>"
            "<div id=\"quoteMeta\" style=\"display:none\">%s</div>"
            "<div id=\"quoteExplain\" style=\"display:none\">%s</div>"
            "<div class=\"explain-backdrop\" id=\"explainBackdrop\"><div class=\"explain-card\" id=\"explainCard\">"
            "<p class=\"explain-prefix\" id=\"explainPrefix\"></p><p class=\"explain-body\" id=\"explainBody\"></p></div></div>",
            eq, ef, ee);
        free(eq);
        free(ef);
        free(ee);
        if (qn > 0 && qn < (int)sizeof(quote_part))
            conn_send(ctx, quote_part, (size_t)qn);
        else {
            const char *fb2 = "<div class=\"quote-box\" id=\"quoteBox\" title=\"点击查看解释\"><p class=\"quote-text\">时间不在于你拥有多少，而在于你怎样使用。</p></div>"
                "<div id=\"quoteMeta\" style=\"display:none\">时间刺客·艾克</div>"
                "<div id=\"quoteExplain\" style=\"display:none\">生命的价值在于质量和对时间的利用。</div>"
                "<div class=\"explain-backdrop\" id=\"explainBackdrop\"><div class=\"explain-card\" id=\"explainCard\">"
                "<p class=\"explain-prefix\" id=\"explainPrefix\"></p><p class=\"explain-body\" id=\"explainBody\"></p></div></div>";
            conn_send(ctx, fb2, strlen(fb2));
        }
        }
    }

    n = snprintf(page, sizeof(page),
        "<a href=\"/blog\" class=\"hero-link\" title=\"进入博客\"><img class=\"hero-img\" src=\"%s\" alt=\"点击进入博客\"></a>"
        "</main>"
        "<section class=\"metrics-section-wrapper\">"
        "<div class=\"metrics-card-outer\">"
        "<p class=\"metrics-card-title\">服务器信息</p>"
        "<section class=\"metrics\">"
        "<div class=\"metric-card\"><p class=\"metric-title\">服务器启动时间</p><p class=\"metric-value\">%s</p></div>"
        "<div class=\"metric-card\"><p class=\"metric-title\">服务器内存占用</p><p class=\"metric-value\">%s</p></div>"
        "<div class=\"metric-card\"><p class=\"metric-title\">服务器磁盘占用</p><p class=\"metric-value\">%s</p></div>"
        "<div class=\"metric-card\"><p class=\"metric-title\">服务器CPU温度</p><p class=\"metric-value\">%s</p></div>"
        "<div class=\"metric-card\"><p class=\"metric-title\">今日页面访问</p><p class=\"metric-value\">%s</p><p class=\"metric-desc\" style=\"margin-top:4px;\">按打开整页计次，不含静态资源</p></div>"
        "</section>"
        "<div class=\"metrics-footer\">最后更新：%s</div>"
        "</div>"
        "</section>"
        "<footer class=\"footer\">"
        "<p>© 2026 zhaozzz.top</p>"
        "<p><span id=\"run-days-footer\">已运行 0 天 00 时 00 分 00 秒</span></p>"
        "<p style=\"margin:0.35rem 0 0;font-size:0.8rem;color:var(--text-muted);\">累积访问人次：%lld</p>"
        "</footer>",
        bg_url,
        m.boot_time,
        m.mem_usage,
        m.disk_usage,
        m.cpu_temp,
        m.visits_today_str,
        m.last_update,
        (long long)metrics_get_total_visits());
    if (n > 0 && (size_t)n < sizeof(page)) {
        conn_send(ctx, page, (size_t)n);
        /* 仅弹窗：点击台词区域显示解释，刷新页面才换一句 */
        {
            const char *quote_js =
                "<script>(function(){var box=document.getElementById('quoteBox'),meta=document.getElementById('quoteMeta'),ex=document.getElementById('quoteExplain'),bd=document.getElementById('explainBackdrop'),card=document.getElementById('explainCard'),pre=document.getElementById('explainPrefix'),body=document.getElementById('explainBody');if(box&&ex&&bd&&card&&body){box.onclick=function(){var m=(meta&&meta.textContent)?meta.textContent.trim():'';if(pre)pre.textContent=m?('这是英雄联盟这款游戏里'+m+'的台词，这句话的意思是：'):'这句话的意思是：';body.textContent=ex.textContent||'';bd.classList.add('open');};card.onclick=function(){bd.classList.remove('open');};}if(bd)bd.onclick=function(){bd.classList.remove('open');};})();</script>";
            conn_send(ctx, quote_js, strlen(quote_js));
        }
        char run_js[640];
        int rn = snprintf(run_js, sizeof(run_js),
            "<script>(function(){var startSec=%lld;function pad(n){return n<10?'0'+n:n;}function update(){var now=Math.floor(Date.now()/1000),d=Math.floor((now-startSec)/86400),h=pad(Math.floor((now-startSec)%%86400/3600)),m=pad(Math.floor((now-startSec)%%3600/60)),s=pad((now-startSec)%%60),el=document.getElementById('run-days-footer');if(el)el.textContent='已运行 '+d+' 天 '+h+' 时 '+m+' 分 '+s+' 秒';}update();setInterval(update,1000);})();</script></body></html>",
            (long long)metrics_get_server_start_time());
        if (rn > 0 && rn < (int)sizeof(run_js))
            conn_send(ctx, run_js, (size_t)rn);
        else
            conn_send(ctx, "</body></html>", 14);
    } else {
landing_err:
        conn_send(ctx, "<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"></head><body>错误</body></html>",
                  strlen("<!DOCTYPE html><html lang=\"zh-CN\"><head><meta charset=\"utf-8\"></head><body>错误</body></html>"));
    }
}

