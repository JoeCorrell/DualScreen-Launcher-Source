// Real SetSensorSet and base Initialize bytes, fixture UObject metadata and
// subclass callbacks. This does not test physical-player AI or live gameplay.
#include "NativeAiSensors.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(4) uint8_t bytes[0x900]{};const wchar_t* type=L"Object";uint32_t id=0;bool valid=true,deleting=false; };
Object ai,pawn,other,player,world,analyzer,set,database,sensor,sensor2,foreign,ai_class,sensor_class;
Object* objects[]={&ai,&pawn,&other,&player,&world,&analyzer,&set,&database,&sensor,&sensor2,&foreign,&ai_class,&sensor_class};
std::array<void*,2> entries{};uint8_t* image=nullptr;
bool plan=true,input=true,writes=true,layout=true;unsigned callbacks=0,dirty=0,derived=0;
NativePlayerInputLease move,look;
template<class T>T Read(const void* p,size_t offset){T v{};std::memcpy(&v,static_cast<const uint8_t*>(p)+offset,sizeof(v));return v;}
template<class T>void Put(void* p,size_t offset,T value){std::memcpy(static_cast<uint8_t*>(p)+offset,&value,sizeof(value));}
void Ptr(Object& p,size_t at,Object* value){Put(p.bytes,at,value ? static_cast<void*>(value->bytes):nullptr);}
Object* Find(void* p){for(auto* o:objects)if(o->valid && p==o->bytes)return o;return nullptr;}
void Jump(uintptr_t va,void* fn){auto* p=image+va-native_reference_code::kImageBase;p[0]=0xe9;
  Put(p,1,static_cast<uint32_t>(reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(p+5)));}
bool Ready(){return PrepareNativeAiSensors(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),ai.bytes);}
NativeAiSensorResult Rebind(){return NativeRebindAiSensors(ai.bytes,pawn.bytes,other.bytes,move,look);}
void Reset(){uint32_t id=1;for(auto* o:objects){*o={};o->id=id++;}
  plan=input=writes=layout=true;callbacks=dirty=derived=0;
  ai.type=L"TnAiController";pawn.type=other.type=ai_class.type=L"TnAiPawn";
  player.type=L"TnPlayerController";world.type=L"WorldInfo";
  analyzer.type=L"TnTacticalAnalyzer";set.type=L"HmAiSensorSet";database.type=L"TnTacticalDatabase";
  sensor.type=sensor2.type=sensor_class.type=L"TnAiSensor";
  for(auto* o:{&ai,&pawn,&other,&player}){Ptr(*o,0x98,&world);o->bytes[0x66]=3;Put(o->bytes,8,0x800000u);}
  Ptr(ai,0x1C8,&pawn);Ptr(pawn,0x1D4,&ai);Ptr(pawn,0x2C,&ai);
  Ptr(player,0x1C8,&other);Ptr(player,0x300,&other);Ptr(other,0x1D4,&player);Ptr(other,0x2C,&player);
  Ptr(ai,0x2F4,&analyzer);Ptr(analyzer,0x28,&ai);Ptr(analyzer,0x2C,&database);Ptr(analyzer,0x30,&set);
  entries={sensor.bytes,sensor2.bytes};Put(set.bytes,0x28,entries.data());Put(set.bytes,0x2C,2);Put(set.bytes,0x30,2);
  void* table=image+0x2201000;
  Put(table,0x40,static_cast<void*>(image+native_ai_sensor_code::kInitializeVa-native_reference_code::kImageBase));
  Put(table,0x44,static_cast<void*>(image+0x2202000));
  for(auto* o:{&sensor,&sensor2}){Put(o->bytes,0x10,table);Ptr(*o,0x28,&ai);Ptr(*o,0x2C,&other);Ptr(*o,0x30,&database);}
  move={};move.controller=player.bytes;move.world=world.bytes;move.axis=NativeInputAxis::kMove;move.held=true;
  look=move;look.axis=NativeInputAxis::kLook;
  Put(image,0x12B1047C-native_reference_code::kImageBase,0u);Put(image,0x12B10474-native_reference_code::kImageBase,0u);
  assert(Ready());
}
void Reject(){const auto a=analyzer,s=sensor,t=sensor2;assert(Rebind()==NativeAiSensorResult::kRejected);assert(!callbacks);
  assert(!std::memcmp(a.bytes,analyzer.bytes,sizeof(a.bytes)) && !std::memcmp(s.bytes,sensor.bytes,sizeof(s.bytes)) &&
      !std::memcmp(t.bytes,sensor2.bytes,sizeof(t.bytes)));}
