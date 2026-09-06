#include "NativeWeaponModifiers.h"
#include <cmath>
#include <cstring>
namespace wfcds {
namespace {
thread_local bool g_modifiers_busy=false;
struct ModifierSchema {
  const char* type;
  std::array<const wchar_t*,2> tuning;
  std::array<const wchar_t*,2> flags;
  const wchar_t* timer;
};
constexpr std::array<ModifierSchema,5> kModifierSchemas={{
  {"TnChargeHeatModifier",{L"GainRate",nullptr},{L"IsCharging",L"IsOverheated"},nullptr},
  {"TnHoldHeatModifier",{L"GainRate",nullptr},{L"IsHoldingFire",L"IsOverheated"},nullptr},
  {"TnLoseHeatModifier",{L"LossRate",L"LossDelay"},{L"IsHoldingFire",L"IsOverheated"},L"TimeSinceLastShot"},
  {"TnOverheatHeatModifier",{L"OverheatDelay",nullptr},{nullptr,nullptr},L"TimeLeftOnOverheat"},
  {"TnPerShotHeatModifier",{L"AddPerShot",nullptr},{nullptr,nullptr},nullptr}
}};
bool ModifierMemory(const void* p,size_t n,bool write=false) {
  MEMORY_BASIC_INFORMATION m{};
  if (!p || !VirtualQuery(p,&m,sizeof(m)) || m.State!=MEM_COMMIT || (m.Protect&(PAGE_GUARD|PAGE_NOACCESS)) ||
      n>m.RegionSize-(reinterpret_cast<uintptr_t>(p)-reinterpret_cast<uintptr_t>(m.BaseAddress))) return false;
  const DWORD protection=m.Protect&0xFF;
  return !write || protection==PAGE_READWRITE || protection==PAGE_WRITECOPY ||
      protection==PAGE_EXECUTE_READWRITE || protection==PAGE_EXECUTE_WRITECOPY;
}
bool ModifierFloat(void* object,const wchar_t* name,float* out,uint32_t* offset=nullptr,bool require_write=true) {
  uint32_t off=0;float raw=0;
  if (!UnrealResolveFloatPropertyOffset(object,name,&off) || off<0x28 || off>=0x1000 || (off&3) ||
      !ModifierMemory(static_cast<uint8_t*>(object)+off,4,offset!=nullptr && require_write) ||
      !UnrealReadFloatProperty(object,name,out) || !std::isfinite(*out)) return false;
  std::memcpy(&raw,static_cast<uint8_t*>(object)+off,4);
  if (offset) *offset=off;
  return raw==*out;
}
bool CaptureModifier(void* object,NativeHeatModifierState* out,bool require_write=true) {
  *out={};auto& m=*out;char type[48]{};m.object=object;
  if (!UnrealObjectIsA(object,L"TnHeatModifier") || !(m.index=UnrealObjectIndex(object)) ||
      !UnrealReadObjectProperty(object,L"WeaponOwner",&m.owner) ||
      (m.owner && (!UnrealObjectValid(m.owner) || !(m.owner_index=UnrealObjectIndex(m.owner)))) ||
      !UnrealObjectClassName(object,type,sizeof(type))) return false;
  size_t kind=0;
  while (kind<kModifierSchemas.size() && std::strcmp(type,kModifierSchemas[kind].type)) ++kind;
  if (kind==kModifierSchemas.size()) return false;
  m.kind=static_cast<uint8_t>(kind);const auto& schema=kModifierSchemas[kind];
  for (size_t j=0;j<2;++j) {
    if (schema.tuning[j] && (!ModifierFloat(object,schema.tuning[j],&m.tuning[j]) || m.tuning[j]<0)) return false;
    if (schema.flags[j] && !UnrealReadBoolProperty(object,schema.flags[j],&m.flags[j])) return false;
  }
  if (kind==4 && (!UnrealReadByteProperty(object,L"EventTypeToAddOn",&m.event) || m.event>8)) return false;
  if (schema.timer && (!ModifierFloat(object,schema.timer,&m.timer,&m.timer_offset,require_write) || (kind==2 && m.timer<0))) return false;
  return true;
}
bool CaptureModifiers(DWORD thread,void* pawn,void* weapon,NativeWeaponModifiers* out) {
  if (!out) return false;
  *out={};
  if (!NativeCaptureWeaponProgress(thread,pawn,weapon,&out->context) ||
      !UnrealReadObjectProperty(weapon,L"WeaponHeat",&out->heat) ||
      !UnrealObjectIsA(out->heat,L"TnWeaponHeat") || !(out->heat_index=UnrealObjectIndex(out->heat))) return false;
  char type[48]{};void* heat_owner=nullptr;UnrealStructValueView properties{};
  if (!UnrealObjectClassName(out->heat,type,sizeof(type)) || std::strcmp(type,"TnWeaponHeat") ||
      !UnrealReadObjectProperty(out->heat,L"OwnerPawn",&heat_owner) || (heat_owner && heat_owner!=pawn) ||
      !UnrealReadStructProperty(out->heat,L"HeatProperties",&properties) ||
      !UnrealReadStructFloatProperty(out->heat,L"HeatProperties",L"HeatMax",&out->heat_maximum) ||
      !std::isfinite(out->heat_maximum) || out->heat_maximum<0 ||
      !UnrealReadStructTypedArray(properties,L"Modifiers",L"ObjectProperty",4,&out->array,64) ||
      out->array.count<0 || out->array.count>64 || out->array.capacity<out->array.count || out->array.capacity>64 ||
      (out->array.count && !ModifierMemory(out->array.data,out->array.count*4))) return false;
  for (int32_t i=0;i<out->array.count;++i) {
    auto& m=out->modifiers[i];
    void* object=nullptr;
    if (!UnrealReadObjectArrayElement(out->array,i,&object) || !CaptureModifier(object,&m) ||
        UnrealObjectOuter(m.object)!=out->heat || (m.owner && m.owner!=pawn)) return false;
    // SetHeatProperties clones each entry with the heat service as Outer.
    // Duplicate pointers or templates must not receive transient-state writes.
    for (int32_t j=0;j<i;++j) if (out->modifiers[j].object==m.object) return false;
    // Overheat legitimately overshoots below zero in OnHeatEvent(4); do not
    // clamp its stored timer or reject this source-authored terminal state.
  }
  out->captured=true;return true;
}
bool ValidSavedModifiers(const NativeWeaponModifiers& s) {
  if (!s.captured || !s.context.captured || s.array.count<0 || s.array.count>64 ||
      !std::isfinite(s.heat_maximum) || s.heat_maximum<0) return false;
  for (int32_t i=0;i<s.array.count;++i) {
    const auto& m=s.modifiers[i];
    if (m.kind>=kModifierSchemas.size() || !std::isfinite(m.timer) || (m.kind==2 && m.timer<0)) return false;
    for (float f:m.tuning) if (!std::isfinite(f) || f<0) return false;
  }
  return true;
}
}
bool NativeCaptureWeaponModifiers(DWORD thread,void* pawn,void* weapon,NativeWeaponModifiers* out) {
  if (!out) return false;
  *out={};return !g_modifiers_busy && CaptureModifiers(thread,pawn,weapon,out);
}
NativePlayerServiceResult NativeRestoreWeaponModifiers(const NativeWeaponModifiers& saved) {
  NativeWeaponModifiers current;
  if (g_modifiers_busy || !ValidSavedModifiers(saved) ||
      !CaptureModifiers(saved.context.thread,saved.context.objects[0],saved.context.objects[1],&current) ||
      !SameWeaponModifierIdentity(saved,current)) return NativePlayerServiceResult::kRejected;
  g_modifiers_busy=true;struct Clear { ~Clear(){g_modifiers_busy=false;} } clear;
  bool changed=false;
  for (int32_t i=0;i<current.array.count;++i) {
    const auto& m=current.modifiers[i];const auto& desired=saved.modifiers[i];const auto& schema=kModifierSchemas[m.kind];
    for (size_t j=0;j<2;++j) if (schema.flags[j] && m.flags[j]!=desired.flags[j]) {
      if (!UnrealWriteBoolPropertyExpected(m.object,schema.flags[j],m.flags[j],desired.flags[j]))
        return changed ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected;
      changed=true;
    }
    if (schema.timer && m.timer!=desired.timer) {
      if (!ModifierMemory(static_cast<uint8_t*>(m.object)+m.timer_offset,4,true))
        return changed ? NativePlayerServiceResult::kStateMismatch:NativePlayerServiceResult::kRejected;
      std::memcpy(static_cast<uint8_t*>(m.object)+m.timer_offset,&desired.timer,4);changed=true;
    }
  }
  NativeWeaponModifiers after;
  if (!CaptureModifiers(saved.context.thread,saved.context.objects[0],saved.context.objects[1],&after) ||
      !SameWeaponModifierIdentity(current,after) || !SameWeaponProgressIdentity(current.context,after.context) ||
      current.context.objects!=after.context.objects || current.context.indices!=after.context.indices)
    return NativePlayerServiceResult::kStateMismatch;
  for (int32_t i=0;i<after.array.count;++i)
    if (after.modifiers[i].flags!=saved.modifiers[i].flags || after.modifiers[i].timer!=saved.modifiers[i].timer)
      return NativePlayerServiceResult::kStateMismatch;
  return NativePlayerServiceResult::kApplied;
}
#include "NativeWeaponModifierInitialization.inc"
}
