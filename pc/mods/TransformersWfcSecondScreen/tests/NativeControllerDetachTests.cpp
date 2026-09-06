// Executes shipping controller/pawn UnPossess bodies and observer Find/Remove.
// World movement, SetOwner, type lookup, interaction and camera are fixtures;
// this is an ABI/state-cleanup test, NOT a game-session control-transfer test.
#include "NativeControllerDetach.cpp"
#include "NativePawnOwnership.cpp"
#include "NativePlayerAdoption.cpp"
#include "NativeAiAdoption.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>

using namespace wfcds;
namespace {
struct Object {
  alignas(16) uint8_t bytes[0xA00]{};
  const wchar_t* type;
  bool valid = true, deleting = false;
  uint32_t index = 1;
};
Object player{{}, L"TnPlayerController"}, ai{{}, L"TnAiController"},
    pawn{{}, L"TnPlayerPawn"}, buddy{{}, L"TnAiPawn"}, world{{}, L"WorldInfo"},
    foreign_world{{}, L"WorldInfo"}, analyzer{{}, L"TnTacticalAnalyzer"},
    pri{{}, L"PlayerReplicationInfo"}, saved{{}, L"SavedMove"},
    camera{{}, L"Camera"},
    pc_class{{}, L"Class"}, ai_class{{}, L"Class"},
    inv1{{}, L"TnInventoryManager"}, inv2{{}, L"TnInventoryManager"},
    weapon1{{}, L"TnWeapon"}, weapon2{{}, L"TnWeapon"},
    active_state{{}, L"State"}, inactive_state{{}, L"State"}, default_pawn{{}, L"TnPawn"},
    local_player{{}, L"LocalPlayer"}, local_class{{}, L"Class"}, blueprint{{}, L"TnTransformer"},
    icon{{},L"HmPawnIconDrawer"}, aimer{{},L"TnAiWeaponAimer"}, shooter{{},L"TnAiWeaponShooter"},
    detector{{},L"TnAiRangeDetector"}, runner{{},L"HmGoapPlanRunner"}, range_struct{{},L"ScriptStruct"};
Object* objects[] = {&player,&ai,&pawn,&buddy,&world,&foreign_world,&analyzer,&pri,&saved,&camera,&pc_class,&ai_class,
                    &inv1,&inv2,&weapon1,&weapon2,&active_state,&inactive_state,&default_pawn,
                    &local_player,&local_class,&blueprint,&icon,&aimer,&shooter,&detector,&runner,&range_struct};
uint8_t* image = nullptr;
alignas(16) UnrealScriptDelegate observer_storage[4096];
alignas(16) float configured_ranges[16]{}, detector_ranges[16]{};
bool plan_ready = true, shooter_ready = true, range_shape_wrong = false, range_type_wrong = false;
bool reject_ai_cast = false, reenter_ai_adoption = false, lose_plan = false, change_aimer = false;
bool fail_icon_write = false, fail_shooter_write = false, corrupt_append = false;
bool change_camera_index = false, lose_player_pair = false;
unsigned observer_resizes = 0;
void* view_target = nullptr;
bool wrong_offset = false, wrong_array = false, corrupt_owner = false, reenter = false;
bool change_world = false, corrupt_observer = false, change_index = false;
bool destroy_camera = false, change_mode = false;
unsigned owner_calls = 0, movement_calls = 0, camera_calls = 0, interact_calls = 0;
constexpr uintptr_t kFixtureVtable = 0x12620000u;
constexpr uintptr_t kFixtureViewGetter = 0x11000080u;
constexpr uintptr_t kAiName = 0x12B31C68u;
constexpr uint32_t kObserverName = 0x123400u;
alignas(16) uint8_t weapon_frames[2][0x80]{}, engine_fixture[0x80]{}, context_fixture[0x40]{};
void* fixture_world = nullptr;
NativePlayerInputLease move_lease,look_lease;
DWORD fixture_thread = 0;
bool fail_publication = false, lose_input = false, reject_local_cast = false, reenter_adoption = false;
Object* Lookup(void* p) { for (auto* o : objects) if (p == o->bytes && o->valid) return o; return nullptr; }
uint32_t Read(const void* p, size_t offset) {
  uint32_t value = 0; std::memcpy(&value, static_cast<const uint8_t*>(p) + offset, 4); return value;
}
void Put(void* p, size_t offset, uintptr_t value) {
  const auto v = static_cast<uint32_t>(value); std::memcpy(static_cast<uint8_t*>(p) + offset, &v, 4);
}
uint8_t* At(uintptr_t va) { return image + va - native_reference_code::kImageBase; }
void Link(Object& o, size_t offset, Object* v) { Put(o.bytes, offset, reinterpret_cast<uintptr_t>(v ? v->bytes : nullptr)); }
bool IsPawn(const Object* o) { return o == &pawn || o == &buddy; }
bool IsController(const Object* o) { return o == &player || o == &ai; }
bool Field(void* p, const wchar_t* name, uint32_t* out) {
  const auto* o = Lookup(p); if (!o) return false;
  const auto match = [&](const wchar_t* n, uint32_t offset) {
    if (std::wcscmp(n, name)) return false;
    *out = offset; return true;
  };
  if ((IsPawn(o) || IsController(o) || o == &camera || o == &weapon1 || o == &weapon2) && match(L"WorldInfo", 0x98)) return true;
  if (IsController(o) && match(L"Pawn", 0x1C8)) return true;
  if (IsController(o) && match(L"PlayerReplicationInfo", 0x1CC)) return true;
  if (IsPawn(o) && (match(L"Controller",0x1D4) || match(L"PlayerReplicationInfo",0x3C0) ||
      match(L"DrivenVehicle",0x404) || match(L"Owner",0x2C) || match(L"UseableInteract",0x640) ||
      match(L"_BoundController",0x87C) || match(L"InvManager",0x424) || match(L"Weapon",0x428) ||
      match(L"_Blueprint",0x578))) return true;
  if ((o == &inv1 || o == &inv2 || o == &weapon1 || o == &weapon2) && match(L"Instigator",0x94)) return true;
  if ((o == &inv1 || o == &inv2) && match(L"PendingWeapon",0x1CC)) return true;
  if ((o == &weapon1 || o == &weapon2) && match(L"InvManager",0x1CC)) return true;
  if (o == &player && (match(L"SavedMoves",0x348) || match(L"PendingMove",0x350) ||
      match(L"UseableInteract",0x62C) || match(L"PlayerCamera",0x2D4) || match(L"Player",0x2D0) ||
      match(L"AcknowledgedPawn",0x300))) return true;
  if (o == &ai && (match(L"TacticalAnalyzer",0x2F4) || match(L"_IconDrawer",0x3C4) ||
      match(L"WeaponAimer",0x2FC) || match(L"WeaponShooter",0x2F8) || match(L"_RangeDetector",0x458) ||
      match(L"PlanRunner",0x304) || match(L"_DebugManager",0x3F4))) return true;
  if (o == &icon && match(L"_OwnerPawn",0x28)) return true;
  if (o == &aimer && (match(L"_Owner",0x28) || match(L"_OwnerController",0x30))) return true;
  if (o == &shooter && match(L"Owner",0x2C)) return true;
  if (o == &runner && match(L"_Owner",0x30)) return true;
  if (o == &analyzer && match(L"Owner",0x28)) return true;
  return false;
}
void InstallJump(uintptr_t va, const void* function) {
  auto* code = At(va); code[0] = 0xE9;
  Put(code, 1, reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(code + 5));
}
void InstallRegisterStackShim(uintptr_t va, uint8_t push_register, const void* function) {
  // Convert ESI/EDI plus one stack argument to cdecl(receiver, argument), RET4.
  const uint8_t code[] = {0xFF,0x74,0x24,0x04,push_register,0xE8,0,0,0,0,0x83,0xC4,0x08,0xC2,0x04,0x00};
  std::memcpy(At(va), code, sizeof(code));
  Put(At(va), 6, reinterpret_cast<uintptr_t>(function) - reinterpret_cast<uintptr_t>(At(va) + 10));
}
void SetOwner(void* receiver, void* owner) {
  assert(IsPawn(Lookup(receiver)) && (!owner || IsController(Lookup(owner)))); ++owner_calls;
  if (reenter && !owner) {
    assert(NativeDetachPlayer(player.bytes, pawn.bytes) == NativeDetachResult::kRejected);
    assert(NativeDetachAi(ai.bytes, buddy.bytes) == NativeDetachResult::kRejected);
  }
  if (reenter && owner) assert(NativeBindPawnOwnership(receiver,owner) == NativePawnOwnershipResult::kRejected);
  if (reenter_adoption && owner)
    assert(NativeAdoptExistingPlayerPawn(owner,receiver,move_lease,look_lease) == NativePlayerAdoptionResult::kRejected);
  if (reenter_ai_adoption && owner == ai.bytes)
    assert(NativeAdoptExistingAiPawn(owner,receiver,receiver == pawn.bytes ? buddy.bytes : pawn.bytes,
        move_lease,look_lease) == NativeAiAdoptionResult::kRejected);
  if (!corrupt_owner) Put(receiver, 0x2C, reinterpret_cast<uintptr_t>(owner));
  if (change_world) Put(receiver, 0x98, reinterpret_cast<uintptr_t>(foreign_world.bytes));
  if (change_index) ++Lookup(receiver)->index;
  if (destroy_camera) camera.deleting = true;
  if (change_mode) world.bytes[0x39C] = 1;
  if (corrupt_observer) observer_storage[0].compiled_context = nullptr;
  if (lose_plan) plan_ready = false;
  if (change_aimer) ++aimer.index;
  if (change_camera_index) ++camera.index;
  if (lose_player_pair) Link(player,0x1C8,nullptr);
}
void __attribute__((regparm(1), stdcall)) SetRotation(void* controller,const void* rotation) {
  assert(IsController(Lookup(controller))); std::memcpy(static_cast<uint8_t*>(controller)+0xE4,rotation,12);
}
void* PawnDefault(void* receiver,uint32_t size) {
  assert(IsPawn(Lookup(receiver)) && size == 0x488); return default_pawn.bytes;
}
void ResizeObserver(void* array, uint32_t count) {
  // Engine allocator fixture; native Append/Remove owns count/capacity and
  // passes the OLD live count. Preserve all existing delegate words on grow.
  auto* a = static_cast<UnrealArrayView*>(array);
  assert(array == analyzer.bytes+0x34 && count <= 4096 && a->capacity <= 4096);
  a->data = a->capacity ? observer_storage : nullptr;
  ++observer_resizes;
  if (corrupt_append && count) observer_storage[0].compiled_function = nullptr;
}
void* __attribute__((fastcall)) Move(void*, void*, void* controller, void* location,
                                    uint32_t a, uint32_t b, uint32_t c) {
  assert(controller == player.bytes && !a && !b && !c); ++movement_calls;
  std::memcpy(player.bytes + 0xD8, location, 12); return controller;
}
void* __attribute__((fastcall)) GetView(void* receiver, void*) {
  assert(receiver == player.bytes); return view_target;
}
void __attribute__((regparm(3), stdcall)) SetView(void* transition, void*, void* target, void* receiver) {
  // ECX is target, stack is raw player controller. UnPossess passes self for
  // BOTH, so this fixture alone cannot prove different-object argument order.
  assert(receiver == player.bytes && target == player.bytes);
  assert(Read(transition,0) == 0 && static_cast<uint8_t*>(transition)[4] == 1 && Read(transition,8) == 0x40000000u);
  ++camera_calls; view_target = target;
}
void* __cdecl GetClass(void* cache, const void*) {
  if (cache == At(0x12B6D684u)) return local_class.bytes + 0x10;
  return cache == At(0x12B6ED40u) ? ai_class.bytes + 0x10 : pc_class.bytes + 0x10;
}
void* __attribute__((fastcall)) Cast(void* object, void*, void* cls) {
  if (cls == local_class.bytes && object == local_player.bytes) {
    if (lose_input) move_lease.invalidated = true;
    return reject_local_cast ? nullptr : object;
  }
  if (cls == ai_class.bytes && object == ai.bytes && reject_ai_cast) return nullptr;
  if ((cls == ai_class.bytes && object == ai.bytes) || (cls == pc_class.bytes && object == player.bytes)) return object;
  return nullptr;
}
void* __attribute__((regparm(1))) GetPlayer(void* field) {
  assert(Read(field,0) == reinterpret_cast<uintptr_t>(player.bytes)); return player.bytes + 0x10;
}
bool __attribute__((regparm(1))) GetAiInitialized(void* field) {
  assert(Read(field,0) == reinterpret_cast<uintptr_t>(ai.bytes)); return true;
}
void __attribute__((fastcall)) SetInteract(void* script_pawn, void*, void* interact, uint32_t state) {
  assert(script_pawn == pawn.bytes + 0x10 && interact == reinterpret_cast<void*>(0x10) && !state);
  assert(!Read(pawn.bytes,0x640)); ++interact_calls;
}
void __attribute__((regparm(1))) ClearPlayerInteract(void* script_player) {
  assert(script_player == player.bytes + 0x10 && !player.bytes[0x630]);
}
void Reset(bool cross_role = false, int observer_count = 3, int own = 1) {
  for (auto* o : objects) { std::memset(o->bytes,0,sizeof(o->bytes)); o->valid = true; o->deleting = false; o->index = 1; }
  Object* player_pawn = cross_role ? &buddy : &pawn;
  Object* ai_pawn = cross_role ? &pawn : &buddy;
  Link(player,0x1C8,player_pawn); Link(ai,0x1C8,ai_pawn);
  Link(*player_pawn,0x1D4,&player); Link(*ai_pawn,0x1D4,&ai);
  Link(*player_pawn,0x2C,&player); Link(*ai_pawn,0x2C,&ai);
  for (auto* o : {&player,&ai,&pawn,&buddy}) { Link(*o,0x98,&world); o->bytes[0x66] = 3; }
  Link(pawn,0x3C0,&pri); Link(buddy,0x3C0,&pri);
  Link(player,0x348,&saved); Link(player,0x350,&saved);
  Link(player,0x1CC,&pri); Link(ai,0x1CC,&pri);
  Link(player,0x2D4,&camera); Link(camera,0x98,&world);
  Link(player,0x2D0,&local_player); Link(player,0x300,player_pawn);
  Link(ai,0x2F4,&analyzer); Link(analyzer,0x28,&ai);
  Link(ai,0x3C4,&icon); Link(ai,0x2FC,&aimer); Link(ai,0x2F8,&shooter);
  Link(ai,0x458,&detector); Link(ai,0x304,&runner); Link(runner,0x30,&ai);
  Link(icon,0x28,ai_pawn); Link(aimer,0x28,ai_pawn); Link(aimer,0x30,&ai); Link(shooter,0x2C,ai_pawn);
  for (size_t i = 0; i < 16; ++i) configured_ranges[i] = detector_ranges[i] = static_cast<float>(i*100);
  const UnrealArrayView source_range{configured_ranges,8,8}, target_range{detector_ranges,8,8};
  std::memcpy(ai.bytes+0x360,&source_range,sizeof(source_range));
  std::memcpy(detector.bytes+0x28,&target_range,sizeof(target_range));
  Put(player.bytes,0,reinterpret_cast<uintptr_t>(At(kFixtureVtable)));
  Put(pawn.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x126A5FB0u)));
  Put(buddy.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x12688680u)));
  pawn.bytes[0x65] = buddy.bytes[0x65] = 2;
  // Arbitrary live health/ammo/form/location bytes untouched by native detach.
  for (auto* o : {&pawn,&buddy}) {
    for (size_t i = 0xD8; i < 0x108; ++i) o->bytes[i] = static_cast<uint8_t>(i * 7);
    for (size_t i = 0x500; i < 0x600; ++i) o->bytes[i] = static_cast<uint8_t>(i * 3);
  }
  for (int i = 0; i < observer_count; ++i) {
    observer_storage[i] = {analyzer.bytes,static_cast<uint32_t>(kObserverName+i+1),analyzer.bytes+0x10,At(0x10BB84E0u)};
    if (i == own) observer_storage[i] = {ai.bytes,kObserverName,ai.bytes+0x10,At(0x10BB84E0u)};
  }
  UnrealArrayView observers{observer_count ? observer_storage : nullptr,observer_count,observer_count};
  std::memcpy(analyzer.bytes+0x34,&observers,sizeof(observers));
  Put(At(kAiName),0,kObserverName); Put(At(0x12B1047C),0,0); Put(At(0x12B10474),0,0);
  Link(pawn,0x424,&inv1); Link(buddy,0x424,&inv2);
  Link(pawn,0x428,&weapon1); Link(buddy,0x428,&weapon2);
  Link(inv1,0x94,&pawn); Link(inv2,0x94,&buddy);
  Link(weapon1,0x94,&pawn); Link(weapon2,0x94,&buddy);
  Link(weapon1,0x1CC,&inv1); Link(weapon2,0x1CC,&inv2);
  Link(weapon1,0x98,&world); Link(weapon2,0x98,&world);
  weapon1.bytes[0x66] = weapon2.bytes[0x66] = 3;
  Put(inv1.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x128266B8u)));
  Put(inv2.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x128266B8u)));
  Put(weapon1.bytes,0x28,reinterpret_cast<uintptr_t>(weapon_frames[0]));
  Put(weapon2.bytes,0x28,reinterpret_cast<uintptr_t>(weapon_frames[1]));
  Put(weapon_frames[0],0x38,reinterpret_cast<uintptr_t>(active_state.bytes));
  Put(weapon_frames[1],0x38,reinterpret_cast<uintptr_t>(active_state.bytes));
  Put(active_state.bytes,4,0x423400u); Put(inactive_state.bytes,4,0x523400u);
  Put(At(0x12B3122Cu),0,0x523400u); default_pawn.bytes[0x65] = 2;
  Link(pawn,0x578,&blueprint); Link(buddy,0x578,&blueprint);
  Put(blueprint.bytes,0xB4,0x42C80000u); // Blueprint eye height = 100.
  Put(pawn.bytes,0x1F4,0x42480000u); Put(buddy.bytes,0x1F4,0x42480000u); // Crouch height = 50.
  Put(world.bytes,0x338,0x42F60000u); // 123 seconds.
  Put(inv1.bytes,0x200,0x41400000u); Put(inv2.bytes,0x200,0x41400000u); // 12 seconds.
  Put(At(0x12B10A9Cu),0,reinterpret_cast<uintptr_t>(engine_fixture));
  Put(engine_fixture,0x40,reinterpret_cast<uintptr_t>(context_fixture));
  Put(context_fixture,0x28,reinterpret_cast<uintptr_t>(&fixture_world)); fixture_world = world.bytes;
  view_target = player_pawn->bytes;
  wrong_offset = wrong_array = corrupt_owner = reenter = change_world = corrupt_observer = change_index = false;
  destroy_camera = change_mode = false;
  fail_publication = lose_input = reject_local_cast = reenter_adoption = false;
  plan_ready = shooter_ready = true;
  range_shape_wrong = range_type_wrong = reject_ai_cast = reenter_ai_adoption = lose_plan = change_aimer = false;
  fail_icon_write = fail_shooter_write = corrupt_append = false; observer_resizes = 0;
  change_camera_index = lose_player_pair = false;
  move_lease = {player.bytes,world.bytes,1,1,0,NativeInputAxis::kMove,true};
  look_lease = {player.bytes,world.bytes,1,1,0,NativeInputAxis::kLook,true};
  player.bytes[0x305] = player.bytes[0x306] = 1;
  owner_calls = movement_calls = camera_calls = interact_calls = 0;
}
void Rejected(bool player_side) {
  const Object old_player = player, old_ai = ai, old_pawn = pawn, old_buddy = buddy, old_analyzer = analyzer;
  const unsigned before = owner_calls;
  void* target = reinterpret_cast<void*>(Read(player_side ? player.bytes : ai.bytes,0x1C8));
  assert((player_side ? NativeDetachPlayer(player.bytes,target) : NativeDetachAi(ai.bytes,target)) == NativeDetachResult::kRejected);
  assert(!std::memcmp(player.bytes,old_player.bytes,sizeof(player.bytes)) &&
         !std::memcmp(ai.bytes,old_ai.bytes,sizeof(ai.bytes)) &&
         !std::memcmp(pawn.bytes,old_pawn.bytes,sizeof(pawn.bytes)) &&
         !std::memcmp(buddy.bytes,old_buddy.bytes,sizeof(buddy.bytes)) &&
         !std::memcmp(analyzer.bytes,old_analyzer.bytes,sizeof(analyzer.bytes)) && owner_calls == before);
}
DWORD WINAPI WrongThread(void*) { Rejected(true); Rejected(false); return 0; }
void OwnershipRejected(void* target,void* controller) {
  const Object before_player = player, before_ai = ai, before_pawn = pawn, before_buddy = buddy,
      before_weapon1 = weapon1, before_weapon2 = weapon2, before_inv1 = inv1, before_inv2 = inv2;
  const auto calls = owner_calls;
  assert(NativeBindPawnOwnership(target,controller) == NativePawnOwnershipResult::kRejected);
  assert(owner_calls == calls && !std::memcmp(player.bytes,before_player.bytes,sizeof(player.bytes)) &&
      !std::memcmp(ai.bytes,before_ai.bytes,sizeof(ai.bytes)) &&
      !std::memcmp(pawn.bytes,before_pawn.bytes,sizeof(pawn.bytes)) &&
      !std::memcmp(buddy.bytes,before_buddy.bytes,sizeof(buddy.bytes)) &&
      !std::memcmp(weapon1.bytes,before_weapon1.bytes,sizeof(weapon1.bytes)) &&
      !std::memcmp(weapon2.bytes,before_weapon2.bytes,sizeof(weapon2.bytes)) &&
      !std::memcmp(inv1.bytes,before_inv1.bytes,sizeof(inv1.bytes)) &&
      !std::memcmp(inv2.bytes,before_inv2.bytes,sizeof(inv2.bytes)));
}
DWORD WINAPI WrongOwnershipThread(void*) {
  OwnershipRejected(buddy.bytes,player.bytes); OwnershipRejected(pawn.bytes,ai.bytes); return 0;
}
void AdoptionRejected() {
  const Object old_player = player, old_pawn = pawn, old_buddy = buddy;
  const auto calls = owner_calls;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kRejected);
  assert(!std::memcmp(player.bytes,old_player.bytes,sizeof(player.bytes)) &&
      !std::memcmp(pawn.bytes,old_pawn.bytes,sizeof(pawn.bytes)) &&
      !std::memcmp(buddy.bytes,old_buddy.bytes,sizeof(buddy.bytes)) && calls == owner_calls);
}
DWORD WINAPI WrongAdoptionThread(void*) { AdoptionRejected(); return 0; }
void AiAdoptionRejected() {
  std::vector<Object> copies; for (auto* object : objects) copies.push_back(*object);
  const unsigned calls = owner_calls, resizes = observer_resizes;
  const auto observers = std::vector<UnrealScriptDelegate>(std::begin(observer_storage),std::end(observer_storage));
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kRejected);
  for (size_t i = 0; i < copies.size(); ++i) assert(!std::memcmp(objects[i]->bytes,copies[i].bytes,sizeof(Object::bytes)));
  assert(calls == owner_calls && resizes == observer_resizes &&
      !std::memcmp(observers.data(),observer_storage,sizeof(observer_storage)));
}
DWORD WINAPI WrongAiAdoptionThread(void*) { AiAdoptionRejected(); return 0; }
void ResetDetached() {
  Reset();
  assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kApplied);
  assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kApplied);
}
void ResetAiDetached() {
  ResetDetached();
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kApplied);
}
void VerifyFile(const char* path) {
  std::ifstream file(path,std::ios::binary|std::ios::ate);
  assert(file && file.tellg() == 37667328);
  std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg())); file.seekg(0);
  assert(file.read(reinterpret_cast<char*>(bytes.data()),bytes.size()));
  assert(native_detach_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    return !std::memcmp(bytes.data()+va-0x10900C00u,code,sizeof(code));
  }));
  assert(native_pawn_ownership_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    return !std::memcmp(bytes.data()+va-0x10900C00u,code,sizeof(code));
  }));
  assert(native_player_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    return !std::memcmp(bytes.data()+va-0x10900C00u,code,sizeof(code));
  }));
  assert(native_ai_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    return !std::memcmp(bytes.data()+va-0x10900C00u,code,sizeof(code));
  }));
  for (auto pair : {std::pair<uintptr_t,uintptr_t>{0x126A5FB0u+0x224,native_detach_code::kPlayerPawnVa},
                   {0x12688680u+0x224,native_detach_code::kAiPawnVa}, {0x126A5FB0u+0x344,0x10BA1D60u}})
    assert(Read(bytes.data(),pair.first-0x11948000u+0x1046E00u) == pair.second);
  std::puts("Native detachment: 11 complete bodies and 3 pawn callback slots match TWFC.exe");
  assert(Read(bytes.data(),0x128266B8u+0x228-0x11948000u+0x1046E00u) == native_pawn_ownership_code::kInventoryVa);
  std::puts("Native pawn ownership: 7 complete callback/weapon/GC bodies and inventory slot match TWFC.exe");
  for (auto table : {0x126A5FB0u,0x12688680u}) {
    assert(Read(bytes.data(),table+0x204-0x11948000u+0x1046E00u) == native_player_adoption_code::kEyeHeightVa);
    assert(Read(bytes.data(),table+0x200-0x11948000u+0x1046E00u) == native_player_adoption_code::kOriginalEyeHeightVa);
  }
  std::puts("Native player adoption: 7 complete acknowledgment/eye-height/GC bodies and 4 virtual slots match TWFC.exe");
  std::puts("Native AI adoption: complete aimer, Find, Append, Resize and GC bodies match TWFC.exe");
}
}  // namespace

