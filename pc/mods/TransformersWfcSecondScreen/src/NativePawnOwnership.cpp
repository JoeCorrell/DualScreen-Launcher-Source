#include "NativePawnOwnership.h"
#include <array>
#include <cmath>
#include <cstring>
#include "Log.h"
#include "NativePawnOwnershipCode.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {
using PawnOwnershipCall = void (__attribute__((fastcall)) *)(void* pawn, void* unused_edx,
                                                            void* controller, uint32_t transition);
struct PawnOwnershipBindings { HMODULE module = nullptr; DWORD thread = 0; };
PawnOwnershipBindings g_pawn_ownership;
thread_local bool g_pawn_ownership_busy = false;
struct OwnershipCapture {
  void* pawn = nullptr;
  void* world = nullptr;
  void* inventory = nullptr;
  void* weapon = nullptr;
  void* frame = nullptr;
  void* state = nullptr;
  void* callback = nullptr;
  uint32_t pawn_index = 0, world_index = 0, inventory_index = 0, weapon_index = 0;
  bool physical_ai = false;
  std::array<uint8_t,0x30> pose{}; // Location, Rotation, Velocity, Acceleration.
};
uintptr_t OwnershipAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_pawn_ownership.module) + va - native_reference_code::kImageBase;
}
bool OwnershipMemory(const void* p, size_t size, HMODULE module = nullptr,
                      bool execute = false, bool write = false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase != module) ||
      size > info.RegionSize - (reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD prot = info.Protect & 0xFFu;
  if (write && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
      prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
      prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}
bool OwnershipPointer(const void* base, size_t offset, void** out) {
  if (!base || !out || !OwnershipMemory(static_cast<const uint8_t*>(base)+offset,4)) return false;
  std::memcpy(out,static_cast<const uint8_t*>(base)+offset,4); return true;
}
bool OwnershipField(void* object,const wchar_t* name,uint32_t offset,void** out) {
  uint32_t actual = 0;
  return UnrealResolveObjectPropertyOffset(object,name,&actual) && actual == offset &&
      UnrealReadObjectProperty(object,name,out);
}
bool OwnershipByte(void* object,const wchar_t* name,uint32_t offset,uint8_t* out) {
  uint32_t actual = 0;
  return UnrealResolveBytePropertyOffset(object,name,&actual) && actual == offset &&
      UnrealReadByteProperty(object,name,out);
}
bool OwnershipActor(void* object,const wchar_t* type) {
  bool deleting = true; uint8_t role = 0;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting &&
      OwnershipByte(object,L"Role",0x66,&role) && role == 3;
}
bool OwnershipVirtual(void* object,size_t slot,uintptr_t va,void** callback = nullptr) {
  void* table = nullptr; void* method = nullptr;
  if (!OwnershipPointer(object,0x10,&table) || !OwnershipMemory(table,slot+4,g_pawn_ownership.module) ||
      !OwnershipPointer(table,slot,&method) || reinterpret_cast<uintptr_t>(method) != OwnershipAddress(va) ||
      !OwnershipMemory(method,1,g_pawn_ownership.module,true)) return false;
  if (callback) *callback = method;
  return true;
}
bool BindPawnOwnershipImage(HMODULE module,DWORD thread,PawnOwnershipBindings* out) {
  if (!module || !out || !thread || thread != GetCurrentThreadId()) return false;
  const auto image = reinterpret_cast<uintptr_t>(module);
  if (!native_pawn_ownership_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes = native_reference_code::Relocate(code,image,operands);
    const auto* address = reinterpret_cast<const void*>(image+va-native_reference_code::kImageBase);
    return OwnershipMemory(address,bytes.size(),module,true) && !std::memcmp(address,bytes.data(),bytes.size());
  })) return false;
  *out = {module,thread}; return true;
}
bool OwnershipCurrentWorld(void* expected) {
  void *engine = nullptr, *context = nullptr, *world_slot = nullptr, *world = nullptr;
  return OwnershipPointer(reinterpret_cast<void*>(OwnershipAddress(0x12B10A9Cu)),0,&engine) &&
      OwnershipPointer(engine,0x40,&context) && OwnershipPointer(context,0x28,&world_slot) &&
      OwnershipPointer(world_slot,0,&world) && world == expected;
}
bool OwnershipWeaponState(void* weapon,void** frame,void** state) {
  *state = nullptr;
  if (!OwnershipPointer(weapon,0x28,frame)) return false;
  // Native IsInState returns false for an absent frame/state. That already
  // reaches the same-equipped early return; do not invent a state or reject it.
  if (!*frame) return true;
  if (!OwnershipMemory(*frame,0x3C) || !OwnershipPointer(*frame,0x38,state)) return false;
  if (!*state) return true;
  const auto* inactive = reinterpret_cast<const uint32_t*>(OwnershipAddress(0x12B3122Cu));
  if (!OwnershipMemory(inactive,4,g_pawn_ownership.module) || !*inactive) return false;
  std::array<void*,32> visited{};
  void* current = *state;
  for (size_t i = 0; current; ++i) {
    if (i == visited.size() || !UnrealObjectIsA(current,L"State") ||
        !OwnershipMemory(current,0x2C)) return false;
    for (size_t j = 0; j < i; ++j) if (visited[j] == current) return false;
    visited[i] = current;
    uint32_t name = 0; std::memcpy(&name,static_cast<uint8_t*>(current)+4,4);
    // Match the shipping IsInState ancestry test, not just the current name.
    if (name == *inactive || !OwnershipPointer(current,0x28,&current)) return false;
  }
  return true;
}
bool CapturePawnOwnership(void* pawn,OwnershipCapture* out,bool post_call = false) {
  if (!out || (g_pawn_ownership_busy && !post_call) || !g_pawn_ownership.module ||
      GetCurrentThreadId() != g_pawn_ownership.thread || !OwnershipActor(pawn,L"TnPawn")) return false;
  *out = {};
  out->pawn = pawn; out->pawn_index = UnrealObjectIndex(pawn);
  const bool physical_player = UnrealObjectIsA(pawn,L"TnPlayerPawn");
  out->physical_ai = UnrealObjectIsA(pawn,L"TnAiPawn");
  void* value = nullptr; uint8_t mode = 0xFF;
  if (physical_player == out->physical_ai ||
      !OwnershipMemory(pawn,out->physical_ai ? 0x954 : 0x8CC,nullptr,false,true) ||
      !OwnershipVirtual(pawn,0x220,out->physical_ai ? native_pawn_ownership_code::kAiPawnVa :
          native_pawn_ownership_code::kPawnVa,&out->callback) ||
      !OwnershipField(pawn,L"WorldInfo",0x98,&out->world) ||
      !UnrealObjectIsA(out->world,L"WorldInfo") || !(out->world_index = UnrealObjectIndex(out->world)) ||
      !OwnershipByte(out->world,L"NetMode",0x39C,&mode) || mode != 0 ||
      !OwnershipField(pawn,L"Controller",0x1D4,&value) ||
      !OwnershipField(pawn,L"PlayerReplicationInfo",0x3C0,&value) ||
      !OwnershipField(pawn,L"DrivenVehicle",0x404,&value) || value ||
      !UnrealReadObjectProperty(pawn,L"UseableInteract",&value) || value ||
      (out->physical_ai && !OwnershipField(pawn,L"_BoundController",0x87C,&value)) ||
      !OwnershipField(pawn,L"InvManager",0x424,&out->inventory) ||
      !UnrealObjectIsA(out->inventory,L"TnInventoryManager") ||
      !(out->inventory_index = UnrealObjectIndex(out->inventory)) ||
      !OwnershipField(out->inventory,L"Instigator",0x94,&value) || value != pawn ||
      !OwnershipField(out->inventory,L"PendingWeapon",0x1CC,&value) || value ||
      !OwnershipField(pawn,L"Weapon",0x428,&out->weapon)) return false;
  std::memcpy(out->pose.data(),static_cast<uint8_t*>(pawn)+0xD8,out->pose.size());
  if (!out->weapon) return true;
  float timestamp = 0, last_timestamp = 0, now = 0;
  out->weapon_index = UnrealObjectIndex(out->weapon);
  return OwnershipActor(out->weapon,L"TnWeapon") && out->weapon_index &&
      !UnrealObjectIsA(out->weapon,L"TnWeaponBombBase") && !UnrealObjectIsA(out->weapon,L"TnWeaponFlagBase") &&
      OwnershipMemory(out->weapon,0x28C,nullptr,false,true) &&
      OwnershipMemory(out->inventory,0x204,nullptr,false,true) &&
      OwnershipField(out->weapon,L"Instigator",0x94,&value) && value == pawn &&
      OwnershipField(out->weapon,L"InvManager",0x1CC,&value) && value == out->inventory &&
      OwnershipVirtual(out->inventory,0x228,native_pawn_ownership_code::kInventoryVa) &&
      OwnershipWeaponState(out->weapon,&out->frame,&out->state) &&
      UnrealReadFloatProperty(out->weapon,L"ClientWeaponSetTime",&timestamp) && std::isfinite(timestamp) &&
      UnrealReadFloatProperty(out->inventory,L"LastClientWeaponSetTime",&last_timestamp) && std::isfinite(last_timestamp) &&
      UnrealReadFloatProperty(out->world,L"TimeSeconds",&now) && std::isfinite(now) && now >= 0 &&
      OwnershipCurrentWorld(out->world);
}
bool VerifyPawnOwnership(const OwnershipCapture& before,void* controller,uint32_t controller_index,
                          void* pri,uint32_t pri_index,bool player_controller) {
  void* value = nullptr;
  OwnershipCapture after;
  // Keep the reentrancy barrier held throughout post-call inspection.
  const bool captured = CapturePawnOwnership(before.pawn,&after,true);
  if (!captured || !OwnershipActor(controller,player_controller ? L"TnPlayerController" : L"TnAiController") ||
      UnrealObjectIndex(controller) != controller_index ||
      after.pawn_index != before.pawn_index || after.world != before.world || after.world_index != before.world_index ||
      after.inventory != before.inventory || after.inventory_index != before.inventory_index ||
      after.weapon != before.weapon || after.weapon_index != before.weapon_index ||
      after.frame != before.frame || after.state != before.state || after.pose != before.pose ||
      !OwnershipField(controller,L"WorldInfo",0x98,&value) || value != before.world ||
      !OwnershipField(controller,L"Pawn",0x1C8,&value) || value ||
      !OwnershipField(controller,L"PlayerReplicationInfo",0x1CC,&value) || value != pri ||
      UnrealObjectIndex(pri) != pri_index ||
      !OwnershipField(before.pawn,L"Controller",0x1D4,&value) || value != controller ||
      !UnrealReadObjectProperty(before.pawn,L"Owner",&value) || value != controller ||
      !OwnershipField(before.pawn,L"PlayerReplicationInfo",0x3C0,&value) || value != pri) return false;
  return !before.physical_ai || (OwnershipField(before.pawn,L"_BoundController",0x87C,&value) &&
      value == (UnrealObjectIsA(controller,L"TnAiController") ? controller : nullptr));
}
}  // namespace

