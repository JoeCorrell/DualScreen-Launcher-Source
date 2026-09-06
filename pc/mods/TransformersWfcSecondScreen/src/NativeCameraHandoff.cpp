#include "NativeCameraHandoff.h"
#include <array>
#include <cmath>
#include <cstring>
#include "Log.h"
#include "NativeCameraCode.h"
#include "NativeViewTargetCode.h"
#include "NativeScriptAbi.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {
using InitializeFormsCall = void (__attribute__((regparm(1))) *)(void* manager);
using StartFormCall = void (__attribute__((fastcall)) *)(void* manager, void*, uint32_t form);
struct CameraBindings { HMODULE module = nullptr; DWORD thread = 0; };
CameraBindings g_camera_handoff;
thread_local bool g_camera_handoff_busy = false;
// Verified state selector 10BDA8A0. Only ordinary gameplay states (and None,
// for recovery of a partially applied handoff), not death/turret/cinematics.
constexpr uintptr_t kCameraTables[] = {0x12801188, 0x128B6D10, 0x128B6DE0,
    0x128B6E48, 0x128B6EB0, 0x128B6F18, 0x128B6F80};
struct CameraCapture {
  // player, new pawn, other pawn, world, manager, camera, strategy set, form,
  // selected strategy type, selected strategy. Identities survive callbacks.
  std::array<void*,10> objects{};
  std::array<uint32_t,10> indices{};
  void* previous_owner = nullptr;
  uint8_t type = 4;
};
uintptr_t CameraAddress(uintptr_t va) {
  return reinterpret_cast<uintptr_t>(g_camera_handoff.module) + va - native_reference_code::kImageBase;
}
bool CameraMemory(const void* p, size_t size, HMODULE module = nullptr, bool execute = false, bool write = false) {
  MEMORY_BASIC_INFORMATION info{};
  if (!p || !VirtualQuery(p,&info,sizeof(info)) || info.State != MEM_COMMIT ||
      (info.Protect & (PAGE_GUARD|PAGE_NOACCESS)) || (module && info.AllocationBase != module) ||
      size > info.RegionSize - (reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(info.BaseAddress))) return false;
  const DWORD prot = info.Protect & 0xFFu;
  if (write && prot != PAGE_READWRITE && prot != PAGE_WRITECOPY &&
      prot != PAGE_EXECUTE_READWRITE && prot != PAGE_EXECUTE_WRITECOPY) return false;
  return !execute || prot == PAGE_EXECUTE || prot == PAGE_EXECUTE_READ ||
      prot == PAGE_EXECUTE_READWRITE || prot == PAGE_EXECUTE_WRITECOPY;
}
bool CameraPointer(const void* base, size_t offset, void** out) {
  if (!base || !out || !CameraMemory(static_cast<const uint8_t*>(base)+offset,4)) return false;
  std::memcpy(out,static_cast<const uint8_t*>(base)+offset,4); return true;
}
bool CameraField(void* object, const wchar_t* name, uint32_t expected, void** out) {
  uint32_t offset = 0;
  return UnrealResolveObjectPropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadObjectProperty(object,name,out);
}
bool CameraLive(void* object, const wchar_t* type) {
  bool deleting = true;
  return UnrealObjectIsA(object,type) && UnrealReadBoolProperty(object,L"bDeleteMe",&deleting) && !deleting;
}
bool CameraByte(void* object,const wchar_t* name,uint32_t expected,uint8_t required) {
  uint32_t offset = 0; uint8_t value = 0;
  return UnrealResolveBytePropertyOffset(object,name,&offset) && offset == expected &&
      UnrealReadByteProperty(object,name,&value) && value == required;
}
bool CameraCurrentWorld(void* expected) {
  void *engine = nullptr, *context = nullptr, *slot = nullptr, *world = nullptr;
  return CameraPointer(reinterpret_cast<void*>(CameraAddress(0x12B10A9C)),0,&engine) &&
      CameraPointer(engine,0x40,&context) && CameraPointer(context,0x28,&slot) &&
      CameraPointer(slot,0,&world) && world == expected;
}
bool CameraNamesReady() {
  constexpr uintptr_t names[] = {0x12B2FA48,0x12B324AC,0x12B2FF48,0x12B32FA4,0x12B320B4};
  std::array<uint32_t,5> values{};
  for (size_t i = 0; i < values.size(); ++i) {
    const auto* p = reinterpret_cast<const void*>(CameraAddress(names[i]));
    if (!CameraMemory(p,4,g_camera_handoff.module)) return false;
    std::memcpy(&values[i],p,4);
    if ((i == 0 && values[i] != 0) || (i != 0 && values[i] == 0)) return false;
    for (size_t j = 0; j < i; ++j) if (values[j] == values[i]) return false;
  }
  return true;
}
bool CameraPawnMode(void* pawn,uint8_t mode) {
  void *current = nullptr, *form = nullptr, *transformation = nullptr; float blend = 0;
  return UnrealReadObjectProperty(pawn,L"_CurrentForm",&current) && current &&
      UnrealReadObjectProperty(pawn,mode == 1 ? L"_VehicleForm" : L"_RobotForm",&form) && current == form &&
      UnrealReadObjectProperty(pawn,L"_CurrentTransformation",&transformation) && !transformation &&
      UnrealReadFloatProperty(pawn,L"_TransformBlendTimeRemaining",&blend) && std::isfinite(blend) && blend <= 0;
}
bool CameraTable(void* manager, uintptr_t expected = 0) {
  void *table = nullptr, *start = nullptr;
  if (!CameraPointer(manager,0x10,&table) || !CameraMemory(table,0x68,g_camera_handoff.module) ||
      !CameraPointer(table,0x48,&start) || reinterpret_cast<uintptr_t>(start) !=
      CameraAddress(native_camera_code::kStartFormVa)) return false;
  for (const auto va : kCameraTables)
    if (reinterpret_cast<uintptr_t>(table) == CameraAddress(va)) return !expected || expected == va;
  return false;
}
bool BindCameraImage(HMODULE module, DWORD thread, CameraBindings* out) {
  if (!out || !module || !thread || thread != GetCurrentThreadId()) return false;
  const auto image = reinterpret_cast<uintptr_t>(module);
  if (!native_camera_code::Visit([&](uintptr_t va,const auto& code,std::initializer_list<size_t> operands) {
    const auto expected = native_reference_code::Relocate(code,image,operands);
    const auto* p = reinterpret_cast<const void*>(image+va-native_reference_code::kImageBase);
    return CameraMemory(p,expected.size(),module,true) && !std::memcmp(p,expected.data(),expected.size());
  })) return false;
  for (const auto table : kCameraTables) {
    void* start = nullptr;
    const auto* p = reinterpret_cast<const void*>(image+table-native_reference_code::kImageBase);
    if (!CameraMemory(p,0x68,module) || !CameraPointer(p,0x48,&start) ||
        reinterpret_cast<uintptr_t>(start) != image+native_camera_code::kStartFormVa-native_reference_code::kImageBase) return false;
  }
  *out = {module,thread}; return true;
}
bool CameraVehicle(void* pawn,void** form,uint8_t* type) {
  if (!CameraField(pawn,L"_VehicleForm",0x580,form)) return false;
  *type = 4;
  if (!*form) return true;
  if (!UnrealObjectIsA(*form,L"TnVehicleForm")) return false;
  void *table = nullptr, *getter = nullptr;
  if (!CameraPointer(*form,0x10,&table) || !CameraMemory(table,0xD0,g_camera_handoff.module) ||
      !CameraPointer(table,0xCC,&getter) || !CameraMemory(getter,6,g_camera_handoff.module,true)) return false;
  // Read only the CURRENT virtual's complete pure constant leaf. Do not
  // dispatch a guessed reflected getter, execute an arbitrary override or
  // infer vehicle type from the character's name. These are the compiler's
  // constant-byte return encodings; nonconstant/new bodies require review.
  const auto* b = static_cast<const uint8_t*>(getter);
  if ((b[0] == 0x33 || b[0] == 0x32) && b[1] == 0xC0 && b[2] == 0xC3) *type = 0;
  else if (b[0] == 0xB0 && b[2] == 0xC3) *type = b[1];
  else if (b[0] == 0xB8 && !b[2] && !b[3] && !b[4] && b[5] == 0xC3) *type = b[1];
  else return false;
  const wchar_t* expected = *type <= 1 ? L"TnCarForm" : *type == 2 ? L"TnPlaneForm" : L"TnTankForm";
  return *type <= 3 && UnrealObjectIsA(*form,expected);
}
bool CameraStrategy(void* set,const char* type_name,void** type,void** strategy) {
  UnrealStructArrayView entries; UnrealArrayView raw;
  if (!UnrealObjectIsA(set,L"HmCameraStrategySet") ||
      !UnrealReadStructArrayProperty(set,L"Strategies",&entries) || entries.element_size != 24 ||
      entries.count < 1 || entries.count > 64 || entries.capacity < entries.count || entries.capacity > 256 ||
      !CameraMemory(entries.data,static_cast<size_t>(entries.count)*24) || !CameraMemory(set,0x6C)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(set)+0x60,sizeof(raw));
  if (raw.data != entries.data || raw.count != entries.count || raw.capacity != entries.capacity) return false;
  *type = *strategy = nullptr;
  for (int32_t i = 0; i < entries.count; ++i) {
    void *t = nullptr, *s = nullptr, *native_type = nullptr, *native_strategy = nullptr;
    char name[80]{};
    const auto* entry = static_cast<const uint8_t*>(entries.data)+i*24;
    if (!UnrealReadStructArrayObjectField(entries,i,L"Type",&t) ||
        !UnrealReadStructArrayObjectField(entries,i,L"Strategy",&s) ||
        !CameraPointer(entry,0,&native_type) || t != native_type ||
        !CameraPointer(entry,16,&native_strategy) || s != native_strategy) return false;
    if (!t) continue;
    if (!UnrealObjectIsA(t,L"Class") || !UnrealObjectName(t,name,sizeof(name))) return false;
    if (std::strcmp(name,type_name)) continue;
    // No duplicate ambiguous Type entries. Native GetStrategy uses the FIRST
    // entry, even if its Strategy is None; don't silently choose a later one.
    if (*type || !UnrealObjectIsA(s,L"HmCameraStrategy")) return false;
    *type = t; *strategy = s;
  }
  return *type && *strategy;
}
bool CameraFormsMatch(void* manager,void* form,uint8_t type) {
  const wchar_t* names[] = {L"_TankForm",L"_CarForm",L"_PlaneForm"};
  const uint32_t offsets[] = {0x38,0x3C,0x40};
  for (size_t i = 0; i < 3; ++i) {
    void* actual = nullptr;
    void* expected = (i == 0 && type == 3) || (i == 1 && type <= 1) || (i == 2 && type == 2) ? form : nullptr;
    if (!CameraField(manager,names[i],offsets[i],&actual) || actual != expected) return false;
  }
  return true;
}
bool CaptureCamera(void* player,void* pawn,void* other,uint8_t mode,CameraCapture* c,bool reciprocal) {
  if (!c || !g_camera_handoff.module || GetCurrentThreadId() != g_camera_handoff.thread || mode > 1 || pawn == other ||
      !UnrealObjectReferenceWritesReady() || !CameraNamesReady() || !CameraLive(player,L"TnPlayerController") ||
      !CameraByte(player,L"Role",0x66,3)) return false;
  *c = {}; auto& o = c->objects; o[0] = player; o[1] = pawn; o[2] = other;
  void* value = nullptr; bool turret = true;
  if (!CameraField(player,L"WorldInfo",0x98,&o[3]) || !CameraLive(o[3],L"WorldInfo") ||
      !CameraByte(o[3],L"NetMode",0x39C,0) || !CameraCurrentWorld(o[3])) return false;
  for (auto* actor : {pawn,other}) {
    bool stasis = true, tear_off = true;
    if (!CameraLive(actor,L"TnPawn") || !CameraByte(actor,L"Role",0x66,3) ||
        !CameraField(actor,L"WorldInfo",0x98,&value) || value != o[3] ||
        !UnrealReadBoolProperty(actor,L"bStasis",&stasis) || stasis ||
        !UnrealReadBoolProperty(actor,L"bTearOff",&tear_off) || tear_off) return false;
  }
  if (reciprocal && (!CameraField(player,L"Pawn",0x1C8,&value) || value != pawn ||
      !CameraField(player,L"AcknowledgedPawn",0x300,&value) || value != pawn ||
      !CameraField(pawn,L"Controller",0x1D4,&value) || value != player || !CameraPawnMode(pawn,mode))) return false;
  if (!CameraField(player,L"_CameraStrategyManager",0x60C,&o[4]) || !UnrealObjectIsA(o[4],L"TnCameraStrategyManager") ||
      !CameraMemory(o[4],0x48,nullptr,false,true) || !CameraTable(o[4]) ||
      !UnrealReadBoolProperty(o[4],L"IsInTurret",&turret) || turret ||
      !CameraField(player,L"PlayerCamera",0x2D4,&o[5]) || !CameraLive(o[5],L"TnCamera") ||
      !CameraField(o[4],L"_Camera",0x30,&value) || value != o[5] ||
      !CameraField(o[5],L"WorldInfo",0x98,&value) || value != o[3] ||
      !CameraField(o[5],L"Owner",0x2C,&value) || (value != pawn && value != other) ||
      !CameraField(o[4],L"_TransformerPawn",0x34,&c->previous_owner) ||
      (c->previous_owner != pawn && c->previous_owner != other) ||
      !CameraField(o[4],L"CameraStrategySet",0x2C,&o[6]) ||
      !CameraField(o[4],L"_CameraStrategy",0x44,&value) ||
      !CameraField(o[5],L"ActiveCameraStrategy",0x3AC,&value) ||
      !CameraVehicle(pawn,&o[7],&c->type) || (mode == 1 && c->type == 4)) return false;
  // Validate each destination's metadata BEFORE InitializeForms clears caches.
  for (const auto* name : {L"_TankForm",L"_CarForm",L"_PlaneForm"}) {
    const uint32_t expected = name[1] == L'T' ? 0x38 : name[1] == L'C' ? 0x3C : 0x40;
    if (!CameraField(o[4],name,expected,&value)) return false;
  }
  const char* strategy = mode == 0 ? "TnOverTheShoulderCameraStrategyType" :
      c->type <= 1 ? "TnHoveringCameraStrategyType" : "TnDrivingCameraStrategyType";
  if (!CameraStrategy(o[6],strategy,&o[8],&o[9])) return false;
  for (size_t i = 0; i < o.size(); ++i) {
    c->indices[i] = UnrealObjectIndex(o[i]);
    if (o[i] && !c->indices[i]) return false;
  }
  return true;
}
bool CameraIdentities(const CameraCapture& c) {
  for (size_t i = 0; i < c.objects.size(); ++i)
    if (c.objects[i] && UnrealObjectIndex(c.objects[i]) != c.indices[i]) return false;
  return true;
}
bool CameraGuards(void* player,const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return move.controller == player && look.controller == player && move.axis == NativeInputAxis::kMove &&
      look.axis == NativeInputAxis::kLook && NativePlayerInputHeld(move) && NativePlayerInputHeld(look);
}
bool CameraFormIsCurrent(const CameraCapture& c,uint8_t mode) {
  const auto& o=c.objects;
  const uintptr_t expected_table = mode == 0 ? 0x128B6D10 : c.type <= 1 ? 0x128B6DE0 :
      c.type == 2 ? 0x128B6F18 : 0x128B6EB0;
  void* value=nullptr;
  return CameraIdentities(c) && c.previous_owner == o[1] && CameraFormsMatch(o[4],o[7],c.type) &&
      CameraTable(o[4],expected_table) &&
      CameraField(o[4],L"_CameraStrategy",0x44,&value) && value == o[8] &&
      CameraField(o[5],L"ActiveCameraStrategy",0x3AC,&value) && value == o[9] &&
      CameraField(o[5],L"Owner",0x2C,&value) && value == o[1];
}
}  // namespace

