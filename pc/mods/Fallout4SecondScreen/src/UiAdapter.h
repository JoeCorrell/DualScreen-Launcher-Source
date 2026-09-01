#pragma once

#include "NativeActions.h"
#include "PipboyProtocol.h"
#include "UiSnapshot.h"

#include <string>

namespace fo4ds {

// Converts Fallout 4's live native Pip-Boy model into the bounded presentation
// model consumed by the Fallout 3-derived dual-screen UI renderer.
void AdaptPipboySnapshot(const PipboySnapshot& source, bool actions_enabled,
                         Snapshot* destination);

// Resolves a UI selection back to current authoritative native identifiers.
// UI identifiers are never trusted as engine handles without this lookup.
bool BuildNativeActionRequest(const Command& command,
                              const PipboySnapshot& source, uint64_t token,
                              NativeActionRequest* request);

}  // namespace fo4ds
