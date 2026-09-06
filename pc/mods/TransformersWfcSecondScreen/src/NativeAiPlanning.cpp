#include "NativeAiPlanning.h"

#include <array>
#include <cstdint>
#include <cstring>

#include "Log.h"
#include "NativeScriptAbi.h"
#include "NativeAiPlanningCode.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void WfcCallEsiObject(void* function, void* script_this, void* compiled_value);
namespace {
constexpr uintptr_t kPlanningImageBase = 0x10900000u;
constexpr uintptr_t kSuspendRunnerRva = 0x002B95F0u;
constexpr uintptr_t kResumeRunnerRva = 0x002B9620u;
// The complete TnAiController.SuspendPlanRunner body. Resume is identical
// except its final virtual slot is +58. The leading network/Role check is
// compiler-inserted and absent from the cooked UnrealScript body. Keep it.
constexpr uint8_t kSuspendRunnerCode[] = {
    0x8B, 0x81, 0x88, 0x00, 0x00, 0x00, 0x85, 0xC0, 0x74, 0x0F,
    0x80, 0xB8, 0x9C, 0x03, 0x00, 0x00, 0x00, 0x74, 0x06,
    0x80, 0x79, 0x56, 0x01, 0x76, 0x10,
    0x8B, 0x81, 0xF4, 0x02, 0x00, 0x00, 0x8D, 0x48, 0x10,
    0x8B, 0x01, 0x8B, 0x50, 0x54, 0xFF, 0xE2, 0xC3};
constexpr size_t kRunnerSuspendSlot = 0x54 / 4;
constexpr size_t kRunnerResumeSlot = 0x58 / 4;
constexpr size_t kRunnerEnqueueSlot = 0x60 / 4;
struct RunnerStateSpec {
  uintptr_t table, suspend, resume, enqueue;
};
// 1137D590 constructs default, Waiting, Running, Stopping and Suspended.
// Suspended.Resume must transition to Waiting, NOT call base Resume, which
// only clears SuspendPending and leaves the runner stuck in Suspended.
constexpr RunnerStateSpec kRunnerStates[] = {
    {0x129C6C48u, 0x113790A0u, 0x10FDD5B0u, 0x1139B3D0u},
    {0x129C7190u, 0x113790A0u, 0x10FDD5B0u, 0x1139BA00u},
    {0x129C71F8u, 0x113790A0u, 0x10FDD5B0u, 0x1139B3D0u},
    {0x129C7260u, 0x113790A0u, 0x10FDD5B0u, 0x10FDD5D0u},
    {0x129C72C8u, 0x10FDD600u, 0x11378E20u, 0x10FDD5F0u},
};
using RunnerCall = void (__attribute__((fastcall)) *)(void* script_this, void* unused_edx);
using ReplanCall = void (__attribute__((fastcall)) *)(void* script_this, void* unused_edx, uint32_t priority);
struct BoundRunnerState {
  const uintptr_t* table = nullptr;
  uintptr_t suspend = 0, resume = 0, enqueue = 0;
};
struct PlanningBindings {
  HMODULE module = nullptr;
  DWORD thread = 0;
  RunnerCall suspend = nullptr, resume = nullptr;
  RunnerCall clear = nullptr;
  // Fastcall's second register is EDX. The first (ECX) is unused on entry.
  RunnerCall pump = nullptr;
  void* set_goal = nullptr;
  void* set_action = nullptr;
  ReplanCall request = nullptr;
  const void* engine_root = nullptr;
  std::array<BoundRunnerState, 5> states = {};
};
PlanningBindings g_planning;
thread_local bool g_planning_mutating = false;
struct PlanningMutationGuard {
  PlanningMutationGuard() { g_planning_mutating = true; }
  ~PlanningMutationGuard() { g_planning_mutating = false; }
};

bool PlanningImageReadable(HMODULE module, const void* address, size_t size) {
  MEMORY_BASIC_INFORMATION info = {};
  return VirtualQuery(address, &info, sizeof(info)) &&
      info.AllocationBase == module && info.State == MEM_COMMIT &&
      !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
      size <= info.RegionSize - (reinterpret_cast<uintptr_t>(address) -
                                reinterpret_cast<uintptr_t>(info.BaseAddress));
}

bool BindPlanningImage(HMODULE module, DWORD thread, PlanningBindings* result) {
  if (!module || !thread || thread != GetCurrentThreadId() || !result) return false;
  const auto base = reinterpret_cast<uintptr_t>(module);
  for (unsigned method = 0; method < 2; ++method) {
    const auto* code = reinterpret_cast<const uint8_t*>(base +
        (method ? kResumeRunnerRva : kSuspendRunnerRva));
    if (!PlanningImageReadable(module, code, sizeof(kSuspendRunnerCode))) return false;
    for (size_t i = 0; i < sizeof(kSuspendRunnerCode); ++i) {
      const auto expected = i == 38 && method ? 0x58 : kSuspendRunnerCode[i];
      if (code[i] != expected) return false;
    }
  }
  PlanningBindings next;
  next.module = module;
  next.thread = thread;
  next.suspend = reinterpret_cast<RunnerCall>(base + kSuspendRunnerRva);
  next.resume = reinterpret_cast<RunnerCall>(base + kResumeRunnerRva);
  for (const auto& spec : kPlanLifecycleCode) {
    std::array<uint8_t, 160> expected = {};
    if (spec.size > expected.size()) return false;
    std::memcpy(expected.data(), spec.bytes, spec.size);
    for (size_t i = 0; i < spec.relocation_count; ++i) {
      uint32_t address = 0;
      const auto offset = spec.relocations[i];
      if (offset + 4 > spec.size) return false;
      std::memcpy(&address, expected.data() + offset, 4);
      address += static_cast<uint32_t>(base - kPlanningImageBase);
      std::memcpy(expected.data() + offset, &address, 4);
    }
    const auto* code = reinterpret_cast<const void*>(base + spec.rva);
    if (!PlanningImageReadable(module, code, spec.size) ||
        std::memcmp(code, expected.data(), spec.size)) return false;
  }
  next.clear = reinterpret_cast<RunnerCall>(base + kPlanLifecycleCode[0].rva);
  next.set_goal = reinterpret_cast<void*>(base + kPlanLifecycleCode[1].rva);
  next.set_action = reinterpret_cast<void*>(base + kPlanLifecycleCode[2].rva);
  next.pump = reinterpret_cast<RunnerCall>(base + kPlanLifecycleCode[3].rva);
  next.request = reinterpret_cast<ReplanCall>(base + kPlanLifecycleCode[4].rva);
  next.engine_root = reinterpret_cast<const void*>(base + 0x02210A9Cu);
  if (!PlanningImageReadable(module, next.engine_root, sizeof(void*))) return false;
  for (size_t i = 0; i < next.states.size(); ++i) {
    auto& state = next.states[i];
    const auto& spec = kRunnerStates[i];
    state.table = reinterpret_cast<const uintptr_t*>(base + spec.table - kPlanningImageBase);
    state.suspend = base + spec.suspend - kPlanningImageBase;
    state.resume = base + spec.resume - kPlanningImageBase;
    state.enqueue = base + spec.enqueue - kPlanningImageBase;
    if (!PlanningImageReadable(module, state.table, 0x64) ||
        state.table[kRunnerSuspendSlot] != state.suspend ||
        state.table[kRunnerResumeSlot] != state.resume ||
        state.table[kRunnerEnqueueSlot] != state.enqueue) return false;
  }
  *result = next;
  return true;
}

void* PlanningRunner(void* controller) {
  if (!g_planning.module || GetCurrentThreadId() != g_planning.thread ||
      !UnrealObjectIsA(controller, L"TnAiController")) return nullptr;
  bool deleting = false;
  uint8_t net_mode = 0xFF;
  void* world = nullptr;
  void* runner = nullptr;
  void* owner = nullptr;
  if (!UnrealReadBoolProperty(controller, L"bDeleteMe", &deleting) || deleting ||
      !UnrealReadObjectProperty(controller, L"WorldInfo", &world) ||
      !UnrealObjectIsA(world, L"WorldInfo") ||
      !UnrealReadByteProperty(world, L"NetMode", &net_mode) || net_mode != 0 ||
      !UnrealReadObjectProperty(controller, L"PlanRunner", &runner) ||
      !UnrealObjectIsA(runner, L"HmGoapPlanRunner") ||
      !UnrealReadObjectProperty(runner, L"_Owner", &owner) || owner != controller)
    return nullptr;
  const auto* table = *reinterpret_cast<const uintptr_t* const*>(
      static_cast<uint8_t*>(runner) + kNativeScriptThisOffset);
  for (const auto& state : g_planning.states) {
    if (state.table == table)
      return table[kRunnerSuspendSlot] == state.suspend &&
             table[kRunnerResumeSlot] == state.resume &&
             table[kRunnerEnqueueSlot] == state.enqueue ? runner : nullptr;
  }
  return nullptr;
}

NativeAiPlanningResult ChangePlanSuspension(void* controller, bool suspend) {
  if (g_planning_mutating) return NativeAiPlanningResult::kRejected;
  void* runner = PlanningRunner(controller);
  if (!runner) return NativeAiPlanningResult::kRejected;
  // Do not execute first and discover missing state properties afterwards.
  bool suspended = false, pending = false;
  if (!UnrealReadBoolProperty(runner, L"_Suspended", &suspended) ||
      !UnrealReadBoolProperty(runner, L"_SuspendPending", &pending))
    return NativeAiPlanningResult::kRejected;
  PlanningMutationGuard guard;
  (suspend ? g_planning.suspend : g_planning.resume)(
      static_cast<uint8_t*>(controller) + kNativeScriptThisOffset, nullptr);
  if (PlanningRunner(controller) != runner ||
      !UnrealReadBoolProperty(runner, L"_Suspended", &suspended) ||
      !UnrealReadBoolProperty(runner, L"_SuspendPending", &pending))
    return NativeAiPlanningResult::kStateMismatch;
  const bool expected = suspend ? suspended || pending : !suspended && !pending;
  return expected ? NativeAiPlanningResult::kApplied : NativeAiPlanningResult::kStateMismatch;
}

bool RunnerInState(void* runner, size_t state) {
  return *reinterpret_cast<const uintptr_t* const*>(
      static_cast<uint8_t*>(runner) + kNativeScriptThisOffset) == g_planning.states[state].table;
}

bool PlanLifecycleLayout(void* runner) {
  const struct { const wchar_t* name; uint32_t offset; } properties[] = {
      {L"_Owner", 0x30}, {L"_GoalSet", 0x34}, {L"_ActionSet", 0x38},
      {L"_CurrentSolution", 0x40}, {L"_PendingSolution", 0x44},
      {L"_PendingActionSet", 0x5C}, {L"_PendingGoalSet", 0x60}};
  for (const auto& property : properties) {
    uint32_t offset = 0;
    if (!UnrealResolveObjectPropertyOffset(runner, property.name, &offset) ||
        offset != property.offset) return false;
  }
  return true;
}

bool PlanPropertyEmpty(void* runner, const wchar_t* name) {
  void* value = nullptr;
  return UnrealReadObjectProperty(runner, name, &value) && !value;
}

bool PlanSolutionsValid(void* runner) {
  for (const auto* name : {L"_CurrentSolution", L"_PendingSolution"}) {
    void* plan = nullptr;
    if (!UnrealReadObjectProperty(runner, name, &plan) ||
        (plan && !UnrealObjectIsA(plan, L"HmGoapPlan"))) return false;
  }
  return true;
}

bool PlanSetValid(void* set, bool action) {
  if (!set) return true;
  if (!UnrealObjectIsA(set, action ? L"HmGoapActionSet" : L"HmGoapGoalSet")) return false;
  UnrealArrayView items;
  if (!UnrealReadArrayProperty(set, action ? L"Actions" : L"Goals", &items) ||
      items.count < 0 || items.count > 256) return false;
  // These arrays are consumed directly by the compiled setters, not through
  // metadata. Prove the runtime header matches the native layout too.
  const size_t offset = action ? 0x3C : 0x48;
  void* data = nullptr;
  int32_t count = -1;
  std::memcpy(&data, static_cast<uint8_t*>(set) + offset, 4);
  std::memcpy(&count, static_cast<uint8_t*>(set) + offset + 4, 4);
  if (items.data != data || items.count != count) return false;
  for (int32_t i = 0; i < items.count; ++i) {
    void* item = nullptr;
    if (!UnrealReadObjectArrayElement(items, i, &item) ||
        !UnrealObjectIsA(item, action ? L"HmGoapAction" : L"HmGoapGoal")) return false;
    // ActionSet.Bind dereferences _BaseBlueprint before dispatching OnBind.
    if (action) {
      void* blueprint = nullptr;
      if (!UnrealReadObjectProperty(item, L"_BaseBlueprint", &blueprint) ||
          !UnrealObjectValid(blueprint)) return false;
    }
  }
  return true;
}

bool RunnerSetsValid(void* runner) {
  void* action = nullptr;
  void* goal = nullptr;
  return UnrealReadObjectProperty(runner, L"_ActionSet", &action) &&
      UnrealReadObjectProperty(runner, L"_GoalSet", &goal) &&
      PlanSetValid(action, true) && PlanSetValid(goal, false);
}

bool RunnerSuspended(void* runner) {
  bool suspended = false, pending = true;
  return RunnerInState(runner, 4) &&
      UnrealReadBoolProperty(runner, L"_Suspended", &suspended) && suspended &&
      UnrealReadBoolProperty(runner, L"_SuspendPending", &pending) && !pending;
}

bool RunnerDrained(void* runner) {
  return RunnerSuspended(runner) &&
      PlanPropertyEmpty(runner, L"_CurrentSolution") &&
      PlanPropertyEmpty(runner, L"_PendingSolution") &&
      PlanPropertyEmpty(runner, L"_PendingActionSet") &&
      PlanPropertyEmpty(runner, L"_PendingGoalSet");
}

bool PhysicalAiPair(void* controller) {
  void* pawn = nullptr;
  void* owner = nullptr;
  bool deleting = true;
  return UnrealReadObjectProperty(controller, L"Pawn", &pawn) &&
      UnrealObjectIsA(pawn, L"TnAiPawn") &&
      UnrealReadBoolProperty(pawn, L"bDeleteMe", &deleting) && !deleting &&
      UnrealReadObjectProperty(pawn, L"Controller", &owner) && owner == controller;
}

bool PlanningDataReadable(const void* address, size_t size) {
  MEMORY_BASIC_INFORMATION info = {};
  return address && size && VirtualQuery(address, &info, sizeof(info)) &&
      info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD)) &&
      size <= info.RegionSize - (reinterpret_cast<uintptr_t>(address) -
                                reinterpret_cast<uintptr_t>(info.BaseAddress));
}

