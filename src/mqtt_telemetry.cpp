#include "mqtt_telemetry.hpp"

#include <Arduino.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>

#include <string.h>

#include "telemetry_utils.hpp"

namespace {

#ifndef MQTT_BROKER_HOST
#define MQTT_BROKER_HOST "broker.hivemq.com"
#endif

#ifndef MQTT_TOPIC
#define MQTT_TOPIC "karimFin/esp32-edge-monitor-firmware/telemetry"
#endif

#ifndef MQTT_USERNAME
#define MQTT_USERNAME ""
#endif

#ifndef MQTT_PASSWORD
#define MQTT_PASSWORD ""
#endif

#ifndef MQTT_USE_TLS
#define MQTT_USE_TLS 0
#endif

#ifndef MQTT_TLS_INSECURE
#define MQTT_TLS_INSECURE 1
#endif

constexpr char kWifiSsid[] = "Wokwi-GUEST";
constexpr char kWifiPassword[] = "";
constexpr char kMqttHost[] = MQTT_BROKER_HOST;
constexpr char kTelemetryTopic[] = MQTT_TOPIC;

#if MQTT_USE_TLS
constexpr uint16_t kMqttPort = 8883;
#else
constexpr uint16_t kMqttPort = 1883;
#endif

constexpr char kMqttClientIdPrefix[] = "esp32-edge-monitor-";
constexpr uint32_t kReconnectPeriodMs = 3000;

#if MQTT_USE_TLS
WiFiClientSecure g_wifi_client;
#else
WiFiClient g_wifi_client;
#endif

PubSubClient g_mqtt_client(g_wifi_client);

}  // namespace

void MqttTelemetry::begin() {
  WiFi.mode(WIFI_STA);
#if MQTT_USE_TLS && MQTT_TLS_INSECURE
  g_wifi_client.setInsecure();
#endif
  g_mqtt_client.setServer(kMqttHost, kMqttPort);
  g_mqtt_client.setKeepAlive(30);
}

void MqttTelemetry::loop() {
  if (ensure_wifi()) {
    ensure_mqtt();
  }
  if (g_mqtt_client.connected()) {
    g_mqtt_client.loop();
    publish_queued();
  }
}

bool MqttTelemetry::connected() const {
  return (WiFi.status() == WL_CONNECTED) && g_mqtt_client.connected();
}

bool MqttTelemetry::publish_snapshot(const ProcessedSample& sample,
                                     const AlarmDecision& decision,
                                     bool supervisor_fault,
                                     const char* supervisor_reason) {
  char payload[kPayloadMaxLen];
  format_telemetry_payload(payload, sizeof(payload), sample, decision, supervisor_fault,
                           supervisor_reason);
  if (!enqueue_payload(payload)) {
    return false;
  }

  return publish_queued();
}

bool MqttTelemetry::ensure_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  const uint32_t now = millis();
  if (!retry_interval_elapsed(now, last_wifi_attempt_ms_, kReconnectPeriodMs)) {
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
  if (!retry_interval_elapsed(now, last_mqtt_attempt_ms_, kReconnectPeriodMs)) {
    return false;
  }

  last_mqtt_attempt_ms_ = now;

  char client_id[64];
  snprintf(client_id, sizeof(client_id), "%s%06lx", kMqttClientIdPrefix,
           static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFF));

  Serial.printf("mqtt: connecting broker=%s:%u\n", kMqttHost, kMqttPort);
  const bool has_auth = strlen(MQTT_USERNAME) > 0;
  const bool ok = has_auth ? g_mqtt_client.connect(client_id, MQTT_USERNAME, MQTT_PASSWORD)
                           : g_mqtt_client.connect(client_id);
  if (ok) {
    Serial.println("mqtt: connected");
    return true;
  }

  Serial.printf("mqtt: connect failed rc=%d\n", g_mqtt_client.state());
  return false;
}

bool MqttTelemetry::enqueue_payload(const char* payload) {
  if (payload == nullptr) {
    return false;
  }

  if (queue_count_ == kQueueDepth) {
    // Drop oldest snapshot when offline for too long to keep memory bounded.
    queue_tail_ = (queue_tail_ + 1) % kQueueDepth;
    --queue_count_;
  }

  strncpy(queue_[queue_head_], payload, kPayloadMaxLen - 1);
  queue_[queue_head_][kPayloadMaxLen - 1] = '\0';
  queue_head_ = (queue_head_ + 1) % kQueueDepth;
  ++queue_count_;
  return true;
}

bool MqttTelemetry::publish_queued() {
  if (!connected()) {
    return false;
  }

  bool published_any = false;
  while (!queue_empty()) {
    if (!g_mqtt_client.publish(kTelemetryTopic, queue_[queue_tail_], false)) {
      break;
    }
    published_any = true;
    queue_tail_ = (queue_tail_ + 1) % kQueueDepth;
    --queue_count_;
  }
  return published_any;
}

bool MqttTelemetry::queue_empty() const {
  return queue_count_ == 0;
}
