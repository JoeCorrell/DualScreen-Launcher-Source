// The bottom-screen interface.
//
// Designed for 1240x1080 and nothing else. That is an unusual shape -- taller
// than it is wide -- and it drives most of the layout: a horizontal tab strip
// across the top, one page filling the rest, and a status strip pinned to the
// bottom that never changes between pages so the vitals are readable no matter
// what is open.
//
// Two rules the pages follow, both of which come from this being a *second*
// screen rather than a menu:
//
//   Nothing here pauses the game. A long-press context sheet may temporarily
//   own bottom-panel touches, but the simulation on the main display keeps
//   running and the sheet can always be dismissed with one tap.
//
//   A value that could not be read says so. Pages never substitute a zero, a
//   dash that looks like a value, or the last number they saw. `Value::valid`
//   is carried all the way from the reflection call to the draw call, and the
//   unavailable treatment is visually distinct from empty -- see Canvas::Bar.
//
// Touch targets are sized for a thumb on a handheld held in both hands: the
// tab strip is 96px tall and no interactive row is under 64px.

#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include "Canvas.h"

namespace dis {

class GameState;
class Game;

// ---- the palette ----------------------------------------------------------
// Dead Island's own register: sun-bleached bone and blood red over near-black.
// Dark crimson surfaces separate sections; the brighter red remains reserved
// for selection, progression and danger so interactive state stays legible.
namespace theme {

inline constexpr Color kBackground   {  5,  5,  6};
inline constexpr Color kSurface      { 14, 10, 11};
inline constexpr Color kSurfaceHigh  { 28, 14, 16};
inline constexpr Color kEdge         { 76, 30, 32};
inline constexpr Color kEdgeBright   {132, 46, 49};

inline constexpr Color kText         {236,235,232};
inline constexpr Color kTextDim      {180,180,177};
inline constexpr Color kTextFaint    { 91, 93, 94};

inline constexpr Color kAccent       {218, 25, 30};   // Dead Island blood red
inline constexpr Color kAccentDeep   {111,  8, 12};
inline constexpr Color kAccentMuted  { 55, 13, 16};
inline constexpr Color kBlood        {218, 25, 30};
inline constexpr Color kStamina      {132,171, 54};
inline constexpr Color kFury         {226, 91,  0};
inline constexpr Color kGood         {114,156, 55};

inline constexpr int kScreenW = 1240;
inline constexpr int kScreenH = 1080;
inline constexpr int kTabH    = 116;
inline constexpr int kStatusH = 0;
inline constexpr int kMargin  = 20;
inline constexpr int kGap     = 14;
inline constexpr int kRadius  = 2;

inline const FontSpec kFontHuge   {58, FW_BOLD};
inline const FontSpec kFontBig    {36, FW_SEMIBOLD};
inline const FontSpec kFontTitle  {26, FW_SEMIBOLD};
inline const FontSpec kFontBody   {22, FW_NORMAL};
inline const FontSpec kFontSmall  {18, FW_NORMAL};
inline const FontSpec kFontLabel  {16, FW_SEMIBOLD};

}  // namespace theme

enum class Page {
  kStatus = 0,
  kInventory = 1,
  kSkills = 2,
  kMap = 3,
  kCount = 4,

