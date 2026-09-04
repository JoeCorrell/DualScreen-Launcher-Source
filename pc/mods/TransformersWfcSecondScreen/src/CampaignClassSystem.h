#pragma once

#include <cstddef>

#include <windows.h>

namespace wfcds {

// Runs exclusively from the outer UGameEngine::Tick detour on the recorded
// game thread. The system hooks WFC's native campaign-lobby travel and
// CharacterSelect bindings, then performs only deferred UI work here (never
// recursively from a UFunction executor hook).
bool TickCampaignClassSystem(void* game_engine, DWORD game_thread,
                             ULONGLONG now, bool* did_work);
bool CampaignClassSelectionActive();
// Converts the mission-stable shell identity stored in WFC's pawn/squad data
// back to the player-selected iconic identity for bottom-screen presentation.
bool CampaignClassSelectedCharacterName(const char* mission_shell,
                                        char* output, size_t capacity);
bool ShutdownCampaignClassSystem();

}  // namespace wfcds
