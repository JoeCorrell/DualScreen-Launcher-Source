#include "NativeAiAdoption.h"
#include <array>
#include <cmath>
#include <cstring>
#include "Log.h"
#include "NativeAiAdoptionCode.h"
#include "NativeAiPlanning.h"
#include "NativePawnOwnership.h"
#include "NativeWeaponShooter.h"
#include "UnrealBridge.h"

namespace wfcds {
extern "C" void __cdecl WfcCallEdiEax(void* target, void* receiver, void* argument);
namespace {
struct AiAdoptionBindings { HMODULE module = nullptr; DWORD thread = 0; };
AiAdoptionBindings g_ai_adoption;
thread_local bool g_ai_adoption_busy = false;
constexpr size_t kAiAdoptionObserverLimit = 256;
struct AiAdoptionCapture {
  // World, PRI (optional), analyzer, icon, aimer, shooter, range detector,
  // plan runner, debug manager (optional). No controller-owned service is rebuilt.
  std::array<void*,9> objects{};
  std::array<uint32_t,9> indices{};
  uint32_t controller_index = 0, pawn_index = 0, other_index = 0;
  std::array<void*,3> owners{};
  std::array<uint8_t,0x30> pose{};
  std::array<uint8_t,64> ranges{};
  UnrealStructArrayView source_range{}, detector_range{};
  UnrealArrayView observer_array{};
  std::array<UnrealScriptDelegate,kAiAdoptionObserverLimit> observers{};
  int32_t own_observer = -1;
};
uintptr_t AiAdoptionAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_ai_adoption.module)+va-native_reference_code::kImageBase;
}
bool AiAdoptionMemory(const void* p,size_t size,HMODULE module = nullptr,bool execute = false,bool write = false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase != module) ||
      size > info.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD prot = info.Protect & 0xFFu;
  if (write && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
      prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
      prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}
