#include "PanelInput.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace fo4ds {
namespace {

constexpr size_t kMaximumPacketBytes = 255;

bool Space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && Space(value.front())) value.remove_prefix(1);
  while (!value.empty() && Space(value.back())) value.remove_suffix(1);
  return value;
}

size_t Tokens(std::string_view value, std::string_view* output, size_t maximum) {
  size_t count = 0;
  value = Trim(value);
  while (!value.empty()) {
    if (count >= maximum) return maximum + 1;
    size_t end = 0;
    while (end < value.size() && !Space(value[end])) ++end;
    output[count++] = value.substr(0, end);
    value.remove_prefix(end);
    value = Trim(value);
  }
  return count;
}

bool Float(std::string_view token, float* output) {
  if (!output || token.empty() || token.size() >= 64) return false;
  char buffer[64] = {};
  std::memcpy(buffer, token.data(), token.size());
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(buffer, &end);
  if (errno == ERANGE || end != buffer + token.size() ||
      !std::isfinite(value))
    return false;
  *output = value;
  return true;
}

bool Coordinate(float value) { return value >= 0.0f && value <= 1.0f; }

bool ViewportValid(const PanelViewport& viewport) {
  return viewport.panel_width > 0 && viewport.panel_height > 0 &&
         viewport.content_x >= 0 && viewport.content_y >= 0 &&
         viewport.content_width > 0 && viewport.content_height > 0 &&
         viewport.content_width <= viewport.panel_width &&
         viewport.content_height <= viewport.panel_height &&
         viewport.content_x <= viewport.panel_width - viewport.content_width &&
         viewport.content_y <= viewport.panel_height - viewport.content_height;
}

bool MapPoint(float x, float y, const PanelViewport& viewport, bool clamp,
              float* mapped_x, float* mapped_y) {
  if (mapped_x) *mapped_x = 0.0f;
  if (mapped_y) *mapped_y = 0.0f;
  if (!mapped_x || !mapped_y || !Coordinate(x) || !Coordinate(y) ||
      !ViewportValid(viewport))
    return false;
  const double physical_x = static_cast<double>(x) * viewport.panel_width;
  const double physical_y = static_cast<double>(y) * viewport.panel_height;
  const double right =
      static_cast<double>(viewport.content_x) + viewport.content_width;
  const double bottom =
      static_cast<double>(viewport.content_y) + viewport.content_height;
  if (!clamp &&
      (physical_x < viewport.content_x || physical_x > right ||
       physical_y < viewport.content_y || physical_y > bottom))
    return false;
  *mapped_x = std::clamp(static_cast<float>(
                             (physical_x - viewport.content_x) /
                             viewport.content_width),
                         0.0f, 1.0f);
  *mapped_y = std::clamp(static_cast<float>(
                             (physical_y - viewport.content_y) /
                             viewport.content_height),
                         0.0f, 1.0f);
  return true;
}

PanelGesture Direct(PanelGestureKind kind, int direction = 0) {
  PanelGesture gesture;
  gesture.kind = kind;
  gesture.direction = direction;
  return gesture;
}

}  // namespace

