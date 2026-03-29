#!/bin/bash
set -e

# 安装 nginx 与 rtmp 模块（Ubuntu 20.04）
# 注意: 在某些系统上 libnginx-mod-rtmp 包可能不可用；请根据需要编译 nginx 或使用其他 RTMP 服务。

echo "更新 apt 源并安装 nginx, ffmpeg, curl"
sudo apt-get update
sudo apt-get install -y nginx ffmpeg curl

# 安装 libnginx-mod-rtmp 如果可用
if apt-cache show libnginx-mod-rtmp >/dev/null 2>&1; then
  echo "安装 libnginx-mod-rtmp"
  sudo apt-get install -y libnginx-mod-rtmp
else
  echo "libnginx-mod-rtmp 包不可用，继续并尝试仅安装 nginx"
fi

NGINX_CONF="/etc/nginx/nginx.conf"
BACKUP_CONF="/etc/nginx/nginx.conf.bak"

if [ ! -f "$BACKUP_CONF" ]; then
  sudo cp "$NGINX_CONF" "$BACKUP_CONF"
fi

cat <<'EOF' | sudo tee $NGINX_CONF
user www-data;
worker_processes auto;
pid /run/nginx.pid;
include /etc/nginx/modules-enabled/*.conf;

events { worker_connections 1024; }

rtmp_auto_push on;

http {
    sendfile off;
    tcp_nopush on;
    directio 512;
    default_type application/octet-stream;

    server {
        listen 8080;

        location /hls {
            types {
                application/vnd.apple.mpegurl m3u8;
                video/mp2t ts;
            }
            alias /tmp/hls;
            add_header Cache-Control no-cache;
        }

        location /stat {
            rtmp_stat all;
            rtmp_stat_stylesheet stat.xsl;
        }

        location /stat.xsl {
            root /usr/share/nginx/html;
        }
    }

    include /etc/nginx/conf.d/*.conf;
}

rtmp {
    server {
        listen 1935;
        chunk_size 4096;

        application live {
            live on;
            record off;

            hls on;
            hls_path /tmp/hls;
            hls_fragment 3;
            hls_playlist_length 10;
        }
    }
}
EOF

sudo mkdir -p /tmp/hls
sudo chown www-data:www-data /tmp/hls || true

sudo nginx -t
sudo systemctl restart nginx

echo "nginx + rtmp 已配置并重启。RTMP: rtmp://<this_server>:1935/live/<stream>  HLS: http://<this_server>:8080/hls/<stream>.m3u8"