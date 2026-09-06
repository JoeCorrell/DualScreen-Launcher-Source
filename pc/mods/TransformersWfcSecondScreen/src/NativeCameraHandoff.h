#pragma once
#include <windows.h>
#include <cstdint>
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativeCameraResult { kRejected, kApplied, kStateMismatch };
// Read-only admission. This does not satisfy the separate player/AI role,
// controller form-state or mission/checkpoint requirements. View-target has
// its own explicit admission below, after this camera binding succeeds.
bool PrepareNativeCameraHandoff(HMODULE executable, DWORD game_thread,
    void* player, void* first_pawn, void* second_pawn);
// Changes this controller's EXISTING camera manager, never clones a template.
// The requested pawn must already be the acknowledged reciprocal player pawn.
// A dirty failure requires the outer transaction's guarded rollback.
NativeCameraResult NativeHandoffCamera(void* player, void* pawn, void* other_pawn,
    uint8_t form, const NativePlayerInputLease& move, const NativePlayerInputLease& look);
// Controller BeginState itself calls camera.StartForm. Stage owner/form aliases
// without activating here, then enter the controller state exactly once and
// verify below. Only the guarded atomic control-transfer transaction may use
// this split path; do not yield a game frame between staging and state entry.
NativeCameraResult NativeStageCameraForPlayerForm(void* player, void* pawn, void* other_pawn,
    uint8_t form, const NativePlayerInputLease& move, const NativePlayerInputLease& look);
bool NativeCameraFormIsCurrent(void* player, void* pawn, void* other_pawn, uint8_t form);
// Shared exact-build, read-only form classification used by controller entry.
// Reads the actual current form and verified side-effect-free vehicle getter.
bool NativeReadPawnForm(void* pawn, uint8_t* mode, uint8_t* vehicle_type);
// Bind only after camera admission. Uses the real controller/camera view-target
// chain with an immediate transition, not a Controller.ViewTarget field write.
bool PrepareNativeViewTarget(void* player, void* current_pawn, void* other_pawn);
NativeCameraResult NativeSetPlayerViewTarget(void* player, void* pawn, void* other_pawn,
    const NativePlayerInputLease& move, const NativePlayerInputLease& look);
}  // namespace wfcds
