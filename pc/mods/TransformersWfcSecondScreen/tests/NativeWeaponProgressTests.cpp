// Production progress port; UObject metadata and SetData are fixtures.
#include "NativeWeaponProgress.cpp"
#include "NativeWeaponRoleData.cpp"
#include "NativeWeaponAmmo.cpp"
#include "WeaponRoleJournal.h"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <limits>
#include <vector>
using namespace wfcds;
namespace {
struct alignas(4096) Object { uint8_t bytes[0x800]{};const wchar_t* type;uint32_t index=1;bool valid=true,dead=false; };
Object pawn{{},L"TnPawn"},weapon{{},L"TnWeapon"},lerper{{},L"HmValueLerper"},other_lerper{{},L"HmValueLerper"},
    pc{{},L"TnPlayerController"},ai{{},L"TnAiController"},world{{},L"WorldInfo"},
    old_data{{},L"TnWeaponData"},new_data{{},L"TnWeaponData"},
    base_skill{{},L"Class"},grenade_skill{{},L"Class"},clips_skill{{},L"Class"},unknown_skill{{},L"Class"};
Object* objects[]={&pawn,&weapon,&lerper,&other_lerper,&pc,&ai,&world,&old_data,&new_data,&base_skill,&grenade_skill,&clips_skill,&unknown_skill};
std::array<void*,64> ammo_skill_slots{};
UnrealArrayView ammo_skills{ammo_skill_slots.data(),0,64};
bool ammo_affected=false;unsigned ammo_fault=0,ammo_writes=0;
bool firing=false,bad_offset=false,bad_tuning_layout=false;unsigned dirty=0;
NativeWeaponProgress saved;
Object* Lookup(void* p) { for(auto* o:objects)if(o->valid && p==o->bytes)return o;return nullptr; }
void Link(Object& o,uint32_t off,Object* other){void* p=other ? other->bytes:nullptr;std::memcpy(o.bytes+off,&p,4);}
void Float(Object& o,uint32_t off,float f){std::memcpy(o.bytes+off,&f,4);}
float Value(Object& o,uint32_t off){float f=0;std::memcpy(&f,o.bytes+off,4);return f;}
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p);if(!o)return false;
  const auto f=[&](const wchar_t* n,uint32_t off){if(std::wcscmp(name,n))return false;*out=off;return true;};
  if(f(L"WorldInfo",0x98))return true;
  if(o==&pawn)return f(L"Controller",0x1D4);
  if(o==&pc || o==&ai)return f(L"Pawn",0x1C8)||f(L"AcknowledgedPawn",0x300);
  if(o==&weapon)return f(L"Owner",0x80)||f(L"Instigator",0x94)||f(L"FireIntervalLerper",0x2EC)||f(L"Data",0x240)||f(L"PlayerData",0x244)||f(L"AiData",0x248)||f(L"IncreaseAmmoSkill",0x500);
  return false;
}
void Reset(bool player=true,float ramp=10,float elapsed=3.5f) {
  uint32_t id=1;for(auto* o:objects){std::memset(o->bytes,0,sizeof(o->bytes));o->index=id++;o->valid=true;o->dead=false;}
  lerper.type=L"HmValueLerper";firing=bad_offset=bad_tuning_layout=false;dirty=0;g_weapon_progress_busy=false;saved={};
  for(auto* o:{&pawn,&weapon,&pc,&ai}){Link(*o,0x98,&world);o->bytes[0x66]=3;}
  Link(pawn,0x1D4,player ? &pc:&ai);Link(pc,0x1C8,&pawn);Link(ai,0x1C8,&pawn);Link(pc,0x300,&pawn);
  Link(weapon,0x80,&pawn);Link(weapon,0x94,&pawn);Link(weapon,0x2EC,&lerper);Link(weapon,0x240,&old_data);
  Float(weapon,0x300,2.75f);Float(lerper,0x28,1.25f);Float(lerper,0x2C,0.125f);Float(lerper,0x30,ramp);Float(lerper,0x34,elapsed);
  assert(NativeCaptureWeaponProgress(GetCurrentThreadId(),pawn.bytes,weapon.bytes,&saved));
}
void Retune(float duration=4,bool initialize=true) {
  Link(weapon,0x240,&new_data);
  Float(new_data,0x11C,2);Float(new_data,0x120,0.25f);Float(new_data,0x124,duration);
  if(initialize){Float(weapon,0x300,0.125f);
    Float(lerper,0x28,2);Float(lerper,0x2C,0.25f);Float(lerper,0x30,duration);Float(lerper,0x34,0);}
}
NativePlayerServiceResult Apply(bool rollback=false){return NativeRestoreWeaponProgress(saved,rollback ? old_data.bytes:new_data.bytes,rollback);}
void Reject(){const auto w=weapon,l=lerper;assert(Apply()==NativePlayerServiceResult::kRejected);
  assert(!std::memcmp(w.bytes,weapon.bytes,sizeof(w.bytes)) && !std::memcmp(l.bytes,lerper.bytes,sizeof(l.bytes)));}
