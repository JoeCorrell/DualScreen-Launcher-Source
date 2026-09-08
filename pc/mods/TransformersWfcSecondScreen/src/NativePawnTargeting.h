#pragma once
#include <windows.h>
namespace wfcds {
// Raw pawn -> canonical, reciprocal logical player identity. No class spoof.
using NativePawnTargetingResolver=bool (*)(void*);
bool ValidateNativePawnTargeting(HMODULE image);
// Canonical pawn validation is the caller's responsibility. Reject a native
// target-location override we have not traced, rather than ignore it.
bool NativePawnLocationDispatchSupported(HMODULE image,void* raw_pawn);
bool InstallNativePawnTargeting(HMODULE image,DWORD thread,NativePawnTargetingResolver resolver);
bool RemoveNativePawnTargeting();
bool NativePawnTargetingInstalled();
void PublishNativePawnTargetingPlayer(void* pawn);
}
