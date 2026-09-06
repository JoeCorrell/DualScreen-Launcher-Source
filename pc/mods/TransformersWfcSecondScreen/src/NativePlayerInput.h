#pragma once
#include <windows.h>
#include <cstdint>

namespace wfcds {
enum class NativeInputAxis { kMove, kLook };
enum class NativeInputResult { kRejected, kApplied, kStateMismatch };
// One owned increment, not a boolean setter. Keep at a stable address until
// released. Copying a lease does not transfer ownership. No destructor makes
// engine calls (world teardown may already have invalidated the controller).
struct NativePlayerInputLease {
  void* controller = nullptr;
  void* world = nullptr;
  uint32_t controller_index = 0, world_index = 0;
  uint8_t baseline = 0;
  NativeInputAxis axis = NativeInputAxis::kMove;
  bool held = false;
  bool invalidated = false;
};
bool PrepareNativePlayerInput(HMODULE executable, DWORD game_thread, void* player);
NativeInputResult NativeAcquirePlayerInput(void* player, NativeInputAxis axis,
                                          NativePlayerInputLease* lease);
bool NativePlayerInputHeld(const NativePlayerInputLease& lease);
NativeInputResult NativeReleasePlayerInput(NativePlayerInputLease* lease);
// Process teardown only: keep the pinned native trampoline as pass-through.
void StopNativePlayerInput();
}  // namespace wfcds
