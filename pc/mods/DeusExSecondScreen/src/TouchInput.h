// Normalized touch gestures delivered by WEMU's physical bottom-screen overlay.

#pragma once

namespace dxs {

enum class GuestHapticCue {
  kSelection,
  kAction,
  kUnlock,
};

bool StartTouchInput();
void StopTouchInput();
// Replies to the Android socket which most recently delivered a lower-panel
// gesture. The app owns the vibrator; native game code only emits semantic
// success cues after an action has actually completed.
bool SendGuestHaptic(GuestHapticCue cue);

}  // namespace dxs
