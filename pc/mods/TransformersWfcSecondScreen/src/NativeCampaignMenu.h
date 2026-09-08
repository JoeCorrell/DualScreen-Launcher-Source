#pragma once

#include <windows.h>

namespace wfcds {

// Result is an already initialized, engine-owned FString. Return true only
// after replacing it successfully; false preserves the stock menu getter.
using CampaignMenuCurrentCharacter = bool (*)(void* result);
// Called after the stock roster has been assigned. Binding is the raw UObject,
// not the compiled +0x10 base. Caller owns validation/filtering of this output.
using CampaignMenuPlayableCharacters = void (*)(void* binding, void* result);

bool InstallNativeCampaignMenu(HMODULE executable, DWORD game_thread,
                              CampaignMenuCurrentCharacter current_character,
                              CampaignMenuPlayableCharacters playable_characters);
bool NativeCampaignMenuReady();
// The detour remains pinned as a stock pass-through; no executing trampoline
// is freed during shutdown or a failed higher-level class-flow installation.
void StopNativeCampaignMenu();

}  // namespace wfcds
