// Shares the form module's exact-build resolver, reciprocal ownership capture,
// registered form caches, game-thread checks and input leases.
namespace {
namespace clear_code=native_player_clear_code;
constexpr uintptr_t kClearMethods[]={clear_code::kWalkingVa,clear_code::kCarVa,clear_code::kPlaneVa,clear_code::kTankVa};
constexpr uintptr_t kStopBoostMethods[]={0,clear_code::kCarStopBoostVa,clear_code::kPlaneStopBoostVa,clear_code::kTankStopBoostVa};
bool g_player_clear_bound=false;
bool BindPlayerClearImage() {
  if (!g_player_form.module || GetCurrentThreadId()!=g_player_form.thread) return false;
  return clear_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(g_player_form.module),operands);
    const auto* p=reinterpret_cast<void*>(FormAddress(va));
    return FormMemory(p,bytes.size(),g_player_form.module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool ClearMethod(void* object,uint32_t slot,uintptr_t expected) {
  void *table=nullptr,*method=nullptr;
  if (!FormPointer(object,0x10,&table) || !FormMemory(table,slot+4,g_player_form.module) ||
      !FormPointer(table,slot,&method) || !FormMemory(method,6,g_player_form.module,true)) return false;
  if (reinterpret_cast<uintptr_t>(method)==FormAddress(expected)) return true;
  // Only a no-argument call/return or tail-jump forwarding thunk is equivalent.
  // Do not accept a state-name match or an arbitrary executable target.
  const auto* code=static_cast<const uint8_t*>(method); int32_t rel=0;
  if (code[0]!=0xE9 && !(code[0]==0xE8 && code[5]==0xC3)) return false;
  std::memcpy(&rel,code+1,4);
  return reinterpret_cast<uintptr_t>(code+5+rel)==FormAddress(expected);
}
struct ClearCapture {
  FormCapture form;
  void* input=nullptr;
  uint32_t input_index=0,flags=0;
  unsigned index=0;
};
bool CapturePlayerClear(void* player,void* pawn,ClearCapture* out) {
  if (!out || !g_player_clear_bound || !CapturePlayerForm(player,pawn,&out->form,false)) return false;
  const auto index=static_cast<unsigned>(out->form.spec-kPlayerForms); out->index=index;
  if (index>=4 || !ClearMethod(player,0x444,kClearMethods[index]) ||
      !FormMemory(static_cast<uint8_t*>(player)+0x614,4,nullptr,false,true) ||
      (out->form.objects[7] && (!FormByte(out->form.objects[7],L"Role",0x66,3) ||
       !FormVirtual(out->form.objects[7],0x10,0x288)))) return false;
  std::memcpy(&out->flags,static_cast<uint8_t*>(player)+0x614,4);
  bool deferred=false,allowed=false,boost=false;
  if (!UnrealReadBoolProperty(player,L"bDeferredFire",&deferred) ||
      !UnrealReadBoolProperty(player,L"IsAllowedToFire",&allowed) ||
      !UnrealReadBoolProperty(player,L"IsBoostPressed",&boost) ||
      deferred!=((out->flags&1)!=0) || allowed!=((out->flags&2)!=0) || boost!=((out->flags&4)!=0)) return false;
  if (!index) {
    // Stock EndBlock/AbortMelee cast to TnPlayerPawn before service lookup.
    // They are not safe cross-role adapters and must not silently do nothing
    // for a logical player that is still a physical TnAiPawn allocation.
    return UnrealObjectIsA(pawn,L"TnPlayerPawn") &&
        ClearMethod(player,0x4A4,0x10B1C980) &&
        (ClearMethod(player,0x440,clear_code::kGlobalVa) || ClearMethod(player,0x440,clear_code::kBaseVa));
  }
  const wchar_t* input_type=index==1 ? L"TnCarInputState":index==2 ? L"TnPlaneInputState":L"TnTankInputState";
  void* owner=nullptr;
  return ClearMethod(player,0x4D4,kStopBoostMethods[index]) &&
      FormField(out->form.objects[4],L"_InputState",out->form.spec->input_offset,&out->input) &&
      UnrealObjectIsA(out->input,input_type) && (out->input_index=UnrealObjectIndex(out->input)) &&
      FormMemory(out->input,0x60,nullptr,false,true) &&
      FormPointer(out->input,0x2C,&owner) && owner==pawn &&
      FormVirtual(out->form.objects[4],0x10,0xD0);
}
bool ClearSame(const ClearCapture& before,void* player,void* pawn) {
  ClearCapture now;
  return CapturePlayerClear(player,pawn,&now) && before.index==now.index &&
      before.form.objects==now.form.objects && before.form.indices==now.form.indices &&
      before.form.frame==now.form.frame && before.form.state==now.form.state &&
      before.input==now.input && before.input_index==now.input_index;
}
} // namespace
bool PrepareNativePlayerClearInputs(void* player,void* pawn) {
  if (g_player_form_busy || !g_player_form.module || GetCurrentThreadId()!=g_player_form.thread) return false;
  g_player_clear_bound=BindPlayerClearImage();
  ClearCapture capture; return CapturePlayerClear(player,pawn,&capture);
}
NativePlayerFormResult NativeClearPlayerFormInputs(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  ClearCapture before;
  if (g_player_form_busy || !FormInputHeld(player,move,look) ||
      !PrepareNativePlayerClearInputs(player,pawn) || !CapturePlayerClear(player,pawn,&before))
    return NativePlayerFormResult::kRejected;
  g_player_form_busy=true;
  struct ClearBusy { ~ClearBusy() { g_player_form_busy=false; } } guard;
  using Call=void (__attribute__((fastcall)) *)(void*,void*);
  reinterpret_cast<Call>(FormAddress(kClearMethods[before.index]))(static_cast<uint8_t*>(player)+0x10,nullptr);
  if (!FormInputHeld(player,move,look) || !ClearSame(before,player,pawn)) return NativePlayerFormResult::kStateMismatch;
  uint32_t flags=0; std::memcpy(&flags,static_cast<uint8_t*>(player)+0x614,4);
  if (flags!=((before.flags&~7u)|2u)) return NativePlayerFormResult::kStateMismatch;
  if (before.index) {
    const auto* input=static_cast<const uint8_t*>(before.input);
    const size_t flags_offset=before.index==2 ? 0x4C:0x48;
    const uint32_t mask=before.index==3 ? 3u:15u;
    uint32_t input_flags=0; std::memcpy(&input_flags,input+flags_offset,4);
    if (input_flags&mask) return NativePlayerFormResult::kStateMismatch;
    for (size_t offset=0x3C;offset<flags_offset;offset+=4) {
      float axis=1; std::memcpy(&axis,input+offset,4);
      if (axis!=0) return NativePlayerFormResult::kStateMismatch;
    }
    if (before.index!=3) for (size_t offset=flags_offset+4;offset<flags_offset+16;offset+=4) {
      uint32_t axis=1; std::memcpy(&axis,input+offset,4);
      if (axis) return NativePlayerFormResult::kStateMismatch;
    }
  }
  return NativePlayerFormResult::kApplied;
}
