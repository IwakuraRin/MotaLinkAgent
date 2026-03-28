MULD-Vehicle：多功能无人配送与操作小车
--
MULD-Vehicle___Multifunctional.Unmanned.Logistics.Vehicle.for.Delivery_Handling

Multifunctional Logistics Vehicle for Unmanned Delivery and Handling/缩写简称MULD Vehicle

项目简介
MULD-Vehicle 是一个集自动配送与抓取操作于一体的多功能无人小车开发平台。

1.硬件架构
上位机：x86 Linux 计算平台，负责任务调度、全局规划与交互。
下位机：ESP32-S3 微控制器，基于 Arduino 框架开发，负责电机、传感器等硬件的实时控制。
执行单元：车体配备多自由度简易机械臂与货舱。

2.核心功能
移动抓取：机械臂可抓取小型物体，并自动放置于车身货舱内。
自主移动：小车可按预设路线进行自动循迹行驶。
远程遥控：支持通过遥控指令控制车辆移动与机械臂动作。

3.项目特点
采用上下位机设计，平衡了计算性能与实时控制需求。
在同一平台上实现了移动底盘与简单操作能力的融合。
项目硬件设计、下位机固件与上位机软件开源。
本项目适用于机器人学习、自动化物流概念验证等场景
