#include <cassert>
#include <iostream>

#include "PageCarousel.h"

int main() {
  using namespace gdds;
  assert(WrapPage(0, -1, 8) == 7);
  assert(WrapPage(7, 1, 8) == 0);
  assert(CarouselPage(0, 0, 8) == 7);
  assert(CarouselPage(0, 1, 8) == 0);
  assert(CarouselPage(0, 2, 8) == 1);

  constexpr float arrows = 0.08f;
  assert(CarouselTapDelta(0.01f, arrows) == -1);
  assert(CarouselTapDelta(0.99f, arrows) == 1);
  assert(CarouselTapDelta(0.15f, arrows) == -1);
  assert(CarouselTapDelta(0.50f, arrows) == 0);
  assert(CarouselTapDelta(0.85f, arrows) == 1);
  assert(CarouselTapDelta(-0.1f, arrows) == 0);
  std::cout << "PageCarouselTests passed\n";
  return 0;
}
