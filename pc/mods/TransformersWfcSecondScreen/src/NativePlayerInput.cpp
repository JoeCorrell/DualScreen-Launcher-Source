#include "NativePlayerInput.h"
#include <array>
#include <atomic>
#include <cstring>
#include "Hook.h"
#include "Log.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" {
void WfcHookInputReset();
void* WfcInputResetOriginal = nullptr;
}
namespace {
constexpr uintptr_t kInputImageBase = 0x10900000u;
constexpr uintptr_t kIgnoreMoveVa = 0x1177F960u, kIgnoreLookVa = 0x1177F8E0u;
// Complete compiled PlayerController.Ignore*Input bodies. No relocations,
// calls, callbacks, allocations or state dispatch in either leaf. ECX is
// UObject+10, one 32-bit boolean stack parameter, RET 4.
constexpr uint8_t kIgnoreLookCode[] = {
    0x8B,0x81,0x88,0x00,0x00,0x00,0x85,0xC0,0x74,0x0F,0x80,0xB8,0x9C,0x03,0x00,0x00,
    0x00,0x74,0x06,0x80,0x79,0x56,0x01,0x76,0x27,0x0F,0xB6,0x81,0xF6,0x02,0x00,0x00,
    0x33,0xD2,0x39,0x54,0x24,0x04,0x0F,0x95,0xC2,0x8D,0x54,0x12,0xFF,0x03,0xD0,0xB8,
    0x00,0x00,0x00,0x00,0x0F,0x98,0xC0,0x48,0x23,0xC2,0x88,0x81,0xF6,0x02,0x00,0x00,
    0xC2,0x04,0x00,
};
constexpr auto MoveCode() {
  std::array<uint8_t, sizeof(kIgnoreLookCode)> bytes{};
  for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = kIgnoreLookCode[i];
  bytes[28] = bytes[60] = 0xF5;
  return bytes;
}
constexpr auto kIgnoreMoveCode = MoveCode();
constexpr uintptr_t kInputResetVa = 0x117934E0u;
constexpr uint8_t kInputResetCode[] = {
    0x55,0x8B,0xEC,0x83,0xE4,0xF8,0x51,0x8B,0x86,0x88,0x00,0x00,0x00,0x57,0x85,0xC0,
    0x74,0x0F,0x80,0xB8,0x9C,0x03,0x00,0x00,0x00,0x74,0x06,0x80,0x7E,0x56,0x01,0x76,
    0x2F,0x8D,0x7E,0xF0,0x68,0x58,0x05,0x00,0x00,0xE8,0x02,0xBD,0xF5,0xFF,0x8A,0x80,
    0x05,0x03,0x00,0x00,0x68,0x58,0x05,0x00,0x00,0x88,0x86,0xF5,0x02,0x00,0x00,0xE8,
    0xEC,0xBC,0xF5,0xFF,0x8A,0x88,0x06,0x03,0x00,0x00,0x88,0x8E,0xF6,0x02,0x00,0x00,
    0x5F,0x8B,0xE5,0x5D,0xC3,
};
using InputCall = void (__attribute__((fastcall)) *)(void*, void*, uint32_t);
struct InputBindings {
  HMODULE module = nullptr;
  DWORD thread = 0;
  InputCall move = nullptr, look = nullptr;
  void* reset = nullptr;
};
InputBindings g_input;
NativePlayerInputLease* g_input_owners[2] = {};
InlineHook g_input_reset_hook;
std::atomic<bool> g_input_stopping{false};
thread_local bool g_input_busy = false;
bool InputMemory(const void* p, size_t bytes, HMODULE module, bool executable) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      (module && info.AllocationBase != module) ||
      bytes > info.RegionSize - (reinterpret_cast<uintptr_t>(p) -
                                reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD mode = info.Protect & 0xFFu;
  return executable ? (mode == PAGE_EXECUTE_READ || mode == PAGE_EXECUTE_READWRITE ||
                       mode == PAGE_EXECUTE_WRITECOPY) :
      (mode == PAGE_READWRITE || mode == PAGE_WRITECOPY ||
       mode == PAGE_EXECUTE_READWRITE || mode == PAGE_EXECUTE_WRITECOPY);
}
bool BindInputImage(HMODULE module, DWORD thread, InputBindings* output) {
  if (!module || !thread || thread != GetCurrentThreadId() || !output) return false;
  const uintptr_t image = reinterpret_cast<uintptr_t>(module);
  auto* move = reinterpret_cast<void*>(image + kIgnoreMoveVa - kInputImageBase);
  auto* look = reinterpret_cast<void*>(image + kIgnoreLookVa - kInputImageBase);
  auto* reset = reinterpret_cast<void*>(image + kInputResetVa - kInputImageBase);
  if (!InputMemory(move, kIgnoreMoveCode.size(), module, true) ||
      !InputMemory(look, sizeof(kIgnoreLookCode), module, true) ||
      !InputMemory(reset, sizeof(kInputResetCode), module, true) ||
      std::memcmp(move, kIgnoreMoveCode.data(), kIgnoreMoveCode.size()) ||
      std::memcmp(look, kIgnoreLookCode, sizeof(kIgnoreLookCode)) ||
      std::memcmp(reset, kInputResetCode, sizeof(kInputResetCode))) return false;
  *output = {module, thread, reinterpret_cast<InputCall>(move), reinterpret_cast<InputCall>(look), reset};
  return true;
}
bool EnsureInputResetObserver() {
  if (g_input_stopping.load(std::memory_order_acquire)) return false;
  if (g_input_reset_hook.target)
    return g_input_reset_hook.target == g_input.reset && WfcInputResetOriginal;
  // First acquisition is on the OUTER game tick, before invoking any game
  // function. Publish the trampoline before installing its jump. The six
  // stolen bytes are complete, position-independent prologue instructions.
  return InputMemory(g_input.reset, sizeof(kInputResetCode), nullptr, true) &&
      !std::memcmp(g_input.reset, kInputResetCode, sizeof(kInputResetCode)) &&
      InstallInlineHook(&g_input_reset_hook, g_input.reset, reinterpret_cast<void*>(&WfcHookInputReset),
                        kInputResetCode, 6, &WfcInputResetOriginal);
}
bool InputAxisValid(NativeInputAxis axis) {
  return axis == NativeInputAxis::kMove || axis == NativeInputAxis::kLook;
}
size_t AxisIndex(NativeInputAxis axis) { return axis == NativeInputAxis::kMove ? 0 : 1; }
bool InputByte(void* object, const wchar_t* name, uint32_t offset, uint8_t* value) {
  uint32_t actual = 0;
  return UnrealResolveBytePropertyOffset(object, name, &actual) && actual == offset &&
      UnrealReadByteProperty(object, name, value);
}
bool InputContext(void* player, void** world) {
  bool deleting = true;
  uint32_t offset = 0;
  uint8_t role = 0, mode = 0xFF;
  return g_input.module && !g_input_stopping.load(std::memory_order_acquire) &&
      GetCurrentThreadId() == g_input.thread &&
      UnrealObjectIsA(player, L"TnPlayerController") &&
      UnrealReadBoolProperty(player, L"bDeleteMe", &deleting) && !deleting &&
      UnrealResolveObjectPropertyOffset(player, L"WorldInfo", &offset) && offset == 0x98 &&
      UnrealReadObjectProperty(player, L"WorldInfo", world) &&
      UnrealObjectIsA(*world, L"WorldInfo") &&
      UnrealReadBoolProperty(*world, L"bDeleteMe", &deleting) && !deleting &&
      InputByte(*world, L"NetMode", 0x39C, &mode) && mode == 0 &&
      InputByte(player, L"Role", 0x66, &role) && role == 3;
}
bool InputCounter(void* player, NativeInputAxis axis, uint8_t* value) {
  if (!InputAxisValid(axis)) return false;
  const uint32_t offset = axis == NativeInputAxis::kMove ? 0x305 : 0x306;
  return InputMemory(static_cast<uint8_t*>(player) + offset, 1, nullptr, false) &&
      InputByte(player, axis == NativeInputAxis::kMove ? L"bIgnoreMoveInput" : L"bIgnoreLookInput",
                offset, value);
}
void CallInput(void* player, NativeInputAxis axis, bool acquire) {
  (axis == NativeInputAxis::kMove ? g_input.move : g_input.look)(
      static_cast<uint8_t*>(player) + kNativeScriptThisOffset, nullptr, acquire ? 1u : 0u);
}
}  // namespace

