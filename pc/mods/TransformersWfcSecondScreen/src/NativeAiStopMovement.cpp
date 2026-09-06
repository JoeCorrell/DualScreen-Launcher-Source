#include "NativeAiMovement.h"
#include <array>
#include <cstring>
#include "NativeAiStopCode.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void WfcCallEsiVoid(void* method,void* receiver);
namespace {
namespace stop_code=native_ai_stop_code;
HMODULE g_stop_module=nullptr;
DWORD g_stop_thread=0;
thread_local bool g_stop_busy=false;
uintptr_t StopAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_stop_module)+va-native_reference_code::kImageBase;
}
bool StopMemory(const void* p,size_t n,HMODULE module=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State!=MEM_COMMIT ||
      (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase!=module) ||
      n>info.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const auto protection=info.Protect&0xFFu;
  if (write && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
      protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool StopPointer(void* object,uint32_t offset,void** out) {
  if (!out || !object || !StopMemory(static_cast<uint8_t*>(object)+offset,4)) return false;
  std::memcpy(out,static_cast<uint8_t*>(object)+offset,4); return true;
}
bool StopField(void* object,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset=0; void* raw=nullptr;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadObjectProperty(object,name,out) && StopPointer(object,offset,&raw) && raw==*out;
}
bool StopLive(void* object,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool StopByte(void* object,const wchar_t* name,uint32_t expected,uint8_t wanted) {
  uint32_t offset=0; uint8_t value=0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset==expected &&
      UnrealReadByteProperty(object,name,&value) && value==wanted;
}
bool BindStopImage(HMODULE module) {
  return stop_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes=native_reference_code::Relocate(code,reinterpret_cast<uintptr_t>(module),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(module)+va-native_reference_code::kImageBase);
    return StopMemory(p,bytes.size(),module,true) && !std::memcmp(p,bytes.data(),bytes.size());
  });
}
bool StopVirtual(void* object,uint32_t slot,void** method) {
  void* table=nullptr;
  return StopPointer(object,0x10,&table) && StopMemory(table,slot+4,g_stop_module) &&
      StopPointer(table,slot,method) && StopMemory(*method,6,g_stop_module,true);
}
bool StopFormMode(void* form,uint8_t wanted) {
  void* method=nullptr;
  if (!StopVirtual(form,0x70,&method)) return false;
  // The current getter must be a complete side-effect-free constant leaf,
  // not an arbitrary subclass override or a guessed class-name mode.
  const auto* b=static_cast<const uint8_t*>(method);
  return (!wanted && (b[0]==0x32 || b[0]==0x33) && b[1]==0xC0 && b[2]==0xC3) ||
      (b[0]==0xB0 && b[1]==wanted && b[2]==0xC3) ||
      (b[0]==0xB8 && b[1]==wanted && !b[2] && !b[3] && !b[4] && b[5]==0xC3);
}
struct StopCapture {
  // controller,pawn,world,current form,strategy(optional),input(optional)
  std::array<void*,6> objects{};
  std::array<uint32_t,6> indices{};
  std::array<uint8_t,12> rotation{};
  uint32_t strategy_flags=0,input_flags=0,input_offset=0;
  bool vehicle=false;
};
bool CaptureStop(void* controller,void* pawn,StopCapture* out) {
  if (!out || !g_stop_module || GetCurrentThreadId()!=g_stop_thread ||
      !StopLive(controller,L"TnAiController") || !StopLive(pawn,L"TnPawn") ||
      !StopByte(controller,L"Role",0x66,3) || !StopByte(pawn,L"Role",0x66,3)) return false;
  *out={}; auto& o=out->objects; o[0]=controller; o[1]=pawn; void* value=nullptr;
  if (!StopField(controller,L"Pawn",0x1C8,&value) || value!=pawn ||
      !StopField(pawn,L"Controller",0x1D4,&value) || value!=controller ||
      !StopField(controller,L"WorldInfo",0x98,&o[2]) || !StopLive(o[2],L"WorldInfo") ||
      !StopByte(o[2],L"NetMode",0x39C,0) ||
      !StopField(pawn,L"WorldInfo",0x98,&value) || value!=o[2]) return false;
  void *engine=nullptr,*context=nullptr,*slot=nullptr;
  if (!StopPointer(reinterpret_cast<void*>(StopAddress(0x12B10A9C)),0,&engine) ||
      !StopPointer(engine,0x40,&context) || !StopPointer(context,0x28,&slot) ||
      !StopPointer(slot,0,&value) || value!=o[2] ||
      !StopField(pawn,L"_CurrentForm",0x584,&o[3]) || !UnrealObjectIsA(o[3],L"TnForm")) return false;
  void *robot=nullptr,*vehicle=nullptr;
  if (!StopField(pawn,L"_RobotForm",0x57C,&robot) || !StopField(pawn,L"_VehicleForm",0x580,&vehicle)) return false;
  if (o[3]==robot && robot!=vehicle && UnrealObjectIsA(robot,L"TnRobotForm") && StopFormMode(robot,0)) {
    uint32_t offset=0;
    if (!UnrealResolveStructPropertyOffset(pawn,L"Acceleration",12,&offset) || offset!=0xFC ||
        !UnrealResolveStructPropertyOffset(pawn,L"DesiredRotation",12,&offset) || offset!=0x174 ||
        !UnrealResolveStructPropertyOffset(pawn,L"Rotation",12,&offset) || offset!=0xE4 ||
        !StopMemory(static_cast<uint8_t*>(pawn)+0xE4,12) ||
        !StopMemory(static_cast<uint8_t*>(pawn)+0xFC,12,nullptr,false,true) ||
        !StopMemory(static_cast<uint8_t*>(pawn)+0x174,12,nullptr,false,true)) return false;
    std::memcpy(out->rotation.data(),static_cast<uint8_t*>(pawn)+0xE4,12);
  } else {
    // Native vehicle strategies still contain physical-AI assumptions. A
    // physical player pawn's mapped vehicle must use the existing proxy input
    // path, not pretend to own a stock strategy. That role work stays gated.
    if (o[3]!=vehicle || !UnrealObjectIsA(pawn,L"TnAiPawn") ||
        !UnrealObjectIsA(vehicle,L"TnVehicleForm") || !StopFormMode(vehicle,1)) return false;
    out->vehicle=true;
    const bool car=UnrealObjectIsA(vehicle,L"TnCarForm"),plane=UnrealObjectIsA(vehicle,L"TnPlaneForm"),tank=UnrealObjectIsA(vehicle,L"TnTankForm");
    if (static_cast<int>(car)+static_cast<int>(plane)+static_cast<int>(tank)!=1) return false;
    const auto setter=car ? stop_code::kCarBoostVa:plane ? stop_code::kPlaneBoostVa:stop_code::kTankBoostVa;
    const wchar_t* input_type=car ? L"TnCarInputState":plane ? L"TnPlaneInputState":L"TnTankInputState";
    void* method=nullptr;
    if (!StopField(controller,L"_VehicleAiControl",0x3CC,&o[4]) || !UnrealObjectIsA(o[4],L"TnVehicleAiControlStrategy") ||
        !StopField(o[4],L"_BoundController",0x28,&value) || value!=controller ||
        !StopField(o[4],L"_TransformerPawn",0x2C,&value) || value!=pawn ||
        !StopField(vehicle,L"_InputState",tank ? 0x104:0x108,&o[5]) || !UnrealObjectIsA(o[5],input_type) ||
        !StopPointer(o[5],0x2C,&value) || value!=pawn ||
        !StopVirtual(vehicle,0xD0,&method) || reinterpret_cast<uintptr_t>(method)!=StopAddress(setter) ||
        !StopMemory(static_cast<uint8_t*>(o[4])+0x68,4,nullptr,false,true)) return false;
    out->input_offset=plane ? 0x4C:0x48;
    if (!StopMemory(static_cast<uint8_t*>(o[5])+out->input_offset,4,nullptr,false,true)) return false;
    std::memcpy(&out->strategy_flags,static_cast<uint8_t*>(o[4])+0x68,4);
    std::memcpy(&out->input_flags,static_cast<uint8_t*>(o[5])+out->input_offset,4);
    bool stopped=false,boost=false;
    if (!UnrealReadBoolProperty(o[4],L"_StopMovement",&stopped) || stopped!=((out->strategy_flags&2)!=0) ||
        !UnrealReadBoolProperty(o[5],L"_Boosting",&boost) || boost!=((out->input_flags&1)!=0)) return false;
  }
  for (size_t i=0;i<o.size();++i) {
    out->indices[i]=UnrealObjectIndex(o[i]); if (o[i] && !out->indices[i]) return false;
  }
  return true;
}
}
bool PrepareNativeAiStopMovement(HMODULE module,DWORD thread,void* controller,void* pawn) {
  if (g_stop_busy || !module || module!=GetModuleHandleW(nullptr) || !thread || thread!=GetCurrentThreadId() ||
      (g_stop_module && (module!=g_stop_module || thread!=g_stop_thread)) || !BindStopImage(module)) return false;
  g_stop_module=module; g_stop_thread=thread;
  StopCapture capture; return CaptureStop(controller,pawn,&capture);
}
NativeAiMovementResult NativeAiStopMovement(void* controller,void* pawn) {
  StopCapture before;
  if (g_stop_busy || !CaptureStop(controller,pawn,&before)) return NativeAiMovementResult::kRejected;
  g_stop_busy=true; struct ClearBusy { ~ClearBusy() { g_stop_busy=false; } } busy;
  if (before.vehicle) {
    WfcCallEsiVoid(reinterpret_cast<void*>(StopAddress(stop_code::kVehicleVa)),static_cast<uint8_t*>(before.objects[4])+0x10);
  } else {
    // Exact native robot branch, lifted across the concrete TnAiPawn cast.
    // Do not zero Velocity, set physics, pop states or change the actual pose.
    std::memset(static_cast<uint8_t*>(pawn)+0xFC,0,12);
    std::memcpy(static_cast<uint8_t*>(pawn)+0x174,before.rotation.data(),12);
  }
  StopCapture after;
  if (!CaptureStop(controller,pawn,&after) || after.objects!=before.objects || after.indices!=before.indices ||
      after.vehicle!=before.vehicle || after.rotation!=before.rotation) return NativeAiMovementResult::kStateMismatch;
  if (before.vehicle) {
    if (after.strategy_flags!=(before.strategy_flags|2u) || after.input_flags!=(before.input_flags&~1u))
      return NativeAiMovementResult::kStateMismatch;
  } else {
    const std::array<uint8_t,12> zero{};
    if (std::memcmp(static_cast<uint8_t*>(pawn)+0xFC,zero.data(),12) ||
        std::memcmp(static_cast<uint8_t*>(pawn)+0x174,before.rotation.data(),12)) return NativeAiMovementResult::kStateMismatch;
  }
  return NativeAiMovementResult::kApplied;
}
}
