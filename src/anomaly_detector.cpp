#include "anomaly_detector.hpp"

#include <math.h>

namespace {

constexpr float kAlpha = 0.08f;
constexpr float kMinSigma = 0.01f;

float z_to_score(float z) {
  // Map z-score from [1.0, 4.0+] into [0.0, 1.0].
  const float normalized = (z - 1.0f) / 3.0f;
  if (normalized < 0.0f) {
    return 0.0f;
  }
  if (normalized > 1.0f) {
    return 1.0f;
  }
  return normalized;
}

float update_and_get_z(float value, float& mean, float& var) {
  const float prev_mean = mean;
  const float delta = value - prev_mean;
  mean = ((1.0f - kAlpha) * mean) + (kAlpha * value);
  var = ((1.0f - kAlpha) * var) + (kAlpha * delta * delta);
  const float sigma = sqrtf(var);
  const float safe_sigma = sigma < kMinSigma ? kMinSigma : sigma;
  return fabsf(value - mean) / safe_sigma;
}

}  // namespace

AnomalyResult AnomalyDetector::evaluate(const ProcessedSample& sample) {
  AnomalyResult result{};

  if (!initialized_) {
    mean_temp_ = sample.temperature_avg_c;
    mean_hum_ = sample.humidity_avg_pct;
    mean_vib_ = sample.vibration_avg_g;
    mean_gas_ = static_cast<float>(sample.gas_raw);
    var_temp_ = 1.0f;
    var_hum_ = 1.0f;
    var_vib_ = 0.05f;
    var_gas_ = 1000.0f;
    sample_count_ = 1;
    initialized_ = true;
    return result;
  }

  const float z_temp = update_and_get_z(sample.temperature_avg_c, mean_temp_, var_temp_);
  const float z_hum = update_and_get_z(sample.humidity_avg_pct, mean_hum_, var_hum_);
  const float z_vib = update_and_get_z(sample.vibration_avg_g, mean_vib_, var_vib_);
  const float z_gas =
      update_and_get_z(static_cast<float>(sample.gas_raw), mean_gas_, var_gas_);
  ++sample_count_;

  float max_z = z_temp;
  result.source = AnomalySource::Temperature;
  if (z_hum > max_z) {
    max_z = z_hum;
    result.source = AnomalySource::Humidity;
  }
  if (z_vib > max_z) {
    max_z = z_vib;
    result.source = AnomalySource::Vibration;
  }
  if (z_gas > max_z) {
    max_z = z_gas;
    result.source = AnomalySource::Gas;
  }

  result.score = z_to_score(max_z);
  if (sample_count_ < kWarmupSamples) {
    result.score = 0.0f;
    result.source = AnomalySource::None;
    return result;
  }

  result.detected = result.score >= kDetectThreshold;
  if (!result.detected) {
    result.source = AnomalySource::None;
  }
  return result;
}

const char* anomaly_source_name(AnomalySource source) {
  switch (source) {
    case AnomalySource::Temperature:
      return "temperature";
    case AnomalySource::Humidity:
      return "humidity";
    case AnomalySource::Vibration:
      return "vibration";
    case AnomalySource::Gas:
      return "gas";
    case AnomalySource::None:
    default:
      return "none";
  }
}
