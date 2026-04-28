#pragma once

#include <stddef.h>
#include <stdint.h>

class II2cBus {
 public:
  virtual ~II2cBus() = default;

  virtual bool write_register(uint8_t device_address, uint8_t reg, uint8_t value) = 0;
  virtual bool read_registers(uint8_t device_address, uint8_t start_reg, uint8_t* data,
                              size_t len) = 0;
};