bool PlanningPointer(const void* object, size_t offset, void** output) {
  if (!object || !output || reinterpret_cast<uintptr_t>(object) > UINTPTR_MAX - offset)
    return false;
  const auto* address = reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(object) + offset);
  if (!PlanningDataReadable(address, sizeof(void*))) return false;
  std::memcpy(output, address, sizeof(void*));
  return *output != nullptr;
}

void* ActivePlanningScheduler(void* controller) {
  void* world = nullptr;
  void* game = nullptr;
  void* scheduler = nullptr;
  uint32_t offset = 0;
  bool deleting = true, active = false;
  if (!UnrealReadObjectProperty(controller, L"WorldInfo", &world) ||
      !UnrealObjectIsA(world, L"WorldInfo") ||
      !UnrealReadBoolProperty(world, L"bDeleteMe", &deleting) || deleting ||
      !UnrealResolveObjectPropertyOffset(world, L"Game", &offset) || offset != 0x3C4 ||
      !UnrealReadObjectProperty(world, L"Game", &game) || !UnrealObjectIsA(game, L"TnGame") ||
      !UnrealReadBoolProperty(game, L"bDeleteMe", &deleting) || deleting ||
      !UnrealResolveObjectPropertyOffset(game, L"_AiPlanningScheduler", &offset) || offset != 0x380 ||
      !UnrealReadObjectProperty(game, L"_AiPlanningScheduler", &scheduler) ||
      !UnrealObjectIsA(scheduler, L"TnAiPlanningScheduler") ||
      !UnrealReadBoolProperty(scheduler, L"_Activated", &active) || !active ||
      !PlanningDataReadable(static_cast<uint8_t*>(scheduler) + 0x50, 4) ||
      !(*reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(scheduler) + 0x50) & 1))
    return nullptr;
  // Engine.GetGame does not use controller.WorldInfo. Prove its native global
  // chain resolves the SAME world before the compiled method dereferences it.
  // In particular, never submit an old checkpoint's runner to a new level.
  void* engine = nullptr;
  void* context = nullptr;
  void* worlds = nullptr;
  void* current_world = nullptr;
  if (!PlanningPointer(g_planning.engine_root, 0, &engine) ||
      !PlanningPointer(engine, 0x40, &context) ||
      !PlanningPointer(context, 0x28, &worlds) ||
      !PlanningPointer(worlds, 0, &current_world) || current_world != world)
    return nullptr;
  void* native_game = nullptr;
  void* native_scheduler = nullptr;
  return PlanningPointer(world, 0x3C4, &native_game) && native_game == game &&
      PlanningPointer(game, 0x380, &native_scheduler) && native_scheduler == scheduler
      ? scheduler : nullptr;
}

