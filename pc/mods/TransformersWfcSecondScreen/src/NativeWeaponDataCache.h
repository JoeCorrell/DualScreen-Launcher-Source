#pragma once
#include "NativeWeaponProgress.h"
namespace wfcds {
struct NativeWeaponDataCache {
  NativeWeaponProgress context{};
  void* target_data=nullptr;
  uint32_t target_index=0;
  uint32_t cache_offset=0;
  void* grenade_thrower=nullptr;
  uint32_t thrower_index=0;
  uint32_t thrower_cache_offset=0;
  uint8_t kind=0; // 0: base; 1: beam; 2: grenade launcher; 3: grenade bag; 4: repair.
  bool captured=false;
};
bool NativeCaptureWeaponDataCache(DWORD thread,void* pawn,void* weapon,void* target_data,NativeWeaponDataCache* out);
inline bool SameWeaponDataCacheIdentity(const NativeWeaponDataCache& a,const NativeWeaponDataCache& b) {
  return a.captured && b.captured && SameWeaponProgressIdentity(a.context,b.context) &&
      a.target_data==b.target_data && a.target_index==b.target_index &&
      a.cache_offset==b.cache_offset && a.kind==b.kind &&
      a.grenade_thrower==b.grenade_thrower && a.thrower_index==b.thrower_index &&
      a.thrower_cache_offset==b.thrower_cache_offset;
}
// Source port of the derived UpdateData reference assignments only. Data must
// already be the journal's forward/rollback target. No base UpdateData, spawning,
// inventory, ammo, heat, animation or pickup initialization is performed here.
NativePlayerServiceResult NativeRetargetWeaponDataCache(const NativeWeaponDataCache& saved,bool rollback);
}