bool ParsePanelPacket(const char* bytes, size_t size, PanelPacket* packet) {
  if (!bytes || !packet || !size || size > kMaximumPacketBytes ||
      std::memchr(bytes, '\0', size))
    return false;
  *packet = {};
  std::string_view tokens[5];
  const size_t count = Tokens(std::string_view(bytes, size), tokens, 5);
  if (!count || count > 4) return false;

  auto coordinates = [&](PanelPacketKind kind, bool value_required) {
    const size_t required = value_required ? 4 : 3;
    if (count != required || !Float(tokens[1], &packet->x) ||
        !Float(tokens[2], &packet->y) || !Coordinate(packet->x) ||
        !Coordinate(packet->y))
      return false;
    if (value_required &&
        (!Float(tokens[3], &packet->value) || packet->value < 0.0f))
      return false;
    packet->kind = kind;
    return true;
  };

  if (tokens[0] == "tap" || tokens[0] == "select") {
    if (count == 3) return coordinates(PanelPacketKind::kTap, false);
    return coordinates(PanelPacketKind::kTap, true);
  }
  if (tokens[0] == "hold")
    return coordinates(PanelPacketKind::kHold, true);
  if (tokens[0] == "down")
    return coordinates(PanelPacketKind::kDown, false);
  if (tokens[0] == "move")
    return coordinates(PanelPacketKind::kMove, false);
  if (tokens[0] == "up")
    return coordinates(PanelPacketKind::kUp, false);
  if (tokens[0] == "cancel") {
    if (count == 1) {
      packet->kind = PanelPacketKind::kCancel;
      return true;
    }
    return coordinates(PanelPacketKind::kCancel, false);
  }
  if (count == 2 && tokens[0] == "page") {
    if (tokens[1] == "next") packet->kind = PanelPacketKind::kPageNext;
    else if (tokens[1] == "previous")
      packet->kind = PanelPacketKind::kPagePrevious;
    return packet->kind != PanelPacketKind::kInvalid;
  }
  if (count == 2 && tokens[0] == "nav") {
    if (tokens[1] == "left") packet->kind = PanelPacketKind::kNavLeft;
    else if (tokens[1] == "right") packet->kind = PanelPacketKind::kNavRight;
    else if (tokens[1] == "up") packet->kind = PanelPacketKind::kNavUp;
    else if (tokens[1] == "down") packet->kind = PanelPacketKind::kNavDown;
    else if (tokens[1] == "activate")
      packet->kind = PanelPacketKind::kNavActivate;
    return packet->kind != PanelPacketKind::kInvalid;
  }
  return false;
}

bool PanelPacketCarriesPointerInput(PanelPacketKind kind) {
  switch (kind) {
    case PanelPacketKind::kTap:
    case PanelPacketKind::kHold:
    case PanelPacketKind::kDown:
    case PanelPacketKind::kMove:
    case PanelPacketKind::kUp:
    case PanelPacketKind::kCancel:
      return true;
    default:
      return false;
  }
}

bool MapPanelPointToContent(float x, float y, int panel_width,
                            int panel_height, int content_x, int content_y,
                            int content_width, int content_height,
                            float* mapped_x, float* mapped_y) {
  return MapPoint(x, y,
                  {panel_width, panel_height, content_x, content_y,
                   content_width, content_height},
                  false, mapped_x, mapped_y);
}

bool PanelPacketMapper::Map(const PanelPacket& packet,
                            const PanelViewport& viewport,
                            PanelPacket* mapped) {
  if (!mapped || !ViewportValid(viewport)) return false;
  *mapped = packet;
  switch (packet.kind) {
    case PanelPacketKind::kTap:
    case PanelPacketKind::kHold:
      if (pointer_active_ || rejected_sequence_) return false;
      return MapPoint(packet.x, packet.y, viewport, false, &mapped->x,
                      &mapped->y);
    case PanelPacketKind::kDown:
      pointer_active_ = false;
      rejected_sequence_ = !MapPoint(packet.x, packet.y, viewport, false,
                                     &mapped->x, &mapped->y);
      if (rejected_sequence_) return false;
      pointer_active_ = true;
      return true;
    case PanelPacketKind::kMove:
      if (rejected_sequence_ || !pointer_active_) return false;
      return MapPoint(packet.x, packet.y, viewport, true, &mapped->x,
                      &mapped->y);
    case PanelPacketKind::kUp: {
      if (rejected_sequence_) {
        Reset();
        return false;
      }
      if (!pointer_active_) return false;
      const bool result = MapPoint(packet.x, packet.y, viewport, true,
                                   &mapped->x, &mapped->y);
      Reset();
      return result;
    }
    case PanelPacketKind::kCancel:
      Reset();
      return true;
    default:
      return true;
  }
}

void PanelPacketMapper::Reset() {
  pointer_active_ = false;
  rejected_sequence_ = false;
}

