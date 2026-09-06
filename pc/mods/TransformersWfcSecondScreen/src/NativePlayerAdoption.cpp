#include "NativePlayerAdoption.h"
#include <array>
#include <cmath>
#include <cstring>
#include <utility>
#include "Log.h"
#include "NativePawnOwnership.h"
#include "NativePlayerAdoptionCode.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void __cdecl WfcCallEdiObject(void* target, void* receiver, void* argument);
namespace {
struct PlayerAdoptionBindings { HMODULE module = nullptr; DWORD thread = 0; };
PlayerAdoptionBindings g_player_adoption;
thread_local bool g_player_adoption_busy = false;
struct AdoptionPlayer {
  void *world = nullptr, *local = nullptr, *pri = nullptr, *camera = nullptr;
  uint32_t index = 0, world_index = 0, local_index = 0, pri_index = 0, camera_index = 0;
};
struct AdoptionPawn {
  void* blueprint = nullptr;
  uint32_t index = 0, blueprint_index = 0;
  std::array<uint8_t,0x30> pose{};
};
uintptr_t AdoptionAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_player_adoption.module) + va - native_reference_code::kImageBase;
}
bool AdoptionMemory(const void* p,size_t size,HMODULE module = nullptr,bool execute = false,bool write = false) {
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
bool BindPlayerAdoptionImage(HMODULE module,DWORD thread,PlayerAdoptionBindings* out) {
  if (!module || !out || !thread || thread != GetCurrentThreadId()) return false;
  const uintptr_t base = reinterpret_cast<uintptr_t>(module);
  if (!native_player_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes = native_reference_code::Relocate(code,base,operands);
    const void* address = reinterpret_cast<void*>(base+va-native_reference_code::kImageBase);
    return AdoptionMemory(address,bytes.size(),module,true) && !std::memcmp(address,bytes.data(),bytes.size());
  })) return false;
  // Complete crouched eye-height arithmetic uses these native double constants.
  for (const auto& constant : {std::pair<uintptr_t,double>{0x129D2300u,10.0},
                               {0x129D2560u,static_cast<double>(0.8f)}}) {
    const void* address = reinterpret_cast<void*>(base+constant.first-native_reference_code::kImageBase);
    if (!AdoptionMemory(address,8,module) || std::memcmp(address,&constant.second,8)) return false;
  }
  *out = {module,thread}; return true;
}
bool AdoptionField(void* object,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset = 0;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadObjectProperty(object,name,out);
}
bool AdoptionFloat(void* object,const wchar_t* name,uint32_t expected,float* out) {
  uint32_t offset = 0;
  return UnrealResolveFloatPropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadFloatProperty(object,name,out) && std::isfinite(*out);
}
bool AdoptionByte(void* object,const wchar_t* name,uint32_t expected,uint8_t* out) {
  uint32_t offset = 0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadByteProperty(object,name,out);
}
bool AdoptionLive(void* object,const wchar_t* type) {
  bool deleting = true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool AdoptionFlag(void* object,const wchar_t* name,uint32_t offset,uint32_t mask,bool* out) {
  uint32_t value = 0;
  if (!UnrealReadBoolProperty(object,name,out) || !AdoptionMemory(static_cast<uint8_t*>(object)+offset,4)) return false;
  std::memcpy(&value,static_cast<uint8_t*>(object)+offset,4);
  return *out == ((value & mask) != 0);
}
bool AdoptionVirtual(void* pawn,size_t slot,uintptr_t expected) {
  void *table = nullptr, *method = nullptr;
  if (!AdoptionMemory(pawn,0x14)) return false;
  std::memcpy(&table,static_cast<uint8_t*>(pawn)+0x10,4);
  if (!AdoptionMemory(table,slot+4,g_player_adoption.module)) return false;
  std::memcpy(&method,static_cast<uint8_t*>(table)+slot,4);
  return reinterpret_cast<uintptr_t>(method) == AdoptionAddress(expected) &&
      AdoptionMemory(method,1,g_player_adoption.module,true);
}
bool CaptureAdoptionPlayer(void* controller,AdoptionPlayer* out) {
  uint8_t role = 0, mode = 0xFF; bool spectator = true; void* value = nullptr;
  if (!out || !AdoptionLive(controller,L"TnPlayerController") ||
      !AdoptionMemory(controller,0x334,nullptr,false,true) ||
      !AdoptionByte(controller,L"Role",0x66,&role) || role != 3 ||
      !AdoptionField(controller,L"WorldInfo",0x98,&out->world) || !AdoptionLive(out->world,L"WorldInfo") ||
      !AdoptionByte(out->world,L"NetMode",0x39C,&mode) || mode != 0 ||
      !AdoptionField(controller,L"Player",0x2D0,&out->local) || !UnrealObjectIsA(out->local,L"LocalPlayer") ||
      !AdoptionField(controller,L"PlayerReplicationInfo",0x1CC,&out->pri) ||
      !AdoptionLive(out->pri,L"PlayerReplicationInfo") ||
      !AdoptionFlag(out->pri,L"bOnlySpectator",0x204,4,&spectator) || spectator ||
      !AdoptionField(controller,L"PlayerCamera",0x2D4,&out->camera) || !AdoptionLive(out->camera,L"Camera") ||
      !AdoptionField(out->camera,L"WorldInfo",0x98,&value) || value != out->world ||
      !AdoptionField(controller,L"AcknowledgedPawn",0x300,&value) ||
      (value && !AdoptionLive(value,L"TnPawn"))) return false;
  out->index = UnrealObjectIndex(controller); out->world_index = UnrealObjectIndex(out->world);
  out->local_index = UnrealObjectIndex(out->local); out->pri_index = UnrealObjectIndex(out->pri);
  out->camera_index = UnrealObjectIndex(out->camera);
  return out->local_index && out->pri_index && out->camera_index;
}
bool CaptureAdoptionPawn(void* pawn,void* world,AdoptionPawn* out) {
  uint8_t role = 0; bool stasis = true, tear_off = true, crouched = false;
  void* value = nullptr; float eye = 0, crouch = 0;
  if (!out || !AdoptionLive(pawn,L"TnPawn") ||
      !AdoptionMemory(pawn,0x57C,nullptr,false,true) ||
      !AdoptionByte(pawn,L"Role",0x66,&role) || role != 3 ||
      !AdoptionField(pawn,L"WorldInfo",0x98,&value) || value != world ||
      !AdoptionFlag(pawn,L"bStasis",0x78,0x200,&stasis) || stasis ||
      !AdoptionFlag(pawn,L"bTearOff",0x7C,4,&tear_off) || tear_off ||
      !AdoptionFlag(pawn,L"bIsCrouched",0x1E8,8,&crouched) ||
      !AdoptionFloat(pawn,L"CrouchHeight",0x1F4,&crouch) || crouch < 0 ||
      !AdoptionFloat(pawn,L"BaseEyeHeight",0x32C,&eye) ||
      !AdoptionFloat(pawn,L"EyeHeight",0x330,&eye) ||
      !AdoptionVirtual(pawn,0x204,native_player_adoption_code::kEyeHeightVa) ||
      !AdoptionVirtual(pawn,0x200,native_player_adoption_code::kOriginalEyeHeightVa) ||
      !AdoptionField(pawn,L"_Blueprint",0x578,&out->blueprint) ||
      !UnrealObjectIsA(out->blueprint,L"TnTransformer") ||
      !AdoptionFloat(out->blueprint,L"BaseEyeHeight",0xB4,&eye)) return false;
  // Live configured characters have their own blueprint. Do not enter the
  // class-default fallback with a missing or partially loaded character.
  out->index = UnrealObjectIndex(pawn); out->blueprint_index = UnrealObjectIndex(out->blueprint);
  std::memcpy(out->pose.data(),static_cast<uint8_t*>(pawn)+0xD8,out->pose.size());
  return out->blueprint_index != 0;
}
bool AdoptionInputHeld(void* controller,const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return move.controller == controller && look.controller == controller &&
      move.axis == NativeInputAxis::kMove && look.axis == NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look);
}
bool SameAdoptionPlayer(const AdoptionPlayer& a,const AdoptionPlayer& b) {
  return a.index == b.index && a.world == b.world && a.world_index == b.world_index &&
      a.local == b.local && a.local_index == b.local_index && a.pri == b.pri && a.pri_index == b.pri_index &&
      a.camera == b.camera && a.camera_index == b.camera_index;
}
bool SameAdoptionPawn(const AdoptionPawn& a,const AdoptionPawn& b) {
  return a.index == b.index && a.blueprint == b.blueprint && a.blueprint_index == b.blueprint_index && a.pose == b.pose;
}
void CallAdoptionAcknowledge(void* controller,void* pawn) {
  // Shipping AcknowledgePossession has EDI script-this, one compiled-pawn
  // stack argument and RET4. Preserve the caller's EDI across the narrow call.
  WfcCallEdiObject(reinterpret_cast<void*>(AdoptionAddress(native_player_adoption_code::kAcknowledgeVa)),
      static_cast<uint8_t*>(controller)+0x10,static_cast<uint8_t*>(pawn)+0x10);
}
}  // namespace

