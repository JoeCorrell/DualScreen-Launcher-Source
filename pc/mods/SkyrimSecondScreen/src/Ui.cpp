#include "Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "NativeAssets.h"
#include "../../DeadIslandSecondScreen/src/Log.h"

namespace dis {
namespace {

Ui& g_ui = *new Ui();
DrawNativeItemPreviewFn g_draw_native_item_preview = nullptr;
RequestNativeItemPreviewFn g_request_native_item_preview = nullptr;
BeginNativeItemPreviewCompositionFn
    g_begin_native_item_preview_composition = nullptr;
DrawNativeCharacterPreviewFn g_draw_native_character_preview = nullptr;
DrawNativeMapPreviewFn g_draw_native_map_preview = nullptr;

// Skyrim's menus are dark, but the Thor's lower LCD loses near-black detail
// much sooner than a desktop monitor.  Keep the cool charcoal / warm brass
// identity while separating the backdrop, panels, cards, and selected rows
// into distinct luminance bands.  These values are deliberately opaque where
// possible so the UI remains predictable on both SDR and HDR host desktops.
constexpr Color kBlack(12, 16, 17);
constexpr Color kBackdropTop(52, 61, 60);
constexpr Color kBackdropBottom(22, 29, 30);
constexpr Color kPanelTop(46, 54, 51);
constexpr Color kPanelBottom(25, 32, 32);
constexpr Color kPanelSelectedTop(63, 73, 68);
constexpr Color kPanelSelectedBottom(35, 43, 41);
constexpr Color kSurface(28, 35, 35);
constexpr Color kSurfaceTop(51, 59, 56);
constexpr Color kSurfaceBottom(29, 36, 35);
constexpr Color kRowTop(43, 51, 49);
constexpr Color kRowBottom(30, 38, 37);
constexpr Color kRowAltTop(38, 46, 44);
constexpr Color kRowAltBottom(26, 34, 33);
constexpr Color kObjectiveRow(38, 46, 44);
constexpr Color kObjectiveRowAlt(32, 40, 39);
constexpr Color kObjectiveCurrent(55, 66, 61);
constexpr Color kSurfaceSelectedTop(73, 84, 79);
constexpr Color kSurfaceSelectedBottom(42, 51, 48);
constexpr Color kPreviewTop(55, 64, 62);
constexpr Color kPreviewBottom(24, 31, 32);
constexpr Color kEdge(91, 99, 95);
constexpr Color kEdgeBright(164, 174, 167);
constexpr Color kText(239, 241, 235);
constexpr Color kMuted(183, 191, 184);
constexpr Color kSelected(208, 218, 211);
constexpr Color kWarm(218, 185, 111);
constexpr Color kCyan(113, 218, 228);
constexpr Color kHealth(181, 53, 54);
constexpr Color kMagicka(62, 108, 174);
constexpr Color kStamina(96, 145, 71);
// Keep a small compositor-safe inset, but do not spend the outer edge on a
// decorative frame.  The old 20 px inset plus frame consumed useful room and
// made the already dense inventory/detail layouts look clipped on the Thor.
constexpr int kNavX = 12;
constexpr int kNavY = 12;
constexpr int kNavH = 88;
constexpr int kNavGap = 8;
constexpr int kContentX = 12;
constexpr int kContentY = 112;
constexpr int kContentRightMargin = 12;
constexpr int kContentBottomMargin = 12;
constexpr int kResourcePanelHeight = 134;
constexpr int kHotbarPanelHeight = 224;
constexpr int kQuestListWidth = 430;
constexpr int kEquipmentSummaryHeight = 62;
constexpr int kEquipmentListEndGap = 10;
constexpr int kStatsFooterHeight = 78;
constexpr float kMapWorldMin = -393216.0f;
constexpr float kMapWorldMax = 393216.0f;
constexpr uint32_t kTamrielWorldForm = 0x0000003Cu;
constexpr float kMapMaxZoom = 24.0f;
constexpr int kPageCount = 5;
constexpr int kVisiblePageCount = 3;
// Keep the map implementation compiled, but do not expose it while the native
// map renderer is disabled. Navigation uses this explicit list rather than
// the enum's contiguous values so arrows, tabs, and page dots skip kMap.
constexpr Page kNavigablePages[] = {
    Page::kStatus, Page::kInventory, Page::kMagic, Page::kQuests};
constexpr int kNavigablePageCount =
    static_cast<int>(sizeof(kNavigablePages) / sizeof(kNavigablePages[0]));
// A composition reports the complete bounded set of previews it actually
// needs. This is demand admission only: the proxy de-duplicates/cache-checks
// these FormIDs and Skyrim's bridge serializes the native Inventory3D work.
// Keeping the bound above every visible Inventory/Magic/Equipped/Hotbar row
// prevents a repaint from silently starving later cards without allowing an
// unbounded snapshot-sized request burst.
constexpr unsigned kItemPreviewRequestsPerComposition = 24;
unsigned g_item_preview_requested_count = 0;
uint32_t
    g_item_preview_requested[kItemPreviewRequestsPerComposition] = {};
bool g_item_preview_requested_priority[
    kItemPreviewRequestsPerComposition] = {};
uint64_t g_item_preview_request_world_epoch = 0;
uint64_t g_item_preview_composition_revision = 0;

void BeginItemPreviewRequestFrame(uint64_t world_epoch) {
  g_item_preview_requested_count = 0;
  g_item_preview_request_world_epoch = world_epoch;
  if (++g_item_preview_composition_revision == 0)
    ++g_item_preview_composition_revision;
  if (g_begin_native_item_preview_composition)
    g_begin_native_item_preview_composition(
        world_epoch, g_item_preview_composition_revision);
  std::fill_n(g_item_preview_requested,
              kItemPreviewRequestsPerComposition, 0u);
  std::fill_n(g_item_preview_requested_priority,
              kItemPreviewRequestsPerComposition, false);
}

void RequestItemPreviewBudgeted(uint32_t form_id, bool priority = false) {
  if (!form_id || !g_request_native_item_preview) return;
  for (unsigned i = 0; i < g_item_preview_requested_count; ++i) {
    if (g_item_preview_requested[i] != form_id) continue;
    // A row/hotbar card may report the FormID before its selected detail card.
    // Forward one priority upgrade instead of admitting a duplicate request.
    if (priority && !g_item_preview_requested_priority[i]) {
      g_item_preview_requested_priority[i] = true;
      g_request_native_item_preview(form_id,
                                    g_item_preview_request_world_epoch, true,
                                    g_item_preview_composition_revision);
    }
    return;
  }
  if (g_item_preview_requested_count >=
      kItemPreviewRequestsPerComposition)
    return;
  const unsigned admitted = g_item_preview_requested_count++;
  g_item_preview_requested[admitted] = form_id;
  g_item_preview_requested_priority[admitted] = priority;
  g_request_native_item_preview(form_id, g_item_preview_request_world_epoch,
                                priority,
                                g_item_preview_composition_revision);
}

int DetailListWidth(int right) {
  // Inventory and Magic deliberately share one layout contract so their
  // native preview cards, descriptions, and action buttons never jump in size
  // when the player changes page.
  return static_cast<int>((right - kContentX - 8) * 0.48f);
}

bool DrawOrRequestItemPreview(Canvas& canvas, const Box& destination,
                               uint32_t form_id, bool priority = false) {
  if (!form_id) return false;
  if (g_draw_native_item_preview &&
      g_draw_native_item_preview(canvas, destination, form_id,
                                 g_item_preview_request_world_epoch)) {
    return true;
  }
  RequestItemPreviewBudgeted(form_id, priority);
  return false;
}

const char* PageName(Page page) {
  static const char* names[] = {"CHARACTER", "INVENTORY", "MAP", "MAGIC", "QUESTS"};
  const int index = static_cast<int>(page);
  return index >= 0 && index < kPageCount ? names[index] : "UNKNOWN";
}

int NavigablePageIndex(Page page) {
  for (int index = 0; index < kNavigablePageCount; ++index)
    if (kNavigablePages[index] == page) return index;
  return 0;
}

Page WrappedNavigablePage(int value) {
  value %= kNavigablePageCount;
  if (value < 0) value += kNavigablePageCount;
  return kNavigablePages[value];
}

Page OffsetNavigablePage(Page page, int delta) {
  return WrappedNavigablePage(NavigablePageIndex(page) + delta);
}

struct NavigationLayout {
  Box left;
  Box right;
  Box tabs[kVisiblePageCount];
  int pages[kVisiblePageCount] = {};
};

struct MapLayout {
  Box panel;
  Box inside;
  Box header;
  Box viewport;
};

struct QuestDetailLayout {
  Box header;
  Box status;
  Box lore;
  Box objectives;
  Box track;
  int objective_row_height = 60;
};

QuestDetailLayout BuildQuestDetailLayout(const Box& detail) {
  QuestDetailLayout layout;
  layout.header = Box(detail.x + 22, detail.y + 14, detail.w - 44, 64);
  layout.status = Box(detail.x + 42, detail.y + 80, detail.w - 84, 34);
  layout.track = Box(detail.x + 55, detail.bottom() - 76,
                     detail.w - 110, 58);
  const int content_top = detail.y + 122;
  const int content_bottom = layout.track.y - 10;
  const int content_height = std::max(220, content_bottom - content_top);
  // Lore remains readable, but objectives are the primary vertical list on
  // this page. The old 42% lore split left room for only a couple of rows.
  const int lore_height = std::clamp(content_height * 34 / 100, 108, 190);
  layout.lore = Box(detail.x + 32, content_top, detail.w - 64,
                    lore_height);
  layout.objectives = Box(detail.x + 32, layout.lore.bottom() + 8,
                          detail.w - 64,
                          std::max(94, content_bottom - layout.lore.bottom() - 8));
  return layout;
}

MapLayout BuildMapLayout(int width, int height) {
  MapLayout layout;
  layout.panel = Box(kContentX, kContentY,
                     width - kContentX - kContentRightMargin,
                     height - kContentY - kContentBottomMargin);
  layout.inside = layout.panel.Inset(12);
  layout.header = Box(layout.inside.x, layout.inside.y,
                      layout.inside.w, 64);
  const int viewport_y = layout.header.bottom() + 8;
  layout.viewport = Box(layout.inside.x, viewport_y, layout.inside.w,
                        std::max(1, layout.inside.bottom() - viewport_y));
  return layout;
}

NavigationLayout BuildNavigationLayout(int width, Page selected) {
  const Box navigation(kNavX, kNavY, width - kNavX * 2, kNavH);
  NavigationLayout layout;
  // Match the surrounding card geometry: navigation arrows are square touch
  // targets, not narrow pills stretched to the full navigation height.
  const int arrow_width = navigation.h - 16;
  layout.left = Box(navigation.x + 8, navigation.y + 8,
                    arrow_width, navigation.h - 16);
  layout.right = Box(navigation.right() - 8 - arrow_width,
                     navigation.y + 8, arrow_width, navigation.h - 16);
  const int tabs_x = layout.left.right() + kNavGap;
  const int tabs_width = layout.right.x - kNavGap - tabs_x;
  const int tab_width = (tabs_width - kNavGap * (kVisiblePageCount - 1)) /
                        kVisiblePageCount;
  const int current = NavigablePageIndex(selected);
  for (int i = 0; i < kVisiblePageCount; ++i) {
    layout.pages[i] =
        static_cast<int>(WrappedNavigablePage(current + i - 1));
    layout.tabs[i] = Box(tabs_x + i * (tab_width + kNavGap), navigation.y + 8,
                         tab_width, navigation.h - 16);
  }
  return layout;
}

constexpr uint64_t kFnvOffset = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashBytes(uint64_t& hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= kFnvPrime;
  }
}

template <typename T>
void HashValue(uint64_t& hash, const T& value) {
  HashBytes(hash, &value, sizeof(value));
}

void HashText(uint64_t& hash, const char* text, size_t capacity) {
  if (!text || capacity == 0) return;
  size_t length = 0;
  while (length < capacity && text[length]) ++length;
  HashBytes(hash, text, length);
  HashValue(hash, length);
}

int32_t Quantize(float value, float scale = 10.0f) {
  if (!std::isfinite(value)) return 0;
  const double scaled = static_cast<double>(value) * scale;
  if (scaled >= static_cast<double>(INT32_MAX)) return INT32_MAX;
  if (scaled <= static_cast<double>(INT32_MIN)) return INT32_MIN;
  return static_cast<int32_t>(std::lround(scaled));
}

float Ratio(float v, float max) {
  return max > 0.001f ? std::clamp(v / max, 0.0f, 1.0f) : -1.0f;
}

std::string Number(float value, int decimals = 0) {
  char text[48] = {};
  if (decimals == 1) std::snprintf(text, sizeof(text), "%.1f", value);
  else std::snprintf(text, sizeof(text), "%.0f", value);
  return text;
}

std::string Pair(float value, float maximum) {
  return Number(value) + "/" + Number(maximum);
}

const char* CategoryName(int category) {
  static const char* names[] = {"ALL", "WEAPONS", "APPAREL", "POTIONS",
                                "INGREDIENTS", "BOOKS", "MISC"};
  return category >= 0 && category < 7 ? names[category] : "ALL";
}

const char* MagicCategoryName(int category) {
  static const char* names[] = {"ALL", "FAVORITES", "ALTERATION", "CONJURATION",
                                "DESTRUCTION", "ILLUSION", "RESTORATION", "POWERS"};
  return category >= 0 && category < 8 ? names[category] : "ALL";
}

const char* MagicSchoolName(int school) {
  switch (school) {
    case 18: return "ALTERATION";
    case 19: return "CONJURATION";
    case 20: return "DESTRUCTION";
    case 21: return "ILLUSION";
    case 22: return "RESTORATION";
    default: return "POWER";
  }
}

const char* MagicTypeName(uint32_t type) {
  switch (type) {
    case 0: return "SPELL";
    case 2: return "POWER";
    case 3: return "LESSER POWER";
    case 4: return "ABILITY";
    case 11: return "VOICE POWER";
    default: return "MAGIC";
  }
}

const char* EquipmentSlotName(EquipmentSlot slot) {
  switch (slot) {
    case EquipmentSlot::kRightHand: return "RIGHT HAND";
    case EquipmentSlot::kLeftHand: return "LEFT HAND";
    case EquipmentSlot::kHead: return "HEAD";
    case EquipmentSlot::kBody: return "BODY";
    case EquipmentSlot::kHands: return "HANDS";
    case EquipmentSlot::kFeet: return "FEET";
    case EquipmentSlot::kRing: return "RING";
    case EquipmentSlot::kAmulet: return "AMULET";
    default: return "EQUIPPED";
  }
}

void DrawNavIcon(Canvas& canvas, int index, const Box& box, const Color& color) {
  if (TheNativeAssets().DrawPageIcon(canvas, box, index, color)) return;
  const int cx = box.x + box.w / 2;
  const int cy = box.y + box.h / 2;
  if (index == 0) {
    canvas.StrokeCircle(cx, cy - 10, 9, 2, color);
    canvas.Line(cx - 14, cy + 23, cx - 9, cy + 1, 3, color);
    canvas.Line(cx + 14, cy + 23, cx + 9, cy + 1, 3, color);
    canvas.Line(cx - 9, cy + 1, cx + 9, cy + 1, 3, color);
    canvas.Line(cx - 14, cy + 23, cx + 14, cy + 23, 3, color);
  } else if (index == 1) {
    canvas.StrokeRect(Box(cx - 18, cy - 11, 36, 30), 3, color);
    canvas.Line(cx - 9, cy - 11, cx - 5, cy - 21, 3, color);
    canvas.Line(cx - 5, cy - 21, cx + 5, cy - 21, 3, color);
    canvas.Line(cx + 5, cy - 21, cx + 9, cy - 11, 3, color);
  } else if (index == 2) {
    canvas.StrokeCircle(cx, cy, 22, 2, color);
    canvas.Line(cx, cy - 17, cx + 7, cy + 6, 3, color);
    canvas.Line(cx + 7, cy + 6, cx - 12, cy + 14, 3, color);
    canvas.Line(cx - 12, cy + 14, cx, cy - 17, 3, color);
  } else if (index == 3) {
    canvas.Line(cx, cy - 24, cx, cy + 24, 3, color);
    canvas.Line(cx - 24, cy, cx + 24, cy, 3, color);
    canvas.Line(cx - 16, cy - 16, cx + 16, cy + 16, 2, color);
    canvas.Line(cx + 16, cy - 16, cx - 16, cy + 16, 2, color);
    canvas.FillCircle(cx, cy, 5, color);
  } else {
    canvas.StrokeRect(Box(cx - 20, cy - 22, 40, 44), 2, color);
    for (int row = 0; row < 3; ++row) {
      canvas.FillCircle(cx - 11, cy - 11 + row * 12, 2, color);
      canvas.Line(cx - 4, cy - 11 + row * 12, cx + 13, cy - 11 + row * 12, 2, color);
    }
  }
}

void DrawScrollBar(Canvas& canvas, const Box& track, int offset, int visible,
                   int total) {
  if (track.w <= 0 || track.h <= 0 || visible <= 0 || total <= visible) return;
  const int maximum = std::max(1, total - visible);
  const int thumb_h = std::max(34, track.h * visible / total);
  const int travel = std::max(0, track.h - thumb_h);
  const int thumb_y = track.y + travel * std::clamp(offset, 0, maximum) /
                                    maximum;
  canvas.FillRect(Box(track.x + track.w / 2 - 1, track.y, 2, track.h),
                  kEdge.Alpha(95));
  canvas.VGradient(Box(track.x, thumb_y, track.w, thumb_h),
                   kEdgeBright.Alpha(225), kEdge.Alpha(205));
  canvas.FillRect(Box(track.x + 2, thumb_y + 3, track.w - 4, 2),
                  kText.Alpha(110));
}

void DrawCategoryButton(Canvas& canvas, const Box& button, const char* label,
                        bool selected, int font_size = 14) {
  canvas.VGradient(button,
                   selected ? kPanelSelectedTop : kSurfaceTop,
                   selected ? kPanelSelectedBottom : kSurfaceBottom);
  canvas.StrokeRect(button, selected ? 2 : 1,
                    selected ? kSelected.Alpha(235) : kEdge.Alpha(180));
  canvas.FillRect(Box(button.x + 2, button.y + 2, button.w - 4, 1),
                  (selected ? kWarm : kEdgeBright).Alpha(95));
  if (selected) {
    canvas.FillRect(Box(button.x + 7, button.bottom() - 4,
                        button.w - 14, 2), kWarm);
    TheNativeAssets().DrawDiamond(
        canvas, Box(button.x + button.w / 2 - 7, button.bottom() - 9, 14, 14),
        kWarm);
  }
  canvas.Text(Box(button.x + 6, button.y, button.w - 12, button.h - 3), label,
              {font_size, selected ? FW_SEMIBOLD : FW_NORMAL},
              selected ? kText : kMuted, Align::kCenter);
}

bool MagicIsPassive(const MagicState& magic) {
  // Skyrim's SpellType 4 is an ability/passive effect. It is already active
  // when known and must never be sent to ActorEquipManager::EquipSpell.
  return magic.spell_type == 4;
}

const char* ItemActionLabel(const ItemState& item) {
  if (item.flags & kItemEquipped) return "UNEQUIP";
  switch (item.form_type) {
    case 0x29: case 0x1A: return "EQUIP";
    case 0x2E: return "USE";
    case 0x1E: return "EAT / USE";
    case 0x17: case 0x1B: case 0x30: return "READ";
    default: return "USE ITEM";
  }
}

}  // namespace

