#pragma once
#include "Mailbox.h"
#include <atomic>
#include <string>

namespace dishonored {
using LogFunction = void (*)(const char*) noexcept;
// Runs on the payload worker, never on Dishonored's render/game thread.
// Prefer a real secondary monitor; also recognize Skyrim's verified Thor
// 1280x1800 tall-desktop layout without placing the panel over its upper 720px.
bool RunPanel(Bridge& bridge, const std::wstring& assets, const std::wstring& configuration,
    std::atomic<bool>& stopping, LogFunction log);
}
