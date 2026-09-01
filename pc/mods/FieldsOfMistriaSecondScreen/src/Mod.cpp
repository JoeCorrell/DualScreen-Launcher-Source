#include "Mod.h"

#include <atomic>
#include <exception>
#include <string>
#include <vector>

#include "BuildIdentity.h"
#include "Log.h"

namespace fomds {
namespace {

HMODULE g_module = nullptr;
std::atomic<bool> g_started{false};

bool LaunchPanelHost() {
  std::vector<wchar_t> module_path(32768);
  const DWORD length = GetModuleFileNameW(
      g_module, module_path.data(), static_cast<DWORD>(module_path.size()));
  if (!length || length >= module_path.size()) return false;
  std::wstring host_path(module_path.data(), length);
  const std::size_t slash = host_path.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  host_path.resize(slash + 1);
  // Keep the implementation helper out of WEMU's normal executable picker.
  // It is still an ordinary PE image launched by exact path; the leading dot
  // gives Wine/shared-storage browsers their standard hidden-file behavior.
  host_path.append(L".fomds-panel-host.exe");
  if (GetFileAttributesW(host_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
    Log(LogLevel::kError,
        "[HOST] isolated panel host is missing; SDL forwarding remains active");
    return false;
  }

  std::wstring command_line = L"\"" + host_path + L"\" --parent-pid " +
      std::to_wstring(GetCurrentProcessId());
  std::vector<wchar_t> mutable_command(command_line.begin(), command_line.end());
  mutable_command.push_back(L'\0');
  STARTUPINFOW startup = {};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process = {};
  const BOOL created = CreateProcessW(
      host_path.c_str(), mutable_command.data(), nullptr, nullptr, FALSE,
      CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &startup, &process);
  if (!created) {
    Log(LogLevel::kError,
        "[HOST] isolated panel host launch failed error=%lu; SDL forwarding remains active",
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  Log(LogLevel::kInfo,
      "[HOST] isolated panel host launched pid=%lu parent=%lu",
      static_cast<unsigned long>(process.dwProcessId),
      static_cast<unsigned long>(GetCurrentProcessId()));
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return true;
}

DWORD WINAPI Bootstrap(void*) {
  try {
    InitializeLog(g_module);
    Log(LogLevel::kInfo,
        "[INIT] dual-screen proxy bootstrap; isolated panel host architecture");
    const BuildIdentity identity = IdentifyInstalledBuild(g_module);
    Log(identity.supported ? LogLevel::kInfo : LogLevel::kWarning,
        "[COMPAT] %s", identity.reason);
    if (!identity.supported) {
      Log(LogLevel::kWarning,
          "[COMPAT] fail-closed: panel host and all mod behavior remain disabled");
      return 0;
    }
    LaunchPanelHost();
  } catch (const std::exception& error) {
    Log(LogLevel::kError,
        "[INIT] bootstrap exception; fail-closed with SDL forwarding: %s",
        error.what());
  } catch (...) {
    Log(LogLevel::kError,
        "[INIT] unknown bootstrap exception; fail-closed with SDL forwarding");
  }
  return 0;
}

}  // namespace

bool StartModAsync(HMODULE module) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return true;
  g_module = module;
  HANDLE thread = CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
  if (!thread) {
    g_started.store(false, std::memory_order_release);
    return false;
  }
  CloseHandle(thread);
  return true;
}

void RequestModStop() {
  // The panel is owned by an adjacent helper process which watches this game
  // process handle. DllMain must not wait or perform IPC while loader-locked.
}

}  // namespace fomds
