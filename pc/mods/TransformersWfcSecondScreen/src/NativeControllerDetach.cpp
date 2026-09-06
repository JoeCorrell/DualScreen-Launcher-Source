#include "NativeControllerDetach.h"
#include <array>
#include <cstring>
#include <utility>
#include "Log.h"
#include "NativeControllerDetachCode.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {
using DetachCall = void (__attribute__((fastcall)) *)(void* script_this, void* unused_edx);
struct DetachBindings {
  HMODULE module = nullptr;
  DWORD thread = 0;
  DetachCall player = nullptr, ai = nullptr;
};
DetachBindings g_detach;
thread_local bool g_detach_busy = false;
constexpr size_t kMaxObservers = 256;
struct DetachCapture {
  void* controller = nullptr;
  void* pawn = nullptr;
  void* world = nullptr;
  void* analyzer = nullptr;
  void* camera = nullptr;
  uint32_t controller_index = 0, pawn_index = 0, world_index = 0, analyzer_index = 0;
  uint32_t camera_index = 0;
  bool player = false;
  int32_t observer_count = 0, own_observer = -1;
  std::array<UnrealScriptDelegate, kMaxObservers> observers{};
};
uintptr_t DetachAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_detach.module) + va - native_reference_code::kImageBase;
}
bool DetachMemory(const void* p, size_t size, HMODULE module = nullptr,
                  bool execute = false, bool write = false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      (module && info.AllocationBase != module) ||
      size > info.RegionSize - (reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD protection = info.Protect & 0xFFu;
  if (write && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
      protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}
bool BindDetachImage(HMODULE module, DWORD thread, DetachBindings* output) {
  if (!module || !thread || thread != GetCurrentThreadId() || !output) return false;
  const uintptr_t image = reinterpret_cast<uintptr_t>(module);
  if (!native_detach_code::Visit([&](uintptr_t va, const auto& code,
                                     std::initializer_list<size_t> operands) {
    const auto relocated = native_reference_code::Relocate(code, image, operands);
    const auto* address = reinterpret_cast<const void*>(image + va - native_reference_code::kImageBase);
    return DetachMemory(address, relocated.size(), module, true) &&
        !std::memcmp(address, relocated.data(), relocated.size());
  })) return false;
  for (const auto constant : {std::pair<uintptr_t,uint32_t>{0x126269A4u,0x40000000u},
                              {0x1296CD1Cu,0x40A00000u}}) {
    const auto* address = reinterpret_cast<const uint32_t*>(image + constant.first - native_reference_code::kImageBase);
    if (!DetachMemory(address,4,module) || *address != constant.second) return false;
  }
  *output = {module, thread,
      reinterpret_cast<DetachCall>(image + native_detach_code::kPlayerVa - native_reference_code::kImageBase),
      reinterpret_cast<DetachCall>(image + native_detach_code::kAiVa - native_reference_code::kImageBase)};
  return true;
}
bool DetachField(void* object, const wchar_t* name, uint32_t offset, void** value) {
  uint32_t actual = 0;
  return UnrealResolveObjectPropertyOffset(object, name, &actual) && actual == offset &&
      UnrealReadObjectProperty(object, name, value);
}
bool DetachByte(void* object, const wchar_t* name, uint32_t offset, uint8_t* value) {
  uint32_t actual = 0;
  return UnrealResolveBytePropertyOffset(object, name, &actual) && actual == offset &&
      UnrealReadByteProperty(object, name, value);
}
bool DetachActor(void* object, const wchar_t* type) {
  bool deleting = true;
  uint8_t role = 0;
  return UnrealObjectIsA(object, type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object, L"bDeleteMe", &deleting) && !deleting &&
      DetachByte(object, L"Role", 0x66, &role) && role == 3;
}
bool DetachVirtual(void* object, size_t table_offset, size_t slot, uintptr_t expected = 0) {
  uintptr_t table = 0, function = 0;
  if (!DetachMemory(static_cast<uint8_t*>(object) + table_offset, 4)) return false;
  std::memcpy(&table, static_cast<uint8_t*>(object) + table_offset, 4);
  if (!table || !DetachMemory(reinterpret_cast<void*>(table), slot + 4, g_detach.module)) return false;
  std::memcpy(&function, reinterpret_cast<void*>(table + slot), 4);
  return function && (!expected || function == DetachAddress(expected)) &&
      DetachMemory(reinterpret_cast<void*>(function), 1, g_detach.module, true);
}
bool DetachObservers(void* analyzer, UnrealArrayView* array) {
  uint32_t offset = 0;
  return UnrealObjectIsA(analyzer, L"TnTacticalAnalyzer") &&
      UnrealReadDelegateArrayProperty(analyzer, L"NewEnemyObservers", array, &offset) &&
      offset == 0x34 && array->count >= 0 && array->count <= static_cast<int32_t>(kMaxObservers) &&
      array->capacity >= array->count && array->capacity <= 4096 &&
      DetachMemory(static_cast<uint8_t*>(analyzer) + 0x34, sizeof(*array), nullptr, false, true) &&
      (!array->capacity || DetachMemory(array->data,
          static_cast<size_t>(array->capacity) * sizeof(UnrealScriptDelegate), nullptr, false, true));
}
bool CaptureDetach(void* controller, void* pawn, bool player, DetachCapture* capture) {
  if (!capture || g_detach_busy || !g_detach.module || GetCurrentThreadId() != g_detach.thread ||
      !DetachActor(controller, player ? L"TnPlayerController" : L"TnAiController") ||
      !DetachActor(pawn, L"TnPawn") || controller == pawn) return false;
  void *value = nullptr, *world = nullptr, *pawn_world = nullptr;
  uint8_t mode = 0xFF;
  const bool physical_player = UnrealObjectIsA(pawn, L"TnPlayerPawn");
  const bool physical_ai = UnrealObjectIsA(pawn, L"TnAiPawn");
  if (physical_player == physical_ai ||
      !DetachMemory(controller, player ? 0x700 : 0x500, nullptr, false, true) ||
      !DetachMemory(pawn, physical_player ? 0x8CC : 0x954, nullptr, false, true) ||
      !DetachField(controller, L"Pawn", 0x1C8, &value) || value != pawn ||
      !DetachField(pawn, L"Controller", 0x1D4, &value) || value != controller ||
      !DetachField(controller, L"WorldInfo", 0x98, &world) ||
      !DetachField(pawn, L"WorldInfo", 0x98, &pawn_world) || pawn_world != world ||
      !UnrealObjectIsA(world, L"WorldInfo") || !UnrealObjectIndex(world) ||
      !DetachByte(world, L"NetMode", 0x39C, &mode) || mode != 0 ||
      !DetachMemory(world, 0x39D) ||
      !DetachField(pawn, L"PlayerReplicationInfo", 0x3C0, &value) ||
      !DetachField(pawn, L"DrivenVehicle", 0x404, &value) || value ||
      !UnrealReadObjectProperty(pawn, L"Owner", &value) || value != controller ||
      !UnrealReadObjectProperty(pawn, L"UseableInteract", &value) || value ||
      !DetachVirtual(pawn, 0x10, 0x224, physical_player ?
          native_detach_code::kPlayerPawnVa : native_detach_code::kAiPawnVa)) return false;
  // Use the actual pawn callback, not a base-class fallback. Derived/current
  // state overrides are admitted only when they resolve to these audited bodies.
  if (physical_player && !DetachVirtual(pawn, 0x10, 0x344, 0x10BA1D60u)) return false;
  *capture = {};
  capture->controller = controller; capture->pawn = pawn; capture->world = world;
  capture->controller_index = UnrealObjectIndex(controller);
  capture->pawn_index = UnrealObjectIndex(pawn); capture->world_index = UnrealObjectIndex(world);
  capture->player = player;
  if (player) {
    uint8_t interaction = 0xFF;
    bool deleting = true;
    if (!UnrealReadObjectProperty(controller, L"PlayerCamera", &capture->camera) ||
        !UnrealObjectIsA(capture->camera, L"Camera") ||
        !(capture->camera_index = UnrealObjectIndex(capture->camera)) ||
        !UnrealReadBoolProperty(capture->camera, L"bDeleteMe", &deleting) || deleting ||
        !UnrealReadObjectProperty(capture->camera, L"WorldInfo", &value) || value != world) return false;
    return DetachVirtual(controller, 0, 0x368) &&
        DetachByte(controller, L"PlayerInteractState", 0x630, &interaction) && interaction == 0 &&
        UnrealReadObjectProperty(controller, L"UseableInteract", &value) && !value &&
        DetachField(controller, L"SavedMoves", 0x348, &value) &&
        DetachField(controller, L"PendingMove", 0x350, &value);
  }
  if (!DetachField(controller, L"TacticalAnalyzer", 0x2F4, &capture->analyzer) ||
      !UnrealObjectIndex(capture->analyzer) ||
      !UnrealReadObjectProperty(capture->analyzer, L"Owner", &value) || value != controller) return false;
  capture->analyzer_index = UnrealObjectIndex(capture->analyzer);
  UnrealArrayView observers;
  if (!DetachObservers(capture->analyzer, &observers)) return false;
  const auto* name = reinterpret_cast<const uint32_t*>(DetachAddress(0x12B31C68u));
  if (!DetachMemory(name, 4, g_detach.module) || !*name) return false;
  capture->observer_count = observers.count;
  if (observers.count) std::memcpy(capture->observers.data(), observers.data,
      static_cast<size_t>(observers.count) * sizeof(UnrealScriptDelegate));
  for (int32_t i = 0; i < observers.count; ++i) {
    const auto& delegate = capture->observers[i];
    // Native Find compares the first two words, but all four words must agree
    // with this controller's real native OnNewEnemy binding before removal.
    if (delegate.object != controller || delegate.function_name != *name) continue;
    if (capture->own_observer != -1 ||
        reinterpret_cast<uintptr_t>(delegate.compiled_context) != reinterpret_cast<uintptr_t>(controller) + 0x10 ||
        reinterpret_cast<uintptr_t>(delegate.compiled_function) != DetachAddress(0x10BB84E0u)) return false;
    capture->own_observer = i;
  }
  // An already absent observer is legal on a detached/rebound proxy lifecycle;
  // never fabricate it or remove another observer with a similar callback.
  return true;
}
bool VerifyDetached(const DetachCapture& capture) {
  void* value = nullptr;
  uint8_t mode = 0xFF;
  if (!DetachActor(capture.controller, capture.player ? L"TnPlayerController" : L"TnAiController") ||
      !DetachActor(capture.pawn, L"TnPawn") ||
      UnrealObjectIndex(capture.controller) != capture.controller_index ||
      UnrealObjectIndex(capture.pawn) != capture.pawn_index ||
      UnrealObjectIndex(capture.world) != capture.world_index ||
      !DetachByte(capture.world, L"NetMode", 0x39C, &mode) || mode != 0 ||
      !DetachField(capture.controller, L"WorldInfo", 0x98, &value) || value != capture.world ||
      !DetachField(capture.pawn, L"WorldInfo", 0x98, &value) || value != capture.world ||
      !DetachField(capture.controller, L"Pawn", 0x1C8, &value) || value ||
      !DetachField(capture.pawn, L"Controller", 0x1D4, &value) || value ||
      !DetachField(capture.pawn, L"PlayerReplicationInfo", 0x3C0, &value) || value ||
      !UnrealReadObjectProperty(capture.pawn, L"Owner", &value) || value ||
      !UnrealReadObjectProperty(capture.pawn, L"UseableInteract", &value) || value) return false;
  if (capture.player) {
    uint8_t role = 0, interaction = 0xFF;
    bool deleting = true;
    return DetachByte(capture.pawn, L"RemoteRole", 0x65, &role) && role == 1 &&
        UnrealReadObjectProperty(capture.controller, L"PlayerCamera", &value) && value == capture.camera &&
        UnrealObjectIndex(capture.camera) == capture.camera_index &&
        UnrealReadBoolProperty(capture.camera, L"bDeleteMe", &deleting) && !deleting &&
        UnrealReadObjectProperty(capture.camera, L"WorldInfo", &value) && value == capture.world &&
        DetachByte(capture.controller, L"PlayerInteractState", 0x630, &interaction) && interaction == 0 &&
        UnrealReadObjectProperty(capture.controller, L"UseableInteract", &value) && !value &&
        DetachField(capture.controller, L"SavedMoves", 0x348, &value) && !value &&
        DetachField(capture.controller, L"PendingMove", 0x350, &value) && !value;
  }
  UnrealArrayView observers;
  if (UnrealObjectIndex(capture.analyzer) != capture.analyzer_index ||
      !DetachField(capture.controller, L"TacticalAnalyzer", 0x2F4, &value) || value != capture.analyzer ||
      !UnrealReadObjectProperty(capture.analyzer, L"Owner", &value) || value != capture.controller ||
      !DetachObservers(capture.analyzer, &observers) ||
      observers.count != capture.observer_count - (capture.own_observer >= 0 ? 1 : 0)) return false;
  int32_t next = 0;
  for (int32_t i = 0; i < capture.observer_count; ++i) {
    if (i == capture.own_observer) continue;
    if (std::memcmp(&capture.observers[i], static_cast<UnrealScriptDelegate*>(observers.data) + next++,
                    sizeof(UnrealScriptDelegate))) return false;
  }
  return true;
}
NativeDetachResult Detach(void* controller, void* pawn, bool player) {
  DetachCapture capture;
  if (!CaptureDetach(controller, pawn, player, &capture)) return NativeDetachResult::kRejected;
  g_detach_busy = true;
  struct ClearBusy { ~ClearBusy() { g_detach_busy = false; } } guard;
  (player ? g_detach.player : g_detach.ai)(reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(controller) + kNativeScriptThisOffset), nullptr);
  // Once native code executes, ALL postcondition failures retain rollback
  // responsibility: even a failed observer removal may already detach the pawn.
  return VerifyDetached(capture) ? NativeDetachResult::kApplied : NativeDetachResult::kStateMismatch;
}
}  // namespace

bool PrepareNativeControllerDetach(HMODULE module, DWORD thread, void* player, void* ai) {
  if (!module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_detach.module && !BindDetachImage(module, thread, &g_detach)) {
    Log(LogLevel::kError, "[SquadSwitch] native controller-detach signatures mismatch");
    return false;
  }
  void* pawn = nullptr;
  DetachCapture capture;
  return g_detach.module == module && g_detach.thread == thread &&
      UnrealReadObjectProperty(player, L"Pawn", &pawn) && CaptureDetach(player, pawn, true, &capture) &&
      UnrealReadObjectProperty(ai, L"Pawn", &pawn) && CaptureDetach(ai, pawn, false, &capture);
}
NativeDetachResult NativeDetachPlayer(void* controller, void* pawn) { return Detach(controller, pawn, true); }
NativeDetachResult NativeDetachAi(void* controller, void* pawn) { return Detach(controller, pawn, false); }
}  // namespace wfcds