Ui& TheUi() { return g_ui; }

void SetNativeItemPreviewCallbacks(DrawNativeItemPreviewFn draw,
                                   RequestNativeItemPreviewFn request,
                                   BeginNativeItemPreviewCompositionFn begin) {
  g_draw_native_item_preview = draw;
  g_request_native_item_preview = request;
  g_begin_native_item_preview_composition = begin;
}

void SetNativeCharacterPreviewCallback(DrawNativeCharacterPreviewFn draw) {
  g_draw_native_character_preview = draw;
}

void SetNativeMapPreviewCallback(DrawNativeMapPreviewFn draw) {
  g_draw_native_map_preview = draw;
}

bool Ui::Create(int width, int height) {
  if (canvas_.ready() && canvas_.width() == width && canvas_.height() == height) return true;
  dirty_ = true;
  loading_visible_ = false;
  loading_dirty_ = true;
  rendered_sequence_ = UINT64_MAX;
  rendered_fingerprint_ = UINT64_MAX;
  have_rendered_state_ = false;
  const bool created = canvas_.Create(width, height);
  if (created) TheNativeAssets().ready();
  return created;
}

bool Ui::RenderLoading() {
  if (!canvas_.ready()) return false;
  if (loading_visible_ && !loading_dirty_) return false;
  if (!loading_visible_) {
    // A world boundary must not carry selection, scroll, or the private map
    // camera into the next save. Reset once on the live-to-loading transition.
    page_ = Page::kStatus;
    category_ = 0;
    magic_category_ = 0;
    selected_item_ = selected_magic_ = selected_quest_ = -1;
    selected_item_form_ = selected_magic_form_ = selected_quest_form_ = 0;
    equipment_scroll_ = inventory_scroll_ = magic_scroll_ = quest_scroll_ = 0;
    quest_objective_scroll_ = 0;
    character_rotation_degrees_ = 0.0f;
    character_zoom_ = 1.0f;
    ++character_control_revision_;
    map_zoom_ = 1.0f;
    map_pan_x_ = map_pan_y_ = 0.0f;
    map_anchor_x_ = map_anchor_y_ = 0.0f;
    map_anchor_world_id_ = 0;
    map_anchor_valid_ = false;
    last_character_tap_ms_ = last_map_tap_ms_ = 0;
    pointer_down_ = pointer_moved_ = false;
    drag_scroll_pixels_ = 0.0f;
    drag_target_ = DragTarget::kNone;
    last_map_marker_refresh_request_ms_ = 0;
    map_control_revision_ = 0;
  }
  const int width = canvas_.width();
  const int height = canvas_.height();
  canvas_.Clear(kBlack);

  // A restrained Skyrim-style waiting state: the panel is present as soon as
  // game data is available, but none of the interactive UI is exposed until a
  // valid in-world snapshot has arrived.
  const Box frame(24, 24, width - 48, height - 48);
  canvas_.VGradient(frame, kPanelTop, kPanelBottom);
  canvas_.StrokeRect(frame, 2, kEdgeBright);
  canvas_.StrokeRect(frame.Inset(6), 1, kEdge.Alpha(150));

  const int center_y = height / 2;
  canvas_.Line(width / 2 - 300, center_y - 68, width / 2 - 86, center_y - 68,
               1, kEdge);
  canvas_.Line(width / 2 + 86, center_y - 68, width / 2 + 300, center_y - 68,
               1, kEdge);
  canvas_.Text(Box(width / 2 - 110, center_y - 98, 220, 60), "SKYRIM",
               {24, FW_NORMAL}, kEdgeBright, Align::kCenter);
  canvas_.Text(Box(90, center_y - 28, width - 180, 58),
               "WAITING FOR WORLD TO LOAD", {31, FW_SEMIBOLD}, kText,
               Align::kCenter);
  canvas_.Text(Box(90, center_y + 32, width - 180, 40),
               "The interface will appear when the player is ready",
               {18, FW_NORMAL}, kMuted, Align::kCenter);
  canvas_.Line(width / 2 - 300, center_y + 96, width / 2 + 300, center_y + 96,
               1, kEdge);
  // The canvas now contains a loading frame. Force the first live snapshot to
  // replace it even if the new save happens to have the same visible values as
  // the previous world.
  loading_visible_ = true;
  loading_dirty_ = false;
  dirty_ = true;
  have_rendered_state_ = false;
  rendered_sequence_ = UINT64_MAX;
  rendered_fingerprint_ = UINT64_MAX;
  return true;
}

void Ui::DrawPanel(const Box& b, bool selected) {
  // One full-bleed interior and one Skyrim nine-slice frame.  The former
  // inset grey outline created a second, smaller "panel inside the panel" and
  // made rows extending into the real content area look misaligned.
  canvas_.FillRect(b, kBlack);
  canvas_.VGradient(b.Inset(2),
                    selected ? kPanelSelectedTop : kPanelTop,
                    selected ? kPanelSelectedBottom : kPanelBottom);
  canvas_.FillRect(Box(b.x + 3, b.y + 3, b.w - 6, 2),
                   (selected ? kWarm : kEdgeBright).Alpha(105));
  if (!TheNativeAssets().DrawFrame(
          canvas_, b, (selected ? kSelected : kEdgeBright).Alpha(215))) {
    canvas_.StrokeRect(b, selected ? 2 : 1,
                       selected ? kSelected : kEdgeBright.Alpha(205));
  }
}

void Ui::DrawHeader(const Box& b, const char* title) {
  const char* safe_title = title ? title : "";
  int title_size = 24;
  const int title_width = std::max(1, b.w - 116);
  while (title_size > 14 &&
         canvas_.TextWidth(safe_title, {title_size, FW_NORMAL}) >
             title_width)
    --title_size;
  canvas_.Text(Box(b.x + 58, b.y, b.w - 116, b.h - 7), safe_title,
               {title_size, FW_NORMAL}, kText, Align::kCenter);
  const int center = b.x + b.w / 2;
  const int ornament_y = b.bottom() - 8;
  canvas_.Line(b.x + 18, ornament_y, center - 22, ornament_y, 1, kEdge);
  canvas_.Line(center + 22, ornament_y, b.right() - 18, ornament_y, 1, kEdge);
  if (!TheNativeAssets().DrawDiamond(
          canvas_, Box(center - 9, ornament_y - 9, 18, 18),
          kEdgeBright.Alpha(205))) {
    canvas_.FillCircle(center, ornament_y, 3, kEdgeBright);
  }
}

void Ui::DrawMeter(const Box& b, const char* label, float current, float maximum,
                   const Color& color) {
  const bool available = maximum > 0.001f;
  const Box label_row(b.x, b.y, b.w, 32);
  canvas_.Text(Box(label_row.x, label_row.y, label_row.w * 9 / 20,
                   label_row.h), label, {17, FW_SEMIBOLD}, kText);
  canvas_.Text(Box(label_row.x + label_row.w * 9 / 20, label_row.y,
                   label_row.w * 11 / 20, label_row.h),
               available ? Pair(current, maximum) : "--/--",
               {17, FW_NORMAL}, available ? kText : kMuted, Align::kRight);
  const float ratio = available ? Ratio(current, maximum) : -1.0f;
  // HUDMenu_I2CC is authored at 193x22. Keep that aspect at the fixed
  // 1240-wide panel instead of flattening the native strip into an 8px line.
  const int native_height = std::clamp(
      static_cast<int>(std::lround(b.w * 22.0 / 193.0)), 18, 34);
  const Box native_track(b.x, b.bottom() - native_height, b.w, native_height);
  if (available &&
      TheNativeAssets().DrawMeter(canvas_, native_track, ratio, color)) {
    return;
  }
  {
    const Box track(b.x, b.bottom() - 12, b.w, 8);
    canvas_.FillRect(track, kSurface);
    if (available) {
      canvas_.FillRect(Box(track.x + 2, track.y + 2,
                           static_cast<int>((track.w - 4) * ratio),
                           track.h - 4), color);
    } else {
      for (int x = track.x + 2; x < track.right() - 2; x += 10)
        canvas_.Line(x, track.bottom() - 2, x + 6, track.y + 2, 1,
                     kMuted.Alpha(120));
    }
    canvas_.StrokeRect(track, 1, kEdgeBright);
  }
}

void Ui::DrawNavigation() {
  const Box navigation(kNavX, kNavY, canvas_.width() - kNavX * 2, kNavH);
  DrawPanel(navigation);
  const NavigationLayout layout = BuildNavigationLayout(canvas_.width(), page_);
  auto draw_arrow = [&](const Box& button, bool right) {
    canvas_.VGradient(button, kSurfaceTop, kSurfaceBottom);
    canvas_.StrokeRect(button, 1, kEdgeBright.Alpha(175));
    canvas_.FillRect(Box(button.x + 2, button.y + 2, button.w - 4, 1),
                     kEdgeBright.Alpha(90));
    if (!TheNativeAssets().DrawArrow(canvas_, button.Inset(17), right, kWarm)) {
      const int direction = right ? 1 : -1;
      const int cx = button.x + button.w / 2;
      const int cy = button.y + button.h / 2;
      canvas_.Line(cx - direction * 8, cy - 14, cx + direction * 8, cy, 3, kWarm);
      canvas_.Line(cx + direction * 8, cy, cx - direction * 8, cy + 14, 3, kWarm);
    }
  };
  draw_arrow(layout.left, false);
  draw_arrow(layout.right, true);
  for (int visible = 0; visible < kVisiblePageCount; ++visible) {
    const int page_index = layout.pages[visible];
    const Box item = layout.tabs[visible];
    const bool selected = static_cast<int>(page_) == page_index;
    if (selected) {
      canvas_.VGradient(item, kSurfaceSelectedTop, kSurfaceSelectedBottom);
      canvas_.StrokeRect(item, 2, kSelected.Alpha(225));
      canvas_.FillRect(Box(item.x + 8, item.bottom() - 4, item.w - 16, 2),
                       kWarm);
      TheNativeAssets().DrawDiamond(
          canvas_, Box(item.x + item.w / 2 - 9, item.bottom() - 12, 18, 18),
          kWarm);
    } else {
      canvas_.VGradient(item, kSurfaceTop, kSurfaceBottom);
      canvas_.StrokeRect(item, 1, kEdge.Alpha(180));
    }
    const Box icon(item.x + 24, item.y + 13, 52, item.h - 26);
    const Color icon_color = selected ? kWarm : kMuted;
    DrawNavIcon(canvas_, page_index, icon, icon_color);
    canvas_.Text(Box(item.x + 88, item.y + 4, item.w - 100, item.h - 8),
                 PageName(static_cast<Page>(page_index)),
                 {18, selected ? FW_SEMIBOLD : FW_NORMAL},
                 selected ? kText : kMuted, Align::kCenter);
  }
  const int dots_w = kNavigablePageCount * 14;
  const int dots_x = navigation.x + (navigation.w - dots_w) / 2;
  const int selected_dot = NavigablePageIndex(page_);
  for (int index = 0; index < kNavigablePageCount; ++index) {
    const int cx = dots_x + index * 14 + 7;
    const int cy = navigation.bottom() - 7;
    if (index == selected_dot)
      canvas_.FillCircle(cx, cy, 3, kWarm);
    else
      canvas_.StrokeCircle(cx, cy, 2, 1, kEdgeBright.Alpha(150));
  }
}

