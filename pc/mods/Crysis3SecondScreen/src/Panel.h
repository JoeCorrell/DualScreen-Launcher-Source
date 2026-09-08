#pragma once
#include "Model.h"
#include <atomic>
#include <string>

namespace c3 {
// Runs on its caller's dedicated panel thread. Never reads native game memory,
// grants adapter verification, or forwards commands/input to the game.
// False means startup/topology/runtime failure; true means a normal shutdown.
bool RunPanel(Mailbox& mailbox, const std::wstring& assets,
              std::atomic<bool>& stopping, void (*log)(const char*) noexcept);
} // namespace c3
