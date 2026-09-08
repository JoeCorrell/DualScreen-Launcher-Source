#pragma once
#include "NativeWeaponProgress.h"
#include "UnrealBridge.h"
#include "NativeWeaponHeat.h"
namespace wfcds {
// Copies scalar state only. These are weak, identity-checked references, not GC
// roots. No array/object ownership is transferred to this short-lived journal.
struct NativeHeatModifierState {
  void* object=nullptr;
  uint32_t index=0;
  void* owner=nullptr;
  uint32_t owner_index=0;
  uint8_t kind=0;
  std::array<float,2> tuning{};
  uint8_t event=0;
  std::array<bool,2> flags{};
  float timer=0;
  uint32_t timer_offset=0;
};
struct NativeWeaponModifiers {
  NativeWeaponProgress context{};
  void* heat=nullptr;
  uint32_t heat_index=0;
  float heat_maximum=0;
  UnrealArrayView array{};
  std::array<NativeHeatModifierState,64> modifiers{};
  bool captured=false;
};
// Run at the individual weapon's mutation boundary after AI/fire quiescence.
bool NativeCaptureWeaponModifiers(DWORD thread,void* pawn,void* weapon,NativeWeaponModifiers* output);
// Read-only compatibility check against an authored tuning record. Does not
// compare transient timers/default flags and never clones/replaces live state.
bool NativeWeaponModifiersMatchData(const NativeWeaponModifiers& live,void* data);
inline bool SameWeaponModifierIdentity(const NativeWeaponModifiers& a,const NativeWeaponModifiers& b) {
  if (!a.captured || !b.captured || !a.context.captured || !b.context.captured ||
      a.heat!=b.heat || a.heat_index!=b.heat_index || a.heat_maximum!=b.heat_maximum || a.array.count<0 || a.array.count>64 ||
      a.array.data!=b.array.data || a.array.count!=b.array.count || a.array.capacity!=b.array.capacity) return false;
  // Data/controller may change during retuning/rollback, not these actors.
  for (size_t i=0;i<4;++i)
    if (a.context.objects[i]!=b.context.objects[i] || a.context.indices[i]!=b.context.indices[i]) return false;
  for (int32_t i=0;i<a.array.count;++i) {
    const auto& x=a.modifiers[i];const auto& y=b.modifiers[i];
    if (x.object!=y.object || x.index!=y.index || x.owner!=y.owner || x.owner_index!=y.owner_index ||
        x.kind!=y.kind || x.tuning!=y.tuning || x.event!=y.event || x.timer_offset!=y.timer_offset) return false;
  }
  return true;
}
inline bool SameWeaponModifierRuntime(const NativeWeaponModifiers& a,const NativeWeaponModifiers& b) {
  if(!SameWeaponModifierIdentity(a,b))return false;
  for(int32_t i=0;i<a.array.count;++i)
    if(a.modifiers[i].flags!=b.modifiers[i].flags || a.modifiers[i].timer!=b.modifiers[i].timer)return false;
  return true;
}
// Restore only the exact, still-owned live modifier objects. A cloned, removed,
// retuned or reordered set is incompatible, not something to guess/copy into.
// Caller must arm its rollback journal before this potentially dirty operation.
NativePlayerServiceResult NativeRestoreWeaponModifiers(const NativeWeaponModifiers& saved);
// Initial AI->player bootstrap only; does not change Data/current heat/state
// or invoke UpdateData. An empty cold AI service can receive PlayerData HeatMax.
// Live modifiers are never replaced/reset. PrepareNativeWeaponHeat must pass
// first; Begin/Finish must run in one game-thread pulse, before control transfer.
// A successful begin keeps new clones rooted and journaled until Finish.
// Every successful Begin, including an already-initialized no-op, requires
// exactly one successful Finish. A failed dirty Finish retains recovery
// ownership; neither Begin nor a second successful Finish may reuse it.
struct NativeWeaponModifierInitialization {
  NativeWeaponModifiers before{};
  NativeWeaponModifiers initialized{};
  UnrealObjectCloneArray clones{};
  void* player_data=nullptr;
  uint32_t player_data_index=0;
  // Includes capacity-only initialization: the nested allocation journal
  // alone cannot protect a transaction that never allocates any modifiers.
  const NativeWeaponModifierInitialization* journal_address=nullptr;
  bool capacity_seeded=false;
};
NativePlayerServiceResult NativeBeginWeaponModifierInitialization(DWORD thread,void* pawn,
    void* weapon,void* player_data,NativeWeaponModifierInitialization* journal);
NativePlayerServiceResult NativeFinishWeaponModifierInitialization(
    NativeWeaponModifierInitialization* journal,bool commit);
}
