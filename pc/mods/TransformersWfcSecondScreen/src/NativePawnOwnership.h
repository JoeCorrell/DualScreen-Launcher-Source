#pragma once
#include <windows.h>

namespace wfcds {
enum class NativePawnOwnershipResult { kRejected, kApplied, kStateMismatch };
// The pawn half of existing-actor adoption. Runs Pawn.PossessedBy (or its AI
// override), NOT Controller.Possess/Restart. Does not publish Controller.Pawn,
// acknowledge input ownership, rebind AI/player services, or complete a swap.
// The custom transaction must supply those steps inside its guarded commit.
bool PrepareNativePawnOwnership(HMODULE executable, DWORD game_thread,
                                 void* first_pawn, void* second_pawn);
NativePawnOwnershipResult NativeBindPawnOwnership(void* pawn, void* controller);
}  // namespace wfcds
