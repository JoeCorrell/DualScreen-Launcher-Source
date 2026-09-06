#include "NativePlayerServices.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include "Log.h"
#include "NativeReferenceCode.h"
#include "NativeScriptAbi.h"
#include "NativeAbilitySkillCode.h"
#include "NativeAbilityResourceCode.h"
#include "NativeAbilityHud.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {
constexpr uintptr_t kFineAimVa = 0x10A93E00u;
constexpr uintptr_t kPickupVa = 0x10B11C30u;
// Full native bodies; the compiler's parameter order is NOT declaration order.
// OnSetPawn's calls at 10B21F55/79 independently confirm both ABIs.
constexpr uint8_t kFineAimCode[] = {
    0x55,0x8B,0xEC,0x83,0xE4,0xF8,0x83,0xC0,0xF0,0x50,0x89,0x42,0x24,0xE8,0x0E,0x8A,
    0xC6,0x00,0x8B,0x45,0x0C,0x83,0xC4,0x04,0x83,0xC0,0xF0,0x89,0x42,0x20,0x74,0x21,
    0x83,0x3D,0x7C,0x04,0xB1,0x12,0x03,0x7F,0x18,0x8B,0x40,0x08,0x25,0x00,0x00,0x80,
    0x00,0x33,0xC9,0x0B,0xC1,0x74,0x0A,0xC7,0x05,0x74,0x04,0xB1,0x12,0x01,0x00,0x00,
    0x00,0x8B,0x45,0x08,0x83,0xC0,0xF0,0x89,0x42,0x1C,0x74,0x21,0x83,0x3D,0x7C,0x04,
    0xB1,0x12,0x03,0x7F,0x18,0x8B,0x40,0x08,0x25,0x00,0x00,0x80,0x00,0x33,0xC9,0x0B,
    0xC1,0x74,0x0A,0xC7,0x05,0x74,0x04,0xB1,0x12,0x01,0x00,0x00,0x00,0x8B,0xE5,0x5D,
    0xC2,0x08,0x00,
};
constexpr uint8_t kPickupCode[] = {
    0x55,0x8B,0xEC,0x83,0xE4,0xF8,0x83,0xC0,0xF0,0x50,0x89,0x42,0x28,0xE8,0xDE,0xAB,
    0xBE,0x00,0x8B,0x45,0x08,0x83,0xC4,0x04,0x83,0xC0,0xF0,0x89,0x42,0x30,0x74,0x21,
    0x83,0x3D,0x7C,0x04,0xB1,0x12,0x03,0x7F,0x18,0x8B,0x40,0x08,0x25,0x00,0x00,0x80,
    0x00,0x33,0xC9,0x0B,0xC1,0x74,0x0A,0xC7,0x05,0x74,0x04,0xB1,0x12,0x01,0x00,0x00,
    0x00,0x8B,0xE5,0x5D,0xC2,0x04,0x00,
};
using FineAimCall = void (__attribute__((regparm(2), stdcall)) *)(
    void* pawn, void* manager, void* targeting, void* camera);
using PickupCall = void (__attribute__((regparm(2), stdcall)) *)(
    void* pawn, void* manager, void* commander);
struct PlayerServiceBindings {
  HMODULE module = nullptr;
  DWORD thread = 0;
  FineAimCall fine_aim = nullptr;
  PickupCall pickup = nullptr;
};
PlayerServiceBindings g_player_services;
thread_local bool g_player_services_busy = false;

bool ServiceMemory(const void* p, size_t bytes, HMODULE module = nullptr, bool executable = false, bool writable = false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p, &info, sizeof(info)) || info.State != MEM_COMMIT ||
      (module && info.AllocationBase != module) || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
      bytes > info.RegionSize - (reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(info.BaseAddress)))
    return false;
  const DWORD protection = info.Protect & 0xFFu;
  if (writable && protection != PAGE_READWRITE && protection != PAGE_WRITECOPY &&
      protection != PAGE_EXECUTE_READWRITE && protection != PAGE_EXECUTE_WRITECOPY) return false;
  return !executable || protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
      protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
}

