// Runs the actual factory, tank specialization, owner/path initializers and
// all four native form callbacks. UObject allocation, class metadata, input
// leases and GOAP readiness are fixtures. This is NOT a live character swap.
#include "NativeVehicleAi.cpp"
#include "NativeRolePawnCode.h"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x900]{};uint32_t id=0;const wchar_t* type=L"Object";void* outer=nullptr;bool valid=true,deleting=false; };
Object ai,pawn,player,player_pawn,world,form,blueprint,vehicle_input,old_strategy,strategy,path,old_path,foreign;
Object ai_class,tank_ai_class,car_class,plane_class,tank_class,giant_class;
Object car_form_class,plane_form_class,tank_form_class,car_blueprint_class,plane_blueprint_class,tank_blueprint_class;
Object goap,goap_key,goap_defaults,goap_entry_definition,goap_value_definition;
std::array<uint8_t,32*24> goap_entries{};
Object* all[]={&ai,&pawn,&player,&player_pawn,&world,&form,&blueprint,&vehicle_input,&old_strategy,&strategy,&path,&old_path,&foreign,
  &ai_class,&tank_ai_class,&car_class,&plane_class,&tank_class,&giant_class,
  &car_form_class,&plane_form_class,&tank_form_class,&car_blueprint_class,&plane_blueprint_class,&tank_blueprint_class,
  &goap,&goap_key,&goap_defaults,&goap_entry_definition,&goap_value_definition};
uint8_t* image=nullptr;uint8_t vehicle_type=0,mode=0;
unsigned allocations=0,mutation=0,late_mutation=0;bool plan_ready=true,input_held=true,writes=true,bad_layout=false,reenter=false;
NativePlayerInputLease move,look;
Object* Find(void* p){for(auto* o:all)if(p==o->bytes && o->valid)return o;return nullptr;}
template<class T>T Read(const void* p,size_t o){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+o,sizeof(v));return v;}
template<class T>void Put(void* p,size_t o,T v){std::memcpy(static_cast<uint8_t*>(p)+o,&v,sizeof(v));}
void Ptr(Object& o,size_t at,Object* p){Put(o.bytes,at,p ? static_cast<void*>(p->bytes):nullptr);}
void Jump(uintptr_t va,void* fn){auto* p=image+va-native_reference_code::kImageBase;p[0]=0xE9;
  Put(p,1,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(p+5)));}
