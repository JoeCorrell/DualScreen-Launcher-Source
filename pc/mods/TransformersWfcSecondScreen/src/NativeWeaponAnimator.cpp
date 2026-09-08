#include "NativeWeaponAnimator.h"
#include "NativeWeaponAnimatorCode.h"
#include <cmath>
#include <cstring>
#include <cwchar>
extern "C" void WfcCallWeaponAnimatorInitialize(void*,void*,const void*,const void*);
namespace wfcds {
namespace {
thread_local bool g_animator_binding_busy=false;
struct AnimatorInterface { void* object=nullptr;uint32_t unused=0;void* interface_address=nullptr; };
static_assert(sizeof(AnimatorInterface)==12,"WFC cooked interface records are three words");
struct AnimatorBinding {
  NativeWeaponProgress context{};
  void* form=nullptr;uint32_t form_index=0;
  void* container=nullptr;uint32_t container_index=0;
  std::array<AnimatorInterface,2> services{};
  std::array<uint32_t,2> indices{};
};
bool AnimatorMemory(const void* p,size_t n,HMODULE image=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image))return false;
  const auto offset=reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress);
  if(offset>=m.RegionSize || n>m.RegionSize-offset)return false;
  const auto access=m.Protect&0xff;
  return (!execute || access==PAGE_EXECUTE || access==PAGE_EXECUTE_READ || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY) &&
      (!write || access==PAGE_READWRITE || access==PAGE_WRITECOPY || access==PAGE_EXECUTE_READWRITE || access==PAGE_EXECUTE_WRITECOPY);
}
bool AnimatorClass(void* object,std::initializer_list<const char*> types) {
  char name[64]{};
  if(!UnrealObjectClassName(object,name,sizeof(name)))return false;
  for(const char* type:types)if(!std::strcmp(name,type))return true;
  return false;
}
bool AnimatorCode(HMODULE image,uintptr_t va,const uint8_t* bytes,size_t size) {
  if(!image)return false;
  const auto* p=reinterpret_cast<const uint8_t*>(image)+va-native_reference_code::kImageBase;
  return AnimatorMemory(p,size,image,true) && !std::memcmp(p,bytes,size);
}
bool AnimatorRecord(HMODULE image,void* service,AnimatorInterface* out) {
  using namespace native_weapon_animator_code;
  const bool robot=AnimatorClass(service,{"TnRobotAnimator"});
  if(!robot && !AnimatorClass(service,{"TnBasicAnimator"}))return false;
  uint32_t offset=0;
  if(!UnrealObjectIndex(service) || !UnrealResolveStructPropertyOffset(service,L"__ITnAnimatorServiceVTable",4,&offset) ||
      offset!=(robot ? 0x28u:0x38u) || !AnimatorMemory(static_cast<uint8_t*>(service)+0x10,4) ||
      !AnimatorMemory(static_cast<uint8_t*>(service)+offset,4))return false;
  uintptr_t table=0,query=0,interface_table=0;
  std::memcpy(&table,static_cast<uint8_t*>(service)+0x10,4);
  if(!AnimatorMemory(reinterpret_cast<void*>(table),8,image))return false;
  std::memcpy(&query,reinterpret_cast<void*>(table+4),4);
  const uintptr_t base=reinterpret_cast<uintptr_t>(image),va=robot ? kRobotQueryVa:kBasicQueryVa;
  const auto code=Query(base,robot);
  if(query!=base+va-native_reference_code::kImageBase || !AnimatorCode(image,va,code.data(),code.size()))return false;
  std::memcpy(&interface_table,static_cast<uint8_t*>(service)+offset,4);
  if(!AnimatorMemory(reinterpret_cast<void*>(interface_table),4,image))return false;
  // Exact QueryInterface leaf returns compiled-this + 0x18/0x28. Store the
  // ADDRESS of the embedded table, not the table or compiled/raw UObject.
  *out={service,0,static_cast<uint8_t*>(service)+offset};return true;
}
bool CaptureAnimatorBinding(HMODULE image,DWORD thread,void* pawn,void* weapon,AnimatorBinding* out) {
  *out={};uint8_t type=0;void* transform=nullptr;void* owner=nullptr;float blend=0;
  if(!NativeCaptureWeaponProgress(thread,pawn,weapon,&out->context) || out->context.objects[6]!=pawn ||
      !UnrealReadByteProperty(weapon,L"WeaponType",&type) ||
      !UnrealReadObjectProperty(pawn,L"_CurrentTransformation",&transform) || transform ||
      !UnrealReadFloatProperty(pawn,L"_TransformBlendTimeRemaining",&blend) || !std::isfinite(blend) || blend>0 ||
      !UnrealReadObjectProperty(pawn,type==6 ? L"_VehicleForm":L"_RobotForm",&out->form) ||
      !(out->form_index=UnrealObjectIndex(out->form)) ||
      !(type==6 ? AnimatorClass(out->form,{"TnCarForm","TnTruckForm","TnPlaneForm","TnTankForm"}):AnimatorClass(out->form,{"TnRobotForm"})) ||
      !UnrealReadObjectProperty(out->form,L"_OwnerPawn",&owner) || owner!=pawn ||
      !UnrealReadObjectProperty(out->form,L"_Services",&out->container) ||
      !(out->container_index=UnrealObjectIndex(out->container)) || !AnimatorClass(out->container,{"TnServiceContainer"}))return false;
  UnrealStructArrayView rows{};
  if(!UnrealReadStructArrayProperty(out->container,L"_ServiceDescriptions",&rows) || rows.count<0 || rows.count>128)return false;
  constexpr const wchar_t* keys[]={L"TnAnimatorService",L"TnAdditiveAnimatorService"};
  constexpr const wchar_t* fields[]={L"_Animator",L"_AdditiveAnimator"};
  for(size_t n=0;n<2;++n) {
    void* native_field=nullptr;void* cached=nullptr;
    if(!UnrealReadObjectProperty(out->form,fields[n],&native_field))return false;
    for(int32_t i=0;i<rows.count;++i) {
      bool match=false;
      if(!UnrealStructArrayNameEquals(rows,i,L"Type",keys[n],&match))return false;
      if(match) { if(!UnrealReadStructArrayObjectField(rows,i,L"Service",&cached))return false;break; }
    }
    // A cache override may belong to a scripted service configuration. Do not
    // initialize against it while silently ignoring the form's real animator.
    if((cached && cached!=native_field) || !AnimatorRecord(image,native_field,&out->services[n]))return false;
    out->indices[n]=UnrealObjectIndex(native_field);
  }
  return true;
}
bool SameAnimatorBinding(const AnimatorBinding& a,const AnimatorBinding& b) {
  return a.context.objects==b.context.objects && a.context.indices==b.context.indices &&
      a.context.interval==b.context.interval && a.context.spread==b.context.spread &&
      a.form==b.form && a.form_index==b.form_index && a.container==b.container && a.container_index==b.container_index &&
      !std::memcmp(a.services.data(),b.services.data(),sizeof(a.services)) && a.indices==b.indices;
}
bool AnimatorBootstrapSource(void* weapon,void* target,void** source,uint32_t* index) {
  void* animator=nullptr;
  if(!UnrealObjectIsA(target,L"TnWeaponData") || !UnrealObjectIndex(target) ||
      !UnrealReadObjectProperty(weapon,L"OwnerAnimator",&animator) || animator ||
      !UnrealReadObjectProperty(target,L"OwnerAnimations",source) ||
      !UnrealObjectIsA(*source,L"TnWeaponOwnerAnimator") || !(*index=UnrealObjectIndex(*source)) ||
      !AnimatorMemory(static_cast<uint8_t*>(*source)+0xa8,24))return false;
  const std::array<uint8_t,24> empty{};
  return !std::memcmp(static_cast<uint8_t*>(*source)+0xa8,empty.data(),empty.size());
}
}
bool NativeCanPrepareWeaponAnimator(HMODULE image,DWORD thread,void* pawn,void* weapon,void* target) {
  using namespace native_weapon_animator_code;
  AnimatorBinding before{},after{};void* source=nullptr;uint32_t source_index=0;
  const auto barrier=native_reference_code::Relocate(native_reference_code::kBarrier,reinterpret_cast<uintptr_t>(image),{15,38});
  return !g_animator_binding_busy && UnrealObjectCloningReady() &&
      AnimatorCode(image,kInitializeVa,kInitialize,sizeof(kInitialize)) &&
      AnimatorCode(image,native_reference_code::kBarrierVa,barrier.data(),barrier.size()) &&
      CaptureAnimatorBinding(image,thread,pawn,weapon,&before) &&
      AnimatorBootstrapSource(weapon,target,&source,&source_index) &&
      CaptureAnimatorBinding(image,thread,pawn,weapon,&after) && SameAnimatorBinding(before,after);
}
NativePlayerServiceResult NativePrepareWeaponAnimator(HMODULE image,DWORD thread,
    void* pawn,void* weapon,void* target,UnrealObjectCloneProperty* recovery) {
  if(!recovery || recovery->pending || !NativeCanPrepareWeaponAnimator(image,thread,pawn,weapon,target))
    return NativePlayerServiceResult::kRejected;
  AnimatorBinding before{},after{};void* source=nullptr;uint32_t source_index=0;
  const auto target_index=UnrealObjectIndex(target);
  if(!CaptureAnimatorBinding(image,thread,pawn,weapon,&before) ||
      !AnimatorBootstrapSource(weapon,target,&source,&source_index))return NativePlayerServiceResult::kRejected;
  const auto unchanged=[&](){
    void* current_source=nullptr;
    return UnrealObjectIndex(target)==target_index && UnrealObjectIndex(source)==source_index &&
        UnrealReadObjectProperty(target,L"OwnerAnimations",&current_source) && current_source==source &&
        CaptureAnimatorBinding(image,thread,pawn,weapon,&after) && SameAnimatorBinding(before,after);
  };
  const auto failed=[&](){
    const bool stable=unchanged();
    if(recovery->pending && !UnrealFinishObjectCloneProperty(recovery,false))
      return NativePlayerServiceResult::kStateMismatch;
    void* current=nullptr;
    return stable && unchanged() && UnrealReadObjectProperty(weapon,L"OwnerAnimator",&current) && !current
        ? NativePlayerServiceResult::kRejected:NativePlayerServiceResult::kStateMismatch;
  };
  if(!UnrealBeginObjectCloneProperty(weapon,L"OwnerAnimator",source,recovery) || !unchanged())return failed();
  if(NativeBindClonedWeaponAnimator(image,thread,pawn,weapon,recovery)!=NativePlayerServiceResult::kApplied ||
      !unchanged())return failed();
  if(!UnrealFinishObjectCloneProperty(recovery,true))return failed();
  return NativePlayerServiceResult::kApplied;
}
NativePlayerServiceResult NativeBindClonedWeaponAnimator(HMODULE image,DWORD thread,
    void* pawn,void* weapon,UnrealObjectCloneProperty* clone) {
  using namespace native_weapon_animator_code;
  AnimatorBinding before{},after{};
  const auto barrier=native_reference_code::Relocate(native_reference_code::kBarrier,reinterpret_cast<uintptr_t>(image),{15,38});
  if(g_animator_binding_busy || !clone || !UnrealObjectClonePropertyPending(*clone) || clone->owner!=weapon ||
      std::wcscmp(clone->property.data(),L"OwnerAnimator") || !UnrealObjectIsA(clone->clone,L"TnWeaponOwnerAnimator") ||
      !AnimatorCode(image,kInitializeVa,kInitialize,sizeof(kInitialize)) ||
      !AnimatorCode(image,native_reference_code::kBarrierVa,barrier.data(),barrier.size()) ||
      !CaptureAnimatorBinding(image,thread,pawn,weapon,&before))return NativePlayerServiceResult::kRejected;
  auto* destination=static_cast<uint8_t*>(clone->clone);
  if(!AnimatorMemory(destination+0x78,4,nullptr,false,true) ||
      !AnimatorMemory(destination+0xa8,24,nullptr,false,true))return NativePlayerServiceResult::kRejected;
  const std::array<uint8_t,24> empty{};
  if(std::memcmp(destination+0xa8,empty.data(),empty.size()))return NativePlayerServiceResult::kRejected;
  uint32_t flags=0;std::memcpy(&flags,destination+0x78,4);
  if(!CaptureAnimatorBinding(image,thread,pawn,weapon,&after) || !SameAnimatorBinding(before,after) ||
      !UnrealObjectClonePropertyPending(*clone))return NativePlayerServiceResult::kRejected;
  g_animator_binding_busy=true;struct Clear { ~Clear(){g_animator_binding_busy=false;} } clear;
  WfcCallWeaponAnimatorInitialize(reinterpret_cast<uint8_t*>(image)+kInitializeVa-native_reference_code::kImageBase,
      destination+0x10,&before.services[0],&before.services[1]);
  uint32_t actual_flags=0;std::memcpy(&actual_flags,destination+0x78,4);
  if(!UnrealObjectClonePropertyPending(*clone) || actual_flags!=(flags|2u) ||
      std::memcmp(destination+0xa8,before.services.data(),24) ||
      !CaptureAnimatorBinding(image,thread,pawn,weapon,&after) || !SameAnimatorBinding(before,after))
    return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
