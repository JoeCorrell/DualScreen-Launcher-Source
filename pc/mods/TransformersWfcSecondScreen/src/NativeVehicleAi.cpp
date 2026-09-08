#include "NativeVehicleAi.h"
#include "NativeVehicleAiCode.h"
#include "NativeAiPlanning.h"
#include "NativeCameraHandoff.h"
#include "NativeRolePawn.h"
#include "UnrealBridge.h"
#include <cstring>

namespace wfcds {
extern "C" void* WfcCallEaxPointer(void* method,void* unused_edi,void* receiver);
extern "C" void WfcCallEsiVoid(void* method,void* receiver);
namespace {
namespace vehicle_code=native_vehicle_ai_code;
HMODULE g_vehicle_image=nullptr;
DWORD g_vehicle_thread=0;
thread_local bool g_vehicle_busy=false;
uintptr_t VehicleAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_vehicle_image)+va-native_reference_code::kImageBase;
}
bool VehicleMemory(const void* p,size_t n,HMODULE image=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !n || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (image && m.AllocationBase!=image) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD prot=m.Protect&0xFF;
  if (write && prot!=PAGE_READWRITE && prot!=PAGE_WRITECOPY && prot!=PAGE_EXECUTE_READWRITE &&
      prot!=PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || prot==PAGE_EXECUTE || prot==PAGE_EXECUTE_READ ||
      prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY;
}
bool VehicleImage(HMODULE image) {
  return vehicle_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(image),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(image)+va-native_reference_code::kImageBase);
    if (va==vehicle_code::kPawnGetterVa)
      return NativeRolePawnCodeMatches(image,va,bytes.data(),bytes.size());
    return VehicleMemory(p,bytes.size(),image,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool VehiclePointer(void* p,uint32_t offset,void** out) {
  if (!p || !out || !VehicleMemory(static_cast<uint8_t*>(p)+offset,4)) return false;
  std::memcpy(out,static_cast<uint8_t*>(p)+offset,4);return true;
}
bool VehicleField(void* p,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset=0;void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(p,name,&offset) && offset==expected &&
      UnrealReadObjectProperty(p,name,out) && VehiclePointer(p,offset,&raw) && raw==*out;
}
bool VehicleLive(void* p,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) &&
      UnrealReadBoolProperty(p,L"bDeleteMe",&deleting) && !deleting;
}
bool VehicleByte(void* p,const wchar_t* name,uint32_t expected,uint8_t required) {
  uint32_t offset=0;uint8_t value=0;
  return UnrealResolveBytePropertyOffset(p,name,&offset) && offset==expected &&
      UnrealReadByteProperty(p,name,&value) && value==required;
}
struct VehicleCapture {
  // controller, pawn, world, vehicle, blueprint, vehicle input. All borrowed; no allocations
  // or partially initialized UObject ownership escapes this function's pulse.
  std::array<void*,6> objects{};
  std::array<uint32_t,6> indices{};
  std::array<uint8_t,0x30> pose{};
  uint8_t mode=0,type=4;
  bool giant=false;
  uint32_t input_flags=0,input_offset=0;
};
bool CaptureVehicle(void* controller,void* pawn,VehicleCapture* out,bool reciprocal) {
  if (!out || !g_vehicle_image || GetCurrentThreadId()!=g_vehicle_thread ||
      !VehicleLive(controller,L"TnAiController") || !VehicleLive(pawn,L"TnAiPawn") ||
      !VehicleByte(controller,L"Role",0x66,3) || !VehicleByte(pawn,L"Role",0x66,3) ||
      !VehicleMemory(controller,0x3D0,nullptr,false,true) || !VehicleMemory(pawn,0x588)) return false;
  *out={};auto& o=out->objects;o[0]=controller;o[1]=pawn;void* value=nullptr;
  if (!VehicleField(controller,L"WorldInfo",0x98,&o[2]) || !VehicleLive(o[2],L"WorldInfo") ||
      !VehicleByte(o[2],L"NetMode",0x39C,0) ||
      !VehicleField(pawn,L"WorldInfo",0x98,&value) || value!=o[2] ||
      !VehicleField(controller,L"_VehicleAiControl",0x3CC,&value) ||
      !VehicleField(pawn,L"_VehicleForm",0x580,&o[3]) ||
      !NativeReadPawnForm(pawn,&out->mode,&out->type)) return false;
  if (reciprocal && (!VehicleField(controller,L"Pawn",0x1C8,&value) || value!=pawn ||
      !VehicleField(pawn,L"Controller",0x1D4,&value) || value!=controller ||
      !UnrealReadObjectProperty(pawn,L"Owner",&value) || value!=controller)) return false;
  if (o[3]) {
    const wchar_t* blueprint=out->type<=1 ? L"TnCarFormBlueprint":out->type==2 ? L"TnPlaneFormBlueprint":L"TnTankFormBlueprint";
    if (out->type>3 || !UnrealObjectIsA(o[3],L"TnVehicleForm") ||
        !VehicleField(o[3],L"_Blueprint",0x74,&o[4]) || !UnrealObjectIsA(o[4],blueprint)) return false;
    const wchar_t* input=out->type<=1 ? L"TnCarInputState":out->type==2 ? L"TnPlaneInputState":L"TnTankInputState";
    const uintptr_t setter=out->type<=1 ? native_ai_stop_code::kCarBoostVa:
        out->type==2 ? native_ai_stop_code::kPlaneBoostVa:native_ai_stop_code::kTankBoostVa;
    void *table=nullptr,*method=nullptr;
    out->input_offset=out->type==2 ? 0x4C:0x48;
    if (!VehicleField(o[3],L"_InputState",out->type==3 ? 0x104:0x108,&o[5]) || !UnrealObjectIsA(o[5],input) ||
        !VehiclePointer(o[5],0x2C,&value) || value!=pawn ||
        !VehicleMemory(o[5],out->input_offset+4,nullptr,false,true) ||
        !VehiclePointer(o[3],0x10,&table) || !VehicleMemory(table,0xD4,g_vehicle_image) ||
        !VehiclePointer(table,0xD0,&method) || reinterpret_cast<uintptr_t>(method)!=VehicleAddress(setter)) return false;
    std::memcpy(&out->input_flags,static_cast<uint8_t*>(o[5])+out->input_offset,4);
  } else if (out->type!=4 || out->mode!=0) return false;
  out->giant=out->type==3 && UnrealObjectIsA(pawn,L"TnTankAiPawn");
  for (size_t i=0;i<o.size();++i) {
    out->indices[i]=UnrealObjectIndex(o[i]);if (o[i] && !out->indices[i]) return false;
  }
  std::memcpy(out->pose.data(),static_cast<uint8_t*>(pawn)+0xD8,out->pose.size());
  return true;
}
bool VehicleSame(const VehicleCapture& a,const VehicleCapture& b) {
  return a.objects==b.objects && a.indices==b.indices && a.pose==b.pose &&
      a.mode==b.mode && a.type==b.type && a.giant==b.giant && a.input_offset==b.input_offset;
}
bool VehicleStrategy(const VehicleCapture& c,void* strategy,void** path,bool initialized) {
  *path=nullptr;
  if (!c.objects[3]) return !strategy;
  const bool plane=c.type==2;
  const char* type=c.type<=1 ? "TnVehicleCarAiControlStrategy":plane ? "TnVehiclePlaneAiControlStrategy":
      c.giant ? "TnVehicleGiantTankAiControlStrategy":"TnVehicleTankAiControlStrategy";
  const uintptr_t callback=c.type<=1 ? vehicle_code::kCarInitializeVa:plane ? vehicle_code::kPlaneInitializeVa:
      c.giant ? vehicle_code::kGiantInitializeVa:vehicle_code::kTankInitializeVa;
  char actual[80]{};void *table=nullptr,*method=nullptr,*value=nullptr;uint32_t offset=0;
  if (!UnrealObjectIndex(strategy) || !UnrealObjectClassName(strategy,actual,sizeof(actual)) || std::strcmp(actual,type) ||
      UnrealObjectOuter(strategy)!=c.objects[0] || !VehicleMemory(strategy,0x7C,nullptr,false,!initialized) ||
      !VehiclePointer(strategy,0x10,&table) || !VehicleMemory(table,0x3C,g_vehicle_image) ||
      !VehiclePointer(table,0x38,&method) || reinterpret_cast<uintptr_t>(method)!=VehicleAddress(callback) ||
      !VehicleField(strategy,L"_PathFollower",0x64,path) || !UnrealObjectIsA(*path,L"TnPathFollower") ||
      !UnrealObjectIndex(*path) || UnrealObjectOuter(*path)!=strategy ||
      !VehicleMemory(*path,0x30,nullptr,false,!initialized) ||
      !VehicleField(strategy,L"_BoundController",0x28,&value) || value!=(initialized ? c.objects[0]:nullptr) ||
      !VehicleField(strategy,L"_TransformerPawn",0x2C,&value) || value!=(initialized ? c.objects[1]:nullptr) ||
      !VehicleField(*path,L"_Controller",0x28,&value) || value!=(initialized ? c.objects[0]:nullptr) ||
      !VehicleField(*path,L"_Pawn",0x2C,&value) || value!=(initialized ? c.objects[1]:nullptr) ||
      !VehicleField(strategy,plane ? L"_PlaneForm":c.type<=1 ? L"CarForm":L"TankForm",0x78,&value) ||
      value!=(initialized ? c.objects[3]:nullptr) ||
      !VehicleField(strategy,plane ? L"_Blueprint":L"Blueprint",0x74,&value) ||
      value!=(initialized ? c.objects[4]:nullptr) ||
      !UnrealResolveStructPropertyOffset(strategy,L"_DesiredLocation",12,&offset) || offset!=0x48 ||
      !UnrealResolveStructPropertyOffset(strategy,L"_DesiredRotation",12,&offset) || offset!=0x30) return false;
  return true;
}
using VehiclePlayerIdentity=std::array<uintptr_t,4>;
struct VehicleNavigationView {
  NativeVehicleAiNavigation identity{};
  UnrealStructValueView value{};
};
bool VehicleNavigation(void* controller,VehicleNavigationView* out) {
  if (!out || !g_vehicle_image || GetCurrentThreadId()!=g_vehicle_thread ||
      !VehicleLive(controller,L"TnAiController")) return false;
  *out={};void *collection=nullptr,*key=nullptr,*defaults=nullptr,*type=nullptr,*subject=nullptr;
  UnrealStructArrayView array{};int32_t index=-1;bool hashed=false;uint32_t offset=0;
  if (!VehicleField(controller,L"AiWorldState",0x300,&collection) ||
      !UnrealObjectIsA(collection,L"HmGoapWorldStateCollection") || !UnrealObjectIndex(collection) ||
      !UnrealReadBoolProperty(collection,L"_HashCreated",&hashed) || !hashed ||
      !UnrealResolveClassByPath(L"TransGame.TnAiWorldStateVehicleMode",&key) ||
      !VehiclePointer(key,0x9C,&defaults) || !UnrealObjectIndex(defaults) ||
      UnrealObjectClass(defaults)!=key || !UnrealReadIntProperty(defaults,L"HashIndex",&index) ||
      !UnrealReadStructArrayProperty(collection,L"Properties",&array) || array.element_size!=24 ||
      index<0 || index>=array.count || array.capacity<array.count || array.capacity>256 ||
      !VehicleMemory(array.data,static_cast<size_t>(array.count)*24)) return false;
  const auto* data=static_cast<const uint8_t*>(array.data)+static_cast<size_t>(index)*24;
  const UnrealStructValueView entry{data,24,array.struct_definition};
  if (!UnrealReadStructObjectField(entry,L"Type",&type) || type!=key ||
      !UnrealReadStructObjectField(entry,L"Subject",&subject) || subject ||
      !UnrealResolveStructFieldOffset(entry,L"Value",L"StructProperty",12,&offset) || offset!=8 ||
      !UnrealReadStructStructField(entry,L"Value",&out->value) || out->value.size!=12 ||
      out->value.data!=data+8 ||
      !UnrealResolveStructFieldOffset(out->value,L"IntVal",L"IntProperty",4,&offset) || offset!=0 ||
      !UnrealReadStructIntField(out->value,L"IntVal",&out->identity.mode) ||
      out->identity.mode<0 || out->identity.mode>2) return false;
  out->identity.controller=controller;out->identity.controller_index=UnrealObjectIndex(controller);
  out->identity.collection=collection;out->identity.collection_index=UnrealObjectIndex(collection);
  return true;
}
bool VehicleNavigationSame(const NativeVehicleAiNavigation& a,const NativeVehicleAiNavigation& b) {
  return a.controller==b.controller && a.controller_index==b.controller_index &&
      a.collection==b.collection && a.collection_index==b.collection_index;
}
bool VehicleNavigationWrite(const NativeVehicleAiNavigation& expected,int32_t mode) {
  VehicleNavigationView current{},after{};
  if (mode<0 || mode>2 || !UnrealObjectReferenceWritesReady() ||
      !NativeAiPlanHandoffReady(expected.controller) || !VehicleNavigation(expected.controller,&current) ||
      !VehicleNavigationSame(current.identity,expected) || current.identity.mode!=expected.mode ||
      !VehicleMemory(current.value.data,4,nullptr,false,true)) return false;
  // SetIntValue (11386910) has no notifications: it ensures the hash/slot,
  // assigns Type, IntVal and Subject. For this EXISTING typed, subjectless
  // slot, only IntVal changes. Do not call its SetLength path: even an equal
  // length may shrink/reallocate the engine array. No object reference changes
  // here, so no GC barrier or additional root is needed.
  if (!UnrealAssignStructIntField(current.value,L"IntVal",mode)) return false;
  return VehicleNavigation(expected.controller,&after) &&
      VehicleNavigationSame(after.identity,expected) && after.identity.mode==mode;
}
bool VehicleGuard(const VehicleCapture& c,const NativePlayerInputLease& move,const NativePlayerInputLease& look,
                  VehiclePlayerIdentity* identity) {
  void *world=nullptr,*pawn=nullptr,*value=nullptr;
  if (!(move.controller!=c.objects[0] && move.controller==look.controller &&
      move.world==c.objects[2] && look.world==c.objects[2] &&
      move.axis==NativeInputAxis::kMove && look.axis==NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look) &&
      VehicleLive(move.controller,L"TnPlayerController") && VehicleByte(move.controller,L"Role",0x66,3) &&
      VehicleField(move.controller,L"WorldInfo",0x98,&world) && world==c.objects[2] &&
      VehicleField(move.controller,L"Pawn",0x1C8,&pawn) && pawn!=c.objects[1] && VehicleLive(pawn,L"TnPawn") &&
      VehicleField(move.controller,L"AcknowledgedPawn",0x300,&value) && value==pawn &&
      VehicleField(pawn,L"Controller",0x1D4,&value) && value==move.controller &&
      UnrealReadObjectProperty(pawn,L"Owner",&value) && value==move.controller &&
      VehicleField(pawn,L"WorldInfo",0x98,&value) && value==world &&
      NativeAiPlanHandoffReady(c.objects[0]) && UnrealObjectReferenceWritesReady())) return false;
  *identity={reinterpret_cast<uintptr_t>(move.controller),UnrealObjectIndex(move.controller),
      reinterpret_cast<uintptr_t>(pawn),UnrealObjectIndex(pawn)};
  return true;
}
}
bool NativeReadVehicleAiNavigation(void* controller,NativeVehicleAiNavigation* out) {
  if (!out) return false;
  *out={};VehicleNavigationView view{};
  if (g_vehicle_busy || !VehicleNavigation(controller,&view)) return false;
  *out=view.identity;return true;
}
bool NativeRestoreVehicleAiNavigation(const NativeVehicleAiNavigation& original) {
  VehicleNavigationView current{};
  if (g_vehicle_busy || !VehicleNavigation(original.controller,&current) ||
      !VehicleNavigationSame(current.identity,original) ||
      (current.identity.mode!=original.mode && current.identity.mode!=1)) return false;
  return VehicleNavigationWrite(current.identity,original.mode);
}
bool PrepareNativeVehicleAi(HMODULE image,DWORD thread,void* controller,void* pawn) {
  if (g_vehicle_busy || !image || !thread || thread!=GetCurrentThreadId() ||
      (g_vehicle_image && (image!=g_vehicle_image || thread!=g_vehicle_thread)) || !VehicleImage(image)) return false;
  g_vehicle_image=image;g_vehicle_thread=thread;
  VehicleCapture capture;return CaptureVehicle(controller,pawn,&capture,false);
}
bool NativeVehicleAiMatchesPawn(void* controller,void* pawn) {
  VehicleCapture capture;void *strategy=nullptr,*path=nullptr;
  return !g_vehicle_busy && CaptureVehicle(controller,pawn,&capture,true) &&
      VehicleField(controller,L"_VehicleAiControl",0x3CC,&strategy) && VehicleStrategy(capture,strategy,&path,true);
}
NativeVehicleAiResult NativeRebuildVehicleAi(void* controller,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  VehicleCapture before,after;void *previous=nullptr,*strategy=nullptr,*path=nullptr,*value=nullptr;
  VehiclePlayerIdentity player_before{},player_after{};
  VehicleNavigationView navigation{};
  if (g_vehicle_busy || !CaptureVehicle(controller,pawn,&before,true) || !VehicleGuard(before,move,look,&player_before) ||
      !VehicleImage(g_vehicle_image) || !VehicleField(controller,L"_VehicleAiControl",0x3CC,&previous))
    return NativeVehicleAiResult::kRejected;
  if (before.mode==1 && !VehicleNavigation(controller,&navigation)) return NativeVehicleAiResult::kRejected;
  g_vehicle_busy=true;struct Clear { ~Clear(){g_vehicle_busy=false;} } clear;
  // A physical TnPlayerPawn fails admission BEFORE this factory's hard cast.
  // 10BB4D5C dereferences the cast result without a None check; substituting
  // a logical role/name here would be unsafe even when that pawn is an AI role.
  void* compiled=WfcCallEaxPointer(reinterpret_cast<void*>(VehicleAddress(vehicle_code::kFactoryVa)),
      nullptr,static_cast<uint8_t*>(controller)+0x10);
  const auto address=reinterpret_cast<uintptr_t>(compiled);
  if (address<0x10) return NativeVehicleAiResult::kStateMismatch;
  strategy=reinterpret_cast<void*>(address-0x10);
  if (!CaptureVehicle(controller,pawn,&after,true) || !VehicleSame(before,after) || after.input_flags!=before.input_flags ||
      !VehicleGuard(after,move,look,&player_after) || player_before!=player_after ||
      !VehicleField(controller,L"_VehicleAiControl",0x3CC,&value) || value!=previous ||
      (strategy && strategy==previous) || !VehicleStrategy(before,strategy,&path,false)) return NativeVehicleAiResult::kStateMismatch;
  const uint32_t strategy_index=UnrealObjectIndex(strategy),path_index=UnrealObjectIndex(path);
  if (!UnrealWriteObjectPropertyExpected(controller,L"_VehicleAiControl",previous,strategy))
    return NativeVehicleAiResult::kStateMismatch;
  if (strategy) {
    using Initialize=void(__attribute__((stdcall)) *)(void*,void*);
    reinterpret_cast<Initialize>(VehicleAddress(vehicle_code::kInitializeVa))(
        static_cast<uint8_t*>(strategy)+0x10,static_cast<uint8_t*>(controller)+0x10);
  }
  void* final_path=nullptr;
  if (!CaptureVehicle(controller,pawn,&after,true) || !VehicleSame(before,after) || after.input_flags!=before.input_flags ||
      !VehicleGuard(after,move,look,&player_after) || player_before!=player_after ||
      !VehicleField(controller,L"_VehicleAiControl",0x3CC,&value) || value!=strategy ||
      UnrealObjectIndex(strategy)!=strategy_index || UnrealObjectIndex(path)!=path_index ||
      !VehicleStrategy(after,strategy,&final_path,true) || final_path!=path) return NativeVehicleAiResult::kStateMismatch;
  if (strategy) {
    // Activate's cache-only pose copies (109DEF53..109DEF89). Rebuilding a
    // strategy for a pawn ALREADY transformed must not leave zero steering
    // targets. For an already transformed pawn also initialize Activate's
    // navigation intent. VM_OnGround is the native initial planning intent,
    // NOT a command to physically land or change the pawn's form. Plane path
    // callbacks subsequently choose VM_InAir from their navigation goal.
    if (before.mode==1 && !VehicleNavigationWrite(navigation.identity,1))
      return NativeVehicleAiResult::kStateMismatch;
    if (!VehicleMemory(strategy,0x7C,nullptr,false,true)) return NativeVehicleAiResult::kStateMismatch;
    std::memcpy(static_cast<uint8_t*>(strategy)+0x48,before.pose.data(),12);
    std::memcpy(static_cast<uint8_t*>(strategy)+0x30,before.pose.data()+12,12);
    // AI Tick updates the vehicle BEFORE PlanRunner.Update. A suspended GOAP
    // runner alone does not stop a newly created strategy during settle ticks.
    // Use the existing native StopMovement body, including boost release, and
    // retain its stop flag until normal AI movement explicitly supplies a goal.
    uint32_t flags=0;bool stopped=false;
    if (!VehicleMemory(static_cast<uint8_t*>(strategy)+0x68,4,nullptr,false,true) ||
        !UnrealReadBoolProperty(strategy,L"_StopMovement",&stopped)) return NativeVehicleAiResult::kStateMismatch;
    std::memcpy(&flags,static_cast<uint8_t*>(strategy)+0x68,4);
    if (stopped!=((flags&2)!=0)) return NativeVehicleAiResult::kStateMismatch;
    WfcCallEsiVoid(reinterpret_cast<void*>(VehicleAddress(native_ai_stop_code::kVehicleVa)),static_cast<uint8_t*>(strategy)+0x10);
    uint32_t final_flags=0;std::memcpy(&final_flags,static_cast<uint8_t*>(strategy)+0x68,4);
    if (final_flags!=(flags|2u) || !CaptureVehicle(controller,pawn,&after,true) || !VehicleSame(before,after) ||
        after.input_flags!=(before.input_flags&~1u) || !VehicleGuard(after,move,look,&player_after) || player_before!=player_after ||
        std::memcmp(static_cast<uint8_t*>(strategy)+0x48,before.pose.data(),12) ||
        std::memcmp(static_cast<uint8_t*>(strategy)+0x30,before.pose.data()+12,12) ||
        !VehicleField(controller,L"_VehicleAiControl",0x3CC,&value) || value!=strategy ||
        UnrealObjectIndex(strategy)!=strategy_index || UnrealObjectIndex(path)!=path_index ||
        !VehicleStrategy(after,strategy,&final_path,true) || final_path!=path) return NativeVehicleAiResult::kStateMismatch;
  }
  return NativeVehicleAiResult::kApplied;
}
NativeVehicleAiResult NativeRestoreVehicleAi(void* controller,void* pawn,
    void* original,uint32_t original_index,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  VehicleCapture before,after;VehiclePlayerIdentity player_before{},player_after{};
  void *current=nullptr,*path=nullptr,*final_path=nullptr;
  if (g_vehicle_busy || !CaptureVehicle(controller,pawn,&before,true) ||
      !VehicleGuard(before,move,look,&player_before) || !VehicleImage(g_vehicle_image) ||
      !VehicleField(controller,L"_VehicleAiControl",0x3CC,&current)) return NativeVehicleAiResult::kRejected;
  // Check the borrowed identity BEFORE touching any of its private fields.
  const bool original_live=original ? original_index && UnrealObjectIndex(original)==original_index:!original_index;
  if (!original_live || !VehicleStrategy(before,original,&path,true))
    return NativeRebuildVehicleAi(controller,pawn,move,look);
  const uint32_t path_index=UnrealObjectIndex(path);
  g_vehicle_busy=true;struct Clear { ~Clear(){g_vehicle_busy=false;} } clear;
  if (current!=original && !UnrealWriteObjectPropertyExpected(controller,L"_VehicleAiControl",current,original))
    return NativeVehicleAiResult::kStateMismatch;
  if (!CaptureVehicle(controller,pawn,&after,true) || !VehicleSame(before,after) ||
      after.input_flags!=before.input_flags || !VehicleGuard(after,move,look,&player_after) || player_before!=player_after ||
      !VehicleField(controller,L"_VehicleAiControl",0x3CC,&current) || current!=original ||
      UnrealObjectIndex(original)!=original_index || UnrealObjectIndex(path)!=path_index ||
      !VehicleStrategy(after,original,&final_path,true) || final_path!=path) return NativeVehicleAiResult::kStateMismatch;
  return NativeVehicleAiResult::kApplied;
}
}
