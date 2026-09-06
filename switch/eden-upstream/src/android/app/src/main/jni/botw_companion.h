


#pragma once

#include <string>

class EmulationSession;

namespace BotwCompanion {


std::string GetSnapshot(EmulationSession& session, bool lightweight = false);

// Read-only, bounded skeletal sample for the visible character preview only.
std::string GetHeldPreview(EmulationSession& session);
std::string GetMapDiscovery(EmulationSession& session, const std::string& known_regions);


bool PerformAction(EmulationSession& session, int action, long long argument);

}
