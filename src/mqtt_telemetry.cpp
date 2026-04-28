#include "mqtt_telemetry.hpp"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>

namespace {

constexpr char kWifiSsid[] = "Wokwi-GUEST";
constexpr char kWifiPassword[] = "";
constexpr char kMqttHost[] = "broker.hivemq.com";
constexpr uint16_t kMqttPort = 1883;
constexpr char kMqttClientIdPrefix[] = "esp32-edge-monitor-";
constexpr char kTelemetryTopic[] = "karimFin/esp32-edge-monitor-firmware/telemetry";
constexpr uint32_t kReconnectPeriodMs = 3000;

WiFiClient g_wifi_client;
PubSubClient g_mqtt_client(g_wifi_client);

}  // namespace

void MqttTelemetry::begin() {
  WiFi.mode(WIFI_STA);
  g_mqtt_client.setServer(kMqttHost, kMqttPort);
}

void MqttTelemetry::loop() {
  if (ensure_wifi()) {
    ensure_mqtt();
  }
  if (g_mqtt_client.connected()) {
    g_mqtt_client.loop();
  }
}

bool MqttTelemetry::connected() const {
  return (WiFi.status() == WL_CONNECTED) && g_mqtt_client.connected();
}

bool MqttTelemetry::publish_snapshot(const ProcessedSample& sample,
                                     const AlarmDecision& decision,
                                     bool supervisor_fault,
                                     const char* supervisor_reason) {
  if (!connected()) {
    return false;
  }

  char payload[384];
  snprintf(
      payload, sizeof(payload),
      "{\"sample\":%lu,\"temp_avg\":%.2f,\"hum_avg\":%.2f,\"vib_avg\":%.3f,\"gas\":%d,"
      "\"state\":\"%s\",\"latched\":%d,\"safe_ms\":%lu,\"fault\":%d,\"reason\":\"%s\"}",
      static_cast<unsigned long>(sample.sample_id), sample.temperature_avg_c,
      sample.humidity_avg_pct, sample.vibration_avg_g, sample.gas_raw,
      state_name(decision.state), decision.latched_alarm ? 1 : 0,
      static_cast<unsigned long>(decision.safe_hold_elapsed_ms),
      supervisor_fault ? 1 : 0,
      supervisor_reason ? supervisor_reason : "none");

  return g_mqtt_client.publish(kTelemetryTopic, payload, false);
}

bool MqttTelemetry::ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  const uint32_t now = millis();
  if ((now - last_wifi_attempt_ms_) < kReconnectPeriodMs) {
    return false;
  }

  last_wifi_attempt_ms_ = now;
  Serial.printf("mqtt: connecting wifi ssid=%s\n", kWifiSsid);
  WiFi.begin(kWifiSsid, kWifiPassword);
  return false;
}

bool MqttTelemetry::ensure_mqtt() {
  if (g_mqtt_client.connected()) {
    return true;
  }

  const uint32_t now = millis();
  if ((now - last_mqtt_attempt_ms_) < kReconnectPeriodMs) {
    return false;
  }

  last_mqtt_attempt_ms_ = now;

  char client_id[64];
  snprintf(client_id, sizeof(client_id), "%s%06lx", kMqttClientIdPrefix,
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFF));

  Serial.printf("mqtt: connecting broker=%s:%u\n", kMqttHost, kMqttPort);
  if (g_mqtt_client.connect(client_id)) {
    Serial.println("mqtt: connected");
    return true;
  }

  Serial.printf("mqtt: connect failed rc=%d\n", g_mqtt_client.state());
  return false;
}

const char* MqttTelemetry::state_name(NodeState state) const {
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

