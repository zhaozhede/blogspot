# 用户名与密码哈希存储说明

## 当前存储方式

### 1. 存储位置（文件）

- **数据库文件**：SQLite 数据库文件，路径固定为 **`./data/users.db`**（与博客库 `./data/blog.db` 同目录）。
- **数据目录**：程序固定使用 **`./data`**，不可通过环境变量修改。

### 2. 存储结构（表与字段）

在 `src/user_db.c` 的 `user_db_init()` 中建表：

```sql
CREATE TABLE IF NOT EXISTS users (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  username TEXT UNIQUE NOT NULL,
  salt TEXT NOT NULL,
  password_hash TEXT NOT NULL,
  created_at INTEGER NOT NULL
);
```

- **username**：明文存储
- **salt**：16 字节随机盐，以 32 位十六进制字符串存储
- **password_hash**：PBKDF2-HMAC-SHA256(密码, salt, 10000 次) 的 32 字节结果，以 64 位十六进制字符串存储
- **created_at**：注册时间戳（Unix 秒）

### 3. 相关代码位置

| 功能           | 文件         | 函数/位置 |
|----------------|--------------|-----------|
| 建表           | `src/user_db.c` | `user_db_init()` |
| 注册（写入）   | `src/user_db.c` | `user_register()` |
| 登录验证       | `src/user_db.c` | `user_verify()` |
| 按 id 取用户名 | `src/user_db.c` | `user_get_username()` |
| 打开用户库     | `src/main.c`    | `main()` 中拼 `user_path`，调用 `user_db_open(&user_db, user_path)` |
| 默认路径常量   | `src/user_db.h` | `USER_DB_PATH_DEFAULT "data/users.db"` |

### 4. 哈希算法参数（user_db.c）

- **PBKDF2_ITER**：10000
- **SALT_LEN**：16 字节
- **HASH_LEN**：32 字节（SHA-256 输出）
- 盐与哈希在库中均以十六进制字符串形式存储
