# Frontend（Vue 上位机界面）

**Vue 3 + Vite + Tailwind**，包管理：**pnpm**。构建产物在 **`dist/`**，由 **`OmniOS/backend`** 作为静态文件托管。

## 构建

在 **`OmniOS/frontend`**：

```bash
pnpm install
pnpm run build
```

## 开发（热更新）

终端 1：在 **`OmniOS/backend`** 执行：

```bash
go run . -addr 0.0.0.0:8080 -static ../frontend/dist
```

终端 2：在本目录执行 `pnpm dev`，浏览器打开 Vite 提示的地址（如 `:5173`）；Vite 将 `/ws`、`/api` 代理到 `8080`。

## 环境变量

复制 `.env.example` 为 `.env`（如 `VITE_CAMERA_URL`）；修改后重新 `pnpm build` 供生产部署。
