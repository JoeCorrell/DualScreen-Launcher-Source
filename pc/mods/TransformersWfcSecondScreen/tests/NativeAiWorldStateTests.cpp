// Actual shipping refresh, class lookup, getters, all three GOAP setters,
// array helpers and GC barrier. Metadata/IsA/input/planning are fixtures.
#include "NativeAiWorldState.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object {alignas(4) uint8_t bytes[0x900]{};uint32_t id=0;const wchar_t* type=L"Object";bool valid=true,deleting=false;};
Object controller,pawn,world,collection,form,weapon,pc,base_weapon,ai_class,weapon_class,keys[3],defaults[3],entry_type,value_type,foreign;
Object* objects[]={&controller,&pawn,&world,&collection,&form,&weapon,&pc,&base_weapon,&ai_class,&weapon_class,
  &keys[0],&keys[1],&keys[2],&defaults[0],&defaults[1],&defaults[2],&entry_type,&value_type,&foreign};
std::array<uint8_t,256*24> entries{};
uint8_t* image=nullptr;uint8_t mode=0,vehicle_type=0;
bool writes=true,held=true,plan=true,bad_layout=false,form_ready=true;
unsigned late_fault=0,metadata_reads=0;
DWORD game_thread=0;
NativePlayerInputLease move,look;
template<class T>T Read(const void* p,size_t off){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+off,sizeof(v));return v;}
template<class T>void Put(void* p,size_t off,T v){std::memcpy(static_cast<uint8_t*>(p)+off,&v,sizeof(v));}
Object* Find(void* p){for(auto* o:objects)if(o->bytes==p && o->valid)return o;return nullptr;}
void* At(uintptr_t va){return image+va-native_reference_code::kImageBase;}
void Ptr(Object& o,size_t off,Object* value){Put(o.bytes,off,value ? static_cast<void*>(value->bytes):nullptr);}
void Jump(uintptr_t va,void* fn){auto* p=static_cast<uint8_t*>(At(va));p[0]=0xE9;Put(p,1,uint32_t(reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(p)-5));}
bool Ready(){return PrepareNativeAiWorldState(reinterpret_cast<HMODULE>(image),game_thread,controller.bytes,pawn.bytes);}
NativeAiWorldStateResult Refresh(){return NativeRefreshAiWorldState(controller.bytes,pawn.bytes,move,look);}
void Getter(Object& o,uint32_t slot,uintptr_t table_va,uintptr_t code_va,uint8_t bit) {
  Put(o.bytes,0x10,At(table_va));Put(At(table_va),slot,At(code_va));
  auto* b=static_cast<uint8_t*>(At(code_va));b[0]=0xB8;Put(b,1,uint32_t(bit));b[5]=0xC3;
}
void Reset(bool has_weapon=true,bool hot=false,uint8_t current_form=0,int32_t count=16,int32_t capacity=16) {
  uint32_t id=1;for(auto* o:objects){*o={};o->id=id++;}
  writes=held=plan=form_ready=true;bad_layout=false;late_fault=0;mode=current_form;vehicle_type=2;
  controller.type=L"TnAiController";pawn.type=L"TnAiPawn";world.type=L"WorldInfo";
  collection.type=L"HmGoapWorldStateCollection";form.type=L"TnTransformerForm";weapon.type=L"TnWeapon";pc.type=L"TnPlayerController";
  for(auto* o:{&base_weapon,&ai_class,&weapon_class,&keys[0],&keys[1],&keys[2]}){o->type=L"Class";Put(o->bytes,8,0x800000u);}
  for(auto* o:{&controller,&pawn,&weapon,&pc}){Ptr(*o,0x98,&world);Put(o->bytes,0x66,uint8_t{3});}
  Ptr(controller,0x1C8,&pawn);Ptr(pawn,0x1D4,&controller);Ptr(pawn,0x2C,&controller);
  Ptr(controller,0x300,&collection);Ptr(pawn,0x584,&form);Ptr(pawn,0x428,has_weapon ? &weapon:nullptr);Ptr(weapon,0x94,&pawn);
  Put(weapon.bytes,0x14,7u);Put(At(0x12A29FD4+7*4),0,static_cast<void*>(weapon_class.bytes));
  Getter(weapon,0x350,0x12B30000,0x12B31000,hot);Getter(form,0x70,0x12B32000,0x12B33000,mode);
  Put(At(0x12B70270),0,static_cast<void*>(base_weapon.bytes+0x10));Put(At(0x12B6ECD0),0,static_cast<void*>(ai_class.bytes+0x10));
  const uintptr_t caches[]={0x12B6EC3C,0x12B6EBAC,0x12B6EBB8};
  entries.fill(0xA4);
  for(size_t i=0;i<3;++i) {
    Put(At(caches[i]),0,static_cast<void*>(keys[i].bytes+0x10));Ptr(keys[i],0x9C,&defaults[i]);Put(defaults[i].bytes,0x28,int32_t(i));
    Put(entries.data(),i*24,static_cast<void*>(keys[i].bytes));Put(entries.data(),i*24+4,static_cast<void*>(nullptr));
  }
  Put(entries.data(),12,static_cast<void*>(base_weapon.bytes));Put(entries.data(),24+16,0xA4u);Put(entries.data(),48+8,int32_t(1-mode));
  Put(collection.bytes,0x28,entries.data());Put(collection.bytes,0x2C,count);Put(collection.bytes,0x30,capacity);Put(collection.bytes,0x34,1u);
  Put(At(0x12B1047C),0,0u);Put(At(0x12B10474),0,0u);
  move={};move.controller=pc.bytes;move.world=world.bytes;move.axis=NativeInputAxis::kMove;move.held=true;
  look=move;look.axis=NativeInputAxis::kLook;
}
void Reject(){auto before=entries;assert(Refresh()==NativeAiWorldStateResult::kRejected);assert(entries==before);assert(Read<uint32_t>(At(0x12B10474),0)==0);}
void Pass(bool has_weapon,bool hot,uint8_t current_form,int32_t count=16,int32_t cap=16) {
  Reset(has_weapon,hot,current_form,count,cap);assert(Ready());auto expected=entries;const auto old_pawn=pawn,old_weapon=weapon,old_form=form,old_controller=controller;
  Put(expected.data(),12,static_cast<void*>(has_weapon ? weapon_class.bytes:base_weapon.bytes));
  Put(expected.data(),24+16,0xA4u+uint32_t(has_weapon && hot));Put(expected.data(),48+8,int32_t(current_form));
  assert(Refresh()==NativeAiWorldStateResult::kApplied);assert(NativeAiWorldStateMatchesPawn(controller.bytes,pawn.bytes));
  assert(entries==expected);assert(Read<uint32_t>(At(0x12B10474),0)==1);
  assert(!std::memcmp(old_pawn.bytes,pawn.bytes,sizeof(pawn.bytes)) && !std::memcmp(old_weapon.bytes,weapon.bytes,sizeof(weapon.bytes)) &&
      !std::memcmp(old_form.bytes,form.bytes,sizeof(form.bytes)) && !std::memcmp(old_controller.bytes,controller.bytes,sizeof(controller.bytes)));
  assert(Read<int32_t>(collection.bytes,0x2C)==count && Read<int32_t>(collection.bytes,0x30)==cap);
  assert(Refresh()==NativeAiWorldStateResult::kApplied);assert(entries==expected);
}
DWORD WINAPI OtherThread(void*){const auto prior=metadata_reads;assert(!Ready());Reject();assert(metadata_reads==prior);return 0;}
}
extern "C" int StateTestCastImpl(void* object,void* cls){
  assert(cls==base_weapon.bytes || cls==ai_class.bytes);
  return UnrealObjectIsA(object,cls==base_weapon.bytes ? L"TnWeapon":L"TnAiPawn");
}
extern "C" __attribute__((naked)) void StateTestCast(){asm volatile("pushl 4(%esp); pushl %ecx; call _StateTestCastImpl; addl $8,%esp; ret $4");}
extern "C" void StateNoAllocation(){assert(false && "native no-allocation guard failed");}
namespace wfcds {
uint32_t UnrealObjectIndex(void* p){++metadata_reads;auto* o=Find(p);return o ? o->id:0;}
void* UnrealObjectAt(int32_t index){
  if(late_fault && Read<uint32_t>(At(0x12B10474),0)) {
    const auto fault=late_fault;late_fault=0;
    if(fault==1)pawn.valid=false;
    if(fault==2)Ptr(controller,0x1C8,&foreign);
    if(fault==3)held=false;
    if(fault==4)plan=false;
    if(fault==5)writes=false;
    if(fault==6)Put(entries.data(),24+16,Read<uint32_t>(entries.data(),24+16)^1u);
    if(fault==7)Ptr(controller,0x300,&foreign);
  }
  for(auto* o:objects)if(o->valid && o->id==uint32_t(index+1))return o->bytes;
  return nullptr;
}
bool UnrealObjectIsA(void* p,const wchar_t* type){++metadata_reads;auto* o=Find(p);return o && !std::wcscmp(o->type,type);}
void* UnrealObjectClass(void* p){if(p==weapon.bytes)return weapon_class.bytes;
  for(size_t i=0;i<3;++i)if(p==defaults[i].bytes && defaults[i].valid)return keys[i].bytes;
  return nullptr;}
bool UnrealResolveClassByPath(const wchar_t* name,void** out){
  const wchar_t* paths[]={L"TransGame.TnWeapon",L"TransGame.TnAiPawn",L"TransGame.TnAiWorldStateCurrentWeapon",L"TransGame.TnAiWorldStateWeaponOverheated",L"TransGame.TnAiWorldStateTransformerForm"};
  Object* values[]={&base_weapon,&ai_class,&keys[0],&keys[1],&keys[2]};
  for(size_t i=0;i<5;++i)if(!std::wcscmp(paths[i],name) && values[i]->valid){*out=values[i]->bytes;return true;}
  return false;
}
bool UnrealResolveObjectPropertyOffset(void*,const wchar_t* name,uint32_t* out){
  if(bad_layout)return false;
  const struct {const wchar_t* name;uint32_t offset;} fields[]={
    {L"Pawn",0x1C8},{L"Controller",0x1D4},{L"Owner",0x2C},{L"WorldInfo",0x98},
    {L"AiWorldState",0x300},{L"_CurrentForm",0x584},{L"Weapon",0x428},{L"Instigator",0x94}};
  for(const auto& f:fields)if(!std::wcscmp(name,f.name)){*out=f.offset;return true;}
  return false;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out){uint32_t offset=0;
  if(!Find(p) || !UnrealResolveObjectPropertyOffset(p,name,&offset))return false;
  *out=Read<void*>(p,offset);return true;}
bool UnrealResolveBytePropertyOffset(void*,const wchar_t* name,uint32_t* out){
  if(!std::wcscmp(name,L"Role"))*out=0x66;else if(!std::wcscmp(name,L"NetMode"))*out=0x39C;else return false;return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out){uint32_t offset=0;
  if(!Find(p) || !UnrealResolveBytePropertyOffset(p,name,&offset))return false;
  *out=Read<uint8_t>(p,offset);return true;}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out){auto* o=Find(p);if(!o)return false;
  if(!std::wcscmp(name,L"bDeleteMe"))*out=o->deleting;
  else if(p==collection.bytes && !std::wcscmp(name,L"_HashCreated"))*out=(Read<uint32_t>(p,0x34)&1)!=0;
  else return false;
  return true;}
