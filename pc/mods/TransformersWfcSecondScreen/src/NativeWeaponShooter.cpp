#include "NativeWeaponShooter.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "Log.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {

constexpr uintptr_t kImageBase = 0x10900000u;
constexpr uintptr_t kActiveWeaponRva = 0x002C8A20u;
constexpr uintptr_t kActivateRva = 0x002C8AA0u;
constexpr uintptr_t kResetRva = 0x002C8C30u;
constexpr uintptr_t kDeactivateExecRva = 0x002C9CD0u;
constexpr size_t kStartFireSlot = 0x48 / 4;
constexpr size_t kStopFireSlot = 0x4C / 4;
constexpr size_t kIsFiringSlot = 0x50 / 4;
constexpr size_t kInitialStateSlot = 0x64 / 4;

// The executable's state constructor 10BC8860 selects these secondary-base
// vtables. Entries are StartFire, StopFire, IsFiring, SetupInitialState VAs.
// Both no-op and inherited methods are real native state dispatch entries.
struct StateSpec {
  uintptr_t table_va;
  std::array<uintptr_t, 4> methods;
};
constexpr StateSpec kStates[] = {
    {0x128020A0u, {0x109F9950u, 0x109F9960u, 0x109F9970u, 0x10BC8CD0u}},
    {0x128B0F40u, {0x10BB3E40u, 0x10BB3E30u, 0x10BB3E20u, 0x10BC8CD0u}},
    {0x128B0FB0u, {0x10BC8EE0u, 0x10BB3E60u, 0x10BB3E50u, 0x10BC8CD0u}},
    {0x128B1020u, {0x109F9950u, 0x10BC8FE0u, 0x10BB3E80u, 0x10BC8CD0u}},
    {0x128B1090u, {0x109F9950u, 0x10BB3EA0u, 0x10BB3EC0u, 0x10BC8CD0u}},
    {0x128B1100u, {0x109F9950u, 0x10BB3EF0u, 0x10BB3F10u, 0x10BC8CD0u}},
    {0x128B1170u, {0x109F9950u, 0x10BB3F30u, 0x10BB3F50u, 0x10BC8CD0u}},
    {0x128B11E0u, {0x109F9950u, 0x10BB3F80u, 0x10BB3FA0u, 0x10BC8CD0u}},
    {0x128B1250u, {0x10BB3FC0u, 0x10BB3FD0u, 0x10BB3FE0u, 0x10BC8CD0u}},
    {0x128B12C0u, {0x10BB4000u, 0x10BB3FF0u, 0x10BB4010u, 0x10BB4020u}},
};
constexpr size_t kSlots[] = {
    kStartFireSlot, kStopFireSlot, kIsFiringSlot, kInitialStateSlot};
constexpr uint8_t kGetterCode[] = {0x8B, 0x40, 0x20, 0x83, 0xC0, 0x10, 0xC3};
constexpr uint8_t kResetHead[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x50, 0x20, 0x83, 0xEC, 0x14};
constexpr uint8_t kActivateHead[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8, 0x8B, 0x08, 0x56, 0x8B, 0x75, 0x08,
    0x89, 0x4E, 0x4C, 0x8B, 0x48, 0x04, 0x89, 0x4E, 0x50, 0x8B, 0x48, 0x0C,
    0x89, 0x4E, 0x58, 0x8B, 0x40, 0x08, 0x89, 0x46, 0x54};
// Exported no-parameter executor: ECX=script-this; ignores FFrame/result,
// dispatches virtual StopFire, clears BOTH four-word delegates, RET 8.
constexpr uint8_t kDeactivateCode[] = {
    0x56, 0x8B, 0xF1, 0x8B, 0x06, 0x8B, 0x50, 0x4C, 0xFF, 0xD2, 0x33, 0xC0,
    0x89, 0x46, 0x4C, 0x89, 0x46, 0x50, 0x89, 0x46, 0x54, 0x89, 0x46, 0x58,
    0x89, 0x46, 0x5C, 0x89, 0x46, 0x60, 0x89, 0x46, 0x64, 0x89, 0x46, 0x68,
    0x5E, 0xC2, 0x08, 0x00};

