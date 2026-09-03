#pragma once

#include <cstdint>

#include "PanelInput.h"
#include "Snapshot.h"

namespace wfcds {

void ResetState(Lifecycle lifecycle, const char* build_label,
                bool supported_build);
bool PublishSnapshot(const Snapshot& snapshot);
bool CopyLatestSnapshot(Snapshot* output);
// Lock-free change token for consumers that only need to decide whether a
// snapshot copy is necessary. Zero means no snapshot has been published yet.
uint64_t LatestSequence();
bool SnapshotContentEqual(const Snapshot& left, const Snapshot& right);
// Compares only state rendered by the requested panel page. This lets the UI
// adopt a fresh snapshot without repainting for off-page domain changes.
bool SnapshotPanelContentEqual(const Snapshot& left, const Snapshot& right,
                               PanelPage page);
// Combat-page state outside the two self-contained ability cards. When this
// is equal, an ability update can repaint only those cards.
bool SnapshotCombatNonAbilityContentEqual(const Snapshot& left,
                                          const Snapshot& right);
// Publishes snapshot changes to the panel message loop. The target is an HWND
// kept opaque here so StateStore's public surface does not require windows.h.
// Passing nullptr disables notifications before panel teardown.
void SetSnapshotNotificationTarget(void* native_window, uint32_t message);
// The panel publishes its visible page so the game-thread producer can avoid
// collecting page-specific state that no current UI consumes.
void SetRequestedPanelPage(PanelPage page);
PanelPage RequestedPanelPage();
bool QueueCommand(const Command& command, uint64_t* request_id = nullptr);
bool PollCommand(Command* output);
// Closes the producer side of the command queue while a multi-tick native
// transition owns the game state. Closing the gate also discards anything
// that was queued against the pre-transition snapshot.
void SetCommandAcceptance(bool accepting);
void DiscardAllCommands();
void RejectStaleCommands(uint64_t world_epoch);
// Lock-free mirror used by the per-frame hook to avoid taking the queue lock
// when no command is waiting.
uint32_t PendingCommandCount();

}  // namespace wfcds
