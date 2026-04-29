# ROS（OmniOS）

本目录存放 **ROS 1 Noetic** 的 Catkin 工作区，与同级的 `frontend/`、`backend/`、`database/` 等目录并列（均在仓库的 `software/` 下）。

## 子目录

- **`catkin_ws/`** — 标准 Catkin 工作区；源码在 `catkin_ws/src/`，编译产物在 `build/`、`devel/`（通常不提交 Git）。

## 常用命令

```bash
cd catkin_ws
source /opt/ros/noetic/setup.bash
catkin_make
source devel/setup.bash
```

或使用仓库根的 `setup_ros1.bash` 一次加载 Noetic + 本工作区。

## USB 摄像头推到前端

本仓库前端的视频面板读取 `camera_url`，浏览器直接播放 MJPEG/HLS URL。ROS 侧可以用 `usb_cam` 读取系统物理摄像头，再用 `web_video_server` 把 `sensor_msgs/Image` 转成 MJPEG：

```bash
source /opt/ros/noetic/setup.bash
source /home/orbot_admin/orbot/software/ros/catkin_ws/devel/setup.bash
roslaunch simple_robotic_arm usb_camera_web.launch video_device:=/dev/video0 web_video_port:=8081
```

前端设置里的摄像头地址填写：

```text
http://192.168.2.141:8081/stream?topic=/usb_cam/image_raw
```

物理摄像头设备由 Linux V4L2 暴露为 `/dev/video*`，可用下面命令查看：

```bash
v4l2-ctl --list-devices
ls -l /dev/video*
```

如果不是 `/dev/video0`，启动时把 `video_device` 改成对应设备。读取摄像头需要当前用户在 `video` 组；加组后通常要重新登录 SSH 才生效。

## 与一键脚本的关系

仓库根目录的 `./omniroam.sh` 会按布局 source 对应 `catkin_ws/devel/setup.bash`（若已编译，例如 `software/ros/catkin_ws`）。未编译时在相应 `catkin_ws` 内执行 `catkin_make` 即可。

若工作区**刚从其他路径移动过来**，旧的 `build/`、`devel/` 里可能残留绝对路径，建议在 `catkin_ws` 下执行 `catkin_make clean` 后重新 `catkin_make`，或删除 `build`、`devel` 再全量编译。
