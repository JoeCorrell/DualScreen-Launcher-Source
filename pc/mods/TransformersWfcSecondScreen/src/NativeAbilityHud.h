#pragma once
#include <windows.h>
#include "AbilityHudPlan.h"
#include "NativePlayerInput.h"
namespace wfcds {
enum class NativeAbilityHudResult { kRejected, kDispatched, kStateMismatch };
// Low-level native HUD transport only. This does not rebind AbilityManager or
// collect effective ability values; those are separate integration requirements.
bool PrepareNativeAbilityHud(HMODULE executable,DWORD game_thread,void* player,void* pawn,void* hud);
// Optional synchronous owner-state guard, evaluated before the first native
// callback and after each callback. Context must live for the entire dispatch.
struct NativeAbilityHudValidation {
  bool (*validate)(void*)=nullptr;
  void* context=nullptr;
};
NativeAbilityHudResult NativeDispatchAbilityHud(void* player,void* pawn,void* hud,
    const AbilityHudPlan& plan,const NativePlayerInputLease& move,const NativePlayerInputLease& look,
    const NativeAbilityHudValidation& validation={});
} // namespace wfcds
