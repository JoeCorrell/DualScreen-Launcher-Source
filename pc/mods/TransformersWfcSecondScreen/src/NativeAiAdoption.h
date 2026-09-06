#pragma once
#include <windows.h>
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativeAiAdoptionResult { kRejected, kApplied, kStateMismatch };
// Existing actor ownership plus owner-bound AI services. No Possess/Restart,
// template application, inventory creation or GOAP resume. The transaction
// must separately adapt the physical pawn's role, sensors and vehicle AI.
bool PrepareNativeAiAdoption(HMODULE executable, DWORD game_thread,
    void* controller, void* first_pawn, void* second_pawn);
// other_pawn constrains stale service owners during forward/rollback adoption.
// Only these two live actors may own the services; unrelated owners reject.
// Player adoption/acknowledgment of other_pawn must already be reciprocal.
NativeAiAdoptionResult NativeAdoptExistingAiPawn(void* controller, void* pawn,
    void* other_pawn, const NativePlayerInputLease& move, const NativePlayerInputLease& look);
}  // namespace wfcds
