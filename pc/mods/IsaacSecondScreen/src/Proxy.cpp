#include <windows.h>

#include <cstring>

#include "Log.h"
#include "Mod.h"

namespace {

HMODULE g_proxy = nullptr;
HMODULE g_real = nullptr;
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
volatile LONG g_runtime_started = 0;

using MmResult = UINT;
struct TimeCaps {
  UINT period_min;
  UINT period_max;
};
constexpr MmResult kTimerErrorCannotComplete = 97;

using TimeBeginPeriodFn = MmResult(WINAPI*)(UINT);
using TimeEndPeriodFn = MmResult(WINAPI*)(UINT);
using TimeGetDevCapsFn = MmResult(WINAPI*)(TimeCaps*, UINT);

TimeBeginPeriodFn g_time_begin_period = nullptr;
TimeEndPeriodFn g_time_end_period = nullptr;
TimeGetDevCapsFn g_time_get_dev_caps = nullptr;

template <typename T>
T Resolve(const char* name) {
  const FARPROC address = g_real ? GetProcAddress(g_real, name) : nullptr;
  T result = nullptr;
  static_assert(sizeof(result) == sizeof(address),
                "Windows function pointers must have equal width");
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

bool IsI386Module(HMODULE module) {
  if (!module) return false;
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return false;
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
      reinterpret_cast<const unsigned char*>(module) + dos->e_lfanew);
  return nt->Signature == IMAGE_NT_SIGNATURE &&
         nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
         nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
}

BOOL CALLBACK InitializeRealWinmm(PINIT_ONCE, PVOID, PVOID*) {
  wchar_t path[MAX_PATH] = {};
  UINT length = GetSystemWow64DirectoryW(path, MAX_PATH);
  if (!length || length >= MAX_PATH - 11)
    length = GetSystemDirectoryW(path, MAX_PATH);
  if (!length || length >= MAX_PATH - 11) return FALSE;
  lstrcatW(path, L"\\winmm.dll");
  g_real = LoadLibraryW(path);
  if (!g_real || g_real == g_proxy || !IsI386Module(g_real)) {
    if (g_real && g_real != g_proxy) FreeLibrary(g_real);
    g_real = nullptr;
    return FALSE;
  }
  g_time_begin_period = Resolve<TimeBeginPeriodFn>("timeBeginPeriod");
  g_time_end_period = Resolve<TimeEndPeriodFn>("timeEndPeriod");
  g_time_get_dev_caps = Resolve<TimeGetDevCapsFn>("timeGetDevCaps");
  if (!g_time_begin_period || !g_time_end_period || !g_time_get_dev_caps) {
    FreeLibrary(g_real);
    g_real = nullptr;
    return FALSE;
  }
  return TRUE;
}

bool EnsureRealWinmm() {
  return InitOnceExecuteOnce(&g_once, InitializeRealWinmm, nullptr, nullptr) !=
             FALSE &&
         g_real != nullptr;
}

void StartRuntimeOnce() {
  if (InterlockedCompareExchange(&g_runtime_started, -1, 0) != 0) return;

  HMODULE pinned = nullptr;
  const DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                      GET_MODULE_HANDLE_EX_FLAG_PIN;
  if (!GetModuleHandleExW(flags, reinterpret_cast<LPCWSTR>(&g_proxy), &pinned) ||
      pinned != g_proxy || !isaacds::StartMod(g_proxy)) {
    InterlockedExchange(&g_runtime_started, 0);
    return;
  }
  InterlockedExchange(&g_runtime_started, 1);
  isaacds::Log(isaacds::LogLevel::kInfo,
               "[Proxy] PE32 winmm forwarding active; native panel bootstrap "
               "started outside loader lock");
}

}  // namespace

extern "C" MmResult WINAPI timeBeginPeriod(UINT period) {
  if (!EnsureRealWinmm()) return kTimerErrorCannotComplete;
  StartRuntimeOnce();
  return g_time_begin_period(period);
}

extern "C" MmResult WINAPI timeEndPeriod(UINT period) {
  if (!EnsureRealWinmm()) return kTimerErrorCannotComplete;
  StartRuntimeOnce();
  return g_time_end_period(period);
}

extern "C" MmResult WINAPI timeGetDevCaps(TimeCaps* capabilities,
                                            UINT bytes) {
  if (!EnsureRealWinmm()) return kTimerErrorCannotComplete;
  StartRuntimeOnce();
  return g_time_get_dev_caps(capabilities, bytes);
}

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_proxy = module;
    DisableThreadLibraryCalls(module);
  } else if (reason == DLL_PROCESS_DETACH) {
    isaacds::NotifyProcessDetach();
  }
  return TRUE;
}
