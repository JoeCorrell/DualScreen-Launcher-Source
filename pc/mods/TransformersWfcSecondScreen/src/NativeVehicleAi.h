#pragma once
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativeVehicleAiResult { kRejected, kApplied, kStateMismatch };
// Copyable journal identity only. Never retain a pointer into the GOAP array:
// the engine can resize it between transaction pulses.
struct NativeVehicleAiNavigation {
  void* controller=nullptr;
  void* collection=nullptr;
  uint32_t controller_index=0,collection_index=0;
  int32_t mode=0;
};
bool NativeReadVehicleAiNavigation(void* controller,NativeVehicleAiNavigation* out);
// Restore only our known Activate transition (1), or accept an unchanged value.
// Requires a suspended/drained planner and the original collection identity.
bool NativeRestoreVehicleAiNavigation(const NativeVehicleAiNavigation& original);
// Read-only admission for a prospective physical AI pawn. This does NOT make
// TnPlayerPawn compatible with AI-only strategy/action fields or enable swaps.
bool PrepareNativeVehicleAi(HMODULE image,DWORD game_thread,void* controller,void* pawn);
// Validates the CURRENT strategy, path follower, form and blueprint ownership.
// A null strategy is correct only for a pawn with no native vehicle form.
bool NativeVehicleAiMatchesPawn(void* controller,void* pawn);
// Same-pulse, suspended/drained-plan operation on an already reciprocal pair.
// Creates only a native strategy UObject (never a pawn). Publishes it using the
// native GC barrier, then runs the verified initializer and subclass callback.
// The caller must own rollback BEFORE entry, including dirty failures.
NativeVehicleAiResult NativeRebuildVehicleAi(void* controller,void* pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
// Rollback after restoring the original reciprocal graph. Reuses the exact
// original strategy if its identity and owners remain valid; otherwise rebuilds
// for the restored pawn. A borrowed journal pointer is never treated as a root.
NativeVehicleAiResult NativeRestoreVehicleAi(void* controller,void* pawn,
    void* original,uint32_t original_index,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
}
