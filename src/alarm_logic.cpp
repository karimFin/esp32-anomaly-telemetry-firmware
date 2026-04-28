#include "alarm_logic.hpp"

AlarmEvaluator::AlarmEvaluator() = default;

void AlarmEvaluator::set_config(const ThresholdConfig& config) {
  config_ = config;
}

const ThresholdConfig& AlarmEvaluator::config() const {
  return config_;
}

void AlarmEvaluator::force_fault(bool active) {
  fault_active_ = active;
}

AlarmEvaluator::Severity AlarmEvaluator::classify(const ProcessedSample& sample) const {
  if (sample.gas_raw >= config_.gas_alarm_raw ||
      sample.temperature_avg_c >= config_.temperature_alarm_c ||
      sample.humidity_avg_pct >= config_.humidity_alarm_pct ||
      sample.vibration_avg_g >= config_.vibration_alarm_g) {
    return Severity::Alarm;
  }

  if (sample.gas_raw >= config_.gas_warning_raw ||
      sample.temperature_avg_c >= config_.temperature_warning_c ||
      sample.humidity_avg_pct >= config_.humidity_warning_pct ||
      sample.vibration_avg_g >= config_.vibration_warning_g) {
    return Severity::Warning;
  }

  return Severity::Normal;
}

AlarmDecision AlarmEvaluator::evaluate(const ProcessedSample& sample, bool fault_active, uint32_t now_ms) {
  force_fault(fault_active);
  const Severity severity = classify(sample);

  if (severity == Severity::Alarm) {
    latched_alarm_ = true;
    safe_window_active_ = false;
    safe_window_start_ms_ = 0;
  } else if (latched_alarm_) {
    if (severity == Severity::Normal) {
      if (!safe_window_active_) {
        safe_window_active_ = true;
        safe_window_start_ms_ = now_ms;
      }

      if ((now_ms - safe_window_start_ms_) >= config_.alarm_clear_hold_ms) {
        latched_alarm_ = false;
        safe_window_active_ = false;
        safe_window_start_ms_ = 0;
      }
    } else {
      safe_window_active_ = false;
      safe_window_start_ms_ = 0;
    }
  } else {
    safe_window_active_ = false;
    safe_window_start_ms_ = 0;
  }

  AlarmDecision decision{};
  decision.latched_alarm = latched_alarm_;
  decision.safe_hold_elapsed_ms =
      safe_window_active_ ? (now_ms - safe_window_start_ms_) : 0;
  decision.safe_to_reset = !fault_active_ && severity == Severity::Normal;

  if (fault_active_) {
    decision.state = NodeState::Fault;
  } else if (latched_alarm_ || severity == Severity::Alarm) {
    decision.state = NodeState::Alarm;
  } else if (severity == Severity::Warning) {
    decision.state = NodeState::Warning;
  } else {
    decision.state = NodeState::Normal;
  }

  return decision;
}

bool AlarmEvaluator::manual_reset_if_safe(bool currently_safe) {
  if (fault_active_ || !currently_safe) {
    return false;
  }

  latched_alarm_ = false;
  safe_window_active_ = false;
  safe_window_start_ms_ = 0;
  return true;
}

RollingAverage::RollingAverage(size_t capacity) : capacity_(capacity) {
  if (capacity_ == 0) {
    capacity_ = 1;
  }
  if (capacity_ > kMaxCapacity) {
    capacity_ = kMaxCapacity;
  }
}

float RollingAverage::add(float value) {
  if (count_ < capacity_) {
    values_[index_] = value;
    sum_ += value;
    ++count_;
  } else {
    sum_ -= values_[index_];
    values_[index_] = value;
    sum_ += value;
  }

  index_ = (index_ + 1) % capacity_;
  return current();
}

float RollingAverage::current() const {
  if (count_ == 0) {
    return 0.0f;
  }
  return sum_ / static_cast<float>(count_);
}

size_t RollingAverage::size() const {
  return count_;
}
