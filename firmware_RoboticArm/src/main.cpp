#include <Arduino.h>
#include "PCA9685_Servo.h"

//定义GPIO参数
#define I2C_SDA_PIN 8
#define I2C_SCL_PIN 9
//PCA9685_连接到ESP32S3 IIC

//宏定义 为PCA9685的16个通道定义名称
#define MG996R_0 0
#define MG996R_1 1
#define MG996R_2 2
#define MG90S_3 3
#define MG90S_4 4


PCA9685_Servo servoController(0x40, 150, 600, 50);

struct RobotPose_LeftLeg {                 
    uint16_t MG996R_0A ;    
    uint16_t MG996R_1A ;
    uint16_t MG996R_2A ;
    uint16_t MG90S_3A ;
    uint16_t MG90S_4A ;
 
};
//定义左腿的3个关节为一个结构体

const RobotPose_LeftLeg POSE_HOME = {90, 90, 90, 90, 90};
const RobotPose_LeftLeg POSE_READY = {45, 120, 60, 30, 90};
const RobotPose_LeftLeg POSE_PICK = {45, 135, 45, 45, 0};

void setRobotPose_LeftLeg(const RobotPose_LeftLeg& pose, uint16_t duration = 1000) {
    Serial.println("设置机器人姿态...");
    
    servoController.setAngle(MG996R_0, pose.MG996R_0A, true);
    servoController.setAngle(MG996R_1, pose.MG996R_1A, true);
    servoController.setAngle(MG996R_2, pose.MG996R_2A, true);
    servoController.setAngle(MG90S_3, pose.MG90S_3A, true);
    servoController.setAngle(MG90S_4, pose.MG90S_4A, true);

    delay(duration);
}

void setupHardware() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n=== ESP32-S3 PCA9685 舵机控制 ===");
    
    // 先初始化I2C总线
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.printf("I2C引脚: SDA=%d, SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
    
    // 扫描I2C设备
    PCA9685_Servo::scanI2C(Wire);
    
    // 初始化舵机控制器（注意：现在不需要传递Wire参数）
    if (!servoController.begin()) {
        Serial.println("舵机控制器初始化失败！请检查连接。");
        while(1);
    }
    
    Serial.println("硬件初始化完成！");
}

void testAllServos() {
    Serial.println("\n=== 舵机测试 ===");
    
    Serial.println("测试通道0...");
    servoController.setAngle(0, 0, true);
    delay(1000);
    servoController.setAngle(0, 90, true);
    delay(1000);
    servoController.setAngle(0, 180, true);
    delay(1000);
    servoController.setAngle(0, 90, true);
    delay(1000);
    
    Serial.println("测试通道0-4同时运动...");
    uint8_t channels[] = {0, 1, 2, 3, 4};
    uint16_t angles1[] = {0, 45, 90, 135, 180};
    servoController.setAngles(channels, angles1, 5, true);
    delay(2000);
    
    uint16_t angles2[] = {180, 135, 90, 45, 0};
    servoController.setAngles(channels, angles2, 5, true);
    delay(2000);
    
    Serial.println("所有舵机回到中位...");
    servoController.setAllToCenter(0, 4);
    delay(1000);
    
    Serial.println("舵机测试完成！");
}

void setup() {
    setupHardware();
    
    delay(2000);
    
    testAllServos();
    delay(1000);
    
    Serial.println("\n=== 进入主循环 ===");
}

void loop() {
    static bool direction = true;
    static uint8_t currentAngle = 90;
    
    if (direction) {
        currentAngle += 5;
        if (currentAngle >= 180) direction = false;
    } else {
        currentAngle -= 5;
        if (currentAngle <= 0) direction = true;
    }
    
    servoController.setAngle(0, currentAngle, false);
    servoController.setAngle(1, 180 - currentAngle, false);
    servoController.setAngle(2, currentAngle, false);
    
    delay(50);
}