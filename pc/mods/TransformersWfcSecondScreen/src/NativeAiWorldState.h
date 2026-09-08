#pragma once
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativeAiWorldStateResult { kRejected, kApplied, kStateMismatch };
// Read-only admission for a prospective physical AI pawn. Does not make the
// sibling TnPlayerPawn allocation compatible with AI-only planning/actions.
bool PrepareNativeAiWorldState(HMODULE image,DWORD thread,void* controller,void* pawn);
// Refresh the existing controller's weapon class, current virtual overheat
// state and robot/vehicle form. Calls shipping RefreshPawnWorldState, never
// SetPawn/Restart/form transitions. Requires reciprocal ownership, stable form,
// held input leases and a drained/suspended plan. No per-frame polling.
// Own rollback BEFORE entry; kStateMismatch means native writes have occurred.
NativeAiWorldStateResult NativeRefreshAiWorldState(void* controller,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
bool NativeAiWorldStateMatchesPawn(void* controller,void* pawn);
}
