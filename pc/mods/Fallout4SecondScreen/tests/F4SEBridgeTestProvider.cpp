#include "../f4se/BridgeABI.h"

#include <cstdint>
#include <cstring>

namespace {

std::uint32_t g_mode = 0;
std::uint64_t g_generation = 7;

bool FO4DS_BRIDGE_CALL CopySnapshot(FO4DS_BridgeSnapshotV1* output,
                                    std::uint32_t output_size) {
  if (g_mode == 2 || !output || output_size < sizeof(*output)) return false;
  FO4DS_BridgeSnapshotV1 value = {};
  value.struct_size = g_mode == 3 ? 4 : sizeof(value);
  value.abi_version = FO4DS_BRIDGE_ABI_V1;
  value.capabilities = FO4DS_CAP_LIFECYCLE;
  value.lifecycle_flags =
      g_mode == 7 ? 0u
                  : FO4DS_LIFE_PLUGIN_LOADED | FO4DS_LIFE_GAME_LOADED |
                        FO4DS_LIFE_DATA_READY;
  value.runtime_version = g_mode == 7 ? 0u : 0x010B0F00u;
  value.f4se_version = g_mode == 7 ? 0u : 0x00070090u;
  value.f4se_release_index = 33;
  value.lifecycle_generation = g_generation;
  value.last_transition_tick_ms = 1234 + g_generation;
  std::memcpy(value.last_transition, "provider transition", 20);
  *output = value;
  return true;
}

FO4DS_BridgeApiV1 g_api = {
    sizeof(FO4DS_BridgeApiV1), FO4DS_BRIDGE_ABI_V1, &CopySnapshot};

}  // namespace

extern "C" __declspec(dllexport) const FO4DS_BridgeApiV1*
FO4DS_BRIDGE_CALL FO4DS_GetBridgeV1() {
  if (g_mode == 5)
    return reinterpret_cast<const FO4DS_BridgeApiV1*>(
        static_cast<std::uintptr_t>(1));
  g_api.struct_size = g_mode == 4 ? 4u : sizeof(FO4DS_BridgeApiV1);
  g_api.abi_version = g_mode == 1 ? 99u : FO4DS_BRIDGE_ABI_V1;
  g_api.copy_snapshot = g_mode == 6 ? nullptr : &CopySnapshot;
  return &g_api;
}

extern "C" __declspec(dllexport) void FO4DS_BRIDGE_CALL
FO4DS_TestSetMode(std::uint32_t mode) {
  g_mode = mode;
}

extern "C" __declspec(dllexport) void FO4DS_BRIDGE_CALL
FO4DS_TestAdvanceGeneration() {
  ++g_generation;
}
