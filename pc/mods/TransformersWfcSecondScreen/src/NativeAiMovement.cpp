#include "NativeAiMovement.h"

#include <array>
#include <cstdint>
#include <cstring>
#include "Log.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {
constexpr uintptr_t kMovementImageBase = 0x10900000u;
constexpr size_t kCancelMoveSlot = 0x29C / 4;
constexpr size_t kPoppedMoveSlot = 0x44 / 4;
struct MoveStateSpec { uintptr_t table, cancel, popped; bool moving; };
// Default constructor 10A27C90, state constructors 10BB4680..10BB4800.
// Only MovingToPosition/MovingToActor override CancelMove. The base is RET.
constexpr MoveStateSpec kMoveStates[] = {
    {0x127F96D8u, 0x109D4530u, 0x10902790u, false},
    {0x128AFB58u, 0x10BBA8A0u, 0x10BBA920u, true},
    {0x128AFE30u, 0x10BBA9E0u, 0x10BBAAD0u, true},
    {0x128B0108u, 0x109D4530u, 0x10902790u, false},
    {0x128B03E0u, 0x109D4530u, 0x10902790u, false},
    {0x128B06B8u, 0x109D4530u, 0x10BBADA0u, false},
    {0x128B0990u, 0x109D4530u, 0x10BBADF0u, false},
    {0x128B0C68u, 0x109D4530u, 0x10902790u, false},
};
constexpr uint8_t kCancelPositionCode[] = {
    0x8B, 0x81, 0x88, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0F, 0x80, 0xB8,
    0x9C, 0x03, 0x00, 0x00, 0x00, 0x74, 0x06, 0x80, 0x79, 0x56, 0x01, 0x76,
    0x0C, 0x56, 0x6A, 0x00, 0x8D, 0x71, 0xF0, 0xE8, 0xBC, 0x5B, 0xB4, 0x00,
    0x5E, 0xC3,
};
constexpr uint8_t kPoppedPositionCode[] = {
    0x8B, 0xD1, 0x8B, 0x82, 0x88, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0F,
    0x80, 0xB8, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x74, 0x06, 0x80, 0x7A, 0x56,
    0x01, 0x76, 0x46, 0x6A, 0x00, 0xC7, 0x82, 0x0C, 0x03, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xE8, 0xD4, 0x1E, 0xB4, 0x00, 0x83, 0xC4, 0x04, 0x33,
    0xC0, 0x89, 0x82, 0x54, 0x04, 0x00, 0x00, 0x89, 0x82, 0x58, 0x04, 0x00,
    0x00, 0x89, 0x82, 0x5C, 0x04, 0x00, 0x00, 0x89, 0x82, 0x60, 0x04, 0x00,
    0x00, 0x89, 0x82, 0x64, 0x04, 0x00, 0x00, 0x89, 0x82, 0x68, 0x04, 0x00,
    0x00, 0x89, 0x82, 0x6C, 0x04, 0x00, 0x00, 0x89, 0x82, 0x70, 0x04, 0x00,
    0x00, 0xC3,
};
constexpr uint8_t kPopStateHead[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x46, 0x28, 0x83, 0xEC, 0x08,
    0x53, 0x57, 0x85, 0xC0, 0x0F, 0x84, 0xFE, 0x00, 0x00, 0x00, 0x83, 0x78,
    0x38, 0x00, 0x0F, 0x84, 0xF4, 0x00, 0x00, 0x00,
};
struct MoveBindings { HMODULE module = nullptr; DWORD thread = 0; uintptr_t image = 0; };
MoveBindings g_movement;
thread_local bool g_movement_busy = false;
using MoveCall = void (__attribute__((fastcall)) *)(void*, void*);

bool MoveReadable(const void* p, size_t size, HMODULE module = nullptr) {
  MEMORY_BASIC_INFORMATION info = {};
  return p && VirtualQuery(p, &info, sizeof(info)) && info.State == MEM_COMMIT &&
      (!module || info.AllocationBase == module) && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
      size <= info.RegionSize - (reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(info.BaseAddress));
}

bool BindMovementImage(HMODULE module, DWORD thread, MoveBindings* result) {
  if (!module || !thread || GetCurrentThreadId() != thread || !result) return false;
  const auto image = reinterpret_cast<uintptr_t>(module);
  auto at = [&](uintptr_t va) { return reinterpret_cast<const uint8_t*>(image + va - kMovementImageBase); };
  if (!MoveReadable(at(0x109D4530), 1, module) || *at(0x109D4530) != 0xC3 ||
      !MoveReadable(at(0x11700480), sizeof(kPopStateHead), module) ||
      std::memcmp(at(0x11700480), kPopStateHead, sizeof(kPopStateHead))) return false;
  for (int state = 1; state <= 2; ++state) {
    std::array<uint8_t, sizeof(kCancelPositionCode)> cancel;
    std::array<uint8_t, sizeof(kPoppedPositionCode)> popped;
    std::memcpy(cancel.data(), kCancelPositionCode, cancel.size());
    std::memcpy(popped.data(), kPoppedPositionCode, popped.size());
    if (state == 2) { // Only their relative PopState / GC-call displacements differ.
      cancel[32] = 0x7C; cancel[33] = 0x5A;
      popped[40] = 0x24; popped[41] = 0x1D;
    }
    const auto& spec = kMoveStates[state];
    if (!MoveReadable(at(spec.cancel), cancel.size(), module) ||
        !MoveReadable(at(spec.popped), popped.size(), module) ||
        std::memcmp(at(spec.cancel), cancel.data(), cancel.size()) ||
        std::memcmp(at(spec.popped), popped.data(), popped.size())) return false;
  }
  for (const auto& state : kMoveStates) {
    const auto* table = reinterpret_cast<const uintptr_t*>(at(state.table));
    if (!MoveReadable(table, 0x2A0, module) ||
        table[kCancelMoveSlot] != reinterpret_cast<uintptr_t>(at(state.cancel)) ||
        table[kPoppedMoveSlot] != reinterpret_cast<uintptr_t>(at(state.popped))) return false;
  }
  *result = {module, thread, image};
  return true;
}

