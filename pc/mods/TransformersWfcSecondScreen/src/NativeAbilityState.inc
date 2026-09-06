// Included in wfcds after the common service/HUD ownership guards.
// Capture the existing manager. Never Initialize/AddAbility/SetHud to obtain
// this data: those paths can reset gameplay state or mark cooldowns ready.
namespace {
struct AbilityStateCapture {
  ServiceCapture services;
  NativeAbilityState state;
  void* controller=nullptr;
  std::array<UnrealArrayView,3> arrays{};
};
bool AbilityArray(void* manager,const wchar_t* name,UnrealArrayView* out) {
  return UnrealReadArrayProperty(manager,name,out) && out->count>=0 && out->count<=3 &&
      out->capacity>=out->count && out->capacity<=4096 &&
      (!out->capacity || out->data) && (out->count==0 || ServiceMemory(out->data,out->count*4));
}
bool CaptureAbilityState(void* player,void* pawn,AbilityStateCapture* c) {
  if (!c || !g_player_services.module || GetCurrentThreadId()!=g_player_services.thread) return false;
  *c={}; c->state.pawn=pawn;
  auto& s=c->state;
  void *controlled=nullptr,*owner=nullptr;
  if (!ServiceField(player,L"Pawn",0x1C8,&controlled) ||
      !UnrealReadObjectProperty(pawn,L"AbilityManager",&s.manager) ||
      !UnrealObjectIsA(s.manager,L"TnAbilityManager") ||
      !ServiceField(s.manager,L"OwnerPawn",0x44,&owner) || owner!=pawn ||
      !ServiceField(s.manager,L"OwnerHud",0x48,&s.hud) ||
      !ServiceField(pawn,L"Controller",0x1D4,&c->controller) ||
      !CaptureServices(controlled,{pawn,s.manager,c->controller,s.hud},&c->services) ||
      c->services.objects[0]!=player || !ServiceActor(pawn,L"TnPawn") ||
      !(c->controller==player || ServiceActor(c->controller,L"TnAiController")) ||
      !SameServiceOwner(c->controller,L"Pawn",pawn) ||
      !SameServiceOwner(pawn,L"WorldInfo",c->services.objects[2]) ||
      !SameServiceOwner(c->controller,L"WorldInfo",c->services.objects[2]) ||
      !BuffHudActor(s.hud,player,c->services.objects[2])) return false;
  uint8_t role=0;
  if (!UnrealReadByteProperty(pawn,L"Role",&role) || role!=3 ||
      !UnrealReadByteProperty(c->controller,L"Role",&role) || role!=3 ||
      !AbilityArray(s.manager,L"Abilities",&c->arrays[0]) ||
      !AbilityArray(s.manager,L"Cooldowns",&c->arrays[1]) ||
      !AbilityArray(s.manager,L"SpamPreventionCooldowns",&c->arrays[2]) ||
      c->arrays[0].count!=c->arrays[1].count || c->arrays[0].count!=c->arrays[2].count ||
      !UnrealReadFloatProperty(s.manager,L"CooldownMultiplier",&s.multiplier) ||
      !UnrealReadFloatProperty(s.manager,L"Resource",&s.resource) ||
      !std::isfinite(s.multiplier) || s.multiplier<0 || !std::isfinite(s.resource) || s.resource<0) return false;
  s.count=static_cast<uint32_t>(c->arrays[0].count);
  s.pawn_index=UnrealObjectIndex(pawn); s.manager_index=UnrealObjectIndex(s.manager);
  for (uint32_t i=0;i<s.count;++i) {
    if (!UnrealReadObjectArrayElement(c->arrays[0],i,&s.abilities[i]) ||
        !UnrealReadFloatArrayElement(c->arrays[1],i,&s.cooldowns[i]) ||
        !UnrealReadFloatArrayElement(c->arrays[2],i,&s.spam_cooldowns[i]) ||
        !std::isfinite(s.cooldowns[i]) || s.cooldowns[i]<0 || !std::isfinite(s.spam_cooldowns[i])) return false;
    // AddAbility(None) explicitly appends an empty slot and both timer entries.
    // Preserve its slot number; do not compact or instantiate a replacement.
    if (!s.abilities[i]) continue;
    if (!UnrealObjectIsA(s.abilities[i],L"TnAbility") ||
        !SameServiceOwner(s.abilities[i],L"OwnerPawn",pawn) ||
        !(s.ability_indices[i]=UnrealObjectIndex(s.abilities[i]))) return false;
    // Tick subtracts DeltaTime from spam prevention WITHOUT clamping to zero.
    // A finite negative spam timer is valid and must not reject or reset a pawn.
    for (uint32_t j=0;j<i;++j) if (s.abilities[i]==s.abilities[j]) return false;
  }
  return ServicesCurrent(c->services);
}
bool AbilityStateUnchanged(const AbilityStateCapture& before,const AbilityStateCapture& after) {
  const auto& a=before.state; const auto& b=after.state;
  return before.services.objects==after.services.objects && before.services.indices==after.services.indices &&
      before.controller==after.controller && a.pawn==b.pawn && a.manager==b.manager && a.hud==b.hud &&
      a.pawn_index==b.pawn_index && a.manager_index==b.manager_index && a.count==b.count &&
      a.abilities==b.abilities && a.ability_indices==b.ability_indices &&
      !std::memcmp(before.arrays.data(),after.arrays.data(),sizeof(before.arrays)) &&
      !std::memcmp(a.cooldowns.data(),b.cooldowns.data(),sizeof(a.cooldowns)) &&
      !std::memcmp(a.spam_cooldowns.data(),b.spam_cooldowns.data(),sizeof(a.spam_cooldowns)) &&
      !std::memcmp(&a.multiplier,&b.multiplier,sizeof(float)) && !std::memcmp(&a.resource,&b.resource,sizeof(float));
}
} // namespace
bool NativeReadAbilityState(void* player,void* pawn,NativeAbilityState* out) {
  if (!out) return false;
  *out={}; AbilityStateCapture c;
  if (g_player_services_busy || !CaptureAbilityState(player,pawn,&c)) return false;
  *out=c.state; return true;
}
NativePlayerServiceResult NativeClearAbilityHud(void* player,void* pawn) {
  AbilityStateCapture before;
  if (g_player_services_busy || !CaptureAbilityState(player,pawn,&before) || !UnrealObjectReferenceWritesReady())
    return NativePlayerServiceResult::kRejected;
  if (!before.state.hud) return NativePlayerServiceResult::kApplied;
  g_player_services_busy=true;
  struct ClearBusy { ~ClearBusy() { g_player_services_busy=false; } } guard;
  if (!UnrealWriteObjectPropertyExpected(before.state.manager,L"OwnerHud",before.state.hud,nullptr))
    return NativePlayerServiceResult::kRejected;
  // Expected HUD null is the ONLY allowed difference. Retain the old HUD's
  // lifetime check too: a callback invalidating it is a dirty journal result.
  if (!ServicesCurrent(before.services) || !BuffHudActor(before.state.hud,player,before.services.objects[2]))
    return NativePlayerServiceResult::kStateMismatch;
  before.state.hud=nullptr; before.services.objects[6]=nullptr; before.services.indices[6]=0;
  AbilityStateCapture after;
  return CaptureAbilityState(player,pawn,&after) && AbilityStateUnchanged(before,after)
      ? NativePlayerServiceResult::kApplied : NativePlayerServiceResult::kStateMismatch;
}
