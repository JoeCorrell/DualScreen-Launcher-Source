


#pragma once

#include <string>

class EmulationSession;

namespace BotwCompanion {


std::string GetSnapshot(EmulationSession& session, bool lightweight = false);


bool PerformAction(EmulationSession& session, int action, long long argument);

}