  // Kept as internal diagnostic renderers for the desktop preview and future
  // adapters; they are deliberately not part of the player-facing tab cycle.
  kGear = 5,
  kMissions = 6,
  kData = 7,
};

enum class UiAction {
  kNone,
  kInventoryItem,
  kInventoryEquip,
  kInventoryDrop,
  kInventoryCompare,
  kInventoryInspect,
  kInventoryAssignQuickSlot,
  kInventoryPopupCancel,
  kGearSlot,
  kMission,
  kMissionTrack,
  kMissionDetails,
  kSkill,
  kSkillLearn,
  kSkillView,
  kSkillConfirmYes,
  kSkillConfirmNo,
  kMapZoomIn,
  kMapZoomOut,
  kMapCenter,
  kScrollUp,
  kScrollDown,
};

struct UiTarget {
  Box bounds;
  UiAction action = UiAction::kNone;
  int index = -1;
  // Optional second semantic index. Inventory quick-slot assignment uses the
  // primary index for the exact inventory row and this value for the live
  // destination slot, keeping both under the same snapshot-revision guard.
  int secondary_index = -1;
  bool enabled = true;
  Page page = Page::kStatus;
  uint64_t source_revision = 0;
  bool modal = false;
};

const char* PageTitle(Page page);

// ---- shared widgets -------------------------------------------------------
// Kept here rather than in each page so the interface reads as one thing. A
// page that needs a surface reaches for Card; it does not invent a rectangle.

// A titled surface. Returns the rect inside it that content may use.
Box Card(Canvas& canvas, const Box& bounds, const std::string& title);

// A labelled meter with its numbers spelled out on the label line, so the bar
// is a shape to glance at rather than the only way to read the value.
void MeterRow(Canvas& canvas, const Box& bounds, const std::string& label,
              double value, double max, bool valid, const Color& fill);

// A large number with a caption under it. `valid` false draws the unavailable
// treatment instead of the number.
void StatTile(Canvas& canvas, const Box& bounds, const std::string& caption,
              const std::string& value, bool valid, const Color& accent);

// Explains why a pane is empty, in place, without pretending to hold data.
void EmptyState(Canvas& canvas, const Box& bounds, const std::string& title,
                const std::string& detail);

// ---- the shell ------------------------------------------------------------
class Interface {
 public:
  // Sized to the panel, not to the design constants: the layout is authored
  // for 1240x1080 but reads its bounds from the canvas, so a differently sized
  // second monitor gets a stretched-to-fit layout rather than a clipped one.
  bool Create(int width, int height);
  void Destroy();
  bool ready() const { return canvas_.ready(); }
  Canvas& canvas() { return canvas_; }

  // Draws the whole screen for the current state. Returns false when nothing
  // changed and the previous frame is still valid, so the caller can skip the
  // upload -- a 5MB transfer per frame for a HUD that did not move is the one
  // real cost of rasterising on the CPU, and this is what avoids it.
  bool Render(Game& game, GameState& state);

  // Touch, in panel pixels. Returns true when the interface consumed it.
  bool OnTouchDown(int x, int y);
  bool OnTouchMove(int x, int y);
  bool OnTouchUp(int x, int y);
  bool OnHold(int x, int y);
  bool OnPinch(int x, int y, float factor);

  // Window messages are pumped from Panel::Present. Touch therefore queues a
  // semantic action; native game callbacks are consumed at the beginning of a
  // later frame, after level lifecycle state has been reconciled.
  void ProcessPendingActions();
  void CancelPendingActions();

  // Controller page cycling, for when the panel is not being touched.
  void NextPage();
  void PreviousPage();

  Page page() const { return page_; }
  void SetPage(Page page);

  // Page renderers register only the controls they actually drew. Touch never
  // guesses native coordinates and never escapes to the game's input layer.
  void AddTarget(const Box& bounds, UiAction action, int index = -1,
                 bool enabled = true);
  void AddModalTarget(const Box& bounds, UiAction action, int index = -1,
                      bool enabled = true, int secondary_index = -1);
  bool TargetPressed(UiAction action, int index = -1,
                     int secondary_index = -1) const;
  int ScrollOffset(Page page) const;
  void ClampScroll(Page page, int maximum);
  // Registers the exact inventory-list viewport for gesture scrolling. The
  // row stride is also used to convert a drag into deterministic whole-row
  // movement, while taps and stationary long presses keep their semantics.
  void SetInventoryListViewport(const Box& bounds, int row_stride,
                                int maximum);
  float MapZoom() const { return map_zoom_; }
  float MapCenterU() const { return map_center_u_; }
  float MapCenterV() const { return map_center_v_; }
  void SetMapViewport(const Box& bounds, float player_u, float player_v,
                      const std::string& level_name);
  bool InventoryPopupOpen() const { return inventory_popup_open_; }
  int InventoryPopupIndex() const { return inventory_popup_index_; }
  void SetInventoryPopupBounds(const Box& bounds) {
    inventory_popup_bounds_ = bounds;
  }

