#pragma once
#include <windows.h>
namespace wfcds {
// Preserves the bounded former-player proxy's existing robot-form restriction.
// This is NOT a vehicle AI implementation. All other pawns run stock code.
bool ValidateNativePawnTransform(HMODULE image);
bool NativePawnTransformDispatchSupported(HMODULE image,void* pawn);
bool InstallNativePawnTransform(HMODULE image,DWORD game_thread);
bool RemoveNativePawnTransform();
bool NativePawnTransformInstalled();
// Publish only after the canonical role mapping; clear on restore/shutdown.
// The guard only denies a call: it never dereferences the published address.
void PublishNativePawnTransformGuard(void* former_player);
}
