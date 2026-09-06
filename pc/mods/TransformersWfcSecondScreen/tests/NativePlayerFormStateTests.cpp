// Shipping GotoState, campaign forwarding thunks and native entry/exit primitives
// execute with the production selective BeginState hooks. UObject state dispatch,
// deep camera/input/weapon/physics callbacks are fixtures, NOT gameplay.
#include "NativePlayerFormState.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <limits>
#include <vector>

using namespace wfcds;
namespace {
struct Object {
  alignas(16) uint8_t bytes[0x800]{};
  const wchar_t* type; const char* name="fixture";
  bool valid=true,deleting=false; uint32_t index=1;
};
Object player{{},L"TnPlayerController"}, pawn{{},L"TnPawn"}, buddy{{},L"TnPawn"},
    world{{},L"WorldInfo"}, foreign{{},L"WorldInfo"}, cls{{},L"Class"},
    state{{},L"State"}, robot{{},L"TnRobotForm"}, car{{},L"TnCarForm"},
    plane{{},L"TnPlaneForm"}, tank{{},L"TnTankForm"}, input{{},L"Object"},
    aim{{},L"TnFineAimManager"}, inventory{{},L"TnInventoryManager"}, weapon{{},L"TnWeapon"},
    player_input{{},L"PlayerInput"},profile{{},L"TnProfileSettings"},pri{{},L"TnPlayerReplicationInfo"},
    tracker{{},L"TnClassFormStatHelper"},camera_manager{{},L"TnCameraStrategyManager"};
Object* objects[]={&player,&pawn,&buddy,&world,&foreign,&cls,&state,&robot,&car,&plane,&tank,&input,&aim,&inventory,&weapon,
    &player_input,&profile,&pri,&tracker,&camera_manager};
uint8_t* image;
alignas(16) uint8_t frame[0x54]{}, engine[0x50]{}, context[0x30]{}, slot[4]{}, stack[12]{};
NativePlayerInputLease move_lease,look_lease;
unsigned mode=0,exits=0,aim_calls=0,fire_calls=0,clear_calls=0,remote_calls=0,timer_calls=0,move_calls=0;
bool bad_offset=false,fail_state=false,lose_pair=false,change_index=false,lose_guard=false,change_form=false;
bool references=true,reject_alias=false,reject_input=false,wrong_array=false,fail_camera=false,entry_phase=false;
bool stats_lose_guard=false,bad_delegate_offset=false;
unsigned entries=0,camera_calls=0,stats_calls=0,physics_calls=0;
unsigned block_calls=0,melee_calls=0,boost_stop_calls=0;
bool clearing_inputs=false,clear_dirty=false;
alignas(16) uint8_t mappings[60*4]{},settings[36*4]{};
const wchar_t class_name[]=L"Soldier";
Object* Lookup(void* p) { for (auto* o:objects) if (o->valid && p==o->bytes) return o; return nullptr; }
uint32_t Read(const void* p,size_t offset) { uint32_t v=0; std::memcpy(&v,static_cast<const uint8_t*>(p)+offset,4); return v; }
void Put(void* p,size_t offset,uintptr_t v) { const uint32_t x=static_cast<uint32_t>(v); std::memcpy(static_cast<uint8_t*>(p)+offset,&x,4); }
void Link(Object& o,size_t offset,Object* v) { Put(o.bytes,offset,reinterpret_cast<uintptr_t>(v ? v->bytes:nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,void* target) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(At(va)+5)); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if (!o) return false;
  const auto match=[&](const wchar_t* field,uint32_t offset) { if (std::wcscmp(field,name)) return false; *out=offset; return true; };
  if (match(L"WorldInfo",0x98)) return true;
  if (o==&player) return match(L"Pawn",0x1C8) || match(L"AcknowledgedPawn",0x300) ||
      match(L"PlayerInput",0x428) || match(L"ProfileSettings",0x560) || match(L"PlayerReplicationInfo",0x1CC) ||
      match(L"_CameraStrategyManager",0x60C) ||
      match(L"_FineAimManager",0x620) || match(L"CarForm",0x66C) || match(L"PlaneForm",0x670) || match(L"TankForm",0x668);
  if (o==&pawn || o==&buddy) return match(L"Controller",0x1D4) || match(L"InvManager",0x424) ||
      match(L"Weapon",0x428) || match(L"_RobotForm",0x57C) || match(L"_VehicleForm",0x580) ||
      match(L"_CurrentForm",0x584) || match(L"_CurrentTransformation",0x588);
  if (o==&aim) return match(L"Owner",0x34);
  if (o==&pri) return match(L"Owner",0x2C) || match(L"_ClassFormTracker",0x4AC);
  if (o==&camera_manager) return match(L"_TransformerPawn",0x34);
  if (o==&inventory || o==&weapon) return match(L"Instigator",0x94);
  if (o==&car || o==&plane || o==&tank) return match(L"_InputState",o==&tank ? 0x104:0x108);
  return false;
}
NativePlayerFormResult Leave() { return NativeLeavePlayerForm(player.bytes,pawn.bytes,move_lease,look_lease); }
void CheckOldPair() {
  assert(Read(player.bytes,0x1C8)==reinterpret_cast<uintptr_t>(pawn.bytes));
  assert(Read(player.bytes,0x300)==reinterpret_cast<uintptr_t>(pawn.bytes));
  assert(Read(pawn.bytes,0x1D4)==reinterpret_cast<uintptr_t>(player.bytes));
  assert(NativePlayerInputHeld(move_lease) && NativePlayerInputHeld(look_lease));
  assert(Leave()==NativePlayerFormResult::kRejected); // Nested requests cannot run.
}
void __attribute__((fastcall)) StopAim(void* manager,void*) {
  assert(manager==aim.bytes+0x10); assert(Read(aim.bytes,0x34)==reinterpret_cast<uintptr_t>(pawn.bytes));
  CheckOldPair(); ++aim_calls;
}
void __attribute__((fastcall)) StopFire(void* gun,void*,uint32_t fire_mode) {
  assert(gun==weapon.bytes+0x10 && fire_mode==0); CheckOldPair(); ++fire_calls;
  if (clearing_inputs && clear_dirty) ++pawn.index;
}
void __attribute__((fastcall)) StopBlock(void* controller,void*) {
  assert(controller==player.bytes+0x10); CheckOldPair(); ++block_calls;
}
void __attribute__((fastcall)) AbortMelee(void* controller,void*) {
  assert(controller==player.bytes+0x10); CheckOldPair(); ++melee_calls;
}
void __attribute__((fastcall)) StopBoostInput(void* form,void*,uint32_t value) {
  assert(!value && form==static_cast<uint8_t*>(reinterpret_cast<void*>(Read(pawn.bytes,0x584)))+0x10);
  CheckOldPair(); ++boost_stop_calls;
}
void __attribute__((fastcall)) ClearInputs(void* controller,void*) {
  assert(controller==player.bytes+0x10 && mode); CheckOldPair(); ++clear_calls;
  const auto& spec=kPlayerForms[mode];
  assert(Read(player.bytes,spec.cache_offset)==Read(pawn.bytes,0x584));
}
void __attribute__((regparm(1))) Remote(void* controller) {
  assert(controller==player.bytes+0x10 && mode); CheckOldPair(); ++remote_calls;
}
void __attribute__((fastcall)) Timer(void* controller,void*,uint32_t name) {
  assert(controller==player.bytes && name==501); CheckOldPair(); ++timer_calls;
}
void __attribute__((fastcall)) SavedMoves(void*,void* controller) {
  assert(controller==player.bytes+0x10); CheckOldPair(); ++move_calls;
}
uint32_t __attribute__((fastcall)) Goto(void* controller,void*,uint32_t name,uint32_t force,uint32_t keep) {
  assert(controller==player.bytes && force==0 && keep==0); CheckOldPair();
  if (name) {
    assert(name>=101 && name<=104 && entry_phase); ++entries;
    if (fail_state) return 0;
    mode=name-101; state.name=kPlayerForms[mode].state; Put(state.bytes,4,name);
    Put(frame,0x38,reinterpret_cast<uintptr_t>(state.bytes));
    Put(player.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C2000+0x500*mode)));
    using Begin=void (__attribute__((fastcall)) *)(void*,void*,uint32_t);
    reinterpret_cast<Begin>(Read(At(0x129C2000+0x500*mode),0x38))(player.bytes+0x10,nullptr,0);
    return 1;
  }
  ++exits;
  if (fail_state) return 0;
  {
    using End=void (__attribute__((fastcall)) *)(void*,void*,uint32_t);
    reinterpret_cast<End>(Read(At(0x129C2000+0x500*mode),0x3C))(player.bytes+0x10,nullptr,0);
  }
  Put(frame,0x38,reinterpret_cast<uintptr_t>(cls.bytes));
  Put(player.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C1000)));
  if (lose_pair) Link(player,0x1C8,&buddy);
  if (change_index) ++pawn.index;
  if (lose_guard) look_lease.held=false;
  if (change_form) Link(pawn,0x584,&buddy);
  return 1;
}
void __attribute__((fastcall)) Label(void* controller,void*,uint32_t name) { assert(controller==player.bytes && name==0x190000); }
void Reset(unsigned form=0) {
  uint32_t index=1;
  for (auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->valid=true; o->deleting=false; o->index=index++; }
  std::memset(frame,0,sizeof(frame)); mode=form;
  Object* forms[]={&robot,&car,&plane,&tank};
  for (auto* o:{&player,&pawn}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Link(player,0x1C8,&pawn); Link(player,0x300,&pawn); Link(pawn,0x1D4,&player);
  Link(player,0x620,&aim); Link(aim,0x34,&pawn);
  Link(pawn,0x424,&inventory); Link(inventory,0x94,&pawn); Link(pawn,0x428,&weapon); Link(weapon,0x94,&pawn); weapon.bytes[0x66]=3;
  Link(pawn,0x57C,&robot); Link(pawn,0x580,form ? forms[form]:&car); Link(pawn,0x584,forms[form]);
  for (auto* o:{&car,&plane,&tank}) Link(*o,o==&tank ? 0x104:0x108,&input);
  if (form) Link(player,kPlayerForms[form].cache_offset,forms[form]);
  state.name=kPlayerForms[form].state; Put(state.bytes,4,101+form);
  Put(player.bytes,0,reinterpret_cast<uintptr_t>(At(0x129C0000)));
  Put(player.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C2000+0x500*form)));
  Put(weapon.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C4000)));
  Put(player.bytes,0x14,1); Put(At(0x12A29FD4),4,reinterpret_cast<uintptr_t>(cls.bytes));
  Put(player.bytes,0x28,reinterpret_cast<uintptr_t>(frame)); Put(frame,0x38,reinterpret_cast<uintptr_t>(state.bytes));
  Put(player.bytes,0x61C,0xA5A5A5A5);
  for (size_t i=0xD8;i<0x108;++i) pawn.bytes[i]=static_cast<uint8_t>(i);
  Put(At(0x12B10A9C),0,reinterpret_cast<uintptr_t>(engine)); Put(engine,0x40,reinterpret_cast<uintptr_t>(context));
  Put(context,0x28,reinterpret_cast<uintptr_t>(slot)); Put(slot,0,reinterpret_cast<uintptr_t>(world.bytes));
  Put(At(0x12B3056C),0,501);
  move_lease={}; look_lease={}; move_lease.controller=look_lease.controller=player.bytes;
  move_lease.held=look_lease.held=true; look_lease.axis=NativeInputAxis::kLook;
  bad_offset=fail_state=lose_pair=change_index=lose_guard=change_form=false;
  references=true; reject_alias=reject_input=wrong_array=fail_camera=stats_lose_guard=bad_delegate_offset=false;
  for (unsigned i=0;i<4;++i) Put(At(kFormNames[i]),0,101+i);
  Link(player,0x428,&player_input); Link(player,0x560,&profile); Link(player,0x1CC,&pri);
  Link(player,0x60C,&camera_manager); Link(pri,0x2C,&player); Link(pri,0x4AC,&tracker);
  Link(camera_manager,0x34,&pawn); pawn.bytes[0x64]=1;
  Put(camera_manager.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C5000)));
  Put(pawn.bytes,0,reinterpret_cast<uintptr_t>(At(0x129C5100)));
  Put(player_input.bytes,0xD4,0xA5A5A5A5);
  Put(profile.bytes,0x60,reinterpret_cast<uintptr_t>(mappings)); Put(profile.bytes,0x64,4); Put(profile.bytes,0x68,4);
  Put(profile.bytes,0x38,reinterpret_cast<uintptr_t>(settings)); Put(profile.bytes,0x3C,4); Put(profile.bytes,0x40,4);
  std::memset(mappings,0,sizeof(mappings)); std::memset(settings,0,sizeof(settings));
  for (unsigned i=0;i<4;++i) { Put(mappings,i*60,41+i); Put(settings,i*36+4,41+i); settings[i*36+8]=7; Put(settings,i*36+12,i%2); }
  tracker.bytes[0x34]=2; const float now=100; std::memcpy(world.bytes+0x338,&now,4);
  Put(tracker.bytes,0x28,reinterpret_cast<uintptr_t>(class_name)); Put(tracker.bytes,0x2C,8); Put(tracker.bytes,0x30,8);
  g_player_form={reinterpret_cast<HMODULE>(image),GetCurrentThreadId()};
}
void Rejected() {
  const auto pc_before=player, pawn_before=pawn; const unsigned before=exits;
  assert(Leave()==NativePlayerFormResult::kRejected);
  assert(before==exits && !std::memcmp(pc_before.bytes,player.bytes,sizeof(player.bytes)) &&
      !std::memcmp(pawn_before.bytes,pawn.bytes,sizeof(pawn.bytes)));
}
DWORD WINAPI WrongThread(void*) { Rejected(); return 0; }
NativePlayerFormResult Enter() { return NativeEnterPlayerForm(player.bytes,pawn.bytes,move_lease,look_lease); }
void __attribute__((fastcall)) Physics(void* p,void*,uint32_t physics,void* floor,const float* normal) {
  assert(p==pawn.bytes && physics==1 && !floor && normal[0]==0 && normal[1]==0 && normal[2]==1);
  ++physics_calls; assert(Enter()==NativePlayerFormResult::kRejected);
}
void __attribute__((fastcall)) CameraStart(void* camera,void*,uint32_t form) {
  assert(camera==camera_manager.bytes+0x10 && form==(mode ? 1u:0u)); ++camera_calls;
  assert(Enter()==NativePlayerFormResult::kRejected);
  if (fail_camera) look_lease.held=false;
}
void __attribute__((fastcall)) Stat(void* context,void*,float value,uint32_t id,uint32_t send) {
  assert(context==pri.bytes+0x10 && std::isfinite(value) && !send && id>=256 && id<=336);
  ++stats_calls; assert(Enter()==NativePlayerFormResult::kRejected);
  if (stats_lose_guard) look_lease.held=false;
}
void __attribute__((fastcall)) CopyString(void* out,void*,const void* value) { std::memcpy(out,value,12); }
void __attribute__((fastcall)) DropString(void*,void*) {}
uint32_t __attribute__((fastcall)) StringEqual(void* value,void*,const wchar_t* other) {
  const auto* text=reinterpret_cast<const wchar_t*>(Read(value,0)); return text && !_wcsicmp(text,other);
}
void* GetClass(void*,const void*) { return cls.bytes+0x10; }
void* __attribute__((fastcall)) Cast(void* object,void*,void*) { return object; }
void SetStatDelegate() {
  Link(tracker,0x54,&pri); Put(tracker.bytes,0x58,555);
  Put(tracker.bytes,0x5C,reinterpret_cast<uintptr_t>(pri.bytes+0x10));
  Put(tracker.bytes,0x60,reinterpret_cast<uintptr_t>(At(0x10903000)));
}
void Neutral() {
  Put(frame,0x38,reinterpret_cast<uintptr_t>(cls.bytes));
  Put(player.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C1000)));
}
void EntryRejected() {
  const auto previous=player; const auto before=entries;
  assert(Enter()==NativePlayerFormResult::kRejected && before==entries);
  assert(!std::memcmp(previous.bytes,player.bytes,sizeof(player.bytes)));
}
DWORD WINAPI EntryWrongThread(void*) { EntryRejected(); return 0; }
DWORD WINAPI ClearWrongThread(void*) {
  assert(!PrepareNativePlayerClearInputs(player.bytes,pawn.bytes));
  assert(NativeClearPlayerFormInputs(player.bytes,pawn.bytes,move_lease,look_lease)==NativePlayerFormResult::kRejected);
  return 0;
}
} // namespace

