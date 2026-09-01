#pragma once

namespace fomds {

int WrapPage(int page, int delta, int page_count);
int CarouselPage(int selected_page, int visible_slot, int page_count);
int CarouselTapDelta(float normalized_x, float arrow_fraction);

}  // namespace fomds
