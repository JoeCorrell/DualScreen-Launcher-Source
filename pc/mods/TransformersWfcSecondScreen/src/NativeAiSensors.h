#pragma once
#include "NativePlayerInput.h"

namespace wfcds {
enum class NativeAiSensorResult { kRejected, kApplied, kStateMismatch };
// This native lifecycle accepts physical TnAiPawn allocations only. The
// physical-player AI-role adapter must not bypass that constraint.
bool PrepareNativeAiSensors(HMODULE image,DWORD thread,void* controller);
NativeAiSensorResult NativeRebindAiSensors(void* controller,void* pawn,void* previous_pawn,
    const NativePlayerInputLease& move,const NativePlayerInputLease& look);
}
