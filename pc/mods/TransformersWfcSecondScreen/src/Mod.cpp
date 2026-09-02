#include "Mod.h"

#include "BuildIdentity.h"
#include "GameBridge.h"
#include "Log.h"
#include "Panel.h"
#include "StateStore.h"

namespace wfcds {
namespace {

HMODULE g_module = nullptr;
HANDLE g_worker = nullptr;
volatile LONG g_stopping = 0;

DWORD WINAPI Worker(void*) {
  Log(LogLevel::kInfo, "[Lifecycle] mod worker ready tid=%lu",
      static_cast<unsigned long>(GetCurrentThreadId()));
  const BuildIdentity build = IdentifyExecutable(GetModuleHandleW(nullptr));
  Log(LogLevel::kInfo,
      "[DualScreen] executable supported=%u machine=%04X timestamp=%08lX image=%08lX bytes=%llu sha256=%s",
      build.supported ? 1u : 0u, build.machine,
      static_cast<unsigned long>(build.timestamp),
      static_cast<unsigned long>(build.image_size),
      static_cast<unsigned long long>(build.file_size), build.sha256);
  ResetState(build.supported ? Lifecycle::kWaitingForWorld :
                               Lifecycle::kUnsupportedBuild,
             build.label, build.supported);
  if (!StartPanel(g_module)) {
    Log(LogLevel::kError, "[DualScreen] panel worker failed to start");
    return 1;
  }
  if (build.supported && !InitializeGameBridge(GetModuleHandleW(nullptr))) {
    ResetState(Lifecycle::kBridgeError, build.label, true);
  }
  while (InterlockedCompareExchange(&g_stopping, 0, 0) == 0) Sleep(250);
  ShutdownGameBridge();
  StopPanel();
  ShutdownLog();
  return 0;
}

}  // namespace

bool StartMod(HMODULE module) {
  if (!module || g_worker) return g_worker != nullptr;
  g_module = module;
  InitializeLog(module);
  DWORD thread_id = 0;
  g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, &thread_id);
  return g_worker != nullptr;
}

void NotifyProcessDetach() {
  InterlockedExchange(&g_stopping, 1);
}

}  // namespace wfcds
