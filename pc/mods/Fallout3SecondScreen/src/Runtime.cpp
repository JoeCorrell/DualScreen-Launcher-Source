#include "Runtime.h"

#include <atomic>

#include "CombatLagFix.h"
#include "Engine.h"
#include "Log.h"
#include "Panel.h"
#include "Perf.h"
#include "StateStore.h"
#include "Version.h"

namespace fo3ds {
namespace {

std::atomic<bool> g_started{false};
std::atomic<bool> g_process_detaching{false};
HANDLE g_stop_event = nullptr;

struct WindowChoice {
  HWND window = nullptr;
  uint64_t area = 0;
};

BOOL CALLBACK WindowSearchProc(HWND window, LPARAM raw_choice) {
  auto* choice = reinterpret_cast<WindowChoice*>(raw_choice);
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id != GetCurrentProcessId() || !IsWindowVisible(window) ||
      GetWindow(window, GW_OWNER))
    return TRUE;
  const LONG_PTR extended = GetWindowLongPtrW(window, GWL_EXSTYLE);
  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  if ((extended & WS_EX_TOOLWINDOW) || (style & WS_CHILD)) return TRUE;
  wchar_t class_name[96] = {};
  GetClassNameW(window, class_name, 96);
  if (lstrcmpW(class_name, L"Fallout3SecondScreenPanel") == 0) return TRUE;
  RECT client = {};
  if (!GetClientRect(window, &client)) return TRUE;
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width < 320 || height < 200) return TRUE;
  const uint64_t area = static_cast<uint64_t>(width) * height;
  if (area > choice->area) {
    choice->window = window;
    choice->area = area;
  }
  return TRUE;
}

HWND FindGameWindow() {
  WindowChoice choice = {};
  EnumWindows(WindowSearchProc, reinterpret_cast<LPARAM>(&choice));
  return choice.window;
}

DWORD WINAPI RuntimeThread(void* parameter) {
  HMODULE module = static_cast<HMODULE>(parameter);
  InitializeLog(module);
  const BuildIdentity build = IdentifyExecutable(GetModuleHandleW(nullptr));
  Log(LogLevel::kInfo,
      "[VERSION] valid=%u i386=%u candidate=%u ts=%08X image=%u text=%08X/%u hash=%016llX",
      build.valid_pe ? 1u : 0u, build.is_i386 ? 1u : 0u,
      build.known_candidate ? 1u : 0u, build.timestamp, build.image_size,
      build.text_rva, build.text_size,
      static_cast<unsigned long long>(build.memory_text_hash));
  Log(LogLevel::kInfo, "[VERSION] file_size=%llu sha256=%s",
      static_cast<unsigned long long>(build.file_size),
      build.file_sha256[0] ? build.file_sha256 : "unavailable");
  Log(LogLevel::kInfo, "[VERSION] %s", build.label);

  const bool bridge_ready = InitializeEngineBridge(build);
  ResetState(bridge_ready ? Lifecycle::kMainMenu : Lifecycle::kUnsupported,
             build.label, bridge_ready);
  if (!StartPanel(module))
    Log(LogLevel::kError, "[PANEL] failed to start optional lower-screen worker");

  // The window timer is only a temporary bounded Phase 2 synchronization point:
  // its callback executes on Fallout's owning UI/game thread and samples small
  // actor-value primitives at 10 Hz. Inventory performs only a bounded
  // allocation-free fingerprint at one-second intervals and rebuilds after a
  // stable two-pass change. A proven dirty hook can replace that fallback.
  if (g_stop_event) {
    HWND game_window = nullptr;
    while (WaitForSingleObject(g_stop_event, 1000) == WAIT_TIMEOUT) {
      FlushPerformanceLog();
      FlushCombatLagDiagnostics();
      if (!bridge_ready) continue;
      if (!game_window || !IsWindow(game_window)) {
        game_window = FindGameWindow();
      }
      if (game_window && !AttachGameThreadTimer(game_window))
        game_window = nullptr;
    }
  }
  if (!g_process_detaching.load(std::memory_order_acquire)) {
    RequestPanelStop();
    ShutdownEngineBridge();
    ShutdownLog();
  }
  return 0;
}

}  // namespace

bool StartRuntime(HMODULE module) {
  if (g_started.exchange(true, std::memory_order_acq_rel)) return true;
  g_stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!g_stop_event) {
    g_started.store(false, std::memory_order_release);
    return false;
  }
  HANDLE thread = CreateThread(nullptr, 0, RuntimeThread, module, 0, nullptr);
  if (thread) {
    if (!SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL))
      Log(LogLevel::kWarning,
          "[RUNTIME] could not lower diagnostics worker priority error=%lu",
          static_cast<unsigned long>(GetLastError()));
    CloseHandle(thread);
    return true;
  } else {
    g_started.store(false, std::memory_order_release);
    CloseHandle(g_stop_event);
    g_stop_event = nullptr;
    return false;
  }
}

void NotifyProcessDetach() {
  g_process_detaching.store(true, std::memory_order_release);
  if (g_stop_event) SetEvent(g_stop_event);
}

}  // namespace fo3ds