bool UnrealReadIntProperty(void* p,const wchar_t* name,int32_t* out){
  if(!Find(p) || std::wcscmp(name,L"HashIndex"))return false;
  *out=Read<int32_t>(p,0x28);return true;}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out){
  if(p!=collection.bytes || std::wcscmp(name,L"Properties"))return false;
  *out={Read<void*>(p,0x28),Read<int32_t>(p,0x2C),Read<int32_t>(p,0x30),24,entry_type.bytes};return true;}
bool UnrealResolveStructFieldOffset(const UnrealStructValueView& v,const wchar_t* name,const wchar_t* type,uint32_t size,uint32_t* out){
  if(v.struct_definition==entry_type.bytes && !std::wcscmp(name,L"Value") && !std::wcscmp(type,L"StructProperty") && size==12)*out=8;
  else if(v.struct_definition==value_type.bytes && size==4) {
    if(!std::wcscmp(name,L"ClassVal") && !std::wcscmp(type,L"ClassProperty"))*out=4;
    else if(!std::wcscmp(name,L"BoolVal") && !std::wcscmp(type,L"BoolProperty"))*out=8;
    else if(!std::wcscmp(name,L"IntVal") && !std::wcscmp(type,L"IntProperty"))*out=0;else return false;
  }else return false;return true;
}
bool UnrealReadStructObjectField(const UnrealStructValueView& v,const wchar_t* name,void** out){
  if(v.struct_definition==entry_type.bytes && !std::wcscmp(name,L"Type"))*out=Read<void*>(v.data,0);
  else if(v.struct_definition==entry_type.bytes && !std::wcscmp(name,L"Subject"))*out=Read<void*>(v.data,4);
  else if(v.struct_definition==value_type.bytes && !std::wcscmp(name,L"ClassVal"))*out=Read<void*>(v.data,4);
  else return false;
  return true;
}
bool UnrealReadStructStructField(const UnrealStructValueView& v,const wchar_t* name,UnrealStructValueView* out){
  if(v.struct_definition!=entry_type.bytes || std::wcscmp(name,L"Value"))return false;
  *out={static_cast<const uint8_t*>(v.data)+8,12,value_type.bytes};return true;}