bool BindPlayerServiceImage(HMODULE module, DWORD thread, PlayerServiceBindings* output) {
  if (!module || !thread || thread != GetCurrentThreadId() || !output) return false;
  const uintptr_t image = reinterpret_cast<uintptr_t>(module);
  const auto match = [&](uintptr_t va, const auto& bytes) {
    const auto* address = reinterpret_cast<const void*>(image + va - native_reference_code::kImageBase);
    return ServiceMemory(address, bytes.size(), module, true) &&
        !std::memcmp(address, bytes.data(), bytes.size());
  };
  if (!match(kFineAimVa, native_reference_code::Relocate(kFineAimCode, image, {34, 57, 78, 101})) ||
      !match(kPickupVa, native_reference_code::Relocate(kPickupCode, image, {34, 57})) ||
      !match(native_reference_code::kBarrierVa,
             native_reference_code::Relocate(native_reference_code::kBarrier, image, {15, 38}))) return false;
  *output = {module, thread,
      reinterpret_cast<FineAimCall>(image + kFineAimVa - native_reference_code::kImageBase),
      reinterpret_cast<PickupCall>(image + kPickupVa - native_reference_code::kImageBase)};
  return true;
}

void* ServiceScript(void* object) {
  // Compiled None is 0x10, not nullptr. Integer adjustment also avoids C++
  // pointer arithmetic on nullptr for the optional contextual commander.
  return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(object) + kNativeScriptThisOffset);
}
bool ServiceField(void* object, const wchar_t* name, uint32_t offset, void** value) {
  uint32_t actual = 0;
  return UnrealResolveObjectPropertyOffset(object, name, &actual) && actual == offset &&
      UnrealReadObjectProperty(object, name, value);
}
bool ServiceActor(void* object, const wchar_t* type) {
  bool deleting = true;
  return UnrealObjectIsA(object, type) &&
      UnrealReadBoolProperty(object, L"bDeleteMe", &deleting) && !deleting;
}
bool ServicePlayerPair(void* pawn, void** player) {
  void* controlled = nullptr;
  void* pawn_world = nullptr;
  void* player_world = nullptr;
  uint8_t mode = 0xFF, player_role=0, pawn_role=0;
  return g_player_services.module && GetCurrentThreadId() == g_player_services.thread &&
      ServiceActor(pawn, L"TnPawn") &&
      ServiceField(pawn, L"Controller", 0x1D4, player) &&
      ServiceActor(*player, L"TnPlayerController") &&
      ServiceField(*player, L"Pawn", 0x1C8, &controlled) && controlled == pawn &&
      ServiceField(*player, L"AcknowledgedPawn", 0x300, &controlled) && controlled == pawn &&
      ServiceField(pawn, L"WorldInfo", 0x98, &pawn_world) &&
      ServiceField(*player, L"WorldInfo", 0x98, &player_world) && pawn_world == player_world &&
      ServiceActor(player_world, L"WorldInfo") &&
      UnrealReadByteProperty(*player, L"Role", &player_role) && player_role==3 &&
      UnrealReadByteProperty(pawn, L"Role", &pawn_role) && pawn_role==3 &&
      UnrealReadByteProperty(player_world, L"NetMode", &mode) && mode == 0;
}
// A native setter can report the expected fields while a nested callback has
// invalidated its controller, world or UObject identity. Capture all operands
// before execution and reject that dirty result; do not continue the handoff.
struct ServiceCapture {
  std::array<void*,8> objects{}; // player, controlled pawn, world, service operands
  std::array<uint32_t,8> indices{};
};
bool CaptureServices(void* pawn,std::initializer_list<void*> operands,ServiceCapture* capture) {
  if (!capture || operands.size()>5) return false;
  *capture={}; capture->objects[1]=pawn;
  if (!ServicePlayerPair(pawn,&capture->objects[0]) ||
      !ServiceField(pawn,L"WorldInfo",0x98,&capture->objects[2])) return false;
  size_t i=3; for (auto* object:operands) capture->objects[i++]=object;
  for (i=0;i<capture->objects.size();++i) {
    capture->indices[i]=UnrealObjectIndex(capture->objects[i]);
    if (capture->objects[i] && !capture->indices[i]) return false;
  }
  return true;
}
bool ServicesCurrent(const ServiceCapture& capture) {
  void* player=nullptr; void* world=nullptr;
  if (!ServicePlayerPair(capture.objects[1],&player) || player!=capture.objects[0] ||
      !ServiceField(player,L"WorldInfo",0x98,&world) || world!=capture.objects[2]) return false;
  for (size_t i=0;i<capture.objects.size();++i)
    if (UnrealObjectIndex(capture.objects[i])!=capture.indices[i]) return false;
  return true;
}
bool FineAimFields(void* manager) {
  void* value = nullptr;
  return UnrealObjectIsA(manager, L"TnFineAimManager") &&
      ServiceField(manager, L"Owner", 0x34, &value) &&
      ServiceField(manager, L"PlayerTargeting", 0x2C, &value) &&
      ServiceField(manager, L"CameraStrategyManager", 0x30, &value);
}
bool PickupFields(void* manager) {
  void* value = nullptr;
  return UnrealObjectIsA(manager, L"TnPickupManager") &&
      ServiceField(manager, L"OwnerPawn", 0x38, &value) &&
      ServiceField(manager, L"ContextualCommander", 0x40, &value);
}
bool SameServiceOwner(void* manager, const wchar_t* name, void* expected) {
  void* observed = nullptr;
  return UnrealReadObjectProperty(manager, name, &observed) && observed == expected;
}
}  // namespace