bool ReplanQueueCounts(void* scheduler, void* runner, std::array<int32_t, 2>* matches) {
  *matches = {};
  UnrealStructArrayView lists;
  if (!UnrealReadFixedStructArrayProperty(scheduler, L"_PendingRequests", &lists) ||
      lists.data != static_cast<uint8_t*>(scheduler) + 0x28 || lists.count != 2 ||
      lists.element_size != sizeof(UnrealArrayView)) return false;
  for (int priority = 0; priority < 2; ++priority) {
    const auto* header = static_cast<uint8_t*>(lists.data) + priority * lists.element_size;
    UnrealStructValueView list{header, lists.element_size, lists.struct_definition};
    UnrealArrayView queue;
    if (!UnrealReadStructTypedArray(list, L"Requests", L"ObjectProperty", 4, &queue, 4096) ||
        !PlanningDataReadable(header, sizeof(queue)) || std::memcmp(header, &queue, sizeof(queue)) ||
        (queue.capacity && !PlanningDataReadable(queue.data, queue.capacity * sizeof(void*))) ||
        queue.count >= 2048) return false; // Bounded one-shot verification, never a per-frame scan.
    for (int i = 0; i < queue.count; ++i) {
      void* queued = nullptr;
      if (!UnrealReadObjectArrayElement(queue, i, &queued)) return false;
      if (queued == runner) ++(*matches)[priority];
    }
  }
  // The shipping scheduler promises uniqueness. Don't try to repair a corrupt
  // queue by writing/removing its entries behind the engine's back.
  return (*matches)[0] + (*matches)[1] <= 1;
}

