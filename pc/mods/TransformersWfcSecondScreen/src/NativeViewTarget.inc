// Included inside wfcds; shares the existing camera/lease guards and binding.
extern "C" void WfcCallPlayerViewTarget(void*,void*,void*,const void*);
namespace {
namespace view_code=native_view_target_code;
bool g_view_target_ready=false;
bool BindViewTargetImage() {
  if (!g_camera_handoff.module) return false;
  const auto image=reinterpret_cast<uintptr_t>(g_camera_handoff.module);
  if (!view_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto expected=native_reference_code::Relocate(code,image,operands);
    const auto* p=reinterpret_cast<void*>(CameraAddress(va));
    return CameraMemory(p,expected.size(),g_camera_handoff.module,true) && !std::memcmp(p,expected.data(),expected.size());
  })) return false;
  const uint32_t zero=0,two=0x40000000;
  for (const auto pair:{std::pair<uintptr_t,uint32_t>{0x1262720C,zero},{0x126269A4,two}}) {
    const auto* p=reinterpret_cast<void*>(CameraAddress(pair.first));
    if (!CameraMemory(p,4,g_camera_handoff.module) || std::memcmp(p,&pair.second,4)) return false;
  }
  return true;
}
bool ViewField(void* o,const wchar_t* name,uint32_t offset,void** value) {
  void* raw=nullptr;
  return CameraField(o,name,offset,value) && CameraPointer(o,offset,&raw) && raw==*value;
}
bool ViewVirtual(void* object,uint32_t slot,uintptr_t expected,bool script=false) {
  void *table=nullptr,*method=nullptr;
  return CameraPointer(object,script ? 0x10:0,&table) && CameraMemory(table,slot+4,g_camera_handoff.module) &&
      CameraPointer(table,slot,&method) && reinterpret_cast<uintptr_t>(method)==CameraAddress(expected);
}
bool ViewPureVirtual(void* object,uint32_t slot,bool self,bool script=false) {
  void *table=nullptr,*method=nullptr;
  if (!CameraPointer(object,script ? 0x10:0,&table) || !CameraMemory(table,slot+4,g_camera_handoff.module) ||
      !CameraPointer(table,slot,&method) || !CameraMemory(method,3,g_camera_handoff.module,true)) return false;
  const uint8_t expected[]={static_cast<uint8_t>(self ? 0x8B:0x33),static_cast<uint8_t>(self ? 0xC1:0xC0),0xC3};
  return !std::memcmp(method,expected,3);
}
bool ViewActorVirtuals(void* actor,bool controller) {
  void *table=nullptr,*callback=nullptr;
  const uint8_t no_callback[]={0xC2,4,0};
  // These two native RTTI-style getters are pointer/zero leaves. Do not run
  // a script controller cast on a sibling physical pawn allocation.
  return ViewPureVirtual(actor,0x2DC,controller) && ViewPureVirtual(actor,0x2E4,!controller) &&
      ViewVirtual(actor,0x4C,view_code::kCanExecuteVa) && ViewVirtual(actor,0x48,view_code::kPendingKillVa) &&
      CameraPointer(actor,0x10,&table) && CameraMemory(table,0x11C,g_camera_handoff.module) &&
      CameraPointer(table,0x118,&callback) && CameraMemory(callback,3,g_camera_handoff.module,true) &&
      !std::memcmp(callback,no_callback,3);
}
struct ViewCapture {
  CameraCapture camera;
  // Local player, canonical local class, player PRI, current ViewTarget.
  std::array<void*,4> objects{};
  std::array<uint32_t,4> indices{};
  std::array<uint8_t,0x30> pawn_pose{},other_pose{};
  std::array<void*,3> strategy_state{}; // Camera owner, active strategy, manager state table.
};
bool CaptureViewTarget(void* player,void* pawn,void* other,ViewCapture* out) {
  uint8_t mode=0,type=0;
  if (!out || !g_view_target_ready || !NativeReadPawnForm(pawn,&mode,&type) ||
      !CaptureCamera(player,pawn,other,mode,&out->camera,true)) return false;
  auto& o=out->objects; void* value=nullptr; auto* camera=out->camera.objects[5];
  if (!ViewField(player,L"Player",0x2D0,&o[0]) || !UnrealObjectIsA(o[0],L"LocalPlayer") ||
      !CameraPointer(reinterpret_cast<void*>(CameraAddress(0x12B11274)),0,&o[1]) || !UnrealObjectIsA(o[1],L"Class") ||
      !UnrealResolveClassByPath(L"Engine.LocalPlayer",&value) || value!=o[1] ||
      !ViewField(player,L"PlayerReplicationInfo",0x1CC,&o[2]) || !CameraLive(o[2],L"PlayerReplicationInfo") ||
      !ViewField(o[2],L"Owner",0x2C,&value) || value!=player ||
      !ViewField(player,L"ViewTarget",0x30C,&o[3]) || (o[3]!=player && o[3]!=pawn && o[3]!=other) ||
      !ViewField(camera,L"PCOwner",0x1C8,&value) || value!=player ||
      !CameraMemory(player,0x314,nullptr,false,true) || !CameraMemory(camera,0x374,nullptr,false,true) ||
      !ViewActorVirtuals(player,true) || !ViewActorVirtuals(pawn,false) || !ViewActorVirtuals(other,false) ||
      !ViewVirtual(player,0x354,view_code::kLocalVa)) return false;
  uint32_t offset=0;
  if (!UnrealResolveStructPropertyOffset(camera,L"ViewTarget",44,&offset) || offset!=0x31C ||
      !UnrealResolveStructPropertyOffset(camera,L"PendingViewTarget",44,&offset) || offset!=0x348 ||
      !CameraPointer(camera,0x31C,&value) || value!=o[3] ||
      !CameraPointer(camera,0x348,&value) || value) return false;
  // The engine can retarget via PRI.Owner.Pawn. Admit only the transaction's
  // local PRI (or None), so that path cannot escape to an unrelated actor.
  for (auto* actor:{pawn,other}) {
    if (!ViewField(actor,L"PlayerReplicationInfo",0x3C0,&value) || (value && value!=o[2])) return false;
    uint32_t flags=0; std::memcpy(&flags,static_cast<uint8_t*>(actor)+0x78,4);
    if (flags&0x80) return false;
    std::memcpy(&flags,static_cast<uint8_t*>(actor)+0xC,4); if (flags&0x20000000) return false;
  }
  for (auto offset_pri:{0x344u,0x370u}) {
    if (!CameraPointer(camera,offset_pri,&value) || (value && value!=o[2])) return false;
  }
  std::memcpy(out->pawn_pose.data(),static_cast<uint8_t*>(pawn)+0xD8,0x30);
  std::memcpy(out->other_pose.data(),static_cast<uint8_t*>(other)+0xD8,0x30);
  if (!ViewField(camera,L"Owner",0x2C,&out->strategy_state[0]) ||
      !ViewField(camera,L"ActiveCameraStrategy",0x3AC,&out->strategy_state[1]) ||
      !CameraPointer(out->camera.objects[4],0x10,&out->strategy_state[2])) return false;
  for (size_t i=0;i<o.size();++i) { out->indices[i]=UnrealObjectIndex(o[i]); if (!out->indices[i]) return false; }
  return true;
}
}
bool PrepareNativeViewTarget(void* player,void* current,void* other) {
  if (g_camera_handoff_busy || g_camera_handoff.module!=GetModuleHandleW(nullptr) ||
      GetCurrentThreadId()!=g_camera_handoff.thread || !BindViewTargetImage()) return false;
  g_view_target_ready=true;
  ViewCapture before;
  return CaptureViewTarget(player,current,other,&before) && before.objects[3]==current;
}
NativeCameraResult NativeSetPlayerViewTarget(void* player,void* pawn,void* other,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  ViewCapture before;
  if (g_camera_handoff_busy || !CameraGuards(player,move,look) || !CaptureViewTarget(player,pawn,other,&before))
    return NativeCameraResult::kRejected;
  g_camera_handoff_busy=true;
  struct ResetViewBusy { ~ResetViewBusy() { g_camera_handoff_busy=false; } } busy;
  // Native struct defaults, including padding, not an all-zero optional frame.
  const std::array<uint32_t,3> transition={0,1,0x40000000};
  WfcCallPlayerViewTarget(reinterpret_cast<void*>(CameraAddress(view_code::kPlayerVa)),player,pawn,transition.data());
  ViewCapture after;
  if (!CameraGuards(player,move,look) || !CaptureViewTarget(player,pawn,other,&after) ||
      after.objects[3]!=pawn || after.camera.objects!=before.camera.objects || after.camera.indices!=before.camera.indices ||
      after.camera.previous_owner!=before.camera.previous_owner || after.camera.type!=before.camera.type ||
      after.pawn_pose!=before.pawn_pose || after.other_pose!=before.other_pose ||
      after.strategy_state!=before.strategy_state) return NativeCameraResult::kStateMismatch;
  for (size_t i=0;i<3;++i) if (after.objects[i]!=before.objects[i] || after.indices[i]!=before.indices[i]) return NativeCameraResult::kStateMismatch;
  void* pri=nullptr;
  if (!CameraPointer(player,0x310,&pri) || pri) return NativeCameraResult::kStateMismatch;
  return NativeCameraResult::kApplied;
}
