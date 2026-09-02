#pragma once

namespace wfcds {

constexpr int kPanelWidth = 1240;
constexpr int kPanelHeight = 1080;

struct PanelPixelRect {
  int left;
  int top;
  int right;
  int bottom;
};

constexpr PanelPixelRect kPreviousPageRect = {14, 14, 112, 84};
constexpr PanelPixelRect kCombatPageRect = {124, 14, 425, 84};
constexpr PanelPixelRect kSquadPageRect = {437, 14, 738, 84};
constexpr PanelPixelRect kObjectivesPageRect = {750, 14, 1116, 84};
constexpr PanelPixelRect kNextPageRect = {1128, 14, 1226, 84};

constexpr PanelPixelRect kAbility1Rect = {488, 220, 785, 520};
constexpr PanelPixelRect kAbility2Rect = {488, 532, 785, 832};
constexpr PanelPixelRect kGrenadeRect = {14, 846, 233, 1065};
constexpr PanelPixelRect kObjectiveRangeRect = {245, 846, 464, 1065};
constexpr PanelPixelRect kTransformRect = {476, 846, 764, 1065};
constexpr PanelPixelRect kReloadRect = {776, 846, 995, 1065};
constexpr PanelPixelRect kSwapRect = {1007, 846, 1226, 1065};

constexpr PanelPixelRect SquadCardRect(unsigned slot) {
  const int left = 14 + static_cast<int>(slot) * 407;
  return {left, 276, left + 392, 1010};
}

}  // namespace wfcds
