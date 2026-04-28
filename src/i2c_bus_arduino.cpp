#include "board_i2c.hpp"

#include <Arduino.h>
#include <Wire.h>

class ArduinoI2cBus final : public II2cBus {
 public:
  bool write_register(uint8_t device_address, uint8_t reg, uint8_t value) override {
    Wire.beginTransmission(device_address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }

  bool read_registers(uint8_t device_address, uint8_t start_reg, uint8_t* data,
                      size_t len) override {
    Wire.beginTransmission(device_address);
    Wire.write(start_reg);
    if (Wire.endTransmission(false) != 0) {
      return false;
    }

    const size_t received = Wire.requestFrom(static_cast<int>(device_address),
                                             static_cast<int>(len), static_cast<int>(true));
    if (received != len) {
      return false;
    }

    for (size_t i = 0; i < len; ++i) {
      if (!Wire.available()) {
        return false;
      }
      data[i] = static_cast<uint8_t>(Wire.read());
    }

    return true;
  }
};

void board_i2c_init(int sda_pin, int scl_pin) {
  Wire.begin(sda_pin, scl_pin);
}

ArduinoI2cBus& board_i2c_bus() {
  static ArduinoI2cBus bus;
  return bus;
}