bool RunnerReadyToReplan(void* runner) {
  bool suspended = true, pending = true, evaluate = false;
  if ((!RunnerInState(runner, 1) && !RunnerInState(runner, 2)) ||
      !UnrealReadBoolProperty(runner, L"_Suspended", &suspended) || suspended ||
      !UnrealReadBoolProperty(runner, L"_SuspendPending", &pending) || pending ||
      !UnrealReadBoolProperty(runner, L"_EvaluateCurrentGoalOnReplan", &evaluate) ||
      !PlanPropertyEmpty(runner, L"_PendingActionSet") ||
      !PlanPropertyEmpty(runner, L"_PendingGoalSet")) return false;
  const auto flags = *reinterpret_cast<const uint32_t*>(static_cast<uint8_t*>(runner) + 0x58);
  return !(flags & 6) && ((flags & 1) != 0) == evaluate;
}

NativeAiPlanningResult SetPlanSet(void* controller, void* value, bool action) {
  if (g_planning_mutating) return NativeAiPlanningResult::kRejected;
  void* runner = PlanningRunner(controller);
  if (!runner || !PlanLifecycleLayout(runner) || !RunnerDrained(runner) ||
      !PhysicalAiPair(controller) || !RunnerSetsValid(runner) || !PlanSetValid(value, action))
    return NativeAiPlanningResult::kRejected;
  PlanningMutationGuard guard;
  WfcCallEsiObject(action ? g_planning.set_action : g_planning.set_goal,
      static_cast<uint8_t*>(runner) + kNativeScriptThisOffset,
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(value) + kNativeScriptThisOffset));
  void* actual = nullptr;
  if (PlanningRunner(controller) != runner || !RunnerDrained(runner) ||
      !PhysicalAiPair(controller) ||
      !UnrealReadObjectProperty(runner, action ? L"_ActionSet" : L"_GoalSet", &actual) ||
      actual != value) return NativeAiPlanningResult::kStateMismatch;
  return NativeAiPlanningResult::kApplied;
}
}  // namespace

