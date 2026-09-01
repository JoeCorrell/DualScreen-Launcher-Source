#pragma once

#include <cstddef>
#include <cstdint>

// Stable C ABI shared by the optional F4SE plugin and the DXGI-hosted lower
// panel.  Never expose C++ containers or game pointers for callers to
// dereference.  CopySnapshot is the only synchronization boundary.

#if defined(_WIN32)
#define FO4DS_BRIDGE_CALL __cdecl
#else
#define FO4DS_BRIDGE_CALL
#endif

enum FO4DS_BridgeCapability : std::uint32_t {
  FO4DS_CAP_LIFECYCLE = 1u << 0,
  FO4DS_CAP_PIPBOY_TREE = 1u << 1,
  FO4DS_CAP_GAME_THREAD_ACTIONS = 1u << 2,
};

enum FO4DS_LifecycleFlag : std::uint32_t {
  FO4DS_LIFE_PLUGIN_LOADED = 1u << 0,
  FO4DS_LIFE_GAME_LOADED = 1u << 1,
  FO4DS_LIFE_DATA_READY = 1u << 2,
  FO4DS_LIFE_SAVE_LOADING = 1u << 3,
  FO4DS_LIFE_SAVE_ACTIVE = 1u << 4,
  FO4DS_LIFE_NEW_GAME = 1u << 5,
};

struct FO4DS_BridgeSnapshotV1 {
  std::uint32_t struct_size;
  std::uint32_t abi_version;
  std::uint32_t capabilities;
  std::uint32_t lifecycle_flags;
  std::uint32_t runtime_version;
  std::uint32_t f4se_version;
  std::uint32_t f4se_release_index;
  std::uint32_t reserved0;
  std::uint64_t lifecycle_generation;
  std::uint64_t pipboy_change_serial;
  std::uint64_t last_transition_tick_ms;
  std::uint64_t pipboy_data_manager_address;
  std::uint64_t pipboy_root_address;
  char last_transition[96];
};

using FO4DS_CopySnapshotV1 = bool(FO4DS_BRIDGE_CALL*)(
    FO4DS_BridgeSnapshotV1* output, std::uint32_t output_size);

struct FO4DS_BridgeApiV1 {
  std::uint32_t struct_size;
  std::uint32_t abi_version;
  FO4DS_CopySnapshotV1 copy_snapshot;
};

using FO4DS_GetBridgeV1Fn = const FO4DS_BridgeApiV1*(FO4DS_BRIDGE_CALL*)();

inline constexpr std::uint32_t FO4DS_BRIDGE_ABI_V1 = 1;

static_assert(offsetof(FO4DS_BridgeSnapshotV1, last_transition) == 72);
static_assert(sizeof(FO4DS_BridgeSnapshotV1) == 168);
static_assert(sizeof(FO4DS_BridgeApiV1) == 16);
