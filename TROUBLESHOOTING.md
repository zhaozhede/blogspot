## 顽固问题记录（blogpost）

### 1. main.c 大重构导致的异常

- **现象**：最初将所有 HTTP 处理逻辑从 `main.c` 拆分到多个 `handlers_*.c` / `dispatch.c` 后，出现链接期多重定义、以及一次误操作把 `main.c` 主体截断。
- **原因**：
  - 同一个 handler 既保留在 `main.c`，又在新建的 `handlers_xxx.c` 中实现，导致 `multiple definition`。
  - 使用 `sed` 大段删除时范围拿得太大，把 `main()` 一起删掉。
- **解决方案**：
  - 创建统一的 `handlers.h`，把所有 handler 只在各自 `handlers_*.c` 中实现，在 `dispatch.c` 中通过声明调用。
  - 把 `main.c` 彻底重写成：只保留服务器初始化（监听 socket + epoll 循环）、数据库和鉴权初始化、`handle_client` 分发调用，无业务逻辑。

### 2. OpenSSL / RSA 相关告警

- **现象**：`auth.c` 中大量 `RSA_size`、`RSA_private_decrypt`、`PEM_read_RSAPrivateKey` 等 OpenSSL 3.0 弃用告警。
- **原因**：系统 OpenSSL 升级到 3.x，而当前代码仍使用 1.x/早期 3.x 兼容 API。
- **解决方案**：
  - 暂时只作为编译告警保留，不影响运行。
  - 若以后需要彻底解决，需要改为 EVP_PKEY / EVP_PKEY_CTX 体系，这属于独立重构任务。

### 3. 登录页「正在加载加密库…」常驻

- **现象**：电脑端访问登录页时，页面一直显示「正在加载加密库…」，即使输入正确密码也提示账户或密码错误。
- **原因**：
  - 前端逻辑会在 `window.load` 时检查 `typeof JSEncrypt`，如果为 `undefined` 就认为加密库未加载，**不会**绑定表单加密逻辑。
  - 此时表单以明文提交，后端按明文密码与配置值比较，如果不一致就返回登录失败。
  - 典型诱因是 `/blog/static/jsencrypt.min.js` 未正确加载（路径错误、404、文件内容不对等）。
- **解决方案**：
  - 在浏览器 Network 面板确认 `jsencrypt.min.js` 返回 200 且内容正确。
  - Console 中无 JS 报错后，再通过 `typeof JSEncrypt` 验证库已加载。
  - 登录失败时，检查 `auth.c` 中 `get_password()` 返回的实际密码与前端输入是否一致。

### 4. 后台首条文章「不在卡片里」的问题

- **现象**：`/blog/admin` 页面中，最新一篇文章总是显示在上一个大卡片的「外层」，开发者工具里看到类似：
  - `<div class="post-list" <article onclick="/blog/post?id=19"> ...`
- **原因**：
  - 在早期实现里使用 `conn_send(ctx, "<div class=\"post-list\">", 22);`，硬编码长度 22。
  - 之后把字符串内容改动过（空格 / 属性等），但长度常量未同步，导致最后一个 `>` 没有被发送。
  - 浏览器把第一个 `<article>` 当成仍然挂在 `<div class="post-list"` 这一行上，结构错位。
- **解决方案**：
  - 改为使用 `strlen` 发送完整字符串：
    - `const char *list_open = "<div class=\"post-list\">";`
    - `conn_send(ctx, list_open, strlen(list_open));`
  - 后续所有 HTML 片段都尽量用 `strlen` 配合常量字符串，避免再用手写长度。

### 5. 后台文章卡片拆分输出

- **现象**：在调整后台风格时，对 `snprintf` 格式参数个数掌握不严，编译出现 `-Wformat-extra-args` 告警。
- **原因**：一行 `snprintf` 同时拼接 `<article>`、`<header>`、按钮等复杂 HTML，参数多且容易漏改。
- **解决方案**：
  - 把一张文章卡片拆成三段输出：外层 `<article>`、标题/时间 `<header>`、底部操作按钮 `<div class="post-actions">`。
  - 每段 `snprintf` 只接收真正需要的 2～3 个参数，可读性和安全性都更好，也方便以后继续调整样式。

