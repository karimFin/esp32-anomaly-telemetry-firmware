#pragma once

#include <stddef.h>

enum class CommandType {
  Help = 0,
  Status,
  Thresholds,
  ResetAlarm,
  SetThreshold,
  Invalid,
};

enum class ThresholdField {
  TempWarn = 0,
  TempAlarm,
  HumWarn,
  HumAlarm,
  VibWarn,
  VibAlarm,
  GasWarn,
  GasAlarm,
  SafeHoldMs,
  Unknown,
};

struct ParsedCommand {
  CommandType type = CommandType::Invalid;
  ThresholdField field = ThresholdField::Unknown;
  float value = 0.0f;
  bool ok = false;
  const char* error = nullptr;
};

ParsedCommand parse_command(const char* line);
const char* threshold_field_name(ThresholdField field);
void normalize_line(char* line);