 private:
  void DrawTabs(Canvas& canvas);
  void DrawStatus(Canvas& canvas, Game& game, GameState& state);
  void DrawPage(Canvas& canvas, const Box& body, Game& game, GameState& state);

  Box TabRect(int index) const;
  bool InvokeTarget(const UiTarget& target);
  bool QueueTarget(const UiTarget& target);
  void UpdateLongPress();
  bool OpenInventoryPopup(const UiTarget& target);
  void CloseInventoryPopup();
  void ResetPressedTarget();
  bool InventoryPopupItemStillMatches() const;
  void ClampMapCenter();

  Canvas canvas_;
  Page page_ = Page::kStatus;
  int pressed_tab_ = -1;
  bool has_pressed_target_ = false;
  UiTarget pressed_target_{};
  uint64_t target_generation_ = 0;
  uint64_t pressed_generation_ = 0;
  UiAction pressed_action_ = UiAction::kNone;
  int pressed_action_index_ = -1;
  uint64_t press_started_ms_ = 0;
  bool long_press_fired_ = false;
  bool consume_release_ = false;
  std::vector<UiTarget> targets_;
  std::mutex pending_lock_;
  std::deque<UiTarget> pending_actions_;
  int inventory_offset_ = 0;
  Box inventory_list_viewport_{};
  int inventory_row_stride_ = 1;
  int inventory_scroll_max_ = 0;
  bool inventory_scroll_armed_ = false;
  bool inventory_scrolling_ = false;
  int inventory_scroll_start_y_ = 0;
  int inventory_scroll_start_offset_ = 0;
  int missions_offset_ = 0;
  int skills_offset_ = 0;
  float map_zoom_ = 1.0f;
  Box map_viewport_{};
  float map_center_u_ = 0.5f;
  float map_center_v_ = 0.5f;
  float map_player_u_ = 0.5f;
  float map_player_v_ = 0.5f;
  bool map_center_valid_ = false;
  bool map_follow_player_ = true;
  bool map_dragging_ = false;
  int map_drag_x_ = 0;
  int map_drag_y_ = 0;
  std::string map_level_name_;
  bool inventory_popup_open_ = false;
  int inventory_popup_index_ = -1;
  std::string inventory_popup_item_key_;
  Box inventory_popup_bounds_{};
  bool dirty_ = true;

  // What the last frame was drawn from. Render compares against this and skips
  // when nothing a viewer could see has changed.
  struct Signature {
    int page = -1;
    long long health = -1, max_health = -1, stamina = -1, max_stamina = -1;
    long long fury = -1, max_fury = -1, level = -1, money = -1;
    long long experience = -1, exp_to_next = -1, skill_points = -1;
    long long inventory_slots = -1, inventory_max = -1;
    long long quick_slots = -1, quick_max = -1;
    uint64_t page_revision = 0;
    bool bound = false;
    bool searching = false;
    bool operator==(const Signature& o) const {
      return page == o.page && health == o.health && max_health == o.max_health &&
             stamina == o.stamina && max_stamina == o.max_stamina &&
             fury == o.fury && max_fury == o.max_fury && level == o.level &&
             money == o.money && experience == o.experience &&
             exp_to_next == o.exp_to_next && skill_points == o.skill_points &&
             inventory_slots == o.inventory_slots &&
             inventory_max == o.inventory_max && quick_slots == o.quick_slots &&
             quick_max == o.quick_max && page_revision == o.page_revision &&
             bound == o.bound &&
             searching == o.searching;
    }
  };
  Signature last_;
};

Interface& TheInterface();

}  // namespace dis
