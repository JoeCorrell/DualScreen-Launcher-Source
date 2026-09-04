#pragma once

#include <cstddef>
#include <cstdint>

namespace isaacds {

enum class PanelInputKind : uint8_t {
  kInvalid,
  kTap,
  kHold,
  kDown,
  kMove,
  kUp,
  kCancel,
  kPageNext,
  kPagePrevious,
  kNavLeft,
  kNavRight,
  kNavUp,
  kNavDown,
  kNavActivate,
};

struct PanelInput {
  PanelInputKind kind = PanelInputKind::kInvalid;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
};

bool ParsePanelInput(const char* bytes, size_t size, PanelInput* output);

}  // namespace isaacds
