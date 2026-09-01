#pragma once

#include <cstddef>
#include <cstdint>

namespace fo4ds {

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
bool PanelPacketCarriesPointerInput(PanelPacketKind kind);

// Converts Android's normalized full-display coordinate into the normalized
// content rectangle actually presented by the guest panel. Touches in aspect-
// ratio letterbox bars are rejected instead of being folded onto edge controls.
bool MapPanelPointToContent(float x, float y, int panel_width,
                            int panel_height, int content_x, int content_y,
                            int content_width, int content_height,
                            float* mapped_x, float* mapped_y);

struct PanelViewport {
  int panel_width = 0;
  int panel_height = 0;
  int content_x = 0;
  int content_y = 0;
  int content_width = 0;
  int content_height = 0;
};

// Maps complete packet sequences before gesture arbitration. A pointer that
// begins in a letterbox bar remains rejected through release, while a pointer
// that begins on content may finish outside and is clamped to the content edge.
class PanelPacketMapper {
 public:
  bool Map(const PanelPacket& packet, const PanelViewport& viewport,
           PanelPacket* mapped);
  void Reset();

 private:
  bool pointer_active_ = false;
  bool rejected_sequence_ = false;
};

enum class PanelGestureKind : uint8_t {
  kNone,
  kTap,
  kHold,
  kPage,
  kScroll,
  kDrag,
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
  float delta_x = 0.0f;
  float delta_y = 0.0f;
  int direction = 0;
};

class PanelGestureArbiter {
 public:
  PanelGesture Consume(const PanelPacket& packet);
  void Reset();

 private:
 bool pointer_down_ = false;
  bool reserved_origin_ = false;
  bool moved_beyond_tap_ = false;
  float down_x_ = 0.0f;
  float down_y_ = 0.0f;
};

}  // namespace fo4ds