bool PrepareNativePlayerAdoption(HMODULE module,DWORD thread,void* controller,void* first,void* second) {
  if (g_player_adoption_busy || !module || module != GetModuleHandleW(nullptr) ||
      !thread || thread != GetCurrentThreadId()) return false;
  if (!g_player_adoption.module && !BindPlayerAdoptionImage(module,thread,&g_player_adoption)) {
    Log(LogLevel::kError,"[SquadSwitch] native player-adoption signatures mismatch"); return false;
  }
  AdoptionPlayer player; AdoptionPawn pawn;
  return module == g_player_adoption.module && thread == g_player_adoption.thread &&
      UnrealObjectReferenceWritesReady() && CaptureAdoptionPlayer(controller,&player) &&
      CaptureAdoptionPawn(first,player.world,&pawn) && CaptureAdoptionPawn(second,player.world,&pawn);
}
NativePlayerAdoptionResult NativeAdoptExistingPlayerPawn(void* controller,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  AdoptionPlayer before; AdoptionPawn previous_pawn; void* value = nullptr;
  if (g_player_adoption_busy || !g_player_adoption.module || GetCurrentThreadId() != g_player_adoption.thread ||
      !UnrealObjectReferenceWritesReady() || !AdoptionInputHeld(controller,move,look) ||
      !CaptureAdoptionPlayer(controller,&before) || !CaptureAdoptionPawn(pawn,before.world,&previous_pawn) ||
      !AdoptionField(controller,L"Pawn",0x1C8,&value) || value)
    return NativePlayerAdoptionResult::kRejected;
  g_player_adoption_busy = true;
  struct ClearBusy { ~ClearBusy() { g_player_adoption_busy = false; } } guard;
  const auto ownership = NativeBindPawnOwnership(pawn,controller);
  if (ownership == NativePawnOwnershipResult::kRejected) return NativePlayerAdoptionResult::kRejected;
  AdoptionPlayer after; AdoptionPawn current_pawn;
  // Native ownership callbacks can observe/mutate world state. Revalidate all
  // identities and current write permission before publishing the reciprocal
  // controller link; don't wait until acknowledgment to notice camera/travel.
  if (ownership != NativePawnOwnershipResult::kApplied || !AdoptionInputHeld(controller,move,look) ||
      !CaptureAdoptionPlayer(controller,&after) || !SameAdoptionPlayer(before,after) ||
      !CaptureAdoptionPawn(pawn,before.world,&current_pawn) || !SameAdoptionPawn(previous_pawn,current_pawn) ||
      !AdoptionField(controller,L"Pawn",0x1C8,&value) || value)
    return NativePlayerAdoptionResult::kStateMismatch;
  // Match native replicated Pawn publication at 1179617A..1179618C: dirty
  // notification plus GC-aware UObject assignment, not script-base pointers.
  InterlockedOr(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(controller)+0x30),8);
  if (!UnrealWriteObjectPropertyExpected(controller,L"Pawn",nullptr,pawn))
    return NativePlayerAdoptionResult::kStateMismatch;
  CallAdoptionAcknowledge(controller,pawn);
  float eye = 0, base_eye = 0;
  const bool coherent = CaptureAdoptionPlayer(controller,&after) && CaptureAdoptionPawn(pawn,before.world,&current_pawn) &&
      SameAdoptionPlayer(before,after) && SameAdoptionPawn(previous_pawn,current_pawn) &&
      AdoptionField(controller,L"Pawn",0x1C8,&value) && value == pawn &&
      AdoptionField(controller,L"AcknowledgedPawn",0x300,&value) && value == pawn &&
      AdoptionField(pawn,L"Controller",0x1D4,&value) && value == controller &&
      UnrealReadObjectProperty(pawn,L"Owner",&value) && value == controller &&
      AdoptionField(pawn,L"PlayerReplicationInfo",0x3C0,&value) && value == before.pri &&
      AdoptionFloat(pawn,L"EyeHeight",0x330,&eye) && AdoptionFloat(pawn,L"BaseEyeHeight",0x32C,&base_eye) && eye == base_eye &&
      AdoptionInputHeld(controller,move,look);
  return coherent ? NativePlayerAdoptionResult::kApplied : NativePlayerAdoptionResult::kStateMismatch;
}
}  // namespace wfcds
