#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

#include "alarm_logic.hpp"
#include "anomaly_detector.hpp"
#include "console_parser.hpp"
#include "mpu6050_driver.hpp"
#include "telemetry_utils.hpp"

namespace {

int g_failures = 0;

class FakeI2cBus final : public II2cBus {
 public:
  bool write_register(uint8_t device_address, uint8_t reg, uint8_t value) override {
    registers_[key(device_address, reg)] = value;
    return true;
  }

  bool read_registers(uint8_t device_address, uint8_t start_reg, uint8_t* data,
                      size_t len) override {
    for (size_t i = 0; i < len; ++i) {
      auto it = registers_.find(key(device_address, static_cast<uint8_t>(start_reg + i)));
      if (it == registers_.end()) {
        return false;
      }
      data[i] = it->second;
    }
    return true;
  }

  void set_register(uint8_t device_address, uint8_t reg, uint8_t value) {
    registers_[key(device_address, reg)] = value;
  }

 private:
  static uint16_t key(uint8_t device_address, uint8_t reg) {
    return static_cast<uint16_t>((device_address << 8U) | reg);
  }

  std::map<uint16_t, uint8_t> registers_;
};

void expect_true(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++g_failures;
  }
}

void expect_near(float actual, float expected, float tolerance, const char* message) {
  if (std::fabs(actual - expected) > tolerance) {
    std::cerr << "FAIL: " << message << " actual=" << actual << " expected=" << expected
              << '\n';
    ++g_failures;
  }
}

void test_rolling_average() {
  RollingAverage avg(4);
  expect_near(avg.add(10.0f), 10.0f, 0.001f, "rolling average after first sample");
  expect_near(avg.add(20.0f), 15.0f, 0.001f, "rolling average after second sample");
  expect_near(avg.add(30.0f), 20.0f, 0.001f, "rolling average after third sample");
  expect_near(avg.add(40.0f), 25.0f, 0.001f, "rolling average after fourth sample");
  expect_near(avg.add(50.0f), 35.0f, 0.001f, "rolling average after rollover");
}

void test_alarm_latch_and_auto_clear() {
  AlarmEvaluator evaluator;
  ThresholdConfig cfg{};
  cfg.alarm_clear_hold_ms = 3000;
  evaluator.set_config(cfg);

  ProcessedSample sample{};
  sample.gas_raw = 3000;
  AlarmDecision decision = evaluator.evaluate(sample, false, 1000);
  expect_true(decision.state == NodeState::Alarm, "alarm should trigger");
  expect_true(decision.latched_alarm, "alarm should latch");

  sample.gas_raw = 1000;
  decision = evaluator.evaluate(sample, false, 2000);
  expect_true(decision.state == NodeState::Alarm, "latched alarm should stay active");

  decision = evaluator.evaluate(sample, false, 3500);
  expect_true(decision.state == NodeState::Alarm, "alarm should still be latched before hold");

  decision = evaluator.evaluate(sample, false, 5001);
  expect_true(decision.state == NodeState::Normal, "alarm should clear after safe hold");
  expect_true(!decision.latched_alarm, "latched alarm should clear after hold");
}

void test_fault_overrides_alarm_state() {
  AlarmEvaluator evaluator;
  ProcessedSample sample{};
  sample.gas_raw = 1000;
  AlarmDecision decision = evaluator.evaluate(sample, true, 100);
  expect_true(decision.state == NodeState::Fault, "fault should override state");
}

void test_vibration_thresholds() {
  AlarmEvaluator evaluator;
  ProcessedSample sample{};

  sample.vibration_avg_g = 0.20f;
  AlarmDecision decision = evaluator.evaluate(sample, false, 100);
  expect_true(decision.state == NodeState::Warning, "vibration warning should trigger");

  sample.vibration_avg_g = 0.40f;
  decision = evaluator.evaluate(sample, false, 200);
  expect_true(decision.state == NodeState::Alarm, "vibration alarm should trigger");
}

void test_manual_reset_requires_safe_condition() {
  AlarmEvaluator evaluator;
  ThresholdConfig cfg{};
  cfg.alarm_clear_hold_ms = 10000;
  evaluator.set_config(cfg);

  ProcessedSample sample{};
  sample.gas_raw = 3500;
  AlarmDecision decision = evaluator.evaluate(sample, false, 100);
  expect_true(decision.latched_alarm, "manual reset test should start latched");

  expect_true(!evaluator.manual_reset_if_safe(false), "unsafe reset should fail");

  sample.gas_raw = 100;
  decision = evaluator.evaluate(sample, false, 200);
  expect_true(decision.safe_to_reset, "safe condition should be detected");
  expect_true(evaluator.manual_reset_if_safe(decision.safe_to_reset), "safe reset should pass");

  decision = evaluator.evaluate(sample, false, 201);
  expect_true(decision.state == NodeState::Normal, "manual reset should clear latch");
}

