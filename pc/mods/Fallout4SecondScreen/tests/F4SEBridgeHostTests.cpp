#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../f4se/BridgeABI.h"
#include "../f4se/F4SEMinimal.h"

namespace {

fo4ds::f4se::MessagingInterface::EventCallback g_listener = nullptr;

bool Check(bool condition, const char* message) {
  if (!condition) std::fprintf(stderr, "FAIL: %s\n", message);
  return condition;
}

void* QueryInterface(std::uint32_t id);
fo4ds::f4se::PluginHandle GetPluginHandle() { return 7; }
std::uint32_t GetReleaseIndex() { return 33; }
const fo4ds::f4se::PluginInfo* GetPluginInfo(const char*) { return nullptr; }
const char* GetSaveFolderName() { return "Fallout4"; }
bool RegisterListener(fo4ds::f4se::PluginHandle handle, const char* sender,
                      fo4ds::f4se::MessagingInterface::EventCallback callback) {
  if (handle != 7 || !sender || std::strcmp(sender, "F4SE") != 0 || !callback)
    return false;
  g_listener = callback;
  return true;
}
bool Dispatch(fo4ds::f4se::PluginHandle, std::uint32_t, void*, std::uint32_t,
              const char*) {
  return false;
}
void* GetEventDispatcher(std::uint32_t) { return nullptr; }

fo4ds::f4se::MessagingInterface g_messaging = {
    1, &RegisterListener, &Dispatch, &GetEventDispatcher};

void* QueryInterface(std::uint32_t id) {
  return id == fo4ds::f4se::kInterfaceMessaging ? &g_messaging : nullptr;
}

void Send(std::uint32_t type, void* data = nullptr,
          std::uint32_t size = 0) {
  fo4ds::f4se::MessagingInterface::Message message = {
      "F4SE", type, size, data};
  g_listener(&message);
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 2) {
    std::fprintf(stderr, "usage: F4SEBridgeHostTests <plugin.dll>\n");
    return 2;
  }
  HMODULE plugin = LoadLibraryA(argv[1]);
  if (!Check(plugin != nullptr, "plugin loads in a plain x64 host")) return 1;

  bool ok = true;
  auto* version = reinterpret_cast<fo4ds::f4se::PluginVersionData*>(
      GetProcAddress(plugin, "F4SEPlugin_Version"));
  ok &= Check(version != nullptr, "version declaration is exported");
  if (version) {
    ok &= Check(version->dataVersion == 1, "version declaration ABI is 1");
    ok &= Check(version->compatibleVersions[0] ==
                    fo4ds::f4se::kRuntime_1_11_240 &&
                    version->compatibleVersions[1] == 0,
                "only runtime 1.11.240 is declared compatible");
    ok &= Check(version->seVersionRequired == fo4ds::f4se::kF4SE_0_7_9,
                "F4SE 0.7.9 is the minimum");
  }

  const FARPROC raw_get_bridge = GetProcAddress(plugin, "FO4DS_GetBridgeV1");
  FO4DS_GetBridgeV1Fn get_bridge = nullptr;
  static_assert(sizeof(raw_get_bridge) == sizeof(get_bridge));
  std::memcpy(&get_bridge, &raw_get_bridge, sizeof(get_bridge));
  ok &= Check(get_bridge != nullptr, "bridge entry point is exported");
  if (get_bridge) {
    const FO4DS_BridgeApiV1* api = get_bridge();
    ok &= Check(api && api->abi_version == FO4DS_BRIDGE_ABI_V1 &&
                    api->struct_size == sizeof(*api) && api->copy_snapshot,
                "bridge API header is valid");
    if (api && api->copy_snapshot) {
      FO4DS_BridgeSnapshotV1 snapshot = {};
      ok &= Check(api->copy_snapshot(&snapshot, sizeof(snapshot)),
                  "snapshot copy succeeds");
      ok &= Check(snapshot.abi_version == FO4DS_BRIDGE_ABI_V1 &&
                      snapshot.struct_size == sizeof(snapshot),
                  "snapshot header is valid");
      ok &= Check(snapshot.capabilities == FO4DS_CAP_LIFECYCLE,
                  "unvalidated game-state capabilities remain disabled");
      ok &= Check(!api->copy_snapshot(nullptr, sizeof(snapshot)) &&
                      !api->copy_snapshot(&snapshot, sizeof(snapshot) - 1),
                  "snapshot copy rejects invalid buffers");

      using PluginLoadFn = bool (*)(const fo4ds::f4se::Interface*);
      const FARPROC raw_load = GetProcAddress(plugin, "F4SEPlugin_Load");
      PluginLoadFn load = nullptr;
      static_assert(sizeof(raw_load) == sizeof(load));
      std::memcpy(&load, &raw_load, sizeof(load));
      fo4ds::f4se::Interface f4se = {
          fo4ds::f4se::kF4SE_0_7_9,
          fo4ds::f4se::kRuntime_1_11_240,
          0,
          0,
          &QueryInterface,
          &GetPluginHandle,
          &GetReleaseIndex,
          &GetPluginInfo,
          &GetSaveFolderName,
      };
      ok &= Check(load && load(&f4se) && g_listener,
                  "plugin accepts the exact fake F4SE contract");
      ok &= Check(api->copy_snapshot(&snapshot, sizeof(snapshot)) &&
                      (snapshot.lifecycle_flags & FO4DS_LIFE_PLUGIN_LOADED) &&
                      snapshot.runtime_version ==
                          fo4ds::f4se::kRuntime_1_11_240 &&
                      snapshot.f4se_release_index == 33,
                  "load state is published through the bridge");

      if (g_listener) {
        Send(fo4ds::f4se::MessagingInterface::kGameLoaded);
        bool ready = true;
        Send(fo4ds::f4se::MessagingInterface::kGameDataReady, &ready,
             sizeof(ready));
        Send(fo4ds::f4se::MessagingInterface::kPreLoadGame);
        bool loaded = true;
        Send(fo4ds::f4se::MessagingInterface::kPostLoadGame, &loaded,
             sizeof(loaded));
        ok &= Check(api->copy_snapshot(&snapshot, sizeof(snapshot)) &&
                        (snapshot.lifecycle_flags & FO4DS_LIFE_GAME_LOADED) &&
                        (snapshot.lifecycle_flags & FO4DS_LIFE_DATA_READY) &&
                        (snapshot.lifecycle_flags & FO4DS_LIFE_SAVE_ACTIVE) &&
                        !(snapshot.lifecycle_flags & FO4DS_LIFE_SAVE_LOADING) &&
                        snapshot.lifecycle_generation >= 2,
                    "F4SE lifecycle transitions publish correct state");
      }
    }
  }

  FreeLibrary(plugin);
  char log_path[MAX_PATH] = {};
  std::snprintf(log_path, sizeof(log_path), "%s", argv[1]);
  char* slash = std::strrchr(log_path, '\\');
  if (slash) {
    std::snprintf(slash + 1,
                  sizeof(log_path) - static_cast<std::size_t>(slash + 1 - log_path),
                  "%s", "Fallout4SecondScreenF4SE.log");
    DeleteFileA(log_path);
  }
  if (!ok) return 1;
  std::puts("F4SE bridge host tests passed");
  return 0;
}
