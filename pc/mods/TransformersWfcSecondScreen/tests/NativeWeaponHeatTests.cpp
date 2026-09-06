// Executes shipping Sync/Clamp/state checks/selector/BeginState/observer loops.
// UObject state dispatch and weapon callbacks are fixtures, not game simulation.
#include "NativeWeaponHeat.cpp"
#include "WeaponRoleJournal.h"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <limits>
#include <vector>
using namespace wfcds;
namespace {
struct Object { alignas(16) uint8_t bytes[0x800]{};const wchar_t* type;uint32_t index=1;bool valid=true,dead=false; };
Object pawn{{},L"TnPawn"},weapon{{},L"TnWeapon"},heat{{},L"TnWeaponHeat"},pc{{},L"TnPlayerController"},ai{{},L"TnAiController"},
    world{{},L"WorldInfo"},cool{{},L"State"},hot{{},L"State"},cls{{},L"Class"},modifier{{},L"TnHeatModifier"},player_data{{},L"TnWeaponData"};
Object* objects[]={&pawn,&weapon,&heat,&pc,&ai,&world,&cool,&hot,&cls,&modifier,&player_data};
uint8_t* image=nullptr;
alignas(16) uint8_t frame[0x54]{},native_table[0x140]{};
std::array<UnrealScriptDelegate,2> overheated{},cooled{};
void* mods[1]{};
unsigned notify_hot=0,notify_cool=0,dirty=0,transitions=0,seed_dirty=0;
bool bad_offset=false,firing=false,bad_method=false;
Object* Lookup(void* p) { for(auto* o:objects) if(o->valid && p==o->bytes) return o;return nullptr; }
uint32_t Read(const void* p,size_t off) { uint32_t v=0;std::memcpy(&v,static_cast<const uint8_t*>(p)+off,4);return v; }
void Put(void* p,size_t off,uintptr_t v) { const auto x=static_cast<uint32_t>(v);std::memcpy(static_cast<uint8_t*>(p)+off,&x,4); }
void Link(Object& o,size_t off,Object* p) { Put(o.bytes,off,reinterpret_cast<uintptr_t>(p ? p->bytes:nullptr)); }
void Float(Object& o,size_t off,float f) { std::memcpy(o.bytes+off,&f,4); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* fn) { At(va)[0]=0xE9;Put(At(va),1,reinterpret_cast<uintptr_t>(fn)-reinterpret_cast<uintptr_t>(At(va)+5)); }
NativePlayerServiceResult Apply(float value) { return NativeSyncWeaponHeat(pawn.bytes,weapon.bytes,value); }
void Dirty() {
  assert(g_heat_busy && Apply(1)==NativePlayerServiceResult::kRejected);
  if(dirty==1)++weapon.index;
  if(dirty==2)Link(pc,0x300,nullptr);
  if(dirty==3)world.bytes[0x39C]=3;
  if(dirty==4)Float(heat,0x2C,50);
  if(dirty==5)Link(heat,0x40,nullptr);
  if(dirty==6)++modifier.index;
  if(dirty==7)cooled[0].compiled_function=nullptr;
}
void __attribute__((fastcall)) HotCallback(void* context,void*) { assert(context==weapon.bytes+0x10);++notify_hot;Dirty(); }
void __attribute__((fastcall)) CoolCallback(void* context,void*) { assert(context==weapon.bytes+0x10);++notify_cool;Dirty(); }
int __attribute__((fastcall)) GoState(void* receiver,void*,uint32_t name,uint32_t force,uint32_t keep) {
  assert(receiver==heat.bytes && !force && !keep);++transitions;
  const uint32_t old=Read(reinterpret_cast<void*>(Read(frame,0x38)),4);
  assert(name==31 || name==32);Put(frame,0x38,reinterpret_cast<uintptr_t>(name==31 ? cool.bytes:hot.bytes));
  using Select=void (__attribute__((fastcall)) *)(void*,void*,uint32_t*);
  reinterpret_cast<Select>(At(heat_code::kSelectStateVa))(heat.bytes+0x10,nullptr,&name);
  using Begin=void (__attribute__((fastcall)) *)(void*,void*,uint32_t);
  reinterpret_cast<Begin>(Read(reinterpret_cast<void*>(Read(heat.bytes,0x10)),0x38))(heat.bytes+0x10,nullptr,old);
  return 1;
}
void __attribute__((fastcall)) Label(void* receiver,void*,uint32_t label) { assert(receiver==heat.bytes && label==0x190000); }
void Reset(bool ai_role=false,bool is_hot=false) {
  uint32_t id=1;for(auto* o:objects){std::memset(o->bytes,0,sizeof(o->bytes));o->index=id++;o->valid=true;o->dead=false;}
  heat.type=L"TnWeaponHeat";std::memset(frame,0,sizeof(frame));std::memset(native_table,0,sizeof(native_table));
  for(auto* o:{&pawn,&weapon,&pc,&ai}){Link(*o,0x98,&world);o->bytes[0x66]=3;}
  Link(pawn,0x1D4,ai_role ? &ai:&pc);Link(pc,0x1C8,&pawn);Link(ai,0x1C8,&pawn);Link(pc,0x300,&pawn);
  Link(weapon,0x80,&pawn);Link(weapon,0x94,&pawn);Link(weapon,0x324,&heat);Link(heat,0x40,&pawn);
  Link(weapon,0x244,&player_data);Float(player_data,0x2C,100);
  Float(heat,0x2C,100);Float(heat,0x3C,is_hot ? 100:25);
  Put(heat.bytes,0x28,reinterpret_cast<uintptr_t>(frame));Put(frame,0x38,reinterpret_cast<uintptr_t>(is_hot ? hot.bytes:cool.bytes));
  Put(cool.bytes,4,31);Put(hot.bytes,4,32);Put(heat.bytes,0x14,1);
  Put(At(0x12A29FD4),4,reinterpret_cast<uintptr_t>(cls.bytes));Put(At(0x12B302CC),0,31);Put(At(0x12B31E80),0,32);
  Put(heat.bytes,0,reinterpret_cast<uintptr_t>(native_table));Put(native_table,0x12C,reinterpret_cast<uintptr_t>(At(0x11010000)));
  Put(native_table,0x138,reinterpret_cast<uintptr_t>(At(0x11010010)));
  Put(heat.bytes,0x10,reinterpret_cast<uintptr_t>(At(is_hot ? 0x127FA238:0x127FA1E8)));
  for(bool h:{false,true}) { auto* t=At(h ? 0x127FA238:0x127FA1E8);
    Put(t,0x14,reinterpret_cast<uintptr_t>(At(heat_code::kSelectStateVa)));
    Put(t,0x38,reinterpret_cast<uintptr_t>(At(h ? heat_code::kOverheatedBeginVa:heat_code::kCoolBeginVa)));
    Put(t,0x48,reinterpret_cast<uintptr_t>(At(h ? heat_code::kOverheatedCheckVa:heat_code::kCoolCheckVa)));
  }
  overheated={};cooled={};
  overheated[0]={weapon.bytes,41u<<14,weapon.bytes+0x10,At(0x11010020)};
  cooled[0]={weapon.bytes,42u<<14,weapon.bytes+0x10,At(0x11010030)};
  UnrealArrayView a{overheated.data(),1,2},b{cooled.data(),1,2};
  std::memcpy(heat.bytes+0x44,&a,12);std::memcpy(heat.bytes+0x50,&b,12);
  mods[0]=modifier.bytes;UnrealArrayView m{mods,1,1};std::memcpy(heat.bytes+0x30,&m,12);
  Float(modifier,0x50,13.75f); // A live modifier timer must not be recreated/reset.
  notify_hot=notify_cool=dirty=transitions=seed_dirty=0;bad_offset=firing=bad_method=false;
  g_heat_module=reinterpret_cast<HMODULE>(image);g_heat_thread=GetCurrentThreadId();g_heat_busy=false;
}
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p);if(!o)return false;
  const auto f=[&](const wchar_t* n,uint32_t off){if(std::wcscmp(name,n))return false;*out=off;return true;};
  if(f(L"WorldInfo",0x98))return true;
  if(o==&pawn)return f(L"Controller",0x1D4);
  if(o==&pc || o==&ai)return f(L"Pawn",0x1C8)||f(L"AcknowledgedPawn",0x300);
  if(o==&weapon)return f(L"Owner",0x80)||f(L"Instigator",0x94)||f(L"WeaponHeat",0x324)||f(L"PlayerData",0x244);
  if(o==&heat)return f(L"OwnerPawn",0x40);
  return false;
}
void Reject(float value=50) { const auto h=heat;const unsigned n=transitions;
  assert(Apply(value)==NativePlayerServiceResult::kRejected && n==transitions && !std::memcmp(heat.bytes,h.bytes,sizeof(h.bytes))); }