DWORD WINAPI Thread(void*){assert(!Ready());Reject();return 0;}
}
extern "C" void* SensorTestClass(void* cache,void*){
  const auto va=reinterpret_cast<uintptr_t>(cache)-reinterpret_cast<uintptr_t>(image)+native_reference_code::kImageBase;
  assert(va==0x12B6EC80 || va==0x12B6ECD0);return (va==0x12B6EC80 ? sensor_class.bytes:ai_class.bytes)+0x10;
}
extern "C" int SensorTestCastImpl(void* object,void* type){auto* cls=Find(type);assert(cls);return UnrealObjectIsA(object,cls->type);}
extern "C" __attribute__((naked)) void SensorTestCast(){asm volatile("pushl 4(%esp)\n pushl %ecx\n call _SensorTestCastImpl\n addl $8,%esp\n ret $4");}
extern "C" void SensorTestCallbackImpl(void* compiled){++callbacks;auto* object=static_cast<uint8_t*>(compiled)-0x10;
  assert(Read<void*>(analyzer.bytes,0x30)==set.bytes); // Never unrooted, including during callbacks.
  assert(Read<void*>(object,0x28)==ai.bytes && Read<void*>(object,0x2C)==pawn.bytes && Read<void*>(object,0x30)==database.bytes);
  assert(!Ready());assert(Rebind()==NativeAiSensorResult::kRejected);
  if(dirty==1)++pawn.id;
  if(dirty==2)input=false;
  if(dirty==3)++sensor.id;
  if(dirty==4)Ptr(sensor,0x2C,&foreign);
  if(dirty==5)plan=false;
  if(dirty==6)entries[1]=foreign.bytes;
  if(dirty==7)Put(sensor2.bytes,0x10,static_cast<void*>(image+0x2205000));
  if(dirty==8)Put(image+0x2201000,0x44,static_cast<void*>(image+0x2202001));
  if(dirty==9)Ptr(ai,0x1C8,&foreign);
}
extern "C" __attribute__((naked)) void SensorTestCallback(){asm volatile("pushl %ecx\n call _SensorTestCallbackImpl\n addl $4,%esp\n ret");}
extern "C" void SensorTestDerivedInitializeImpl(void* compiled,void* controller,void* data){
  SensorInitializeCall(image+native_ai_sensor_code::kInitializeVa-native_reference_code::kImageBase,
      compiled,controller,data);
  // Represents work after super.Initialize, e.g. BiggestThreat's service bind.
  ++derived;
}
extern "C" __attribute__((naked)) void SensorTestDerivedInitialize(){asm volatile(
    "pushl %ebp\n movl %esp,%ebp\n pushl 12(%ebp)\n pushl 8(%ebp)\n pushl %ecx\n"
    "call _SensorTestDerivedInitializeImpl\n addl $12,%esp\n leave\n ret $8");}
namespace wfcds {
uint32_t UnrealObjectIndex(void* p){auto* o=Find(p);return o ? o->id:0;}
bool UnrealObjectIsA(void* p,const wchar_t* type){auto* o=Find(p);return o && (!std::wcscmp(type,o->type) ||
    (!std::wcscmp(type,L"TnPawn") && (!std::wcscmp(o->type,L"TnAiPawn") || !std::wcscmp(o->type,L"TnPlayerPawn"))));}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out){auto* o=Find(p);if(!o || std::wcscmp(name,L"bDeleteMe"))return false;*out=o->deleting;return true;}
bool UnrealResolveObjectPropertyOffset(void*,const wchar_t* name,uint32_t* out){if(!layout)return false;
  const struct {const wchar_t* name;uint32_t offset;} fields[]={
    {L"TacticalAnalyzer",0x2F4},{L"Owner",0x28},{L"SensorSet",0x30},{L"TacticalData",0x2C},
    {L"_OwnerController",0x28},{L"_OwnerPawn",0x2C},{L"_TacticalDatabase",0x30},
    {L"WorldInfo",0x98},{L"Pawn",0x1C8},{L"Controller",0x1D4},{L"AcknowledgedPawn",0x300}};
  for(const auto& f:fields)if(!std::wcscmp(f.name,name)){*out=f.offset;return true;}
  return false;}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out){uint32_t offset=0;if(!Find(p))return false;
  if((p==pawn.bytes || p==other.bytes) && !std::wcscmp(name,L"Owner"))offset=0x2C;
  else if(!UnrealResolveObjectPropertyOffset(p,name,&offset))return false;
  *out=Read<void*>(p,offset);return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out){if(!Find(p))return false;
  if(!std::wcscmp(name,L"Role"))*out=Read<uint8_t>(p,0x66);else if(!std::wcscmp(name,L"NetMode"))*out=Read<uint8_t>(p,0x39C);else return false;return true;}
