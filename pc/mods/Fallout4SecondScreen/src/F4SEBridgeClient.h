#pragma once

#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>

namespace fo4ds {

enum class F4SEBridgeAvailability : std::uint32_t {
  kNeverChecked = 0,
  kAbsent,
  kWaitingForPlugin,
  kReady,
  kMalformed,
};

// POD only: callers never receive module handles, function pointers, or game
// pointers. Capture() publishes a coherent copy under the client state lock.
struct F4SEBridgeLifecycleState {
  std::uint32_t struct_size = sizeof(F4SEBridgeLifecycleState);
  std::uint32_t abi_version = 1;
  F4SEBridgeAvailability availability =
      F4SEBridgeAvailability::kNeverChecked;
  std::uint32_t capabilities = 0;
  std::uint32_t lifecycle_flags = 0;
  std::uint32_t runtime_version = 0;
  std::uint32_t f4se_version = 0;
  std::uint32_t f4se_release_index = 0;
  std::uint32_t discovery_attempts = 0;
  std::uint32_t reserved0 = 0;
  std::uint64_t revision = 0;
  std::uint64_t lifecycle_generation = 0;
  std::uint64_t pipboy_change_serial = 0;
  std::uint64_t last_plugin_transition_tick_ms = 0;
  std::uint64_t last_discovery_attempt_tick_ms = 0;
  char last_transition[96] = {};
  char diagnostic[128] = {};
};

static_assert(sizeof(F4SEBridgeLifecycleState) == 304);

class F4SEBridgeClient {
 public:
  explicit F4SEBridgeClient(
      const wchar_t* module_name = L"Fallout4SecondScreenF4SE.dll",
      std::uint64_t discovery_retry_ms = 1000);

  // Safe to call frequently. Missing/malformed module discovery is attempted
  // no more often than discovery_retry_ms; a bound plugin snapshot is cheap
  // and may be sampled on every caller tick.
  void Poll(std::uint64_t now_ms);
  F4SEBridgeLifecycleState Capture() const;

 private:
  using CopySnapshotFn = bool(__cdecl*)(void*, std::uint32_t);

  void AttemptDiscovery(std::uint64_t now_ms);
  void PollBound(std::uint64_t now_ms);
  void PublishAvailability(F4SEBridgeAvailability availability,
                           const char* diagnostic,
                           std::uint64_t now_ms);
  void PublishSnapshot(const void* snapshot, std::uint64_t now_ms);
  void ClearBinding();

  std::wstring module_name_;
  std::uint64_t discovery_retry_ms_ = 1000;
  std::uint64_t next_discovery_attempt_ms_ = 0;
  HMODULE module_ = nullptr;
  CopySnapshotFn copy_snapshot_ = nullptr;
  mutable SRWLOCK state_lock_ = SRWLOCK_INIT;
  mutable std::mutex poll_mutex_;
  F4SEBridgeLifecycleState state_;
};

}  // namespace fo4ds