extern "C" void WfcObserveInputReset(void* script_this) {
  // Notification only. No engine calls, logging, allocations, suppression or
  // replacement reset values. Assembly preserves the native register/FPU ABI.
  if (g_input_stopping.load(std::memory_order_acquire) || GetCurrentThreadId() != g_input.thread) return;
  const uintptr_t compiled = reinterpret_cast<uintptr_t>(script_this);
  if (compiled <= kNativeScriptThisOffset) return;
  void* player = reinterpret_cast<void*>(compiled - kNativeScriptThisOffset);
  for (auto* lease : g_input_owners)
    if (lease && lease->held && lease->controller == player) lease->invalidated = true;
}

bool PrepareNativePlayerInput(HMODULE module, DWORD thread, void* player) {
  if (!module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_input.module && !BindInputImage(module, thread, &g_input)) {
    Log(LogLevel::kError, "[SquadSwitch] native input signatures mismatch");
    return false;
  }
  void* world = nullptr;
  uint8_t move = 0, look = 0;
  return module == g_input.module && thread == g_input.thread && InputContext(player, &world) &&
      InputCounter(player, NativeInputAxis::kMove, &move) &&
      InputCounter(player, NativeInputAxis::kLook, &look) && move < 255 && look < 255 &&
      !g_input_owners[0] && !g_input_owners[1];
}