bool UnrealReadArrayProperty(void* p,const wchar_t* name,UnrealArrayView* out){if(p!=set.bytes || std::wcscmp(name,L"Sensors"))return false;
  *out=Read<UnrealArrayView>(p,0x28);return true;}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out){if(i<0 || i>=a.count || !a.data)return false;*out=static_cast<void**>(a.data)[i];return true;}
bool NativePlayerInputHeld(const NativePlayerInputLease& l){return input && l.held && !l.invalidated;}
bool NativeAiPlanHandoffReady(void* p){return plan && p==ai.bytes;}
bool UnrealObjectReferenceWritesReady(){return writes;}
}
int main(int argc,char** argv){
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2400000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  const auto base=reinterpret_cast<uintptr_t>(image);
  native_ai_sensor_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,base,relocs);
    std::memcpy(image+va-native_reference_code::kImageBase,bytes.data(),bytes.size());return true;});
  Jump(0x110031A0,reinterpret_cast<void*>(&SensorTestClass));Jump(0x10901430,reinterpret_cast<void*>(&SensorTestCast));
  Jump(native_reference_code::kImageBase+0x2202000,reinterpret_cast<void*>(&SensorTestCallback));
  Jump(native_reference_code::kImageBase+0x2204000,reinterpret_cast<void*>(&SensorTestDerivedInitialize));
  if(argc>1){std::ifstream stream(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(stream),{}};assert(exe.size()==37667328);
    native_ai_sensor_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
      assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
      auto expected=native_reference_code::Relocate(code,native_reference_code::kImageBase,{});
      for(size_t p=0x2148A00;p<0x2148A00+0x243ABC;){const auto page=Read<uint32_t>(exe.data(),p),size=Read<uint32_t>(exe.data(),p+4);assert(size>=8);
        for(size_t q=p+8;q<p+size;q+=2){const auto entry=Read<uint16_t>(exe.data(),q);const uintptr_t address=native_reference_code::kImageBase+page+(entry&4095);
          if((entry>>12)==3 && address>=va && address+4<=va+sizeof(code))Put(expected.data(),address-va,Read<uint32_t>(expected.data(),address-va)+static_cast<uint32_t>(base-native_reference_code::kImageBase));}
        p+=size;
      }assert(expected==native_reference_code::Relocate(code,base,relocs));return true;});
  }
  // Preserve a derived Initialize override, not merely the base OnInitialize.
  Reset();std::memcpy(image+0x2203000,image+0x2201000,0x48);
  Put(image+0x2203000,0x40,static_cast<void*>(image+0x2204000));
  Put(sensor2.bytes,0x10,static_cast<void*>(image+0x2203000));
  for(int i=0;i<1000;++i){assert(Rebind()==NativeAiSensorResult::kApplied);
    assert(derived==static_cast<unsigned>(i+1));assert(callbacks==2*derived);}
  for(int i=0;i<1000;++i){Reset();const auto a=pawn,b=other;assert(Rebind()==NativeAiSensorResult::kApplied);assert(callbacks==2);
    assert(Rebind()==NativeAiSensorResult::kApplied);assert(callbacks==4);
    assert(!std::memcmp(a.bytes,pawn.bytes,sizeof(a.bytes)) && !std::memcmp(b.bytes,other.bytes,sizeof(b.bytes)));
    assert(Read<uint32_t>(image,0x12B10474-native_reference_code::kImageBase)==1);
  }
  // No second sensor callback after the first changes any graph dependency.
  for(unsigned d=1;d<=9;++d){Reset();dirty=d;assert(Rebind()==NativeAiSensorResult::kStateMismatch);assert(callbacks==1);}
  Reset();pawn.type=L"TnPlayerPawn";Reject();Reset();pawn.deleting=true;Reject();Reset();world.bytes[0x39C]=1;Reject();
  Reset();input=false;Reject();Reset();plan=false;Reject();Reset();writes=false;Reject();Reset();layout=false;Reject();
  Reset();Ptr(player,0x300,&foreign);Reject();Reset();Ptr(sensor,0x2C,&foreign);Reject();Reset();Ptr(sensor,0x30,&foreign);Reject();
  Reset();entries[1]=sensor.bytes;assert(!Ready());Reject();Reset();entries[0]=nullptr;assert(!Ready());Reject();
  Reset();Put(set.bytes,0x30,1);Reject();Reset();Put(set.bytes,0x2C,129);Reject();Reset();Ptr(analyzer,0x28,&player);Reject();
  Reset();HANDLE thread=CreateThread(nullptr,0,Thread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  native_ai_sensor_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){auto* p=image+va-native_reference_code::kImageBase;
    for(size_t i=0;i<sizeof(code);++i){p[i]^=1;assert(!Ready());p[i]^=1;}return true;});
  assert(VirtualFree(image,0,MEM_RELEASE));std::puts("NativeAiSensorTests passed (real base lifecycle; subclass callbacks/engine metadata are fixtures)");
}
