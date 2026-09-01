#pragma once

#include <cstddef>

namespace fomds {

enum class PanelPacketKind {
  kInvalid,
  kTap,
  kHold,
  kDown,
  kMove,
  kUp,
  kCancel,
  kPinch,
  kPageNext,
  kPagePrevious,
  kNavLeft,
  kNavRight,
  kNavUp,
  kNavDown,
  kNavActivate,
};

struct PanelPacket {
  PanelPacketKind kind = PanelPacketKind::kInvalid;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
};

enum class PanelGestureKind {
  kNone,
  kTap,
  kHold,
  kScroll,
  kPan,
  kPinch,
  kPage,
  kNavLeft,
  kNavRight,
  kNavUp,
  kNavDown,
  kNavActivate,
};

struct PanelGesture {
  PanelGestureKind kind = PanelGestureKind::kNone;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
  int direction = 0;
};

bool ParsePanelPacket(const char* bytes, std::size_t size, PanelPacket* packet);

class PanelGestureArbiter {
 public:
  // Map content owns drags that begin in the content region. This prevents a
  // pan from being reinterpreted as a page swipe; tabs/controller commands
  // remain available for deterministic page changes.
  void SetMapCapture(bool enabled) { map_capture_ = enabled; }
  PanelGesture Consume(const PanelPacket& packet);
  void Reset();

 private:
  bool pointer_down_ = false;
  bool reserved_origin_ = false;
  bool moved_beyond_tap_ = false;
  bool map_capture_ = false;
  float down_x_ = 0.0f;
  float down_y_ = 0.0f;
};

}  // namespace fomds
