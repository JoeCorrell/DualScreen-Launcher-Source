#pragma once

#include <windows.h>

namespace wfcds {

// true forwards the original travel; false defers it to the existing selector.
// The callback receives UObject*, not the native script secondary base.
using CampaignTravelAdmission = bool (*)(void* lobby);
// Install only from the outer game tick, never from the startup worker.
bool InstallNativeCampaignTravel(HMODULE executable, DWORD game_thread,
                                CampaignTravelAdmission admission);
bool NativeCampaignTravelReady();
bool ResumeNativeCampaignTravel(void* lobby);
// Puts the pinned hook into pass-through mode. No worker-thread unpatch/free.
void StopNativeCampaignTravel();

}  // namespace wfcds