bool PrepareNativeAiPlanning(HMODULE module, DWORD game_thread, void* controller) {
  if (!module || module != GetModuleHandleW(nullptr) || !game_thread ||
      game_thread != GetCurrentThreadId()) return false;
  if (!g_planning.module) {
    if (!BindPlanningImage(module, game_thread, &g_planning)) {
      Log(LogLevel::kError, "[SquadSwitch] native AI planning signature mismatch");
      return false;
    }
    Log(LogLevel::kInfo, "[SquadSwitch] native AI planning bindings ready (5 state tables)");
  }
  uint32_t offset = 0;
  return module == g_planning.module && game_thread == g_planning.thread &&
      UnrealResolveObjectPropertyOffset(controller, L"PlanRunner", &offset) && offset == 0x304 &&
      UnrealResolveObjectPropertyOffset(controller, L"WorldInfo", &offset) && offset == 0x98 &&
      PlanningRunner(controller);
}
NativeAiPlanningResult NativeAiSuspendPlanRunner(void* controller) { return ChangePlanSuspension(controller, true); }
NativeAiPlanningResult NativeAiResumePlanRunner(void* controller) { return ChangePlanSuspension(controller, false); }

bool PrepareNativeAiPlanLifecycle(HMODULE module, DWORD thread, void* controller) {
  if (!PrepareNativeAiPlanning(module, thread, controller)) return false;
  void* runner = PlanningRunner(controller);
  return runner && PlanLifecycleLayout(runner) && RunnerSetsValid(runner) && PlanSolutionsValid(runner);
}

