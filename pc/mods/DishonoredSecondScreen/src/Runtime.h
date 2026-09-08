#pragma once
#include "GameMemory.h"
#include "Panel.h"

namespace dishonored {
// Called only after the proxy has verified the entire installed executable.
// Owns all hooks and resources for the duration of the lower-panel worker.
bool RunRuntime(Address image, const std::wstring& executableDirectory,
    std::atomic<bool>& stopping, LogFunction log);
}
