#include "PageCarousel.h"

#include <algorithm>

namespace fomds {

int WrapPage(int page, int delta, int page_count) {
  if (page_count <= 0) return 0;
  int value = (page + delta) % page_count;
  return value < 0 ? value + page_count : value;
}

int CarouselPage(int selected_page, int visible_slot, int page_count) {
  return WrapPage(selected_page, std::clamp(visible_slot, 0, 2) - 1,
                  page_count);
}

int CarouselTapDelta(float normalized_x, float arrow_fraction) {
  const float edge = std::clamp(arrow_fraction, 0.0f, 0.49f);
  if (normalized_x <= edge) return -1;
  if (normalized_x >= 1.0f - edge) return 1;
  return 0;
}

}  // namespace fomds