bool UnrealReadStructIntField(const UnrealStructValueView& v,const wchar_t* name,int32_t* out){
  if(v.struct_definition!=value_type.bytes || std::wcscmp(name,L"IntVal"))return false;
  *out=Read<int32_t>(v.data,0);return true;}
bool NativeReadPawnForm(void* p,uint8_t* m,uint8_t* type){if(p!=pawn.bytes || !form_ready)return false;*m=mode;*type=vehicle_type;return true;}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease){return held && lease.held && !lease.invalidated;}
bool NativeAiPlanHandoffReady(void* p){return plan && p==controller.bytes;}
bool UnrealObjectReferenceWritesReady(){return writes;}
}
int main(int argc,char** argv) {
  game_thread=GetCurrentThreadId();
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);
  native_ai_world_state_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);std::memcpy(At(va),bytes.data(),bytes.size());return true;});
  Jump(0x10901430,reinterpret_cast<void*>(&StateTestCast));Jump(0x10AAB7F0,reinterpret_cast<void*>(&StateNoAllocation));
  Jump(0x116E0560,reinterpret_cast<void*>(&StateNoAllocation)); // No lazy FindClass path.
  if(argc>1){std::ifstream stream(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(stream),{}};assert(exe.size()==37667328);
    native_ai_world_state_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){const auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){const auto entry=Read<uint16_t>(exe.data(),q);const uintptr_t address=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && address>=va && address+4<=va+sizeof(code))Put(expected.data(),address-va,Read<uint32_t>(expected.data(),address-va)+uint32_t(base-native_reference_code::kImageBase));}p+=size;
      }assert(expected==native_reference_code::Relocate(code,base,relocs));return true;});
  }
  for(unsigned i=0;i<2000;++i)Pass(i%3!=0,i%2!=0,(i/2)%2);
  // Include retained-capacity cases; equal SetLength must not secretly shrink.
  for(int count=3;count<=256;count+=13)for(int cap=count;cap<=256;cap+=19) {
    const bool shrinking=3*count<2*cap && cap-count>64;
    if(shrinking){Reset(true,false,0,count,cap);assert(!Ready());Reject();}
    else Pass(true,true,1,count,cap);
  }
  // Execute refresh with the real role hooks installed. The AI getter's own
  // intact patch/trampoline is accepted; physical AI ownership stays native.
  native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);std::memcpy(At(va),bytes.data(),bytes.size());return true;});
  assert(InstallNativeRolePawnGetters(reinterpret_cast<HMODULE>(image),game_thread,
      [](void*,bool)->void*{assert(false && "physical AI refresh must not require a cross-role resolver");return nullptr;}));
  Pass(true,true,1);Pass(false,false,0);
  assert(RemoveNativeRolePawnGetters());
  for(unsigned fault=1;fault<=7;++fault){Reset();assert(Ready());late_fault=fault;
    assert(Refresh()==NativeAiWorldStateResult::kStateMismatch);assert(!NativeAiWorldStateMatchesPawn(controller.bytes,pawn.bytes) || fault==3 || fault==4 || fault==5);}
  Reset();assert(Ready());pawn.type=L"TnPlayerPawn";assert(!Ready());Reject();
  Reset();assert(Ready());Ptr(pawn,0x1D4,&pc);Reject();
  Reset();assert(Ready());plan=false;Reject();
  Reset();assert(Ready());held=false;Reject();
  Reset();assert(Ready());writes=false;Reject();
  Reset();assert(Ready());bad_layout=true;Reject();
  Reset();assert(Ready());form_ready=false;Reject();
  Reset();assert(Ready());Put(At(0x12B6EC3C),0,static_cast<void*>(nullptr));Reject();
  Reset();assert(Ready());Put(entries.data(),4,static_cast<void*>(pawn.bytes));Reject();
  Reset();assert(Ready());Put(defaults[2].bytes,0x28,1);Reject();
  Reset();assert(Ready());Put(world.bytes,0x39C,uint8_t{1});Reject();
  Reset();assert(Ready());static_cast<uint8_t*>(At(0x12B31000))[0]=0xC3;Reject();
  Reset();assert(Ready());HANDLE t=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(t);assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  Reset();assert(Ready());
  native_ai_world_state_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=static_cast<uint8_t*>(At(va));
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!Ready());Reject();p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeAiWorldStateTests passed: 2,000 native refresh/idle cycles, capacity boundaries, GC, state/identity/input guards; metadata/IsA/planning/input are fixtures, not gameplay");
}
