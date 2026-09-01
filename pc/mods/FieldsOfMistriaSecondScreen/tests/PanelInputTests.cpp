#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "PanelInput.h"

namespace {

fomds::PanelPacket Parse(const std::string& text) {
  fomds::PanelPacket packet;
  assert(fomds::ParsePanelPacket(text.data(), text.size(), &packet));
  return packet;
}

bool Reject(const std::string& text) {
  fomds::PanelPacket packet;
  return !fomds::ParsePanelPacket(text.data(), text.size(), &packet);
}

}  // namespace

int main() {
  using namespace fomds;
  assert(Parse("tap 0.2 0.3").kind == PanelPacketKind::kTap);
  assert(Parse("select 0.2 0.3 1.0").value == 1.0f);
  assert(Parse("hold 0.2 0.3 0.75").kind == PanelPacketKind::kHold);
  assert(Parse("pinch 0.5 0.5 1.25").kind == PanelPacketKind::kPinch);
  assert(Parse("page next").kind == PanelPacketKind::kPageNext);
  assert(Parse("nav activate").kind == PanelPacketKind::kNavActivate);
  assert(Reject("tap nan 0.3"));
  assert(Reject("tap 1.1 0.3"));
  assert(Reject("tap 0.2 0.3 trailing"));
  assert(Reject("tap 0.2 0.3 -0.01"));
  assert(Reject("tap 0.2 0.3 1.01"));
  assert(Reject("hold 0.2 0.3 -0.01"));
  assert(Reject("hold 0.2 0.3 60.01"));
  assert(Reject("pinch 0.5 0.5 0"));
  assert(Reject("pinch 0.5 0.5 -1"));
  assert(Reject("pinch 0.5 0.5 20.01"));
  assert(Reject("pinch 0.5 0.5 1e999"));
  assert(Reject("page sideways"));
  const std::string embedded("tap 0.2\0 0.3", 12);
  assert(Reject(embedded));

  PanelGestureArbiter arbiter;
  arbiter.Consume(Parse("down 0.8 0.5"));
  arbiter.Consume(Parse("move 0.6 0.5"));
  PanelGesture result = arbiter.Consume(Parse("up 0.4 0.52"));
  assert(result.kind == PanelGestureKind::kPage && result.direction == 1);
  arbiter.Consume(Parse("down 0.5 0.3"));
  result = arbiter.Consume(Parse("up 0.51 0.7"));
  assert(result.kind == PanelGestureKind::kScroll && result.value > 0.0f);
  arbiter.Consume(Parse("down 0.3 0.3"));
  arbiter.Consume(Parse("move 0.7 0.3"));
  arbiter.Consume(Parse("move 0.31 0.31"));
  assert(arbiter.Consume(Parse("up 0.31 0.31")).kind ==
         PanelGestureKind::kNone);
  arbiter.Consume(Parse("down 0.5 0.96"));
  assert(arbiter.Consume(Parse("up 0.5 0.5")).kind ==
         PanelGestureKind::kNone);
  assert(arbiter.Consume(Parse("tap 0.5 0.97")).kind ==
         PanelGestureKind::kNone);
  arbiter.Consume(Parse("down 0.8 0.5"));
  arbiter.Consume(Parse("cancel 0.8 0.5"));
  assert(arbiter.Consume(Parse("up 0.2 0.5")).kind ==
         PanelGestureKind::kNone);
  assert(arbiter.Consume(Parse("page previous")).direction == -1);
  arbiter.SetMapCapture(true);
  arbiter.Consume(Parse("down 0.7 0.5"));
  result = arbiter.Consume(Parse("up 0.3 0.6"));
  assert(result.kind == PanelGestureKind::kPan);
  assert(result.x < 0.0f && result.y > 0.0f);
  arbiter.SetMapCapture(false);
  arbiter.Consume(Parse("down 0.7 0.5"));
  result = arbiter.Consume(Parse("up 0.3 0.6"));
  assert(result.kind == PanelGestureKind::kPage);
  std::puts("PanelInputTests passed");
  return 0;
}
