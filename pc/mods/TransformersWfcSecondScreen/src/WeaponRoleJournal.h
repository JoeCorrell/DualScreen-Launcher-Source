#pragma once
#include <cstdint>
#include "NativeWeaponProgress.h"
#include "NativeWeaponModifiers.h"
#include "NativeWeaponDataCache.h"
#include "NativeWeaponRoleData.h"
#include "NativeWeaponAmmo.h"
namespace wfcds {
struct InventoryWeaponRole {
  void* weapon=nullptr;
  uint32_t object_index=0;
  uint8_t previous_data_type=0;
  void* previous_data=nullptr;
  void* desired_data=nullptr;
  void* weapon_heat=nullptr;
  uint32_t weapon_heat_index=0;
  float current_heat=0;
  bool heat_state_captured=false;
  bool changed=false;
  NativeWeaponProgress progress{};
  NativeWeaponModifiers modifiers{};
  NativeWeaponDataCache data_cache{};
  NativeWeaponRoleData role_data{};
  NativeWeaponAmmo ammo{};
};
// Identity/topology comparison only. Both snapshots must first pass the live
// inventory reader. Heat can evolve during the intervening native AI ticks;
// ApplyInventoryWeaponRoles captures it afresh immediately before mutation.
inline bool SameInventoryWeaponRoleIdentity(const InventoryWeaponRole& a,const InventoryWeaponRole& b) {
  return a.weapon==b.weapon && a.object_index==b.object_index &&
      a.previous_data_type==b.previous_data_type && a.previous_data==b.previous_data &&
      a.desired_data==b.desired_data && a.heat_state_captured==b.heat_state_captured &&
      a.weapon_heat==b.weapon_heat && a.weapon_heat_index==b.weapon_heat_index &&
      a.progress.captured==b.progress.captured &&
      a.role_data.captured==b.role_data.captured &&
      (!a.role_data.captured || SameWeaponRoleDataIdentity(a.role_data,b.role_data)) &&
      a.ammo.captured==b.ammo.captured &&
      (!a.ammo.captured || SameWeaponAmmoIdentity(a.ammo,b.ammo)) &&
      a.data_cache.captured==b.data_cache.captured &&
      (!a.data_cache.captured || SameWeaponDataCacheIdentity(a.data_cache,b.data_cache)) &&
      a.modifiers.captured==b.modifiers.captured &&
      (!a.modifiers.captured || SameWeaponModifierIdentity(a.modifiers,b.modifiers)) &&
      (!a.progress.captured || SameWeaponProgressIdentity(a.progress,b.progress));
}
}