bool PrepareNativePawnOwnership(HMODULE module,DWORD thread,void* first,void* second) {
  if (!module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_pawn_ownership.module && !BindPawnOwnershipImage(module,thread,&g_pawn_ownership)) {
    Log(LogLevel::kError,"[SquadSwitch] native pawn-ownership signatures mismatch"); return false;
  }
  OwnershipCapture capture;
  const bool first_ok = CapturePawnOwnership(first,&capture);
  const bool second_ok = CapturePawnOwnership(second,&capture);
  return module == g_pawn_ownership.module && thread == g_pawn_ownership.thread && first_ok && second_ok;
}
NativePawnOwnershipResult NativeBindPawnOwnership(void* pawn,void* controller) {
  OwnershipCapture capture; void *value = nullptr, *pri = nullptr;
  if (!CapturePawnOwnership(pawn,&capture) ||
      (!OwnershipActor(controller,L"TnPlayerController") && !OwnershipActor(controller,L"TnAiController")) ||
      !OwnershipMemory(controller,0x1D0,nullptr,false,true) ||
      !OwnershipField(controller,L"Pawn",0x1C8,&value) || value ||
      !OwnershipField(controller,L"WorldInfo",0x98,&value) || value != capture.world ||
      !OwnershipField(controller,L"PlayerReplicationInfo",0x1CC,&pri) ||
      (pri && !UnrealObjectIsA(pri,L"PlayerReplicationInfo")) ||
      !OwnershipField(pawn,L"Controller",0x1D4,&value) || value ||
      !UnrealReadObjectProperty(pawn,L"Owner",&value) || value ||
      !OwnershipField(pawn,L"PlayerReplicationInfo",0x3C0,&value) || value) return NativePawnOwnershipResult::kRejected;
  const uint32_t index = UnrealObjectIndex(controller);
  const uint32_t pri_index = UnrealObjectIndex(pri);
  const bool player_controller = UnrealObjectIsA(controller,L"TnPlayerController");
  g_pawn_ownership_busy = true;
  struct ClearBusy { ~ClearBusy() { g_pawn_ownership_busy = false; } } guard;
  reinterpret_cast<PawnOwnershipCall>(capture.callback)(
      static_cast<uint8_t*>(pawn)+kNativeScriptThisOffset,nullptr,
      static_cast<uint8_t*>(controller)+kNativeScriptThisOffset,0);
  // Full Possess, Restart, ClientRestart and spawn/touch events never run.
  // In the admitted weapon branch, only native selection timestamps can change;
  // it returns before SetCurrentWeapon or state transition. No temporary weapon
  // pointer removal or copied inventory is used to force this branch.
  return VerifyPawnOwnership(capture,controller,index,pri,pri_index,player_controller) ? NativePawnOwnershipResult::kApplied
                                                         : NativePawnOwnershipResult::kStateMismatch;
}
}  // namespace wfcds