namespace wfcds {
bool UnrealObjectValid(void* p) { return Lookup(p); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p); return o ? o->index:0; }
void* UnrealObjectClass(void* p) { return p==player.bytes && cls.valid ? cls.bytes:nullptr; }
bool UnrealObjectIsA(void* p,const wchar_t* type) {
  auto* o=Lookup(p); if (!o) return false;
  return !std::wcscmp(o->type,type) ||
      ((o==&pawn || o==&buddy) && !std::wcscmp(type,L"TnPawn"));
}
bool UnrealObjectName(void* p,char* out,size_t cap) {
  auto* o=Lookup(p); if (!o || std::strlen(o->name)>=cap) return false;
  std::strcpy(out,o->name); return true;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if (!Offset(p,name,out)) return false;
  if (bad_offset) *out+=4;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) {
  uint32_t offset=0; if (!Offset(p,name,&offset)) return false;
  *out=reinterpret_cast<void*>(Read(p,offset)); return !*out || Lookup(*out);
}
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if (!Lookup(p)) return false;
  if (!std::wcscmp(name,L"Role")) { *out=0x66; return true; }
  if (!std::wcscmp(name,L"NetMode")) { *out=0x39C; return true; }
  if (!std::wcscmp(name,L"Physics")) { *out=0x64; return true; }
  if (!std::wcscmp(name,L"DoubleClickDir")) { *out=0x304; return true; }
  if (!std::wcscmp(name,L"CurrentForm")) { *out=0x34; return true; }
  return false;
}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) {
  uint32_t offset=0; if (!UnrealResolveBytePropertyOffset(p,name,&offset)) return false;
  *out=static_cast<uint8_t*>(p)[offset]; return true;
}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out) {
  if (!Lookup(p) || std::wcscmp(name,L"_TransformBlendTimeRemaining")) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+0x590,4); return true;
}
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) {
  auto* o=Lookup(p); if (!o) return false;
  if (!std::wcscmp(name,L"bDeleteMe")) *out=o->deleting;
  else if (o==&player && !std::wcscmp(name,L"IsBoostPressed")) *out=(Read(p,0x614)&4)!=0;
  else if (o==&player && !std::wcscmp(name,L"bDeferredFire")) *out=(Read(p,0x614)&1)!=0;
  else if (o==&player && !std::wcscmp(name,L"IsAllowedToFire")) *out=(Read(p,0x614)&2)!=0;
  else if (o==&player_input && !std::wcscmp(name,L"bInvertMouse")) *out=(Read(p,0xD4)&2)!=0;
  else return false;
  return true;
}
bool UnrealObjectReferenceWritesReady() { return references; }
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* expected,void* value) {
  uint32_t offset=0;
  if (reject_alias || !Offset(p,name,&offset) || Read(p,offset)!=reinterpret_cast<uintptr_t>(expected)) return false;
  Put(p,offset,reinterpret_cast<uintptr_t>(value)); return true;
}
bool UnrealWriteBoolPropertyExpected(void* p,const wchar_t* name,bool expected,bool value) {
  bool current=false;
  if (reject_input || !UnrealReadBoolProperty(p,name,&current) || current!=expected) return false;
  Put(p,0xD4,(Read(p,0xD4)&~2u)|(value ? 2:0)); return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if (p!=profile.bytes) return false;
  const size_t offset=!std::wcscmp(name,L"ProfileMappings") ? 0x60 : !std::wcscmp(name,L"ProfileSettings") ? 0x38 : 0;
  if (!offset) return false;
  out->data=reinterpret_cast<void*>(Read(p,offset)); out->count=Read(p,offset+4); out->capacity=Read(p,offset+8);
  out->element_size=wrong_array ? 1 : offset==0x60 ? 60:36; return true;
}
bool UnrealReadDelegateProperty(void* p,const wchar_t* name,UnrealScriptDelegate* out,uint32_t* offset) {
  if (p!=tracker.bytes || std::wcscmp(name,L"__AddToGameStat__Delegate")) return false;
  if (offset) *offset=bad_delegate_offset ? 0x58:0x54;
  std::memcpy(out,tracker.bytes+0x54,sizeof(*out)); return true;
}
bool NativeReadPawnForm(void* p,uint8_t* value,uint8_t* type) {
  if (!Lookup(p)) return false;
  void* vehicle=reinterpret_cast<void*>(Read(p,0x580)); void* current=reinterpret_cast<void*>(Read(p,0x584));
  *type=vehicle==car.bytes ? 0 : vehicle==plane.bytes ? 2 : vehicle==tank.bytes ? 3 : 4;
  if (current==robot.bytes) { *value=0; return true; }
  if (current==vehicle && *type<=3) { *value=1; return true; }
  return false;
}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease) {
  return (&lease==&move_lease || &lease==&look_lease) && lease.held && !lease.invalidated;
}
} // namespace wfcds

