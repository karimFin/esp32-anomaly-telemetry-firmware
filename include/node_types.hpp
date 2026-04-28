#pragma once

#include <stdint.h>

enum class NodeState : uint8_t {
  Normal = 0,
  Warning,
  Alarm,
  Fault,
};

struct ThresholdConfig {
  float temperature_warning_c = 35.0f;
  float temperature_alarm_c = 45.0f;
  float humidity_warning_pct = 70.0f;
  float humidity_alarm_pct = 85.0f;
  float vibration_warning_g = 0.15f;
  float vibration_alarm_g = 0.35f;
  int gas_warning_raw = 1800;
  int gas_alarm_raw = 2800;
  uint32_t alarm_clear_hold_ms = 10000;
};

struct SensorSample {
  float temperature_c = 0.0f;
  float humidity_pct = 0.0f;
  float vibration_g = 0.0f;
  int gas_raw = 0;
  uint32_t sample_id = 0;
  uint32_t timestamp_ms = 0;
};

struct ProcessedSample {
  float temperature_avg_c = 0.0f;
  float humidity_avg_pct = 0.0f;
  float vibration_avg_g = 0.0f;
  int gas_raw = 0;
  uint32_t sample_id = 0;
  uint32_t timestamp_ms = 0;
};

struct AlarmDecision {
  NodeState state = NodeState::Normal;
  bool latched_alarm = false;
  bool safe_to_reset = false;
  uint32_t safe_hold_elapsed_ms = 0;
};
