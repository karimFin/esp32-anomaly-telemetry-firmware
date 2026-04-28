#include <Arduino.h>
#include <DHTesp.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "alarm_logic.hpp"
#include "anomaly_detector.hpp"
#include "board_i2c.hpp"
#include "config_persistence.hpp"
#include "console_parser.hpp"
#include "mqtt_telemetry.hpp"
#include "mpu6050_driver.hpp"
#include "node_types.hpp"

namespace {

constexpr int kPinDht = 15;
constexpr int kPinGas = 34;
constexpr int kPinI2cSda = 21;
constexpr int kPinI2cScl = 22;
constexpr int kPinLedGreen = 25;
constexpr int kPinLedYellow = 26;
constexpr int kPinLedRed = 27;

constexpr uint32_t kSensorPeriodMs = 500;
constexpr uint32_t kSupervisorPeriodMs = 1000;
constexpr uint32_t kTaskTimeoutMs = 2500;
constexpr size_t kConsoleBufferLength = 96;

enum class TaskId : uint8_t {
  Sensor = 0,
  Processing,
  Control,
  Console,
  Supervisor,
  Telemetry,
  Count,
};

struct SharedRuntime {
  ProcessedSample latest_sample{};
  AlarmDecision latest_decision{};
  ThresholdConfig config{};
  bool supervisor_fault = false;
  char supervisor_reason[64] = "none";
};

QueueHandle_t g_sensor_queue = nullptr;
QueueHandle_t g_processed_queue = nullptr;
DHTesp g_dht;
Mpu6050Driver g_mpu(board_i2c_bus());
MqttTelemetry g_mqtt;
portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t g_heartbeat_ms[static_cast<size_t>(TaskId::Count)] = {};
volatile bool g_manual_reset_requested = false;
SharedRuntime g_runtime{};

const char* state_name(NodeState state) {
  switch (state) {
    case NodeState::Normal:
      return "NORMAL";
    case NodeState::Warning:
      return "WARNING";
    case NodeState::Alarm:
      return "ALARM";
    case NodeState::Fault:
      return "FAULT";
    default:
      return "UNKNOWN";
  }
}

void update_heartbeat(TaskId id) {
  g_heartbeat_ms[static_cast<size_t>(id)] = millis();
}

void set_leds(bool green, bool yellow, bool red) {
  digitalWrite(kPinLedGreen, green ? HIGH : LOW);
  digitalWrite(kPinLedYellow, yellow ? HIGH : LOW);
  digitalWrite(kPinLedRed, red ? HIGH : LOW);
}

void render_state_leds(NodeState state, bool phase) {
  switch (state) {
    case NodeState::Normal:
      set_leds(true, false, false);
      break;
    case NodeState::Warning:
      set_leds(false, phase, false);
      break;
    case NodeState::Alarm:
      set_leds(false, false, phase);
      break;
    case NodeState::Fault:
      set_leds(false, phase, !phase);
      break;
  }
}

SharedRuntime snapshot_runtime() {
  SharedRuntime copy{};
  portENTER_CRITICAL(&g_lock);
  copy = g_runtime;
  portEXIT_CRITICAL(&g_lock);
  return copy;
}

void store_runtime(const ProcessedSample& sample, const AlarmDecision& decision,
                   bool supervisor_fault, const char* reason,
                   const ThresholdConfig& config) {
  portENTER_CRITICAL(&g_lock);
  g_runtime.latest_sample = sample;
  g_runtime.latest_decision = decision;
  g_runtime.supervisor_fault = supervisor_fault;
  g_runtime.config = config;
  strncpy(g_runtime.supervisor_reason, reason, sizeof(g_runtime.supervisor_reason) - 1);
  g_runtime.supervisor_reason[sizeof(g_runtime.supervisor_reason) - 1] = '\0';
  portEXIT_CRITICAL(&g_lock);
}

void save_config(const ThresholdConfig& cfg, bool persist_to_flash = true) {
  portENTER_CRITICAL(&g_lock);
  g_runtime.config = cfg;
  portEXIT_CRITICAL(&g_lock);

  if (persist_to_flash && !save_config_to_flash(cfg)) {
    Serial.println("warning: failed to persist config to flash");
  }
}

void request_manual_reset() {
  g_manual_reset_requested = true;
}

bool consume_manual_reset_request() {
  if (!g_manual_reset_requested) {
    return false;
  }
  g_manual_reset_requested = false;
  return true;
}

void print_help() {
  Serial.println("commands:");
  Serial.println("  help");
  Serial.println("  status");
  Serial.println("  thresholds");
  Serial.println("  reset_alarm");
  Serial.println("  set temp_warn <value>");
  Serial.println("  set temp_alarm <value>");
  Serial.println("  set hum_warn <value>");
  Serial.println("  set hum_alarm <value>");
  Serial.println("  set vib_warn <value>");
  Serial.println("  set vib_alarm <value>");
  Serial.println("  set gas_warn <value>");
  Serial.println("  set gas_alarm <value>");
  Serial.println("  set safe_ms <value>");
}

void print_thresholds(const ThresholdConfig& cfg) {
  Serial.printf("thresholds: temp_warn=%.2f temp_alarm=%.2f hum_warn=%.2f hum_alarm=%.2f vib_warn=%.3f vib_alarm=%.3f gas_warn=%d gas_alarm=%d safe_ms=%lu\n",
                cfg.temperature_warning_c, cfg.temperature_alarm_c,
                cfg.humidity_warning_pct, cfg.humidity_alarm_pct,
                cfg.vibration_warning_g, cfg.vibration_alarm_g, cfg.gas_warning_raw,
                cfg.gas_alarm_raw, static_cast<unsigned long>(cfg.alarm_clear_hold_ms));
}

bool apply_threshold_update(ThresholdConfig& cfg, ThresholdField field, float value) {
  switch (field) {
    case ThresholdField::TempWarn:
      cfg.temperature_warning_c = value;
      return cfg.temperature_warning_c < cfg.temperature_alarm_c;
    case ThresholdField::TempAlarm:
      cfg.temperature_alarm_c = value;
      return cfg.temperature_warning_c < cfg.temperature_alarm_c;
    case ThresholdField::HumWarn:
      cfg.humidity_warning_pct = value;
      return cfg.humidity_warning_pct < cfg.humidity_alarm_pct;
    case ThresholdField::HumAlarm:
      cfg.humidity_alarm_pct = value;
      return cfg.humidity_warning_pct < cfg.humidity_alarm_pct;
    case ThresholdField::VibWarn:
      cfg.vibration_warning_g = value;
      return cfg.vibration_warning_g < cfg.vibration_alarm_g;
    case ThresholdField::VibAlarm:
      cfg.vibration_alarm_g = value;
      return cfg.vibration_warning_g < cfg.vibration_alarm_g;
    case ThresholdField::GasWarn:
      cfg.gas_warning_raw = static_cast<int>(value);
      return cfg.gas_warning_raw < cfg.gas_alarm_raw;
    case ThresholdField::GasAlarm:
      cfg.gas_alarm_raw = static_cast<int>(value);
      return cfg.gas_warning_raw < cfg.gas_alarm_raw;
    case ThresholdField::SafeHoldMs:
      cfg.alarm_clear_hold_ms = static_cast<uint32_t>(value);
      return true;
    case ThresholdField::Unknown:
    default:
      return false;
  }
}

void handle_console_command(const ParsedCommand& cmd) {
  SharedRuntime runtime = snapshot_runtime();

  if (!cmd.ok) {
    Serial.printf("error: %s\n", cmd.error ? cmd.error : "invalid command");
    return;
  }

  switch (cmd.type) {
    case CommandType::Help:
      print_help();
      break;
    case CommandType::Status:
      Serial.printf("status: state=%s latched=%d safe_to_reset=%d safe_elapsed_ms=%lu fault=%d reason=%s mqtt=%d sample=%lu temp=%.2f hum=%.2f vib=%.3f gas=%d anomaly_score=%.2f anomaly=%d anomaly_src=%s\n",
                    state_name(runtime.latest_decision.state),
                    runtime.latest_decision.latched_alarm ? 1 : 0,
                    runtime.latest_decision.safe_to_reset ? 1 : 0,
                    static_cast<unsigned long>(runtime.latest_decision.safe_hold_elapsed_ms),
                    runtime.supervisor_fault ? 1 : 0,
                    runtime.supervisor_reason,
                    g_mqtt.connected() ? 1 : 0,
                    static_cast<unsigned long>(runtime.latest_sample.sample_id),
                    runtime.latest_sample.temperature_avg_c,
                    runtime.latest_sample.humidity_avg_pct,
                    runtime.latest_sample.vibration_avg_g,
                    runtime.latest_sample.gas_raw,
                    runtime.latest_sample.anomaly_score,
                    runtime.latest_sample.anomaly_detected ? 1 : 0,
                    anomaly_source_name(runtime.latest_sample.anomaly_source));
      break;
    case CommandType::Thresholds:
      print_thresholds(runtime.config);
      break;
    case CommandType::ResetAlarm:
      request_manual_reset();
      Serial.println("manual reset requested");
      break;
    case CommandType::SetThreshold: {
      ThresholdConfig cfg = runtime.config;
      if (!apply_threshold_update(cfg, cmd.field, cmd.value)) {
        Serial.println("error: invalid threshold update");
        break;
      }
      save_config(cfg);
      Serial.printf("updated %s to %.2f\n", threshold_field_name(cmd.field), cmd.value);
      print_thresholds(cfg);
      break;
    }
    case CommandType::Invalid:
    default:
      Serial.println("error: unsupported command");
      break;
  }
}

void sensor_task(void*) {
  uint32_t sample_id = 0;

  while (true) {
    update_heartbeat(TaskId::Sensor);
    const uint32_t now = millis();

    TempAndHumidity th = g_dht.getTempAndHumidity();
    if (isnan(th.temperature) || isnan(th.humidity)) {
      Serial.println("sensor: dht read failed");
      vTaskDelay(pdMS_TO_TICKS(kSensorPeriodMs));
      continue;
    }

    SensorSample sample{};
    sample.temperature_c = th.temperature;
    sample.humidity_pct = th.humidity;

    Mpu6050Reading motion{};
    if (g_mpu.read_acceleration(motion)) {
      sample.vibration_g = motion.vibration_magnitude_g;
    } else {
      sample.vibration_g = 0.0f;
      static uint32_t last_mpu_error_ms = 0;
      if ((now - last_mpu_error_ms) >= 2000) {
        Serial.println("sensor: mpu6050 read failed");
        last_mpu_error_ms = now;
      }
    }

    sample.gas_raw = analogRead(kPinGas);
    sample.sample_id = sample_id++;
    sample.timestamp_ms = now;

    if (xQueueSend(g_sensor_queue, &sample, pdMS_TO_TICKS(50)) != pdPASS) {
      Serial.println("sensor: queue full, dropping sample");
    }

    vTaskDelay(pdMS_TO_TICKS(kSensorPeriodMs));
  }
}

void processing_task(void*) {
  RollingAverage temp_avg(8);
  RollingAverage hum_avg(8);
  RollingAverage vib_avg(8);
  AnomalyDetector anomaly_detector;

  while (true) {
    SensorSample sample{};
    if (xQueueReceive(g_sensor_queue, &sample, portMAX_DELAY) == pdPASS) {
      update_heartbeat(TaskId::Processing);

      ProcessedSample processed{};
      processed.temperature_avg_c = temp_avg.add(sample.temperature_c);
      processed.humidity_avg_pct = hum_avg.add(sample.humidity_pct);
      processed.vibration_avg_g = vib_avg.add(sample.vibration_g);
      processed.gas_raw = sample.gas_raw;
      const AnomalyResult anomaly = anomaly_detector.evaluate(processed);
      processed.anomaly_score = anomaly.score;
      processed.anomaly_detected = anomaly.detected;
      processed.anomaly_source = anomaly.source;
      processed.sample_id = sample.sample_id;
      processed.timestamp_ms = sample.timestamp_ms;

      if (xQueueSend(g_processed_queue, &processed, pdMS_TO_TICKS(50)) != pdPASS) {
        Serial.println("processing: queue full, dropping processed sample");
      }
    }
  }
}

void control_task(void*) {
  AlarmEvaluator evaluator;
  bool phase = false;
  uint32_t last_led_update_ms = 0;
  AlarmDecision last_decision{};

  while (true) {
    update_heartbeat(TaskId::Control);

    ProcessedSample processed{};
    if (xQueueReceive(g_processed_queue, &processed, pdMS_TO_TICKS(100)) == pdPASS) {
      const SharedRuntime runtime = snapshot_runtime();

      evaluator.set_config(runtime.config);
      last_decision = evaluator.evaluate(processed, runtime.supervisor_fault, millis());

      if (consume_manual_reset_request()) {
        if (evaluator.manual_reset_if_safe(last_decision.safe_to_reset)) {
          last_decision = evaluator.evaluate(processed, runtime.supervisor_fault, millis());
          Serial.println("control: alarm latch cleared");
        } else {
          Serial.println("control: reset rejected, system not safe");
        }
      }

      store_runtime(processed, last_decision, runtime.supervisor_fault,
                    runtime.supervisor_reason, runtime.config);

      Serial.printf(
          "{\"sample\":%lu,\"temp_avg\":%.2f,\"hum_avg\":%.2f,\"vib_avg\":%.3f,\"gas\":%d,\"state\":\"%s\",\"latched\":%d,\"safe_ms\":%lu,\"anomaly_score\":%.2f,\"anomaly\":%d,\"anomaly_src\":\"%s\"}\n",
          static_cast<unsigned long>(processed.sample_id), processed.temperature_avg_c,
          processed.humidity_avg_pct, processed.vibration_avg_g, processed.gas_raw,
          state_name(last_decision.state), last_decision.latched_alarm ? 1 : 0,
          static_cast<unsigned long>(last_decision.safe_hold_elapsed_ms),
          processed.anomaly_score, processed.anomaly_detected ? 1 : 0,
          anomaly_source_name(processed.anomaly_source));
    }

    const uint32_t now = millis();
    uint32_t period = 200;
    switch (last_decision.state) {
      case NodeState::Normal:
        period = 250;
        break;
      case NodeState::Warning:
        period = 250;
        break;
      case NodeState::Alarm:
        period = 120;
        break;
      case NodeState::Fault:
        period = 150;
        break;
    }

    if ((now - last_led_update_ms) >= period) {
      phase = !phase;
      render_state_leds(last_decision.state, phase);
      last_led_update_ms = now;
    }
  }
}

void console_task(void*) {
  char line[kConsoleBufferLength];
  size_t len = 0;
  memset(line, 0, sizeof(line));

  while (true) {
    update_heartbeat(TaskId::Console);

    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());

      if (c == '\r') {
        continue;
      }

      if (c == '\n') {
        line[len] = '\0';
        ParsedCommand cmd = parse_command(line);
        handle_console_command(cmd);
        len = 0;
        memset(line, 0, sizeof(line));
        continue;
      }

      if (len < (sizeof(line) - 1)) {
        line[len++] = c;
      } else {
        Serial.println("error: command too long");
        len = 0;
        memset(line, 0, sizeof(line));
      }
    }

    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void supervisor_task(void*) {
  while (true) {
    bool fault = false;
    char reason[64] = "none";
    const uint32_t now = millis();

    for (size_t i = 0; i < static_cast<size_t>(TaskId::Count); ++i) {
      const uint32_t age = now - g_heartbeat_ms[i];
      if (g_heartbeat_ms[i] != 0 && age > kTaskTimeoutMs) {
        fault = true;
        snprintf(reason, sizeof(reason), "task_%u_stalled", static_cast<unsigned>(i));
        break;
      }
    }

    SharedRuntime runtime = snapshot_runtime();
    store_runtime(runtime.latest_sample, runtime.latest_decision, fault, reason, runtime.config);
    update_heartbeat(TaskId::Supervisor);

    vTaskDelay(pdMS_TO_TICKS(kSupervisorPeriodMs));
  }
}

void telemetry_task(void*) {
  uint32_t last_publish_ms = 0;
  uint32_t last_sample_id = UINT32_MAX;

  while (true) {
    update_heartbeat(TaskId::Telemetry);
    g_mqtt.loop();

    const SharedRuntime runtime = snapshot_runtime();
    const uint32_t now = millis();

    if ((now - last_publish_ms) >= 2000 &&
        runtime.latest_sample.sample_id != last_sample_id) {
      if (g_mqtt.publish_snapshot(runtime.latest_sample, runtime.latest_decision,
                                  runtime.supervisor_fault, runtime.supervisor_reason)) {
        last_publish_ms = now;
        last_sample_id = runtime.latest_sample.sample_id;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(kPinLedGreen, OUTPUT);
  pinMode(kPinLedYellow, OUTPUT);
  pinMode(kPinLedRed, OUTPUT);
  set_leds(false, false, false);

  analogReadResolution(12);
  board_i2c_init(kPinI2cSda, kPinI2cScl);
  g_dht.setup(kPinDht, DHTesp::DHT22);
  g_mqtt.begin();
  if (!g_mpu.initialize()) {
    Serial.println("warning: mpu6050 init failed, vibration will stay at 0");
  }

  g_sensor_queue = xQueueCreate(8, sizeof(SensorSample));
  g_processed_queue = xQueueCreate(8, sizeof(ProcessedSample));

  if (g_sensor_queue == nullptr || g_processed_queue == nullptr) {
    Serial.println("fatal: queue creation failed");
    while (true) {
      set_leds(false, false, true);
      delay(100);
      set_leds(false, false, false);
      delay(100);
    }
  }

  ThresholdConfig boot_cfg{};
  if (load_config_from_flash(boot_cfg)) {
    save_config(boot_cfg, false);
    Serial.println("config: loaded thresholds from flash");
  } else {
    save_config(boot_cfg, true);
    Serial.println("config: using default thresholds");
  }

  print_help();
  Serial.println("industrial edge node started");

  xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(processing_task, "processing", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(control_task, "control", 4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(console_task, "console", 4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(supervisor_task, "supervisor", 4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(telemetry_task, "telemetry", 4096, nullptr, 1, nullptr, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
