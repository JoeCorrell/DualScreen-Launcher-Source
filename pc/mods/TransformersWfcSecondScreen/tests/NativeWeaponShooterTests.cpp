// Executes the production dispatch against x86 register/stack fixtures. No
// game process, actor spawning, APK or device is involved.
#include "NativeWeaponShooter.cpp"
#include "NativeAiPlanning.cpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <fstream>
#include <vector>

using namespace wfcds;
namespace {
struct Object {
  alignas(4) uint8_t bytes[0x500] = {};
  const wchar_t* type = nullptr;
  bool valid = true;
  bool deleting = false;
};
Object shooter, pawn, controller, weapon, second_weapon, inventory, weapon_class;
Object runner, world, game, scheduler, queue_definition;
Object action_set, goal_set, action, goal, blueprint, old_plan, pending_plan;
Object* objects[] = {&shooter, &pawn, &controller, &weapon, &second_weapon,
                    &inventory, &weapon_class, &runner, &world, &action_set,
                    &goal_set, &action, &goal, &blueprint, &old_plan, &pending_plan,
                    &game, &scheduler, &queue_definition};
void* owner = pawn.bytes;
void* pawn_controller = controller.bytes;
void* controller_pawn = pawn.bytes;
void* controller_shooter = shooter.bytes;
void* equipped = weapon.bytes;
void* manager = inventory.bytes;
uint32_t owner_offset = 0x2C;
uint32_t weapon_offset = 0x30;
uintptr_t observed_this = 0, observed_class = 0;
UnrealScriptDelegate observed_callback;
unsigned starts = 0, stops = 0, resets = 0;
bool recursive_checks = false;
uint8_t reflected_net_mode = 0;
void* runner_owner = controller.bytes;
uint32_t plan_layout_shift = 0;
unsigned action_binds = 0, action_unbinds = 0, goal_binds = 0, goal_unbinds = 0;
uintptr_t observed_bind_owner = 0, observed_bind_set = 0;
bool array_header_mismatch = false;
void* plan_items[1] = {};
bool lifecycle_reentry_checks = false;
bool callback_requeues = false;
uintptr_t observed_enqueue_argument = 0;
alignas(4) uint8_t engine_fixture[0x50] = {}, context_fixture[0x30] = {};
void* current_world_fixture = world.bytes;
unsigned game_cast_calls = 0, queue_allocations = 0, queue_removals = 0;
bool replan_reentry_checks = false, game_cast_rejects = false, deactivate_on_cast = false;
bool queue_layout_mismatch = false;

Object* Find(void* p) {
  for (auto* object : objects) if (object->bytes == p) return object;
  return nullptr;
}
void Put(void* p, size_t offset, uintptr_t value) {
  std::memcpy(static_cast<uint8_t*>(p) + offset, &value, 4);
}
uintptr_t Read(void* p, size_t offset) {
  uintptr_t value = 0;
  std::memcpy(&value, static_cast<uint8_t*>(p) + offset, 4);
  return value;
}
struct Emit {
  uint8_t* p;
  void Byte(uint8_t b) { *p++ = b; }
  void Word(uintptr_t v) { std::memcpy(p, &v, 4); p += 4; }
  void StoreEax(void* address) { Byte(0xA3); Word(reinterpret_cast<uintptr_t>(address)); }
  void StoreEcx(void* address) { Byte(0x89); Byte(0x0D); Word(reinterpret_cast<uintptr_t>(address)); }
  void Increment(void* address) { Byte(0xFF); Byte(0x05); Word(reinterpret_cast<uintptr_t>(address)); }
};
void ResetCallback(void* script_this) {
  assert(script_this == shooter.bytes + 16);
  ++resets;
  if (recursive_checks) {
    assert(!NativeWeaponShooterReset(shooter.bytes));
    assert(!NativeWeaponShooterDeactivate(shooter.bytes));
    assert(!NativeWeaponShooterStartFire(shooter.bytes));
    assert(!NativeWeaponShooterStopFire(shooter.bytes));
    assert(!NativeWeaponShooterActivateEquipped(shooter.bytes, weapon.bytes));
    bool firing = true;
    assert(NativeWeaponShooterIsFiring(shooter.bytes, &firing));
  }
  Put(shooter.bytes, 0x30, 0);
}
void PlanningCallback() {
  if (lifecycle_reentry_checks) {
    assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiSetActionSet(controller.bytes, action_set.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiSetGoalSet(controller.bytes, goal_set.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiResumePlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kRejected);
  }
  if (callback_requeues) Put(runner.bytes, 0x44, reinterpret_cast<uintptr_t>(pending_plan.bytes));
}
void* ResolveGameClass(void*, void*) { return weapon_class.bytes + 16; }
uint32_t __attribute__((fastcall)) CastGame(void* object, void*, void* type) {
  assert(object == game.bytes && type == weapon_class.bytes);
  ++game_cast_calls;
  if (replan_reentry_checks) {
    assert(NativeAiRequestReplan(controller.bytes, 0) == NativeAiPlanningResult::kRejected);
    assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  }
  if (deactivate_on_cast) Put(scheduler.bytes, 0x50, 0);
  return game_cast_rejects ? 0 : 1;
}
void ResizeQueue(UnrealArrayView* queue, uint32_t old_count) {
  assert(queue == reinterpret_cast<UnrealArrayView*>(scheduler.bytes + 0x28) ||
         queue == reinterpret_cast<UnrealArrayView*>(scheduler.bytes + 0x34));
  assert(queue->count == static_cast<int32_t>(old_count) + 1);
  assert(queue->capacity >= queue->count);
  queue->data = std::realloc(queue->data, queue->capacity * 4);
  assert(queue->data);
  ++queue_allocations;
}
void RemoveQueue(uint32_t count, uint32_t index, UnrealArrayView* queue) {
  assert(queue == reinterpret_cast<UnrealArrayView*>(scheduler.bytes + 0x28));
  assert(count == 1 && index < static_cast<uint32_t>(queue->count));
  auto* values = static_cast<void**>(queue->data);
  std::memmove(values + index, values + index + count, (queue->count - index - count) * 4);
  queue->count -= count;
  ++queue_removals;
}
DWORD WINAPI OtherThread(void*) {
  bool firing = false;
  void* active = nullptr;
  assert(!NativeWeaponShooterIsFiring(shooter.bytes, &firing));
  assert(!NativeWeaponShooterActiveWeapon(shooter.bytes, &active));
  assert(!NativeWeaponShooterStartFire(shooter.bytes));
  assert(!NativeWeaponShooterStopFire(shooter.bytes));
  assert(!NativeWeaponShooterReset(shooter.bytes));
  assert(!NativeWeaponShooterActivateEquipped(shooter.bytes, weapon.bytes));
  assert(!PrepareNativeWeaponShooter(GetModuleHandleW(nullptr), g_shooter.thread, shooter.bytes));
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiResumePlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiSetActionSet(controller.bytes, action_set.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiSetGoalSet(controller.bytes, goal_set.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kRejected);
  return 0;
}

void VerifyRetainedExecutable(const char* path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  assert(input && input.tellg() == 37667328);
  std::vector<uint8_t> bytes(static_cast<size_t>(input.tellg()));
  input.seekg(0);
  assert(input.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
  const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
  assert(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 && dos->e_lfanew < 4096);
  const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
  assert(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == kImageBase);
  assert(nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 && nt->FileHeader.NumberOfSections < 32);
  const auto file_at = [&](uintptr_t rva, size_t size) -> const uint8_t* {
    const auto* section = IMAGE_FIRST_SECTION(nt);
    for (size_t n = 0; n < nt->FileHeader.NumberOfSections; ++n, ++section) {
      if (rva < section->VirtualAddress) continue;
      const auto delta = rva - section->VirtualAddress;
      if (delta > section->SizeOfRawData || size > section->SizeOfRawData - delta) continue;
      const auto offset = section->PointerToRawData + delta;
      assert(offset <= bytes.size() && size <= bytes.size() - offset);
      return bytes.data() + offset;
    }
    assert(false && "native signature is outside file-backed PE sections");
    return nullptr;
  };
  const auto compare = [&](uintptr_t rva, const uint8_t* code, size_t size) {
    assert(std::memcmp(file_at(rva, size), code, size) == 0);
  };
  compare(kActiveWeaponRva, kGetterCode, sizeof(kGetterCode));
  compare(kActivateRva, kActivateHead, sizeof(kActivateHead));
  compare(kResetRva, kResetHead, sizeof(kResetHead));
  compare(kDeactivateExecRva, kDeactivateCode, sizeof(kDeactivateCode));
  for (const auto& state : kStates) {
    for (size_t j = 0; j < state.methods.size(); ++j) {
      uintptr_t method = 0;
      std::memcpy(&method, file_at(state.table_va - kImageBase + 4 * kSlots[j], 4), 4);
      assert(method == state.methods[j]);
    }
  }
  compare(kSuspendRunnerRva, kSuspendRunnerCode, sizeof(kSuspendRunnerCode));
  auto resume_code = std::array<uint8_t, sizeof(kSuspendRunnerCode)>{};
  std::memcpy(resume_code.data(), kSuspendRunnerCode, resume_code.size());
  resume_code[38] = 0x58;
  compare(kResumeRunnerRva, resume_code.data(), resume_code.size());
  for (const auto& spec : kPlanLifecycleCode) {
    compare(spec.rva, spec.bytes, spec.size);
    for (size_t i = 0; i < spec.relocation_count; ++i) {
      uint32_t operand = 0;
      std::memcpy(&operand, spec.bytes + spec.relocations[i], 4);
      assert(operand == 0x12B1047Cu || operand == 0x12B10474u || operand == 0x12B324C0u ||
             operand == 0x12B10A9Cu || operand == 0x12667230u || operand == 0x12B6FC1Cu);
    }
  }
  for (const auto& state : kRunnerStates) {
    uintptr_t method = 0;
    std::memcpy(&method, file_at(state.table - kPlanningImageBase + 0x54, 4), 4);
    assert(method == state.suspend);
    std::memcpy(&method, file_at(state.table - kPlanningImageBase + 0x58, 4), 4);
    assert(method == state.resume);
    std::memcpy(&method, file_at(state.table - kPlanningImageBase + 0x60, 4), 4);
    assert(method == state.enqueue);
  }
  std::puts("Native shooter signatures and 40 state slots match retained TWFC.exe");
  std::puts("Native AI planning/lifecycle signatures and 15 state slots match retained TWFC.exe");
}
}  // namespace

namespace wfcds {
bool UnrealObjectValid(void* p) { auto* o = Find(p); return o && o->valid; }
bool UnrealObjectIsA(void* p, const wchar_t* type) {
  auto* o = Find(p);
  return o && o->valid && o->type && (!std::wcscmp(o->type, type) ||
      (!std::wcscmp(o->type, L"TnAiPawn") && !std::wcscmp(type, L"TnPawn")));
}
void* UnrealObjectClass(void* p) { return UnrealObjectValid(p) ? weapon_class.bytes : nullptr; }
bool UnrealResolveObjectPropertyOffset(void*, const wchar_t* name, uint32_t* out) {
  if (!std::wcscmp(name, L"Owner")) *out = owner_offset;
  else if (!std::wcscmp(name, L"_Weapon")) *out = weapon_offset;
  else if (!std::wcscmp(name, L"PlanRunner")) *out = 0x304;
  else if (!std::wcscmp(name, L"WorldInfo")) *out = 0x98;
  else if (!std::wcscmp(name, L"_Owner")) *out = 0x30 + plan_layout_shift;
  else if (!std::wcscmp(name, L"_GoalSet")) *out = 0x34;
  else if (!std::wcscmp(name, L"_ActionSet")) *out = 0x38;
  else if (!std::wcscmp(name, L"_CurrentSolution")) *out = 0x40;
  else if (!std::wcscmp(name, L"_PendingSolution")) *out = 0x44;
  else if (!std::wcscmp(name, L"_PendingActionSet")) *out = 0x5C;
  else if (!std::wcscmp(name, L"_PendingGoalSet")) *out = 0x60;
  else if (!std::wcscmp(name, L"Game")) *out = 0x3C4;
  else if (!std::wcscmp(name, L"_AiPlanningScheduler")) *out = 0x380;
  else return false;
  return true;
}
bool UnrealReadBoolProperty(void* p, const wchar_t* name, bool* out) {
  auto* o = Find(p);
  if (!o || !o->valid) return false;
  if (p == runner.bytes && !std::wcscmp(name, L"_Suspended")) {
    *out = (Read(p, 0x58) & 4) != 0;
    return true;
  }
  if (p == runner.bytes && !std::wcscmp(name, L"_SuspendPending")) {
    *out = (Read(p, 0x58) & 2) != 0;
    return true;
  }
  if (p == runner.bytes && !std::wcscmp(name, L"_EvaluateCurrentGoalOnReplan")) {
    *out = (Read(p, 0x58) & 1) != 0;
    return true;
  }
  if (p == scheduler.bytes && !std::wcscmp(name, L"_Activated")) {
    *out = (Read(p, 0x50) & 1) != 0;
    return true;
  }
  if (std::wcscmp(name, L"bDeleteMe")) return false;
  *out = o->deleting;
  return true;
}
bool UnrealReadByteProperty(void* p, const wchar_t* name, uint8_t* out) {
  if (p != world.bytes || std::wcscmp(name, L"NetMode")) return false;
  *out = reflected_net_mode;
  return true;
}
bool UnrealReadObjectProperty(void* p, const wchar_t* name, void** out) {
  if (!UnrealObjectValid(p)) return false;
  if (p == shooter.bytes && !std::wcscmp(name, L"Owner")) *out = owner;
  else if (p == shooter.bytes && !std::wcscmp(name, L"_Weapon")) *out = reinterpret_cast<void*>(Read(p, 0x30));
  else if (p == pawn.bytes && !std::wcscmp(name, L"Controller")) *out = pawn_controller;
  else if (p == pawn.bytes && !std::wcscmp(name, L"Weapon")) *out = equipped;
  else if (p == pawn.bytes && !std::wcscmp(name, L"InvManager")) *out = manager;
  else if (p == controller.bytes && !std::wcscmp(name, L"Pawn")) *out = controller_pawn;
  else if (p == controller.bytes && !std::wcscmp(name, L"WeaponShooter")) *out = controller_shooter;
  else if (p == controller.bytes && !std::wcscmp(name, L"WorldInfo")) *out = reinterpret_cast<void*>(Read(p, 0x98));
  else if (p == controller.bytes && !std::wcscmp(name, L"PlanRunner")) *out = reinterpret_cast<void*>(Read(p, 0x304));
  else if (p == runner.bytes && !std::wcscmp(name, L"_Owner")) *out = runner_owner;
  else if (p == runner.bytes) {
    uint32_t offset = 0;
    if (!UnrealResolveObjectPropertyOffset(p, name, &offset)) return false;
    *out = reinterpret_cast<void*>(Read(p, offset));
  }
  else if (p == action.bytes && !std::wcscmp(name, L"_BaseBlueprint")) *out = blueprint.bytes;
  else if (p == world.bytes && !std::wcscmp(name, L"Game")) *out = reinterpret_cast<void*>(Read(p, 0x3C4));
  else if (p == game.bytes && !std::wcscmp(name, L"_AiPlanningScheduler")) *out = reinterpret_cast<void*>(Read(p, 0x380));
  else return false;
  return true;
}
bool UnrealReadArrayProperty(void* p, const wchar_t* name, UnrealArrayView* out) {
  size_t offset = 0;
  if (p == action_set.bytes && !std::wcscmp(name, L"Actions")) offset = 0x3C;
  else if (p == goal_set.bytes && !std::wcscmp(name, L"Goals")) offset = 0x48;
  else return false;
  out->data = reinterpret_cast<void*>(Read(p, offset));
  out->count = static_cast<int32_t>(Read(p, offset + 4)) + (array_header_mismatch ? 1 : 0);
  out->capacity = out->count;
  return true;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& items, int32_t index, void** out) {
  if (!items.data || index < 0 || index >= items.count) return false;
  *out = static_cast<void**>(items.data)[index];
  return !*out || UnrealObjectValid(*out);
}
bool UnrealReadFixedStructArrayProperty(void* object, const wchar_t* name, UnrealStructArrayView* out) {
  if (!UnrealObjectValid(object) || object != scheduler.bytes || std::wcscmp(name, L"_PendingRequests"))
    return false;
  *out = {scheduler.bytes + 0x28, queue_layout_mismatch ? 3 : 2, 2, 12, queue_definition.bytes};
  return true;
}
bool UnrealReadStructTypedArray(const UnrealStructValueView& value, const wchar_t* name,
    const wchar_t* inner, uint32_t element_size, UnrealArrayView* out, uint32_t max_capacity) {
  if (value.struct_definition != queue_definition.bytes || value.size != 12 ||
      std::wcscmp(name, L"Requests") || std::wcscmp(inner, L"ObjectProperty") || element_size != 4)
    return false;
  std::memcpy(out, value.data, 12);
  return out->count >= 0 && out->capacity >= out->count &&
      static_cast<uint32_t>(out->capacity) <= max_capacity &&
      (!out->count || PlanningDataReadable(out->data, out->count * 4));
}
}  // namespace wfcds

int main(int argc, char** argv) {
  if (argc == 2) VerifyRetainedExecutable(argv[1]);
  shooter.type = L"TnAiWeaponShooter";
  pawn.type = L"TnPawn";
  controller.type = L"TnAiController";
  weapon.type = second_weapon.type = L"TnWeapon";
  inventory.type = L"TnInventoryManager";
  weapon_class.type = L"Class";
  runner.type = L"HmGoapPlanRunner";
  world.type = L"WorldInfo";
  game.type = L"TnGame";
  scheduler.type = L"TnAiPlanningScheduler";
  queue_definition.type = L"ScriptStruct";
  action_set.type = L"HmGoapActionSet";
  goal_set.type = L"HmGoapGoalSet";
  action.type = L"HmGoapAction";
  goal.type = L"HmGoapGoal";
  blueprint.type = L"HmGoapActionBlueprint";
  old_plan.type = pending_plan.type = L"HmGoapPlan";
  bool firing = true;
  assert(!NativeWeaponShooterIsFiring(shooter.bytes, &firing) && !firing);
  auto* image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x02300000,
      MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
  assert(image);
  auto module = reinterpret_cast<HMODULE>(image);
  auto rva = [&](uintptr_t va) { return image + va - kImageBase; };
  std::memcpy(image + kActiveWeaponRva, kGetterCode, sizeof(kGetterCode));
  std::memcpy(image + kActivateRva, kActivateHead, sizeof(kActivateHead));
  std::memcpy(image + kResetRva, kResetHead, sizeof(kResetHead));
  std::memcpy(image + kDeactivateExecRva, kDeactivateCode, sizeof(kDeactivateCode));
  for (const auto& state : kStates)
    for (size_t j = 0; j < state.methods.size(); ++j)
      Put(rva(state.table_va), kSlots[j] * 4, reinterpret_cast<uintptr_t>(rva(state.methods[j])));
  ShooterBindings bound;
  assert(!BindShooterImage(nullptr, GetCurrentThreadId(), &bound));
  assert(BindShooterImage(module, GetCurrentThreadId(), &bound));
  image[kActivateRva + 24] ^= 1;
  ShooterBindings rejected;
  assert(!BindShooterImage(module, GetCurrentThreadId(), &rejected));
  assert(!rejected.module);
  image[kActivateRva + 24] ^= 1;
  auto* stopped_table = rva(kStates[2].table_va);
  Put(stopped_table, kIsFiringSlot * 4, 0);
  assert(!BindShooterImage(module, GetCurrentThreadId(), &rejected));
  Put(stopped_table, kIsFiringSlot * 4, reinterpret_cast<uintptr_t>(rva(kStates[2].methods[2])));
  assert(BindShooterImage(module, GetCurrentThreadId(), &g_shooter));
  // Test the public object/layout preflight with the already-verified fixture
  // bindings; only the module identity is substituted for this test process.
  g_shooter.module = GetModuleHandleW(nullptr);
  Put(shooter.bytes, 16, reinterpret_cast<uintptr_t>(stopped_table));
  assert(PrepareNativeWeaponShooter(g_shooter.module, g_shooter.thread, shooter.bytes));
  ++owner_offset;
  assert(!PrepareNativeWeaponShooter(g_shooter.module, g_shooter.thread, shooter.bytes));
  --owner_offset;
  assert(!PrepareNativeWeaponShooter(module, g_shooter.thread, shooter.bytes));

  // Keep and execute the EXACT controller Suspend/Resume bodies. Only the
  // runner's state functions are fixtures, so register adjustment, tail-jump,
  // authority guard and state dispatch all execute the shipping instructions.
  std::memcpy(image + kSuspendRunnerRva, kSuspendRunnerCode, sizeof(kSuspendRunnerCode));
  std::memcpy(image + kResumeRunnerRva, kSuspendRunnerCode, sizeof(kSuspendRunnerCode));
  image[kResumeRunnerRva + 38] = 0x58;
  // Keep the complete lifecycle bodies, including ESI setters and EDX pump.
  // Model normal PE relocation of their absolute GC/state-name operands.
  for (const auto& spec : kPlanLifecycleCode) {
    std::memcpy(image + spec.rva, spec.bytes, spec.size);
    for (size_t i = 0; i < spec.relocation_count; ++i) {
      const auto offset = spec.relocations[i];
      Put(image + spec.rva, offset,
          Read(image + spec.rva, offset) + reinterpret_cast<uintptr_t>(image) - kPlanningImageBase);
    }
  }
  for (const auto& state : kRunnerStates) {
    Put(rva(state.table), 0x54, reinterpret_cast<uintptr_t>(rva(state.suspend)));
    Put(rva(state.table), 0x58, reinterpret_cast<uintptr_t>(rva(state.resume)));
    Put(rva(state.table), 0x60, reinterpret_cast<uintptr_t>(rva(state.enqueue)));
  }
  assert(BindPlanningImage(module, GetCurrentThreadId(), &g_planning));
  PlanningBindings invalid_planning;
  image[kResumeRunnerRva + 38] = 0x54;
  assert(!BindPlanningImage(module, GetCurrentThreadId(), &invalid_planning));
  assert(!invalid_planning.module);
  image[kResumeRunnerRva + 38] = 0x58;
  for (const auto& spec : kPlanLifecycleCode) {
    image[spec.rva] ^= 1;
    assert(!BindPlanningImage(module, GetCurrentThreadId(), &invalid_planning));
    image[spec.rva] ^= 1;
    for (size_t i = 0; i < spec.relocation_count; ++i) {
      image[spec.rva + spec.relocations[i]] ^= 1;
      assert(!BindPlanningImage(module, GetCurrentThreadId(), &invalid_planning));
      image[spec.rva + spec.relocations[i]] ^= 1;
    }
  }
  g_planning.module = GetModuleHandleW(nullptr);
  Put(controller.bytes, 0x98, reinterpret_cast<uintptr_t>(world.bytes));
  Put(controller.bytes, 0x304, reinterpret_cast<uintptr_t>(runner.bytes));
  Put(runner.bytes, 16, reinterpret_cast<uintptr_t>(rva(kRunnerStates[1].table)));
  assert(PrepareNativeAiPlanning(g_planning.module, g_planning.thread, controller.bytes));
  Put(runner.bytes, 0x30, reinterpret_cast<uintptr_t>(controller.bytes));
  Put(runner.bytes, 0x34, reinterpret_cast<uintptr_t>(goal_set.bytes));
  Put(runner.bytes, 0x38, reinterpret_cast<uintptr_t>(action_set.bytes));
  assert(PrepareNativeAiPlanLifecycle(g_planning.module, g_planning.thread, controller.bytes));
  Put(world.bytes, 0x3C4, reinterpret_cast<uintptr_t>(game.bytes));
  Put(game.bytes, 0x380, reinterpret_cast<uintptr_t>(scheduler.bytes));
  Put(scheduler.bytes, 0x50, 1);
  Put(engine_fixture, 0x40, reinterpret_cast<uintptr_t>(context_fixture));
  Put(context_fixture, 0x28, reinterpret_cast<uintptr_t>(&current_world_fixture));
  Put(rva(0x12B10A9C), 0, reinterpret_cast<uintptr_t>(engine_fixture));
  Emit suspend{rva(0x113790A0)};
  suspend.Byte(0x83); suspend.Byte(0x49); suspend.Byte(0x48); suspend.Byte(0x02);
  suspend.Byte(0xC7); suspend.Byte(0x01); suspend.Word(reinterpret_cast<uintptr_t>(rva(kRunnerStates[3].table)));
  suspend.Byte(0xC3);
  // Base Resume's exact AND/RET, and Suspended.Suspend's exact no-op RET.
  const uint8_t base_resume[] = {0x83, 0x61, 0x48, 0xFD, 0xC3};
  std::memcpy(rva(0x10FDD5B0), base_resume, sizeof(base_resume));
  *rva(0x10FDD600) = 0xC3;
  Emit resume{rva(0x11378E20)};
  resume.Byte(0x83); resume.Byte(0x61); resume.Byte(0x48); resume.Byte(0xF9);
  resume.Byte(0xC7); resume.Byte(0x01); resume.Word(reinterpret_cast<uintptr_t>(rva(kRunnerStates[1].table)));
  resume.Byte(0xC3);

  // Nested native lifecycle dependencies are fixtures; the top-level setters,
  // ClearSolution and null PumpQueue paths remain the exact shipping bytes.
  *rva(0x116FC820) = 0xC3;  // GC write barrier, EDX preserved as in the binary.
  auto callback = [&](Emit& emit) {
    emit.Byte(0xB8); emit.Word(reinterpret_cast<uintptr_t>(&PlanningCallback));
    emit.Byte(0xFF); emit.Byte(0xD0);
  };
  auto return_stack = [](Emit& emit, uint8_t size) {
    emit.Byte(0xC2); emit.Byte(size); emit.Byte(0);
  };
  // Execute the real controller, static entry, Submit, Find, Add and Remove.
  // Only class resolution/cast and the engine heap/memmove helpers are fixtures.
  auto jump_to = [&](uintptr_t address, uintptr_t target) {
    Emit emit{rva(address)};
    emit.Byte(0xB8); emit.Word(target); emit.Byte(0xFF); emit.Byte(0xE0);
  };
  jump_to(0x110031A0, reinterpret_cast<uintptr_t>(&ResolveGameClass));
  jump_to(0x10901430, reinterpret_cast<uintptr_t>(&CastGame));
  Emit grow_queue{rva(0x10BF5390)};
  grow_queue.Byte(0xFF); grow_queue.Byte(0x74); grow_queue.Byte(0x24); grow_queue.Byte(4);
  grow_queue.Byte(0x56);  // ESI array header; old count already on stack.
  grow_queue.Byte(0xB8); grow_queue.Word(reinterpret_cast<uintptr_t>(&ResizeQueue));
  grow_queue.Byte(0xFF); grow_queue.Byte(0xD0);
  grow_queue.Byte(0x83); grow_queue.Byte(0xC4); grow_queue.Byte(8);
  return_stack(grow_queue, 4);
  Emit remove_queue{rva(0x10BF46B0)};
  remove_queue.Byte(0xFF); remove_queue.Byte(0x74); remove_queue.Byte(0x24); remove_queue.Byte(4);
  remove_queue.Byte(0x52); remove_queue.Byte(0x50);  // EDX index, EAX count.
  remove_queue.Byte(0xB8); remove_queue.Word(reinterpret_cast<uintptr_t>(&RemoveQueue));
  remove_queue.Byte(0xFF); remove_queue.Byte(0xD0);
  remove_queue.Byte(0x83); remove_queue.Byte(0xC4); remove_queue.Byte(12);
  return_stack(remove_queue, 4);
  Emit bind_action{rva(0x11382B60)};
  bind_action.Increment(&action_binds);
  bind_action.Byte(0x8B); bind_action.Byte(0x44); bind_action.Byte(0x24); bind_action.Byte(4);
  bind_action.StoreEax(&observed_bind_set);
  bind_action.Byte(0x8B); bind_action.Byte(0x44); bind_action.Byte(0x24); bind_action.Byte(8);
  bind_action.StoreEax(&observed_bind_owner);
  callback(bind_action);
  return_stack(bind_action, 8);
  Emit unbind_action{rva(0x1137D6C0)};
  unbind_action.Increment(&action_unbinds);
  callback(unbind_action);
  return_stack(unbind_action, 4);
  Emit bind_goal{rva(0x113765B0)};
  bind_goal.Increment(&goal_binds);
  bind_goal.Byte(0x89); bind_goal.Byte(0xF8); // EAX = EDI (compiled goal set).
  bind_goal.StoreEax(&observed_bind_set);
  bind_goal.Byte(0x8B); bind_goal.Byte(0x44); bind_goal.Byte(0x24); bind_goal.Byte(4);
  bind_goal.StoreEax(&observed_bind_owner);
  callback(bind_goal);
  return_stack(bind_goal, 4);
  Emit unbind_goal{rva(0x11376580)};
  unbind_goal.Increment(&goal_unbinds);
  callback(unbind_goal);
  unbind_goal.Byte(0xC3);
  for (auto address : {0x1139B3D0u, 0x1139BA00u}) {
    Emit enqueue{rva(address)};
    enqueue.Byte(0x8B); enqueue.Byte(0x44); enqueue.Byte(0x24); enqueue.Byte(4);
    enqueue.StoreEax(&observed_enqueue_argument);
    enqueue.Byte(0x83); enqueue.Byte(0xC0); enqueue.Byte(0xF0);
    enqueue.Byte(0x89); enqueue.Byte(0x41); enqueue.Byte(0x34);
    callback(enqueue);
    return_stack(enqueue, 4);
  }
  for (auto address : {0x10FDD5D0u, 0x10FDD5F0u}) {
    Emit ignored{rva(address)};
    return_stack(ignored, 4);
  }
  // Verify the host ABI separately: even a private method that overwrites
  // its ESI receiver cannot corrupt the surrounding C++ transaction's ESI.
  Emit esi_target{image + 0x20000};
  esi_target.Byte(0xBE); esi_target.Word(0xBAD00000u);
  return_stack(esi_target, 4);
  Emit esi_probe{image + 0x20100};
  esi_probe.Byte(0x56); // push esi
  esi_probe.Byte(0xBE); esi_probe.Word(0x12345678u);
  esi_probe.Byte(0x6A); esi_probe.Byte(16);
  esi_probe.Byte(0x68); esi_probe.Word(reinterpret_cast<uintptr_t>(runner.bytes + 16));
  esi_probe.Byte(0x68); esi_probe.Word(reinterpret_cast<uintptr_t>(image + 0x20000));
  esi_probe.Byte(0xB8); esi_probe.Word(reinterpret_cast<uintptr_t>(&WfcCallEsiObject));
  esi_probe.Byte(0xFF); esi_probe.Byte(0xD0);
  esi_probe.Byte(0x83); esi_probe.Byte(0xC4); esi_probe.Byte(12);
  esi_probe.Byte(0x89); esi_probe.Byte(0xF0); // mov eax, esi
  esi_probe.Byte(0x5E); esi_probe.Byte(0xC3);

  // Native virtuals use ECX. These fixtures change the vtable during the call,
  // requiring the very next call to re-dispatch through the new state.
  for (size_t i = 0; i < g_shooter.states.size(); ++i) {
    const auto& state = kStates[i];
    auto fixture = [&](size_t j) {
      auto* body = image + 0x10000 + i * 256 + j * 64;
      Emit jump{rva(state.methods[j])};
      jump.Byte(0xE9);
      jump.Word(static_cast<uint32_t>(body - (rva(state.methods[j]) + 5)));
      return Emit{body};
    };
    Emit start = fixture(0);
    start.StoreEcx(&observed_this);
    start.Increment(&starts);
    start.Byte(0xC7); start.Byte(0x01); start.Word(reinterpret_cast<uintptr_t>(rva(kStates[3].table_va)));
    start.Byte(0xC3);
    Emit stop = fixture(1);
    stop.StoreEcx(&observed_this);
    stop.Increment(&stops);
    stop.Byte(0xC7); stop.Byte(0x01); stop.Word(reinterpret_cast<uintptr_t>(stopped_table));
    stop.Byte(0xC3);
    Emit query = fixture(2);
    query.StoreEcx(&observed_this);
    query.Byte(0xB8); query.Word((i >= 3 && i <= 7) || i == 9 ? 1 : 0);
    query.Byte(0xC3);
  }
  // Activate: EAX=delegate*, EDX=compiled class, [ESP+4]=script-this, RET 4.
  Emit activate{image + kActivateRva};
  activate.Byte(0x89); activate.Byte(0x15); activate.Word(reinterpret_cast<uintptr_t>(&observed_class));
  for (size_t i = 0; i < 16; i += 4) {
    activate.Byte(0x8B); activate.Byte(0x48); activate.Byte(static_cast<uint8_t>(i));
    activate.StoreEcx(reinterpret_cast<uint8_t*>(&observed_callback) + i);
  }
  activate.Byte(0x8B); activate.Byte(0x44); activate.Byte(0x24); activate.Byte(0x04);
  activate.StoreEax(&observed_this);
  activate.Byte(0xC2); activate.Byte(0x04); activate.Byte(0x00);
  // Reset: EAX=script-this, ordinary RET; re-entry is checked in the callback.
  Emit reset{image + kResetRva};
  reset.StoreEax(&observed_this);
  reset.Byte(0x50);
  reset.Byte(0xBA); reset.Word(reinterpret_cast<uintptr_t>(&ResetCallback));
  reset.Byte(0xFF); reset.Byte(0xD2);
  reset.Byte(0x83); reset.Byte(0xC4); reset.Byte(0x04); reset.Byte(0xC3);
  DWORD old = 0;
  assert(VirtualProtect(image, 0x01F00000, PAGE_EXECUTE_READ, &old));
  // All tables are beyond this RX region. Tests intentionally change a slot
  // later to verify rejection, without writable executable code.
  FlushInstructionCache(GetCurrentProcess(), image, 0x01F00000);
  for (unsigned n = 0; n < 3000; ++n)
    assert(reinterpret_cast<uint32_t(*)()>(image + 0x20100)() == 0x12345678u);

  for (size_t i = 0; i < g_shooter.states.size(); ++i) {
    Put(shooter.bytes, 16, reinterpret_cast<uintptr_t>(g_shooter.states[i].table));
    assert(NativeWeaponShooterIsFiring(shooter.bytes, &firing));
    assert(firing == ((i >= 3 && i <= 7) || i == 9));
    assert(observed_this == reinterpret_cast<uintptr_t>(shooter.bytes + 16));
  }
  Put(shooter.bytes, 16, reinterpret_cast<uintptr_t>(stopped_table));
  void* active = reinterpret_cast<void*>(1);
  assert(NativeWeaponShooterActiveWeapon(shooter.bytes, &active) && !active);
  assert(NativeWeaponShooterHandoffReady(shooter.bytes));
  for (size_t i = 0; i < g_shooter.states.size(); ++i) {
    Put(shooter.bytes,16,reinterpret_cast<uintptr_t>(g_shooter.states[i].table));
    assert(NativeWeaponShooterHandoffReady(shooter.bytes) == (i == 2));
  }
  Put(shooter.bytes,16,reinterpret_cast<uintptr_t>(stopped_table));
  for (size_t i = 0x5C; i < 0x7C; ++i) {
    shooter.bytes[i] = 1; assert(!NativeWeaponShooterHandoffReady(shooter.bytes)); shooter.bytes[i] = 0;
  }
  g_shooter_mutating = true; assert(!NativeWeaponShooterHandoffReady(shooter.bytes)); g_shooter_mutating = false;
  assert(!NativeWeaponShooterStartFire(shooter.bytes));  // Null _Weapon is idle, not fireable.
  Put(shooter.bytes, 0x30, reinterpret_cast<uintptr_t>(weapon.bytes));
  assert(NativeWeaponShooterActiveWeapon(shooter.bytes, &active) && active == weapon.bytes);
  assert(!NativeWeaponShooterHandoffReady(shooter.bytes));
  for (unsigned n = 0; n < 3000; ++n) {
    assert(NativeWeaponShooterStartFire(shooter.bytes));
    assert(NativeWeaponShooterIsFiring(shooter.bytes, &firing) && firing);
    assert(NativeWeaponShooterStopFire(shooter.bytes));
    assert(NativeWeaponShooterIsFiring(shooter.bytes, &firing) && !firing);
    assert(NativeWeaponShooterActivateEquipped(shooter.bytes, weapon.bytes));
    assert(observed_this == reinterpret_cast<uintptr_t>(shooter.bytes + 16));
    assert(observed_class == reinterpret_cast<uintptr_t>(weapon_class.bytes + 16));
    assert(!observed_callback.object && !observed_callback.function_name &&
           !observed_callback.compiled_context && !observed_callback.compiled_function);
  }
  assert(starts == 3000 && stops == 3000);
  // Independently prove all four words cross the unusual native Activate ABI.
  UnrealScriptDelegate complete{pawn.bytes, 1234, controller.bytes + 16, weapon.bytes};
  g_shooter.activate(&complete, weapon_class.bytes + 16, shooter.bytes + 16);
  assert(std::memcmp(&complete, &observed_callback, 16) == 0);
  // Execute the EXACT shipping Deactivate body, including its 8-byte stack
  // cleanup. It dispatches StopFire then clears both complete delegates.
  for (unsigned n = 0; n < 3000; ++n) {
    std::memset(shooter.bytes + 0x5C, 0xAB, 32);
    assert(NativeWeaponShooterDeactivate(shooter.bytes));
    for (size_t i = 0x5C; i < 0x7C; ++i) assert(shooter.bytes[i] == 0);
  }
  recursive_checks = true;
  assert(NativeWeaponShooterReset(shooter.bytes) && resets == 1);
  assert(NativeWeaponShooterActiveWeapon(shooter.bytes, &active) && !active);
  assert(!g_shooter_mutating);
  Put(shooter.bytes, 0x30, reinterpret_cast<uintptr_t>(weapon.bytes));
  // Mismatched state/ownership/inventory cannot call native code.
  assert(!NativeWeaponShooterActivateEquipped(shooter.bytes, second_weapon.bytes));
  controller_pawn = nullptr;
  assert(!NativeWeaponShooterStartFire(shooter.bytes));
  assert(!NativeWeaponShooterActivateEquipped(shooter.bytes, weapon.bytes));
  controller_pawn = pawn.bytes;
  weapon.deleting = true;
  assert(!NativeWeaponShooterStartFire(shooter.bytes));
  assert(!NativeWeaponShooterReset(shooter.bytes));
  weapon.deleting = false;
  pawn.valid = false;
  assert(!NativeWeaponShooterDeactivate(shooter.bytes));
  pawn.valid = true;
  Put(shooter.bytes, 16, 0xBAD00000u);
  assert(!NativeWeaponShooterIsFiring(shooter.bytes, &firing));
  assert(!NativeWeaponShooterReset(shooter.bytes));
  Put(shooter.bytes, 16, reinterpret_cast<uintptr_t>(stopped_table));
  Put(stopped_table, kIsFiringSlot * 4, 0xBAD00000u);
  assert(!NativeWeaponShooterIsFiring(shooter.bytes, &firing));
  Put(stopped_table, kIsFiringSlot * 4, reinterpret_cast<uintptr_t>(rva(kStates[2].methods[2])));
  // Native suspension is asynchronous: pending does NOT mean quiesced. Resume
  // from Suspended must call its state override and return to Waiting.
  for (unsigned n = 0; n < 3000; ++n) {
    assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert((Read(runner.bytes, 0x58) & 6) == 2);
    assert(Read(runner.bytes, 16) == reinterpret_cast<uintptr_t>(rva(kRunnerStates[3].table)));
    // Simulate native action completion between engine ticks.
    Put(runner.bytes, 16, reinterpret_cast<uintptr_t>(rva(kRunnerStates[4].table)));
    Put(runner.bytes, 0x58, 4);
    assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert(NativeAiResumePlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert((Read(runner.bytes, 0x58) & 6) == 0);
    assert(Read(runner.bytes, 16) == reinterpret_cast<uintptr_t>(rva(kRunnerStates[1].table)));
  }
  // Early resume while Stopping clears the pending request but doesn't force
  // an illegal state transition while the native current action still stops.
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
  assert(NativeAiResumePlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
  assert(Read(runner.bytes, 16) == reinterpret_cast<uintptr_t>(rva(kRunnerStates[3].table)));
  g_planning_mutating = true;
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  g_planning_mutating = false;
  runner_owner = nullptr;
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  runner_owner = controller.bytes;
  reflected_net_mode = 1;
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  reflected_net_mode = 0;
  // Deliberately inconsistent fixture data makes the compiled authority guard
  // skip dispatch: report a failed POSTcondition, not fictitious success.
  world.bytes[0x39C] = 1;
  controller.bytes[0x66] = 1;
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kStateMismatch);
  world.bytes[0x39C] = 0;
  Put(runner.bytes, 16, 0xBAD00000u);
  assert(NativeAiSuspendPlanRunner(controller.bytes) == NativeAiPlanningResult::kRejected);
  Put(runner.bytes, 16, reinterpret_cast<uintptr_t>(rva(kRunnerStates[1].table)));
  pawn.type = L"TnAiPawn";
  lifecycle_reentry_checks = true;
  auto plan_state = [&](size_t index, uint32_t flags) {
    Put(runner.bytes, 16, reinterpret_cast<uintptr_t>(rva(kRunnerStates[index].table)));
    Put(runner.bytes, 0x58, flags);
  };
  for (unsigned n = 0; n < 3000; ++n) {
    plan_state(2, 0);
    Put(runner.bytes, 0x40, reinterpret_cast<uintptr_t>(old_plan.bytes));
    Put(runner.bytes, 0x44, reinterpret_cast<uintptr_t>(pending_plan.bytes));
    assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert(observed_enqueue_argument == 16);
    assert(Read(runner.bytes, 0x44) == 0 && Read(runner.bytes, 0x40) != 0);
    assert((Read(runner.bytes, 0x58) & 6) == 2);
    assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(!NativeAiPlanHandoffReady(controller.bytes));
    assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
    // Native action finishes on an intervening game tick; the stopped plan
    // stays cached in Suspended (unlike a Completed plan). Clear is a no-op.
    plan_state(4, 4);
    g_planning.clear(runner.bytes + 16, nullptr);
    assert(Read(runner.bytes, 0x40) == reinterpret_cast<uintptr_t>(old_plan.bytes));
    assert(!NativeAiPlanHandoffReady(controller.bytes));
    assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert(Read(runner.bytes, 0x40) == 0 && Read(runner.bytes, 0x44) == 0);
    assert(NativeAiPlanHandoffReady(controller.bytes));
    g_planning_mutating = true; assert(!NativeAiPlanHandoffReady(controller.bytes)); g_planning_mutating = false;
    assert(RunnerInState(runner.bytes, 4));
    assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kApplied);
    assert(Read(runner.bytes, 0x38) == 0);
    assert(NativeAiSetActionSet(controller.bytes, action_set.bytes) == NativeAiPlanningResult::kApplied);
    assert(observed_bind_owner == reinterpret_cast<uintptr_t>(controller.bytes + 16));
    assert(observed_bind_set == reinterpret_cast<uintptr_t>(action_set.bytes + 16));
    assert(NativeAiSetGoalSet(controller.bytes, nullptr) == NativeAiPlanningResult::kApplied);
    assert(Read(runner.bytes, 0x34) == 0 && Read(runner.bytes, 0x3C) == 0);
    assert(NativeAiSetGoalSet(controller.bytes, goal_set.bytes) == NativeAiPlanningResult::kApplied);
    assert(observed_bind_owner == reinterpret_cast<uintptr_t>(controller.bytes + 16));
    assert(observed_bind_set == reinterpret_cast<uintptr_t>(goal_set.bytes + 16));
    assert(Read(runner.bytes, 0x3C) == 0xFFFFFFFFu); // Empty Goals.Length - 1.
    assert(NativeAiResumePlanRunner(controller.bytes) == NativeAiPlanningResult::kApplied);
    assert(!NativeAiPlanHandoffReady(controller.bytes));
  }
  assert(action_binds == 3000 && action_unbinds == 3000);
  assert(goal_binds == 3000 && goal_unbinds == 3000);
  plan_state(4, 4);
  Put(runner.bytes, 0x44, reinterpret_cast<uintptr_t>(pending_plan.bytes));
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiSetGoalSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  assert(Read(runner.bytes, 0x44) == reinterpret_cast<uintptr_t>(pending_plan.bytes));
  Put(runner.bytes, 0x44, 0);
  plan_state(3, 0);
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
  plan_state(2, 0);
  Put(runner.bytes, 0x40, reinterpret_cast<uintptr_t>(old_plan.bytes));
  old_plan.valid = false;
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
  old_plan.valid = true;
  Put(runner.bytes, 0x40, 0);
  Put(runner.bytes, 0x44, reinterpret_cast<uintptr_t>(weapon.bytes));
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
  Put(runner.bytes, 0x44, 0);
  plan_state(4, 4);
  plan_layout_shift = 4;
  assert(!NativeAiPlanHandoffReady(controller.bytes));
  assert(!PrepareNativeAiPlanLifecycle(g_planning.module, g_planning.thread, controller.bytes));
  assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
  assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  plan_layout_shift = 0;
  pawn.type = L"TnPlayerPawn";
  assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  pawn.type = L"TnAiPawn";
  pawn_controller = nullptr;
  assert(NativeAiSetGoalSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  pawn_controller = controller.bytes;
  array_header_mismatch = true;
  assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  array_header_mismatch = false;
  Put(action_set.bytes, 0x3C, reinterpret_cast<uintptr_t>(plan_items));
  Put(action_set.bytes, 0x40, 1);
  plan_items[0] = nullptr;
  assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  plan_items[0] = action.bytes;
  blueprint.valid = false;
  assert(NativeAiSetActionSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
  blueprint.valid = true;
  assert(PlanSetValid(action_set.bytes, true));
  Put(action_set.bytes, 0x3C, 0);
  Put(action_set.bytes, 0x40, 0);
  for (const auto* name : {L"_PendingActionSet", L"_PendingGoalSet"}) {
    uint32_t offset = 0;
    assert(UnrealResolveObjectPropertyOffset(runner.bytes, name, &offset));
    Put(runner.bytes, offset, reinterpret_cast<uintptr_t>(action_set.bytes));
    assert(!NativeAiPlanHandoffReady(controller.bytes));
    assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
    assert(NativeAiSetGoalSet(controller.bytes, nullptr) == NativeAiPlanningResult::kRejected);
    Put(runner.bytes, offset, 0);
  }
  // A nested engine callback that queues work causes an executed mutation
  // failure, not fictitious success or a no-mutation result.
  callback_requeues = true;
  assert(NativeAiSetActionSet(controller.bytes, action_set.bytes) == NativeAiPlanningResult::kStateMismatch);
  Put(runner.bytes, 0x44, 0);
  plan_state(2, 0);
  assert(NativeAiBeginPlanHandoff(controller.bytes) == NativeAiPlanningResult::kStateMismatch);
  callback_requeues = false;
  Put(runner.bytes, 0x44, 0);
  plan_state(4, 4);
  Put(rva(kRunnerStates[4].table), 0x60, 0xBAD00000u);
  assert(NativeAiDrainStoppedPlan(controller.bytes) == NativeAiPlanningResult::kRejected);
  Put(rva(kRunnerStates[4].table), 0x60, reinterpret_cast<uintptr_t>(rva(kRunnerStates[4].enqueue)));
  // Native queue acceptance, priority promotion and duplicate suppression.
  auto* low = reinterpret_cast<UnrealArrayView*>(scheduler.bytes + 0x28);
  auto* high = reinterpret_cast<UnrealArrayView*>(scheduler.bytes + 0x34);
  plan_state(1, 0);
  assert(PrepareNativeAiReplanning(g_planning.module, g_planning.thread, controller.bytes));
  replan_reentry_checks = true;
  for (unsigned n = 0; n < 3000; ++n) {
    low->count = high->count = 0;
    Put(runner.bytes, 0x58, 0);
    assert(NativeAiRequestReplan(controller.bytes, 0) == NativeAiPlanningResult::kApplied);
    assert((Read(runner.bytes, 0x58) & 1) && low->count == 1 && high->count == 0);
    assert(static_cast<void**>(low->data)[0] == runner.bytes); // UObject, not script-this.
    assert(NativeAiRequestReplan(controller.bytes, 0) == NativeAiPlanningResult::kApplied);
    assert(low->count == 1 && high->count == 0);
    assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kApplied);
    assert(low->count == 0 && high->count == 1);
    assert(static_cast<void**>(high->data)[0] == runner.bytes);
    assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kApplied);
    assert(NativeAiRequestReplan(controller.bytes, 0) == NativeAiPlanningResult::kApplied);
    assert(low->count == 0 && high->count == 1); // Never demotes an existing high request.
  }
  assert(queue_allocations == 2 && queue_removals == 3000 && game_cast_calls == 15000);
  // Exercise the shipping Find loops and nonzero-index Remove/promotion in a
  // queue larger than the loadout helper's old 64-entry limit. Null entries
  // are legitimate: native scheduler Update explicitly skips None runners.
  for (auto* queue : {low, high}) {
    queue->data = std::realloc(queue->data, 128 * 4);
    assert(queue->data);
    queue->capacity = 128;
    queue->count = 80;
    std::memset(queue->data, 0, 128 * 4);
  }
  static_cast<void**>(low->data)[63] = runner.bytes;
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kApplied);
  assert(low->count == 79 && high->count == 81);
  for (int i = 0; i < low->count; ++i) assert(static_cast<void**>(low->data)[i] == nullptr);
  assert(static_cast<void**>(high->data)[80] == runner.bytes);
  assert(NativeAiRequestReplan(controller.bytes, 0) == NativeAiPlanningResult::kApplied);
  assert(high->count == 81);
  low->count = high->count = 0;
  auto rejected_replan = [&]() {
    const auto calls = game_cast_calls, flags = Read(runner.bytes, 0x58);
    assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kRejected);
    assert(game_cast_calls == calls && Read(runner.bytes, 0x58) == flags);
  };
  for (auto state : {0u, 3u, 4u}) {
    plan_state(state, state == 4 ? 4 : 0);
    rejected_replan();
  }
  plan_state(1, 2); rejected_replan(); // Pending stop cannot accept work either.
  plan_state(1, 0);
  assert(NativeAiRequestReplan(controller.bytes, 2) == NativeAiPlanningResult::kRejected);
  Put(scheduler.bytes, 0x50, 0); rejected_replan(); Put(scheduler.bytes, 0x50, 1);
  current_world_fixture = game.bytes; rejected_replan(); current_world_fixture = world.bytes;
  Put(context_fixture, 0x28, 0xBAD00000u); rejected_replan();
  Put(context_fixture, 0x28, reinterpret_cast<uintptr_t>(&current_world_fixture));
  Put(world.bytes, 0x3C4, 0); rejected_replan();
  Put(world.bytes, 0x3C4, reinterpret_cast<uintptr_t>(game.bytes));
  game.deleting = true; rejected_replan(); game.deleting = false;
  scheduler.valid = false; rejected_replan(); scheduler.valid = true;
  queue_layout_mismatch = true; rejected_replan(); queue_layout_mismatch = false;
  low->count = -1; rejected_replan(); low->count = 0;
  const auto capacity = low->capacity;
  low->capacity = 4097; rejected_replan(); low->capacity = capacity;
  const auto data = low->data;
  low->data = reinterpret_cast<void*>(0xBAD00000u); rejected_replan(); low->data = data;
  low->count = 1;
  static_cast<void**>(low->data)[0] = reinterpret_cast<void*>(0xBAD00000u);
  rejected_replan(); low->count = 0;
  // Reject duplicates across priorities without mutating the engine queues.
  low->count = high->count = 1;
  static_cast<void**>(low->data)[0] = static_cast<void**>(high->data)[0] = runner.bytes;
  rejected_replan();
  low->count = high->count = 0;
  pawn.type = L"TnPlayerPawn"; rejected_replan(); pawn.type = L"TnAiPawn";
  runner_owner = nullptr; rejected_replan(); runner_owner = controller.bytes;
  game_cast_rejects = true;
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kStateMismatch);
  assert(low->count == 0 && high->count == 0);
  game_cast_rejects = false;
  deactivate_on_cast = true;
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kStateMismatch);
  deactivate_on_cast = false;
  Put(scheduler.bytes, 0x50, 1);
  // The controller's native authority guard is preserved, not bypassed.
  Put(runner.bytes, 0x58, 0);
  world.bytes[0x39C] = 1;
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kStateMismatch);
  assert(Read(runner.bytes, 0x58) == 0);
  world.bytes[0x39C] = 0;
  assert(NativeAiRequestReplan(controller.bytes, 1) == NativeAiPlanningResult::kApplied);
  std::free(low->data); std::free(high->data);
  *low = {}; *high = {};
  HANDLE thread = CreateThread(nullptr, 0, &OtherThread, nullptr, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 10000) == WAIT_OBJECT_0);
  CloseHandle(thread);
  g_shooter = {};
  g_planning = {};
  assert(VirtualFree(image, 0, MEM_RELEASE));
  std::puts("NativeWeaponShooterTests passed");
  std::puts("Native AI planning fixture tests passed");
  std::puts("Native AI lifecycle: 3000 exact-body handoff/drain/setter cycles passed");
  std::puts("Native AI replanning: 15000 exact-body submit/dedup/promotion calls passed");
}
