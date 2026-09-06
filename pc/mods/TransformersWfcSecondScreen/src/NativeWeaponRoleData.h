#pragma once
#include "NativeWeaponProgress.h"
namespace wfcds {
struct NativeWeaponRoleData {
  NativeWeaponProgress context{};
  void* target=nullptr;
  uint32_t target_index=0;
  uint8_t previous_role=0;
  uint8_t desired_role=0;
  bool captured=false;
};
inline bool SameWeaponRoleDataIdentity(const NativeWeaponRoleData& a,const NativeWeaponRoleData& b) {
  return a.captured && b.captured && SameWeaponProgressIdentity(a.context,b.context) &&
      a.target==b.target && a.target_index==b.target_index &&
      a.previous_role==b.previous_role && a.desired_role==b.desired_role;
}
// Assignment leaf of the custom role update, NOT a complete SetWeaponDataType
// replacement. The caller still owns derived caches, heat/animator/skill setup
// and preserved runtime values. No UpdateData, possession or respawn is called.
bool NativeCaptureWeaponRoleData(HMODULE image,DWORD thread,void* pawn,void* weapon,
    uint8_t role,NativeWeaponRoleData* output);
// Arm the enclosing rollback journal before entry. Dirty failures can leave a
// mixed type/data pair; rollback accepts only this journal's two known values.
NativePlayerServiceResult NativeAssignWeaponRoleData(const NativeWeaponRoleData& saved,bool rollback);
}
