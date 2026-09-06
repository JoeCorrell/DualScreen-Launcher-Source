#pragma once
#include "NativePlayerServices.h"
namespace wfcds {
// OnSetPawn's real POI Reset: camera focus/FOV, contextual-command queues,
// and GC-aware reference clearing. This does not change window/input routing.
bool PrepareNativePointOfInterest(HMODULE module,DWORD thread,void* player,void* manager);
// Caller has already armed the service journal and established an acknowledged
// player pair. Rollback deliberately uses the same native Reset semantics.
NativePlayerServiceResult NativeResetPointOfInterest(void* player,void* pawn,void* manager);
}
