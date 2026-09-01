#pragma once

#include <cstddef>

class EmulationSession;

namespace ArkhamCompanion {

int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);

}
