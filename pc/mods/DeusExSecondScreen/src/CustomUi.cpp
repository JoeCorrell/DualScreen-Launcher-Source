#include "CustomUi.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <string>

namespace dxs {
namespace {

constexpr COLORREF kBackground = RGB(3, 5, 5);
constexpr COLORREF kPanel = RGB(8, 10, 9);
constexpr COLORREF kPanelRaised = RGB(13, 15, 13);
constexpr COLORREF kGold = RGB(231, 169, 48);
constexpr COLORREF kGoldBright = RGB(255, 205, 86);
constexpr COLORREF kGoldDim = RGB(91, 67, 27);
constexpr COLORREF kRail = RGB(51, 43, 26);
constexpr COLORREF kText = RGB(220, 218, 209);
constexpr COLORREF kMuted = RGB(143, 142, 137);
constexpr COLORREF kCyan = RGB(82, 201, 205);

constexpr int kMaximumInventoryItems = 256;

int SafeInventoryItemCount(const InventorySnapshot& inventory) {
  return (std::max)(0, (std::min)(kMaximumInventoryItems,
                                  inventory.item_count));
}

int SafeAugmentationCount(const InventorySnapshot& inventory) {
  return (std::max)(0, (std::min)(kMaximumAugmentationTrees,
                                  inventory.augmentation_count));
}

int SafeAugmentationNodeCount(const AugmentationTreeSnapshot& tree) {
  return (std::max)(0, (std::min)(kMaximumAugmentationNodesPerTree,
                                  tree.node_count));
}

int SafePrerequisiteCount(const AugmentationNodeSnapshot& node) {
  return (std::max)(0, (std::min)(kMaximumAugmentationPrerequisites,
                                  node.prerequisite_count));
}

int SafeMapMarkerCount(const NativeMapSnapshot& map) {
  return (std::max)(0, (std::min)(
      static_cast<int>(sizeof(map.markers) / sizeof(map.markers[0])),
      map.marker_count));
}

int SafeObjectiveMarkerCount(const NativeMapSnapshot& map) {
  return (std::max)(0, (std::min)(
      static_cast<int>(sizeof(map.objective_markers) /
                       sizeof(map.objective_markers[0])),
      map.objective_marker_count));
}

struct Fonts {
  HFONT micro = nullptr;
  HFONT tiny = nullptr;
  HFONT small = nullptr;
  HFONT body = nullptr;
  HFONT title = nullptr;
  HFONT hero = nullptr;

  explicit Fonts(int height) {
    const float scale = (std::max)(0.75f, height / 1080.0f);
    auto make = [&](int pixels, int weight) {
      return CreateFontW(-static_cast<int>(pixels * scale), 0, 0, 0, weight,
                         FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, FF_DONTCARE, L"Arial Narrow");
    };
    micro = make(11, FW_NORMAL);
    tiny = make(14, FW_NORMAL);
    small = make(17, FW_NORMAL);
    body = make(21, FW_NORMAL);
    title = make(24, FW_SEMIBOLD);
    hero = make(34, FW_LIGHT);
  }
  ~Fonts() {
    if (micro) DeleteObject(micro);
    if (tiny) DeleteObject(tiny);
    if (small) DeleteObject(small);
    if (body) DeleteObject(body);
    if (title) DeleteObject(title);
    if (hero) DeleteObject(hero);
  }
};

std::wstring Wide(const char* text) {
  if (!text || !*text) return L"";
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                                  nullptr, 0);
  UINT cp = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (count <= 0) {
    cp = CP_ACP;
    flags = 0;
    count = MultiByteToWideChar(cp, flags, text, -1, nullptr, 0);
  }
  if (count <= 1) return L"";
  // MultiByteToWideChar includes the terminator in `count`; reserve space for
  // it while converting, then trim it from the C++ string.  Writing `count`
  // characters into a `count - 1` buffer corrupted the renderer's heap as
  // soon as a real localized inventory name arrived.
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(cp, flags, text, -1, &result[0], count);
  result.resize(static_cast<size_t>(count - 1));
  return result;
}

void Fill(HDC dc, const RECT& r, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &r, brush);
  DeleteObject(brush);
}

void Line(HDC dc, int x0, int y0, int x1, int y1, COLORREF color,
          int thickness = 1) {
  HPEN pen = CreatePen(PS_SOLID, thickness, color);
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, nullptr);
  LineTo(dc, x1, y1);
  SelectObject(dc, old);
  DeleteObject(pen);
}

void Plate(HDC dc, const RECT& r, bool selected = false,
           COLORREF fill_color = kPanel) {
  HBRUSH fill = CreateSolidBrush(fill_color);
  HPEN edge = CreatePen(PS_SOLID, selected ? 2 : 1,
                        selected ? kGoldBright : kGoldDim);
  HGDIOBJ old_fill = SelectObject(dc, fill);
  HGDIOBJ old_pen = SelectObject(dc, edge);
  Rectangle(dc, r.left, r.top, r.right, r.bottom);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_fill);
  DeleteObject(edge);
  DeleteObject(fill);
  Line(dc, r.left + 4, r.top + 3, r.right - 4, r.top + 3,
       selected ? RGB(119, 82, 23) : kRail);
}

