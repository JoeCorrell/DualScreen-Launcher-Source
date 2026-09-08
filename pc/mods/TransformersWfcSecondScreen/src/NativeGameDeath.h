#pragma once
#include <windows.h>
#include <cstdint>
namespace wfcds {
enum class NativeDeathRole:uint32_t {kStock,kPlayer,kAi,kSuppress};
struct NativeDeathIdentity {
  NativeDeathRole role=NativeDeathRole::kSuppress;
  uint32_t generation=0;
};
// Capture runs before GameInfo.Killed. Validation runs at the native class
// decision afterward: notification callbacks may have detached the victim.
using NativeDeathCapture=NativeDeathIdentity (*)(void* pawn,void* controller);
using NativeDeathValidate=bool (*)(void* pawn,void* controller,NativeDeathIdentity);
using NativeDeathNotify=void (*)();
bool ValidateNativeGameDeath(HMODULE);
bool InstallNativeGameDeath(HMODULE,DWORD,NativeDeathCapture,NativeDeathValidate,NativeDeathNotify);
bool RemoveNativeGameDeath();
bool NativeGameDeathInstalled();
void PublishNativeGameDeathPawns(void* player_controlled_ai,void* ai_controlled_player);
}
