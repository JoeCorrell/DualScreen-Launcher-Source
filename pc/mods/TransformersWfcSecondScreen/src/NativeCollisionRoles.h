#pragma once
#include "NativePlayerServices.h"
namespace wfcds {
bool PrepareNativeCollisionRole(DWORD thread,void* pawn,void* collision_controller);
// Port of TnPawnCollisionController.SetRBCollidesWithChannel, specialized to
// player-only blocking volumes (channel 15). Calls both actual mesh executors;
// does not reinitialize physics, collision modes, teams, forms or components.
// Caller arms rollback before entry, including partial robot/vehicle updates.
NativePlayerServiceResult NativeSetCollisionRole(void* pawn,void* collision_controller,bool player);
}
