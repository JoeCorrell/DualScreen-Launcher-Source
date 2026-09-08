#include "NativeWeaponDataCache.h"
#include "UnrealBridge.h"
#include <cstring>
namespace wfcds {
namespace {
thread_local bool g_data_cache_busy=false;
struct CacheSchema { const wchar_t* pawn_class;const wchar_t* field;const wchar_t* data_class;uint32_t known_offset; };
constexpr std::array<CacheSchema,4> kWeaponCacheSchemas={{
  {L"TnWeaponBeam",L"BeamData",L"TnWeaponDataBeam",0x394},
  {L"TnWeaponGrenadeLauncherBase",L"GrenadeData",L"TnWeaponDataGrenadeLauncher",0x3A4},
  {L"TnGrenadeBag",L"GrenadeBagData",L"TnWeaponDataGrenadeBag",0x398},
  {L"TnWeaponRepair",L"RepairData",L"TnWeaponDataRepair",0x39C}
}};
bool CacheMemory(const void* p,size_t size) {
  MEMORY_BASIC_INFORMATION m{};
  return p && VirtualQuery(p,&m,sizeof(m)) && m.State==MEM_COMMIT && !(m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) &&
      size<=m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress));
}
bool CacheLayout(void* weapon,uint8_t* kind,uint32_t* offset,void** current,void** beam) {
  *kind=0;*offset=0;*current=nullptr;*beam=nullptr;unsigned matches=0;
  for(size_t i=0;i<kWeaponCacheSchemas.size();++i) {
    const auto& s=kWeaponCacheSchemas[i];
    if(!UnrealObjectIsA(weapon,s.pawn_class))continue;
    matches|=1u<<i;
    // Repair derives from Beam: its native UpdateData writes RepairData and
    // then calls Beam.UpdateData. Both caches must survive a retained handoff.
    if(*kind && !(i==3 && matches==9))return false;
    *kind=static_cast<uint8_t>(i+1);void* raw=nullptr;
    if(!UnrealResolveObjectPropertyOffset(weapon,s.field,offset) || *offset<0x28 || *offset>=0x1000 || (*offset&3) ||
        (s.known_offset && *offset!=s.known_offset) || !CacheMemory(static_cast<uint8_t*>(weapon)+*offset,4) ||
        !UnrealReadObjectProperty(weapon,s.field,current))return false;
    std::memcpy(&raw,static_cast<uint8_t*>(weapon)+*offset,4);
    if(raw!=*current || !UnrealObjectIsA(*current,s.data_class) || !UnrealObjectIndex(*current))return false;
    if(i==0)*beam=*current;
  }
  return *kind!=4 || matches==9;
}
bool SameCacheActors(const NativeWeaponProgress& a,const NativeWeaponProgress& b) {
  for(size_t i=0;i<4;++i)if(a.objects[i]!=b.objects[i] || a.indices[i]!=b.indices[i])return false;
  return true;
}
bool ThrowerCache(void* pawn,void* weapon,void** thrower,uint32_t* index,uint32_t* offset,void** data) {
  void* owner=nullptr;void* raw=nullptr;UnrealStructArrayView timers{};
  if(!UnrealReadObjectProperty(weapon,L"GrenadeThrower",thrower) || !UnrealObjectIsA(*thrower,L"TnGrenadeThrower") ||
      !(*index=UnrealObjectIndex(*thrower)) || UnrealObjectOuter(*thrower)!=weapon ||
      !UnrealReadObjectProperty(*thrower,L"ThrowerPawn",&owner) || owner!=pawn ||
      !UnrealResolveObjectPropertyOffset(*thrower,L"GrenadeData",offset) || *offset<0x28 || *offset>=0x1000 || (*offset&3) ||
      !CacheMemory(static_cast<uint8_t*>(*thrower)+*offset,4) ||
      !UnrealReadObjectProperty(*thrower,L"GrenadeData",data) || !UnrealObjectIsA(*data,L"TnWeaponDataGrenadeBag") ||
      !UnrealReadStructArrayProperty(pawn,L"Timers",&timers) || timers.count<0 || timers.count>256 ||
      timers.capacity<timers.count || timers.capacity>1024)return false;
  std::memcpy(&raw,static_cast<uint8_t*>(*thrower)+*offset,4);
  if(raw!=*data)return false;
  // ServerToss schedules SpawnGrenade on ThrowerPawn with TimerObj=self. Do not
  // retune the projectile data between starting an animation and its toss.
  // Reject any pending timer for this thrower, including paused/unknown names;
  // never cancel a native timer or discard an in-flight grenade.
  for(int32_t i=0;i<timers.count;++i) {
    void* timer_object=nullptr;
    if(!UnrealReadStructArrayObjectField(timers,i,L"TimerObj",&timer_object) || timer_object==*thrower)return false;
  }
  return true;
}
}
bool NativeCaptureWeaponDataCache(DWORD thread,void* pawn,void* weapon,void* target,NativeWeaponDataCache* out) {
  if(!out)return false;
  *out={};void* cache=nullptr;void* beam=nullptr;
  if(g_data_cache_busy || !NativeCaptureWeaponProgress(thread,pawn,weapon,&out->context) ||
      !UnrealObjectIsA(target,L"TnWeaponData") || !(out->target_index=UnrealObjectIndex(target)) ||
      !CacheLayout(weapon,&out->kind,&out->cache_offset,&cache,&beam))return false;
  if(out->kind && (cache!=out->context.objects[4] || !UnrealObjectIsA(target,kWeaponCacheSchemas[out->kind-1].data_class)))return false;
  if(out->kind==4 && (beam!=cache || !UnrealObjectIsA(target,L"TnWeaponDataBeam")))return false;
  if(out->kind==3) {
    void* thrower_data=nullptr;
    if(!ThrowerCache(pawn,weapon,&out->grenade_thrower,&out->thrower_index,&out->thrower_cache_offset,&thrower_data) ||
        thrower_data!=cache)return false;
  }
  out->target_data=target;out->captured=true;return true;
}
NativePlayerServiceResult NativeRetargetWeaponDataCache(const NativeWeaponDataCache& saved,bool rollback) {
  NativeWeaponProgress current;uint8_t kind=0;uint32_t offset=0;void* cache=nullptr;void* beam=nullptr;
  const auto& before=saved.context;
  void* desired=rollback ? before.objects[4]:saved.target_data;
  void* prior=rollback ? saved.target_data:before.objects[4];
  if(g_data_cache_busy || !saved.captured || !before.captured || saved.kind>kWeaponCacheSchemas.size() ||
      !NativeCaptureWeaponProgress(before.thread,before.objects[0],before.objects[1],&current) ||
      !SameCacheActors(before,current) || current.objects[4]!=desired ||
      !UnrealObjectIsA(before.objects[4],L"TnWeaponData") || UnrealObjectIndex(before.objects[4])!=before.indices[4] ||
      !UnrealObjectIsA(saved.target_data,L"TnWeaponData") || UnrealObjectIndex(saved.target_data)!=saved.target_index ||
      !CacheLayout(before.objects[1],&kind,&offset,&cache,&beam) || kind!=saved.kind || offset!=saved.cache_offset)
    return NativePlayerServiceResult::kRejected;
  if(kind==4 && (!UnrealObjectIsA(desired,L"TnWeaponDataBeam") || (beam!=prior && beam!=desired)))
    return NativePlayerServiceResult::kRejected;
  if(kind && ((!UnrealObjectIsA(desired,kWeaponCacheSchemas[kind-1].data_class)) || (cache!=prior && cache!=desired)))
    return NativePlayerServiceResult::kRejected;
  void* thrower=nullptr;void* thrower_data=nullptr;uint32_t thrower_index=0,thrower_offset=0;
  if(kind==3 && (!ThrowerCache(before.objects[0],before.objects[1],&thrower,&thrower_index,&thrower_offset,&thrower_data) ||
      thrower!=saved.grenade_thrower || thrower_index!=saved.thrower_index || thrower_offset!=saved.thrower_cache_offset ||
      (thrower_data!=prior && thrower_data!=desired)))return NativePlayerServiceResult::kRejected;
  g_data_cache_busy=true;struct Clear { ~Clear(){g_data_cache_busy=false;} } clear;
  if(kind && cache!=desired &&
      !UnrealWriteObjectPropertyExpected(before.objects[1],kWeaponCacheSchemas[kind-1].field,cache,desired)) {
    // The shared GC writer can publish the reference and then fail its native
    // barrier/postcheck. Never report that as a clean, untouched rejection.
    return NativePlayerServiceResult::kStateMismatch;
  }
  if(kind==4) {
    NativeWeaponProgress boundary;void* live_cache=nullptr;void* live_beam=nullptr;
    uint8_t live_kind=0;uint32_t live_offset=0;
    if(!NativeCaptureWeaponProgress(before.thread,before.objects[0],before.objects[1],&boundary) ||
        boundary.objects!=current.objects || boundary.indices!=current.indices ||
        !CacheLayout(before.objects[1],&live_kind,&live_offset,&live_cache,&live_beam) ||
        live_kind!=kind || live_offset!=offset || live_cache!=desired || live_beam!=beam ||
        (live_beam!=desired && !UnrealWriteObjectPropertyExpected(before.objects[1],L"BeamData",live_beam,desired)))
      return NativePlayerServiceResult::kStateMismatch;
  }
  if(kind==3) {
    void* live_thrower=nullptr;void* live_data=nullptr;uint32_t live_index=0,live_offset=0;
    NativeWeaponProgress boundary;
    if(!NativeCaptureWeaponProgress(before.thread,before.objects[0],before.objects[1],&boundary) ||
        boundary.objects!=current.objects || boundary.indices!=current.indices ||
        !ThrowerCache(before.objects[0],before.objects[1],&live_thrower,&live_index,&live_offset,&live_data) ||
        live_thrower!=thrower || live_index!=thrower_index || live_offset!=thrower_offset || live_data!=thrower_data ||
        (live_data!=desired && !UnrealWriteObjectPropertyExpected(thrower,L"GrenadeData",live_data,desired)))
      return NativePlayerServiceResult::kStateMismatch;
  }
  NativeWeaponProgress after;void* updated=nullptr;void* after_beam=nullptr;uint8_t after_kind=0;uint32_t after_offset=0;
  if(!NativeCaptureWeaponProgress(before.thread,before.objects[0],before.objects[1],&after) ||
      current.objects!=after.objects || current.indices!=after.indices ||
      !CacheLayout(before.objects[1],&after_kind,&after_offset,&updated,&after_beam) || after_kind!=kind || after_offset!=offset ||
      (kind && updated!=desired) || (kind==4 && after_beam!=desired))return NativePlayerServiceResult::kStateMismatch;
  if(kind==3 && (!ThrowerCache(before.objects[0],before.objects[1],&thrower,&thrower_index,&thrower_offset,&thrower_data) ||
      thrower!=saved.grenade_thrower || thrower_index!=saved.thrower_index || thrower_offset!=saved.thrower_cache_offset || thrower_data!=desired))
    return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
