#include "NativeAiWorldState.h"
#include "NativeAiWorldStateCode.h"
#include "NativeAiPlanning.h"
#include "NativeCameraHandoff.h"
#include "NativeRolePawn.h"
#include "UnrealBridge.h"
#include <cstring>

namespace wfcds {
extern "C" void WfcCallEsiVoid(void*,void*);
namespace {
HMODULE g_world_state_image=nullptr;
DWORD g_world_state_thread=0;
thread_local bool g_world_state_busy=false;
bool StateMemory(const void* p,size_t size,HMODULE image=nullptr,bool code=false,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if(!p || !size || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_NOACCESS|PAGE_GUARD)) || (image && m.AllocationBase!=image) ||
      size>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress)))return false;
  const auto protect=m.Protect&255;
  if(write && protect!=PAGE_READWRITE && protect!=PAGE_WRITECOPY && protect!=PAGE_EXECUTE_READWRITE &&
      protect!=PAGE_EXECUTE_WRITECOPY)return false;
  return !code || protect==PAGE_EXECUTE || protect==PAGE_EXECUTE_READ ||
      protect==PAGE_EXECUTE_READWRITE || protect==PAGE_EXECUTE_WRITECOPY;
}
void* StateAddress(HMODULE image,uintptr_t va) {
  return reinterpret_cast<uint8_t*>(image)+va-native_reference_code::kImageBase;
}
template<class T>bool StateRead(const void* p,size_t offset,T* out) {
  if(!p || !out || !StateMemory(static_cast<const uint8_t*>(p)+offset,sizeof(T)))return false;
  std::memcpy(out,static_cast<const uint8_t*>(p)+offset,sizeof(T));return true;
}
bool StateImage(HMODULE image) {
  if(!image)return false;
  return native_ai_world_state_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> relocs){
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),relocs);
    if(va==native_role_pawn_code::kAiVa)return NativeRolePawnCodeMatches(image,va,bytes.data(),bytes.size());
    auto* p=StateAddress(image,va);
    return StateMemory(p,bytes.size(),image,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool StateField(void* p,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset=0;void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(p,name,&offset) && offset==expected &&
      UnrealReadObjectProperty(p,name,out) && StateRead(p,offset,&raw) && raw==*out;
}
bool StateActor(void* p,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) &&
      UnrealReadBoolProperty(p,L"bDeleteMe",&deleting) && !deleting;
}
bool StateByte(void* p,const wchar_t* name,uint32_t expected,uint8_t required) {
  uint32_t offset=0;uint8_t value=0,raw=0;
  return UnrealResolveBytePropertyOffset(p,name,&offset) && offset==expected &&
      UnrealReadByteProperty(p,name,&value) && value==required && StateRead(p,offset,&raw) && raw==value;
}
bool CachedClass(HMODULE image,uintptr_t va,void* expected) {
  void* actual=nullptr;
  return expected && StateMemory(StateAddress(image,va),4,image) && StateRead(StateAddress(image,va),0,&actual) &&
      actual==static_cast<uint8_t*>(expected)+0x10;
}
// Only side-effect-free literal bool/byte getters can run inside this refresh.
// The slot comes from the shipping call site, not a reflected function name.
bool LiteralBitGetter(HMODULE image,void* object,uint32_t slot,bool* value) {
  void *table=nullptr,*method=nullptr;
  if(!StateRead(object,0x10,&table) || !StateMemory(table,slot+4,image) ||
      !StateRead(table,slot,&method) || !StateMemory(method,6,image,true))return false;
  const auto* b=static_cast<const uint8_t*>(method);
  if(b[0]==0x33 && b[1]==0xC0 && b[2]==0xC3){*value=false;return true;}
  if(b[0]==0xB0 && b[1]<=1 && b[2]==0xC3){*value=b[1]!=0;return true;}
  if(b[0]==0xB8 && b[1]<=1 && b[2]==0 && b[3]==0 && b[4]==0 && b[5]==0xC3){*value=b[1]!=0;return true;}
  return false;
}
struct StateCapture {
  // Borrowed same-pulse identities only; no owning allocation or array entry
  // pointer survives a transaction yield. Native stores retain the live root.
  std::array<void*,15> objects{};
  std::array<uint32_t,15> indices{};
  UnrealStructArrayView array{};
  void* cached_class=nullptr;
  bool cached_heat=false,heat=false;
  int32_t cached_form=-1;
  uint8_t form=0,vehicle=0;
};
bool CaptureState(HMODULE image,void* controller,void* pawn,bool reciprocal,StateCapture* out) {
  if(!out || !StateActor(controller,L"TnAiController") || !StateActor(pawn,L"TnAiPawn") ||
      !StateByte(controller,L"Role",0x66,3) || !StateByte(pawn,L"Role",0x66,3))return false;
  *out={};auto& o=out->objects;o[0]=controller;o[1]=pawn;void* value=nullptr;
  if(!StateField(controller,L"WorldInfo",0x98,&o[2]) || !StateActor(o[2],L"WorldInfo") ||
      !StateByte(o[2],L"NetMode",0x39C,0) || !StateField(pawn,L"WorldInfo",0x98,&value) || value!=o[2] ||
      !StateField(controller,L"AiWorldState",0x300,&o[3]) || !UnrealObjectIsA(o[3],L"HmGoapWorldStateCollection") ||
      !StateField(pawn,L"_CurrentForm",0x584,&o[4]) || !UnrealObjectIsA(o[4],L"TnTransformerForm") ||
      !NativeReadPawnForm(pawn,&out->form,&out->vehicle) || out->form>1 ||
      !StateField(pawn,L"Weapon",0x428,&o[5]))return false;
  if(reciprocal && (!StateField(controller,L"Pawn",0x1C8,&value) || value!=pawn ||
      !StateField(pawn,L"Controller",0x1D4,&value) || value!=controller ||
      !UnrealReadObjectProperty(pawn,L"Owner",&value) || value!=controller))return false;
  bool native_form=false;
  if(!LiteralBitGetter(image,o[4],0x70,&native_form) || native_form!=(out->form!=0) ||
      !UnrealResolveClassByPath(L"TransGame.TnWeapon",&o[7]) || !CachedClass(image,0x12B70270,o[7]) ||
      !UnrealResolveClassByPath(L"TransGame.TnAiPawn",&o[8]) || !CachedClass(image,0x12B6ECD0,o[8]))return false;
  o[6]=o[7];
  if(o[5]) {
    uint32_t packed=0;void* native_class=nullptr;
    o[6]=UnrealObjectClass(o[5]);
    if(!StateActor(o[5],L"TnWeapon") || !UnrealObjectIsA(o[6],L"Class") ||
        !StateField(o[5],L"Instigator",0x94,&value) || value!=pawn ||
        !LiteralBitGetter(image,o[5],0x350,&out->heat) || !StateRead(o[5],0x14,&packed) || !(packed&4095) ||
        !StateMemory(StateAddress(image,0x12A29FD4u+(packed&4095)*4),4,image) ||
        !StateRead(StateAddress(image,0x12A29FD4u+(packed&4095)*4),0,&native_class) || native_class!=o[6])return false;
  }
  bool hashed=false;uint32_t raw_hash=0;
  if(!UnrealReadBoolProperty(o[3],L"_HashCreated",&hashed) || !hashed ||
      !StateRead(o[3],0x34,&raw_hash) || !(raw_hash&1) ||
      !UnrealReadStructArrayProperty(o[3],L"Properties",&out->array) || out->array.element_size!=24 ||
      out->array.count<1 || out->array.capacity<out->array.count || out->array.capacity>256 ||
      !StateMemory(out->array.data,static_cast<size_t>(out->array.capacity)*24,nullptr,false,true) ||
      !StateMemory(static_cast<uint8_t*>(o[3])+0x28,12,nullptr,false,true) ||
      std::memcmp(static_cast<uint8_t*>(o[3])+0x28,&out->array,12))return false;
  // SetLength(count) still calls Remove(count,0), which can shrink storage.
  // Prove its exact native capacity policy will not allocate/reallocate here.
  const int32_t slack=out->array.capacity-out->array.count;
  if((3*out->array.count<2*out->array.capacity || 24*slack>=0x4000) && slack>64)return false;
  const wchar_t* names[]={L"TransGame.TnAiWorldStateCurrentWeapon",L"TransGame.TnAiWorldStateWeaponOverheated",L"TransGame.TnAiWorldStateTransformerForm"};
  const uintptr_t caches[]={0x12B6EC3C,0x12B6EBAC,0x12B6EBB8};
  std::array<int32_t,3> hashes{};
  for(size_t i=0;i<3;++i) {
    auto*& key=o[9+i];auto*& defaults=o[12+i];int32_t raw_index=-1;uint32_t offset=0;
    if(!UnrealResolveClassByPath(names[i],&key) || !CachedClass(image,caches[i],key) ||
        !StateRead(key,0x9C,&defaults) || UnrealObjectClass(defaults)!=key ||
        !UnrealReadIntProperty(defaults,L"HashIndex",&hashes[i]) || !StateRead(defaults,0x28,&raw_index) ||
        hashes[i]!=raw_index || hashes[i]<0 || hashes[i]>=out->array.count)return false;
    for(size_t j=0;j<i;++j)if(hashes[j]==hashes[i])return false;
    const auto* data=static_cast<const uint8_t*>(out->array.data)+hashes[i]*24;
    const UnrealStructValueView entry{data,24,out->array.struct_definition};UnrealStructValueView v{};void* raw=nullptr;
    if(!UnrealReadStructObjectField(entry,L"Type",&value) || value!=key || !StateRead(data,0,&raw) || raw!=key ||
        !UnrealReadStructObjectField(entry,L"Subject",&value) || value || !StateRead(data,4,&raw) || raw ||
        !UnrealResolveStructFieldOffset(entry,L"Value",L"StructProperty",12,&offset) || offset!=8 ||
        !UnrealReadStructStructField(entry,L"Value",&v) || v.data!=data+8 || v.size!=12)return false;
    if(i==0) {
      if(!UnrealResolveStructFieldOffset(v,L"ClassVal",L"ClassProperty",4,&offset) || offset!=4 ||
          !UnrealReadStructObjectField(v,L"ClassVal",&out->cached_class) ||
          !StateRead(v.data,4,&raw) || raw!=out->cached_class ||
          (raw && !UnrealObjectIsA(raw,L"Class")))return false;
    } else if(i==1) {
      uint32_t bits=0;
      if(!UnrealResolveStructFieldOffset(v,L"BoolVal",L"BoolProperty",4,&offset) || offset!=8 ||
          !StateRead(v.data,8,&bits))return false;
      // The pinned native SetBoolValue changes precisely bit zero.
      out->cached_heat=(bits&1)!=0;
    } else {
      int32_t raw_form=-1;
      if(!UnrealResolveStructFieldOffset(v,L"IntVal",L"IntProperty",4,&offset) || offset!=0 ||
          !UnrealReadStructIntField(v,L"IntVal",&out->cached_form) || !StateRead(v.data,0,&raw_form) ||
          raw_form!=out->cached_form || raw_form<0 || raw_form>1)return false;
    }
  }
  for(size_t i=0;i<o.size();++i)if(o[i] && !(out->indices[i]=UnrealObjectIndex(o[i])))return false;
  return true;
}
bool StateIdentities(const StateCapture& s) {
  for(size_t i=0;i<s.objects.size();++i)
    if(s.objects[i] && UnrealObjectAt(static_cast<int32_t>(s.indices[i]-1))!=s.objects[i])return false;
  return true;
}
bool StateMatches(const StateCapture& s) {
  return s.cached_class==s.objects[6] && s.cached_heat==s.heat && s.cached_form==s.form;
}
bool StateLeases(const StateCapture& s,const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return move.controller!=s.objects[0] && move.controller==look.controller &&
      move.world==s.objects[2] && look.world==s.objects[2] &&
      move.axis==NativeInputAxis::kMove && look.axis==NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look) &&
      NativeAiPlanHandoffReady(s.objects[0]) && UnrealObjectReferenceWritesReady();
}
}
bool PrepareNativeAiWorldState(HMODULE image,DWORD thread,void* controller,void* pawn) {
  StateCapture s{};
  if(g_world_state_busy || !thread || thread!=GetCurrentThreadId() ||
      (g_world_state_thread && g_world_state_thread!=thread) ||
      (g_world_state_image && g_world_state_image!=image) || !StateImage(image) ||
      !CaptureState(image,controller,pawn,false,&s) || !StateIdentities(s))return false;
  g_world_state_image=image;g_world_state_thread=thread;return true;
}
bool NativeAiWorldStateMatchesPawn(void* controller,void* pawn) {
  StateCapture s{};
  return !g_world_state_busy && g_world_state_image && GetCurrentThreadId()==g_world_state_thread &&
      CaptureState(g_world_state_image,controller,pawn,true,&s) && StateIdentities(s) && StateMatches(s);
}
NativeAiWorldStateResult NativeRefreshAiWorldState(void* controller,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  StateCapture before{},after{};
  if(g_world_state_busy || !g_world_state_image || GetCurrentThreadId()!=g_world_state_thread ||
      !StateImage(g_world_state_image) || !CaptureState(g_world_state_image,controller,pawn,true,&before) ||
      !StateIdentities(before) || !StateLeases(before,move,look))return NativeAiWorldStateResult::kRejected;
  struct Busy {Busy(){g_world_state_busy=true;}~Busy(){g_world_state_busy=false;}} busy;
  WfcCallEsiVoid(StateAddress(g_world_state_image,native_ai_world_state_code::kRefreshVa),static_cast<uint8_t*>(controller)+0x10);
  if(!StateIdentities(before) || !CaptureState(g_world_state_image,controller,pawn,true,&after) ||
      before.objects!=after.objects || before.indices!=after.indices || before.form!=after.form || before.vehicle!=after.vehicle ||
      before.heat!=after.heat || before.array.data!=after.array.data || before.array.count!=after.array.count ||
      before.array.capacity!=after.array.capacity || !StateLeases(after,move,look) || !StateMatches(after))
    return NativeAiWorldStateResult::kStateMismatch;
  return NativeAiWorldStateResult::kApplied;
}
}
