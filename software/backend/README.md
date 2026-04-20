# Backend（Go 上位机服务）

HTTP 静态资源（Vue 构建产物）、WebSocket（日志、键盘、Shell、VNC 代理）、REST API（鉴权、设置、串口、文件浏览、更新检测等）。

本目录为 **Go Modules** 工程，**必须**包含 **`go.mod`** 与 **`go.sum`** 并纳入版本控制。仓库根 `./omniroam.sh` 使用 `go run .` 启动后端；若缺少上述文件，Go 会报错且脚本会拒绝启动。

首次拉代码或依赖有变更时，在 **`software/backend`** 执行：

```bash
go mod tidy
```

若访问 `proxy.golang.org` 不稳定（例如部分网络环境），可设置镜像后再 tidy：

```bash
export GOPROXY=https://goproxy.cn,direct
go mod tidy
```

## 开发运行

在 **`software/backend`** 目录：

```bash
go mod tidy   # 首次或依赖变更时
go run . -addr 0.0.0.0:8080 -static ../frontend/dist
```

需先有 **`../frontend/dist`**（在 `software/frontend` 执行 `pnpm install && pnpm run build`）。

## 仓库根一键启动

在仓库根执行 `./omniroam.sh` 时，会进入本目录并以 `go run` 启动（静态目录为前端 `dist` 的绝对路径，并传入 `-repo-root`、SQLite 等）。启动前会检查 `go.mod` / `go.sum` 是否存在。

## 数据库

- **MySQL**：`MYSQL_DSN` 或 `-mysql-dsn`。
- **SQLite（开发/LAN）**：`-sqlite-users` / `HOSTPC_SQLITE_USERS`。

MySQL 容器见 `../database/`。
