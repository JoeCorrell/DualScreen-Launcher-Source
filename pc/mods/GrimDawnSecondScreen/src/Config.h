#pragma once

#include <windows.h>

namespace gdds {

struct Config {
  DWORD sample_interval_ms = 100;
  // Tier 6 is the device-validated read-only telemetry path. Falling back to
  // tier 0 when the INI is missing leaves a healthy installation permanently
  // on the waiting screen with no actionable error.
  unsigned telemetry_tier = 6;
  bool panel_enabled = true;
  bool startup_audio_guard = true;
  unsigned short input_port = 7788;
};

Config LoadConfig(HMODULE module);

}  // namespace gdds