void Ui::DrawDashboard(const Snapshot& s) {
  const int right = canvas_.width() - kContentRightMargin;
  const int resource_gap = 9;
  const int resource_w = (right - kContentX - resource_gap * 2) / 3;
  const Box health(kContentX, kContentY, resource_w, kResourcePanelHeight);
  const Box magicka(health.right() + resource_gap, kContentY, resource_w,
                    kResourcePanelHeight);
  const Box stamina(magicka.right() + resource_gap, kContentY,
                    right - magicka.right() - resource_gap,
                    kResourcePanelHeight);
  DrawPanel(health); DrawPanel(magicka); DrawPanel(stamina);
  DrawMeter(Box(health.x + 24, health.y + 22, health.w - 48,
                kResourcePanelHeight - 44),
            "HEALTH", s.health, s.max_health, kHealth);
  DrawMeter(Box(magicka.x + 24, magicka.y + 22, magicka.w - 48,
                kResourcePanelHeight - 44),
            "MAGICKA", s.magicka, s.max_magicka, kMagicka);
  DrawMeter(Box(stamina.x + 24, stamina.y + 22, stamina.w - 48,
                kResourcePanelHeight - 44),
            "STAMINA", s.stamina, s.max_stamina, kStamina);

  const int middle_y = health.bottom() + 8;
  const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
  const int middle_h = hotbar_y - middle_y - 8;
  const int column_gap = 9;
  const int column_w = (right - kContentX - column_gap * 2) / 3;
  const Box stats(kContentX, middle_y, column_w, middle_h);
  const Box preview(stats.right() + column_gap, middle_y, column_w, middle_h);
  const Box loadout(preview.right() + column_gap, middle_y,
                    right - preview.right() - column_gap, middle_h);
  DrawPanel(stats); DrawPanel(preview, true); DrawPanel(loadout);
  DrawHeader(Box(stats.x + 12, stats.y + 12, stats.w - 24, 54), "PLAYER STATS");

  char line[128] = {};
  const int stats_list_top = stats.y + 78;
  const int stats_footer_y = stats.bottom() - kStatsFooterHeight;
  constexpr int kStatRowCount = 6;
  const int stat_stride = std::max(
      56, (stats_footer_y - 10 - stats_list_top) / kStatRowCount);
  const int stat_card_height = std::max(50, stat_stride - 7);
  auto stat_row = [&](int row, const char* label, const char* value,
                       const Color& value_color = kText,
                       float progress = -1.0f) {
    const Box row_box(stats.x + 18, stats_list_top + row * stat_stride,
                      stats.w - 36, stat_card_height);
    canvas_.VGradient(row_box, kSurfaceTop, kSurfaceBottom);
    canvas_.StrokeRect(row_box, 1, kEdge.Alpha(165));
    canvas_.Text(Box(row_box.x + 14, row_box.y, row_box.w / 2, row_box.h),
                 label, {15, FW_NORMAL}, kMuted);
    canvas_.Text(Box(row_box.x + row_box.w / 2, row_box.y,
                     row_box.w / 2 - 14, row_box.h), value,
                 {20, FW_SEMIBOLD}, value_color, Align::kRight);
    if (progress >= 0.0f) {
      canvas_.Bar(Box(row_box.x + 14, row_box.bottom() - 8,
                      row_box.w - 28, 5), std::clamp(progress, 0.0f, 1.0f),
                  kSelected, kSurface);
    }
  };
  std::snprintf(line, sizeof(line), "%u", s.level);
  stat_row(0, "LEVEL", line, kText,
           std::max(0.0f, Ratio(s.level_xp, s.level_xp_max)));
  std::snprintf(line, sizeof(line), "%d", s.gold);
  stat_row(1, "GOLD", line, kWarm);
  std::snprintf(line, sizeof(line), "%.0f / %.0f", s.carry, s.max_carry);
  const float carry_ratio = Ratio(s.carry, s.max_carry);
  stat_row(2, "CARRY", line,
           carry_ratio >= 1.0f ? kHealth
           : carry_ratio >= 0.9f ? kWarm : kText);
  std::snprintf(line, sizeof(line), "%u", s.perk_points);
  stat_row(3, "PERK POINTS", line);
  const std::string armor_value = Number(s.armor_rating);
  const std::string damage_value = Number(s.damage_rating);
  stat_row(4, "ARMOR", armor_value.c_str(), kSelected);
  stat_row(5, "DAMAGE", damage_value.c_str(), kWarm);
  canvas_.Line(stats.x + 22, stats_footer_y - 10, stats.right() - 22,
               stats_footer_y - 10, 1, kEdge);
  canvas_.Text(Box(stats.x + 24, stats_footer_y, stats.w - 48, 22),
               "CURRENT LOCATION", {13, FW_NORMAL}, kMuted);
  canvas_.TextWrap(Box(stats.x + 24, stats_footer_y + 22, stats.w - 48, 46),
                   s.location[0] ? s.location : "Skyrim",
                   {18, FW_SEMIBOLD}, kText);

  int equipped_count = 0;
  for (uint32_t i = 0; i < s.item_count && i < kMaxItems; ++i) {
    const ItemState& item = s.items[i];
    if ((item.flags & kItemEquipped) == 0) continue;
    ++equipped_count;
  }
  const char* const player_identity =
      s.player_name[0] ? s.player_name : "PLAYER";
  DrawHeader(Box(preview.x + 14, preview.y + 12, preview.w - 28, 54),
             player_identity);
  const Box portrait = CharacterPreviewBounds();
  canvas_.VGradient(portrait, kPreviewTop, kPreviewBottom);
  canvas_.StrokeRect(portrait, 1, kEdge.Alpha(190));
  const bool portrait_drawn = g_draw_native_character_preview &&
      g_draw_native_character_preview(canvas_, portrait);
  if (!portrait_drawn) {
    // Asset assembly is asynchronous and never blocks the game/update thread.
    // Keep this honest loading state until a complete custom-renderer frame is
    // available; no screenshot, generic actor, or world-camera fallback is
    // substituted.
    const int center_y = portrait.y + portrait.h / 2;
    canvas_.Line(portrait.x + 34, center_y - 54,
                 portrait.right() - 34, center_y - 54, 1,
                 kEdge.Alpha(105));
    const std::string assembly_label =
        std::string("ASSEMBLING ") + player_identity + "...";
    canvas_.Text(Box(portrait.x + 18, center_y - 38,
                     portrait.w - 36, 38),
                 assembly_label.c_str(), {19, FW_SEMIBOLD}, kText,
                 Align::kCenter);
    canvas_.TextWrap(Box(portrait.x + 32, center_y + 8,
                         portrait.w - 64, 72),
                     "Resolving player records, equipment, meshes, and "
                     "materials.",
                     {14, FW_NORMAL}, kMuted, Align::kCenter);
  }
  DrawHeader(Box(loadout.x + 12, loadout.y + 12, loadout.w - 24, 54), "EQUIPPED");
  struct SlotView { EquipmentSlot slot; uint32_t hand_flag; };
  static constexpr SlotView slots[] = {
      {EquipmentSlot::kRightHand, kItemRightHand},
      {EquipmentSlot::kLeftHand, kItemLeftHand},
      {EquipmentSlot::kHead, 0}, {EquipmentSlot::kBody, 0},
      {EquipmentSlot::kHands, 0}, {EquipmentSlot::kFeet, 0},
      {EquipmentSlot::kRing, 0}, {EquipmentSlot::kAmulet, 0},
  };
  const int list_top = loadout.y + 78;
  const int summary_y = loadout.bottom() - kEquipmentSummaryHeight;
  constexpr int equipment_card_height = 65;
  constexpr int equipment_row_stride = 73;
  const int visible_rows = std::max(
      1, (summary_y - kEquipmentListEndGap - list_top) /
             equipment_row_stride);
  equipment_scroll_ = std::clamp(equipment_scroll_, 0,
                                 std::max(0, 8 - visible_rows));
  for (int visible = 0; visible < visible_rows; ++visible) {
    const int slot_index = equipment_scroll_ + visible;
    if (slot_index >= 8) break;
    const Box item_box(loadout.x + 18,
                       list_top + visible * equipment_row_stride,
                       loadout.w - 36, equipment_card_height);
    const SlotView& view = slots[slot_index];
    const ItemState* equipped = nullptr;
    const MagicState* equipped_magic = nullptr;
    for (uint32_t i = 0; i < s.item_count && i < kMaxItems; ++i) {
      const ItemState& candidate = s.items[i];
      if ((candidate.flags & kItemEquipped) == 0) continue;
      if ((view.hand_flag && (candidate.flags & view.hand_flag)) ||
          (!view.hand_flag && candidate.equip_slot == view.slot)) {
        equipped = &candidate;
        break;
      }
    }
    // Actor::selectedSpells is distinct from equipped inventory objects.
    // The old panel only searched ItemState, which guaranteed that an actual
    // left-hand spell was labelled EMPTY.
    if (!equipped && view.slot == EquipmentSlot::kLeftHand) {
      for (uint32_t i = 0; i < s.magic_count && i < kMaxMagic; ++i) {
        if (s.magic[i].flags & kMagicLeftHand) {
          equipped_magic = &s.magic[i];
          break;
        }
      }
    } else if (!equipped && view.slot == EquipmentSlot::kRightHand) {
      for (uint32_t i = 0; i < s.magic_count && i < kMaxMagic; ++i) {
        if (s.magic[i].flags & kMagicRightHand) {
          equipped_magic = &s.magic[i];
          break;
        }
      }
    }
    const bool occupied = equipped || equipped_magic;
    canvas_.VGradient(item_box,
                      occupied ? kSurfaceSelectedTop : kSurfaceTop,
                      occupied ? kSurfaceSelectedBottom : kSurfaceBottom);
    canvas_.StrokeRect(item_box, 1,
                       occupied ? kEdgeBright.Alpha(190) : kEdge.Alpha(125));
    const Box thumbnail(item_box.x + 7, item_box.y + 4,
                        item_box.h - 8, item_box.h - 8);
    canvas_.FillRect(thumbnail, kSurface);
    canvas_.StrokeRect(thumbnail, 1, kEdge.Alpha(125));
    if (equipped)
      DrawOrRequestItemPreview(canvas_, thumbnail, equipped->form_id);
    else if (equipped_magic && equipped_magic->preview_form_id)
      DrawOrRequestItemPreview(canvas_, thumbnail,
                               equipped_magic->preview_form_id);
    const int text_x = thumbnail.right() + 12;
    canvas_.Text(Box(text_x, item_box.y + 23,
                     item_box.right() - text_x - 10, item_box.h - 25),
                     equipped && equipped->name[0]
                         ? equipped->name
                         : equipped_magic && equipped_magic->name[0]
                               ? equipped_magic->name
                               : "EMPTY",
                     {17, occupied ? FW_SEMIBOLD : FW_NORMAL},
                     occupied ? kText : kMuted.Alpha(125));
    // The labels are intentionally larger on the physical lower display.
    canvas_.Text(Box(text_x, item_box.y + 1,
                     item_box.right() - text_x - 10, 23),
                 EquipmentSlotName(view.slot), {14, FW_NORMAL}, kMuted);
    if (occupied)
      canvas_.FillRect(Box(item_box.x, item_box.y, 3, item_box.h), kWarm);
  }
  if (visible_rows < 8) {
    const int track_y = list_top;
    const int track_h = std::max(
        1, summary_y - kEquipmentListEndGap - list_top);
    const int thumb_h = std::max(24, track_h * visible_rows / 8);
    const int travel = std::max(0, track_h - thumb_h);
    const int maximum = std::max(1, 8 - visible_rows);
    const int thumb_y = track_y + travel * equipment_scroll_ / maximum;
    canvas_.FillRect(Box(loadout.right() - 10, track_y, 2, track_h),
                     kEdge.Alpha(100));
    canvas_.FillRect(Box(loadout.right() - 12, thumb_y, 6, thumb_h),
                     kEdgeBright.Alpha(210));
  }
  canvas_.Line(loadout.x + 22, summary_y - 8, loadout.right() - 22,
               summary_y - 8, 1, kEdge);
  canvas_.Text(Box(loadout.x + 24, summary_y, loadout.w - 48, 27),
               "ARMOR  " + Number(s.armor_rating) + "     DAMAGE  " +
                   Number(s.damage_rating),
               {17, FW_SEMIBOLD}, kText, Align::kCenter);
  canvas_.Text(Box(loadout.x + 24, summary_y + 27, loadout.w - 48, 23),
               std::to_string(equipped_count) + " PIECES EQUIPPED",
               {13, FW_NORMAL}, kMuted, Align::kCenter);

  DrawFavorites(s);
}

Box Ui::CharacterPreviewBounds() const {
  if (!canvas_.ready()) return {};
  const int right = canvas_.width() - kContentRightMargin;
  const int middle_y = kContentY + kResourcePanelHeight + 8;
  const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
  const int middle_h = hotbar_y - middle_y - 8;
  const int column_gap = 9;
  const int column_w = (right - kContentX - column_gap * 2) / 3;
  const Box preview(kContentX + column_w + column_gap, middle_y,
                    column_w, middle_h);
  // The portrait itself is the interaction affordance.  Reserve only the
  // panel header and a narrow visual gutter; the old instruction strip and
  // oversized insets made the 400x640 character target look like a thumbnail.
  return Box(preview.x + 10, preview.y + 72, preview.w - 20,
             preview.h - 84);
}

int Ui::CategoryFor(uint32_t type) const {
  switch (type) {
    case 0x29: case 0x2A: return 1;  // weapon/ammo
    case 0x1A: return 2;
    case 0x2E: return 3;
    case 0x1E: return 4;
    case 0x17: case 0x1B: case 0x30: return 5;  // scroll/book/note
    default: return 6;
  }
}

std::vector<int> Ui::FilteredItems(const Snapshot& state) const {
  std::vector<int> result;
  for (uint32_t i = 0; i < state.item_count && i < kMaxItems; ++i) {
    if (state.items[i].form_id == 0 || state.items[i].count <= 0) continue;
    if (category_ == 0 || CategoryFor(state.items[i].form_type) == category_) result.push_back(i);
  }
  return result;
}