void DragSelectionOverlay(HDC dc, const RECT& r, uint64_t animation_ms) {
  const int phase = static_cast<int>((animation_ms / 90u) % 8u);
  const int inset = 2 + (phase > 3 ? 1 : 0);
  const COLORREF color = phase < 4 ? kGoldBright : kGold;
  HPEN pen = CreatePen(PS_SOLID, phase < 4 ? 3 : 2, color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, r.left + inset, r.top + inset,
            r.right - inset, r.bottom - inset);
  const int corner = (std::max)(7, (std::min)(
      15, static_cast<int>((r.right - r.left) / 7)));
  MoveToEx(dc, r.left + 1, r.top + corner, nullptr);
  LineTo(dc, r.left + 1, r.top + 1);
  LineTo(dc, r.left + corner, r.top + 1);
  MoveToEx(dc, r.right - corner, r.bottom - 2, nullptr);
  LineTo(dc, r.right - 2, r.bottom - 2);
  LineTo(dc, r.right - 2, r.bottom - corner);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

void ActiveGoldOverlay(HDC dc, const RECT& r) {
  // GDI is the opaque chrome layer and native textures are composited after
  // it, so a restrained dark-gold field gives an active card a real overlay
  // without washing out its original augmentation icon.
  RECT inner{r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
  const int saved_dc = SaveDC(dc);
  IntersectClipRect(dc, inner.left, inner.top, inner.right, inner.bottom);
  Fill(dc, inner, RGB(45, 32, 7));
  Line(dc, inner.left, inner.top, inner.right, inner.top, kGoldBright, 3);
  Line(dc, inner.left, inner.bottom - 1, inner.right, inner.bottom - 1,
       RGB(137, 91, 18), 2);
  Line(dc, inner.left, inner.top, inner.left, inner.bottom, kGold, 2);
  Line(dc, inner.right - 1, inner.top, inner.right - 1, inner.bottom,
       kGold, 2);
  for (int x = inner.left - (inner.bottom - inner.top); x < inner.right;
       x += 18) {
    Line(dc, x, inner.bottom - 2,
         x + (inner.bottom - inner.top), inner.top + 2,
         RGB(64, 46, 12));
  }
  RestoreDC(dc, saved_dc);
}

void Text(HDC dc, HFONT font, const wchar_t* value, RECT r, COLORREF color,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
  if (!value) return;
  HGDIOBJ old = font ? SelectObject(dc, font) : nullptr;
  SetTextColor(dc, color);
  SetBkMode(dc, TRANSPARENT);
  DrawTextW(dc, value, -1, &r, flags);
  if (old) SelectObject(dc, old);
}

void Text(HDC dc, HFONT font, const std::wstring& value, RECT r,
          COLORREF color, UINT flags = DT_LEFT | DT_VCENTER |
                                      DT_SINGLELINE | DT_END_ELLIPSIS) {
  Text(dc, font, value.c_str(), r, color, flags);
}

void FittedText(HDC dc, const std::wstring& value, RECT r, COLORREF color,
                UINT flags, const HFONT* choices, size_t choice_count) {
  if (!dc || value.empty() || !choices || choice_count == 0) return;
  HFONT selected = choices[choice_count - 1];
  const int available_width = (std::max)(0L, r.right - r.left);
  const int available_height = (std::max)(0L, r.bottom - r.top);
  for (size_t index = 0; index < choice_count; ++index) {
    if (!choices[index]) continue;
    RECT measured{0, 0, available_width, available_height};
    HGDIOBJ old = SelectObject(dc, choices[index]);
    UINT measure_flags = (flags & ~(DT_VCENTER | DT_BOTTOM |
                                    DT_END_ELLIPSIS)) | DT_CALCRECT;
    DrawTextW(dc, value.c_str(), -1, &measured, measure_flags);
    if (old) SelectObject(dc, old);
    if (measured.right - measured.left <= available_width &&
        measured.bottom - measured.top <= available_height) {
      selected = choices[index];
      break;
    }
  }
  Text(dc, selected, value, r, color, flags);
}

void Section(HDC dc, const Fonts& fonts, const RECT& r, const wchar_t* title,
             const wchar_t* badge = nullptr) {
  Plate(dc, r);
  RECT badge_rect{};
  if (badge) {
    SIZE measured{};
    HGDIOBJ old = fonts.tiny ? SelectObject(dc, fonts.tiny) : nullptr;
    GetTextExtentPoint32W(dc, badge, static_cast<int>(std::wcslen(badge)),
                          &measured);
    if (old) SelectObject(dc, old);
    const int section_width = (std::max)(1L, r.right - r.left);
    const int maximum_width = (std::max)(
        73, (std::min)(150, section_width * 45 / 100));
    const int badge_width = (std::max)(
        73, (std::min)(maximum_width, static_cast<int>(measured.cx) + 18));
    badge_rect = RECT{r.right - 15 - badge_width, r.top + 9,
                      r.right - 15, r.top + 37};
  }
  RECT label{r.left + 18, r.top + 8,
             badge ? badge_rect.left - 8 : r.right - 18, r.top + 43};
  const HFONT title_fonts[] = {fonts.title, fonts.body, fonts.small,
                               fonts.tiny};
  FittedText(dc, title ? title : L"", label, kGold,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             title_fonts, sizeof(title_fonts) / sizeof(title_fonts[0]));
  Line(dc, r.left + 15, r.top + 43, r.right - 15, r.top + 43, kRail);
  if (badge) {
    Plate(dc, badge_rect, false, RGB(17, 17, 13));
    const HFONT badge_fonts[] = {fonts.tiny, fonts.micro};
    FittedText(dc, badge, badge_rect, kMuted,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
               badge_fonts, sizeof(badge_fonts) / sizeof(badge_fonts[0]));
  }
}

void AddHit(CustomUiFrame* frame, const RECT& rect, CustomHitKind kind,
            int value, int secondary = -1,
            uint32_t pickup_id = 0xFFFFFFFFu);

void Pager(HDC dc, const Fonts& fonts, const RECT& bounds, int page,
           int page_count, CustomHitKind previous_kind,
           CustomHitKind next_kind, CustomUiFrame* frame) {
  const int button_width = (std::min)(68, static_cast<int>(bounds.right - bounds.left) / 4);
  const bool can_page = page_count > 1;
  RECT previous{bounds.left, bounds.top, bounds.left + button_width, bounds.bottom};
  RECT next{bounds.right - button_width, bounds.top, bounds.right, bounds.bottom};
  Plate(dc, previous, can_page, RGB(8, 10, 8));
  Plate(dc, next, can_page, RGB(8, 10, 8));
  Text(dc, fonts.title, L"<", previous, can_page ? kGoldBright : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, fonts.title, L">", next,
       can_page ? kGoldBright : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  wchar_t label[40] = {};
  std::swprintf(label, sizeof(label) / sizeof(label[0]), L"%d / %d",
                page + 1, page_count);
  RECT center{previous.right + 4, bounds.top, next.left - 4, bounds.bottom};
  Text(dc, fonts.small, label, center, kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (can_page) {
    AddHit(frame, previous, previous_kind, page_count);
    AddHit(frame, next, next_kind, page_count);
  }
}

void AddHit(CustomUiFrame* frame, const RECT& rect, CustomHitKind kind,
            int value, int secondary, uint32_t pickup_id) {
  if (!frame) return;
  frame->hit_rects.push_back(rect);
  frame->hits.push_back(CustomHit{kind, value, secondary, pickup_id});
}

void AddPickup(CustomUiFrame* frame, uint32_t id, bool rotated,
               const RECT& rect, float alpha = 1.0f,
               bool reserve_quantity = false) {
  if (!frame || id == 0 || id == 0xFFFFFFFFu) return;
  const float pad_x = (std::max)(5.0f, (rect.right - rect.left) * 0.08f);
  const float pad_y = (std::max)(5.0f, (rect.bottom - rect.top) * 0.08f);
  CustomUiIcon icon;
  icon.kind = CustomUiIcon::Kind::kPickup;
  icon.id = id;
  icon.rotated = rotated;
  // D3D item art is composited after this GDI layer. Reserve the complete
  // lower count lane when needed so opaque pickup pixels cannot hide text.
  const float quantity_lane = reserve_quantity
      ? (std::min)(30.0f,
          (std::max)(0.0f, (rect.bottom - rect.top) * 0.36f))
      : pad_y;
  icon.rect = NativeUiRect{rect.left + pad_x, rect.top + pad_y,
                           rect.right - pad_x,
                           rect.bottom - (std::max)(pad_y, quantity_lane)};
  icon.color.alpha = alpha;
  frame->icons.push_back(icon);
}

void AddGoldTexture(CustomUiFrame* frame, uint32_t id, const RECT& rect,
                    float alpha = 1.0f, float brightness = 1.0f) {
  if (!frame || id == 0 || id == 0xFFFFFFFFu ||
      rect.right <= rect.left || rect.bottom <= rect.top) {
    return;
  }
  CustomUiIcon icon;
  icon.kind = CustomUiIcon::Kind::kTexture;
  icon.id = id;
  icon.rect = NativeUiRect{static_cast<float>(rect.left),
                           static_cast<float>(rect.top),
                           static_cast<float>(rect.right),
                           static_cast<float>(rect.bottom)};
  icon.color.red = 1.0f * brightness;
  icon.color.green = 0.72f * brightness;
  icon.color.blue = 0.16f * brightness;
  icon.color.alpha = alpha;
  frame->icons.push_back(icon);
}

void AddLock(CustomUiFrame* frame, const RECT& rect) {
  if (!frame || rect.right <= rect.left || rect.bottom <= rect.top) return;
  CustomUiIcon icon;
  icon.kind = CustomUiIcon::Kind::kLock;
  icon.rect = NativeUiRect{static_cast<float>(rect.left),
                           static_cast<float>(rect.top),
                           static_cast<float>(rect.right),
                           static_cast<float>(rect.bottom)};
  icon.color = NativeUiColor{1.0f, 0.72f, 0.12f, 1.0f};
  frame->icons.push_back(icon);
}

NativeUiColor NativeColor(COLORREF color, float alpha = 1.0f) {
  NativeUiColor result;
  result.red = GetRValue(color) / 255.0f;
  result.green = GetGValue(color) / 255.0f;
  result.blue = GetBValue(color) / 255.0f;
  result.alpha = alpha;
  return result;
}

void AddMapRaster(CustomUiFrame* frame, uint32_t map_dtp_id, int floor,
                  float left, float top, float right, float bottom,
                  const RECT& clip) {
  if (!frame || !map_dtp_id || right <= left || bottom <= top) return;
  const float clipped_left = (std::max)(left, static_cast<float>(clip.left));
  const float clipped_top = (std::max)(top, static_cast<float>(clip.top));
  const float clipped_right = (std::min)(right, static_cast<float>(clip.right));
  const float clipped_bottom = (std::min)(bottom, static_cast<float>(clip.bottom));
  if (clipped_right <= clipped_left || clipped_bottom <= clipped_top) return;

  CustomUiIcon icon;
  icon.kind = CustomUiIcon::Kind::kMapRaster;
  icon.id = map_dtp_id;
  icon.secondary = floor;
  icon.rect = NativeUiRect{clipped_left, clipped_top,
                           clipped_right, clipped_bottom};
  icon.uv = NativeUiUvRect{
      (clipped_left - left) / (right - left),
      (clipped_top - top) / (bottom - top),
      (clipped_right - left) / (right - left),
      (clipped_bottom - top) / (bottom - top)};
  frame->icons.push_back(icon);
}

bool AxisAlignedMarkerFits(const POINT& point, int radius, const RECT& clip) {
  return point.x - radius >= clip.left && point.y - radius >= clip.top &&
         point.x + radius + 1 <= clip.right &&
         point.y + radius + 1 <= clip.bottom;
}

bool RotatedMarkerFits(const POINT& point, int radius, const RECT& clip) {
  // DrawView rotates the complete square quad around its half-pixel center.
  // Reject against its circumscribed circle so no transparent quad or visible
  // arrow sample can ever cross into the adjacent location panel.
  const float center_x = static_cast<float>(point.x) + 0.5f;
  const float center_y = static_cast<float>(point.y) + 0.5f;
  const float half_diagonal =
      (static_cast<float>(radius * 2 + 1) * 0.5f) * 1.4142135623730951f;
  return center_x - half_diagonal >= static_cast<float>(clip.left) &&
         center_y - half_diagonal >= static_cast<float>(clip.top) &&
         center_x + half_diagonal <= static_cast<float>(clip.right) &&
         center_y + half_diagonal <= static_cast<float>(clip.bottom);
}

void AddMarker(CustomUiFrame* frame, const POINT& point, int radius,
               COLORREF color, bool diamond = false,
               const RECT* clip = nullptr) {
  if (!frame || radius <= 0) return;
  // The dark outline is two pixels larger than the requested marker. Native
  // D3D icons are drawn after GDI has restored its clip region, so reject the
  // whole overlay here instead of relying on the GDI canvas clip.
  if (clip && !AxisAlignedMarkerFits(point, radius + 2, *clip)) return;
  auto append = [&](int size, COLORREF tint) {
    CustomUiIcon icon;
    icon.kind = diamond ? CustomUiIcon::Kind::kDiamond
                        : CustomUiIcon::Kind::kCircle;
    icon.rect = NativeUiRect{static_cast<float>(point.x - size),
                             static_cast<float>(point.y - size),
                             static_cast<float>(point.x + size + 1),
                             static_cast<float>(point.y + size + 1)};
    icon.color = NativeColor(tint);
    frame->icons.push_back(icon);
  };
  append(radius + 2, RGB(2, 3, 3));
  append(radius, color);
}

void AddPlayerArrow(CustomUiFrame* frame, const POINT& point,
                    float native_heading_degrees, const RECT* clip = nullptr) {
  if (!frame || !std::isfinite(native_heading_degrees)) return;
  if (clip && !RotatedMarkerFits(point, 18, *clip)) return;
  auto append = [&](int radius, COLORREF color) {
    CustomUiIcon icon;
    icon.kind = CustomUiIcon::Kind::kPlayerArrow;
    icon.rect = NativeUiRect{static_cast<float>(point.x - radius),
                             static_cast<float>(point.y - radius),
                             static_cast<float>(point.x + radius + 1),
                             static_cast<float>(point.y + radius + 1)};
    // DXHR's serialized value is already clockwise in map/screen space, and
    // the renderer's pixel-coordinate rotation uses the same convention.
    icon.rotation_degrees = native_heading_degrees;
    icon.color = NativeColor(color);
    frame->icons.push_back(icon);
  };
  // A slightly larger dark copy gives the live gold arrow a readable outline
  // over both pale interiors and the busiest authored floor geometry.
  append(18, RGB(1, 2, 2));
  append(14, kGoldBright);
}

const InventoryItemSnapshot* FindPickup(const InventorySnapshot& snapshot,
                                        uint32_t pickup_id) {
  for (int i = 0; i < SafeInventoryItemCount(snapshot); ++i) {
    if (snapshot.items[i].pickup.pickup_id == pickup_id) return &snapshot.items[i];
  }
  return nullptr;
}

const SmartGlassInventoryItem* FindSmartGlassItem(
    const SmartGlassSnapshot& snapshot, const InventoryItemSnapshot& item) {
  auto find = [&](const std::vector<SmartGlassInventoryItem>& source)
      -> const SmartGlassInventoryItem* {
    for (const auto& candidate : source) {
      // pickupID identifies the DTP record. itemIndex is only a transient
      // InventorySlot index and is reused by quick-slot/update payloads. An
      // item-index-only match can therefore attach another pickup's name and
      // weapon flags to a perfectly valid native slot.
      if (candidate.pickup_id >= 0 &&
          static_cast<uint32_t>(candidate.pickup_id) == item.pickup.pickup_id) {
        return &candidate;
      }
    }
    return nullptr;
  };
  if (const auto* weapon = find(snapshot.weapons)) return weapon;
  return find(snapshot.inventory_items);
}

bool IsWeapon(const InventoryItemSnapshot& item,
              const SmartGlassSnapshot& snapshot) {
  // Retail keeps its equipped primary weapon (category 3) and throwable /
  // secondary item (category 4) in separate slots.  SmartGlass labels both as
  // weapons, but grenades belong in inventory/quick slots, never WEAPONS.
  if (item.pickup.category == 3) return true;
  if (item.pickup.category == 4) return false;
  const SmartGlassInventoryItem* smart = FindSmartGlassItem(snapshot, item);
  return smart && smart->pickup_id >= 0 && smart->is_weapon;
}

struct CoreAbilitySpec {
  int aug_id;
  int tree_id;
  uint32_t texture_id;
  const wchar_t* name;
  const wchar_t* description;
};

constexpr CoreAbilitySpec kCoreAbilities[] = {
    {0x14, 87231, 9429, L"TYPHOON",
     L"Deploys Jensen's close-range 360-degree explosive defense system."},
    {0x39, 87229, 9427, L"CLOAK",
     L"Activates optical camouflage while drawing power from the energy reserve."},
    {0x33, 87248, 9446, L"SMART VISION",
     L"Highlights useful objects and threats beyond Jensen's direct line of sight."},
    {0x35, 87235, 9508, L"RUN SILENTLY",
     L"Suppresses movement noise while Jensen moves at speed."},
};

static_assert(sizeof(kCoreAbilities) / sizeof(kCoreAbilities[0]) ==
                  kCoreAugmentationCount,
              "core augmentation presentation is incomplete");

int CoreAbilityTreeIndex(const InventorySnapshot& inventory, int slot) {
  if (!inventory.augmentation_catalog_valid || slot < 0 ||
      slot >= kCoreAugmentationCount) {
    return -1;
  }
  for (int index = 0; index < SafeAugmentationCount(inventory); ++index) {
    if (inventory.augmentations[index].tree_id ==
        kCoreAbilities[slot].tree_id) {
      return index;
    }
  }
  return -1;
}

int CoreAbilitySlotForTree(int tree_id) {
  for (int slot = 0; slot < kCoreAugmentationCount; ++slot) {
    if (kCoreAbilities[slot].tree_id == tree_id) return slot;
  }
  return -1;
}

bool AbilityUnlocked(const InventorySnapshot& inventory,
                     const SmartGlassSnapshot& smartglass, int slot,
                     int* smartglass_index = nullptr) {
  if (smartglass_index) *smartglass_index = -1;
  if (slot < 0 || slot >= kCoreAugmentationCount) return false;
  bool smartglass_unlocked = false;
  for (int index = 0;
       index < static_cast<int>(smartglass.augmentations.size()); ++index) {
    const SmartGlassAugmentation& augmentation =
        smartglass.augmentations[index];
    if (augmentation.aug_id != kCoreAbilities[slot].aug_id) continue;
    if (smartglass_index && (*smartglass_index < 0 || augmentation.installed ||
                            augmentation.learned)) {
      *smartglass_index = index;
    }
    smartglass_unlocked = smartglass_unlocked || augmentation.installed ||
                          augmentation.learned;
  }
  if (inventory.augmentation_state_valid) {
    return inventory.augmentation_unlocked[slot];
  }
  const int tree_index = CoreAbilityTreeIndex(inventory, slot);
  if (tree_index >= 0) {
    return inventory.augmentations[tree_index].tree_present;
  }
  return smartglass_unlocked;
}

bool ContainsAsciiInsensitive(const char* text, const char* needle) {
  if (!text || !needle || !*needle) return false;
  for (const char* start = text; *start; ++start) {
    const char* a = start;
    const char* b = needle;
    while (*a && *b &&
           std::tolower(static_cast<unsigned char>(*a)) ==
               std::tolower(static_cast<unsigned char>(*b))) {
      ++a;
      ++b;
    }
    if (!*b) return true;
  }
  return false;
}

int InventoryCardCapacity(const InventorySnapshot& inventory) {
  // Cybernetic Arm Prosthesis is the retail inventory-capacity family. Each
  // purchased Carrying/Inventory Capacity node unlocks six additional slots
  // in the compact lower-screen presentation: 6 -> 12 -> 18 total. Capacity
  // is global across pages; it must never repeat at the start of every page.
  if (inventory.augmentation_catalog_valid) {
    for (int tree_index = 0; tree_index < SafeAugmentationCount(inventory);
         ++tree_index) {
      const AugmentationTreeSnapshot& tree =
          inventory.augmentations[tree_index];
      if (tree.tree_id != 87234) continue;
      int levels = 0;
      bool capacity_metadata_found = false;
      for (int node_index = 0;
           node_index < SafeAugmentationNodeCount(tree); ++node_index) {
        const AugmentationNodeSnapshot& node = tree.nodes[node_index];
        const bool capacity_node =
            ContainsAsciiInsensitive(node.name, "capacity") ||
            ContainsAsciiInsensitive(node.name, "carrying") ||
            ContainsAsciiInsensitive(node.description, "inventory") ||
            ContainsAsciiInsensitive(node.help, "inventory");
        if (!capacity_node) continue;
        capacity_metadata_found = true;
        if (node.state == AugmentationNodeState::kPurchased) ++levels;
      }
      if (capacity_metadata_found) {
        return 6 + 6 * (std::max)(0, (std::min)(2, levels));
      }
      break;
    }
  }

  // The live log confirms Director's Cut expands the native grid width from
  // 8 to 10 when the first capacity node is purchased; the second tier adds
  // the next two columns. Use that saved physical state only during the short
  // catalog-streaming window.
  int fallback_levels = 0;
  if (inventory.grid_width >= 12) fallback_levels = 2;
  else if (inventory.grid_width >= 10) fallback_levels = 1;
  return 6 + fallback_levels * 6;
}

void Quantity(HDC dc, const Fonts& fonts, const RECT& card, int amount) {
  if (amount <= 1) return;
  wchar_t text[24] = {};
  std::swprintf(text, sizeof(text) / sizeof(text[0]), L"%d", amount);
  RECT shadow{card.left + 3, card.bottom - 28, card.right - 6, card.bottom - 5};
  Text(dc, fonts.body, text, shadow, RGB(0, 0, 0),
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  OffsetRect(&shadow, -2, -2);
  Text(dc, fonts.body, text, shadow, kText,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void ItemCard(HDC dc, const Fonts& fonts, const RECT& card,
              const InventoryItemSnapshot& item, bool selected,
              bool show_name, CustomHitKind hit_kind, CustomUiFrame* frame,
              const SmartGlassInventoryItem* smart_item = nullptr,
              uint64_t animation_ms = 0, int visual_slot = -1) {
  Plate(dc, card, selected, selected ? RGB(20, 19, 13) : kPanelRaised);
  RECT art = card;
  art.bottom -= show_name ? 34 : 4;
  AddPickup(frame, item.pickup.pickup_id, item.rotated, art, 1.0f,
            item.count > 1);
  Quantity(dc, fonts, art, item.count);
  if (show_name) {
    RECT name{card.left + 7, card.bottom - 34, card.right - 7, card.bottom - 5};
    std::wstring display_name = Wide(item.pickup.name);
    if (display_name.empty() && smart_item) {
      // SmartGlass `subtitle` is an equipment codename/product string, not
      // the label shown by DXHR's inventory. Falling back to it is what made a
      // real Stun Gun appear as "Buzzkill Threat Negation Device" whenever
      // the native localisation table was still streaming.
      display_name = smart_item->name;
    }
    // Never invent an item label. A missing localized string is a recoverable
    // streaming state and will be retried on the next snapshot.
    if (!display_name.empty()) {
      Text(dc, fonts.tiny, display_name, name,
           selected ? kGoldBright : kGold,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  }
  if (selected) DragSelectionOverlay(dc, card, animation_ms);
  AddHit(frame, card, hit_kind, item.item_index,
         visual_slot >= 0 ? visual_slot
                           : -1,
         item.pickup.pickup_id);
}

void WeaponCard(HDC dc, const Fonts& fonts, const RECT& card,
                const InventoryItemSnapshot& item, bool selected,
                CustomUiFrame* frame,
                const SmartGlassInventoryItem* smart_item,
                uint64_t animation_ms) {
  Plate(dc, card, selected, selected ? RGB(20, 19, 13) : kPanelRaised);

  // The magazine readout is gameplay-critical on the handheld panel. Reserve
  // a real text lane for it instead of forcing it through the 14px metadata
  // font used by long localized item names.
  constexpr int footer_height = 46;
  RECT art{card.left + 2, card.top + 2, card.right - 2,
           card.bottom - footer_height};
  AddPickup(frame, item.pickup.pickup_id, item.rotated, art, 1.0f, false);

  std::wstring display_name = Wide(item.pickup.name);
  if (display_name.empty() && smart_item) display_name = smart_item->name;

  const int card_width = card.right - card.left;
  const int ammo_width = item.magazine_state_valid
                             ? (std::max)(116, card_width * 40 / 100)
                             : 0;
  RECT name{card.left + 8, card.bottom - footer_height,
            card.right - 8 - ammo_width, card.bottom - 5};
  if (!display_name.empty()) {
    const HFONT name_fonts[] = {fonts.tiny, fonts.micro};
    FittedText(dc, display_name.c_str(), name,
               selected ? kGoldBright : kText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
               name_fonts, sizeof(name_fonts) / sizeof(name_fonts[0]));
  }

  if (item.magazine_state_valid) {
    const int loaded = (std::max)(0, item.magazine_ammo);
    const int capacity = (std::max)(1, item.magazine_capacity);
    const bool empty = loaded == 0;
    const bool low = !empty && loaded * 4 <= capacity;
    const COLORREF ammo_color = empty ? RGB(220, 83, 48)
                                      : (low ? kGoldBright : kGold);
    wchar_t ammo[40] = {};
    std::swprintf(ammo, sizeof(ammo) / sizeof(ammo[0]),
                  L"MAG %d / %d", loaded, capacity);
    RECT ammo_text{card.right - ammo_width - 5,
                   card.bottom - footer_height,
                   card.right - 8, card.bottom - 9};
    const HFONT ammo_fonts[] = {fonts.body, fonts.small, fonts.tiny};
    FittedText(dc, ammo, ammo_text, ammo_color,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE,
               ammo_fonts, sizeof(ammo_fonts) / sizeof(ammo_fonts[0]));

    // A two-pixel magazine rail makes a low weapon immediately visible while
    // leaving the localized weapon name and artwork untouched.
    RECT rail{ammo_text.left + 8, card.bottom - 8,
              ammo_text.right, card.bottom - 5};
    Fill(dc, rail, RGB(34, 31, 23));
    RECT fill = rail;
    fill.right = fill.left +
        static_cast<int>((fill.right - fill.left) *
                         (std::min)(loaded, capacity) /
                         static_cast<double>(capacity));
    if (fill.right > fill.left) Fill(dc, fill, ammo_color);
  }

  // Equipped state belongs to the loadout and quick-slot surfaces, not the
  // storage grid. A slim gold rail communicates the held weapon without
  // turning the whole card into the drag-selection treatment.
  if (item.equipped) {
    Line(dc, card.left + 3, card.top + 5,
         card.left + 3, card.bottom - 5, kGoldBright, 3);
    Line(dc, card.left + 4, card.top + 3,
         card.right - 4, card.top + 3, RGB(122, 84, 22), 2);
  }

  if (selected) DragSelectionOverlay(dc, card, animation_ms);

  AddHit(frame, card, CustomHitKind::kWeapon, item.item_index, -1,
         item.pickup.pickup_id);
}

void Bar(HDC dc, const Fonts& fonts, RECT r, const wchar_t* label,
         double value, double maximum, COLORREF color) {
  Plate(dc, r, false, RGB(7, 9, 8));
  const int height = static_cast<int>(r.bottom - r.top);
  const int icon_size = (std::min)(45,
      (std::max)(24, height - 18));
  const int icon_top = r.top + (height - icon_size) / 2;
  RECT icon{r.left + 10, icon_top, r.left + 10 + icon_size,
            icon_top + icon_size};
  HBRUSH dot = CreateSolidBrush(RGB(19, 20, 16));
  HGDIOBJ old = SelectObject(dc, dot);
  HPEN ring = CreatePen(PS_SOLID, 1, kGoldDim);
  HGDIOBJ old_pen = SelectObject(dc, ring);
  Ellipse(dc, icon.left, icon.top, icon.right, icon.bottom);
  SelectObject(dc, old_pen);
  SelectObject(dc, old);
  DeleteObject(ring);
  DeleteObject(dot);

  // Compact native-style glyphs keep the cards readable at a glance instead
  // of leaving the circular status medallions as empty outlines.
  const int cx = (icon.left + icon.right) / 2;
  const int cy = (icon.top + icon.bottom) / 2;
  if (std::wcscmp(label, L"ENERGY") == 0) {
    POINT bolt[] = {{cx + 2, icon.top + 7}, {cx - 9, cy + 2},
                    {cx - 1, cy + 2}, {cx - 5, icon.bottom - 7},
                    {cx + 10, cy - 3}, {cx + 2, cy - 3}};
    HBRUSH glyph = CreateSolidBrush(kGoldBright);
    HGDIOBJ previous_brush = SelectObject(dc, glyph);
    HGDIOBJ previous_pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, bolt, sizeof(bolt) / sizeof(bolt[0]));
    SelectObject(dc, previous_pen);
    SelectObject(dc, previous_brush);
    DeleteObject(glyph);
  } else {
    HPEN glyph = CreatePen(PS_SOLID, 3, kGoldBright);
    HGDIOBJ previous_pen = SelectObject(dc, glyph);
    POINT pulse[] = {{icon.left + 7, cy}, {cx - 7, cy}, {cx - 3, cy - 8},
                     {cx + 2, cy + 9}, {cx + 6, cy - 4},
                     {icon.right - 7, cy - 4}};
    Polyline(dc, pulse, sizeof(pulse) / sizeof(pulse[0]));
    SelectObject(dc, previous_pen);
    DeleteObject(glyph);
  }

  const int content_left = icon.right + 11;
  const int header_bottom = r.top + (std::max)(25, height / 2);
  RECT label_rect{content_left, r.top + 4,
                  content_left + (r.right - content_left) / 2,
                  header_bottom};
  const HFONT label_fonts[] = {fonts.small, fonts.tiny, fonts.micro};
  FittedText(dc, label ? label : L"", label_rect, kGold,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             label_fonts, sizeof(label_fonts) / sizeof(label_fonts[0]));
  wchar_t value_text[64] = L"-- / --";
  if (value >= 0.0 && maximum > 0.0) {
    std::swprintf(value_text, sizeof(value_text) / sizeof(value_text[0]),
                  L"%.0f / %.0f", value, maximum);
  }
  RECT value_rect{label_rect.right - 4, r.top + 4, r.right - 13,
                  header_bottom};
  const HFONT value_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                               fonts.micro};
  FittedText(dc, value_text, value_rect, kText,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             value_fonts, sizeof(value_fonts) / sizeof(value_fonts[0]));
  const int track_height = (std::max)(10, (std::min)(15, height / 5));
  RECT track{content_left, r.bottom - track_height - 9,
             r.right - 13, r.bottom - 9};
  Fill(dc, track, RGB(18, 17, 12));
  HBRUSH frame_brush = CreateSolidBrush(kGoldDim);
  FrameRect(dc, &track, frame_brush);
  DeleteObject(frame_brush);
  double ratio = (maximum > 0.0 && value >= 0.0) ? value / maximum : 0.0;
  ratio = (std::max)(0.0, (std::min)(1.0, ratio));
  RECT fill = track;
  fill.right = fill.left + static_cast<int>((fill.right - fill.left) * ratio);
  InflateRect(&fill, -2, -2);
  if (fill.right > fill.left) {
    Fill(dc, fill, color);
    const int full_width = (track.right - track.left) - 4;
    for (int segment = 1; segment < 10; ++segment) {
      const int separator = track.left + 2 + full_width * segment / 10;
      if (separator >= fill.right) break;
      Line(dc, separator, fill.top, separator - 3, fill.bottom,
           RGB(63, 49, 23));
    }
  }
}

struct InventoryRects {
  RECT weapons;
  RECT inventory;
  RECT status;
  RECT quick;
  RECT grid;
};

InventoryRects InventoryGeometry(int width, int height, int header) {
  const int margin = (std::max)(14, width / 70);
  const int gap = (std::max)(9, width / 120);
  const int bottom = height - margin;
  const int quick_h = (std::max)(142, static_cast<int>(height * 0.155f));
  const int body_bottom = bottom - quick_h - gap;
  const int left_w = static_cast<int>(width * 0.286f);
  const int middle_w = static_cast<int>(width * 0.378f);
  InventoryRects result;
  result.weapons = RECT{margin, header + margin, margin + left_w, body_bottom};
  result.inventory = RECT{result.weapons.right + gap, header + margin,
                          result.weapons.right + gap + middle_w, body_bottom};
  result.status = RECT{result.inventory.right + gap, header + margin,
                       width - margin, body_bottom};
  result.quick = RECT{margin, body_bottom + gap, width - margin, bottom};
  // The shorter desktop test panel is height-bound rather than width-bound.
  // Reserve one compact row for Sort there; on the Thor the square grid is
  // already width-bound, so this does not reduce its card size at all.
  const int inventory_footer_reserve = height < 900 ? 91 : 61;
  result.grid = RECT{result.inventory.left + 14, result.inventory.top + 52,
                     result.inventory.right - 14,
                     result.inventory.bottom - inventory_footer_reserve};
  return result;
}

void DrawInventory(HDC dc, const Fonts& fonts, int width, int height, int header,
                   const InventorySnapshot& inv, const SmartGlassSnapshot& sg,
                   const CustomUiState& state, CustomUiFrame* frame) {
  InventoryRects r = InventoryGeometry(width, height, header);
  const int unlocked_cards = InventoryCardCapacity(inv);
  const int item_count = SafeInventoryItemCount(inv);
  std::vector<const InventoryItemSnapshot*> weapons;
  std::vector<const InventoryItemSnapshot*> items;
  for (int i = 0; i < item_count; ++i) {
    const auto& item = inv.items[i];
    if (IsWeapon(item, sg)) weapons.push_back(&item);
    // Loadout is a useful duplicate view, not alternate storage. Keeping every
    // item here prevents a sixth weapon from becoming unreachable.
    items.push_back(&item);
  }
  wchar_t inventory_badge[40] = {};
  std::swprintf(inventory_badge, 40, L"%d ITEMS  /  %d UNLOCKED",
                item_count, unlocked_cards);
  wchar_t weapon_badge[32] = {};
  std::swprintf(weapon_badge, 32, L"%d WEAPONS",
                static_cast<int>(weapons.size()));
  Section(dc, fonts, r.weapons, L"//  WEAPONS", weapon_badge);
  Section(dc, fonts, r.inventory, L"//  INVENTORY", inventory_badge);
  Section(dc, fonts, r.status, L"//  STATUS", L"LIVE");
  Section(dc, fonts, r.quick, L"//  QUICK SLOTS",
          L"TAP USE  /  HOLD DRAG");

  const int weapon_gap = 8;
  const int weapon_top = r.weapons.top + 52;
  const int weapon_bottom = r.weapons.bottom - 12;
  const int weapon_h = (weapon_bottom - weapon_top - weapon_gap * 4) / 5;
  for (int row = 0; row < 5; ++row) {
    RECT card{r.weapons.left + 13, weapon_top + row * (weapon_h + weapon_gap),
              r.weapons.right - 13,
              weapon_top + row * (weapon_h + weapon_gap) + weapon_h};
    if (row < static_cast<int>(weapons.size())) {
      WeaponCard(dc, fonts, card, *weapons[row],
                 weapons[row]->item_index == state.selected_item, frame,
                 FindSmartGlassItem(sg, *weapons[row]), state.animation_ms);
    } else {
      Plate(dc, card, false, RGB(6, 8, 7));
      wchar_t empty_label[32] = {};
      std::swprintf(empty_label, 32, L"WEAPON %d  /  EMPTY", row + 1);
      Text(dc, fonts.tiny, empty_label, card, RGB(57, 57, 55),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }

  const int columns = 3;
  const int rows = 4;
  const int cell_gap = 8;
  // Inventory cards are physical touch targets and artwork frames, so never
  // stretch them with the panel aspect ratio. Fit a centered 3x4 matrix of
  // exact squares into the available inventory area instead.
  const int available_grid_width = r.grid.right - r.grid.left;
  const int available_grid_height = r.grid.bottom - r.grid.top;
  const int cell_size = (std::max)(1, (std::min)(
      (available_grid_width - cell_gap * (columns - 1)) / columns,
      (available_grid_height - cell_gap * (rows - 1)) / rows));
  const int occupied_grid_width = cell_size * columns + cell_gap * (columns - 1);
  const int occupied_grid_height = cell_size * rows + cell_gap * (rows - 1);
  r.grid.left += (available_grid_width - occupied_grid_width) / 2;
  // Keep the first inventory row level with the first weapon card. Vertical
  // centering was the small but visible offset in the previous layout.
  r.grid.right = r.grid.left + occupied_grid_width;
  r.grid.bottom = r.grid.top + occupied_grid_height;
  if (frame) {
    frame->inventory_grid = r.grid;
    frame->inventory_grid_width = inv.grid_width;
    frame->inventory_grid_height = inv.grid_height;
    frame->inventory_card_columns = columns;
    frame->inventory_card_rows = rows;
    frame->inventory_card_size = cell_size;
    frame->inventory_card_gap = cell_gap;
    frame->inventory_card_page = state.inventory_page;
    frame->inventory_unlocked_slots = unlocked_cards;
    // A single background target makes card gaps valid drop space. Item cards
    // are registered afterward and win HitCustomUi's reverse-order test.
    AddHit(frame, r.grid, CustomHitKind::kInventoryGrid, -1);
  }
  const int cell_w = cell_size;
  const int cell_h = cell_size;
  constexpr int per_page = columns * rows;
  // Keep the next capacity page available even when it contains only locked
  // slots. Previously increasing capacity also increased `per_page`, which
  // could collapse two pages into one and make both arrows disappear.
  // Page count represents this UI's capacity upgrade, not the native
  // container's historical contents. Otherwise an old/full save can create a
  // tail of pages containing nothing but locked cards beyond the 18-slot cap.
  const int visible_slot_count = unlocked_cards;
  const int page_count = (std::max)(
      2, (visible_slot_count + per_page - 1) / per_page);
  const int page = (std::max)(0, (std::min)(state.inventory_page, page_count - 1));
  if (frame) frame->inventory_card_page = page;
  // Always draw the complete 3x4 footprint. Slots beyond the currently saved
  // capacity stay visible as upgrade targets instead of becoming unexplained
  // empty space below the usable rows.
  for (int cell = 0; cell < columns * rows; ++cell) {
    const int column = cell % columns;
    const int row = cell / columns;
    RECT card{r.grid.left + column * (cell_w + cell_gap),
              r.grid.top + row * (cell_h + cell_gap),
              r.grid.left + column * (cell_w + cell_gap) + cell_w,
              r.grid.top + row * (cell_h + cell_gap) + cell_h};
    const int global_slot = page * per_page + cell;
    const InventoryItemSnapshot* card_item = nullptr;
    if (!state.inventory_slots.empty() &&
        global_slot < static_cast<int>(state.inventory_slots.size())) {
      const int wanted_item = state.inventory_slots[global_slot];
      for (const InventoryItemSnapshot* candidate : items) {
        if (candidate && candidate->item_index == wanted_item) {
          card_item = candidate;
          break;
        }
      }
    } else if (global_slot < static_cast<int>(items.size())) {
      card_item = items[global_slot];
    }
    if (global_slot < unlocked_cards && card_item) {
      ItemCard(dc, fonts, card, *card_item,
               card_item->item_index == state.selected_item, true,
               CustomHitKind::kInventoryItem, frame,
               FindSmartGlassItem(sg, *card_item), state.animation_ms,
               global_slot);
    } else if (global_slot < unlocked_cards) {
      Plate(dc, card, false, RGB(6, 8, 7));
    } else {
      Plate(dc, card, false, RGB(4, 5, 5));
      const int lock_size = (std::max)(24, (std::min)(44, cell_size / 2));
      RECT lock{(card.left + card.right - lock_size) / 2,
                (card.top + card.bottom - lock_size) / 2 - 5,
                (card.left + card.right + lock_size) / 2,
                (card.top + card.bottom + lock_size) / 2 - 5};
      AddLock(frame, lock);
      RECT locked_text{card.left + 4, card.bottom - 24,
                       card.right - 4, card.bottom - 3};
      Text(dc, fonts.micro, L"CAPACITY AUG", locked_text, RGB(92, 75, 39),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  }
  wchar_t pager[48] = {};
  std::swprintf(pager, 48, L"%d / %d", page + 1, page_count);
  const bool can_page = page_count > 1;
  const int controls_top = r.inventory.bottom - 53;
  const int controls_bottom = r.inventory.bottom - 9;
  RECT previous{r.inventory.left + 14, controls_top,
                r.inventory.left + 82, controls_bottom};
  RECT next{r.inventory.right - 82, controls_top,
            r.inventory.right - 14, controls_bottom};
  Plate(dc, previous, can_page, RGB(8, 10, 8));
  Plate(dc, next, can_page, RGB(8, 10, 8));
  Text(dc, fonts.title, L"<", previous, can_page ? kGoldBright : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, fonts.title, L">", next,
       can_page ? kGoldBright : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (can_page) {
    AddHit(frame, previous, CustomHitKind::kInventoryPrevious, page_count);
    AddHit(frame, next, CustomHitKind::kInventoryNext, page_count);
  }
  RECT discard{previous.right + 8, controls_top,
               next.left - 7, controls_bottom};

  // The square grid leaves a deliberate strip above the fixed-size footer.
  // Sort owns that whole strip: matching the footer's outer edges makes the
  // utility column read as one deliberate block instead of a narrow button
  // floating above Trash.
  const int sort_available_top = r.grid.bottom + 6;
  const int sort_available_bottom = controls_top - 6;
  if (sort_available_bottom - sort_available_top >= 24) {
    RECT sort{previous.left, sort_available_top,
              next.right, sort_available_bottom};
    Plate(dc, sort, true, RGB(12, 13, 10));
    const HFONT sort_fonts[] = {fonts.small, fonts.tiny, fonts.micro};
    FittedText(dc, L"SORT INVENTORY", sort, kGold,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
               sort_fonts, sizeof(sort_fonts) / sizeof(sort_fonts[0]));
    AddHit(frame, sort, CustomHitKind::kInventorySort, 0);
  }

  const bool discard_selected = state.selected_item >= 0;
  Plate(dc, discard, discard_selected,
        discard_selected ? RGB(30, 14, 9) : RGB(8, 9, 8));
  wchar_t discard_label[96] = {};
  std::wstring selected_name;
  if (discard_selected) {
    for (const InventoryItemSnapshot* item : items) {
      if (item && item->item_index == state.selected_item) {
        selected_name = Wide(item->pickup.name);
        break;
      }
    }
  }
  if (!selected_name.empty()) {
    std::swprintf(discard_label,
                  sizeof(discard_label) / sizeof(discard_label[0]),
                  L"TRASH %ls  /  HOLD     %ls", selected_name.c_str(), pager);
  } else {
    std::swprintf(discard_label,
                  sizeof(discard_label) / sizeof(discard_label[0]),
                  L"TRASH  /  HOLD TO DISCARD     %ls", pager);
  }
  const HFONT discard_fonts[] = {fonts.small, fonts.tiny, fonts.micro};
  FittedText(dc, discard_label, discard,
       discard_selected ? kGoldBright : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
       discard_fonts, sizeof(discard_fonts) / sizeof(discard_fonts[0]));
  AddHit(frame, discard, CustomHitKind::kDiscard, state.selected_item);

  const int status_left = r.status.left + 13;
  const int status_right = r.status.right - 13;
  int y = r.status.top + 53;
  const int content_bottom = r.status.bottom - 12;
  // Keep both real resources compact. The old fill-the-column calculation
  // made these cards over 200 px tall on the Thor and pushed useful
  // progression fields off the 720p desktop test panel.
  const int status_gap = (std::max)(5, (std::min)(8, height / 130));
  const int bar_h = (std::max)(52, (std::min)(82,
      static_cast<int>(height * 0.075f)));
  const int metric_h = (std::max)(36, (std::min)(54,
      static_cast<int>(height * 0.052f)));
  const int content_height = (std::max)(0, content_bottom - y);
  const int fixed_height = bar_h * 2 + metric_h * 3 + status_gap * 5;
  const int abilities_h = (std::max)(0, content_height - fixed_height);
  const double health = sg.status.health >= 0.0
      ? sg.status.health : static_cast<double>(inv.health);
  const double max_health = sg.status.max_health > 0.0
      ? sg.status.max_health : static_cast<double>(inv.max_health);
  const double energy = sg.status.energy >= 0.0
      ? sg.status.energy : static_cast<double>(inv.energy);
  const double max_energy = sg.status.max_energy > 0.0
      ? sg.status.max_energy : static_cast<double>(inv.max_energy);
  Bar(dc, fonts, RECT{status_left, y, status_right, y + bar_h}, L"HEALTH",
      health, max_health, RGB(218, 97, 55));
  y += bar_h + status_gap;
  Bar(dc, fonts, RECT{status_left, y, status_right, y + bar_h}, L"ENERGY",
      energy, max_energy, RGB(236, 174, 55));
  y += bar_h + status_gap;

  if (abilities_h > 0) {
    RECT ability_panel{status_left, y, status_right, y + abilities_h};
    Plate(dc, ability_panel, false, RGB(7, 9, 8));
    const int title_h = (std::max)(20, (std::min)(29, abilities_h / 4));
    RECT ability_title{ability_panel.left + 10, ability_panel.top + 2,
                       ability_panel.right - 10,
                       ability_panel.top + title_h};

    int unlocked_count = 0;
    int active_count = 0;
    for (int slot = 0; slot < kCoreAugmentationCount; ++slot) {
      if (AbilityUnlocked(inv, sg, slot)) ++unlocked_count;
      if (inv.augmentation_active_valid &&
          inv.augmentation_active[slot]) ++active_count;
    }

    wchar_t ability_heading[48] = {};
    std::swprintf(ability_heading, 48,
                  L"AUG ABILITIES   %d READY  /  %d ACTIVE",
                  unlocked_count, active_count);
    const HFONT ability_fonts[] = {fonts.tiny, fonts.micro};
    FittedText(dc, ability_heading, ability_title, kGold,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
               ability_fonts,
               sizeof(ability_fonts) / sizeof(ability_fonts[0]));
    Line(dc, ability_panel.left + 9, ability_title.bottom,
         ability_panel.right - 9, ability_title.bottom, kRail);

    const int card_gap = (std::max)(4, status_gap - 1);
    const int cards_top = ability_title.bottom + card_gap;
    const int cards_bottom = ability_panel.bottom - card_gap;
    const int card_w = (ability_panel.right - ability_panel.left -
                        card_gap * 3) / 2;
    const int card_h = (cards_bottom - cards_top - card_gap) / 2;
    for (int slot = 0; slot < kCoreAugmentationCount; ++slot) {
      const int column = slot % 2;
      const int row = slot / 2;
      RECT card{ability_panel.left + card_gap + column * (card_w + card_gap),
                cards_top + row * (card_h + card_gap),
                ability_panel.left + card_gap + column * (card_w + card_gap) +
                    card_w,
                cards_top + row * (card_h + card_gap) + card_h};
      if (card.bottom <= card.top || card.right <= card.left) continue;
      const CoreAbilitySpec& spec = kCoreAbilities[slot];
      int aug_index = -1;
      const bool unlocked = AbilityUnlocked(inv, sg, slot, &aug_index);
      const bool active = unlocked && inv.augmentation_active_valid &&
                          inv.augmentation_active[slot];
      Plate(dc, card, false, unlocked ? RGB(14, 15, 11) : RGB(5, 7, 7));
      if (active) ActiveGoldOverlay(dc, card);
      const int glyph_size = (std::max)(1, (std::min)(
          86, (std::min)(card_w - 14, card_h - 28)));
      RECT glyph{(card.left + card.right - glyph_size) / 2,
                 card.top + 3,
                 (card.left + card.right + glyph_size) / 2,
                 card.top + 3 + glyph_size};
      AddGoldTexture(frame, spec.texture_id, glyph,
                     unlocked ? 1.0f : 0.34f,
                     active ? 1.22f : (unlocked ? 1.0f : 0.55f));
      RECT name{card.left + 4, card.bottom - 25, card.right - 4,
                card.bottom - 2};
      if (!unlocked) {
        const int lock_size = (std::max)(30, (std::min)(
            58, (std::min)(card_w - 18, card_h - 31)));
        const int lock_center_y = (card.top + card.bottom - 20) / 2;
        RECT lock{(card.left + card.right - lock_size) / 2,
                  lock_center_y - lock_size / 2,
                  (card.left + card.right + lock_size) / 2,
                  lock_center_y + lock_size / 2};
        AddLock(frame, lock);
      }
      const UINT name_flags = DT_CENTER | DT_VCENTER | DT_WORDBREAK |
                              DT_END_ELLIPSIS;
      std::wstring label = spec.name;
      Text(dc, fonts.tiny, label,
           name, active ? RGB(255, 225, 126) :
                          (unlocked ? kGoldBright : kMuted),
           name_flags);
      const int catalog_selection = CoreAbilityTreeIndex(inv, slot);
      const int fallback_selection = catalog_selection >= 0
          ? catalog_selection : slot;
      AddHit(frame, card, CustomHitKind::kStatusAugmentation, slot,
             fallback_selection);
    }
    y += abilities_h + status_gap;
  }

  RECT xp{status_left, y, status_right, y + metric_h};
  Plate(dc, xp, false, RGB(8, 10, 9));
  wchar_t xp_text[96] = L"XP  --     NEXT PRAXIS  --";
  const int total_xp = sg.status.total_xp >= 0
      ? sg.status.total_xp : inv.total_xp;
  const int next_praxis = sg.status.next_praxis >= 0
      ? sg.status.next_praxis : inv.next_praxis;
  if (total_xp >= 0 || next_praxis >= 0) {
    std::swprintf(xp_text, 96, L"XP  %d     NEXT PRAXIS  %d",
                  (std::max)(0, total_xp),
                  (std::max)(0, next_praxis));
  }
  const HFONT metric_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                                fonts.micro};
  FittedText(dc, xp_text, xp, kText,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             metric_fonts, sizeof(metric_fonts) / sizeof(metric_fonts[0]));
  y += metric_h + status_gap;
  RECT money{status_left, y, status_right, y + metric_h};
  Plate(dc, money, false, RGB(8, 10, 9));
  wchar_t money_text[64] = {};
  const int credits = sg.status.credits >= 0 ? sg.status.credits :
                      static_cast<int>(inv.credits);
  std::swprintf(money_text, 64, L"CREDITS     %d%c", credits, 0x00A5);
  FittedText(dc, money_text, money, kCyan,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             metric_fonts, sizeof(metric_fonts) / sizeof(metric_fonts[0]));
  y += metric_h + status_gap;
  RECT praxis{status_left, y, status_right,
              (std::min)(static_cast<int>(r.status.bottom - 12), y + metric_h)};
  Plate(dc, praxis, false, RGB(8, 10, 9));
  wchar_t praxis_text[64] = L"PRAXIS POINTS     --";
  const int praxis_points = sg.status.praxis_points >= 0
      ? sg.status.praxis_points : inv.praxis_points;
  if (praxis_points >= 0) {
    std::swprintf(praxis_text, 64, L"PRAXIS POINTS     %d",
                  praxis_points);
  }
  FittedText(dc, praxis_text, praxis, kGold,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             metric_fonts, sizeof(metric_fonts) / sizeof(metric_fonts[0]));

  const int quick_left = r.quick.left + 18;
  const int quick_right = r.quick.right - 18;
  const int quick_top = r.quick.top + 47;
  const int quick_bottom = r.quick.bottom - 12;
  const int quick_gap = 8;
  const int quick_w = (quick_right - quick_left - quick_gap * 9) / 10;
  const int quick_size = (std::max)(1, (std::min)(quick_w,
      quick_bottom - quick_top));
  const int quick_y = quick_top +
      ((quick_bottom - quick_top) - quick_size) / 2;
  bool smart_quick_has_zero = false;
  bool smart_quick_has_ten = false;
  for (const auto& quick : sg.quick_slots) {
    smart_quick_has_zero = smart_quick_has_zero || quick.slot == 0;
    smart_quick_has_ten = smart_quick_has_ten || quick.slot == 10;
  }
  const bool smart_quick_one_based = !smart_quick_has_zero && smart_quick_has_ten;
  for (int slot = 0; slot < 10; ++slot) {
    const int cell_left = quick_left + slot * (quick_w + quick_gap);
    const int card_left = cell_left + (quick_w - quick_size) / 2;
    RECT card{card_left, quick_y, card_left + quick_size,
              quick_y + quick_size};
    const auto& quick = inv.quick_slots[slot];
    uint32_t quick_pickup = quick.pickup_id;
    int quick_amount = 0;
    bool quick_rotated = false;
    const InventoryItemSnapshot* source =
        quick_pickup != 0 && quick_pickup != 0xFFFFFFFFu
            ? FindPickup(inv, quick_pickup) : nullptr;
    if (source) {
      quick_amount = source->count;
      quick_rotated = source->rotated;
    }
    if (quick_pickup == 0 || quick_pickup == 0xFFFFFFFFu) {
      for (size_t i = 0; i < sg.quick_slots.size(); ++i) {
        const auto& smart_quick = sg.quick_slots[i];
        const int expected_slot = slot + (smart_quick_one_based ? 1 : 0);
        if ((smart_quick.slot == expected_slot ||
             (smart_quick.slot < 0 && static_cast<int>(i) == slot)) &&
            smart_quick.pickup_id >= 0) {
          quick_pickup = static_cast<uint32_t>(smart_quick.pickup_id);
          quick_amount = smart_quick.amount;
          break;
        }
      }
    }
    if (!source && quick_pickup != 0 &&
        quick_pickup != 0xFFFFFFFFu) {
      source = FindPickup(inv, quick_pickup);
      if (source) {
        quick_amount = source->count;
        quick_rotated = source->rotated;
      }
    }
    const bool quick_equipped = source && source->equipped;
    Plate(dc, card, quick_equipped,
          quick_equipped ? RGB(20, 18, 11) : RGB(10, 12, 10));
    if (quick_equipped) {
      Line(dc, card.left + 3, card.bottom - 4,
           card.right - 3, card.bottom - 4, kGoldBright, 3);
    }
    wchar_t number[3] = {};
    std::swprintf(number, 3, L"%d", (slot + 1) % 10);
    RECT number_rect{card.left + 6, card.top + 3, card.right - 4,
                     card.top + 24};
    Text(dc, fonts.tiny, number, number_rect,
         quick_equipped ? kGoldBright : kGold);
    if (quick_pickup != 0 && quick_pickup != 0xFFFFFFFFu) {
      RECT art{card.left + 4, card.top + 18, card.right - 4, card.bottom - 4};
      AddPickup(frame, quick_pickup, quick_rotated, art, 1.0f,
                quick_amount > 1);
      Quantity(dc, fonts, art, quick_amount);
    }
    AddHit(frame, card, CustomHitKind::kQuickSlot, slot,
           static_cast<int>(quick_pickup), quick_pickup);
  }

  // A physical drag gets a native-art ghost under the finger. The latched
  // tap-to-move mode uses the animated source outline above, so it remains
  // usable without covering the intended destination with a floating card.
  if (state.drag_active && state.drag_item >= 0) {
    const InventoryItemSnapshot* moving = nullptr;
    for (const InventoryItemSnapshot* item : items) {
      if (item && item->item_index == state.drag_item) {
        moving = item;
        break;
      }
    }
    if (moving) {
      const int ghost_size = (std::max)(76, (std::min)(112, height / 8));
      int left = static_cast<int>(std::lround(state.drag_x)) - ghost_size / 2;
      int top = static_cast<int>(std::lround(state.drag_y)) - ghost_size / 2;
      left = (std::max)(8, (std::min)(width - ghost_size - 8, left));
      top = (std::max)(header + 4,
                       (std::min)(height - ghost_size - 8, top));
      RECT ghost{left, top, left + ghost_size, top + ghost_size};
      Plate(dc, ghost, true, RGB(22, 19, 10));
      DragSelectionOverlay(dc, ghost, state.animation_ms);
      AddPickup(frame, moving->pickup.pickup_id, moving->rotated, ghost,
                0.92f, moving->count > 1);
      Quantity(dc, fonts, ghost, moving->count);
    }
  }
}

void EmptyNotice(HDC dc, const Fonts& fonts, RECT r, const wchar_t* title,
                 const wchar_t* detail) {
  // Empty/live-streaming states are centered inside their actual content
  // panel. The old fixed top offsets collided with Section headings on short
  // windows and left the rest of the panel looking accidentally unfinished.
  const int width = (std::max)(1L, r.right - r.left);
  const int height = (std::max)(1L, r.bottom - r.top);
  const int card_width = (std::max)(120, (std::min)(
      width - 24, width * 82 / 100));
  const int desired_card_height = (std::max)(86, height * 36 / 100);
  const int card_height = (std::max)(1, (std::min)(
      height - 24, desired_card_height));
  RECT card{r.left + (width - card_width) / 2,
            r.top + (height - card_height) / 2,
            r.left + (width + card_width) / 2,
            r.top + (height + card_height) / 2};
  Plate(dc, card, false, RGB(7, 10, 9));
  const int title_height = (std::max)(30, card_height * 42 / 100);
  RECT t{card.left + 14, card.top + 8,
         card.right - 14, card.top + title_height};
  const HFONT title_fonts[] = {fonts.hero, fonts.title, fonts.body,
                               fonts.small, fonts.tiny};
  FittedText(dc, title ? title : L"", t, kGold,
             DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS,
             title_fonts, sizeof(title_fonts) / sizeof(title_fonts[0]));
  Line(dc, card.left + 18, t.bottom + 2, card.right - 18, t.bottom + 2,
       kRail);
  RECT d{card.left + 18, t.bottom + 8,
         card.right - 18, card.bottom - 9};
  const HFONT detail_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                                fonts.micro};
  FittedText(dc, detail ? detail : L"", d, kMuted,
             DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS,
             detail_fonts, sizeof(detail_fonts) / sizeof(detail_fonts[0]));
}

struct MapViewportGeometry {
  RECT panel = {};
  RECT canvas = {};
  float zoom = 1.0f;
  float pan_x = 0.0f;
  float pan_y = 0.0f;
  float canvas_width = 1.0f;
  float canvas_height = 1.0f;
  float fitted_width = 1.0f;
  float fitted_height = 1.0f;
  float center_x = 0.0f;
  float center_y = 0.0f;
};

MapViewportGeometry MakeMapViewportGeometry(
    int width, int height, int header, const NativeMapSnapshot& native_map,
    float requested_zoom, float requested_pan_x, float requested_pan_y) {
  constexpr int kMargin = 18;
  MapViewportGeometry viewport;
  viewport.panel = RECT{kMargin, header + kMargin,
                        static_cast<int>(width * 0.73f), height - kMargin};
  viewport.canvas = RECT{viewport.panel.left + 14,
                         viewport.panel.top + 52,
                         viewport.panel.right - 14,
                         viewport.panel.bottom - 14};
  viewport.canvas_width = static_cast<float>((std::max)(
      1L, viewport.canvas.right - viewport.canvas.left));
  viewport.canvas_height = static_cast<float>((std::max)(
      1L, viewport.canvas.bottom - viewport.canvas.top));
  viewport.zoom = std::isfinite(requested_zoom)
      ? (std::max)(0.55f, (std::min)(3.0f, requested_zoom)) : 1.0f;

  // Raster dimensions are authoritative because individual floors have
  // different aspect ratios. World bounds remain a safe pre-cache fallback.
  float source_width = static_cast<float>(native_map.raster_width);
  float source_height = static_cast<float>(native_map.raster_height);
  if (source_width <= 0.0f || source_height <= 0.0f) {
    source_width = native_map.width;
    source_height = native_map.height;
  }
  viewport.fitted_width = viewport.canvas_width;
  viewport.fitted_height = viewport.canvas_height;
  if (std::isfinite(source_width) && std::isfinite(source_height) &&
      source_width > 0.0f && source_height > 0.0f) {
    const float fit = (std::min)(viewport.canvas_width / source_width,
                                 viewport.canvas_height / source_height);
    viewport.fitted_width = source_width * fit;
    viewport.fitted_height = source_height * fit;
  }

  // A short, letterboxed axis stays centered. Once zoom makes an axis larger
  // than the canvas, allow exactly the overflow on either side and no more.
  // This guarantees a gesture cannot expose additional blank space or move the
  // authored floor entirely away from the lower display.
  const float scaled_width = viewport.fitted_width * viewport.zoom;
  const float scaled_height = viewport.fitted_height * viewport.zoom;
  const float max_pan_x = (std::max)(
      0.0f, (scaled_width - viewport.canvas_width) * 0.5f);
  const float max_pan_y = (std::max)(
      0.0f, (scaled_height - viewport.canvas_height) * 0.5f);
  const float finite_pan_x = std::isfinite(requested_pan_x)
      ? requested_pan_x : 0.0f;
  const float finite_pan_y = std::isfinite(requested_pan_y)
      ? requested_pan_y : 0.0f;
  viewport.pan_x = (std::max)(-max_pan_x,
                              (std::min)(max_pan_x, finite_pan_x));
  viewport.pan_y = (std::max)(-max_pan_y,
                              (std::min)(max_pan_y, finite_pan_y));
  viewport.center_x =
      (viewport.canvas.left + viewport.canvas.right) * 0.5f + viewport.pan_x;
  viewport.center_y =
      (viewport.canvas.top + viewport.canvas.bottom) * 0.5f + viewport.pan_y;
  return viewport;
}

void DrawMap(HDC dc, const Fonts& fonts, int width, int height, int header,
             const SmartGlassSnapshot& sg, const NativeMapSnapshot& native_map,
             const CustomUiState& state,
             CustomUiFrame* frame) {
  const MapViewportGeometry viewport = MakeMapViewportGeometry(
      width, height, header, native_map, state.map_zoom, state.map_pan_x,
      state.map_pan_y);
  const int margin = 18;
  const int gap = 12;
  const RECT map = viewport.panel;
  RECT info{map.right + gap, header + margin, width - margin, height - margin};
  const bool live = native_map.valid;
  const int display_floor = live ? native_map.display_level : sg.map.level;
  Section(dc, fonts, map, L"//  CITY MAP",
          live && native_map.radar_live ? L"LIVE RADAR" : L"MAP");
  Section(dc, fonts, info, L"//  LOCATION",
          live ? L"SYNCHRONIZED" : L"ACQUIRING");
  const RECT canvas = viewport.canvas;
  Fill(dc, canvas, RGB(5, 10, 10));
  const float zoom = viewport.zoom;
  const float center_x = viewport.center_x;
  const float center_y = viewport.center_y;
  const float step = (std::max)(24.0f, 58.0f * zoom);
  const float fitted_w = viewport.fitted_width;
  const float fitted_h = viewport.fitted_height;

  // Pan and zoom are one transform for the authored floor and every overlay.
  // The floor texture is composited beneath this GDI layer when its one-shot
  // vector raster is available; this low-contrast coordinate field remains a
  // truthful loading/fallback surface rather than inventing building walls.
  const int saved_dc = SaveDC(dc);
  IntersectClipRect(dc, canvas.left, canvas.top, canvas.right, canvas.bottom);
  const float first_x = center_x +
      std::floor((canvas.left - center_x) / step) * step;
  for (float x = first_x; x < canvas.right; x += step) {
    Line(dc, static_cast<int>(std::lround(x)), canvas.top,
         static_cast<int>(std::lround(x)), canvas.bottom, RGB(16, 35, 33));
  }
  const float first_y = center_y +
      std::floor((canvas.top - center_y) / step) * step;
  for (float y = first_y; y < canvas.bottom; y += step) {
    Line(dc, canvas.left, static_cast<int>(std::lround(y)), canvas.right,
         static_cast<int>(std::lround(y)), RGB(16, 35, 33));
  }

  const int world_left = static_cast<int>(std::lround(
      center_x - fitted_w * 0.5f * zoom));
  const int world_right = static_cast<int>(std::lround(
      center_x + fitted_w * 0.5f * zoom));
  const int world_top = static_cast<int>(std::lround(
      center_y - fitted_h * 0.5f * zoom));
  const int world_bottom = static_cast<int>(std::lround(
      center_y + fitted_h * 0.5f * zoom));
  if (live) {
    // The exact floor raster is queued first in the native overlay batch. The
    // contacts below are queued afterward, so they stay visible over the map
    // without re-entering or opening the game's pause-menu map.
    const int raster_floor = native_map.raster_width && native_map.raster_height
        ? native_map.raster_floor : display_floor;
    AddMapRaster(frame, native_map.map_resource_id, raster_floor,
                 static_cast<float>(world_left),
                 static_cast<float>(world_top),
                 static_cast<float>(world_right),
                 static_cast<float>(world_bottom), canvas);
  }
  HPEN boundary = CreatePen(PS_SOLID, 2, RGB(38, 82, 73));
  HGDIOBJ old_boundary = SelectObject(dc, boundary);
  HGDIOBJ old_boundary_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, world_left, world_top, world_right, world_bottom);
  SelectObject(dc, old_boundary_brush);
  SelectObject(dc, old_boundary);
  DeleteObject(boundary);
  Line(dc, static_cast<int>(std::lround(center_x)), world_top,
       static_cast<int>(std::lround(center_x)), world_bottom, RGB(27, 57, 53));
  Line(dc, world_left, static_cast<int>(std::lround(center_y)), world_right,
       static_cast<int>(std::lround(center_y)), RGB(27, 57, 53));

  auto map_point = [&](double nx, double ny) -> POINT {
    nx = (std::max)(0.0, (std::min)(1.0, nx));
    ny = (std::max)(0.0, (std::min)(1.0, ny));
    POINT result;
    result.x = static_cast<LONG>(std::lround(
        center_x + (static_cast<float>(nx) - 0.5f) * fitted_w * zoom));
    result.y = static_cast<LONG>(std::lround(
        center_y + (static_cast<float>(ny) - 0.5f) * fitted_h * zoom));
    return result;
  };

  auto marker_color = [](NativeMapMarkerKind kind) -> COLORREF {
    switch (kind) {
      case NativeMapMarkerKind::kFriend: return RGB(69, 220, 123);
      case NativeMapMarkerKind::kHostile: return RGB(239, 67, 53);
      case NativeMapMarkerKind::kAlert: return RGB(255, 116, 36);
      case NativeMapMarkerKind::kCautious: return RGB(245, 184, 45);
      case NativeMapMarkerKind::kNeutral: return RGB(205, 211, 198);
      case NativeMapMarkerKind::kStatic:
      default: return kCyan;
    }
  };

  // Native static map markers and radar actors use the exact same authored
  // bounds as the vanilla map serializer. Actors on another known floor are
  // omitted, matching the normal map's floor filtering.
  if (live) {
    for (int i = 0; i < SafeMapMarkerCount(native_map); ++i) {
      const NativeMapMarker& marker = native_map.markers[i];
      if (marker.level != -1 && marker.level != display_floor) continue;
      if (!std::isfinite(marker.normalized_x) ||
          !std::isfinite(marker.normalized_y) ||
          marker.normalized_x < 0.0f || marker.normalized_x > 1.0f ||
          marker.normalized_y < 0.0f || marker.normalized_y > 1.0f) {
        continue;
      }
      const POINT point = map_point(marker.normalized_x, marker.normalized_y);
      const COLORREF color = marker_color(marker.kind);
      const int radius = marker.kind == NativeMapMarkerKind::kStatic ? 6 : 5;
      AddMarker(frame, point, radius, color, false, &canvas);
    }
  }


  // Mission positions come from the same live locator records used by the
  // retail SmartGlass map serializer. Tracked objectives get the large gold
  // diamond; other main/side mission locations remain readable but quieter.
  const int native_objective_count = SafeObjectiveMarkerCount(native_map);
  if (live) {
    for (int i = 0; i < native_objective_count; ++i) {
      const NativeObjectiveMarker& marker = native_map.objective_markers[i];
      if (marker.level != -1 && marker.level != display_floor) continue;
      if (!std::isfinite(marker.normalized_x) ||
          !std::isfinite(marker.normalized_y) ||
          marker.normalized_x < 0.0f || marker.normalized_x > 1.0f ||
          marker.normalized_y < 0.0f || marker.normalized_y > 1.0f) {
        continue;
      }
      const POINT point = map_point(marker.normalized_x, marker.normalized_y);
      const int pulse = marker.tracked
          ? static_cast<int>((state.animation_ms / 350u) % 2u) : 0;
      const int radius = marker.tracked ? 11 + pulse : (marker.primary ? 9 : 8);
      AddMarker(frame, point, radius,
                marker.primary || marker.tracked ? kGoldBright : kCyan,
                true, &canvas);
    }
  }

  // Mission pins from SmartGlass remain useful when that optional serializer
  // is live. Use them only as a fallback so the same native mission position
  // is not painted twice when both serializers are attached.
  for (size_t i = 0; native_objective_count == 0 &&
       i < sg.map.markers.size(); ++i) {
    const auto& marker = sg.map.markers[i];
    double nx = marker.x;
    double ny = marker.y;
    if (sg.map.width > 0 && std::fabs(nx) > 1.0) nx /= sg.map.width;
    if (sg.map.height > 0 && std::fabs(ny) > 1.0) ny /= sg.map.height;
    nx = (std::max)(0.0, (std::min)(1.0, nx));
    ny = (std::max)(0.0, (std::min)(1.0, ny));
    const POINT point = map_point(nx, ny);
    AddMarker(frame, point, 7, marker.main_mission ? kGoldBright : kCyan,
              false, &canvas);
  }

  if (live && native_map.player_valid && display_floor == native_map.level) {
    const POINT player = map_point(native_map.player_x, native_map.player_y);
    if (native_map.player_heading_valid) {
      AddPlayerArrow(frame, player, native_map.player_heading_degrees,
                     &canvas);
    } else {
      AddMarker(frame, player, 10, kGoldBright, true, &canvas);
    }
  }
  RestoreDC(dc, saved_dc);

  wchar_t map_readout[160] = {};
  const std::wstring map_name = live ? Wide(native_map.image_name) : L"";
  if (!map_name.empty()) {
    std::swprintf(map_readout, sizeof(map_readout) / sizeof(map_readout[0]),
                  L"LEVEL %d    ZOOM %d%%    %ls",
                  display_floor,
                  static_cast<int>(std::lround(zoom * 100.0f)),
                  map_name.c_str());
  } else {
    std::swprintf(map_readout, sizeof(map_readout) / sizeof(map_readout[0]),
                  L"LEVEL %d    ZOOM %d%%",
                  display_floor,
                  static_cast<int>(std::lround(zoom * 100.0f)));
  }
  AddHit(frame, canvas, CustomHitKind::kMap, 0);

  RECT location{info.left + 15, info.top + 54, info.right - 15, info.top + 126};
  Plate(dc, location, false, RGB(9, 13, 12));
  std::wstring where = live && native_map.location[0]
      ? Wide(native_map.location)
      : (sg.map.location.empty() ? L"LOCATION UNAVAILABLE" : sg.map.location);
  const std::wstring sublocation = live && native_map.sublocation[0]
      ? Wide(native_map.sublocation) : sg.map.sublocation;
  RECT where_rect = location;
  if (!sublocation.empty()) where_rect.bottom -= 25;
  const HFONT where_fonts[] = {fonts.title, fonts.body, fonts.small};
  FittedText(dc, where, where_rect, kGoldBright,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             where_fonts, sizeof(where_fonts) / sizeof(where_fonts[0]));
  if (!sublocation.empty()) {
    RECT sub{location.left + 8, location.bottom - 27, location.right - 8, location.bottom - 5};
    Text(dc, fonts.small, sublocation, sub, kMuted,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  int y = location.bottom + 12;
  RECT legend{info.left + 15, y, info.right - 15, info.bottom - 267};
  Plate(dc, legend, false, RGB(7, 10, 9));
  RECT lt{legend.left + 12, legend.top + 8,
          legend.left + (legend.right - legend.left) / 2, legend.top + 40};
  const HFONT legend_title_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                                      fonts.micro};
  FittedText(dc, L"TACTICAL CONTACTS", lt, kGold,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             legend_title_fonts,
             sizeof(legend_title_fonts) / sizeof(legend_title_fonts[0]));
  RECT map_state{lt.right, legend.top + 8, legend.right - 12, legend.top + 40};
  const HFONT map_state_fonts[] = {fonts.tiny, fonts.micro};
  FittedText(dc, map_readout, map_state, kMuted,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             map_state_fonts,
             sizeof(map_state_fonts) / sizeof(map_state_fonts[0]));
  y = legend.top + 45;
  int friends = 0;
  int hostiles = 0;
  int suspicious = 0;
  int locations = 0;
  if (live) {
    for (int i = 0; i < SafeMapMarkerCount(native_map); ++i) {
      const NativeMapMarker& marker = native_map.markers[i];
      if (marker.level != -1 && marker.level != display_floor) continue;
      switch (marker.kind) {
        case NativeMapMarkerKind::kFriend: ++friends; break;
        case NativeMapMarkerKind::kHostile:
        case NativeMapMarkerKind::kAlert: ++hostiles; break;
        case NativeMapMarkerKind::kCautious:
        case NativeMapMarkerKind::kNeutral: ++suspicious; break;
        case NativeMapMarkerKind::kStatic: ++locations; break;
      }
    }
  }
  int objectives = 0;
  if (live && native_objective_count > 0) {
    for (int i = 0; i < native_objective_count; ++i) {
      const NativeObjectiveMarker& marker = native_map.objective_markers[i];
      if (marker.level == -1 || marker.level == display_floor) ++objectives;
    }
  } else {
    objectives = static_cast<int>(sg.map.markers.size());
  }
  struct LegendRow { const wchar_t* label; int count; COLORREF color; };
  const LegendRow rows[] = {
      {L"HOSTILE / ALERT", hostiles, RGB(239, 67, 53)},
      {L"FRIENDLY", friends, RGB(69, 220, 123)},
      {L"NEUTRAL / CAUTIOUS", suspicious, RGB(245, 184, 45)},
      {L"DISCOVERED LOCATIONS", locations, kCyan},
      {L"MISSION OBJECTIVES", objectives, kGold},
  };
  const int available_legend_height =
      static_cast<int>(legend.bottom) - y - 4;
  const int legend_row_height = (std::max)(24, (std::min)(39,
      available_legend_height /
          static_cast<int>(sizeof(rows) / sizeof(rows[0]))));
  for (const LegendRow& row_data : rows) {
    RECT row{legend.left + 10, y, legend.right - 10,
             y + legend_row_height - 1};
    const int dot_size = (std::max)(9, (std::min)(13,
        legend_row_height - 12));
    RECT dot{row.left + 7, row.top + (legend_row_height - dot_size) / 2,
             row.left + 7 + dot_size,
             row.top + (legend_row_height - dot_size) / 2 + dot_size};
    HBRUSH dot_brush = CreateSolidBrush(row_data.color);
    HGDIOBJ old_dot = SelectObject(dc, dot_brush);
    Ellipse(dc, dot.left, dot.top, dot.right, dot.bottom);
    SelectObject(dc, old_dot);
    DeleteObject(dot_brush);
    RECT label{dot.right + 8, row.top, row.right - 35, row.bottom};
    Text(dc, fonts.tiny, row_data.label, label, kText,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    wchar_t count[16] = {};
    std::swprintf(count, 16, L"%d", row_data.count);
    RECT value_box{row.right - 34, row.top, row.right - 4, row.bottom};
    Text(dc, fonts.small, count, value_box, row_data.color,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    y += legend_row_height;
  }
  if (!live) {
    RECT waiting{legend.left + 15, y + 4, legend.right - 15, legend.bottom - 8};
    Text(dc, fonts.tiny,
         L"Waiting for the current map and HUD radar to finish attaching...",
         waiting, kMuted, DT_CENTER | DT_TOP | DT_WORDBREAK);
  }
  const int controls_left = info.left + 15;
  const int controls_right = info.right - 15;
  const int controls_gap = 9;
  const int half = (controls_right - controls_left - controls_gap) / 2;
  const int floor_arrow_width = 58;
  RECT floor_previous{controls_left, info.bottom - 250,
                      controls_left + floor_arrow_width, info.bottom - 188};
  RECT floor_next{controls_right - floor_arrow_width, info.bottom - 250,
                  controls_right, info.bottom - 188};
  RECT floor_label{floor_previous.right + controls_gap, info.bottom - 250,
                   floor_next.left - controls_gap, info.bottom - 188};
  RECT zoom_in{controls_left, info.bottom - 174,
               controls_left + half, info.bottom - 112};
  RECT zoom_out{zoom_in.right + controls_gap, info.bottom - 174,
                controls_right, info.bottom - 112};
  RECT center{controls_left, info.bottom - 101,
              controls_right, info.bottom - 49};
  Plate(dc, floor_previous); Plate(dc, floor_label); Plate(dc, floor_next);
  Plate(dc, zoom_in); Plate(dc, zoom_out); Plate(dc, center);
  const int floor_count = live ? (std::max)(1, (std::min)(
      native_map.floor_count, 65)) : 1;
  int floor_index = 0;
  if (live) {
    for (int index = 0; index < floor_count; ++index) {
      if (native_map.floors[index] == display_floor) {
        floor_index = index;
        break;
      }
    }
  }
  const COLORREF floor_control_color = floor_count > 1 ? kGoldBright : kMuted;
  Text(dc, fonts.title, L"<", floor_previous, floor_control_color,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, fonts.title, L">", floor_next, floor_control_color,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  wchar_t floor_text[48] = {};
  std::swprintf(floor_text, sizeof(floor_text) / sizeof(floor_text[0]),
                L"FLOOR %d  //  %d OF %d", display_floor,
                floor_index + 1, floor_count);
  const HFONT floor_fonts[] = {fonts.small, fonts.tiny, fonts.micro};
  FittedText(dc, floor_text, floor_label, kGold,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             floor_fonts, sizeof(floor_fonts) / sizeof(floor_fonts[0]));
  Text(dc, fonts.title, L"+  ZOOM", zoom_in, kGoldBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, fonts.title, L"-  ZOOM", zoom_out, kGoldBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  const wchar_t* center_label = state.map_follow_player
      ? L"FOLLOWING JENSEN" : L"RESUME AUTO-FOLLOW";
  const HFONT center_fonts[] = {fonts.body, fonts.small, fonts.tiny};
  FittedText(dc, center_label, center,
             state.map_follow_player ? kGoldBright : kText,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             center_fonts, sizeof(center_fonts) / sizeof(center_fonts[0]));
  RECT gesture_hint{controls_left, info.bottom - 43,
                    controls_right, info.bottom - 12};
  Text(dc, fonts.tiny, L"DRAG TO PAN   //   PINCH TO ZOOM", gesture_hint,
       kMuted, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  AddHit(frame, zoom_in, CustomHitKind::kMapZoomIn, 0);
  AddHit(frame, zoom_out, CustomHitKind::kMapZoomOut, 0);
  AddHit(frame, center, CustomHitKind::kMapCenter, 0);
  if (floor_count > 1) {
    AddHit(frame, floor_previous, CustomHitKind::kMapFloorPrevious, 0);
    AddHit(frame, floor_next, CustomHitKind::kMapFloorNext, 0);
  }
}

void DrawObjectives(HDC dc, const Fonts& fonts, int width, int height, int header,
                    const SmartGlassSnapshot& sg, const CustomUiState& state,
                    CustomUiFrame* frame) {
  const int margin = 18;
  const int gap = 12;
  RECT list{margin, header + margin, static_cast<int>(width * 0.37f), height - margin};
  RECT detail{list.right + gap, header + margin, width - margin, height - margin};
  const int selected_index = sg.objectives.empty()
      ? -1
      : (std::max)(0, (std::min)(state.selected_objective,
                                  static_cast<int>(sg.objectives.size()) - 1));
  const bool selected_tracked = selected_index >= 0 &&
                                sg.objectives[selected_index].tracked;
  wchar_t objective_badge[32] = {};
  std::swprintf(objective_badge, 32, L"%d ACTIVE",
                static_cast<int>(sg.objectives.size()));
  Section(dc, fonts, list, L"//  OBJECTIVES", objective_badge);
  Section(dc, fonts, detail, L"//  MISSION BRIEF",
          selected_index < 0 ? L"STANDBY" :
          (selected_tracked ? L"TRACKED" : L"UNTRACKED"));
  int y = list.top + 53;
  const int row_h = 82;
  const int pager_height = 35;
  const int row_bottom = list.bottom - pager_height - 18;
  const int visible = (std::max)(1, (row_bottom - y) / row_h);
  const int page_count = (std::max)(1,
      (static_cast<int>(sg.objectives.size()) + visible - 1) / visible);
  const int page = (std::max)(0, (std::min)(state.objective_page, page_count - 1));
  const int start = page * visible;
  const int finish = (std::min)(static_cast<int>(sg.objectives.size()), start + visible);
  for (int i = start; i < finish; ++i) {
    RECT row{list.left + 12, y, list.right - 12, y + row_h - 7};
    Plate(dc, row, i == state.selected_objective,
          i == state.selected_objective ? RGB(24, 21, 13) : kPanelRaised);
    RECT number{row.left + 8, row.top + 7, row.left + 38, row.bottom - 7};
    wchar_t number_text[12] = {};
    std::swprintf(number_text, 12, L"%02d", i + 1);
    Text(dc, fonts.small, number_text, number,
         i == state.selected_objective ? kGoldBright : kGoldDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Line(dc, number.right + 2, row.top + 8, number.right + 2,
         row.bottom - 8,
         i == state.selected_objective ? kGold : kRail, 2);
    // Mission names can be substantially longer in localized builds. Reserve
    // two lines and select down through the micro face instead of clipping the
    // end of the title outside a fixed single-line strip.
    RECT name{number.right + 12, row.top + 6, row.right - 10, row.top + 49};
    const HFONT row_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                               fonts.micro};
    FittedText(dc,
         sg.objectives[i].title.empty() ? L"CLASSIFIED OBJECTIVE" :
             sg.objectives[i].title,
         name, i == state.selected_objective ? kGoldBright : kText,
         DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX,
         row_fonts, sizeof(row_fonts) / sizeof(row_fonts[0]));
    RECT type{name.left, row.top + 50, row.right - 10, row.bottom - 5};
    std::wstring type_label = sg.objectives[i].primary
        ? L"PRIMARY" : L"SECONDARY";
    if (sg.objectives[i].tracked) type_label += L"  //  TRACKED";
    const COLORREF objective_color = sg.objectives[i].primary
        ? (sg.objectives[i].tracked ? kGoldBright : kGold)
        : kCyan;
    Text(dc, fonts.tiny, type_label, type, objective_color);
    AddHit(frame, row, CustomHitKind::kObjective, i, page_count);
    y += row_h;
  }
  RECT objective_pager{list.left + 12, list.bottom - pager_height - 10,
                       list.right - 12, list.bottom - 10};
  Pager(dc, fonts, objective_pager, page, page_count,
        CustomHitKind::kObjectivePrevious, CustomHitKind::kObjectiveNext,
        frame);
  if (sg.objectives.empty()) {
    RECT empty_list{list.left + 10, list.top + 50,
                    list.right - 10, objective_pager.top - 7};
    EmptyNotice(dc, fonts, empty_list, L"NO ACTIVE DATA",
                L"Objectives populate automatically from the live mission log.");
  }
  RECT paper{detail.left + 18, detail.top + 56, detail.right - 18, detail.bottom - 18};
  Plate(dc, paper, false, RGB(11, 13, 11));
  if (!sg.objectives.empty()) {
    const int index = selected_index;
    const auto& objective = sg.objectives[index];
    RECT title{paper.left + 28, paper.top + 20, paper.right - 28,
               paper.top + 100};
    const HFONT title_fonts[] = {fonts.hero, fonts.title, fonts.body,
                                 fonts.small, fonts.tiny, fonts.micro};
    FittedText(dc, objective.title, title, kGoldBright,
               DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS |
                   DT_NOPREFIX,
               title_fonts, sizeof(title_fonts) / sizeof(title_fonts[0]));
    Line(dc, paper.left + 25, paper.top + 108, paper.right - 25,
         paper.top + 108, kRail);
    RECT classification{paper.left + 30, paper.top + 110,
                        paper.right - 30, paper.top + 136};
    wchar_t classification_text[96] = {};
    std::swprintf(classification_text, 96, L"%ls MISSION  //  ID %d",
                  objective.primary ? L"PRIMARY" : L"SECONDARY",
                  objective.objective_id >= 0 ? objective.objective_id
                                              : objective.mission_id);
    Text(dc, fonts.tiny, classification_text, classification,
         objective.primary ? kMuted : kCyan,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    RECT tracking{paper.left + 30, paper.bottom - 82, paper.right - 30,
                  paper.bottom - 24};
    Plate(dc, tracking, objective.tracked, RGB(13, 14, 10));
    const HFONT action_fonts[] = {fonts.title, fonts.body, fonts.small,
                                  fonts.tiny, fonts.micro};
    FittedText(dc,
         objective.tracked ? L"STOP TRACKING" : L"TRACK OBJECTIVE", tracking,
         objective.tracked ? kGoldBright : kText,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS |
             DT_NOPREFIX,
         action_fonts, sizeof(action_fonts) / sizeof(action_fonts[0]));
    AddHit(frame, tracking, CustomHitKind::kObjectiveToggleTracking,
           objective.mission_id >= 0 ? objective.mission_id
                                     : objective.objective_id,
           index);
    RECT body{paper.left + 30, paper.top + 146, paper.right - 30,
              tracking.top - 16};
    const std::wstring& text = !objective.long_description.empty()
        ? objective.long_description : objective.short_description;
    const HFONT body_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                                fonts.micro};
    FittedText(dc, text, body, kText,
               DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS |
                   DT_NOPREFIX,
               body_fonts, sizeof(body_fonts) / sizeof(body_fonts[0]));
  } else {
    EmptyNotice(dc, fonts, paper, L"MISSION LOG READY",
                L"The top screen remains unpaused while this page updates.");
  }
}

const wchar_t* AugmentationTreeStateText(AugmentationTreeState state) {
  switch (state) {
    case AugmentationTreeState::kLocked: return L"LOCKED";
    case AugmentationTreeState::kAvailable: return L"AVAILABLE";
    case AugmentationTreeState::kPurchased: return L"PURCHASED";
    case AugmentationTreeState::kComplete: return L"FULLY UPGRADED";
    case AugmentationTreeState::kUnavailable: return L"UNAVAILABLE";
  }
  return L"UNAVAILABLE";
}

const wchar_t* AugmentationNodeStateText(AugmentationNodeState state) {
  switch (state) {
    case AugmentationNodeState::kLocked: return L"UNLOCK FAMILY FIRST";
    case AugmentationNodeState::kPrerequisiteBlocked:
      return L"PREREQUISITES NOT MET";
    case AugmentationNodeState::kAvailable: return L"AVAILABLE";
    case AugmentationNodeState::kPurchased: return L"PURCHASED";
    case AugmentationNodeState::kUnavailable: return L"UNAVAILABLE";
  }
  return L"UNAVAILABLE";
}

COLORREF AugmentationNodeColor(AugmentationNodeState state) {
  switch (state) {
    case AugmentationNodeState::kPurchased: return kGoldBright;
    case AugmentationNodeState::kAvailable: return kGold;
    case AugmentationNodeState::kPrerequisiteBlocked: return RGB(174, 124, 43);
    case AugmentationNodeState::kLocked: return kMuted;
    case AugmentationNodeState::kUnavailable: return RGB(91, 91, 87);
  }
  return kMuted;
}

const AugmentationNodeSnapshot* FindAugmentationNode(
    const AugmentationTreeSnapshot& tree, int native_node_index) {
  for (int index = 0; index < SafeAugmentationNodeCount(tree); ++index) {
    if (tree.nodes[index].node_index == native_node_index) {
      return &tree.nodes[index];
    }
  }
  return nullptr;
}

void DrawNativeAugs(HDC dc, const Fonts& fonts, int width, int height,
                    int header, const InventorySnapshot& inv,
                    const CustomUiState& state, CustomUiFrame* frame) {
  const int margin = 18;
  const float scale = (std::max)(0.75f, height / 1080.0f);
  RECT page{margin, header + margin, width - margin, height - margin};
  wchar_t praxis_badge[40] = L"PRAXIS  --";
  if (inv.praxis_points >= 0) {
    std::swprintf(praxis_badge, sizeof(praxis_badge) /
                                    sizeof(praxis_badge[0]),
                  L"PRAXIS  %d", inv.praxis_points);
  }
  Section(dc, fonts, page, L"//  AUGMENTATIONS", praxis_badge);

  const int content_top = page.top + static_cast<int>(47 * scale);
  const int content_gap = static_cast<int>(12 * scale);
  const int content_width = page.right - page.left - 30;
  const int tree_width = static_cast<int>(content_width * 0.58f);
  RECT trees{page.left + 15, content_top,
             page.left + 15 + tree_width, page.bottom - 15};
  RECT detail{trees.right + content_gap, content_top,
              page.right - 15, page.bottom - 15};
  Plate(dc, trees, false, RGB(5, 8, 8));
  Plate(dc, detail, false, RGB(8, 10, 9));

  const int augmentation_count = SafeAugmentationCount(inv);
  if (!inv.augmentation_catalog_valid || augmentation_count <= 0) {
    EmptyNotice(dc, fonts, trees, L"NATIVE AUGMENTATION DATA UNAVAILABLE",
                L"Waiting for the loaded save's GlobalPlayerInfo and Experience graph.");
    EmptyNotice(dc, fonts, detail, L"PRAXIS SYSTEM NOT ATTACHED",
                L"No upgrade state is guessed while the player is loading.");
    return;
  }

  const int columns = 4;
  const int rows = 6;
  const int per_page = columns * rows;
  const int page_count = (std::max)(
      1, (augmentation_count + per_page - 1) / per_page);
  const int page_index = (std::max)(0, (std::min)(state.augmentation_page,
                                                   page_count - 1));
  const int pager_height = page_count > 1 ? static_cast<int>(37 * scale) : 0;
  // The family buttons are the content; a redundant heading previously left
  // a dead strip above them. Start close to the panel edge so the complete
  // 4x6 selector reads as one compact native control surface.
  RECT grid_area{trees.left + 14, trees.top + static_cast<int>(10 * scale),
                 trees.right - 14,
                 trees.bottom - 12 - pager_height};
  const int gap = (std::max)(6, static_cast<int>(9 * scale));
  const int max_cell_w =
      (grid_area.right - grid_area.left - gap * (columns - 1)) / columns;
  const int max_cell_h =
      (grid_area.bottom - grid_area.top - gap * (rows - 1)) / rows;
  const int cell_size = (std::max)(48, (std::min)(max_cell_w, max_cell_h));
  const int grid_width = columns * cell_size + (columns - 1) * gap;
  const int grid_left = grid_area.left +
      (grid_area.right - grid_area.left - grid_width) / 2;
  // Keep the first row directly beneath the heading. The prior vertical
  // centering wasted a conspicuous strip above the augmentation families.
  const int grid_top = grid_area.top;

  for (int cell_index = 0; cell_index < per_page; ++cell_index) {
    const int tree_index = page_index * per_page + cell_index;
    const int column = cell_index % columns;
    const int row = cell_index / columns;
    RECT cell{grid_left + column * (cell_size + gap),
              grid_top + row * (cell_size + gap),
              grid_left + column * (cell_size + gap) + cell_size,
              grid_top + row * (cell_size + gap) + cell_size};
    if (tree_index >= augmentation_count) {
      Plate(dc, cell, false, RGB(4, 6, 6));
      continue;
    }
    const AugmentationTreeSnapshot& tree = inv.augmentations[tree_index];
    const int tree_node_count = SafeAugmentationNodeCount(tree);
    const bool selected = tree_index == state.selected_aug;
    const bool owned = tree.state == AugmentationTreeState::kPurchased ||
                       tree.state == AugmentationTreeState::kComplete;
    const int core_slot = CoreAbilitySlotForTree(tree.tree_id);
    const bool active = core_slot >= 0 && inv.augmentation_active_valid &&
                        inv.augmentation_active[core_slot];
    Plate(dc, cell, selected,
          owned ? RGB(20, 22, 15) : RGB(7, 9, 8));
    if (active) ActiveGoldOverlay(dc, cell);
    const int status_lane = (std::max)(16, static_cast<int>(20 * scale));
    const int icon_pad = (std::max)(7, cell_size / 10);
    RECT icon{cell.left + icon_pad, cell.top + icon_pad,
              cell.right - icon_pad, cell.bottom - status_lane - 3};
    AddGoldTexture(frame, tree.texture_id, icon,
                   owned ? 1.0f : 0.68f, owned ? 1.0f : 0.64f);
    if (!owned) {
      const int icon_extent = static_cast<int>((std::min)(
          icon.right - icon.left, icon.bottom - icon.top));
      const int lock_size = (std::max)(24, icon_extent * 3 / 5);
      RECT lock{(icon.left + icon.right - lock_size) / 2,
                (icon.top + icon.bottom - lock_size) / 2,
                (icon.left + icon.right + lock_size) / 2,
                (icon.top + icon.bottom + lock_size) / 2};
      AddLock(frame, lock);
    }
    wchar_t progress[24] = {};
    if (active) {
      std::swprintf(progress, 24, L"ACTIVE");
    } else if (tree.state == AugmentationTreeState::kComplete) {
      std::swprintf(progress, 24, L"MAX");
    } else if (tree.tree_present) {
      std::swprintf(progress, 24, L"%d / %d",
                    (std::max)(0, (std::min)(tree_node_count,
                                             tree.installed_nodes)),
                    tree_node_count);
    } else if (tree.base_praxis_cost >= 0) {
      std::swprintf(progress, 24, L"%d P", tree.base_praxis_cost);
    } else {
      std::swprintf(progress, 24, L"--");
    }
    RECT progress_rect{cell.left + 3, cell.bottom - status_lane,
                       cell.right - 3, cell.bottom - 2};
    Text(dc, fonts.micro, progress, progress_rect,
         owned ? kGoldBright : kMuted,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(frame, cell, CustomHitKind::kAugmentation, tree_index, page_count);
  }
  if (page_count > 1) {
    RECT pager{trees.left + 14, trees.bottom - pager_height - 5,
               trees.right - 14, trees.bottom - 7};
    Pager(dc, fonts, pager, page_index, page_count,
          CustomHitKind::kAugmentationPrevious,
          CustomHitKind::kAugmentationNext, frame);
  }

  const int selected_tree_index = (std::max)(0, (std::min)(
      state.selected_aug, augmentation_count - 1));
  const AugmentationTreeSnapshot& tree =
      inv.augmentations[selected_tree_index];
  const int node_count = SafeAugmentationNodeCount(tree);
  const int detail_inset = (std::max)(12, static_cast<int>(16 * scale));
  // Keep the selected-family identity compact so the authored upgrade tree,
  // branches and node states receive the majority of the detail column.
  const int family_height = (std::max)(76, static_cast<int>(88 * scale));
  RECT family{detail.left + detail_inset, detail.top + detail_inset,
              detail.right - detail_inset,
              detail.top + detail_inset + family_height};
  Plate(dc, family, state.selected_aug_node < 0, RGB(13, 15, 12));
  AddHit(frame, family, CustomHitKind::kAugmentationNode,
         selected_tree_index, -1);
  const int family_icon_size = (std::max)(46, family_height - 18);
  RECT family_icon{family.left + 10,
                   (family.top + family.bottom - family_icon_size) / 2,
                   family.left + 10 + family_icon_size,
                   (family.top + family.bottom + family_icon_size) / 2};
  AddGoldTexture(frame, tree.texture_id, family_icon, 1.0f,
                 tree.tree_present ? 1.0f : 0.68f);
  RECT family_name{family_icon.right + 12, family.top + 8,
                   family.right - 9, family.top + family_height * 2 / 3};
  const HFONT family_fonts[] = {fonts.title, fonts.body, fonts.small,
                                fonts.tiny};
  FittedText(dc, Wide(tree.name), family_name, kGoldBright,
             DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS,
             family_fonts, sizeof(family_fonts) / sizeof(family_fonts[0]));
  RECT family_state{family_icon.right + 12, family.top + family_height * 2 / 3,
                    family.right - 9, family.bottom - 5};
  std::wstring family_status = AugmentationTreeStateText(tree.state);
  if (tree.tree_id == 87234) {
    family_status += L"  //  INVENTORY ";
    family_status += std::to_wstring(InventoryCardCapacity(inv));
    family_status += L" / 18 SLOTS";
  }
  Text(dc, fonts.tiny, family_status, family_state,
       tree.tree_present ? kGold : kMuted,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  const int info_height = (std::max)(170, static_cast<int>(255 * scale));
  RECT graph{detail.left + detail_inset, family.bottom + content_gap,
             detail.right - detail_inset,
             detail.bottom - info_height - content_gap * 2};
  Plate(dc, graph, false, RGB(5, 8, 8));
  RECT graph_label{graph.left + 10, graph.top + 4,
                   graph.right - 10, graph.top + static_cast<int>(29 * scale)};
  Text(dc, fonts.tiny, L"NATIVE UPGRADE TREE", graph_label, kMuted,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  Line(dc, graph.left + 9, graph_label.bottom, graph.right - 9,
       graph_label.bottom, kRail);

  RECT node_rects[kMaximumAugmentationNodesPerTree] = {};
  int maximum_tier = 0;
  int tier_counts[kMaximumAugmentationNodesPerTree] = {};
  auto safe_tier = [node_count](const AugmentationNodeSnapshot& node) {
    return (std::max)(0, (std::min)(
        (std::max)(0, node_count - 1), node.tier));
  };
  for (int index = 0; index < node_count; ++index) {
    const int tier = safe_tier(tree.nodes[index]);
    maximum_tier = (std::max)(maximum_tier, tier);
    ++tier_counts[tier];
  }
  const int tier_total = (std::max)(1, maximum_tier + 1);
  int widest_tier = 1;
  for (int tier = 0; tier < tier_total; ++tier) {
    widest_tier = (std::max)(widest_tier, tier_counts[tier]);
  }
  RECT graph_content{graph.left + 10, graph_label.bottom + 8,
                     graph.right - 10, graph.bottom - 9};
  const int node_gap = (std::max)(5, static_cast<int>(8 * scale));
  const int maximum_by_width =
      (graph_content.right - graph_content.left -
       node_gap * (widest_tier - 1)) / widest_tier;
  const int maximum_by_height =
      (graph_content.bottom - graph_content.top -
       node_gap * (tier_total - 1)) / tier_total;
  const int node_size = (std::max)(34, (std::min)(
      static_cast<int>(74 * scale),
      (std::min)(maximum_by_width, maximum_by_height)));
  const int tier_step = tier_total > 1
      ? (graph_content.bottom - graph_content.top - node_size) /
            (tier_total - 1)
      : 0;
  for (int index = 0; index < node_count; ++index) {
    const AugmentationNodeSnapshot& node = tree.nodes[index];
    const int tier = safe_tier(node);
    const int count = (std::max)(1, tier_counts[tier]);
    const int order = (std::max)(0, (std::min)(count - 1,
                                               node.order_in_tier));
    const int row_width = count * node_size + (count - 1) * node_gap;
    const int left = graph_content.left +
        (graph_content.right - graph_content.left - row_width) / 2 +
        order * (node_size + node_gap);
    const int top = graph_content.top + tier * tier_step;
    node_rects[index] = RECT{left, top, left + node_size, top + node_size};
  }
  // Draw the authored dependency links before node plates so every branch is
  // recognizable without letting connector lines cross the icon artwork.
  for (int index = 0; index < node_count; ++index) {
    const AugmentationNodeSnapshot& child = tree.nodes[index];
    const RECT& child_rect = node_rects[index];
    for (int parent = 0; parent < SafePrerequisiteCount(child); ++parent) {
      const AugmentationNodeSnapshot* parent_node = FindAugmentationNode(
          tree, child.prerequisite_indices[parent]);
      if (!parent_node) continue;
      const int parent_array_index =
          static_cast<int>(parent_node - tree.nodes);
      if (parent_array_index < 0 || parent_array_index >= node_count) {
        continue;
      }
      const RECT& parent_rect = node_rects[parent_array_index];
      const bool selected_edge =
          state.selected_aug_node == child.node_index ||
          state.selected_aug_node == parent_node->node_index;
      Line(dc, (parent_rect.left + parent_rect.right) / 2,
           parent_rect.bottom,
           (child_rect.left + child_rect.right) / 2,
           child_rect.top,
           selected_edge ? kGold : kGoldDim, selected_edge ? 2 : 1);
    }
  }
  for (int index = 0; index < node_count; ++index) {
    const AugmentationNodeSnapshot& node = tree.nodes[index];
    const RECT& cell = node_rects[index];
    const bool selected = state.selected_aug_node == node.node_index;
    const bool purchased = node.state == AugmentationNodeState::kPurchased;
    Plate(dc, cell, selected,
          purchased ? RGB(22, 24, 16) : RGB(7, 9, 8));
    const int lane = (std::max)(10, node_size / 5);
    const int pad = (std::max)(4, node_size / 10);
    RECT icon{cell.left + pad, cell.top + pad,
              cell.right - pad, cell.bottom - lane - 2};
    const float alpha = purchased ||
                        node.state == AugmentationNodeState::kAvailable
        ? 1.0f : 0.52f;
    AddGoldTexture(frame, node.texture_id, icon, alpha,
                   purchased ? 1.0f : 0.72f);
    if (node.state == AugmentationNodeState::kLocked ||
        node.state == AugmentationNodeState::kPrerequisiteBlocked ||
        node.state == AugmentationNodeState::kUnavailable) {
      const int lock_size = (std::max)(18, node_size / 2);
      RECT lock{(icon.left + icon.right - lock_size) / 2,
                (icon.top + icon.bottom - lock_size) / 2,
                (icon.left + icon.right + lock_size) / 2,
                (icon.top + icon.bottom + lock_size) / 2};
      AddLock(frame, lock);
    }
    const wchar_t* short_state = purchased ? L"OWNED" :
        (node.state == AugmentationNodeState::kAvailable ? L"READY" :
         (node.state == AugmentationNodeState::kPrerequisiteBlocked
              ? L"REQ" : L"LOCK"));
    RECT state_lane{cell.left + 2, cell.bottom - lane,
                    cell.right - 2, cell.bottom - 1};
    Text(dc, fonts.micro, short_state, state_lane,
         AugmentationNodeColor(node.state),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(frame, cell, CustomHitKind::kAugmentationNode,
           selected_tree_index, node.node_index);
  }

  RECT info{detail.left + detail_inset, graph.bottom + content_gap,
            detail.right - detail_inset, detail.bottom - detail_inset};
  Plate(dc, info, false, RGB(10, 12, 10));
  const AugmentationNodeSnapshot* selected_node =
      FindAugmentationNode(tree, state.selected_aug_node);
  const std::wstring selected_name = selected_node
      ? Wide(selected_node->name) : Wide(tree.name);
  RECT name{info.left + 12, info.top + 7, info.right - 12,
            info.top + static_cast<int>(37 * scale)};
  const HFONT detail_name_fonts[] = {fonts.title, fonts.body, fonts.small,
                                     fonts.tiny};
  FittedText(dc, selected_name, name, kGoldBright,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             detail_name_fonts,
             sizeof(detail_name_fonts) / sizeof(detail_name_fonts[0]));
  const wchar_t* state_text = selected_node
      ? AugmentationNodeStateText(selected_node->state)
      : AugmentationTreeStateText(tree.state);
  RECT state_line{info.left + 12, name.bottom,
                  info.right - 12, name.bottom + static_cast<int>(24 * scale)};
  Text(dc, fonts.tiny, state_text, state_line,
       selected_node ? AugmentationNodeColor(selected_node->state) :
                       (tree.tree_present ? kGold : kMuted),
       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  Line(dc, info.left + 10, state_line.bottom + 2, info.right - 10,
       state_line.bottom + 2, kRail);

  const int purchase_height = (std::max)(38, static_cast<int>(48 * scale));
  RECT purchase{info.left + 10, info.bottom - purchase_height - 9,
                info.right - 10, info.bottom - 9};
  RECT description{info.left + 13, state_line.bottom + 10,
                   info.right - 13, purchase.top - 8};
  std::wstring detail_text;
  if (selected_node) {
    detail_text = Wide(selected_node->description);
    const std::wstring help = Wide(selected_node->help);
    if (!help.empty() && help != detail_text) {
      if (!detail_text.empty()) detail_text += L"\n\n";
      detail_text += help;
    }
    if (SafePrerequisiteCount(*selected_node) > 0 &&
        selected_node->state ==
            AugmentationNodeState::kPrerequisiteBlocked) {
      detail_text += L"\n\nREQUIRES: ";
      bool first = true;
      for (int parent = 0; parent < SafePrerequisiteCount(*selected_node);
           ++parent) {
        const AugmentationNodeSnapshot* prerequisite = FindAugmentationNode(
            tree, selected_node->prerequisite_indices[parent]);
        if (!prerequisite ||
            prerequisite->state == AugmentationNodeState::kPurchased) {
          continue;
        }
        if (!first) detail_text += L", ";
        detail_text += Wide(prerequisite->name);
        first = false;
      }
    }
  } else {
    detail_text = Wide(tree.description);
  }
  const HFONT body_fonts[] = {fonts.body, fonts.small, fonts.tiny,
                              fonts.micro};
  FittedText(dc, detail_text, description, kText,
             DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS |
                 DT_NOPREFIX,
             body_fonts, sizeof(body_fonts) / sizeof(body_fonts[0]));

  bool can_purchase = false;
  bool affordable = false;
  int purchase_node = -1;
  int cost = tree.base_praxis_cost;
  wchar_t purchase_text[96] = {};
  if (selected_node) {
    purchase_node = selected_node->node_index;
    cost = selected_node->praxis_cost;
    can_purchase = selected_node->state == AugmentationNodeState::kAvailable;
    affordable = can_purchase && selected_node->affordable;
    if (selected_node->state == AugmentationNodeState::kPurchased) {
      std::swprintf(purchase_text, 96, L"PURCHASED");
    } else if (can_purchase && affordable) {
      std::swprintf(purchase_text, 96, L"PURCHASE  //  %d PRAXIS", cost);
    } else if (can_purchase) {
      std::swprintf(purchase_text, 96, L"AVAILABLE  //  NEED %d PRAXIS", cost);
    } else {
      std::swprintf(purchase_text, 96, L"%ls",
                    AugmentationNodeStateText(selected_node->state));
    }
  } else {
    can_purchase = !tree.tree_present &&
                   tree.state == AugmentationTreeState::kAvailable;
    affordable = can_purchase && inv.praxis_points >= 0 &&
                 inv.praxis_points >= tree.base_praxis_cost;
    if (tree.state == AugmentationTreeState::kComplete) {
      std::swprintf(purchase_text, 96, L"FULLY UPGRADED");
    } else if (tree.tree_present) {
      std::swprintf(purchase_text, 96, L"FAMILY PURCHASED  //  %d / %d UPGRADES",
                    (std::max)(0, (std::min)(node_count,
                                             tree.installed_nodes)),
                    node_count);
    } else if (can_purchase && affordable) {
      std::swprintf(purchase_text, 96, L"UNLOCK FAMILY  //  %d PRAXIS", cost);
    } else if (can_purchase) {
      std::swprintf(purchase_text, 96,
                    L"AVAILABLE  //  NEED %d PRAXIS", cost);
    } else {
      std::swprintf(purchase_text, 96, L"%ls",
                    AugmentationTreeStateText(tree.state));
    }
  }
  Plate(dc, purchase, affordable, affordable ? RGB(27, 24, 13) :
                                               RGB(7, 9, 8));
  const HFONT purchase_fonts[] = {fonts.body, fonts.small, fonts.tiny};
  FittedText(dc, purchase_text, purchase,
             affordable ? kGoldBright : kMuted,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
             purchase_fonts,
             sizeof(purchase_fonts) / sizeof(purchase_fonts[0]));
  if (can_purchase && affordable) {
    AddHit(frame, purchase, CustomHitKind::kAugmentationPurchase,
           tree.tree_id, purchase_node);
  }
}

void DrawAugs(HDC dc, const Fonts& fonts, int width, int height, int header,
              const InventorySnapshot& inv, const SmartGlassSnapshot&,
              const CustomUiState& state, CustomUiFrame* frame) {
  // The native GlobalPlayerInfo/Experience model is the sole authority for
  // this page. Do not fall back to the old flattened SmartGlass cards: they do
  // not contain the DTP relation graph and cannot represent prerequisites.
  DrawNativeAugs(dc, fonts, width, height, header, inv, state, frame);
}

void DrawData(HDC dc, const Fonts& fonts, int width, int height, int header,
              const SmartGlassSnapshot& sg, const CustomUiState& state,
              CustomUiFrame* frame) {
  const int margin = 18;
  RECT list{margin, header + margin, static_cast<int>(width * 0.36f), height - margin};
  RECT detail{list.right + 12, header + margin, width - margin, height - margin};
  wchar_t data_badge[32] = {};
  std::swprintf(data_badge, 32, L"%d RECORDS",
                static_cast<int>(sg.media.size()));
  Section(dc, fonts, list, L"//  DATA", data_badge);
  Section(dc, fonts, detail, L"//  READER",
          sg.media.empty() ? L"STANDBY" : L"DECRYPTED");
  int y = list.top + 53;
  const int row_h = 68;
  const int pager_height = 35;
  const int row_bottom = list.bottom - pager_height - 18;
  const int visible = (std::max)(1, (row_bottom - y) / row_h);
  const int page_count = (std::max)(1,
      (static_cast<int>(sg.media.size()) + visible - 1) / visible);
  const int page = (std::max)(0, (std::min)(state.media_page, page_count - 1));
  const int start = page * visible;
  const int finish = (std::min)(static_cast<int>(sg.media.size()), start + visible);
  for (int i = start; i < finish; ++i) {
    RECT row{list.left + 12, y, list.right - 12, y + 61};
    Plate(dc, row, i == state.selected_media,
          i == state.selected_media ? RGB(24, 21, 13) : kPanelRaised);
    RECT index_box{row.left + 7, row.top + 6, row.left + 39, row.bottom - 6};
    wchar_t index_text[12] = {};
    std::swprintf(index_text, 12, L"%02d", i + 1);
    Text(dc, fonts.small, index_text, index_box,
         i == state.selected_media ? kGoldBright : kGoldDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Line(dc, index_box.right + 2, row.top + 8, index_box.right + 2,
         row.bottom - 8, i == state.selected_media ? kGold : kRail, 2);
    RECT title{index_box.right + 11, row.top + 5,
               row.right - 10, row.bottom - 19};
    const HFONT row_fonts[] = {fonts.small, fonts.tiny};
    FittedText(dc, sg.media[i].title.empty() ? L"UNTITLED RECORD" :
         sg.media[i].title, title,
         i == state.selected_media ? kGoldBright : kText,
         DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS,
         row_fonts, sizeof(row_fonts) / sizeof(row_fonts[0]));
    const wchar_t* kind = sg.media[i].media_type == 0 ? L"E-MAIL" :
                          (sg.media[i].media_type == 1
                               ? L"POCKET SECRETARY" : L"DISCOVERED DATA");
    RECT kind_rect{title.left, row.bottom - 22, row.right - 10,
                   row.bottom - 3};
    Text(dc, fonts.micro, kind, kind_rect, kMuted,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    AddHit(frame, row, CustomHitKind::kMedia, i, page_count);
    y += row_h;
  }
  RECT media_pager{list.left + 12, list.bottom - pager_height - 10,
                   list.right - 12, list.bottom - 10};
  Pager(dc, fonts, media_pager, page, page_count,
        CustomHitKind::kMediaPrevious, CustomHitKind::kMediaNext, frame);
  if (sg.media.empty()) {
    RECT empty_list{list.left + 10, list.top + 50,
                    list.right - 10, media_pager.top - 7};
    EmptyNotice(dc, fonts, empty_list, L"NO DISCOVERED DATA",
                L"E-mails and pocket secretaries appear here as Jensen finds them.");
  }
  RECT reader{detail.left + 18, detail.top + 56, detail.right - 18, detail.bottom - 18};
  Plate(dc, reader, false, RGB(10, 12, 10));
  if (!sg.media.empty()) {
    const int index = (std::max)(0, (std::min)(state.selected_media,
                                               static_cast<int>(sg.media.size()) - 1));
    const auto& media = sg.media[index];
    RECT title{reader.left + 25, reader.top + 20, reader.right - 25, reader.top + 78};
    const HFONT title_fonts[] = {fonts.hero, fonts.title, fonts.body,
                                 fonts.small};
    FittedText(dc, media.title, title, kGoldBright,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
               title_fonts, sizeof(title_fonts) / sizeof(title_fonts[0]));
    RECT meta{reader.left + 28, reader.top + 77, reader.right - 28, reader.top + 118};
    std::wstring metadata;
    const wchar_t* kind = media.media_type == 0 ? L"E-MAIL" :
                          (media.media_type == 1
                               ? L"POCKET SECRETARY" : L"DISCOVERED DATA");
    metadata = std::wstring(kind) + L"     ";
    if (!media.from.empty()) metadata += L"FROM: " + media.from;
    if (!media.to.empty()) metadata += (metadata.empty() ? L"" : L"     ") +
                                             std::wstring(L"TO: ") + media.to;
    const HFONT meta_fonts[] = {fonts.small, fonts.tiny};
    FittedText(dc, metadata, meta, kMuted,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS,
         meta_fonts, sizeof(meta_fonts) / sizeof(meta_fonts[0]));
    Line(dc, reader.left + 25, reader.top + 122, reader.right - 25, reader.top + 122, kRail);
    RECT content{reader.left + 30, reader.top + 145, reader.right - 30, reader.bottom - 30};
    const HFONT content_fonts[] = {fonts.body, fonts.small, fonts.tiny};
    FittedText(dc, media.content, content, kText,
         DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS,
         content_fonts, sizeof(content_fonts) / sizeof(content_fonts[0]));
  } else {
    EmptyNotice(dc, fonts, reader, L"DATA ARCHIVE READY",
                L"E-mails, pocket secretaries, and discovered media appear here.");
  }
}

}  // namespace

void ClampCustomMapView(int width, int height, int header,
                        const NativeMapSnapshot& native_map, float* zoom,
                        float* pan_x, float* pan_y) {
  if (!zoom || !pan_x || !pan_y || width <= 0 || height <= 0) return;
  const MapViewportGeometry viewport = MakeMapViewportGeometry(
      width, height, header, native_map, *zoom, *pan_x, *pan_y);
  *zoom = viewport.zoom;
  *pan_x = viewport.pan_x;
  *pan_y = viewport.pan_y;
}

void CenterCustomMapOnPlayer(int width, int height, int header,
                             const NativeMapSnapshot& native_map,
                             float* zoom, float* pan_x, float* pan_y) {
  if (!zoom || !pan_x || !pan_y || width <= 0 || height <= 0 ||
      !native_map.valid || !native_map.player_valid ||
      native_map.display_level != native_map.level ||
      !std::isfinite(native_map.player_x) ||
      !std::isfinite(native_map.player_y)) {
    return;
  }
  const MapViewportGeometry base = MakeMapViewportGeometry(
      width, height, header, native_map, *zoom, 0.0f, 0.0f);
  const float player_x = (std::max)(0.0f, (std::min)(
      1.0f, static_cast<float>(native_map.player_x)));
  const float player_y = (std::max)(0.0f, (std::min)(
      1.0f, static_cast<float>(native_map.player_y)));
  *pan_x = -(player_x - 0.5f) * base.fitted_width * base.zoom;
  *pan_y = -(player_y - 0.5f) * base.fitted_height * base.zoom;
  ClampCustomMapView(width, height, header, native_map, zoom, pan_x, pan_y);
}

void DrawCustomUi(HDC dc, int width, int height, int header, int page,
                  const InventorySnapshot& inventory,
                  const SmartGlassSnapshot& smartglass,
                  const NativeMapSnapshot& native_map,
                  const CustomUiState& state, CustomUiFrame* frame) {
  if (!dc || width <= 0 || height <= 0 || !frame) return;
  frame->icons.clear();
  frame->hits.clear();
  frame->hit_rects.clear();
  frame->inventory_identity = inventory.inventory_identity;
  frame->inventory_revision = inventory.revision;
  frame->inventory_grid = RECT{};
  frame->inventory_grid_width = 0;
  frame->inventory_grid_height = 0;
  Fonts fonts(height);
  RECT body{0, header, width, height};
  Fill(dc, body, kBackground);
  const int grid_step = (std::max)(38, width / 26);
  for (int x = grid_step; x < width; x += grid_step)
    Line(dc, x, header, x, height, RGB(8, 13, 12));
  for (int y = header + grid_step; y < height; y += grid_step)
    Line(dc, 0, y, width, y, RGB(8, 13, 12));

  switch (page) {
    case 1: DrawMap(dc, fonts, width, height, header, smartglass, native_map,
                    state, frame); break;
    case 2: DrawObjectives(dc, fonts, width, height, header, smartglass, state, frame); break;
    case 4: DrawAugs(dc, fonts, width, height, header, inventory, smartglass,
                     state, frame); break;
    case 5: DrawData(dc, fonts, width, height, header, smartglass, state, frame); break;
    case 3:
    default:
      DrawInventory(dc, fonts, width, height, header, inventory, smartglass,
                    state, frame);
      break;
  }

  // Panel draws the header after this body. Do not guess an archive-local
  // RenderResource domain ID here: 2526 is a DTP ID in this build, not a
  // verified Deus Ex logo texture.
}

CustomHit HitCustomUi(const CustomUiFrame& frame, int x, int y) {
  // Last drawn is visually on top and therefore owns an overlap.
  for (size_t i = frame.hit_rects.size(); i-- > 0;) {
    const RECT& r = frame.hit_rects[i];
    if (x >= r.left && x < r.right && y >= r.top && y < r.bottom)
      return frame.hits[i];
  }
  return CustomHit{};
}

}  // namespace dxs