bool Ready(){return PrepareNativeVehicleAi(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),ai.bytes,pawn.bytes);}
NativeVehicleAiResult Rebuild(){return NativeRebuildVehicleAi(ai.bytes,pawn.bytes,move,look);}
NativeVehicleAiResult Restore(void* original,uint32_t index){return NativeRestoreVehicleAi(ai.bytes,pawn.bytes,original,index,move,look);}
void Configure(Object& s,Object& follower,bool initialized){
  s.outer=ai.bytes;follower.outer=s.bytes;follower.type=L"TnPathFollower";
  const bool giant=!std::wcscmp(pawn.type,L"TnTankAiPawn");
  s.type=vehicle_type<=1 ? L"TnVehicleCarAiControlStrategy":vehicle_type==2 ? L"TnVehiclePlaneAiControlStrategy":
      giant ? L"TnVehicleGiantTankAiControlStrategy":L"TnVehicleTankAiControlStrategy";
  const uintptr_t callback=vehicle_type<=1 ? vehicle_code::kCarInitializeVa:vehicle_type==2 ? vehicle_code::kPlaneInitializeVa:
      giant ? vehicle_code::kGiantInitializeVa:vehicle_code::kTankInitializeVa;
  void* table=image+0x2201000+(&s==&strategy ? 0:0x100);
  Put(s.bytes,0x10,table);Put(table,0x38,static_cast<void*>(image+callback-native_reference_code::kImageBase));
  Ptr(s,0x64,&follower);Ptr(s,0x28,initialized ? &ai:nullptr);Ptr(s,0x2C,initialized ? &pawn:nullptr);
  Ptr(follower,0x28,initialized ? &ai:nullptr);Ptr(follower,0x2C,initialized ? &pawn:nullptr);
  Ptr(s,0x74,initialized ? &blueprint:nullptr);Ptr(s,0x78,initialized ? &form:nullptr);
}
void Reset(uint8_t type=0,bool giant=false,uint8_t current_mode=0){
  uint32_t index=1;for(auto* o:all){*o={};o->id=index++;}
  vehicle_type=type;mode=current_mode;allocations=mutation=late_mutation=0;
  plan_ready=input_held=writes=true;bad_layout=reenter=false;
  ai.type=L"TnAiController";pawn.type=giant ? L"TnTankAiPawn":L"TnAiPawn";
  player.type=L"TnPlayerController";player_pawn.type=L"TnPlayerPawn";world.type=L"WorldInfo";
  ai_class.type=L"TnAiPawn";tank_ai_class.type=L"TnTankAiPawn";
  car_class.type=L"TnVehicleCarAiControlStrategy";plane_class.type=L"TnVehiclePlaneAiControlStrategy";
  tank_class.type=L"TnVehicleTankAiControlStrategy";giant_class.type=L"TnVehicleGiantTankAiControlStrategy";
  car_form_class.type=L"TnCarForm";plane_form_class.type=L"TnPlaneForm";tank_form_class.type=L"TnTankForm";
  car_blueprint_class.type=L"TnCarFormBlueprint";plane_blueprint_class.type=L"TnPlaneFormBlueprint";tank_blueprint_class.type=L"TnTankFormBlueprint";
  form.type=type<=1 ? L"TnCarForm":type==2 ? L"TnPlaneForm":L"TnTankForm";
  blueprint.type=type<=1 ? L"TnCarFormBlueprint":type==2 ? L"TnPlaneFormBlueprint":L"TnTankFormBlueprint";
  for(auto* o:{&ai,&pawn,&player,&player_pawn}){Ptr(*o,0x98,&world);o->bytes[0x66]=3;Put(o->bytes,8,0x800000u);}
  Ptr(ai,0x1C8,&pawn);Ptr(pawn,0x1D4,&ai);Ptr(pawn,0x2C,&ai);
  goap_entries.fill(0);goap.type=L"HmGoapWorldStateCollection";Ptr(ai,0x300,&goap);
  Ptr(goap_key,0x9C,&goap_defaults);Put(goap_defaults.bytes,0x28,20);
  Put(goap.bytes,0x28,goap_entries.data());Put(goap.bytes,0x2C,32);Put(goap.bytes,0x30,32);Put(goap.bytes,0x34,1u);
  Put(goap_entries.data(),20*24,static_cast<void*>(goap_key.bytes));Put(goap_entries.data(),20*24+8,2);
  Ptr(player,0x1C8,&player_pawn);Ptr(player,0x300,&player_pawn);Ptr(player_pawn,0x1D4,&player);Ptr(player_pawn,0x2C,&player);
  Ptr(pawn,0x580,type==4 ? nullptr:&form);Ptr(form,0x74,&blueprint);
  Put(pawn.bytes,0xD8,120.5f);Put(pawn.bytes,0xDC,-320.0f);Put(pawn.bytes,0xE0,18.25f);
  Put(pawn.bytes,0xE4,120);Put(pawn.bytes,0xE8,28000);Put(pawn.bytes,0xEC,-20);
  vehicle_input.type=type<=1 ? L"TnCarInputState":type==2 ? L"TnPlaneInputState":L"TnTankInputState";
  Ptr(form,type==3 ? 0x104:0x108,&vehicle_input);Ptr(vehicle_input,0x2C,&pawn);
  Put(vehicle_input.bytes,type==2 ? 0x4C:0x48,0xA5u);
  Put(form.bytes,8,0x800000u);Put(blueprint.bytes,8,0x800000u);
  void* table=image+0x2202000;Put(form.bytes,0x10,table);void* getter=image+0x2203000;
  Put(table,0xCC,getter);static_cast<uint8_t*>(getter)[0]=0xB0;static_cast<uint8_t*>(getter)[1]=type;static_cast<uint8_t*>(getter)[2]=0xC3;
  const uintptr_t boost=type<=1 ? native_ai_stop_code::kCarBoostVa:type==2 ? native_ai_stop_code::kPlaneBoostVa:native_ai_stop_code::kTankBoostVa;
  Put(table,0xD0,static_cast<void*>(image+boost-native_reference_code::kImageBase));
  Configure(old_strategy,old_path,true);Ptr(ai,0x3CC,type==4 ? nullptr:&old_strategy);
  move={};move.controller=player.bytes;move.world=world.bytes;move.axis=NativeInputAxis::kMove;move.held=true;
  look=move;look.axis=NativeInputAxis::kLook;
  Put(image,0x12B1047C-native_reference_code::kImageBase,0u);Put(image,0x12B10474-native_reference_code::kImageBase,0u);
  assert(Ready());assert(NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));
}
void Reject(){const auto previous=ai;assert(Rebuild()==NativeVehicleAiResult::kRejected);
  assert(!std::memcmp(previous.bytes,ai.bytes,sizeof(ai.bytes)));assert(!allocations);}
