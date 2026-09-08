// Runs the production read-only retention gate. UObject/control/heat-template
// readers are fixtures. No claim of a live AI/controller handoff is made here.
#include "NativeWeaponRetention.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <limits>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x400]{};uint32_t index=0;bool valid=true;void* outer=nullptr; };
Object pawn,weapon,lerper,world,old_data,new_data,controller,other,animator,mesh,dropped,factory,heat,old_template,new_template;
Object* objects[]={&pawn,&weapon,&lerper,&world,&old_data,&new_data,&controller,&other,&animator,&mesh,&dropped,&factory,&heat,&old_template,&new_template};
uint8_t* image=nullptr;
NativeWeaponProgress context{};
NativeWeaponRetention saved{};
std::array<void*,7> refs{};
float old_max=0,new_max=0,live_max=0;
bool modifiers_match=true,bad_type=false,valid_control=true;
bool animator_ready=false;
unsigned progress_reads=0,dirty_at=0,dirty_kind=0;
uint8_t kind=0;
Object* Lookup(void* p){for(auto* o:objects)if(p==o->bytes && o->valid)return o;return nullptr;}
template<size_t N> void PutBody(uintptr_t va,const std::array<uint8_t,N>& bytes){std::memcpy(image+va-native_reference_code::kImageBase,bytes.data(),N);}
void SetDispatch(uintptr_t va){const uintptr_t table=reinterpret_cast<uintptr_t>(image+0x1f0000),fn=reinterpret_cast<uintptr_t>(image)+va-native_reference_code::kImageBase;
  std::memcpy(weapon.bytes+0x10,&table,4);std::memcpy(reinterpret_cast<void*>(table+0x228),&fn,4);}
bool Capture(NativeWeaponRetention* out,const char** reason=nullptr){return NativeCaptureWeaponRetention(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes,new_data.bytes,kind,out,reason);}
void Reset(uint8_t k=0){
  uint32_t id=1;for(auto* o:objects){*o={};o->index=id++;}kind=k;
  using namespace native_weapon_update_code;const auto base=reinterpret_cast<uintptr_t>(image);
  PutBody(kBaseVa,Base(base));PutBody(kBeamVa,Beam(base));PutBody(kGrenadeVa,Grenade(base));PutBody(kBagVa,Bag(base));
  PutBody(kRepairVa,Repair(base));
  SetDispatch(k==1 ? kBeamVa:k==2 ? kGrenadeVa:k==3 ? kBagVa:k==4 ? kRepairVa:kBaseVa);
  context={};context.thread=GetCurrentThreadId();context.captured=true;
  context.objects={pawn.bytes,weapon.bytes,lerper.bytes,world.bytes,old_data.bytes,controller.bytes,pawn.bytes};
  for(size_t i=0;i<context.objects.size();++i)context.indices[i]=Lookup(context.objects[i])->index;
  refs={animator.bytes,mesh.bytes,dropped.bytes,factory.bytes,nullptr,old_template.bytes,new_template.bytes};
  animator.outer=dropped.outer=factory.outer=weapon.bytes;
  old_max=new_max=live_max=0;modifiers_match=valid_control=true;bad_type=animator_ready=false;progress_reads=dirty_at=dirty_kind=0;
  assert(Capture(&saved));progress_reads=0;
}
void Reject(const char* reason){NativeWeaponRetention candidate;const char* why=nullptr;
  const auto before=weapon;assert(!Capture(&candidate,&why) && !candidate.captured && !std::strcmp(why,reason));
  assert(!std::memcmp(before.bytes,weapon.bytes,sizeof(weapon.bytes)));}
