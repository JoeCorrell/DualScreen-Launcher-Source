#pragma once

#include <cstdint>
#include <string>

namespace fomds {

struct Config {
  std::uint16_t input_port = 7788;
  std::uint32_t refresh_hz = 30;
  std::int32_t panel_x = 0;
  std::int32_t panel_y = 720;
  std::uint32_t panel_width = 1240;
  std::uint32_t panel_height = 1080;
  std::wstring bridge_directory;
  std::uint32_t bridge_poll_ms = 50;
  std::uint32_t bridge_stale_ms = 60000;
};

Config LoadConfig();

}  // namespace fomds
