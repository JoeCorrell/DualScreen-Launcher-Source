#pragma once
#include <windows.h>
namespace wfcds {
// Shared scoped player-role checks for combat, vehicle mouse-input adjustment,
// movement records, fine aim and targeting. These never globally spoof IsA.
// Melee activation additionally requires a live vehicle-mesh chain: WFC's
// native visibility getter dereferences it without a None guard.
using NativePlayerCombatResolver=bool (*)(void* pawn,bool activating_melee);
// This second validator is required: the native setter reads the actual
// targeting object's fourth entry before performing any array bounds handling.
using NativeMeleeTargetResolver=bool (*)(void* pawn,void* targeting,void* victim);
bool ValidateNativePlayerCombat(HMODULE);
bool InstallNativePlayerCombat(HMODULE,DWORD,NativePlayerCombatResolver,NativeMeleeTargetResolver);
bool RemoveNativePlayerCombat();
bool NativePlayerCombatInstalled();
void PublishNativePlayerCombatPawns(void* player_controlled_ai,void* ai_controlled_player);
}
