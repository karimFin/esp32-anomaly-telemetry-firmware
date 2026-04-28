#pragma once

#include "node_types.hpp"

struct AnomalyResult {
  float score = 0.0f;
  bool detected = false;
  AnomalySource source = AnomalySource::None;
};

class AnomalyDetector {
 public:
  AnomalyResult evaluate(const ProcessedSample& sample);

 private:
  static constexpr float kAlpha = 0.08f;
  static constexpr float kMinSigma = 0.01f;
  static constexpr float kDetectThreshold = 0.65f;
  static constexpr uint32_t kWarmupSamples = 20;

  float mean_temp_ = 0.0f;
  float mean_hum_ = 0.0f;
  float mean_vib_ = 0.0f;
  float mean_gas_ = 0.0f;
  float var_temp_ = 1.0f;
  float var_hum_ = 1.0f;
  float var_vib_ = 1.0f;
  float var_gas_ = 1.0f;
  uint32_t sample_count_ = 0;
  bool initialized_ = false;
};

const char* anomaly_source_name(AnomalySource source);

