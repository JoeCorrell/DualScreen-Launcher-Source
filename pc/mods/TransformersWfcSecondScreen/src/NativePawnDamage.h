#pragma once
#include <windows.h>
namespace wfcds {
// Narrow AI-only damage continuations. Shared damage/health-on-block behavior
// remains native. Does not replace damage, controller or buff systems.
bool ValidateNativePawnDamage(HMODULE image);
bool InstallNativePawnDamage(HMODULE image,DWORD game_thread);
bool RemoveNativePawnDamage();
bool NativePawnDamageInstalled();
// Only fast no-write identities; canonical role ownership is published by the
// existing adapter transaction. Never aliases a physical pawn's layout.
void PublishNativePawnDamagePlayer(void* physical_ai_pawn);
}
