#include "Ui.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "Game.h"
#include "GameState.h"
#include "NativeMap.h"
#include "NativeUi.h"
#include "Rtti.h"

namespace dis {
namespace {

// The proxy is a process-lifetime dependency. Leaking the interface avoids its
// Canvas destructor releasing D3D resources from CRT teardown under the loader
// lock; the OS reclaims them on process exit.
Interface& g_interface = *new Interface();

std::string Format(const char* fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  return std::string(buffer);
}

// Whole numbers without a decimal point, because a health value of "87" reads
// faster than "87.0" and this is a thing being glanced at mid-fight.
std::string Whole(double v) { return Format("%.0f", v); }

long long Quantise(const Value& v) {
  // Signature values are compared to decide whether to repaint. Rounding to a
  // tenth means a health value drifting in the third decimal does not force a
  // 5MB upload every frame, while any change a viewer could see still does.
  return v.valid ? static_cast<long long>(v.v * 10.0) : -1;
}

uint64_t NativeRevisionForPage(Page page) {
  const NativeUi& native = TheNativeUi();
  switch (page) {
    case Page::kInventory:
    case Page::kGear:
      return native.inventory_revision();
    case Page::kMissions:
      return native.missions_revision();
    case Page::kSkills:
      return native.skills_revision();
    default:
      return 0;
  }
}

uint64_t SteadyMilliseconds() {
  return static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string InventoryItemKey(const NativeUiRow& row, int index) {
  std::string key = row.id;
  key.push_back('|');
  key += row.icon_texture;
  key.push_back('|');
  if (!row.text.empty()) key += row.text.front();
  key.push_back('|');
  key += std::to_string(row.native_ordinal >= 0 ? row.native_ordinal : index);
  return key;
}

void MapViewportSpan(const Box& viewport, float zoom,
                     float* span_u, float* span_v) {
  if (!span_u || !span_v || viewport.w <= 0 || viewport.h <= 0) return;
  const float safe_zoom = std::clamp(zoom, 1.0f, 4.0f);
  const float aspect = static_cast<float>(viewport.w) /
                       static_cast<float>(viewport.h);
  *span_u = (aspect < 1.0f ? aspect : 1.0f) / safe_zoom;
  *span_v = (aspect >= 1.0f ? 1.0f / aspect : 1.0f) / safe_zoom;
}

void DrawTabIcon(Canvas& canvas, Page page, const Box& box,
                 const Color& color) {
  const int cx = box.x + box.w / 2;
  const int cy = box.y + box.h / 2;
  switch (page) {
    case Page::kStatus:
      canvas.FillCircle(cx - 17, cy - 14, 6, color);
      canvas.FillRoundRect(Box(cx - 24, cy - 6, 14, 25), 5, color);
      canvas.FillRect(Box(cx - 2, cy + 6, 7, 13), color);
      canvas.FillRect(Box(cx + 9, cy - 2, 7, 21), color);
      canvas.FillRect(Box(cx + 20, cy - 12, 7, 31), color);
      break;
    case Page::kInventory:
      canvas.StrokeRoundRect(Box(cx - 22, cy - 20, 44, 42), 7, 4, color);
      canvas.StrokeRoundRect(Box(cx - 12, cy - 27, 24, 12), 5, 4, color);
      canvas.Line(cx - 12, cy - 5, cx - 12, cy + 12, 3, color);
      canvas.Line(cx + 12, cy - 5, cx + 12, cy + 12, 3, color);
      canvas.Line(cx - 8, cy + 7, cx + 8, cy + 7, 3, color);
      break;
    case Page::kSkills:
      for (int i = 0; i < 4; ++i)
        canvas.FillRoundRect(Box(cx - 19 + i * 10, cy - 18, 8, 18), 3, color);
      canvas.FillRoundRect(Box(cx - 19, cy - 2, 38, 23), 5, color);
      canvas.Line(cx - 19, cy + 2, cx - 29, cy - 8, 7, color);
      canvas.Line(cx + 4, cy + 18, cx + 4, cy + 28, 7, color);
      break;
    case Page::kMap:
      canvas.Line(cx - 26, cy - 20, cx - 9, cy - 14, 3, color);
      canvas.Line(cx - 9, cy - 14, cx + 9, cy - 21, 3, color);
      canvas.Line(cx + 9, cy - 21, cx + 26, cy - 15, 3, color);
      canvas.Line(cx - 26, cy - 20, cx - 26, cy + 23, 3, color);
      canvas.Line(cx - 9, cy - 14, cx - 9, cy + 28, 3, color);
      canvas.Line(cx + 9, cy - 21, cx + 9, cy + 21, 3, color);
      canvas.Line(cx + 26, cy - 15, cx + 26, cy + 27, 3, color);
      canvas.Line(cx - 26, cy + 23, cx - 9, cy + 28, 3, color);
      canvas.Line(cx - 9, cy + 28, cx + 9, cy + 21, 3, color);
      canvas.Line(cx + 9, cy + 21, cx + 26, cy + 27, 3, color);
      canvas.StrokeCircle(cx + 5, cy - 2, 7, 3, color);
      canvas.Line(cx + 5, cy + 5, cx + 5, cy + 14, 3, color);
      break;
    default:
      break;
  }
}

}  // namespace

const char* PageTitle(Page page) {
  switch (page) {
    case Page::kStatus:    return "STATS";
    case Page::kInventory: return "INVENTORY";
    case Page::kGear:      return "GEAR";
    case Page::kMap:       return "MAP";
    case Page::kMissions:  return "MISSIONS";
    case Page::kSkills:    return "SKILLS";
    case Page::kData:      return "DATA";
    default:               return "";
  }
}

Interface& TheInterface() { return g_interface; }

// ------------------------------------------------------------------- widgets
Box Card(Canvas& canvas, const Box& bounds, const std::string& title) {
  canvas.FillRoundRect(bounds, theme::kRadius, theme::kSurface);
  canvas.StrokeRoundRect(bounds, theme::kRadius, 1, theme::kEdge);
  if (bounds.w > 12 && bounds.h > 12) {
    canvas.StrokeRect(bounds.Inset(4), 1, Color(25, 27, 28));
    canvas.Line(bounds.x + 5, bounds.y + 5, bounds.x + 27, bounds.y + 5,
                1, theme::kEdgeBright);
    canvas.Line(bounds.x + 5, bounds.y + 5, bounds.x + 5, bounds.y + 27,
                1, theme::kEdgeBright);
  }

  if (title.empty()) return bounds.Inset(18);

  canvas.VGradient(Box(bounds.x + 2, bounds.y + 2, bounds.w - 4, 43),
                   theme::kAccentMuted, theme::kSurfaceHigh);
  canvas.FillRect(Box(bounds.x + 2, bounds.y + 2, 5, 43),
                  theme::kAccent);

  const Box header(bounds.x + 18, bounds.y + 14, bounds.w - 36, 26);
  canvas.Text(header, title, theme::kFontLabel, theme::kTextDim, Align::kLeft);
  // A short accent rule under the heading rather than a full-width divider.
  // The full-width line was tried and it cut every card in half.
  canvas.FillRect(Box(bounds.x + 18, header.bottom() + 6, 34, 2), theme::kAccent);
  return Box(bounds.x + 18, header.bottom() + 18, bounds.w - 36,
              bounds.bottom() - header.bottom() - 32);
}

void MeterRow(Canvas& canvas, const Box& bounds, const std::string& label,
              double value, double max, bool valid, const Color& fill) {
  const Box label_row(bounds.x, bounds.y, bounds.w, 24);
  canvas.Text(label_row, label, theme::kFontLabel, theme::kTextDim, Align::kLeft);

  if (valid) {
    const std::string reading = max > 0 ? Whole(value) + " / " + Whole(max)
                                        : Whole(value);
    canvas.Text(label_row, reading, theme::kFontBody, theme::kText, Align::kRight);
  } else {
    canvas.Text(label_row, "unavailable", theme::kFontSmall, theme::kTextFaint,
                Align::kRight);
  }

  const Box bar(bounds.x, label_row.bottom() + 8, bounds.w, 22);
  const float ratio = valid && max > 0 ? static_cast<float>(value / max) : -1.0f;
  canvas.Bar(bar, ratio, fill, theme::kSurfaceHigh);
}

void StatTile(Canvas& canvas, const Box& bounds, const std::string& caption,
              const std::string& value, bool valid, const Color& accent) {
  canvas.FillRoundRect(bounds, theme::kRadius, theme::kSurfaceHigh);
  canvas.FillRect(Box(bounds.x, bounds.y, 3, bounds.h), accent);

  const Box caption_row(bounds.x + 16, bounds.y + 12, bounds.w - 32, 20);
  canvas.Text(caption_row, caption, theme::kFontLabel, theme::kTextDim, Align::kLeft);

  const Box value_row(bounds.x + 16, caption_row.bottom() + 4, bounds.w - 32,
                       bounds.bottom() - caption_row.bottom() - 16);
  if (valid) {
    canvas.Text(value_row, value, theme::kFontBig, theme::kText, Align::kLeft);
  } else {
    canvas.Text(value_row, "--", theme::kFontBig, theme::kTextFaint, Align::kLeft);
  }
}

void EmptyState(Canvas& canvas, const Box& bounds, const std::string& title,
                const std::string& detail) {
  const int height = 120;
  const Box box(bounds.x, bounds.y + (bounds.h - height) / 2, bounds.w, height);
  canvas.Text(Box(box.x, box.y, box.w, 40), title, theme::kFontTitle,
              theme::kTextDim, Align::kCenter);
  canvas.Text(Box(box.x + 60, box.y + 48, box.w - 120, 60), detail,
              theme::kFontSmall, theme::kTextFaint, Align::kCenter);
}

// --------------------------------------------------------------------- shell
bool Interface::Create(int width, int height) {
  if (canvas_.ready() && canvas_.width() == width && canvas_.height() == height) {
    return true;
  }
  canvas_.Destroy();
  if (!canvas_.Create(width, height)) return false;
  dirty_ = true;
  return true;
}

void Interface::Destroy() { canvas_.Destroy(); }

void Interface::SetPage(Page page) {
  if (page == page_) return;
  CloseInventoryPopup();
  map_dragging_ = false;
  inventory_scroll_armed_ = false;
  inventory_scrolling_ = false;
  page_ = page;
  ResetPressedTarget();
  CancelPendingActions();
  dirty_ = true;
}

void Interface::AddTarget(const Box& bounds, UiAction action, int index,
                          bool enabled) {
  if (bounds.w <= 0 || bounds.h <= 0 || action == UiAction::kNone) return;
  uint64_t source_revision = 0;
  if (page_ == Page::kInventory || page_ == Page::kGear ||
      page_ == Page::kMissions || page_ == Page::kSkills) {
    source_revision = NativeRevisionForPage(page_);
  }
  targets_.push_back(
      UiTarget{bounds, action, index, -1, enabled, page_, source_revision,
               false});
}

void Interface::AddModalTarget(const Box& bounds, UiAction action, int index,
                               bool enabled, int secondary_index) {
  if (bounds.w <= 0 || bounds.h <= 0 || action == UiAction::kNone) return;
  uint64_t source_revision = 0;
  if (page_ == Page::kInventory || page_ == Page::kGear ||
      page_ == Page::kMissions || page_ == Page::kSkills) {
    source_revision = NativeRevisionForPage(page_);
  }
  targets_.push_back(
      UiTarget{bounds, action, index, secondary_index, enabled, page_,
               source_revision, true});
}

bool Interface::TargetPressed(UiAction action, int index,
                              int secondary_index) const {
  return pressed_action_ == action &&
         (index < 0 || pressed_action_index_ == index) &&
         (secondary_index < 0 ||
          (has_pressed_target_ &&
           pressed_target_.secondary_index == secondary_index));
}

int Interface::ScrollOffset(Page page) const {
  switch (page) {
    case Page::kInventory: return inventory_offset_;
    case Page::kMissions: return missions_offset_;
    case Page::kSkills: return skills_offset_;
    default: return 0;
  }
}

void Interface::ClampScroll(Page page, int maximum) {
  maximum = std::max(0, maximum);
  int* value = nullptr;
  switch (page) {
    case Page::kInventory: value = &inventory_offset_; break;
    case Page::kMissions: value = &missions_offset_; break;
    case Page::kSkills: value = &skills_offset_; break;
    default: return;
  }
  *value = std::clamp(*value, 0, maximum);
}

void Interface::SetInventoryListViewport(const Box& bounds, int row_stride,
                                         int maximum) {
  inventory_list_viewport_ = bounds;
  inventory_row_stride_ = std::max(1, row_stride);
  inventory_scroll_max_ = std::max(0, maximum);
  inventory_offset_ = std::clamp(inventory_offset_, 0,
                                 inventory_scroll_max_);
}

void Interface::ClampMapCenter() {
  if (!map_center_valid_ || map_viewport_.w <= 0 || map_viewport_.h <= 0) {
    return;
  }
  float span_u = 1.0f;
  float span_v = 1.0f;
  MapViewportSpan(map_viewport_, map_zoom_, &span_u, &span_v);
  const float half_u = std::min(0.5f, span_u * 0.5f);
  const float half_v = std::min(0.5f, span_v * 0.5f);
  map_center_u_ = span_u >= 1.0f
      ? 0.5f : std::clamp(map_center_u_, half_u, 1.0f - half_u);
  map_center_v_ = span_v >= 1.0f
      ? 0.5f : std::clamp(map_center_v_, half_v, 1.0f - half_v);
}

void Interface::SetMapViewport(const Box& bounds, float player_u,
                               float player_v,
                               const std::string& level_name) {
  map_viewport_ = bounds;
  map_player_u_ = std::clamp(player_u, 0.0f, 1.0f);
  map_player_v_ = std::clamp(player_v, 0.0f, 1.0f);
  if (!map_center_valid_ || level_name != map_level_name_) {
    map_level_name_ = level_name;
    map_zoom_ = 1.0f;
    map_follow_player_ = true;
    map_center_valid_ = true;
    map_center_u_ = map_player_u_;
    map_center_v_ = map_player_v_;
  } else if (map_follow_player_) {
    map_center_u_ = map_player_u_;
    map_center_v_ = map_player_v_;
  }
  ClampMapCenter();
}

bool Interface::InvokeTarget(const UiTarget& target) {
  // An ordinal is meaningful only for the exact owned snapshot that was
  // rendered. If a native list changed while a finger was down, discard the
  // tap instead of selecting a different row that inherited the same index.
  if (target.page != page_) return false;
  if (target.source_revision != 0 &&
      target.source_revision != NativeRevisionForPage(target.page)) {
    return false;
  }

  NativeUi& native = TheNativeUi();
  auto invoke_inventory_action = [&](InventoryNativeAction action) {
    // Context-sheet actions carry the row they were opened for. Reacquire and
    // select that exact native row immediately before invoking the game's own
    // action button, so an external controller movement cannot redirect a
    // Drop/Use operation to a different item.
    if (target.modal && target.index >= 0) {
      const InventorySnapshot& inventory = native.inventory();
      if (target.index >= static_cast<int>(inventory.items.size())) {
        CloseInventoryPopup();
        return false;
      }
      if (!inventory.items[target.index].selected &&
          !native.SelectInventoryItem(static_cast<std::size_t>(target.index))) {
        CloseInventoryPopup();
        return false;
      }
    }
    const bool invoked = native.SelectInventoryAction(action);
    if (target.modal) CloseInventoryPopup();
    return invoked;
  };
  switch (target.action) {
    case UiAction::kInventoryItem:
      return target.index >= 0 && native.SelectInventoryItem(target.index);
    case UiAction::kInventoryEquip:
      return invoke_inventory_action(InventoryNativeAction::kEquip);
    case UiAction::kInventoryDrop:
      return invoke_inventory_action(InventoryNativeAction::kDrop);
    case UiAction::kInventoryCompare:
      return invoke_inventory_action(InventoryNativeAction::kCompare);
    case UiAction::kInventoryInspect:
      return invoke_inventory_action(InventoryNativeAction::kInspect);
    case UiAction::kInventoryAssignQuickSlot: {
      if (!target.modal || target.index < 0 ||
          target.secondary_index < 0) {
        return false;
      }
      const bool assigned = native.AssignInventoryItemToQuickSlot(
          static_cast<std::size_t>(target.index),
          static_cast<std::size_t>(target.secondary_index));
      if (assigned) CloseInventoryPopup();
      return assigned;
    }
    case UiAction::kInventoryPopupCancel:
      CloseInventoryPopup();
      return true;
    case UiAction::kGearSlot:
      if (target.index < 0) return false;
      return target.page == Page::kStatus
          ? native.ActivateQuickSlot(target.index)
          : native.SelectGearSlot(target.index);
    case UiAction::kMission:
      return target.index >= 0 && native.SelectMission(target.index);
    case UiAction::kMissionTrack:
      return native.SelectMissionAction(MissionNativeAction::kTrack);
    case UiAction::kMissionDetails:
      return native.SelectMissionAction(MissionNativeAction::kDetails);
    case UiAction::kSkill:
      return target.index >= 0 && native.SelectSkill(target.index);
    case UiAction::kSkillLearn:
      return native.SelectSkillAction(SkillNativeAction::kLearn);
    case UiAction::kSkillView:
      return native.SelectSkillAction(SkillNativeAction::kView);
    case UiAction::kSkillConfirmYes:
      return native.SelectSkillAction(SkillNativeAction::kConfirmYes);
    case UiAction::kSkillConfirmNo:
      return native.SelectSkillAction(SkillNativeAction::kConfirmNo);
    case UiAction::kMapZoomIn:
      map_zoom_ = std::min(4.0f, map_zoom_ * 1.35f);
      ClampMapCenter();
      return true;
    case UiAction::kMapZoomOut:
      map_zoom_ = std::max(1.0f, map_zoom_ / 1.35f);
      ClampMapCenter();
      return true;
    case UiAction::kMapCenter:
      map_zoom_ = 1.0f;
      map_center_u_ = map_player_u_;
      map_center_v_ = map_player_v_;
      map_center_valid_ = true;
      map_follow_player_ = true;
      ClampMapCenter();
      return true;
    case UiAction::kScrollUp:
    case UiAction::kScrollDown: {
      int* offset = nullptr;
      int step = 6;
      if (page_ == Page::kInventory) offset = &inventory_offset_;
      if (page_ == Page::kMissions) offset = &missions_offset_;
      if (page_ == Page::kSkills) { offset = &skills_offset_; step = 8; }
      if (!offset) return false;
      *offset = std::max(0, *offset +
          (target.action == UiAction::kScrollDown ? step : -step));
      return true;
    }
    default:
      return false;
  }
}

void Interface::ResetPressedTarget() {
  has_pressed_target_ = false;
  pressed_target_ = {};
  pressed_generation_ = 0;
  pressed_action_ = UiAction::kNone;
  pressed_action_index_ = -1;
  press_started_ms_ = 0;
  long_press_fired_ = false;
}

void Interface::CloseInventoryPopup() {
  if (!inventory_popup_open_) return;
  inventory_popup_open_ = false;
  inventory_popup_index_ = -1;
  inventory_popup_item_key_.clear();
  inventory_popup_bounds_ = {};
  dirty_ = true;
}

bool Interface::InventoryPopupItemStillMatches() const {
  if (!inventory_popup_open_ || inventory_popup_index_ < 0) return false;
  const InventorySnapshot& inventory = TheNativeUi().inventory();
  if (!inventory.available ||
      inventory_popup_index_ >= static_cast<int>(inventory.items.size())) {
    return false;
  }
  return inventory_popup_item_key_ ==
      InventoryItemKey(inventory.items[inventory_popup_index_],
                       inventory_popup_index_);
}

bool Interface::OpenInventoryPopup(const UiTarget& target) {
  if (inventory_popup_open_ || page_ != Page::kInventory ||
      target.page != Page::kInventory ||
      target.action != UiAction::kInventoryItem || target.index < 0 ||
      (target.source_revision != 0 &&
       target.source_revision != NativeRevisionForPage(target.page))) {
    return false;
  }
  const InventorySnapshot& inventory = TheNativeUi().inventory();
  if (!inventory.available ||
      target.index >= static_cast<int>(inventory.items.size())) {
    return false;
  }

  // Select through the queued game-thread route, then keep an owned identity
  // key. Every popup action reselects the same native row before invoking it.
  if (!QueueTarget(target)) return false;
  inventory_scroll_armed_ = false;
  inventory_scrolling_ = false;
  inventory_popup_open_ = true;
  inventory_popup_index_ = target.index;
  inventory_popup_item_key_ =
      InventoryItemKey(inventory.items[target.index], target.index);
  inventory_popup_bounds_ = {};
  dirty_ = true;
  return true;
}

void Interface::UpdateLongPress() {
  constexpr uint64_t kLongPressMilliseconds = 560;
  if (inventory_popup_open_ || !has_pressed_target_ || long_press_fired_ ||
      page_ != Page::kInventory ||
      pressed_target_.action != UiAction::kInventoryItem ||
      pressed_target_.index < 0 || press_started_ms_ == 0 ||
      SteadyMilliseconds() - press_started_ms_ < kLongPressMilliseconds) {
    return;
  }

  if (OpenInventoryPopup(pressed_target_)) long_press_fired_ = true;
}

bool Interface::QueueTarget(const UiTarget& target) {
  if (!target.enabled || target.action == UiAction::kNone) return false;
  std::lock_guard<std::mutex> lock(pending_lock_);
  constexpr std::size_t kMaximumPendingActions = 32;
  if (pending_actions_.size() >= kMaximumPendingActions) return false;
  // Some frontends emit a resolved `tap` as well as its down/up pair. Coalesce
  // the adjacent semantic duplicate so a consumable is never used twice and
  // a quick slot is never toggled twice for one physical touch.
  if (!pending_actions_.empty()) {
    const UiTarget& previous = pending_actions_.back();
    if (previous.action == target.action && previous.index == target.index &&
        previous.secondary_index == target.secondary_index &&
        previous.page == target.page &&
        previous.source_revision == target.source_revision) {
      return true;
    }
  }
  pending_actions_.push_back(target);
  return true;
}

void Interface::ProcessPendingActions() {
  // Bound native UI work added to one game frame. Ordinary touch produces one
  // action; the larger cap merely absorbs a short burst without an allocation
  // or unbounded loop inside Present.
  constexpr int kMaximumActionsPerFrame = 4;
  for (int i = 0; i < kMaximumActionsPerFrame; ++i) {
    UiTarget target;
    {
      std::lock_guard<std::mutex> lock(pending_lock_);
      if (pending_actions_.empty()) break;
      target = pending_actions_.front();
      pending_actions_.pop_front();
    }
    InvokeTarget(target);
    dirty_ = true;
  }
}

void Interface::CancelPendingActions() {
  std::lock_guard<std::mutex> lock(pending_lock_);
  pending_actions_.clear();
}

void Interface::NextPage() {
  SetPage(static_cast<Page>((static_cast<int>(page_) + 1) %
                            static_cast<int>(Page::kCount)));
}

void Interface::PreviousPage() {
  const int count = static_cast<int>(Page::kCount);
  SetPage(static_cast<Page>((static_cast<int>(page_) + count - 1) % count));
}

Box Interface::TabRect(int index) const {
  const int count = static_cast<int>(Page::kCount);
  const int margin = 30;
  const int gap = 12;
  const int width = (canvas_.width() - 2 * margin - (count - 1) * gap) / count;
  const int x = margin + index * (width + gap);
  const int w = index == count - 1 ? canvas_.width() - margin - x : width;
  return Box(x, 24, w, 76);
}

void Interface::DrawTabs(Canvas& canvas) {
  canvas.VGradient(Box(0, 0, canvas.width(), theme::kTabH),
                   theme::kBackground.Mix(theme::kSurfaceHigh, 0.25f),
                   theme::kBackground);

  for (int i = 0; i < static_cast<int>(Page::kCount); ++i) {
    const Box tab = TabRect(i);
    const bool active = static_cast<int>(page_) == i;
    const bool pressed = pressed_tab_ == i;

    const Color fill = active
        ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.28f)
        : (pressed ? theme::kSurfaceHigh : theme::kSurface);
    canvas.FillRect(tab, fill);
    canvas.StrokeRect(tab, active ? 3 : 2,
                      active ? theme::kAccent : theme::kEdge);
    canvas.StrokeRect(tab.Inset(5), 1,
                      active ? theme::kEdgeBright : Color(30, 32, 33));
    if (active)
      canvas.FillRect(Box(tab.x, tab.bottom() - 5, tab.w, 5), theme::kAccent);

    const Page page = static_cast<Page>(i);
    const Box icon(tab.x + 20, tab.y + 10, 72, tab.h - 20);
    DrawTabIcon(canvas, page, icon,
                active ? theme::kAccent : theme::kTextDim);
    canvas.Text(Box(tab.x + 91, tab.y, tab.w - 101, tab.h), PageTitle(page),
                theme::kFontTitle, active ? theme::kText : theme::kTextDim,
                Align::kCenter);
  }
}

// The strip along the bottom. Present on every page on purpose: the numbers
// that matter while something is trying to bite you should not be behind a tab.
void Interface::DrawStatus(Canvas& canvas, Game& game, GameState& state) {
  const int y = canvas.height() - theme::kStatusH;
  canvas.FillRect(Box(0, y, canvas.width(), theme::kStatusH), theme::kSurface);
  canvas.FillRect(Box(0, y, canvas.width(), 1), theme::kEdge);

  const Vitals& v = state.vitals();
  if (!v.usable()) {
    const std::string message =
        game.searching()
            ? Format("locating player  (%d ids scanned)", game.scanned())
            : (game.has_level() ? "no player state bound" : "waiting for level");
    canvas.Text(Box(theme::kMargin, y, canvas.width() - 2 * theme::kMargin,
                     theme::kStatusH),
                message, theme::kFontBody, theme::kTextFaint, Align::kCenter);
    return;
  }

  // Three meters across, sized so the widest reading still fits its column.
  const int gap = theme::kGap;
  const int width = (canvas.width() - 2 * theme::kMargin - 2 * gap) / 3;
  const int top = y + 16;

  struct Meter { const char* label; const Value* cur; const Value* max; Color fill; };
  const Meter meters[] = {
      {"HEALTH",  &v.health,  &v.max_health,  theme::kBlood},
      {"STAMINA", &v.stamina, &v.max_stamina, theme::kStamina},
      {"FURY",    &v.fury,    &v.max_fury,    theme::kFury},
  };
  for (int i = 0; i < 3; ++i) {
    const Box column(theme::kMargin + i * (width + gap), top, width, 58);
    const Meter& m = meters[i];
    MeterRow(canvas, column, m.label, m.cur->v, m.max->valid ? m.max->v : 0.0,
             m.cur->valid, m.fill);
  }
}

bool Interface::Render(Game& game, GameState& state) {
  if (!canvas_.ready()) return false;

  UpdateLongPress();
  if (inventory_popup_open_ && !InventoryPopupItemStillMatches()) {
    CloseInventoryPopup();
  }

  const Vitals& v = state.vitals();
  Signature now;
  now.page = static_cast<int>(page_);
  now.health = Quantise(v.health);
  now.max_health = Quantise(v.max_health);
  now.stamina = Quantise(v.stamina);
  now.max_stamina = Quantise(v.max_stamina);
  now.fury = Quantise(v.fury);
  now.max_fury = Quantise(v.max_fury);
  now.level = Quantise(v.level);
  now.money = Quantise(v.money);
  now.experience = Quantise(v.experience);
  now.exp_to_next = Quantise(v.exp_to_next);
  now.skill_points = Quantise(v.skill_points);
  const InventoryCapacity& capacity = state.inventory_capacity();
  now.inventory_slots = Quantise(capacity.slots);
  now.inventory_max = Quantise(capacity.max_slots);
  now.quick_slots = Quantise(capacity.quick_slots);
  now.quick_max = Quantise(capacity.max_quick_slots);
  if (page_ == Page::kMap) {
    now.page_revision = TheNativeMap().Revision();
  } else if (page_ == Page::kStatus) {
    // Stats contains both the live HUD/equipment feed and the minimap. Fold
    // both producers into its repaint key so either one can update the panel
    // without requiring the full Map page to have been visited first.
    const uint64_t map_revision = TheNativeMap().Revision();
    const uint64_t hud_revision = TheNativeUi().revision();
    now.page_revision = hud_revision ^
        (map_revision + 0x9E3779B97F4A7C15ull +
         (hud_revision << 6) + (hud_revision >> 2));
  } else if (page_ == Page::kInventory || page_ == Page::kSkills) {
    now.page_revision = TheNativeUi().revision();
  }
  now.bound = state.bound();
  now.searching = game.searching();

  if (!dirty_ && now == last_) return false;
  last_ = now;
  dirty_ = false;

  canvas_.ClearClip();
  canvas_.Clear(theme::kBackground);
  DrawTabs(canvas_);

  const Box body(theme::kMargin, theme::kTabH + theme::kMargin,
                  canvas_.width() - 2 * theme::kMargin,
                  canvas_.height() - theme::kTabH - 2 * theme::kMargin);
  canvas_.SetClip(body);
  ++target_generation_;
  if (target_generation_ == 0) ++target_generation_;
  targets_.clear();
  DrawPage(canvas_, body, game, state);
  canvas_.ClearClip();

  // A repaint rebuilds touch targets even when their geometry did not change.
  // Rebind an in-flight press to the equivalent freshly drawn target so the
  // visual pressed state and the long-press timer cannot invalidate an
  // otherwise stationary finger merely because the native snapshot refreshed.
  if (has_pressed_target_) {
    bool rebound = false;
    for (const UiTarget& target : targets_) {
      if (target.enabled && target.action == pressed_target_.action &&
          target.index == pressed_target_.index &&
          target.secondary_index == pressed_target_.secondary_index &&
          target.page == pressed_target_.page &&
          target.modal == pressed_target_.modal &&
          target.source_revision == pressed_target_.source_revision) {
        pressed_target_ = target;
        pressed_generation_ = target_generation_;
        rebound = true;
        break;
      }
    }
    if (!rebound && !long_press_fired_) ResetPressedTarget();
  }

  return true;
}

// --------------------------------------------------------------------- touch
bool Interface::OnTouchDown(int x, int y) {
  consume_release_ = false;
  inventory_scroll_armed_ = false;
  inventory_scrolling_ = false;
  if (inventory_popup_open_) {
    // The sheet owns the bottom panel until dismissed. Disabled entries still
    // consume the touch, but deliberately never acquire a pressed state.
    for (int i = static_cast<int>(targets_.size()) - 1; i >= 0; --i) {
      const UiTarget& target = targets_[i];
      if (!target.modal || !target.bounds.Contains(x, y)) continue;
      if (!target.enabled) {
        consume_release_ = true;
        return true;
      }
      pressed_target_ = target;
      has_pressed_target_ = true;
      pressed_generation_ = target_generation_;
      pressed_action_ = target.action;
      pressed_action_index_ = target.index;
      press_started_ms_ = 0;
      long_press_fired_ = false;
      dirty_ = true;
      return true;
    }
    if (!inventory_popup_bounds_.Contains(x, y)) CloseInventoryPopup();
    consume_release_ = true;
    return true;
  }

  if (y < theme::kTabH) {
    for (int i = 0; i < static_cast<int>(Page::kCount); ++i) {
      if (TabRect(i).Contains(x, y)) {
        pressed_tab_ = i;
        dirty_ = true;
        return true;
      }
    }
  }
  if (page_ == Page::kInventory &&
      inventory_list_viewport_.Contains(x, y)) {
    inventory_scroll_armed_ = true;
    inventory_scroll_start_y_ = y;
    inventory_scroll_start_offset_ = inventory_offset_;
  }
  for (int i = static_cast<int>(targets_.size()) - 1; i >= 0; --i) {
    if (targets_[i].enabled && targets_[i].bounds.Contains(x, y)) {
      pressed_target_ = targets_[i];
      has_pressed_target_ = true;
      pressed_generation_ = target_generation_;
      pressed_action_ = pressed_target_.action;
      pressed_action_index_ = pressed_target_.index;
      press_started_ms_ =
          pressed_target_.action == UiAction::kInventoryItem
              ? SteadyMilliseconds() : 0;
      long_press_fired_ = false;
      dirty_ = true;
      return true;
    }
  }
  if (page_ == Page::kMap && map_viewport_.Contains(x, y)) {
    map_dragging_ = true;
    map_drag_x_ = x;
    map_drag_y_ = y;
    return true;
  }
  // Section headings and empty list space have no action target, but still
  // need to own the pointer so a drag beginning there can scroll the list.
  if (inventory_scroll_armed_) return true;
  return false;
}

bool Interface::OnTouchMove(int x, int y) {
  if (consume_release_) return true;
  if (inventory_scroll_armed_) {
    constexpr int kScrollThreshold = 12;
    const int dy = y - inventory_scroll_start_y_;
    if (!inventory_scrolling_ && std::abs(dy) >= kScrollThreshold) {
      inventory_scrolling_ = true;
      ResetPressedTarget();
    }
    if (inventory_scrolling_) {
      const int row_delta = static_cast<int>(std::lround(
          static_cast<float>(dy) / inventory_row_stride_));
      const int next = std::clamp(inventory_scroll_start_offset_ - row_delta,
                                  0, inventory_scroll_max_);
      if (next != inventory_offset_) {
        inventory_offset_ = next;
        dirty_ = true;
      }
      return true;
    }
  }
  if (map_dragging_) {
    const int dx = x - map_drag_x_;
    const int dy = y - map_drag_y_;
    map_drag_x_ = x;
    map_drag_y_ = y;
    if (dx != 0 || dy != 0) {
      float span_u = 1.0f;
      float span_v = 1.0f;
      MapViewportSpan(map_viewport_, map_zoom_, &span_u, &span_v);
      map_center_u_ -= static_cast<float>(dx) /
                       std::max(1, map_viewport_.w) * span_u;
      map_center_v_ -= static_cast<float>(dy) /
                       std::max(1, map_viewport_.h) * span_v;
      map_center_valid_ = true;
      map_follow_player_ = false;
      ClampMapCenter();
      dirty_ = true;
    }
    return true;
  }
  if (has_pressed_target_) {
    if (pressed_generation_ != target_generation_ ||
        !pressed_target_.bounds.Contains(x, y)) {
      ResetPressedTarget();
      dirty_ = true;
    }
    return true;
  }
  if (inventory_popup_open_) return true;
  if (inventory_scroll_armed_) return true;
  if (pressed_tab_ < 0) return false;
  // Sliding off the tab cancels it, the way a button ought to behave.
  if (!TabRect(pressed_tab_).Contains(x, y)) {
    pressed_tab_ = -1;
    dirty_ = true;
  }
  return true;
}

bool Interface::OnTouchUp(int x, int y) {
  if (consume_release_) {
    consume_release_ = false;
    return true;
  }
  const bool inventory_list_release = inventory_scroll_armed_;
  const bool was_inventory_scroll = inventory_scrolling_;
  inventory_scroll_armed_ = false;
  inventory_scrolling_ = false;
  if (was_inventory_scroll) {
    ResetPressedTarget();
    dirty_ = true;
    return true;
  }
  if (map_dragging_) {
    map_dragging_ = false;
    return true;
  }
  if (has_pressed_target_) {
    const UiTarget target = pressed_target_;
    const bool same_layout = pressed_generation_ == target_generation_;
    const bool was_long_press = long_press_fired_;
    ResetPressedTarget();
    dirty_ = true;
    if (!was_long_press && same_layout && target.bounds.Contains(x, y)) {
      QueueTarget(target);
    }
    return true;
  }
  if (inventory_popup_open_) return true;
  if (inventory_list_release) return true;
  if (pressed_tab_ < 0) return false;
  const int tab = pressed_tab_;
  pressed_tab_ = -1;
  dirty_ = true;
  if (TabRect(tab).Contains(x, y)) {
    SetPage(static_cast<Page>(tab));
    return true;
  }
  return true;
}

bool Interface::OnHold(int x, int y) {
  if (inventory_popup_open_) return true;
  if (page_ != Page::kInventory) return false;
  for (int i = static_cast<int>(targets_.size()) - 1; i >= 0; --i) {
    const UiTarget& target = targets_[i];
    if (!target.enabled || target.action != UiAction::kInventoryItem ||
        !target.bounds.Contains(x, y)) {
      continue;
    }
    pressed_tab_ = -1;
    ResetPressedTarget();
    return OpenInventoryPopup(target);
  }
  return false;
}

bool Interface::OnPinch(int x, int y, float factor) {
  if (page_ != Page::kMap || !map_center_valid_ ||
      !map_viewport_.Contains(x, y) || !std::isfinite(factor) ||
      factor <= 0.0f) {
    return false;
  }

  const float previous_zoom = map_zoom_;
  const float next_zoom = std::clamp(previous_zoom * factor, 1.0f, 4.0f);
  if (std::fabs(next_zoom - previous_zoom) < 0.0001f) return true;

  float old_span_u = 1.0f;
  float old_span_v = 1.0f;
  float new_span_u = 1.0f;
  float new_span_v = 1.0f;
  MapViewportSpan(map_viewport_, previous_zoom, &old_span_u, &old_span_v);
  MapViewportSpan(map_viewport_, next_zoom, &new_span_u, &new_span_v);
  const float anchor_u = static_cast<float>(x - map_viewport_.x) /
                         std::max(1, map_viewport_.w) - 0.5f;
  const float anchor_v = static_cast<float>(y - map_viewport_.y) /
                         std::max(1, map_viewport_.h) - 0.5f;
  map_center_u_ += anchor_u * (old_span_u - new_span_u);
  map_center_v_ += anchor_v * (old_span_v - new_span_v);
  map_zoom_ = next_zoom;
  map_follow_player_ = false;
  map_dragging_ = false;
  ClampMapCenter();
  dirty_ = true;
  return true;
}

}  // namespace dis
