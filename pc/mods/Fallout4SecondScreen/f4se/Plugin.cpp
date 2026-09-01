#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "BridgeABI.h"
#include "F4SEMinimal.h"

namespace {

using namespace fo4ds;

HMODULE g_module = nullptr;
SRWLOCK g_state_lock = SRWLOCK_INIT;
FO4DS_BridgeSnapshotV1 g_state = [] {
  FO4DS_BridgeSnapshotV1 value = {};
  value.struct_size = sizeof(value);
  value.abi_version = FO4DS_BRIDGE_ABI_V1;
  value.capabilities = FO4DS_CAP_LIFECYCLE;
  return value;
}();

void CopyText(char* output, std::size_t output_size, const char* text) {
  if (!output || !output_size) return;
  std::snprintf(output, output_size, "%s", text ? text : "");
  output[output_size - 1] = '\0';
}

void Log(const char* message) {
  char debug[256] = {};
  std::snprintf(debug, sizeof(debug), "Fallout4SecondScreenF4SE: %s\n",
                message ? message : "");
  OutputDebugStringA(debug);

  wchar_t path[MAX_PATH] = {};
  if (!g_module) return;
  const DWORD path_length = GetModuleFileNameW(g_module, path, MAX_PATH);
  if (!path_length || path_length >= MAX_PATH) return;
  wchar_t* slash = wcsrchr(path, L'\\');
  if (!slash) return;
  constexpr wchar_t kLogName[] = L"Fallout4SecondScreenF4SE.log";
  const std::size_t prefix = static_cast<std::size_t>(slash + 1 - path);
  if (prefix + sizeof(kLogName) / sizeof(kLogName[0]) > MAX_PATH) return;
  std::memcpy(slash + 1, kLogName, sizeof(kLogName));
  HANDLE file = CreateFileW(path, FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return;
  SYSTEMTIME now = {};
  GetLocalTime(&now);
  char line[384] = {};
  const int length = std::snprintf(
      line, sizeof(line), "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n",
      now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
      now.wMilliseconds, message ? message : "");
  if (length > 0) {
    DWORD written = 0;
    WriteFile(file, line,
              static_cast<DWORD>(std::min<int>(length, sizeof(line) - 1)),
              &written, nullptr);
  }
  CloseHandle(file);
}

void Transition(std::uint32_t set_flags, std::uint32_t clear_flags,
                const char* description, bool new_generation) {
  AcquireSRWLockExclusive(&g_state_lock);
  g_state.lifecycle_flags =
      (g_state.lifecycle_flags | set_flags) & ~clear_flags;
  if (new_generation) ++g_state.lifecycle_generation;
  g_state.last_transition_tick_ms = GetTickCount64();
  CopyText(g_state.last_transition, sizeof(g_state.last_transition),
           description);
  ReleaseSRWLockExclusive(&g_state_lock);
  Log(description);
}

void OnF4SEMessage(f4se::MessagingInterface::Message* message) {
  if (!message) return;
  switch (message->type) {
    case f4se::MessagingInterface::kPostLoad:
      Transition(0, 0, "F4SE PostLoad", false);
      break;
    case f4se::MessagingInterface::kPostPostLoad:
      Transition(0, 0, "F4SE PostPostLoad", false);
      break;
    case f4se::MessagingInterface::kPreLoadGame:
      Transition(FO4DS_LIFE_SAVE_LOADING,
                 FO4DS_LIFE_SAVE_ACTIVE | FO4DS_LIFE_NEW_GAME,
                 "save load beginning", true);
      break;
    case f4se::MessagingInterface::kPostLoadGame: {
      const bool succeeded = message->data && message->dataLen >= sizeof(bool) &&
                             *static_cast<const bool*>(message->data);
      Transition(succeeded ? static_cast<std::uint32_t>(FO4DS_LIFE_SAVE_ACTIVE)
                           : 0u,
                 FO4DS_LIFE_SAVE_LOADING |
                     (succeeded
                          ? 0u
                          : static_cast<std::uint32_t>(
                                FO4DS_LIFE_SAVE_ACTIVE)),
                 succeeded ? "save load completed" : "save load failed", false);
      break;
    }
    case f4se::MessagingInterface::kNewGame:
      Transition(FO4DS_LIFE_NEW_GAME | FO4DS_LIFE_SAVE_ACTIVE,
                 FO4DS_LIFE_SAVE_LOADING, "new game created", true);
      break;
    case f4se::MessagingInterface::kGameLoaded:
      Transition(FO4DS_LIFE_GAME_LOADED, 0, "game runtime loaded", false);
      break;
    case f4se::MessagingInterface::kGameDataReady: {
      const bool ready = message->data && message->dataLen >= sizeof(bool) &&
                         *static_cast<const bool*>(message->data);
      Transition(ready ? static_cast<std::uint32_t>(FO4DS_LIFE_DATA_READY)
                       : 0u,
                 ready ? 0u
                       : static_cast<std::uint32_t>(FO4DS_LIFE_DATA_READY),
                 ready ? "game data ready" : "game data unavailable", false);
      break;
    }
    case f4se::MessagingInterface::kInputLoaded:
      Transition(0, 0, "game input loaded", false);
      break;
    default:
      break;
  }
}

bool FO4DS_BRIDGE_CALL CopySnapshot(FO4DS_BridgeSnapshotV1* output,
                                    std::uint32_t output_size) {
  if (!output || output_size < sizeof(FO4DS_BridgeSnapshotV1)) return false;
  AcquireSRWLockShared(&g_state_lock);
  std::memcpy(output, &g_state, sizeof(g_state));
  ReleaseSRWLockShared(&g_state_lock);
  return true;
}

const FO4DS_BridgeApiV1 g_bridge_api = {
    sizeof(FO4DS_BridgeApiV1),
    FO4DS_BRIDGE_ABI_V1,
    &CopySnapshot,
};

}  // namespace

extern "C" __declspec(dllexport) fo4ds::f4se::PluginVersionData
    F4SEPlugin_Version;
fo4ds::f4se::PluginVersionData F4SEPlugin_Version = {
        fo4ds::f4se::PluginVersionData::kVersion,
        fo4ds::f4se::PackVersion(0, 1, 0),
        "Fallout4SecondScreenF4SE",
        "Fallout4SecondScreen project",
        0,  // Exact-runtime declaration; no address-dependent path enabled yet.
        fo4ds::f4se::PluginVersionData::kStructureIndependenceNoStructs,
        {fo4ds::f4se::kRuntime_1_11_240, 0},
        fo4ds::f4se::kF4SE_0_7_9,
        0,
        0,
        {},
};

extern "C" __declspec(dllexport) bool F4SEPlugin_Load(
    const fo4ds::f4se::Interface* f4se) {
  if (!f4se || f4se->isEditor ||
      f4se->runtimeVersion != fo4ds::f4se::kRuntime_1_11_240 ||
      f4se->f4seVersion < fo4ds::f4se::kF4SE_0_7_9 ||
      !f4se->QueryInterface || !f4se->GetPluginHandle) {
    Log("load rejected: unsupported F4SE/runtime contract");
    return false;
  }
  const auto handle = f4se->GetPluginHandle();
  auto* messaging = static_cast<fo4ds::f4se::MessagingInterface*>(
      f4se->QueryInterface(fo4ds::f4se::kInterfaceMessaging));
  if (handle == fo4ds::f4se::kInvalidPluginHandle || !messaging ||
      messaging->interfaceVersion < 1 || !messaging->RegisterListener) {
    Log("load rejected: F4SE messaging interface unavailable");
    return false;
  }

  AcquireSRWLockExclusive(&g_state_lock);
  g_state.runtime_version = f4se->runtimeVersion;
  g_state.f4se_version = f4se->f4seVersion;
  g_state.f4se_release_index =
      f4se->GetReleaseIndex ? f4se->GetReleaseIndex() : 0;
  ReleaseSRWLockExclusive(&g_state_lock);

  if (!messaging->RegisterListener(handle, "F4SE", &OnF4SEMessage)) {
    Log("load rejected: F4SE listener registration failed");
    return false;
  }
  Transition(FO4DS_LIFE_PLUGIN_LOADED, 0,
             "plugin loaded; lifecycle bridge ready", true);
  return true;
}

extern "C" __declspec(dllexport) const FO4DS_BridgeApiV1*
FO4DS_BRIDGE_CALL FO4DS_GetBridgeV1() {
  return &g_bridge_api;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    g_module = instance;
  }
  return TRUE;
}
