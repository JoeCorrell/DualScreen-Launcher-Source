// Actual InitializeForms, StartForm, GotoState dispatch, typed cast wrappers,
// GetStrategy/set_CameraStrategy and GC execute. Engine state callbacks, class
// lookup and deep camera activation are fixtures, NOT a game/camera simulation.
#include "NativeCameraHandoff.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>

using namespace wfcds;
extern "C" void WfcCallEsiObject(void*,void*,void*);
namespace {
struct Object {
  alignas(16) uint8_t bytes[0x800]{};
  const wchar_t* type; const char* name = "fixture";
  bool valid = true, deleting = false; uint32_t index = 1;
};
Object player{{},L"TnPlayerController"}, pawn{{},L"TnPawn"}, buddy{{},L"TnPawn"},
    world{{},L"WorldInfo"}, foreign{{},L"WorldInfo"}, manager{{},L"TnCameraStrategyManager"},
    camera{{},L"TnCamera"}, set{{},L"HmCameraStrategySet"},
    car{{},L"TnCarForm"}, truck{{},L"TnTruckForm"}, plane{{},L"TnPlaneForm"}, tank{{},L"TnTankForm"},
    robot{{},L"TnRobotForm"},
    car_class{{},L"Class"}, tank_class{{},L"Class"}, plane_class{{},L"Class"},
    shoulder_type{{},L"Class","TnOverTheShoulderCameraStrategyType"},
    hover_type{{},L"Class","TnHoveringCameraStrategyType"},
    driving_type{{},L"Class","TnDrivingCameraStrategyType"},
    shoulder{{},L"HmCameraStrategy"}, hover{{},L"HmCameraStrategy"}, driving{{},L"HmCameraStrategy"},
    state{{},L"State"}, structure{{},L"ScriptStruct"},
    view_local{{},L"LocalPlayer"},view_local_class{{},L"Class"},view_pri{{},L"PlayerReplicationInfo"};
Object* objects[] = {&player,&pawn,&buddy,&world,&foreign,&manager,&camera,&set,&car,&truck,&plane,&tank,&robot,
    &car_class,&tank_class,&plane_class,&shoulder_type,&hover_type,&driving_type,&shoulder,&hover,&driving,&state,&structure,
    &view_local,&view_local_class,&view_pri};
uint8_t* image;
alignas(16) uint8_t engine[0x50]{}, context[0x30]{}, world_slot[4]{}, frame[0x40]{}, entries[24*8]{};
NativePlayerInputLease move_lease,look_lease;
bool references = true, reject_write = false, bad_offset = false, corrupt_owner = false, cast_reject = false;
bool lose_pair = false, change_index = false, fail_state = false, invalid_mode = false, corrupt_camera = false;
bool wrong_array = false, turret = false, lose_guard = false;
bool fail_enter = false;
bool probe_reentry = true;
unsigned activations = 0, state_calls = 0, writes = 0;
unsigned view_calls=0,view_dirty=0;
bool view_testing=false;
uint32_t requested_mode = 0;
Object* current_pawn = &buddy;
Object* Lookup(void* p) { for (auto* o:objects) if (p == o->bytes && o->valid) return o; return nullptr; }
uint32_t Read(const void* p,size_t offset) { uint32_t x = 0; std::memcpy(&x,static_cast<const uint8_t*>(p)+offset,4); return x; }
void Put(void* p,size_t offset,uintptr_t x) { const uint32_t v = static_cast<uint32_t>(x); std::memcpy(static_cast<uint8_t*>(p)+offset,&v,4); }
void Link(Object& o,size_t offset,Object* value) { Put(o.bytes,offset,reinterpret_cast<uintptr_t>(value ? value->bytes : nullptr)); }
uint8_t* At(uintptr_t va) { return image+va-native_reference_code::kImageBase; }
void Jump(uintptr_t va,const void* target) { At(va)[0]=0xE9; Put(At(va),1,reinterpret_cast<uintptr_t>(target)-reinterpret_cast<uintptr_t>(At(va)+5)); }
bool Offset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o=Lookup(p); if (!o) return false;
  const auto match=[&](const wchar_t* field,uint32_t offset) { if (std::wcscmp(field,name)) return false; *out=offset; return true; };
  if (match(L"WorldInfo",0x98) || match(L"Owner",0x2C)) return true;
  if ((o==&pawn || o==&buddy) && (match(L"Controller",0x1D4) || match(L"_VehicleForm",0x580) ||
      match(L"_RobotForm",0x57C) || match(L"_CurrentForm",0x584) || match(L"_CurrentTransformation",0x588) ||
      match(L"PlayerReplicationInfo",0x3C0))) return true;
  if (o==&player && (match(L"Pawn",0x1C8) || match(L"AcknowledgedPawn",0x300) ||
      match(L"PlayerCamera",0x2D4) || match(L"_CameraStrategyManager",0x60C) ||
      match(L"Player",0x2D0) || match(L"PlayerReplicationInfo",0x1CC) || match(L"ViewTarget",0x30C))) return true;
  if (o==&manager && (match(L"CameraStrategySet",0x2C) || match(L"_Camera",0x30) ||
      match(L"_TransformerPawn",0x34) || match(L"_TankForm",0x38) || match(L"_CarForm",0x3C) ||
      match(L"_PlaneForm",0x40) || match(L"_CameraStrategy",0x44))) return true;
  return o==&camera && (match(L"ActiveCameraStrategy",0x3AC) || match(L"PCOwner",0x1C8));
}
void Entry(size_t i,Object* type,Object* strategy) {
  Put(entries,24*i,reinterpret_cast<uintptr_t>(type ? type->bytes:nullptr));
  Put(entries,24*i+16,reinterpret_cast<uintptr_t>(strategy ? strategy->bytes:nullptr));
}
void SetTable(uintptr_t table) { Put(manager.bytes,0x10,reinterpret_cast<uintptr_t>(At(table))); }
void Select(Object& controlled,Object& vehicle,uint8_t mode) {
  Link(player,0x1C8,&controlled); Link(player,0x300,&controlled);
  Link(pawn,0x1D4,nullptr); Link(buddy,0x1D4,nullptr); Link(controlled,0x1D4,&player);
  Link(controlled,0x580,&vehicle); Link(controlled,0x584,mode ? &vehicle : &robot);
  current_pawn=&controlled; requested_mode=mode;
}
NativeCameraResult Handoff() {
  return NativeHandoffCamera(player.bytes,current_pawn->bytes,current_pawn==&pawn ? buddy.bytes:pawn.bytes,
      static_cast<uint8_t>(requested_mode),move_lease,look_lease);
}
void Reset() {
  uint32_t index=1;
  for (auto* o:objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->valid=true; o->deleting=false; o->index=index++; }
  for (auto* o:{&player,&pawn,&buddy,&camera}) { Link(*o,0x98,&world); o->bytes[0x66]=3; }
  Link(player,0x60C,&manager); Link(player,0x2D4,&camera);
  Link(manager,0x30,&camera); Link(manager,0x34,&pawn); Link(manager,0x2C,&set);
  Link(manager,0x44,&shoulder_type); Link(manager,0x3C,&car);
  Link(camera,0x2C,&pawn); Link(camera,0x3AC,&shoulder);
  for (auto* o:{&pawn,&buddy}) {
    Link(*o,0x57C,&robot); Link(*o,0x580,&car); Link(*o,0x584,&robot);
    for (size_t i=0xD8;i<0x108;++i) o->bytes[i]=static_cast<uint8_t>(i);
  }
  for (auto pair:{std::pair<Object*,unsigned>{&car,0},{&truck,1},{&plane,2},{&tank,3}})
    Put(pair.first->bytes,0x10,reinterpret_cast<uintptr_t>(At(0x129C0000+0x100*pair.second)));
  SetTable(0x128B6D10); Put(manager.bytes,0,reinterpret_cast<uintptr_t>(At(0x129C1000)));
  Put(manager.bytes,0x28,reinterpret_cast<uintptr_t>(frame));
  Put(frame,0x38,reinterpret_cast<uintptr_t>(state.bytes)); Put(state.bytes,4,101);
  std::memset(entries,0,sizeof(entries)); Entry(0,&driving_type,&driving); Entry(1,&shoulder_type,&shoulder); Entry(2,&hover_type,&hover);
  Put(set.bytes,0x60,reinterpret_cast<uintptr_t>(entries)); Put(set.bytes,0x64,3); Put(set.bytes,0x68,8);
  Put(At(0x12B10A9C),0,reinterpret_cast<uintptr_t>(engine)); Put(engine,0x40,reinterpret_cast<uintptr_t>(context));
  Put(context,0x28,reinterpret_cast<uintptr_t>(world_slot)); Put(world_slot,0,reinterpret_cast<uintptr_t>(world.bytes));
  Put(At(0x12B2FA48),0,0); Put(At(0x12B324AC),0,101); Put(At(0x12B2FF48),0,102);
  Put(At(0x12B32FA4),0,103); Put(At(0x12B320B4),0,104);
  Put(At(0x12B1047C),0,0); Put(At(0x12B10474),0,0);
  move_lease={}; look_lease={}; move_lease.controller=look_lease.controller=player.bytes;
  move_lease.held=look_lease.held=true; look_lease.axis=NativeInputAxis::kLook;
  references=true; reject_write=bad_offset=corrupt_owner=cast_reject=lose_pair=change_index=fail_state=false;
  invalid_mode=corrupt_camera=wrong_array=turret=lose_guard=fail_enter=false;
  probe_reentry=true;
  Select(buddy,car,0);
}
void* GetClass(void* cache,const void*) {
  Object* cls=cache==At(0x12B6E88C) ? &car_class : cache==At(0x12B6EE78) ? &tank_class : &plane_class;
  return cls->bytes+0x10;
}
void* __attribute__((fastcall)) Cast(void* object,void*,void* cls) {
  if (cls==view_local_class.bytes) {
    assert(view_testing && object==view_local.bytes && g_camera_handoff_busy);
    ++view_calls;
    assert(NativeSetPlayerViewTarget(player.bytes,current_pawn->bytes,current_pawn==&pawn ? buddy.bytes:pawn.bytes,
        move_lease,look_lease)==NativeCameraResult::kRejected);
    if(view_dirty==1) ++camera.index;
    if(view_dirty==2) look_lease.held=false;
    if(view_dirty==3) Put(current_pawn->bytes,0xD8,0);
    if(view_dirty==4) Link(camera,0x2C,&buddy);
    if(view_dirty==5) Link(camera,0x3AC,&hover);
    if(view_dirty==6) world.bytes[0x39C]=3;
    if(view_dirty==7) Link(player,0x300,nullptr);
    if(view_dirty==8) Link(*current_pawn,0x1D4,nullptr);
    return object;
  }
  auto* form=Lookup(object);
  assert(form && (cls==car_class.bytes || cls==tank_class.bytes || cls==plane_class.bytes));
  if (cast_reject) return nullptr;
  return object;
}
// SetActiveCameraStrategy's optimized ABI is EAX strategy, stack camera,parent.
void __attribute__((regparm(1),stdcall)) Activate(void* strategy,void* cam,void* parent) {
  assert(cam==camera.bytes+0x10 && parent==current_pawn->bytes+0x10);
  assert(Read(manager.bytes,0x34)==reinterpret_cast<uintptr_t>(current_pawn->bytes));
  ++activations;
  if (probe_reentry) assert(Handoff()==NativeCameraResult::kRejected);
  Put(camera.bytes,0x3AC,reinterpret_cast<uintptr_t>(static_cast<uint8_t*>(strategy)-0x10));
  Link(camera,0x2C,corrupt_camera ? &foreign:current_pawn);
  if (lose_guard) look_lease.held=false;
  if (change_index) ++camera.index;
  if (lose_pair) Link(player,0x1C8,nullptr);
  if (invalid_mode) Link(*current_pawn,0x584,nullptr);
}
uint32_t __attribute__((fastcall)) Goto(void* object,void*,uint32_t name,uint32_t force,uint32_t keep) {
  assert(object==manager.bytes && force==0 && keep==0); ++state_calls;
  if (probe_reentry) assert(Handoff()==NativeCameraResult::kRejected);
  if (fail_state || (fail_enter && name)) return 0;
  uintptr_t table=0x12801188; Object* type=nullptr;
  if (name==101) { table=0x128B6D10; type=&shoulder_type; }
  if (name==102) { table=0x128B6DE0; type=&hover_type; }
  if (name==103) { table=0x128B6EB0; type=&driving_type; }
  if (name==104) { table=0x128B6F18; type=&driving_type; }
  SetTable(table); Put(frame,0x38,name ? reinterpret_cast<uintptr_t>(state.bytes):0); Put(state.bytes,4,name);
  if (type) WfcCallEsiObject(At(native_camera_code::kSetStrategyVa),manager.bytes+0x10,type->bytes+0x10);
  return 1;
}
void __attribute__((fastcall)) Label(void* object,void*,uint32_t name) { assert(object==manager.bytes && name==0x190000); }
void Rejected() {
  const auto before_manager=manager, before_camera=camera, before_pawn=pawn, before_buddy=buddy;
  const auto calls=activations, old_writes=writes;
  assert(Handoff()==NativeCameraResult::kRejected);
  assert(!std::memcmp(manager.bytes,before_manager.bytes,sizeof(manager.bytes)));
  assert(!std::memcmp(camera.bytes,before_camera.bytes,sizeof(camera.bytes)));
  assert(!std::memcmp(pawn.bytes,before_pawn.bytes,sizeof(pawn.bytes)));
  assert(!std::memcmp(buddy.bytes,before_buddy.bytes,sizeof(buddy.bytes)));
  assert(calls==activations && old_writes==writes);
}
DWORD WINAPI WrongThread(void*) {
  Rejected(); uint8_t mode=2,type=4;
  assert(!NativeReadPawnForm(buddy.bytes,&mode,&type)); return 0;
}
}  // namespace

