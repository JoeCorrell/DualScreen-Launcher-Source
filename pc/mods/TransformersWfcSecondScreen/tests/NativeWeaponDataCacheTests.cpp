// Production derived-cache port. Control/metadata/GC are fixtures; the shared
// reference writer has its own actual implementation/ABI suite.
#include "NativeWeaponDataCache.cpp"
#include "WeaponRoleJournal.h"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
using namespace wfcds;
namespace {
struct alignas(4096) Object { uint8_t bytes[0x800]{};uint32_t index=1;bool valid=true; };
Object pawn,weapon,lerper,world,old_data,new_data,controller,other,thrower;
Object* objects[]={&pawn,&weapon,&lerper,&world,&old_data,&new_data,&controller,&other,&thrower};
NativeWeaponProgress context;
NativeWeaponDataCache saved;
unsigned kind=0,writes=0,dirty=0;
bool bad_layout=false,bad_type=false,ambiguous=false,fail_write=false,publish_failure=false,control_valid=true;
void* thrower_owner=nullptr;
void* thrower_outer=nullptr;
void* timer_object=nullptr;
int32_t timer_count=0;
unsigned fail_write_number=0,mid_write_dirty=0;
void SetThrowerCache(void* p){std::memcpy(thrower.bytes+0x34,&p,4);}
void* GetThrowerCache(){void* p=nullptr;std::memcpy(&p,thrower.bytes+0x34,4);return p;}
Object* Lookup(void* p){for(auto* o:objects)if(p==o->bytes && o->valid)return o;return nullptr;}
uint32_t CacheOffset(){return kind==4 ? 0x440:kind ? kWeaponCacheSchemas[kind-1].known_offset:0;}
void SetCache(void* p){if(kind)std::memcpy(weapon.bytes+CacheOffset(),&p,4);}
void* GetCache(){void* p=nullptr;if(kind)std::memcpy(&p,weapon.bytes+CacheOffset(),4);return p;}
bool Capture(NativeWeaponDataCache* out){return NativeCaptureWeaponDataCache(GetCurrentThreadId(),pawn.bytes,weapon.bytes,new_data.bytes,out);}
void Reset(unsigned k=1){
  uint32_t id=1;for(auto* o:objects){*o={};o->index=id++;}kind=k;
  writes=dirty=0;bad_layout=bad_type=ambiguous=fail_write=publish_failure=false;control_valid=true;g_data_cache_busy=false;
  thrower_owner=pawn.bytes;thrower_outer=weapon.bytes;timer_object=nullptr;timer_count=0;fail_write_number=mid_write_dirty=0;
  context={};context.objects={pawn.bytes,weapon.bytes,lerper.bytes,world.bytes,old_data.bytes,controller.bytes,pawn.bytes};
  for(size_t i=0;i<context.objects.size();++i)context.indices[i]=Lookup(context.objects[i])->index;
  context.captured=true;context.thread=GetCurrentThreadId();context.interval={1,2,4,1};
  SetCache(old_data.bytes);SetThrowerCache(old_data.bytes);assert(Capture(&saved));
}
void Data(bool rollback=false){context.objects[4]=rollback ? old_data.bytes:new_data.bytes;context.indices[4]=Lookup(context.objects[4])->index;}
void Reject(bool rollback=false){const auto w=weapon;const unsigned n=writes;
  assert(NativeRetargetWeaponDataCache(saved,rollback)==NativePlayerServiceResult::kRejected && n==writes);
  assert(!std::memcmp(w.bytes,weapon.bytes,sizeof(w.bytes)));
}
DWORD WINAPI WrongThread(void*){Reject();return 0;}
void Dirty(){
  if(!g_data_cache_busy || !dirty)return;
  assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kRejected);
  NativeWeaponDataCache rejected;assert(!Capture(&rejected) && !rejected.captured);
  const unsigned d=dirty;dirty=0;
  if(d==1)++context.indices[1];
  if(d==2)context.objects[5]=other.bytes;
  if(d==3)context.objects[4]=old_data.bytes;
  if(d==4)context.objects[6]=nullptr;
  if(d==5)SetCache(other.bytes);
  if(d==6)control_valid=false;
  if(d==7)bad_layout=true;
}
}
namespace wfcds {
bool NativeCaptureWeaponProgress(DWORD thread,void* p,void* w,NativeWeaponProgress* out){
  *out={};if(!thread || thread!=GetCurrentThreadId() || p!=pawn.bytes || w!=weapon.bytes)return false;
  Dirty();if(!control_valid)return false;*out=context;return true;
}
bool UnrealObjectIsA(void* p,const wchar_t* type){
  if(!Lookup(p))return false;
  if(p==thrower.bytes)return !std::wcscmp(type,L"TnGrenadeThrower");
  if(p==weapon.bytes){for(size_t i=0;i<kWeaponCacheSchemas.size();++i) {
    if(!std::wcscmp(type,kWeaponCacheSchemas[i].pawn_class))return kind==i+1 || (ambiguous && i==2);
    }
    return false;
  }
  if(p==old_data.bytes || p==new_data.bytes || p==other.bytes){
    if(!std::wcscmp(type,L"TnWeaponData"))return true;
    if(kind && !std::wcscmp(type,kWeaponCacheSchemas[kind-1].data_class))return !(bad_type && p==new_data.bytes);
  }
  return false;
}
uint32_t UnrealObjectIndex(void* p){auto* o=Lookup(p);return o ? o->index:0;}
void* UnrealObjectOuter(void* p){return p==thrower.bytes && thrower.valid ? thrower_outer:nullptr;}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out){
  if(p!=pawn.bytes || std::wcscmp(name,L"Timers"))return false;
  *out={&timer_object,timer_count,1,20,world.bytes};return true;
}
bool UnrealReadStructArrayObjectField(const UnrealStructArrayView&,int32_t i,const wchar_t* name,void** out){
  if(i!=0 || std::wcscmp(name,L"TimerObj"))return false;
  *out=timer_object;return true;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* off){
  if(p==thrower.bytes && !std::wcscmp(name,L"GrenadeData")){*off=0x34;return true;}
  if(p!=weapon.bytes || !kind || std::wcscmp(name,kWeaponCacheSchemas[kind-1].field))return false;
  *off=CacheOffset()+(bad_layout ? 1:0);return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out){uint32_t off=0;
  if(p==weapon.bytes && !std::wcscmp(name,L"GrenadeThrower")){*out=thrower.bytes;return true;}
  if(p==thrower.bytes && !std::wcscmp(name,L"ThrowerPawn")){*out=thrower_owner;return true;}
  if(!UnrealResolveObjectPropertyOffset(p,name,&off))return false;
  *out=p==thrower.bytes ? GetThrowerCache():GetCache();return true;
}
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* expected,void* desired){
  uint32_t off=0;MEMORY_BASIC_INFORMATION info{};
  if(!UnrealResolveObjectPropertyOffset(p,name,&off) || expected!=(p==thrower.bytes ? GetThrowerCache():GetCache()) || fail_write ||
      (fail_write_number && writes+1==fail_write_number) ||
      !VirtualQuery(p,&info,sizeof(info)) || !(info.Protect&PAGE_READWRITE))return false;
  assert(g_data_cache_busy && Lookup(desired));
  if(p==thrower.bytes)SetThrowerCache(desired);else SetCache(desired);
  ++writes;
  if(writes==1 && mid_write_dirty==1){timer_count=1;timer_object=thrower.bytes;}
  if(writes==1 && mid_write_dirty==2)++thrower.index;
  if(writes==1 && mid_write_dirty==3)context.objects[5]=other.bytes;
  return !publish_failure;
}
}
int main(int argc,char** argv){
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto code=[&](uintptr_t va,std::initializer_list<uint8_t> bytes){assert(!std::memcmp(exe.data()+va-0x10900C00,bytes.begin(),bytes.size()));};
    code(0x109EAEB2,{0x89,0x87,0x84,0x03,0,0}); // script+384 -> raw BeamData+394
    code(0x109EE797,{0x89,0x87,0x94,0x03,0,0}); // script+394 -> raw GrenadeData+3A4
    code(0x10AB6F57,{0x89,0x87,0x88,0x03,0,0}); // script+388 -> raw GrenadeBagData+398
  }
  for(unsigned i=0;i<4000;++i){Reset(i%5);const auto original=weapon,original_thrower=thrower;Data();
    assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kApplied);
    assert(writes==(kind==3 ? 2u:kind ? 1u:0u));auto expected=original,expected_thrower=original_thrower;
    if(kind==3){void* p=new_data.bytes;std::memcpy(expected_thrower.bytes+0x34,&p,4);}
    if(kind){void* p=new_data.bytes;std::memcpy(expected.bytes+CacheOffset(),&p,4);}
    assert(!std::memcmp(expected.bytes,weapon.bytes,sizeof(weapon.bytes)));
    assert(!std::memcmp(expected_thrower.bytes,thrower.bytes,sizeof(thrower.bytes)));
    const unsigned n=writes;assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kApplied && writes==n);
    context.objects[5]=other.bytes;context.indices[5]=other.index;Data(true);
    assert(NativeRetargetWeaponDataCache(saved,true)==NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(original.bytes,weapon.bytes,sizeof(weapon.bytes)));
    assert(!std::memcmp(original_thrower.bytes,thrower.bytes,sizeof(thrower.bytes)));
  }
  Reset();Data();SetCache(new_data.bytes);assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kApplied && !writes);
  Reset();Data();publish_failure=true;assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && writes==1);
  publish_failure=false;Data(true);assert(NativeRetargetWeaponDataCache(saved,true)==NativePlayerServiceResult::kApplied && GetCache()==old_data.bytes);
  Reset(3);Data();fail_write_number=2;
  assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && writes==1 && GetThrowerCache()==old_data.bytes);
  fail_write_number=0;Data(true);assert(NativeRetargetWeaponDataCache(saved,true)==NativePlayerServiceResult::kApplied && GetCache()==old_data.bytes);
  for(unsigned d=1;d<=3;++d){Reset(3);Data();mid_write_dirty=d;
    assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && writes==1 && GetThrowerCache()==old_data.bytes);}
  Reset(3);Data();timer_count=1;timer_object=thrower.bytes;Reject();
  Reset(3);Data();timer_count=1;timer_object=other.bytes;assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kApplied);
  Reset(3);Data();thrower_outer=other.bytes;Reject();Reset(3);Data();thrower_owner=other.bytes;Reject();
  Reset(3);Data();++thrower.index;Reject();Reset(3);Data();SetThrowerCache(other.bytes);Reject();
  Reset(3);Data();timer_count=257;Reject();Reset(3);Data();timer_count=-1;Reject();
  for(unsigned d=1;d<=7;++d){Reset();Data();dirty=d;assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && !g_data_cache_busy);}
  Reset();Reject();Reset();Data();bad_layout=true;Reject();Reset();Data();bad_type=true;Reject();
  Reset();Data();SetCache(other.bytes);Reject();Reset();Data();SetCache(nullptr);Reject();
  Reset();Data();++old_data.index;Reject();Reset();Data();++new_data.index;Reject();
  Reset();Data();old_data.valid=false;Reject();Reset();Data();new_data.valid=false;Reject();
  Reset();Data();++saved.context.indices[0];Reject();Reset();Data();++saved.cache_offset;Reject();
  Reset();Data();saved.kind=5;Reject();Reset();Data();saved.captured=false;Reject();
  Reset();Data();ambiguous=true;Reject();Reset();Data();g_data_cache_busy=true;Reject();g_data_cache_busy=false;
  Reset();Data();fail_write=true;assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && !writes);
  Reset();Data();DWORD old=0;assert(VirtualProtect(weapon.bytes,4096,PAGE_READONLY,&old));
  assert(NativeRetargetWeaponDataCache(saved,false)==NativePlayerServiceResult::kStateMismatch && !writes);
  assert(VirtualProtect(weapon.bytes,4096,old,&old));
  Reset();Data();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  Reset();InventoryWeaponRole a{},b{};a.data_cache=saved;b=a;b.data_cache.context.interval[3]=2;
  assert(SameInventoryWeaponRoleIdentity(a,b));++b.data_cache.target_index;assert(!SameInventoryWeaponRoleIdentity(a,b));
  std::puts("NativeWeaponDataCacheTests passed: 4000 base/beam/grenade-launcher/grenade-bag/repair round trips, exact cache-only writes, rollback, 7 dirty checks and retained offset instructions (control/GC fixtures, not gameplay)");
}
