#include "NativeMeleeTargetGuard.h"
#include <cstring>
namespace wfcds {
namespace {
template<class T>T TargetRaw(void* p,size_t offset){T v{};std::memcpy(&v,static_cast<uint8_t*>(p)+offset,sizeof(v));return v;}
bool TargetSlot(void* p,uint32_t index){return p && index && UnrealObjectAt(static_cast<int32_t>(index-1))==p;}
bool TargetHeaderEqual(const UnrealArrayView& a,const UnrealArrayView& b){return a.data==b.data && a.count==b.count && a.capacity==b.capacity;}
bool TargetGuardLive(const NativeMeleeTargetGuard& s){
  return TargetSlot(s.pawn,s.pawn_index) && TargetSlot(s.controller,s.controller_index) && TargetSlot(s.targeting,s.targeting_index) &&
      TargetRaw<void*>(s.pawn,0x1D4)==s.controller && TargetRaw<void*>(s.controller,0x1C8)==s.pawn &&
      TargetRaw<void*>(s.controller,0x608)==s.targeting && TargetRaw<void*>(s.targeting,0x48)==s.pawn &&
      TargetHeaderEqual(s.targets,TargetRaw<UnrealArrayView>(s.targeting,0x34));
}
bool TargetLink(void* object,const wchar_t* field,uint32_t offset,void* value){
  uint32_t observed=0;void* actual=nullptr;
  return UnrealResolveObjectPropertyOffset(object,field,&observed) && observed==offset &&
      UnrealReadObjectProperty(object,field,&actual) && actual==value;
}
bool CaptureTargetGuard(NativeMeleeTargetGuard* out,DWORD thread,uint32_t generation,
    void* pawn,void* controller,void* targeting){
  *out={};UnrealStructArrayView array;uint32_t offset=0,field_offset=0;
  if(!pawn || !controller || !targeting || !UnrealObjectIsA(pawn,L"TnPawn") ||
      !UnrealObjectIsA(controller,L"TnPlayerController") || !UnrealObjectIsA(targeting,L"TnPlayerTargeting") ||
      !TargetLink(pawn,L"Controller",0x1D4,controller) || !TargetLink(controller,L"Pawn",0x1C8,pawn) ||
      !TargetLink(controller,L"PlayerTargeting",0x608,targeting) || !TargetLink(targeting,L"_TargetingOwner",0x48,pawn) ||
      !UnrealResolveArrayPropertyOffset(targeting,L"PickedTargets",&offset) || offset!=0x34 ||
      !UnrealReadStructArrayProperty(targeting,L"PickedTargets",&array) || !array.data ||
      array.count<4 || array.count>64 || array.capacity<array.count || array.capacity>64 || array.element_size!=8 ||
      !UnrealResolveStructFieldOffset({array.data,array.element_size,array.struct_definition},
          L"TargetActor",L"ObjectProperty",4,&field_offset) || field_offset!=0)return false;
  out->thread=thread;out->generation=generation;out->pawn=pawn;out->controller=controller;out->targeting=targeting;
  out->pawn_index=UnrealObjectIndex(pawn);out->controller_index=UnrealObjectIndex(controller);out->targeting_index=UnrealObjectIndex(targeting);
  out->targets={array.data,array.count,array.capacity};return TargetGuardLive(*out);
}
}
bool NativeMeleeTargetSafe(NativeMeleeTargetGuard* cache,DWORD thread,uint32_t generation,
    void* pawn,void* controller,void* targeting,void* victim){
  if(!cache || !thread || thread!=GetCurrentThreadId() || !generation || !pawn || !controller || !targeting)return false;
  const bool cached=cache->thread==thread && cache->generation==generation && cache->pawn==pawn &&
      cache->controller==controller && cache->targeting==targeting && TargetGuardLive(*cache);
  if(!cached){
    NativeMeleeTargetGuard fresh;
    if(!CaptureTargetGuard(&fresh,thread,generation,pawn,controller,targeting)){*cache={};return false;}
    *cache=fresh;
  }
  // Native setter performs no work for the same victim. Avoid class/field
  // lookups on every steady target update; changed targets are validated.
  if(TargetRaw<void*>(cache->targets.data,3*8)==victim)return true;
  bool deleting=false;
  if(victim && (!UnrealObjectIsA(victim,L"Actor") || !UnrealReadBoolProperty(victim,L"bDeleteMe",&deleting) || deleting))return false;
  return TargetGuardLive(*cache);
}
}