bool NativeAiPlanHandoffReady(void* controller) {
  if (g_planning_mutating) return false;
  void* runner = PlanningRunner(controller);
  return runner && PlanLifecycleLayout(runner) && RunnerDrained(runner);
}

NativeAiPlanningResult NativeAiBeginPlanHandoff(void* controller) {
  if (g_planning_mutating) return NativeAiPlanningResult::kRejected;
  void* runner = PlanningRunner(controller);
  if (!runner || !PlanLifecycleLayout(runner) || !RunnerSetsValid(runner) || !PlanSolutionsValid(runner) ||
      !PlanPropertyEmpty(runner, L"_PendingActionSet") ||
      !PlanPropertyEmpty(runner, L"_PendingGoalSet")) return NativeAiPlanningResult::kRejected;
  if (RunnerSuspended(runner))
    return PlanPropertyEmpty(runner, L"_PendingSolution") ? NativeAiPlanningResult::kApplied
                                                        : NativeAiPlanningResult::kRejected;
  // Stopping ignores EnqueueSolution too. Do not pretend that a clear was
  // accepted or cancel an unrelated pending transition. Retry another time.
  bool suspended = true, pending = true;
  if ((!RunnerInState(runner, 1) && !RunnerInState(runner, 2)) ||
      !UnrealReadBoolProperty(runner, L"_Suspended", &suspended) || suspended ||
      !UnrealReadBoolProperty(runner, L"_SuspendPending", &pending) || pending)
    return NativeAiPlanningResult::kRejected;
  PlanningMutationGuard guard;
  g_planning.clear(static_cast<uint8_t*>(runner) + kNativeScriptThisOffset, nullptr);
  // Clear can enter Stopping/Waiting and invoke game callbacks. Revalidate
  // before touching the controller again. Any failure after this is a mutation.
  if (PlanningRunner(controller) != runner || !PlanPropertyEmpty(runner, L"_PendingSolution"))
    return NativeAiPlanningResult::kStateMismatch;
  g_planning.suspend(static_cast<uint8_t*>(controller) + kNativeScriptThisOffset, nullptr);
  if (PlanningRunner(controller) != runner ||
      !UnrealReadBoolProperty(runner, L"_Suspended", &suspended) ||
      !UnrealReadBoolProperty(runner, L"_SuspendPending", &pending) ||
      (!suspended && !pending) || !PlanPropertyEmpty(runner, L"_PendingSolution"))
    return NativeAiPlanningResult::kStateMismatch;
  return NativeAiPlanningResult::kApplied;
}

