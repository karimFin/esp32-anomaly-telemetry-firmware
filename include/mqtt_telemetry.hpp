#pragma once

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
  bool ensure_wifi();
  bool ensure_mqtt();
  const char* state_name(NodeState state) const;

  uint32_t last_wifi_attempt_ms_ = 0;
  uint32_t last_mqtt_attempt_ms_ = 0;
};

