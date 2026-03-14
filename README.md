# 个人博客系统（C + SQLite）

本项目用于在 **华为电视盒子 HI3798mv100（ARMv7，HiNAS 系统）** 上运行一个超轻量级的个人博客站点：

- 后端：C 语言 + SQLite，自带 HTTP 服务（epoll，监听 80 端口）
- 前端：原生 HTML/CSS/JS，Markdown 在浏览器端渲染（marked.js + KaTeX 公式）
- 数据：文章与评论存 SQLite，支持后台触发增量备份到家目录与 U 盘

## 目录结构

- `src/`
  - `main.c`：程序入口，epoll 事件循环
  - `conn.c` / `conn.h`：连接池与读写缓冲
  - `dispatch.c` / `dispatch.h`：请求解析与路由分发
  - `db.c` / `db.h`：博客库（文章、评论、访问统计）
  - `user_db.c` / `user_db.h`：用户库（注册、登录）
  - `auth.c` / `auth.h`：RSA 登录、session 管理
  - `http.c` / `http.h`：HTTP 响应与工具（重定向、HTML 转义、query 解析）
  - `metrics.c` / `metrics.h`：服务器指标与访问统计
  - `handlers_*.c`：各路径处理（首页、博客列表/详情、静态、登录/注册、后台、评论）
  - `landing_quotes_embed.h`：由 `tools/gen_landing_quotes_h.py` 根据 `static/landing-quotes.json` 生成
- `static/`：前端静态资源（样式、JS、图片、`landing-quotes.json`、`jsencrypt.min.js` 等）
- `data/`：运行时数据目录，存放 `blog.db`、`users.db`、RSA 密钥（可由 `BLOG_DATA_DIR` 指定到 EMMC）
- `scripts/`
  - `backup.sh`：增量备份脚本（将数据目录同步到家目录、U 盘）
  - `install_autostart.sh`：注册 systemd 服务实现开机自启

## 数据库设计

- **文章表 `posts`**：`id`, `title`, `content_md`, `created_at`, `updated_at`
- **评论表 `comments`**：`id`, `post_id`, `parent_id`（0 为顶级，否则为被回复评论 id）, `nickname`, `ip`, `location`, `content`, `created_at`
- **用户表 `users`**（`users.db`）：`id`, `username`, `salt`, `password_hash`, `created_at`
- **访问统计 `site_stats`**：全站总访问、今日访问（按自然日重置）

## 主要路由

- 访客前台：`GET /` 首页（随机台词 + 服务器指标），`GET /about` 关于页，`GET /blog` 文章列表，`GET /blog/post?id=...` 文章详情，`POST /blog/comment` 发表/回复评论
- 登录注册：`GET /blog/login`、`POST /blog/login`（统一登录，先试管理员再试普通用户），`GET /blog/register`、`POST /blog/user/register`，`GET /blog/user/logout`
- 后台管理（需 session）：`GET /blog/admin` 文章列表，`GET /blog/admin/post/new` 新建，`GET /blog/admin/post/edit?id=...` 编辑，`POST /blog/admin/post/create`、`POST /blog/admin/post/update`、`POST /blog/admin/post/delete`，`POST /blog/admin/comment/delete`，`GET /blog/admin/logout`

未登录访问后台路径会重定向到 `/blog/admin` 登录页。

## 首页台词数据（landing-quotes.json → .h）

首页随机台词由服务端写入 HTML，数据来自 `static/landing-quotes.json`（字段：`q` 台词、`f` 角色名、`e` 解释）。修改 JSON 后需重新生成头文件并编译：

- `make quotes`：仅根据 JSON 生成 `src/landing_quotes_embed.h`
- `make`：会先执行 `quotes` 再编译

## 编译与运行

```bash
make              # 清理并生成 blog（含 landing-quotes）
./blog            # 默认监听 0.0.0.0:80
```

依赖：`gcc`、`libsqlite3-dev`、`libssl-dev`（OpenSSL，用于 RSA 与用户密码 PBKDF2）。

## 备份与自启动（部署在 HI3798 / Ubuntu）

### 项目路径与备份脚本

- **项目源代码目录**（本机部署）：`/home/ubuntu/ai_code/blogpost`
- **备份脚本绝对路径**：`/home/ubuntu/ai_code/blogpost/scripts/backup.sh`  
程序在**新增、修改、删除文章**时会自动在后台调用该脚本（不阻塞请求）。若项目迁到其他目录，需修改 `src/handlers_admin.c` 中的常量：
  ```c
  const char *script = "/home/ubuntu/ai_code/blogpost/scripts/backup.sh";
  ```

### 备份行为

- 备份脚本将 **数据目录**（默认 `data/`，可由环境变量 `BLOG_DATA_DIR` 指定）下的所有文件（如 `blog.db`、`users.db`、RSA 密钥）增量同步到：
  1. **家目录**：默认 `$HOME/blog_backup`（如 `/home/ubuntu/blog_backup`）
  2. **U 盘**：默认 `/mnt/usb1/blog_backup`（可按实际挂载点用 `BLOG_BACKUP_USB` 覆盖，例如 `/mnt/usb/blog_backup`）
- 若 **systemd 以 root 运行**博客服务，脚本也会以 root 执行，此时 `$HOME` 为 `/root`，家目录备份会写到 `/root/blog_backup`。若希望仍备份到 ubuntu 用户目录，可在 systemd 单元中增加环境变量，例如：
  ```ini
  Environment=BLOG_BACKUP_HOME=/home/ubuntu/blog_backup
  ```
  然后执行 `sudo systemctl daemon-reload` 和 `sudo systemctl restart blog`。

### 自启动（systemd）

- 服务以 **root** 运行，项目路径为 `/home/ubuntu/ai_code/blogpost` 时，在项目根目录执行：
  ```bash
  sudo scripts/install_autostart.sh
  ```
  脚本会创建 `/etc/systemd/system/blog.service`，设置 `ExecStart`、`WorkingDirectory`、`Environment=BLOG_DATA_DIR`，并 `enable --now` 服务。
- 查看状态：`sudo systemctl status blog`
- 重新编译后无需重启整机，重启服务即可加载新程序：`sudo systemctl restart blog`

## 部署注意事项

- **仅 HTTP**：后台登录使用本机生成的 RSA 密钥对（`data/admin_private.pem`、`data/admin_public.pem`），公钥下发给登录页，客户端用本机提供的 `static/jsencrypt.min.js` 加密「用户名:密码」后 POST。管理密码由环境变量 `BLOG_ADMIN_TOKEN` 配置，未设置时默认 `admin123`，请修改并限制后台访问。
- **数据目录**：`BLOG_DATA_DIR` 未设置时使用项目下 `data/`；部署时可设为固定路径（如 `/mnt/emmc/blog`），则 `blog.db`、`users.db` 等均在该目录。
- **端口**：默认 80，在 `src/main.c` 中 `SERVER_PORT` 可改。

## 维护与扩展

- 常见问题与解决方案见项目根目录 `TROUBLESHOOTING.md`。
- Makefile 中还有 `make backup`（手动执行一次备份）、`make install-autostart`（同 `scripts/install_autostart.sh`），详见 `Makefile` 内注释。

