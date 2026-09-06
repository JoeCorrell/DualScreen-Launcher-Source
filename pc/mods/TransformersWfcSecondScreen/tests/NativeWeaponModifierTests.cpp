// Production scalar-state preservation; object metadata/control graph are
// fixtures. This does not emulate cloning, engine GC, AI or live weapon events.
#include "NativeWeaponModifiers.cpp"
#include "WeaponRoleJournal.h"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <limits>
using namespace wfcds;
namespace {
struct alignas(4096) Object {
  uint8_t bytes[256]{};
  const char* type="TnHeatModifier";
  uint32_t index=1;
  void* owner=nullptr;
  void* outer=nullptr;
  bool valid=true;
};
Object pawn,weapon,heat,world,lerper,data,controller,other,player_data;
std::array<Object,64> mods;
std::array<Object,64> templates;
std::array<void*,64> slots;
std::array<void*,64> template_slots,cloned_slots;
UnrealArrayView array;
UnrealArrayView template_array;
void* player_role_data=nullptr;
bool cloning_ready=true;
unsigned clone_calls=0,clone_fault=0,seed_fault=0;
NativeWeaponProgress context;
NativeWeaponModifiers saved;
void* weapon_heat=nullptr;
void* heat_owner=nullptr;
unsigned writes=0,fail_write=0,dirty=0;
bool bad_layout=false,control_valid=true;
Object* Lookup(void* p) {
  for(auto* o:{&pawn,&weapon,&heat,&world,&lerper,&data,&controller,&other,&player_data}) if(p==o->bytes && o->valid)return o;
  for(auto& o:mods)if(p==o.bytes && o.valid)return &o;
  for(auto& o:templates)if(p==o.bytes && o.valid)return &o;
  return nullptr;
}
void Put(Object& o,uint32_t off,float f){std::memcpy(o.bytes+off,&f,4);}
float Get(const Object& o,uint32_t off){float f=0;std::memcpy(&f,o.bytes+off,4);return f;}
uint32_t Flags(const Object& o){uint32_t f=0;std::memcpy(&f,o.bytes+0x40,4);return f;}
void Flags(Object& o,uint32_t f){std::memcpy(o.bytes+0x40,&f,4);}
bool FloatOffset(Object* o,const wchar_t* name,uint32_t* off){
  if(!o)return false;
  for(const auto& s:kModifierSchemas)if(!std::strcmp(o->type,s.type)) {
    for(size_t i=0;i<2;++i)if(s.tuning[i] && !std::wcscmp(name,s.tuning[i])){*off=0x30+i*4;return true;}
    if(s.timer && !std::wcscmp(name,s.timer)){*off=0x44;return true;}
  }
  return false;
}
bool FlagMask(Object* o,const wchar_t* name,uint32_t* mask){
  if(!o)return false;
  for(const auto& s:kModifierSchemas)if(!std::strcmp(o->type,s.type))
    for(size_t i=0;i<2;++i)if(s.flags[i] && !std::wcscmp(name,s.flags[i])){*mask=i ? 0x80000000u:4u;return true;}
  return false;
}
bool Capture(NativeWeaponModifiers* out){return NativeCaptureWeaponModifiers(GetCurrentThreadId(),pawn.bytes,weapon.bytes,out);}
void Reset(unsigned count=5){
  uint32_t id=1;
  for(auto* o:{&pawn,&weapon,&heat,&world,&lerper,&data,&controller,&other,&player_data}){*o={};o->index=id++;}
  heat.type="TnWeaponHeat";slots={};g_modifiers_busy=false;
  Put(heat,0x2C,100);
  writes=fail_write=dirty=0;bad_layout=false;control_valid=true;
  weapon_heat=heat.bytes;heat_owner=pawn.bytes;
  for(size_t i=0;i<mods.size();++i){auto& m=mods[i];m={};m.index=id++;m.type=kModifierSchemas[i%5].type;
    m.owner=pawn.bytes;m.outer=heat.bytes;slots[i]=m.bytes;
    Put(m,0x30,4.0f);Put(m,0x34,0.25f);Put(m,0x44,i%5==3 ? -0.125f:3.75f);
    Flags(m,0xB5555555u);m.bytes[0x48]=2;
  }
  for(size_t i=0;i<templates.size();++i){templates[i]=mods[i];templates[i].index=id++;
    templates[i].owner=nullptr;templates[i].outer=player_data.bytes;template_slots[i]=templates[i].bytes;}
  template_array={template_slots.data(),5,64};player_role_data=player_data.bytes;Put(player_data,0x2C,100);
  cloning_ready=true;clone_calls=clone_fault=seed_fault=0;cloned_slots={};
  array={slots.data(),static_cast<int32_t>(count),64};context={};
  context.objects={pawn.bytes,weapon.bytes,lerper.bytes,world.bytes,data.bytes,controller.bytes,pawn.bytes};
  for(size_t i=0;i<context.objects.size();++i)context.indices[i]=Lookup(context.objects[i])->index;
  context.captured=true;context.thread=GetCurrentThreadId();context.interval={1,2,4,1};
  assert(Capture(&saved));
}
void ResetEvents(){
  for(int i=0;i<array.count;++i){Flags(mods[i],Flags(mods[i])&~0x80000004u);Put(mods[i],0x44,0);}
}
void Reject(){const auto before=mods;const unsigned w=writes;
  assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kRejected);
  assert(!std::memcmp(before.data(),mods.data(),sizeof(mods)) && writes==w);
}
DWORD WINAPI WrongThread(void*){Reject();return 0;}
void Dirty(){
  if(!g_modifiers_busy || !dirty)return;
  assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kRejected);
  NativeWeaponModifiers rejected;assert(!Capture(&rejected) && !rejected.captured);
  const unsigned d=dirty;dirty=0;
  if(d==1)++mods[0].index;
  if(d==2)mods[0].outer=other.bytes;
  if(d==3)context.objects[5]=other.bytes;
  if(d==4)++heat.index;
  if(d==5)slots[0]=mods[1].bytes;
  if(d==6)Put(mods[0],0x30,99);
  if(d==7)control_valid=false;
  if(d==8)context.objects[4]=other.bytes;
}
}
namespace wfcds {
bool NativeCaptureWeaponProgress(DWORD thread,void* p,void* w,NativeWeaponProgress* out){
  *out={};if(!thread || thread!=GetCurrentThreadId() || p!=pawn.bytes || w!=weapon.bytes)return false;
  Dirty();if(!control_valid)return false;*out=context;return true;
}
bool UnrealObjectIsA(void* p,const wchar_t* type){auto* o=Lookup(p);if(!o)return false;
  if(p==heat.bytes)return !std::wcscmp(type,L"TnWeaponHeat");
  if(p==player_data.bytes)return !std::wcscmp(type,L"TnWeaponData");
  for(auto& m:mods)if(p==m.bytes)return !std::wcscmp(type,L"TnHeatModifier");
  for(auto& m:templates)if(p==m.bytes)return !std::wcscmp(type,L"TnHeatModifier");
  return false;
}
bool UnrealObjectValid(void* p){return Lookup(p)!=nullptr;}
uint32_t UnrealObjectIndex(void* p){auto* o=Lookup(p);return o ? o->index:0;}
void* UnrealObjectOuter(void* p){auto* o=Lookup(p);return o ? o->outer:nullptr;}
bool UnrealObjectClassName(void* p,char* out,size_t n){auto* o=Lookup(p);if(!o)return false;std::snprintf(out,n,"%s",o->type);return true;}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out){auto* o=Lookup(p);if(!o)return false;
  if(p==weapon.bytes && !std::wcscmp(name,L"WeaponHeat"))*out=weapon_heat;
  else if(p==weapon.bytes && !std::wcscmp(name,L"PlayerData"))*out=player_role_data;
  else if(p==heat.bytes && !std::wcscmp(name,L"OwnerPawn"))*out=heat_owner;
  else if(!std::wcscmp(name,L"WeaponOwner"))*out=o->owner;
  else return false;
  return true;
}
bool UnrealReadStructProperty(void* p,const wchar_t* name,UnrealStructValueView* out){
  if((p!=heat.bytes && p!=player_data.bytes) || std::wcscmp(name,L"HeatProperties"))return false;
  *out={static_cast<uint8_t*>(p)+0x2C,16,Lookup(p)};return true;
}
bool UnrealReadStructFloatProperty(void* p,const wchar_t* name,const wchar_t* field,float* out){
  if((p!=heat.bytes && p!=player_data.bytes) || std::wcscmp(name,L"HeatProperties") || std::wcscmp(field,L"HeatMax"))return false;
  *out=Get(*Lookup(p),0x2C);return true;
}
bool UnrealReadStructTypedArray(const UnrealStructValueView& view,const wchar_t* name,const wchar_t* type,uint32_t size,UnrealArrayView* out,uint32_t max){
  assert((view.data==heat.bytes+0x2C || view.data==player_data.bytes+0x2C) && !std::wcscmp(name,L"Modifiers") && !std::wcscmp(type,L"ObjectProperty") && size==4 && max==64);
  *out=view.data==heat.bytes+0x2C ? array:template_array;return !bad_layout;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out){if(i<0 || i>=a.count)return false;*out=static_cast<void**>(a.data)[i];return true;}