bool BindAiAdoptionImage(HMODULE module,DWORD thread,AiAdoptionBindings* out) {
  if (!module || !out || !thread || thread != GetCurrentThreadId()) return false;
  const uintptr_t base = reinterpret_cast<uintptr_t>(module);
  if (!native_ai_adoption_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto bytes = native_reference_code::Relocate(code,base,operands);
    const void* address = reinterpret_cast<void*>(base+va-native_reference_code::kImageBase);
    return AiAdoptionMemory(address,bytes.size(),module,true) && !std::memcmp(address,bytes.data(),bytes.size());
  })) return false;
  *out = {module,thread}; return true;
}
bool AiAdoptionField(void* object,const wchar_t* name,uint32_t expected,void** out) {
  uint32_t offset = 0;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadObjectProperty(object,name,out);
}
bool AiAdoptionLive(void* object,const wchar_t* type) {
  bool deleting = true;
  return UnrealObjectIsA(object,type) && UnrealObjectIndex(object) &&
      UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool AiAdoptionByte(void* object,const wchar_t* name,uint32_t expected,uint8_t required) {
  uint32_t offset = 0; uint8_t value = 0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadByteProperty(object,name,&value) && value == required;
}
bool AiAdoptionPawn(void* pawn,void* world) {
  void* value = nullptr; bool stasis = true, tear_off = true;
  return AiAdoptionLive(pawn,L"TnPawn") &&
      AiAdoptionMemory(pawn,0x42C,nullptr,false,true) &&
      AiAdoptionByte(pawn,L"Role",0x66,3) &&
      AiAdoptionField(pawn,L"WorldInfo",0x98,&value) && value == world &&
      UnrealReadBoolProperty(pawn,L"bStasis",&stasis) && !stasis &&
      UnrealReadBoolProperty(pawn,L"bTearOff",&tear_off) && !tear_off;
}
bool AiAdoptionRange(void* object,const wchar_t* name,uint32_t offset,UnrealStructArrayView* out) {
  char type[64]{}; UnrealArrayView raw;
  if (!UnrealReadStructArrayProperty(object,name,out) || out->count != 8 ||
      out->capacity < 8 || out->capacity > 4096 || out->element_size != 8 ||
      !UnrealObjectName(out->struct_definition,type,sizeof(type)) || std::strcmp(type,"FloatRange") ||
      !AiAdoptionMemory(static_cast<uint8_t*>(object)+offset,sizeof(raw)) ||
      !AiAdoptionMemory(out->data,64)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+offset,sizeof(raw));
  if (raw.data != out->data || raw.count != out->count || raw.capacity != out->capacity) return false;
  for (size_t i = 0; i < 16; ++i) {
    float value = 0; std::memcpy(&value,static_cast<uint8_t*>(out->data)+4*i,4);
    if (!std::isfinite(value)) return false;
  }
  return true;
}
bool CaptureAiAdoption(void* controller,void* pawn,void* other,AiAdoptionCapture* out) {
  if (!out || !g_ai_adoption.module || GetCurrentThreadId() != g_ai_adoption.thread || pawn == other ||
      !AiAdoptionLive(controller,L"TnAiController") || !AiAdoptionMemory(controller,0x45C,nullptr,false,true) ||
      !AiAdoptionByte(controller,L"Role",0x66,3)) return false;
  *out = {};
  const struct { const wchar_t* name; uint32_t offset; const wchar_t* type; bool optional; } fields[] = {
    {L"WorldInfo",0x98,L"WorldInfo",false}, {L"PlayerReplicationInfo",0x1CC,L"PlayerReplicationInfo",true},
    {L"TacticalAnalyzer",0x2F4,L"TnTacticalAnalyzer",false}, {L"_IconDrawer",0x3C4,L"HmPawnIconDrawer",false},
    {L"WeaponAimer",0x2FC,L"TnAiWeaponAimer",false}, {L"WeaponShooter",0x2F8,L"TnAiWeaponShooter",false},
    {L"_RangeDetector",0x458,L"TnAiRangeDetector",false}, {L"PlanRunner",0x304,L"HmGoapPlanRunner",false},
    {L"_DebugManager",0x3F4,L"TnDebugManager",true}};
  for (size_t i = 0; i < out->objects.size(); ++i) {
    const auto& f = fields[i]; void*& object = out->objects[i];
    if (!AiAdoptionField(controller,f.name,f.offset,&object) ||
        ((!f.optional || object) && (!UnrealObjectIsA(object,f.type) || !UnrealObjectIndex(object)))) return false;
    out->indices[i] = UnrealObjectIndex(object);
  }
  void* value = nullptr;
  if (!AiAdoptionLive(out->objects[0],L"WorldInfo") || !AiAdoptionByte(out->objects[0],L"NetMode",0x39C,0) ||
      !AiAdoptionPawn(pawn,out->objects[0]) || !AiAdoptionPawn(other,out->objects[0]) ||
      !AiAdoptionField(out->objects[2],L"Owner",0x28,&value) || value != controller ||
      !AiAdoptionField(out->objects[7],L"_Owner",0x30,&value) || value != controller ||
      !AiAdoptionField(out->objects[3],L"_OwnerPawn",0x28,&out->owners[0]) ||
      !AiAdoptionField(out->objects[4],L"_Owner",0x28,&out->owners[1]) ||
      !AiAdoptionField(out->objects[5],L"Owner",0x2C,&out->owners[2]) ||
      !AiAdoptionField(out->objects[4],L"_OwnerController",0x30,&value) || value != controller) return false;
  for (void* owner : out->owners) if (owner != pawn && owner != other) return false;
  if (!AiAdoptionMemory(out->objects[3],0x2C,nullptr,false,true) ||
      !AiAdoptionMemory(out->objects[4],0x34,nullptr,false,true) ||
      !AiAdoptionMemory(out->objects[5],0x30,nullptr,false,true) ||
      !AiAdoptionRange(controller,L"RangeSet",0x360,&out->source_range) ||
      !AiAdoptionRange(out->objects[6],L"Range",0x28,&out->detector_range) ||
      out->source_range.struct_definition != out->detector_range.struct_definition ||
      std::memcmp(out->source_range.data,out->detector_range.data,64)) return false;
  // Native RangeSet is eight FloatRange elements, not a tagged union. The
  // existing detector must already match; don't allocate/reset unchanged data.
  std::memcpy(out->ranges.data(),out->source_range.data,out->ranges.size());
  uint32_t offset = 0;
  auto& a = out->observer_array;
  if (!UnrealReadDelegateArrayProperty(out->objects[2],L"NewEnemyObservers",&a,&offset) || offset != 0x34 ||
      a.count < 0 || a.count > static_cast<int32_t>(kAiAdoptionObserverLimit) ||
      a.capacity < a.count || a.capacity > 4096 ||
      !AiAdoptionMemory(static_cast<uint8_t*>(out->objects[2])+offset,sizeof(a),nullptr,false,true) ||
      (a.capacity && !AiAdoptionMemory(a.data,static_cast<size_t>(a.capacity)*16,nullptr,false,true))) return false;
  const auto* name = reinterpret_cast<const uint32_t*>(AiAdoptionAddress(0x12B31C68u));
  if (!AiAdoptionMemory(name,4,g_ai_adoption.module) || !*name) return false;
  if (a.count) std::memcpy(out->observers.data(),a.data,static_cast<size_t>(a.count)*16);
  for (int32_t i = 0; i < a.count; ++i) {
    const auto& d = out->observers[i];
    if (d.object != controller || d.function_name != *name) continue;
    if (out->own_observer != -1 || d.compiled_context != static_cast<uint8_t*>(controller)+0x10 ||
        reinterpret_cast<uintptr_t>(d.compiled_function) != AiAdoptionAddress(0x10BB84E0u)) return false;
    out->own_observer = i;
  }
  if (out->own_observer < 0 && a.count == static_cast<int32_t>(kAiAdoptionObserverLimit)) return false;
  out->controller_index = UnrealObjectIndex(controller); out->pawn_index = UnrealObjectIndex(pawn);
  out->other_index = UnrealObjectIndex(other);
  std::memcpy(out->pose.data(),static_cast<uint8_t*>(pawn)+0xD8,out->pose.size());
  return true;
}
bool AiAdoptionSame(const AiAdoptionCapture& a,const AiAdoptionCapture& b) {
  const auto same_range = [](const UnrealStructArrayView& x,const UnrealStructArrayView& y) {
    return x.data == y.data && x.count == y.count && x.capacity == y.capacity &&
        x.element_size == y.element_size && x.struct_definition == y.struct_definition;
  };
  return a.objects == b.objects && a.indices == b.indices && a.controller_index == b.controller_index &&
      a.pawn_index == b.pawn_index && a.other_index == b.other_index && a.pose == b.pose && a.ranges == b.ranges &&
      same_range(a.source_range,b.source_range) && same_range(a.detector_range,b.detector_range);
}
bool AiAdoptionQuiescent(void* controller,const AiAdoptionCapture& c,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look,
    void* other,void* expected_camera,uint32_t camera_index) {
  void* world = nullptr; void* camera = nullptr; void* value = nullptr;
  return move.controller != controller && move.controller == look.controller &&
      move.world == c.objects[0] && look.world == c.objects[0] &&
      move.axis == NativeInputAxis::kMove && look.axis == NativeInputAxis::kLook &&
      NativePlayerInputHeld(move) && NativePlayerInputHeld(look) &&
      AiAdoptionLive(move.controller,L"TnPlayerController") &&
      AiAdoptionByte(move.controller,L"Role",0x66,3) &&
      AiAdoptionField(move.controller,L"WorldInfo",0x98,&world) && world == c.objects[0] &&
      AiAdoptionField(move.controller,L"Pawn",0x1C8,&value) && value == other &&
      AiAdoptionField(move.controller,L"AcknowledgedPawn",0x300,&value) && value == other &&
      AiAdoptionField(other,L"Controller",0x1D4,&value) && value == move.controller &&
      UnrealReadObjectProperty(other,L"Owner",&value) && value == move.controller &&
      AiAdoptionField(move.controller,L"PlayerCamera",0x2D4,&camera) && AiAdoptionLive(camera,L"Camera") &&
      camera == expected_camera && UnrealObjectIndex(camera) == camera_index &&
      AiAdoptionField(camera,L"WorldInfo",0x98,&world) && world == c.objects[0] &&
      NativeAiPlanHandoffReady(controller) && NativeWeaponShooterHandoffReady(c.objects[5]);
}
bool AiAdoptionObserversEqual(const AiAdoptionCapture& a,const AiAdoptionCapture& b) {
  return a.observer_array.count == b.observer_array.count &&
      !std::memcmp(a.observers.data(),b.observers.data(),static_cast<size_t>(a.observer_array.count)*16);
}
bool AiAdoptionAddObserver(void* controller,const AiAdoptionCapture& before) {
  uint32_t name = 0; std::memcpy(&name,reinterpret_cast<void*>(AiAdoptionAddress(0x12B31C68u)),4);
  UnrealScriptDelegate delegate{controller,name,static_cast<uint8_t*>(controller)+0x10,
      reinterpret_cast<void*>(AiAdoptionAddress(0x10BB84E0u))};
  void* header = static_cast<uint8_t*>(before.objects[2])+0x34;
  using Find = int32_t (__attribute__((fastcall)) *)(void*,void*,const UnrealScriptDelegate*);
  const int32_t index = reinterpret_cast<Find>(AiAdoptionAddress(native_detach_code::kFindObserverVa))(
      nullptr,header,&delegate); // EDX array; stack delegate; RET4.
  if (index != before.own_observer) return false;
  if (index < 0) {
    using Append = int32_t (__attribute__((regparm(1),stdcall)) *)(void*,const UnrealScriptDelegate*);
    if (reinterpret_cast<Append>(AiAdoptionAddress(native_ai_adoption_code::kAppendVa))(header,&delegate) !=
        before.observer_array.count) return false;
  }
  return true;
}
}  // namespace

