#pragma once

#include <windows.h>

namespace gdds {

// Exact-build compatibility guard for Grim Dawn's unchecked null external
// audio stream during Theora startup-video playback.
bool InstallStartupAudioGuard(HMODULE engine);

}  // namespace gdds
