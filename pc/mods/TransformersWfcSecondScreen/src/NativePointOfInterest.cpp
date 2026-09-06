#include "NativePointOfInterest.h"
#include "NativePointOfInterestCode.h"
#include "UnrealBridge.h"
#include <array>
#include <cstring>
namespace wfcds {
extern "C" void WfcCallEaxEcx(void*,void*,void*);
namespace {
namespace poi_code=native_point_of_interest_code;
HMODULE g_poi_module=nullptr;
DWORD g_poi_thread=0;
thread_local bool g_poi_busy=false;
uintptr_t PoiAddress(uintptr_t va) { return reinterpret_cast<uintptr_t>(g_poi_module)+va-native_reference_code::kImageBase; }
bool PoiMemory(const void* p,size_t n,bool write=false,HMODULE image=nullptr,bool execute=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image) || n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const auto prot=m.Protect&0xFF;
  return (!write || prot==PAGE_READWRITE || prot==PAGE_WRITECOPY || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY) &&
      (!execute || prot==PAGE_EXECUTE || prot==PAGE_EXECUTE_READ || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY);
}
bool PoiRead(void* p,uint32_t offset,void* out,size_t size=4) {
  if (!p || !out || !PoiMemory(static_cast<uint8_t*>(p)+offset,size)) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+offset,size); return true;
}
bool PoiField(void* p,const wchar_t* name,uint32_t off,void** value) {
  uint32_t reflected=0; void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(p,name,&reflected) && reflected==off &&
      UnrealReadObjectProperty(p,name,value) && PoiRead(p,off,&raw) && raw==*value;
}
bool PoiLive(void* p,const wchar_t* type) {
  bool dead=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) && UnrealReadBoolProperty(p,L"bDeleteMe",&dead) && !dead;
}
bool PoiByte(void* p,const wchar_t* name,uint32_t off,uint8_t wanted) {
  uint32_t reflected=0; uint8_t v=0,raw=0;
  return UnrealResolveBytePropertyOffset(p,name,&reflected) && reflected==off &&
      UnrealReadByteProperty(p,name,&v) && v==wanted && PoiRead(p,off,&raw,1) && raw==v;
}
bool PoiFlags(void* p,const wchar_t* name,uint32_t off,uint32_t mask,uint32_t* flags) {
  bool value=false;
  return PoiRead(p,off,flags) && PoiMemory(static_cast<uint8_t*>(p)+off,4,true) &&
      UnrealReadBoolProperty(p,name,&value) && value==((*flags&mask)!=0);
}
bool BindPoiImage(HMODULE module) {
  return poi_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(module),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module)+va-native_reference_code::kImageBase);
    return PoiMemory(p,bytes.size(),false,module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
struct PoiItem { void* object=nullptr; uint32_t index=0; uint8_t action=0; bool matches=false; };
struct PoiList {
  UnrealArrayView view{};
  std::array<PoiItem,256> items{};
};
// The compiled command loops resolve classes through the compact object-type
// table, then traverse UClass.SuperField. Validate that exact graph, including
// termination, before entering the native loop (not just IsA(command)).
bool PoiCommandClass(void* object,void* command_class,bool* matches,bool by_name) {
  void* cls=UnrealObjectClass(object); void* table_class=nullptr; uint32_t packed=0,wanted_name=0;
  if (!cls || !PoiRead(object,0x14,&packed) || !(packed&0xFFF) ||
      !PoiRead(reinterpret_cast<void*>(PoiAddress(0x12A29FD4)),(packed&0xFFF)*4,&table_class) || cls!=table_class ||
      !PoiRead(command_class,4,&wanted_name)) return false;
  *matches=false;
  std::array<void*,64> visited{};
  for (size_t i=0;cls && i<visited.size();++i) {
    if (!UnrealObjectIsA(cls,L"Class") || !UnrealObjectIndex(cls)) return false;
    for (size_t j=0;j<i;++j) if (visited[j]==cls) return false;
    visited[i]=cls; uint32_t name=0; void* parent=nullptr;
    if (!PoiRead(cls,4,&name) || !PoiRead(cls,0x28,&parent)) return false;
    if (by_name ? name==wanted_name:cls==command_class) *matches=true;
    cls=parent;
  }
  return !cls;
}
bool PoiArray(void* owner,const wchar_t* name,uint32_t off,uint32_t stride,PoiList* out,
              void* command_class=nullptr,bool notifications=false) {
  if (!out || !PoiMemory(static_cast<uint8_t*>(owner)+off,12,true) || !PoiRead(owner,off,&out->view,12) ||
      out->view.count<0 || out->view.count>256 || out->view.capacity<out->view.count || out->view.capacity>512 ||
      (out->view.capacity && !PoiMemory(out->view.data,out->view.capacity*stride,true))) return false;
  if (notifications) {
    UnrealStructArrayView a{};
    if (!UnrealReadStructArrayProperty(owner,name,&a) || a.element_size!=8 || a.data!=out->view.data ||
        a.count!=out->view.count || a.capacity!=out->view.capacity) return false;
  } else {
    UnrealArrayView a{};
    if (!UnrealReadArrayProperty(owner,name,&a) || a.data!=out->view.data || a.count!=out->view.count || a.capacity!=out->view.capacity) return false;
  }
  for (int32_t i=0;i<out->view.count;++i) {
    auto& item=out->items[i];
    if (!PoiRead(out->view.data,i*stride,&item.object) || !(item.index=UnrealObjectIndex(item.object)) ||
        !UnrealObjectIsA(item.object,command_class ? L"TnContextualCommand":L"Class")) return false;
    if (command_class && !PoiCommandClass(item.object,command_class,&item.matches,notifications)) return false;
    if (notifications && (!PoiRead(out->view.data,i*stride+4,&item.action,1) || item.action>1)) return false;
  }
  return true;
}
struct PoiCapture {
  // Controller, pawn, world, POI manager, command, commander, camera manager,
  // camera, focus-state class, exact command class. Active POI is optional.
  std::array<void*,11> objects{};
  std::array<uint32_t,11> indices{};
  PoiList commands,notifications,states;
  uint32_t focus_flags=0,camera_flags=0;
  void* command_poi=nullptr;
  uint32_t command_poi_index=0;
};
bool CapturePoi(void* player,void* pawn,void* manager,PoiCapture* c) {
  if (!c || !g_poi_module || GetCurrentThreadId()!=g_poi_thread || !UnrealObjectReferenceWritesReady() ||
      !PoiLive(player,L"TnPlayerController") || !PoiLive(pawn,L"TnPawn") ||
      !PoiByte(player,L"Role",0x66,3) || !PoiByte(pawn,L"Role",0x66,3)) return false;
  *c={}; auto& o=c->objects; o[0]=player; o[1]=pawn; o[3]=manager; void* value=nullptr;
  char type[80]{};
  if (!PoiField(player,L"Pawn",0x1C8,&value) || value!=pawn ||
      !PoiField(player,L"AcknowledgedPawn",0x300,&value) || value!=pawn ||
      !PoiField(pawn,L"Controller",0x1D4,&value) || value!=player ||
      !PoiField(player,L"WorldInfo",0x98,&o[2]) || !PoiLive(o[2],L"WorldInfo") || !PoiByte(o[2],L"NetMode",0x39C,0) ||
      !PoiField(pawn,L"WorldInfo",0x98,&value) || value!=o[2] ||
      !PoiField(player,L"_PointOfInterestManager",0x67C,&value) || value!=manager ||
      !UnrealObjectClassName(manager,type,sizeof(type)) || std::strcmp(type,"TnPointOfInterestManager") ||
      !PoiField(manager,L"Owner",0x2C,&value) || value!=player ||
      !PoiField(manager,L"_Command",0x34,&o[4]) || !UnrealObjectClassName(o[4],type,sizeof(type)) || std::strcmp(type,"TnContextualCommandPointOfInterest") ||
      !PoiField(manager,L"ActivePointOfInterest",0x28,&o[10]) ||
      !PoiField(o[4],L"PointOfInterest",0x34,&c->command_poi) ||
      !PoiMemory(static_cast<uint8_t*>(manager)+0x28,16,true) || !PoiMemory(static_cast<uint8_t*>(o[4])+0x34,4,true) ||
      !PoiFlags(manager,L"bFocusing",0x30,1,&c->focus_flags) ||
      !PoiField(player,L"ContextualCommander",0x674,&o[5]) || !UnrealObjectIsA(o[5],L"TnContextualCommandHandler") ||
      !PoiField(player,L"_CameraStrategyManager",0x60C,&o[6]) || !UnrealObjectIsA(o[6],L"TnCameraStrategyManager") ||
      !PoiField(o[6],L"_Camera",0x30,&o[7]) || !PoiLive(o[7],L"TnCamera") ||
      !PoiField(player,L"PlayerCamera",0x2D4,&value) || value!=o[7] ||
      !PoiField(o[7],L"PCOwner",0x1C8,&value) || value!=player ||
      !PoiField(o[7],L"WorldInfo",0x98,&value) || value!=o[2] || !PoiByte(o[7],L"Role",0x66,3) ||
      !PoiFlags(o[7],L"bUseFovSettings",0x498,2,&c->camera_flags) ||
      !UnrealResolveClassByPath(L"TransGame.TnPCS_FocusingOnPointOfInterest",&o[8]) ||
      !PoiRead(reinterpret_cast<void*>(PoiAddress(0x12B6F460)),0,&value) || value!=static_cast<uint8_t*>(o[8])+0x10 ||
      !(o[9]=UnrealObjectClass(o[4]))) return false;
  bool matches=false;
  if (!PoiCommandClass(o[4],o[9],&matches,false) || !matches ||
      !PoiArray(o[5],L"ContextualCommands",0x28,4,&c->commands,o[9]) ||
      !PoiArray(o[5],L"UINotifications",0x34,8,&c->notifications,o[9],true) ||
      !PoiArray(o[7],L"PlayerCameraStates",0x49C,4,&c->states)) return false;
  // Ordinary Remove clears ActivePointOfInterest but leaves Command.POI until
  // Reset or the next Add. A stale-but-valid retained command target is normal.
  for (void* target:{o[10],c->command_poi}) if (target &&
      (!UnrealObjectIsA(target,L"TnPointOfInterest") || !UnrealObjectIndex(target) ||
       !PoiField(target,L"WorldInfo",0x98,&value) || value!=o[2])) return false;
  c->command_poi_index=UnrealObjectIndex(c->command_poi);
  int final_notifications=c->notifications.view.count; bool remove_command=false;
  for (int i=0;i<c->notifications.view.count;++i) if(c->notifications.items[i].matches && !c->notifications.items[i].action) --final_notifications;
  for (int i=0;i<c->commands.view.count;++i) remove_command=remove_command || c->commands.items[i].matches;
  if (final_notifications+(remove_command ? 1:0)>256) return false;
  // Already initialized engine allocator; native array growth/shrink keeps
  // using WFC's heap, never a mod-allocated replacement array.
  void *allocator=nullptr,*table=nullptr,*method=nullptr;
  if (!PoiRead(reinterpret_cast<void*>(PoiAddress(0x12B00204)),0,&allocator) || !PoiRead(allocator,0,&table)) return false;
  for (uint32_t slot:{8u,12u,16u}) if (!PoiRead(table,slot,&method) || !PoiMemory(method,1,false,nullptr,true)) return false;
  if (!PoiRead(reinterpret_cast<void*>(PoiAddress(0x11948544)),0,&method) || !PoiMemory(method,1,false,nullptr,true)) return false;
  if (!PoiRead(reinterpret_cast<void*>(PoiAddress(0x119482F0)),0,&method) || !PoiMemory(method,1,false,nullptr,true)) return false;
  for (size_t i=0;i<o.size();++i) { c->indices[i]=UnrealObjectIndex(o[i]); if (o[i] && !c->indices[i]) return false; }
  return true;
}
bool SamePoiItem(const PoiItem& a,const PoiItem& b,bool action=false) {
  return a.object==b.object && a.index==b.index && (!action || a.action==b.action);
}
bool PoiResult(const PoiCapture& before,const PoiCapture& after) {
  for (size_t i=0;i<10;++i) if (before.objects[i]!=after.objects[i] || before.indices[i]!=after.indices[i]) return false;
  if (before.objects[10] && UnrealObjectIndex(before.objects[10])!=before.indices[10]) return false;
  if (before.command_poi && UnrealObjectIndex(before.command_poi)!=before.command_poi_index) return false;
  if (after.objects[10] || after.command_poi || after.focus_flags!=(before.focus_flags&~1u) || after.camera_flags!=(before.camera_flags&~2u)) return false;
  int out=0,removed=-1;
  for (int i=0;i<before.commands.view.count;++i) {
    if (removed<0 && before.commands.items[i].matches) { removed=i; continue; }
    if (out>=after.commands.view.count || !SamePoiItem(before.commands.items[i],after.commands.items[out++])) return false;
  }
  if (out!=after.commands.view.count) return false;
  out=0;
  for (int i=0;i<before.notifications.view.count;++i) {
    const auto& item=before.notifications.items[i];
    if (item.matches && !item.action) continue;
    if (out>=after.notifications.view.count || !SamePoiItem(item,after.notifications.items[out++],true)) return false;
  }
  if (removed>=0) {
    if (out>=after.notifications.view.count || !SamePoiItem(before.commands.items[removed],after.notifications.items[out]) || after.notifications.items[out++].action!=1) return false;
  }
  if (out!=after.notifications.view.count) return false;
  out=0;
  for (int i=0;i<before.states.view.count;++i) {
    const auto& item=before.states.items[i]; if (item.object==before.objects[8]) continue;
    if (out>=after.states.view.count || !SamePoiItem(item,after.states.items[out++])) return false;
  }
  return out==after.states.view.count;
}
}
bool PrepareNativePointOfInterest(HMODULE module,DWORD thread,void* player,void* manager) {
  if (g_poi_busy || !module || module!=GetModuleHandleW(nullptr) || !thread || thread!=GetCurrentThreadId() ||
      (g_poi_module && (g_poi_module!=module || g_poi_thread!=thread)) || !BindPoiImage(module)) return false;
  g_poi_module=module; g_poi_thread=thread; void* pawn=nullptr; PoiCapture c;
  return PoiField(player,L"Pawn",0x1C8,&pawn) && CapturePoi(player,pawn,manager,&c);
}
NativePlayerServiceResult NativeResetPointOfInterest(void* player,void* pawn,void* manager) {
  PoiCapture before;
  if (g_poi_busy || !CapturePoi(player,pawn,manager,&before)) return NativePlayerServiceResult::kRejected;
  g_poi_busy=true; struct ClearBusy { ~ClearBusy() { g_poi_busy=false; } } busy;
  // EAX=compiled manager (raw+10), plain RET. Reset calls the real Remove,
  // StopFocus and Unregister routines, including UI notifications and barriers.
  WfcCallEaxEcx(reinterpret_cast<void*>(PoiAddress(poi_code::kResetVa)),static_cast<uint8_t*>(manager)+0x10,nullptr);
  PoiCapture after;
  return CapturePoi(player,pawn,manager,&after) && PoiResult(before,after) ?
      NativePlayerServiceResult::kApplied:NativePlayerServiceResult::kStateMismatch;
}
}
