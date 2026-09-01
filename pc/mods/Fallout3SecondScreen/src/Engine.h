#pragma once

#include "Version.h"

namespace fo3ds {

// Resolves every required engine entry point as a unit. A partial match is a
// hard failure: no pointer is published and no game memory is read.
bool InitializeEngineBridge(const BuildIdentity& build);
bool AttachGameThreadTimer(HWND game_window);
void OnGameThreadTick();
void ShutdownEngineBridge();

}  // namespace fo3ds