void Ui::DrawInventory(const Snapshot& s) {
  const int right = canvas_.width() - kContentRightMargin;
  const int bottom = canvas_.height() - kContentBottomMargin;
  const Box categories(kContentX, kContentY, right - kContentX, 92);
  // Give the native model card a true detail-column footprint. The previous
  // 55/45 split constrained it to a 240 px square even on the Thor panel.
  const int list_width = DetailListWidth(right);
  const Box list(kContentX, categories.bottom() + 8, list_width,
                 bottom - categories.bottom() - 8);
  const Box details(list.right() + 8, categories.bottom() + 8,
                    right - list.right() - 8, bottom - categories.bottom() - 8);
  DrawPanel(categories); DrawPanel(list); DrawPanel(details);

  const int cat_gap = 6;
  const int cat_w = (categories.w - 24 - cat_gap * 6) / 7;
  for (int i = 0; i < 7; ++i) {
    Box row(categories.x + 12 + i * (cat_w + cat_gap), categories.y + 12,
            cat_w, categories.h - 24);
    DrawCategoryButton(canvas_, row, CategoryName(i), i == category_, 14);
  }

  const int name_width = list.w - 292;
  std::vector<int> filtered = FilteredItems(s);
  // The large detail model is the item the player is actively inspecting.
  // Prime it before list thumbnails consume this frame's bounded request
  // budget. The later draw call is de-duplicated by FormID.
  if (selected_item_ >= 0 &&
      selected_item_ < static_cast<int>(std::min(s.item_count, kMaxItems))) {
    RequestItemPreviewBudgeted(s.items[selected_item_].form_id, true);
  }
  canvas_.Text(Box(list.x + 58, list.y + 8, name_width - 40, 42),
               std::string("ITEM  /  ") + std::to_string(filtered.size()),
               {17, FW_SEMIBOLD}, kMuted);
  canvas_.Text(Box(list.right() - 246, list.y + 8, 64, 42), "DMG", {17, FW_SEMIBOLD}, kMuted, Align::kRight);
  canvas_.Text(Box(list.right() - 166, list.y + 8, 58, 42), "WT", {17, FW_SEMIBOLD}, kMuted, Align::kRight);
  canvas_.Text(Box(list.right() - 88, list.y + 8, 62, 42), "VAL", {17, FW_SEMIBOLD}, kMuted, Align::kRight);
  canvas_.Line(list.x + 12, list.y + 50, list.right() - 12, list.y + 50, 1, kEdgeBright);

  const int row_h = 62;
  const int visible_rows = std::max(1, (list.h - 62) / row_h);
  inventory_scroll_ = std::clamp(
      inventory_scroll_, 0,
      std::max(0, static_cast<int>(filtered.size()) - visible_rows));
  for (int row = 0; row < visible_rows; ++row) {
    const int at = inventory_scroll_ + row;
    if (at >= static_cast<int>(filtered.size())) break;
    const int index = filtered[at];
    const ItemState& item = s.items[index];
    Box r(list.x + 8, list.y + 56 + row * row_h, list.w - 16, row_h - 3);
    const bool selected = index == selected_item_;
    canvas_.VGradient(r,
                      selected ? kSurfaceSelectedTop
                               : (row & 1) ? kRowAltTop : kRowTop,
                      selected ? kSurfaceSelectedBottom
                               : (row & 1) ? kRowAltBottom : kRowBottom);
    if (selected) canvas_.StrokeRect(r, 1, kSelected.Alpha(145));
    if (item.flags & kItemEquipped) canvas_.FillRect(Box(r.x, r.y, 4, r.h), kWarm);
    std::string name = item.name[0] ? item.name : "Unknown item";
    if (item.count > 1) name += " (" + std::to_string(item.count) + ")";
    const Box item_preview(r.x + 8, r.y + 4, 50, r.h - 8);
    canvas_.FillRect(item_preview, kSurface);
    canvas_.StrokeRect(item_preview, 1, kEdge.Alpha(130));
    DrawOrRequestItemPreview(canvas_, item_preview, item.form_id);
    canvas_.Text(Box(r.x + 62, r.y, name_width - 44, r.h), name,
                 {18, selected ? FW_SEMIBOLD : FW_NORMAL},
                 selected ? kText : kMuted);
    const float stat = item.damage > 0.0f ? item.damage : item.armor;
    canvas_.Text(Box(r.right() - 246, r.y, 64, r.h), stat > 0.0f ? Number(stat) : "-",
                 {19, FW_NORMAL}, selected ? kText : kMuted, Align::kRight);
    canvas_.Text(Box(r.right() - 166, r.y, 58, r.h), Number(item.weight, 1), {19, FW_NORMAL},
                 selected ? kText : kMuted, Align::kRight);
    canvas_.Text(Box(r.right() - 88, r.y, 62, r.h), std::to_string(item.value), {19, FW_NORMAL},
                 selected ? kText : kMuted, Align::kRight);
    canvas_.Line(r.x + 8, r.bottom(), r.right() - 8, r.bottom(), 1, kEdge.Alpha(150));
  }
  if (filtered.empty()) {
    canvas_.TextWrap(Box(list.x + 48, list.y + 160, list.w - 96, 130),
                     std::string("NO ") + CategoryName(category_) +
                         " ITEMS FOUND",
                     {21, FW_NORMAL}, kMuted, Align::kCenter);
  }
  DrawScrollBar(canvas_, Box(list.right() - 12, list.y + 58, 6,
                             list.h - 72), inventory_scroll_, visible_rows,
                static_cast<int>(filtered.size()));

  const ItemState* selected = nullptr;
  if (selected_item_ >= 0 &&
      selected_item_ < static_cast<int>(std::min(s.item_count, kMaxItems)))
    selected = &s.items[selected_item_];
  DrawHeader(Box(details.x + 12, details.y + 12, details.w - 24, 60),
             selected && selected->name[0] ? selected->name : "SELECT AN ITEM");
  if (selected) {
    const int available_preview_height = std::max(220, details.h - 430);
    const int icon_size = std::min({360, details.w - 72,
                                    available_preview_height});
    const Box icon(details.x + (details.w - icon_size) / 2, details.y + 82,
                   icon_size, icon_size);
    canvas_.VGradient(icon, kPreviewTop, kPreviewBottom);
    canvas_.StrokeRect(icon, 1, kEdgeBright.Alpha(180));
    DrawOrRequestItemPreview(canvas_, icon.Inset(4), selected->form_id, true);
    canvas_.Text(Box(details.x + 30, icon.bottom() + 7, details.w - 60, 32),
                 CategoryName(CategoryFor(selected->form_type)),
                 {18, FW_NORMAL}, kWarm, Align::kCenter);
    std::string badges;
    if (selected->flags & kItemEquipped) badges = "EQUIPPED";
    if (selected->flags & kItemFavorite)
      badges += badges.empty() ? "FAVORITE" : "   /   FAVORITE";
    if (!badges.empty())
      canvas_.Text(Box(details.x + 30, icon.bottom() + 34,
                       details.w - 60, 26), badges, {13, FW_SEMIBOLD},
                   kSelected, Align::kCenter);
    const int y = icon.bottom() + 66;
    auto detail_row = [&](int row, const char* label, const std::string& value,
                          const Color& value_color = kText) {
      const Box metric(details.x + 38, y + row * 46, details.w - 76, 40);
      canvas_.Text(Box(metric.x, metric.y, metric.w / 2, metric.h), label,
                   {17, FW_NORMAL}, kMuted);
      canvas_.Text(Box(metric.x + metric.w / 2, metric.y, metric.w / 2,
                       metric.h), value, {20, FW_SEMIBOLD}, value_color,
                   Align::kRight);
      canvas_.Line(metric.x, metric.bottom(), metric.right(), metric.bottom(),
                   1, kEdge.Alpha(130));
    };
    int metric_count = 0;
    if (selected->armor > 0.0f || selected->damage > 0.0f) {
      detail_row(metric_count++, selected->armor > 0.0f ? "ARMOR" : "DAMAGE",
                 Number(selected->armor > 0.0f ? selected->armor
                                                : selected->damage));
    }
    detail_row(metric_count++, "WEIGHT", Number(selected->weight, 1));
    detail_row(metric_count++, "VALUE", std::to_string(selected->value),
               kWarm);
    if (selected->count > 1)
      detail_row(metric_count++, "QUANTITY", std::to_string(selected->count));
    const bool show_description =
        (selected->form_type == 0x29 || selected->form_type == 0x1A) &&
        selected->description[0];
    if (show_description) {
      const int description_y = y + metric_count * 46 + 14;
      canvas_.TextWrap(Box(details.x + 38, description_y, details.w - 76,
                           std::max(60, details.bottom() - description_y - 184)),
                       selected->description, {17, FW_NORMAL}, kText);
    }
    const Box use(details.x + 30, details.bottom() - 154,
                  details.w - 60, 58);
    DrawPanel(use, true);
    canvas_.Text(use, ItemActionLabel(*selected),
                 {22, FW_SEMIBOLD}, kText, Align::kCenter);
    const Box hotbar(details.x + 30, details.bottom() - 88,
                     details.w - 60, 58);
    DrawPanel(hotbar, true);
    canvas_.Text(hotbar, "ADD TO HOTBAR", {20, FW_SEMIBOLD}, kWarm,
                 Align::kCenter);
  } else {
    canvas_.TextWrap(Box(details.x + 55, details.y + 190,
                         details.w - 110, 150),
                     "SELECT AN ITEM TO VIEW ITS MODEL, STATS, EFFECTS, AND "
                     "AVAILABLE ACTIONS.",
                     {20, FW_NORMAL}, kMuted, Align::kCenter);
  }
}

void Ui::DrawFavorites(const Snapshot& s) {
  const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
  const Box panel(kContentX, hotbar_y,
                  canvas_.width() - kContentX - kContentRightMargin,
                  canvas_.height() - hotbar_y - kContentBottomMargin);
  DrawPanel(panel);
  canvas_.Text(Box(panel.x + 22, panel.y + 10, 260, 34), "HOTBAR",
               {19, FW_SEMIBOLD}, kText);
  uint32_t occupied_slots = 0;
  for (uint32_t i = 0; i < std::min(s.favorite_count, kMaxFavorites); ++i)
    if (s.favorites[i]) ++occupied_slots;
  const std::string hotbar_hint = hotbar_assignment_form_
      ? "CHOOSE A SLOT FOR THE SELECTED ITEM"
      : std::to_string(occupied_slots) +
            " / 8 ASSIGNED     TAP TO EQUIP OR USE";
  canvas_.Text(Box(panel.right() - 520, panel.y + 10, 495, 34),
               hotbar_hint, {14, FW_NORMAL},
               hotbar_assignment_form_ ? kWarm : kMuted, Align::kRight);
  const int gap = 10;
  const int slot_size = std::min(panel.h - 62, (panel.w - 44 - gap * 7) / 8);
  const int total_width = slot_size * 8 + gap * 7;
  const int start_x = panel.x + (panel.w - total_width) / 2;
  const uint32_t favorite_count = std::min(s.favorite_count, kMaxFavorites);
  const uint32_t item_count = std::min(s.item_count, kMaxItems);
  const uint32_t magic_count = std::min(s.magic_count, kMaxMagic);
  for (int i = 0; i < 8; ++i) {
    const Box slot(start_x + i * (slot_size + gap), panel.y + 50,
                   slot_size, slot_size);
    uint32_t id = i < static_cast<int>(favorite_count) ? s.favorites[i] : 0;
    const ItemState* item = nullptr;
    for (uint32_t n = 0; n < item_count; ++n) if (s.items[n].form_id == id) { item = &s.items[n]; break; }
    const MagicState* magic = nullptr;
    for (uint32_t n = 0; n < magic_count; ++n)
      if (s.magic[n].form_id == id) { magic = &s.magic[n]; break; }
    const bool item_active = item && (item->flags & kItemEquipped);
    const bool magic_active = magic &&
        (magic->flags & (kMagicEquipped | kMagicLeftHand |
                         kMagicRightHand | kMagicVoice));
    const bool active = item_active || magic_active;
    const Color active_accent = magic_active ? kCyan : kWarm;
    canvas_.VGradient(slot,
                      active ? kSurfaceSelectedTop : kRowTop,
                      active ? kSurfaceSelectedBottom : kRowBottom);
    canvas_.StrokeRect(slot, hotbar_assignment_form_ || active ? 2 : 1,
                       hotbar_assignment_form_ ? kWarm
                           : active ? active_accent
                                    : kEdgeBright.Alpha(205));
    canvas_.FillRect(Box(slot.x + 1, slot.y + 1, slot.w - 2, 2),
                     (active ? active_accent : kEdge).Alpha(120));
    canvas_.Text(Box(slot.x + 8, slot.y + 5, 28, 23), std::to_string(i + 1),
                 {15, FW_SEMIBOLD}, kWarm);
    if (item) {
      const int preview_size = std::min(68, slot.w - 30);
      const Box preview(slot.x + (slot.w - preview_size) / 2,
                        slot.y + 25, preview_size, preview_size);
      DrawOrRequestItemPreview(canvas_, preview, item->form_id);
      canvas_.Text(Box(slot.x + 8, slot.y + 94, slot.w - 16,
                       slot.h - 101),
                   item->name, {14, FW_SEMIBOLD}, kText, Align::kCenter);
      if (item->count > 1) {
        const Box badge(slot.right() - 44, slot.y + 6, 35, 23);
        canvas_.FillRect(badge, kSurface.Alpha(235));
        canvas_.StrokeRect(badge, 1, kEdge.Alpha(180));
        canvas_.Text(badge, std::to_string(item->count), {13, FW_SEMIBOLD},
                     kText, Align::kCenter);
      }
      if (active)
        canvas_.FillRect(Box(slot.x + 7, slot.bottom() - 5,
                            slot.w - 14, 3), active_accent);
    } else if (magic) {
      if (magic->preview_form_id) {
        // Skyrim's spell MDOB artwork has considerably more transparent
        // breathing room than inventory meshes. Give it most of the card so
        // the actual rune/element silhouette is legible on the Thor panel.
        const int preview_size = std::min(104, slot.w - 14);
        DrawOrRequestItemPreview(
            canvas_, Box(slot.x + (slot.w - preview_size) / 2,
                         slot.y + 15, preview_size, preview_size),
            magic->preview_form_id);
      }
      canvas_.Text(Box(slot.x + 8, slot.bottom() - 39, slot.w - 16, 31),
                   magic->name[0] ? magic->name : "MAGIC",
                   {13, FW_SEMIBOLD}, kText, Align::kCenter);
      if (magic_active) {
        canvas_.StrokeRect(slot.Inset(4), 1, kCyan.Alpha(155));
        canvas_.FillRect(Box(slot.x + 7, slot.bottom() - 5,
                            slot.w - 14, 3), kCyan);
      }
    } else if (id) {
      canvas_.Text(Box(slot.x + 8, slot.y + 28, slot.w - 16, slot.h - 36),
                   "UNAVAILABLE", {12, FW_SEMIBOLD}, kMuted,
                   Align::kCenter);
    } else {
      canvas_.Text(Box(slot.x + 8, slot.y + 33, slot.w - 16, slot.h - 42),
                   "EMPTY", {13, FW_NORMAL}, kMuted.Alpha(105),
                   Align::kCenter);
    }
  }
}

void Ui::DrawMap(const Snapshot& s) {
  const MapLayout layout = BuildMapLayout(canvas_.width(), canvas_.height());
  DrawPanel(layout.panel);
  canvas_.VGradient(layout.header, kSurfaceTop, kSurfaceBottom);
  canvas_.FillRect(layout.viewport, kBlack);
  canvas_.StrokeRect(layout.viewport, 1, kEdgeBright.Alpha(185));

  const ULONGLONG map_now = GetTickCount64();
  // Refresh markers on entry and re-arm a failed first native frame at a
  // bounded cadence. A terminal renderer failure must never strand this page
  // in a one-shot loading state; retries contain no pan/zoom input and cannot
  // manipulate the gameplay camera.
  const bool first_map_request = !last_map_marker_refresh_request_ms_;
  if (first_map_request && QueueMapMarkerRefresh(s))
    last_map_marker_refresh_request_ms_ = map_now;

  // The map page has exactly one terrain source: Skyrim's native world-map
  // pass rendered through the plugin's private NiCamera and offscreen target.
  // Never fall back to the old flat/CPU LOD texture when that pass is pending.
  const bool map_drawn = g_draw_native_map_preview &&
      g_draw_native_map_preview(canvas_, layout.viewport, map_zoom_,
                                map_pan_x_, map_pan_y_);
  if (!map_drawn) {
    if (!first_map_request && last_map_marker_refresh_request_ms_ &&
        map_now - last_map_marker_refresh_request_ms_ >= 3000 &&
        QueueMapPreview(s, 0.0f, 0.0f, 0.0f, 0))
      last_map_marker_refresh_request_ms_ = map_now;
    // A clean empty target is the only pre-first-frame state. Never substitute
    // a flat atlas, screenshot, or top-screen framebuffer.
    canvas_.VGradient(layout.viewport, Color(10, 14, 20),
                      Color(18, 25, 34));
  }
  canvas_.Line(layout.header.x, layout.header.bottom() - 1,
               layout.header.right(), layout.header.bottom() - 1,
               1, kEdgeBright.Alpha(165));
  canvas_.Text(Box(layout.header.x + 22, layout.header.y + 10,
                   layout.header.w - 44, 42),
               s.location[0] ? s.location : "SKYRIM", {25, FW_SEMIBOLD},
               kText, Align::kCenter);
  canvas_.Text(Box(layout.header.right() - 250, layout.header.y + 17,
                   225, 28),
               "NATIVE 3D MAP", {12, FW_NORMAL}, kCyan, Align::kRight);
  static ULONGLONG next_native_map_diagnostic = 0;
  if (map_now >= next_native_map_diagnostic) {
    DIS_INFO("[map-native] page world=%08X drawn=%u zoom=%.3f "
             "pan=(%.1f,%.1f) control_revision=%llu gameplay_camera_writes=0",
             s.world_form_id, map_drawn ? 1u : 0u, map_zoom_, map_pan_x_,
             map_pan_y_,
             static_cast<unsigned long long>(map_control_revision_));
    next_native_map_diagnostic = map_now + 5000;
  }
  return;

}

