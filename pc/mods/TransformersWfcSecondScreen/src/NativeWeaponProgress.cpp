#include "NativeWeaponProgress.h"
#include "UnrealBridge.h"
#include <cmath>
#include <cstring>
namespace wfcds {
namespace {
thread_local bool g_weapon_progress_busy=false;
constexpr std::array<const wchar_t*,4> kProgressFields={L"StartValue",L"EndValue",L"RampTime",L"CurTime"};
bool ProgressMemory(const void* p,size_t n,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const auto prot=m.Protect&0xFF;
  return !write || prot==PAGE_READWRITE || prot==PAGE_WRITECOPY || prot==PAGE_EXECUTE_READWRITE || prot==PAGE_EXECUTE_WRITECOPY;
}
bool ProgressLive(void* p,const wchar_t* type) {
  bool dead=true;
  return UnrealObjectIsA(p,type) && UnrealObjectIndex(p) && UnrealReadBoolProperty(p,L"bDeleteMe",&dead) && !dead;
}
bool ProgressObject(void* object,const wchar_t* field,uint32_t off,void** output) {
  uint32_t reflected=0;void* raw=nullptr;
  if (!UnrealResolveObjectPropertyOffset(object,field,&reflected) || reflected!=off ||
      !ProgressMemory(static_cast<uint8_t*>(object)+off,4) || !UnrealReadObjectProperty(object,field,output)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+off,4);return raw==*output;
}
bool ProgressFloat(void* object,const wchar_t* field,uint32_t off,float* output) {
  uint32_t reflected=0;float raw=0;
  if (!UnrealResolveFloatPropertyOffset(object,field,&reflected) || reflected!=off ||
      !ProgressMemory(static_cast<uint8_t*>(object)+off,4,true) || !UnrealReadFloatProperty(object,field,output) ||
      !std::isfinite(*output)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+off,4);return raw==*output;
}
bool ValidProgressValues(const NativeWeaponProgress& p) {
  if (!std::isfinite(p.spread) || p.spread<0) return false;
  for (float f:p.interval) if (!std::isfinite(f) || f<0) return false;
  return p.interval[3]<=p.interval[2];
}
bool CaptureProgress(DWORD thread,void* pawn,void* weapon,NativeWeaponProgress* out) {
  if (!out || !thread || thread!=GetCurrentThreadId()) return false;
  *out={};auto& o=out->objects;o[0]=pawn;o[1]=weapon;out->thread=thread;
  void *value=nullptr;uint8_t role=0,mode=255;bool firing=true;
  if (!ProgressLive(pawn,L"TnPawn") || !ProgressLive(weapon,L"TnWeapon") ||
      !UnrealReadObjectProperty(weapon,L"Owner",&value) || value!=pawn ||
      !UnrealReadObjectProperty(weapon,L"Instigator",&o[6]) || (o[6] && o[6]!=pawn) ||
      !UnrealReadObjectProperty(pawn,L"Controller",&o[5]) ||
      !UnrealReadObjectProperty(o[5],L"Pawn",&value) || value!=pawn ||
      !UnrealReadObjectProperty(pawn,L"WorldInfo",&o[3]) || !ProgressLive(o[3],L"WorldInfo") ||
      !UnrealReadByteProperty(o[3],L"NetMode",&mode) || mode!=0) return false;
  void* controller=o[5];const bool player=ProgressLive(controller,L"TnPlayerController");
  if (player ? (!UnrealReadObjectProperty(controller,L"AcknowledgedPawn",&value) || value!=pawn):!ProgressLive(controller,L"TnAiController")) return false;
  for (void* actor:{pawn,weapon,controller})
    if (!UnrealReadByteProperty(actor,L"Role",&role) || role!=3 ||
        !UnrealReadObjectProperty(actor,L"WorldInfo",&value) || value!=o[3]) return false;
  char type[48]{};
  // Raw offsets are independently present in retained InitializeFireInterval
  // 109E72C0..109E736C and UpdateData 109E6520..109E6701. Check live metadata too.
  if (!ProgressObject(weapon,L"FireIntervalLerper",0x2EC,&o[2]) ||
      !UnrealObjectClassName(o[2],type,sizeof(type)) || std::strcmp(type,"HmValueLerper") ||
      !ProgressObject(weapon,L"Data",0x240,&o[4]) || !UnrealObjectIsA(o[4],L"TnWeaponData") ||
      !UnrealReadBoolProperty(weapon,L"bIsFiring",&firing) || firing ||
      !ProgressFloat(weapon,L"CurrentSpread",0x300,&out->spread)) return false;
  for (size_t i=0;i<4;++i)
    if (!ProgressFloat(o[2],kProgressFields[i],0x28+static_cast<uint32_t>(i)*4,&out->interval[i])) return false;
  for (size_t i=0;i<o.size();++i) { out->indices[i]=UnrealObjectIndex(o[i]);if (!out->indices[i] && !(i==6 && !o[i])) return false; }
  out->captured=ValidProgressValues(*out);return out->captured;
}
bool SameProgressActors(const NativeWeaponProgress& a,const NativeWeaponProgress& b) {
  // Data intentionally differs after retuning; the actor, lerper and world must not.
  for (size_t i=0;i<4;++i) if (a.objects[i]!=b.objects[i] || a.indices[i]!=b.indices[i]) return false;
  return true;
}
bool ReadIntervalTuning(void* data,std::array<float,3>* values) {
  UnrealStructValueView tuning{},range{};uint32_t offset=0;
  // InitializeFireInterval reads raw Data+11C/+120/+124. Resolve the nested
  // structures too: a guessed raw float or a different field layout is not
  // a valid substitute for the role's authored interval range and duration.
  if (!UnrealObjectIsA(data,L"TnWeaponData") ||
      !UnrealResolveStructPropertyOffset(data,L"FireIntervalModifier",12,&offset) || offset!=0x11C ||
      !UnrealReadStructProperty(data,L"FireIntervalModifier",&tuning) || tuning.size!=12 ||
      tuning.data!=static_cast<uint8_t*>(data)+0x11C ||
      !UnrealReadStructStructField(tuning,L"IntervalRange",&range) || range.size!=8 || range.data!=tuning.data ||
      !UnrealReadStructFloatField(range,L"Min",&(*values)[0]) ||
      !UnrealReadStructFloatField(range,L"Max",&(*values)[1]) ||
      !UnrealReadStructFloatField(tuning,L"RampTime",&(*values)[2]) ||
      !ProgressMemory(tuning.data,12)) return false;
  std::array<float,3> raw{};std::memcpy(raw.data(),tuning.data,12);
  for (float f:*values) if (!std::isfinite(f) || f<0) return false;
  return raw==*values;
}
}
bool NativeCaptureWeaponProgress(DWORD thread,void* pawn,void* weapon,NativeWeaponProgress* out) {
  if (!out) return false;
  *out={};
  return !g_weapon_progress_busy && CaptureProgress(thread,pawn,weapon,out);
}
NativePlayerServiceResult NativeRestoreWeaponProgress(const NativeWeaponProgress& saved,void* expected_data,bool rollback) {
  NativeWeaponProgress current;
  if (g_weapon_progress_busy || !saved.captured || !ValidProgressValues(saved) ||
      !CaptureProgress(saved.thread,saved.objects[0],saved.objects[1],&current) || !SameProgressActors(saved,current) ||
      current.objects[4]!=expected_data || !UnrealObjectIsA(saved.objects[4],L"TnWeaponData") ||
      UnrealObjectIndex(saved.objects[4])!=saved.indices[4] || (rollback && expected_data!=saved.objects[4])) return NativePlayerServiceResult::kRejected;
  auto desired=current.interval;
  std::array<float,3> tuning{};
  if (rollback) desired=saved.interval;
  else {
    if (!ReadIntervalTuning(current.objects[4],&tuning)) return NativePlayerServiceResult::kRejected;
    for (size_t i=0;i<tuning.size();++i) desired[i]=tuning[i];
    // HmValueLerper.SetRampTime preserves CurTime/RampTime. Initialize zeros
    // CurTime before that setter, so the normal role update loses this fraction.
    // Zero-duration intervals have no ramp progress to transfer. Read the
    // target Data directly instead of assuming a destructive UpdateData has
    // already initialized this lerper. The same-object custom role path does
    // not rebuild a weapon just to obtain these three scalar values.
    const float fraction=saved.interval[2]==0 ? 0:saved.interval[3]/saved.interval[2];
    desired[3]=desired[2]*fraction;
    if (!std::isfinite(desired[3]) || desired[3]<0 || desired[3]>desired[2]) return NativePlayerServiceResult::kRejected;
  }
  // Only plain transient float fields change. No reflected/native callbacks,
  // allocations, GC references, Actor timer APIs, ownership or inventory operations run.
  // The enclosing role journal must be armed before either destination is written.
  if (!ProgressMemory(static_cast<uint8_t*>(current.objects[1])+0x300,4,true) ||
      !ProgressMemory(static_cast<uint8_t*>(current.objects[2])+0x28,16,true)) return NativePlayerServiceResult::kRejected;
  g_weapon_progress_busy=true;struct ClearBusy { ~ClearBusy(){g_weapon_progress_busy=false;} } busy;
  std::memcpy(static_cast<uint8_t*>(current.objects[1])+0x300,&saved.spread,4);
  std::memcpy(static_cast<uint8_t*>(current.objects[2])+0x28,desired.data(),16);
  NativeWeaponProgress after;
  std::array<float,3> after_tuning{};
  if (!CaptureProgress(saved.thread,saved.objects[0],saved.objects[1],&after) || !SameProgressActors(current,after) ||
      after.objects[4]!=current.objects[4] || after.indices[4]!=current.indices[4] ||
      after.objects[5]!=current.objects[5] || after.indices[5]!=current.indices[5] ||
      after.objects[6]!=current.objects[6] || after.indices[6]!=current.indices[6] ||
      after.spread!=saved.spread || after.interval!=desired ||
      (!rollback && (!ReadIntervalTuning(after.objects[4],&after_tuning) || after_tuning!=tuning)))
    return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
}