DWORD WINAPI WrongThread(void*){NativeWeaponRetention out;assert(!Capture(&out));assert(!NativeWeaponRetentionStable(saved));return 0;}
}
namespace wfcds {
bool NativeCanPrepareWeaponAnimator(HMODULE,DWORD thread,void* p,void* w,void* target){
  assert(thread==GetCurrentThreadId() && p==pawn.bytes && w==weapon.bytes && target==new_data.bytes);return animator_ready;
}
bool NativeCaptureWeaponProgress(DWORD thread,void* p,void* w,NativeWeaponProgress* out){
  *out={};if(thread!=GetCurrentThreadId() || thread!=context.thread || p!=pawn.bytes || w!=weapon.bytes || !valid_control)return false;
  if(++progress_reads==dirty_at){
    if(dirty_kind==1){context.objects[5]=other.bytes;context.indices[5]=other.index;}
    if(dirty_kind==2){++context.indices[0];}
    if(dirty_kind==3){context.objects[4]=other.bytes;context.indices[4]=other.index;}
  }
  *out=context;return true;
}
bool UnrealObjectIsA(void* p,const wchar_t* type){if(!Lookup(p))return false;
  if(p==old_data.bytes || p==new_data.bytes)return !std::wcscmp(type,L"TnWeaponData");
  if(p==animator.bytes || p==old_template.bytes || p==new_template.bytes)return !bad_type && !std::wcscmp(type,L"TnWeaponOwnerAnimator");
  if(p==mesh.bytes)return !std::wcscmp(type,L"TnWeaponMesh");
  if(p==dropped.bytes || p==factory.bytes)return !std::wcscmp(type,L"PrimitiveComponent");
  if(p==heat.bytes)return !std::wcscmp(type,L"TnWeaponHeat");
  return false;
}
uint32_t UnrealObjectIndex(void* p){auto* o=Lookup(p);return o ? o->index:0;}
void* UnrealObjectOuter(void* p){auto* o=Lookup(p);return o ? o->outer:nullptr;}
bool UnrealReadObjectProperty(void* p,const wchar_t* field,void** out){
  constexpr const wchar_t* names[]={L"OwnerAnimator",L"WeaponMeshTemplate",L"DroppedPickupMesh",L"PickupFactoryMesh",L"WeaponHeat"};
  if(p==weapon.bytes){for(size_t i=0;i<5;++i)if(!std::wcscmp(field,names[i])){*out=refs[i];return true;}}
  if(!std::wcscmp(field,L"OwnerAnimations") && (p==old_data.bytes || p==new_data.bytes)){*out=refs[p==old_data.bytes ? 5:6];return true;}
  return false;
}
bool UnrealReadStructFloatProperty(void* p,const wchar_t* structure,const wchar_t* field,float* out){
  if(std::wcscmp(structure,L"HeatProperties") || std::wcscmp(field,L"HeatMax") || (p!=old_data.bytes && p!=new_data.bytes))return false;
  *out=p==old_data.bytes ? old_max:new_max;return true;
}
bool NativeCaptureWeaponModifiers(DWORD thread,void* p,void* w,NativeWeaponModifiers* out){
  if(thread!=GetCurrentThreadId() || p!=pawn.bytes || w!=weapon.bytes)return false;
  *out={};out->heat=heat.bytes;out->heat_index=heat.index;out->heat_maximum=live_max;out->captured=true;return true;
}
bool NativeWeaponModifiersMatchData(const NativeWeaponModifiers&,void* p){assert(p==new_data.bytes);return modifiers_match;}
}
int main(int argc,char** argv){
  using namespace native_weapon_update_code;
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x200000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto check=[&](uintptr_t va,const auto& bytes){assert(!std::memcmp(exe.data()+va-0x10900c00,bytes,sizeof(bytes)));};
    check(kBaseVa,kBase);check(kBeamVa,kBeam);check(kGrenadeVa,kGrenade);check(kBagVa,kBag);
    check(kRepairVa,kRepair);
    assert(*reinterpret_cast<const uint32_t*>(exe.data()+0x1281b7d8+0x228-0x10901200)==kRepairVa);
    // Each ASLR operand list must equal the PE relocation directory, not just
    // produce bytes which match this test's own relocation helper.
    const auto relocations=[&](uintptr_t va,const auto& bytes,const auto& relocated){
      std::vector<size_t> actual,expected;
      for(size_t p=0x2148a00;p<0x2148a00+0x243abc;){uint32_t page=0,size=0;std::memcpy(&page,exe.data()+p,4);std::memcpy(&size,exe.data()+p+4,4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){uint16_t e=0;std::memcpy(&e,exe.data()+q,2);const uintptr_t address=native_reference_code::kImageBase+page+(e&0xfff);
          if((e>>12)==3 && address>=va && address<va+sizeof(bytes))expected.push_back(address-va);}
        p+=size;
      }
      for(size_t i=0;i<sizeof(bytes);++i)if(bytes[i]!=relocated[i]){actual.push_back(i);i+=3;}
      assert(actual==expected);
    };
    relocations(kBaseVa,kBase,Base(native_reference_code::kImageBase+1));
    relocations(kBeamVa,kBeam,Beam(native_reference_code::kImageBase+1));
    relocations(kGrenadeVa,kGrenade,Grenade(native_reference_code::kImageBase+1));
    relocations(kBagVa,kBag,Bag(native_reference_code::kImageBase+1));
    relocations(kRepairVa,kRepair,Repair(native_reference_code::kImageBase+1));
  }
  for(unsigned i=0;i<640;++i){Reset(i%5);const auto before=weapon;
    assert(NativeWeaponRetentionStable(saved));
    context.objects[4]=new_data.bytes;context.indices[4]=new_data.index;
    context.objects[5]=other.bytes;context.indices[5]=other.index;
    assert(NativeWeaponRetentionStable(saved));
    context.objects[4]=old_data.bytes;context.indices[4]=old_data.index;
    assert(NativeWeaponRetentionStable(saved));assert(!std::memcmp(before.bytes,weapon.bytes,sizeof(weapon.bytes)));
  }
  const auto corrupt=[&](uint8_t k,uintptr_t va,size_t length){for(size_t i=0;i<length;++i){Reset(k);image[va-native_reference_code::kImageBase+i]^=1;
      Reject("unsupported-native-UpdateData");assert(!NativeWeaponRetentionStable(saved));}};
  corrupt(0,kBaseVa,sizeof(kBase));corrupt(1,kBeamVa,sizeof(kBeam));corrupt(2,kGrenadeVa,sizeof(kGrenade));corrupt(3,kBagVa,sizeof(kBag));
  corrupt(4,kRepairVa,sizeof(kRepair));corrupt(4,kBeamVa,sizeof(kBeam));corrupt(4,kBaseVa,sizeof(kBase));
  Reset();SetDispatch(0x1093c560);Reject("unsupported-native-UpdateData");
  Reset(1);SetDispatch(kBaseVa);Reject("unsupported-native-UpdateData");
  Reset();kind=4;Reject("unsupported-native-UpdateData");
  Reset(4);SetDispatch(kBeamVa);Reject("unsupported-native-UpdateData");
  Reset();uintptr_t invalid=0;std::memcpy(weapon.bytes+0x10,&invalid,4);Reject("unsupported-native-UpdateData");
  Reset();bad_type=true;Reject("invalid-retained-service");
  Reset();animator.outer=other.bytes;Reject("foreign-owner-animator");
  Reset();dropped.outer=other.bytes;Reject("foreign-pickup-mesh");
  Reset();refs[0]=nullptr;Reject("owner-animator-needs-initialization");
  animator_ready=true;assert(Capture(&saved) && saved.needs_animator && !saved.services[0]);
  assert(NativeWeaponRetentionStable(saved));
  animator_ready=false;assert(!NativeWeaponRetentionStable(saved));
  animator_ready=true;new_max=1;Reject("heat-role-tick-incompatible");new_max=0;
  refs[0]=animator.bytes;NativeWeaponRetention initialized{};assert(Capture(&initialized) && !initialized.needs_animator);
  assert(!SameWeaponRetentionIdentity(saved,initialized));
  saved.services[0]=initialized.services[0];saved.indices[0]=initialized.indices[0];saved.needs_animator=false;
  assert(SameWeaponRetentionIdentity(saved,initialized) && NativeWeaponRetentionStable(saved));
  Reset();refs[0]=nullptr;
  refs[6]=nullptr;assert(Capture(&saved)); // genuinely animation-less data
  Reset();refs[0]=nullptr;context.objects[6]=nullptr;context.indices[6]=0;assert(Capture(&saved)); // native skips initialization when holstered
  context.objects[6]=pawn.bytes;context.indices[6]=pawn.index;
  assert(!NativeWeaponRetentionStable(saved)); // holstered admission is not an equip admission
  Reset();context.objects[6]=nullptr;context.indices[6]=0;
  assert(!NativeWeaponRetentionStable(saved)); // even an existing animator cannot hide changed instigation
  Reset();++context.indices[6];assert(!NativeWeaponRetentionStable(saved));
  Reset();new_max=1;Reject("heat-role-tick-incompatible");
  Reset();old_max=new_max=1;Reject("missing-heat-service");
  Reset();new_max=std::numeric_limits<float>::infinity();Reject("invalid-authored-heat");
  Reset();refs[4]=heat.bytes;old_max=new_max=live_max=100;assert(Capture(&saved));assert(NativeWeaponRetentionStable(saved));
  live_max=0;Reject("heat-needs-initialization-or-adaptation");live_max=100;modifiers_match=false;Reject("heat-needs-initialization-or-adaptation");
  for(unsigned field=0;field<7;++field){Reset();++Lookup(refs[field] ? refs[field]:heat.bytes)->index;
    if(refs[field])assert(!NativeWeaponRetentionStable(saved));}
  Reset();++new_data.index;assert(!NativeWeaponRetentionStable(saved));
  Reset();++old_data.index;assert(!NativeWeaponRetentionStable(saved));
  for(unsigned d=1;d<=3;++d){Reset();dirty_kind=d;dirty_at=2;assert(!NativeWeaponRetentionStable(saved));
    Reset();dirty_kind=d;dirty_at=progress_reads+2;NativeWeaponRetention out;assert(!Capture(&out));}
  Reset();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  Reset();DWORD old=0;assert(VirtualProtect(image+0xe6000,4096,PAGE_READWRITE,&old));Reject("unsupported-native-UpdateData");
  assert(VirtualProtect(image+0xe6000,4096,old,&old));assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeWeaponRetentionTests passed: 640 role round trips; complete native bodies/ASLR/corruption checks including Repair->Beam->Base; retained animation/mesh/heat compatibility, stale identity/control and unknown dispatch rejection (read-only fixtures, not gameplay)");
}
