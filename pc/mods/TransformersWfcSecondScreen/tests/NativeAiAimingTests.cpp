// Execute shipping setter, aiming, adjusted aim, location getter and rotator
// conversion bodies. Metadata and deep math primitives are fixtures, not game.
#include "NativeAiAiming.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(16) uint8_t bytes[0x500]{}; const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; };
Object controller{{},L"TnAiController"},pawn{{},L"TnPlayerPawn"},aimer{{},L"TnAiWeaponAimer"},
    world{{},L"WorldInfo"},weapon{{},L"TnWeapon"},enemy{{},L"TnPawn"},collection{{},L"HmGoapWorldStateCollection"},
    los_class{{},L"Class"},defaults{{},L"TnAiWorldStateLOSToEnemy"},definition{{},L"ScriptStruct"},foreign{{},L"WorldInfo"};
Object* objects[]={&controller,&pawn,&aimer,&world,&weapon,&enemy,&collection,&los_class,&defaults,&definition,&foreign};
uint8_t* image=nullptr;
alignas(16) uint8_t properties[32*24]{},engine[0x44]{},context[0x2C]{},world_slot[4]{};
bool bad_offset=false,downed=false,transforming=false,bad_shape=false;
unsigned matrix_calls=0,dirty=0;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && p==o->bytes) return o; return nullptr; }
uint32_t Read(const void* p,size_t offset) { uint32_t v; std::memcpy(&v,static_cast<const uint8_t*>(p)+offset,4); return v; }
void Put(void* p,size_t offset,uintptr_t value) { uint32_t v=static_cast<uint32_t>(value); std::memcpy(static_cast<uint8_t*>(p)+offset,&v,4); }
void Link(Object& o,size_t offset,Object* v) { Put(o.bytes,offset,reinterpret_cast<uintptr_t>(v ? v->bytes:nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* target) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(At(va)+5)); }
void Position(Object& o,float x,float y,float z=0) { const float v[]={x,y,z}; std::memcpy(o.bytes+0xD8,v,12); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if(!o) return false;
  const auto field=[&](const wchar_t* n,uint32_t offset) { if(std::wcscmp(n,name)) return false; *out=offset; return true; };
  if(field(L"WorldInfo",0x98)) return true;
  if(o==&controller) return field(L"Pawn",0x1C8) || field(L"WeaponAimer",0x2FC) || field(L"Enemy",0x2A4) || field(L"AiWorldState",0x300);
  if(o==&pawn) return field(L"Controller",0x1D4) || field(L"Weapon",0x428);
  if(o==&aimer) return field(L"_Owner",0x28) || field(L"_OwnerController",0x30);
  if(o==&weapon) return field(L"Instigator",0x94);
  return false;
}
void Reset() {
  uint32_t id=1; for(auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->index=id++; o->valid=true; o->dead=false; }
  pawn.type=L"TnPlayerPawn"; aimer.type=L"TnAiWeaponAimer";
  Link(controller,0x1C8,&pawn); Link(pawn,0x1D4,&controller); Link(controller,0x2FC,&aimer);
  Link(aimer,0x28,&pawn); Link(aimer,0x30,&controller); Link(controller,0x300,&collection);
  Link(controller,0x2A4,&enemy); Link(pawn,0x428,&weapon); Link(weapon,0x94,&pawn);
  for(auto* o:{&controller,&pawn,&weapon,&enemy}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Put(At(0x12B10A9C),0,reinterpret_cast<uintptr_t>(engine));
  Put(engine,0x40,reinterpret_cast<uintptr_t>(context)); Put(context,0x28,reinterpret_cast<uintptr_t>(world_slot));
  Put(world_slot,0,reinterpret_cast<uintptr_t>(world.bytes));
  Link(los_class,0x9C,&defaults); Put(defaults.bytes,0x28,13);
  std::memset(properties,0,sizeof(properties)); Put(properties,13*24,reinterpret_cast<uintptr_t>(los_class.bytes)); Put(properties,13*24+16,1);
  Put(collection.bytes,0x28,reinterpret_cast<uintptr_t>(properties)); Put(collection.bytes,0x2C,32); Put(collection.bytes,0x30,32);
  aimer.bytes[0x2C]=0; Put(aimer.bytes,0x80,0xA5A5A5A5);
  Position(enemy,1000,0); Position(pawn,0,0); Position(weapon,0,0);
  bad_offset=downed=transforming=bad_shape=false; dirty=0;
  g_aim_module=reinterpret_cast<HMODULE>(image); g_aim_thread=GetCurrentThreadId();
  g_aim_los_class=los_class.bytes; g_aim_los_index=los_class.index;
}
bool Update(bool* aligned) { return NativeUpdateAiAiming(controller.bytes,pawn.bytes,aimer.bytes,enemy.bytes,aligned); }
void Rejected() {
  const auto before=aimer; bool aligned=true; assert(!Update(&aligned) && !aligned);
  assert(!std::memcmp(before.bytes,aimer.bytes,sizeof(aimer.bytes)));
}
DWORD WINAPI WrongThread(void*) { Rejected(); return 0; }
void* __attribute__((fastcall)) ZeroRotation(void*,void*,int32_t* out) { std::memset(out,0,12); return out; }
// The stock Rotation.Vector wrapper calls its matrix primitive with EAX=out,
// ECX=rotator; GCC regparm's unused EDX parameter preserves that ABI.
void* __attribute__((regparm(3))) Matrix(float* out,void*,const int32_t* r) {
  ++matrix_calls;
  bool aligned=true; assert(g_aim_busy && !Update(&aligned) && !aligned);
  constexpr double tau=6.283185307179586476925286766559;
  const auto pitch=r[0]*tau/65536.,yaw=r[1]*tau/65536.;
  std::memset(out,0,64); out[0]=static_cast<float>(std::cos(pitch)*std::cos(yaw));
  out[1]=static_cast<float>(std::cos(pitch)*std::sin(yaw)); out[2]=static_cast<float>(std::sin(pitch));
  if(dirty && matrix_calls%2==0) {
    if(dirty==1) ++pawn.index;
    if(dirty==2) Link(controller,0x2A4,nullptr);
    if(dirty==3) Put(properties,13*24+16,0);
    if(dirty==4) Link(aimer,0x30,nullptr);
  }
  return out;
}
float __cdecl Atan2(float y,float x) { return std::atan2(y,x); }
float __cdecl Sqrt(float x) { return std::sqrt(x); }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* name) {
  const auto* o=Lookup(p); if(!o) return false;
  if(!std::wcscmp(o->type,name)) return true;
  if(!std::wcscmp(name,L"TnPawn")) return o==&pawn || o==&enemy;
  if(!std::wcscmp(name,L"Actor")) return o==&pawn || o==&enemy || o==&weapon || o==&controller;
  return false;
}
uint32_t UnrealObjectIndex(void* p) { const auto* o=Lookup(p); return o ? o->index:0; }
void* UnrealObjectClass(void* p) { return p==defaults.bytes ? los_class.bytes:nullptr; }
bool UnrealObjectClassName(void* p,char* out,size_t size) {
  auto* o=Lookup(p); if(!o) return false; std::snprintf(out,size,"%ls",o->type); return true;
}
bool UnrealObjectName(void* p,char* out,size_t size) {
  if(p!=definition.bytes) return false;
  std::snprintf(out,size,"HmGoapWorldStateProperty"); return true;
}
bool UnrealResolveClassByPath(const wchar_t* name,void** out) {
  if(std::wcscmp(name,L"TransGame.TnAiWorldStateLOSToEnemy")) return false;
  *out=los_class.bytes; return true;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if(!Offset(p,name,out)) return false;
  if(bad_offset) ++*out;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) {
  if(p==pawn.bytes && !std::wcscmp(name,L"_CurrentTransformation")) { *out=transforming ? enemy.bytes:nullptr; return true; }
  uint32_t offset=0; if(!Offset(p,name,&offset)) return false; *out=reinterpret_cast<void*>(Read(p,offset)); return true;
}
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if(!Lookup(p)) return false;
  if(!std::wcscmp(name,L"Role")) *out=0x66;
  else if(!std::wcscmp(name,L"NetMode")) *out=0x39C;
  else if(p==aimer.bytes && !std::wcscmp(name,L"_AimingMode")) *out=0x2C;
  else return false;
  return true;
}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) {
  uint32_t offset=0; if(!UnrealResolveBytePropertyOffset(p,name,&offset)) return false; *out=static_cast<uint8_t*>(p)[offset]; return true;
}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) {
  auto* o=Lookup(p); if(!o) return false;
  if(!std::wcscmp(name,L"bDeleteMe")) *out=o->dead;
  else if(!std::wcscmp(name,L"bStasis") || !std::wcscmp(name,L"bTearOff")) *out=false;
  else if(p==pawn.bytes && !std::wcscmp(name,L"_IsDowned")) *out=downed;
  else if(p==aimer.bytes && !std::wcscmp(name,L"_CheckAimFromWeaponRotation")) *out=(Read(p,0x80)&1)!=0;
  else return false;
  return true;
}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out) {
  if(p!=pawn.bytes || std::wcscmp(name,L"_TransformBlendTimeRemaining")) return false;
  *out=0; return true;
}
bool UnrealReadIntProperty(void* p,const wchar_t* name,int32_t* out) {
  if(p!=defaults.bytes || std::wcscmp(name,L"HashIndex")) return false;
  *out=static_cast<int32_t>(Read(p,0x28)); return true;
}
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* name,uint32_t size,uint32_t* out) {
  if(!Lookup(p) || size!=12) return false;
  if(!std::wcscmp(name,L"Location")) *out=0xD8;
  else if(!std::wcscmp(name,L"Rotation")) *out=0xE4;
  else if(p==aimer.bytes && !std::wcscmp(name,L"_TargetLocation")) *out=0x34;
  else return false;
  return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if(p!=collection.bytes || std::wcscmp(name,L"Properties")) return false;
  *out={reinterpret_cast<void*>(Read(p,0x28)),static_cast<int32_t>(Read(p,0x2C)),static_cast<int32_t>(Read(p,0x30)),bad_shape ? 20u:24u,definition.bytes}; return true;
}
}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); assert(image);
  std::vector<uint8_t> exe;
  if(argc>1) { std::ifstream f(argv[1],std::ios::binary); exe.assign(std::istreambuf_iterator<char>(f),{}); assert(exe.size()==37667328); }
  aim_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if(!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  std::memcpy(At(0x129D22C0),aim_code::kThreshold,8);
  std::memcpy(At(0x129D2410),aim_code::kAngleRadians,8);
  std::memcpy(At(0x129D2344),aim_code::kAngleUnits,4);
  if(!exe.empty()) for(auto value:{std::pair<uintptr_t,size_t>{0x129D22C0,8},{0x129D2410,8},{0x129D2344,4}}) {
    if(std::memcmp(At(value.first),exe.data()+value.first-0x10901200,value.second)) {
      std::fprintf(stderr,"Constant mismatch %08lx actual=",static_cast<unsigned long>(value.first));
      for(size_t i=0;i<value.second;++i) std::fprintf(stderr,"%02x",At(value.first)[i]);
      std::fprintf(stderr," expected=");
      for(size_t i=0;i<value.second;++i) std::fprintf(stderr,"%02x",exe[value.first-0x10901200+i]);
      std::fprintf(stderr,"\n");
    }
    assert(!std::memcmp(At(value.first),exe.data()+value.first-0x10901200,value.second));
  }
  assert(BindAimImage(reinterpret_cast<HMODULE>(image)));
  aim_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for(size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!BindAimImage(reinterpret_cast<HMODULE>(image))); At(va)[i]^=1; }
    return true;
  });
  At(0x129D22C0)[0]^=1; assert(!BindAimImage(reinterpret_cast<HMODULE>(image))); At(0x129D22C0)[0]^=1;
  Reset(); assert(!PrepareNativeAiAiming(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),controller.bytes,pawn.bytes,aimer.bytes)); // Not process EXE.
  Jump(aim_code::kZeroRotationVa,reinterpret_cast<void*>(&ZeroRotation));
  Jump(0x1094CCA0,reinterpret_cast<void*>(&Matrix)); Jump(0x10BB3610,reinterpret_cast<void*>(&Atan2)); Jump(0x1094BF30,reinterpret_cast<void*>(&Sqrt));
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000);
  for(unsigned i=0;i<6000;++i) {
    Reset(); pawn.type=i%2 ? L"TnAiPawn":L"TnPlayerPawn";
    const bool right=i%3==0; Position(enemy,right ? 0.f:1000.f,right ? 1000.f:0.f);
    // Put the PREVIOUS target directly opposite the current enemy. The first
    // update must aim at the new position, including actor-target mode 0.
    const float stale[]={-1000.f,0,0}; std::memcpy(aimer.bytes+0x34,stale,12); aimer.bytes[0x2C]=i%3;
    const auto before_pawn=pawn,before_weapon=weapon,before_aimer=aimer;
    bool aligned=false; assert(Update(&aligned) && aligned==!right);
    auto expected=before_aimer; expected.bytes[0x2C]=1; std::memcpy(expected.bytes+0x34,enemy.bytes+0xD8,12);
    assert(!std::memcmp(aimer.bytes,expected.bytes,sizeof(aimer.bytes)));
    assert(!std::memcmp(pawn.bytes,before_pawn.bytes,sizeof(pawn.bytes)) && !std::memcmp(weapon.bytes,before_weapon.bytes,sizeof(weapon.bytes)));
  }
  bool aligned=false;
  Reset(); Put(weapon.bytes,0xE8,16384); assert(Update(&aligned) && !aligned); // Weapon not pawn rotation.
  Put(aimer.bytes,0x80,0xA5A5A5A4); assert(Update(&aligned) && aligned); // Configured pawn rotation.
  Reset(); Link(pawn,0x428,nullptr); assert(Update(&aligned) && aligned); // Native unarmed fallback.
  Reset(); Position(weapon,1000,-1000); Put(weapon.bytes,0xE8,16384); assert(Update(&aligned) && aligned); // Weapon origin too.
  for(int degrees:{-180,-90,-31,-29,0,29,31,90,180}) {
    Reset(); Put(weapon.bytes,0xE8,static_cast<int32_t>(degrees*65536./360.));
    assert(Update(&aligned) && aligned==(std::abs(degrees)<30));
  }
  for(unsigned missing=0;missing<4;++missing) {
    Reset(); const auto before=aimer; const auto calls=matrix_calls;
    if(missing==0) Put(properties,13*24+16,2); // Other BoolVal/Any bit must not count.
    if(missing==1) Put(properties,13*24,reinterpret_cast<uintptr_t>(foreign.bytes));
    if(missing==2) Put(collection.bytes,0x2C,13);
    if(missing==3) { Link(controller,0x2A4,nullptr); assert(NativeUpdateAiAiming(controller.bytes,pawn.bytes,aimer.bytes,nullptr,&aligned) && !aligned); }
    else assert(Update(&aligned) && !aligned);
    assert(matrix_calls==calls && !std::memcmp(before.bytes,aimer.bytes,sizeof(aimer.bytes)));
  }
  Reset(); Put(defaults.bytes,0x28,7); Put(properties,7*24,reinterpret_cast<uintptr_t>(los_class.bytes)); Put(properties,7*24+16,1);
  assert(Update(&aligned) && aligned); // Live CDO index, not hardcoded slot 13.
  Reset(); Put(defaults.bytes,0x28,0xFFFFFFFF); Rejected();
  Reset(); bad_offset=true; Rejected(); Reset(); bad_shape=true; Rejected();
  for(unsigned paused=0;paused<2;++paused) {
    Reset(); downed=paused==0; transforming=paused==1; const auto before=aimer; const auto calls=matrix_calls;
    assert(Update(&aligned) && !aligned && matrix_calls==calls && !std::memcmp(before.bytes,aimer.bytes,sizeof(aimer.bytes)));
    downed=transforming=false; assert(Update(&aligned) && aligned); // Resume without reinitializing anything.
  }
  Reset(); enemy.dead=true; Rejected(); Reset(); pawn.valid=false; Rejected();
  Reset(); pawn.bytes[0x66]=2; Rejected(); Reset(); world.bytes[0x39C]=1; Rejected();
  Reset(); Link(aimer,0x28,&enemy); Rejected(); Reset(); Link(weapon,0x94,&enemy); Rejected();
  Reset(); Link(pawn,0x1D4,&enemy); Rejected(); Reset(); Link(enemy,0x98,&foreign); Rejected();
  Reset(); Put(world_slot,0,reinterpret_cast<uintptr_t>(foreign.bytes)); Rejected();
  Reset(); Put(collection.bytes,0x30,4097); Rejected(); Reset(); aimer.bytes[0x2C]=3; Rejected();
  Reset(); aimer.type=L"UnexpectedAimerSubclass"; Rejected();
  Reset(); Position(enemy,NAN,0); Rejected(); Reset(); Position(weapon,INFINITY,0); Rejected();
  Reset(); ++los_class.index; Rejected();
  Reset(); assert(!NativeUpdateAiAiming(controller.bytes,pawn.bytes,aimer.bytes,foreign.bytes,&aligned) && !aligned);
  Reset(); assert(!Update(nullptr));
  Reset(); auto thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  for(unsigned failure=1;failure<=4;++failure) {
    Reset(); dirty=failure; aligned=true; assert(!Update(&aligned) && !aligned && !g_aim_busy);
  }
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeAiAimingTests passed: 6000 native target-before-alignment updates; weapon/pawn pose, live LOS lookup, stale target and dirty ownership guards. Metadata/deep math are fixtures, not gameplay.");
}