DWORD WINAPI WrongThread(void*) { Reject();return 0; }
}
namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* type) { auto* o=Lookup(p);return o && !std::wcscmp(o->type,type); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p);return o ? o->index:0; }
bool UnrealObjectClassName(void* p,char* out,size_t n) { auto* o=Lookup(p);if(!o)return false;std::snprintf(out,n,"%ls",o->type);return true; }
bool UnrealObjectName(void* p,char* out,size_t n) { if(p!=cool.bytes && p!=hot.bytes)return false;std::snprintf(out,n,"%s",p==cool.bytes ? "Cool":"Overheated");return true; }
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) { uint32_t off=0;if(!Offset(p,name,&off))return false;*out=reinterpret_cast<void*>(Read(p,off));return true; }
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(!Offset(p,name,out))return false;if(bad_offset)++*out;return true; }
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) { if(!Lookup(p))return false;
  if(!std::wcscmp(name,L"Role"))*out=static_cast<uint8_t*>(p)[0x66];else if(!std::wcscmp(name,L"NetMode"))*out=static_cast<uint8_t*>(p)[0x39C];else return false;return true; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) { auto* o=Lookup(p);if(!o)return false;
  if(!std::wcscmp(name,L"bDeleteMe"))*out=o->dead;else if(o==&weapon && !std::wcscmp(name,L"bIsFiring"))*out=firing;else return false;return true; }
