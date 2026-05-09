# software（一体软件包）

OmniRoam 在单机上的**一体软件包**：ROS、上位机前后端、可选 MySQL、桌面小工具与部署脚本，按目录拆分，互不揉在一起。仓库内路径为 **`software/`**（原 `OmniControlPanel/` 已迁入此处）。

## 目录结构

| 目录 | 说明 |
|------|------|
| `ros/` | ROS 1（Noetic）Catkin 工作区 |
| `frontend/` | 上位机 **Vue 3 + Vite** 前端，`pnpm build` → `dist/` |
| `backend/` | 上位机 **C** HTTP/WebSocket 服务，读取 `frontend/dist` |
| `deploy/` | systemd 安装、`hostpc-self-update.sh` 等生产部署脚本 |
| `database/` | 可选：Docker Compose 启动 MySQL |
| `systeminfo/` | 可选：桌面系统信息小工具（见该目录） |

## 推荐启动方式

在 **OmniRoam 仓库根** 执行：

```bash
./omniroam.sh
```

会识别 `software/`（或旧布局 `OmniControlPanel` / `OmniOS` / `HostPC`）下的前后端，先做环境检测，再启动后端与 Vite（可用 `OMNIROAM_NO_VITE=1` 仅 8080），并进入终端菜单。

仅构建并启动后端（无交互菜单）时：

```bash
bash software/start-hostpc.sh
```

加载 ROS 覆盖层：

```bash
source /path/to/OmniRoam/setup_ros1.bash
```

## 数据库（可选）

```bash
cd software/database
cp .env.example .env
docker compose up -d
```

C 后端默认使用轻量账号文件 `hostpc-users.cauth`，不再依赖 Go 或 MySQL。`database/` 保留给其它实验模块按需使用。
