#include "PageCarousel.h"

#include <algorithm>
#include <cmath>

namespace gdds {

int WrapPage(int page, int delta, int page_count) {
  if (page_count <= 0) return 0;
  const int value = (page + delta) % page_count;
  return value < 0 ? value + page_count : value;
}

int CarouselPage(int selected_page, int visible_slot, int page_count) {
  return WrapPage(selected_page, std::clamp(visible_slot, 0, 2) - 1,
                  page_count);
}

int CarouselTapDelta(float normalized_x, float arrow_fraction) {
  if (!std::isfinite(normalized_x) || !std::isfinite(arrow_fraction) ||
      normalized_x < 0.0f || normalized_x > 1.0f ||
      arrow_fraction <= 0.0f || arrow_fraction >= 0.5f)
    return 0;
  if (normalized_x < arrow_fraction) return -1;
  if (normalized_x >= 1.0f - arrow_fraction) return 1;
  const float content =
      (normalized_x - arrow_fraction) / (1.0f - 2.0f * arrow_fraction);
  return std::clamp(static_cast<int>(content * 3.0f), 0, 2) - 1;
}

}  // namespace gdds
