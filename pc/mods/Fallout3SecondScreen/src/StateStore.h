#pragma once

#include "Snapshot.h"

namespace fo3ds {

void ResetState(Lifecycle lifecycle, const char* build_label,
                bool supported_build);
bool PublishSnapshot(const Snapshot& snapshot);
bool CopyLatestSnapshot(Snapshot* output);
uint64_t LatestSequence();

bool QueueCommand(const Command& command, uint64_t* assigned_request_id = nullptr);
bool PollCommand(Command* output);
void RejectStaleCommands(uint64_t world_epoch);
uint32_t PendingCommandCount();

}  // namespace fo3ds
