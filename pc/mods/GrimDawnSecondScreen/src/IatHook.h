#pragma once

#include <windows.h>

namespace gdds {

void** FindIatSlot(HMODULE image, const char* imported_module,
                   const char* imported_name);
bool InstallIatHook(void** slot, void* expected, void* replacement,
                    void** original);
bool RestoreIatHook(void** slot, void* expected_replacement, void* original);

}  // namespace gdds
