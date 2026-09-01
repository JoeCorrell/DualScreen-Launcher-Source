#pragma once

#include <cstdint>

namespace dis {

class HookSet;

// Installs counting stubs on every executable slot of one live element's
// vtable. The caller owns the HookSet and must RemoveAll() it when the probe is
// finished.
bool UiProbeInstall(void* element, HookSet* hooks);

// Writes the slot/call counts to the log, marking per-frame candidates.
void UiProbeReport(uint32_t frames);

// Resets counters so a second probe can be installed cleanly.
void UiProbeReset();

int UiProbeSlotCount();
void** UiProbeVtable();
void* UiProbeOriginal(int slot);

}  // namespace dis
