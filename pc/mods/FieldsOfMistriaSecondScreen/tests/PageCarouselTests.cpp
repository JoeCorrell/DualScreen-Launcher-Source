#include <cassert>
#include <cstdio>

#include "PageCarousel.h"

int main() {
  using namespace fomds;
  assert(WrapPage(0, -1, 7) == 6);
  assert(WrapPage(6, 1, 7) == 0);
  assert(WrapPage(3, 8, 7) == 4);
  assert(CarouselPage(0, 0, 7) == 6);
  assert(CarouselPage(0, 1, 7) == 0);
  assert(CarouselPage(0, 2, 7) == 1);
  assert(CarouselTapDelta(0.05f, 0.13f) == -1);
  assert(CarouselTapDelta(0.95f, 0.13f) == 1);
  assert(CarouselTapDelta(0.50f, 0.13f) == 0);
  std::puts("PageCarouselTests passed");
  return 0;
}
