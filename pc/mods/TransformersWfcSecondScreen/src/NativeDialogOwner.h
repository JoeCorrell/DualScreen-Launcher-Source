#pragma once
#include "NativePlayerServices.h"
namespace wfcds {
// Pure owner assignments from the cooked initializers, not dialogue restart.
// Does not adapt AI-only dialogue consumers to a physical player pawn.
bool PrepareNativeDialogOwners(DWORD game_thread,void* player,void* ai,void* player_pawn,void* ai_pawn);
NativePlayerServiceResult NativeInitializeDialogOwner(void* controller,void* pawn,void* other_pawn,
    // Rollback only: other_pawn must be this AI controller's physical-player
    // proxy; pawn must be the retained original physical-AI dialogue owner.
    bool restore_proxy_owner=false);
}
