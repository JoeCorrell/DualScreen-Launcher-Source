#pragma once

#include <windows.h>

namespace isaacds {

// Installs the exact-build Lua import hook that publishes the state-submit
// function into Isaac's sandboxed Lua globals. The J460 sandbox removes
// package.loadlib, so the bridge must be registered by the native patch.
bool InstallLuaBridgeHook(HMODULE executable);

}  // namespace isaacds
