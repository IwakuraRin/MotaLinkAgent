#include <Arduino.h>
#include "PCA9685_Servo.h"

constexpr uint8_t I2C_SDA_PIN = 8;
constexpr uint8_t I2C_SCL_PIN = 9;
constexpr uint8_t SERVO_COUNT = 6;

// PCA9685 通道映射（0~5）
enum ServoChannel : uint8_t {
    SERVO_0 = 0,
    SERVO_1 = 1,
    SERVO_2 = 2,
    SERVO_3 = 3,
    SERVO_4 = 4,
    SERVO_5 = 5
};

PCA9685_Servo servoController(0x40, 150, 600, 50);

struct RobotPose {
    uint16_t angle[SERVO_COUNT];
};

const uint8_t kChannels[SERVO_COUNT] = {
    SERVO_0, SERVO_1, SERVO_2, SERVO_3, SERVO_4, SERVO_5
};

const RobotPose POSE_HOME = {{90, 90, 90, 90, 90, 90}};
const RobotPose POSE_READY = {{60, 120, 90, 90, 70, 110}};
const RobotPose POSE_PICK = {{40, 135, 70, 120, 45, 135}};

void applyPose(const RobotPose &pose, uint16_t durationMs = 1000, bool debug = true) {
    uint16_t targetAngles[SERVO_COUNT];
    for (uint8_t i = 0; i < SERVO_COUNT; i++) {
        targetAngles[i] = pose.angle[i];
    }

    servoController.setAngles(kChannels, targetAngles, SERVO_COUNT, debug);
    delay(durationMs);
}

void setupHardware() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n=== ESP32-S3 PCA9685 舵机控制 ===");

    // 初始化 I2C 总线（ESP32-S3 自定义引脚）
    Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
    Serial.printf("I2C引脚: SDA=%d, SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);

    // 扫描 I2C 设备（不再重复 Wire.begin，避免覆盖上面的自定义引脚）
    PCA9685_Servo::scanI2C(Wire);

    // 初始化舵机控制器
    if (!servoController.begin()) {
        Serial.println("舵机控制器初始化失败！请检查连接。");
        while(1);
    }
    
    Serial.println("硬件初始化完成！");
}

void testAllServos() {
    Serial.println("\n=== 6路舵机测试 ===");

    for (uint8_t channel = 0; channel < SERVO_COUNT; channel++) {
        Serial.printf("测试通道 %d...\n", channel);
        servoController.setAngle(channel, 30, true);
        delay(500);
        servoController.setAngle(channel, 90, true);
        delay(500);
        servoController.setAngle(channel, 150, true);
        delay(500);
        servoController.setAngle(channel, 90, true);
        delay(300);
    }

    Serial.println("6路舵机联动测试...");
    uint16_t patternA[SERVO_COUNT] = {60, 120, 80, 100, 70, 110};
    uint16_t patternB[SERVO_COUNT] = {120, 60, 100, 80, 110, 70};
    servoController.setAngles(kChannels, patternA, SERVO_COUNT, true);
    delay(1500);
    servoController.setAngles(kChannels, patternB, SERVO_COUNT, true);
    delay(1500);

    Serial.println("所有舵机回到中位...");
    servoController.setAllToCenter(0, SERVO_COUNT - 1);
    delay(1000);

    Serial.println("舵机测试完成！");
}

void setup() {
    setupHardware();

    Serial.println("切换至预设姿态...");
    applyPose(POSE_HOME, 1200, true);
    applyPose(POSE_READY, 1200, true);
    applyPose(POSE_PICK, 1200, true);
    applyPose(POSE_HOME, 1200, true);

    delay(2000);

    testAllServos();
    delay(1000);

    Serial.println("\n=== 进入主循环 ===");
}

void loop() {
    static int currentAngle = 90;
    static int step = 2;

    currentAngle += step;
    if (currentAngle >= 140) {
        currentAngle = 140;
        step = -step;
    } else if (currentAngle <= 40) {
        currentAngle = 40;
        step = -step;
    }

    // 简单镜像联动：6路舵机成对反向摆动
    servoController.setAngle(SERVO_0, currentAngle, false);
    servoController.setAngle(SERVO_1, 180 - currentAngle, false);
    servoController.setAngle(SERVO_2, currentAngle, false);
    servoController.setAngle(SERVO_3, 180 - currentAngle, false);
    servoController.setAngle(SERVO_4, currentAngle, false);
    servoController.setAngle(SERVO_5, 180 - currentAngle, false);

    delay(25);
}