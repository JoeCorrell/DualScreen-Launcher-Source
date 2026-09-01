#include <windows.h>

#include <cwchar>
#include <exception>
#include <thread>

#include "Log.h"
#include "Config.h"
#include "Mod.h"
#include "NativeActions.h"
#include "NativePipboyBridge.h"
#include "Panel.h"
#include "PipboyProtocol.h"
#include "Runtime.h"

namespace fo4ds {
namespace {

INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;

bool IsFallout4(const std::wstring& path) {
  const wchar_t* backslash = std::wcsrchr(path.c_str(), L'\\');
  const wchar_t* slash = std::wcsrchr(path.c_str(), L'/');
  const wchar_t* separator =
      !backslash ? slash : (!slash || backslash > slash ? backslash : slash);
  const wchar_t* name = separator ? separator + 1 : path.c_str();
  return _wcsicmp(name, L"Fallout4.exe") == 0;
}

void BootstrapImpl() {
  InitializeLog();
  Log(LogLevel::kInfo, "[INIT] bootstrap thread entered");
  const uint64_t begin = GetTickCount64();
  const RuntimeIdentity identity = IdentifyRuntime();
  Log(identity.supported ? LogLevel::kInfo : LogLevel::kWarning,
      "[RUNTIME] version=%s sha256=%s supported=%u identity_ms=%llu",
      identity.version.c_str(), identity.sha256.c_str(),
      identity.supported ? 1u : 0u,
      static_cast<unsigned long long>(GetTickCount64() - begin));
  if (!IsFallout4(identity.path)) {
    Log(LogLevel::kInfo, "[INIT] non-Fallout host; proxy remains transparent");
    return;
  }
  if (!identity.supported) {
    Log(LogLevel::kError,
        "[INIT] unsupported Fallout4.exe; mod services will not start");
    return;
  }
  const Config& config = LoadConfig();
  if (!config.enabled) {
    Log(LogLevel::kInfo, "[INIT] disabled by configuration; proxy remains transparent");
    return;
  }

  // These objects intentionally live for the host process lifetime. Fallout's
  // DXGI module is not unloaded during normal play, and avoiding static teardown
  // prevents thread joins or Winsock work while the Windows loader lock is held.
  const NativeActionInstallStatus action_status = ConfigureNativeActions(
      config.actions_enabled,
      reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)),
      identity.sha256.c_str());
  const bool action_install_expected = config.actions_enabled;
  const bool action_status_ok =
      action_status == NativeActionInstallStatus::kReady ||
      (!action_install_expected &&
       action_status ==
           NativeActionInstallStatus::kDisabledByConfiguration);
  Log(action_status_ok ? LogLevel::kInfo : LogLevel::kWarning,
      "[ACTION] native game-thread pump status=%s configured=%u",
      NativeActionInstallStatusLabel(action_status),
      config.actions_enabled ? 1u : 0u);

  auto* database = new PipboyDatabase();
  auto* native_bridge = new NativePipboyBridge(database);
  if (!native_bridge->Start()) {
    Log(LogLevel::kError, "[INIT] native Pip-Boy bridge failed to start");
    return;
  }
  if (!StartPanel(database, nullptr)) {
    Log(LogLevel::kError, "[INIT] lower-panel thread failed to start");
    native_bridge->Stop();
    return;
  }
  Log(LogLevel::kInfo,
      "[INIT] Fallout 4 dual-screen initialized native_state=1 runtime_supported=%u",
      identity.supported ? 1u : 0u);
}

void Bootstrap() noexcept {
  try {
    BootstrapImpl();
  } catch (const std::exception& error) {
    try {
      InitializeLog();
      Log(LogLevel::kError, "[INIT] bootstrap exception: %s", error.what());
    } catch (...) {
      OutputDebugStringA("Fallout4SecondScreen: bootstrap exception logging failed\n");
    }
  } catch (...) {
    try {
      InitializeLog();
      Log(LogLevel::kError, "[INIT] bootstrap unknown exception");
    } catch (...) {
      OutputDebugStringA("Fallout4SecondScreen: bootstrap exception logging failed\n");
    }
  }
}

BOOL CALLBACK BeginBootstrap(PINIT_ONCE, PVOID, PVOID*) {
  try {
    std::thread(Bootstrap).detach();
    return TRUE;
  } catch (...) {
    OutputDebugStringA("Fallout4SecondScreen: bootstrap thread creation failed\n");
    return FALSE;
  }
}

}  // namespace

void ModStartServices() {
  InitOnceExecuteOnce(&g_once, BeginBootstrap, nullptr, nullptr);
}

void ModOnDxgiUsed() { ModStartServices(); }

}  // namespace fo4ds
