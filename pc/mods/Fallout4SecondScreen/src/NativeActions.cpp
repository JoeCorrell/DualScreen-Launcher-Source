#include "NativeActions.h"

#include <windows.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>

namespace fo4ds {
namespace {

constexpr char kExecutableSha256[] =
    "061C1AB3738612DBC46D748F02C27795A1003501C90C15D1E649D8A07FCBBB4E";
constexpr std::array<uint8_t, 7> kPumpPrologue = {{
    0x48, 0x8B, 0xC4,              // mov rax,rsp
    0x48, 0x89, 0x58, 0x08,        // mov [rax+8],rbx
}};
constexpr size_t kTrampolineOffset = 0;
constexpr size_t kRelayOffset = 32;
constexpr size_t kAbsoluteJumpBytes = 14;
constexpr size_t kMaximumSuspendedThreads = 1024;
constexpr unsigned kMaximumThreadEnumerationPasses = 4;
constexpr size_t kMaximumActionsPerPump = 16;
constexpr uint32_t kMaximumRadioObjectCapacity = 4096;

using PumpFunction = bool (*)(void*, float, uint32_t);
using InventoryVersionFunction = uint32_t (*)(uintptr_t);
using HandleValidFunction = bool (*)(uint32_t*);
using ResolveInventoryHandleFunction = uintptr_t (*)(uintptr_t, uint32_t*);
using InventoryStackCountFunction = uint32_t (*)(uintptr_t);
using UseInventoryItemFunction =
    void (*)(uint32_t, uint32_t, bool*, bool*);
using ResolveQuestFunction = uintptr_t (*)(uint32_t);
using ToggleQuestFunction = void (*)(uint32_t, uint32_t, uint32_t);
using QuestActiveFunction = bool (*)(uintptr_t);
using FindRadioStationFunction = uintptr_t (*)(uintptr_t, uint32_t);
using EnablePlayerRadioFunction = void (*)(bool, bool);
using PlayerRadioUpdateFunction = void (*)(uintptr_t, uintptr_t);

struct BackendState {
  std::mutex install_mutex;
  std::atomic<NativeActionInstallStatus> status{
      NativeActionInstallStatus::kNotInstalled};
  NativeActionQueue queue;
  uintptr_t module = 0;
  void* trampoline_page = nullptr;
  PumpFunction original_pump = nullptr;
};

BackendState& State() {
  static BackendState* state = new BackendState();
  return *state;
}

bool ProtectionReadable(DWORD protection) {
  protection &= 0xff;
  return protection == PAGE_READONLY || protection == PAGE_READWRITE ||
         protection == PAGE_WRITECOPY || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

bool ProtectionWritable(DWORD protection) {
  protection &= 0xff;
  return protection == PAGE_READWRITE || protection == PAGE_WRITECOPY ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

bool IsMemory(uintptr_t address, size_t bytes, bool writable) {
  if (!address || !bytes ||
      address > (std::numeric_limits<uintptr_t>::max)() - bytes) {
    return false;
  }
  MEMORY_BASIC_INFORMATION information = {};
  if (!VirtualQuery(reinterpret_cast<const void*>(address), &information,
                    sizeof(information)) ||
      information.State != MEM_COMMIT ||
      (information.Protect & (PAGE_NOACCESS | PAGE_GUARD)) ||
      (writable ? !ProtectionWritable(information.Protect)
                : !ProtectionReadable(information.Protect))) {
    return false;
  }
  const uintptr_t region =
      reinterpret_cast<uintptr_t>(information.BaseAddress);
  if (region > (std::numeric_limits<uintptr_t>::max)() -
                   information.RegionSize) {
    return false;
  }
  return address >= region && address + bytes <= region + information.RegionSize;
}

bool IsExecutable(uintptr_t address) {
  MEMORY_BASIC_INFORMATION information = {};
  if (!address ||
      !VirtualQuery(reinterpret_cast<const void*>(address), &information,
                    sizeof(information)) ||
      information.State != MEM_COMMIT ||
      (information.Protect & (PAGE_NOACCESS | PAGE_GUARD))) {
    return false;
  }
  const DWORD protection = information.Protect & 0xff;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

template <class T>
bool Read(uintptr_t address, T* value) {
  if (!value || !IsMemory(address, sizeof(T), false)) return false;
  std::memcpy(value, reinterpret_cast<const void*>(address), sizeof(T));
  return true;
}

bool ReadPointer(uintptr_t address, uintptr_t* value) {
  return Read(address, value) && *value;
}

bool IsExactVtable(uintptr_t object, uintptr_t expected) {
  uintptr_t vtable = 0;
  return Read(object, &vtable) && vtable == expected;
}

void WriteAbsoluteJump(uint8_t* destination, uintptr_t target) {
  destination[0] = 0xFF;
  destination[1] = 0x25;
  std::memset(destination + 2, 0, 4);
  std::memcpy(destination + 6, &target, sizeof(target));
}

bool RelativeJump(uintptr_t source, uintptr_t destination, int32_t* output) {
  if (!output) return false;
  const int64_t displacement = static_cast<int64_t>(destination) -
                               static_cast<int64_t>(source + 5);
  if (displacement < (std::numeric_limits<int32_t>::min)() ||
      displacement > (std::numeric_limits<int32_t>::max)()) {
    return false;
  }
  *output = static_cast<int32_t>(displacement);
  return true;
}

uintptr_t AlignUp(uintptr_t value, uintptr_t alignment) {
  if (!alignment || value > (std::numeric_limits<uintptr_t>::max)() -
                                  (alignment - 1)) {
    return 0;
  }
  return (value + alignment - 1) & ~(alignment - 1);
}

void* AllocateNear(uintptr_t target) {
  SYSTEM_INFO system = {};
  GetSystemInfo(&system);
  const uintptr_t granularity = system.dwAllocationGranularity;
  const size_t page_bytes = system.dwPageSize;
  const uintptr_t minimum =
      reinterpret_cast<uintptr_t>(system.lpMinimumApplicationAddress);
  const uintptr_t maximum =
      reinterpret_cast<uintptr_t>(system.lpMaximumApplicationAddress);
  if (!granularity || !page_bytes || maximum <= minimum) return nullptr;

  const int64_t relative_min = static_cast<int64_t>(target + 5) +
                               (std::numeric_limits<int32_t>::min)();
  const int64_t relative_max = static_cast<int64_t>(target + 5) +
                               (std::numeric_limits<int32_t>::max)();
  uintptr_t low = relative_min <= static_cast<int64_t>(minimum + kRelayOffset)
                      ? minimum
                      : static_cast<uintptr_t>(relative_min) - kRelayOffset;
  uintptr_t high = relative_max >= static_cast<int64_t>(maximum)
                       ? maximum
                       : static_cast<uintptr_t>(relative_max);
  low = AlignUp(low, granularity);
  if (!low || low >= high) return nullptr;

  uintptr_t cursor = low;
  while (cursor < high) {
    MEMORY_BASIC_INFORMATION information = {};
    if (!VirtualQuery(reinterpret_cast<const void*>(cursor), &information,
                      sizeof(information))) {
      break;
    }
    const uintptr_t region =
        reinterpret_cast<uintptr_t>(information.BaseAddress);
    uintptr_t next = maximum;
    if (region <= maximum - information.RegionSize)
      next = region + information.RegionSize;
    if (information.State == MEM_FREE) {
      const uintptr_t candidate = AlignUp((std::max)(cursor, region), granularity);
      if (candidate && candidate <= high - page_bytes) {
        int32_t displacement = 0;
        if (RelativeJump(target, candidate + kRelayOffset, &displacement)) {
          void* page = VirtualAlloc(reinterpret_cast<void*>(candidate), page_bytes,
                                    MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
          if (page) return page;
        }
      }
    }
    if (next <= cursor) break;
    cursor = next;
  }
  return nullptr;
}

struct SuspendedThread {
  HANDLE handle = nullptr;
  DWORD id = 0;
  DWORD64 original_pc = 0;
  bool pc_changed = false;
};

class ThreadTransaction final {
 public:
  ~ThreadTransaction() { ResumeAll(false); }

  bool SuspendAll(uintptr_t patch, size_t patch_bytes,
                  uintptr_t trampoline) {
    const DWORD process_id = GetCurrentProcessId();
    const DWORD current_id = GetCurrentThreadId();
    bool stable = false;
    for (unsigned pass = 0; pass < kMaximumThreadEnumerationPasses; ++pass) {
      bool found_new = false;
      HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
      if (snapshot == INVALID_HANDLE_VALUE) return false;
      THREADENTRY32 entry = {};
      entry.dwSize = sizeof(entry);
      BOOL have_entry = Thread32First(snapshot, &entry);
      while (have_entry) {
        if (entry.th32OwnerProcessID == process_id &&
            entry.th32ThreadID != current_id &&
            !Contains(entry.th32ThreadID)) {
          found_new = true;
          if (!Suspend(entry.th32ThreadID)) {
            CloseHandle(snapshot);
            return false;
          }
        }
        have_entry = Thread32Next(snapshot, &entry);
      }
      const DWORD enumeration_error = GetLastError();
      CloseHandle(snapshot);
      if (enumeration_error != ERROR_NO_MORE_FILES) return false;
      if (!found_new) {
        stable = true;
        break;
      }
    }
    if (!stable) return false;

    for (size_t i = 0; i < count_; ++i) {
      CONTEXT context = {};
      context.ContextFlags = CONTEXT_CONTROL;
      if (!GetThreadContext(threads_[i].handle, &context)) return false;
      const uintptr_t pc = static_cast<uintptr_t>(context.Rip);
      if (pc < patch || pc >= patch + patch_bytes) continue;
      uintptr_t relocated = 0;
      if (!RelocateNativeActionPumpPc(patch, trampoline, pc, &relocated))
        return false;
      threads_[i].original_pc = context.Rip;
      context.Rip = static_cast<DWORD64>(relocated);
      if (!SetThreadContext(threads_[i].handle, &context)) return false;
      threads_[i].pc_changed = true;
    }
    return true;
  }

  bool CommitAndResume() { return ResumeAll(true); }
  bool AbortAndResume() { return ResumeAll(false); }

 private:
  bool Contains(DWORD id) const {
    for (size_t i = 0; i < count_; ++i) {
      if (threads_[i].id == id) return true;
    }
    return false;
  }

  bool Suspend(DWORD id) {
    if (count_ >= threads_.size()) return false;
    HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                   THREAD_SET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, id);
    if (!thread) return false;
    if (SuspendThread(thread) == static_cast<DWORD>(-1)) {
      CloseHandle(thread);
      return false;
    }
    threads_[count_].handle = thread;
    threads_[count_].id = id;
    ++count_;
    return true;
  }

  bool ResumeAll(bool keep_relocated_pc) {
    bool succeeded = true;
    for (size_t i = count_; i > 0; --i) {
      SuspendedThread& thread = threads_[i - 1];
      if (!keep_relocated_pc && thread.pc_changed) {
        CONTEXT context = {};
        context.ContextFlags = CONTEXT_CONTROL;
        if (GetThreadContext(thread.handle, &context)) {
          context.Rip = thread.original_pc;
          if (!SetThreadContext(thread.handle, &context)) succeeded = false;
        } else {
          succeeded = false;
        }
      }
      if (ResumeThread(thread.handle) == static_cast<DWORD>(-1))
        succeeded = false;
      CloseHandle(thread.handle);
      thread = {};
    }
    count_ = 0;
    return succeeded;
  }

  std::array<SuspendedThread, kMaximumSuspendedThreads> threads_{};
  size_t count_ = 0;
};

bool PoolStringEquals(uintptr_t entry, const char* expected) {
  if (!entry || !expected) return false;
  for (unsigned hop = 0; hop < 8; ++hop) {
    uint16_t flags = 0;
    if (!Read(entry + 0x08, &flags)) return false;
    if (flags & 0x4000) {
      uintptr_t next = 0;
      if (!ReadPointer(entry + 0x10, &next) || next == entry) return false;
      entry = next;
      continue;
    }
    uint32_t length = 0;
    if (!Read(entry + 0x10, &length) || length > 32 ||
        std::strlen(expected) != length) {
      return false;
    }
    if (flags & 0x8000) {
      if (!IsMemory(entry + 0x18, length * sizeof(wchar_t), false)) return false;
      const wchar_t* value = reinterpret_cast<const wchar_t*>(entry + 0x18);
      for (uint32_t index = 0; index < length; ++index) {
        if (value[index] != static_cast<unsigned char>(expected[index]))
          return false;
      }
      return true;
    }
    return IsMemory(entry + 0x18, length, false) &&
           std::memcmp(reinterpret_cast<const void*>(entry + 0x18), expected,
                       length) == 0;
  }
  return false;
}

bool FindObjectProperty(uintptr_t module, uintptr_t object, const char* key,
                        uintptr_t* value) {
  if (!value ||
      !IsExactVtable(object,
                     module + native_action_rva::kPipboyObjectVtable)) {
    return false;
  }
  uint32_t capacity = 0;
  uint32_t free = 0;
  uintptr_t entries = 0;
  if (!Read(object + 0x24, &capacity) || !Read(object + 0x28, &free) ||
      !Read(object + 0x40, &entries) || !capacity || free > capacity ||
      capacity > kMaximumRadioObjectCapacity ||
      (capacity & (capacity - 1)) ||
      !IsMemory(entries, static_cast<size_t>(capacity) * 0x18, false)) {
    return false;
  }
  for (uint32_t index = 0; index < capacity; ++index) {
    const uintptr_t slot = entries + static_cast<size_t>(index) * 0x18;
    uintptr_t next = 0;
    uintptr_t key_entry = 0;
    uintptr_t child = 0;
    if (!Read(slot + 0x10, &next) || !next || !Read(slot, &key_entry) ||
        !Read(slot + 0x08, &child)) {
      continue;
    }
    if (PoolStringEquals(key_entry, key)) {
      *value = child;
      return child != 0;
    }
  }
  return false;
}

NativeActionResult BaseResult(const NativeActionRequest& request) {
  NativeActionResult result;
  result.token = request.token;
  result.kind = request.kind;
  return result;
}

NativeActionResult ExecuteItem(uintptr_t module,
                               const NativeActionRequest& request) {
  NativeActionResult result = BaseResult(request);
  uintptr_t manager = 0;
  const uintptr_t manager_pointer =
      module + native_action_rva::kPipboyDataManagerPointer;
  if (!ReadPointer(manager_pointer, &manager)) return result;
  const uintptr_t inventory =
      manager + native_action_rva::kInventoryGroupOffset;
  if (!IsExactVtable(inventory,
                     module + native_action_rva::kInventoryGroupVtable)) {
    return result;
  }

  const auto inventory_version = reinterpret_cast<InventoryVersionFunction>(
      module + native_action_rva::kInventoryVersion);
  if (inventory_version(inventory) != request.item.inventory_version) {
    result.code = NativeActionResultCode::kStaleInventoryVersion;
    return result;
  }

  uint32_t handle_id = request.item.handle_id;
  const auto handle_valid = reinterpret_cast<HandleValidFunction>(
      module + native_action_rva::kHandleValid);
  if (!handle_valid(&handle_id)) {
    result.code = NativeActionResultCode::kInvalidInventoryItem;
    return result;
  }
  uintptr_t inventory_interface = 0;
  if (!ReadPointer(module + native_action_rva::kInventoryInterfacePointer,
                   &inventory_interface)) {
    return result;
  }
  const auto resolve = reinterpret_cast<ResolveInventoryHandleFunction>(
      module + native_action_rva::kResolveInventoryHandle);
  const auto stack_count = reinterpret_cast<InventoryStackCountFunction>(
      module + native_action_rva::kInventoryStackCount);
  const uintptr_t inventory_item = resolve(inventory_interface, &handle_id);
  if (!inventory_item) {
    result.code = NativeActionResultCode::kInvalidInventoryItem;
    return result;
  }
  if (stack_count(inventory_item) <= request.item.stack_id) {
    result.code = NativeActionResultCode::kInvalidInventoryStack;
    return result;
  }

  uintptr_t pipboy_manager = 0;
  uint8_t opening = 0;
  uint8_t closing = 0;
  uint32_t lowering_reason = 0;
  if (!ReadPointer(module + native_action_rva::kPipboyManagerPointer,
                   &pipboy_manager) ||
      !Read(pipboy_manager + 0x1C6, &opening) ||
      !Read(pipboy_manager + 0x1C7, &closing) ||
      !Read(pipboy_manager + 0x1A8, &lowering_reason)) {
    return result;
  }
  if (opening || closing || lowering_reason) {
    result.code = NativeActionResultCode::kPipboyTransitionInProgress;
    return result;
  }

  bool succeeded = false;
  bool secondary = false;
  const auto use_item = reinterpret_cast<UseInventoryItemFunction>(
      module + native_action_rva::kUseInventoryItem);
  // HandleValid receives a mutable pointer in the native command validator;
  // preserve any canonicalization it performed for the matching execute call.
  use_item(handle_id, request.item.stack_id, &succeeded, &secondary);
  result.state = succeeded;
  result.code = succeeded ? NativeActionResultCode::kSucceeded
                          : NativeActionResultCode::kRejectedByEngine;
  return result;
}

NativeActionResult ExecuteQuest(uintptr_t module,
                                const NativeActionRequest& request) {
  NativeActionResult result = BaseResult(request);
  const auto resolve = reinterpret_cast<ResolveQuestFunction>(
      module + native_action_rva::kResolveQuest);
  const auto toggle = reinterpret_cast<ToggleQuestFunction>(
      module + native_action_rva::kToggleQuestActive);
  const auto active = reinterpret_cast<QuestActiveFunction>(
      module + native_action_rva::kQuestActive);

  // ToggleQuestActiveStatusCommand explicitly accepts this sentinel tuple.
  const bool sentinel = request.quest.form_id == 0 && request.quest.type == 6;
  const uintptr_t quest = resolve(request.quest.form_id);
  if (!quest) {
    if (sentinel) {
      toggle(request.quest.form_id, request.quest.instance, request.quest.type);
      result.code = NativeActionResultCode::kSucceeded;
    } else {
      result.code = NativeActionResultCode::kInvalidQuest;
    }
    return result;
  }
  uint32_t instance = 0;
  uint8_t flags = 0;
  if (!Read(quest + 0x50, &instance) || !Read(quest + 0xF4, &flags))
    return result;
  if (!(flags & 0x01) || (flags & 0x42)) {
    result.code = NativeActionResultCode::kInvalidQuest;
    return result;
  }
  if (instance != request.quest.instance) {
    result.code = NativeActionResultCode::kStaleQuestInstance;
    return result;
  }
  const bool before = active(quest);
  toggle(request.quest.form_id, request.quest.instance, request.quest.type);
  result.state = active(quest);
  result.code = result.state != before ? NativeActionResultCode::kSucceeded
                                       : NativeActionResultCode::kRejectedByEngine;
  return result;
}

NativeActionResult ExecuteRadio(uintptr_t module,
                                const NativeActionRequest& request) {
  NativeActionResult result = BaseResult(request);
  const auto enable = reinterpret_cast<EnablePlayerRadioFunction>(
      module + native_action_rva::kEnablePlayerRadio);
  if (request.kind == NativeActionKind::kRadioOff) {
    enable(false, true);
    result.code = NativeActionResultCode::kSucceeded;
    result.state = false;
    return result;
  }

  uintptr_t manager = 0;
  if (!ReadPointer(module + native_action_rva::kPipboyDataManagerPointer,
                   &manager)) {
    return result;
  }
  const uintptr_t radio = manager + native_action_rva::kRadioGroupOffset;
  if (!IsExactVtable(radio,
                     module + native_action_rva::kRadioGroupVtable)) {
    return result;
  }
  const auto find = reinterpret_cast<FindRadioStationFunction>(
      module + native_action_rva::kFindRadioStation);
  const uintptr_t station = find(radio, request.radio.node_id);
  if (!station) {
    result.code = NativeActionResultCode::kInvalidRadioStation;
    return result;
  }
  uintptr_t frequency_value = 0;
  uintptr_t active_value = 0;
  float frequency = 0.0f;
  uint8_t is_active = 0;
  if (!FindObjectProperty(module, station, "frequency", &frequency_value) ||
      !FindObjectProperty(module, station, "active", &active_value) ||
      !IsExactVtable(frequency_value,
                     module + native_action_rva::kPipboyFloatVtable) ||
      !IsExactVtable(active_value,
                     module + native_action_rva::kPipboyBoolVtable) ||
      !Read(frequency_value + 0x18, &frequency) || !std::isfinite(frequency) ||
      !Read(active_value + 0x18, &is_active)) {
    result.code = NativeActionResultCode::kMalformedRadioStation;
    return result;
  }
  if (is_active) {
    enable(false, true);
    result.code = NativeActionResultCode::kSucceeded;
    result.state = false;
    return result;
  }

  uintptr_t pipboy_manager = 0;
  uintptr_t player = 0;
  if (!ReadPointer(module + native_action_rva::kPipboyManagerPointer,
                   &pipboy_manager) ||
      !ReadPointer(module + native_action_rva::kPlayerCharacterPointer,
                   &player) ||
      !IsMemory(pipboy_manager + 0x1AC, sizeof(frequency), true)) {
    return result;
  }
  enable(true, true);
  std::memcpy(reinterpret_cast<void*>(pipboy_manager + 0x1AC), &frequency,
              sizeof(frequency));
  const auto update = reinterpret_cast<PlayerRadioUpdateFunction>(
      module + native_action_rva::kPlayerRadioUpdate);
  update(player, 0);
  result.code = NativeActionResultCode::kSucceeded;
  result.state = true;
  return result;
}

NativeActionResult Execute(uintptr_t module,
                           const NativeActionRequest& request) {
  switch (request.kind) {
    case NativeActionKind::kUseInventoryItem:
      return ExecuteItem(module, request);
    case NativeActionKind::kToggleQuestActive:
      return ExecuteQuest(module, request);
    case NativeActionKind::kToggleRadioStation:
    case NativeActionKind::kRadioOff:
      return ExecuteRadio(module, request);
  }
  return BaseResult(request);
}

bool HookedPump(void* queue, float timeout, uint32_t passes) noexcept {
  BackendState& state = State();
  const bool result = state.original_pump(queue, timeout, passes);
  // Fallout's native calls do not use C++ exceptions across this ABI.  Catch
  // local allocation/mutex exceptions so a failed action drain cannot turn the
  // noexcept hook into std::terminate; native hardware faults remain fatal.
  try {
    for (size_t index = 0; index < kMaximumActionsPerPump; ++index) {
      NativeActionRequest request;
      if (!state.queue.Take(&request)) break;
      try {
        state.queue.Complete(Execute(state.module, request));
      } catch (...) {
        NativeActionResult failed = BaseResult(request);
        state.queue.Complete(failed);
      }
    }
  } catch (...) {
    // Leave later requests queued.  Readiness remains an installation gate;
    // callers can observe the absence of a completion for this pump cycle.
  }
  return result;
}

bool ValidateAddresses(uintptr_t module) {
  constexpr std::array<uintptr_t, 12> functions = {{
      native_action_rva::kMessageQueueProcessTask,
      native_action_rva::kUseInventoryItem,
      native_action_rva::kInventoryVersion,
      native_action_rva::kHandleValid,
      native_action_rva::kResolveInventoryHandle,
      native_action_rva::kInventoryStackCount,
      native_action_rva::kResolveQuest,
      native_action_rva::kToggleQuestActive,
      native_action_rva::kQuestActive,
      native_action_rva::kFindRadioStation,
      native_action_rva::kEnablePlayerRadio,
      native_action_rva::kPlayerRadioUpdate,
  }};
  for (const uintptr_t rva : functions) {
    if (!IsExecutable(module + rva)) return false;
  }
  return true;
}

bool WritePumpPatch(uintptr_t target,
                    const std::array<uint8_t, 7>& patch) {
  DWORD old_protection = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(target), patch.size(),
                      PAGE_EXECUTE_READWRITE, &old_protection)) {
    return false;
  }
  std::memcpy(reinterpret_cast<void*>(target), patch.data(), patch.size());
  bool flushed = FlushInstructionCache(GetCurrentProcess(),
                                        reinterpret_cast<void*>(target),
                                        patch.size()) != FALSE;
  if (!flushed) {
    std::memcpy(reinterpret_cast<void*>(target), kPumpPrologue.data(),
                kPumpPrologue.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target),
                          kPumpPrologue.size());
  }
  DWORD ignored = 0;
  const bool restored =
      VirtualProtect(reinterpret_cast<void*>(target), patch.size(),
                     old_protection, &ignored) != FALSE;
  if (!restored && flushed) {
    std::memcpy(reinterpret_cast<void*>(target), kPumpPrologue.data(),
                kPumpPrologue.size());
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target),
                          kPumpPrologue.size());
    VirtualProtect(reinterpret_cast<void*>(target), patch.size(),
                   old_protection, &ignored);
    return false;
  }
  return flushed && restored;
}

}  // namespace

