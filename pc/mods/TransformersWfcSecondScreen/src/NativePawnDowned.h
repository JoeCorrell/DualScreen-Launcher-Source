#pragma once
#include <windows.h>
namespace wfcds {
// Return the canonical raw controller for a mapped raw pawn and logical role.
using NativePawnDownedResolver=void* (*)(void* pawn,bool ai);
bool ValidateNativePawnDowned(HMODULE image);
bool InstallNativePawnDowned(HMODULE image,DWORD thread,NativePawnDownedResolver resolver);
bool RemoveNativePawnDowned();
bool NativePawnDownedInstalled();
void PublishNativePawnDownedPawns(void* player_controlled_ai,void* ai_controlled_player);
}
