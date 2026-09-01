#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "common/common_types.h"

class EmulationSession;

namespace DualScreenRomfsAssets {

std::vector<u8> LoadAsset(EmulationSession& session, std::string_view logical_path);
std::string GetRevision(EmulationSession& session);
void Reset();

}
