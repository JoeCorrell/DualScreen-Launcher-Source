#pragma once
#include "NativePlayerServices.h"
#include <array>
namespace wfcds {
struct NativeWeaponProgress {
  // Pawn, weapon, existing lerper, world, tuning data, controller, Instigator.
  std::array<void*,7> objects{};
  std::array<uint32_t,7> indices{};
  // HmValueLerper: StartValue, EndValue, RampTime, CurTime.
  std::array<float,4> interval{};
  float spread=0;
  DWORD thread=0;
  bool captured=false;
};
// Compare persistent identity, not timing or controller role, across quiescence.
inline bool SameWeaponProgressIdentity(const NativeWeaponProgress& a,const NativeWeaponProgress& b) {
  if (!a.captured || !b.captured) return false;
  for (size_t i=0;i<5;++i) if (a.objects[i]!=b.objects[i] || a.indices[i]!=b.indices[i]) return false;
  return true;
}
bool NativeCaptureWeaponProgress(DWORD thread,void* pawn,void* weapon,NativeWeaponProgress* output);
// After the role Data assignment: apply its authored interval bounds/duration
// directly while retaining the old normalized ramp progress and spread. Does
// not depend on UpdateData/InitializeFireInterval having reset the lerper.
// Rollback restores the exact old values. Never replaces/initializes a weapon.
NativePlayerServiceResult NativeRestoreWeaponProgress(const NativeWeaponProgress& saved,
    void* expected_current_data,bool rollback);
}
