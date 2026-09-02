#include "PanelInput.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

#include "PanelLayout.h"

namespace wfcds {
namespace {

bool Space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string_view Trim(std::string_view value) {
  while (!value.empty() && Space(value.front())) value.remove_prefix(1);
  while (!value.empty() && Space(value.back())) value.remove_suffix(1);
  return value;
}

size_t Split(std::string_view input, std::string_view* tokens, size_t capacity) {
  size_t count = 0;
  input = Trim(input);
  while (!input.empty()) {
    if (count == capacity) return capacity + 1;
    size_t end = 0;
    while (end < input.size() && !Space(input[end])) ++end;
    tokens[count++] = input.substr(0, end);
    input.remove_prefix(end);
    input = Trim(input);
  }
  return count;
}

bool ParseCoordinate(std::string_view token, float* output) {
  if (!output || token.empty() || token.size() >= 48) return false;
  char text[48] = {};
  std::memcpy(text, token.data(), token.size());
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(text, &end);
  if (errno == ERANGE || end != text + token.size() || !std::isfinite(value) ||
      value < 0.0f || value > 1.0f)
    return false;
  *output = value;
  return true;
}

}  // namespace

bool ParsePanelInput(const char* bytes, size_t size, PanelInput* output) {
  if (!bytes || !output || size == 0 || size > 255 ||
      std::memchr(bytes, '\0', size))
    return false;
  *output = {};
  std::string_view tokens[5];
  const size_t count = Split(std::string_view(bytes, size), tokens, 5);
  if (count == 2 && tokens[0] == "page") {
    if (tokens[1] == "next") output->kind = PanelInputKind::kPageNext;
    else if (tokens[1] == "previous")
      output->kind = PanelInputKind::kPagePrevious;
    else
      return false;
    return true;
  }
  if (count == 2 && tokens[0] == "nav") {
    if (tokens[1] == "left") output->kind = PanelInputKind::kNavLeft;
    else if (tokens[1] == "right") output->kind = PanelInputKind::kNavRight;
    else if (tokens[1] == "up") output->kind = PanelInputKind::kNavUp;
    else if (tokens[1] == "down") output->kind = PanelInputKind::kNavDown;
    else if (tokens[1] == "activate")
      output->kind = PanelInputKind::kNavActivate;
    else
      return false;
    return true;
  }
  if (count != 3 && count != 4) return false;
  if (!ParseCoordinate(tokens[1], &output->x) ||
      !ParseCoordinate(tokens[2], &output->y))
    return false;
  if (tokens[0] == "tap" || tokens[0] == "select")
    output->kind = PanelInputKind::kTap;
  else if (tokens[0] == "down") output->kind = PanelInputKind::kDown;
  else if (tokens[0] == "move") output->kind = PanelInputKind::kMove;
  else if (tokens[0] == "up") output->kind = PanelInputKind::kUp;
  else if (tokens[0] == "cancel") output->kind = PanelInputKind::kCancel;
  else if (tokens[0] == "hold") output->kind = PanelInputKind::kHold;
  else return false;
  return true;
}

HitTarget HitTestPanel(PanelPage page, float x, float y,
                       uint32_t squad_count) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f ||
      y < 0.0f || y > 1.0f)
    return HitTarget::kNone;
  const int pixel_x = static_cast<int>(x * kPanelWidth);
  const int pixel_y = static_cast<int>(y * kPanelHeight);
  const auto contains = [pixel_x, pixel_y](const PanelPixelRect& rect) {
    return pixel_x >= rect.left && pixel_x < rect.right &&
           pixel_y >= rect.top && pixel_y < rect.bottom;
  };
  if (contains(kPreviousPageRect)) return HitTarget::kPreviousPage;
  if (contains(kCombatPageRect)) return HitTarget::kCombatPage;
  if (contains(kSquadPageRect)) return HitTarget::kSquadPage;
  if (contains(kObjectivesPageRect)) return HitTarget::kObjectivesPage;
  if (contains(kNextPageRect)) return HitTarget::kNextPage;
  if (page == PanelPage::kCombat) {
    if (contains(kAbility1Rect)) return HitTarget::kAbility1;
    if (contains(kAbility2Rect)) return HitTarget::kAbility2;
    if (contains(kGrenadeRect)) return HitTarget::kGrenade;
    if (contains(kTransformRect)) return HitTarget::kTransform;
    if (contains(kReloadRect)) return HitTarget::kReload;
    if (contains(kSwapRect)) return HitTarget::kSwap;
  } else if (page == PanelPage::kSquad) {
    for (uint32_t slot = 0; slot < squad_count && slot < 3; ++slot) {
      if (contains(SquadCardRect(slot)))
        return static_cast<HitTarget>(
            static_cast<uint8_t>(HitTarget::kSquad0) + slot);
    }
  }
  return HitTarget::kNone;
}

}  // namespace wfcds
