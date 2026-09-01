#pragma once

#include <cstdint>

namespace fo4ds {

struct Config {
  bool enabled = true;
  bool actions_enabled = true;
  bool scanlines = true;
  uint16_t touch_port = 7788;
  uint32_t diagnostic_refresh_hz = 10;
};

const Config& LoadConfig();

}  // namespace fo4ds
