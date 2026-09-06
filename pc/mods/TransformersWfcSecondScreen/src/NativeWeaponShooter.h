#pragma once

#include <windows.h>

namespace wfcds {

// Shipping WFC removes these private UFunctions. Bind only the verified native
// implementation and its live state vtable; never manufacture script metadata.
// Preparation is read-only and must run on the established game thread.
bool PrepareNativeWeaponShooter(HMODULE executable, DWORD game_thread,
                                void* shooter);
bool NativeWeaponShooterIsFiring(void* shooter, bool* result);
bool NativeWeaponShooterActiveWeapon(void* shooter, void** result);
// Stronger than !IsFiring: exact Stopped state, no equipped shooter weapon or
// pending four-word notifications, and no nested mutation in progress.
bool NativeWeaponShooterHandoffReady(void* shooter);
bool NativeWeaponShooterStartFire(void* shooter);
bool NativeWeaponShooterStopFire(void* shooter);
bool NativeWeaponShooterDeactivate(void* shooter);
bool NativeWeaponShooterReset(void* shooter);

// Only rebind the owner's EXISTING equipped weapon. Do not take Activate's
// alternate inventory/equipping branch and inadvertently replace its loadout.
bool NativeWeaponShooterActivateEquipped(void* shooter, void* weapon);

}  // namespace wfcds
