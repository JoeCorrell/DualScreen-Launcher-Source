#pragma once
#include <cstddef>
#include <cstdint>
#include <windows.h>

namespace wfcds {
// Resolver receives a raw UObject and role. Return raw mapped pawn, or nullptr
// to fail closed as compiled None. Must validate the existing role mapping,
// canonical object slots and reciprocal links; never reinterpret class layout.
using NativeRolePawnResolver=void* (*)(void* controller,bool ai);
bool ValidateNativeRolePawnCode(HMODULE image);
bool InstallNativeRolePawnGetters(HMODULE image,DWORD game_thread,NativeRolePawnResolver resolver);
bool RemoveNativeRolePawnGetters();
bool NativeRolePawnGettersInstalled();
// Includes native AI-buddy getter/set-true hooks; mapped former players use
// logical buddy status, never physical TnAiPawn-only tail reads or writes.
// Fast reject identities only, NOT a second pawn/ownership mapping. Publish
// after the role seqlock snapshot is complete; clear before teardown.
void PublishNativeRolePawnControllers(void* player,void* ai);
// Shared signature audit: accepts exact unmodified code, or our intact target
// patch + original trampoline + untouched suffix. Never accepts a foreign hook.
bool NativeRolePawnCodeMatches(HMODULE image,uintptr_t va,const uint8_t* expected,size_t size);
}
