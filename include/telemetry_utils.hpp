#pragma once

#include <stddef.h>
#include <stdint.h>

#include "node_types.hpp"

const char* telemetry_state_name(NodeState state);

size_t format_telemetry_payload(char* out, size_t out_len, const ProcessedSample& sample,
                                const AlarmDecision& decision, bool supervisor_fault,
                                const char* supervisor_reason);

bool retry_interval_elapsed(uint32_t now_ms, uint32_t last_attempt_ms,
                            uint32_t retry_interval_ms);

