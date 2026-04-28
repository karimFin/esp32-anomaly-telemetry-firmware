#include "console_parser.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

char* trim_in_place(char* s) {
  while (*s != '\0' && isspace(static_cast<unsigned char>(*s))) {
    ++s;
  }

  size_t len = strlen(s);
  while (len > 0 && isspace(static_cast<unsigned char>(s[len - 1]))) {
    s[len - 1] = '\0';
    --len;
  }

  return s;
}

bool equals(const char* a, const char* b) {
  return strcmp(a, b) == 0;
}

ThresholdField parse_field(const char* field) {
  if (equals(field, "temp_warn")) {
    return ThresholdField::TempWarn;
  }
  if (equals(field, "temp_alarm")) {
    return ThresholdField::TempAlarm;
  }
  if (equals(field, "hum_warn")) {
    return ThresholdField::HumWarn;
  }
  if (equals(field, "hum_alarm")) {
    return ThresholdField::HumAlarm;
  }
  if (equals(field, "vib_warn")) {
    return ThresholdField::VibWarn;
  }
  if (equals(field, "vib_alarm")) {
    return ThresholdField::VibAlarm;
  }
  if (equals(field, "gas_warn")) {
    return ThresholdField::GasWarn;
  }
  if (equals(field, "gas_alarm")) {
    return ThresholdField::GasAlarm;
  }
  if (equals(field, "safe_ms")) {
    return ThresholdField::SafeHoldMs;
  }
  return ThresholdField::Unknown;
}

}  // namespace

void normalize_line(char* line) {
  for (size_t i = 0; line[i] != '\0'; ++i) {
    line[i] = static_cast<char>(tolower(static_cast<unsigned char>(line[i])));
  }
}

const char* threshold_field_name(ThresholdField field) {
  switch (field) {
    case ThresholdField::TempWarn:
      return "temp_warn";
    case ThresholdField::TempAlarm:
      return "temp_alarm";
    case ThresholdField::HumWarn:
      return "hum_warn";
    case ThresholdField::HumAlarm:
      return "hum_alarm";
    case ThresholdField::VibWarn:
      return "vib_warn";
    case ThresholdField::VibAlarm:
      return "vib_alarm";
    case ThresholdField::GasWarn:
      return "gas_warn";
    case ThresholdField::GasAlarm:
      return "gas_alarm";
    case ThresholdField::SafeHoldMs:
      return "safe_ms";
    case ThresholdField::Unknown:
    default:
      return "unknown";
  }
}

ParsedCommand parse_command(const char* line) {
  ParsedCommand cmd{};
  cmd.error = "invalid command";

  char buffer[96];
  strncpy(buffer, line, sizeof(buffer) - 1);
  buffer[sizeof(buffer) - 1] = '\0';

  char* trimmed = trim_in_place(buffer);
  normalize_line(trimmed);

  if (*trimmed == '\0') {
    cmd.error = "empty command";
    return cmd;
  }

  if (equals(trimmed, "help")) {
    cmd.type = CommandType::Help;
    cmd.ok = true;
    cmd.error = nullptr;
    return cmd;
  }

  if (equals(trimmed, "status")) {
    cmd.type = CommandType::Status;
    cmd.ok = true;
    cmd.error = nullptr;
    return cmd;
  }

  if (equals(trimmed, "thresholds")) {
    cmd.type = CommandType::Thresholds;
    cmd.ok = true;
    cmd.error = nullptr;
    return cmd;
  }

  if (equals(trimmed, "reset_alarm")) {
    cmd.type = CommandType::ResetAlarm;
    cmd.ok = true;
    cmd.error = nullptr;
    return cmd;
  }

  if (strncmp(trimmed, "set ", 4) == 0) {
    char field[32];
    char value_text[32];

    if (sscanf(trimmed, "set %31s %31s", field, value_text) != 2) {
      cmd.error = "usage: set <field> <value>";
      return cmd;
    }

    cmd.field = parse_field(field);
    if (cmd.field == ThresholdField::Unknown) {
      cmd.error = "unknown threshold field";
      return cmd;
    }

    char* end_ptr = nullptr;
    cmd.value = strtof(value_text, &end_ptr);
    if (end_ptr == value_text || *end_ptr != '\0') {
      cmd.error = "value must be numeric";
      return cmd;
    }

    cmd.type = CommandType::SetThreshold;
    cmd.ok = true;
    cmd.error = nullptr;
    return cmd;
  }

  return cmd;
}
