#include "NativePlayerFormState.h"
#include <array>
#include <cmath>
#include <cstring>
#include "Log.h"
#include "Hook.h"
#include "NativeCameraHandoff.h"
#include "NativePlayerFormCode.h"
#include "NativePlayerFormEntryCode.h"
#include "NativePlayerClearCode.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void WfcCallObjectGotoState(void* function,void* object,uint32_t name);
namespace {
struct FormBinding { HMODULE module = nullptr; DWORD thread = 0; };
FormBinding g_player_form;
thread_local bool g_player_form_busy = false;
struct FormStateSpec {
  const char* state;
  uintptr_t end;
  const wchar_t* cache;
  uint32_t cache_offset, input_offset;
  const wchar_t* form_class;
};
constexpr FormStateSpec kPlayerForms[] = {
  {"PlayerWalking",0x10B24900,nullptr,0,0,L"TnRobotForm"},
  {"PlayerInCarForm",0x10B29410,L"CarForm",0x66C,0x108,L"TnCarForm"},
  {"PlayerInPlaneForm",0x10B287C0,L"PlaneForm",0x670,0x108,L"TnPlaneForm"},
  {"PlayerInTankForm",0x10B281D0,L"TankForm",0x668,0x104,L"TnTankForm"}};
struct FormCapture {
  std::array<void*,8> objects{}; // controller,pawn,world,class,form,fine-aim,inventory,weapon
  std::array<uint32_t,8> indices{};
  void* frame = nullptr;
  void* state = nullptr;
  const FormStateSpec* spec = nullptr;
};
uintptr_t FormAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_player_form.module)+va-native_reference_code::kImageBase;
}
bool FormMemory(const void* p,size_t size,HMODULE module=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase != module) ||
      size > info.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD prot=info.Protect&0xFFu;
  if (write && prot!=PAGE_READWRITE && prot!=PAGE_WRITECOPY &&
      prot!=PAGE_EXECUTE_READWRITE && prot!=PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || prot==PAGE_EXECUTE || prot==PAGE_EXECUTE_READ ||
      prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY;
}
bool FormPointer(const void* object,size_t offset,void** value) {
  if (!object || !value || !FormMemory(static_cast<const uint8_t*>(object)+offset,4)) return false;
  std::memcpy(value,static_cast<const uint8_t*>(object)+offset,4); return true;
}
bool FormField(void* object,const wchar_t* name,uint32_t expected,void** value) {
  uint32_t offset=0;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadObjectProperty(object,name,value);
}
bool FormByte(void* object,const wchar_t* name,uint32_t expected,uint8_t required) {
  uint32_t offset=0; uint8_t value=0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadByteProperty(object,name,&value) && value==required;
}
bool FormLive(void* object,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(object,type) && UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool FormMethodMatches(void* method,uintptr_t expected) {
  if (reinterpret_cast<uintptr_t>(method)==FormAddress(expected)) return true;
  // Campaign inherits through a complete one-argument forwarding body, not
  // a direct base-method vtable pointer: mov eax,[esp+4]; push eax; call base;
  // ret 4. Admit ONLY this exact ABI-preserving shape and exact destination.
  if (!FormMemory(method,13,g_player_form.module,true)) return false;
  const auto* code=static_cast<const uint8_t*>(method);
  constexpr uint8_t head[]={0x8B,0x44,0x24,0x04,0x50,0xE8};
  constexpr uint8_t tail[]={0xC2,0x04,0x00};
  int32_t relative=0; std::memcpy(&relative,code+6,4);
  return !std::memcmp(code,head,6) && !std::memcmp(code+10,tail,3) &&
      reinterpret_cast<uintptr_t>(code+10+relative)==FormAddress(expected);
}
bool FormVirtual(void* object,uint32_t context,uint32_t slot,uintptr_t expected=0) {
  void *table=nullptr,*method=nullptr;
  return FormPointer(object,context,&table) && FormMemory(table,slot+4,g_player_form.module) &&
      FormPointer(table,slot,&method) && FormMemory(method,1,g_player_form.module,true) &&
      (!expected || FormMethodMatches(method,expected));
}
bool FormCurrentWorld(void* expected) {
  void *engine=nullptr,*context=nullptr,*slot=nullptr,*world=nullptr;
  return FormPointer(reinterpret_cast<void*>(FormAddress(0x12B10A9C)),0,&engine) &&
      FormPointer(engine,0x40,&context) && FormPointer(context,0x28,&slot) &&
      FormPointer(slot,0,&world) && world==expected;
}
bool BindPlayerFormImage(HMODULE module,DWORD thread,FormBinding* out) {
  if (!out || !module || !thread || thread!=GetCurrentThreadId()) return false;
  const auto image=reinterpret_cast<uintptr_t>(module);
  if (!native_player_form_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,image,operands);
    const auto* p=reinterpret_cast<const void*>(image+va-native_reference_code::kImageBase);
    return FormMemory(p,bytes.size(),module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  })) return false;
  *out={module,thread}; return true;
}
bool CapturePlayerForm(void* player,void* pawn,FormCapture* out,bool neutral) {
  if (!out || !g_player_form.module || GetCurrentThreadId()!=g_player_form.thread ||
      !FormLive(player,L"TnPlayerController") || !FormLive(pawn,L"TnPawn") ||
      !FormByte(player,L"Role",0x66,3) || !FormByte(pawn,L"Role",0x66,3) ||
      !FormMemory(player,0x674,nullptr,false,true)) return false;
  *out={}; auto& o=out->objects; o[0]=player; o[1]=pawn; o[3]=UnrealObjectClass(player);
  void* value=nullptr; void* current=nullptr; float blend=0;
  if (!o[3] || !FormField(player,L"Pawn",0x1C8,&value) || value!=pawn ||
      !FormField(player,L"AcknowledgedPawn",0x300,&value) || value!=pawn ||
      !FormField(pawn,L"Controller",0x1D4,&value) || value!=player ||
      !FormField(player,L"WorldInfo",0x98,&o[2]) || !FormLive(o[2],L"WorldInfo") ||
      !FormField(pawn,L"WorldInfo",0x98,&value) || value!=o[2] ||
      !FormByte(o[2],L"NetMode",0x39C,0) || !FormCurrentWorld(o[2]) ||
      !UnrealReadObjectProperty(pawn,L"_CurrentTransformation",&value) || value ||
      !UnrealReadFloatProperty(pawn,L"_TransformBlendTimeRemaining",&blend) || !std::isfinite(blend) || blend>0 ||
      !UnrealReadObjectProperty(pawn,L"_CurrentForm",&current) || !current ||
      !FormField(player,L"_FineAimManager",0x620,&o[5]) || !UnrealObjectIsA(o[5],L"TnFineAimManager") ||
      !FormField(o[5],L"Owner",0x34,&value) || value!=pawn ||
      !FormField(pawn,L"InvManager",0x424,&o[6]) || !UnrealObjectIsA(o[6],L"TnInventoryManager") ||
      !FormField(o[6],L"Instigator",0x94,&value) || value!=pawn ||
      !FormField(pawn,L"Weapon",0x428,&o[7]) ||
      (o[7] && (!FormLive(o[7],L"TnWeapon") || !FormField(o[7],L"Instigator",0x94,&value) || value!=pawn)) ||
      !FormPointer(player,0x28,&out->frame) || !FormMemory(out->frame,0x54,nullptr,false,true) ||
      !FormPointer(out->frame,0x38,&out->state) ||
      !FormVirtual(player,0,0x12C) || !FormVirtual(player,0,0x138)) return false;
  UnrealArrayView stack{}; std::memcpy(&stack,static_cast<uint8_t*>(out->frame)+0x48,sizeof(stack));
  if (stack.count!=0 || stack.capacity<0 || stack.capacity>64 ||
      (stack.capacity && !FormMemory(stack.data,static_cast<size_t>(stack.capacity)*12))) return false;
  if (neutral) {
    if ((out->state && out->state!=o[3]) || !FormVirtual(player,0x10,0x3C,0x10902770)) return false;
    o[4]=current;
  } else {
    char name[64]{};
    if (!UnrealObjectIsA(out->state,L"State") || !UnrealObjectName(out->state,name,sizeof(name))) return false;
    for (const auto& spec:kPlayerForms) if (!std::strcmp(name,spec.state)) out->spec=&spec;
    if (!out->spec || !FormVirtual(player,0x10,0x3C,out->spec->end)) return false;
    const auto& spec=*out->spec;
    if (!spec.cache && o[7] &&
        (!FormByte(o[7],L"Role",0x66,3) || !FormVirtual(o[7],0x10,0x288))) return false;
    if (!UnrealReadObjectProperty(pawn,spec.cache ? L"_VehicleForm":L"_RobotForm",&o[4]) ||
        o[4]!=current || !UnrealObjectIsA(o[4],spec.form_class)) return false;
    if (spec.cache) {
      void* input=nullptr;
      if (!FormField(player,spec.cache,spec.cache_offset,&value) || value!=o[4] ||
          !FormField(o[4],L"_InputState",spec.input_offset,&input) || !UnrealObjectValid(input) ||
          !FormVirtual(player,0x10,0x444)) return false;
    }
  }
  for (size_t i=0;i<o.size();++i) {
    out->indices[i]=UnrealObjectIndex(o[i]); if (o[i] && !out->indices[i]) return false;
  }
  return true;
}
bool FormInputHeld(void* player,const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return move.controller==player && look.controller==player && move.axis==NativeInputAxis::kMove &&
      look.axis==NativeInputAxis::kLook && NativePlayerInputHeld(move) && NativePlayerInputHeld(look);
}
}  // namespace