namespace wfcds {
bool UnrealObjectIsA(void* p,const wchar_t* type) {
  const auto* o = Lookup(p);
  return o && (!std::wcscmp(type,o->type) || (!std::wcscmp(type,L"TnPawn") && IsPawn(o)) ||
      (!std::wcscmp(type,L"TnWeapon") && (o == &weapon1 || o == &weapon2)) ||
      (!std::wcscmp(type,L"Controller") && IsController(o)));
}
bool UnrealResolveFloatPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  auto* o = Lookup(p); if (!o) return false;
  uint32_t offset = 0;
  if ((o == &weapon1 || o == &weapon2) && !std::wcscmp(name,L"ClientWeaponSetTime")) offset = 0x288;
  else if ((o == &inv1 || o == &inv2) && !std::wcscmp(name,L"LastClientWeaponSetTime")) offset = 0x200;
  else if (o == &world && !std::wcscmp(name,L"TimeSeconds")) offset = 0x338;
  else if (IsPawn(o) && !std::wcscmp(name,L"BaseEyeHeight")) offset = 0x32C;
  else if (IsPawn(o) && !std::wcscmp(name,L"EyeHeight")) offset = 0x330;
  else if (IsPawn(o) && !std::wcscmp(name,L"CrouchHeight")) offset = 0x1F4;
  else if (o == &blueprint && !std::wcscmp(name,L"BaseEyeHeight")) offset = 0xB4;
  else return false;
  *out = offset; return true;
}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out) {
  uint32_t offset = 0;
  if (!UnrealResolveFloatPropertyOffset(p,name,&offset)) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+offset,4); return true;
}
uint32_t UnrealObjectIndex(void* p) { const auto* o = Lookup(p); return o ? o->index : 0; }
bool UnrealReadBoolProperty(void* p,const wchar_t* name,bool* out) {
  auto* o = Lookup(p); if (!o) return false;
  if (!std::wcscmp(name,L"bDeleteMe")) *out = o->deleting;
  else if (o == &pri && !std::wcscmp(name,L"bOnlySpectator")) *out = (Read(p,0x204)&4) != 0;
  else if (IsPawn(o) && !std::wcscmp(name,L"bStasis")) *out = (Read(p,0x78)&0x200) != 0;
  else if (IsPawn(o) && !std::wcscmp(name,L"bTearOff")) *out = (Read(p,0x7C)&4) != 0;
  else if (IsPawn(o) && !std::wcscmp(name,L"bIsCrouched")) *out = (Read(p,0x1E8)&8) != 0;
  else return false;
  return true;
}
bool UnrealReadByteProperty(void* p,const wchar_t* name,uint8_t* out) {
  auto* o = Lookup(p); if (!o) return false;
  uint32_t offset;
  if (!std::wcscmp(name,L"Role")) offset = 0x66;
  else if (!std::wcscmp(name,L"RemoteRole")) offset = 0x65;
  else if (!std::wcscmp(name,L"NetMode")) offset = 0x39C;
  else if (!std::wcscmp(name,L"PlayerInteractState")) offset = 0x630;
  else return false;
  *out = o->bytes[offset]; return true;
}
bool UnrealResolveBytePropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  uint8_t value = 0;
  if (!UnrealReadByteProperty(p,name,&value)) return false;
  if (!std::wcscmp(name,L"Role")) *out = 0x66;
  else if (!std::wcscmp(name,L"RemoteRole")) *out = 0x65;
  else if (!std::wcscmp(name,L"NetMode")) *out = 0x39C;
  else *out = 0x630;
  if (wrong_offset) ++*out;
  return true;
}
bool UnrealResolveObjectPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if (!Field(p,name,out)) return false;
  if (wrong_offset) *out += 4;
  return true;
}
bool UnrealReadObjectProperty(void* p,const wchar_t* name,void** out) {
  uint32_t offset = 0; if (!Field(p,name,&offset)) return false;
  *out = reinterpret_cast<void*>(Read(p,offset)); return !*out || Lookup(*out);
}
bool UnrealReadDelegateArrayProperty(void* p,const wchar_t* name,UnrealArrayView* out,uint32_t* offset) {
  if (p != analyzer.bytes || !Lookup(p) || std::wcscmp(name,L"NewEnemyObservers") || wrong_array) return false;
  std::memcpy(out,analyzer.bytes+0x34,sizeof(*out)); if (offset) *offset = 0x34; return true;
}
bool UnrealObjectName(void* p,char* out,size_t size) {
  if (p != range_struct.bytes || !Lookup(p) || size < 16) return false;
  std::strcpy(out,range_type_wrong ? "NotFloatRange" : "FloatRange"); return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if (!Lookup(p)) return false;
  uint32_t offset = 0;
  if (p == ai.bytes && !std::wcscmp(name,L"RangeSet")) offset = 0x360;
  else if (p == detector.bytes && !std::wcscmp(name,L"Range")) offset = 0x28;
  else return false;
  UnrealArrayView raw; std::memcpy(&raw,static_cast<uint8_t*>(p)+offset,sizeof(raw));
  *out = {raw.data,raw.count,raw.capacity,range_shape_wrong ? 4u : 8u,range_struct.bytes}; return true;
}
// Actual planning/shooter readiness implementations are tested separately in
// NativeWeaponShooterTests. Here they inject quiescence/loss at exact callbacks.
bool NativeAiPlanHandoffReady(void* p) { return p == ai.bytes && Lookup(p) && plan_ready; }
bool NativeWeaponShooterHandoffReady(void* p) { return p == shooter.bytes && Lookup(p) && shooter_ready; }
bool UnrealObjectReferenceWritesReady() { return fixture_thread == GetCurrentThreadId(); }
bool UnrealWriteObjectPropertyExpected(void* p,const wchar_t* name,void* expected,void* value) {
  uint32_t offset = 0;
  if (fail_publication || (p == icon.bytes && fail_icon_write) || (p == shooter.bytes && fail_shooter_write) ||
      !UnrealObjectReferenceWritesReady() || !Field(p,name,&offset) ||
      Read(p,offset) != reinterpret_cast<uintptr_t>(expected)) return false;
  Put(p,offset,reinterpret_cast<uintptr_t>(value));
  reinterpret_cast<void (__cdecl *)(void*)>(At(native_reference_code::kBarrierVa))(value);
  return true;
}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease) {
  return (&lease == &move_lease || &lease == &look_lease) && lease.held && !lease.invalidated &&
      lease.controller == player.bytes && lease.world == world.bytes &&
      GetCurrentThreadId() == fixture_thread &&
      player.bytes[lease.axis == NativeInputAxis::kMove ? 0x305 : 0x306] == lease.baseline+1;
}
}  // namespace wfcds

