// Included inside wfcds after the shared player-service guard implementation.
// Cooked TransGame.TnBuffManager.SetHud (export 8914) is exactly
// OwnerHud = NewHud. Initialize is NOT equivalent: it grows replicated arrays.
namespace {
struct BuffHudCapture {
  ServiceCapture services;
  void *player=nullptr,*pawn=nullptr,*manager=nullptr,*controller=nullptr,*old_hud=nullptr,*new_hud=nullptr;
};
bool BuffHudActor(void* hud,void* player,void* world) {
  if (!hud) return true;
  return ServiceActor(hud,L"TnHUD") && SameServiceOwner(hud,L"WorldInfo",world) &&
      SameServiceOwner(hud,L"PlayerOwner",player) && SameServiceOwner(player,L"myHUD",hud);
}
bool BuffHudLinks(const BuffHudCapture& c) {
  uint8_t role=0;
  void* world=c.services.objects[2];
  return ServiceActor(c.pawn,L"TnPawn") &&
      (ServiceActor(c.controller,L"TnAiController") || c.controller==c.player) &&
      UnrealReadByteProperty(c.pawn,L"Role",&role) && role==3 &&
      UnrealReadByteProperty(c.controller,L"Role",&role) && role==3 &&
      SameServiceOwner(c.pawn,L"Controller",c.controller) &&
      SameServiceOwner(c.controller,L"Pawn",c.pawn) &&
      SameServiceOwner(c.pawn,L"WorldInfo",world) && SameServiceOwner(c.controller,L"WorldInfo",world) &&
      UnrealObjectIsA(c.manager,L"TnBuffManager") &&
      SameServiceOwner(c.pawn,L"_BuffManager",c.manager) && SameServiceOwner(c.manager,L"OwnerPawn",c.pawn) &&
      BuffHudActor(c.old_hud,c.player,world) && BuffHudActor(c.new_hud,c.player,world) &&
      (!c.new_hud || c.pawn==c.services.objects[1]);
}
bool CaptureBuffHud(void* player,void* pawn,void* new_hud,BuffHudCapture* c) {
  if (!c || !g_player_services.module || GetCurrentThreadId()!=g_player_services.thread ||
      !UnrealObjectReferenceWritesReady()) return false;
  *c={}; c->player=player; c->pawn=pawn; c->new_hud=new_hud;
  void* controlled=nullptr;
  if (!ServiceField(player,L"Pawn",0x1C8,&controlled) ||
      !UnrealReadObjectProperty(pawn,L"_BuffManager",&c->manager) ||
      !UnrealObjectIsA(c->manager,L"TnBuffManager") ||
      !UnrealReadObjectProperty(c->manager,L"OwnerHud",&c->old_hud) ||
      !ServiceField(pawn,L"Controller",0x1D4,&c->controller) ||
      !CaptureServices(controlled,{pawn,c->manager,c->controller,c->old_hud,new_hud},&c->services) ||
      c->services.objects[0]!=player) return false;
  return BuffHudLinks(*c);
}
} // namespace

bool PrepareNativeBuffHud(void* player,void* pawn) {
  BuffHudCapture capture;
  // Read-only: inspect either owned squad pawn without requiring the AI pawn
  // to be the current player. Attaching a HUD later requires the new player pair.
  return !g_player_services_busy && CaptureBuffHud(player,pawn,nullptr,&capture) &&
      (!capture.old_hud || pawn==capture.services.objects[1]);
}
NativePlayerServiceResult NativeSetBuffHud(void* player,void* pawn,void* hud) {
  BuffHudCapture c;
  if (g_player_services_busy || !CaptureBuffHud(player,pawn,hud,&c)) return NativePlayerServiceResult::kRejected;
  g_player_services_busy=true;
  struct ClearBusy { ~ClearBusy() { g_player_services_busy=false; } } guard;
  // No buff reinitialization, expiration, inventory change or runtime copying.
  if (!UnrealWriteObjectPropertyExpected(c.manager,L"OwnerHud",c.old_hud,hud)) return NativePlayerServiceResult::kRejected;
  return ServicesCurrent(c.services) && BuffHudLinks(c) && SameServiceOwner(c.manager,L"OwnerHud",hud)
      ? NativePlayerServiceResult::kApplied : NativePlayerServiceResult::kStateMismatch;
}
