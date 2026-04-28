#pragma once

#include <stddef.h>
#include <stdint.h>

#include "node_types.hpp"

class MqttTelemetry {
 public:
  void begin();
  void loop();
  bool connected() const;
  bool publish_snapshot(const ProcessedSample& sample, const AlarmDecision& decision,
                        bool supervisor_fault, const char* supervisor_reason);

 private:
  static constexpr size_t kPayloadMaxLen = 384;
  static constexpr size_t kQueueDepth = 8;

  bool ensure_wifi();
  bool ensure_mqtt();
  bool enqueue_payload(const char* payload);
  bool publish_queued();
  bool queue_empty() const;

  char queue_[kQueueDepth][kPayloadMaxLen] = {};
  size_t queue_head_ = 0;
  size_t queue_tail_ = 0;
  size_t queue_count_ = 0;

  uint32_t last_wifi_attempt_ms_ = 0;
  uint32_t last_mqtt_attempt_ms_ = 0;
};
