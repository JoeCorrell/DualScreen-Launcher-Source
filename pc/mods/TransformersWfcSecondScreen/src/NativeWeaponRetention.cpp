#include "NativeWeaponRetention.h"
#include "NativeWeaponUpdateCode.h"
#include "NativeWeaponModifiers.h"
#include "NativeWeaponAnimator.h"
#include "UnrealBridge.h"
#include <cmath>
#include <cstring>
namespace wfcds {
namespace {
bool RetentionMemory(const void* p,size_t size,HMODULE image=nullptr,bool code=false) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      size>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress)) ||
      (image && m.AllocationBase!=image))return false;
  const auto protect=m.Protect&0xff;
  return !code || protect==PAGE_EXECUTE || protect==PAGE_EXECUTE_READ ||
      protect==PAGE_EXECUTE_READWRITE || protect==PAGE_EXECUTE_WRITECOPY;
}
template<size_t N> bool UpdateBody(HMODULE image,uintptr_t va,const std::array<uint8_t,N>& bytes) {
  const auto* p=reinterpret_cast<const void*>(reinterpret_cast<uintptr_t>(image)+va-native_reference_code::kImageBase);
  return RetentionMemory(p,N,image,true) && !std::memcmp(p,bytes.data(),N);
}
bool UpdateDispatch(HMODULE image,void* weapon,uint8_t kind) {
  using namespace native_weapon_update_code;
  if(!image || !RetentionMemory(static_cast<uint8_t*>(weapon)+0x10,4))return false;
  uintptr_t table=0,method=0;std::memcpy(&table,static_cast<uint8_t*>(weapon)+0x10,4);
  if(!RetentionMemory(reinterpret_cast<void*>(table),0x22c,image))return false;
  std::memcpy(&method,reinterpret_cast<void*>(table+0x228),4);
  const auto base=reinterpret_cast<uintptr_t>(image);
  // Unknown overrides (including the unported PlasmaCannon body)
  // cannot be treated as the base class just because their cache is unknown.
  uintptr_t expected=0;
  switch(kind) {
    case 0: expected=kBaseVa;break;
    case 1: if(!UpdateBody(image,kBeamVa,Beam(base)))return false;expected=kBeamVa;break;
    case 2: if(!UpdateBody(image,kGrenadeVa,Grenade(base)))return false;expected=kGrenadeVa;break;
    case 3: if(!UpdateBody(image,kBagVa,Bag(base)))return false;expected=kBagVa;break;
    case 4:
      if(!UpdateBody(image,kRepairVa,Repair(base)) || !UpdateBody(image,kBeamVa,Beam(base)))return false;
      expected=kRepairVa;break;
    default:return false;
  }
  return method==base+expected-native_reference_code::kImageBase && UpdateBody(image,kBaseVa,Base(base));
}
bool Service(void* owner,const wchar_t* field,const wchar_t* type,void** value,uint32_t* index) {
  *value=nullptr;*index=0;
  if(!UnrealReadObjectProperty(owner,field,value))return false;
  if(!*value)return true;
  return UnrealObjectIsA(*value,type) && (*index=UnrealObjectIndex(*value))!=0;
}
bool HeatMaximum(void* data,float* maximum) {
  return UnrealReadStructFloatProperty(data,L"HeatProperties",L"HeatMax",maximum) &&
      std::isfinite(*maximum) && *maximum>=0;
}
bool CaptureServices(NativeWeaponRetention* out,const char** reason) {
  const auto fail=[&](const char* why){if(reason)*reason=why;return false;};
  const auto& c=out->context;void* weapon=c.objects[1];
  if(!UpdateDispatch(out->image,weapon,out->cache_kind))return fail("unsupported-native-UpdateData");
  constexpr const wchar_t* fields[]={L"OwnerAnimator",L"WeaponMeshTemplate",L"DroppedPickupMesh",L"PickupFactoryMesh",L"WeaponHeat"};
  constexpr const wchar_t* types[]={L"TnWeaponOwnerAnimator",L"TnWeaponMesh",L"PrimitiveComponent",L"PrimitiveComponent",L"TnWeaponHeat"};
  for(size_t i=0;i<5;++i)
    if(!Service(weapon,fields[i],types[i],&out->services[i],&out->indices[i]))return fail("invalid-retained-service");
  if(!Service(c.objects[4],L"OwnerAnimations",L"TnWeaponOwnerAnimator",&out->services[5],&out->indices[5]) ||
      !Service(out->target,L"OwnerAnimations",L"TnWeaponOwnerAnimator",&out->services[6],&out->indices[6]))
    return fail("invalid-animation-template");
  if(out->services[0] && UnrealObjectOuter(out->services[0])!=weapon)return fail("foreign-owner-animator");
  // Native InitializeOwnerAnimator returns immediately for an existing
  // animator; it is character-owned, not controller-owned. A missing animator
  // with an instigator and authored animation requires actual initialization.
  out->needs_animator=!out->services[0] && c.objects[6] && out->services[6];
  if(out->needs_animator && !NativeCanPrepareWeaponAnimator(out->image,c.thread,c.objects[0],weapon,out->target))
    return fail("owner-animator-needs-initialization");
  for(size_t i=2;i<4;++i)
    if(out->services[i] && UnrealObjectOuter(out->services[i])!=weapon)return fail("foreign-pickup-mesh");
  float target_max=0;
  if(!HeatMaximum(c.objects[4],&out->heat_maximum) || !HeatMaximum(out->target,&target_max))return fail("invalid-authored-heat");
  // IsHeatBased reads Data, not the retained heat service. A transition to
  // zero-max AI tuning would silently stop the native Tick cooling path.
  // Do not admit that transition before its custom retention adapter exists.
  if(target_max!=out->heat_maximum)return fail("heat-role-tick-incompatible");
  if(!out->services[4])return target_max==0 || fail("missing-heat-service");
  NativeWeaponModifiers modifiers{};
  if(!NativeCaptureWeaponModifiers(c.thread,c.objects[0],weapon,&modifiers) || modifiers.heat!=out->services[4] ||
      modifiers.heat_index!=out->indices[4] || modifiers.heat_maximum!=target_max ||
      !NativeWeaponModifiersMatchData(modifiers,out->target))return fail("heat-needs-initialization-or-adaptation");
  return true;
}
}
bool NativeCaptureWeaponRetention(HMODULE image,DWORD thread,void* pawn,void* weapon,
    void* target,uint8_t kind,NativeWeaponRetention* out,const char** reason) {
  if(reason)*reason="invalid-retention-context";
  if(!out)return false;
  *out={};out->image=image;out->target=target;out->cache_kind=kind;
  if(!NativeCaptureWeaponProgress(thread,pawn,weapon,&out->context) || !UnrealObjectIsA(target,L"TnWeaponData") ||
      !(out->target_index=UnrealObjectIndex(target)) || !CaptureServices(out,reason))return false;
  NativeWeaponProgress after{};
  if(!NativeCaptureWeaponProgress(thread,pawn,weapon,&after) || after.objects!=out->context.objects ||
      after.indices!=out->context.indices)return false;
  out->captured=true;if(reason)*reason="ready";return true;
}
bool NativeWeaponRetentionStable(const NativeWeaponRetention& s) {
  if(!s.captured || !s.context.captured || UnrealObjectIndex(s.target)!=s.target_index ||
      UnrealObjectIndex(s.context.objects[4])!=s.context.indices[4])return false;
  NativeWeaponRetention current{};
  // CaptureServices uses original data for the original template comparison,
  // while current control is checked independently on both sides of the read.
  NativeWeaponProgress before{},after{};
  if(!NativeCaptureWeaponProgress(s.context.thread,s.context.objects[0],s.context.objects[1],&before))return false;
  for(size_t i=0;i<4;++i)if(before.objects[i]!=s.context.objects[i] || before.indices[i]!=s.context.indices[i])return false;
  // Controller/Data may change as part of our handoff. Instigator may not:
  // losing or gaining it changes whether native UpdateData initializes the
  // owner animator and heat ownership. Do not reuse an earlier admission for
  // a weapon whose native initialization path has since changed.
  if(before.objects[6]!=s.context.objects[6] || before.indices[6]!=s.context.indices[6])return false;
  if(before.objects[4]!=s.context.objects[4] && before.objects[4]!=s.target)return false;
  current=s;current.context=before;current.context.objects[4]=s.context.objects[4];current.context.indices[4]=s.context.indices[4];
  if(!CaptureServices(&current,nullptr) || !SameWeaponRetentionIdentity(s,current) ||
      !NativeCaptureWeaponProgress(s.context.thread,s.context.objects[0],s.context.objects[1],&after))return false;
  return before.objects==after.objects && before.indices==after.indices;
}
}
