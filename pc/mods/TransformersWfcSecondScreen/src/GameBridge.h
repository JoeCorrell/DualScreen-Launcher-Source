#pragma once

#include <windows.h>

namespace wfcds {

bool InitializeGameBridge(HMODULE executable);
void ShutdownGameBridge();

}  // namespace wfcds
