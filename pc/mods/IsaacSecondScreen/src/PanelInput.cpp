#include "PanelInput.h"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace isaacds {
namespace {

bool Space(char value) {
  return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

std::string_view Trim(std::string_view value) {
  while (!value.empty() && Space(value.front())) value.remove_prefix(1);
  while (!value.empty() && Space(value.back())) value.remove_suffix(1);
  return value;
}

size_t Split(std::string_view input, std::string_view* output,
             size_t capacity) {
  size_t count = 0;
  input = Trim(input);
  while (!input.empty()) {
    if (count == capacity) return capacity + 1;
    size_t end = 0;
    while (end < input.size() && !Space(input[end])) ++end;
    output[count++] = input.substr(0, end);
    input.remove_prefix(end);
    input = Trim(input);
  }
  return count;
}

bool Float(std::string_view token, float* output) {
  if (!output || token.empty() || token.size() >= 48) return false;
  char text[48] = {};
  std::memcpy(text, token.data(), token.size());
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(text, &end);
  if (errno == ERANGE || end != text + token.size() ||
      !std::isfinite(value))
    return false;
  *output = value;
  return true;
}


bool Coordinate(float value) { return value >= 0.0f && value <= 1.0f; }

}  // namespace

bool ParsePanelInput(const char* bytes, size_t size, PanelInput* output) {
  if (!bytes || !output || !size || size > 255 ||
      std::memchr(bytes, '\0', size))
    return false;
  *output = {};
  std::string_view tokens[5];
  const size_t count = Split(std::string_view(bytes, size), tokens, 5);
  if (!count || count > 4) return false;
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
  if (!Float(tokens[1], &output->x) || !Float(tokens[2], &output->y) ||
      !Coordinate(output->x) || !Coordinate(output->y))
    return false;
  if (count == 4 && (!Float(tokens[3], &output->value) || output->value < 0.0f))
    return false;
  if (tokens[0] == "tap" || tokens[0] == "select")
    output->kind = PanelInputKind::kTap;
  else if (tokens[0] == "hold")
    output->kind = PanelInputKind::kHold;
  else if (tokens[0] == "down")
    output->kind = PanelInputKind::kDown;
  else if (tokens[0] == "move")
    output->kind = PanelInputKind::kMove;
  else if (tokens[0] == "up")
    output->kind = PanelInputKind::kUp;
  else if (tokens[0] == "cancel")
    output->kind = PanelInputKind::kCancel;
  else
    return false;
  return true;
}

}  // namespace isaacds