bool PrepareNativeCameraHandoff(HMODULE module,DWORD thread,void* player,void* first,void* second) {
  if (g_camera_handoff_busy || !module || module != GetModuleHandleW(nullptr) || !thread || thread != GetCurrentThreadId()) return false;
  if (!g_camera_handoff.module && !BindCameraImage(module,thread,&g_camera_handoff)) {
    Log(LogLevel::kError,"[SquadSwitch] native camera signature mismatch"); return false;
  }
  if (module != g_camera_handoff.module || thread != g_camera_handoff.thread) return false;
  // Both possible directions and both form strategies must be available before
  // detaching either controller. No camera call or template allocation here.
  CameraCapture capture;
  for (auto* pawn : {first,second}) {
    void* form = nullptr; uint8_t type = 4;
    if (!CameraVehicle(pawn,&form,&type)) return false;
    for (uint8_t mode = 0; mode <= (form ? 1 : 0); ++mode)
      if (!CaptureCamera(player,pawn,pawn == first ? second : first,mode,&capture,false)) return false;
  }
  return true;
}

static NativeCameraResult TransferCamera(void* player,void* pawn,void* other,uint8_t mode,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look,bool activate) {
  CameraCapture c;
  if (g_camera_handoff_busy || !CameraGuards(player,move,look) || !CaptureCamera(player,pawn,other,mode,&c,true))
    return NativeCameraResult::kRejected;
  g_camera_handoff_busy = true;
  struct ResetBusy { ~ResetBusy() { g_camera_handoff_busy = false; } } busy;
  const auto dirty = [&](const char* step) {
    Log(LogLevel::kError,"[SquadSwitch] native camera handoff dirty stage=%s",step);
    return NativeCameraResult::kStateMismatch;
  };
  auto& o = c.objects;
  // Deliberately NOT set_CameraOwner: it activates the previous form's camera
  // strategy on the new pawn before selecting the new form. Stage the GC-aware
  // owner and execute the real InitializeForms, THEN real StartForm. The swap
  // transaction defers StartForm to controller BeginState to avoid double
  // activation; its guarded postcondition checks the same native result. These
  // ordinary manager states have no cooked EndState overrides. StartForm
  // exits through None, ensuring BeginState runs even for same-form swaps.
  if (!UnrealWriteObjectPropertyExpected(o[4],L"_TransformerPawn",c.previous_owner,pawn))
    return NativeCameraResult::kRejected;
  const auto still_current = [&]() {
    CameraCapture observed;
    return CameraIdentities(c) && CameraGuards(player,move,look) &&
        CaptureCamera(player,pawn,other,mode,&observed,true) && observed.objects == o &&
        observed.indices == c.indices && observed.previous_owner == pawn && observed.type == c.type;
  };
  if (!still_current()) return dirty("owner");
  reinterpret_cast<InitializeFormsCall>(CameraAddress(native_camera_code::kInitializeFormsVa))(
      static_cast<uint8_t*>(o[4])+kNativeScriptThisOffset);
  if (!still_current() || !CameraFormsMatch(o[4],o[7],c.type)) return dirty("forms");
  if (!activate) return NativeCameraResult::kApplied;
  reinterpret_cast<StartFormCall>(CameraAddress(native_camera_code::kStartFormVa))(
      static_cast<uint8_t*>(o[4])+kNativeScriptThisOffset,nullptr,mode);
  CameraCapture after;
  if (!CameraIdentities(c) || !CameraGuards(player,move,look) ||
      !CaptureCamera(player,pawn,other,mode,&after,true) || after.objects != o || after.indices != c.indices ||
      !CameraFormIsCurrent(after,mode)) return dirty("start-form");
  return NativeCameraResult::kApplied;
}
NativeCameraResult NativeHandoffCamera(void* player,void* pawn,void* other,uint8_t mode,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return TransferCamera(player,pawn,other,mode,move,look,true);
}
NativeCameraResult NativeStageCameraForPlayerForm(void* player,void* pawn,void* other,uint8_t mode,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look) {
  return TransferCamera(player,pawn,other,mode,move,look,false);
}
bool NativeCameraFormIsCurrent(void* player,void* pawn,void* other,uint8_t mode) {
  CameraCapture capture;
  return CaptureCamera(player,pawn,other,mode,&capture,true) && CameraFormIsCurrent(capture,mode);
}
bool NativeReadPawnForm(void* pawn,uint8_t* mode,uint8_t* vehicle_type) {
  if (!mode || !vehicle_type || !g_camera_handoff.module ||
      GetCurrentThreadId()!=g_camera_handoff.thread || !CameraLive(pawn,L"TnPawn")) return false;
  void* vehicle=nullptr;
  if (!CameraVehicle(pawn,&vehicle,vehicle_type)) return false;
  if (CameraPawnMode(pawn,0)) { *mode=0; return true; }
  if (*vehicle_type<=3 && CameraPawnMode(pawn,1)) { *mode=1; return true; }
  return false;
}
#include "NativeViewTarget.inc"
}  // namespace wfcds
