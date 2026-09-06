#include "NativeAiAiming.h"
#include <array>
#include <cmath>
#include <cstring>
#include "NativeAiAimCode.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void WfcCallEaxEcx(void* method,void* object,const void* argument);
namespace {
namespace aim_code=native_ai_aim_code;
HMODULE g_aim_module=nullptr;
DWORD g_aim_thread=0;
void* g_aim_los_class=nullptr;
uint32_t g_aim_los_index=0;
thread_local bool g_aim_busy=false;
uintptr_t AimAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_aim_module)+va-native_reference_code::kImageBase;
}
bool AimMemory(const void* p,size_t n,HMODULE module=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
      (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase!=module) ||
      n>info.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const auto protection=info.Protect&0xFFu;
  if (write && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
      protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool AimPointer(void* object,uint32_t offset,void** out) {
  if (!out || !object || !AimMemory(static_cast<uint8_t*>(object)+offset,4)) return false;
  std::memcpy(out,static_cast<uint8_t*>(object)+offset,4); return true;
}
bool AimField(void* object,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset=0; void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadObjectProperty(object,name,out) && AimPointer(object,offset,&raw) && raw==*out;
}
bool AimLive(void* object,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool AimByte(void* object,const wchar_t* name,uint32_t expected,uint8_t wanted) {
  uint32_t offset=0; uint8_t value=0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadByteProperty(object,name,&value) && value==wanted;
}
bool AimPose(void* actor,std::array<float,3>* location) {
  uint32_t offset=0;
  if (!UnrealResolveStructPropertyOffset(actor,L"Location",12,&offset) || offset!=0xD8 ||
      !UnrealResolveStructPropertyOffset(actor,L"Rotation",12,&offset) || offset!=0xE4 ||
      !AimMemory(static_cast<uint8_t*>(actor)+0xD8,24)) return false;
  std::memcpy(location->data(),static_cast<uint8_t*>(actor)+0xD8,12);
  for (float v:*location) if (!std::isfinite(v)) return false;
  return true;
}
bool BindAimImage(HMODULE module) {
  if (!aim_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(module),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module)+va-native_reference_code::kImageBase);
    return AimMemory(p,bytes.size(),module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  })) return false;
  // IsAiming's exact shipping threshold, not a rounded UI angle or epsilon.
  const auto constant=[&](uintptr_t va,const auto& bytes) {
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module)+va-native_reference_code::kImageBase);
    return AimMemory(p,sizeof(bytes),module) && !std::memcmp(p,bytes,sizeof(bytes));
  };
  return constant(0x129D22C0,aim_code::kThreshold) && constant(0x129D2410,aim_code::kAngleRadians) &&
      constant(0x129D2344,aim_code::kAngleUnits);
}
struct AimCapture {
  // controller,pawn,aimer,world,weapon(optional),enemy(optional),LOS collection
  std::array<void*,7> objects{};
  std::array<uint32_t,7> indices{};
  std::array<float,3> target{};
  bool visible=false,paused=false;
};
bool CaptureAim(void* controller,void* pawn,void* aimer,AimCapture* out) {
  if (!out || !g_aim_module || GetCurrentThreadId()!=g_aim_thread ||
      !AimLive(controller,L"TnAiController") || !AimLive(pawn,L"TnPawn") ||
      !UnrealObjectIsA(aimer,L"TnAiWeaponAimer") ||
      !AimMemory(aimer,0x84,nullptr,false,true) ||
      !AimByte(controller,L"Role",0x66,3) || !AimByte(pawn,L"Role",0x66,3)) return false;
  *out={}; auto& o=out->objects; o[0]=controller; o[1]=pawn; o[2]=aimer;
  char type[64]{}; void* value=nullptr;
  if (!UnrealObjectClassName(aimer,type,sizeof(type)) || std::strcmp(type,"TnAiWeaponAimer") ||
      !AimField(controller,L"Pawn",0x1C8,&value) || value!=pawn ||
      !AimField(pawn,L"Controller",0x1D4,&value) || value!=controller ||
      !AimField(controller,L"WeaponAimer",0x2FC,&value) || value!=aimer ||
      !AimField(aimer,L"_Owner",0x28,&value) || value!=pawn ||
      !AimField(aimer,L"_OwnerController",0x30,&value) || value!=controller ||
      !AimField(controller,L"WorldInfo",0x98,&o[3]) || !AimLive(o[3],L"WorldInfo") ||
      !AimByte(o[3],L"NetMode",0x39C,0) ||
      !AimField(pawn,L"WorldInfo",0x98,&value) || value!=o[3]) return false;
  void *engine=nullptr,*context=nullptr,*slot=nullptr;
  if (!AimPointer(reinterpret_cast<void*>(AimAddress(0x12B10A9C)),0,&engine) ||
      !AimPointer(engine,0x40,&context) || !AimPointer(context,0x28,&slot) ||
      !AimPointer(slot,0,&value) || value!=o[3]) return false;
  bool downed=true,stasis=true,tear=true; float blend=0;
  if (!UnrealReadBoolProperty(pawn,L"_IsDowned",&downed) ||
      !UnrealReadBoolProperty(pawn,L"bStasis",&stasis) ||
      !UnrealReadBoolProperty(pawn,L"bTearOff",&tear) ||
      !UnrealReadObjectProperty(pawn,L"_CurrentTransformation",&value) ||
      !UnrealReadFloatProperty(pawn,L"_TransformBlendTimeRemaining",&blend) || !std::isfinite(blend)) return false;
  out->paused=downed || stasis || tear || value || blend>0;
  if (!AimField(pawn,L"Weapon",0x428,&o[4]) ||
      (o[4] && (!AimLive(o[4],L"TnWeapon") || !AimField(o[4],L"Instigator",0x94,&value) || value!=pawn ||
       !AimField(o[4],L"WorldInfo",0x98,&value) || value!=o[3])) ||
      !AimField(controller,L"Enemy",0x2A4,&o[5]) ||
      (o[5] && (o[5]==pawn || !AimLive(o[5],L"Actor") ||
       !AimField(o[5],L"WorldInfo",0x98,&value) || value!=o[3])) ||
      !AimField(controller,L"AiWorldState",0x300,&o[6]) ||
      !UnrealObjectIsA(o[6],L"HmGoapWorldStateCollection")) return false;
  uint32_t offset=0; uint8_t mode=255; bool from_weapon=false; uint32_t flags=0;
  if (!UnrealResolveBytePropertyOffset(aimer,L"_AimingMode",&offset) || offset!=0x2C ||
      !UnrealReadByteProperty(aimer,L"_AimingMode",&mode) || mode>2 ||
      !UnrealResolveStructPropertyOffset(aimer,L"_TargetLocation",12,&offset) || offset!=0x34 ||
      !UnrealReadBoolProperty(aimer,L"_CheckAimFromWeaponRotation",&from_weapon)) return false;
  std::memcpy(&flags,static_cast<uint8_t*>(aimer)+0x80,4);
  if (((flags&1)!=0)!=from_weapon) return false;
  std::array<float,3> origin{};
  if (!AimPose(pawn,&origin) || (o[4] && !AimPose(o[4],&origin)) ||
      (o[5] && !AimPose(o[5],&out->target))) return false;
  // Uses the CDO's live HashIndex and verifies the exact type at that index.
  // A missing property is native false, not an invitation to scan other slots.
  void* defaults=nullptr; int32_t hash=-1; UnrealStructArrayView properties{}; UnrealArrayView raw{};
  if (!g_aim_los_class || UnrealObjectIndex(g_aim_los_class)!=g_aim_los_index ||
      !AimPointer(g_aim_los_class,0x9C,&defaults) || !UnrealObjectIsA(defaults,L"TnAiWorldStateLOSToEnemy") ||
      UnrealObjectClass(defaults)!=g_aim_los_class || !UnrealReadIntProperty(defaults,L"HashIndex",&hash) || hash<0 ||
      !UnrealReadStructArrayProperty(o[6],L"Properties",&properties) || properties.element_size!=24 ||
      properties.count<0 || properties.capacity<properties.count || properties.capacity>4096 ||
      !UnrealObjectName(properties.struct_definition,type,sizeof(type)) || std::strcmp(type,"HmGoapWorldStateProperty") ||
      !AimMemory(static_cast<uint8_t*>(o[6])+0x28,12)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(o[6])+0x28,12);
  if (raw.data!=properties.data || raw.count!=properties.count || raw.capacity!=properties.capacity ||
      (raw.count && !AimMemory(raw.data,24*static_cast<size_t>(raw.count)))) return false;
  if (o[5] && hash<properties.count) {
    const auto* property=static_cast<const uint8_t*>(properties.data)+24*hash;
    void* property_type=nullptr; uint32_t bool_flags=0;
    std::memcpy(&property_type,property,4); std::memcpy(&bool_flags,property+16,4);
    out->visible=property_type==g_aim_los_class && (bool_flags&1)!=0;
  }
  for (size_t i=0;i<o.size();++i) {
    out->indices[i]=UnrealObjectIndex(o[i]); if (o[i] && !out->indices[i]) return false;
  }
  return true;
}
}
bool PrepareNativeAiAiming(HMODULE module,DWORD thread,void* controller,void* pawn,void* aimer) {
  if (g_aim_busy || !module || module!=GetModuleHandleW(nullptr) || !thread || thread!=GetCurrentThreadId() ||
      (g_aim_module && (module!=g_aim_module || thread!=g_aim_thread)) || !BindAimImage(module)) return false;
  void* los=nullptr;
  if (!UnrealResolveClassByPath(L"TransGame.TnAiWorldStateLOSToEnemy",&los) || !UnrealObjectIndex(los)) return false;
  g_aim_module=module; g_aim_thread=thread; g_aim_los_class=los; g_aim_los_index=UnrealObjectIndex(los);
  AimCapture capture; return CaptureAim(controller,pawn,aimer,&capture) && !capture.paused;
}
bool NativeUpdateAiAiming(void* controller,void* pawn,void* aimer,void* enemy,bool* aligned) {
  if (aligned) *aligned=false;
  AimCapture before;
  if (!aligned || g_aim_busy || !CaptureAim(controller,pawn,aimer,&before) || before.objects[5]!=enemy) return false;
  // Transformation/downed/stasis is a temporary no-fire state, not a broken
  // role mapping. Let the caller stop firing and retry on the next proxy slice.
  if (!before.visible || before.paused) return true;
  g_aim_busy=true; struct ClearBusy { ~ClearBusy() { g_aim_busy=false; } } busy;
  WfcCallEaxEcx(reinterpret_cast<void*>(AimAddress(aim_code::kAimAtLocationVa)),
      static_cast<uint8_t*>(aimer)+0x10,before.target.data());
  // This exact setter has no callbacks and changes only mode/location. Never
  // call the actor-target branch with cached services from a previous enemy.
  uint8_t mode=0; std::memcpy(&mode,static_cast<uint8_t*>(aimer)+0x2C,1);
  if (mode!=1 || std::memcmp(static_cast<uint8_t*>(aimer)+0x34,before.target.data(),12)) return false;
  using IsAiming=uint32_t (__attribute__((regparm(1))) *)(void*);
  const auto result=reinterpret_cast<IsAiming>(AimAddress(aim_code::kIsAimingVa))(static_cast<uint8_t*>(aimer)+0x10);
  AimCapture after;
  if (result>1 || !CaptureAim(controller,pawn,aimer,&after) || after.objects!=before.objects ||
      after.indices!=before.indices || !after.visible || after.paused || after.target!=before.target) return false;
  *aligned=result!=0; return true;
}
}