DWORD WINAPI OtherThread(void*){assert(!Ready());Reject();assert(Restore(old_strategy.bytes,old_strategy.id)==NativeVehicleAiResult::kRejected);return 0;}
}
extern "C" void* VehicleTestClass(void* cache,void*){
  const auto va=reinterpret_cast<uintptr_t>(cache)-reinterpret_cast<uintptr_t>(image)+native_reference_code::kImageBase;
  Object* result=nullptr;
  switch(va){
    case 0x12B6ECD0:result=&ai_class;break;case 0x12B6EE84:result=&tank_ai_class;break;
    case 0x12B702B8:result=&car_class;break;case 0x12B70298:result=&plane_class;break;
    case 0x12B70294:result=&tank_class;break;case 0x12B7029C:result=&giant_class;break;
    case 0x12B6E88C:result=&car_form_class;break;case 0x12B6F418:result=&plane_form_class;break;case 0x12B6EE78:result=&tank_form_class;break;
    case 0x12B6E888:result=&car_blueprint_class;break;case 0x12B6F414:result=&plane_blueprint_class;break;case 0x12B6EE74:result=&tank_blueprint_class;break;
    default:assert(false);
  }return result->bytes+0x10;
}
extern "C" int VehicleTestCastImpl(void* p,void* cls){auto* type=Find(cls);assert(type);
  const bool result=UnrealObjectIsA(p,type->type);
  if(p==blueprint.bytes && late_mutation){
    if(late_mutation==1)++pawn.id;
    if(late_mutation==2)input_held=false;
    if(late_mutation==3)++path.id;
    if(late_mutation==4)Ptr(path,0x2C,&foreign);
    if(late_mutation==5)++player_pawn.id;
  }return result ? 1:0;
}
extern "C" __attribute__((naked)) void VehicleTestCast(){asm volatile("pushl 4(%esp)\n pushl %ecx\n call _VehicleTestCastImpl\n addl $8,%esp\n ret $4");}
extern "C" __attribute__((naked)) int VehicleTestSetInt(void*,void*,void*,int){
  asm volatile("pushl %ebp\n movl %esp,%ebp\n movl 8(%ebp),%edx\n movl 12(%ebp),%ecx\n movl 16(%ebp),%eax\n pushl $16\n pushl 20(%ebp)\n call *%edx\n leave\n ret");
}
extern "C" void* VehicleTestAllocate(void* cls,void* outer,uint32_t name,uint32_t flags,void* templ,void* error,void*,void* graph,void* instancer){
  assert(outer==ai.bytes && !name && !flags && !templ && !error && !graph && !instancer);++allocations;
  Configure(strategy,path,false);assert(!std::wcscmp(Find(cls)->type,strategy.type));
  Put(strategy.bytes,8,0x800000u);
  if(reenter){assert(!Ready());assert(!NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));assert(Rebuild()==NativeVehicleAiResult::kRejected);}
  if(mutation==1)Ptr(ai,0x1C8,&foreign);
  if(mutation==2)Ptr(pawn,0x580,&foreign);
  if(mutation==3)++pawn.id;
  if(mutation==4)input_held=false;
  if(mutation==5)plan_ready=false;
  if(mutation==6){void* table=Read<void*>(strategy.bytes,0x10);Put(table,0x38,reinterpret_cast<void*>(VehicleAddress(vehicle_code::kInitializeVa)));}
  if(mutation==7)Ptr(strategy,0x64,&old_path);
  if(mutation==8)Ptr(form,0x74,nullptr);
  if(mutation==9)return nullptr;
  if(mutation==10)return old_strategy.bytes;
  if(mutation==11)Ptr(ai,0x3CC,&foreign);
  if(mutation==12)strategy.outer=foreign.bytes;
  if(mutation==13)++player_pawn.id;
  if(mutation==14){foreign.type=L"TnPlayerPawn";Ptr(player,0x1C8,&foreign);Ptr(player,0x300,&foreign);
    Ptr(foreign,0x1D4,&player);Ptr(foreign,0x2C,&player);Ptr(foreign,0x98,&world);}
  if(mutation==15)++goap.id;
  if(mutation==16)Put(goap_entries.data(),20*24+8,0);
  return strategy.bytes;
}
namespace wfcds {
void* UnrealObjectClass(void* p){return p==goap_defaults.bytes ? goap_key.bytes:nullptr;}
bool UnrealResolveClassByPath(const wchar_t* name,void** out){
  if(std::wcscmp(name,L"TransGame.TnAiWorldStateVehicleMode") || !goap_key.valid)return false;
  *out=goap_key.bytes;return true;}
bool UnrealReadIntProperty(void* p,const wchar_t* name,int32_t* out){
  if(p!=goap_defaults.bytes || std::wcscmp(name,L"HashIndex"))return false;
  *out=Read<int32_t>(p,0x28);return true;}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out){
  if(p!=goap.bytes || std::wcscmp(name,L"Properties"))return false;
  *out={Read<void*>(p,0x28),Read<int32_t>(p,0x2C),Read<int32_t>(p,0x30),24,goap_entry_definition.bytes};return true;}
