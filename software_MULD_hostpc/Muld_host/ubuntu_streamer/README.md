说明（中文）

目标：在 Ubuntu 20.04 LTS server 上捕获摄像头（/dev/video0）的原始视频并实时推流，
通过本机运行的 RTMP 服务（nginx-rtmp）输出到本地端口 1935，然后使用 frp 将本地 1935 端口映射到远程机器，远程机器即可通过远端地址拉流。

包含文件：
- `setup_nginx_rtmp.sh`：在 Ubuntu 上安装 `nginx + libnginx-mod-rtmp`、配置 RTMP 与 HLS 输出并重启 nginx。
- `stream_camera.sh`：使用 `ffmpeg` 从 `/dev/video0` 捕获并推送到本机 RTMP（可传入设备、流名和目标地址覆盖默认项）。
- `frp/frpc.ini`：frp 客户端配置示例。请替换 `YOUR_FRP_SERVER_IP` / `token` 等为你自己的 frps 配置。

快速使用步骤：
1. 在 Ubuntu 20.04 上将本目录拷贝到机器（或使用 git clone）。
2. 运行：
   - `sudo bash setup_nginx_rtmp.sh`  # 安装并配置 nginx-rtmp，默认 RTMP 端口 1935，HLS 存放于 /tmp/hls
3. 准备 frp：
   - 将 `frp/frpc.ini` 中的 `server_addr` 与 `server_port` 修改为你的 frps 地址和端口，必要时设置 `token`。
   - 在机器上放置 `frpc` 可执行文件（从 frp 官方 releases 下载对应 Linux 版本），并运行：
     `./frpc -c ./frp/frpc.ini &`
   - 这样 frp 会把本地 1935 端口映射到远程 frps 所在机器的 1935（或你在 frps 中配置的端口）。
4. 启动摄像头推流：
   - `bash stream_camera.sh /dev/video0 stream`  # 第一个参数为设备（可省略），第二个参数为流名（默认 `stream`）
   - 推送目标为 `rtmp://127.0.0.1/live/stream`（也可给 `stream_camera.sh` 第三个参数覆盖），然后 nginx-rtmp 会在本地提供 RTMP 与 HLS（示例 HLS 在 `/tmp/hls/stream.m3u8`）。
5. 在远端机器上通过 frp 外网地址拉流：
   - RTMP：`rtmp://FRP_SERVER_IP:1935/live/stream`
   - 或 HLS（如果你在远端把 8080 也做了映射）：`http://FRP_SERVER_IP:8080/hls/stream.m3u8`

注意事项：
- 需要在推流端（摄像头所在机器）安装 `ffmpeg`、`nginx`（以及 `libnginx-mod-rtmp`）。脚本会尝试安装这些包。
- 如果你的摄像头分辨率或帧率不同，请调整 `stream_camera.sh` 中的 `-framerate` 与 `-video_size` 参数。
- frp 的安全性与端口映射规则由你的 frps 配置决定，请在 frps 端做好访问控制。

示例测试命令：
- `sudo bash setup_nginx_rtmp.sh`
- 下载 `frpc` 并编辑 `frp/frpc.ini`，然后 `./frpc -c frp/frpc.ini &`
- `bash stream_camera.sh /dev/video0 stream`
- 在远端使用 VLC 或 ffplay 播放：
  `ffplay rtmp://FRP_SERVER_IP:1935/live/stream`
