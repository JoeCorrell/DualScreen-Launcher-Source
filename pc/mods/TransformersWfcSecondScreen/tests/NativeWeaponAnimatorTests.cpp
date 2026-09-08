// Executes the exact native initializer and real assembly caller. Form lookup,
// UObject metadata, quiescence and clone/root ownership are fixtures, not WFC.
#include "NativeWeaponAnimator.cpp"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x400]{};const char* type="";uint32_t id=0; };
Object pawn,weapon,lerper,world,data,controller,form,container,primary,additive,animator,source,other;
Object* objects[]={&pawn,&weapon,&lerper,&world,&data,&controller,&form,&container,&primary,&additive,&animator,&source,&other};
uint8_t* image=nullptr;NativeWeaponProgress context{};UnrealObjectCloneProperty journal{};
void* form_owner=nullptr;void* cached=nullptr;bool pending=true;float blend=0;void* transformation=nullptr;
uint8_t weapon_type=0;unsigned reads=0,dirty_at=0;uint32_t interface_offset=0;
void* owner_animator=nullptr;void* authored_animator=nullptr;
unsigned clone_fault=0,clone_calls=0;bool cloning_ready=true;
Object* Lookup(void* p){for(auto* o:objects)if(p==o->bytes)return o;return nullptr;}
template<class T> void Put(void* p,size_t offset,T value){std::memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
void InstallCode(){
  using namespace native_weapon_animator_code;
  const auto put=[&](uintptr_t va,const auto& code){std::memcpy(image+va-native_reference_code::kImageBase,code.data(),code.size());};
  std::memcpy(image+kInitializeVa-native_reference_code::kImageBase,kInitialize,sizeof(kInitialize));
  put(kBasicQueryVa,Query(reinterpret_cast<uintptr_t>(image),false));
  put(kRobotQueryVa,Query(reinterpret_cast<uintptr_t>(image),true));
  put(native_reference_code::kBarrierVa,native_reference_code::Relocate(native_reference_code::kBarrier,reinterpret_cast<uintptr_t>(image),{15,38}));
  constexpr wchar_t name[]=L"TnAnimatorService";
  std::memcpy(image+0x1262366c-native_reference_code::kImageBase,name,sizeof(name));
  std::memcpy(image+0x12632fa4-native_reference_code::kImageBase,name,sizeof(name));
}
void Service(Object& service,bool robot,unsigned table_offset){
  using namespace native_weapon_animator_code;
  service.type=robot ? "TnRobotAnimator":"TnBasicAnimator";
  Put(service.bytes,0x10,image+table_offset);
  Put(image,table_offset+4,image+(robot ? kRobotQueryVa:kBasicQueryVa)-native_reference_code::kImageBase);
  Put(service.bytes,robot ? 0x28:0x38,image+table_offset+0x100);
  Put(service.bytes,8,uint32_t{0x800000}); // native reference barrier must mark rescan
}
void Reset(unsigned vehicle=0){
  uint32_t id=1;for(auto* o:objects){*o={};o->id=id++;}
  InstallCode();weapon_type=vehicle ? 6:0;pending=true;blend=0;transformation=nullptr;cached=nullptr;
  form.type=vehicle ? (vehicle==1 ? "TnCarForm":vehicle==2 ? "TnTruckForm":vehicle==3 ? "TnPlaneForm":"TnTankForm"):"TnRobotForm";
  container.type="TnServiceContainer";animator.type="TnWeaponOwnerAnimator";form_owner=pawn.bytes;
  Service(primary,!vehicle,0x1000);Service(additive,false,0x2000);
  context={};context.thread=GetCurrentThreadId();context.captured=true;
  context.objects={pawn.bytes,weapon.bytes,lerper.bytes,world.bytes,data.bytes,controller.bytes,pawn.bytes};
  for(size_t i=0;i<context.objects.size();++i)context.indices[i]=Lookup(context.objects[i])->id;
  journal={};journal.owner=weapon.bytes;journal.clone=animator.bytes;journal.source=source.bytes;
  journal.clone_index=animator.id;journal.journal_address=&journal;journal.thread=context.thread;journal.pending=true;
  std::wcscpy(journal.property.data(),L"OwnerAnimator");
  Put(animator.bytes,0x78,uint32_t{0x45});reads=dirty_at=interface_offset=0;
  owner_animator=animator.bytes;authored_animator=source.bytes;clone_fault=clone_calls=0;cloning_ready=true;
  Put(image,0x12b1047c-native_reference_code::kImageBase,uint32_t{0});
  Put(image,0x12b10474-native_reference_code::kImageBase,uint32_t{0});
}
NativePlayerServiceResult Bind(UnrealObjectCloneProperty* j=&journal){return NativeBindClonedWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,j);}
void Reject(){const auto old=animator;assert(Bind()==NativePlayerServiceResult::kRejected);assert(!std::memcmp(old.bytes,animator.bytes,sizeof(old.bytes)));}
DWORD WINAPI WrongThread(void*){Reject();return 0;}
}
namespace wfcds {
bool UnrealObjectCloningReady(){return cloning_ready;}
bool UnrealBeginObjectCloneProperty(void* owner,const wchar_t* property,void* input,UnrealObjectCloneProperty* j){
  assert(owner==weapon.bytes && !std::wcscmp(property,L"OwnerAnimator") && input==source.bytes && !owner_animator && !j->pending);
  cloning_ready=false;UnrealObjectCloneProperty nested;
  assert(NativePrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes,&nested)==NativePlayerServiceResult::kRejected);
  cloning_ready=true;
  ++clone_calls;if(clone_fault==1)return false;
  *j={};j->owner=owner;j->clone=animator.bytes;j->source=input;j->clone_index=animator.id;
  j->journal_address=j;j->thread=GetCurrentThreadId();j->pending=true;
  std::wcscpy(j->property.data(),property);std::memcpy(animator.bytes,source.bytes,sizeof(animator.bytes));owner_animator=animator.bytes;
  if(clone_fault==2)++primary.id;
  if(clone_fault==3)authored_animator=other.bytes;
  if(clone_fault==4 || clone_fault==6)Put(animator.bytes,0xa8,primary.bytes);
  return true;
}
bool UnrealFinishObjectCloneProperty(UnrealObjectCloneProperty* j,bool commit){
  assert(j->pending && j->journal_address==j && owner_animator==animator.bytes);
  if((clone_fault==5 && commit) || (clone_fault==6 && !commit))return false;
  if(!commit)owner_animator=nullptr;
  *j={};return true;
}
bool NativeCaptureWeaponProgress(DWORD thread,void* p,void* w,NativeWeaponProgress* out){
  if(thread!=context.thread || thread!=GetCurrentThreadId() || p!=pawn.bytes || w!=weapon.bytes)return false;
  if(++reads==dirty_at)++primary.id;
  *out=context;return true;
}
bool UnrealObjectClonePropertyPending(const UnrealObjectCloneProperty& j){return pending && j.pending && &j==j.journal_address &&
    j.thread==GetCurrentThreadId() && j.clone==animator.bytes && j.clone_index==animator.id;}