int Ui::MagicCategoryFor(const MagicState& magic) const {
  if (magic.spell_type == 2 || magic.spell_type == 3 ||
      magic.spell_type == 4 || magic.spell_type == 11)
    return 7;
  if (magic.school >= 18 && magic.school <= 22)
    return magic.school - 16;
  return 7;
}

std::vector<int> Ui::FilteredMagic(const Snapshot& state) const {
  std::vector<int> result;
  for (uint32_t i = 0; i < state.magic_count && i < kMaxMagic; ++i) {
    const MagicState& magic = state.magic[i];
    if (magic.form_id == 0) continue;
    if (magic_category_ == 0 ||
        (magic_category_ == 1 && (magic.flags & kMagicFavorite)) ||
        (magic_category_ > 1 && MagicCategoryFor(magic) == magic_category_))
      result.push_back(static_cast<int>(i));
  }
  return result;
}

void Ui::DrawMagic(const Snapshot& s) {
  const int right = canvas_.width() - kContentRightMargin;
  const int bottom = canvas_.height() - kContentBottomMargin;
  const Box categories(kContentX, kContentY, right - kContentX, 92);
  const int list_width = DetailListWidth(right);
  const Box list(kContentX, categories.bottom() + 8, list_width,
                 bottom - categories.bottom() - 8);
  const Box detail(list.right() + 8, categories.bottom() + 8,
                   right - list.right() - 8, bottom - categories.bottom() - 8);
  DrawPanel(categories); DrawPanel(list); DrawPanel(detail);

  const int category_gap = 6;
  const int category_width = (categories.w - 24 - category_gap * 7) / 8;
  for (int i = 0; i < 8; ++i) {
    const Box button(categories.x + 12 + i * (category_width + category_gap),
                     categories.y + 12, category_width, categories.h - 24);
    DrawCategoryButton(canvas_, button, MagicCategoryName(i),
                       i == magic_category_, 13);
  }

  std::vector<int> filtered = FilteredMagic(s);
  // Match Inventory: the selected large preview is promoted before visible
  // row thumbnails consume the bounded native-model request budget.
  if (selected_magic_ >= 0 &&
      selected_magic_ < static_cast<int>(std::min(s.magic_count, kMaxMagic)) &&
      s.magic[selected_magic_].preview_form_id)
    RequestItemPreviewBudgeted(s.magic[selected_magic_].preview_form_id,
                               true);
  const int school_x = list.right() - 258;
  const int cost_x = list.right() - 86;
  canvas_.Text(Box(list.x + 74, list.y + 8,
                   school_x - list.x - 86, 42),
               std::string("SPELL  /  ") + std::to_string(filtered.size()),
               {17, FW_SEMIBOLD}, kMuted);
  canvas_.Text(Box(school_x, list.y + 8, 156, 42), "SCHOOL",
               {17, FW_SEMIBOLD}, kMuted);
  canvas_.Text(Box(cost_x, list.y + 8, 62, 42), "COST",
               {17, FW_SEMIBOLD}, kMuted, Align::kRight);
  canvas_.Line(list.x + 12, list.y + 50, list.right() - 12, list.y + 50,
               1, kEdgeBright);

  const int row_h = 62;
  const int visible_rows = std::max(1, (list.h - 62) / row_h);
  magic_scroll_ = std::clamp(magic_scroll_, 0,
                             std::max(0, static_cast<int>(filtered.size()) -
                                             visible_rows));
  for (int row = 0; row < visible_rows; ++row) {
    const int at = magic_scroll_ + row;
    if (at >= static_cast<int>(filtered.size())) break;
    const int index = filtered[at];
    const MagicState& magic = s.magic[index];
    const Box r(list.x + 8, list.y + 56 + row * row_h, list.w - 16, row_h - 3);
    const bool selected = index == selected_magic_;
    canvas_.VGradient(r,
                      selected ? kSurfaceSelectedTop
                               : (row & 1) ? kRowAltTop : kRowTop,
                      selected ? kSurfaceSelectedBottom
                               : (row & 1) ? kRowAltBottom : kRowBottom);
    if (selected) canvas_.StrokeRect(r, 1, kSelected.Alpha(145));
    if (magic.flags & kMagicEquipped)
      canvas_.FillRect(Box(r.x, r.y, 4, r.h), kWarm);
    const Box spell_preview(r.x + 8, r.y + 4, 50, r.h - 8);
    canvas_.FillRect(spell_preview, kSurface);
    canvas_.StrokeRect(spell_preview, 1, kEdge.Alpha(130));
    if (magic.preview_form_id)
      DrawOrRequestItemPreview(canvas_, spell_preview,
                               magic.preview_form_id);
    const int row_school_x = r.right() - 250;
    const int row_cost_x = r.right() - 78;
    canvas_.Text(Box(r.x + 62, r.y, row_school_x - r.x - 72, r.h),
                   magic.name[0] ? magic.name : "Unknown magic",
                   {18, selected ? FW_SEMIBOLD : FW_NORMAL},
                  selected ? kText : kMuted);
    canvas_.Text(Box(row_school_x, r.y, 156, r.h), MagicSchoolName(magic.school),
                  {15, FW_NORMAL}, selected ? kText : kMuted);
    canvas_.Text(Box(row_cost_x, r.y, 58, r.h),
                   (magic.flags & kMagicCostKnown) ? Number(magic.cost) : "--",
                  {19, FW_NORMAL}, selected ? kText : kMuted, Align::kRight);
    if (magic.flags & kMagicFavorite)
      canvas_.Text(Box(row_school_x - 32, r.y + 5, 24, 22), "*",
                   {18, FW_SEMIBOLD},
                   kWarm, Align::kRight);
    canvas_.Line(r.x + 8, r.bottom(), r.right() - 8, r.bottom(),
                 1, kEdge.Alpha(150));
  }
  if (filtered.empty()) {
    canvas_.TextWrap(Box(list.x + 48, list.y + 170, list.w - 96, 130),
                     std::string("NO ") + MagicCategoryName(magic_category_) +
                         " MAGIC FOUND",
                     {21, FW_NORMAL}, kMuted, Align::kCenter);
  }
  DrawScrollBar(canvas_, Box(list.right() - 12, list.y + 58, 6,
                             list.h - 72), magic_scroll_, visible_rows,
                 static_cast<int>(filtered.size()));
  const MagicState* selected =
      selected_magic_ >= 0 &&
              selected_magic_ <
                  static_cast<int>(std::min(s.magic_count, kMaxMagic))
          ? &s.magic[selected_magic_]
          : nullptr;
  DrawHeader(Box(detail.x + 12, detail.y + 12, detail.w - 24, 60),
             selected && selected->name[0] ? selected->name : "SELECT MAGIC");
  if (selected) {
    // Use exactly the Inventory detail-card sizing contract. Native RGBA stays
    // square and the spell statistics follow the same vertical reading order
    // as item damage, weight, value, and quantity.
    const int available_preview_height = std::max(220, detail.h - 430);
    const int sigil_size = std::min({360, detail.w - 72,
                                     available_preview_height});
    const Box sigil(detail.x + (detail.w - sigil_size) / 2,
                    detail.y + 82, sigil_size, sigil_size);
    canvas_.VGradient(sigil, kPreviewTop, kPreviewBottom);
    canvas_.StrokeRect(sigil, 1, kEdgeBright.Alpha(180));
    if (!selected->preview_form_id ||
        !DrawOrRequestItemPreview(canvas_, sigil.Inset(4),
                                  selected->preview_form_id, true)) {
      canvas_.TextWrap(
          sigil.Inset(42),
          selected->preview_form_id
              ? "LOADING SKYRIM MENU MODEL"
              : "SKYRIM RECORD HAS NO MENU DISPLAY MODEL",
          {17, FW_NORMAL}, kMuted, Align::kCenter);
    }
    canvas_.Text(Box(detail.x + 30, sigil.bottom() + 4, detail.w - 60, 30),
                 MagicSchoolName(selected->school), {18, FW_NORMAL},
                 kWarm, Align::kCenter);
    const int info_y = sigil.bottom() + 42;
    auto magic_metric = [&](int row, const char* label,
                            const std::string& value,
                            const Color& value_color = kText) {
      const Box metric(detail.x + 38, info_y + row * 46,
                       detail.w - 76, 40);
      canvas_.Text(Box(metric.x, metric.y, metric.w / 2, metric.h), label,
                   {17, FW_NORMAL}, kMuted);
      canvas_.Text(Box(metric.x + metric.w / 2, metric.y,
                        metric.w / 2, metric.h), value,
                   {20, FW_SEMIBOLD}, value_color, Align::kRight);
      canvas_.Line(metric.x, metric.bottom(), metric.right(), metric.bottom(),
                   1, kEdge.Alpha(130));
    };
    magic_metric(0, "TYPE", MagicTypeName(selected->spell_type));
    magic_metric(1, "MAGICKA COST",
                 (selected->flags & kMagicCostKnown)
                     ? Number(selected->cost) : "--", kMagicka);
    std::string spell_status =
        (selected->flags & kMagicEquipped) ? "EQUIPPED" : "KNOWN";
    if (selected->flags & kMagicFavorite) spell_status += " / FAVORITE";
    magic_metric(2, "STATUS", spell_status,
                 (selected->flags & kMagicEquipped) ? kSelected
                 : (selected->flags & kMagicFavorite) ? kWarm : kText);
    const int description_y = info_y + 3 * 46 + 14;
    canvas_.TextWrap(Box(detail.x + 38, description_y, detail.w - 76,
                          std::max(34, detail.bottom() - description_y - 164)),
                     selected->description[0]
                         ? selected->description
                         : "No spell description is available.",
                     {16, FW_NORMAL}, selected->description[0] ? kText : kMuted);
    const Box equip(detail.x + 30, detail.bottom() - 154,
                    detail.w - 60, 58);
    const bool passive = MagicIsPassive(*selected);
    const bool equipped = (selected->flags & kMagicEquipped) != 0;
    DrawPanel(equip, !passive);
    canvas_.Text(equip, passive ? "PASSIVE / ALWAYS ACTIVE"
                                : equipped ? "UNEQUIP MAGIC" : "EQUIP MAGIC",
                 {20, FW_SEMIBOLD},
                 passive ? kMuted : kText, Align::kCenter);
    const Box hotbar(detail.x + 30, detail.bottom() - 88,
                     detail.w - 60, 58);
    DrawPanel(hotbar, true);
    canvas_.Text(hotbar, "ADD TO HOTBAR", {20, FW_SEMIBOLD}, kWarm,
                 Align::kCenter);
  } else {
    canvas_.TextWrap(Box(detail.x + 45, detail.y + 170, detail.w - 90, 140),
                     "No known magic is available in this category.",
                     {21, FW_NORMAL}, kMuted, Align::kCenter);
  }
}