bool NativeActionQueue::Enqueue(const NativeActionRequest& request) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (outstanding_ >= requests_.size() || request_size_ >= requests_.size())
    return false;
  requests_[(request_head_ + request_size_) % requests_.size()] = request;
  ++request_size_;
  ++outstanding_;
  return true;
}

bool NativeActionQueue::Take(NativeActionRequest* request) noexcept {
  if (!request) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!request_size_) return false;
  *request = requests_[request_head_];
  request_head_ = (request_head_ + 1) % requests_.size();
  --request_size_;
  ++in_flight_;
  return true;
}

bool NativeActionQueue::Complete(const NativeActionResult& result) noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!in_flight_ || result_size_ >= results_.size()) return false;
  results_[(result_head_ + result_size_) % results_.size()] = result;
  ++result_size_;
  --in_flight_;
  return true;
}

bool NativeActionQueue::Poll(NativeActionResult* result) noexcept {
  if (!result) return false;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!result_size_) return false;
  *result = results_[result_head_];
  result_head_ = (result_head_ + 1) % results_.size();
  --result_size_;
  --outstanding_;
  return true;
}

size_t NativeActionQueue::Outstanding() const noexcept {
  std::lock_guard<std::mutex> lock(mutex_);
  return outstanding_;
}

bool IsNativeActionImageHash(const char* sha256) noexcept {
  if (!sha256 || std::strlen(sha256) != sizeof(kExecutableSha256) - 1)
    return false;
  for (size_t index = 0; index < sizeof(kExecutableSha256) - 1; ++index) {
    char actual = sha256[index];
    if (actual >= 'a' && actual <= 'f') actual -= 'a' - 'A';
    if (actual != kExecutableSha256[index]) return false;
  }
  return true;
}

