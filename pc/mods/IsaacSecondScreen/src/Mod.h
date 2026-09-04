#pragma once

#include <windows.h>

namespace isaacds {

bool StartMod(HMODULE module);
void NotifyProcessDetach();

}  // namespace isaacds