void Ui::DrawQuests(const Snapshot& s) {
  const int right = canvas_.width() - kContentRightMargin;
  const Box list(kContentX, kContentY, kQuestListWidth,
                 canvas_.height() - kContentY - kContentBottomMargin);
  const Box detail(list.right() + 8, kContentY, right - list.right() - 8,
                    canvas_.height() - kContentY - kContentBottomMargin);
  const uint32_t quest_count = std::min(s.quest_count, kMaxQuests);
  DrawPanel(list); DrawPanel(detail);
  DrawHeader(Box(list.x + 10, list.y + 8, list.w - 20, 54),
              (std::string("QUESTS  /  ") +
               std::to_string(quest_count)).c_str());
  const int row_h = 76;
  const int visible_rows = std::max(1, (list.h - 74) / row_h);
  quest_scroll_ = std::clamp(
      quest_scroll_, 0,
      std::max(0, static_cast<int>(quest_count) - visible_rows));
  for (int row = 0; row < visible_rows; ++row) {
    int index = quest_scroll_ + row;
    if (index >= static_cast<int>(quest_count)) break;
    const QuestState& quest = s.quests[index];
    Box r(list.x + 10, list.y + 70 + row * row_h, list.w - 30, row_h - 4);
    const bool selected = index == selected_quest_;
    canvas_.VGradient(r,
                      selected ? kSurfaceSelectedTop
                               : (row & 1) ? kRowAltTop : kRowTop,
                      selected ? kSurfaceSelectedBottom
                               : (row & 1) ? kRowAltBottom : kRowBottom);
    if (selected) {
      canvas_.StrokeRect(r, 1, kSelected.Alpha(220));
    }
    if ((quest.flags & kQuestActive) &&
        !TheNativeAssets().DrawQuestMarker(
            canvas_, Box(r.x + 11, r.y + 15, 20, r.h - 30), 62,
            kSelected))
      canvas_.FillRect(Box(r.x + 14, r.y + 12, 5, r.h - 24), kSelected);
    const Color quest_color = (quest.flags & kQuestFailed) ? Color(181, 88, 78)
                              : (quest.flags & kQuestCompleted) ? kMuted
                                                                : (selected ? kText : kMuted);
    canvas_.Text(Box(r.x + 40, r.y, r.w - 52, r.h), quest.name[0] ? quest.name : "Unnamed quest",
                  {21, selected ? FW_SEMIBOLD : FW_NORMAL}, quest_color);
    canvas_.Line(r.x + 8, r.bottom(), r.right() - 8, r.bottom(), 1, kEdge);
  }
  if (!quest_count) {
    canvas_.TextWrap(Box(list.x + 45, list.y + 180, list.w - 90, 150),
                     "NO ACTIVE OR COMPLETED QUESTS ARE AVAILABLE.",
                     {20, FW_NORMAL}, kMuted, Align::kCenter);
  }
  DrawScrollBar(canvas_, Box(list.right() - 13, list.y + 70, 6,
                              list.h - 84), quest_scroll_, visible_rows,
                static_cast<int>(quest_count));
  const QuestState* selected = selected_quest_ >= 0 && selected_quest_ < static_cast<int>(quest_count)
                                    ? &s.quests[selected_quest_] : nullptr;
  const QuestDetailLayout layout = BuildQuestDetailLayout(detail);
  DrawHeader(layout.header,
              selected && selected->name[0] ? selected->name : "SELECT A QUEST");
  if (selected) {
    const char* status = (selected->flags & kQuestFailed) ? "FAILED"
                         : (selected->flags & kQuestCompleted) ? "COMPLETED"
                         : (selected->flags & kQuestActive) ? "TRACKED" : "AVAILABLE";
    const Color status_color =
        (selected->flags & kQuestFailed) ? Color(181, 88, 78)
        : (selected->flags & kQuestActive) ? kSelected : kMuted;
    canvas_.Text(layout.status,
                 std::string(status) + "     STAGE " +
                     std::to_string(selected->stage) + "     " +
                     std::to_string(selected->objective_count) +
                     (selected->objective_count == 1 ? " OBJECTIVE"
                                                     : " OBJECTIVES"),
                 {16, FW_SEMIBOLD}, status_color, Align::kCenter);

    // Journal lore comes directly from PlayerCharacter::questLog's localized
    // CNAM entry for this stage.  It is intentionally separate from the
    // objective list, matching Skyrim's journal hierarchy.
    canvas_.VGradient(layout.lore, kSurfaceTop, kSurfaceBottom);
    canvas_.StrokeRect(layout.lore, 1, kEdge.Alpha(185));
    canvas_.Text(Box(layout.lore.x + 20, layout.lore.y + 10,
                     layout.lore.w - 40, 28), "JOURNAL",
                 {15, FW_SEMIBOLD}, kWarm);
    canvas_.Line(layout.lore.x + 20, layout.lore.y + 39,
                 layout.lore.right() - 20, layout.lore.y + 39, 1, kEdge);
    canvas_.TextWrap(Box(layout.lore.x + 22, layout.lore.y + 48,
                         layout.lore.w - 44, layout.lore.h - 58),
                     selected->lore[0]
                         ? selected->lore
                         : "No localized journal entry is available for this stage.",
                     {17, FW_NORMAL}, selected->lore[0] ? kText : kMuted);

    canvas_.FillRect(layout.objectives, kSurface);
    canvas_.StrokeRect(layout.objectives, 1, kEdge.Alpha(185));
    canvas_.Text(Box(layout.objectives.x + 20, layout.objectives.y + 8,
                     layout.objectives.w - 40, 30), "OBJECTIVES",
                 {15, FW_SEMIBOLD}, kWarm);
    canvas_.Line(layout.objectives.x + 20, layout.objectives.y + 39,
                 layout.objectives.right() - 20, layout.objectives.y + 39,
                 1, kEdge);

    const uint16_t stored_count = std::min<uint16_t>(
        selected->objective_stored,
        static_cast<uint16_t>(kMaxQuestObjectives));
    const bool legacy_fallback = stored_count == 0 && selected->objective[0];
    const int displayed_count = legacy_fallback ? 1 : stored_count;
    const int visible_objectives = std::max(
        1, (layout.objectives.h - 47) / layout.objective_row_height);
    quest_objective_scroll_ = std::clamp(
        quest_objective_scroll_, 0,
        std::max(0, displayed_count - visible_objectives));
    for (int row = 0; row < visible_objectives; ++row) {
      const int at = quest_objective_scroll_ + row;
      if (at >= displayed_count) break;
      const QuestObjectiveState* objective = legacy_fallback
          ? nullptr : &selected->objectives[at];
      const uint8_t objective_state = objective ? objective->state : 1;
      const bool current = objective_state == 1;
      const bool completed = objective_state == 2 || objective_state == 3;
      const bool failed = objective_state == 4 || objective_state == 5;
      const char* state_text = current ? "CURRENT"
                               : completed ? "COMPLETE"
                               : failed ? "FAILED" : "RECEIVED";
      const Color state_color = current ? kWarm
                                : failed ? Color(181, 88, 78)
                                : completed ? kSelected : kMuted;
      const Box row_box(layout.objectives.x + 8,
                        layout.objectives.y + 43 +
                            row * layout.objective_row_height,
                        layout.objectives.w - 22,
                        layout.objective_row_height - 3);
      canvas_.FillRect(row_box,
                       current ? kObjectiveCurrent
                               : (row & 1) ? kObjectiveRowAlt
                                           : kObjectiveRow);
      if (current) canvas_.FillRect(Box(row_box.x, row_box.y, 4, row_box.h),
                                    kWarm);
      const int status_divider_x = row_box.x + 122;
      canvas_.Text(Box(row_box.x + 10, row_box.y, 104, row_box.h), state_text,
                   {13, FW_SEMIBOLD}, state_color, Align::kCenter);
      canvas_.Line(status_divider_x, row_box.y + 8, status_divider_x,
                   row_box.bottom() - 8, 1, kEdge.Alpha(125));
      canvas_.TextWrap(Box(status_divider_x + 14, row_box.y + 5,
                           row_box.right() - status_divider_x - 24,
                           row_box.h - 10),
                       objective ? objective->text : selected->objective,
                       {17, FW_NORMAL}, completed ? kMuted : kText);
    }
    if (!displayed_count) {
      canvas_.TextWrap(Box(layout.objectives.x + 30,
                           layout.objectives.y + 52,
                           layout.objectives.w - 60,
                           layout.objectives.h - 64),
                       "No player-facing objectives are available.",
                       {17, FW_NORMAL}, kMuted, Align::kCenter);
    }
    DrawScrollBar(canvas_,
                  Box(layout.objectives.right() - 10,
                      layout.objectives.y + 45, 5,
                      layout.objectives.h - 52),
                  quest_objective_scroll_, visible_objectives,
                  displayed_count);

    const bool finished =
        (selected->flags & (kQuestCompleted | kQuestFailed)) != 0;
    DrawPanel(layout.track, !finished);
    canvas_.Text(layout.track,
                 finished ? status
                          : (selected->flags & kQuestActive)
                               ? "STOP TRACKING" : "TRACK QUEST",
                 {20, FW_SEMIBOLD}, finished ? kMuted : kText,
                 Align::kCenter);
  } else {
    canvas_.TextWrap(Box(detail.x + 70, detail.y + 210,
                         detail.w - 140, 150),
                     "SELECT A QUEST TO REVIEW ITS JOURNAL LORE, OBJECTIVES, "
                     "AND TRACKING STATUS.",
                     {21, FW_NORMAL}, kMuted, Align::kCenter);
  }
}

uint64_t Ui::VisualFingerprint(const Snapshot& s) const {
  uint64_t hash = kFnvOffset;
  const auto number = [&hash](float value, float scale = 10.0f) {
    const int32_t key = Quantize(value, scale);
    HashValue(hash, key);
  };

  // Only hash values that are actually visible on the active page. The lower
  // surface is a 5.4 MB GDI upload, so health regeneration must not repaint a
  // map or inventory page whose pixels have not changed.
  HashValue(hash, s.in_game);
  HashValue(hash, s.world_epoch);
  HashValue(hash, page_);

  switch (page_) {
    case Page::kStatus: {
      number(s.health); number(s.max_health);
      number(s.magicka); number(s.max_magicka);
      number(s.stamina); number(s.max_stamina);
      number(s.carry); number(s.max_carry);
      number(s.level_xp_max > 0.0f ? s.level_xp / s.level_xp_max : 0.0f,
             1000.0f);
      HashValue(hash, s.level);
      HashValue(hash, s.gold);
      HashValue(hash, s.perk_points);
      HashValue(hash, character_control_revision_);
      number(s.armor_rating); number(s.damage_rating);
      HashValue(hash, hotbar_assignment_form_);
      HashText(hash, s.location, sizeof(s.location));
      const uint32_t count = std::min(s.item_count, kMaxItems);
      HashValue(hash, count);
      for (uint32_t i = 0; i < count; ++i) {
        const ItemState& item = s.items[i];
        HashValue(hash, item.form_id); HashValue(hash, item.count);
        HashValue(hash, item.flags); HashValue(hash, item.equip_slot);
        number(item.damage); number(item.armor);
        HashText(hash, item.name, sizeof(item.name));
      }
      const uint32_t magic_count = std::min(s.magic_count, kMaxMagic);
      HashValue(hash, magic_count);
      for (uint32_t i = 0; i < magic_count; ++i) {
        const MagicState& magic = s.magic[i];
        if ((magic.flags & (kMagicLeftHand | kMagicRightHand)) == 0) continue;
        HashValue(hash, magic.form_id);
        HashValue(hash, magic.preview_form_id);
        HashValue(hash, magic.flags);
        HashValue(hash, magic.school);
        HashText(hash, magic.name, sizeof(magic.name));
      }
      const uint32_t favorites = std::min(s.favorite_count, kMaxFavorites);
      HashValue(hash, favorites);
      for (uint32_t i = 0; i < favorites; ++i) HashValue(hash, s.favorites[i]);
      break;
    }

    case Page::kMap:
      // The private map has an independent anchor after its first exterior
      // sample. Walking or turning the actor must not invalidate or recenter
      // the map view.
      HashValue(hash, s.world_form_id);
      HashText(hash, s.location, sizeof(s.location));
      HashValue(hash, s.map_marker_count);
      for (uint32_t i = 0; i < s.map_marker_count && i < kMaxMapMarkers; ++i) {
        const MapMarkerState& marker = s.map_markers[i];
        HashValue(hash, marker.form_id); HashValue(hash, marker.flags);
        HashValue(hash, marker.type);
        number(marker.x, 1.0f); number(marker.y, 1.0f);
        HashText(hash, marker.name, sizeof(marker.name));
      }
      break;

    case Page::kInventory: {
      HashValue(hash, category_);
      HashValue(hash, selected_item_form_);
      HashValue(hash, inventory_scroll_);
      const uint32_t count = std::min(s.item_count, kMaxItems);
      HashValue(hash, count);
      for (uint32_t i = 0; i < count; ++i) {
        const ItemState& item = s.items[i];
        HashValue(hash, item.form_id); HashValue(hash, item.form_type);
        HashValue(hash, item.count); HashValue(hash, item.flags);
        HashValue(hash, item.equip_slot);
        HashValue(hash, item.icon_kind);
        number(item.weight, 10.0f); number(item.value, 1.0f);
        number(item.damage, 1.0f); number(item.armor, 1.0f);
        HashText(hash, item.name, sizeof(item.name));
        HashText(hash, item.description, sizeof(item.description));
      }
      const uint32_t favorites = std::min(s.favorite_count, kMaxFavorites);
      HashValue(hash, favorites);
      for (uint32_t i = 0; i < favorites; ++i) HashValue(hash, s.favorites[i]);
      break;
    }

    case Page::kMagic: {
      HashValue(hash, magic_category_);
      HashValue(hash, selected_magic_form_);
      HashValue(hash, magic_scroll_);
      const uint32_t count = std::min(s.magic_count, kMaxMagic);
      HashValue(hash, count);
      for (uint32_t i = 0; i < count; ++i) {
        const MagicState& magic = s.magic[i];
        HashValue(hash, magic.form_id);
        HashValue(hash, magic.preview_form_id);
        HashValue(hash, magic.form_type);
        HashValue(hash, magic.flags); HashValue(hash, magic.spell_type);
        HashValue(hash, magic.school); number(magic.cost);
        HashText(hash, magic.name, sizeof(magic.name));
        HashText(hash, magic.description, sizeof(magic.description));
      }
      break;
    }

    case Page::kQuests: {
      HashValue(hash, selected_quest_form_);
      HashValue(hash, quest_scroll_);
      HashValue(hash, quest_objective_scroll_);
      const uint32_t count = std::min(s.quest_count, kMaxQuests);
      HashValue(hash, count);
      for (uint32_t i = 0; i < count; ++i) {
        const QuestState& quest = s.quests[i];
        HashValue(hash, quest.form_id); HashValue(hash, quest.flags);
        HashValue(hash, quest.stage); HashValue(hash, quest.objective_count);
        HashValue(hash, quest.objective_stored);
        HashText(hash, quest.name, sizeof(quest.name));
        HashText(hash, quest.objective, sizeof(quest.objective));
        HashText(hash, quest.lore, sizeof(quest.lore));
        const uint16_t objective_count = std::min<uint16_t>(
            quest.objective_stored,
            static_cast<uint16_t>(kMaxQuestObjectives));
        for (uint16_t objective = 0; objective < objective_count; ++objective) {
          HashValue(hash, quest.objectives[objective].index);
          HashValue(hash, quest.objectives[objective].state);
          HashText(hash, quest.objectives[objective].text,
                   sizeof(quest.objectives[objective].text));
        }
      }
      break;
    }
  }
  return hash;
}

void Ui::DrawFrame(const Snapshot& s) {
  // Native item previews are serial asynchronous work. A small per-composition
  // budget prevents one inventory paint from flooding both bridge queues.
  // Missing models remain absent until Skyrim's real asset render completes;
  // no synthetic item glyph is substituted for a failed native resource.
  BeginItemPreviewRequestFrame(s.world_epoch);
  canvas_.Clear(kBlack);
  canvas_.VGradient(Box(0, 0, canvas_.width(), canvas_.height()),
                    kBackdropTop, kBackdropBottom);
  canvas_.FillRect(Box(kContentX - 4, kContentY - 6,
                       canvas_.width() - (kContentX - 4) * 2,
                       canvas_.height() - kContentY -
                           kContentBottomMargin + 10),
                   kSurface.Alpha(76));
  DrawNavigation();
  switch (page_) {
    case Page::kStatus: DrawDashboard(s); break;
    case Page::kMap: DrawMap(s); break;
    case Page::kInventory: DrawInventory(s); break;
    case Page::kMagic: DrawMagic(s); break;
    case Page::kQuests: DrawQuests(s); break;
  }
}

bool Ui::Render(const Snapshot& state) {
  if (!canvas_.ready()) return false;
  if (have_rendered_state_ &&
      state.world_form_id != rendered_state_.world_form_id) {
    map_zoom_ = 1.0f;
    map_pan_x_ = map_pan_y_ = 0.0f;
    map_anchor_valid_ = false;
    map_anchor_world_id_ = 0;
    last_map_marker_refresh_request_ms_ = 0;
    if (drag_target_ == DragTarget::kMap) CancelPointer();
  }
  if (!dirty_ && rendered_sequence_ == state.sequence) return false;

  // Snapshot arrays can be reordered whenever Skyrim refreshes them. Resolve
  // stable FormIDs only for the active page; scanning both large arrays on
  // every telemetry sample caused needless CPU work on the Thor.
  if (page_ == Page::kInventory) {
    const std::vector<int> visible_items = FilteredItems(state);
    selected_item_ = -1;
    if (selected_item_form_) {
      for (int index : visible_items) {
        if (state.items[index].form_id == selected_item_form_) {
          selected_item_ = index;
          break;
        }
      }
    }
    if (selected_item_ < 0) {
      for (int index : visible_items) {
        if ((state.items[index].flags & kItemEquipped) != 0) {
          selected_item_ = index;
          break;
        }
      }
      if (selected_item_ < 0 && !visible_items.empty())
        selected_item_ = visible_items.front();
      selected_item_form_ = selected_item_ >= 0
                                ? state.items[selected_item_].form_id
                                : 0;
    }
  } else if (page_ == Page::kMagic) {
    const std::vector<int> visible_magic = FilteredMagic(state);
    selected_magic_ = -1;
    if (selected_magic_form_) {
      for (int index : visible_magic) {
        if (state.magic[index].form_id == selected_magic_form_) {
          selected_magic_ = index;
          break;
        }
      }
    }
    if (selected_magic_ < 0) {
      selected_magic_ = visible_magic.empty() ? -1 : visible_magic.front();
      selected_magic_form_ = selected_magic_ >= 0
                                 ? state.magic[selected_magic_].form_id
                                 : 0;
    }
  } else if (page_ == Page::kQuests) {
    const uint32_t previous_quest_form = selected_quest_form_;
    selected_quest_ = -1;
    const uint32_t quest_count = std::min(state.quest_count, kMaxQuests);
    if (selected_quest_form_) {
      for (uint32_t index = 0; index < quest_count; ++index) {
        if (state.quests[index].form_id == selected_quest_form_) {
          selected_quest_ = static_cast<int>(index);
          break;
        }
      }
    }
    if (selected_quest_ < 0) {
      for (uint32_t index = 0; index < quest_count; ++index) {
        if (state.quests[index].form_id) {
          selected_quest_ = static_cast<int>(index);
          break;
        }
      }
      selected_quest_form_ = selected_quest_ >= 0
                                  ? state.quests[selected_quest_].form_id
                                  : 0;
    }
    if (selected_quest_form_ != previous_quest_form)
      quest_objective_scroll_ = 0;
  }
  const uint64_t fingerprint = VisualFingerprint(state);
  if (!dirty_ && rendered_fingerprint_ == fingerprint) {
    // A new telemetry sample arrived but it would produce the same pixels.
    // Remember its sequence so the common present path remains O(1).
    rendered_sequence_ = state.sequence;
    return false;
  }
  DrawFrame(state);
  rendered_state_ = state;
  have_rendered_state_ = true;
  loading_visible_ = false;
  loading_dirty_ = true;
  rendered_sequence_ = state.sequence;
  rendered_fingerprint_ = fingerprint;
  dirty_ = false;
  return true;
}

