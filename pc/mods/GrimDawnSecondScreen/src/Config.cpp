#include "Config.h"

#include <cstring>
#include <cwchar>

namespace gdds {

Config LoadConfig(HMODULE module) {
  Config config;
  wchar_t path[MAX_PATH] = {};
  const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
  if (!length || length >= MAX_PATH) return config;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return config;
  constexpr wchar_t kConfigName[] = L"GrimDawnSecondScreen.ini";
  const size_t directory_chars = static_cast<size_t>(slash + 1 - path);
  if (directory_chars +
          sizeof(kConfigName) / sizeof(kConfigName[0]) >
      MAX_PATH)
    return config;
  std::memcpy(slash + 1, kConfigName, sizeof(kConfigName));
  const UINT interval = GetPrivateProfileIntW(
      L"Bridge", L"SampleIntervalMs", config.sample_interval_ms, path);
  if (interval >= 25 && interval <= 1000)
    config.sample_interval_ms = interval;
  const UINT telemetry_tier =
      GetPrivateProfileIntW(L"Bridge", L"TelemetryTier",
                            config.telemetry_tier, path);
  if (telemetry_tier <= 6) config.telemetry_tier = telemetry_tier;
  config.panel_enabled =
      GetPrivateProfileIntW(L"Panel", L"Enabled", 1, path) != 0;
  config.startup_audio_guard =
      GetPrivateProfileIntW(L"Compatibility", L"StartupAudioGuard", 1,
                            path) != 0;
  const UINT port =
      GetPrivateProfileIntW(L"Panel", L"InputPort", config.input_port, path);
  if (port >= 1024 && port <= 65535)
    config.input_port = static_cast<unsigned short>(port);
  return config;
}

}  // namespace gdds
