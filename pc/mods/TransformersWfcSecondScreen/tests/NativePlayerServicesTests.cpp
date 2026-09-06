// Executes the COMPLETE shipping pickup/fine-aim setters and GC barrier in a
// relocated local fixture. Object metadata is a test double, not the game.
#include "NativePlayerServices.cpp"
#include <cassert>
#include <cstdio>
#include <cwchar>
#include <fstream>
#include <vector>
#include <limits>

using namespace wfcds;
namespace {
struct Object {
  alignas(16) uint8_t bytes[0x800]{};
  const wchar_t* type;
  bool valid = true, deleting = false;
  uint32_t index=1;
};
Object player{{}, L"TnPlayerController"}, pawn{{}, L"TnPawn"}, buddy{{}, L"TnPawn"},
    world{{}, L"WorldInfo"}, other_world{{}, L"WorldInfo"},
    fine{{}, L"TnFineAimManager"}, pickup{{}, L"TnPickupManager"},
    camera{{}, L"TnCameraStrategyManager"}, targeting{{}, L"TnPlayerTargeting"},
    commander{{}, L"TnContextualCommandHandler"}, foreign_fine{{}, L"TnFineAimManager"},
    ai{{},L"TnAiController"},buff{{},L"TnBuffManager"},buddy_buff{{},L"TnBuffManager"},
    hud{{},L"TnHUD"},foreign_hud{{},L"TnHUD"},
    ability_manager{{},L"TnAbilityManager"},buddy_ability_manager{{},L"TnAbilityManager"},
    ability0{{},L"TnAbility"},ability1{{},L"TnAbility"},ability2{{},L"TnAbility"},
    buddy_ability0{{},L"TnAbility"},buddy_ability1{{},L"TnAbility"},buddy_ability2{{},L"TnAbility"},
    shared_services{{},L"TnServiceContainer"},form_services{{},L"TnServiceContainer"},
    robot{{},L"TnRobotForm"},vehicle{{},L"TnCarForm"},robot_buff{{},L"TnBuffManager"},
    game{{},L"TnGame"},game_class{{},L"Class"},game_defaults{{},L"TnGame"},
    cooldown_buff_class{{},L"Class"},other_buff_class{{},L"Class"},cooldown_buff{{},L"TnBuff"},other_buff{{},L"TnBuff"},
    resource_modifier{{},L"TnValueTimesProductBuffModifier"},resource_buff{{},L"TnBuffFloatModifier"},
    modifier_class{{},L"Class"},float_buff_class{{},L"Class"},difficulty_buff_class{{},L"Class"},
    resource_condition{{},L"TnBuffCondition"},downed_class{{},L"Class"},behind_class{{},L"Class"},
    target_ai_class{{},L"Class"},target_player_class{{},L"Class"},target_sentry_class{{},L"Class"};
Object* objects[] = {&player, &pawn, &buddy, &world, &other_world, &fine, &pickup,
                    &camera, &targeting, &commander, &foreign_fine,&ai,&buff,&buddy_buff,&hud,&foreign_hud,
                    &ability_manager,&buddy_ability_manager,&ability0,&ability1,&ability2,&buddy_ability0,&buddy_ability1,&buddy_ability2,
                    &shared_services,&form_services,&robot,&vehicle,&robot_buff,
                    &game,&game_class,&game_defaults,&cooldown_buff_class,&other_buff_class,&cooldown_buff,&other_buff,
                    &resource_modifier,&resource_buff,&modifier_class,&float_buff_class,&difficulty_buff_class,
                    &resource_condition,&downed_class,&behind_class,&target_ai_class,&target_player_class,&target_sentry_class};
Object *actual_modifier_class=nullptr,*actual_resource_class=nullptr,*actual_condition_class=nullptr;
void *requested_resource_buffs[2]{},*resource_conditions[2]{};
float resource_factors[2]{},resource_sp_factors[2]{};
uint8_t skill_engine[0x50]{},skill_context[0x30]{};
void* skill_world=nullptr;
void* active_buffs[2]{};
float configured_cooldowns[2][3][2]{};
struct ServiceRow { uint32_t name; void* service; } shared_rows[3]{},form_rows[3]{};
const char* pawn_class_override=nullptr;
const char* form_class_override=nullptr;
bool bad_service_names=false;
Object* ability_objects[2][3]={{&ability0,&ability1,&ability2},{&buddy_ability0,&buddy_ability1,&buddy_ability2}};
void* ability_entries[2][3]{};
float cooldown_values[2][3]{},spam_values[2][3]{};
uint8_t* image = nullptr;
bool bad_offset = false, bad_read = false;
bool reference_ready = true, reject_reference = false, corrupt_reference = false;
unsigned reference_calls = 0;
unsigned property_reads=0;
NativePlayerInputLease restore_move,restore_look;
bool bad_hud_prepare=false,reject_hud_dispatch=false;
unsigned hud_mutation=0,hud_mutation_at=0;
std::vector<AbilityHudCommand> restored_commands;
unsigned mutate_context=0;
Object* protected_object=nullptr;
Object* Lookup(void* p) {
  for (auto* o : objects) if (p == o->bytes && o->valid) return o;
  if (protected_object && p==protected_object->bytes && protected_object->valid) return protected_object;
  return nullptr;
}
uint32_t Read(const void* p, size_t offset) {
  uint32_t value = 0; std::memcpy(&value, static_cast<const uint8_t*>(p) + offset, 4); return value;
}
void Put(void* p, size_t offset, uintptr_t value) {
  const uint32_t v = static_cast<uint32_t>(value);
  std::memcpy(static_cast<uint8_t*>(p) + offset, &v, 4);
}
void Link(Object& o, size_t offset, Object* value) {
  Put(o.bytes, offset, reinterpret_cast<uintptr_t>(value ? value->bytes : nullptr));
}
uint8_t* At(uintptr_t va) { return image + (va - native_reference_code::kImageBase); }
bool Offset(void* p, const wchar_t* name, uint32_t* out) {
  Object* o = Lookup(p);
  if (!o) return false;
  struct Field { const wchar_t* type; const wchar_t* name; uint32_t offset; };
  constexpr Field fields[] = {
      {L"TnPlayerController", L"Pawn", 0x1C8},
      {L"TnPlayerController", L"AcknowledgedPawn", 0x300},
      {L"TnPlayerController", L"myHUD", 0x400}, // Metadata fixtures, not native HUD/buff offsets.
      {L"TnAiController", L"Pawn", 0x1C8},
      {L"TnAiController", L"WorldInfo", 0x98},
      {L"TnHUD", L"WorldInfo", 0x98},
      {L"TnHUD", L"PlayerOwner", 0x200},
      {L"TnPawn", L"_BuffManager", 0x500},
      {L"TnBuffManager", L"OwnerPawn", 0x40},
      {L"TnBuffManager", L"OwnerHud", 0x44},
      {L"TnPawn", L"AbilityManager", 0x504}, // Fixture pawn offset.
      {L"TnAbilityManager", L"OwnerPawn", 0x44},
      {L"TnAbilityManager", L"OwnerHud", 0x48},
      {L"TnAbility", L"OwnerPawn", 0x28},
      {L"TnAbility", L"ResourceAmountModifier", 0x90},
      {L"WorldInfo", L"WorldInfo", 0x98}, {L"WorldInfo", L"Game", 0x3C4}, {L"TnGame", L"WorldInfo", 0x98},
      {L"TnPawn", L"_Services", 0x508}, {L"TnPawn", L"_CurrentForm", 0x50C},
      {L"TnPawn", L"_RobotForm", 0x510}, {L"TnPawn", L"_VehicleForm", 0x514},
      {L"TnPawn", L"_CurrentTransformation", 0x518},
      {L"TnRobotForm", L"_OwnerPawn", 0x28}, {L"TnRobotForm", L"_Services", 0x2C},
      {L"TnRobotForm", L"_BuffManager", 0x30},
      {L"TnCarForm", L"_OwnerPawn", 0x28}, {L"TnCarForm", L"_Services", 0x2C},
      {L"TnPlayerController", L"_FineAimManager", 0x620},
      {L"TnPlayerController", L"_PickupManager", 0x690},
      {L"TnPlayerController", L"PlayerTargeting", 0x608},
      {L"TnPlayerController", L"_CameraStrategyManager", 0x60C},
      {L"TnPlayerController", L"ContextualCommander", 0x674},
      {L"TnPlayerController", L"WorldInfo", 0x98},
      {L"TnPawn", L"Controller", 0x1D4},
      {L"TnPawn", L"WorldInfo", 0x98},
      {L"TnFineAimManager", L"Owner", 0x34},
      {L"TnFineAimManager", L"PlayerTargeting", 0x2C},
      {L"TnFineAimManager", L"CameraStrategyManager", 0x30},
      {L"TnPickupManager", L"OwnerPawn", 0x38},
      {L"TnPickupManager", L"ContextualCommander", 0x40},
      {L"TnPlayerTargeting", L"_TargetingOwner", 0x48},
  };
  for (const auto& f : fields) if (!std::wcscmp(f.type, o->type) && !std::wcscmp(f.name, name)) {
    *out = f.offset; return true;
  }
  return false;
}
void Reset() {
  uint32_t index=1;
  for (auto* o : objects) {
    std::memset(o->bytes, 0, sizeof(o->bytes)); o->valid = true; o->deleting = false;
    o->index=index++;
  }
  Link(player, 0x1C8, &pawn); Link(player,0x300,&pawn); Link(pawn, 0x1D4, &player);
  player.bytes[0x66]=pawn.bytes[0x66]=buddy.bytes[0x66]=3;
  Link(player, 0x98, &world); Link(pawn, 0x98, &world); Link(buddy, 0x98, &world);
  Link(player, 0x620, &fine); Link(player, 0x690, &pickup);
  Link(player, 0x608, &targeting); Link(player, 0x60C, &camera); Link(player, 0x674, &commander);
  Link(fine, 0x34, &pawn); Link(fine, 0x2C, &targeting); Link(fine, 0x30, &camera);
  Link(pickup, 0x38, &pawn); Link(pickup, 0x40, &commander);
  Link(targeting, 0x48, &pawn);
  ai.bytes[0x66]=3; Link(ai,0x98,&world); Link(ai,0x1C8,&buddy); Link(buddy,0x1D4,&ai);
  Link(player,0x400,&hud); Link(hud,0x98,&world); Link(hud,0x200,&player);
  Link(pawn,0x500,&buff); Link(buddy,0x500,&buddy_buff);
  Link(buff,0x40,&pawn); Link(buff,0x44,&hud); Link(buddy_buff,0x40,&buddy);
  Link(pawn,0x504,&ability_manager); Link(buddy,0x504,&buddy_ability_manager);
  for (unsigned side=0;side<2;++side) {
    auto& m=side ? buddy_ability_manager:ability_manager; auto& p=side ? buddy:pawn;
    Link(m,0x44,&p); Link(m,0x48,side ? nullptr:&hud);
    const float multiplier=2,resource=37; std::memcpy(m.bytes+0x58,&multiplier,4); std::memcpy(m.bytes+0x5C,&resource,4);
    for (unsigned slot=0;slot<3;++slot) {
      ability_entries[side][slot]=ability_objects[side][slot]->bytes;
      Link(*ability_objects[side][slot],0x28,&p);
      cooldown_values[side][slot]=slot ? (slot==1 ? 4:std::numeric_limits<float>::max()):0;
      spam_values[side][slot]=slot==0 ? -0.125f:0.25f;
      configured_cooldowns[side][slot][0]=8+slot*2; configured_cooldowns[side][slot][1]=12+slot*2;
      const UnrealArrayView configured{configured_cooldowns[side][slot],2,2};
      std::memcpy(ability_objects[side][slot]->bytes+0x44,&configured,12);
      const float reduction=0.5f; std::memcpy(ability_objects[side][slot]->bytes+0x50,&reduction,4);
      const float cost=50; std::memcpy(ability_objects[side][slot]->bytes+0x54,&cost,4);
      Link(*ability_objects[side][slot],0x90,&resource_modifier);
    }
    const UnrealArrayView arrays[]={{ability_entries[side],3,3},{cooldown_values[side],3,3},{spam_values[side],3,3}};
    std::memcpy(m.bytes+0x38,&arrays[0],12); std::memcpy(m.bytes+0x4C,&arrays[1],12); std::memcpy(m.bytes+0x60,&arrays[2],12);
  }
  bad_offset = bad_read = false;
  reference_ready = true; reject_reference = corrupt_reference = false;
  mutate_context=0;
  pawn_class_override=form_class_override=nullptr; bad_service_names=false;
  Link(pawn,0x508,&shared_services); Link(pawn,0x50C,&robot); Link(pawn,0x510,&robot); Link(pawn,0x514,&vehicle);
  Link(robot,0x28,&pawn); Link(vehicle,0x28,&pawn); Link(robot,0x2C,&form_services); Link(vehicle,0x2C,&form_services);
  Link(robot,0x30,&robot_buff); Link(robot_buff,0x40,&pawn);
  for (unsigned i=0;i<3;++i) { shared_rows[i]={42+i,nullptr}; form_rows[i]={42+i,nullptr}; }
  Link(world,0x98,&world); Link(world,0x3C4,&game); Link(game,0x98,&world); Link(game_class,0x9C,&game_defaults);
  Put(game_defaults.bytes,0x10,reinterpret_cast<uintptr_t>(At(0x12B24000)));
  Put(At(0x12B24000),0x354,reinterpret_cast<uintptr_t>(At(0x12B28000)));
  At(0x12B28000)[0]=0xB8; Put(At(0x12B28000),1,1); At(0x12B28000)[5]=0xC3;
  Put(At(0x12B10A9C),0,reinterpret_cast<uintptr_t>(skill_engine));
  Put(skill_engine,0x40,reinterpret_cast<uintptr_t>(skill_context));
  Put(skill_context,0x28,reinterpret_cast<uintptr_t>(&skill_world)); skill_world=world.bytes;
  active_buffs[0]=other_buff.bytes; active_buffs[1]=nullptr;
  const UnrealArrayView buff_array{active_buffs,2,2};
  std::memcpy(robot_buff.bytes+0x80,&buff_array,12); std::memcpy(buff.bytes+0x80,&buff_array,12);
  actual_modifier_class=&modifier_class; actual_resource_class=&difficulty_buff_class; actual_condition_class=&downed_class;
  requested_resource_buffs[0]=difficulty_buff_class.bytes; requested_resource_buffs[1]=nullptr;
  resource_conditions[0]=resource_condition.bytes; resource_conditions[1]=nullptr;
  resource_factors[0]=0.5f; resource_factors[1]=0.25f;
  resource_sp_factors[0]=2; resource_sp_factors[1]=3;
  // Buff fields here are reflection fixtures; only the native modifier array
  // and ability fields above are asserted against real compiled offsets.
  const UnrealArrayView resource_arrays[]={{requested_resource_buffs,1,2},{resource_conditions,0,2},
      {resource_factors,2,2},{resource_sp_factors,2,2}};
  std::memcpy(resource_modifier.bytes+0x28,&resource_arrays[0],12);
  std::memcpy(resource_buff.bytes+0x100,&resource_arrays[1],12);
  std::memcpy(resource_buff.bytes+0x110,&resource_arrays[2],12);
  std::memcpy(resource_buff.bytes+0x120,&resource_arrays[3],12);
  restore_move={}; restore_look={}; restore_move.controller=restore_look.controller=player.bytes;
  restore_move.world=restore_look.world=world.bytes;
  restore_move.controller_index=restore_look.controller_index=player.index;
  restore_move.world_index=restore_look.world_index=world.index;
  restore_move.held=restore_look.held=true; restore_look.axis=NativeInputAxis::kLook;
  bad_hud_prepare=reject_hud_dispatch=false; hud_mutation=hud_mutation_at=0; restored_commands.clear();
}
void MutateContext() {
  switch (mutate_context) {
    case 1: Link(player,0x1C8,&buddy); break;
    case 2: Link(player,0x300,&buddy); break;
    case 3: Link(player,0x98,&other_world); Link(pawn,0x98,&other_world); break;
    case 4: Link(player,0x620,&foreign_fine); Link(player,0x690,nullptr); Link(player,0x608,nullptr);
      Link(pawn,0x500,nullptr); Link(buddy,0x500,nullptr); Link(pawn,0x504,nullptr); break;
    case 5: ++pawn.index; break;
    case 6: world.deleting=true; break;
    case 7: ++fine.index; ++pickup.index; ++targeting.index; ++buff.index; ++buddy_buff.index; ++ability_manager.index; break;
    case 8: player.bytes[0x66]=1; break;
    case 9: ++player.index; break;
    case 10: ++world.index; break;
    case 11: cooldown_values[0][1]=3; break;
    case 12: ++ability1.index; break;
    case 13: Link(ability1,0x28,&buddy); break;
    case 14: Link(ability_manager,0x48,&hud); break;
    case 15: Put(ability_manager.bytes,0x60,reinterpret_cast<uintptr_t>(spam_values[1])); break;
    case 16: spam_values[0][0]=-0.5f; break;
    case 17: hud.deleting=true; break;
    default: break;
  }
}
void AssertBothRejected() {
  const auto old_fine = fine, old_pickup = pickup;
  assert(NativeInitializeFineAim(fine.bytes, pawn.bytes, targeting.bytes, camera.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeInitializePickup(pickup.bytes, pawn.bytes, commander.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeSetTargetingOwner(targeting.bytes, pawn.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeSetBuffHud(player.bytes,pawn.bytes,hud.bytes)==NativePlayerServiceResult::kRejected);
  NativeAbilityState state; assert(!NativeReadAbilityState(player.bytes,pawn.bytes,&state));
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  void* service=nullptr; assert(!NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service));
  std::array<float,3> cooldowns; assert(!NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&cooldowns));
  float cost=99; assert(!NativeReadAbilityResourceRequired(player.bytes,pawn.bytes,&cost) && cost==0);
  assert(!PrepareNativeAbilityHudRestore(player.bytes,pawn.bytes,hud.bytes));
  assert(NativeRestoreAbilityHud(player.bytes,pawn.bytes,hud.bytes,restore_move,restore_look)==NativePlayerServiceResult::kRejected);
  assert(!std::memcmp(old_fine.bytes, fine.bytes, sizeof(fine.bytes)));
  assert(!std::memcmp(old_pickup.bytes, pickup.bytes, sizeof(pickup.bytes)));
}
DWORD WINAPI WrongThread(void*) {
  const auto reads=property_reads; AssertBothRejected();
  assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes) && property_reads==reads); return 0;
}
void __attribute__((regparm(2), stdcall)) CorruptFine(void* p, void* m, void* t, void* c) {
  reinterpret_cast<FineAimCall>(At(kFineAimVa))(p, m, t, c);
  assert(NativeInitializePickup(pickup.bytes, pawn.bytes, commander.bytes) == NativePlayerServiceResult::kRejected);
  Link(fine, 0x30, nullptr); // Simulate native execution without its expected postcondition.
}
void __attribute__((regparm(2), stdcall)) CorruptPickup(void* p, void* m, void* c) {
  reinterpret_cast<PickupCall>(At(kPickupVa))(p, m, c);
  assert(NativeInitializeFineAim(fine.bytes, pawn.bytes, targeting.bytes, camera.bytes) == NativePlayerServiceResult::kRejected);
  Link(pickup, 0x40, nullptr);
}
void __attribute__((regparm(2), stdcall)) MutateFine(void* p,void* m,void* t,void* c) {
  reinterpret_cast<FineAimCall>(At(kFineAimVa))(p,m,t,c); MutateContext();
}
void __attribute__((regparm(2), stdcall)) MutatePickup(void* p,void* m,void* c) {
  reinterpret_cast<PickupCall>(At(kPickupVa))(p,m,c); MutateContext();
}
void VerifyFile(const char* path) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  assert(file && file.tellg() == 37667328);
  std::vector<uint8_t> bytes(static_cast<size_t>(file.tellg()));
  file.seekg(0); assert(file.read(reinterpret_cast<char*>(bytes.data()), bytes.size()));
  assert(!std::memcmp(bytes.data() + kFineAimVa - 0x10900C00, kFineAimCode, sizeof(kFineAimCode)));
  assert(!std::memcmp(bytes.data() + kPickupVa - 0x10900C00, kPickupCode, sizeof(kPickupCode)));
  assert(!std::memcmp(bytes.data() + native_reference_code::kBarrierVa - 0x10900C00,
      native_reference_code::kBarrier, sizeof(native_reference_code::kBarrier)));
  constexpr uint8_t fine_call[] = {0x50,0x83,0xC1,0x10,0x51,0x83,0xC2,0x10,0x8B,0xC7,0xE8,0x82,0x1E,0xF7,0xFF};
  constexpr uint8_t pickup_call[] = {0x50,0x83,0xC2,0x10,0x8B,0xC3,0xE8,0xD6,0xFC,0xFE,0xFF};
  assert(!std::memcmp(bytes.data() + 0x10B21F6F - 0x10900C00, fine_call, sizeof(fine_call)));
  assert(!std::memcmp(bytes.data() + 0x10B21F4F - 0x10900C00, pickup_call, sizeof(pickup_call)));
  native_ability_skill_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    assert(!std::memcmp(bytes.data()+va-0x10900C00,code,sizeof(code))); return true;
  });
  native_ability_resource_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    assert(!std::memcmp(bytes.data()+va-0x10900C00,code,sizeof(code))); return true;
  });
  std::puts("Native player services: 3 complete bodies and both OnSetPawn call-site ABIs match TWFC.exe");
}
}  // namespace
namespace wfcds {
uint32_t UnrealObjectIndex(void* object) { auto* o=Lookup(object); return o ? o->index:0; }
bool UnrealObjectReferenceWritesReady() { return reference_ready; }
bool UnrealWriteObjectPropertyExpected(void* p, const wchar_t* name, void* expected, void* value) {
  uint32_t offset = 0;
  if (reject_reference || !Offset(p, name, &offset) || Read(p, offset) != reinterpret_cast<uintptr_t>(expected)) return false;
  ++reference_calls;
  assert(NativeSetTargetingOwner(p, value) == NativePlayerServiceResult::kRejected);
  assert(NativeSetBuffHud(player.bytes,pawn.bytes,hud.bytes)==NativePlayerServiceResult::kRejected);
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  Put(p, offset, reinterpret_cast<uintptr_t>(corrupt_reference ? nullptr : value));
  MutateContext();
  return true;
}
bool UnrealObjectIsA(void* p, const wchar_t* type) {
  auto* o = Lookup(p); return o && (!std::wcscmp(o->type, type) || (o==&resource_buff && !std::wcscmp(type,L"TnBuff")));
}
bool UnrealResolveObjectPropertyOffset(void* p, const wchar_t* name, uint32_t* out) {
  if (!Offset(p, name, out)) return false;
  if (bad_offset) *out += 4;
  return true;
}
bool UnrealReadObjectProperty(void* p, const wchar_t* name, void** out) {
  ++property_reads;
  uint32_t offset = 0;
  if (bad_read || !Offset(p, name, &offset)) return false;
  *out = reinterpret_cast<void*>(Read(p, offset));
  return !*out || Lookup(*out);
}
bool UnrealReadBoolProperty(void* p, const wchar_t* name, bool* out) {
  if (UnrealObjectIsA(p,L"TnPawn") && !std::wcscmp(name,L"_IsDowned")) {
    *out=static_cast<uint8_t*>(p)[0x520]!=0; return !bad_read;
  }
  auto* o = Lookup(p); if (!o || std::wcscmp(name, L"bDeleteMe")) return false;
  *out = o->deleting; return true;
}
bool UnrealReadByteProperty(void* p, const wchar_t* name, uint8_t* out) {
  if (Lookup(p) && !std::wcscmp(name,L"Role")) { *out=static_cast<uint8_t*>(p)[0x66]; return true; }
  if ((p != world.bytes && p != other_world.bytes) || std::wcscmp(name, L"NetMode")) return false;
  *out = static_cast<uint8_t*>(p)[0x39C]; return true;
}
bool UnrealReadArrayProperty(void* p,const wchar_t* name,UnrealArrayView* out) {
  if (!bad_read && Lookup(p)) {
    const size_t offset=p==resource_modifier.bytes && !std::wcscmp(name,L"BuffsWhichModifyProperty") ? 0x28:
        p==resource_buff.bytes ? (!std::wcscmp(name,L"Conditions") ? 0x100:
          !std::wcscmp(name,L"FloatModifier") ? 0x110:!std::wcscmp(name,L"FloatModifierSP") ? 0x120:0):0;
    if (offset) { std::memcpy(out,static_cast<uint8_t*>(p)+offset,12); return true; }
  }
  if (!bad_read && ((UnrealObjectIsA(p,L"TnAbility") && !std::wcscmp(name,L"Cooldown")) ||
      (UnrealObjectIsA(p,L"TnBuffManager") && !std::wcscmp(name,L"Buffs")))) {
    std::memcpy(out,static_cast<uint8_t*>(p)+(UnrealObjectIsA(p,L"TnAbility") ? 0x44:0x80),12); return true;
  }
  if (bad_read || !UnrealObjectIsA(p,L"TnAbilityManager")) return false;
  const size_t offset=!std::wcscmp(name,L"Abilities") ? 0x38:!std::wcscmp(name,L"Cooldowns") ? 0x4C:
      !std::wcscmp(name,L"SpamPreventionCooldowns") ? 0x60:0;
  if (!offset) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+offset,12); return true;
}
bool UnrealReadFloatProperty(void* p,const wchar_t* name,float* out) {
  if (!bad_read && UnrealObjectIsA(p,L"TnAbility") && !std::wcscmp(name,L"ResourceRequired")) {
    std::memcpy(out,static_cast<uint8_t*>(p)+0x54,4); return true;
  }
  if (UnrealObjectIsA(p,L"TnAbility") && !std::wcscmp(name,L"CooldownReductionModifier")) {
    std::memcpy(out,static_cast<uint8_t*>(p)+0x50,4); return true;
  }
  if (UnrealObjectIsA(p,L"TnPawn") && !std::wcscmp(name,L"_TransformBlendTimeRemaining")) {
    std::memcpy(out,static_cast<uint8_t*>(p)+0x51C,4); return true;
  }
  if (bad_read || !UnrealObjectIsA(p,L"TnAbilityManager")) return false;
  const size_t offset=!std::wcscmp(name,L"CooldownMultiplier") ? 0x58:!std::wcscmp(name,L"Resource") ? 0x5C:0;
  if (!offset) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+offset,4); return true;
}
bool UnrealReadIntProperty(void* p,const wchar_t* name,int32_t* out) {
  if (bad_read || p!=resource_buff.bytes || !Lookup(p) || std::wcscmp(name,L"BuffLevel")) return false;
  std::memcpy(out,resource_buff.bytes+0x130,4); return true;
}
bool UnrealReadObjectArrayElement(const UnrealArrayView& a,int32_t i,void** out) {
  if (!a.data || i<0 || i>=a.count) return false;
  *out=static_cast<void**>(a.data)[i]; return !*out || Lookup(*out)!=nullptr;
}
bool UnrealReadFloatArrayElement(const UnrealArrayView& a,int32_t i,float* out) {
  if (!a.data || i<0 || i>=a.count) return false;
  *out=static_cast<float*>(a.data)[i]; return true;
}
bool UnrealObjectClassName(void* p,char* out,size_t size) {
  auto* o=Lookup(p); if (!o || size<32) return false;
  if (o==&pawn || o==&buddy) std::strcpy(out,pawn_class_override ? pawn_class_override:(o==&pawn ? "TnPlayerPawn":"TnAiPawn"));
  else if (o==&robot) std::strcpy(out,"TnRobotForm");
  else if (o==&vehicle) std::strcpy(out,form_class_override ? form_class_override:"TnCarForm");
  else if (o==&shared_services || o==&form_services) std::strcpy(out,"TnServiceContainer");
  else if (!std::wcscmp(o->type,L"TnBuffManager")) std::strcpy(out,"TnBuffManager");
  else return false;
  return true;
}
bool UnrealReadStructArrayProperty(void* p,const wchar_t* name,UnrealStructArrayView* out) {
  if (bad_read || std::wcscmp(name,L"_ServiceDescriptions")) return false;
  if (p!=shared_services.bytes && p!=form_services.bytes) return false;
  *out={p==shared_services.bytes ? shared_rows:form_rows,3,3,sizeof(ServiceRow),p}; return true;
}
bool UnrealStructArrayNameEquals(const UnrealStructArrayView& a,int32_t i,const wchar_t* field,const wchar_t* expected,bool* out) {
  assert(!std::wcscmp(field,L"Type") && !std::wcscmp(expected,L"TnBuffService"));
  if (bad_service_names || i<0 || i>=a.count) return false;
  *out=static_cast<ServiceRow*>(a.data)[i].name==42; return true;
}
bool UnrealReadStructArrayObjectField(const UnrealStructArrayView& a,int32_t i,const wchar_t* field,void** out) {
  assert(!std::wcscmp(field,L"Service")); if (i<0 || i>=a.count) return false;
  *out=static_cast<ServiceRow*>(a.data)[i].service; return !*out || Lookup(*out);
}
void* UnrealObjectClass(void* p) {
  if (!Lookup(p)) return nullptr;
  if (p==game.bytes || p==game_defaults.bytes) return game_class.bytes;
  if (p==cooldown_buff.bytes) return cooldown_buff_class.bytes;
  if (p==other_buff.bytes) return other_buff_class.bytes;
  if (p==resource_modifier.bytes) return actual_modifier_class ? actual_modifier_class->bytes:nullptr;
  if (p==resource_buff.bytes) return actual_resource_class ? actual_resource_class->bytes:nullptr;
  if (p==resource_condition.bytes) return actual_condition_class ? actual_condition_class->bytes:nullptr;
  return nullptr;
}
bool UnrealResolveClassByPath(const wchar_t* path,void** out) {
  const std::pair<const wchar_t*,Object*> classes[]={
      {L"TransGame.TnBuffAbilityCooldown",&cooldown_buff_class},
      {L"TransGame.TnValueTimesProductBuffModifier",&modifier_class},
      {L"TransGame.TnBuffFloatModifier",&float_buff_class},
      {L"TransGame.TnBuffDifficultyAbilityResourceModifier",&difficulty_buff_class},
      {L"TransGame.TnBuffConditionDowned",&downed_class},
      {L"TransGame.TnBuffConditionBehindTarget",&behind_class},
      {L"TransGame.TnBuffConditionTargetIsAi",&target_ai_class},
      {L"TransGame.TnBuffConditionTargetIsPlayer",&target_player_class},
      {L"TransGame.TnBuffConditionTargetIsSentry",&target_sentry_class}};
  *out=nullptr;
  for (const auto& entry:classes) if (!std::wcscmp(path,entry.first)) {
    *out=entry.second->valid ? entry.second->bytes:nullptr; break;
  }
  return *out!=nullptr;
}
bool UnrealResolveFloatPropertyOffset(void* p,const wchar_t* name,uint32_t* out) {
  if (UnrealObjectIsA(p,L"TnAbility") && !std::wcscmp(name,L"ResourceRequired")) {
    *out=bad_offset ? 0x58:0x54; return true;
  }
  if (!UnrealObjectIsA(p,L"TnAbility") || std::wcscmp(name,L"CooldownReductionModifier")) return false;
  *out=bad_offset ? 0x54:0x50; return true;
}
bool NativePlayerInputHeld(const NativePlayerInputLease& lease) {
  // Production checks pointer identity, not just copied fields.
  return (&lease==&restore_move || &lease==&restore_look) && lease.held && !lease.invalidated &&
      lease.controller==player.bytes && lease.world==world.bytes &&
      lease.controller_index==player.index && lease.world_index==world.index;
}
bool PrepareNativeAbilityHud(HMODULE module,DWORD thread,void* pc,void* p,void* h) {
  return !bad_hud_prepare && module==reinterpret_cast<HMODULE>(image) && thread==GetCurrentThreadId() &&
      pc==player.bytes && Read(pc,0x1C8)==reinterpret_cast<uintptr_t>(p) && h==hud.bytes;
}
NativeAbilityHudResult NativeDispatchAbilityHud(void* pc,void* p,void* h,const AbilityHudPlan& plan,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look,const NativeAbilityHudValidation& validation) {
  if (reject_hud_dispatch || !NativePlayerInputHeld(move) || !NativePlayerInputHeld(look) ||
      !validation.validate || !validation.validate(validation.context)) return NativeAbilityHudResult::kRejected;
  assert(pc==player.bytes && h==hud.bytes && Read(pc,0x1C8)==reinterpret_cast<uintptr_t>(p));
  assert(NativeRestoreAbilityHud(pc,p,h,move,look)==NativePlayerServiceResult::kRejected);
  for (uint32_t i=0;i<plan.count;++i) {
    restored_commands.push_back(plan.commands[i]);
    if (hud_mutation && i==hud_mutation_at) {
      mutate_context=hud_mutation; MutateContext(); mutate_context=0;
    }
    if (!validation.validate(validation.context)) return NativeAbilityHudResult::kStateMismatch;
  }
  return NativeAbilityHudResult::kDispatched;
}
}  // namespace wfcds

