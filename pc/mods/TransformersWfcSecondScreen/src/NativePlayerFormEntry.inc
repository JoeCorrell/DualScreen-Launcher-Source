// Included inside wfcds in NativePlayerFormState.cpp; shares its guarded
// captures and exact-build addressing instead of creating a second resolver.
namespace {
namespace entry_code = native_player_form_entry_code;
constexpr uintptr_t kFormBegins[] = {entry_code::kWalkingBeginVa,entry_code::kCarBeginVa,
    entry_code::kPlaneBeginVa,entry_code::kTankBeginVa};
constexpr uintptr_t kFormNames[] = {0x12B32150,0x12B320DC,0x12B320E0,0x12B320E8};
constexpr uintptr_t kFormProfiles[] = {entry_code::kRobotProfileVa,entry_code::kCarProfileVa,
    entry_code::kPlaneProfileVa,entry_code::kTankProfileVa};
constexpr size_t kBeginHeads[] = {6,8,8,8}; // Complete, non-relative prologues.
InlineHook g_form_begin_hooks[4];
void* g_form_begin_originals[4]{};
bool g_form_entry_prepared=false;
using FormBeginCall = void (__attribute__((fastcall)) *)(void*,void*,uint32_t);
extern "C" void WfcCallEsiValue(void*,void*,uint32_t);

struct EntryServices {
  // PlayerInput, ProfileSettings, PRI, form/stat tracker, camera manager.
  std::array<void*,5> objects{};
  std::array<uint32_t,5> indices{};
};
struct FormEntryContext {
  FormCapture base;
  EntryServices services;
  const NativePlayerInputLease *move=nullptr,*look=nullptr;
  unsigned spec=0;
  uint8_t mode=0,type=4;
  std::array<uint8_t,0x30> pose{};
  bool entered=false,inside_callback=false,failed=false;
};
thread_local FormEntryContext* g_form_entry_context=nullptr;

bool EntryArray(void* object,const wchar_t* name,size_t offset,size_t stride) {
  UnrealStructArrayView reflected{}; UnrealArrayView raw{};
  if (!UnrealReadStructArrayProperty(object,name,&reflected) ||
      !FormMemory(static_cast<uint8_t*>(object)+offset,sizeof(raw))) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+offset,sizeof(raw));
  return reflected.data==raw.data && reflected.count==raw.count && reflected.capacity==raw.capacity &&
      reflected.element_size==stride && raw.count>=0 && raw.count<=1024 &&
      raw.capacity>=raw.count && raw.capacity<=4096 &&
      (!raw.count || FormMemory(raw.data,static_cast<size_t>(raw.count)*stride));
}
bool EntryTracker(void* tracker,void* pri) {
  if (!UnrealObjectIsA(tracker,L"TnClassFormStatHelper") || !FormMemory(tracker,0x64,nullptr,false,true)) return false;
  UnrealArrayView text{}; std::memcpy(&text,static_cast<uint8_t*>(tracker)+0x28,sizeof(text));
  if (text.count<0 || text.count>256 || text.capacity<text.count || text.capacity>1024 ||
      (text.count && (!FormMemory(text.data,static_cast<size_t>(text.count)*2) ||
       static_cast<const uint16_t*>(text.data)[text.count-1]!=0))) return false;
  uint8_t form=0; uint32_t offset=0;
  if (!UnrealResolveBytePropertyOffset(tracker,L"CurrentForm",&offset) || offset!=0x34 ||
      !UnrealReadByteProperty(tracker,L"CurrentForm",&form) || form>2 ||
      form!=static_cast<uint8_t*>(tracker)[0x34]) return false;
  for (const auto position:{0x3Cu,0x40u,0x44u,0x48u,0x4Cu,0x50u}) {
    float value=0; std::memcpy(&value,static_cast<uint8_t*>(tracker)+position,4);
    if (!std::isfinite(value)) return false;
  }
  UnrealScriptDelegate callback{},raw{};
  std::memcpy(&raw,static_cast<uint8_t*>(tracker)+0x54,sizeof(raw));
  if (!UnrealReadDelegateProperty(tracker,L"__AddToGameStat__Delegate",&callback,&offset) || offset!=0x54 ||
      std::memcmp(&raw,&callback,sizeof(raw))) return false;
  if (!callback.object) return !callback.function_name && !callback.compiled_context && !callback.compiled_function;
  // PostBeginPlay binds this exact PRI's IncrementGameStat. Preserve all four
  // delegate words; a callback belonging to the old AI/another PRI is unsafe.
  return callback.object==pri && callback.function_name &&
      callback.compiled_context==static_cast<uint8_t*>(pri)+0x10 &&
      FormMemory(callback.compiled_function,1,g_player_form.module,true);
}
bool CaptureEntryServices(void* player,void* pawn,uint8_t mode,EntryServices* out,bool staged) {
  if (!out || !UnrealObjectReferenceWritesReady()) return false;
  *out={}; auto& o=out->objects; uint32_t offset=0; uint8_t physics=0; bool boost=true,invert=false;
  if (!FormField(player,L"PlayerInput",0x428,&o[0]) || !UnrealObjectIsA(o[0],L"PlayerInput") ||
      !FormMemory(o[0],0xD8,nullptr,false,true) || !UnrealReadBoolProperty(o[0],L"bInvertMouse",&invert) ||
      !FormField(player,L"ProfileSettings",0x560,&o[1]) || !UnrealObjectIsA(o[1],L"TnProfileSettings") ||
      !EntryArray(o[1],L"ProfileMappings",0x60,60) || !EntryArray(o[1],L"ProfileSettings",0x38,36) ||
      !FormField(player,L"PlayerReplicationInfo",0x1CC,&o[2]) || !FormLive(o[2],L"TnPlayerReplicationInfo") ||
      !FormField(o[2],L"_ClassFormTracker",0x4AC,&o[3]) || !EntryTracker(o[3],o[2]) ||
      !FormField(player,L"_CameraStrategyManager",0x60C,&o[4]) || !UnrealObjectIsA(o[4],L"TnCameraStrategyManager") ||
      !FormVirtual(o[4],0x10,0x48,native_camera_code::kStartFormVa) ||
      !UnrealReadBoolProperty(player,L"IsBoostPressed",&boost) || boost ||
      !UnrealResolveBytePropertyOffset(player,L"DoubleClickDir",&offset) || offset!=0x304 ||
      !UnrealResolveBytePropertyOffset(pawn,L"Physics",&offset) || offset!=0x64 ||
      !UnrealReadByteProperty(pawn,L"Physics",&physics) ||
      (!mode && physics!=1 && physics!=2 && physics!=4 && physics!=10)) return false;
  uint32_t flags=0; std::memcpy(&flags,static_cast<uint8_t*>(player)+0x614,4);
  if (flags&4) return false; // Native IsBoostPressed storage, not a UI flag.
  void* value=nullptr;
  if (!FormField(o[2],L"Owner",0x2C,&value) || value!=player ||
      (staged && (!FormField(o[4],L"_TransformerPawn",0x34,&value) || value!=pawn))) return false;
  if (!mode && physics==1 && !FormVirtual(pawn,0,0x220)) return false;
  uint32_t input_flags=0; std::memcpy(&input_flags,static_cast<uint8_t*>(o[0])+0xD4,4);
  if (invert!=((input_flags&2)!=0)) return false;
  for (size_t i=0;i<o.size();++i) { out->indices[i]=UnrealObjectIndex(o[i]); if (!out->indices[i]) return false; }
  return true;
}
bool EntryDescribe(void* pawn,uint8_t* mode,uint8_t* type,unsigned* spec) {
  if (!NativeReadPawnForm(pawn,mode,type)) return false;
  *spec=!*mode ? 0 : *type<=1 ? 1 : *type==2 ? 2 : 3;
  return *spec<4;
}
bool EntryNames(uint32_t* selected,unsigned spec) {
  uint32_t names[4]{};
  for (unsigned i=0;i<4;++i) {
    const void* address=reinterpret_cast<void*>(FormAddress(kFormNames[i]));
    if (!FormMemory(address,4,g_player_form.module)) return false;
    std::memcpy(&names[i],address,4); if (!names[i]) return false;
    for (unsigned j=0;j<i;++j) if (names[j]==names[i]) return false;
  }
  *selected=names[spec]; return true;
}
bool EntryCodeReady() {
  return g_player_form.module && entry_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(g_player_form.module),operands);
    const auto* at=reinterpret_cast<const uint8_t*>(FormAddress(va));
    if (!FormMemory(at,bytes.size(),g_player_form.module,true)) return false;
    for (unsigned i=0;i<4;++i) if (va==kFormBegins[i] && g_form_begin_hooks[i].target) {
      const auto& hook=g_form_begin_hooks[i]; int32_t relative=0; std::memcpy(&relative,at+1,4);
      if (hook.target!=at || at[0]!=0xE9 || at+5+relative!=hook.replacement ||
          hook.stolen_bytes!=kBeginHeads[i] || !hook.trampoline || g_form_begin_originals[i]!=hook.trampoline ||
          std::memcmp(hook.original,bytes.data(),hook.stolen_bytes)) return false;
      const auto* trampoline=static_cast<const uint8_t*>(hook.trampoline);
      if (!FormMemory(trampoline,hook.stolen_bytes+5,nullptr,true) ||
          std::memcmp(trampoline,bytes.data(),hook.stolen_bytes) || trampoline[hook.stolen_bytes]!=0xE9) return false;
      std::memcpy(&relative,trampoline+hook.stolen_bytes+1,4);
      if (trampoline+hook.stolen_bytes+5+relative!=at+hook.stolen_bytes) return false;
      for (size_t j=5;j<hook.stolen_bytes;++j) if (at[j]!=0x90) return false;
      return !std::memcmp(at+hook.stolen_bytes,bytes.data()+hook.stolen_bytes,bytes.size()-hook.stolen_bytes);
    }
    return !std::memcmp(at,bytes.data(),bytes.size());
  });
}
bool EntryAlive(FormEntryContext& c,bool cached) {
  if (c.failed || !FormInputHeld(c.base.objects[0],*c.move,*c.look) || !FormCurrentWorld(c.base.objects[2])) return false;
  for (size_t i=0;i<c.base.objects.size();++i)
    if (c.base.objects[i] && UnrealObjectIndex(c.base.objects[i])!=c.base.indices[i]) return false;
  EntryServices now; uint8_t mode=0,type=4; unsigned spec=0;
  void* player=c.base.objects[0]; void* pawn=c.base.objects[1]; void* value=nullptr;
  void* frame=nullptr; void* state=nullptr; char state_name[64]{};
  if (!FormLive(player,L"TnPlayerController") || !FormLive(pawn,L"TnPawn") ||
      !FormByte(player,L"Role",0x66,3) || !FormByte(pawn,L"Role",0x66,3) ||
      !FormField(player,L"WorldInfo",0x98,&value) || value!=c.base.objects[2] ||
      !FormField(pawn,L"WorldInfo",0x98,&value) || value!=c.base.objects[2] ||
      !FormPointer(player,0x28,&frame) || frame!=c.base.frame || !FormPointer(frame,0x38,&state) ||
      !UnrealObjectName(state,state_name,sizeof(state_name)) || std::strcmp(state_name,kPlayerForms[c.spec].state) ||
      !FormVirtual(player,0x10,0x38,kFormBegins[c.spec]) ||
      !EntryDescribe(pawn,&mode,&type,&spec) || mode!=c.mode || type!=c.type || spec!=c.spec ||
      !CaptureEntryServices(player,pawn,mode,&now,true) || now.objects!=c.services.objects || now.indices!=c.services.indices ||
      !FormField(player,L"Pawn",0x1C8,&value) || value!=pawn ||
      !FormField(player,L"AcknowledgedPawn",0x300,&value) || value!=pawn ||
      !FormField(pawn,L"Controller",0x1D4,&value) || value!=player ||
      !FormMemory(static_cast<uint8_t*>(pawn)+0xD8,c.pose.size()) ||
      std::memcmp(c.pose.data(),static_cast<uint8_t*>(pawn)+0xD8,c.pose.size())) return false;
  if (cached) { FormCapture after; return CapturePlayerForm(player,pawn,&after,false) &&
      FormVirtual(player,0x10,0x38,kFormBegins[c.spec]) &&
      after.objects==c.base.objects && after.indices==c.base.indices && after.frame==c.base.frame; }
  return true;
}
bool EnterProfile(FormEntryContext& c) {
  bool previous=false;
  if (!UnrealReadBoolProperty(c.services.objects[0],L"bInvertMouse",&previous)) return false;
  using Getter=uint32_t (__attribute__((fastcall)) *)(void*,void*);
  const uint32_t value=reinterpret_cast<Getter>(FormAddress(kFormProfiles[c.spec]))(
      static_cast<uint8_t*>(c.services.objects[1])+0x10,nullptr);
  if (value>1 || !EntryAlive(c,true)) return false;
  uint32_t before=0,after=0; std::memcpy(&before,static_cast<uint8_t*>(c.services.objects[0])+0xD4,4);
  if (!UnrealWriteBoolPropertyExpected(c.services.objects[0],L"bInvertMouse",previous,value!=0)) return false;
  std::memcpy(&after,static_cast<uint8_t*>(c.services.objects[0])+0xD4,4);
  return after==((before&~2u)|(value ? 2u:0u));
}
void AdaptFormBegin(unsigned spec,void* script,uint32_t previous) {
  auto* c=g_form_entry_context;
  if (!c || script!=static_cast<uint8_t*>(c->base.objects[0])+0x10) {
    if (g_form_begin_originals[spec]) reinterpret_cast<FormBeginCall>(g_form_begin_originals[spec])(script,nullptr,previous);
    return;
  }
  if (spec!=c->spec || previous || c->entered || c->inside_callback || !EntryAlive(*c,false)) { c->failed=true; return; }
  c->inside_callback=true; c->entered=true;
  struct ClearCallback { FormEntryContext& c; ~ClearCallback() { c.inside_callback=false; } } clear{*c};
  const auto fail=[&](const char* step) { c->failed=true; Log(LogLevel::kError,"[SquadSwitch] native form BeginState failed stage=%s",step); };
  void* player=c->base.objects[0]; void* pawn=c->base.objects[1];
  if (spec) {
    void* old=nullptr; const auto& form=kPlayerForms[spec];
    if (!FormField(player,form.cache,form.cache_offset,&old) ||
        !UnrealWriteObjectPropertyExpected(player,form.cache,old,c->base.objects[4])) { fail("vehicle-alias"); return; }
  }
  if (!EntryAlive(*c,true)) { fail("selected-state"); return; }
  if (!spec) {
    static_cast<uint8_t*>(player)[0x304]=0; // Native DoubleClickDir only, NOT input counters +305/+306.
    uint8_t physics=0; UnrealReadByteProperty(pawn,L"Physics",&physics);
    if (physics==1) {
      void *table=nullptr,*method=nullptr; FormPointer(pawn,0,&table); FormPointer(table,0x220,&method);
      const float floor_normal[3]={0,0,1};
      using Physics=void (__attribute__((fastcall)) *)(void*,void*,uint32_t,void*,const float*);
      reinterpret_cast<Physics>(method)(pawn,nullptr,1,nullptr,floor_normal);
      if (!EntryAlive(*c,true)) { fail("walking-physics"); return; }
    }
  } else {
    using Saved=void (__attribute__((fastcall)) *)(void*,void*);
    reinterpret_cast<Saved>(FormAddress(native_detach_code::kSavedMovesVa))(nullptr,script);
    if (!EntryAlive(*c,true)) { fail("saved-moves"); return; }
  }
  const auto camera=[&]() {
    reinterpret_cast<FormBeginCall>(FormAddress(native_camera_code::kStartFormVa))(
        static_cast<uint8_t*>(c->services.objects[4])+0x10,nullptr,c->mode);
    return EntryAlive(*c,true);
  };
  // Preserve the cooked order: walking camera -> input -> stats; vehicle
  // saved moves -> input -> stats -> camera. Boost-held entries are rejected.
  if (!spec && !camera()) { fail("robot-camera"); return; }
  if (!EnterProfile(*c) || !EntryAlive(*c,true)) { fail("profile-input"); return; }
  WfcCallEsiValue(reinterpret_cast<void*>(FormAddress(entry_code::kSetFormVa)),
      static_cast<uint8_t*>(c->services.objects[3])+0x10,c->mode);
  if (!EntryAlive(*c,true)) { fail("class-form-statistics"); return; }
  uint8_t tracked=2;
  if (!UnrealReadByteProperty(c->services.objects[3],L"CurrentForm",&tracked) || tracked!=c->mode) { fail("tracked-form"); return; }
  if (spec && !camera()) { fail("vehicle-camera"); return; }
}
void __attribute__((fastcall)) EnterWalking(void* c,void*,uint32_t p) { AdaptFormBegin(0,c,p); }
void __attribute__((fastcall)) EnterCar(void* c,void*,uint32_t p) { AdaptFormBegin(1,c,p); }
void __attribute__((fastcall)) EnterPlane(void* c,void*,uint32_t p) { AdaptFormBegin(2,c,p); }
void __attribute__((fastcall)) EnterTank(void* c,void*,uint32_t p) { AdaptFormBegin(3,c,p); }
} // namespace