namespace wfcds {
bool UnrealResolveClassByPath(const wchar_t* name,void** out) {
  if(std::wcscmp(name,L"Engine.LocalPlayer")) return false;
  *out=view_local_class.bytes; return true;
}
bool UnrealResolveStructPropertyOffset(void* p,const wchar_t* name,uint32_t size,uint32_t* out) {
  if(p!=camera.bytes || size!=44) return false;
  if(!std::wcscmp(name,L"ViewTarget")) *out=0x31C;
  else if(!std::wcscmp(name,L"PendingViewTarget")) *out=0x348;
  else return false;
  if(bad_offset) ++*out;
  return true;
}
bool UnrealObjectValid(void* p) { return Lookup(p); }
uint32_t UnrealObjectIndex(void* p) { auto* o=Lookup(p); return o ? o->index:0; }
bool UnrealObjectIsA(void* p,const wchar_t* type) {
  auto* o=Lookup(p); if (!o) return false;
  if (!std::wcscmp(o->type,type)) return true;
  if (!std::wcscmp(type,L"TnVehicleForm")) return o==&car || o==&truck || o==&tank || o==&plane;
  return o==&truck && !std::wcscmp(type,L"TnCarForm");
}
bool UnrealObjectName(void* p,char* out,size_t capacity) {
  auto* o=Lookup(p); if (!o || std::strlen(o->name)>=capacity) return false;
  std::strcpy(out,o->name); return true;
}
bool UnrealObjectReferenceWritesReady() { return references; }
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* expected,void* value) {
  uint32_t offset=0;
  if (reject_write || !Offset(p,name,&offset) || Read(p,offset)!=reinterpret_cast<uintptr_t>(expected)) return false;
  ++writes; assert(Handoff()==NativeCameraResult::kRejected);
  Put(p,offset,reinterpret_cast<uintptr_t>(corrupt_owner ? foreign.bytes:value));
  return true;
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
  else if (!std::wcscmp(name,L"IsInTurret")) *out=turret;
  else if (!std::wcscmp(name,L"bStasis") || !std::wcscmp(name,L"bTearOff")) *out=false;
  else return false;
  return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if (p!=set.bytes || std::wcscmp(name,L"Strategies")) return false;
  out->data=reinterpret_cast<void*>(Read(p,0x60)); out->count=Read(p,0x64); out->capacity=Read(p,0x68);
  out->element_size=wrong_array ? 20:24; out->struct_definition=structure.bytes; return true;
}
bool UnrealReadStructArrayObjectField(const UnrealStructArrayView& array,int32_t i,const wchar_t* name,void** out) {
  if (i<0 || i>=array.count) return false;
  const size_t offset=!std::wcscmp(name,L"Type") ? 0:16;
  *out=reinterpret_cast<void*>(Read(array.data,24*i+offset)); return !*out || Lookup(*out);
}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease) {
  return (&lease==&move_lease || &lease==&look_lease) && lease.held && !lease.invalidated;
}
}  // namespace wfcds