bool UnrealResolveFloatPropertyOffset(void* p,const wchar_t* name,uint32_t* off){return FloatOffset(Lookup(p),name,off);}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out){uint32_t off=0;auto* o=Lookup(p);if(!FloatOffset(o,name,&off))return false;*out=Get(*o,off);return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out){auto* o=Lookup(p);if(!o || std::wcscmp(name,L"EventTypeToAddOn"))return false;*out=o->bytes[0x48];return true;}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out){auto* o=Lookup(p);uint32_t mask=0;if(!FlagMask(o,name,&mask))return false;*out=(Flags(*o)&mask)!=0;return true;}
bool UnrealWriteBoolPropertyExpected(void* p,const wchar_t* name,bool expected,bool value){
  auto* o=Lookup(p);uint32_t mask=0;
  if(!FlagMask(o,name,&mask) || !ModifierMemory(o->bytes+0x40,4,true) || ((Flags(*o)&mask)!=0)!=expected ||
      (fail_write && writes+1==fail_write))return false;
  Flags(*o,value ? Flags(*o)|mask:Flags(*o)&~mask);++writes;return true;
}
}
#include "NativeWeaponModifierInitializationTests.inc"
int main(){
  for(unsigned i=0;i<3000;++i){Reset(i%2 ? 5:64);const auto original=mods;ResetEvents();const auto before=mods;
    assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied);
    auto expected=before;
    for(int j=0;j<array.count;++j){const auto& s=kModifierSchemas[j%5];
      if(s.flags[0])Flags(expected[j],Flags(original[j]));
      if(s.timer)Put(expected[j],0x44,Get(mods[j],0x44));
      if(s.timer)assert(Get(mods[j],0x44)==Get(original[j],0x44));
    }
    assert(!std::memcmp(expected.data(),mods.data(),sizeof(mods)));
    const unsigned count=writes;assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied && writes==count);
    // Controller and tuning-data replacement are legitimate between transaction
    // stages, but cannot happen during the scalar restoration itself.
    context.objects[5]=other.bytes;context.indices[5]=other.index;
    context.objects[4]=other.bytes;context.indices[4]=other.index;
    ResetEvents();assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied);
  }
  Reset(0);assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied && writes==0);
  Reset();heat_owner=nullptr;for(auto& m:mods)m.owner=nullptr;assert(Capture(&saved));ResetEvents();assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied);
  for(unsigned d=1;d<=8;++d){Reset();ResetEvents();dirty=d;assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kStateMismatch && !g_modifiers_busy);}
  Reset();ResetEvents();fail_write=1;Reject();
  Reset();ResetEvents();fail_write=2;assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kStateMismatch && writes==1);
  Reset();ResetEvents();++mods[0].index;Reject();Reset();ResetEvents();mods[0].valid=false;Reject();
  Reset();ResetEvents();slots[0]=mods[1].bytes;Reject();Reset();ResetEvents();std::swap(slots[0],slots[1]);Reject();
  Reset();ResetEvents();mods[0].outer=other.bytes;Reject();Reset();ResetEvents();mods[0].owner=other.bytes;Reject();
  Reset();ResetEvents();mods[0].type="TnCustomHeatModifier";Reject();Reset();ResetEvents();mods[0].type="TnHeatModifier";Reject();
  Reset();ResetEvents();Put(mods[0],0x30,8);Reject();Reset();ResetEvents();mods[4].bytes[0x48]=9;Reject();
  Reset();mods[4].bytes[0x48]=8;assert(Capture(&saved));ResetEvents();assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied);
  Reset();ResetEvents();Put(mods[2],0x44,-0.1f);Reject();Reset();ResetEvents();Put(mods[3],0x44,std::numeric_limits<float>::infinity());Reject();
  Reset();ResetEvents();saved.modifiers[2].timer=std::numeric_limits<float>::quiet_NaN();Reject();
  Reset();ResetEvents();weapon_heat=other.bytes;Reject();Reset();ResetEvents();++heat.index;Reject();
  Reset();ResetEvents();Put(heat,0x2C,50);Reject();Reset();ResetEvents();Put(heat,0x2C,-1);Reject();
  Reset(0);Put(heat,0x2C,0);assert(Capture(&saved));assert(NativeRestoreWeaponModifiers(saved)==NativePlayerServiceResult::kApplied);
  Reset();ResetEvents();bad_layout=true;Reject();Reset();ResetEvents();array.count=65;Reject();
  Reset();ResetEvents();array.count=-1;Reject();Reset();ResetEvents();array.capacity=4;Reject();
  Reset();ResetEvents();array.capacity=65;Reject();Reset();ResetEvents();array.data=nullptr;Reject();
  Reset();ResetEvents();g_modifiers_busy=true;Reject();g_modifiers_busy=false;
  Reset();ResetEvents();HANDLE t=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(t && WaitForSingleObject(t,10000)==WAIT_OBJECT_0);CloseHandle(t);
  Reset();ResetEvents();DWORD protection=0;assert(VirtualProtect(mods[2].bytes,4096,PAGE_READONLY,&protection));Reject();assert(VirtualProtect(mods[2].bytes,4096,protection,&protection));
  Reset();InventoryWeaponRole a{},b{};a.modifiers=saved;b=a;b.modifiers.modifiers[2].timer=50;
  assert(SameInventoryWeaponRoleIdentity(a,b));++b.modifiers.modifiers[2].index;assert(!SameInventoryWeaponRoleIdentity(a,b));
  TestModifierInitialization();
  std::puts("NativeWeaponModifierTests passed: 3000 timer/flag restorations plus 2000 capacity/clone commit/rollback batches, seed-only and partial-undo recovery, 5 concrete classes and dirty ownership/state rejection (metadata/control/clone/capacity fixtures, not gameplay)");
}