bool UnrealReadStructObjectField(const UnrealStructValueView& v,const wchar_t* name,void** out){
  if(v.struct_definition!=goap_entry_definition.bytes || v.size!=24)return false;
  if(!std::wcscmp(name,L"Type"))*out=Read<void*>(v.data,0);
  else if(!std::wcscmp(name,L"Subject"))*out=Read<void*>(v.data,4);else return false;
  return true;}
bool UnrealResolveStructFieldOffset(const UnrealStructValueView& v,const wchar_t* name,const wchar_t* type,uint32_t size,uint32_t* out){
  if(bad_layout)return false;
  if(v.struct_definition==goap_entry_definition.bytes && !std::wcscmp(name,L"Value") && !std::wcscmp(type,L"StructProperty") && size==12)*out=8;
  else if(v.struct_definition==goap_value_definition.bytes && !std::wcscmp(name,L"IntVal") && !std::wcscmp(type,L"IntProperty") && size==4)*out=0;
  else return false;
  return true;}
bool UnrealReadStructStructField(const UnrealStructValueView& v,const wchar_t* name,UnrealStructValueView* out){
  if(v.struct_definition!=goap_entry_definition.bytes || std::wcscmp(name,L"Value"))return false;
  *out={static_cast<const uint8_t*>(v.data)+8,12,goap_value_definition.bytes};return true;}