NativeInputResult NativeAcquirePlayerInput(void* player, NativeInputAxis axis, NativePlayerInputLease* lease) {
  void* world = nullptr;
  uint8_t before = 0;
  if (g_input_busy || !lease || lease->held || !InputAxisValid(axis) ||
      g_input_owners[AxisIndex(axis)] || !InputContext(player, &world) ||
      !InputCounter(player, axis, &before) || before == 255 ||
      !EnsureInputResetObserver()) return NativeInputResult::kRejected;
  // The native byte(Max(counter+1,0)) wraps 255 to zero; never enter that path.
  *lease = {player, world, UnrealObjectIndex(player), UnrealObjectIndex(world), before, axis, true};
  g_input_owners[AxisIndex(axis)] = lease;
  g_input_busy = true;
  struct ClearBusy { ~ClearBusy() { g_input_busy = false; } } guard;
  CallInput(player, axis, true);
  return NativePlayerInputHeld(*lease) ? NativeInputResult::kApplied : NativeInputResult::kStateMismatch;
}

bool NativePlayerInputHeld(const NativePlayerInputLease& lease) {
  void* world = nullptr;
  uint8_t value = 0;
  return lease.held && !lease.invalidated && InputAxisValid(lease.axis) &&
      g_input_owners[AxisIndex(lease.axis)] == &lease && lease.baseline < 255 &&
      InputContext(lease.controller, &world) && world == lease.world &&
      UnrealObjectIndex(lease.controller) == lease.controller_index &&
      UnrealObjectIndex(world) == lease.world_index && InputCounter(lease.controller, lease.axis, &value) &&
      value == static_cast<uint8_t>(lease.baseline + 1);
}

NativeInputResult NativeReleasePlayerInput(NativePlayerInputLease* lease) {
  // Restart/ClientRestart can RESET these counters. A stale boolean must never
  // decrement another gameplay/menu lock or report a reset-to-zero as success.
  // This does not make the full Possess->Restart path suitable for live swaps.
  if (g_input_busy || !lease || !NativePlayerInputHeld(*lease)) return NativeInputResult::kRejected;
  g_input_busy = true;
  struct ClearBusy { ~ClearBusy() { g_input_busy = false; } } guard;
  CallInput(lease->controller, lease->axis, false);
  uint8_t after = 0;
  if (!InputCounter(lease->controller, lease->axis, &after) || after != lease->baseline)
    return NativeInputResult::kStateMismatch;
  g_input_owners[AxisIndex(lease->axis)] = nullptr;
  lease->held = false;
  return NativeInputResult::kApplied;
}

void StopNativePlayerInput() {
  // Proxy.cpp pins the DLL. Do not remove an inline patch from the teardown
  // worker while a native call could be entering. The observer becomes inert;
  // the exact original reset continues to run through its retained trampoline.
  g_input_stopping.store(true, std::memory_order_release);
}
}  // namespace wfcds
