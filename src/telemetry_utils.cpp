#include "telemetry_utils.hpp"

#include <stdio.h>

const char* telemetry_state_name(NodeState state) {
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

size_t format_telemetry_payload(char* out, size_t out_len, const ProcessedSample& sample,
                                const AlarmDecision& decision, bool supervisor_fault,
                                const char* supervisor_reason) {
  if (out == nullptr || out_len == 0) {
    return 0;
  }

  const char* reason = supervisor_reason ? supervisor_reason : "none";
  const int written = snprintf(
      out, out_len,
      "{\"sample\":%lu,\"temp_avg\":%.2f,\"hum_avg\":%.2f,\"vib_avg\":%.3f,\"gas\":%d,"
      "\"state\":\"%s\",\"latched\":%d,\"safe_ms\":%lu,\"fault\":%d,\"reason\":\"%s\"}",
      static_cast<unsigned long>(sample.sample_id), sample.temperature_avg_c,
      sample.humidity_avg_pct, sample.vibration_avg_g, sample.gas_raw,
      telemetry_state_name(decision.state), decision.latched_alarm ? 1 : 0,
      static_cast<unsigned long>(decision.safe_hold_elapsed_ms),
      supervisor_fault ? 1 : 0, reason);

  if (written <= 0) {
    out[0] = '\0';
    return 0;
  }

  if (static_cast<size_t>(written) >= out_len) {
    out[out_len - 1] = '\0';
    return out_len - 1;
  }

  return static_cast<size_t>(written);
}

bool retry_interval_elapsed(uint32_t now_ms, uint32_t last_attempt_ms,
                            uint32_t retry_interval_ms) {
  return (now_ms - last_attempt_ms) >= retry_interval_ms;
}