int main(int argc, char** argv) {
  image = static_cast<uint8_t*>(VirtualAlloc(nullptr, 0x2300000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(image);
  const auto base = reinterpret_cast<uintptr_t>(image);
  const auto install = [&](uintptr_t va, const auto& code) { std::memcpy(At(va), code.data(), code.size()); };
  install(kFineAimVa, native_reference_code::Relocate(kFineAimCode, base, {34,57,78,101}));
  install(kPickupVa, native_reference_code::Relocate(kPickupCode, base, {34,57}));
  install(native_reference_code::kBarrierVa, native_reference_code::Relocate(native_reference_code::kBarrier, base, {15,38}));
  native_ability_skill_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    install(va,native_reference_code::Relocate(code,base,operands)); return true;
  });
  native_ability_resource_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    install(va,native_reference_code::Relocate(code,base,operands)); return true;
  });
  if (argc == 2) VerifyFile(argv[1]);
  DWORD old = 0;
  assert(VirtualProtect(At(0x12B28000),4096,PAGE_EXECUTE_READWRITE,&old));
  assert(VirtualProtect(image, 0x1E00000, PAGE_EXECUTE_READWRITE, &old));
  FlushInstructionCache(GetCurrentProcess(), image, 0x1E00000);
  PlayerServiceBindings bound;
  const auto module = reinterpret_cast<HMODULE>(image);
  assert(BindPlayerServiceImage(module, GetCurrentThreadId(), &bound));
  // Every instruction/operand byte is part of the gate, including absolute
  // GC globals after image relocation. No permissive wildcard signatures.
  for (auto range : {std::pair<uintptr_t, size_t>{kFineAimVa, sizeof(kFineAimCode)},
                     {kPickupVa, sizeof(kPickupCode)},
                     {native_reference_code::kBarrierVa, sizeof(native_reference_code::kBarrier)}}) {
    for (size_t i = 0; i < range.second; ++i) {
      At(range.first)[i] ^= 1;
      PlayerServiceBindings rejected;
      assert(!BindPlayerServiceImage(module, GetCurrentThreadId(), &rejected));
      assert(!rejected.module);
      At(range.first)[i] ^= 1;
    }
  }
  assert(VirtualProtect(image, 0x1E00000, PAGE_EXECUTE_READ, &old));
  FlushInstructionCache(GetCurrentProcess(), image, 0x1E00000);
  g_player_services = bound;
  g_player_services.module = GetModuleHandleW(nullptr); // Public host-identity guard.
  Reset();
  assert(PrepareNativePlayerServices(GetModuleHandleW(nullptr), GetCurrentThreadId(), player.bytes));
  // Native barrier's None, GC phase and reference-mark branches; no test
  // doubles in the executed GC path. Both inline and called barriers execute.
  for (unsigned n = 0; n < 6000; ++n) {
    Reset();
    Object* next = n & 1 ? &buddy : &pawn;
    Link(player, 0x1C8, next); Link(player,0x300,next); Link(pawn, 0x1D4, nullptr); Link(buddy, 0x1D4, nullptr);
    Link(*next, 0x1D4, &player);
    const uint32_t stage = n % 6, bits = (n / 6) % 16;
    Put(next->bytes, 8, (bits & 1) ? 0x800000 : 0);
    Put(targeting.bytes, 8, (bits & 2) ? 0x800000 : 0);
    Put(camera.bytes, 8, (bits & 4) ? 0x800000 : 0);
    Put(commander.bytes, 8, (bits & 8) ? 0x800000 : 0);
    Put(At(0x12B1047C), 0, stage); Put(At(0x12B10474), 0, 0);
    const Object before_pawn = *next;
    assert(NativeSetTargetingOwner(targeting.bytes, next->bytes) == NativePlayerServiceResult::kApplied);
    assert(Read(targeting.bytes, 0x48) == reinterpret_cast<uintptr_t>(next->bytes));
    auto expected_fine = fine;
    Link(expected_fine, 0x34, next); Link(expected_fine, 0x2C, &targeting); Link(expected_fine, 0x30, &camera);
    assert(NativeInitializeFineAim(fine.bytes, next->bytes, targeting.bytes, camera.bytes) == NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(fine.bytes, expected_fine.bytes, sizeof(fine.bytes)));
    assert(Read(At(0x12B10474), 0) == static_cast<uint32_t>(stage <= 3 && (bits & 7)));
    Put(At(0x12B10474), 0, 0);
    const bool with_commander = n % 3 != 0;
    Link(player, 0x674, with_commander ? &commander : nullptr);
    auto expected_pickup = pickup;
    Link(expected_pickup, 0x38, next); Link(expected_pickup, 0x40, with_commander ? &commander : nullptr);
    assert(NativeInitializePickup(pickup.bytes, next->bytes, with_commander ? commander.bytes : nullptr) == NativePlayerServiceResult::kApplied);
    assert(!std::memcmp(pickup.bytes, expected_pickup.bytes, sizeof(pickup.bytes)));
    assert(Read(At(0x12B10474), 0) == static_cast<uint32_t>(stage <= 3 && ((bits & 1) || (with_commander && (bits & 8)))));
    assert(!std::memcmp(next->bytes, before_pawn.bytes, sizeof(next->bytes)));
    reinterpret_cast<void (__attribute__((cdecl)) *)(void*)>(At(native_reference_code::kBarrierVa))(nullptr);
  }
  Reset(); bad_offset = true; AssertBothRejected();
  assert(!PrepareNativePlayerServices(GetModuleHandleW(nullptr), GetCurrentThreadId(), player.bytes));
  Reset(); bad_read = true; AssertBothRejected();
  Reset(); pawn.deleting = true; AssertBothRejected();
  Reset(); player.deleting = true; AssertBothRejected();
  Reset(); Link(pawn, 0x1D4, nullptr); AssertBothRejected();
  Reset(); Link(player,0x300,&buddy); AssertBothRejected();
  Reset(); player.bytes[0x66]=1; AssertBothRejected();
  Reset(); pawn.bytes[0x66]=1; AssertBothRejected();
  Reset(); pawn.index=0; AssertBothRejected();
  Reset(); world.deleting=true; AssertBothRejected();
  Reset(); Link(player, 0x1C8, &buddy); AssertBothRejected();
  Reset(); Link(pawn, 0x98, &other_world); AssertBothRejected();
  for (uint8_t mode = 1; mode < 4; ++mode) { Reset(); world.bytes[0x39C] = mode; AssertBothRejected(); }
  Reset(); g_player_services_busy = true; AssertBothRejected(); g_player_services_busy = false;
  Reset();
  const auto previous_references = reference_calls;
  reference_ready = false;
  assert(NativeSetTargetingOwner(targeting.bytes, pawn.bytes) == NativePlayerServiceResult::kRejected);
  Reset(); reject_reference = true;
  assert(NativeSetTargetingOwner(targeting.bytes, pawn.bytes) == NativePlayerServiceResult::kRejected);
  assert(previous_references == reference_calls);
  Reset(); corrupt_reference = true;
  assert(NativeSetTargetingOwner(targeting.bytes, pawn.bytes) == NativePlayerServiceResult::kStateMismatch);
  Reset();
  HANDLE thread = CreateThread(nullptr, 0, WrongThread, nullptr, 0, nullptr);
  assert(thread && WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0); CloseHandle(thread);
  assert(NativeInitializeFineAim(foreign_fine.bytes, pawn.bytes, targeting.bytes, camera.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeInitializeFineAim(fine.bytes, pawn.bytes, camera.bytes, targeting.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeInitializePickup(pickup.bytes, pawn.bytes, targeting.bytes) == NativePlayerServiceResult::kRejected);
  assert(NativeInitializePickup(pickup.bytes, pawn.bytes, nullptr) == NativePlayerServiceResult::kRejected);
  g_player_services.fine_aim = &CorruptFine;
  assert(NativeInitializeFineAim(fine.bytes, pawn.bytes, targeting.bytes, camera.bytes) == NativePlayerServiceResult::kStateMismatch);
  Reset(); g_player_services.pickup = &CorruptPickup;
  assert(NativeInitializePickup(pickup.bytes, pawn.bytes, commander.bytes) == NativePlayerServiceResult::kStateMismatch);
  // Correct output fields alone are not success after world/identity/control
  // invalidation. Each native primitive has already executed in these cases.
  g_player_services.fine_aim=&MutateFine; g_player_services.pickup=&MutatePickup;
  for (unsigned change=1;change<=10;++change) {
    Reset(); mutate_context=change;
    assert(NativeInitializeFineAim(fine.bytes,pawn.bytes,targeting.bytes,camera.bytes)==NativePlayerServiceResult::kStateMismatch);
    Reset(); mutate_context=change;
    assert(NativeInitializePickup(pickup.bytes,pawn.bytes,commander.bytes)==NativePlayerServiceResult::kStateMismatch);
    Reset(); mutate_context=change;
    assert(NativeSetTargetingOwner(targeting.bytes,pawn.bytes)==NativePlayerServiceResult::kStateMismatch);
  }
  // The native setters write directly, so reflected readability is not enough.
  Reset(); protected_object=static_cast<Object*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
  assert(protected_object); *protected_object=fine; protected_object->index=100;
  Link(player,0x620,protected_object); DWORD protection=0;
  assert(VirtualProtect(protected_object,4096,PAGE_READONLY,&protection));
  assert(NativeInitializeFineAim(protected_object->bytes,pawn.bytes,targeting.bytes,camera.bytes)==NativePlayerServiceResult::kRejected);
  assert(VirtualProtect(protected_object,4096,PAGE_READWRITE,&protection));
  *protected_object=pickup; protected_object->index=100; Link(player,0x690,protected_object);
  assert(VirtualProtect(protected_object,4096,PAGE_READONLY,&protection));
  assert(NativeInitializePickup(protected_object->bytes,pawn.bytes,commander.bytes)==NativePlayerServiceResult::kRejected);
  assert(VirtualFree(protected_object,0,MEM_RELEASE)); protected_object=nullptr;
  Reset();
  assert(PrepareNativeBuffHud(player.bytes,pawn.bytes) && PrepareNativeBuffHud(player.bytes,buddy.bytes));
  // Buff handoff never initializes the manager or copies its active buffs.
  // Both exact existing managers retain all bytes except their HUD reference.
  for (unsigned round=0;round<3000;++round) {
    Reset();
    for (size_t byte=0x80;byte<0x100;++byte) {
      buff.bytes[byte]=static_cast<uint8_t>(round+byte);
      buddy_buff.bytes[byte]=static_cast<uint8_t>(round-byte);
    }
    const auto before_buff=buff,before_buddy_buff=buddy_buff;
    for (unsigned direction=0;direction<2;++direction) {
      auto& selected=direction ? pawn:buddy; auto& previous_pawn=direction ? buddy:pawn;
      Link(player,0x1C8,&selected); Link(player,0x300,&selected); Link(selected,0x1D4,&player);
      Link(ai,0x1C8,&previous_pawn); Link(previous_pawn,0x1D4,&ai);
      assert(NativeSetBuffHud(player.bytes,selected.bytes,hud.bytes)==NativePlayerServiceResult::kApplied);
      assert(NativeSetBuffHud(player.bytes,previous_pawn.bytes,nullptr)==NativePlayerServiceResult::kApplied);
    }
    assert(!std::memcmp(buff.bytes,before_buff.bytes,sizeof(buff.bytes)));
    assert(!std::memcmp(buddy_buff.bytes,before_buddy_buff.bytes,sizeof(buddy_buff.bytes)));
  }
  Reset(); assert(NativeSetBuffHud(player.bytes,buddy.bytes,hud.bytes)==NativePlayerServiceResult::kRejected);
  Reset(); Link(buddy_buff,0x44,&hud); assert(!PrepareNativeBuffHud(player.bytes,buddy.bytes));
  Reset(); Link(buff,0x40,&buddy); assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); Link(hud,0x200,&ai); assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); Link(hud,0x98,&other_world); assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); Link(player,0x400,&foreign_hud); assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); hud.deleting=true; assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); ai.bytes[0x66]=1; assert(!PrepareNativeBuffHud(player.bytes,buddy.bytes));
  Reset(); Link(ai,0x1C8,&pawn); assert(!PrepareNativeBuffHud(player.bytes,buddy.bytes));
  Reset(); reference_ready=false; assert(!PrepareNativeBuffHud(player.bytes,pawn.bytes));
  Reset(); reject_reference=true;
  assert(NativeSetBuffHud(player.bytes,pawn.bytes,nullptr)==NativePlayerServiceResult::kRejected);
  for (unsigned change=1;change<=10;++change) {
    Reset(); mutate_context=change;
    assert(NativeSetBuffHud(player.bytes,pawn.bytes,nullptr)==NativePlayerServiceResult::kStateMismatch);
  }
  // Live-state reads/clears on both existing owners. The reference writer and
  // metadata remain fixtures; attaching HUDs below is fixture setup, NOT a swap.
  for (unsigned round=0;round<3000;++round) {
    Reset(); Link(buddy_ability_manager,0x48,&hud);
    for (unsigned side=0;side<2;++side) {
      auto& p=side ? buddy:pawn; auto& m=side ? buddy_ability_manager:ability_manager;
      NativeAbilityState state;
      assert(NativeReadAbilityState(player.bytes,p.bytes,&state));
      assert(state.manager==m.bytes && state.count==3 && state.resource==37 && state.multiplier==2 &&
             state.cooldowns[1]==4 && state.cooldowns[2]==std::numeric_limits<float>::max() && state.spam_cooldowns[0]==-0.125f);
      auto expected=m; Put(expected.bytes,0x48,0); const auto before_pawn=p;
      const auto calls=reference_calls;
      assert(NativeClearAbilityHud(player.bytes,p.bytes)==NativePlayerServiceResult::kApplied && reference_calls==calls+1);
      assert(NativeClearAbilityHud(player.bytes,p.bytes)==NativePlayerServiceResult::kApplied && reference_calls==calls+1);
      assert(!std::memcmp(expected.bytes,m.bytes,sizeof(m.bytes)) && !std::memcmp(before_pawn.bytes,p.bytes,sizeof(p.bytes)));
      NativeAbilityState after; assert(NativeReadAbilityState(player.bytes,p.bytes,&after));
      assert(!after.hud && after.abilities==state.abilities && after.ability_indices==state.ability_indices &&
             after.cooldowns==state.cooldowns && after.spam_cooldowns==state.spam_cooldowns);
    }
  }
  const auto invalid_ability=[&]() {
    NativeAbilityState state; state.count=99; const auto calls=reference_calls;
    assert(!NativeReadAbilityState(player.bytes,pawn.bytes,&state) && state.count==0 && !state.manager);
    assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected && reference_calls==calls);
  };
  Reset(); Link(ability_manager,0x44,&buddy); invalid_ability();
  Reset(); Link(ability1,0x28,&buddy); invalid_ability();
  Reset(); ability_entries[0][1]=ability_entries[0][0]; invalid_ability();
  Reset(); ability1.valid=false; invalid_ability();
  Reset(); ability1.index=0; invalid_ability();
  Reset(); Put(ability_manager.bytes,0x50,2); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x3C,4); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x64,static_cast<uint32_t>(-1)); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x54,2); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x4C,0); invalid_ability();
  Reset(); cooldown_values[0][1]=-0.1f; invalid_ability();
  Reset(); spam_values[0][0]=std::numeric_limits<float>::quiet_NaN(); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x58,0x7F800000); invalid_ability();
  Reset(); Put(ability_manager.bytes,0x5C,0xBF800000); invalid_ability();
  Reset(); Link(hud,0x200,&ai); invalid_ability();
  Reset(); Link(hud,0x98,&other_world); invalid_ability();
  for (unsigned change=1;change<=17;++change) {
    Reset(); mutate_context=change;
    assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kStateMismatch);
  }
  Reset(); reject_reference=true;
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  Reset(); reference_ready=false;
  NativeAbilityState state; assert(NativeReadAbilityState(player.bytes,pawn.bytes,&state)); // Read does not need write permission.
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kRejected);
  Reset(); Put(ability_manager.bytes,0x3C,0); Put(ability_manager.bytes,0x50,0); Put(ability_manager.bytes,0x64,0);
  assert(NativeReadAbilityState(player.bytes,pawn.bytes,&state) && !state.count);
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);
  Reset(); ability_entries[0][0]=ability_entries[0][2]=nullptr;
  assert(NativeReadAbilityState(player.bytes,pawn.bytes,&state) && state.count==3 && !state.abilities[0] &&
         state.abilities[1]==ability1.bytes && !state.abilities[2]);
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);
  assert(!NativeReadAbilityState(player.bytes,pawn.bytes,nullptr));
  // Follow exact first-match service precedence without publishing lazy caches.
  for (unsigned round=0;round<1000;++round) {
    Reset(); void* service=nullptr; const auto before_robot=robot, before_buff=robot_buff;
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==robot_buff.bytes);
    shared_rows[0].service=buff.bytes;
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==buff.bytes);
    shared_rows[0].service=nullptr; shared_rows[1]={42,buff.bytes};
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==robot_buff.bytes); // FIRST match is None.
    form_rows[0].service=buff.bytes;
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==buff.bytes);
    Link(pawn,0x50C,&vehicle);
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==buff.bytes); // Cached vehicle service wins.
    form_rows[0].service=nullptr;
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && !service); // No vehicle fallback buff.
    Link(pawn,0x50C,nullptr);
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && !service);
    assert(!std::memcmp(before_robot.bytes,robot.bytes,sizeof(robot.bytes)) && !std::memcmp(before_buff.bytes,robot_buff.bytes,sizeof(robot_buff.bytes)));
    assert(!form_rows[0].service); // No lazy cache mutation or buff creation.
  }
  const auto service_rejected=[&]() { void* service=reinterpret_cast<void*>(1);
    assert(!NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && !service); };
  Reset(); bad_service_names=true; service_rejected();
  Reset(); Link(robot_buff,0x40,&buddy); service_rejected();
  Reset(); Link(robot,0x28,&buddy); service_rejected();
  Reset(); Link(pawn,0x510,&vehicle); service_rejected();
  Reset(); shared_rows[0].service=fine.bytes; service_rejected();
  Reset(); robot_buff.index=0; service_rejected();
  Reset(); shared_rows[0].service=foreign_hud.bytes; service_rejected();
  Reset(); Link(pawn,0x518,&robot); service_rejected();
  Reset(); Put(pawn.bytes,0x51C,0x3F000000); service_rejected();
  Reset(); Put(pawn.bytes,0x51C,0x7FC00000); service_rejected();
  Reset(); pawn_class_override="TnSentryPawn"; service_rejected();
  Reset(); Link(pawn,0x50C,&vehicle); form_class_override="UnknownVehicleForm"; service_rejected();
  Reset(); Link(pawn,0x508,nullptr); service_rejected();
  Reset(); Link(robot,0x2C,nullptr); service_rejected();
  Reset(); void* service=nullptr; reference_ready=false;
  assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==robot_buff.bytes);
  for (const auto* form_type:{"TnCarForm","TnTruckForm","TnPlaneForm","TnTankForm"}) {
    Reset(); form_class_override=form_type; Link(pawn,0x50C,&vehicle);
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && !service);
    form_rows[0].service=buff.bytes;
    assert(NativeResolveAbilityBuffService(player.bytes,pawn.bytes,&service) && service==buff.bytes);
  }
  Reset(); // Same read path on an actual existing reciprocal squad-AI owner.
  Link(buddy,0x508,&shared_services); Link(buddy,0x50C,&robot); Link(buddy,0x510,&robot);
  Link(robot,0x28,&buddy); Link(robot_buff,0x40,&buddy);
  assert(NativeResolveAbilityBuffService(player.bytes,buddy.bytes,&service) && service==robot_buff.bytes);
  assert(!NativeResolveAbilityBuffService(player.bytes,pawn.bytes,nullptr));
  g_player_services.module=module;
  std::array<float,3> effective{};
  for (unsigned round=0;round<1000;++round) {
    Reset(); const auto untouched=ability_manager;
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && (effective==std::array<float,3>{12,14,16}));
    active_buffs[1]=cooldown_buff.bytes;
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && (effective==std::array<float,3>{6,7,8}));
    Put(At(0x12B28000),1,0);
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && (effective==std::array<float,3>{4,5,6}));
    Put(At(0x12B28000),1,7); // Native fallback to first configured value.
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && (effective==std::array<float,3>{4,5,6}));
    Put(ability1.bytes,0x48,0); // No configured cooldown => zero, not a fabricated default.
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && effective[1]==0);
    Link(pawn,0x50C,&vehicle);
    assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && effective[0]==8 && effective[1]==0 && effective[2]==12);
    assert(!std::memcmp(untouched.bytes,ability_manager.bytes,sizeof(ability_manager.bytes)));
  }
  const auto cooldown_rejected=[&]() { effective.fill(99);
    assert(!NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && (effective==std::array<float,3>{})); };
  Reset(); skill_world=other_world.bytes; cooldown_rejected();
  Reset(); Link(world,0x3C4,nullptr); cooldown_rejected();
  Reset(); game.deleting=true; cooldown_rejected();
  Reset(); game_defaults.valid=false; cooldown_rejected();
  Reset(); Link(game,0x98,&other_world); cooldown_rejected();
  Reset(); Put(game_class.bytes,0x9C,0); cooldown_rejected();
  Reset(); Put(At(0x12B28000),1,static_cast<uint32_t>(-1)); cooldown_rejected();
  Reset(); At(0x12B28000)[0]=0xE9; cooldown_rejected(); // Unknown/dynamic override.
  Reset(); cooldown_buff_class.valid=false; cooldown_rejected();
  Reset(); active_buffs[0]=fine.bytes; cooldown_rejected();
  Reset(); configured_cooldowns[0][0][1]=std::numeric_limits<float>::quiet_NaN(); cooldown_rejected();
  Reset(); active_buffs[0]=cooldown_buff.bytes; Put(ability0.bytes,0x50,0x7F800000); cooldown_rejected();
  Reset(); active_buffs[0]=cooldown_buff.bytes; configured_cooldowns[0][0][1]=std::numeric_limits<float>::max();
  const float huge=2; std::memcpy(ability0.bytes+0x50,&huge,4); cooldown_rejected();
  Reset(); active_buffs[0]=nullptr; active_buffs[1]=nullptr;
  assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && effective[0]==12);
  Reset(); ability_entries[0][1]=nullptr;
  assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && effective[1]==0);
  Reset(); At(0x12B28000)[0]=0x33; At(0x12B28000)[1]=0xC0; At(0x12B28000)[2]=0xC3;
  assert(NativeReadAbilityCooldowns(player.bytes,pawn.bytes,&effective) && effective[0]==8);
  Reset(); Link(buddy,0x508,&shared_services); Link(buddy,0x50C,&robot); Link(buddy,0x510,&robot);
  Link(robot,0x28,&buddy); Link(robot_buff,0x40,&buddy);
  assert(NativeReadAbilityCooldowns(player.bytes,buddy.bytes,&effective) && effective[1]==14);
  Reset(); assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READWRITE,&old));
  native_ability_skill_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; cooldown_rejected(); At(va)[i]^=1; }
    return true;
  });
  assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READ,&old));
  assert(!NativeReadAbilityCooldowns(player.bytes,pawn.bytes,nullptr));
  float resource_cost=0;
  const auto cost_is=[&](float expected) {
    resource_cost=-1;
    assert(NativeReadAbilityResourceRequired(player.bytes,pawn.bytes,&resource_cost) && resource_cost==expected);
  };
  for (unsigned round=0;round<1000;++round) {
    Reset(); const auto untouched=ability_manager,untouched_ability=ability0;
    cost_is(50); // No exact matching active resource buff.
    active_buffs[1]=resource_buff.bytes; cost_is(100);
    Put(resource_buff.bytes,0x130,1); cost_is(150);
    Put(resource_buff.bytes,0x130,99); cost_is(150); // LAST configured buff level.
    Put(resource_buff.bytes,0x124,0); cost_is(12.5f); // Empty SP => normal values.
    Put(resource_buff.bytes,0x124,2);
    requested_resource_buffs[1]=difficulty_buff_class.bytes;
    Put(resource_modifier.bytes,0x2C,2); cost_is(450); // Do not deduplicate modifier classes.
    Put(resource_modifier.bytes,0x2C,1); Put(resource_buff.bytes,0x104,1);
    cost_is(50); pawn.bytes[0x520]=1; cost_is(150); // Downed condition preserves actual pawn flag.
    actual_condition_class=&target_player_class; cost_is(50); // OptionalInfo is None, not the owner.
    Link(pawn,0x50C,&vehicle); cost_is(50); // No vehicle-form service.
    Put(At(0x12B28000),1,7); cost_is(0); // Non-resource game mode.
    assert(!std::memcmp(untouched.bytes,ability_manager.bytes,sizeof(ability_manager.bytes)) &&
           !std::memcmp(untouched_ability.bytes,ability0.bytes,sizeof(ability0.bytes)));
  }
  const auto resource_rejected=[&]() {
    resource_cost=99;
    assert(!NativeReadAbilityResourceRequired(player.bytes,pawn.bytes,&resource_cost) && resource_cost==0);
  };
  const auto resource_active=[&]() { Reset(); active_buffs[1]=resource_buff.bytes; };
  Reset(); Link(ability0,0x90,nullptr); resource_rejected();
  Reset(); actual_modifier_class=&other_buff_class; resource_rejected();
  Reset(); modifier_class.valid=false; resource_rejected();
  Reset(); bad_offset=true; resource_rejected();
  Reset(); bad_read=true; resource_rejected();
  Reset(); Put(ability0.bytes,0x54,0x7FC00000); resource_rejected();
  Reset(); Put(ability0.bytes,0x54,0xBF800000); resource_rejected();
  Reset(); skill_world=other_world.bytes; resource_rejected();
  Reset(); At(0x12B28000)[0]=0xE9; resource_rejected();
  Reset(); Put(resource_modifier.bytes,0x2C,33); resource_rejected();
  Reset(); Put(resource_modifier.bytes,0x30,0); resource_rejected();
  Reset(); requested_resource_buffs[0]=fine.bytes; resource_rejected();
  Reset(); active_buffs[0]=fine.bytes; resource_rejected();
  resource_active(); actual_resource_class=&other_buff_class; requested_resource_buffs[0]=other_buff_class.bytes;
  active_buffs[0]=nullptr; resource_rejected(); // Unknown actual virtual override, not no buff.
  resource_active(); Put(resource_buff.bytes,0x130,static_cast<uint32_t>(-1)); resource_rejected();
  resource_active(); Put(resource_buff.bytes,0x124,0); Put(resource_buff.bytes,0x114,0); resource_rejected();
  resource_active(); resource_sp_factors[0]=std::numeric_limits<float>::infinity(); resource_rejected();
  resource_active(); resource_sp_factors[0]=-1; resource_rejected();
  resource_active(); resource_sp_factors[0]=std::numeric_limits<float>::max(); resource_rejected();
  resource_active(); Put(resource_buff.bytes,0x104,1); resource_conditions[0]=nullptr; resource_rejected();
  resource_active(); Put(resource_buff.bytes,0x104,1); actual_condition_class=&other_buff_class; resource_rejected();
  resource_active(); Put(resource_buff.bytes,0x104,33); resource_rejected();
  resource_active(); Put(resource_buff.bytes,0x104,1); resource_condition.index=0; resource_rejected();
  resource_active(); requested_resource_buffs[0]=nullptr; cost_is(50);
  resource_active(); actual_resource_class=&float_buff_class; requested_resource_buffs[0]=float_buff_class.bytes; cost_is(100);
  resource_active(); resource_sp_factors[0]=0; cost_is(0); // Genuine zero is valid, not a failure sentinel.
  for (auto* condition_class:{&behind_class,&target_ai_class,&target_player_class,&target_sentry_class}) {
    resource_active(); Put(resource_buff.bytes,0x104,1); actual_condition_class=condition_class; cost_is(50);
  }
  resource_active(); Put(resource_buff.bytes,0x104,2); resource_conditions[1]=nullptr;
  cost_is(50); // Native condition short-circuit: later entries aren't evaluated.
  pawn.bytes[0x520]=1; resource_rejected(); // Now invalid second condition is reached.
  Reset(); ability_entries[0][0]=nullptr; Link(ability0,0x90,nullptr); cost_is(0);
  Reset(); Put(At(0x12B28000),1,0); Link(ability0,0x90,nullptr); cost_is(0);
  Reset(); Link(buddy,0x508,&shared_services); Link(buddy,0x50C,&robot); Link(buddy,0x510,&robot);
  Link(robot,0x28,&buddy); Link(robot_buff,0x40,&buddy); active_buffs[1]=resource_buff.bytes;
  assert(NativeReadAbilityResourceRequired(player.bytes,buddy.bytes,&resource_cost) && resource_cost==100);
  Reset(); g_player_services_busy=true; resource_rejected(); g_player_services_busy=false;
  Reset(); assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READWRITE,&old));
  native_ability_resource_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t>) {
    for (size_t i=0;i<sizeof(code);++i) { At(va)[i]^=1; resource_rejected(); At(va)[i]^=1; }
    return true;
  });
  assert(VirtualProtect(image,0x1E00000,PAGE_EXECUTE_READ,&old));
  assert(!NativeReadAbilityResourceRequired(player.bytes,pawn.bytes,nullptr));
  const auto restore_hud=[&]() { return NativeRestoreAbilityHud(player.bytes,pawn.bytes,hud.bytes,restore_move,restore_look); };
  for (unsigned round=0;round<3000;++round) {
    Reset();
    if (round%3==1) { const float zero=0; std::memcpy(ability_manager.bytes+0x58,&zero,4); }
    if (round%3==2) { const float full=50; std::memcpy(ability_manager.bytes+0x5C,&full,4); }
    const auto before=ability_manager, before_ability=ability1;
    const auto writes=reference_calls;
    assert(PrepareNativeAbilityHudRestore(player.bytes,pawn.bytes,hud.bytes) && reference_calls==writes && restored_commands.empty());
    assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);
    assert(restore_hud()==NativePlayerServiceResult::kApplied);
    assert(restored_commands.size()==(round%3==2 ? 4:3));
    const auto& resource_command=restored_commands[0];
    const volatile float expected_resource=round%3==2 ? 1.0f:37.0f/50;
    assert(resource_command.slot==0 && !resource_command.ready && resource_command.duration==0 &&
        resource_command.progress==expected_resource);
    const auto& cooldown_command=restored_commands[round%3==2 ? 2:1];
    const volatile float expected_progress=1.0f-4.0f/14;
    assert(cooldown_command.slot==1 && !cooldown_command.ready &&
        cooldown_command.duration==(round%3==1 ? std::numeric_limits<float>::max():2) &&
        cooldown_command.progress==expected_progress);
    assert(restored_commands.back().slot==2 && restored_commands.back().duration==std::numeric_limits<float>::max());
    assert(!std::memcmp(before.bytes,ability_manager.bytes,sizeof(before.bytes)) &&
           !std::memcmp(before_ability.bytes,ability1.bytes,sizeof(before_ability.bytes)));
    assert(reference_calls==writes+2); // Exactly one clear and one GC-aware attach.
  }
  Reset(); const auto same_writes=reference_calls;
  assert(restore_hud()==NativePlayerServiceResult::kApplied && reference_calls==same_writes); // Rehydrate without redundant assignment.
  Reset(); auto copied_move=restore_move;
  assert(NativeRestoreAbilityHud(player.bytes,pawn.bytes,hud.bytes,copied_move,restore_look)==NativePlayerServiceResult::kRejected);
  Reset(); restore_look.held=false; assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); restore_look.axis=NativeInputAxis::kMove; assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); bad_hud_prepare=true; assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); reference_ready=false; assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); reject_reference=true; Link(ability_manager,0x48,nullptr); assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); reject_hud_dispatch=true; assert(restore_hud()==NativePlayerServiceResult::kRejected);
  Reset(); reject_hud_dispatch=true; Link(ability_manager,0x48,nullptr);
  assert(restore_hud()==NativePlayerServiceResult::kStateMismatch && Read(ability_manager.bytes,0x48)==reinterpret_cast<uintptr_t>(hud.bytes));
  Reset(); assert(!PrepareNativeAbilityHudRestore(player.bytes,pawn.bytes,nullptr));
  assert(NativeRestoreAbilityHud(player.bytes,pawn.bytes,nullptr,restore_move,restore_look)==NativePlayerServiceResult::kRejected);
  for (unsigned change:{1u,2u,3u,4u,5u,6u,7u,8u,9u,10u,11u,12u,13u,15u,16u,17u}) {
    Reset(); Link(ability_manager,0x48,nullptr); mutate_context=change;
    assert(restore_hud()==NativePlayerServiceResult::kStateMismatch && restored_commands.empty());
    for (unsigned stage=0;stage<3;++stage) {
      Reset(); Link(ability_manager,0x48,nullptr); hud_mutation=change; hud_mutation_at=stage;
      assert(restore_hud()==NativePlayerServiceResult::kStateMismatch && restored_commands.size()==stage+1);
    }
  }
  // Read-only preflight accepts the existing reciprocal AI pawn, but actual
  // attachment must wait until that exact pawn has the acknowledged player.
  Reset(); Link(buddy,0x508,&shared_services); Link(buddy,0x50C,&robot); Link(buddy,0x510,&robot);
  Link(robot,0x28,&buddy); Link(robot_buff,0x40,&buddy);
  assert(PrepareNativeAbilityHudRestore(player.bytes,buddy.bytes,hud.bytes));
  assert(NativeRestoreAbilityHud(player.bytes,buddy.bytes,hud.bytes,restore_move,restore_look)==NativePlayerServiceResult::kRejected);
  assert(NativeClearAbilityHud(player.bytes,pawn.bytes)==NativePlayerServiceResult::kApplied);
  Link(player,0x1C8,&buddy); Link(player,0x300,&buddy); Link(buddy,0x1D4,&player); Link(pawn,0x1D4,&ai); Link(ai,0x1C8,&pawn);
  const auto buddy_before=buddy_ability_manager;
  assert(NativeRestoreAbilityHud(player.bytes,buddy.bytes,hud.bytes,restore_move,restore_look)==NativePlayerServiceResult::kApplied);
  assert(NativeClearAbilityHud(player.bytes,buddy.bytes)==NativePlayerServiceResult::kApplied);
  assert(!std::memcmp(buddy_before.bytes,buddy_ability_manager.bytes,sizeof(buddy_before.bytes)));
  Link(player,0x1C8,&pawn); Link(player,0x300,&pawn); Link(pawn,0x1D4,&player); Link(buddy,0x1D4,&ai); Link(ai,0x1C8,&buddy);
  Link(robot,0x28,&pawn); Link(robot_buff,0x40,&pawn);
  assert(restore_hud()==NativePlayerServiceResult::kApplied);
  g_player_services = {};
  AssertBothRejected();
  assert(VirtualFree(image, 0, MEM_RELEASE));
  std::puts("NativePlayerServicesTests passed: native setters/targeting/buff HUD, 6000 ability clears, 7000 service resolutions, 6000 cooldown and 11000 resource-cost reads, 3000 HUD restorations; ownership, skill/buff/condition policy, stable input leases and per-callback guards (not gameplay)");
}
