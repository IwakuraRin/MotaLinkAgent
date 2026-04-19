# 数据库（MySQL）

使用 Docker Compose 在本机启动 **MySQL 8**，供 OmniRoam HostPC（Go 服务）通过 `MYSQL_DSN` 连接。

## 快速开始

```bash
cd software/database   # 相对 OmniRoam 仓库根
cp .env.example .env
# 编辑 .env：root 密码、端口、数据库名等
docker compose up -d
```

初始化 SQL 位于 `init/`（首次启动容器时会导入）。

## 与 HostPC 对接

1. 确保容器健康：`docker compose ps`
2. 在运行 `hostpc` 的环境中设置 `MYSQL_DSN`，或使用 `-mysql-dsn`（DSN 格式见 Go `database/sql` + MySQL 驱动说明）。
3. 若仅用局域网演示、不配 MySQL，可使用 SQLite：`-sqlite-users` / `HOSTPC_SQLITE_USERS`（见 `hostpc` 服务说明）。

## 文件说明

- **`docker-compose.yml`** — 服务定义与数据卷。
- **`.env.example`** — 环境变量模板，复制为 `.env` 后修改。
- **`init/`** — 挂载到容器的初始化脚本（如建库、建表）。
