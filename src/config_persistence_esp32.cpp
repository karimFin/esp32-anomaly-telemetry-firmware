#include "config_persistence.hpp"

#include <Preferences.h>

namespace {

constexpr char kNamespace[] = "edge_cfg";
constexpr char kKeyTw[] = "tw";
constexpr char kKeyTa[] = "ta";
constexpr char kKeyHw[] = "hw";
constexpr char kKeyHa[] = "ha";
constexpr char kKeyVw[] = "vw";
constexpr char kKeyVa[] = "va";
constexpr char kKeyGw[] = "gw";
constexpr char kKeyGa[] = "ga";
constexpr char kKeySafe[] = "safe";
constexpr char kKeyInit[] = "init";

}  // namespace

bool load_config_from_flash(ThresholdConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, true)) {
    return false;
  }

  const bool initialized = prefs.getBool(kKeyInit, false);
  if (!initialized) {
    prefs.end();
    return false;
  }

  cfg.temperature_warning_c = prefs.getFloat(kKeyTw, cfg.temperature_warning_c);
  cfg.temperature_alarm_c = prefs.getFloat(kKeyTa, cfg.temperature_alarm_c);
  cfg.humidity_warning_pct = prefs.getFloat(kKeyHw, cfg.humidity_warning_pct);
  cfg.humidity_alarm_pct = prefs.getFloat(kKeyHa, cfg.humidity_alarm_pct);
  cfg.vibration_warning_g = prefs.getFloat(kKeyVw, cfg.vibration_warning_g);
  cfg.vibration_alarm_g = prefs.getFloat(kKeyVa, cfg.vibration_alarm_g);
  cfg.gas_warning_raw = prefs.getInt(kKeyGw, cfg.gas_warning_raw);
  cfg.gas_alarm_raw = prefs.getInt(kKeyGa, cfg.gas_alarm_raw);
  cfg.alarm_clear_hold_ms = prefs.getUInt(kKeySafe, cfg.alarm_clear_hold_ms);
  prefs.end();
  return true;
}

bool save_config_to_flash(const ThresholdConfig& cfg) {
  Preferences prefs;
  if (!prefs.begin(kNamespace, false)) {
    return false;
  }

  prefs.putFloat(kKeyTw, cfg.temperature_warning_c);
  prefs.putFloat(kKeyTa, cfg.temperature_alarm_c);
  prefs.putFloat(kKeyHw, cfg.humidity_warning_pct);
  prefs.putFloat(kKeyHa, cfg.humidity_alarm_pct);
  prefs.putFloat(kKeyVw, cfg.vibration_warning_g);
  prefs.putFloat(kKeyVa, cfg.vibration_alarm_g);
  prefs.putInt(kKeyGw, cfg.gas_warning_raw);
  prefs.putInt(kKeyGa, cfg.gas_alarm_raw);
  prefs.putUInt(kKeySafe, cfg.alarm_clear_hold_ms);
  prefs.putBool(kKeyInit, true);
  prefs.end();
  return true;
}

