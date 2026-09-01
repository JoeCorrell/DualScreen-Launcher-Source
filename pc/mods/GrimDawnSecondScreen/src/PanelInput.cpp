#include "PanelInput.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace gdds {
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

PanelGesture Direct(PanelGestureKind kind, int direction = 0) {
  PanelGesture result;
  result.kind = kind;
  result.direction = direction;
  return result;
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
  if (tokens[0] == "hold") return coordinates(PanelPacketKind::kHold, true);
  if (tokens[0] == "down") return coordinates(PanelPacketKind::kDown, false);
  if (tokens[0] == "move") return coordinates(PanelPacketKind::kMove, false);
  if (tokens[0] == "up") return coordinates(PanelPacketKind::kUp, false);
  if (tokens[0] == "cancel")
    return coordinates(PanelPacketKind::kCancel, false);
  if (count == 2 && tokens[0] == "page") {
    if (tokens[1] == "next") packet->kind = PanelPacketKind::kPageNext;
    else if (tokens[1] == "previous")
      packet->kind = PanelPacketKind::kPagePrevious;
    else
      return false;
    return true;
  }
  if (count == 2 && tokens[0] == "nav") {
    if (tokens[1] == "left") packet->kind = PanelPacketKind::kNavLeft;
    else if (tokens[1] == "right") packet->kind = PanelPacketKind::kNavRight;
    else if (tokens[1] == "up") packet->kind = PanelPacketKind::kNavUp;
    else if (tokens[1] == "down") packet->kind = PanelPacketKind::kNavDown;
    else if (tokens[1] == "activate")
      packet->kind = PanelPacketKind::kNavActivate;
    else
      return false;
    return true;
  }
  return false;
}

PanelGesture PanelGestureArbiter::Consume(const PanelPacket& packet) {
  switch (packet.kind) {
    case PanelPacketKind::kTap:
      if (packet.x >= 0.4f && packet.x <= 0.6f && packet.y >= 0.94f)
        return {};
      return PanelGesture{PanelGestureKind::kTap, packet.x, packet.y,
                          packet.value, 0};
    case PanelPacketKind::kHold:
      if (packet.x >= 0.4f && packet.x <= 0.6f && packet.y >= 0.94f)
        return {};
      return PanelGesture{PanelGestureKind::kHold, packet.x, packet.y,
                          packet.value, 0};
    case PanelPacketKind::kDown:
      pointer_down_ = true;
      reserved_origin_ = packet.x >= 0.4f && packet.x <= 0.6f &&
                         packet.y >= 0.94f;
      down_x_ = packet.x;
      down_y_ = packet.y;
      return {};
    case PanelPacketKind::kMove:
      return {};
    case PanelPacketKind::kCancel:
      Reset();
      return {};
    case PanelPacketKind::kUp: {
      if (!pointer_down_ || reserved_origin_) {
        Reset();
        return {};
      }
      const float dx = packet.x - down_x_;
      const float dy = packet.y - down_y_;
      PanelGesture result;
      result.x = down_x_;
      result.y = down_y_;
      if (std::fabs(dx) >= 0.18f && std::fabs(dx) > std::fabs(dy)) {
        result.kind = PanelGestureKind::kPage;
        result.direction = dx < 0.0f ? 1 : -1;
      } else if (std::fabs(dy) >= 0.15f && std::fabs(dy) > std::fabs(dx)) {
        result.kind = PanelGestureKind::kScroll;
        result.value = dy;
      } else if (std::sqrt(dx * dx + dy * dy) <= 0.12f) {
        result.kind = PanelGestureKind::kTap;
      }
      Reset();
      return result;
    }
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
  down_x_ = 0.0f;
  down_y_ = 0.0f;
}

}  // namespace gdds