bool UnrealResolveFloatPropertyOffset(void* p,const wchar_t* name,uint32_t* out) { if(p!=heat.bytes || std::wcscmp(name,L"CurrentHeat"))return false;*out=0x3C+(bad_offset ? 4:0);return true; }
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out) { uint32_t off=0;if(!UnrealResolveFloatPropertyOffset(p,name,&off))return false;std::memcpy(out,static_cast<uint8_t*>(p)+off,4);return true; }
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* name,uint32_t size,uint32_t* out) { if(p!=heat.bytes || std::wcscmp(name,L"HeatProperties") || size!=16)return false;*out=0x2C;return true; }
bool UnrealReadStructProperty(void* p,const wchar_t* name,UnrealStructValueView* out) { uint32_t off=0;if(!UnrealResolveStructPropertyOffset(p,name,16,&off))return false;*out={heat.bytes+off,16,cls.bytes};return true; }
bool UnrealReadStructFloatProperty(void* p,const wchar_t* prop,const wchar_t* name,float* out) {
  if((p!=heat.bytes && p!=player_data.bytes) || std::wcscmp(prop,L"HeatProperties") || std::wcscmp(name,L"HeatMax"))return false;
  if(g_heat_busy && seed_dirty && p==heat.bytes){
    const auto fault=seed_dirty;seed_dirty=0;
    assert(NativeSeedWeaponHeatCapacity(pawn.bytes,weapon.bytes,player_data.bytes,false)==NativePlayerServiceResult::kRejected);
    if(fault==1)Float(player_data,0x2C,50);
    if(fault==2)++weapon.index;
    if(fault==3)Link(heat,0x40,nullptr);
    if(fault==4)Link(pawn,0x1D4,&pc);
    if(fault==5)Put(heat.bytes,0x34,1);
    if(fault==6)Float(heat,0x3C,1);
    if(fault==7){Put(frame,0x38,reinterpret_cast<uintptr_t>(hot.bytes));Put(heat.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x127FA238)));}
  }
  std::memcpy(out,static_cast<uint8_t*>(p)+0x2C,4);return true;
}
bool UnrealReadStructTypedArray(const UnrealStructValueView& v,const wchar_t* name,const wchar_t* type,uint32_t size,UnrealArrayView* out,uint32_t max) {
  assert(v.data==heat.bytes+0x2C && !std::wcscmp(name,L"Modifiers") && !std::wcscmp(type,L"ObjectProperty") && size==4 && max==64);
  std::memcpy(out,heat.bytes+0x30,12);return out->count>=0 && out->count<=out->capacity && out->capacity<=64;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out) { if(i<0 || i>=a.count || !HeatMemory(a.data,a.count*4))return false;*out=static_cast<void**>(a.data)[i];return true; }
