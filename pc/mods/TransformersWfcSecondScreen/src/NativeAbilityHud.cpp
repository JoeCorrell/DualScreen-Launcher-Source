#include "NativeAbilityHud.h"
#include "NativeAbilityHudCode.h"
#include "UnrealBridge.h"
#include <cstring>

namespace wfcds {
namespace {
struct HudBinding { HMODULE module=nullptr; DWORD thread=0; };
HudBinding g_ability_hud;
thread_local bool g_ability_hud_busy=false;
bool HudMemory(const void* p,size_t bytes,HMODULE module=nullptr,bool executable=false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
      (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase!=module) ||
      bytes>info.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const auto protection=info.Protect&0xFF;
  return !executable || protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
uintptr_t HudAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_ability_hud.module)+va-native_reference_code::kImageBase;
}
bool BindAbilityHud(HMODULE module,DWORD thread,HudBinding* out) {
  if (!module || !thread || thread!=GetCurrentThreadId() || !out) return false;
  const auto base=reinterpret_cast<uintptr_t>(module);
  if (!native_ability_hud_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,base,operands);
    const auto* target=reinterpret_cast<void*>(base+va-native_reference_code::kImageBase);
    return HudMemory(target,bytes.size(),module,true) && !std::memcmp(target,bytes.data(),bytes.size());
  })) return false;
  const auto* minus_one=reinterpret_cast<float*>(base+0x12678790-native_reference_code::kImageBase);
  if (!HudMemory(minus_one,4,module) || *minus_one!=-1.0f) return false;
  *out={module,thread}; return true;
}
bool HudField(void* object,const wchar_t* name,uint32_t offset,void** value) {
  uint32_t actual=0;
  return UnrealResolveObjectPropertyOffset(object,name,&actual) && actual==offset &&
      UnrealReadObjectProperty(object,name,value);
}
bool HudOwner(void* object,const wchar_t* name,void* expected) {
  void* actual=nullptr; return UnrealReadObjectProperty(object,name,&actual) && actual==expected;
}
bool HudActor(void* object,const wchar_t* type) {
  bool dead=true; return UnrealObjectIsA(object,type) && UnrealReadBoolProperty(object,L"bDeleteMe",&dead) && !dead;
}
bool HudPointer(const void* object,size_t offset,void** out) {
  if (!object || !HudMemory(static_cast<const uint8_t*>(object)+offset,4)) return false;
  std::memcpy(out,static_cast<const uint8_t*>(object)+offset,4); return true;
}
struct HudCapture {
  std::array<void*,5> objects{}; // PC, pawn, HUD, world, GFxMovie
  std::array<uint32_t,5> indices{};
  void *native_movie=nullptr,*scaleform=nullptr;
  UnrealArrayView callback{};
  std::array<uint16_t,256> callback_text{};
};
bool CaptureHud(void* player,void* pawn,void* hud,HudCapture* c) {
  if (!c || !g_ability_hud.module || GetCurrentThreadId()!=g_ability_hud.thread ||
      !HudActor(player,L"TnPlayerController") || !HudActor(pawn,L"TnPawn") || !HudActor(hud,L"TnHUD")) return false;
  *c={}; c->objects[0]=player; c->objects[1]=pawn; c->objects[2]=hud;
  void* value=nullptr; uint8_t role=0,mode=0xFF;
  if (!HudField(player,L"Pawn",0x1C8,&value) || value!=pawn ||
      !HudField(player,L"AcknowledgedPawn",0x300,&value) || value!=pawn ||
      !HudField(pawn,L"Controller",0x1D4,&value) || value!=player ||
      !HudField(player,L"WorldInfo",0x98,&c->objects[3]) || !HudActor(c->objects[3],L"WorldInfo") ||
      !HudOwner(pawn,L"WorldInfo",c->objects[3]) || !HudOwner(hud,L"WorldInfo",c->objects[3]) ||
      !HudOwner(hud,L"PlayerOwner",player) || !HudOwner(player,L"myHUD",hud) ||
      !UnrealReadByteProperty(c->objects[3],L"NetMode",&mode) || mode!=0 ||
      !UnrealReadByteProperty(player,L"Role",&role) || role!=3 ||
      !UnrealReadByteProperty(pawn,L"Role",&role) || role!=3 ||
      !HudField(hud,L"HudMovie",0x454,&c->objects[4]) || !UnrealObjectIsA(c->objects[4],L"GFxMovie") ||
      !HudPointer(c->objects[4],0x3C,&c->native_movie) || !HudMemory(c->native_movie,0x38) ||
      !HudPointer(reinterpret_cast<void*>(HudAddress(0x12B000B4)),0,&c->scaleform) || !HudMemory(c->scaleform,4) ||
      !HudMemory(static_cast<uint8_t*>(hud)+0x4A0,sizeof(c->callback))) return false;
  std::memcpy(&c->callback,static_cast<uint8_t*>(hud)+0x4A0,sizeof(c->callback));
  if (c->callback.count<2 || c->callback.count>256 || c->callback.capacity<c->callback.count || c->callback.capacity>1024 ||
      !HudMemory(c->callback.data,c->callback.count*2)) return false;
  std::memcpy(c->callback_text.data(),c->callback.data,c->callback.count*2);
  if (c->callback_text[c->callback.count-1]) return false;
  char reflected[256]{},raw[256]{};
  if (!UnrealReadStringProperty(hud,L"UpdateAbilityCooldownCallBack",reflected,sizeof(reflected)) ||
      !UnrealReadStringValue(static_cast<uint8_t*>(hud)+0x4A0,raw,sizeof(raw)) || !raw[0] || std::strcmp(raw,reflected)) return false;
  for (size_t i=0;i<c->objects.size();++i) { c->indices[i]=UnrealObjectIndex(c->objects[i]); if (!c->indices[i]) return false; }
  return true;
}
bool HudSame(const HudCapture& c) {
  HudCapture now;
  return CaptureHud(c.objects[0],c.objects[1],c.objects[2],&now) &&
      now.objects==c.objects && now.indices==c.indices && now.native_movie==c.native_movie && now.scaleform==c.scaleform &&
      !std::memcmp(&now.callback,&c.callback,sizeof(c.callback)) && now.callback_text==c.callback_text;
}
bool HudLeases(const HudCapture& c,const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return move.controller==c.objects[0] && look.controller==c.objects[0] &&
      move.world==c.objects[3] && look.world==c.objects[3] &&
      move.axis==NativeInputAxis::kMove && look.axis==NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look);
}
} // namespace
bool PrepareNativeAbilityHud(HMODULE module,DWORD thread,void* player,void* pawn,void* hud) {
  if (g_ability_hud_busy || module!=GetModuleHandleW(nullptr) || !thread || thread!=GetCurrentThreadId()) return false;
  if (!g_ability_hud.module && !BindAbilityHud(module,thread,&g_ability_hud)) return false;
  HudCapture c; return module==g_ability_hud.module && thread==g_ability_hud.thread && CaptureHud(player,pawn,hud,&c);
}
NativeAbilityHudResult NativeDispatchAbilityHud(void* player,void* pawn,void* hud,const AbilityHudPlan& plan,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look,const NativeAbilityHudValidation& validation) {
  const AbilityHudPlan pending=plan; // Callbacks must not alter the remaining batch.
  const NativeAbilityHudValidation guard=validation;
  HudCapture c;
  if (g_ability_hud_busy || !pending.count || pending.count>pending.commands.size() ||
      !CaptureHud(player,pawn,hud,&c) || !HudLeases(c,move,look)) return NativeAbilityHudResult::kRejected;
  for (uint32_t i=0;i<pending.count;++i) {
    const auto& command=pending.commands[i];
    if (command.slot>2 || !std::isfinite(command.duration) || !std::isfinite(command.progress) ||
        (command.ready ? command.duration!=-1 || command.progress!=0 : command.duration<0)) return NativeAbilityHudResult::kRejected;
  }
  g_ability_hud_busy=true;
  struct Clear { ~Clear() { g_ability_hud_busy=false; } } clear;
  if (guard.validate && !guard.validate(guard.context)) return NativeAbilityHudResult::kRejected;
  using Resource=void (__attribute__((regparm(1),stdcall)) *)(void*,uint32_t,float,float);
  using Ready=void (__attribute__((regparm(1),stdcall)) *)(void*,uint32_t);
  for (uint32_t i=0;i<pending.count;++i) {
    const auto& command=pending.commands[i];
    void* script=static_cast<uint8_t*>(hud)+0x10;
    if (command.ready) reinterpret_cast<Ready>(HudAddress(native_ability_hud_code::kReadyVa))(script,command.slot);
    else reinterpret_cast<Resource>(HudAddress(native_ability_hud_code::kResourceVa))(script,command.slot,command.duration,command.progress);
    if (!HudLeases(c,move,look) || !HudSame(c) || (guard.validate && !guard.validate(guard.context)))
      return NativeAbilityHudResult::kStateMismatch;
  }
  // Native dispatch does not prove rendering; the stock functions discard the
  // GFx Invoke return value. Do not label this a successful gameplay handoff.
  return NativeAbilityHudResult::kDispatched;
}
} // namespace wfcds