int main(int argc,char** argv) {
  fixture_thread = GetCurrentThreadId();
  if (argc == 2) VerifyFile(argv[1]);
  image = static_cast<uint8_t*>(VirtualAlloc(nullptr,0x2300000,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE)); assert(image);
  const uintptr_t base = reinterpret_cast<uintptr_t>(image);
  assert(native_detach_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated = native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  }));
  assert(native_pawn_ownership_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated = native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  }));
  assert(native_player_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated = native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  }));
  assert(native_ai_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto relocated = native_reference_code::Relocate(code,base,operands);
    std::memcpy(At(va),relocated.data(),relocated.size()); return true;
  }));
  const double ten = 10.0, factor = static_cast<double>(0.8f);
  std::memcpy(At(0x129D2300u),&ten,8); std::memcpy(At(0x129D2560u),&factor,8);
  for (auto table : {0x126A5FB0u,0x12688680u}) {
    Put(At(table),0x204,reinterpret_cast<uintptr_t>(At(native_player_adoption_code::kEyeHeightVa)));
    Put(At(table),0x200,reinterpret_cast<uintptr_t>(At(native_player_adoption_code::kOriginalEyeHeightVa)));
  }
  Put(At(0x126269A4u),0,0x40000000u); Put(At(0x1296CD1Cu),0,0x40A00000u);
  Put(At(kFixtureVtable),0x368,reinterpret_cast<uintptr_t>(At(kFixtureViewGetter)));
  Put(At(0x126A5FB0u),0x224,reinterpret_cast<uintptr_t>(At(native_detach_code::kPlayerPawnVa)));
  Put(At(0x126A5FB0u),0x344,reinterpret_cast<uintptr_t>(At(0x10BA1D60u)));
  Put(At(0x12688680u),0x224,reinterpret_cast<uintptr_t>(At(native_detach_code::kAiPawnVa)));
  Put(At(0x126A5FB0u),0x220,reinterpret_cast<uintptr_t>(At(native_pawn_ownership_code::kPawnVa)));
  Put(At(0x12688680u),0x220,reinterpret_cast<uintptr_t>(At(native_pawn_ownership_code::kAiPawnVa)));
  Put(At(0x128266B8u),0x228,reinterpret_cast<uintptr_t>(At(native_pawn_ownership_code::kInventoryVa)));
  Put(At(0x12678864u),0,0x40400000u); Put(At(0x1259FBD4u),0,0x42C80000u);
  InstallRegisterStackShim(0x11506E50u,0x56,reinterpret_cast<void*>(&SetOwner));
  InstallRegisterStackShim(0x116EF210u,0x57,reinterpret_cast<void*>(&PawnDefault));
  InstallJump(0x114C18A0u,reinterpret_cast<void*>(&SetRotation));
  InstallJump(0x11689630u,reinterpret_cast<void*>(&Move));
  At(0x114FA550u)[0] = 0xB8; Put(At(0x114FA550u),1,reinterpret_cast<uintptr_t>(world.bytes)); At(0x114FA550u)[5] = 0xC3;
  InstallJump(kFixtureViewGetter,reinterpret_cast<void*>(&GetView));
  InstallJump(0x11688530u,reinterpret_cast<void*>(&SetView));
  InstallJump(0x110031A0u,reinterpret_cast<void*>(&GetClass));
  InstallJump(0x10901430u,reinterpret_cast<void*>(&Cast));
  InstallJump(0x1097FBD0u,reinterpret_cast<void*>(&GetPlayer));
  InstallJump(0x1097F890u,reinterpret_cast<void*>(&GetAiInitialized));
  InstallJump(0x10BA1D60u,reinterpret_cast<void*>(&SetInteract));
  InstallJump(0x10B1B480u,reinterpret_cast<void*>(&ClearPlayerInteract));
  Put(At(0x11948544u),0,reinterpret_cast<uintptr_t>(&std::memmove));
  DWORD old = 0; assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READWRITE,&old));
  FlushInstructionCache(GetCurrentProcess(),image,0x1E00000);
  DetachBindings bound;
  auto module = reinterpret_cast<HMODULE>(image);
  assert(BindDetachImage(module,GetCurrentThreadId(),&bound));
  PawnOwnershipBindings ownership_bound;
  assert(BindPawnOwnershipImage(module,GetCurrentThreadId(),&ownership_bound));
  PlayerAdoptionBindings adoption_bound;
  assert(BindPlayerAdoptionImage(module,GetCurrentThreadId(),&adoption_bound));
  AiAdoptionBindings ai_adoption_bound;
  assert(BindAiAdoptionImage(module,GetCurrentThreadId(),&ai_adoption_bound));
  assert(native_ai_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i = 0; i < sizeof(code); ++i) {
      At(va)[i] ^= 1; AiAdoptionBindings rejected;
      assert(!BindAiAdoptionImage(module,GetCurrentThreadId(),&rejected) && !rejected.module); At(va)[i] ^= 1;
    }
    return true;
  }));
  InstallRegisterStackShim(0x10B83FA0u,0x57,reinterpret_cast<void*>(&ResizeObserver));
  assert(native_player_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i = 0; i < sizeof(code); ++i) {
      At(va)[i] ^= 1; PlayerAdoptionBindings rejected;
      assert(!BindPlayerAdoptionImage(module,GetCurrentThreadId(),&rejected) && !rejected.module); At(va)[i] ^= 1;
    }
    return true;
  }));
  for (uintptr_t va : {0x129D2300u,0x129D2560u}) {
    At(va)[0] ^= 1; PlayerAdoptionBindings rejected;
    assert(!BindPlayerAdoptionImage(module,GetCurrentThreadId(),&rejected)); At(va)[0] ^= 1;
  }
  // Any accidental entry into reset/restart/spawn is an immediate test fault.
  for (uintptr_t va : {0x11781640u,0x117934E0u,0x11797B80u,0x117960C0u,0x10BB4F90u,0x117DEB70u}) {
    At(va)[0] = 0x0F; At(va)[1] = 0x0B; // UD2
  }
  assert(native_pawn_ownership_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i = 0; i < sizeof(code); ++i) {
      At(va)[i] ^= 1; PawnOwnershipBindings rejected;
      assert(!BindPawnOwnershipImage(module,GetCurrentThreadId(),&rejected) && !rejected.module); At(va)[i] ^= 1;
    }
    return true;
  }));
  assert(native_detach_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i = 0; i < sizeof(code); ++i) {
      At(va)[i] ^= 1; DetachBindings rejected;
      assert(!BindDetachImage(module,GetCurrentThreadId(),&rejected) && !rejected.module); At(va)[i] ^= 1;
    }
    return true;
  }));
  assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READ,&old));
  FlushInstructionCache(GetCurrentProcess(),image,0x1E00000);
  g_detach = bound;
  g_pawn_ownership = ownership_bound;
  g_player_adoption = adoption_bound;
  g_ai_adoption = ai_adoption_bound;
  for (unsigned n = 0; n < 6000; ++n) {
    const bool cross = n & 1;
    const int count = n % 9, own = count ? n % count : -1;
    Reset(cross,count,own); reenter = true;
    Object* pp = cross ? &buddy : &pawn;
    Object* ap = cross ? &pawn : &buddy;
    Object old_pawn = pawn, old_buddy = buddy;
    if (n & 2) view_target = world.bytes; // Respect an unrelated camera target.
    assert(NativeDetachPlayer(player.bytes,pp->bytes) == NativeDetachResult::kApplied);
    assert(NativeDetachAi(ai.bytes,ap->bytes) == NativeDetachResult::kApplied);
    assert(owner_calls == 2 && movement_calls == 1 && camera_calls == ((n & 2) ? 0u : 1u) && interact_calls == 1);
    assert(view_target == ((n & 2) ? world.bytes : player.bytes));
    // Detachment does not restart, reset movement/input locks, copy character
    // data, or change any retained live pose/form/inventory bytes.
    for (auto pair : {std::pair<const Object*,const Object*>{&pawn,&old_pawn},{&buddy,&old_buddy}}) {
      assert(!std::memcmp(pair.first->bytes+0xD8,pair.second->bytes+0xD8,0x30));
      assert(!std::memcmp(pair.first->bytes+0x500,pair.second->bytes+0x500,0x100));
    }
    assert(!Read(player.bytes,0x1C8) && !Read(ai.bytes,0x1C8) && !Read(pawn.bytes,0x1D4) && !Read(buddy.bytes,0x1D4));
    assert(NativeDetachPlayer(player.bytes,pp->bytes) == NativeDetachResult::kRejected);
    assert(NativeDetachAi(ai.bytes,ap->bytes) == NativeDetachResult::kRejected);
  }
  Reset(false,3,-1); assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kApplied);
  Reset(); wrong_offset = true; Rejected(true); Rejected(false);
  Reset(); wrong_array = true; Rejected(false);
  Reset(); world.bytes[0x39C] = 1; Rejected(true); Rejected(false);
  Reset(); player.bytes[0x66] = 1; Rejected(true);
  Reset(); buddy.bytes[0x66] = 1; Rejected(false);
  Reset(); pawn.deleting = true; Rejected(true);
  Reset(); buddy.valid = false; Rejected(false);
  Reset(); Link(pawn,0x98,&foreign_world); Rejected(true);
  Reset(); Link(buddy,0x1D4,&player); Rejected(false);
  Reset(); Link(pawn,0x2C,&ai); Rejected(true);
  Reset(); Link(pawn,0x404,&buddy); Rejected(true);
  Reset(); Link(pawn,0x640,&buddy); Rejected(true);
  Reset(); player.bytes[0x630] = 1; Rejected(true);
  Reset(); Link(player,0x62C,&buddy); Rejected(true);
  Reset(); Link(player,0x2D4,nullptr); Rejected(true);
  Reset(); camera.deleting = true; Rejected(true);
  Reset(); Link(camera,0x98,&foreign_world); Rejected(true);
  Reset(); Link(analyzer,0x28,&player); Rejected(false);
  Reset(); observer_storage[1].compiled_context = nullptr; Rejected(false);
  Reset(); observer_storage[1].compiled_function = nullptr; Rejected(false);
  Reset(); observer_storage[2] = observer_storage[1]; Rejected(false);
  Reset(); Put(analyzer.bytes,0x38,257); Put(analyzer.bytes,0x3C,257); Rejected(false);
  Reset(); Put(analyzer.bytes,0x38,static_cast<uint32_t>(-1)); Rejected(false);
  Reset(); Put(analyzer.bytes,0x3C,0); Rejected(false);
  Reset();
  auto* protected_observers = VirtualAlloc(nullptr,0x1000,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
  assert(protected_observers);
  std::memcpy(protected_observers,observer_storage,3*sizeof(UnrealScriptDelegate));
  Put(analyzer.bytes,0x34,reinterpret_cast<uintptr_t>(protected_observers));
  assert(VirtualProtect(protected_observers,0x1000,PAGE_READONLY,&old)); Rejected(false);
  assert(VirtualFree(protected_observers,0,MEM_RELEASE));
  Reset(); Put(pawn.bytes,0x10,0x10); Rejected(true);
  Reset(); Put(player.bytes,0,0x10); Rejected(true);
  Reset();
  auto* pawn_slot = At(0x126A5FB0u) + 0x224;
  assert(VirtualProtect(pawn_slot,4,PAGE_READWRITE,&old));
  const uint32_t original_slot = Read(pawn_slot,0);
  Put(pawn_slot,0,reinterpret_cast<uintptr_t>(At(native_detach_code::kPawnVa)));
  Rejected(true); // A base callback cannot silently replace the player override.
  Put(pawn_slot,0,original_slot);
  assert(VirtualProtect(pawn_slot,4,old,&old));
  Reset(); HANDLE thread = CreateThread(nullptr,0,WrongThread,nullptr,0,nullptr);
  assert(thread && WaitForSingleObject(thread,INFINITE) == WAIT_OBJECT_0); CloseHandle(thread);
  Reset(); corrupt_owner = true;
  assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); change_world = true;
  assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); change_index = true;
  assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); destroy_camera = true;
  assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); change_mode = true;
  assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); corrupt_observer = true;
  assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kStateMismatch && owner_calls == 1);
  Reset(); player.bytes[0x305] = 3; player.bytes[0x306] = 4;
  assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kApplied);
  assert(player.bytes[0x305] == 3 && player.bytes[0x306] == 4);
  assert(!PrepareNativeControllerDetach(module,GetCurrentThreadId(),player.bytes,ai.bytes)); // Not the process executable.
  // Exercise the pawn half of custom adoption between the two real physical
  // layouts. The fixture alone publishes the reciprocal Controller.Pawn link;
  // production admission still requires the unfinished service/role handoff.
  for (unsigned n = 0; n < 3000; ++n) {
    Reset(); reenter = true;
    const bool cross = n & 1;
    Object* pp = cross ? &buddy : &pawn;
    Object* ap = cross ? &pawn : &buddy;
    if (n & 2) { Link(pawn,0x428,nullptr); Link(buddy,0x428,nullptr); }
    if (n & 4) Link(ai,0x1CC,nullptr);
    assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kApplied);
    assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kApplied);
    std::array<uint8_t,0x100> ammo_sentinel{};
    for (size_t i = 0; i < ammo_sentinel.size(); ++i) ammo_sentinel[i] = static_cast<uint8_t>(n+i);
    std::memcpy(weapon1.bytes+0x300,ammo_sentinel.data(),ammo_sentinel.size());
    std::memcpy(weapon2.bytes+0x300,ammo_sentinel.data(),ammo_sentinel.size());
    player.bytes[0x305] = 2; player.bytes[0x306] = 3;
    assert(NativeBindPawnOwnership(pp->bytes,player.bytes) == NativePawnOwnershipResult::kApplied);
    assert(NativeBindPawnOwnership(ap->bytes,ai.bytes) == NativePawnOwnershipResult::kApplied);
    assert(Read(pp->bytes,0x1D4) == reinterpret_cast<uintptr_t>(player.bytes));
    assert(Read(ap->bytes,0x1D4) == reinterpret_cast<uintptr_t>(ai.bytes));
    assert(!Read(player.bytes,0x1C8) && !Read(ai.bytes,0x1C8));
    assert(player.bytes[0x305] == 2 && player.bytes[0x306] == 3);
    assert(!std::memcmp(weapon1.bytes+0x300,ammo_sentinel.data(),ammo_sentinel.size()));
    assert(!std::memcmp(weapon2.bytes+0x300,ammo_sentinel.data(),ammo_sentinel.size()));
    assert(NativeBindPawnOwnership(pp->bytes,player.bytes) == NativePawnOwnershipResult::kRejected);
    Link(player,0x1C8,pp); Link(ai,0x1C8,ap); // Test harness, not a production adoption implementation.
    assert(NativeDetachPlayer(player.bytes,pp->bytes) == NativeDetachResult::kApplied);
    assert(NativeDetachAi(ai.bytes,ap->bytes) == NativeDetachResult::kApplied);
  }
  Reset(); OwnershipRejected(buddy.bytes,player.bytes); // Existing controllers must first be detached.
  ResetDetached();
  Link(inv2,0x1CC,&weapon1); OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); weapon2.type = L"TnWeaponBombBase"; OwnershipRejected(buddy.bytes,player.bytes); weapon2.type = L"TnWeapon";
  ResetDetached(); weapon2.type = L"TnWeaponFlagBase"; OwnershipRejected(buddy.bytes,player.bytes); weapon2.type = L"TnWeapon";
  ResetDetached(); Put(inv2.bytes,0x200,0x7FC00000u); OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); Put(world.bytes,0x338,0x7FC00000u); OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); world.bytes[0x39C] = 1; OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); wrong_offset = true; OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); inv2.valid = false; OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); Put(weapon2.bytes,0x28,0x10); OwnershipRejected(buddy.bytes,player.bytes);
  ResetDetached(); Put(weapon2.bytes,0x28,0);
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kApplied);
  ResetDetached(); Put(weapon_frames[1],0x38,0);
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kApplied);
  ResetDetached();
  thread = CreateThread(nullptr,0,WrongOwnershipThread,nullptr,0,nullptr);
  assert(thread && WaitForSingleObject(thread,INFINITE) == WAIT_OBJECT_0); CloseHandle(thread);
  ResetDetached();
  Put(active_state.bytes,0x28,reinterpret_cast<uintptr_t>(inactive_state.bytes));
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kRejected);
  Put(active_state.bytes,0x28,reinterpret_cast<uintptr_t>(active_state.bytes));
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kRejected);
  Put(active_state.bytes,0x28,0);
  fixture_world = foreign_world.bytes;
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kRejected);
  fixture_world = world.bytes;
  Link(weapon2,0x1CC,&inv1);
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kRejected);
  Link(weapon2,0x1CC,&inv2); corrupt_owner = true;
  assert(NativeBindPawnOwnership(buddy.bytes,player.bytes) == NativePawnOwnershipResult::kStateMismatch);
  assert(Read(buddy.bytes,0x1D4) == reinterpret_cast<uintptr_t>(player.bytes)); // Mutation must be rolled back.
  // Execute custom reciprocal player adoption with native pawn callbacks and
  // native local/server acknowledgment. AI/service adaptation is NOT simulated
  // as complete. Input leases and shared reference writer are separate fixtures.
  for (unsigned n = 0; n < 3000; ++n) {
    ResetDetached(); reenter_adoption = true;
    Object* target = (n & 1) ? &buddy : &pawn;
    if (n & 2) target->bytes[0x1E8] |= 8;
    if (n & 4) Link(*target,0x428,nullptr);
    const Object old_target = *target, old_weapon1 = weapon1, old_weapon2 = weapon2;
    assert(NativeAdoptExistingPlayerPawn(player.bytes,target->bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kApplied);
    assert(Read(player.bytes,0x1C8) == reinterpret_cast<uintptr_t>(target->bytes));
    assert(Read(player.bytes,0x300) == reinterpret_cast<uintptr_t>(target->bytes));
    assert(Read(player.bytes,0x30) & 8);
    assert(Read(target->bytes,0x330) == ((n & 2) ? 0x42200000u : 0x42C80000u)); // 40 or 100.
    assert(!std::memcmp(target->bytes+0xD8,old_target.bytes+0xD8,0x30));
    assert(!std::memcmp(target->bytes+0x500,old_target.bytes+0x500,0x100));
    assert(!std::memcmp(weapon1.bytes+0x300,old_weapon1.bytes+0x300,0x100));
    assert(!std::memcmp(weapon2.bytes+0x300,old_weapon2.bytes+0x300,0x100));
    assert(player.bytes[0x305] == 1 && player.bytes[0x306] == 1);
    assert(NativeAdoptExistingPlayerPawn(player.bytes,target->bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kRejected);
    assert(NativeDetachPlayer(player.bytes,target->bytes) == NativeDetachResult::kApplied);
  }
  Reset(); AdoptionRejected(); // Still-possessed controller.
  ResetDetached(); player.bytes[0x305] = 0; AdoptionRejected();
  ResetDetached(); move_lease.invalidated = true; AdoptionRejected();
  ResetDetached();
  const NativePlayerInputLease copied_move = move_lease;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,copied_move,look_lease) == NativePlayerAdoptionResult::kRejected);
  ResetDetached(); look_lease.axis = NativeInputAxis::kMove; AdoptionRejected();
  ResetDetached(); Link(player,0x2D0,nullptr); AdoptionRejected();
  ResetDetached(); local_player.type = L"NetConnection"; AdoptionRejected(); local_player.type = L"LocalPlayer";
  ResetDetached(); Link(player,0x2D4,nullptr); AdoptionRejected();
  ResetDetached(); camera.deleting = true; AdoptionRejected();
  ResetDetached(); Link(camera,0x98,&foreign_world); AdoptionRejected();
  ResetDetached(); Link(player,0x1CC,nullptr); AdoptionRejected();
  ResetDetached(); Put(pri.bytes,0x204,4); AdoptionRejected();
  ResetDetached(); Put(buddy.bytes,0x78,0x200); AdoptionRejected();
  ResetDetached(); Put(buddy.bytes,0x7C,4); AdoptionRejected();
  ResetDetached(); Link(buddy,0x578,nullptr); AdoptionRejected();
  ResetDetached(); blueprint.valid = false; AdoptionRejected();
  ResetDetached(); Put(blueprint.bytes,0xB4,0x7FC00000u); AdoptionRejected();
  ResetDetached(); Put(buddy.bytes,0x1F4,0x7F800000u); AdoptionRejected();
  ResetDetached(); world.bytes[0x39C] = 3; AdoptionRejected();
  ResetDetached(); wrong_offset = true; AdoptionRejected();
  ResetDetached();
  auto* eye_slot = At(0x12688680u)+0x204;
  assert(VirtualProtect(eye_slot,4,PAGE_READWRITE,&old));
  const uint32_t original_eye_slot = Read(eye_slot,0);
  Put(eye_slot,0,reinterpret_cast<uintptr_t>(At(native_player_adoption_code::kOriginalEyeHeightVa)));
  AdoptionRejected(); Put(eye_slot,0,original_eye_slot);
  assert(VirtualProtect(eye_slot,4,old,&old));
  ResetDetached();
  thread = CreateThread(nullptr,0,WrongAdoptionThread,nullptr,0,nullptr);
  assert(thread && WaitForSingleObject(thread,5000) == WAIT_OBJECT_0); CloseHandle(thread);
  ResetDetached(); fail_publication = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(!Read(player.bytes,0x1C8) && Read(buddy.bytes,0x1D4) == reinterpret_cast<uintptr_t>(player.bytes));
  ResetDetached(); reject_local_cast = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(Read(player.bytes,0x1C8) == reinterpret_cast<uintptr_t>(buddy.bytes) && Read(player.bytes,0x300) != Read(player.bytes,0x1C8));
  ResetDetached(); lose_input = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(move_lease.invalidated && Read(player.bytes,0x1C8) == reinterpret_cast<uintptr_t>(buddy.bytes));
  ResetDetached(); change_index = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(!Read(player.bytes,0x1C8));
  ResetDetached(); destroy_camera = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(!Read(player.bytes,0x1C8)); // Native ownership callback invalidated camera before publication.
  ResetDetached(); change_world = true;
  assert(NativeAdoptExistingPlayerPawn(player.bytes,buddy.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kStateMismatch);
  assert(!Read(player.bytes,0x1C8));
  // Full reciprocal OWNERSHIP round trips on the same two actors, executing
  // shipping callbacks/aimer/delegate helpers. Role-specific gameplay services
  // and GOAP planning are not simulated as working by this isolated test.
  for (unsigned n = 0; n < 3000; ++n) {
    const int count = n % 9;
    Reset(false,count,count ? static_cast<int>(n % count) : -1);
    if (n & 1) Link(ai,0x1CC,nullptr);
    reenter_ai_adoption = true;
    assert(NativeDetachPlayer(player.bytes,pawn.bytes) == NativeDetachResult::kApplied);
    assert(NativeDetachAi(ai.bytes,buddy.bytes) == NativeDetachResult::kApplied);
    auto* header = reinterpret_cast<UnrealArrayView*>(analyzer.bytes+0x34);
    if (n & 2) header->capacity = header->count; // Exercise native grow/append.
    const Object original_pawn = pawn, original_buddy = buddy, original_weapon1 = weapon1, original_weapon2 = weapon2;
    for (bool cross : {true,false}) {
      Object* pp = cross ? &buddy : &pawn;
      Object* ap = cross ? &pawn : &buddy;
      assert(NativeAdoptExistingPlayerPawn(player.bytes,pp->bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kApplied);
      Put(ap->bytes,8,0x800000u); Put(ai.bytes,8,0x800000u); Put(At(0x12B10474u),0,0);
      assert(NativeAdoptExistingAiPawn(ai.bytes,ap->bytes,pp->bytes,move_lease,look_lease) == NativeAiAdoptionResult::kApplied);
      assert(Read(ai.bytes,0x1C8) == reinterpret_cast<uintptr_t>(ap->bytes));
      assert(Read(ap->bytes,0x1D4) == reinterpret_cast<uintptr_t>(ai.bytes));
      assert(Read(ai.bytes,0x30) & 8);
      assert(Read(At(0x12B10474u),0) == 1); // Actual native reference barrier.
      assert(Read(icon.bytes,0x28) == reinterpret_cast<uintptr_t>(ap->bytes));
      assert(Read(aimer.bytes,0x28) == reinterpret_cast<uintptr_t>(ap->bytes));
      assert(Read(aimer.bytes,0x30) == reinterpret_cast<uintptr_t>(ai.bytes));
      assert(Read(shooter.bytes,0x2C) == reinterpret_cast<uintptr_t>(ap->bytes));
      assert(!std::memcmp(pawn.bytes+0xD8,original_pawn.bytes+0xD8,0x30));
      assert(!std::memcmp(buddy.bytes+0xD8,original_buddy.bytes+0xD8,0x30));
      assert(!std::memcmp(pawn.bytes+0x500,original_pawn.bytes+0x500,0x100));
      assert(!std::memcmp(buddy.bytes+0x500,original_buddy.bytes+0x500,0x100));
      assert(!std::memcmp(weapon1.bytes+0x300,original_weapon1.bytes+0x300,0x100));
      assert(!std::memcmp(weapon2.bytes+0x300,original_weapon2.bytes+0x300,0x100));
      assert(!std::memcmp(configured_ranges,detector_ranges,64));
      assert(player.bytes[0x305] == 1 && player.bytes[0x306] == 1);
      assert(NativeAdoptExistingAiPawn(ai.bytes,ap->bytes,pp->bytes,move_lease,look_lease) == NativeAiAdoptionResult::kRejected);
      assert(NativeDetachAi(ai.bytes,ap->bytes) == NativeDetachResult::kApplied);
      assert(NativeDetachPlayer(player.bytes,pp->bytes) == NativeDetachResult::kApplied);
    }
  }
  // A correctly bound observer must not duplicate; identical UObject/FName
  // with a different compiled callback is corruption, not an AddUnique match.
  ResetAiDetached();
  auto* ai_observers = reinterpret_cast<UnrealArrayView*>(analyzer.bytes+0x34);
  observer_storage[ai_observers->count++] = {ai.bytes,kObserverName,ai.bytes+0x10,At(0x10BB84E0u)};
  const int old_count = ai_observers->count;
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kApplied);
  assert(ai_observers->count == old_count);
  ResetAiDetached();
  observer_storage[0] = {ai.bytes,kObserverName,ai.bytes+0x10,At(0x10BB84E0u)};
  observer_storage[1] = observer_storage[0]; AiAdoptionRejected();
  ResetAiDetached(); observer_storage[0] = {ai.bytes,kObserverName,ai.bytes,At(0x10BB84E0u)}; AiAdoptionRejected();
  ResetAiDetached();
  for (int i = 0; i < 256; ++i) observer_storage[i] = {analyzer.bytes,static_cast<uint32_t>(i+1),nullptr,nullptr};
  ai_observers->count = ai_observers->capacity = 256; AiAdoptionRejected();
  --ai_observers->count;
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kApplied);
  assert(ai_observers->count == 256 && observer_storage[255].object == ai.bytes);
  Reset(); AiAdoptionRejected();
  ResetDetached(); AiAdoptionRejected(); // Other pawn is not yet adopted/acknowledged by the player.
  ResetAiDetached(); Link(player,0x300,&pawn); AiAdoptionRejected();
  ResetAiDetached(); Link(buddy,0x2C,nullptr); AiAdoptionRejected();
  ResetAiDetached(); plan_ready = false; AiAdoptionRejected();
  ResetAiDetached(); shooter_ready = false; AiAdoptionRejected();
  ResetAiDetached(); move_lease.invalidated = true; AiAdoptionRejected();
  ResetAiDetached(); look_lease.axis = NativeInputAxis::kMove; AiAdoptionRejected();
  ResetAiDetached(); range_shape_wrong = true; AiAdoptionRejected();
  ResetAiDetached(); range_type_wrong = true; AiAdoptionRejected();
  ResetAiDetached(); detector_ranges[3] += 1; AiAdoptionRejected();
  ResetAiDetached(); Put(configured_ranges,0,0x7FC00000u); Put(detector_ranges,0,0x7FC00000u); AiAdoptionRejected();
  ResetAiDetached(); Put(ai.bytes,0x364,7); AiAdoptionRejected();
  ResetAiDetached(); Put(detector.bytes,0x28,0x10); AiAdoptionRejected();
  ResetAiDetached(); Link(shooter,0x2C,&world); AiAdoptionRejected();
  ResetAiDetached(); Link(aimer,0x30,&player); AiAdoptionRejected();
  ResetAiDetached(); Link(icon,0x28,nullptr); AiAdoptionRejected();
  ResetAiDetached(); Link(runner,0x30,&player); AiAdoptionRejected();
  ResetAiDetached(); Link(buddy,0x98,&foreign_world); AiAdoptionRejected();
  ResetAiDetached(); camera.deleting = true; AiAdoptionRejected();
  ResetAiDetached(); pawn.deleting = true; AiAdoptionRejected();
  ResetAiDetached(); wrong_offset = true; AiAdoptionRejected();
  ResetAiDetached(); world.bytes[0x39C] = 1; AiAdoptionRejected();
  ResetAiDetached();
  thread = CreateThread(nullptr,0,WrongAiAdoptionThread,nullptr,0,nullptr);
  assert(thread && WaitForSingleObject(thread,5000) == WAIT_OBJECT_0); CloseHandle(thread);
  for (bool* fault : {&fail_publication,&fail_icon_write,&fail_shooter_write,&change_world,&change_index,
                     &destroy_camera,&change_mode,&lose_plan,&change_aimer,&corrupt_observer,&change_camera_index,&lose_player_pair}) {
    ResetAiDetached(); *fault = true;
    assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kStateMismatch);
  }
  ResetAiDetached(); reject_ai_cast = true;
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kStateMismatch);
  assert(Read(ai.bytes,0x1C8) == reinterpret_cast<uintptr_t>(pawn.bytes) && !Read(aimer.bytes,0x30));
  ResetAiDetached(); ai_observers->capacity = ai_observers->count; corrupt_append = true;
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kStateMismatch);
  // Recover a partial service publication by detaching the coherent dirty pair,
  // then restoring ownership. Mixed owners from the two journal actors are legal.
  ResetAiDetached(); fail_shooter_write = true;
  assert(NativeAdoptExistingAiPawn(ai.bytes,pawn.bytes,buddy.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kStateMismatch);
  fail_shooter_write = false;
  assert(NativeDetachAi(ai.bytes,pawn.bytes) == NativeDetachResult::kApplied);
  assert(NativeDetachPlayer(player.bytes,buddy.bytes) == NativeDetachResult::kApplied);
  assert(NativeAdoptExistingPlayerPawn(player.bytes,pawn.bytes,move_lease,look_lease) == NativePlayerAdoptionResult::kApplied);
  assert(NativeAdoptExistingAiPawn(ai.bytes,buddy.bytes,pawn.bytes,move_lease,look_lease) == NativeAiAdoptionResult::kApplied);
  g_ai_adoption = {}; g_player_adoption = {}; g_pawn_ownership = {}; g_detach = {}; VirtualFree(image,0,MEM_RELEASE);
  std::puts("NativeControllerDetachTests passed: 12000 shipping detaches, observer cleanup, role callbacks, guards and dirty failures");
  std::puts("NativePawnOwnershipTests passed: 6000 native pawn bindings and 12000 detach/cleanup calls; no controller Restart");
  std::puts("NativePlayerAdoptionTests passed: 3000 reciprocal player adoptions/acknowledgments; no Restart or input reset");
  std::puts("NativeAiAdoptionTests passed: 3000 reciprocal ownership round trips (6000 AI/player adoptions); native aimer/observer/GC; no Possess/Restart");
}