PanelGesture PanelGestureArbiter::Consume(const PanelPacket& packet) {
  switch (packet.kind) {
    case PanelPacketKind::kTap: {
      if (packet.x >= 0.40f && packet.x <= 0.60f && packet.y >= 0.94f)
        return {};
      PanelGesture gesture = Direct(PanelGestureKind::kTap);
      gesture.x = packet.x;
      gesture.y = packet.y;
      return gesture;
    }
    case PanelPacketKind::kHold: {
      if (packet.x >= 0.40f && packet.x <= 0.60f && packet.y >= 0.94f)
        return {};
      PanelGesture gesture = Direct(PanelGestureKind::kHold);
      gesture.x = packet.x;
      gesture.y = packet.y;
      gesture.value = packet.value;
      return gesture;
    }
    case PanelPacketKind::kDown:
      pointer_down_ = true;
      down_x_ = packet.x;
      down_y_ = packet.y;
      moved_beyond_tap_ = false;
      reserved_origin_ = packet.x >= 0.40f && packet.x <= 0.60f &&
                         packet.y >= 0.94f;
      return {};
    case PanelPacketKind::kMove:
      if (pointer_down_ &&
          (std::fabs(packet.x - down_x_) >= 0.025f ||
           std::fabs(packet.y - down_y_) >= 0.025f))
        moved_beyond_tap_ = true;
      return {};
    case PanelPacketKind::kUp: {
      if (!pointer_down_) return {};
      pointer_down_ = false;
      if (reserved_origin_) {
        reserved_origin_ = false;
        return {};
      }
      const float dx = packet.x - down_x_;
      const float dy = packet.y - down_y_;
      if (std::fabs(dx) >= 0.105f &&
          std::fabs(dx) > std::fabs(dy) * 1.35f) {
        PanelGesture gesture =
            Direct(PanelGestureKind::kPage, dx < 0.0f ? 1 : -1);
        gesture.x = down_x_;
        gesture.y = down_y_;
        gesture.delta_x = dx;
        gesture.delta_y = dy;
        return gesture;
      }
      if (std::fabs(dy) >= 0.055f &&
          std::fabs(dy) > std::fabs(dx) * 1.15f) {
        PanelGesture gesture = Direct(PanelGestureKind::kScroll);
        gesture.x = down_x_;
        gesture.y = down_y_;
        gesture.value = dy;
        gesture.delta_x = dx;
        gesture.delta_y = dy;
        return gesture;
      }
      if (std::fabs(dx) >= 0.025f || std::fabs(dy) >= 0.025f) {
        PanelGesture gesture = Direct(PanelGestureKind::kDrag);
        gesture.x = down_x_;
        gesture.y = down_y_;
        gesture.delta_x = dx;
        gesture.delta_y = dy;
        return gesture;
      }
      if (std::fabs(dx) < 0.025f && std::fabs(dy) < 0.025f) {
        // A routed drag may return to its origin before finger-up.  Classify
        // from the complete trajectory so that release cannot become a tap on
        // a control the user merely dragged across.
        if (moved_beyond_tap_) {
          moved_beyond_tap_ = false;
          return {};
        }
        PanelGesture gesture = Direct(PanelGestureKind::kTap);
        gesture.x = packet.x;
        gesture.y = packet.y;
        return gesture;
      }
      moved_beyond_tap_ = false;
      return {};
    }
    case PanelPacketKind::kCancel:
      Reset();
      return {};
    case PanelPacketKind::kPageNext:
      return Direct(PanelGestureKind::kPage, 1);
    case PanelPacketKind::kPagePrevious:
      return Direct(PanelGestureKind::kPage, -1);
    case PanelPacketKind::kNavLeft:
      return Direct(PanelGestureKind::kNavLeft);
    case PanelPacketKind::kNavRight:
      return Direct(PanelGestureKind::kNavRight);
    case PanelPacketKind::kNavUp:
      return Direct(PanelGestureKind::kNavUp);
    case PanelPacketKind::kNavDown:
      return Direct(PanelGestureKind::kNavDown);
    case PanelPacketKind::kNavActivate:
      return Direct(PanelGestureKind::kNavActivate);
    default:
      return {};
  }
}

void PanelGestureArbiter::Reset() {
  pointer_down_ = false;
  reserved_origin_ = false;
  moved_beyond_tap_ = false;
  down_x_ = down_y_ = 0.0f;
}

}  // namespace fo4ds
