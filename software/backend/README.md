# Backend（Go 上位机服务）

HTTP 静态资源（Vue 构建产物）、WebSocket（日志、键盘、Shell、VNC 代理）、REST API（鉴权、设置、串口、文件浏览、更新检测等）。

## 开发运行

在 **`software/backend`** 目录：

```bash
go mod tidy
go run . -addr 0.0.0.0:8080 -static ../frontend/dist
```

需先有 **`../frontend/dist`**（在 `software/frontend` 执行 `pnpm install && pnpm run build`）。

## 仓库根一键启动

在仓库根执行 `./omniroam.sh` 时，会进入本目录并以 `go run` 启动（静态目录为前端 `dist` 的绝对路径，并传入 `-repo-root`、SQLite 等）。

## 数据库

- **MySQL**：`MYSQL_DSN` 或 `-mysql-dsn`。
- **SQLite（开发/LAN）**：`-sqlite-users` / `HOSTPC_SQLITE_USERS`。

MySQL 容器见 `../database/`。