bool NativeActionPumpPrologueMatches(const void* bytes, size_t size) noexcept {
  return bytes && size >= kPumpPrologue.size() &&
         std::memcmp(bytes, kPumpPrologue.data(), kPumpPrologue.size()) == 0;
}

bool BuildNativeActionPumpPatch(
    uintptr_t source, uintptr_t relay,
    std::array<uint8_t, 7>* patch) noexcept {
  if (!patch) return false;
  int32_t displacement = 0;
  if (!RelativeJump(source, relay, &displacement)) return false;
  *patch = {};
  (*patch)[0] = 0xE9;
  std::memcpy(patch->data() + 1, &displacement, sizeof(displacement));
  (*patch)[5] = 0x90;
  (*patch)[6] = 0x90;
  return true;
}

bool RelocateNativeActionPumpPc(uintptr_t patch, uintptr_t trampoline,
                                uintptr_t pc, uintptr_t* relocated) noexcept {
  if (!relocated || pc < patch || pc >= patch + kPumpPrologue.size())
    return false;
  const uintptr_t offset = pc - patch;
  // These are the only decoded instruction boundaries in the overwritten
  // seven-byte canary: mov rax,rsp (3), then mov [rax+8],rbx (4).
  if (offset != 0 && offset != 3) return false;
  *relocated = trampoline + offset;
  return true;
}