bool UnrealReadStructIntField(const UnrealStructValueView& v,const wchar_t* name,int32_t* out){
  if(v.struct_definition!=goap_value_definition.bytes || std::wcscmp(name,L"IntVal"))return false;
  *out=Read<int32_t>(v.data,0);return true;}
bool UnrealAssignStructIntField(const UnrealStructValueView& v,const wchar_t* name,int32_t value){
  int32_t before=0;if(!UnrealReadStructIntField(v,name,&before))return false;
  Put(const_cast<void*>(v.data),0,value);return true;}
uint32_t UnrealObjectIndex(void* p){auto* o=Find(p);return o ? o->id:0;}
bool UnrealObjectIsA(void* p,const wchar_t* type){auto* o=Find(p);if(!o)return false;if(!std::wcscmp(type,o->type))return true;
  if(!std::wcscmp(type,L"TnPawn"))return p==pawn.bytes || !std::wcscmp(o->type,L"TnPlayerPawn");
  if(!std::wcscmp(type,L"TnAiPawn"))return !std::wcscmp(o->type,L"TnTankAiPawn");
  if(!std::wcscmp(type,L"TnVehicleForm"))return p==form.bytes;
  return false;
}
void* UnrealObjectOuter(void* p){auto* o=Find(p);return o ? o->outer:nullptr;}
bool UnrealObjectClassName(void* p,char* out,size_t n){auto* o=Find(p);if(!o)return false;
  std::snprintf(out,n,"%ls",o->type);return true;}
bool UnrealReadBoolProperty(void* p,const wchar_t* field,bool* out){auto* o=Find(p);if(!o)return false;
  if(!std::wcscmp(field,L"bDeleteMe"))*out=o->deleting;
  else if(!std::wcscmp(field,L"_StopMovement"))*out=(Read<uint32_t>(p,0x68)&2)!=0;
  else if(p==goap.bytes && !std::wcscmp(field,L"_HashCreated"))*out=(Read<uint32_t>(p,0x34)&1)!=0;
  else return false;
  return true;}
bool UnrealResolveBytePropertyOffset(void*,const wchar_t* f,uint32_t* out){
  if(!std::wcscmp(f,L"Role"))*out=0x66;else if(!std::wcscmp(f,L"NetMode"))*out=0x39C;else return false;return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* f,uint8_t* out){uint32_t o=0;if(!Find(p) || !UnrealResolveBytePropertyOffset(p,f,&o))return false;*out=Read<uint8_t>(p,o);return true;}
bool UnrealResolveObjectPropertyOffset(void*,const wchar_t* f,uint32_t* out){
  if(bad_layout)return false;
  if(!std::wcscmp(f,L"_InputState")){*out=vehicle_type==3 ? 0x104:0x108;return true;}
  const struct {const wchar_t* name;uint32_t offset;} fields[]={
    {L"WorldInfo",0x98},{L"Pawn",0x1C8},{L"Controller",0x1D4},{L"Owner",0x2C},{L"AcknowledgedPawn",0x300},
    {L"_VehicleAiControl",0x3CC},{L"AiWorldState",0x300},{L"_VehicleForm",0x580},{L"_Blueprint",0x74},{L"Blueprint",0x74},
    {L"_PlaneForm",0x78},{L"CarForm",0x78},{L"TankForm",0x78},{L"_PathFollower",0x64},
    {L"_BoundController",0x28},{L"_TransformerPawn",0x2C},{L"_Controller",0x28},{L"_Pawn",0x2C}};
  for(const auto& field:fields)if(!std::wcscmp(field.name,f)){*out=field.offset;return true;}
  return false;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* f,void** out){uint32_t o=0;if(!Find(p) || !UnrealResolveObjectPropertyOffset(p,f,&o))return false;*out=Read<void*>(p,o);return true;}
