#pragma once
#include "NativePlayerServices.h"
namespace wfcds {
// Private compiled SyncCurrentHeat, not the stripped reflected UFunction.
// Does not initialize heat properties, clone modifiers or rebuild a weapon.
// Read-only preparation also admits an empty/cold zero-maximum AI service;
// NativeSyncWeaponHeat still requires initialized, positive-capacity heat.
bool PrepareNativeWeaponHeat(HMODULE executable,DWORD thread,void* pawn,void* weapon);
// First player-use preparation while still AI-owned, Cool, at zero heat and
// with no modifiers. Seeds only HeatMax from the actual PlayerData. Rollback
// to zero is permitted only for a caller-journaled seed, after removing clones.
// No state/observer callbacks, modifier resets or control changes are invoked.
NativePlayerServiceResult NativeSeedWeaponHeatCapacity(void* pawn,void* weapon,
    void* player_data,bool rollback);
// Read-only guard across clone/GC boundaries; admits the newly rooted modifiers
// but still requires the same supported cold, zero-current AI heat service.
bool NativeWeaponHeatInitializationStable(void* pawn,void* weapon);
// Caller must arm its rollback journal before entry. State changes run the
// game's actual Cool/Overheated callbacks; a dirty callback is not success.
NativePlayerServiceResult NativeSyncWeaponHeat(void* pawn,void* weapon,float heat);
}