bool Ui::Queue(CommandType type, uint32_t form, int index) {
  Command command;
  command.type = type;
  command.form_id = form;
  command.index = index;
  if (type == CommandType::kRequestMapPreview)
    command.flags |= kCommandPreviewPriority;
  command.world_epoch = have_rendered_state_
                            ? rendered_state_.world_epoch
                            : 0;
  return PushUiCommand(command);
}

bool Ui::QueueMapMarkerRefresh(const Snapshot& state) {
  return QueueMapPreview(state, 0.0f, 0.0f, 0.0f,
                         kCommandRefreshMapMarkers);
}

bool Ui::QueueMapPreview(const Snapshot& state, float pan_x, float pan_y,
                         float zoom_delta, uint32_t extra_flags) {
  if (!state.world_form_id || !std::isfinite(pan_x) ||
      !std::isfinite(pan_y) || !std::isfinite(zoom_delta))
    return false;
  Command command;
  command.type = CommandType::kRequestMapPreview;
  command.flags = kCommandPreviewPriority | extra_flags;
  command.map_pan_x = std::clamp(pan_x, -2.0f, 2.0f);
  command.map_pan_y = std::clamp(pan_y, -2.0f, 2.0f);
  command.map_zoom_delta = std::clamp(zoom_delta, -4.0f, 4.0f);
  command.map_world_id = state.world_form_id;
  command.control_revision = ++map_control_revision_;
  command.world_epoch = state.world_epoch;
  const bool queued = PushUiCommand(command);
  if (extra_flags)
    DIS_INFO("[map-native] request queued=%u world=%08X epoch=%llu "
             "flags=%08X revision=%llu",
             queued ? 1u : 0u, command.map_world_id,
             static_cast<unsigned long long>(command.world_epoch),
             extra_flags,
             static_cast<unsigned long long>(command.control_revision));
  return queued;
}

void Ui::ClampMapPan() {
  if (!canvas_.ready()) {
    map_pan_x_ = map_pan_y_ = 0.0f;
    return;
  }
  const MapLayout layout = BuildMapLayout(canvas_.width(), canvas_.height());
  const float extent = std::max(0.0f, (map_zoom_ - 1.0f) * 0.5f);
  map_pan_x_ = std::clamp(map_pan_x_, -extent * layout.viewport.w,
                          extent * layout.viewport.w);
  map_pan_y_ = std::clamp(map_pan_y_, -extent * layout.viewport.h,
                          extent * layout.viewport.h);
}

void Ui::BeginHotbarAssignment(uint32_t form_id) {
  if (!form_id) return;
  hotbar_assignment_form_ = form_id;
  SelectPage(Page::kStatus, "hotbar-assignment");
  dirty_ = true;
}

void Ui::CancelHotbarAssignment() {
  if (!hotbar_assignment_form_) return;
  hotbar_assignment_form_ = 0;
  dirty_ = true;
}

void Ui::SelectPage(Page page, const char* source) {
  if (page == Page::kMap) {
    DIS_INFO("[ui] map page is temporarily disabled source=%s",
             source ? source : "unknown");
    return;
  }
  if (page == page_) return;
  if (hotbar_assignment_form_ && page != Page::kStatus &&
      (!source || std::strcmp(source, "hotbar-assignment") != 0)) {
    hotbar_assignment_form_ = 0;
  }
  const Page previous = page_;
  page_ = page;
  dirty_ = true;
  last_character_tap_ms_ = last_map_tap_ms_ = 0;
  if (page_ == Page::kMap) last_map_marker_refresh_request_ms_ = 0;
  CancelPointer();
  DIS_INFO("[ui] page switch %s -> %s source=%s",
           PageName(previous), PageName(page_), source ? source : "unknown");
}

bool Ui::Tap(float nx, float ny) {
  if (loading_visible_) return false;
  if (!std::isfinite(nx) || !std::isfinite(ny)) return false;
  nx = std::clamp(nx, 0.0f, 1.0f);
  ny = std::clamp(ny, 0.0f, 1.0f);
  const int x = std::clamp(static_cast<int>(nx * canvas_.width()), 0,
                           std::max(0, canvas_.width() - 1));
  const int y = std::clamp(static_cast<int>(ny * canvas_.height()), 0,
                           std::max(0, canvas_.height() - 1));
  if (y >= kNavY + 8 && y < kNavY + kNavH - 8) {
    const NavigationLayout layout = BuildNavigationLayout(canvas_.width(), page_);
    if (layout.left.Contains(x, y)) {
      SelectPage(OffsetNavigablePage(page_, -1), "left-arrow");
      return true;
    }
    if (layout.right.Contains(x, y)) {
      SelectPage(OffsetNavigablePage(page_, 1), "right-arrow");
      return true;
    }
    for (int visible = 0; visible < kVisiblePageCount; ++visible) {
      if (layout.tabs[visible].Contains(x, y)) {
        SelectPage(static_cast<Page>(layout.pages[visible]), "page-button");
        return true;
      }
    }
  }
  // Act on the exact state used to paint the visible rows. Pulling a newer
  // bridge snapshot here could equip a different item after Skyrim reordered
  // its inventory between the draw and this tap.
  if (!have_rendered_state_) return false;
  const Snapshot& s = rendered_state_;
  const auto selected_item_index = [&]() -> int {
    if (!selected_item_form_) return -1;
    const uint32_t count = std::min(s.item_count, kMaxItems);
    for (uint32_t index = 0; index < count; ++index) {
      const ItemState& item = s.items[index];
      if (item.form_id == selected_item_form_ && item.count > 0 &&
          (category_ == 0 || CategoryFor(item.form_type) == category_))
        return static_cast<int>(index);
    }
    return -1;
  };
  const auto selected_magic_index = [&]() -> int {
    if (!selected_magic_form_) return -1;
    const uint32_t count = std::min(s.magic_count, kMaxMagic);
    for (uint32_t index = 0; index < count; ++index) {
      const MagicState& magic = s.magic[index];
      const bool visible = magic_category_ == 0 ||
          (magic_category_ == 1 && (magic.flags & kMagicFavorite)) ||
          (magic_category_ > 1 &&
           MagicCategoryFor(magic) == magic_category_);
      if (magic.form_id == selected_magic_form_ && visible)
        return static_cast<int>(index);
    }
    return -1;
  };
  const auto selected_quest_index = [&]() -> int {
    if (!selected_quest_form_) return -1;
    const uint32_t count = std::min(s.quest_count, kMaxQuests);
    for (uint32_t index = 0; index < count; ++index)
      if (s.quests[index].form_id == selected_quest_form_)
        return static_cast<int>(index);
    return -1;
  };
  if (page_ == Page::kStatus) {
    const int right = canvas_.width() - kContentRightMargin;
    const int middle_y = kContentY + kResourcePanelHeight + 8;
    const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
    const int middle_h = hotbar_y - middle_y - 8;
    const int column_gap = 9;
    const int column_w = (right - kContentX - column_gap * 2) / 3;
    const Box loadout(kContentX + (column_w + column_gap) * 2, middle_y,
                      right - (kContentX + (column_w + column_gap) * 2),
                      middle_h);
    const int list_top = loadout.y + 78;
    const int summary_y = loadout.bottom() - kEquipmentSummaryHeight;
    constexpr int equipment_row_height = 73;
    constexpr int equipment_card_height = 65;
    const int visible_rows = std::max(
        1, (summary_y - kEquipmentListEndGap - list_top) /
               equipment_row_height);
    if (x >= loadout.x + 18 && x < loadout.right() - 6 &&
        y >= list_top && y < summary_y - kEquipmentListEndGap) {
      const int local_y = y - list_top;
      const int visible_row = local_y / equipment_row_height;
      if (visible_row >= visible_rows ||
          local_y % equipment_row_height >= equipment_card_height)
        return true;
      // This is a read-only status panel. It previously sent ActivateItem for
      // a row tap, so a scroll/touch stream could silently unequip gear and
      // make the equipped count oscillate. Equipment is changed only through
      // an explicit Inventory action or a hotbar activation now.
      const int slot_index = equipment_scroll_ + visible_row;
      if (slot_index >= 0 && slot_index < 8) return true;
    }
    const Box panel(kContentX, hotbar_y,
                    canvas_.width() - kContentX - kContentRightMargin,
                    canvas_.height() - hotbar_y - kContentBottomMargin);
    if (y >= panel.y + 50) {
      const int gap = 10;
      const int slot_size = std::min(panel.h - 62, (panel.w - 44 - gap * 7) / 8);
      const int total_width = slot_size * 8 + gap * 7;
      const int left = panel.x + (panel.w - total_width) / 2;
      if (x >= left && x < left + total_width && y < panel.y + 50 + slot_size) {
        const int stride = slot_size + gap;
        const int slot = std::clamp((x - left) / stride, 0, 7);
        if ((x - left) % stride >= slot_size) return true;
        const uint32_t favorite_count =
            std::min(s.favorite_count, kMaxFavorites);
        if (hotbar_assignment_form_) {
          Queue(CommandType::kAssignHotbar, hotbar_assignment_form_, slot);
          CancelHotbarAssignment();
          return true;
        }
        if (slot < static_cast<int>(favorite_count) && s.favorites[slot]) {
          const uint32_t form_id = s.favorites[slot];
          bool is_magic = false;
          const uint32_t magic_count = std::min(s.magic_count, kMaxMagic);
          for (uint32_t i = 0; i < magic_count; ++i) {
            if (s.magic[i].form_id == form_id) { is_magic = true; break; }
          }
          Queue(is_magic ? CommandType::kActivateMagic
                         : CommandType::kActivateFavorite,
                form_id, slot);
        }
        return true;
      }
    }
  } else if (page_ == Page::kInventory) {
    const int right = canvas_.width() - kContentRightMargin;
    const Box categories(kContentX, kContentY, right - kContentX, 92);
    const int cat_gap = 6;
    const int cat_w = (categories.w - 24 - cat_gap * 6) / 7;
    if (y >= categories.y + 12 && y < categories.bottom() - 12) {
      for (int cat = 0; cat < 7; ++cat) {
        const Box hit(categories.x + 12 + cat * (cat_w + cat_gap),
                      categories.y + 12, cat_w, categories.h - 24);
        if (hit.Contains(x, y)) {
          category_ = cat;
          selected_item_ = -1;
          selected_item_form_ = 0;
          inventory_scroll_ = 0;
          dirty_ = true; return true;
        }
      }
    }
    const int list_width = static_cast<int>((right - kContentX - 8) * 0.48f);
    const Box list(kContentX, categories.bottom() + 8, list_width,
                   canvas_.height() - kContentBottomMargin - categories.bottom() - 8);
    const int row_h = 62;
    const int visible_rows = std::max(1, (list.h - 62) / row_h);
    if (x >= list.x && x < list.right() && y >= list.y + 56 &&
        y < std::min(list.bottom(), list.y + 56 + visible_rows * row_h)) {
      const int row = (y - (list.y + 56)) / row_h;
      auto filtered = FilteredItems(s);
      const int at = inventory_scroll_ + row;
      if (at >= 0 && at < static_cast<int>(filtered.size())) {
        const int item = filtered[at];
        selected_item_ = item;
        selected_item_form_ = s.items[item].form_id;
        dirty_ = true;
        return true;
      }
    }
    const Box details(list.right() + 8, categories.bottom() + 8,
                      right - list.right() - 8,
                      canvas_.height() - kContentBottomMargin - categories.bottom() - 8);
    const Box use(details.x + 30, details.bottom() - 154,
                  details.w - 60, 58);
    const Box hotbar(details.x + 30, details.bottom() - 88,
                     details.w - 60, 58);
    const int action_item = selected_item_index();
    if (action_item >= 0 && use.Contains(x, y)) {
      selected_item_ = action_item;
      const bool queued = Queue(CommandType::kActivateItem,
                                selected_item_form_, action_item);
      DIS_INFO("[ui-action] item button form=%08X index=%d queued=%u",
               selected_item_form_, action_item, queued ? 1u : 0u);
      return true;
    }
    if (action_item >= 0 && hotbar.Contains(x, y)) {
      selected_item_ = action_item;
      BeginHotbarAssignment(selected_item_form_);
      return true;
    }
  } else if (page_ == Page::kMap) {
    const MapLayout layout = BuildMapLayout(canvas_.width(), canvas_.height());
    if (layout.viewport.Contains(x, y)) {
      const uint64_t now = GetTickCount64();
      // Double-tap recenters the isolated map on the player's current world
      // position. It never opens MapMenu or writes Skyrim's camera state.
      if (last_map_tap_ms_ && now - last_map_tap_ms_ <= 350) {
        if (s.world_form_id == kTamrielWorldForm &&
            std::isfinite(s.player_x) && std::isfinite(s.player_y)) {
          map_anchor_x_ = std::clamp(s.player_x, kMapWorldMin, kMapWorldMax);
          map_anchor_y_ = std::clamp(s.player_y, kMapWorldMin, kMapWorldMax);
          map_anchor_world_id_ = s.world_form_id;
          map_anchor_valid_ = true;
        }
        map_pan_x_ = map_pan_y_ = 0.0f;
        map_zoom_ = 1.0f;
        last_map_tap_ms_ = 0;
        QueueMapPreview(s, 0.0f, 0.0f, 0.0f,
                        kCommandResetMapCamera | kCommandRefreshMapMarkers);
        dirty_ = true;
      } else {
        last_map_tap_ms_ = now;
      }
      return true;
    }
  } else if (page_ == Page::kMagic) {
    const int right = canvas_.width() - kContentRightMargin;
    const Box categories(kContentX, kContentY, right - kContentX, 92);
    const int category_gap = 6;
    const int category_width = (categories.w - 24 - category_gap * 7) / 8;
    if (y >= categories.y + 12 && y < categories.bottom() - 12) {
      for (int category = 0; category < 8; ++category) {
        const Box hit(categories.x + 12 + category * (category_width + category_gap),
                      categories.y + 12, category_width, categories.h - 24);
        if (hit.Contains(x, y)) {
          magic_category_ = category;
          selected_magic_ = -1;
          selected_magic_form_ = 0;
          magic_scroll_ = 0;
          dirty_ = true;
          return true;
        }
      }
    }
    const Box list(kContentX, categories.bottom() + 8,
                   DetailListWidth(right),
                   canvas_.height() - kContentBottomMargin - categories.bottom() - 8);
    const int visible_rows = std::max(1, (list.h - 62) / 62);
    if (x >= list.x && x < list.right() &&
        y >= list.y + 56 &&
        y < std::min(list.bottom(), list.y + 56 + visible_rows * 62)) {
      const int row = (y - (list.y + 56)) / 62;
      const std::vector<int> filtered = FilteredMagic(s);
      const int at = magic_scroll_ + row;
      if (at >= 0 && at < static_cast<int>(filtered.size())) {
        selected_magic_ = filtered[at];
        selected_magic_form_ = s.magic[selected_magic_].form_id;
        dirty_ = true;
        return true;
      }
    }
    const Box detail(list.right() + 8, categories.bottom() + 8,
                     right - list.right() - 8,
                     canvas_.height() - kContentBottomMargin - categories.bottom() - 8);
    const Box equip(detail.x + 30, detail.bottom() - 154,
                    detail.w - 60, 58);
    const Box hotbar(detail.x + 30, detail.bottom() - 88,
                     detail.w - 60, 58);
    const int action_magic = selected_magic_index();
    if (action_magic >= 0 && equip.Contains(x, y)) {
      selected_magic_ = action_magic;
      const MagicState& selected = s.magic[action_magic];
      if (!MagicIsPassive(selected)) {
        const bool queued = Queue(CommandType::kActivateMagic,
                                  selected.form_id, action_magic);
        DIS_INFO("[ui-action] magic button form=%08X index=%d equipped=%u "
                 "queued=%u",
                 selected.form_id, action_magic,
                 (selected.flags & kMagicEquipped) ? 1u : 0u,
                 queued ? 1u : 0u);
      }
      return true;
    }
    if (action_magic >= 0 && hotbar.Contains(x, y)) {
      selected_magic_ = action_magic;
      BeginHotbarAssignment(selected_magic_form_);
      return true;
    }
  } else if (page_ == Page::kQuests) {
    const int right = canvas_.width() - kContentRightMargin;
    const Box list(kContentX, kContentY, kQuestListWidth,
                   canvas_.height() - kContentY - kContentBottomMargin);
    const Box detail(list.right() + 8, kContentY, right - list.right() - 8,
                     canvas_.height() - kContentY - kContentBottomMargin);
    const int visible_rows = std::max(1, (list.h - 74) / 76);
    if (x >= list.x && x < list.right() && y >= list.y + 70 &&
        y < std::min(list.bottom(), list.y + 70 + visible_rows * 76)) {
      const int index = quest_scroll_ + (y - (list.y + 70)) / 76;
      const uint32_t quest_count = std::min(s.quest_count, kMaxQuests);
      if (index >= 0 && index < static_cast<int>(quest_count)) {
        selected_quest_ = index;
        selected_quest_form_ = s.quests[index].form_id;
        quest_objective_scroll_ = 0;
        dirty_ = true;
        return true;
      }
    }
    const int action_quest = selected_quest_index();
    if (action_quest >= 0) {
      selected_quest_ = action_quest;
      const QuestState& selected = s.quests[action_quest];
      const Box track = BuildQuestDetailLayout(detail).track;
      if ((selected.flags & (kQuestCompleted | kQuestFailed)) == 0 &&
          track.Contains(x, y)) {
        Queue(CommandType::kSelectQuest, selected.form_id, action_quest);
        return true;
      }
    }
  }
  return false;
}