bool PrepareNativePlayerFormState(HMODULE module,DWORD thread,void* player) {
  if (g_player_form_busy || !module || module!=GetModuleHandleW(nullptr) || !thread || thread!=GetCurrentThreadId()) return false;
  if (!g_player_form.module && !BindPlayerFormImage(module,thread,&g_player_form)) {
    Log(LogLevel::kError,"[SquadSwitch] native player form-exit signatures mismatch"); return false;
  }
  void* pawn=nullptr; FormCapture capture;
  return module==g_player_form.module && thread==g_player_form.thread &&
      FormField(player,L"Pawn",0x1C8,&pawn) && CapturePlayerForm(player,pawn,&capture,false);
}
bool NativePlayerFormIsNeutral(void* player,void* pawn) {
  FormCapture capture; return CapturePlayerForm(player,pawn,&capture,true);
}
bool NativePlayerFormIsCurrent(void* player,void* pawn) {
  FormCapture capture; return CapturePlayerForm(player,pawn,&capture,false);
}
NativePlayerFormResult NativeLeavePlayerForm(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  FormCapture before;
  if (g_player_form_busy || !FormInputHeld(player,move,look)) return NativePlayerFormResult::kRejected;
  if (NativePlayerFormIsNeutral(player,pawn)) return NativePlayerFormResult::kApplied;
  if (!CapturePlayerForm(player,pawn,&before,false)) return NativePlayerFormResult::kRejected;
  g_player_form_busy=true;
  struct ClearBusy { ~ClearBusy() { g_player_form_busy=false; } } busy;
  Log(LogLevel::kInfo,"[SquadSwitch] leave player state=%s controller=%p pawn=%p",
      before.spec->state,player,pawn);
  WfcCallObjectGotoState(reinterpret_cast<void*>(FormAddress(native_camera_code::kGotoStateVa)),player,0);
  FormCapture after;
  if (!FormInputHeld(player,move,look) || !CapturePlayerForm(player,pawn,&after,true) ||
      after.objects!=before.objects || after.indices!=before.indices || after.frame!=before.frame) {
    Log(LogLevel::kError,"[SquadSwitch] player state exit changed identity/ownership/guards or did not reach None");
    return NativePlayerFormResult::kStateMismatch;
  }
  return NativePlayerFormResult::kApplied;
}
#include "NativePlayerFormEntry.inc"
#include "NativePlayerClear.inc"
}  // namespace wfcds