// Fastcall's unused EDX parameter expresses the verified ECX-this ABI without
// applying GCC's member-only thiscall attribute to a free-function pointer.
using VoidThisFn = void (__attribute__((fastcall)) *)(void*, void* unused_edx);
using BoolThisFn = int32_t (__attribute__((fastcall)) *)(void*, void* unused_edx);
using GetterFn = void* (__attribute__((regparm(1))) *)(void*);
using ResetFn = void (__attribute__((regparm(1))) *)(void*);
using ActivateFn = void (__attribute__((regparm(2), stdcall)) *)(
    const UnrealScriptDelegate*, void* script_class, void* script_this);
using DeactivateFn = UnrealFunctionExec;

struct BoundState {
  const uintptr_t* table = nullptr;
  std::array<uintptr_t, 4> methods = {};
};
struct ShooterBindings {
  HMODULE module = nullptr;
  DWORD thread = 0;
  GetterFn getter = nullptr;
  ResetFn reset = nullptr;
  ActivateFn activate = nullptr;
  DeactivateFn deactivate = nullptr;
  std::array<BoundState, 10> states = {};
};
ShooterBindings g_shooter;
thread_local bool g_shooter_mutating = false;

bool ModuleReadable(HMODULE module, const void* address, size_t size) {
  MEMORY_BASIC_INFORMATION info = {};
  const auto value = reinterpret_cast<uintptr_t>(address);
  return VirtualQuery(address, &info, sizeof(info)) &&
      info.AllocationBase == module && info.State == MEM_COMMIT &&
      !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
      value >= reinterpret_cast<uintptr_t>(info.BaseAddress) &&
      size <= info.RegionSize - (value - reinterpret_cast<uintptr_t>(info.BaseAddress));
}

bool BindShooterImage(HMODULE module, DWORD thread, ShooterBindings* output) {
  if (!module || !thread || thread != GetCurrentThreadId() || !output) return false;
  const auto base = reinterpret_cast<uintptr_t>(module);
  const auto signature = [&](uintptr_t rva, const uint8_t* bytes, size_t size) {
    const auto* p = reinterpret_cast<const void*>(base + rva);
    return ModuleReadable(module, p, size) && std::memcmp(p, bytes, size) == 0;
  };
  if (!signature(kActiveWeaponRva, kGetterCode, sizeof(kGetterCode)) ||
      !signature(kResetRva, kResetHead, sizeof(kResetHead)) ||
      !signature(kActivateRva, kActivateHead, sizeof(kActivateHead)) ||
      !signature(kDeactivateExecRva, kDeactivateCode, sizeof(kDeactivateCode)))
    return false;
  ShooterBindings next;
  next.module = module;
  next.thread = thread;
  for (size_t i = 0; i < next.states.size(); ++i) {
    auto& bound = next.states[i];
    bound.table = reinterpret_cast<const uintptr_t*>(base + kStates[i].table_va - kImageBase);
    if (!ModuleReadable(module, bound.table, 0x70)) return false;
    for (size_t j = 0; j < bound.methods.size(); ++j) {
      bound.methods[j] = base + kStates[i].methods[j] - kImageBase;
      if (bound.table[kSlots[j]] != bound.methods[j]) return false;
    }
  }
  next.getter = reinterpret_cast<GetterFn>(base + kActiveWeaponRva);
  next.reset = reinterpret_cast<ResetFn>(base + kResetRva);
  next.activate = reinterpret_cast<ActivateFn>(base + kActivateRva);
  next.deactivate = reinterpret_cast<DeactivateFn>(base + kDeactivateExecRva);
  *output = next;  // No partial binding is ever published.
  return true;
}

void* ScriptThis(void* object) {
  return static_cast<uint8_t*>(object) + kNativeScriptThisOffset;
}

