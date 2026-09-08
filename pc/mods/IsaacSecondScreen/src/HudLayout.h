#pragma once
#include <array>
#include <windows.h>

namespace isaacds {
// Primary frames retain their size. Only occupied secondary storage expands
// the group; its frame and art are 65% of the primary dimensions.
inline std::array<RECT, 2> HudSlots(int left, int right, bool secondary) {
  constexpr int size = 144;
  constexpr int small = size * 65 / 100;
  constexpr int gap = 12;
  const int width = secondary ? size + gap + small : size;
  const int x = (left + right - width) / 2;
  const int y = 126;
  return {{{x, y, x + size, y + size},
           {x + size + gap, y + size - small,
            x + width, y + size}}};
}
}
