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
HANDLE g_stop_event = nullptr;

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
  // This thread only owns orderly teardown after initialization. Keep it
  // completely asleep during gameplay instead of polling four times a second.
  if (g_stop_event) WaitForSingleObject(g_stop_event, INFINITE);
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
  g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!g_stop_event) return false;
  DWORD thread_id = 0;
  g_worker = CreateThread(nullptr, 0, Worker, nullptr, 0, &thread_id);
  if (!g_worker) {
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
  }
  return g_worker != nullptr;
}

void NotifyProcessDetach() {
  InterlockedExchange(&g_stopping, 1);
  if (g_stop_event) SetEvent(g_stop_event);
}

}  // namespace wfcds
