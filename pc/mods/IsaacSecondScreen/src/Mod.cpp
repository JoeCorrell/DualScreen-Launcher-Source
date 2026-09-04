#include "Mod.h"

#include "BuildIdentity.h"
#include "LuaBridge.h"
#include "Log.h"
#include "Panel.h"

namespace isaacds {
namespace {

HMODULE g_module = nullptr;
HANDLE g_worker = nullptr;
HANDLE g_stop_event = nullptr;

DWORD WINAPI Worker(void*) {
  const BuildIdentity build = IdentifyExecutable(GetModuleHandleW(nullptr));
  Log(LogLevel::kInfo,
      "[Build] supported=%u machine=%04X timestamp=%08lX image=%08lX "
      "bytes=%llu sha256=%s label=%s",
      build.supported ? 1u : 0u, build.machine,
      static_cast<unsigned long>(build.timestamp),
      static_cast<unsigned long>(build.image_size),
      static_cast<unsigned long long>(build.file_size), build.sha256,
      build.label);
  if (!build.supported) {
    Log(LogLevel::kError,
        "[Lifecycle] exact-build gate rejected the running executable; no "
        "window or hooks were created");
    ShutdownLog();
    return 2;
  }
  if (!InstallLuaBridgeHook(GetModuleHandleW(nullptr))) {
    Log(LogLevel::kError,
        "[Lifecycle] exact-build Lua import hook installation failed");
    ShutdownLog();
    return 3;
  }
  if (!StartPanel(g_module, build.label)) {
    Log(LogLevel::kError, "[Lifecycle] native panel failed to start");
    ShutdownLog();
    return 4;
  }
  Log(LogLevel::kInfo,
      "[Lifecycle] in-process Isaac patch active tid=%lu",
      static_cast<unsigned long>(GetCurrentThreadId()));
  if (g_stop_event) WaitForSingleObject(g_stop_event, INFINITE);
  StopPanel();
  ShutdownLog();
  return 0;
}

}  // namespace

bool StartMod(HMODULE module) {
  if (!module || g_worker) return g_worker != nullptr;
  g_module = module;
  InitializeLog(module);
  g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!g_stop_event) return false;
  DWORD thread_id = 0;
  g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, &thread_id);
  if (!g_worker) {
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
    return false;
  }
  SetThreadPriority(g_worker, THREAD_PRIORITY_BELOW_NORMAL);
  return true;
}

void NotifyProcessDetach() {
  if (g_stop_event) SetEvent(g_stop_event);
}

}  // namespace isaacds
