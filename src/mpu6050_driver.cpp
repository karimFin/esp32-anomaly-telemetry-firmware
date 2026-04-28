#include "mpu6050_driver.hpp"

#include <math.h>

namespace {

constexpr uint8_t kRegWhoAmI = 0x75;
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegAccelXoutH = 0x3B;
constexpr uint8_t kExpectedWhoAmI = 0x68;
constexpr float kAccelLsbPerG = 16384.0f;

}  // namespace

Mpu6050Driver::Mpu6050Driver(II2cBus& bus, uint8_t address)
    : bus_(bus), address_(address) {}

bool Mpu6050Driver::initialize() {
  uint8_t who_am_i = 0;
  if (!bus_.read_registers(address_, kRegWhoAmI, &who_am_i, 1)) {
    return false;
  }

  if (who_am_i != kExpectedWhoAmI) {
    return false;
  }

  if (!bus_.write_register(address_, kRegPwrMgmt1, 0x00)) {
    return false;
  }

  initialized_ = true;
  return true;
}

bool Mpu6050Driver::read_acceleration(Mpu6050Reading& reading) {
  if (!initialized_ && !initialize()) {
    return false;
  }

  uint8_t raw[6] = {};
  if (!bus_.read_registers(address_, kRegAccelXoutH, raw, sizeof(raw))) {
    return false;
  }

  const int16_t ax = decode_int16(raw[0], raw[1]);
  const int16_t ay = decode_int16(raw[2], raw[3]);
  const int16_t az = decode_int16(raw[4], raw[5]);

  reading.accel_x_g = static_cast<float>(ax) / kAccelLsbPerG;
  reading.accel_y_g = static_cast<float>(ay) / kAccelLsbPerG;
  reading.accel_z_g = static_cast<float>(az) / kAccelLsbPerG;

  const float mag_sq = (reading.accel_x_g * reading.accel_x_g) +
                       (reading.accel_y_g * reading.accel_y_g) +
                       (reading.accel_z_g * reading.accel_z_g);

  // Subtract the nominal 1 g static component to approximate vibration severity.
  const float total_mag_g = sqrtf(mag_sq);
  const float dynamic_mag_g = fabsf(total_mag_g - 1.0f);
  reading.vibration_magnitude_g = dynamic_mag_g;
  reading.valid = true;
  return true;
}

int16_t Mpu6050Driver::decode_int16(uint8_t msb, uint8_t lsb) {
  return static_cast<int16_t>((static_cast<uint16_t>(msb) << 8U) |
                              static_cast<uint16_t>(lsb));
}