DWORD WINAPI WrongThread(void*) {Reject();return 0;}
void Dirty() {
  if(!g_weapon_progress_busy || !dirty)return;
  const unsigned d=dirty;dirty=0;assert(Apply()==NativePlayerServiceResult::kRejected);
  if(d==1)++pawn.index;
  if(d==2)++weapon.index;
  if(d==3)++lerper.index;
  if(d==4)Link(pawn,0x1D4,&ai);
  if(d==5)Link(weapon,0x94,nullptr);
  if(d==6)Link(weapon,0x240,&old_data);
  if(d==7)world.bytes[0x39C]=3;
  if(d==8)Float(new_data,0x11C,99);
}
}
namespace wfcds {
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* name,uint32_t size,uint32_t* output){
  if(!Lookup(p) || std::wcscmp(name,L"FireIntervalModifier") || size!=12)return false;
  *output=bad_tuning_layout ? 0x118:0x11C;return true;
}
bool UnrealReadStructProperty(void* p,const wchar_t* name,UnrealStructValueView* out){
  if(p==pawn.bytes && !std::wcscmp(name,L"_CurrentCharacter")){*out={pawn.bytes+0x400,12,&pawn};return true;}
  if(!Lookup(p) || std::wcscmp(name,L"FireIntervalModifier"))return false;
  *out={static_cast<uint8_t*>(p)+0x11C,12,Lookup(p)};return true;
}
bool UnrealReadStructStructField(const UnrealStructValueView& v,const wchar_t* name,UnrealStructValueView* out){
  if(!v.data || v.size!=12 || std::wcscmp(name,L"IntervalRange"))return false;
  *out={v.data,8,v.struct_definition};return true;
}
bool UnrealReadStructFloatField(const UnrealStructValueView& v,const wchar_t* name,float* out){
  uint32_t off=0;
  if(v.size==8 && !std::wcscmp(name,L"Min"))off=0;
  else if(v.size==8 && !std::wcscmp(name,L"Max"))off=4;
  else if(v.size==12 && !std::wcscmp(name,L"RampTime"))off=8;
  else return false;
  std::memcpy(out,static_cast<const uint8_t*>(v.data)+off,4);return true;
}
bool UnrealObjectIsA(void* p,const wchar_t* name){auto* o=Lookup(p);return o && !std::wcscmp(o->type,name);}
uint32_t UnrealObjectIndex(void* p){auto* o=Lookup(p);return o ? o->index:0;}
bool UnrealObjectClassName(void* p,char* out,size_t size){auto* o=Lookup(p);if(!o)return false;std::snprintf(out,size,"%ls",o->type);return true;}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out){auto* o=Lookup(p);if(!o)return false;
  if(o==&weapon && !std::wcscmp(name,L"IsEffectedByIncreaseAmmoSkill")){*out=ammo_affected;return true;}
  if(!std::wcscmp(name,L"bDeleteMe"))*out=o->dead;else if(o==&weapon && !std::wcscmp(name,L"bIsFiring"))*out=firing;else return false;return true;}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out){if(!Lookup(p))return false;
  if(p==weapon.bytes && !std::wcscmp(name,L"WeaponDataType")){*out=weapon.bytes[0x329];return true;}
  if(!std::wcscmp(name,L"Role"))*out=static_cast<uint8_t*>(p)[0x66];else if(!std::wcscmp(name,L"NetMode"))*out=static_cast<uint8_t*>(p)[0x39C];else return false;return true;}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out){
  if(p==weapon.bytes && !std::wcscmp(name,L"Owner"))Dirty();
  uint32_t off=0;if(!Offset(p,name,&off))return false;std::memcpy(out,static_cast<uint8_t*>(p)+off,4);return true;}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out){if(!Offset(p,name,out))return false;if(bad_offset)++*out;return true;}
