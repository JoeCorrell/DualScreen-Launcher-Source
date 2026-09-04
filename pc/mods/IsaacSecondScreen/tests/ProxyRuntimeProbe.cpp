#include <windows.h>

#include <cstring>
#include <iostream>

template <typename T>
T Symbol(HMODULE module, const char* name) {
  const FARPROC address = GetProcAddress(module, name);
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address));
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

using MmResult = UINT;
struct TimeCaps {
  UINT period_min;
  UINT period_max;
};
constexpr MmResult kTimerNoError = 0;

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: ProxyRuntimeProbe <winmm.dll>\n";
    return 2;
  }
  HMODULE proxy = LoadLibraryA(argv[1]);
  if (!proxy) {
    std::cerr << "LoadLibrary failed error=" << GetLastError() << '\n';
    return 3;
  }

  using Begin = MmResult(WINAPI*)(UINT);
  using End = MmResult(WINAPI*)(UINT);
  using Caps = MmResult(WINAPI*)(TimeCaps*, UINT);
  const Begin begin = Symbol<Begin>(proxy, "timeBeginPeriod");
  const End end = Symbol<End>(proxy, "timeEndPeriod");
  const Caps caps = Symbol<Caps>(proxy, "timeGetDevCaps");
  const FARPROC lua_bridge =
      GetProcAddress(proxy, "luaopen_isaac_second_screen");
  if (!begin || !end || !caps || !lua_bridge) return 4;

  TimeCaps capabilities = {};
  if (caps(&capabilities, sizeof(capabilities)) != kTimerNoError ||
      capabilities.period_min == 0 || capabilities.period_max == 0) {
    return 5;
  }
  const UINT period = capabilities.period_min;
  if (begin(period) != kTimerNoError) return 6;
  if (end(period) != kTimerNoError) return 7;

  // The proxy owns a process-lifetime worker. Let process teardown release it
  // instead of unloading code while that worker may still be returning from
  // the exact-build rejection path used by this unrelated probe executable.
  std::cout << "ProxyRuntimeProbe passed\n";
  return 0;
}
