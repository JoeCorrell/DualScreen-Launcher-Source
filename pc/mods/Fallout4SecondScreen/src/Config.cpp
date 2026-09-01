#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

#include "Log.h"

namespace fo4ds {
namespace {

Config g_config;
std::once_flag g_once;

std::wstring ConfigPath() {
  std::vector<wchar_t> buffer(MAX_PATH);
  for (;;) {
    const DWORD count = GetModuleFileNameW(
        nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count) return {};
    if (count < buffer.size()) {
      std::wstring path(buffer.data(), count);
      const size_t slash = path.find_last_of(L"\\/");
      if (slash == std::wstring::npos) return {};
      path.resize(slash + 1);
      path += L"Fallout4SecondScreen.ini";
      return path;
    }
    if (buffer.size() >= 32768) return {};
    buffer.resize((std::min)(buffer.size() * 2, static_cast<size_t>(32768)));
  }
}

void Read() {
  const std::wstring path = ConfigPath();
  if (path.empty()) {
    Log(LogLevel::kError, "[CONFIG] could not resolve the configuration path");
    return;
  }
  g_config.enabled =
      GetPrivateProfileIntW(L"General", L"Enabled", 1, path.c_str()) != 0;
  g_config.actions_enabled =
      GetPrivateProfileIntW(L"General", L"ActionsEnabled", 1, path.c_str()) != 0;
  g_config.scanlines =
      GetPrivateProfileIntW(L"Rendering", L"Scanlines", 1, path.c_str()) != 0;
  g_config.touch_port = static_cast<uint16_t>(std::clamp(
      GetPrivateProfileIntW(L"General", L"TouchPort", 7788, path.c_str()), 1024u,
      65535u));
  g_config.diagnostic_refresh_hz = static_cast<uint32_t>(std::clamp(
      GetPrivateProfileIntW(L"Rendering", L"DiagnosticRefreshHz", 10,
                            path.c_str()),
      2u, 30u));
  Log(LogLevel::kInfo,
      "[CONFIG] enabled=%u actions=%u touch_port=%u refresh_hz=%u scanlines=%u",
      g_config.enabled ? 1u : 0u, g_config.actions_enabled ? 1u : 0u,
      g_config.touch_port, g_config.diagnostic_refresh_hz,
      g_config.scanlines ? 1u : 0u);
}

}  // namespace

const Config& LoadConfig() {
  std::call_once(g_once, Read);
  return g_config;
}

}  // namespace fo4ds