const BoundState* CurrentState(void* shooter) {
  if (!g_shooter.module || GetCurrentThreadId() != g_shooter.thread ||
      !UnrealObjectIsA(shooter, L"TnAiWeaponShooter")) return nullptr;
  const auto* table = *static_cast<const uintptr_t* const*>(ScriptThis(shooter));
  for (const auto& state : g_shooter.states) {
    if (state.table != table) continue;
    // No GObjects scan, allocation or per-call VirtualQuery. The process image
    // and tables are pinned; re-read the LIVE vtable on every call/state change.
    for (size_t j = 0; j < state.methods.size(); ++j)
      if (table[kSlots[j]] != state.methods[j]) return nullptr;
    return &state;
  }
  return nullptr;  // Unknown subclasses/states cannot fall back to base no-ops.
}

bool LiveActor(void* object, const wchar_t* type) {
  bool deleting = false;
  return UnrealObjectIsA(object, type) &&
      UnrealReadBoolProperty(object, L"bDeleteMe", &deleting) && !deleting;
}

bool ShooterLinks(void* shooter, bool require_ai, void** owner_out = nullptr) {
  void* owner = nullptr;
  void* weapon = nullptr;
  if (!UnrealReadObjectProperty(shooter, L"Owner", &owner) ||
      !LiveActor(owner, L"TnPawn") ||
      !UnrealReadObjectProperty(shooter, L"_Weapon", &weapon) ||
      (weapon && !LiveActor(weapon, L"TnWeapon"))) return false;
  if (require_ai) {
    void* controller = nullptr;
    void* controlled = nullptr;
    void* owned_shooter = nullptr;
    if (!UnrealReadObjectProperty(owner, L"Controller", &controller) ||
        !LiveActor(controller, L"TnAiController") ||
        !UnrealReadObjectProperty(controller, L"Pawn", &controlled) || controlled != owner ||
        !UnrealReadObjectProperty(controller, L"WeaponShooter", &owned_shooter) ||
        owned_shooter != shooter) return false;
  }
  if (owner_out) *owner_out = owner;
  return true;
}

struct MutationScope {
  MutationScope() { g_shooter_mutating = true; }
  ~MutationScope() { g_shooter_mutating = false; }
};

}  // namespace

bool PrepareNativeWeaponShooter(HMODULE module, DWORD game_thread, void* shooter) {
  if (!module || module != GetModuleHandleW(nullptr) ||
      !game_thread || game_thread != GetCurrentThreadId()) return false;
  if (!g_shooter.module) {
    if (!BindShooterImage(module, game_thread, &g_shooter)) {
      Log(LogLevel::kError, "[SquadSwitch] native shooter code/state-table signature mismatch");
      return false;
    }
    Log(LogLevel::kInfo, "[SquadSwitch] native shooter bindings ready (10 state tables)");
  }
  uint32_t owner_offset = 0, weapon_offset = 0;
  return g_shooter.module == module && g_shooter.thread == game_thread &&
      CurrentState(shooter) &&
      UnrealResolveObjectPropertyOffset(shooter, L"Owner", &owner_offset) && owner_offset == 0x2Cu &&
      UnrealResolveObjectPropertyOffset(shooter, L"_Weapon", &weapon_offset) && weapon_offset == 0x30u &&
      ShooterLinks(shooter, true);
}

bool NativeWeaponShooterIsFiring(void* shooter, bool* result) {
  if (!result) return false;
  *result = false;
  const auto* state = CurrentState(shooter);
  if (!state) return false;
  const auto value = reinterpret_cast<BoolThisFn>(state->methods[2])(ScriptThis(shooter), nullptr);
  if (value != 0 && value != 1) return false;
  *result = value != 0;
  return true;
}

