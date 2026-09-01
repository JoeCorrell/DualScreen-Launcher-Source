#include "PanelInput.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace fomds {
namespace {

constexpr std::size_t kMaximumPacketBytes = 192;

std::size_t Tokens(std::string_view input, std::string_view* output,
                   std::size_t capacity) {
  std::size_t count = 0;
  std::size_t at = 0;
  while (at < input.size()) {
    while (at < input.size() && (input[at] == ' ' || input[at] == '\t' ||
                                 input[at] == '\r' || input[at] == '\n')) {
      ++at;
    }
    if (at == input.size()) break;
    const std::size_t start = at;
    while (at < input.size() && input[at] != ' ' && input[at] != '\t' &&
           input[at] != '\r' && input[at] != '\n') {
      ++at;
    }
    if (count >= capacity) return capacity + 1;
    output[count++] = input.substr(start, at - start);
  }
  return count;
}

bool Float(std::string_view text, float* output) {
  if (!output || text.empty() || text.size() >= 48) return false;
  char buffer[48] = {};
  std::memcpy(buffer, text.data(), text.size());
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(buffer, &end);
  if (errno == ERANGE || end != buffer + text.size() ||
      !std::isfinite(value)) {
    return false;
  }
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

bool ParsePanelPacket(const char* bytes, std::size_t size, PanelPacket* packet) {
  if (!bytes || !packet || !size || size > kMaximumPacketBytes ||
      std::memchr(bytes, '\0', size)) {
    return false;
  }
  *packet = {};
  std::string_view tokens[5];
  const std::size_t count = Tokens(std::string_view(bytes, size), tokens, 5);
  if (!count || count > 4) return false;
  auto coordinates = [&](PanelPacketKind kind, bool value_required,
                         float minimum_value = 0.0f,
                         float maximum_value = 0.0f) {
    const std::size_t required = value_required ? 4 : 3;
    if (count != required || !Float(tokens[1], &packet->x) ||
        !Float(tokens[2], &packet->y) || !Coordinate(packet->x) ||
        !Coordinate(packet->y)) {
      return false;
    }
    if (value_required) {
      if (!Float(tokens[3], &packet->value) ||
          packet->value < minimum_value || packet->value > maximum_value) {
        return false;
      }
    }
    packet->kind = kind;
    return true;
  };
  if (tokens[0] == "tap" || tokens[0] == "select") {
    return count == 3 ? coordinates(PanelPacketKind::kTap, false)
                      : coordinates(PanelPacketKind::kTap, true, 0.0f, 1.0f);
  }
  if (tokens[0] == "hold")
    return coordinates(PanelPacketKind::kHold, true, 0.0f, 60.0f);
  if (tokens[0] == "down") return coordinates(PanelPacketKind::kDown, false);
  if (tokens[0] == "move") return coordinates(PanelPacketKind::kMove, false);
  if (tokens[0] == "up") return coordinates(PanelPacketKind::kUp, false);
  if (tokens[0] == "pinch")
    return coordinates(PanelPacketKind::kPinch, true, 0.01f, 20.0f);
  if (tokens[0] == "cancel") {
    if (count == 1) {
      packet->kind = PanelPacketKind::kCancel;
      return true;
    }
    return coordinates(PanelPacketKind::kCancel, false);
  }
  if (count == 2 && tokens[0] == "page") {
    if (tokens[1] == "next") packet->kind = PanelPacketKind::kPageNext;
    if (tokens[1] == "previous") packet->kind = PanelPacketKind::kPagePrevious;
    return packet->kind != PanelPacketKind::kInvalid;
  }
  if (count == 2 && tokens[0] == "nav") {
    if (tokens[1] == "left") packet->kind = PanelPacketKind::kNavLeft;
    if (tokens[1] == "right") packet->kind = PanelPacketKind::kNavRight;
    if (tokens[1] == "up") packet->kind = PanelPacketKind::kNavUp;
    if (tokens[1] == "down") packet->kind = PanelPacketKind::kNavDown;
    if (tokens[1] == "activate") packet->kind = PanelPacketKind::kNavActivate;
    return packet->kind != PanelPacketKind::kInvalid;
  }
  return false;
}

PanelGesture PanelGestureArbiter::Consume(const PanelPacket& packet) {
  switch (packet.kind) {
    case PanelPacketKind::kTap: {
      if (packet.x >= 0.40f && packet.x <= 0.60f && packet.y >= 0.94f)
        return {};
      PanelGesture result = Direct(PanelGestureKind::kTap);
      result.x = packet.x;
      result.y = packet.y;
      return result;
    }
    case PanelPacketKind::kHold: {
      if (packet.x >= 0.40f && packet.x <= 0.60f && packet.y >= 0.94f)
        return {};
      PanelGesture result = Direct(PanelGestureKind::kHold);
      result.x = packet.x;
      result.y = packet.y;
      result.value = packet.value;
      return result;
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
           std::fabs(packet.y - down_y_) >= 0.025f)) {
        moved_beyond_tap_ = true;
      }
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
      if (map_capture_ && down_y_ >= 0.23f && down_y_ < 0.94f &&
          (std::fabs(dx) >= 0.025f || std::fabs(dy) >= 0.025f)) {
        PanelGesture result = Direct(PanelGestureKind::kPan);
        result.x = dx;
        result.y = dy;
        return result;
      }
      if (std::fabs(dx) >= 0.105f &&
          std::fabs(dx) > std::fabs(dy) * 1.35f) {
        return Direct(PanelGestureKind::kPage, dx < 0.0f ? 1 : -1);
      }
      if (std::fabs(dy) >= 0.055f &&
          std::fabs(dy) > std::fabs(dx) * 1.15f) {
        PanelGesture result = Direct(PanelGestureKind::kScroll);
        result.x = down_x_;
        result.y = down_y_;
        result.value = dy;
        return result;
      }
      if (std::fabs(dx) < 0.025f && std::fabs(dy) < 0.025f &&
          !moved_beyond_tap_) {
        PanelGesture result = Direct(PanelGestureKind::kTap);
        result.x = packet.x;
        result.y = packet.y;
        return result;
      }
      moved_beyond_tap_ = false;
      return {};
    }
    case PanelPacketKind::kCancel:
      Reset();
      return {};
    case PanelPacketKind::kPinch: {
      PanelGesture result = Direct(PanelGestureKind::kPinch);
      result.x = packet.x;
      result.y = packet.y;
      result.value = packet.value;
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
  moved_beyond_tap_ = false;
  down_x_ = 0.0f;
  down_y_ = 0.0f;
}

}  // namespace fomds
