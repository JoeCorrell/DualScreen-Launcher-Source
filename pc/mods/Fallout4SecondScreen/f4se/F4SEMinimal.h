#pragma once

#include <cstddef>
#include <cstdint>

namespace fo4ds::f4se {

using PluginHandle = std::uint32_t;
inline constexpr PluginHandle kInvalidPluginHandle = 0xFFFFFFFFu;
inline constexpr std::uint32_t kInterfaceMessaging = 1;

constexpr std::uint32_t PackVersion(std::uint32_t major, std::uint32_t minor,
                                    std::uint32_t build,
                                    std::uint32_t sub = 0) {
  return ((major & 0xFFu) << 24) | ((minor & 0xFFu) << 16) |
         ((build & 0xFFFu) << 4) | (sub & 0xFu);
}

inline constexpr std::uint32_t kRuntime_1_11_240 =
    PackVersion(1, 11, 240);
inline constexpr std::uint32_t kF4SE_0_7_9 = PackVersion(0, 7, 9);

struct PluginInfo {
  std::uint32_t infoVersion;
  const char* name;
  std::uint32_t version;
};

struct Interface {
  std::uint32_t f4seVersion;
  std::uint32_t runtimeVersion;
  std::uint32_t editorVersion;
  std::uint32_t isEditor;
  void* (*QueryInterface)(std::uint32_t id);
  PluginHandle (*GetPluginHandle)();
  std::uint32_t (*GetReleaseIndex)();
  const PluginInfo* (*GetPluginInfo)(const char* name);
  const char* (*GetSaveFolderName)();
};

struct MessagingInterface {
  struct Message {
    const char* sender;
    std::uint32_t type;
    std::uint32_t dataLen;
    void* data;
  };

  using EventCallback = void (*)(Message* message);

  enum : std::uint32_t {
    kPostLoad = 0,
    kPostPostLoad,
    kPreLoadGame,
    kPostLoadGame,
    kPreSaveGame,
    kPostSaveGame,
    kDeleteGame,
    kInputLoaded,
    kNewGame,
    kGameLoaded,
    kGameDataReady,
  };

  std::uint32_t interfaceVersion;
  bool (*RegisterListener)(PluginHandle listener, const char* sender,
                           EventCallback handler);
  bool (*Dispatch)(PluginHandle sender, std::uint32_t messageType, void* data,
                   std::uint32_t dataLen, const char* receiver);
  void* (*GetEventDispatcher)(std::uint32_t dispatcherId);
};

struct PluginVersionData {
  enum : std::uint32_t { kVersion = 1 };
  enum : std::uint32_t {
    kAddressIndependenceSignatures = 1u << 0,
    kAddressIndependenceAddressLibrary_1_10_980 = 1u << 1,
    kAddressIndependenceAddressLibrary_1_11_137 = 1u << 2,
  };
  enum : std::uint32_t {
    kStructureIndependenceNoStructs = 1u << 0,
    kStructureIndependence_1_10_980 = 1u << 1,
    kStructureIndependence_1_11_137 = 1u << 2,
  };

  std::uint32_t dataVersion;
  std::uint32_t pluginVersion;
  char name[256];
  char author[256];
  std::uint32_t addressIndependence;
  std::uint32_t structureIndependence;
  std::uint32_t compatibleVersions[16];
  std::uint32_t seVersionRequired;
  std::uint32_t reservedNonBreaking;
  std::uint32_t reservedBreaking;
  std::uint8_t reserved[512];
};

static_assert(sizeof(Interface) == 0x38);
static_assert(sizeof(MessagingInterface) == 0x20);
static_assert(sizeof(PluginVersionData) == 0x45C);

}  // namespace fo4ds::f4se
