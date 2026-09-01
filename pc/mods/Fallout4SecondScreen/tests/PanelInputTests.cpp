#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "PanelInput.h"

namespace {

fo4ds::PanelPacket Parse(const std::string& text) {
  fo4ds::PanelPacket packet;
  assert(fo4ds::ParsePanelPacket(text.data(), text.size(), &packet));
  return packet;
}

bool Reject(const std::string& text) {
  fo4ds::PanelPacket packet;
  return !fo4ds::ParsePanelPacket(text.data(), text.size(), &packet);
}

}  // namespace

int main() {
  using namespace fo4ds;
  assert(Parse("tap 0.2 0.3").kind == PanelPacketKind::kTap);
  assert(Parse("tap 0.2 0.3 0.1").kind == PanelPacketKind::kTap);
  assert(Parse("hold 0.2 0.3 0.75").kind == PanelPacketKind::kHold);
  assert(Parse("page next").kind == PanelPacketKind::kPageNext);
  assert(Parse("nav activate").kind == PanelPacketKind::kNavActivate);
  assert(PanelPacketCarriesPointerInput(PanelPacketKind::kTap));
  assert(PanelPacketCarriesPointerInput(PanelPacketKind::kCancel));
  assert(!PanelPacketCarriesPointerInput(PanelPacketKind::kPageNext));
  assert(!PanelPacketCarriesPointerInput(PanelPacketKind::kNavActivate));
  assert(Reject("tap 0.2 0.3 trailing"));
  assert(Reject("tap nan 0.3"));
  assert(Reject("tap 1.1 0.3"));
  assert(Reject("hold 0.2 0.3"));
  assert(Reject("page sideways"));
  const std::string embedded("tap 0.2\0 0.3", 12);
  assert(Reject(embedded));

  float mapped_x = -1.0f;
  float mapped_y = -1.0f;
  assert(MapPanelPointToContent(0.5f, 0.5f, 1600, 1080, 180, 0, 1240,
                                1080, &mapped_x, &mapped_y));
  assert(std::fabs(mapped_x - 0.5f) < 0.001f);
  assert(std::fabs(mapped_y - 0.5f) < 0.001f);
  assert(!MapPanelPointToContent(0.05f, 0.5f, 1600, 1080, 180, 0, 1240,
                                 1080, &mapped_x, &mapped_y));
  assert(MapPanelPointToContent(1.0f, 1.0f, 1240, 1080, 0, 0, 1240,
                                1080, &mapped_x, &mapped_y));
  assert(std::fabs(mapped_x - 1.0f) < 0.001f &&
         std::fabs(mapped_y - 1.0f) < 0.001f);
  assert(!MapPanelPointToContent(0.5f, 0.5f, 100, 100, 50, 0, 60, 100,
                                 &mapped_x, &mapped_y));

  const PanelViewport wide_viewport = {1600, 1080, 180, 0, 1240, 1080};
  PanelPacketMapper mapper;
  PanelPacket mapped;
  PanelGestureArbiter mapped_gestures;
  assert(!mapper.Map(Parse("down 0.05 0.5"), wide_viewport, &mapped));
  assert(!mapper.Map(Parse("move 0.01 0.5"), wide_viewport, &mapped));
  assert(!mapper.Map(Parse("tap 0.5 0.5"), wide_viewport, &mapped));
  assert(!mapper.Map(Parse("up 0.2 0.5"), wide_viewport, &mapped));
  // A swipe wholly in the left black bar never reaches gesture arbitration.
  assert(mapped_gestures.Consume(mapped).kind == PanelGestureKind::kNone);

  assert(mapper.Map(Parse("down 0.8 0.5"), wide_viewport, &mapped));
  assert(mapped_gestures.Consume(mapped).kind == PanelGestureKind::kNone);
  assert(mapper.Map(Parse("move 0.6 0.5"), wide_viewport, &mapped));
  assert(mapped_gestures.Consume(mapped).kind == PanelGestureKind::kNone);
  assert(mapper.Map(Parse("up 0.4 0.5"), wide_viewport, &mapped));
  PanelGesture mapped_swipe = mapped_gestures.Consume(mapped);
  assert(mapped_swipe.kind == PanelGestureKind::kPage &&
         mapped_swipe.direction == 1);

  PanelGestureArbiter swipe;
  assert(swipe.Consume(Parse("down 0.8 0.5")).kind == PanelGestureKind::kNone);
  assert(swipe.Consume(Parse("move 0.6 0.5")).kind == PanelGestureKind::kNone);
  PanelGesture result = swipe.Consume(Parse("up 0.4 0.52"));
  assert(result.kind == PanelGestureKind::kPage && result.direction == 1);
  assert(std::fabs(result.x - 0.8f) < 0.001f);
  assert(std::fabs(result.delta_x + 0.4f) < 0.001f);

  swipe.Consume(Parse("down 0.2 0.5"));
  result = swipe.Consume(Parse("up 0.45 0.49"));
  assert(result.kind == PanelGestureKind::kPage && result.direction == -1);

  swipe.Consume(Parse("down 0.5 0.3"));
  result = swipe.Consume(Parse("up 0.51 0.7"));
  assert(result.kind == PanelGestureKind::kScroll && result.value > 0.0f);
  assert(std::fabs(result.x - 0.5f) < 0.001f);
  assert(std::fabs(result.y - 0.3f) < 0.001f);
  assert(std::fabs(result.delta_y - 0.4f) < 0.001f);

  swipe.Consume(Parse("down 0.5 0.5"));
  result = swipe.Consume(Parse("up 0.58 0.57"));
  assert(result.kind == PanelGestureKind::kDrag);
  assert(std::fabs(result.delta_x - 0.08f) < 0.001f);
  assert(std::fabs(result.delta_y - 0.07f) < 0.001f);

  swipe.Consume(Parse("down 0.3 0.3"));
  result = swipe.Consume(Parse("up 0.31 0.31"));
  assert(result.kind == PanelGestureKind::kTap);

  // A real drag can loop back near its origin.  It must remain consumed as a
  // drag instead of activating whichever control happens to be under release.
  swipe.Consume(Parse("down 0.3 0.3"));
  swipe.Consume(Parse("move 0.7 0.3"));
  swipe.Consume(Parse("move 0.31 0.31"));
  result = swipe.Consume(Parse("up 0.31 0.31"));
  assert(result.kind == PanelGestureKind::kNone);

  // Tiny pointer jitter stays a tap and starts cleanly after the rejected drag.
  swipe.Consume(Parse("down 0.3 0.3"));
  swipe.Consume(Parse("move 0.315 0.31"));
  result = swipe.Consume(Parse("up 0.31 0.31"));
  assert(result.kind == PanelGestureKind::kTap);

  swipe.Consume(Parse("down 0.5 0.96"));
  result = swipe.Consume(Parse("up 0.5 0.5"));
  assert(result.kind == PanelGestureKind::kNone);
  assert(swipe.Consume(Parse("tap 0.5 0.97")).kind ==
         PanelGestureKind::kNone);

  swipe.Consume(Parse("down 0.8 0.5"));
  swipe.Consume(Parse("cancel 0.8 0.5"));
  result = swipe.Consume(Parse("up 0.2 0.5"));
  assert(result.kind == PanelGestureKind::kNone);
  assert(swipe.Consume(Parse("page previous")).direction == -1);

  std::cout << "PanelInputTests passed\n";
  return 0;
}
