#pragma once
#include <string>
namespace isaacds {
// Called only from Isaac's Lua callbacks on the game thread, after the J460 gate.
std::string ReadNativeRunState();
std::string ReadNativeRoomFlags();
bool InstallCompanionAchievementCompatibility();
int NativeHitCount(int restore = -1);
}
