#pragma once
#include <windows.h>
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativePlayerAdoptionResult { kRejected, kApplied, kStateMismatch };
// Existing-actor player ownership/acknowledgment, without Possess/Restart.
// NOT a complete player/AI role transfer: the guarded transaction must still
// rebind services, camera/form state, HUD, mission identity and the former AI.
bool PrepareNativePlayerAdoption(HMODULE executable, DWORD game_thread,
                                  void* controller, void* first_pawn, void* second_pawn);
NativePlayerAdoptionResult NativeAdoptExistingPlayerPawn(void* controller, void* pawn,
    const NativePlayerInputLease& move, const NativePlayerInputLease& look);
}  // namespace wfcds
