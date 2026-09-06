// Rebind only HUD ownership; reconstruct the display from the same existing
// manager. Stock SetHud's unconditional FinishAbilityCooldown notifications
// are not appropriate for a mid-cooldown custom control transfer.
namespace {
bool CaptureAbilityHudRestore(void* player,void* pawn,void* hud,
    AbilityStateCapture* state,AbilityHudPlan* plan) {
  if (!hud || g_player_services_busy || !CaptureAbilityState(player,pawn,state) ||
      !UnrealObjectReferenceWritesReady() ||
      !BuffHudActor(hud,player,state->services.objects[2]) || !UnrealObjectIndex(hud) ||
      (state->state.hud && state->services.objects[1]!=pawn)) return false;
  // Bind/capture the actual current player's movie even during target-AI
  // preflight. No HUD owner change or Scaleform invocation occurs here.
  if (!PrepareNativeAbilityHud(g_player_services.module,g_player_services.thread,
      player,state->services.objects[1],hud)) return false;
  std::array<float,3> durations{}; float cost=0;
  if (!NativeReadAbilityCooldowns(player,pawn,&durations) ||
      !NativeReadAbilityResourceRequired(player,pawn,&cost)) return false;
  std::array<AbilityHudSlot,3> slots{};
  for (uint32_t i=0;i<state->state.count;++i)
    slots[i]={state->state.abilities[i]!=nullptr,state->state.cooldowns[i],durations[i]};
  AbilityStateCapture after;
  return CaptureAbilityState(player,pawn,&after) && AbilityStateUnchanged(*state,after) &&
      BuildAbilityHudPlan(slots,state->state.multiplier,state->state.resource,cost,plan);
}
struct AbilityRestoreGuard {
  AbilityStateCapture expected;
  // NativePlayerInputHeld validates the ORIGINAL lease address. Copies are
  // snapshots only and do not own the input lock.
  const NativePlayerInputLease *move=nullptr,*look=nullptr;
  void* hud=nullptr;
  uint32_t hud_index=0;
};
bool AbilityRestoreCurrent(void* context) {
  const auto& g=*static_cast<const AbilityRestoreGuard*>(context);
  const auto& s=g.expected;
  AbilityStateCapture now;
  return g.hud_index && UnrealObjectIndex(g.hud)==g.hud_index &&
      BuffHudActor(g.hud,s.services.objects[0],s.services.objects[2]) &&
      g.move && g.look && g.move->axis==NativeInputAxis::kMove && g.look->axis==NativeInputAxis::kLook &&
      g.move->controller==s.services.objects[0] && g.look->controller==s.services.objects[0] &&
      g.move->world==s.services.objects[2] && g.look->world==s.services.objects[2] &&
      NativePlayerInputHeld(*g.move) && NativePlayerInputHeld(*g.look) &&
      CaptureAbilityState(s.services.objects[0],s.state.pawn,&now) && AbilityStateUnchanged(s,now);
}
} // namespace
bool PrepareNativeAbilityHudRestore(void* player,void* pawn,void* hud) {
  AbilityStateCapture state; AbilityHudPlan plan;
  return CaptureAbilityHudRestore(player,pawn,hud,&state,&plan);
}
NativePlayerServiceResult NativeRestoreAbilityHud(void* player,void* pawn,void* hud,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  AbilityRestoreGuard guard; AbilityHudPlan plan;
  if (!CaptureAbilityHudRestore(player,pawn,hud,&guard.expected,&plan) ||
      guard.expected.services.objects[1]!=pawn || guard.expected.controller!=player)
    return NativePlayerServiceResult::kRejected;
  guard.move=&move; guard.look=&look; guard.hud=hud; guard.hud_index=UnrealObjectIndex(hud);
  if (!AbilityRestoreCurrent(&guard)) return NativePlayerServiceResult::kRejected;
  g_player_services_busy=true;
  struct Clear { ~Clear() { g_player_services_busy=false; } } clear;
  const bool changed=guard.expected.state.hud!=hud;
  if (changed && !UnrealWriteObjectPropertyExpected(guard.expected.state.manager,L"OwnerHud",guard.expected.state.hud,hud))
    return NativePlayerServiceResult::kRejected;
  guard.expected.state.hud=hud;
  guard.expected.services.objects[6]=hud; guard.expected.services.indices[6]=guard.hud_index;
  if (!AbilityRestoreCurrent(&guard)) return NativePlayerServiceResult::kStateMismatch;
  const auto sent=NativeDispatchAbilityHud(player,pawn,hud,plan,move,look,{AbilityRestoreCurrent,&guard});
  if (sent==NativeAbilityHudResult::kRejected)
    return changed ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected;
  if (sent!=NativeAbilityHudResult::kDispatched || !AbilityRestoreCurrent(&guard))
    return NativePlayerServiceResult::kStateMismatch;
  // kApplied means references and native dispatch passed their postconditions,
  // not proof of rendering or a completed player/AI/mission handoff.
  return NativePlayerServiceResult::kApplied;
}
