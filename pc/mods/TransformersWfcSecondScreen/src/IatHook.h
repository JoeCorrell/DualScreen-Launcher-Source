#pragma once

#include <windows.h>

namespace wfcds {

void** FindIatSlot(HMODULE image, const char* module, const char* name);
bool InstallIatHook(void** slot, void* expected, void* replacement,
                    void** original);
bool RestoreIatHook(void** slot, void* replacement, void* original);

}  // namespace wfcds
