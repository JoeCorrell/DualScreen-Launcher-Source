#pragma once
#include <windows.h>
namespace wfcds {
// Existing controller/pawn/aimer only. Preparation is read-only; updating is
// for the custom role proxy on the game thread, never a possession operation.
bool PrepareNativeAiAiming(HMODULE executable,DWORD game_thread,
    void* controller,void* pawn,void* aimer);
// expected_enemy must still be Controller.Enemy. A valid no-LOS result returns
// true with aligned=false and does not modify the aimer. Transforming/downed/
// stasis pawns likewise pause aiming without reporting a broken role mapping.
// Updates the native
// location target BEFORE testing alignment; does not start/stop firing itself.
bool NativeUpdateAiAiming(void* controller,void* pawn,void* aimer,
    void* expected_enemy,bool* aligned);
}