NativeAiPlanningResult NativeAiDrainStoppedPlan(void* controller) {
  if (g_planning_mutating) return NativeAiPlanningResult::kRejected;
  void* runner = PlanningRunner(controller);
  if (!runner || !PlanLifecycleLayout(runner) || !RunnerSuspended(runner) ||
      !RunnerSetsValid(runner) || !PlanPropertyEmpty(runner, L"_PendingSolution") ||
      !PlanPropertyEmpty(runner, L"_PendingActionSet") ||
      !PlanPropertyEmpty(runner, L"_PendingGoalSet")) return NativeAiPlanningResult::kRejected;
  PlanningMutationGuard guard;
  // PumpQueue's complete native None branch drops the halted solution and
  // resets the goal cursor. With PendingSolution == None it cannot transition
  // to Running, dispatch Start, or notify Waiting observers. Never Resume here.
  g_planning.pump(nullptr, static_cast<uint8_t*>(runner) + kNativeScriptThisOffset);
  return PlanningRunner(controller) == runner && RunnerDrained(runner)
      ? NativeAiPlanningResult::kApplied : NativeAiPlanningResult::kStateMismatch;
}
NativeAiPlanningResult NativeAiSetActionSet(void* controller, void* action) { return SetPlanSet(controller, action, true); }
NativeAiPlanningResult NativeAiSetGoalSet(void* controller, void* goal) { return SetPlanSet(controller, goal, false); }

bool PrepareNativeAiReplanning(HMODULE module, DWORD thread, void* controller) {
  if (!PrepareNativeAiPlanLifecycle(module, thread, controller)) return false;
  void* runner = PlanningRunner(controller);
  void* scheduler = ActivePlanningScheduler(controller);
  std::array<int32_t, 2> counts;
  bool evaluate = false;
  return scheduler && PhysicalAiPair(controller) &&
      UnrealReadBoolProperty(runner, L"_EvaluateCurrentGoalOnReplan", &evaluate) &&
      ReplanQueueCounts(scheduler, runner, &counts);
}

NativeAiPlanningResult NativeAiRequestReplan(void* controller, uint8_t priority) {
  if (g_planning_mutating || priority > 1) return NativeAiPlanningResult::kRejected;
  void* runner = PlanningRunner(controller);
  if (!runner || !PhysicalAiPair(controller) || !PlanLifecycleLayout(runner) ||
      !RunnerReadyToReplan(runner)) {
    Log(LogLevel::kWarning, "[SquadSwitch] native replan rejected: runner/controller not ready");
    return NativeAiPlanningResult::kRejected;
  }
  void* scheduler = ActivePlanningScheduler(controller);
  std::array<int32_t, 2> before, after;
  if (!scheduler || !ReplanQueueCounts(scheduler, runner, &before)) {
    Log(LogLevel::kWarning, "[SquadSwitch] native replan rejected: inactive/stale scheduler or invalid queue");
    return NativeAiPlanningResult::kRejected;
  }
  PlanningMutationGuard guard;
  g_planning.request(static_cast<uint8_t*>(controller) + kNativeScriptThisOffset, nullptr, priority);
  bool evaluate = false;
  // The native call only enqueues. Never run TryBetterPlan here: WFC's Update
  // owns its time budget and dispatches the correct current-state method.
  const int expected_priority = priority == 1 || before[1] ? 1 : 0;
  const bool accepted = PlanningRunner(controller) == runner && PhysicalAiPair(controller) &&
      RunnerReadyToReplan(runner) && ActivePlanningScheduler(controller) == scheduler &&
      UnrealReadBoolProperty(runner, L"_EvaluateCurrentGoalOnReplan", &evaluate) && evaluate &&
      ReplanQueueCounts(scheduler, runner, &after) && after[expected_priority] == 1 &&
      after[1 - expected_priority] == 0;
  Log(accepted ? LogLevel::kInfo : LogLevel::kWarning,
      "[SquadSwitch] native replan priority=%u queue=%s", priority,
      accepted ? "accepted" : "postcondition-failed");
  return accepted ? NativeAiPlanningResult::kApplied : NativeAiPlanningResult::kStateMismatch;
}
}  // namespace wfcds
