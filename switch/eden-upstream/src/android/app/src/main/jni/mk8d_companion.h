


#pragma once

#include <cstddef>

class EmulationSession;

namespace Mk8dCompanion {



int FillSnapshot(EmulationSession& session, void* output, std::size_t output_size);


bool ActivateItemSlot(EmulationSession& session, int slot);

}
