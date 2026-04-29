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

## OpenCV 单目避障

`opencv_obstacle_avoidance.py` 是不需要训练的轻量 OpenCV 节点：订阅摄像头图像，估计画面底部地面颜色，在前方 ROI 中找出明显不同于地面的区域，按左/中/右三段障碍占比发布 `/cmd_vel`。

```bash
source /opt/ros/noetic/setup.bash
source /home/orbot_admin/orbot/software/ros/catkin_ws/devel/setup.bash
roslaunch simple_robotic_arm opencv_obstacle_avoidance.launch image_topic:=/usb_cam/image_raw cmd_vel_topic:=/cmd_vel
```

调试时建议先不要直接接真实底盘，用测试话题确认输出：

```bash
roslaunch simple_robotic_arm opencv_obstacle_avoidance.launch cmd_vel_topic:=/obstacle_test_cmd_vel publish_debug_image:=true
rostopic echo /obstacle_test_cmd_vel
```

这是单目启发式避障，不能得到真实距离；实际运行前需要按现场地面、光照、摄像头角度调 `center_stop_ratio`、`roi_y0/roi_y1`、`hsv_floor_dist_thresh` 等参数。需要可靠距离时应接入深度相机、双目或激光雷达。
