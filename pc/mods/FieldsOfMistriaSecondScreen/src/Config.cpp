#include "Config.h"

#include <windows.h>

#include <algorithm>
#include <string>
#include <vector>

#include "Log.h"

namespace fomds {

Config LoadConfig() {
  Config config;
  std::vector<wchar_t> buffer(32768);
  const DWORD length = GetModuleFileNameW(
      nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (!length || length >= buffer.size()) return config;
  std::wstring path(buffer.data(), length);
  const std::size_t slash = path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return config;
  path.resize(slash + 1);
  path.append(L"FieldsOfMistriaSecondScreen.ini");

  const UINT port =
      GetPrivateProfileIntW(L"Input", L"Port", 7788, path.c_str());
  const UINT refresh =
      GetPrivateProfileIntW(L"Panel", L"RefreshHz", 30, path.c_str());
  const UINT panel_x =
      GetPrivateProfileIntW(L"Panel", L"X", 0, path.c_str());
  const UINT panel_y =
      GetPrivateProfileIntW(L"Panel", L"Y", 720, path.c_str());
  const UINT panel_width =
      GetPrivateProfileIntW(L"Panel", L"Width", 1240, path.c_str());
  const UINT panel_height =
      GetPrivateProfileIntW(L"Panel", L"Height", 1080, path.c_str());
  std::vector<wchar_t> bridge_path(32768);
  const DWORD bridge_length = GetPrivateProfileStringW(
      L"Bridge", L"Directory", L"", bridge_path.data(),
      static_cast<DWORD>(bridge_path.size()), path.c_str());
  const UINT bridge_poll =
      GetPrivateProfileIntW(L"Bridge", L"PollMs", 50, path.c_str());
  const UINT bridge_stale =
      GetPrivateProfileIntW(L"Bridge", L"StaleMs", 60000, path.c_str());
  config.input_port = static_cast<std::uint16_t>(
      std::clamp<UINT>(port, 1024, 65535));
  config.refresh_hz = std::clamp<UINT>(refresh, 5, 60);
  config.panel_x = static_cast<std::int32_t>(
      std::clamp<UINT>(panel_x, 0, 8192));
  config.panel_y = static_cast<std::int32_t>(
      std::clamp<UINT>(panel_y, 0, 8192));
  config.panel_width = std::clamp<UINT>(panel_width, 320, 4096);
  config.panel_height = std::clamp<UINT>(panel_height, 240, 4096);
  if (bridge_length && bridge_length < bridge_path.size()) {
    std::vector<wchar_t> expanded(32768);
    const DWORD expanded_length = ExpandEnvironmentStringsW(
        bridge_path.data(), expanded.data(),
        static_cast<DWORD>(expanded.size()));
    if (expanded_length && expanded_length <= expanded.size()) {
      config.bridge_directory.assign(expanded.data(), expanded_length - 1);
    } else {
      config.bridge_directory.assign(bridge_path.data(), bridge_length);
    }
    while (!config.bridge_directory.empty() &&
           (config.bridge_directory.back() == L'\\' ||
            config.bridge_directory.back() == L'/')) {
      config.bridge_directory.pop_back();
    }
  }
  config.bridge_poll_ms = std::clamp<UINT>(bridge_poll, 25, 1000);
  config.bridge_stale_ms = std::clamp<UINT>(bridge_stale, 1000, 60000);
  Log(LogLevel::kInfo,
      "[CONFIG] input_port=%u refresh_hz=%u fallback=%d,%d %ux%u "
      "bridge=%s poll_ms=%u stale_ms=%u",
      static_cast<unsigned>(config.input_port),
      static_cast<unsigned>(config.refresh_hz),
      static_cast<int>(config.panel_x), static_cast<int>(config.panel_y),
      static_cast<unsigned>(config.panel_width),
      static_cast<unsigned>(config.panel_height),
      config.bridge_directory.empty() ? "auto" : "explicit",
      static_cast<unsigned>(config.bridge_poll_ms),
      static_cast<unsigned>(config.bridge_stale_ms));
  return config;
}

}  // namespace fomds
