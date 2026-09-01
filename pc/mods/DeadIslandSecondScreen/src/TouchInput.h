// Thor lower-screen gestures delivered by WEMU over the guest loopback.

#pragma once

namespace dis {

class Panel;

bool StartTouchInput();
void StopTouchInput();

// Drains a bounded gesture queue on the render thread. The socket thread never
// calls Interface or D3D code directly.
void PumpTouchInput(Panel& panel);

}  // namespace dis