const char* NativeActionInstallStatusLabel(
    NativeActionInstallStatus status) noexcept {
  switch (status) {
    case NativeActionInstallStatus::kNotInstalled:
      return "not-installed";
    case NativeActionInstallStatus::kDisabledByConfiguration:
      return "disabled-by-configuration";
    case NativeActionInstallStatus::kInstalling:
      return "installing";
    case NativeActionInstallStatus::kReady:
      return "ready";
    case NativeActionInstallStatus::kUnsupportedImage:
      return "unsupported-image";
    case NativeActionInstallStatus::kPrologueMismatch:
      return "prologue-mismatch";
    case NativeActionInstallStatus::kInvalidAddress:
      return "invalid-address";
    case NativeActionInstallStatus::kTrampolineAllocationFailed:
      return "trampoline-allocation-failed";
    case NativeActionInstallStatus::kThreadTransactionFailed:
      return "thread-transaction-failed";
    case NativeActionInstallStatus::kPatchFailed:
      return "patch-failed";
  }
  return "unknown";
}

const char* NativeActionResultCodeLabel(NativeActionResultCode code) noexcept {
  switch (code) {
    case NativeActionResultCode::kSucceeded:
      return "succeeded";
    case NativeActionResultCode::kStaleInventoryVersion:
      return "stale-inventory-version";
    case NativeActionResultCode::kInvalidInventoryItem:
      return "invalid-inventory-item";
    case NativeActionResultCode::kInvalidInventoryStack:
      return "invalid-inventory-stack";
    case NativeActionResultCode::kPipboyTransitionInProgress:
      return "pipboy-transition-in-progress";
    case NativeActionResultCode::kInvalidQuest:
      return "invalid-quest";
    case NativeActionResultCode::kStaleQuestInstance:
      return "stale-quest-instance";
    case NativeActionResultCode::kInvalidRadioStation:
      return "invalid-radio-station";
    case NativeActionResultCode::kMalformedRadioStation:
      return "malformed-radio-station";
    case NativeActionResultCode::kRejectedByEngine:
      return "rejected-by-engine";
    case NativeActionResultCode::kEngineUnavailable:
      return "engine-unavailable";
  }
  return "unknown";
}

