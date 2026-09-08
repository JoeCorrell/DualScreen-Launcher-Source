#pragma once
#include <windows.h>

namespace wfcds {
// Installs narrowly scoped weapon-role decisions: GivenTo tuning, DropFrom
// ammo preservation and OnPreServerFire exposure. Never hooks global IsA.
// Raw pawn -> true only for the currently validated logical player. This
// resolver must check canonical identities and reciprocal ownership links.
using NativeWeaponExposureResolver=bool (*)(void* pawn);
bool ValidateNativeWeaponExposureCode(HMODULE image);
bool InstallNativeWeaponExposure(HMODULE image,DWORD thread,NativeWeaponExposureResolver resolver);
bool RemoveNativeWeaponExposure();
bool NativeWeaponExposureInstalled();
// Fast-reject identities only; publish after the adapter's existing seqlock.
void PublishNativeWeaponExposurePawns(void* player,void* ai);
}
