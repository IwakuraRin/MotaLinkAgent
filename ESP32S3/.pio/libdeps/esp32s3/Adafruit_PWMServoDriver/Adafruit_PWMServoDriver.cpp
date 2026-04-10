/*************************************************** 
  This is a library for our Adafruit 16-channel PWM & Servo driver

  Pick one up today in the adafruit shop!
  ------> http://www.adafruit.com/products/815

  These displays use I2C to communicate, 2 pins are required to  
  interface. For Arduino UNOs, thats SCL -> Analog 5, SDA -> Analog 4

  Adafruit invests time and resources providing this open source code, 
  please support Adafruit and open-source hardware by purchasing 
  products from Adafruit!

  Written by Limor Fried/Ladyada for Adafruit Industries.  
  BSD license, all text above must be included in any redistribution
  
  Ported to mbed by Brian Dickman, mbed.org user bxd.
 ****************************************************/

#include <Adafruit_PWMServoDriver.h>

Adafruit_PWMServoDriver::Adafruit_PWMServoDriver(I2C *i2c, uint8_t addr) {
  _i2c = i2c;
 
  // Arduino WIRE library takes address as 7-bit (unshifted), mbed takes 8 bit.
  // in other words, use the Adafruit library default value 0x40 to talk with the breakout
  // if you use the default resistor config.
  _i2caddr = addr << 1;
}

void Adafruit_PWMServoDriver::begin(void) {
 reset();
}


void Adafruit_PWMServoDriver::reset(void) {
 
 // leave the ALL CALL address enabled (we might use that!)
 // if you don't want ALL CALL, write 00 to offset 00
 write8(PCA9685_MODE1, 0x1);
}

void Adafruit_PWMServoDriver::setPWMFreq(float freq) {
  //Serial.print("Attempting to set freq ");
  //Serial.println(freq);
  
  float prescaleval = 25000000;
  prescaleval /= 4096;
  prescaleval /= freq;
  prescaleval -= 1;
  //printf("Estimated pre-scale: %f\r\n", prescaleval);
  uint8_t prescale = floor(prescaleval + 0.5);
  //printf("Final pre-scale: %f", prescale);  
  
  uint8_t oldmode = read8(PCA9685_MODE1);
  uint8_t newmode = (oldmode&0x7F) | 0x10; // sleep
  write8(PCA9685_MODE1, newmode); // go to sleep
  write8(PCA9685_PRESCALE, prescale); // set the prescaler
  write8(PCA9685_MODE1, oldmode);
  wait_ms(5);
  write8(PCA9685_MODE1, oldmode | 0xa1);  //  This sets the MODE1 register to turn on auto increment.
                                          // This is why the beginTransmission below was not working.
  //  Serial.print("Mode now 0x"); Serial.println(read8(PCA9685_MODE1), HEX);
}

void Adafruit_PWMServoDriver::setPWM(uint8_t num, uint16_t on, uint16_t off) {
  //Serial.print("Setting PWM "); Serial.print(num); Serial.print(": "); Serial.print(on); Serial.print("->"); Serial.println(off);

  uint8_t data[] = { LED0_ON_L+4*num, on, on >> 8, off, off >> 8 };
  _i2c->write(_i2caddr, (const char *)data, 5);
  /*
  WIRE.beginTransmission(_i2caddr);
  WIRE.write(LED0_ON_L+4*num);
  WIRE.write(on);
  WIRE.write(on>>8);
  WIRE.write(off);
  WIRE.write(off>>8);
  WIRE.endTransmission();
  */
}

uint8_t Adafruit_PWMServoDriver::read8(uint8_t addr) {
    char data;
    if(_i2c->write(_i2caddr, (char *)&addr, 1, true))
        printf("I2C ERR: no ack on write before read.\n");
    if(_i2c->read(_i2caddr, &data, 1))
        printf("I2C ERR: no ack on read\n");
    return (uint8_t)data;
}

void Adafruit_PWMServoDriver::write8(uint8_t addr, uint8_t d) {
    char data[] = { addr, d };
    if(_i2c->write(_i2caddr, data, 2))
    {    
        printf("I2C ERR: No ACK on i2c write!");
    }
}