# Deploy（生产安装与自更新）

- **`install-hostpc.sh`** — 将已编译的 `backend/hostpc` 与 `frontend/dist` 安装到系统路径，并安装/启用 **`omniroam-hostpc.service`**（需 root）。
- **`omniroam-hostpc.service`** — systemd 单元模板。
- **`hostpc-self-update.sh`** — 在仓库根 `git pull` 后重建前端与 Go 二进制并再次执行 `install-hostpc.sh`（管理界面「应用更新」或 `./omniroam.sh` 菜单项会用到）。开发环境可设 `OMNIROAM_SKIP_SYSTEMD_INSTALL=1` 跳过 systemd 安装。

## 构建顺序示例

在仓库根执行：

```bash
pnpm -C software/frontend install && pnpm -C software/frontend run build
go -C software/backend build -o hostpc .
sudo software/deploy/install-hostpc.sh
```
