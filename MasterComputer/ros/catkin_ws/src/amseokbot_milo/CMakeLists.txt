# amseokbot_milo/ — AmseokBot-Milo 的 ROS1 C++ 节点包。
cmake_minimum_required(VERSION 3.0.2)
project(amseokbot_milo)

add_compile_options(-std=c++14 -Wall -Wextra)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  roslib
  std_msgs
  sensor_msgs
  geometry_msgs
  nav_msgs
  cv_bridge
)
find_package(OpenCV REQUIRED)

catkin_package(
  CATKIN_DEPENDS roscpp roslib std_msgs sensor_msgs geometry_msgs nav_msgs cv_bridge
)

include_directories(
  ${catkin_INCLUDE_DIRS}
  ${OpenCV_INCLUDE_DIRS}
)

# ==================== C++ ROS 节点目标 ====================
# 作用：所有 ROS 运行节点统一使用 C++ 编译产物，不再安装 Python 脚本。
# ==========================================================
set(AMSEOKBOT_ROS_NODES
  atmega_serial_bridge
  chassis_kinematics
  line_follow_vision
  opencv_obstacle_avoidance
  single_camera_ultrasonic_mapper
  wheel_odometry
  yolo_obstacle_detector
)

foreach(node_name ${AMSEOKBOT_ROS_NODES})
  add_executable(${node_name} src/${node_name}.cpp)
  add_dependencies(${node_name} ${${PROJECT_NAME}_EXPORTED_TARGETS} ${catkin_EXPORTED_TARGETS})
  target_link_libraries(${node_name} ${catkin_LIBRARIES} ${OpenCV_LIBRARIES})
endforeach()

install(TARGETS ${AMSEOKBOT_ROS_NODES}
  RUNTIME DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(DIRECTORY launch/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/launch
)

install(DIRECTORY models/
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/models
  FILES_MATCHING PATTERN "*.onnx"
)
