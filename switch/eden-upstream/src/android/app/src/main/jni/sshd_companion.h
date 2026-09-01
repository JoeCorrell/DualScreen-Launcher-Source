#pragma once

#include <cstddef>

class EmulationSession;

namespace SshdCompanion {

// Writes one versioned, immutable Skyward Sword HD state frame into output.
// This interface is intentionally read-only until a game-thread action hook is verified.
int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

}
