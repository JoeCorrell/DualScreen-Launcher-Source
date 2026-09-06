#pragma once
#include <windows.h>

namespace wfcds {
enum class NativeDetachResult { kRejected, kApplied, kStateMismatch };
// Native, existing-actor detachment only. Does not implement adoption, restart,
// cross-role service adaptation, or authorize a swap on its own.
bool PrepareNativeControllerDetach(HMODULE executable, DWORD game_thread,
                                   void* player_controller, void* ai_controller);
NativeDetachResult NativeDetachPlayer(void* controller, void* expected_pawn);
NativeDetachResult NativeDetachAi(void* controller, void* expected_pawn);
}  // namespace wfcds
