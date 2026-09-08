#pragma once
#include "Mailbox.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dishonored {
// Called on the established controller thread, with adapterBusy set. No
// D3D hook, focus change, registry write, or rendering-thread operation.
class UpperViewport {
public:
    bool Tick(uintptr_t expectedWindowProcedure, uint64_t now, Bridge& bridge) noexcept;
private:
    uint64_t nextCheck_{};
    uint64_t nextDiagnostic_{};
};
}
