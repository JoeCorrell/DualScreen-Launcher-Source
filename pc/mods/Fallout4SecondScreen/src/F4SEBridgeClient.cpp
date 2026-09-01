#include "F4SEBridgeClient.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../f4se/BridgeABI.h"
#include "Log.h"

namespace fo4ds {
namespace {

constexpr std::uint32_t kRuntime_1_11_240 = 0x010B0F00u;
constexpr std::uint32_t kF4SE_0_7_9 = 0x00070090u;

void CopyText(char* output, std::size_t output_size, const char* text) {
  if (!output || !output_size) return;
  std::snprintf(output, output_size, "%s", text ? text : "");
  output[output_size - 1] = '\0';
}

bool IsExecutableAddress(const void* address, HMODULE expected_module) {
  if (!address || !expected_module) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (VirtualQuery(address, &info, sizeof(info)) != sizeof(info) ||
      info.State != MEM_COMMIT || info.AllocationBase != expected_module ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) {
    return false;
  }
  const DWORD protection = info.Protect & 0xFFu;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

template <class T>
T FunctionPointer(FARPROC address) {
  static_assert(sizeof(T) == sizeof(address));
  T result = nullptr;
  std::memcpy(&result, &address, sizeof(result));
  return result;
}

template <class T>
const void* FunctionAddress(T function) {
  static_assert(sizeof(T) == sizeof(const void*));
  const void* result = nullptr;
  std::memcpy(&result, &function, sizeof(result));
  return result;
}

bool ReadLocal(const void* source, void* output, std::size_t size) {
  if (!source || !output || !size || size > MAXDWORD) return false;
  SIZE_T copied = 0;
  return ReadProcessMemory(GetCurrentProcess(), source, output, size, &copied) &&
         copied == size;
}

}  // namespace

F4SEBridgeClient::F4SEBridgeClient(const wchar_t* module_name,
                                   std::uint64_t discovery_retry_ms)
    : module_name_(module_name && *module_name
                       ? module_name
                       : L"Fallout4SecondScreenF4SE.dll"),
      discovery_retry_ms_(std::max<std::uint64_t>(discovery_retry_ms, 100)) {}

void F4SEBridgeClient::Poll(std::uint64_t now_ms) {
  std::lock_guard<std::mutex> lock(poll_mutex_);
  if (module_) {
    HMODULE current = GetModuleHandleW(module_name_.c_str());
    if (current != module_) {
      ClearBinding();
      PublishAvailability(F4SEBridgeAvailability::kAbsent,
                          "F4SE bridge module unloaded", now_ms);
      next_discovery_attempt_ms_ = now_ms + discovery_retry_ms_;
      return;
    }
    PollBound(now_ms);
    return;
  }
  if (now_ms < next_discovery_attempt_ms_) return;
  next_discovery_attempt_ms_ = now_ms + discovery_retry_ms_;
  AttemptDiscovery(now_ms);
}

F4SEBridgeLifecycleState F4SEBridgeClient::Capture() const {
  AcquireSRWLockShared(&state_lock_);
  const F4SEBridgeLifecycleState copy = state_;
  ReleaseSRWLockShared(&state_lock_);
  return copy;
}

void F4SEBridgeClient::AttemptDiscovery(std::uint64_t now_ms) {
  AcquireSRWLockExclusive(&state_lock_);
  ++state_.discovery_attempts;
  state_.last_discovery_attempt_tick_ms = now_ms;
  ReleaseSRWLockExclusive(&state_lock_);

  HMODULE module = GetModuleHandleW(module_name_.c_str());
  if (!module) {
    PublishAvailability(F4SEBridgeAvailability::kAbsent,
                        "optional F4SE bridge is not loaded", now_ms);
    return;
  }

  const FARPROC raw_export = GetProcAddress(module, "FO4DS_GetBridgeV1");
  if (!raw_export || !IsExecutableAddress(FunctionAddress(raw_export), module)) {
    PublishAvailability(F4SEBridgeAvailability::kMalformed,
                        "F4SE bridge export is missing or invalid", now_ms);
    return;
  }
  const FO4DS_GetBridgeV1Fn get_bridge =
      FunctionPointer<FO4DS_GetBridgeV1Fn>(raw_export);
  const FO4DS_BridgeApiV1* remote_api = get_bridge();
  FO4DS_BridgeApiV1 api = {};
  if (!ReadLocal(remote_api, &api, sizeof(api)) ||
      api.struct_size < sizeof(FO4DS_BridgeApiV1) ||
      api.struct_size > 4096 || api.abi_version != FO4DS_BRIDGE_ABI_V1 ||
      !api.copy_snapshot ||
      !IsExecutableAddress(FunctionAddress(api.copy_snapshot), module)) {
    PublishAvailability(F4SEBridgeAvailability::kMalformed,
                        "F4SE bridge API header is invalid", now_ms);
    return;
  }

  module_ = module;
  copy_snapshot_ = FunctionPointer<CopySnapshotFn>(
      reinterpret_cast<FARPROC>(FunctionAddress(api.copy_snapshot)));
  Log(LogLevel::kInfo,
      "[F4SE_BRIDGE] discovered module=%ls abi=%u api_size=%u",
      module_name_.c_str(), api.abi_version, api.struct_size);
  PollBound(now_ms);
}

void F4SEBridgeClient::PollBound(std::uint64_t now_ms) {
  if (!module_ || !copy_snapshot_) return;
  FO4DS_BridgeSnapshotV1 snapshot = {};
  if (!copy_snapshot_(&snapshot, sizeof(snapshot))) {
    ClearBinding();
    PublishAvailability(F4SEBridgeAvailability::kMalformed,
                        "F4SE bridge rejected snapshot copy", now_ms);
    next_discovery_attempt_ms_ = now_ms + discovery_retry_ms_;
    return;
  }
  snapshot.last_transition[sizeof(snapshot.last_transition) - 1] = '\0';
  if (snapshot.struct_size != sizeof(snapshot) ||
      snapshot.abi_version != FO4DS_BRIDGE_ABI_V1 ||
      !(snapshot.capabilities & FO4DS_CAP_LIFECYCLE)) {
    ClearBinding();
    PublishAvailability(F4SEBridgeAvailability::kMalformed,
                        "F4SE bridge snapshot header is invalid", now_ms);
    next_discovery_attempt_ms_ = now_ms + discovery_retry_ms_;
    return;
  }
  if (!(snapshot.lifecycle_flags & FO4DS_LIFE_PLUGIN_LOADED) ||
      snapshot.runtime_version == 0 || snapshot.f4se_version == 0) {
    PublishSnapshot(&snapshot, now_ms);
    PublishAvailability(F4SEBridgeAvailability::kWaitingForPlugin,
                        "F4SE bridge is waiting for plugin load", now_ms);
    return;
  }
  if (snapshot.runtime_version != kRuntime_1_11_240 ||
      snapshot.f4se_version < kF4SE_0_7_9) {
    ClearBinding();
    PublishAvailability(F4SEBridgeAvailability::kMalformed,
                        "F4SE bridge runtime contract is incompatible", now_ms);
    next_discovery_attempt_ms_ = now_ms + discovery_retry_ms_;
    return;
  }
  PublishSnapshot(&snapshot, now_ms);
  PublishAvailability(F4SEBridgeAvailability::kReady,
                      "F4SE lifecycle bridge ready", now_ms);
}

void F4SEBridgeClient::PublishAvailability(
    F4SEBridgeAvailability availability, const char* diagnostic,
    std::uint64_t now_ms) {
  bool changed = false;
  AcquireSRWLockExclusive(&state_lock_);
  if (state_.availability != availability ||
      std::strncmp(state_.diagnostic, diagnostic ? diagnostic : "",
                   sizeof(state_.diagnostic)) != 0) {
    state_.availability = availability;
    CopyText(state_.diagnostic, sizeof(state_.diagnostic), diagnostic);
    ++state_.revision;
    changed = true;
  }
  if (availability == F4SEBridgeAvailability::kAbsent ||
      availability == F4SEBridgeAvailability::kMalformed) {
    state_.capabilities = 0;
    state_.lifecycle_flags = 0;
    state_.runtime_version = 0;
    state_.f4se_version = 0;
    state_.f4se_release_index = 0;
    state_.lifecycle_generation = 0;
    state_.pipboy_change_serial = 0;
    state_.last_plugin_transition_tick_ms = 0;
    state_.last_transition[0] = '\0';
  }
  ReleaseSRWLockExclusive(&state_lock_);
  if (changed) {
    Log(availability == F4SEBridgeAvailability::kMalformed
            ? LogLevel::kWarning
            : LogLevel::kInfo,
        "[F4SE_BRIDGE] availability=%u at=%llu detail=%s",
        static_cast<unsigned>(availability),
        static_cast<unsigned long long>(now_ms), diagnostic ? diagnostic : "");
  }
}

void F4SEBridgeClient::PublishSnapshot(const void* raw_snapshot,
                                       std::uint64_t) {
  const auto& snapshot =
      *static_cast<const FO4DS_BridgeSnapshotV1*>(raw_snapshot);
  bool transition = false;
  AcquireSRWLockExclusive(&state_lock_);
  transition = state_.lifecycle_generation != snapshot.lifecycle_generation ||
               state_.lifecycle_flags != snapshot.lifecycle_flags ||
               state_.last_plugin_transition_tick_ms !=
                   snapshot.last_transition_tick_ms ||
               std::strncmp(state_.last_transition, snapshot.last_transition,
                            sizeof(state_.last_transition)) != 0;
  const bool changed =
      transition || state_.capabilities != snapshot.capabilities ||
      state_.runtime_version != snapshot.runtime_version ||
      state_.f4se_version != snapshot.f4se_version ||
      state_.f4se_release_index != snapshot.f4se_release_index ||
      state_.pipboy_change_serial != snapshot.pipboy_change_serial;
  state_.capabilities = snapshot.capabilities;
  state_.lifecycle_flags = snapshot.lifecycle_flags;
  state_.runtime_version = snapshot.runtime_version;
  state_.f4se_version = snapshot.f4se_version;
  state_.f4se_release_index = snapshot.f4se_release_index;
  state_.lifecycle_generation = snapshot.lifecycle_generation;
  state_.pipboy_change_serial = snapshot.pipboy_change_serial;
  state_.last_plugin_transition_tick_ms = snapshot.last_transition_tick_ms;
  CopyText(state_.last_transition, sizeof(state_.last_transition),
           snapshot.last_transition);
  if (changed) ++state_.revision;
  ReleaseSRWLockExclusive(&state_lock_);
  if (transition) {
    Log(LogLevel::kInfo,
        "[F4SE_BRIDGE] transition generation=%llu flags=0x%08X event=%s",
        static_cast<unsigned long long>(snapshot.lifecycle_generation),
        snapshot.lifecycle_flags, snapshot.last_transition);
  }
}

void F4SEBridgeClient::ClearBinding() {
  module_ = nullptr;
  copy_snapshot_ = nullptr;
}

}  // namespace fo4ds
