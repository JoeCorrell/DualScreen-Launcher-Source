#pragma once

#include <windows.h>

#include "Config.h"
#include "BuildIdentity.h"

namespace fomds {

bool StartPanel(HMODULE module, const Config& config,
                const BuildIdentity& verified_identity);
void RequestPanelStop();
// Loader-lock-safe detach signal: atomic store only, with no wait or User32 I/O.
void SignalPanelStop();
bool StopPanel(DWORD timeout_ms);
HWND PanelWindow();

}  // namespace fomds
