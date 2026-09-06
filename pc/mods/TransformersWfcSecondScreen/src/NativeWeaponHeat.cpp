#include "NativeWeaponHeat.h"
#include "NativeWeaponHeatCode.h"
#include "UnrealBridge.h"
#include <array>
#include <cmath>
#include <cstring>
namespace wfcds {
extern "C" void WfcCallEsiValue(void*,void*,uint32_t);
namespace {
namespace heat_code=native_weapon_heat_code;
HMODULE g_heat_module=nullptr;
DWORD g_heat_thread=0;
thread_local bool g_heat_busy=false;
uintptr_t HeatAddress(uintptr_t va) { return reinterpret_cast<uintptr_t>(g_heat_module)+va-native_reference_code::kImageBase; }
bool HeatMemory(const void* p,size_t n,bool write=false,HMODULE image=nullptr,bool execute=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      (image && m.AllocationBase!=image) || n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const auto prot=m.Protect&0xFF;
  return (!write || prot==PAGE_READWRITE || prot==PAGE_WRITECOPY || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY) &&
      (!execute || prot==PAGE_EXECUTE || prot==PAGE_EXECUTE_READ || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY);
}
bool HeatRead(void* p,uint32_t off,void* out,size_t size=4) {
  if (!p || !out || !HeatMemory(static_cast<uint8_t*>(p)+off,size)) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+off,size);return true;
}
bool BindHeatImage(HMODULE module) {
  return module && heat_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(module),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module)+va-native_reference_code::kImageBase);
    return HeatMemory(p,bytes.size(),false,module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool HeatLive(void* p,const wchar_t* type) {
  bool dead=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) && UnrealReadBoolProperty(p,L"bDeleteMe",&dead) && !dead;
}
bool HeatObjectField(void* p,const wchar_t* name,uint32_t off,void** value) {
  uint32_t reflected=0;void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(p,name,&reflected) && reflected==off &&
      UnrealReadObjectProperty(p,name,value) && HeatRead(p,off,&raw) && raw==*value;
}
struct HeatCapture {
  // Pawn, weapon, heat service, controller, world, optional Instigator/heat owner.
  std::array<void*,7> objects{};
  std::array<uint32_t,7> indices{};
  std::array<UnrealArrayView,2> observers{};
  std::array<std::array<UnrealScriptDelegate,16>,2> delegates{};
  UnrealArrayView modifiers{};
  std::array<void*,64> modifier_objects{};
  std::array<uint32_t,64> modifier_indices{};
  void* frame=nullptr;
  float current=0,maximum=0;
  bool overheated=false,player=false;
};
bool HeatState(void* heat,HeatCapture* c) {
  uintptr_t table=0;void *state=nullptr,*native_table=nullptr,*entry=nullptr;
  char name[32]{};uint32_t packed=0,cached=0;
  if (!HeatRead(heat,0x10,&table) || !HeatRead(heat,0x28,&c->frame) || !HeatMemory(c->frame,0x54,true) ||
      !HeatRead(c->frame,0x38,&state) || !UnrealObjectIsA(state,L"State") || !UnrealObjectIndex(state) ||
      !UnrealObjectName(state,name,sizeof(name)) || !HeatRead(state,4,&packed)) return false;
  if (!std::strcmp(name,"Cool")) c->overheated=false;
  else if (!std::strcmp(name,"Overheated")) c->overheated=true;
  else return false; // Uninitialized/unknown derived states are not heat recovery.
  const auto va=c->overheated ? 0x127FA238u:0x127FA1E8u;
  if (table!=HeatAddress(va) || !HeatMemory(reinterpret_cast<void*>(table),0x4C,false,g_heat_module) ||
      !HeatRead(reinterpret_cast<void*>(HeatAddress(c->overheated ? 0x12B31E80u:0x12B302CCu)),0,&cached) || packed!=cached || !packed) return false;
  for (bool hot:{false,true}) for (const auto& slot:std::array<std::array<uintptr_t,2>,3>{{
      {{0x14,heat_code::kSelectStateVa}},
      {{0x38,hot ? heat_code::kOverheatedBeginVa:heat_code::kCoolBeginVa}},
      {{0x48,hot ? heat_code::kOverheatedCheckVa:heat_code::kCoolCheckVa}}}}) {
    if (!HeatRead(reinterpret_cast<void*>(HeatAddress(hot ? 0x127FA238u:0x127FA1E8u)),slot[0],&entry) ||
        reinterpret_cast<uintptr_t>(entry)!=HeatAddress(slot[1])) return false;
  }
  UnrealArrayView stack{};
  if (!HeatRead(c->frame,0x48,&stack,sizeof(stack)) || stack.count!=0 || stack.capacity<0 || stack.capacity>64 ||
      (stack.capacity && !HeatMemory(stack.data,stack.capacity*12)) || !HeatRead(heat,0,&native_table)) return false;
  // UObject owns state dispatch. Require real engine code, not a mod-installed
  // state dispatcher; no state-frame allocation is entered (frame must exist).
  for (uint32_t off:{0x12Cu,0x138u})
    if (!HeatRead(native_table,off,&entry) || !HeatMemory(entry,1,false,g_heat_module,true)) return false;
  return true;
}
bool CaptureHeat(void* pawn,void* weapon,HeatCapture* c,bool allow_uninitialized=false) {
  if (!c || !g_heat_module || GetCurrentThreadId()!=g_heat_thread || !HeatLive(pawn,L"TnPawn") || !HeatLive(weapon,L"TnWeapon")) return false;
  *c={};auto& o=c->objects;o[0]=pawn;o[1]=weapon;void* value=nullptr;uint8_t role=0,mode=255;bool firing=true;
  if (!UnrealReadObjectProperty(weapon,L"Owner",&value) || value!=pawn ||
      !UnrealReadObjectProperty(weapon,L"Instigator",&o[5]) || (o[5] && o[5]!=pawn) ||
      !HeatObjectField(weapon,L"WeaponHeat",0x324,&o[2]) || !UnrealObjectIsA(o[2],L"TnWeaponHeat") || !HeatMemory(o[2],0x5C,true) ||
      !HeatObjectField(o[2],L"OwnerPawn",0x40,&o[6]) || (o[6] && o[6]!=pawn) ||
      (o[5] && o[6]!=pawn) ||
      !UnrealReadObjectProperty(pawn,L"Controller",&o[3]) || !UnrealReadObjectProperty(o[3],L"Pawn",&value) || value!=pawn ||
      !UnrealReadObjectProperty(pawn,L"WorldInfo",&o[4]) || !HeatLive(o[4],L"WorldInfo") ||
      !UnrealReadByteProperty(o[4],L"NetMode",&mode) || mode!=0 ||
      !UnrealReadObjectProperty(weapon,L"WorldInfo",&value) || value!=o[4] ||
      !UnrealReadObjectProperty(o[3],L"WorldInfo",&value) || value!=o[4] ||
      !UnrealReadByteProperty(pawn,L"Role",&role) || role!=3 || !UnrealReadByteProperty(weapon,L"Role",&role) || role!=3 ||
      !UnrealReadByteProperty(o[3],L"Role",&role) || role!=3 ||
      !UnrealReadBoolProperty(weapon,L"bIsFiring",&firing) || firing) return false;
  c->player=HeatLive(o[3],L"TnPlayerController");
  if (c->player ? (!UnrealReadObjectProperty(o[3],L"AcknowledgedPawn",&value) || value!=pawn):!HeatLive(o[3],L"TnAiController")) return false;
  char type[48]{};uint32_t offset=0;float raw=0;UnrealStructValueView properties{};
  if (!UnrealObjectClassName(o[2],type,sizeof(type)) || std::strcmp(type,"TnWeaponHeat") ||
      !UnrealResolveFloatPropertyOffset(o[2],L"CurrentHeat",&offset) || offset!=0x3C ||
      !UnrealReadFloatProperty(o[2],L"CurrentHeat",&c->current) || !std::isfinite(c->current) || c->current<0 ||
      !HeatRead(o[2],0x3C,&raw) || raw!=c->current || !HeatMemory(static_cast<uint8_t*>(o[2])+0x3C,4,true) ||
      !UnrealResolveStructPropertyOffset(o[2],L"HeatProperties",16,&offset) || offset!=0x2C ||
      !UnrealReadStructProperty(o[2],L"HeatProperties",&properties) || properties.size!=16 || properties.data!=static_cast<uint8_t*>(o[2])+0x2C ||
      !UnrealReadStructFloatProperty(o[2],L"HeatProperties",L"HeatMax",&c->maximum) || !std::isfinite(c->maximum) || c->maximum<0 ||
      (!allow_uninitialized && c->maximum==0) ||
      !HeatRead(o[2],0x2C,&raw) || raw!=c->maximum ||
      !UnrealReadStructTypedArray(properties,L"Modifiers",L"ObjectProperty",4,&c->modifiers,64) ||
      c->modifiers.count<0 || c->modifiers.count>64 || !HeatState(o[2],c)) return false;
  // An AI weapon can legitimately await its first player heat setup. Admit
  // that empty/cold service for binding validation only. Sync must never enter
  // the native zero-maximum clamp/state path or manufacture initialization.
  if (c->maximum==0 && (c->current!=0 || c->overheated || c->modifiers.count!=0 || c->player)) return false;
  for (int32_t i=0;i<c->modifiers.count;++i) {
    if (!UnrealReadObjectArrayElement(c->modifiers,i,&c->modifier_objects[i]) ||
        !UnrealObjectIsA(c->modifier_objects[i],L"TnHeatModifier") || !(c->modifier_indices[i]=UnrealObjectIndex(c->modifier_objects[i]))) return false;
  }
  for (size_t k=0;k<2;++k) {
    const wchar_t* name=k ? L"WeaponCooledObservers":L"WeaponOverheatedObservers";
    auto& a=c->observers[k];
    if (!UnrealReadDelegateArrayProperty(o[2],name,&a,&offset) || offset!=(k ? 0x50u:0x44u) ||
        a.count<0 || a.count>16 || a.capacity<a.count || a.capacity>64 ||
        (a.count && !HeatMemory(a.data,a.count*sizeof(UnrealScriptDelegate)))) return false;
    for (int32_t i=0;i<a.count;++i) {
      auto& d=c->delegates[k][i];std::memcpy(&d,static_cast<uint8_t*>(a.data)+i*sizeof(d),sizeof(d));
      if (!d.object && !d.function_name && !d.compiled_context && !d.compiled_function) continue;
      // Only this weapon's own attached callbacks are admitted. Check the exact
      // packed method name as well as both compiled binding words. Calling an
      // arbitrary observer just because its address is executable is not safe.
      if (d.object!=weapon || d.compiled_context!=static_cast<uint8_t*>(weapon)+0x10 ||
          !UnrealUnnumberedNameEquals(d.function_name,k ? L"WeaponCooled":L"WeaponOverheated") ||
          !HeatMemory(d.compiled_function,1,false,g_heat_module,true)) return false;
      // Every admitted entry in this array names the same method. Equivalent
      // cooked FName indices must not evade duplicate-callback rejection.
      for (int32_t j=0;j<i;++j) if (c->delegates[k][j].object==d.object) return false;
    }
  }
  for (size_t i=0;i<o.size();++i) { c->indices[i]=UnrealObjectIndex(o[i]);if (o[i] && !c->indices[i]) return false; }
  return true;
}
bool SameHeatRuntime(const HeatCapture& a,const HeatCapture& b) {
  if (a.objects!=b.objects || a.indices!=b.indices || a.frame!=b.frame || a.maximum!=b.maximum || a.player!=b.player ||
      a.modifier_objects!=b.modifier_objects || a.modifier_indices!=b.modifier_indices ||
      std::memcmp(&a.modifiers,&b.modifiers,sizeof(a.modifiers))) return false;
  for (size_t i=0;i<2;++i)
    if (std::memcmp(&a.observers[i],&b.observers[i],sizeof(UnrealArrayView)) ||
        std::memcmp(a.delegates[i].data(),b.delegates[i].data(),sizeof(a.delegates[i]))) return false;
  return true;
}
}
bool PrepareNativeWeaponHeat(HMODULE module,DWORD thread,void* pawn,void* weapon) {
  if (g_heat_busy || !thread || thread!=GetCurrentThreadId() || (g_heat_thread && g_heat_thread!=thread) ||
      (g_heat_module && g_heat_module!=module) || !BindHeatImage(module)) return false;
  g_heat_module=module;g_heat_thread=thread;HeatCapture c;
  return CaptureHeat(pawn,weapon,&c,true);
}
NativePlayerServiceResult NativeSyncWeaponHeat(void* pawn,void* weapon,float value) {
  HeatCapture before;
  // A native clamp would silently lose magazine capacity. The swap requires
  // exact preservation, so an incompatible new HeatMax rejects before mutation.
  if (g_heat_busy || !std::isfinite(value) || value<0 || !CaptureHeat(pawn,weapon,&before) || value>before.maximum)
    return NativePlayerServiceResult::kRejected;
  g_heat_busy=true;struct ClearBusy { ~ClearBusy(){g_heat_busy=false;} } busy;
  uint32_t bits=0;std::memcpy(&bits,&value,4);
  WfcCallEsiValue(reinterpret_cast<void*>(HeatAddress(heat_code::kSyncVa)),static_cast<uint8_t*>(before.objects[2])+0x10,bits);
  HeatCapture after;
  if (!CaptureHeat(pawn,weapon,&after) || !SameHeatRuntime(before,after) || after.current!=value || after.overheated!=(value>=after.maximum))
    return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
bool NativeWeaponHeatInitializationStable(void* pawn,void* weapon) {
  HeatCapture current{};
  return !g_heat_busy && CaptureHeat(pawn,weapon,&current,true) && !current.player &&
      !current.overheated && current.current==0 && current.objects[6]==pawn;
}
NativePlayerServiceResult NativeSeedWeaponHeatCapacity(void* pawn,void* weapon,void* player_data,bool rollback) {
  HeatCapture before{};void* selected=nullptr;float maximum=0;
  if (g_heat_busy || !g_heat_thread || GetCurrentThreadId()!=g_heat_thread || !BindHeatImage(g_heat_module) ||
      !UnrealObjectIsA(player_data,L"TnWeaponData") || !UnrealObjectIndex(player_data) ||
      !UnrealReadObjectProperty(weapon,L"PlayerData",&selected) || selected!=player_data ||
      !UnrealReadStructFloatProperty(player_data,L"HeatProperties",L"HeatMax",&maximum) ||
      !std::isfinite(maximum) || maximum<=0 || !CaptureHeat(pawn,weapon,&before,true) ||
      before.player || before.overheated || before.current!=0 || before.modifiers.count!=0 || before.objects[6]!=pawn ||
      (before.maximum!=0 && before.maximum!=maximum)) return NativePlayerServiceResult::kRejected;
  const auto data_index=UnrealObjectIndex(player_data);
  const float desired=rollback ? 0:maximum;
  if (before.maximum==desired) return NativePlayerServiceResult::kApplied;
  g_heat_busy=true;struct Clear { ~Clear(){g_heat_busy=false;} } clear;
  LONG expected_bits=0,desired_bits=0;
  std::memcpy(&expected_bits,&before.maximum,4);std::memcpy(&desired_bits,&desired,4);
  // CaptureHeat independently checks the reflected struct, raw scalar value
  // and current write permission for this exact shipping +2C destination.
  auto* slot=reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(before.objects[2])+0x2C);
  if (InterlockedCompareExchange(slot,desired_bits,expected_bits)!=expected_bits)
    return NativePlayerServiceResult::kRejected;
  HeatCapture after{},stable{};float authored=0;
  before.maximum=desired;
  if (!CaptureHeat(pawn,weapon,&after,true) || !SameHeatRuntime(before,after) || after.current!=0 || after.overheated ||
      // A change late in a metadata read must not leave a mixed old/new view
      // accepted as success. Recheck the complete live boundary, not just IDs.
      !CaptureHeat(pawn,weapon,&stable,true) || !SameHeatRuntime(before,stable) || stable.current!=0 || stable.overheated ||
      UnrealObjectIndex(player_data)!=data_index || !UnrealReadObjectProperty(weapon,L"PlayerData",&selected) || selected!=player_data ||
      !UnrealReadStructFloatProperty(player_data,L"HeatProperties",L"HeatMax",&authored) || authored!=maximum)
    return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
