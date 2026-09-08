#include "NativeAiSensors.h"
#include "NativeAiSensorCode.h"
#include "NativeAiPlanning.h"
#include "UnrealBridge.h"
#include <cstring>

namespace wfcds {
namespace {
HMODULE g_sensor_image=nullptr;
DWORD g_sensor_thread=0;
thread_local bool g_sensor_busy=false;
// Compiled script ABI is not a C++ member-function ABI. Keep the thunk local;
// GCC diagnoses a thiscall attribute on a free-function pointer under -Werror.
__attribute__((naked)) void SensorInitializeCall(void*,void*,void*,void*) {
  asm volatile("pushl %ebp\n movl %esp,%ebp\n pushl 20(%ebp)\n pushl 16(%ebp)\n"
               "movl 12(%ebp),%ecx\n call *8(%ebp)\n leave\n ret");
}
bool SensorMemory(const void* p,size_t n,HMODULE image=nullptr,bool execute=false,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !n || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT ||
      (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || (image && m.AllocationBase!=image) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD protection=m.Protect&0xff;
  if (write && protection!=PAGE_READWRITE && protection!=PAGE_WRITECOPY &&
      protection!=PAGE_EXECUTE_READWRITE && protection!=PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || protection==PAGE_EXECUTE || protection==PAGE_EXECUTE_READ ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool SensorCode(HMODULE image) {
  return native_ai_sensor_code::Visit([&](uintptr_t va,const auto& bytes,std::initializer_list<size_t> operands) {
    const auto expected=native_reference_code::Relocate(bytes,reinterpret_cast<uintptr_t>(image),operands);
    const auto* p=reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(image)+va-native_reference_code::kImageBase);
    return SensorMemory(p,expected.size(),image,true) && !std::memcmp(p,expected.data(),expected.size());
  });
}
bool SensorField(void* p,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset=0;void* raw=nullptr;
  if (!UnrealResolveObjectPropertyOffset(p,name,&offset) || offset!=expected ||
      !SensorMemory(static_cast<uint8_t*>(p)+offset,4) || !UnrealReadObjectProperty(p,name,out)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(p)+offset,4);return raw==*out;
}
bool SensorLive(void* p,const wchar_t* type) {
  bool deleting=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) &&
      UnrealReadBoolProperty(p,L"bDeleteMe",&deleting) && !deleting;
}
struct SensorCapture {
  // The controller continues rooting analyzer/set/database throughout the call.
  std::array<void*,4> objects{};
  std::array<uint32_t,4> indices{};
  std::array<void*,128> sensors{};
  std::array<uint32_t,128> sensor_indices{};
  std::array<void*,128> tables{};
  std::array<void*,128> initializers{};
  std::array<void*,128> callbacks{};
  int32_t count=0;
};
bool CaptureSensors(void* controller,SensorCapture* out) {
  if (!out || !g_sensor_image || GetCurrentThreadId()!=g_sensor_thread ||
      !SensorLive(controller,L"TnAiController")) return false;
  *out={};auto& o=out->objects;void* owner=nullptr;o[0]=controller;
  if (!SensorField(controller,L"TacticalAnalyzer",0x2F4,&o[1]) ||
      !UnrealObjectIsA(o[1],L"TnTacticalAnalyzer") ||
      !SensorField(o[1],L"Owner",0x28,&owner) || owner!=controller ||
      !SensorField(o[1],L"SensorSet",0x30,&o[2]) || !UnrealObjectIsA(o[2],L"HmAiSensorSet") ||
      !SensorMemory(static_cast<uint8_t*>(o[1])+0x30,4,nullptr,false,true) ||
      !SensorField(o[1],L"TacticalData",0x2C,&o[3]) || !UnrealObjectIsA(o[3],L"TnTacticalDatabase")) return false;
  for(size_t i=0;i<o.size();++i)if (!(out->indices[i]=UnrealObjectIndex(o[i]))) return false;
  UnrealArrayView array{};
  if (!UnrealReadArrayProperty(o[2],L"Sensors",&array) || array.count<0 || array.count>128 ||
      array.capacity<array.count || array.capacity>256 ||
      !SensorMemory(static_cast<uint8_t*>(o[2])+0x28,12)) return false;
  UnrealArrayView raw{};std::memcpy(&raw,static_cast<uint8_t*>(o[2])+0x28,12);
  if (raw.data!=array.data || raw.count!=array.count || raw.capacity!=array.capacity) return false;
  out->count=array.count;
  for(int32_t i=0;i<array.count;++i) {
    auto& sensor=out->sensors[i];void* table=nullptr;void* initialize=nullptr;void* callback=nullptr;
    if (!UnrealReadObjectArrayElement(array,i,&sensor) || !UnrealObjectIsA(sensor,L"TnAiSensor") ||
        !(out->sensor_indices[i]=UnrealObjectIndex(sensor)) || !SensorMemory(static_cast<uint8_t*>(sensor)+0x10,4) ||
        !SensorMemory(static_cast<uint8_t*>(sensor)+0x28,12,nullptr,false,true)) return false;
    for(int32_t j=0;j<i;++j)if (sensor==out->sensors[j]) return false;
    std::memcpy(&table,static_cast<uint8_t*>(sensor)+0x10,4);
    if (!SensorMemory(table,0x48,g_sensor_image)) return false;
    std::memcpy(&initialize,static_cast<uint8_t*>(table)+0x40,4);
    std::memcpy(&callback,static_cast<uint8_t*>(table)+0x44,4);
    // Respect existing native virtual overrides. Foreign module callbacks or
    // missing interface slots are not a supported lifecycle.
    if (!SensorMemory(initialize,1,g_sensor_image,true) || !SensorMemory(callback,1,g_sensor_image,true)) return false;
    out->tables[i]=table;out->initializers[i]=initialize;out->callbacks[i]=callback;
  }
  return true;
}
bool SensorSame(const SensorCapture& a,const SensorCapture& b) {
  return a.objects==b.objects && a.indices==b.indices && a.count==b.count &&
      a.sensors==b.sensors && a.sensor_indices==b.sensor_indices &&
      a.tables==b.tables && a.initializers==b.initializers && a.callbacks==b.callbacks;
}
bool SensorOwners(const SensorCapture& c,void* pawn,void* previous,int32_t initialized) {
  for(int32_t i=0;i<c.count;++i) {
    void *owner=nullptr,*cached=nullptr,*data=nullptr;
    if (!SensorField(c.sensors[i],L"_OwnerController",0x28,&owner) || owner!=c.objects[0] ||
        !SensorField(c.sensors[i],L"_OwnerPawn",0x2C,&cached) || (cached!=pawn && (i<initialized || cached!=previous)) ||
        !SensorField(c.sensors[i],L"_TacticalDatabase",0x30,&data) || data!=c.objects[3]) return false;
  }
  return true;
}
bool SensorGuard(void* controller,void* pawn,void* previous,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  void *value=nullptr,*world=nullptr;uint8_t role=0,net=255;
  return pawn!=previous && SensorLive(pawn,L"TnAiPawn") && SensorLive(previous,L"TnPawn") &&
      SensorLive(move.controller,L"TnPlayerController") && move.controller!=controller &&
      look.controller==move.controller && move.axis==NativeInputAxis::kMove && look.axis==NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look) && NativeAiPlanHandoffReady(controller) &&
      UnrealObjectReferenceWritesReady() &&
      SensorField(controller,L"WorldInfo",0x98,&world) && SensorLive(world,L"WorldInfo") &&
      move.world==world && look.world==world &&
      UnrealReadByteProperty(world,L"NetMode",&net) && net==0 &&
      UnrealReadByteProperty(controller,L"Role",&role) && role==3 &&
      UnrealReadByteProperty(pawn,L"Role",&role) && role==3 &&
      UnrealReadByteProperty(previous,L"Role",&role) && role==3 &&
      UnrealReadByteProperty(move.controller,L"Role",&role) && role==3 &&
      SensorField(controller,L"Pawn",0x1C8,&value) && value==pawn &&
      SensorField(pawn,L"Controller",0x1D4,&value) && value==controller &&
      UnrealReadObjectProperty(pawn,L"Owner",&value) && value==controller &&
      SensorField(pawn,L"WorldInfo",0x98,&value) && value==world &&
      SensorField(move.controller,L"Pawn",0x1C8,&value) && value==previous &&
      SensorField(move.controller,L"WorldInfo",0x98,&value) && value==world &&
      SensorField(move.controller,L"AcknowledgedPawn",0x300,&value) && value==previous &&
      SensorField(previous,L"Controller",0x1D4,&value) && value==move.controller &&
      UnrealReadObjectProperty(previous,L"Owner",&value) && value==move.controller &&
      SensorField(previous,L"WorldInfo",0x98,&value) && value==world;
}
}
bool PrepareNativeAiSensors(HMODULE image,DWORD thread,void* controller) {
  if (g_sensor_busy || !image || !thread || thread!=GetCurrentThreadId() ||
      (g_sensor_image && (image!=g_sensor_image || thread!=g_sensor_thread)) || !SensorCode(image)) return false;
  g_sensor_image=image;g_sensor_thread=thread;SensorCapture capture{};
  return CaptureSensors(controller,&capture);
}
NativeAiSensorResult NativeRebindAiSensors(void* controller,void* pawn,void* previous,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  SensorCapture before{},after{};
  if (g_sensor_busy || !CaptureSensors(controller,&before) || !SensorCode(g_sensor_image) ||
      !SensorGuard(controller,pawn,previous,move,look) || !SensorOwners(before,pawn,previous,0))
    return NativeAiSensorResult::kRejected;
  const std::array<uint32_t,3> identities={UnrealObjectIndex(pawn),UnrealObjectIndex(previous),UnrealObjectIndex(move.controller)};
  g_sensor_busy=true;struct Clear {~Clear(){g_sensor_busy=false;}} clear;
  // SetSensorSet's verified loop dispatches Initialize(controller+10,data+10)
  // through sensor's script table +40 (ECX=sensor+10, RET8). For an already
  // identical set the preceding root assignment is a no-op. Preserve that root
  // and dispatch the exact same overrides individually: subclass callbacks may
  // invalidate the handoff, so never enter the next callback on stale ownership.
  for(int32_t i=0;i<before.count;++i) {
    SensorInitializeCall(before.initializers[i],
        static_cast<uint8_t*>(before.sensors[i])+0x10,
        static_cast<uint8_t*>(controller)+0x10,
        static_cast<uint8_t*>(before.objects[3])+0x10);
    const std::array<uint32_t,3> current={UnrealObjectIndex(pawn),UnrealObjectIndex(previous),UnrealObjectIndex(move.controller)};
    if (identities!=current || !CaptureSensors(controller,&after) || !SensorSame(before,after) ||
        !SensorGuard(controller,pawn,previous,move,look) || !SensorOwners(after,pawn,previous,i+1))
      return NativeAiSensorResult::kStateMismatch;
  }
  return NativeAiSensorResult::kApplied;
}
}
