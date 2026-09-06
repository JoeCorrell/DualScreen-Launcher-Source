// Shipping StopMovement bodies run against bounded actor fixtures. Reflection,
// class lookup and the typed pawn getter are fixtures, not a running game.
#include "NativeAiStopMovement.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
extern "C" void WfcCallEaxEcx(void*,void*,void*);
namespace {
struct Object { alignas(16) uint8_t bytes[0x800]{}; const wchar_t* type; uint32_t index=1; bool valid=true,dead=false; };
Object controller{{},L"TnAiController"},pawn{{},L"TnAiPawn"},world{{},L"WorldInfo"},
    robot{{},L"TnRobotForm"},vehicle{{},L"TnCarForm"},strategy{{},L"TnVehicleAiControlStrategy"},
    input{{},L"TnCarInputState"},foreign{{},L"WorldInfo"};
Object* objects[]={&controller,&pawn,&world,&robot,&vehicle,&strategy,&input,&foreign};
uint8_t* image=nullptr;
alignas(16) uint8_t engine[0x44]{},context[0x2C]{},world_slot[4]{};
bool bad_offset=false,bad_bool=false; unsigned dirty=0; bool post=false;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && p==o->bytes) return o; return nullptr; }
uint32_t Read(const void* p,size_t off) { uint32_t v; std::memcpy(&v,static_cast<const uint8_t*>(p)+off,4); return v; }
void Put(void* p,size_t off,uintptr_t v) { const uint32_t n=static_cast<uint32_t>(v); std::memcpy(static_cast<uint8_t*>(p)+off,&n,4); }
void Link(Object& o,size_t off,Object* v) { Put(o.bytes,off,reinterpret_cast<uintptr_t>(v ? v->bytes:nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* target) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(At(va)+5)); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if(!o) return false;
  const auto field=[&](const wchar_t* n,uint32_t off) { if(std::wcscmp(n,name)) return false; *out=off; return true; };
  if(field(L"WorldInfo",0x98)) return true;
  if(o==&controller) return field(L"Pawn",0x1C8)||field(L"_VehicleAiControl",0x3CC);
  if(o==&pawn) return field(L"Controller",0x1D4)||field(L"_RobotForm",0x57C)||field(L"_VehicleForm",0x580)||field(L"_CurrentForm",0x584);
  if(o==&strategy) return field(L"_BoundController",0x28)||field(L"_TransformerPawn",0x2C);
  if(o==&vehicle) return field(L"_InputState",!std::wcscmp(vehicle.type,L"TnTankForm") ? 0x104:0x108);
  return false;
}
void Reset(unsigned mode=0,bool physical_player=false) {
  uint32_t id=1; for(auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->index=id++; o->valid=true; o->dead=false; }
  pawn.type=physical_player ? L"TnPlayerPawn":L"TnAiPawn";
  vehicle.type=mode==2 ? L"TnPlaneForm":mode==3 ? L"TnTankForm":mode==4 ? L"TnTruckForm":L"TnCarForm";
  input.type=mode==2 ? L"TnPlaneInputState":mode==3 ? L"TnTankInputState":L"TnCarInputState";
  Link(controller,0x1C8,&pawn); Link(pawn,0x1D4,&controller);
  for(auto* o:{&controller,&pawn}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Link(pawn,0x57C,&robot); Link(pawn,0x580,&vehicle); Link(pawn,0x584,mode ? &vehicle:&robot);
  Link(controller,0x3CC,&strategy); Link(strategy,0x28,&controller); Link(strategy,0x2C,&pawn);
  Link(vehicle,mode==3 ? 0x104:0x108,&input); Link(input,0x2C,&pawn);
  Put(strategy.bytes,0x68,0xABCDEFAD); Put(input.bytes,mode==2 ? 0x4C:0x48,0xFEDCBA99);
  Put(robot.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x12900000)));
  Put(vehicle.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x12900200)));
  Put(At(0x12900000),0x70,reinterpret_cast<uintptr_t>(At(0x10902000)));
  Put(At(0x12900200),0x70,reinterpret_cast<uintptr_t>(At(0x10902010)));
  Put(At(0x12900200),0xD0,reinterpret_cast<uintptr_t>(At(mode==2 ? stop_code::kPlaneBoostVa:mode==3 ? stop_code::kTankBoostVa:stop_code::kCarBoostVa)));
  const uint8_t zero[]={0x32,0xC0,0xC3,0,0,0},one[]={0xB0,1,0xC3,0,0,0};
  std::memcpy(At(0x10902000),zero,6); std::memcpy(At(0x10902010),one,6);
  Put(At(0x12B10A9C),0,reinterpret_cast<uintptr_t>(engine)); Put(engine,0x40,reinterpret_cast<uintptr_t>(context));
  Put(context,0x28,reinterpret_cast<uintptr_t>(world_slot)); Put(world_slot,0,reinterpret_cast<uintptr_t>(world.bytes));
  // Nonzero pose, velocity, acceleration, desired rotation, health/inventory
  // sentinels: only acceleration and desired rotation may change on foot.
  for(size_t i=0xD8;i<0x108;++i) pawn.bytes[i]=static_cast<uint8_t>(i);
  for(size_t i=0x174;i<0x180;++i) pawn.bytes[i]=0xCC;
  Put(pawn.bytes,0x3F0,123); Put(pawn.bytes,0x428,0xDEADBEEF);
  bad_offset=bad_bool=false; dirty=0; post=false;
  g_stop_module=reinterpret_cast<HMODULE>(image); g_stop_thread=GetCurrentThreadId(); g_stop_busy=false;
}
NativeAiMovementResult Stop() { return NativeAiStopMovement(controller.bytes,pawn.bytes); }
void Rejected() {
  const auto p=pawn,s=strategy,i=input;
  assert(Stop()==NativeAiMovementResult::kRejected);
  assert(!std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)) && !std::memcmp(s.bytes,strategy.bytes,sizeof(s.bytes)) && !std::memcmp(i.bytes,input.bytes,sizeof(i.bytes)));
}
DWORD WINAPI WrongThread(void*) { Rejected(); return 0; }
void* __cdecl ClassLookup(void*,void*) { return foreign.bytes+0x10; }
// Native IsA is thiscall, one stack argument and RET4; fastcall dummy EDX
// reproduces that ABI without relying on compiler-specific member syntax.
uint32_t __attribute__((fastcall)) IsAi(void* p,void*,void*) { return p==pawn.bytes && !std::wcscmp(pawn.type,L"TnAiPawn"); }
void* __attribute__((regparm(1))) TypedPawn(void* c) {
  assert(c==controller.bytes+0x10); return !std::wcscmp(pawn.type,L"TnAiPawn") ? pawn.bytes+0x10:reinterpret_cast<void*>(0x10);
}
void StockStop() { WfcCallEaxEcx(At(stop_code::kControllerVa),controller.bytes+0x10,nullptr); }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* name) {
  auto* o=Lookup(p); if(!o) return false;
  if(!std::wcscmp(o->type,name)) return true;
  if(!std::wcscmp(name,L"TnPawn")) return o==&pawn;
  if(!std::wcscmp(name,L"TnForm")) return o==&robot||o==&vehicle;
  if(!std::wcscmp(name,L"TnVehicleForm")) return o==&vehicle;
  if(!std::wcscmp(name,L"TnCarForm")) return o==&vehicle && !std::wcscmp(vehicle.type,L"TnTruckForm");
  return false;
}
uint32_t UnrealObjectIndex(void* p) {
  if(g_stop_busy && dirty && !post) {
    post=true;
    if(dirty==1) ++pawn.index;
    if(dirty==2) Link(controller,0x1C8,nullptr);
    if(dirty==3) Link(pawn,0x584,&vehicle);
    if(dirty==4) Put(strategy.bytes,0x68,Read(strategy.bytes,0x68)^4);
    if(dirty==5) Put(input.bytes,0x48,Read(input.bytes,0x48)^4);
    if(dirty==6) Put(pawn.bytes,0xE4,0);
    if(dirty==7) Link(strategy,0x2C,nullptr);
  }
  const auto* o=Lookup(p); return o ? o->index:0;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(!Offset(p,name,out)) return false; if(bad_offset) ++*out; return true; }
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) { uint32_t off=0; if(!Offset(p,name,&off)) return false; *out=reinterpret_cast<void*>(Read(p,off)); return true; }
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if(!Lookup(p)) return false;
  if(!std::wcscmp(name,L"Role")) *out=0x66;
  else if(!std::wcscmp(name,L"NetMode")) *out=0x39C;
  else return false;
  return true;
}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) { uint32_t off=0; if(!UnrealResolveBytePropertyOffset(p,name,&off)) return false; *out=static_cast<uint8_t*>(p)[off]; return true; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) {
  const auto* o=Lookup(p); if(!o) return false;
  if(!std::wcscmp(name,L"bDeleteMe")) *out=o->dead;
  else if(o==&strategy && !std::wcscmp(name,L"_StopMovement")) *out=(Read(p,0x68)&2)!=0;
  else if(o==&input && !std::wcscmp(name,L"_Boosting")) *out=(Read(p,!std::wcscmp(input.type,L"TnPlaneInputState") ? 0x4C:0x48)&1)!=0;
  else return false;
  if(bad_bool && o==&strategy) *out=!*out;
  return true;
}
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* name,uint32_t size,uint32_t* out) {
  if(p!=pawn.bytes || size!=12) return false;
  if(!std::wcscmp(name,L"Rotation")) *out=0xE4;
  else if(!std::wcscmp(name,L"Acceleration")) *out=0xFC;
  else if(!std::wcscmp(name,L"DesiredRotation")) *out=0x174;
  else return false;
  if(bad_offset) ++*out;
  return true;
}
}
int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE)); assert(image);
  std::vector<uint8_t> exe;
  if(argc>1) { std::ifstream f(argv[1],std::ios::binary); exe.assign(std::istreambuf_iterator<char>(f),{}); assert(exe.size()==37667328); }
  stop_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if(!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  assert(BindStopImage(reinterpret_cast<HMODULE>(image)));
  stop_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for(size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!BindStopImage(reinterpret_cast<HMODULE>(image))); At(va)[i]^=1; }
    return true;
  });
  Jump(0x110031A0,reinterpret_cast<void*>(&ClassLookup)); Jump(0x10901430,reinterpret_cast<void*>(&IsAi)); Jump(0x10BB49D0,reinterpret_cast<void*>(&TypedPawn));
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000);
  Reset(); assert(!PrepareNativeAiStopMovement(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),controller.bytes,pawn.bytes)); // Not the host EXE.
  for(unsigned n=0;n<6000;++n) {
    const unsigned mode=n%5; Reset(mode);
    const auto before_p=pawn,before_s=strategy,before_i=input,before_c=controller,before_r=robot,before_v=vehicle;
    StockStop(); const auto stock_p=pawn,stock_s=strategy,stock_i=input;
    pawn=before_p; strategy=before_s; input=before_i;
    assert(Stop()==NativeAiMovementResult::kApplied);
    assert(!std::memcmp(pawn.bytes,stock_p.bytes,sizeof(pawn.bytes)));
    assert(!std::memcmp(strategy.bytes,stock_s.bytes,sizeof(strategy.bytes)));
    assert(!std::memcmp(input.bytes,stock_i.bytes,sizeof(input.bytes)));
    auto expected_p=before_p,expected_s=before_s,expected_i=before_i;
    if(mode) { Put(expected_s.bytes,0x68,Read(before_s.bytes,0x68)|2); Put(expected_i.bytes,mode==2 ? 0x4C:0x48,Read(before_i.bytes,mode==2 ? 0x4C:0x48)&~1u); }
    else { std::memset(expected_p.bytes+0xFC,0,12); std::memcpy(expected_p.bytes+0x174,expected_p.bytes+0xE4,12); }
    assert(!std::memcmp(pawn.bytes,expected_p.bytes,sizeof(pawn.bytes)) && !std::memcmp(strategy.bytes,expected_s.bytes,sizeof(strategy.bytes)) && !std::memcmp(input.bytes,expected_i.bytes,sizeof(input.bytes)));
    assert(!std::memcmp(controller.bytes,before_c.bytes,sizeof(controller.bytes)) && !std::memcmp(robot.bytes,before_r.bytes,sizeof(robot.bytes)) && !std::memcmp(vehicle.bytes,before_v.bytes,sizeof(vehicle.bytes)));
    assert(Stop()==NativeAiMovementResult::kApplied); // Already stopped: idempotent.
    Reset(0,true); const auto player=pawn; StockStop(); assert(!std::memcmp(pawn.bytes,player.bytes,sizeof(pawn.bytes))); // Reproduce old silent no-op.
    assert(Stop()==NativeAiMovementResult::kApplied); auto expected=player;
    std::memset(expected.bytes+0xFC,0,12); std::memcpy(expected.bytes+0x174,expected.bytes+0xE4,12);
    assert(!std::memcmp(pawn.bytes,expected.bytes,sizeof(pawn.bytes)));
  }
  for(unsigned mode=0;mode<5;++mode) {
    Reset(mode); bad_offset=true; Rejected();
    Reset(mode); controller.bytes[0x66]=1; Rejected();
    Reset(mode); pawn.bytes[0x66]=1; Rejected();
    Reset(mode); world.bytes[0x39C]=2; Rejected();
    Reset(mode); Link(pawn,0x1D4,nullptr); Rejected();
    Reset(mode); Link(controller,0x1C8,&foreign); Rejected();
    Reset(mode); Link(pawn,0x98,&foreign); Rejected();
    Reset(mode); Put(world_slot,0,reinterpret_cast<uintptr_t>(foreign.bytes)); Rejected();
    Reset(mode); Link(pawn,0x584,nullptr); Rejected();
    Reset(mode); At(mode ? 0x10902010:0x10902000)[0]=0xE9; Rejected();
    Reset(mode); g_stop_busy=true; Rejected(); g_stop_busy=false;
    for(auto* o:{&pawn,&controller,&world}) { Reset(mode); o->dead=true; Rejected(); }
    for(auto* o:objects) { if(o==&foreign || (mode && o==&robot) || (!mode && (o==&strategy||o==&input||o==&vehicle))) continue; Reset(mode); o->valid=false; Rejected(); }
  }
  for(unsigned mode=1;mode<5;++mode) {
    Reset(mode,true); Rejected(); // Never send a physical player through stock vehicle AI.
    Reset(mode); Link(strategy,0x28,nullptr); Rejected();
    Reset(mode); Link(strategy,0x2C,&foreign); Rejected();
    Reset(mode); Link(input,0x2C,&foreign); Rejected();
    Reset(mode); bad_bool=true; Rejected();
    Reset(mode); Put(At(0x12900200),0xD0,reinterpret_cast<uintptr_t>(At(0x10902010))); Rejected();
  }
  Reset(1);
  const uint8_t stock_mode[]={0xB8,1,0,0,0,0xC3};
  if(!exe.empty()) assert(!std::memcmp(exe.data()+0x109D62F0-0x10900C00,stock_mode,6));
  std::memcpy(At(0x10902010),stock_mode,6); assert(Stop()==NativeAiMovementResult::kApplied);
  At(0x10902010)[2]=1; Rejected(); // Full immediate matters, not only AL.
  Reset(); const uint8_t zero_mode[]={0xB8,0,0,0,0,0xC3};
  std::memcpy(At(0x10902000),zero_mode,6); assert(Stop()==NativeAiMovementResult::kApplied);
  Reset(); HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  for(unsigned failure=1;failure<=7;++failure) {
    Reset(failure==4||failure==5||failure==7 ? 1:0); dirty=failure;
    assert(Stop()==NativeAiMovementResult::kStateMismatch && !g_stop_busy);
  }
  // Memory guards on actual writable ranges, independent of metadata fixtures.
  auto* protected_page=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE); assert(protected_page);
  DWORD old=0; assert(StopMemory(protected_page,12,nullptr,false,true));
  assert(VirtualProtect(protected_page,4096,PAGE_READONLY,&old)); assert(!StopMemory(protected_page,12,nullptr,false,true));
  assert(!StopMemory(static_cast<uint8_t*>(protected_page)+4090,12));
  VirtualFree(protected_page,0,MEM_RELEASE); VirtualFree(image,0,MEM_RELEASE);
  std::puts("NativeAiStopMovementTests passed: shipping robot/vehicle oracle, 6000 native cycles, 6000 cross-role robot stops, guards and dirty state checks");
}
