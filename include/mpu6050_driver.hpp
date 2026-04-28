#pragma once

#include <stdint.h>

#include "i2c_bus.hpp"

struct Mpu6050Reading {
  float accel_x_g = 0.0f;
  float accel_y_g = 0.0f;
  float accel_z_g = 0.0f;
  float vibration_magnitude_g = 0.0f;
  bool valid = false;
};

class Mpu6050Driver {
 public:
  explicit Mpu6050Driver(II2cBus& bus, uint8_t address = 0x68);

  bool initialize();
  bool read_acceleration(Mpu6050Reading& reading);

 private:
  static int16_t decode_int16(uint8_t msb, uint8_t lsb);

  II2cBus& bus_;
  uint8_t address_;
  bool initialized_ = false;
};

