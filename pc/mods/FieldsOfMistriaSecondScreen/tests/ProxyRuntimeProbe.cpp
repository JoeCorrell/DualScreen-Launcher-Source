#include <windows.h>

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "PeExports.h"

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) {
    std::fwprintf(stderr, L"usage: ProxyRuntimeProbe original proxy\n");
    return 2;
  }
  std::vector<PeExport> exports;
  std::string error;
  if (!ReadPeExports(argv[1], &exports, &error) || exports.size() != 1271) {
    std::fprintf(stderr, "could not read reference exports: %s\n", error.c_str());
    return 1;
  }
  HMODULE proxy = LoadLibraryExW(argv[2], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
  if (!proxy) {
    std::fprintf(stderr, "proxy load failed error=%lu\n",
                 static_cast<unsigned long>(GetLastError()));
    return 1;
  }
  // A forwarded-to module may be loaded lazily on the first resolution.
  FARPROC first = GetProcAddress(proxy, exports.front().name.c_str());
  HMODULE original = GetModuleHandleW(L"SDL3_FieldsOfMistriaOriginal.dll");
  if (!first || !original) {
    std::fprintf(stderr, "renamed original was not loaded\n");
    return 1;
  }
  for (const PeExport& entry : exports) {
    FARPROC by_name = GetProcAddress(proxy, entry.name.c_str());
    FARPROC by_ordinal = GetProcAddress(proxy, MAKEINTRESOURCEA(entry.ordinal));
    FARPROC reference = GetProcAddress(original, entry.name.c_str());
    if (!by_name || by_name != by_ordinal || by_name != reference) {
      std::fprintf(stderr, "resolution mismatch %s@%u\n", entry.name.c_str(),
                   entry.ordinal);
      return 1;
    }
  }
  Sleep(750);
  std::wstring log_path(argv[2]);
  const std::size_t slash = log_path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return 1;
  log_path.resize(slash + 1);
  log_path += L"FieldsOfMistriaSecondScreen.log";
  std::ifstream log(log_path.c_str(), std::ios::binary);
  const std::string contents((std::istreambuf_iterator<char>(log)),
                             std::istreambuf_iterator<char>());
  if (contents.find("[COMPAT] fail-closed") == std::string::npos ||
      contents.find("panel host and all mod behavior remain disabled") ==
          std::string::npos) {
    std::fprintf(stderr, "unsupported-host fail-closed log evidence missing\n");
    return 1;
  }
  std::puts("ProxyRuntimeProbe passed: load, all exports, and fail-closed bootstrap");
  // Intentionally do not unload a DLL with a process-lifetime bootstrap worker.
  return 0;
}