#include "NativeViewTargetTests.inc"

int main(int argc,char** argv) {
  image=static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE)); assert(image);
  const auto module=reinterpret_cast<HMODULE>(image); const auto base=reinterpret_cast<uintptr_t>(image);
  std::vector<uint8_t> exe;
  if (argc==2) {
    std::ifstream file(argv[1],std::ios::binary|std::ios::ate); assert(file && file.tellg()==37667328);
    exe.resize(static_cast<size_t>(file.tellg())); file.seekg(0); assert(file.read(reinterpret_cast<char*>(exe.data()),exe.size()));
  }
  native_camera_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    if (!exe.empty()) assert(!std::memcmp(exe.data()+va-0x10900C00,code,sizeof(code)));
    const auto relocated=native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  });
  for (const auto table:kCameraTables) {
    if (!exe.empty()) assert(Read(exe.data(),table-0x11948000+0x1046E00+0x48)==native_camera_code::kStartFormVa);
    Put(At(table),0x48,reinterpret_cast<uintptr_t>(At(native_camera_code::kStartFormVa)));
  }
  CameraBindings bound; assert(BindCameraImage(module,GetCurrentThreadId(),&bound));
  native_camera_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) {
      At(va)[i]^=1; CameraBindings rejected; assert(!BindCameraImage(module,GetCurrentThreadId(),&rejected));
      assert(!rejected.module); At(va)[i]^=1;
    }
    return true;
  });
  for (const auto table:kCameraTables) {
    const auto saved=Read(At(table),0x48); Put(At(table),0x48,0);
    CameraBindings rejected; assert(!BindCameraImage(module,GetCurrentThreadId(),&rejected));
    Put(At(table),0x48,saved);
  }
  Jump(0x110031A0,reinterpret_cast<void*>(&GetClass)); Jump(0x10901430,reinterpret_cast<void*>(&Cast));
  Jump(0x113A1A40,reinterpret_cast<void*>(&Activate));
  Jump(0x10901000,reinterpret_cast<void*>(&Goto)); Jump(0x10901100,reinterpret_cast<void*>(&Label));
  Put(At(0x129C1000),0x12C,reinterpret_cast<uintptr_t>(At(0x10901000)));
  Put(At(0x129C1000),0x138,reinterpret_cast<uintptr_t>(At(0x10901100)));
  for (unsigned i=0;i<4;++i) {
    auto* leaf=At(0x10902000+i*16); leaf[0]=0xB8; Put(leaf,1,i); leaf[5]=0xC3;
    Put(At(0x129C0000+i*0x100),0xCC,reinterpret_cast<uintptr_t>(leaf));
  }
  // Forbidden live-transfer paths: the stock owner setter would reactivate
  // the previous strategy; camera template cloning would create fresh state.
  for (const auto va:{0x10BDABE0u,0x10BDAD70u,0x10BDAEA0u,0x10BDAED0u}) { At(va)[0]=0x0F; At(va)[1]=0x0B; }
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000); g_camera_handoff=bound;
  Reset();
  // Read-only admission of both pawns in either mode before controller detach.
  const auto admission_manager=manager,admission_camera=camera;
  CameraCapture admission;
  for (auto* candidate:{&pawn,&buddy}) for (uint8_t mode=0;mode<2;++mode)
    assert(CaptureCamera(player.bytes,candidate->bytes,candidate==&pawn ? buddy.bytes:pawn.bytes,mode,&admission,false));
  assert(!std::memcmp(manager.bytes,admission_manager.bytes,sizeof(manager.bytes)));
  assert(!std::memcmp(camera.bytes,admission_camera.bytes,sizeof(camera.bytes)));
  assert(!PrepareNativeCameraHandoff(module,GetCurrentThreadId(),player.bytes,pawn.bytes,buddy.bytes)); // not host EXE
  Object* vehicles[]={&car,&truck,&plane,&tank};
  // Shared read-only classifier used by controller form entry: all four actual
  // vehicle types, both current forms, and no owner/camera/pawn mutation.
  for (uint8_t type=0;type<4;++type) for (uint8_t form=0;form<2;++form) {
    Reset(); Select(buddy,*vehicles[type],form);
    const auto before=buddy; uint8_t actual_mode=2,actual_type=4;
    assert(NativeReadPawnForm(buddy.bytes,&actual_mode,&actual_type) && actual_mode==form && actual_type==type);
    assert(!std::memcmp(before.bytes,buddy.bytes,sizeof(buddy.bytes)));
  }
  Reset(); uint8_t read_mode=2,read_type=4;
  assert(!NativeReadPawnForm(buddy.bytes,nullptr,&read_type));
  assert(!NativeReadPawnForm(buddy.bytes,&read_mode,nullptr));
  Link(buddy,0x588,&car); assert(!NativeReadPawnForm(buddy.bytes,&read_mode,&read_type));
  Reset(); Link(buddy,0x580,nullptr);
  assert(NativeReadPawnForm(buddy.bytes,&read_mode,&read_type) && read_mode==0 && read_type==4);
  for (unsigned i=0;i<3000;++i) {
    Reset();
    for (unsigned direction=0;direction<2;++direction) {
      Select(direction ? pawn:buddy,*vehicles[(i+direction)%4],(i/4+direction)%2);
      const auto before_pawn=pawn, before_buddy=buddy;
      const unsigned before=activations, before_states=state_calls;
      assert(Handoff()==NativeCameraResult::kApplied);
      assert(activations==before+1 && state_calls==before_states+2);
      assert(Read(camera.bytes,0x2C)==reinterpret_cast<uintptr_t>(current_pawn->bytes));
      assert(!std::memcmp(pawn.bytes,before_pawn.bytes,sizeof(pawn.bytes)));
      assert(!std::memcmp(buddy.bytes,before_buddy.bytes,sizeof(buddy.bytes)));
    }
  }
  // Every cross-vehicle pairing and both starting/target modes, not just
  // adjacent vehicle types. Same owner + same strategy still re-enters state.
  for (auto* first:vehicles) for (auto* second:vehicles) for (uint8_t a=0;a<2;++a) for (uint8_t b=0;b<2;++b) {
    Reset(); Select(pawn,*first,a); assert(Handoff()==NativeCameraResult::kApplied);
    Select(buddy,*second,b); assert(Handoff()==NativeCameraResult::kApplied);
    const auto count=activations; assert(Handoff()==NativeCameraResult::kApplied); assert(activations==count+1);
  }
  // Controller BeginState owns the one StartForm call in the staged path.
  // Invoke that camera call here as a fixture; the controller entry has its own
  // suite. This camera fixture does not execute its physics/input/statistics.
  const auto controller_camera_start=[&]() {
    probe_reentry=false; // Outside the camera operation; caller owns serialization.
    reinterpret_cast<StartFormCall>(At(native_camera_code::kStartFormVa))(
        manager.bytes+0x10,nullptr,requested_mode);
    probe_reentry=true;
  };
  const auto stage=[&]() {
    return NativeStageCameraForPlayerForm(player.bytes,current_pawn->bytes,
        current_pawn==&pawn ? buddy.bytes:pawn.bytes,static_cast<uint8_t>(requested_mode),move_lease,look_lease);
  };
  const auto ready=[&]() {
    return NativeCameraFormIsCurrent(player.bytes,current_pawn->bytes,
        current_pawn==&pawn ? buddy.bytes:pawn.bytes,static_cast<uint8_t>(requested_mode));
  };
  for (auto* first:vehicles) for (auto* second:vehicles) for (uint8_t a=0;a<2;++a) for (uint8_t b=0;b<2;++b) {
    Reset(); Select(pawn,*first,a); assert(Handoff()==NativeCameraResult::kApplied);
    Select(buddy,*second,b);
    const auto calls=activations, states=state_calls;
    assert(stage()==NativeCameraResult::kApplied && activations==calls && state_calls==states);
    assert(!ready()); // The old camera owner must not pass the completed check.
    controller_camera_start();
    assert(ready() && activations==calls+1 && state_calls==states+2);
    // Same owner/form still stages without activating and enters exactly once.
    assert(stage()==NativeCameraResult::kApplied && activations==calls+1);
    controller_camera_start(); assert(ready() && activations==calls+2);
  }
  Reset(); assert(stage()==NativeCameraResult::kApplied);
  fail_enter=true; controller_camera_start(); assert(!ready());
  fail_enter=false; Select(pawn,car,0); assert(stage()==NativeCameraResult::kApplied);
  controller_camera_start(); assert(ready()); // Restore after entry failed at None.
  Reset(); reject_write=true; assert(stage()==NativeCameraResult::kRejected);
  Reset(); corrupt_owner=true; assert(stage()==NativeCameraResult::kStateMismatch);
  Reset(); cast_reject=true; assert(stage()==NativeCameraResult::kStateMismatch);
  Reset(); move_lease.held=false; assert(stage()==NativeCameraResult::kRejected);
  // Exact side-effect-free getter encodings. A stateful/unknown virtual is
  // rejected before owner writes, even if the object has a familiar class.
  for (const auto first_byte:{0x33u,0x32u,0xB0u}) {
    Reset(); auto* leaf=At(0x10902000); const uint32_t old=Read(leaf,0);
    leaf[0]=static_cast<uint8_t>(first_byte); leaf[1]=first_byte==0xB0 ? 0:0xC0; leaf[2]=0xC3;
    FlushInstructionCache(GetCurrentProcess(),leaf,6);
    assert(Handoff()==NativeCameraResult::kApplied);
    Put(leaf,0,old); FlushInstructionCache(GetCurrentProcess(),leaf,6);
  }
  Reset(); At(0x10902000)[0]=0xCC; Rejected(); At(0x10902000)[0]=0xB8;
  Reset(); Put(At(0x10902000),1,4); Rejected(); Put(At(0x10902000),1,0);
  Reset(); Put(At(0x129C0000),0xCC,reinterpret_cast<uintptr_t>(At(0x10902020))); Rejected();
  Put(At(0x129C0000),0xCC,reinterpret_cast<uintptr_t>(At(0x10902000)));
  FlushInstructionCache(GetCurrentProcess(),image,0x2300000);
  Reset(); Put(At(0x12B324AC),0,0); Rejected();
  Reset(); Put(At(0x12B2FF48),0,101); Rejected();
  Reset(); Link(buddy,0x580,nullptr); assert(Handoff()==NativeCameraResult::kApplied); // robot-only pawn
  Reset(); bad_offset=true; Rejected();
  Reset(); references=false; Rejected();
  Reset(); reject_write=true; Rejected();
  Reset(); turret=true; Rejected();
  Reset(); wrong_array=true; Rejected();
  Reset(); world.bytes[0x39C]=1; Rejected();
  Reset(); buddy.bytes[0x66]=1; Rejected();
  Reset(); camera.deleting=true; Rejected();
  Reset(); Link(buddy,0x98,&foreign); Rejected();
  Reset(); Put(world_slot,0,reinterpret_cast<uintptr_t>(foreign.bytes)); Rejected();
  Reset(); Link(player,0x300,&pawn); Rejected();
  Reset(); Link(player,0x1C8,&pawn); Rejected();
  Reset(); Link(buddy,0x1D4,nullptr); Rejected();
  Reset(); Link(camera,0x2C,&foreign); Rejected();
  Reset(); Link(manager,0x34,&foreign); Rejected();
  Reset(); SetTable(0x128B6FE8); Rejected(); // animated camera
  Reset(); requested_mode=2; Rejected();
  Reset(); requested_mode=1; Rejected(); // current pawn is actually robot form
  Reset(); Link(buddy,0x588,&car); Rejected();
  Reset(); Put(buddy.bytes,0x590,0x7FC00000); Rejected();
  Reset(); Put(buddy.bytes,0x590,0x3F800000); Rejected();
  Reset(); Link(buddy,0x580,nullptr); Select(buddy,car,1); Link(buddy,0x580,nullptr); Rejected();
  Reset(); Entry(3,&shoulder_type,&shoulder); Put(set.bytes,0x64,4); Rejected();
  Reset(); Entry(1,&shoulder_type,nullptr); Rejected();
  Reset(); move_lease.held=false; Rejected();
  Reset(); look_lease.invalidated=true; Rejected();
  Reset(); look_lease.axis=NativeInputAxis::kMove; Rejected();
  Reset();
  HANDLE thread=CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr); assert(thread);
  assert(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0); CloseHandle(thread);
  Reset(); corrupt_owner=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); cast_reject=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); fail_state=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); corrupt_camera=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); lose_guard=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); change_index=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); lose_pair=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); invalid_mode=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  Reset(); fail_enter=true; assert(Handoff()==NativeCameraResult::kStateMismatch);
  assert(CameraTable(manager.bytes,0x12801188));
  // The caller has restored reciprocal ownership under the same held leases.
  // Recover from a partially entered None state without cloning a new camera.
  fail_enter=false; Select(pawn,car,0); assert(Handoff()==NativeCameraResult::kApplied);
  Reset(); assert(NativeHandoffCamera(player.bytes,buddy.bytes,pawn.bytes,0,look_lease,move_lease)==NativeCameraResult::kRejected);
  TestNativeViewTarget(exe);
  assert(VirtualFree(image,0,MEM_RELEASE));
  std::puts("NativeCameraHandoffTests passed: 6000 camera handoffs plus single-activation staged cross-form transfers; binary signatures, ownership, guards and dirty failures. Controller/deep camera callbacks are fixtures.");
}
