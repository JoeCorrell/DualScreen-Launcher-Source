#pragma once

#include <cstddef>
#include <cstdint>

namespace gdds {

enum class PanelPacketKind : uint8_t {
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

struct PanelPacket {
  PanelPacketKind kind = PanelPacketKind::kInvalid;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
};

bool ParsePanelPacket(const char* bytes, size_t size, PanelPacket* packet);

enum class PanelGestureKind : uint8_t {
  kNone,
  kTap,
  kHold,
  kPage,
  kScroll,
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

class PanelGestureArbiter {
 public:
  PanelGesture Consume(const PanelPacket& packet);
  void Reset();

 private:
  bool pointer_down_ = false;
  bool reserved_origin_ = false;
  float down_x_ = 0.0f;
  float down_y_ = 0.0f;
};

}  // namespace gdds