void test_console_parser() {
  ParsedCommand help = parse_command("help");
  expect_true(help.ok && help.type == CommandType::Help, "help command should parse");

  ParsedCommand set = parse_command("set gas_alarm 3000");
  expect_true(set.ok && set.type == CommandType::SetThreshold,
              "set command should parse");
  expect_true(set.field == ThresholdField::GasAlarm, "field should parse");
  expect_near(set.value, 3000.0f, 0.001f, "set value should parse");

  ParsedCommand vib = parse_command("set vib_warn 0.25");
  expect_true(vib.ok && vib.field == ThresholdField::VibWarn,
              "vibration threshold command should parse");
  expect_near(vib.value, 0.25f, 0.001f, "vibration threshold value should parse");

  ParsedCommand invalid = parse_command("set nonsense abc");
  expect_true(!invalid.ok, "invalid set command should fail");
}

void test_mpu6050_driver() {
  FakeI2cBus bus;
  bus.set_register(0x68, 0x75, 0x68);

  // Simulate approximately 1.2 g magnitude so the derived dynamic component is non-zero.
  bus.set_register(0x68, 0x3B, 0x4C);
  bus.set_register(0x68, 0x3C, 0xCC);
  bus.set_register(0x68, 0x3D, 0x00);
  bus.set_register(0x68, 0x3E, 0x00);
  bus.set_register(0x68, 0x3F, 0x00);
  bus.set_register(0x68, 0x40, 0x00);

  Mpu6050Driver driver(bus);
  expect_true(driver.initialize(), "mpu6050 should initialize with correct whoami");

  Mpu6050Reading reading{};
  expect_true(driver.read_acceleration(reading), "mpu6050 read should succeed");
  expect_true(reading.valid, "mpu6050 reading should be marked valid");
  expect_near(reading.accel_x_g, 1.19995f, 0.01f, "x acceleration should decode");
  expect_near(reading.vibration_magnitude_g, 0.19995f, 0.02f,
              "vibration magnitude should be derived from total acceleration");
}

void test_telemetry_payload_format() {
  ProcessedSample sample{};
  sample.sample_id = 77;
  sample.temperature_avg_c = 31.5f;
  sample.humidity_avg_pct = 67.25f;
  sample.vibration_avg_g = 0.181f;
  sample.gas_raw = 2100;
  sample.anomaly_score = 0.72f;
  sample.anomaly_detected = true;
  sample.anomaly_source = AnomalySource::Gas;

  AlarmDecision decision{};
  decision.state = NodeState::Warning;
  decision.latched_alarm = true;
  decision.safe_hold_elapsed_ms = 1234;

  char out[384];
  const size_t len =
      format_telemetry_payload(out, sizeof(out), sample, decision, false, "none");
  expect_true(len > 0, "telemetry payload should be generated");

  const std::string json(out);
  expect_true(json.find("\"sample\":77") != std::string::npos, "payload should include sample");
  expect_true(json.find("\"state\":\"WARNING\"") != std::string::npos,
              "payload should include state text");
  expect_true(json.find("\"latched\":1") != std::string::npos,
              "payload should include latch status");
  expect_true(json.find("\"anomaly_score\":0.72") != std::string::npos,
              "payload should include anomaly score");
  expect_true(json.find("\"anomaly\":1") != std::string::npos,
              "payload should include anomaly flag");
  expect_true(json.find("\"anomaly_src\":\"gas\"") != std::string::npos,
              "payload should include anomaly source");
}

void test_retry_interval_elapsed() {
  expect_true(!retry_interval_elapsed(1000, 0, 3000), "retry should wait for interval");
  expect_true(retry_interval_elapsed(3000, 0, 3000), "retry should allow exact interval");
  expect_true(retry_interval_elapsed(4500, 1000, 3000), "retry should allow later attempt");
}

void test_anomaly_detector_spike_detection() {
  AnomalyDetector detector;
  ProcessedSample sample{};

  for (uint32_t i = 0; i < 25; ++i) {
    sample.temperature_avg_c = 26.0f;
    sample.humidity_avg_pct = 50.0f;
    sample.vibration_avg_g = 0.03f;
    sample.gas_raw = 1200;
    const AnomalyResult result = detector.evaluate(sample);
    if (i < 20) {
      expect_true(!result.detected, "warmup should not trigger anomaly");
    }
  }

  sample.vibration_avg_g = 0.80f;
  const AnomalyResult spike = detector.evaluate(sample);
  expect_true(spike.detected, "vibration spike should trigger anomaly");
  expect_true(spike.source == AnomalySource::Vibration,
              "anomaly source should identify dominant signal");
  expect_true(spike.score > 0.60f, "anomaly score should be elevated");
}

}  // namespace

int main() {
  test_rolling_average();
  test_alarm_latch_and_auto_clear();
  test_fault_overrides_alarm_state();
  test_vibration_thresholds();
  test_manual_reset_requires_safe_condition();
  test_console_parser();
  test_mpu6050_driver();
  test_telemetry_payload_format();
  test_retry_interval_elapsed();
  test_anomaly_detector_spike_detection();

  if (g_failures != 0) {
    std::cerr << g_failures << " test(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All host tests passed\n";
  return EXIT_SUCCESS;
}
