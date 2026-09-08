#include "NativePlayerCombat.h"
#include "NativePlayerCombatCode.h"
#include <cstdint>
#include <cstring>
namespace wfcds {
extern "C" {
alignas(4) uintptr_t WfcCombatPlayerPawn=0,WfcCombatAiPawn=0;
void* WfcCombatOriginalCast=nullptr;
void* WfcCombatFireReject=nullptr;
void* WfcCombatOriginalField=nullptr;
void* WfcCombatFineStartReject=nullptr;void* WfcCombatFineStopReject=nullptr;
void WfcCombatFireCast();void WfcCombatAbortCast();void WfcCombatMeleeCast();
void WfcCombatStickCast();void WfcCombatRecordCast();
void WfcCombatFineStartCast();void WfcCombatFineStopCast();void WfcCombatSnipingCast();void WfcCombatFineHitCast();
void WfcCombatTargetJumpCast();void WfcCombatTargetAngleCast();
void WfcCombatMeleeTargetCast();
void WfcCameraConstrainCast();void WfcCameraLocationCast();void WfcCameraOrbitCast();void WfcCameraFlyingCast();
void WfcDownedHudCast();void WfcOwnerFinderCast();
void WfcNavigateCast();void WfcSpawnFinderCast();void* WfcNavigateReject=nullptr;
void WfcAssignmentCast();void* WfcAssignmentReject=nullptr;
}
namespace {
struct CombatPatch {uint8_t* target=nullptr;uint8_t original[5]{},replacement[5]{};};
constexpr size_t kCombatPatchCount=21;
CombatPatch g_combat_patches[kCombatPatchCount]{};
HMODULE g_combat_image=nullptr;DWORD g_combat_thread=0;
NativePlayerCombatResolver g_combat_resolver=nullptr;
NativeMeleeTargetResolver g_melee_target_resolver=nullptr;
bool CombatExecutable(const void* p,size_t size,HMODULE owner){
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || m.AllocationBase!=owner ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
  const auto offset=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(offset>m.RegionSize || size>m.RegionSize-offset)return false;
  const auto access=m.Protect&255;
  return access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY;
}
bool CombatOwned(const CombatPatch& p){return p.target && CombatExecutable(p.target,5,g_combat_image) && !std::memcmp(p.target,p.replacement,5);}
bool CombatWrite(uint8_t* target,const uint8_t* bytes){
  DWORD old=0,ignored=0;if(!VirtualProtect(target,5,PAGE_EXECUTE_READWRITE,&old))return false;
  std::memcpy(target,bytes,5);FlushInstructionCache(GetCurrentProcess(),target,5);VirtualProtect(target,5,old,&ignored);return true;
}
}
void PublishNativePlayerCombatPawns(void* player,void* ai){
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcCombatPlayerPawn),reinterpret_cast<LONG>(player));
  InterlockedExchange(reinterpret_cast<volatile LONG*>(&WfcCombatAiPawn),reinterpret_cast<LONG>(ai));
}
extern "C" uint32_t WfcResolvePlayerCombat(void* pawn,uint32_t melee){
  return GetCurrentThreadId()==g_combat_thread && g_combat_resolver && pawn &&
      reinterpret_cast<uintptr_t>(pawn)==WfcCombatPlayerPawn && melee<=1 && g_combat_resolver(pawn,melee!=0);
}
extern "C" uint32_t WfcResolvePlayerMeleeTarget(void* pawn,void* targeting,void* victim){
  return GetCurrentThreadId()==g_combat_thread && g_melee_target_resolver &&
      WfcResolvePlayerCombat(pawn,0) && g_melee_target_resolver(pawn,targeting,victim);
}
extern "C" uint32_t WfcResolvePlayerAssignment(void* pawn,void* controller){
  // Resolve first: no access to unvalidated or wrong-thread pawn storage.
  // The native receiver must be the canonical reciprocal controller, not
  // merely some controller invoking OnSetPawn with the mapped player pawn.
  if(!controller || !WfcResolvePlayerCombat(pawn,0))return 0;
  void* owner=nullptr;
  std::memcpy(&owner,static_cast<const uint8_t*>(pawn)+0x1D4,sizeof(owner));
  return owner==controller;
}
bool ValidateNativePlayerCombat(HMODULE image){
  if(!image)return false;
  using namespace native_player_combat_code;
  return Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> r){
    auto expected=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),r);
    auto* p=reinterpret_cast<uint8_t*>(image)+(va-native_reference_code::kImageBase);
    if(!CombatExecutable(p,expected.size(),image))return false;
    constexpr uintptr_t methods[]={kFireVa,kAbortVa,kMeleeVa,kStickVa,kRecordVa,kFineStartVa,kFineStopVa,kSnipingVa,kFineHitVa,kTargetJumpVa,kTargetAngleVa,kTargetSetVa,kConstrainSetVa,kLocationSetVa,kOrbitSetVa,kFlyingStickVa,kDownedHudVa,kOwnerFinderVa,kNavigateVa,kSpawnFinderVa,kAssignmentVa};
    size_t index=0;while(index<kCombatPatchCount && methods[index]!=va)++index;
    if(index==kCombatPatchCount)return !std::memcmp(p,expected.data(),expected.size());
    constexpr size_t offsets[]={kFireCast,kAbortCast,kMeleeCast,kStickCast,kRecordCast,kFineStartCast,kFineStopCast,kSnipingCast,kFineHitCast,kTargetJumpCast,kTargetAngleCast,kTargetSetCast,kConstrainSetCast,kLocationSetCast,kOrbitSetCast,kFlyingStickCast,kDownedHudCast,kOwnerFinderCast,kNavigateCast,kSpawnFinderCast,kAssignmentCast};
    const size_t offset=offsets[index];
    const auto& patch=g_combat_patches[index];
    if(patch.target){
      if(image!=g_combat_image || patch.target!=p+offset || !CombatOwned(patch) ||
          std::memcmp(patch.original,expected.data()+offset,5))return false;
      std::memcpy(expected.data()+offset,patch.replacement,5);
    }
    return !std::memcmp(p,expected.data(),expected.size());
  });
}
bool NativePlayerCombatInstalled(){for(const auto& p:g_combat_patches)if(p.target)return true;return false;}
bool RemoveNativePlayerCombat(){
  if(g_combat_thread && GetCurrentThreadId()!=g_combat_thread)return false;
  PublishNativePlayerCombatPawns(nullptr,nullptr);
  for(const auto& p:g_combat_patches)if(p.target && !CombatOwned(p))return false;
  for(size_t n=kCombatPatchCount;n>0;--n){auto& p=g_combat_patches[n-1];if(p.target && !CombatWrite(p.target,p.original))return false;p={};}
  WfcCombatOriginalCast=WfcCombatFireReject=WfcCombatOriginalField=WfcCombatFineStartReject=WfcCombatFineStopReject=nullptr;
  WfcNavigateReject=WfcAssignmentReject=nullptr;
  g_combat_image=nullptr;g_combat_thread=0;g_combat_resolver=nullptr;g_melee_target_resolver=nullptr;return true;
}
bool InstallNativePlayerCombat(HMODULE image,DWORD thread,NativePlayerCombatResolver resolver,NativeMeleeTargetResolver target_resolver){
  if(!thread || thread!=GetCurrentThreadId() || !resolver || !target_resolver || NativePlayerCombatInstalled() || !ValidateNativePlayerCombat(image))return false;
  using namespace native_player_combat_code;
  const auto at=[image](uintptr_t va){return reinterpret_cast<uint8_t*>(image)+(va-native_reference_code::kImageBase);};
  g_combat_image=image;g_combat_thread=thread;g_combat_resolver=resolver;g_melee_target_resolver=target_resolver;PublishNativePlayerCombatPawns(nullptr,nullptr);
  WfcCombatOriginalCast=at(0x10901430);WfcCombatFireReject=at(kFireRejectVa);
  WfcNavigateReject=at(kNavigateRejectVa);
  WfcAssignmentReject=at(kAssignmentRejectVa);
  WfcCombatOriginalField=at(kPlayerFieldVa);WfcCombatFineStartReject=at(kFineStartRejectVa);WfcCombatFineStopReject=at(kFineStopRejectVa);
  const uintptr_t sites[]={kFireVa+kFireCast,kAbortVa+kAbortCast,kMeleeVa+kMeleeCast,kStickVa+kStickCast,kRecordVa+kRecordCast,
      kFineStartVa+kFineStartCast,kFineStopVa+kFineStopCast,kSnipingVa+kSnipingCast,kFineHitVa+kFineHitCast,kTargetJumpVa+kTargetJumpCast,kTargetAngleVa+kTargetAngleCast,kTargetSetVa+kTargetSetCast,
      kConstrainSetVa+kConstrainSetCast,kLocationSetVa+kLocationSetCast,kOrbitSetVa+kOrbitSetCast,kFlyingStickVa+kFlyingStickCast,kDownedHudVa+kDownedHudCast,kOwnerFinderVa+kOwnerFinderCast,kNavigateVa+kNavigateCast,kSpawnFinderVa+kSpawnFinderCast,kAssignmentVa+kAssignmentCast};
  void* targets[]={reinterpret_cast<void*>(&WfcCombatFireCast),reinterpret_cast<void*>(&WfcCombatAbortCast),reinterpret_cast<void*>(&WfcCombatMeleeCast),reinterpret_cast<void*>(&WfcCombatStickCast),reinterpret_cast<void*>(&WfcCombatRecordCast),
      reinterpret_cast<void*>(&WfcCombatFineStartCast),reinterpret_cast<void*>(&WfcCombatFineStopCast),reinterpret_cast<void*>(&WfcCombatSnipingCast),reinterpret_cast<void*>(&WfcCombatFineHitCast),reinterpret_cast<void*>(&WfcCombatTargetJumpCast),reinterpret_cast<void*>(&WfcCombatTargetAngleCast),reinterpret_cast<void*>(&WfcCombatMeleeTargetCast),
      reinterpret_cast<void*>(&WfcCameraConstrainCast),reinterpret_cast<void*>(&WfcCameraLocationCast),reinterpret_cast<void*>(&WfcCameraOrbitCast),reinterpret_cast<void*>(&WfcCameraFlyingCast),reinterpret_cast<void*>(&WfcDownedHudCast),reinterpret_cast<void*>(&WfcOwnerFinderCast),reinterpret_cast<void*>(&WfcNavigateCast),reinterpret_cast<void*>(&WfcSpawnFinderCast),reinterpret_cast<void*>(&WfcAssignmentCast)};
  static_assert(sizeof(sites)/sizeof(sites[0])==kCombatPatchCount && sizeof(targets)/sizeof(targets[0])==kCombatPatchCount);
  for(size_t i=0;i<kCombatPatchCount;++i){
    auto& p=g_combat_patches[i];auto* site=at(sites[i]);std::memcpy(p.original,site,5);
    p.replacement[0]=0xE8;const uint32_t rel=reinterpret_cast<uintptr_t>(targets[i])-reinterpret_cast<uintptr_t>(site)-5;
    std::memcpy(p.replacement+1,&rel,4);
    if(!CombatWrite(site,p.replacement)){RemoveNativePlayerCombat();return false;}p.target=site;
  }
  return true;
}
}
