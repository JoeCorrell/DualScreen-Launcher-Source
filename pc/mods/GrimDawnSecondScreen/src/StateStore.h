#pragma once

#include "Snapshot.h"

namespace gdds {

void ResetState(Lifecycle lifecycle, const char* build_label,
                const char* status, bool supported);
bool ResetStateIfChanged(Lifecycle lifecycle, const char* build_label,
                         const char* status, bool supported);
bool PublishSnapshot(const Snapshot& snapshot);
bool CopyLatestSnapshot(Snapshot* output);
uint64_t LatestSequence();

}  // namespace gdds
