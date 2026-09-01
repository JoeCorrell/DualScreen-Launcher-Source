#pragma once

#include <windows.h>

namespace gdds {

bool ResolveGameApi(HMODULE executable, HMODULE game, HMODULE engine);
bool InstallGameUpdateHook(HMODULE executable);
bool UninstallGameUpdateHook();
void SetSampleInterval(DWORD milliseconds);
void SetTelemetryTier(unsigned tier);
void SampleGameState();
void SampleAtPrimaryRenderTail();
void* ValidatedRenderPlayer();
void BeginCustomCharacterSubmission();
void EndCustomCharacterSubmission();

}  // namespace gdds