bool UnrealResolveFloatPropertyOffset(void* p,const wchar_t* name,uint32_t* out){auto* o=Lookup(p);if(!o)return false;
  if(o==&weapon && !std::wcscmp(name,L"CurrentSpread")){*out=0x300;return true;}
  if(o==&lerper || o==&other_lerper)for(size_t i=0;i<4;++i)if(!std::wcscmp(name,kProgressFields[i])){*out=0x28+i*4;return true;}
  return false;}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out){uint32_t off=0;if(!UnrealResolveFloatPropertyOffset(p,name,&off))return false;std::memcpy(out,static_cast<uint8_t*>(p)+off,4);return true;}
}
#include "NativeWeaponRoleDataTests.inc"
#include "NativeWeaponAmmoTests.inc"
int main(int argc,char** argv) {
  if(argc>1){std::ifstream f(argv[1],std::ios::binary);std::vector<uint8_t> exe{std::istreambuf_iterator<char>(f),{}};assert(exe.size()==37667328);
    const auto code=[&](uintptr_t va,std::initializer_list<uint8_t> bytes){assert(!std::memcmp(exe.data()+va-0x10900C00,bytes.begin(),bytes.size()));};
    code(0x109E72FB,{0x8B,0x87,0xDC,0x02,0x00,0x00}); // Existing lerper +2EC raw.
    code(0x109E730F,{0xD9,0x58,0x28}); // StartValue +28 raw.
    code(0x109E7315,{0xF3,0x0F,0x11,0x40,0x1C}); // EndValue +2C raw.
    code(0x109E731D,{0xF3,0x0F,0x11,0x40,0x24}); // CurTime +34 raw.
    code(0x109E7330,{0xF3,0x0F,0x10,0x49,0x30}); // RampTime +30 raw.
    code(0x109E7301,{0xD9,0x81,0x0C,0x01,0x00,0x00}); // Data interval Min +11C raw.
    code(0x109E7307,{0xF3,0x0F,0x10,0x81,0x10,0x01,0x00,0x00}); // Max +120.
    code(0x109E7322,{0xF3,0x0F,0x10,0x91,0x14,0x01,0x00,0x00}); // RampTime +124.
    code(0x109E6554,{0xF3,0x0F,0x11,0x83,0xF0,0x02,0x00,0x00}); // Spread +300 raw.
  }
  for(unsigned i=0;i<3000;++i){const float fraction=static_cast<float>(i%101)/100;
    Reset((i&1)!=0,8,fraction*8);const auto original=saved;Retune((i%3)==0 ? 0:4,(i&2)!=0);const auto w=weapon,p=pawn;
    assert(Apply()==NativePlayerServiceResult::kApplied && Value(weapon,0x300)==original.spread);
    assert(Value(lerper,0x34)==Value(lerper,0x30)*fraction && Value(lerper,0x28)==2 && Value(lerper,0x2C)==0.25f);
    // The role boundary restores both before and after heat callbacks: those
    // callbacks may reset transient state, but must not erase saved progress.
    if(i&4){Float(lerper,0x34,0);Float(weapon,0x300,0);}
    assert(Apply()==NativePlayerServiceResult::kApplied);
    assert(Value(lerper,0x34)==Value(lerper,0x30)*fraction);
    auto expected=w;Float(expected,0x300,original.spread);assert(!std::memcmp(expected.bytes,weapon.bytes,sizeof(expected.bytes)));
    assert(!std::memcmp(p.bytes,pawn.bytes,sizeof(p.bytes)));
    Link(pawn,0x1D4,(i&1) ? &ai:&pc); // Graph rollback legitimately changes controller.
    Link(weapon,0x240,&old_data);Float(lerper,0x28,99);Float(lerper,0x2C,88);Float(lerper,0x30,4);Float(lerper,0x34,0);
    assert(Apply(true)==NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(lerper.bytes+0x28,original.interval.data(),16));
  }
  Reset(true,0,0);Retune();assert(Apply()==NativePlayerServiceResult::kApplied && Value(lerper,0x34)==0);
  Reset();Link(weapon,0x94,nullptr);assert(NativeCaptureWeaponProgress(GetCurrentThreadId(),pawn.bytes,weapon.bytes,&saved));Retune();assert(Apply()==NativePlayerServiceResult::kApplied);
  for(unsigned d=1;d<=8;++d){Reset();Retune();dirty=d;assert(Apply()==NativePlayerServiceResult::kStateMismatch && !g_weapon_progress_busy);}
  Reset();Retune(4,false);bad_tuning_layout=true;Reject();
  for(auto off:{0x11Cu,0x120u,0x124u}){
    Reset();Retune(4,false);Float(new_data,off,-1);Reject();
    Reset();Retune(4,false);Float(new_data,off,std::numeric_limits<float>::quiet_NaN());Reject();
    Reset();Retune(4,false);Float(new_data,off,std::numeric_limits<float>::infinity());Reject();
  }
  Reset();Retune(4,false);DWORD source_protection=0;
  assert(VirtualProtect(new_data.bytes,4096,PAGE_READONLY,&source_protection));
  assert(Apply()==NativePlayerServiceResult::kApplied);
  assert(VirtualProtect(new_data.bytes,4096,source_protection,&source_protection));
  Reset();Retune();bad_offset=true;Reject();Reset();Retune();firing=true;Reject();Reset();Retune();Link(weapon,0x2EC,&other_lerper);Reject();
  Reset();Retune();Link(pc,0x300,nullptr);Reject();Reset();Retune();world.bytes[0x39C]=3;Reject();Reset();Retune();saved.captured=false;Reject();
  Reset();Retune();Float(lerper,0x34,5);Reject();Reset();Retune();Float(lerper,0x30,-1);Reject();
  Reset();Retune();saved.interval[2]=std::numeric_limits<float>::quiet_NaN();Reject();Reset();Retune();saved.spread=-1;Reject();
  Reset();Retune();++old_data.index;Reject();Reset();Retune();++weapon.index;Reject();
  Reset();Retune();DWORD protection=0;assert(VirtualProtect(lerper.bytes,4096,PAGE_READONLY,&protection));Reject();assert(VirtualProtect(lerper.bytes,4096,protection,&protection));
  Reset();Retune();g_weapon_progress_busy=true;Reject();g_weapon_progress_busy=false;
  Reset();Retune();HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);assert(thread);assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
  Reset();InventoryWeaponRole a{},b{};a.progress=saved;b=a;b.progress.interval[3]=1;
  assert(SameInventoryWeaponRoleIdentity(a,b));++b.progress.indices[2];assert(!SameInventoryWeaponRoleIdentity(a,b));
  TestRoleData(argc>1 ? argv[1]:nullptr);
  TestWeaponAmmo();
  std::puts("NativeWeaponProgressTests passed: 3000 direct authored-tuning/rollback cycles with and without old UpdateData reset; fraction/spread preservation, typed/finite/read-only source checks and 8 dirty guards (metadata/control fixtures, not gameplay)");
}
