#pragma once

#include <cstdint>

#include "Snapshot.h"

namespace wfcds {

void ResetState(Lifecycle lifecycle, const char* build_label,
                bool supported_build);
bool PublishSnapshot(const Snapshot& snapshot);
bool CopyLatestSnapshot(Snapshot* output);
bool SnapshotContentEqual(const Snapshot& left, const Snapshot& right);
bool QueueCommand(const Command& command, uint64_t* request_id = nullptr);
bool PollCommand(Command* output);
void RejectStaleCommands(uint64_t world_epoch);
uint32_t PendingCommandCount();

}  // namespace wfcds
