#pragma once
#include "NativeWeaponProgress.h"
namespace wfcds {
// Weak journal for existing services. No array allocation, clone ownership or
// initialization is hidden in the control-transfer transaction.
struct NativeWeaponRetention {
  HMODULE image=nullptr;
  NativeWeaponProgress context{};
  void* target=nullptr;
  uint32_t target_index=0;
  // Existing animator, mesh template, dropped/factory meshes, heat service,
  // and the two role-data animation templates.
  std::array<void*,7> services{};
  std::array<uint32_t,7> indices{};
  float heat_maximum=0;
  uint8_t cache_kind=0;
  bool needs_animator=false;
  bool captured=false;
};
bool NativeCaptureWeaponRetention(HMODULE image,DWORD thread,void* pawn,void* weapon,
    void* target,uint8_t cache_kind,NativeWeaponRetention* out,const char** reason=nullptr);
inline bool SameWeaponRetentionIdentity(const NativeWeaponRetention& a,const NativeWeaponRetention& b) {
  return a.captured && b.captured && a.image==b.image && SameWeaponProgressIdentity(a.context,b.context) &&
      a.target==b.target && a.target_index==b.target_index && a.services==b.services && a.indices==b.indices &&
      a.heat_maximum==b.heat_maximum && a.cache_kind==b.cache_kind && a.needs_animator==b.needs_animator;
}
// Accepts only this journal's old/new Data pair. Controller changes BETWEEN
// stages are permitted; changes DURING a check are rejected.
bool NativeWeaponRetentionStable(const NativeWeaponRetention& saved);
}
