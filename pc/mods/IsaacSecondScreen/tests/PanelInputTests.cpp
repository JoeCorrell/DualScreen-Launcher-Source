#include <cassert>
#include <cstring>
#include <iostream>

#include "PanelInput.h"

namespace {

bool Parse(const char* text, isaacds::PanelInput* input) {
  return isaacds::ParsePanelInput(text, std::strlen(text), input);
}

}  // namespace

int main() {
  using isaacds::PanelInputKind;
  isaacds::PanelInput input;

  assert(Parse("tap 0.25 0.75", &input));
  assert(input.kind == PanelInputKind::kTap);
  assert(input.x == 0.25f && input.y == 0.75f);
  assert(Parse("down 0 1", &input));
  assert(input.kind == PanelInputKind::kDown);
  assert(Parse("move 0.5 0.5 12", &input));
  assert(input.kind == PanelInputKind::kMove && input.value == 12.0f);
  assert(Parse("page next", &input));
  assert(input.kind == PanelInputKind::kPageNext);
  assert(Parse("page previous", &input));
  assert(input.kind == PanelInputKind::kPagePrevious);
  assert(Parse("nav activate", &input));
  assert(input.kind == PanelInputKind::kNavActivate);

  assert(!Parse("tap -0.01 0.5", &input));
  assert(!Parse("tap 0.5 1.01", &input));
  assert(!Parse("move nan 0.5", &input));
  assert(!Parse("page sideways", &input));
  assert(!Parse("nav escape", &input));
  assert(!Parse("unknown 0.5 0.5", &input));

  char embedded[] = {'t', 'a', 'p', ' ', '0', '\0', ' ', '0'};
  assert(!isaacds::ParsePanelInput(embedded, sizeof(embedded), &input));

  std::cout << "PanelInputTests passed\n";
  return 0;
}
