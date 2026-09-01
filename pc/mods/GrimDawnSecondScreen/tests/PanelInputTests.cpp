#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "PanelInput.h"

namespace {

gdds::PanelPacket Parse(const std::string& text) {
  gdds::PanelPacket packet;
  assert(gdds::ParsePanelPacket(text.data(), text.size(), &packet));
  return packet;
}

bool Reject(const std::string& text) {
  gdds::PanelPacket packet;
  return !gdds::ParsePanelPacket(text.data(), text.size(), &packet);
}

}  // namespace

int main() {
  using namespace gdds;
  assert(Parse("tap 0.2 0.3").kind == PanelPacketKind::kTap);
  assert(Parse("tap 0.2 0.3 0.1").kind == PanelPacketKind::kTap);
  assert(Parse("hold 0.2 0.3 0.75").kind == PanelPacketKind::kHold);
  assert(Parse("page next").kind == PanelPacketKind::kPageNext);
  assert(Parse("nav activate").kind == PanelPacketKind::kNavActivate);
  assert(Reject("tap 0.2 0.3 trailing"));
  assert(Reject("tap nan 0.3"));
  assert(Reject("tap 1.1 0.3"));
  assert(Reject("hold 0.2 0.3"));
  assert(Reject("page sideways"));
  const std::string embedded("tap 0.2\0 0.3", 12);
  assert(Reject(embedded));

  PanelGestureArbiter swipe;
  assert(swipe.Consume(Parse("down 0.8 0.5")).kind == PanelGestureKind::kNone);
  assert(swipe.Consume(Parse("move 0.6 0.5")).kind == PanelGestureKind::kNone);
  PanelGesture result = swipe.Consume(Parse("up 0.4 0.52"));
  assert(result.kind == PanelGestureKind::kPage && result.direction == 1);
  swipe.Consume(Parse("down 0.2 0.5"));
  result = swipe.Consume(Parse("up 0.45 0.49"));
  assert(result.kind == PanelGestureKind::kPage && result.direction == -1);
  swipe.Consume(Parse("down 0.5 0.3"));
  result = swipe.Consume(Parse("up 0.51 0.7"));
  assert(result.kind == PanelGestureKind::kScroll && result.value > 0.0f);
  swipe.Consume(Parse("down 0.3 0.3"));
  result = swipe.Consume(Parse("up 0.31 0.31"));
  assert(result.kind == PanelGestureKind::kTap);
  swipe.Consume(Parse("down 0.5 0.96"));
  result = swipe.Consume(Parse("up 0.5 0.5"));
  assert(result.kind == PanelGestureKind::kNone);
  assert(swipe.Consume(Parse("tap 0.5 0.97")).kind ==
         PanelGestureKind::kNone);
  std::cout << "PanelInputTests passed\n";
  return 0;
}