NativeActionInstallStatus InstallNativeActions(
    uintptr_t fallout_module, const char* executable_sha256) noexcept {
  BackendState& state = State();
  std::lock_guard<std::mutex> install_lock(state.install_mutex);
  const NativeActionInstallStatus current =
      state.status.load(std::memory_order_acquire);
  if (current != NativeActionInstallStatus::kNotInstalled) return current;
  state.status.store(NativeActionInstallStatus::kInstalling,
                     std::memory_order_release);
  auto fail = [&](NativeActionInstallStatus status) {
    state.status.store(status, std::memory_order_release);
    return status;
  };

  if (!IsNativeActionImageHash(executable_sha256) || !fallout_module ||
      fallout_module !=
          reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))) {
    return fail(NativeActionInstallStatus::kUnsupportedImage);
  }
  if (!ValidateAddresses(fallout_module))
    return fail(NativeActionInstallStatus::kInvalidAddress);

  const uintptr_t target =
      fallout_module + native_action_rva::kMessageQueueProcessTask;
  std::array<uint8_t, kPumpPrologue.size()> actual = {};
  if (!IsMemory(target, actual.size(), false))
    return fail(NativeActionInstallStatus::kInvalidAddress);
  std::memcpy(actual.data(), reinterpret_cast<const void*>(target), actual.size());
  if (!NativeActionPumpPrologueMatches(actual.data(), actual.size()))
    return fail(NativeActionInstallStatus::kPrologueMismatch);

  void* page = AllocateNear(target);
  if (!page)
    return fail(NativeActionInstallStatus::kTrampolineAllocationFailed);
  auto* code = static_cast<uint8_t*>(page);
  std::memcpy(code + kTrampolineOffset, kPumpPrologue.data(),
              kPumpPrologue.size());
  WriteAbsoluteJump(code + kTrampolineOffset + kPumpPrologue.size(),
                    target + kPumpPrologue.size());
  WriteAbsoluteJump(code + kRelayOffset,
                    reinterpret_cast<uintptr_t>(&HookedPump));
  if (!FlushInstructionCache(GetCurrentProcess(), page,
                             kRelayOffset + kAbsoluteJumpBytes)) {
    VirtualFree(page, 0, MEM_RELEASE);
    return fail(NativeActionInstallStatus::kPatchFailed);
  }
  DWORD old_page_protection = 0;
  if (!VirtualProtect(page, kRelayOffset + kAbsoluteJumpBytes,
                      PAGE_EXECUTE_READ, &old_page_protection)) {
    VirtualFree(page, 0, MEM_RELEASE);
    return fail(NativeActionInstallStatus::kPatchFailed);
  }

  std::array<uint8_t, kPumpPrologue.size()> patch = {};
  if (!BuildNativeActionPumpPatch(
          target, reinterpret_cast<uintptr_t>(page) + kRelayOffset, &patch)) {
    VirtualFree(page, 0, MEM_RELEASE);
    return fail(NativeActionInstallStatus::kTrampolineAllocationFailed);
  }

  ThreadTransaction threads;
  if (!threads.SuspendAll(target, patch.size(),
                          reinterpret_cast<uintptr_t>(page) +
                              kTrampolineOffset)) {
    if (threads.AbortAndResume()) VirtualFree(page, 0, MEM_RELEASE);
    return fail(NativeActionInstallStatus::kThreadTransactionFailed);
  }
  // Recheck under the suspension transaction: another detour must never be
  // overwritten between the initial canary and the actual patch.
  std::memcpy(actual.data(), reinterpret_cast<const void*>(target), actual.size());
  if (!NativeActionPumpPrologueMatches(actual.data(), actual.size())) {
    if (threads.AbortAndResume()) VirtualFree(page, 0, MEM_RELEASE);
    return fail(NativeActionInstallStatus::kPrologueMismatch);
  }

  state.module = fallout_module;
  state.trampoline_page = page;
  state.original_pump = reinterpret_cast<PumpFunction>(
      reinterpret_cast<uintptr_t>(page) + kTrampolineOffset);
  if (!WritePumpPatch(target, patch)) {
    // Once a write was attempted, keep the original trampoline and hook state
    // alive even if protection/cache restoration reported failure.  The bytes
    // may be either original or detoured; relocated PCs are safe in both cases,
    // and readiness prevents requests from entering an uncertain hook.
    threads.CommitAndResume();
    return fail(NativeActionInstallStatus::kPatchFailed);
  }
  if (!threads.CommitAndResume())
    return fail(NativeActionInstallStatus::kThreadTransactionFailed);
  state.status.store(NativeActionInstallStatus::kReady,
                     std::memory_order_release);
  return NativeActionInstallStatus::kReady;
}

NativeActionInstallStatus ConfigureNativeActions(
    bool enabled, uintptr_t fallout_module,
    const char* executable_sha256) noexcept {
  if (!enabled)
    return NativeActionInstallStatus::kDisabledByConfiguration;
  return InstallNativeActions(fallout_module, executable_sha256);
}

NativeActionInstallStatus GetNativeActionInstallStatus() noexcept {
  return State().status.load(std::memory_order_acquire);
}

bool NativeActionsReady() noexcept {
  return GetNativeActionInstallStatus() == NativeActionInstallStatus::kReady;
}

bool QueueNativeAction(const NativeActionRequest& request) noexcept {
  return NativeActionsReady() && State().queue.Enqueue(request);
}

bool PollNativeActionResult(NativeActionResult* result) noexcept {
  return State().queue.Poll(result);
}

}  // namespace fo4ds
