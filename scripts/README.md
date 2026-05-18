# AmseokBot-Milo 一键脚本

<!-- 作用：说明开发阶段如何用 Shell 自动安装环境、构建项目、启动服务和更新代码。 -->

## 脚本说明

- `install.sh`：检测 Ubuntu/Debian 环境，安装依赖，生成本机配置并构建项目。
- `build.sh`：构建 C 控制核心、Go API 和 Vue 前端。
- `start.sh`：启动本机服务，默认后台运行。
- `stop.sh`：停止后台服务。
- `update.sh`：检测 Git 远端更新，拉取、构建并重启。
- `service-install.sh`：注册 `amseokbot-milo.service`，开机自启。

## 快速使用

```bash
scripts/install.sh
scripts/start.sh
```

浏览器打开：

```text
http://本机IP:8080
```

首次登录账号默认是 `user`，首次密码由 `install.sh` 写入：

```bash
sudo grep HOSTPC_PASSWORD /etc/amseokbot/milo.env
```

登录后请立即在前端修改密码。

## 更新

```bash
scripts/update.sh
```

如果工作区有本地修改，脚本会拒绝更新；确认要强制同步远端时再使用：

```bash
scripts/update.sh --force
```

## 开机自启

```bash
scripts/service-install.sh
systemctl status amseokbot-milo.service
```