bool Ui::Hold(float nx, float ny) {
  const bool handled = Tap(nx, ny);
  // Android may emit hold plus the original down/up stream. Mark that stream
  // consumed so pointer-up cannot perform the same equip/use a second time.
  if (pointer_down_) pointer_moved_ = true;
  return handled;
}

bool Ui::Pinch(float nx, float ny, float factor) {
  if (loading_visible_) return false;
  if (!std::isfinite(factor) || factor <= 0.0f) return false;
  if (!std::isfinite(nx) || !std::isfinite(ny)) return false;
  const int px = std::clamp(
      static_cast<int>(std::clamp(nx, 0.0f, 1.0f) * canvas_.width()),
      0, std::max(0, canvas_.width() - 1));
  const int py = std::clamp(
      static_cast<int>(std::clamp(ny, 0.0f, 1.0f) * canvas_.height()),
      0, std::max(0, canvas_.height() - 1));
  if (page_ == Page::kMap) {
    const MapLayout layout = BuildMapLayout(canvas_.width(), canvas_.height());
    if (!layout.viewport.Contains(px, py)) return false;
    const float old_zoom = map_zoom_;
    const float new_zoom = std::clamp(map_zoom_ * factor,
                                      1.0f, kMapMaxZoom);
    const float ratio = new_zoom / std::max(0.001f, old_zoom);
    const float local_x =
        px - (layout.viewport.x + layout.viewport.w * 0.5f);
    const float local_y =
        py - (layout.viewport.y + layout.viewport.h * 0.5f);
    // Preserve the terrain point beneath the fingers while changing only the
    // private map transform.
    map_pan_x_ = ratio * map_pan_x_ + (1.0f - ratio) * local_x;
    map_pan_y_ = ratio * map_pan_y_ + (1.0f - ratio) * local_y;
    map_zoom_ = new_zoom;
    ClampMapPan();
    if (have_rendered_state_)
      QueueMapPreview(rendered_state_, 0.0f, 0.0f,
                      std::log2(std::max(0.001f, factor)));
    last_map_tap_ms_ = 0;
    dirty_ = true;
    DIS_INFO("[map-isolated] pinch factor=%.4f zoom=%.4f->%.4f "
             "pan=(%.1f,%.1f)",
             factor, old_zoom, map_zoom_, map_pan_x_, map_pan_y_);
    return true;
  }
  // The retired actor-camera preview has no gesture target.
  return false;
}

bool Ui::Pointer(unsigned message, float nx, float ny) {
  if (loading_visible_) return false;
  if (!std::isfinite(nx) || !std::isfinite(ny)) return false;
  nx = std::clamp(nx, 0.0f, 1.0f);
  ny = std::clamp(ny, 0.0f, 1.0f);
  if (message == WM_LBUTTONDOWN) {
    pointer_down_ = true; pointer_moved_ = false;
    down_x_ = last_x_ = nx; down_y_ = last_y_ = ny;
    drag_scroll_pixels_ = 0.0f;
    drag_target_ = DragTarget::kNone;
    const int px = std::clamp(static_cast<int>(nx * canvas_.width()), 0,
                              std::max(0, canvas_.width() - 1));
    const int py = std::clamp(static_cast<int>(ny * canvas_.height()), 0,
                              std::max(0, canvas_.height() - 1));
    if (py >= kContentY) {
      if (page_ == Page::kStatus) {
        const int right = canvas_.width() - kContentRightMargin;
        const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
        const int middle_y = kContentY + kResourcePanelHeight + 8;
        const int middle_h = hotbar_y - middle_y - 8;
        const int column_gap = 9;
        const int column_w = (right - kContentX - column_gap * 2) / 3;
        const Box loadout(kContentX + (column_w + column_gap) * 2, middle_y,
                          right - (kContentX + (column_w + column_gap) * 2),
                          middle_h);
        const int list_top = loadout.y + 78;
        const int summary_y = loadout.bottom() - kEquipmentSummaryHeight;
        if (px >= loadout.x + 18 && px < loadout.right() - 6 &&
            py >= list_top &&
            py < summary_y - kEquipmentListEndGap)
          drag_target_ = DragTarget::kEquipment;
      } else if (page_ == Page::kInventory) {
        const int right = canvas_.width() - kContentRightMargin;
        const int list_width = static_cast<int>((right - kContentX - 8) * 0.48f);
        if (px < kContentX + list_width && py >= kContentY + 100 + 56)
          drag_target_ = DragTarget::kInventory;
      } else if (page_ == Page::kMap) {
        const MapLayout layout =
            BuildMapLayout(canvas_.width(), canvas_.height());
        if (layout.viewport.Contains(px, py)) {
          drag_target_ = DragTarget::kMap;
          return true;
        }
      }
      else if (page_ == Page::kMagic &&
               px < kContentX + DetailListWidth(
                                    canvas_.width() - kContentRightMargin) &&
               py >= kContentY + 100 + 56)
        drag_target_ = DragTarget::kMagic;
      else if (page_ == Page::kQuests) {
        const int right = canvas_.width() - kContentRightMargin;
        const Box list(kContentX, kContentY, kQuestListWidth,
                       canvas_.height() - kContentY - kContentBottomMargin);
        const Box detail(list.right() + 8, kContentY,
                         right - list.right() - 8,
                         canvas_.height() - kContentY -
                             kContentBottomMargin);
        if (px < list.right() && py >= list.y + 70)
          drag_target_ = DragTarget::kQuests;
        else if (BuildQuestDetailLayout(detail).objectives.Contains(px, py))
          drag_target_ = DragTarget::kQuestObjectives;
      }
    }
    return true;
  }
  if (message == WM_MOUSEMOVE && pointer_down_) {
    const float dx = nx - last_x_;
    const float dy = ny - last_y_;
    if (std::abs(nx - down_x_) + std::abs(ny - down_y_) > 0.012f) pointer_moved_ = true;
    if (drag_target_ == DragTarget::kCharacter) {
      character_rotation_degrees_ = std::fmod(
          character_rotation_degrees_ + dx * 420.0f, 360.0f);
      if (character_rotation_degrees_ < 0.0f)
        character_rotation_degrees_ += 360.0f;
      ++character_control_revision_;
      if (pointer_moved_) last_character_tap_ms_ = 0;
    } else if (drag_target_ == DragTarget::kMap) {
      map_pan_x_ += dx * canvas_.width();
      map_pan_y_ += dy * canvas_.height();
      ClampMapPan();
      if (have_rendered_state_)
        QueueMapPreview(rendered_state_, dx, dy, 0.0f);
      if (pointer_moved_) last_map_tap_ms_ = 0;
    } else if (drag_target_ == DragTarget::kEquipment ||
               drag_target_ == DragTarget::kInventory ||
               drag_target_ == DragTarget::kMagic ||
               drag_target_ == DragTarget::kQuests ||
               drag_target_ == DragTarget::kQuestObjectives) {
      int row_height = 64;
      int maximum = 0;
      int* scroll = nullptr;
      if (drag_target_ == DragTarget::kEquipment) {
        const int right = canvas_.width() - kContentRightMargin;
        const int hotbar_y = canvas_.height() - kHotbarPanelHeight;
        const int middle_y = kContentY + kResourcePanelHeight + 8;
        const int middle_h = hotbar_y - middle_y - 8;
        const int column_w = (right - kContentX - 18) / 3;
        const Box loadout(kContentX + (column_w + 9) * 2, middle_y,
                          right - (kContentX + (column_w + 9) * 2),
                          middle_h);
        row_height = 73;
        const int visible_rows = std::max(
            1, (loadout.bottom() - kEquipmentSummaryHeight -
                kEquipmentListEndGap - (loadout.y + 78)) / row_height);
        maximum = std::max(0, 8 - visible_rows);
        scroll = &equipment_scroll_;
      } else if (drag_target_ == DragTarget::kInventory) {
        const int right = canvas_.width() - kContentRightMargin;
        const Box categories(kContentX, kContentY, right - kContentX, 92);
        const Box list(kContentX, categories.bottom() + 8,
                       static_cast<int>((right - kContentX - 8) * 0.48f),
                       canvas_.height() - kContentBottomMargin -
                           categories.bottom() - 8);
        row_height = 62;
        const int visible_rows = std::max(1, (list.h - 62) / row_height);
        maximum = have_rendered_state_
            ? std::max(0, static_cast<int>(
                              FilteredItems(rendered_state_).size()) -
                              visible_rows)
            : 0;
        scroll = &inventory_scroll_;
      } else if (drag_target_ == DragTarget::kMagic) {
        row_height = 62;
        const int right = canvas_.width() - kContentRightMargin;
        const Box categories(kContentX, kContentY, right - kContentX, 92);
        const Box list(kContentX, categories.bottom() + 8,
                       DetailListWidth(right),
                       canvas_.height() - kContentBottomMargin -
                           categories.bottom() - 8);
        const int visible_rows = std::max(1, (list.h - 62) / row_height);
        maximum = have_rendered_state_
            ? std::max(0, static_cast<int>(
                              FilteredMagic(rendered_state_).size()) -
                              visible_rows)
            : 0;
        scroll = &magic_scroll_;
      } else if (drag_target_ == DragTarget::kQuestObjectives) {
        row_height = 68;
        const int right = canvas_.width() - kContentRightMargin;
        const Box list(kContentX, kContentY, kQuestListWidth,
                       canvas_.height() - kContentY - kContentBottomMargin);
        const Box detail(list.right() + 8, kContentY,
                         right - list.right() - 8,
                         canvas_.height() - kContentY -
                             kContentBottomMargin);
        const QuestDetailLayout layout = BuildQuestDetailLayout(detail);
        const int visible_rows = std::max(
            1, (layout.objectives.h - 47) / row_height);
        int objective_count = 0;
        if (have_rendered_state_ && selected_quest_ >= 0 &&
            selected_quest_ < static_cast<int>(std::min(
                rendered_state_.quest_count, kMaxQuests))) {
          const QuestState& quest = rendered_state_.quests[selected_quest_];
          objective_count = std::min<int>(quest.objective_stored,
                                          kMaxQuestObjectives);
          if (objective_count == 0 && quest.objective[0]) objective_count = 1;
        }
        maximum = std::max(0, objective_count - visible_rows);
        scroll = &quest_objective_scroll_;
      } else {
        row_height = 76;
        const int visible_rows = std::max(
            1, (canvas_.height() - kContentY - kContentBottomMargin - 74) /
                   row_height);
        maximum = have_rendered_state_
            ? std::max(0, static_cast<int>(
                              std::min(rendered_state_.quest_count,
                                       kMaxQuests)) -
                              visible_rows)
            : 0;
        scroll = &quest_scroll_;
      }
      drag_scroll_pixels_ += dy * canvas_.height();
      const float threshold = std::max(24.0f, row_height * 0.62f);
      const int steps = static_cast<int>(
          std::abs(drag_scroll_pixels_) / threshold);
      if (scroll && steps > 0) {
        const int direction = drag_scroll_pixels_ > 0.0f ? -1 : 1;
        *scroll = std::clamp(*scroll + direction * steps, 0, maximum);
        drag_scroll_pixels_ -=
            (drag_scroll_pixels_ > 0.0f ? 1.0f : -1.0f) * steps * threshold;
      }
    }
    last_x_ = nx;
    last_y_ = ny;
    dirty_ = true;
    return true;
  }
  if (message == WM_LBUTTONUP && pointer_down_) {
    pointer_down_ = false;
    const DragTarget released_target = drag_target_;
    drag_target_ = DragTarget::kNone;
    drag_scroll_pixels_ = 0.0f;
    if (released_target == DragTarget::kMap) return true;
    if (!pointer_moved_) return Tap(nx, ny);
    return true;
  }
  return false;
}

void Ui::CancelPointer() {
  pointer_down_ = false;
  pointer_moved_ = false;
  drag_target_ = DragTarget::kNone;
  drag_scroll_pixels_ = 0.0f;
}

}  // namespace dis