bool PrepareNativePlayerFormEntry(HMODULE module,DWORD thread,void* player,void* first,void* second) {
  if (!g_player_form.module || module!=GetModuleHandleW(nullptr) || module!=g_player_form.module ||
      thread!=GetCurrentThreadId() || thread!=g_player_form.thread || g_player_form_busy || !EntryCodeReady()) return false;
  uint32_t name=0; if (!EntryNames(&name,0)) return false;
  for (auto* pawn:{first,second}) {
    uint8_t mode=0,type=4; unsigned spec=0; EntryServices services;
    if (!FormLive(pawn,L"TnPawn") || !EntryDescribe(pawn,&mode,&type,&spec) ||
        !CaptureEntryServices(player,pawn,mode,&services,false)) return false;
  }
  g_form_entry_prepared=true; return true;
}
bool InstallNativePlayerFormEntry() {
  if (!g_form_entry_prepared || g_player_form_busy || GetCurrentThreadId()!=g_player_form.thread || !EntryCodeReady()) return false;
  void* replacements[]={reinterpret_cast<void*>(&EnterWalking),reinterpret_cast<void*>(&EnterCar),
      reinterpret_cast<void*>(&EnterPlane),reinterpret_cast<void*>(&EnterTank)};
  for (unsigned i=0;i<4;++i) if (!g_form_begin_hooks[i].target) {
    auto* address=reinterpret_cast<void*>(FormAddress(kFormBegins[i])); uint8_t expected[8]{};
    std::memcpy(expected,address,kBeginHeads[i]);
    // Any partial installation remains pinned pass-through with no active TLS
    // transaction. Do not tear down a trampoline from a worker/callback stack.
    if (!InstallInlineHook(&g_form_begin_hooks[i],address,replacements[i],expected,
        kBeginHeads[i],&g_form_begin_originals[i])) return false;
  }
  return EntryCodeReady();
}
NativePlayerFormResult NativeEnterPlayerForm(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  FormEntryContext c; uint32_t name=0;
  if (!g_form_entry_prepared || GetCurrentThreadId()!=g_player_form.thread ||
      g_player_form_busy || g_form_entry_context || !FormInputHeld(player,move,look) ||
      !EntryCodeReady() || !CapturePlayerForm(player,pawn,&c.base,true) ||
      !EntryDescribe(pawn,&c.mode,&c.type,&c.spec) || !EntryNames(&name,c.spec) ||
      !CaptureEntryServices(player,pawn,c.mode,&c.services,true)) return NativePlayerFormResult::kRejected;
  for (unsigned i=0;i<4;++i) if (!g_form_begin_hooks[i].target) return NativePlayerFormResult::kRejected;
  if (!FormMemory(static_cast<uint8_t*>(c.base.objects[2])+0x338,4) ||
      !FormMemory(static_cast<uint8_t*>(pawn)+0xD8,c.pose.size())) return NativePlayerFormResult::kRejected;
  float time=0; std::memcpy(&time,static_cast<uint8_t*>(c.base.objects[2])+0x338,4);
  if (!std::isfinite(time) || time<0) return NativePlayerFormResult::kRejected;
  c.move=&move; c.look=&look; std::memcpy(c.pose.data(),static_cast<uint8_t*>(pawn)+0xD8,c.pose.size());
  g_player_form_busy=true; g_form_entry_context=&c;
  struct ResetEntry { ~ResetEntry() { g_form_entry_context=nullptr; g_player_form_busy=false; } } reset;
  Log(LogLevel::kInfo,"[SquadSwitch] native form entry state=%s pawn=%p",kPlayerForms[c.spec].state,pawn);
  // Native GotoState selects the actual controller state table and dispatches
  // BeginState. No fake state pointer/table assignment and no pawn-only cast.
  // Standalone only: ClientGotoState with the same name/None would only revisit
  // the label, not re-enter. No remote ownership/replication path is admitted.
  WfcCallObjectGotoState(reinterpret_cast<void*>(FormAddress(native_camera_code::kGotoStateVa)),player,name);
  if (!c.entered || c.failed || !EntryAlive(c,true)) {
    Log(LogLevel::kError,"[SquadSwitch] native form entry did not complete; guarded rollback required");
    return NativePlayerFormResult::kStateMismatch;
  }
  return NativePlayerFormResult::kApplied;
}