bool UnrealResolveStructPropertyOffset(void*,const wchar_t* f,uint32_t size,uint32_t* out){
  if(bad_layout || size!=12)return false;
  if(!std::wcscmp(f,L"_DesiredLocation"))*out=0x48;
  else if(!std::wcscmp(f,L"_DesiredRotation"))*out=0x30;
  else return false;
  return true;}
bool UnrealObjectReferenceWritesReady(){return writes;}
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* f,void* expected,void* next){
  uint32_t o=0;if(!writes || !UnrealResolveObjectPropertyOffset(p,f,&o) || Read<void*>(p,o)!=expected)return false;
  Put(p,o,next);using Barrier=void(*)(void*);reinterpret_cast<Barrier>(VehicleAddress(native_reference_code::kBarrierVa))(next);return true;}
bool NativeReadPawnForm(void* p,uint8_t* m,uint8_t* type){if(p!=pawn.bytes)return false;*m=mode;*type=vehicle_type;return mode<=1;}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease){return input_held && lease.held && !lease.invalidated;}
bool NativeAiPlanHandoffReady(void* p){return plan_ready && p==ai.bytes;}
}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const uintptr_t base=reinterpret_cast<uintptr_t>(image);
  vehicle_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    auto bytes=native_reference_code::Relocate(code,base,relocs);std::memcpy(reinterpret_cast<void*>(base+va-native_reference_code::kImageBase),bytes.data(),bytes.size());return true;});
  const auto barrier=native_reference_code::Relocate(native_reference_code::kBarrier,base,{15,38});
  std::memcpy(image+native_reference_code::kBarrierVa-native_reference_code::kImageBase,barrier.data(),barrier.size());
  Jump(0x110031A0,reinterpret_cast<void*>(&VehicleTestClass));Jump(0x10901430,reinterpret_cast<void*>(&VehicleTestCast));Jump(0x116EAA40,reinterpret_cast<void*>(&VehicleTestAllocate));
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    vehicle_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){const auto page=Read<uint32_t>(exe.data(),p),length=Read<uint32_t>(exe.data(),p+4);assert(length>=8);
        for(size_t n=p+8;n<p+length;n+=2){const auto entry=Read<uint16_t>(exe.data(),n);const uintptr_t operand=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && operand>=va && operand+4<=va+sizeof(code)){const auto at=operand-va;
            Put(expected.data(),at,Read<uint32_t>(expected.data(),at)+static_cast<uint32_t>(base-native_reference_code::kImageBase));}}
        p+=length;
      }assert(expected==native_reference_code::Relocate(code,base,relocs));return true;
    });
  }
  for(unsigned i=0;i<1000;++i){Reset(i%5,i%2,i%5==4 ? 0:(i/5)%2);const auto original=pawn,old=old_strategy;
    NativeVehicleAiNavigation navigation{};assert(NativeReadVehicleAiNavigation(ai.bytes,&navigation));
    auto expected_entries=goap_entries;if(mode==1)Put(expected_entries.data(),20*24+8,1);
    assert(Rebuild()==NativeVehicleAiResult::kApplied);assert(NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));
    assert(goap_entries==expected_entries);
    assert(!std::memcmp(original.bytes,pawn.bytes,sizeof(pawn.bytes)));assert(!std::memcmp(old.bytes,old_strategy.bytes,sizeof(old.bytes)));
    assert(allocations==(vehicle_type==4 ? 0u:1u));
    if(vehicle_type!=4){assert(Read<uint32_t>(image,0x12B10474-native_reference_code::kImageBase)==1);
      assert((Read<uint32_t>(strategy.bytes,0x68)&2)!=0);
      assert(!std::memcmp(strategy.bytes+0x48,pawn.bytes+0xD8,12));
      assert(!std::memcmp(strategy.bytes+0x30,pawn.bytes+0xE4,12));
      assert(Read<uint32_t>(vehicle_input.bytes,vehicle_type==2 ? 0x4C:0x48)==0xA4u);}
    const auto input_before=vehicle_input;
    for(unsigned repeat=0;repeat<2;++repeat){
      assert(Restore(vehicle_type==4 ? nullptr:old_strategy.bytes,vehicle_type==4 ? 0:old_strategy.id)==NativeVehicleAiResult::kApplied);
      assert(NativeRestoreVehicleAiNavigation(navigation));
      assert(Read<int32_t>(goap_entries.data(),20*24+8)==2);
      assert(NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));
      assert(Read<void*>(ai.bytes,0x3CC)==(vehicle_type==4 ? nullptr:old_strategy.bytes));
      assert(!std::memcmp(old.bytes,old_strategy.bytes,sizeof(old.bytes)));
      assert(!std::memcmp(original.bytes,pawn.bytes,sizeof(pawn.bytes)));
      assert(!std::memcmp(input_before.bytes,vehicle_input.bytes,sizeof(vehicle_input.bytes)));
      assert(allocations==(vehicle_type==4 ? 0u:1u));
    }
  }
  Reset();reenter=true;assert(Rebuild()==NativeVehicleAiResult::kApplied);
  for(unsigned d=1;d<=14;++d){Reset();mutation=d;assert(Rebuild()==NativeVehicleAiResult::kStateMismatch);assert(allocations==1);}
  for(unsigned d=15;d<=16;++d){Reset(2,false,1);mutation=d;assert(Rebuild()==NativeVehicleAiResult::kStateMismatch);assert(allocations==1);}
  for(unsigned d=1;d<=5;++d){Reset();late_mutation=d;assert(Rebuild()==NativeVehicleAiResult::kStateMismatch);assert(allocations==1);}
  Reset();pawn.type=L"TnPlayerPawn";assert(!Ready());Reject();
  Reset();pawn.deleting=true;Reject();Reset();ai.deleting=true;Reject();Reset();world.bytes[0x39C]=1;Reject();
  Reset();ai.bytes[0x66]=1;Reject();Reset();pawn.bytes[0x66]=1;Reject();Reset();Ptr(ai,0x1C8,&foreign);Reject();
  Reset();Ptr(pawn,0x1D4,&foreign);Reject();Reset();Ptr(pawn,0x2C,&foreign);Reject();Reset();Ptr(player,0x300,&foreign);Reject();
  Reset();Ptr(pawn,0x98,&foreign);Reject();Reset();Ptr(form,0x74,nullptr);Reject();Reset();bad_layout=true;Reject();
  Reset();input_held=false;Reject();Reset();plan_ready=false;Reject();Reset();writes=false;Reject();Reset();mode=2;Reject();
  Reset();Ptr(old_path,0x2C,&foreign);assert(!NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));
  assert(Rebuild()==NativeVehicleAiResult::kApplied);assert(NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));
  Reset(4);Ptr(ai,0x3CC,&old_strategy);assert(!NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));assert(Rebuild()==NativeVehicleAiResult::kApplied);
  Reset();Ptr(ai,0x3CC,nullptr);assert(!NativeVehicleAiMatchesPawn(ai.bytes,pawn.bytes));assert(Rebuild()==NativeVehicleAiResult::kApplied);
  Reset();const auto collected_index=old_strategy.id;old_strategy.valid=false;Ptr(ai,0x3CC,nullptr);
  assert(Restore(old_strategy.bytes,collected_index)==NativeVehicleAiResult::kApplied);assert(allocations==1);
  Reset();assert(Restore(old_strategy.bytes,old_strategy.id+1)==NativeVehicleAiResult::kApplied);assert(allocations==1);
  Reset();Ptr(old_path,0x2C,&foreign);assert(Restore(old_strategy.bytes,old_strategy.id)==NativeVehicleAiResult::kApplied);assert(allocations==1);
  Reset();assert(Restore(nullptr,0)==NativeVehicleAiResult::kApplied);assert(allocations==1);
  Reset();plan_ready=false;assert(Restore(old_strategy.bytes,old_strategy.id)==NativeVehicleAiResult::kRejected);assert(!allocations);
  // Compare the in-place specialization with real native SetIntValue + array
  // helpers. For an already typed subjectless entry, the complete collection
  // bytes must match, not just the displayed mode. No allocator fixture runs.
  for(int value=0;value<=2;++value){Reset();NativeVehicleAiNavigation nav{};
    assert(NativeReadVehicleAiNavigation(ai.bytes,&nav));assert(VehicleNavigationWrite(nav,value));
    const auto custom=goap_entries;Put(goap_entries.data(),20*24+8,nav.mode);
    assert(VehicleTestSetInt(image+vehicle_code::kSetGoapIntVa-native_reference_code::kImageBase,
        goap.bytes+0x10,goap_key.bytes+0x10,value)==1);
    assert(goap_entries==custom);assert(Read<int32_t>(goap.bytes,0x2C)==32 && Read<int32_t>(goap.bytes,0x30)==32);
  }
  Reset(2,false,1);Put(goap.bytes,0x34,0u);Reject();
  Reset(2,false,1);Put(goap_defaults.bytes,0x28,32);Reject();
  Reset(2,false,1);Put(goap_defaults.bytes,0x28,-1);Reject();
  Reset(2,false,1);Put(goap_entries.data(),20*24,static_cast<void*>(foreign.bytes));Reject();
  Reset(2,false,1);Put(goap_entries.data(),20*24+4,static_cast<void*>(foreign.bytes));Reject();
  Reset(2,false,1);Put(goap_entries.data(),20*24+8,3);Reject();
  Reset(2,false,1);Put(goap.bytes,0x30,257);Reject();
  Reset();NativeVehicleAiNavigation nav{};assert(NativeReadVehicleAiNavigation(ai.bytes,&nav));
  ++goap.id;assert(!NativeRestoreVehicleAiNavigation(nav));--goap.id;
  Put(goap_entries.data(),20*24+8,0);assert(!NativeRestoreVehicleAiNavigation(nav));
  Put(goap_entries.data(),20*24+8,1);plan_ready=false;assert(!NativeRestoreVehicleAiNavigation(nav));
  plan_ready=true;writes=false;assert(!NativeRestoreVehicleAiNavigation(nav));writes=true;
  assert(NativeRestoreVehicleAiNavigation(nav));assert(NativeRestoreVehicleAiNavigation(nav));
  // Extra capacity does not trigger SetLength or an allocation in our path.
  Reset();assert(NativeReadVehicleAiNavigation(ai.bytes,&nav));Put(goap.bytes,0x30,256);
  assert(VehicleNavigationWrite(nav,1));assert(Read<int32_t>(goap.bytes,0x30)==256);
  Reset();HANDLE thread=CreateThread(nullptr,0,OtherThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  vehicle_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){
    auto* p=image+va-native_reference_code::kImageBase;for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!Ready());p[i]^=1;}return true;});
  Reset();DWORD old=0;assert(VirtualProtect(image,0x2400000,PAGE_READWRITE,&old));assert(!Ready());
  assert(VirtualProtect(image,0x2400000,old,&old));assert(Ready());
  // The role getter patch is a legitimate shared-code owner, not an image
  // mismatch. Exercise production vehicle setup with those hooks installed.
  native_role_pawn_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);
    std::memcpy(image+va-native_reference_code::kImageBase,bytes.data(),bytes.size());return true;});
  assert(InstallNativeRolePawnGetters(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),
      +[](void*,bool)->void*{assert(false);return nullptr;}));
  Reset();assert(Ready());assert(Rebuild()==NativeVehicleAiResult::kApplied);
  auto* getter=image+native_role_pawn_code::kAiVa-native_reference_code::kImageBase;
  getter[1]^=1;assert(!Ready());getter[1]^=1;
  assert(RemoveNativeRolePawnGetters());assert(Ready());
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativeVehicleAiTests passed (native bodies + fixture engine services, not gameplay)");
}