bool NativeWeaponShooterActiveWeapon(void* shooter, void** result) {
  if (!result) return false;
  *result = nullptr;
  if (!CurrentState(shooter)) return false;
  const auto value = reinterpret_cast<uintptr_t>(g_shooter.getter(ScriptThis(shooter)));
  if (value == kNativeScriptThisOffset) return true;
  if (value < kNativeScriptThisOffset) return false;
  void* weapon = reinterpret_cast<void*>(value - kNativeScriptThisOffset);
  if (!LiveActor(weapon, L"TnWeapon")) return false;
  *result = weapon;
  return true;
}

bool NativeWeaponShooterStartFire(void* shooter) {
  if (g_shooter_mutating) return false;
  const auto* state = CurrentState(shooter);
  void* weapon = nullptr;
  // Firing.BeginState dereferences _Weapon; an idle, unactivated shooter is
  // valid to inspect/reset but never valid to enter Firing with a null weapon.
  if (!state || !ShooterLinks(shooter, true) ||
      !NativeWeaponShooterActiveWeapon(shooter, &weapon) || !weapon) return false;
  MutationScope scope;
  reinterpret_cast<VoidThisFn>(state->methods[0])(ScriptThis(shooter), nullptr);
  return true;
}

bool NativeWeaponShooterHandoffReady(void* shooter) {
  void* weapon = nullptr;
  if (g_shooter_mutating || CurrentState(shooter) != &g_shooter.states[2] ||
      !ShooterLinks(shooter, false) || !NativeWeaponShooterActiveWeapon(shooter, &weapon) || weapon) return false;
  // Full shipping Deactivate clears these 8 words (two compiled delegates).
  const auto* delegates = static_cast<uint8_t*>(shooter) + 0x5C;
  MEMORY_BASIC_INFORMATION info{};
  if (!VirtualQuery(delegates, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      32 > info.RegionSize - (reinterpret_cast<uintptr_t>(delegates) - reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const std::array<uint8_t,32> empty{};
  return !std::memcmp(delegates, empty.data(), empty.size());
}

bool NativeWeaponShooterStopFire(void* shooter) {
  if (g_shooter_mutating) return false;
  const auto* state = CurrentState(shooter);
  if (!state || !ShooterLinks(shooter, false)) return false;
  MutationScope scope;
  reinterpret_cast<VoidThisFn>(state->methods[1])(ScriptThis(shooter), nullptr);
  return true;
}

bool NativeWeaponShooterDeactivate(void* shooter) {
  if (g_shooter_mutating || !CurrentState(shooter) || !ShooterLinks(shooter, false)) return false;
  MutationScope scope;
  g_shooter.deactivate(ScriptThis(shooter), nullptr, nullptr, nullptr);
  return true;
}

bool NativeWeaponShooterReset(void* shooter) {
  if (g_shooter_mutating || !CurrentState(shooter) || !ShooterLinks(shooter, false)) return false;
  MutationScope scope;
  g_shooter.reset(ScriptThis(shooter));
  return true;
}

bool NativeWeaponShooterActivateEquipped(void* shooter, void* weapon) {
  if (g_shooter_mutating || !CurrentState(shooter) || !LiveActor(weapon, L"TnWeapon")) return false;
  void* owner = nullptr;
  void* equipped = nullptr;
  void* inventory = nullptr;
  if (!ShooterLinks(shooter, true, &owner) ||
      !UnrealReadObjectProperty(owner, L"Weapon", &equipped) || equipped != weapon ||
      !UnrealReadObjectProperty(owner, L"InvManager", &inventory) ||
      !UnrealObjectIsA(inventory, L"TnInventoryManager")) return false;
  void* weapon_class = UnrealObjectClass(weapon);
  if (!weapon_class || !UnrealObjectValid(weapon_class)) return false;
  // Default callback is the native 16-byte null delegate, not an 8-byte pair.
  const UnrealScriptDelegate on_failed = {};
  static_assert(sizeof(on_failed) == 16, "WFC compiled delegate ABI");
  MutationScope scope;
  g_shooter.activate(&on_failed, ScriptThis(weapon_class), ScriptThis(shooter));
  return true;
}

}  // namespace wfcds