bool PrepareNativePlayerServices(HMODULE module, DWORD thread, void* player) {
  if (!module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_player_services.module && !BindPlayerServiceImage(module, thread, &g_player_services)) {
    Log(LogLevel::kError, "[SquadSwitch] native player-service signatures mismatch");
    return false;
  }
  void* pawn = nullptr;
  void* observed_player = nullptr;
  void* fine_aim = nullptr;
  void* pickup = nullptr;
  void* targeting = nullptr;
  void* camera = nullptr;
  void* commander = nullptr;
  return module == g_player_services.module && thread == g_player_services.thread &&
      ServiceField(player, L"Pawn", 0x1C8, &pawn) && ServicePlayerPair(pawn, &observed_player) &&
      player == observed_player &&
      ServiceField(player, L"_FineAimManager", 0x620, &fine_aim) && FineAimFields(fine_aim) &&
      ServiceField(player, L"_PickupManager", 0x690, &pickup) && PickupFields(pickup) &&
      ServiceField(player, L"PlayerTargeting", 0x608, &targeting) &&
      UnrealObjectIsA(targeting, L"TnPlayerTargeting") &&
      ServiceField(targeting, L"_TargetingOwner", 0x48, &observed_player) && observed_player == pawn &&
      UnrealObjectReferenceWritesReady() &&
      ServiceField(player, L"_CameraStrategyManager", 0x60C, &camera) &&
      UnrealObjectIsA(camera, L"TnCameraStrategyManager") &&
      ServiceField(player, L"ContextualCommander", 0x674, &commander) &&
      (!commander || UnrealObjectIsA(commander, L"TnContextualCommandHandler")) &&
      SameServiceOwner(fine_aim, L"Owner", pawn) &&
      SameServiceOwner(fine_aim, L"PlayerTargeting", targeting) &&
      SameServiceOwner(fine_aim, L"CameraStrategyManager", camera) &&
      SameServiceOwner(pickup, L"OwnerPawn", pawn) &&
      SameServiceOwner(pickup, L"ContextualCommander", commander);
}

NativePlayerServiceResult NativeInitializePickup(void* manager, void* pawn, void* commander) {
  void* player = nullptr;
  void* owned = nullptr;
  ServiceCapture capture;
  if (g_player_services_busy || !ServicePlayerPair(pawn, &player) || !PickupFields(manager) ||
      !ServiceMemory(manager,0x44,nullptr,false,true) || !g_player_services.pickup ||
      !ServiceField(player, L"_PickupManager", 0x690, &owned) || owned != manager ||
      !ServiceField(player, L"ContextualCommander", 0x674, &owned) || owned != commander ||
      (commander && !UnrealObjectIsA(commander, L"TnContextualCommandHandler")) ||
      !CaptureServices(pawn,{manager,commander},&capture)) return NativePlayerServiceResult::kRejected;
  g_player_services_busy = true;
  struct ClearBusy { ~ClearBusy() { g_player_services_busy = false; } } guard;
  g_player_services.pickup(ServiceScript(pawn), ServiceScript(manager), ServiceScript(commander));
  return ServicesCurrent(capture) &&
      SameServiceOwner(player,L"_PickupManager",manager) &&
      SameServiceOwner(player,L"ContextualCommander",commander) &&
      SameServiceOwner(manager, L"OwnerPawn", pawn) &&
      SameServiceOwner(manager, L"ContextualCommander", commander)
      ? NativePlayerServiceResult::kApplied : NativePlayerServiceResult::kStateMismatch;
}

