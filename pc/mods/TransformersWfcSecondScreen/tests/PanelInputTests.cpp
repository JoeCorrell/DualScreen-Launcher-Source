#include <cassert>
#include <cstring>

#include "PanelInput.h"

int main() {
  using namespace wfcds;
  PanelInput input;
  const char tap[] = "tap 0.50 0.90";
  assert(ParsePanelInput(tap, std::strlen(tap), &input));
  assert(input.kind == PanelInputKind::kTap);
  assert(HitTestPanel(PanelPage::kCombat, input.x, input.y, 3) ==
         HitTarget::kTransform);

  const char page[] = "page next";
  assert(ParsePanelInput(page, std::strlen(page), &input));
  assert(input.kind == PanelInputKind::kPageNext);
  assert(HitTestPanel(PanelPage::kCombat, 0.20f, 0.04f, 3) ==
         HitTarget::kCombatPage);
  assert(HitTestPanel(PanelPage::kSquad, 0.50f, 0.50f, 3) ==
         HitTarget::kSquad1);
  assert(HitTestPanel(PanelPage::kSquad, 0.90f, 0.50f, 2) ==
         HitTarget::kNone);
  assert(HitTestPanel(PanelPage::kCombat, 63.0f / 1240.0f,
                      49.0f / 1080.0f, 3) == HitTarget::kPreviousPage);
  assert(HitTestPanel(PanelPage::kCombat, 274.0f / 1240.0f,
                      49.0f / 1080.0f, 3) == HitTarget::kCombatPage);
  assert(HitTestPanel(PanelPage::kCombat, 587.0f / 1240.0f,
                      49.0f / 1080.0f, 3) == HitTarget::kSquadPage);
  assert(HitTestPanel(PanelPage::kCombat, 933.0f / 1240.0f,
                      49.0f / 1080.0f, 3) == HitTarget::kObjectivesPage);
  assert(HitTestPanel(PanelPage::kCombat, 1177.0f / 1240.0f,
                      49.0f / 1080.0f, 3) == HitTarget::kNextPage);
  assert(HitTestPanel(PanelPage::kCombat, 636.0f / 1240.0f,
                      370.0f / 1080.0f, 3) == HitTarget::kAbility1);
  assert(HitTestPanel(PanelPage::kCombat, 636.0f / 1240.0f,
                      682.0f / 1080.0f, 3) == HitTarget::kAbility2);
  assert(HitTestPanel(PanelPage::kCombat, 123.0f / 1240.0f,
                      955.0f / 1080.0f, 3) == HitTarget::kGrenade);
  // Objective range is live information, not an action button.
  assert(HitTestPanel(PanelPage::kCombat, 354.0f / 1240.0f,
                      955.0f / 1080.0f, 3) == HitTarget::kNone);
  assert(HitTestPanel(PanelPage::kCombat, 620.0f / 1240.0f,
                      955.0f / 1080.0f, 3) == HitTarget::kTransform);
  assert(HitTestPanel(PanelPage::kCombat, 885.0f / 1240.0f,
                      955.0f / 1080.0f, 3) == HitTarget::kReload);
  assert(HitTestPanel(PanelPage::kCombat, 1116.0f / 1240.0f,
                      955.0f / 1080.0f, 3) == HitTarget::kSwap);
  assert(HitTestPanel(PanelPage::kSquad, 210.0f / 1240.0f,
                      600.0f / 1080.0f, 3) == HitTarget::kSquad0);
  assert(HitTestPanel(PanelPage::kSquad, 617.0f / 1240.0f,
                      600.0f / 1080.0f, 3) == HitTarget::kSquad1);
  assert(HitTestPanel(PanelPage::kSquad, 1024.0f / 1240.0f,
                      600.0f / 1080.0f, 3) == HitTarget::kSquad2);
  assert(HitTestPanel(PanelPage::kObjectives, 620.0f / 1240.0f,
                      600.0f / 1080.0f, 3) == HitTarget::kNone);
  const char hold[] = "hold 0.50 0.90";
  assert(ParsePanelInput(hold, std::strlen(hold), &input));
  assert(input.kind == PanelInputKind::kHold);
  const char nav_up[] = "nav up";
  assert(ParsePanelInput(nav_up, std::strlen(nav_up), &input));
  assert(input.kind == PanelInputKind::kNavUp);
  const char nav_down[] = "nav down";
  assert(ParsePanelInput(nav_down, std::strlen(nav_down), &input));
  assert(input.kind == PanelInputKind::kNavDown);
  const char nav_activate[] = "nav activate";
  assert(ParsePanelInput(nav_activate, std::strlen(nav_activate), &input));
  assert(input.kind == PanelInputKind::kNavActivate);
  // Exact visible panel bounds are disjoint. Gutters must never activate the
  // adjacent panel, and the full bottom row/card height remains clickable.
  assert(HitTestPanel(PanelPage::kCombat, 0.50f, 520.0f / 1080.0f, 3) ==
         HitTarget::kNone);
  assert(HitTestPanel(PanelPage::kCombat, 500.0f / 1240.0f,
                      533.0f / 1080.0f, 3) == HitTarget::kAbility2);
  assert(HitTestPanel(PanelPage::kCombat, 500.0f / 1240.0f,
                      847.0f / 1080.0f, 3) == HitTarget::kTransform);
  assert(HitTestPanel(PanelPage::kSquad, 20.0f / 1240.0f,
                      1009.0f / 1080.0f, 3) == HitTarget::kSquad0);
  assert(HitTestPanel(PanelPage::kSquad, 410.0f / 1240.0f,
                      500.0f / 1080.0f, 3) == HitTarget::kNone);
  assert(!ParsePanelInput("tap 1.1 0.4", 11, &input));
  assert(!ParsePanelInput("garbage", 7, &input));
  return 0;
}
