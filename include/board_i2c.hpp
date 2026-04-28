#pragma once

#include "i2c_bus.hpp"

void board_i2c_init(int sda_pin, int scl_pin);
II2cBus& board_i2c_bus();
