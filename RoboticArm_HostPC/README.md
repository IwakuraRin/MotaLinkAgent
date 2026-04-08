# OmniRoam HostPC

上位机控制台：**Vue 3 + Vite + Tailwind**（**pnpm**），**Go** HTTP + WebSocket 服务，局域网浏览器访问。界面风格参考 **Proxmox VE** 类工业控制台（深色面板、等宽日志）。

> 说明：你提到 **React** 与 **Vue** 并存；当前实现为 **Vue 3**（单框架 + Tailwind）。若必须 React，可再开子项目迁移组件。  
> **Wails** 适合把同一套 `web/dist` 封进桌面窗口；**局域网访问**依赖本目录下的 **Go `server`**（浏览器打开 `http://<HostPC-IP>:8080`）。

## 布局（16:9）

- **左侧**：USB 摄像头画面（可选 `VITE_CAMERA_URL` 指向 MJPEG 等）。
- **右侧**：系统日志（ROS / 串口 / 控制数据经 WebSocket 推送）。
- **底部**：操作说明 — **W/S** 前后，**A/D** 横移，**Q/E** 逆时针/顺时针旋转（键位事件经 WS 发送，后端可对接 `geometry_msgs/Twist` 或下位机协议）。

## 一键构建与运行（LAN）

```bash
cd RoboticArm_HostPC
./start.sh
```

或手动：

```bash
cd RoboticArm_HostPC/web
pnpm install
pnpm build

cd ../server
go mod tidy
go run . -addr 0.0.0.0:8080 -static ../web/dist
```

启动后终端会打印本机 **IPv4 访问地址**（例如 `http://192.168.x.x:8080/`），同一局域网内用手机 / 另一台电脑浏览器打开即可。

查看本机 IP（示例）：`hostname -I | awk '{print $1}'`

## 开发（热更新）

终端 1：

```bash
cd server && go run . -addr 0.0.0.0:8080 -static ../web/dist
```

（开发时若尚未 `pnpm build`，可先执行一次 `pnpm build` 生成 `dist`，或临时用旧 dist。）

终端 2：

```bash
cd web && pnpm dev
```

开发时打开 `http://<IP>:5173`，Vite 会把 `/ws`、`/api` 代理到 `8080`。

## 摄像头（可选）

复制 `web/.env.example` 为 `web/.env`，设置 `VITE_CAMERA_URL` 后重新 `pnpm build`。浏览器对跨域视频较敏感；若失败，可改为由 Go 端做 MJPEG 反代（后续可加）。

## Wails（可选后续）

在项目外执行 `wails init`，将 `wails.json` 的 `frontend` 指到 `web/`，`wails build` 使用与 `pnpm build` 相同产物；当前仓库未内置 Wails 工程文件，避免与纯浏览器部署冲突。
