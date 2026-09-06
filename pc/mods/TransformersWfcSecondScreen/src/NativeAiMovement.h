#pragma once
#include <windows.h>

namespace wfcds {
enum class NativeAiMovementResult { kRejected, kApplied, kStateMismatch };
bool PrepareNativeAiMovement(HMODULE executable, DWORD game_thread, void* controller);
// Stops existing motion intent, separately from CancelMove's state/delegates.
// Robot branch supports both physical pawn types without the stock AI-only
// cast. Vehicle branch requires the pawn's genuine native AI strategy.
bool PrepareNativeAiStopMovement(HMODULE executable,DWORD game_thread,void* controller,void* pawn);
NativeAiMovementResult NativeAiStopMovement(void* controller,void* pawn);
// Dispatches the CURRENT compiled state override; the native PopState path
// owns PoppedState cleanup and resumption of the previous state. Never invokes
// callbacks directly or clears delegates/PathFocus by writing their fields.
NativeAiMovementResult NativeAiCancelMove(void* controller);
// Stronger than script delegate == None: verify both complete delegates were
// cleared, including compiled callback pointers. Returns false on read failure.
bool NativeAiMoveDelegatesCleared(void* controller, bool* output);
}  // namespace wfcds
