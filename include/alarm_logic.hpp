#pragma once

#include <stddef.h>

#include "node_types.hpp"

class AlarmEvaluator {
 public:
  AlarmEvaluator();

  void set_config(const ThresholdConfig& config);
  const ThresholdConfig& config() const;

  AlarmDecision evaluate(const ProcessedSample& sample, bool fault_active, uint32_t now_ms);
  bool manual_reset_if_safe(bool currently_safe);
  void force_fault(bool active);

 private:
  enum class Severity {
    Normal = 0,
    Warning,
    Alarm,
  };

  Severity classify(const ProcessedSample& sample) const;

  ThresholdConfig config_{};
  bool latched_alarm_ = false;
  bool fault_active_ = false;
  uint32_t safe_window_start_ms_ = 0;
  bool safe_window_active_ = false;
};

class RollingAverage {
 public:
  explicit RollingAverage(size_t capacity);

  float add(float value);
  float current() const;
  size_t size() const;

 private:
  static constexpr size_t kMaxCapacity = 16;

  float values_[kMaxCapacity]{};
  size_t capacity_ = 0;
  size_t count_ = 0;
  size_t index_ = 0;
  float sum_ = 0.0f;
};