const MoveStateSpec* MovementState(void* controller) {
  if (!g_movement.module || GetCurrentThreadId() != g_movement.thread ||
      !UnrealObjectIsA(controller, L"TnAiController")) return nullptr;
  bool deleting = true;
  void* world = nullptr;
  uint8_t net_mode = 0xFF;
  if (!UnrealReadBoolProperty(controller, L"bDeleteMe", &deleting) || deleting ||
      !UnrealReadObjectProperty(controller, L"WorldInfo", &world) ||
      !UnrealObjectIsA(world, L"WorldInfo") ||
      !UnrealReadByteProperty(world, L"NetMode", &net_mode) || net_mode != 0) return nullptr;
  const auto* table = *reinterpret_cast<const uintptr_t* const*>(static_cast<uint8_t*>(controller) + 16);
  for (const auto& state : kMoveStates) {
    const auto delta = g_movement.image - kMovementImageBase;
    if (reinterpret_cast<uintptr_t>(table) == state.table + delta)
      return table[kCancelMoveSlot] == state.cancel + delta &&
          table[kPoppedMoveSlot] == state.popped + delta ? &state : nullptr;
  }
  return nullptr;
}

bool ReadMoveDelegates(void* controller, bool* cleared) {
  UnrealScriptDelegate near_delegate{}, failed_delegate{};
  uint32_t near_offset = 0, failed_offset = 0;
  if (!UnrealReadDelegateProperty(controller, L"__NearDestinationNotify__Delegate", &near_delegate, &near_offset) ||
      !UnrealReadDelegateProperty(controller, L"__MoveFailedNotify__Delegate", &failed_delegate, &failed_offset) ||
      !((near_offset == 0x464 && failed_offset == 0x474) ||
        (near_offset == 0x474 && failed_offset == 0x464))) return false;
  const UnrealScriptDelegate empty{};
  *cleared = !std::memcmp(&near_delegate, &empty, sizeof(empty)) &&
      !std::memcmp(&failed_delegate, &empty, sizeof(empty));
  return true;
}

bool MoveStackValid(void* controller) {
  void* frame = nullptr;
  std::memcpy(&frame, static_cast<uint8_t*>(controller) + 0x28, 4);
  if (!MoveReadable(frame, 0x54)) return false;
  void* state = nullptr;
  std::memcpy(&state, static_cast<uint8_t*>(frame) + 0x38, 4);
  UnrealArrayView stack;
  std::memcpy(&stack, static_cast<uint8_t*>(frame) + 0x48, sizeof(stack));
  if (!UnrealObjectValid(state) || stack.count < 1 || stack.count > 64 ||
      stack.capacity < stack.count || stack.capacity > 128 ||
      !MoveReadable(stack.data, stack.count * 12)) return false;
  for (int i = 0; i < stack.count; ++i) {
    std::memcpy(&state, static_cast<uint8_t*>(stack.data) + i * 12, 4);
    if (!UnrealObjectValid(state)) return false;
  }
  return true;
}
}  // namespace

bool PrepareNativeAiMovement(HMODULE module, DWORD thread, void* controller) {
  if (!module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_movement.module && !BindMovementImage(module, thread, &g_movement)) {
    Log(LogLevel::kError, "[SquadSwitch] native AI movement signature mismatch");
    return false;
  }
  uint32_t offset = 0;
  bool cleared = false;
  return module == g_movement.module && thread == g_movement.thread && MovementState(controller) &&
      UnrealResolveObjectPropertyOffset(controller, L"WorldInfo", &offset) && offset == 0x98 &&
      UnrealResolveObjectPropertyOffset(controller, L"PathFocus", &offset) && offset == 0x31C &&
      ReadMoveDelegates(controller, &cleared);
}

bool NativeAiMoveDelegatesCleared(void* controller, bool* output) {
  if (!output) return false;
  *output = false;
  return MovementState(controller) && ReadMoveDelegates(controller, output);
}

NativeAiMovementResult NativeAiCancelMove(void* controller) {
  if (g_movement_busy) return NativeAiMovementResult::kRejected;
  const auto* state = MovementState(controller);
  bool cleared = false;
  if (!state || !ReadMoveDelegates(controller, &cleared) ||
      (state->moving && !MoveStackValid(controller))) return NativeAiMovementResult::kRejected;
  g_movement_busy = true;
  struct ResetBusy { ~ResetBusy() { g_movement_busy = false; } } guard;
  const auto call = reinterpret_cast<MoveCall>(g_movement.image + state->cancel - kMovementImageBase);
  call(static_cast<uint8_t*>(controller) + kNativeScriptThisOffset, nullptr);
  const auto* after = MovementState(controller);
  void* focus = nullptr;
  const bool accepted = after && !after->moving &&
      ReadMoveDelegates(controller, &cleared) && cleared &&
      (!state->moving || (UnrealReadObjectProperty(controller, L"PathFocus", &focus) && !focus));
  if (!accepted) Log(LogLevel::kWarning, "[SquadSwitch] native CancelMove failed state/delegate cleanup");
  return accepted ? NativeAiMovementResult::kApplied : NativeAiMovementResult::kStateMismatch;
}
}  // namespace wfcds