NativePlayerServiceResult NativeInitializeFineAim(void* manager, void* pawn, void* targeting, void* camera) {
  void* player = nullptr;
  void* owned = nullptr;
  ServiceCapture capture;
  if (g_player_services_busy || !ServicePlayerPair(pawn, &player) || !FineAimFields(manager) ||
      !ServiceMemory(manager,0x38,nullptr,false,true) || !g_player_services.fine_aim ||
      !ServiceField(player, L"_FineAimManager", 0x620, &owned) || owned != manager ||
      !ServiceField(player, L"PlayerTargeting", 0x608, &owned) || owned != targeting ||
      !ServiceField(player, L"_CameraStrategyManager", 0x60C, &owned) || owned != camera ||
      !UnrealObjectIsA(targeting, L"TnPlayerTargeting") ||
      !UnrealObjectIsA(camera, L"TnCameraStrategyManager") ||
      !CaptureServices(pawn,{manager,targeting,camera},&capture)) return NativePlayerServiceResult::kRejected;
  g_player_services_busy = true;
  struct ClearBusy { ~ClearBusy() { g_player_services_busy = false; } } guard;
  g_player_services.fine_aim(ServiceScript(pawn), ServiceScript(manager), ServiceScript(targeting), ServiceScript(camera));
  return ServicesCurrent(capture) &&
      SameServiceOwner(player,L"_FineAimManager",manager) &&
      SameServiceOwner(player,L"PlayerTargeting",targeting) &&
      SameServiceOwner(player,L"_CameraStrategyManager",camera) &&
      SameServiceOwner(manager, L"Owner", pawn) &&
      SameServiceOwner(manager, L"PlayerTargeting", targeting) &&
      SameServiceOwner(manager, L"CameraStrategyManager", camera)
      ? NativePlayerServiceResult::kApplied : NativePlayerServiceResult::kStateMismatch;
}

NativePlayerServiceResult NativeSetTargetingOwner(void* targeting, void* pawn) {
  void* player = nullptr;
  void* owned = nullptr;
  void* old_owner = nullptr;
  ServiceCapture capture;
  if (g_player_services_busy || !ServicePlayerPair(pawn, &player) ||
      !UnrealObjectReferenceWritesReady() || !UnrealObjectIsA(targeting, L"TnPlayerTargeting") ||
      !ServiceField(player, L"PlayerTargeting", 0x608, &owned) || owned != targeting ||
      !ServiceField(targeting, L"_TargetingOwner", 0x48, &old_owner) ||
      !CaptureServices(pawn,{targeting},&capture)) return NativePlayerServiceResult::kRejected;
  // HM_Engine export 7939 has exactly one operation: _TargetingOwner = Value.
  // Compiled TnPlayerTargeting.Update reads the inherited +0x48 UObject field
  // at 10BA79B0/10BA7A72. This is not a replacement for its later typed-pawn
  // consumers: those remain behind the complete role-adapter admission gate.
  g_player_services_busy = true;
  struct ClearBusy { ~ClearBusy() { g_player_services_busy = false; } } guard;
  if (!UnrealWriteObjectPropertyExpected(targeting, L"_TargetingOwner", old_owner, pawn))
    return NativePlayerServiceResult::kRejected;
  return ServicesCurrent(capture) && SameServiceOwner(player,L"PlayerTargeting",targeting) &&
      SameServiceOwner(targeting, L"_TargetingOwner", pawn)
      ? NativePlayerServiceResult::kApplied : NativePlayerServiceResult::kStateMismatch;
}
#include "NativeBuffHud.inc"
#include "NativeAbilityState.inc"
#include "NativeAbilityBuffService.inc"
#include "NativeAbilityValues.inc"
#include "NativeAbilityRestore.inc"
}  // namespace wfcds
