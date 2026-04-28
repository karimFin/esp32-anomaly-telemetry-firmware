#pragma once

#include "node_types.hpp"

bool load_config_from_flash(ThresholdConfig& cfg);
bool save_config_to_flash(const ThresholdConfig& cfg);

