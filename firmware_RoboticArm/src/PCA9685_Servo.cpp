#include "PCA9685_Servo.h"

PCA9685_Servo::PCA9685_Servo(uint8_t i2cAddress, uint16_t servoMin, 
                             uint16_t servoMax, uint8_t servoFreq) {
    _i2cAddress = i2cAddress;
    _servoMin = servoMin;
    _servoMax = servoMax;
    _servoFreq = servoFreq;
    _initialized = false;
    pwm = new Adafruit_PWMServoDriver(_i2cAddress);
}

PCA9685_Servo::~PCA9685_Servo() {
    if (pwm != nullptr) {
        delete pwm;
        pwm = nullptr;
    }
}

// 修改：移除 TwoWire 参数
bool PCA9685_Servo::begin() {
    Serial.printf("[PCA9685] 初始化舵机驱动板，I2C地址: 0x%02X\n", _i2cAddress);
    
    // 修改：调用不带参数的 begin() 函数
    if (!pwm->begin()) {
        Serial.println("[PCA9685] 初始化失败！请检查I2C连接。");
        _initialized = false;
        return false;
    }
    
    pwm->setPWMFreq(_servoFreq);
    Serial.printf("[PCA9685] PWM频率设置为: %d Hz\n", _servoFreq);
    Serial.printf("[PCA9685] 脉冲范围: %d - %d\n", _servoMin, _servoMax);
    
    _initialized = true;
    return true;
}

uint16_t PCA9685_Servo::_angleToPulse(uint16_t angle) {
    if (angle > 180) angle = 180;
    return map(angle, 0, 180, _servoMin, _servoMax);
}

bool PCA9685_Servo::setAngle(uint8_t channel, uint16_t angle, bool debug) {
    if (!_initialized) {
        Serial.println("[PCA9685] 错误：驱动板未初始化！");
        return false;
    }
    
    if (channel > 15) {
        Serial.printf("[PCA9685] 错误：通道 %d 超出范围 (0-15)\n", channel);
        return false;
    }
    
    uint16_t pulse = _angleToPulse(angle);
    
    if (debug) {
        Serial.printf("[PCA9685] 设置通道 %d: 角度=%d°, 脉冲=%d\n", 
                     channel, angle, pulse);
    }
    
    pwm->setPWM(channel, 0, pulse);
    return true;
}

bool PCA9685_Servo::setPulse(uint8_t channel, uint16_t onTime, uint16_t offTime) {
    if (!_initialized) {
        return false;
    }
    
    pwm->setPWM(channel, onTime, offTime);
    return true;
}

void PCA9685_Servo::setAngles(uint8_t channels[], uint16_t angles[], 
                             uint8_t count, bool debug) {
    for (uint8_t i = 0; i < count; i++) {
        setAngle(channels[i], angles[i], debug);
    }
}

void PCA9685_Servo::setFrequency(uint8_t freq) {
    if (!_initialized) return;
    
    _servoFreq = freq;
    pwm->setPWMFreq(freq);
    Serial.printf("[PCA9685] PWM频率更新为: %d Hz\n", freq);
}

void PCA9685_Servo::testSweep(uint8_t startChannel, uint8_t endChannel, 
                             uint16_t startAngle, uint16_t endAngle, 
                             uint16_t step, uint16_t delayMs) {
    if (!_initialized) {
        Serial.println("[PCA9685] 驱动板未初始化，无法测试");
        return;
    }
    
    Serial.printf("[PCA9685] 开始舵机扫描测试，通道 %d-%d\n", 
                 startChannel, endChannel);
    
    for (uint16_t angle = startAngle; angle <= endAngle; angle += step) {
        for (uint8_t channel = startChannel; channel <= endChannel; channel++) {
            setAngle(channel, angle, false);
        }
        delay(delayMs);
    }
    
    for (uint16_t angle = endAngle; angle >= startAngle; angle -= step) {
        for (uint8_t channel = startChannel; channel <= endChannel; channel++) {
            setAngle(channel, angle, false);
        }
        delay(delayMs);
    }
    
    for (uint8_t channel = startChannel; channel <= endChannel; channel++) {
        setAngle(channel, 90, false);
    }
    
    Serial.println("[PCA9685] 测试完成，所有舵机回到中位");
}

void PCA9685_Servo::easeMove(uint8_t channel, uint16_t startAngle, 
                            uint16_t endAngle, uint16_t durationMs, 
                            uint8_t easingType) {
    if (!_initialized) return;
    
    uint16_t steps = durationMs / 20;
    
    for (uint16_t i = 0; i <= steps; i++) {
        float t = (float)i / steps;
        
        float easedT = t;
        switch (easingType) {
            case 1: // 缓入
                easedT = t * t;
                break;
            case 2: // 缓出
                easedT = 1 - (1 - t) * (1 - t);
                break;
            case 3: // 缓入缓出
                easedT = t < 0.5 ? 2 * t * t : 1 - pow(-2 * t + 2, 2) / 2;
                break;
            default:
                easedT = t;
        }
        
        uint16_t currentAngle = startAngle + (endAngle - startAngle) * easedT;
        setAngle(channel, currentAngle, false);
        delay(20);
    }
}

void PCA9685_Servo::calibrateChannel(uint8_t channel, uint16_t minPulse, 
                                     uint16_t maxPulse) {
    _servoMin = minPulse;
    _servoMax = maxPulse;
    Serial.printf("[PCA9685] 通道 %d 校准完成: 脉冲范围 %d-%d\n", 
                 channel, minPulse, maxPulse);
}

void PCA9685_Servo::setAllAngles(uint16_t angle, uint8_t startChannel, 
                                uint8_t endChannel) {
    for (uint8_t channel = startChannel; channel <= endChannel; channel++) {
        setAngle(channel, angle, false);
    }
}

void PCA9685_Servo::turnOffAll(uint8_t startChannel, uint8_t endChannel) {
    for (uint8_t channel = startChannel; channel <= endChannel; channel++) {
        pwm->setPWM(channel, 0, 0);
    }
    Serial.printf("[PCA9685] 已关闭通道 %d-%d 的PWM输出\n", 
                 startChannel, endChannel);
}

// 注意：scanI2C 是静态函数，不依赖于 PCA9685 对象
void PCA9685_Servo::scanI2C(TwoWire &wire) {
    Serial.println("[PCA9685] 开始扫描I2C总线...");
    
    wire.begin();
    for (byte address = 1; address < 127; address++) {
        wire.beginTransmission(address);
        byte error = wire.endTransmission();
        
        if (error == 0) {
            Serial.printf("  --> 发现设备，地址: 0x%02X\n", address);
        }
    }
    Serial.println("[PCA9685] I2C扫描完成");
}