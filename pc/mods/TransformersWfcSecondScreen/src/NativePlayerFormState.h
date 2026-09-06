#pragma once
#include <windows.h>
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativePlayerFormResult { kRejected, kApplied, kStateMismatch };
// Guarded controller-state exit and custom cross-role entry. Ongoing player/AI
// role consumers are separate: the complete role-adapter gate is still required.
bool PrepareNativePlayerFormState(HMODULE executable,DWORD game_thread,void* player);
// Exit while the SAME pawn is still acknowledged and reciprocally controlled.
// No object, pawn, camera, inventory or transformation is replaced here.
NativePlayerFormResult NativeLeavePlayerForm(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
bool NativePlayerFormIsNeutral(void* player,void* pawn);
// Read-only postcondition: current gameplay state AND cached vehicle alias
// belong to this pawn. Same state name with the old pawn's cache is not success.
bool NativePlayerFormIsCurrent(void* player,void* pawn);
// State-specific input cleanup on the current pair, before any ownership move.
// Walking still requires a physical player pawn because the stock block/melee
// leaves contain concrete-player casts; expanding that is role-adapter work.
bool PrepareNativePlayerClearInputs(void* player,void* pawn);
NativePlayerFormResult NativeClearPlayerFormInputs(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
// Read-only preflight, then install only after the complete role admission has
// passed. Hooks forward stock entries unless this guarded transaction owns them.
bool PrepareNativePlayerFormEntry(HMODULE executable,DWORD game_thread,void* player,
    void* first_pawn,void* second_pawn);
bool InstallNativePlayerFormEntry();
NativePlayerFormResult NativeEnterPlayerForm(void* player,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
}  // namespace wfcds
