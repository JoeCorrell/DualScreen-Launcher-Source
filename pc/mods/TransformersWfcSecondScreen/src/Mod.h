#pragma once

#include <windows.h>

namespace wfcds {

bool StartMod(HMODULE module);
void NotifyProcessDetach();

}  // namespace wfcds