bool UnrealReadDelegateArrayProperty(void* p,const wchar_t* name,UnrealArrayView* out,uint32_t* off) {
  if(p!=heat.bytes)return false;
  if(!std::wcscmp(name,L"WeaponOverheatedObservers"))*off=0x44;else if(!std::wcscmp(name,L"WeaponCooledObservers"))*off=0x50;else return false;
  std::memcpy(out,heat.bytes+*off,12);return true;
}
bool UnrealUnnumberedNameEquals(uint32_t value,const wchar_t* name) {
  if(bad_method)return false;
  if(!std::wcscmp(name,L"WeaponOverheated"))return value==(41u<<14) || value==(43u<<14);
  if(!std::wcscmp(name,L"WeaponCooled"))return value==(42u<<14);
  return false;
}
}
int main(int argc,char** argv) {
  InventoryWeaponRole original{weapon.bytes,2,1,pc.bytes,ai.bytes,heat.bytes,3,100,true,false};
  auto evolved=original;evolved.current_heat=27.5f;
  assert(SameInventoryWeaponRoleIdentity(original,evolved));
  for(unsigned i=0;i<8;++i) {
    auto changed=evolved;
    if(i==0)changed.weapon=pawn.bytes;
    if(i==1)++changed.object_index;
    if(i==2)++changed.previous_data_type;
    if(i==3)changed.previous_data=nullptr;
    if(i==4)changed.desired_data=nullptr;
    if(i==5)changed.heat_state_captured=false;
    if(i==6)changed.weapon_heat=nullptr;
    if(i==7)++changed.weapon_heat_index;
    assert(!SameInventoryWeaponRoleIdentity(original,changed));
  }
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
  std::vector<uint8_t> exe;if(argc>1){std::ifstream f(argv[1],std::ios::binary);exe.assign(std::istreambuf_iterator<char>(f),{});assert(exe.size()==37667328);}
  heat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> ops){
    if(!exe.empty())assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto r=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),ops);std::memcpy(At(va),r.data(),r.size());return true;
  });
  if(!exe.empty()) for(bool h:{false,true}) {
    const auto off=(h ? 0x127FA238u:0x127FA1E8u)-0x10901200u;
    assert(Read(exe.data(),off+0x14)==heat_code::kSelectStateVa);
    assert(Read(exe.data(),off+0x38)==(h ? heat_code::kOverheatedBeginVa:heat_code::kCoolBeginVa));
    assert(Read(exe.data(),off+0x48)==(h ? heat_code::kOverheatedCheckVa:heat_code::kCoolCheckVa));
  }
  assert(BindHeatImage(reinterpret_cast<HMODULE>(image)));
  heat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>){
    for(size_t i=0;i<sizeof(code);++i){At(va)[i]^=1;assert(!BindHeatImage(reinterpret_cast<HMODULE>(image)));At(va)[i]^=1;}return true;
  });
  Jump(0x11010000,reinterpret_cast<void*>(&GoState));Jump(0x11010010,reinterpret_cast<void*>(&Label));
  Jump(0x11010020,reinterpret_cast<void*>(&HotCallback));Jump(0x11010030,reinterpret_cast<void*>(&CoolCallback));
  for(unsigned i=0;i<3000;++i) {
    Reset((i&1)!=0,(i&2)!=0);assert(PrepareNativeWeaponHeat(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes));
    const auto w=weapon,m=modifier,p=pawn;
    assert(Apply(100)==NativePlayerServiceResult::kApplied && notify_hot==((i&2) ? 0u:1u));
    assert(Apply(100)==NativePlayerServiceResult::kApplied); // Same-state updates do not replay BeginState.
    assert(Apply(0)==NativePlayerServiceResult::kApplied && notify_cool==1);
    assert(Apply(99.999f)==NativePlayerServiceResult::kApplied && notify_cool==1);
    assert(!std::memcmp(w.bytes,weapon.bytes,sizeof(w.bytes)) && !std::memcmp(m.bytes,modifier.bytes,sizeof(m.bytes)) && !std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)));
  }
  for(unsigned d=1;d<=7;++d){Reset();dirty=d;assert(Apply(100)==NativePlayerServiceResult::kStateMismatch && !g_heat_busy && notify_hot==1);}
  Reset();Reject(101);Reject(-1);Reject(std::numeric_limits<float>::infinity());Reject(std::numeric_limits<float>::quiet_NaN());
  Reset();Float(heat,0x2C,0);Reject();Reset();bad_offset=true;Reject();Reset();bad_method=true;Reject();Reset();firing=true;Reject();
  Reset(true,false);Float(heat,0x2C,0);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);
  const auto uninitialized=heat;
  assert(PrepareNativeWeaponHeat(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes));
  assert(!std::memcmp(uninitialized.bytes,heat.bytes,sizeof(heat.bytes)) && !notify_hot && !notify_cool);
  Reject(0); // Preparing an AI service never authorizes zero-capacity Sync.
  Float(heat,0x3C,1);assert(!PrepareNativeWeaponHeat(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes));
  Float(heat,0x3C,0);Put(heat.bytes,0x34,1);
  assert(!PrepareNativeWeaponHeat(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes));
  Reset(true,true);Float(heat,0x2C,0);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);
  assert(!PrepareNativeWeaponHeat(reinterpret_cast<HMODULE>(image),GetCurrentThreadId(),pawn.bytes,weapon.bytes));
  const auto seed=[&](bool undo=false){return NativeSeedWeaponHeatCapacity(pawn.bytes,weapon.bytes,player_data.bytes,undo);};
  for(unsigned i=0;i<2000;++i){
    Reset(true);Float(heat,0x2C,0);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);
    assert(NativeWeaponHeatInitializationStable(pawn.bytes,weapon.bytes));
    const auto original_heat=heat,w=weapon,p=pawn,m=modifier;auto expected=heat;Float(expected,0x2C,100);
    assert(seed()==NativePlayerServiceResult::kApplied && seed()==NativePlayerServiceResult::kApplied);
    assert(NativeWeaponHeatInitializationStable(pawn.bytes,weapon.bytes));
    assert(!std::memcmp(expected.bytes,heat.bytes,sizeof(heat.bytes)));
    assert(seed(true)==NativePlayerServiceResult::kApplied && seed(true)==NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(original_heat.bytes,heat.bytes,sizeof(heat.bytes)) && !notify_hot && !notify_cool && !transitions);
    assert(!std::memcmp(w.bytes,weapon.bytes,sizeof(w.bytes)) && !std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)) &&
        !std::memcmp(m.bytes,modifier.bytes,sizeof(m.bytes)));
  }
  const auto reject_seed=[&](){const auto h=heat;assert(seed()==NativePlayerServiceResult::kRejected &&
      !std::memcmp(h.bytes,heat.bytes,sizeof(h.bytes)) && !transitions);};
  Reset();Float(heat,0x3C,0);Put(heat.bytes,0x34,0);reject_seed();
  Reset(true);Put(heat.bytes,0x34,0);reject_seed();
  Reset(true);Float(heat,0x3C,0);reject_seed();
  Reset(true,true);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);reject_seed();
  Reset(true);Float(heat,0x3C,0);Float(heat,0x2C,50);Put(heat.bytes,0x34,0);reject_seed();
  Reset(true);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);Float(player_data,0x2C,0);reject_seed();
  Float(player_data,0x2C,std::numeric_limits<float>::quiet_NaN());reject_seed();
  Reset(true);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);g_heat_busy=true;reject_seed();g_heat_busy=false;
  g_heat_thread=GetCurrentThreadId()+1;reject_seed();g_heat_thread=GetCurrentThreadId();
  At(heat_code::kSyncVa)[0]^=1;reject_seed();At(heat_code::kSyncVa)[0]^=1;
  for(unsigned f=1;f<=7;++f){
    Reset(true);Float(heat,0x2C,0);Float(heat,0x3C,0);Put(heat.bytes,0x34,0);seed_dirty=f;
    assert(seed()==NativePlayerServiceResult::kStateMismatch && !g_heat_busy && !notify_hot && !notify_cool && !transitions);
  }
  Reset(true);Float(heat,0x3C,0);assert(NativeWeaponHeatInitializationStable(pawn.bytes,weapon.bytes));
  Float(heat,0x3C,1);assert(!NativeWeaponHeatInitializationStable(pawn.bytes,weapon.bytes));
  Reset(true,true);Float(heat,0x3C,0);assert(!NativeWeaponHeatInitializationStable(pawn.bytes,weapon.bytes));
  Reset();Link(weapon,0x324,nullptr);Reject();Reset();Link(weapon,0x80,&world);Reject();Reset();Link(heat,0x40,&world);Reject();
  Reset();Link(pc,0x300,nullptr);Reject();Reset();world.bytes[0x39C]=3;Reject();Reset();weapon.bytes[0x66]=1;Reject();
  Reset();Put(frame,0x4C,1);Reject();Reset();Put(At(0x127FA238),0x48,0);Reject();Reset();Put(heat.bytes,0x28,0);Reject();
  Reset();overheated[0].compiled_context=heat.bytes+0x10;Reject();Reset();overheated[0].compiled_function=nullptr;Reject();
  Reset();overheated[0].function_name=41;Reject();Reset();overheated[0].function_name|=1;Reject();
  Reset();Link(weapon,0x94,nullptr);Link(heat,0x40,nullptr);Put(heat.bytes,0x48,0);Put(heat.bytes,0x54,0);
  assert(Apply(100)==NativePlayerServiceResult::kApplied && !notify_hot); // Holstered, no callbacks or instigator.
  Reset();Link(heat,0x40,nullptr);Reject();
  Reset();overheated[1]=overheated[0];Put(heat.bytes,0x48,2);Reject();Reset();Put(heat.bytes,0x48,17);Reject();
  Reset();overheated[1]=overheated[0];overheated[1].function_name=43u<<14;Put(heat.bytes,0x48,2);Reject();
  for(auto* o:{&pawn,&weapon,&heat,&pc,&world,&cool,&modifier}){Reset();o->valid=false;Reject();}
  Reset();g_heat_busy=true;Reject();g_heat_busy=false;
  Reset();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t);assert(WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  VirtualFree(image,0,MEM_RELEASE);
  std::puts("NativeWeaponHeatTests passed: 3000 native heat cycles and 2000 empty-AI capacity seed/undo cycles; exact state/byte preservation, zero seed callbacks, signatures, ownership and dirty guards (metadata/weapon callbacks are fixtures, not gameplay)");
}
