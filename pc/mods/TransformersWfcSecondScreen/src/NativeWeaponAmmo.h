#pragma once
#include "NativeWeaponProgress.h"
#include "UnrealBridge.h"
namespace wfcds {
struct NativeWeaponAmmo {
  NativeWeaponProgress context{};
  void* target=nullptr;
  uint32_t target_index=0;
  void* skill=nullptr;
  uint32_t skill_index=0;
  UnrealArrayView skills{};
  std::array<void*,64> skill_objects{};
  std::array<uint32_t,64> skill_indices{};
  int32_t increase=0,desired_increase=0,ammo=0,clip=0,target_maximum=0,target_clip=0;
  bool affected=false,captured=false;
};
inline bool SameWeaponAmmoConfiguration(const NativeWeaponAmmo& a,const NativeWeaponAmmo& b) {
  return a.target==b.target && a.target_index==b.target_index && a.skill==b.skill && a.skill_index==b.skill_index &&
      a.affected==b.affected && a.skills.data==b.skills.data && a.skills.count==b.skills.count && a.skills.capacity==b.skills.capacity &&
      a.skill_objects==b.skill_objects && a.skill_indices==b.skill_indices &&
      a.target_maximum==b.target_maximum && a.target_clip==b.target_clip;
}
inline bool SameWeaponAmmoIdentity(const NativeWeaponAmmo& a,const NativeWeaponAmmo& b) {
  return a.captured && b.captured && SameWeaponProgressIdentity(a.context,b.context) && SameWeaponAmmoConfiguration(a,b);
}
bool NativeCaptureWeaponAmmo(DWORD thread,void* pawn,void* weapon,void* target,NativeWeaponAmmo* output);
// Ports InitializeFromInstigator's skill-derived capacity assignment only.
// Never fills/clamps ammo, changes the magazine, reloads, or rebuilds inventory.
NativePlayerServiceResult NativeRetargetWeaponAmmo(const NativeWeaponAmmo& saved,bool rollback);
}