bool UnrealObjectClassName(void* p,char* out,size_t size){auto* o=Lookup(p);if(!o)return false;std::snprintf(out,size,"%s",o->type);return true;}
bool UnrealObjectIsA(void* p,const wchar_t* type){
  return ((p==animator.bytes || p==source.bytes) && !std::wcscmp(type,L"TnWeaponOwnerAnimator")) ||
      (p==data.bytes && !std::wcscmp(type,L"TnWeaponData"));
}
uint32_t UnrealObjectIndex(void* p){auto* o=Lookup(p);return o ? o->id:0;}
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* field,uint32_t size,uint32_t* out){
  if((p!=primary.bytes && p!=additive.bytes) || std::wcscmp(field,L"__ITnAnimatorServiceVTable") || size!=4)return false;
  *out=interface_offset ? interface_offset:!std::strcmp(Lookup(p)->type,"TnRobotAnimator") ? 0x28:0x38;return true;
}
bool UnrealReadByteProperty(void* p,const wchar_t* field,uint8_t* out){if(p!=weapon.bytes || std::wcscmp(field,L"WeaponType"))return false;*out=weapon_type;return true;}
bool UnrealReadFloatProperty(void* p,const wchar_t* field,float* out){if(p!=pawn.bytes || std::wcscmp(field,L"_TransformBlendTimeRemaining"))return false;*out=blend;return true;}
bool UnrealReadObjectProperty(void* p,const wchar_t* field,void** out){
  if(p==weapon.bytes && !std::wcscmp(field,L"OwnerAnimator")){*out=owner_animator;return true;}
  if(p==data.bytes && !std::wcscmp(field,L"OwnerAnimations")){*out=authored_animator;return true;}
  if(p==pawn.bytes){
    if(!std::wcscmp(field,L"_CurrentTransformation")){*out=transformation;return true;}
    if(!std::wcscmp(field,weapon_type==6 ? L"_VehicleForm":L"_RobotForm")){*out=form.bytes;return true;}
  }
  if(p==form.bytes){
    if(!std::wcscmp(field,L"_OwnerPawn")){*out=form_owner;return true;}
    if(!std::wcscmp(field,L"_Services")){*out=container.bytes;return true;}
    if(!std::wcscmp(field,L"_Animator")){*out=primary.bytes;return true;}
    if(!std::wcscmp(field,L"_AdditiveAnimator")){*out=additive.bytes;return true;}
  }
  return false;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* field,UnrealStructArrayView* out){
  if(p!=container.bytes || std::wcscmp(field,L"_ServiceDescriptions"))return false;
  *out={};out->count=cached ? 1:0;return true;
}
bool UnrealStructArrayNameEquals(const UnrealStructArrayView&,int32_t i,const wchar_t*,const wchar_t* value,bool* out){assert(i==0);*out=!std::wcscmp(value,L"TnAnimatorService");return true;}
bool UnrealReadStructArrayObjectField(const UnrealStructArrayView&,int32_t i,const wchar_t*,void** out){assert(i==0);*out=cached;return true;}
}
int main(int argc,char** argv){
  using namespace native_weapon_animator_code;
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  if(argc>1){
    std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto check=[&](uintptr_t va,const uint8_t* code,size_t size){assert(!std::memcmp(exe.data()+va-0x10900c00,code,size));};
    check(kInitializeVa,kInitialize,sizeof(kInitialize));
    for(bool robot:{false,true}){const auto code=Query(native_reference_code::kImageBase,robot);check(robot ? kRobotQueryVa:kBasicQueryVa,code.data(),code.size());}
    const auto relocations=[&](uintptr_t va,size_t length,const std::vector<size_t>& expected){
      std::vector<size_t> found;
      for(size_t p=0x2148a00;p<0x2148a00+0x243abc;){
        uint32_t page=0,size=0;std::memcpy(&page,exe.data()+p,4);std::memcpy(&size,exe.data()+p+4,4);
        assert(size>=8 && size<=0x2148a00+0x243abc-p);
        for(size_t q=p+8;q<p+size;q+=2){
          uint16_t e=0;std::memcpy(&e,exe.data()+q,2);
          const uintptr_t address=native_reference_code::kImageBase+page+(e&0xfff);
          if((e>>12)==3 && address>=va && address<va+length)found.push_back(address-va);
        }
        p+=size;
      }
      assert(found==expected);
    };
    relocations(kInitializeVa,sizeof(kInitialize),{});
    relocations(kBasicQueryVa,sizeof(kBasicQuery),{5});
    relocations(kRobotQueryVa,sizeof(kBasicQuery),{5});
    relocations(native_reference_code::kBarrierVa,sizeof(native_reference_code::kBarrier),{15,38});
  }
  // Execute both shipping interface-query leaves as well: compiled ECX,
  // one stack argument, callee RET4. This independently checks the three-word
  // record's interface address; it is not a mocked QueryInterface result.
  using QueryFunction=void*(__attribute__((fastcall)) *)(void*,void*,const wchar_t*);
  for(bool robot:{false,true}){
    Reset();Service(primary,robot,0x1000);
    const auto query=reinterpret_cast<QueryFunction>(image+(robot ? kRobotQueryVa:kBasicQueryVa)-native_reference_code::kImageBase);
    assert(query(primary.bytes+0x10,nullptr,L"TnAnimatorService")==primary.bytes+(robot ? 0x28:0x38));
    assert(!query(primary.bytes+0x10,nullptr,L"OtherInterface"));
    assert(!query(nullptr,nullptr,L"TnAnimatorService"));
  }
  for(unsigned n=0;n<2000;++n){
    Reset(n%5);const auto original=animator;const auto old_pawn=pawn,old_weapon=weapon,old_primary=primary,old_additive=additive;
    assert(Bind()==NativePlayerServiceResult::kApplied && journal.pending);
    auto expected=original;Put(expected.bytes,0x78,uint32_t{0x47});
    const AnimatorInterface records[]={ {primary.bytes,0,primary.bytes+(n%5 ? 0x38:0x28)}, {additive.bytes,0,additive.bytes+0x38} };
    std::memcpy(expected.bytes+0xa8,records,sizeof(records));assert(!std::memcmp(expected.bytes,animator.bytes,sizeof(expected.bytes)));
    assert(!std::memcmp(pawn.bytes,old_pawn.bytes,sizeof(pawn.bytes)) && !std::memcmp(weapon.bytes,old_weapon.bytes,sizeof(weapon.bytes)) &&
        !std::memcmp(primary.bytes,old_primary.bytes,sizeof(primary.bytes)) && !std::memcmp(additive.bytes,old_additive.bytes,sizeof(additive.bytes)));
    uint32_t rescan=0;std::memcpy(&rescan,image+0x12b10474-native_reference_code::kImageBase,4);assert(rescan==1);
    Reject(); // Never initialize a bound clone again.
  }
  const auto corrupt=[&](uintptr_t va,size_t size){for(size_t i=0;i<size;++i){Reset();image[va-native_reference_code::kImageBase+i]^=1;Reject();}};
  corrupt(kInitializeVa,sizeof(kInitialize));corrupt(kRobotQueryVa,sizeof(kBasicQuery));corrupt(kBasicQueryVa,sizeof(kBasicQuery));
  corrupt(native_reference_code::kBarrierVa,sizeof(native_reference_code::kBarrier));
  Reset();pending=false;Reject();Reset();journal.owner=other.bytes;Reject();Reset();std::wcscpy(journal.property.data(),L"Other");Reject();
  Reset();auto copied=journal;assert(Bind(&copied)==NativePlayerServiceResult::kRejected);
  Reset();blend=0.1f;Reject();Reset();transformation=other.bytes;Reject();Reset();form_owner=other.bytes;Reject();
  Reset();form.type="UnknownForm";Reject();Reset();primary.type="UnknownAnimator";Reject();Reset();cached=other.bytes;Reject();
  Reset();cached=primary.bytes;assert(Bind()==NativePlayerServiceResult::kApplied);
  Reset();interface_offset=0x40;Reject();Reset();context.objects[6]=nullptr;Reject();
  Reset();dirty_at=2;Reject();Reset();dirty_at=3;assert(Bind()==NativePlayerServiceResult::kStateMismatch && journal.pending);
  Reset();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  for(unsigned n=0;n<2000;++n){
    Reset(n%5);owner_animator=nullptr;journal={};
    const auto old_pawn=pawn,old_weapon=weapon;
    assert(NativeCanPrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes));
    assert(!owner_animator && !clone_calls && !journal.pending); // genuinely read-only admission
    assert(NativePrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes,&journal)==NativePlayerServiceResult::kApplied);
    assert(owner_animator==animator.bytes && !journal.pending && clone_calls==1);
    assert(!NativeCanPrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes));
    assert(NativePrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes,&journal)==NativePlayerServiceResult::kRejected && clone_calls==1);
    assert(!std::memcmp(old_pawn.bytes,pawn.bytes,sizeof(pawn.bytes)) && !std::memcmp(old_weapon.bytes,weapon.bytes,sizeof(weapon.bytes)));
  }
  for(unsigned fault=1;fault<=6;++fault){
    Reset();owner_animator=nullptr;journal={};clone_fault=fault;
    const auto result=NativePrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes,&journal);
    assert(result==((fault==2 || fault==3 || fault==6) ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected));
    assert(journal.pending==(fault==6) && owner_animator==(fault==6 ? animator.bytes:nullptr));
    if(fault==6){clone_fault=0;assert(UnrealFinishObjectCloneProperty(&journal,false));}
  }
  Reset();owner_animator=nullptr;journal={};cloning_ready=false;
  assert(NativePrepareWeaponAnimator(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,data.bytes,&journal)==NativePlayerServiceResult::kRejected && !clone_calls);
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeWeaponAnimatorTests passed: 2000 native bindings + 2000 same-pulse prepare/commit cycles; read-only admission, six clone/commit/recovery faults, nested/repeated rejection and native signature/ABI checks (metadata/root/clone fixtures, not gameplay)");
}