bool PrepareNativeAiAdoption(HMODULE module,DWORD thread,void* controller,void* first,void* second) {
  if (g_ai_adoption_busy || !module || module != GetModuleHandleW(nullptr) ||
      !thread || thread != GetCurrentThreadId()) return false;
  if (!g_ai_adoption.module && !BindAiAdoptionImage(module,thread,&g_ai_adoption)) {
    Log(LogLevel::kError,"[SquadSwitch] native AI-adoption signatures mismatch"); return false;
  }
  AiAdoptionCapture capture;
  return module == g_ai_adoption.module && thread == g_ai_adoption.thread && UnrealObjectReferenceWritesReady() &&
      CaptureAiAdoption(controller,first,second,&capture);
}
NativeAiAdoptionResult NativeAdoptExistingAiPawn(void* controller,void* pawn,void* other,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  AiAdoptionCapture before; void* value = nullptr; void* camera = nullptr; uint32_t camera_index = 0;
  if (g_ai_adoption_busy || !UnrealObjectReferenceWritesReady() ||
      !CaptureAiAdoption(controller,pawn,other,&before) ||
      !AiAdoptionField(move.controller,L"PlayerCamera",0x2D4,&camera) || !(camera_index = UnrealObjectIndex(camera)) ||
      !AiAdoptionQuiescent(controller,before,move,look,other,camera,camera_index) ||
      !AiAdoptionField(controller,L"Pawn",0x1C8,&value) || value)
    return NativeAiAdoptionResult::kRejected;
  g_ai_adoption_busy = true;
  struct ClearBusy { ~ClearBusy() { g_ai_adoption_busy = false; } } guard;
  const auto failed = [](const char* stage) {
    Log(LogLevel::kError,"[SquadSwitch] AI-adoption dirty failure stage=%s; retain transaction guards",stage);
    return NativeAiAdoptionResult::kStateMismatch;
  };
  const auto ownership = NativeBindPawnOwnership(pawn,controller);
  if (ownership == NativePawnOwnershipResult::kRejected) return NativeAiAdoptionResult::kRejected;
  AiAdoptionCapture after;
  if (ownership != NativePawnOwnershipResult::kApplied || !CaptureAiAdoption(controller,pawn,other,&after) ||
      !AiAdoptionSame(before,after) || before.owners != after.owners || !AiAdoptionObserversEqual(before,after) ||
      !AiAdoptionQuiescent(controller,after,move,look,other,camera,camera_index)) return failed("pawn-ownership");
  InterlockedOr(reinterpret_cast<volatile LONG*>(static_cast<uint8_t*>(controller)+0x30),8);
  if (!UnrealWriteObjectPropertyExpected(controller,L"Pawn",nullptr,pawn) ||
      !UnrealWriteObjectPropertyExpected(before.objects[3],L"_OwnerPawn",before.owners[0],pawn))
    return failed("publish-controller/icon");
  // Native initializer takes TnPawn, not TnAiPawn. Supplying the actual pawn
  // avoids the incompatible inline AI-only casts in full TnAiController.Possess.
  WfcCallEdiEax(reinterpret_cast<void*>(AiAdoptionAddress(native_ai_adoption_code::kAimerVa)),
      static_cast<uint8_t*>(before.objects[4])+0x10,static_cast<uint8_t*>(pawn)+0x10);
  if (!AiAdoptionField(before.objects[4],L"_Owner",0x28,&value) || value != pawn ||
      !AiAdoptionField(before.objects[4],L"_OwnerController",0x30,&value) || value != controller ||
      !AiAdoptionQuiescent(controller,before,move,look,other,camera,camera_index) ||
      !UnrealWriteObjectPropertyExpected(before.objects[5],L"Owner",before.owners[2],pawn))
    return failed("aimer/shooter");
  // Revalidate native callback side effects BEFORE registering the observer.
  if (!CaptureAiAdoption(controller,pawn,other,&after) || !AiAdoptionSame(before,after) ||
      !AiAdoptionObserversEqual(before,after) || !AiAdoptionAddObserver(controller,before))
    return failed("enemy-observer");
  if (!CaptureAiAdoption(controller,pawn,other,&after) || !AiAdoptionSame(before,after) ||
      !AiAdoptionQuiescent(controller,after,move,look,other,camera,camera_index) ||
      !AiAdoptionField(controller,L"Pawn",0x1C8,&value) || value != pawn ||
      !AiAdoptionField(pawn,L"Controller",0x1D4,&value) || value != controller ||
      !UnrealReadObjectProperty(pawn,L"Owner",&value) || value != controller ||
      !AiAdoptionField(pawn,L"PlayerReplicationInfo",0x3C0,&value) || value != before.objects[1])
    return failed("postconditions");
  for (void* owner : after.owners) if (owner != pawn) return failed("service-owners");
  const int32_t expected_index = before.own_observer < 0 ? before.observer_array.count : before.own_observer;
  if (after.own_observer != expected_index ||
      after.observer_array.count != before.observer_array.count+(before.own_observer < 0 ? 1 : 0) ||
      std::memcmp(before.observers.data(),after.observers.data(),static_cast<size_t>(before.observer_array.count)*16))
    return failed("observer-postconditions");
  return NativeAiAdoptionResult::kApplied;
}
}  // namespace wfcds