int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)); assert(image);
  std::vector<uint8_t> exe;
  if (argc==2) {
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate); assert(file && file.tellg()==37667328);
    exe.resize(static_cast<size_t>(file.tellg())); file.seekg(0); assert(file.read(reinterpret_cast<char*>(exe.data()),exe.size()));
  }
  const auto module=reinterpret_cast<HMODULE>(image); const auto base=reinterpret_cast<uintptr_t>(image);
  native_player_form_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if (!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  FormBinding bound; assert(BindPlayerFormImage(module,GetCurrentThreadId(),&bound));
  native_player_form_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) {
      At(va)[i]^=1; FormBinding invalid;
      assert(!BindPlayerFormImage(module,GetCurrentThreadId(),&invalid) && !invalid.module); At(va)[i]^=1;
    }
    return true;
  });
  Jump(0x10901000,reinterpret_cast<void*>(&Goto)); Jump(0x10901100,reinterpret_cast<void*>(&Label));
  Jump(0x10A93E80,reinterpret_cast<void*>(&StopAim)); Jump(0x10901200,reinterpret_cast<void*>(&StopFire));
  Jump(0x10901300,reinterpret_cast<void*>(&ClearInputs)); Jump(0x11482830,reinterpret_cast<void*>(&Timer));
  Jump(0x11781530,reinterpret_cast<void*>(&SavedMoves));
  for (auto va:{0x10B28280u,0x10B28870u,0x10B294C0u}) Jump(va,reinterpret_cast<void*>(&Remote));
  Put(At(0x129C0000),0x12C,reinterpret_cast<uintptr_t>(At(0x10901000)));
  Put(At(0x129C0000),0x138,reinterpret_cast<uintptr_t>(At(0x10901100)));
  Put(At(0x129C1000),0x3C,reinterpret_cast<uintptr_t>(At(0x10902770)));
  Put(At(0x129C4000),0x288,reinterpret_cast<uintptr_t>(At(0x10901200)));
  for (unsigned i=0;i<4;++i) {
    Put(At(0x129C2000+0x500*i),0x3C,reinterpret_cast<uintptr_t>(At(kPlayerForms[i].end)));
    Put(At(0x129C2000+0x500*i),0x444,reinterpret_cast<uintptr_t>(At(0x10901300)));
  }
  for (unsigned i=0;i<4000;++i) {
    Reset(i%4); pawn.type=(i/4)%2 ? L"TnAiPawn":L"TnPlayerPawn"; const auto before=pawn;
    assert(NativePlayerFormIsCurrent(player.bytes,pawn.bytes));
    assert(!NativePlayerFormIsNeutral(player.bytes,pawn.bytes));
    assert(Leave()==NativePlayerFormResult::kApplied);
    assert(NativePlayerFormIsNeutral(player.bytes,pawn.bytes));
    assert(!NativePlayerFormIsCurrent(player.bytes,pawn.bytes));
    assert(!std::memcmp(before.bytes,pawn.bytes,sizeof(pawn.bytes)));
    if (!mode) assert(Read(player.bytes,0x61C)==0xA5A5A5A4);
    const unsigned calls=exits; assert(Leave()==NativePlayerFormResult::kApplied && exits==calls);
  }
  assert(exits==4000 && aim_calls==1000 && fire_calls==1000 && clear_calls==3000 &&
      remote_calls==3000 && timer_calls==3000 && move_calls==3000);
  Reset(); Link(pawn,0x428,nullptr); assert(Leave()==NativePlayerFormResult::kApplied && fire_calls==1000);
  Reset(); bad_offset=true; Rejected();
  Reset(); pawn.deleting=true; Rejected();
  Reset(); pawn.index=0; Rejected();
  Reset(); Link(player,0x300,&buddy); Rejected();
  Reset(); Link(pawn,0x1D4,nullptr); Rejected();
  Reset(); Link(pawn,0x98,&foreign); Rejected();
  Reset(); Put(slot,0,reinterpret_cast<uintptr_t>(foreign.bytes)); Rejected();
  Reset(); world.bytes[0x39C]=1; Rejected();
  Reset(); player.bytes[0x66]=2; Rejected();
  Reset(); pawn.bytes[0x66]=2; Rejected();
  Reset(); Link(pawn,0x588,&input); Rejected();
  Reset(); const float nan=std::numeric_limits<float>::quiet_NaN(); std::memcpy(pawn.bytes+0x590,&nan,4); Rejected();
  Reset(); const float blend=0.1f; std::memcpy(pawn.bytes+0x590,&blend,4); Rejected();
  Reset(); Link(aim,0x34,&buddy); Rejected();
  Reset(); Link(inventory,0x94,&buddy); Rejected();
  Reset(); Link(weapon,0x94,&buddy); Rejected();
  Reset(); weapon.bytes[0x66]=2; Rejected();
  Reset(); const auto stop_fire=Read(At(0x129C4000),0x288); Put(At(0x129C4000),0x288,0); Rejected();
  Put(At(0x129C4000),0x288,stop_fire);
  Reset(); state.name="ControllingTurret"; Rejected();
  Reset(); Put(frame,0x4C,1); Rejected();
  Reset(); Put(frame,0x50,65); Rejected();
  Reset(); Put(player.bytes,0x28,0); Rejected();
  Reset();
  void* protected_frame=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE); assert(protected_frame);
  std::memcpy(protected_frame,frame,sizeof(frame)); Put(player.bytes,0x28,reinterpret_cast<uintptr_t>(protected_frame));
  DWORD previous=0; assert(VirtualProtect(protected_frame,4096,PAGE_READONLY,&previous)); Rejected();
  assert(VirtualProtect(protected_frame,4096,PAGE_NOACCESS,&previous)); Rejected();
  assert(VirtualFree(protected_frame,0,MEM_RELEASE));
  Reset(); move_lease.held=false; Rejected();
  Reset(); look_lease.invalidated=true; Rejected();
  Reset(); look_lease.axis=NativeInputAxis::kMove; Rejected();
  for (unsigned i=1;i<4;++i) {
    Reset(i); Link(player,kPlayerForms[i].cache_offset,&robot); Rejected();
    Reset(i); Link(*Lookup(reinterpret_cast<void*>(Read(pawn.bytes,0x584))),kPlayerForms[i].input_offset,nullptr); Rejected();
  }
  Reset(); Put(frame,0x48,reinterpret_cast<uintptr_t>(stack)); Put(frame,0x50,1); assert(Leave()==NativePlayerFormResult::kApplied);
  Reset(); fail_state=true; assert(Leave()==NativePlayerFormResult::kStateMismatch);
  Reset(); lose_pair=true; assert(Leave()==NativePlayerFormResult::kStateMismatch);
  Reset(); change_index=true; assert(Leave()==NativePlayerFormResult::kStateMismatch);
  Reset(); lose_guard=true; assert(Leave()==NativePlayerFormResult::kStateMismatch);
  Reset(); change_form=true; assert(Leave()==NativePlayerFormResult::kStateMismatch);
  Reset(); HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  Reset(); assert(!PrepareNativePlayerFormState(module,GetCurrentThreadId(),player.bytes)); // Not the process EXE.
  // Verify and execute the real campaign inheritance thunks. The old direct-
  // pointer admission would reject them even though they call the same bodies.
  const uintptr_t campaign_begin[]={0x10B2BEC0,0x10B2E850,0x10B2E010,0x10B2DD50};
  const uintptr_t campaign_end[]={0x10B2BED0,0x10B2E860,0x10B2E020,0x10B2DD60};
  for (unsigned i=0;i<4;++i) for (unsigned j=0;j<2;++j) {
    const auto address=j ? campaign_end[i]:campaign_begin[i];
    const auto target=j ? kPlayerForms[i].end:kFormBegins[i];
    const uint8_t head[]={0x8B,0x44,0x24,0x04,0x50,0xE8};
    std::memcpy(At(address),head,6); Put(At(address),6,target-address-10);
    const uint8_t tail[]={0xC2,0x04,0x00}; std::memcpy(At(address)+10,tail,3);
    if (!exe.empty()) assert(!std::memcmp(exe.data()+address-0x10900C00,At(address),13));
  }
  for (unsigned i=0;i<4;++i) {
    Reset(i); Put(At(0x129C2000+0x500*i),0x3C,reinterpret_cast<uintptr_t>(At(campaign_end[i])));
    assert(NativePlayerFormIsCurrent(player.bytes,pawn.bytes));
    assert(Leave()==NativePlayerFormResult::kApplied);
    for (unsigned byte=0;byte<13;++byte) {
      Reset(i); At(campaign_end[i])[byte]^=1; Rejected(); At(campaign_end[i])[byte]^=1;
    }
  }
  // Entry signatures are installed after exit fixtures used their deep-call
  // substitutes; the shipping saved-move and statistics/profile bodies now run.
  entry_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if (!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  Reset(); assert(EntryCodeReady());
  entry_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!EntryCodeReady()); At(va)[i]^=1; }
    return true;
  });
  for (unsigned i=0;i<4;++i)
    Put(At(0x129C2000+0x500*i),0x38,reinterpret_cast<uintptr_t>(At(campaign_begin[i])));
  Jump(native_camera_code::kStartFormVa,reinterpret_cast<void*>(&CameraStart));
  Jump(0x10902000,reinterpret_cast<void*>(&Physics)); Jump(0x10903000,reinterpret_cast<void*>(&Stat));
  Jump(0x109010B0,reinterpret_cast<void*>(&CopyString)); Jump(0x109011B0,reinterpret_cast<void*>(&DropString));
  Jump(0x1094C5C0,reinterpret_cast<void*>(&StringEqual));
  Jump(0x110031A0,reinterpret_cast<void*>(&GetClass)); Jump(0x10901430,reinterpret_cast<void*>(&Cast));
  Put(At(0x119485B0),0,reinterpret_cast<uintptr_t>(&_wcsicmp));
  const double hundred=100; std::memcpy(At(0x129D2270),&hundred,8);
  for (auto pair:{std::pair<uintptr_t,const wchar_t*>{0x12686678,L""},{0x1265E550,L"Soldier"},
      {0x1265E560,L"Leader"},{0x1265E570,L"Scientist"},{0x1265E584,L"Scout"}})
    std::memcpy(At(pair.first),pair.second,(std::wcslen(pair.second)+1)*2);
  At(0x116FC820)[0]=0xC3; // Native saved-move None notifier, no fixture allocation.
  At(0x10B1A630)[0]=0xC3; At(0x10B1A6A0)[0]=0xC3; // Stock pass-through input calls only.
  Put(At(0x129C5000),0x48,reinterpret_cast<uintptr_t>(At(native_camera_code::kStartFormVa)));
  Put(At(0x129C5100),0x220,reinterpret_cast<uintptr_t>(At(0x10902000)));
  const float one=1; std::memcpy(At(0x125848B4),&one,4);
  g_form_entry_prepared=true; assert(InstallNativePlayerFormEntry() && InstallNativePlayerFormEntry());
  assert(EntryCodeReady());
  // Verify both halves of installed hooks, including executable trampoline
  // bytes and the return jump. A damaged pass-through must never be admitted.
  for (unsigned i=0;i<4;++i) {
    auto& hook=g_form_begin_hooks[i]; auto* trampoline=static_cast<uint8_t*>(hook.trampoline);
    for (size_t byte=0;byte<hook.stolen_bytes;++byte) {
      At(kFormBegins[i])[byte]^=1; assert(!EntryCodeReady()); At(kFormBegins[i])[byte]^=1;
    }
    assert(VirtualProtect(trampoline,hook.stolen_bytes+5,PAGE_EXECUTE_READWRITE,&previous));
    for (size_t byte=0;byte<hook.stolen_bytes+5;++byte) {
      trampoline[byte]^=1; assert(!EntryCodeReady()); trampoline[byte]^=1;
    }
    DWORD ignored=0; assert(VirtualProtect(trampoline,hook.stolen_bytes+5,previous,&ignored));
    g_form_begin_originals[i]=At(kFormBegins[i]); assert(!EntryCodeReady()); g_form_begin_originals[i]=hook.trampoline;
    assert(EntryCodeReady());
  }
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000);
  entry_phase=true;
  for (unsigned i=0;i<4000;++i) {
    Reset(i%4); Neutral(); pawn.type=(i/4)%2 ? L"TnAiPawn":L"TnPlayerPawn";
    if (mode) Link(player,kPlayerForms[mode].cache_offset,&robot); // Previous pawn's alias, rebuilt through GC writer.
    const auto before=pawn; const auto cameras=camera_calls;
    assert(Enter()==NativePlayerFormResult::kApplied);
    assert(camera_calls==cameras+1 && NativePlayerFormIsCurrent(player.bytes,pawn.bytes));
    const uint32_t expected=mode==1 || mode==2 ? 2u:0u;
    assert(Read(player_input.bytes,0xD4)==((0xA5A5A5A5u&~2u)|expected));
    assert(!std::memcmp(before.bytes,pawn.bytes,sizeof(pawn.bytes)));
    EntryRejected(); // Already in state: no duplicate entry or camera start.
    assert(Leave()==NativePlayerFormResult::kApplied);
  }
  // Flush genuine native form-stat transitions, preserving the full delegate.
  for (unsigned form=0;form<4;++form) {
    Reset(form); Neutral(); SetStatDelegate(); tracker.bytes[0x34]=form ? 0:1;
    uint8_t before_delegate[16]{}; std::memcpy(before_delegate,tracker.bytes+0x54,16);
    const auto stats=stats_calls; assert(Enter()==NativePlayerFormResult::kApplied);
    assert(stats_calls==stats+(form ? 3u:4u));
    assert(!std::memcmp(before_delegate,tracker.bytes+0x54,16));
  }
  for (auto physics:{2u,4u,10u}) {
    Reset(); Neutral(); pawn.bytes[0x64]=static_cast<uint8_t>(physics);
    const auto calls=physics_calls; assert(Enter()==NativePlayerFormResult::kApplied && physics_calls==calls);
  }
  Reset(); Neutral(); pawn.bytes[0x64]=0; EntryRejected();
  Reset(); Neutral(); Put(player.bytes,0x614,4); EntryRejected();
  Reset(); Neutral(); Link(pri,0x2C,&buddy); EntryRejected();
  Reset(); Neutral(); Link(camera_manager,0x34,&buddy); EntryRejected();
  Reset(); Neutral(); wrong_array=true; EntryRejected();
  Reset(); Neutral(); references=false; EntryRejected();
  Reset(); Neutral(); Put(profile.bytes,0x64,4097); EntryRejected();
  Reset(); Neutral(); tracker.bytes[0x34]=3; EntryRejected();
  Reset(); Neutral(); bad_delegate_offset=true; EntryRejected();
  Reset(); Neutral(); SetStatDelegate(); Link(tracker,0x54,&buddy); EntryRejected();
  Reset(); Neutral(); fail_state=true; assert(Enter()==NativePlayerFormResult::kStateMismatch);
  Reset(1); Neutral(); reject_alias=true; assert(Enter()==NativePlayerFormResult::kStateMismatch);
  Reset(1); Neutral(); reject_input=true; assert(Enter()==NativePlayerFormResult::kStateMismatch);
  Reset(1); Neutral(); fail_camera=true; assert(Enter()==NativePlayerFormResult::kStateMismatch);
  Reset(1); Neutral(); SetStatDelegate(); tracker.bytes[0x34]=0; stats_lose_guard=true;
  const auto calls=camera_calls; assert(Enter()==NativePlayerFormResult::kStateMismatch && calls==camera_calls);
  Reset(); Neutral(); thread=CreateThread(nullptr,0,EntryWrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  // No active transaction: original native BeginState remains pass-through.
  for (unsigned i=0;i<4;++i) {
    Reset(i); Link(player,0x60C,nullptr); Link(player,0x1CC,nullptr);
    const auto before=entries; reinterpret_cast<FormBeginCall>(At(kFormBegins[i]))(player.bytes+0x10,nullptr,123);
    assert(entries==before && !g_form_entry_context);
  }
  for (auto& hook:g_form_begin_hooks) assert(RemoveInlineHook(&hook));
  native_player_clear_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if (!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  Reset(); assert(BindPlayerClearImage());
  native_player_clear_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; assert(!BindPlayerClearImage()); At(va)[i]^=1; }
    return true;
  });
  Jump(0x10B1C980,reinterpret_cast<void*>(&StopBlock));
  Jump(0x10B1BAC0,reinterpret_cast<void*>(&AbortMelee));
  Jump(0x10901800,reinterpret_cast<void*>(&StopBoostInput));
  for (unsigned i=0;i<4;++i) {
    Put(At(0x129C2000+0x500*i),0x444,reinterpret_cast<uintptr_t>(At(kClearMethods[i])));
    Put(At(0x129C2000+0x500*i),0x440,reinterpret_cast<uintptr_t>(At(clear_code::kGlobalVa)));
    Put(At(0x129C2000+0x500*i),0x4A4,reinterpret_cast<uintptr_t>(At(0x10B1C980)));
    if (i) Put(At(0x129C2000+0x500*i),0x4D4,reinterpret_cast<uintptr_t>(At(kStopBoostMethods[i])));
  }
  Put(At(0x129C7000),0xD0,reinterpret_cast<uintptr_t>(At(0x10901800)));
  const auto clear_reset=[&](unsigned form) {
    Reset(form); pawn.type=L"TnPlayerPawn"; clear_dirty=false;
    input.type=form==1 ? L"TnCarInputState":form==2 ? L"TnPlaneInputState":L"TnTankInputState";
    Link(input,0x2C,&pawn); Put(player.bytes,0x614,0xA5A5A5A5);
    for (auto* f:{&car,&plane,&tank}) Put(f->bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C7000)));
    for (size_t offset=0x3C;offset<0x60;offset+=4) Put(input.bytes,offset,0xFFFFFFFF);
  };
  const auto clear_player=[&]() {
    clearing_inputs=true;
    const auto result=NativeClearPlayerFormInputs(player.bytes,pawn.bytes,move_lease,look_lease);
    clearing_inputs=false; return result;
  };
  const auto old_aim=aim_calls,old_fire=fire_calls;
  for (unsigned i=0;i<4000;++i) {
    clear_reset(i%4); const auto before=pawn; const auto weapon_before=weapon;
    assert(PrepareNativePlayerClearInputs(player.bytes,pawn.bytes));
    assert(clear_player()==NativePlayerFormResult::kApplied);
    assert(Read(player.bytes,0x614)==0xA5A5A5A2);
    assert(!std::memcmp(before.bytes,pawn.bytes,sizeof(before.bytes)) &&
           !std::memcmp(weapon_before.bytes,weapon.bytes,sizeof(weapon_before.bytes)));
    assert(NativePlayerFormIsCurrent(player.bytes,pawn.bytes)); // No GotoState or form change.
    if (i%4) {
      const auto input_flags_offset=i%4==2 ? 0x4C:0x48;
      assert(Read(input.bytes,input_flags_offset)==(i%4==3 ? 0xFFFFFFFCu:0xFFFFFFF0u));
      assert(Read(input.bytes,0x5C)==0xFFFFFFFFu); // Unrelated input state is preserved.
    }
  }
  assert(block_calls==1000 && melee_calls==1000 && boost_stop_calls==3000 &&
      aim_calls==old_aim+1000 && fire_calls==old_fire+4000);
  clear_reset(0); pawn.type=L"TnAiPawn";
  assert(!PrepareNativePlayerClearInputs(player.bytes,pawn.bytes) && clear_player()==NativePlayerFormResult::kRejected);
  clear_reset(1); pawn.type=L"TnAiPawn"; assert(clear_player()==NativePlayerFormResult::kApplied);
  clear_reset(0); look_lease.held=false; assert(clear_player()==NativePlayerFormResult::kRejected);
  clear_reset(0); auto copied=move_lease;
  assert(NativeClearPlayerFormInputs(player.bytes,pawn.bytes,copied,look_lease)==NativePlayerFormResult::kRejected);
  clear_reset(0); clear_dirty=true; assert(clear_player()==NativePlayerFormResult::kStateMismatch);
  clear_reset(1); Link(input,0x2C,&buddy); assert(clear_player()==NativePlayerFormResult::kRejected);
  clear_reset(2); input.type=L"Object"; assert(clear_player()==NativePlayerFormResult::kRejected);
  clear_reset(1); const auto boost_ptr=Read(At(0x129C2500),0x4D4); Put(At(0x129C2500),0x4D4,0);
  assert(clear_player()==NativePlayerFormResult::kRejected); Put(At(0x129C2500),0x4D4,boost_ptr);
  clear_reset(2); const auto setter_ptr=Read(At(0x129C7000),0xD0); Put(At(0x129C7000),0xD0,0);
  assert(clear_player()==NativePlayerFormResult::kRejected); Put(At(0x129C7000),0xD0,setter_ptr);
  clear_reset(0); thread=CreateThread(nullptr,0,ClearWrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0); CloseHandle(thread);
  clear_reset(0); state.name="ControllingTurret"; assert(clear_player()==NativePlayerFormResult::kRejected);
  clear_reset(0); At(clear_code::kDeferredVa)[0]^=1;
  assert(clear_player()==NativePlayerFormResult::kRejected); At(clear_code::kDeferredVa)[0]^=1;
  clear_reset(0); const auto clear_ptr=Read(At(0x129C2000),0x444); Put(At(0x129C2000),0x444,0);
  assert(clear_player()==NativePlayerFormResult::kRejected); Put(At(0x129C2000),0x444,clear_ptr);
  clear_reset(0); const auto forward=At(0x10901A00);
  forward[0]=0xE8; Put(forward,1,reinterpret_cast<uintptr_t>(At(clear_code::kWalkingVa))-reinterpret_cast<uintptr_t>(forward+5)); forward[5]=0xC3;
  Put(At(0x129C2000),0x444,reinterpret_cast<uintptr_t>(forward)); assert(clear_player()==NativePlayerFormResult::kApplied);
  forward[5]=0x90; assert(clear_player()==NativePlayerFormResult::kRejected);
  Put(At(0x129C2000),0x444,clear_ptr);
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativePlayerFormStateTests passed: 4000 exits + 4000 entries + 4000 state-specific input clears; native flags/vehicle resets, guarded form/ownership/lease paths (deep services and engine dispatch are fixtures, not gameplay)");
}
