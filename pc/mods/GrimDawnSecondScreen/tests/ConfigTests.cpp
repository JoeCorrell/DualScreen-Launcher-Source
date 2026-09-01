#include <windows.h>

#include <cstdio>

#include "Config.h"

int main() {
  gdds::Config defaults;
  if (defaults.telemetry_tier != 6) {
    std::fprintf(stderr,
                 "FAIL: release defaults must enable validated telemetry\n");
    return 1;
  }

  const gdds::Config loaded = gdds::LoadConfig(GetModuleHandleW(nullptr));
  if (loaded.telemetry_tier != 6) {
    std::fprintf(stderr,
                 "FAIL: missing INI must not silently force safe-mode waiting\n");
    return 1;
  }

  std::puts("Configuration fallback tests passed");
  return 0;
}
