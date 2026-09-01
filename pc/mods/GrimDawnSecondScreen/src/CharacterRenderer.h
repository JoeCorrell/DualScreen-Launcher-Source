#pragma once

#include <windows.h>

namespace gdds {

bool InstallCharacterRenderer(HMODULE executable, HMODULE game,
                              HMODULE engine);
bool UninstallCharacterRenderer();

}  // namespace gdds
