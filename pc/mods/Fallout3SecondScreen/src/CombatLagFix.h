#pragma once

#include "Version.h"

namespace fo3ds {

// Removes the two erroneous HUD target-health timer waits in the exact
// Fallout3.exe build supported by the bridge. The patch is byte-validated and
// refuses to run on any other executable or if another plugin already owns the
// call sites.
bool ApplyCombatLagFix(const BuildIdentity& build);

// Emits rate-limited execution counts and verifies that neither call site was
// overwritten after startup. Called from the bridge worker, never the game
// thread.
void FlushCombatLagDiagnostics();

}  // namespace fo3ds
