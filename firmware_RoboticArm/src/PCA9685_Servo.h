#ifndef PCA9685_SERVO_H
#define PCA9685_SERVO_H

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

class PCA9685_Servo {
private:
    Adafruit_PWMServoDriver* pwm;
    uint8_t _i2cAddress;
    uint16_t _servoMin;
    uint16_t _servoMax;
    uint8_t _servoFreq;
    bool _initialized;
    
    uint16_t _angleToPulse(uint16_t angle);
    
public:
    PCA9685_Servo(uint8_t i2cAddress = 0x40, 
                  uint16_t servoMin = 150, 
                  uint16_t servoMax = 600, 
                  uint8_t servoFreq = 50);
    
    ~PCA9685_Servo();
    
    // 修改：移除 TwoWire 参数
    bool begin();
    
    bool setAngle(uint8_t channel, uint16_t angle, bool debug = false);
    bool setPulse(uint8_t channel, uint16_t onTime, uint16_t offTime);
    void setAngles(uint8_t channels[], uint16_t angles[], uint8_t count, bool debug = false);
    void setFrequency(uint8_t freq);
    bool isInitialized() { return _initialized; }
    uint8_t getI2CAddress() { return _i2cAddress; }
    
    void testSweep(uint8_t startChannel = 0, uint8_t endChannel = 15, 
                   uint16_t startAngle = 0, uint16_t endAngle = 180, 
                   uint16_t step = 10, uint16_t delayMs = 100);
    
    void easeMove(uint8_t channel, uint16_t startAngle, uint16_t endAngle, 
                  uint16_t durationMs, uint8_t easingType = 0);
    
    void calibrateChannel(uint8_t channel, uint16_t minPulse, uint16_t maxPulse);
    void setAllAngles(uint16_t angle, uint8_t startChannel = 0, uint8_t endChannel = 15);
    
    void setAllToCenter(uint8_t startChannel = 0, uint8_t endChannel = 15) {
        setAllAngles(90, startChannel, endChannel);
    }
    
    void turnOffAll(uint8_t startChannel = 0, uint8_t endChannel = 15);
    
    // 注意：scanI2C 函数仍然需要 Wire 参数
    static void scanI2C(TwoWire &wire = Wire);
};

#endif // PCA9685_SERVO_H