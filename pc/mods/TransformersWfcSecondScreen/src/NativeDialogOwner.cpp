#include "NativeDialogOwner.h"
#include "UnrealBridge.h"
#include <array>
#include <cstring>
namespace wfcds {
namespace {
DWORD g_dialog_thread=0;
thread_local bool g_dialog_busy=false;
bool DialogLive(void* object,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool DialogMemory(const void* p,size_t size,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      size>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const auto prot=m.Protect&0xFF;
  return !write || prot==PAGE_READWRITE || prot==PAGE_WRITECOPY || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY;
}
bool DialogField(void* object,const wchar_t* name,void** value,uint32_t* offset=nullptr,bool write=false) {
  uint32_t off=0; void* raw=nullptr;
  if (!object || !value || !UnrealResolveObjectPropertyOffset(object,name,&off) || off<0x28 || off>0x1000 || (off&3) ||
      !DialogMemory(static_cast<uint8_t*>(object)+off,4,write) || !UnrealReadObjectProperty(object,name,value)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+off,4);
  if (offset) *offset=off;
  return raw==*value;
}
struct DialogCapture {
  // controller, requested pawn, other journal pawn, world, manager, old base/typed owners.
  std::array<void*,7> objects{};
  std::array<uint32_t,7> indices{};
  std::array<uint32_t,2> offsets{};
  bool player=false,proxy_restore=false;
  const wchar_t* Alias() const { return player ? L"OwnerPlayerPawn":L"OwnerAiPawn"; }
};
bool CaptureDialog(void* controller,void* pawn,void* other,DialogCapture* c,bool restore_proxy=false) {
  if (!c || !g_dialog_thread || GetCurrentThreadId()!=g_dialog_thread || !UnrealObjectReferenceWritesReady() || pawn==other) return false;
  *c={}; auto& o=c->objects; o[0]=controller; o[1]=pawn; o[2]=other;
  c->player=DialogLive(controller,L"TnPlayerController");
  c->proxy_restore=restore_proxy;
  if ((!c->player && !DialogLive(controller,L"TnAiController")) ||
      !DialogLive(pawn,c->player ? L"TnPawn":L"TnAiPawn") || !DialogLive(other,L"TnPawn")) return false;
  void* value=nullptr; void* controlled=nullptr; uint8_t mode=0xFF,role=0;
  if (!DialogField(controller,L"Pawn",&controlled) ||
      (restore_proxy ? (c->player || controlled!=other || !DialogLive(other,L"TnPlayerPawn")) : controlled!=pawn) ||
      !DialogField(controlled,L"Controller",&value) || value!=controller ||
      (c->player && (!DialogField(controller,L"AcknowledgedPawn",&value) || value!=pawn)) ||
      !DialogField(controller,L"WorldInfo",&o[3]) || !DialogLive(o[3],L"WorldInfo") ||
      !UnrealReadByteProperty(o[3],L"NetMode",&mode) || mode!=0) return false;
  for (auto* actor:{controller,pawn,other}) {
    if (!UnrealReadByteProperty(actor,L"Role",&role) || role!=3 ||
        !DialogField(actor,L"WorldInfo",&value) || value!=o[3]) return false;
  }
  char type[64]{};
  if (!DialogField(controller,L"_DialogManager",&o[4]) || !UnrealObjectClassName(o[4],type,sizeof(type)) ||
      std::strcmp(type,c->player ? "TnDialogManagerPlayer":"TnDialogManagerAi") ||
      !DialogField(o[4],L"OwnerPawn",&o[5],&c->offsets[0],true) ||
      !DialogField(o[4],c->Alias(),&o[6],&c->offsets[1],true) || c->offsets[0]==c->offsets[1]) return false;
  for (size_t i=5;i<7;++i) {
    // A suspended role proxy can retain the ORIGINAL physical AI owner while
    // the player has since swapped to a third squad member. Validate that
    // retained actor, rather than incorrectly restricting it to this pair.
    if (!DialogLive(o[i],L"TnPawn") || !DialogField(o[i],L"WorldInfo",&value) || value!=o[3]) return false;
    if (!c->player && !UnrealObjectIsA(o[i],L"TnAiPawn")) return false;
  }
  for (size_t i=0;i<o.size();++i) { c->indices[i]=UnrealObjectIndex(o[i]); if (!c->indices[i]) return false; }
  return true;
}
bool SameDialogContext(const DialogCapture& c,void* base,void* alias) {
  DialogCapture now;
  if (!CaptureDialog(c.objects[0],c.objects[1],c.objects[2],&now,c.proxy_restore) || now.player!=c.player || now.offsets!=c.offsets ||
      now.objects[5]!=base || now.objects[6]!=alias) return false;
  for (size_t i=0;i<5;++i) if (now.objects[i]!=c.objects[i] || now.indices[i]!=c.indices[i]) return false;
  for (size_t i=5;i<7;++i) if (UnrealObjectIndex(c.objects[i])!=c.indices[i]) return false;
  return true;
}
}
bool PrepareNativeDialogOwners(DWORD thread,void* player,void* ai,void* first,void* second) {
  if (g_dialog_busy || !thread || GetCurrentThreadId()!=thread || (g_dialog_thread && g_dialog_thread!=thread) ||
      !UnrealObjectReferenceWritesReady()) return false;
  g_dialog_thread=thread;
  DialogCapture p,a;
  // A proxy retains its original physical-AI dialog owners on purpose. Its
  // AI-only notifications are not initialized against a physical player.
  if (!CaptureDialog(player,first,second,&p) || !p.player || p.objects[5]!=first || p.objects[6]!=first) return false;
  if (UnrealObjectIsA(second,L"TnAiPawn")) {
    return CaptureDialog(ai,second,first,&a) && !a.player && a.objects[5]==second && a.objects[6]==second;
  }
  void *value=nullptr,*manager=nullptr,*owner=nullptr,*alias=nullptr; char type[64]{};
  uint8_t role=0; uint32_t base_offset=0,alias_offset=0;
  return DialogLive(ai,L"TnAiController") && DialogLive(second,L"TnPlayerPawn") &&
      UnrealReadByteProperty(ai,L"Role",&role) && role==3 &&
      DialogField(ai,L"Pawn",&value) && value==second && DialogField(second,L"Controller",&value) && value==ai &&
      DialogField(ai,L"WorldInfo",&value) && value==p.objects[3] &&
      DialogField(second,L"WorldInfo",&value) && value==p.objects[3] &&
      DialogField(ai,L"_DialogManager",&manager) && UnrealObjectClassName(manager,type,sizeof(type)) &&
      !std::strcmp(type,"TnDialogManagerAi") && UnrealObjectIndex(manager) &&
      DialogField(manager,L"OwnerPawn",&owner,&base_offset,true) &&
      DialogField(manager,L"OwnerAiPawn",&alias,&alias_offset,true) && base_offset!=alias_offset &&
      alias==owner && DialogLive(owner,L"TnAiPawn") &&
      DialogField(owner,L"WorldInfo",&value) && value==p.objects[3];
}
NativePlayerServiceResult NativeInitializeDialogOwner(void* controller,void* pawn,void* other,bool restore_proxy) {
  DialogCapture c;
  if (g_dialog_busy || !CaptureDialog(controller,pawn,other,&c,restore_proxy)) return NativePlayerServiceResult::kRejected;
  g_dialog_busy=true; struct ClearBusy { ~ClearBusy() { g_dialog_busy=false; } } busy;
  // Export 7208 only assigns OwnerPawn; exports 19584/7286 then assign their
  // private typed alias. No sound/cache/timer initialization is part of these
  // functions. Preserve both engine reference barriers and assignment order.
  if (!UnrealWriteObjectPropertyExpected(c.objects[4],L"OwnerPawn",c.objects[5],pawn)) return NativePlayerServiceResult::kRejected;
  if (!SameDialogContext(c,pawn,c.objects[6]) ||
      !UnrealWriteObjectPropertyExpected(c.objects[4],c.Alias(),c.objects[6],pawn) ||
      !SameDialogContext(c,pawn,pawn)) return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
