#include "Panel.h"

#include <d3d11.h>
#include <dxgi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "Config.h"
#include "GameActions.h"
#include "GameTextureArchive.h"
#include "Layout.h"
#include "Log.h"
#include "MapRasterCache.h"
#include "NativeJournal.h"
#include "NativeUiRenderer.h"
#include "SmartGlass.h"
#include "TouchInput.h"

namespace dxs {
namespace {

Panel g_panel;
const wchar_t* kClassName = L"DXHRSecondScreenPanel";
bool g_class_registered = false;
constexpr UINT kDestroyPanelWindowMessage = WM_APP + 0x4D1;

bool DispatchGuestTouch(const GuestTouch& touch, void* context) {
  auto* panel = static_cast<Panel*>(context);
  if (!panel) return false;
  TouchAction action = TouchAction::kTap;
  switch (touch.gesture) {
    case GuestGesture::kTap: action = TouchAction::kTap; break;
    case GuestGesture::kDown: action = TouchAction::kDown; break;
    case GuestGesture::kMove: action = TouchAction::kMove; break;
    case GuestGesture::kUp: action = TouchAction::kUp; break;
    case GuestGesture::kCancel: action = TouchAction::kCancel; break;
    case GuestGesture::kHold: action = TouchAction::kHold; break;
    case GuestGesture::kPinch: action = TouchAction::kPinch; break;
  }
  return panel->HandleNormalizedTouch(action, touch.x, touch.y, touch.value);
}

bool DispatchPageSelection(int page, void* context) {
  auto* panel = static_cast<Panel*>(context);
  return panel && panel->SelectTab(page);
}

struct TabDefinition {
  const wchar_t* label;
};

constexpr TabDefinition kTabs[] = {
    {L"HOME"},
    {L"MAP"},
    {L"OBJECTIVES"},
    {L"INVENTORY"},
    {L"AUGMENTATIONS"},
    {L"DATA"},
};

// Reference order across the navigation carousel. Only three pages are shown
// at once; the side arrows move through all five with wraparound and never
// inject a controller binding into the game.
constexpr int kVisualTabs[] = {3, 4, 1, 2, 5};
constexpr const wchar_t* kVisualLabels[] = {
    L"INVENTORY", L"AUGMENTATIONS", L"MAP", L"OBJECTIVES", L"DATA"};
constexpr int kNoPressedHeaderTarget = -4;

struct HeaderGeometry {
  int nav_top;
  int nav_bottom;
  int side_margin;
  int side_width;
  int tabs_left;
  int tabs_right;
  int gap;
  int tab_width;
};

HeaderGeometry MakeHeaderGeometry(int width, int header_height) {
  HeaderGeometry g = {};
  g.nav_top = (std::max)(7, header_height / 12);
  g.nav_bottom = header_height - (std::max)(7, header_height / 12);
  g.side_margin = (std::max)(12, width / 82);
  g.side_width = (std::max)(52, width / 18);
  g.gap = (std::max)(8, width / 124);
  g.tabs_left = g.side_margin + g.side_width + g.gap;
  g.tabs_right = width - g.side_margin - g.side_width - g.gap;
  g.tab_width = (g.tabs_right - g.tabs_left - g.gap * 2) / 3;
  return g;
}

int VisualSlotOf(int tab) {
  for (int i = 0; i < 5; ++i) if (kVisualTabs[i] == tab) return i;
  return -1;
}

int CycleVisualTab(int active, int direction) {
  int slot = VisualSlotOf(active);
  if (slot < 0) slot = direction > 0 ? -1 : 0;
  slot = (slot + direction + 5) % 5;
  return kVisualTabs[slot];
}

int VisibleTabStart(int active) {
  const int slot = (std::max)(0, VisualSlotOf(active));
  return (std::max)(0, (std::min)(2, slot - 1));
}

// Returns an internal kTabs index, -2 for previous and -3 for next.
int HeaderTargetAt(int x, int y, int width, int header_height, int active) {
  if (x < 0 || x >= width || y < 0 || y >= header_height) return -1;
  const HeaderGeometry g = MakeHeaderGeometry(width, header_height);
  if (y < g.nav_top || y > g.nav_bottom) return -1;
  if (x >= g.side_margin && x < g.side_margin + g.side_width) return -2;
  if (x >= width - g.side_margin - g.side_width &&
      x < width - g.side_margin) return -3;
  const int first = VisibleTabStart(active);
  for (int visible = 0; visible < 3; ++visible) {
    const int left = g.tabs_left + visible * (g.tab_width + g.gap);
    if (x >= left && x < left + g.tab_width) {
      return kVisualTabs[first + visible];
    }
  }
  return -1;
}

void DrawTabIcon(HDC dc, int index, const RECT& bounds, COLORREF color) {
  if (!dc) return;
  const int w = bounds.right - bounds.left;
  const int h = bounds.bottom - bounds.top;
  const int size = (std::max)(18, (std::min)(w, h));
  const int x = bounds.left + (w - size) / 2;
  const int y = bounds.top + (h - size) / 2;
  const int right = x + size;
  const int bottom = y + size;
  const int unit = (std::max)(2, size / 8);
  const int stroke = (std::max)(3, size / 10);

  // These deliberately follow the five visual nouns used by DXHR's native
  // pause menu: firearm, neural network, globe, document stack, and framed
  // information glyph. A single heavy monochrome weight keeps them readable
  // at arm's length on the Thor without looking like controller button art.
  HPEN pen = CreatePen(PS_SOLID, stroke, color);
  HBRUSH brush = CreateSolidBrush(color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));

  switch (index) {
    case 1: {  // MAP / native globe motif.
      const int inset = stroke / 2 + 1;
      Ellipse(dc, x + inset, y + inset, right - inset, bottom - inset);
      Arc(dc, x + size / 4, y + inset, right - size / 4, bottom - inset,
          x + size / 2, y + inset, x + size / 2, bottom - inset);
      Arc(dc, x + size / 4, y + inset, right - size / 4, bottom - inset,
          x + size / 2, bottom - inset, x + size / 2, y + inset);
      MoveToEx(dc, x + inset, y + size / 2, nullptr);
      LineTo(dc, right - inset, y + size / 2);
      MoveToEx(dc, x + unit, y + size / 3, nullptr);
      LineTo(dc, right - unit, y + size / 3);
      MoveToEx(dc, x + unit, y + size * 2 / 3, nullptr);
      LineTo(dc, right - unit, y + size * 2 / 3);
      break;
    }
    case 2: {  // OBJECTIVES / stacked mission documents.
      const int fold = size / 5;
      POINT back[] = {{x + unit, y + 1}, {right - fold, y + 1},
                      {right - 1, y + fold}, {right - 1, bottom - unit},
                      {x + unit, bottom - unit}};
      Polyline(dc, back, static_cast<int>(sizeof(back) / sizeof(back[0])));
      POINT front[] = {{x + 1, y + unit}, {right - fold - unit, y + unit},
                       {right - unit, y + fold + unit},
                       {right - unit, bottom - 1}, {x + 1, bottom - 1},
                       {x + 1, y + unit}};
      Polyline(dc, front, static_cast<int>(sizeof(front) / sizeof(front[0])));
      for (int row = 0; row < 3; ++row) {
        const int row_y = y + unit * 2 + row * unit * 2;
        SelectObject(dc, brush);
        RECT bullet{x + unit, row_y - stroke / 2,
                    x + unit + stroke, row_y + stroke / 2 + 1};
        FillRect(dc, &bullet, brush);
        SelectObject(dc, GetStockObject(NULL_BRUSH));
        MoveToEx(dc, x + unit * 2, row_y, nullptr);
        LineTo(dc, right - unit * 2, row_y);
      }
      break;
    }
    case 3: {  // INVENTORY / angular 10mm pistol silhouette.
      POINT pistol[] = {
          {x + 1, y + unit}, {right - unit, y + unit},
          {right - 1, y + unit * 2}, {right - 1, y + size / 2},
          {x + size / 2, y + size / 2},
          {x + size * 3 / 4, bottom - unit},
          {x + size * 2 / 3, bottom - 1},
          {x + size / 2, bottom - 1},
          {x + size / 3, y + size / 2 + unit},
          {x + 1, y + size / 2},
      };
      SelectObject(dc, brush);
      Polygon(dc, pistol, static_cast<int>(sizeof(pistol) / sizeof(pistol[0])));
      SelectObject(dc, GetStockObject(NULL_BRUSH));
      // Negative slide rail, painted with the same near-black as an inactive
      // button; the heavy outer silhouette remains readable when selected.
      HPEN detail = CreatePen(PS_SOLID, (std::max)(1, stroke / 2), RGB(15, 14, 10));
      HGDIOBJ prior = SelectObject(dc, detail);
      MoveToEx(dc, x + unit, y + unit * 2, nullptr);
      LineTo(dc, right - unit, y + unit * 2);
      SelectObject(dc, prior);
      DeleteObject(detail);
      break;
    }
    case 4: {  // AUGMENTATIONS / neural tree from the native tab glyph.
      const int hub_x = x + size / 2;
      const int hub_y = y + size / 2;
      MoveToEx(dc, hub_x, y + unit, nullptr);
      LineTo(dc, hub_x, bottom - unit);
      MoveToEx(dc, x + unit, y + unit * 2, nullptr);
      LineTo(dc, hub_x, hub_y);
      LineTo(dc, right - unit, y + unit * 2);
      MoveToEx(dc, x + unit, bottom - unit * 2, nullptr);
      LineTo(dc, hub_x, hub_y);
      LineTo(dc, right - unit, bottom - unit * 2);
      const POINT nodes[] = {{hub_x, y + unit},
                             {x + unit, y + unit * 2},
                             {right - unit, y + unit * 2},
                             {hub_x, hub_y},
                             {x + unit, bottom - unit * 2},
                             {right - unit, bottom - unit * 2},
                             {hub_x, bottom - unit}};
      SelectObject(dc, brush);
      const int node = (std::max)(3, stroke);
      for (const POINT& point : nodes) {
        Ellipse(dc, point.x - node, point.y - node,
                point.x + node + 1, point.y + node + 1);
      }
      SelectObject(dc, GetStockObject(NULL_BRUSH));
      break;
    }
    default: {  // DATA / clipped information frame.
      const int clip = unit;
      POINT frame[] = {{x + clip, y + 1}, {right - clip, y + 1},
                       {right - 1, y + clip}, {right - 1, bottom - clip},
                       {right - clip, bottom - 1}, {x + clip, bottom - 1},
                       {x + 1, bottom - clip}, {x + 1, y + clip},
                       {x + clip, y + 1}};
      Polyline(dc, frame, static_cast<int>(sizeof(frame) / sizeof(frame[0])));
      SelectObject(dc, brush);
      RECT top_bar{x + size / 3, y + unit * 2,
                   right - size / 3, y + unit * 2 + stroke};
      RECT stem{x + size / 2 - stroke / 2, y + unit * 2,
                x + size / 2 + (stroke + 1) / 2, bottom - unit * 2};
      RECT bottom_bar{x + size / 3, bottom - unit * 2 - stroke,
                      right - size / 3, bottom - unit * 2};
      FillRect(dc, &top_bar, brush);
      FillRect(dc, &stem, brush);
      FillRect(dc, &bottom_bar, brush);
      SelectObject(dc, GetStockObject(NULL_BRUSH));
      break;
    }
  }

  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(brush);
  DeleteObject(pen);
}

enum class HudPanelKind {
  kGeneric,
  kVitals,
  kSensor,
  kWeapon,
  kObjectives,
  kActivity,
  kQuickAccess,
};

struct HudPanelStyle {
  COLORREF accent;
  COLORREF secondary;
  const wchar_t* system_label;
};

HudPanelStyle StyleFor(HudPanelKind kind) {
  switch (kind) {
    case HudPanelKind::kVitals:
      return {RGB(207, 77, 45), RGB(229, 168, 45), L"BIO"};
    case HudPanelKind::kSensor:
      return {RGB(221, 157, 37), RGB(110, 94, 50), L"NAV"};
    case HudPanelKind::kWeapon:
      return {RGB(232, 176, 55), RGB(135, 105, 48), L"LOADOUT"};
    case HudPanelKind::kObjectives:
      return {RGB(232, 172, 49), RGB(133, 101, 46), L"TRACKED"};
    case HudPanelKind::kActivity:
      return {RGB(178, 133, 44), RGB(89, 76, 45), L"FEED"};
    case HudPanelKind::kQuickAccess:
      return {RGB(213, 156, 42), RGB(105, 87, 47), L"1 - 4"};
    default:
      return {RGB(213, 156, 42), RGB(105, 87, 47), L"SYSTEM"};
  }
}

void DrawHudPanelGlyph(HDC dc, HudPanelKind kind, const RECT& bounds,
                       COLORREF color) {
  if (!dc) return;
  const int width = bounds.right - bounds.left;
  const int height = bounds.bottom - bounds.top;
  if (width < 8 || height < 8) return;

  const int left = bounds.left;
  const int top = bounds.top;
  const int right = bounds.right;
  const int bottom = bounds.bottom;
  const int mid_x = (left + right) / 2;
  const int mid_y = (top + bottom) / 2;
  const int step = (std::max)(2, (std::min)(width, height) / 5);

  HPEN pen = CreatePen(PS_SOLID, 2, color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  switch (kind) {
    case HudPanelKind::kVitals:
      // A compact cardiogram: recognizable without implying a current value.
      MoveToEx(dc, left, mid_y, nullptr);
      LineTo(dc, left + step, mid_y);
      LineTo(dc, left + step * 2, top + step);
      LineTo(dc, left + step * 3, bottom - step);
      LineTo(dc, left + step * 4, mid_y);
      LineTo(dc, right, mid_y);
      break;
    case HudPanelKind::kSensor:
      Ellipse(dc, left + 1, top + 1, right - 1, bottom - 1);
      MoveToEx(dc, mid_x, top, nullptr); LineTo(dc, mid_x, bottom);
      MoveToEx(dc, left, mid_y, nullptr); LineTo(dc, right, mid_y);
      MoveToEx(dc, mid_x, mid_y, nullptr); LineTo(dc, right - step, top + step);
      break;
    case HudPanelKind::kWeapon:
      // Abstract magazine/cartridge silhouette rather than a fake weapon icon.
      MoveToEx(dc, left + step, top, nullptr); LineTo(dc, right - step, top);
      LineTo(dc, right, top + step); LineTo(dc, right - step, bottom);
      LineTo(dc, left + step, bottom); LineTo(dc, left, bottom - step);
      LineTo(dc, left + step, top);
      MoveToEx(dc, left + step, mid_y, nullptr); LineTo(dc, right - step, mid_y);
      break;
    case HudPanelKind::kObjectives: {
      POINT diamond[] = {{mid_x, top}, {right, mid_y},
                         {mid_x, bottom}, {left, mid_y}};
      Polygon(dc, diamond, 4);
      Ellipse(dc, mid_x - 2, mid_y - 2, mid_x + 3, mid_y + 3);
      break;
    }
    case HudPanelKind::kActivity:
      for (int row = 0; row < 3; ++row) {
        const int y = top + 3 + row * ((height - 5) / 3);
        Rectangle(dc, left, y, left + 3, y + 3);
        MoveToEx(dc, left + step, y + 1, nullptr);
        LineTo(dc, right - (row == 1 ? step : 0), y + 1);
      }
      break;
    case HudPanelKind::kQuickAccess:
      for (int row = 0; row < 2; ++row) {
        for (int column = 0; column < 2; ++column) {
          const int x = left + column * (width / 2);
          const int y = top + row * (height / 2);
          Rectangle(dc, x, y, x + width / 2 - 2, y + height / 2 - 2);
        }
      }
      break;
    default:
      MoveToEx(dc, left, mid_y, nullptr); LineTo(dc, right, mid_y);
      MoveToEx(dc, mid_x, top, nullptr); LineTo(dc, mid_x, bottom);
      break;
  }
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

void DrawPanelFrame(HDC dc, const Rect& area, const wchar_t* label,
                     HudPanelKind kind = HudPanelKind::kGeneric) {
  if (!dc || area.w < 20.0f || area.h < 20.0f) return;
  const HudPanelStyle style = StyleFor(kind);
  const int left = static_cast<int>(area.x) + 5;
  const int top = static_cast<int>(area.y) + 5;
  const int right = static_cast<int>(area.x + area.w) - 5;
  const int bottom = static_cast<int>(area.y + area.h) - 5;
  // A near-black shadow separates overlapping native HUD alpha without adding
  // a bright frame that competes with the game data.
  HBRUSH shadow = CreateSolidBrush(RGB(2, 3, 3));
  RECT shadow_rect{left + 3, top + 3, right + 3, bottom + 3};
  FillRect(dc, &shadow_rect, shadow);
  DeleteObject(shadow);

  HBRUSH fill = CreateSolidBrush(RGB(9, 12, 11));
  HPEN edge = CreatePen(PS_SOLID, 1, RGB(112, 84, 35));
  HGDIOBJ old_fill = SelectObject(dc, fill);
  HGDIOBJ old_pen = SelectObject(dc, edge);
  Rectangle(dc, left, top, right, bottom);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_fill);
  DeleteObject(edge);
  DeleteObject(fill);

  // The live game data is restored from x+10/y+38 to right-10/bottom-10.
  // Keep all persistent decoration in that surrounding chrome so nothing here
  // can mask a health value, radar contact, ammo count or objective update.
  const int title_bottom = (std::min)(bottom - 10, static_cast<int>(area.y) + 37);
  RECT title_plate{left + 2, top + 2, right - 2, title_bottom};
  HBRUSH title_fill = CreateSolidBrush(RGB(20, 21, 17));
  FillRect(dc, &title_plate, title_fill);
  DeleteObject(title_fill);

  // Fine inner rails and interrupted lower edges echo the game's machinery
  // without covering the live interior.
  HPEN detail = CreatePen(PS_SOLID, 1, RGB(55, 48, 29));
  old_pen = SelectObject(dc, detail);
  MoveToEx(dc, left + 4, top + 3, nullptr);
  LineTo(dc, right - 4, top + 3);
  MoveToEx(dc, left + 3, top + 4, nullptr);
  LineTo(dc, left + 3, bottom - 4);
  MoveToEx(dc, right - 3, top + 4, nullptr);
  LineTo(dc, right - 3, bottom - 4);
  MoveToEx(dc, left + 4, bottom - 3, nullptr);
  LineTo(dc, left + area.w * 0.42f, bottom - 3);
  MoveToEx(dc, left + area.w * 0.58f, bottom - 3, nullptr);
  LineTo(dc, right - 4, bottom - 3);
  SelectObject(dc, old_pen);
  DeleteObject(detail);

  // A two-tone identity rail gives each system a glanceable home. Vitals use
  // red plus energy amber; the remaining systems use muted amber companions.
  const int rail_left = left + 4;
  const int rail_right = (std::min)(right - 4,
                                     rail_left + (std::max)(64, (right - left) / 3));
  const int rail_mid = rail_left + (rail_right - rail_left) * 2 / 3;
  RECT primary_rail{rail_left, title_bottom - 3, rail_mid, title_bottom - 1};
  RECT secondary_rail{rail_mid + 2, title_bottom - 3, rail_right, title_bottom - 1};
  HBRUSH primary = CreateSolidBrush(style.accent);
  HBRUSH secondary = CreateSolidBrush(style.secondary);
  FillRect(dc, &primary_rail, primary);
  FillRect(dc, &secondary_rail, secondary);
  DeleteObject(primary);
  DeleteObject(secondary);

  // Small corner fasteners and edge notches add depth while remaining inside
  // the ten-pixel chrome border reserved by ComposeHud.
  HBRUSH fastener = CreateSolidBrush(RGB(116, 91, 43));
  RECT top_fastener{left + 4, top + 3, left + 7, top + 6};
  RECT bottom_fastener{right - 7, bottom - 6, right - 4, bottom - 3};
  FillRect(dc, &top_fastener, fastener);
  FillRect(dc, &bottom_fastener, fastener);
  DeleteObject(fastener);

  if (label && *label) {
    const int glyph_size = (std::max)(15, (std::min)(21, title_bottom - top - 8));
    RECT glyph{left + 13, top + 6, left + 13 + glyph_size, top + 6 + glyph_size};
    DrawHudPanelGlyph(dc, kind, glyph, style.accent);

    const int badge_width = (std::max)(42, (std::min)(78, (right - left) / 5));
    RECT title{glyph.right + 10, top + 3, right - badge_width - 10, title_bottom - 2};
    const int title_height = (std::max)(14, (std::min)(19,
        static_cast<int>((title_bottom - top) * 0.62f)));
    HFONT title_font = CreateFontW(-title_height, 0, 0, 0, FW_SEMIBOLD,
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, FF_DONTCARE,
                                   L"Segoe UI Semibold");
    HGDIOBJ previous_font = title_font ? SelectObject(dc, title_font) : nullptr;
    SetTextColor(dc, RGB(205, 183, 132));
    DrawTextW(dc, label, -1, &title,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (previous_font) SelectObject(dc, previous_font);
    if (title_font) DeleteObject(title_font);

    RECT badge{right - badge_width - 4, top + 4, right - 8, title_bottom - 3};
    HBRUSH badge_fill = CreateSolidBrush(RGB(28, 27, 20));
    HPEN badge_edge = CreatePen(PS_SOLID, 1, RGB(70, 59, 34));
    old_fill = SelectObject(dc, badge_fill);
    old_pen = SelectObject(dc, badge_edge);
    Rectangle(dc, badge.left, badge.top, badge.right, badge.bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_fill);
    DeleteObject(badge_edge);
    DeleteObject(badge_fill);

    const int badge_height = (std::max)(10, (std::min)(13, title_bottom - top - 12));
    HFONT badge_font = CreateFontW(-badge_height, 0, 0, 0, FW_SEMIBOLD,
                                   FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, FF_DONTCARE,
                                   L"Segoe UI Semibold");
    previous_font = badge_font ? SelectObject(dc, badge_font) : nullptr;
    SetTextColor(dc, style.accent);
    DrawTextW(dc, style.system_label, -1, &badge,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (previous_font) SelectObject(dc, previous_font);
    if (badge_font) DeleteObject(badge_font);
  }
}

void DrawReferenceSlot(HDC dc, const RECT& bounds) {
  if (!dc || bounds.right - bounds.left < 8 || bounds.bottom - bounds.top < 8) return;
  HBRUSH fill = CreateSolidBrush(RGB(8, 10, 9));
  HPEN edge = CreatePen(PS_SOLID, 1, RGB(91, 66, 25));
  HGDIOBJ old_fill = SelectObject(dc, fill);
  HGDIOBJ old_pen = SelectObject(dc, edge);
  Rectangle(dc, bounds.left, bounds.top, bounds.right, bounds.bottom);
  SelectObject(dc, old_pen);
  SelectObject(dc, old_fill);
  DeleteObject(edge);
  DeleteObject(fill);
}

struct InventoryReferenceRects {
  Rect weapons;
  Rect inventory;
  Rect status;
  Rect quick;
};

InventoryReferenceRects MakeInventoryReferenceRects(int width, int height,
                                                     int header) {
  const float w = static_cast<float>(width);
  const float content_top = static_cast<float>(header) + 12.0f;
  const float content_bottom = static_cast<float>(height) - 14.0f;
  const float quick_h = (std::max)(142.0f, height * 0.155f);
  const float body_bottom = content_bottom - quick_h - 12.0f;
  return {
      {0.018f * w, content_top, 0.286f * w, body_bottom - content_top},
      {0.312f * w, content_top, 0.378f * w, body_bottom - content_top},
      {0.698f * w, content_top, 0.284f * w, body_bottom - content_top},
      {0.018f * w, body_bottom + 12.0f, 0.964f * w,
       content_bottom - body_bottom - 12.0f},
  };
}

[[maybe_unused]] void DrawInventoryReference(HDC dc, int width, int height,
                                             int header) {
  const InventoryReferenceRects r =
      MakeInventoryReferenceRects(width, height, header);
  const Rect& weapons = r.weapons;
  const Rect& inventory = r.inventory;
  const Rect& status = r.status;
  const Rect& quick = r.quick;

  DrawPanelFrame(dc, weapons, L"//  WEAPONS", HudPanelKind::kWeapon);
  DrawPanelFrame(dc, inventory, L"//  INVENTORY", HudPanelKind::kGeneric);
  DrawPanelFrame(dc, status, L"//  STATUS", HudPanelKind::kVitals);
  DrawPanelFrame(dc, quick, L"//  QUICK SLOTS", HudPanelKind::kQuickAccess);

  // The slot furniture is deliberately data-free. The game's real Scaleform
  // page draws names, icons, quantities and selection states over this shell.
  const int title = 45;
  const int inset = 15;
  const int gap = 10;
  const int weapon_left = static_cast<int>(weapons.x) + inset;
  const int weapon_right = static_cast<int>(weapons.x + weapons.w) - inset;
  const int weapon_top = static_cast<int>(weapons.y) + title + 10;
  const int weapon_bottom = static_cast<int>(weapons.y + weapons.h) - inset;
  const int weapon_h = (weapon_bottom - weapon_top - gap * 4) / 5;
  for (int row = 0; row < 5; ++row) {
    RECT slot{weapon_left, weapon_top + row * (weapon_h + gap), weapon_right,
              weapon_top + row * (weapon_h + gap) + weapon_h};
    DrawReferenceSlot(dc, slot);
  }

  const int grid_left = static_cast<int>(inventory.x) + inset;
  const int grid_right = static_cast<int>(inventory.x + inventory.w) - inset;
  const int grid_top = static_cast<int>(inventory.y) + title + 10;
  const int grid_bottom = static_cast<int>(inventory.y + inventory.h) - inset;
  const int cell_w = (grid_right - grid_left - gap * 2) / 3;
  const int cell_h = (grid_bottom - grid_top - gap * 3) / 4;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 3; ++column) {
      RECT slot{grid_left + column * (cell_w + gap),
                grid_top + row * (cell_h + gap),
                grid_left + column * (cell_w + gap) + cell_w,
                grid_top + row * (cell_h + gap) + cell_h};
      DrawReferenceSlot(dc, slot);
    }
  }

  const int status_left = static_cast<int>(status.x) + inset;
  const int status_right = static_cast<int>(status.x + status.w) - inset;
  const int status_top = static_cast<int>(status.y) + title + 10;
  const int status_bottom = static_cast<int>(status.y + status.h) - inset;
  const int status_h = (status_bottom - status_top - gap * 2) / 3;
  for (int row = 0; row < 3; ++row) {
    RECT slot{status_left, status_top + row * (status_h + gap), status_right,
              status_top + row * (status_h + gap) + status_h};
    DrawReferenceSlot(dc, slot);
  }

  const int quick_left = static_cast<int>(quick.x) + 18;
  const int quick_right = static_cast<int>(quick.x + quick.w) - 18;
  const int quick_top = static_cast<int>(quick.y) + title;
  const int quick_bottom = static_cast<int>(quick.y + quick.h) - 12;
  const int quick_gap = 8;
  const int quick_w = (quick_right - quick_left - quick_gap * 9) / 10;
  for (int column = 0; column < 10; ++column) {
    RECT slot{quick_left + column * (quick_w + quick_gap), quick_top,
              quick_left + column * (quick_w + quick_gap) + quick_w, quick_bottom};
    DrawReferenceSlot(dc, slot);
  }
}

LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CLOSE:
      // The panel is not a window the player owns; closing it would leave the
      // interface with nowhere to go while the session carried on.
      return 0;
    case kDestroyPanelWindowMessage:
      // This private message is issued only by Panel when DXVK changes the
      // primary Present worker. The window procedure runs on the creator
      // thread, satisfying Win32's DestroyWindow ownership requirement.
      DestroyWindow(h);
      return 0;
    case WM_ERASEBKGND:
      return 1;  // the swapchain owns every pixel
    case WM_MOUSEACTIVATE:
      // Touching the panel must never take focus off the game, or the keyboard
      // and controller stop reaching the player character.
      return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
      if (ThePanel().HandlePointer(m, GET_X_LPARAM(l), GET_Y_LPARAM(l))) return 0;
      break;
    default:
      return DefWindowProcW(h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

struct MonitorScan { RECT primary; RECT second; bool found; int count; };

BOOL CALLBACK ScanProc(HMONITOR mon, HDC, LPRECT, LPARAM param) {
  auto* c = reinterpret_cast<MonitorScan*>(param);
  MONITORINFO mi;
  ZeroMemory(&mi, sizeof(mi));
  mi.cbSize = sizeof(mi);
  if (!GetMonitorInfoW(mon, &mi)) return TRUE;
  c->count++;
  const bool primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
  DXS_INFO("[panel] monitor %d: %ldx%ld at (%ld,%ld)%s", c->count,
           mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
           mi.rcMonitor.left, mi.rcMonitor.top, primary ? "  PRIMARY" : "");
  if (primary) c->primary = mi.rcMonitor;
  else if (!c->found) { c->second = mi.rcMonitor; c->found = true; }
  return TRUE;
}

}  // namespace

Panel& ThePanel() { return g_panel; }

// Where the panel goes.
//
// A non-primary monitor is the answer whenever there is one, because that is
// what WEMU's RandR layout produces: THOR-TOP at the origin and THOR-BOTTOM
// directly below it. When the container reports a single tall desktop instead
// -- which happens if the second screen was not enabled for the container --
// panel.x and panel.y place it by hand, and the defaults put it immediately
// below a 1080-tall top panel, which is where THOR-BOTTOM would have been.
bool Panel::ResolveRect(RECT* out) {
  MonitorScan scan;
  ZeroMemory(&scan, sizeof(scan));
  EnumDisplayMonitors(nullptr, nullptr, ScanProc, reinterpret_cast<LPARAM>(&scan));
  found_second_monitor_ = scan.found;

  const int cfg_x = Cfg().GetInt("panel.x", -1);
  const int cfg_y = Cfg().GetInt("panel.y", -1);
  const int cfg_w = Cfg().GetInt("panel.width", 0);
  const int cfg_h = Cfg().GetInt("panel.height", 0);

  if (cfg_x >= 0 && cfg_y >= 0 && cfg_w > 0 && cfg_h > 0) {
    out->left = cfg_x; out->top = cfg_y;
    out->right = cfg_x + cfg_w; out->bottom = cfg_y + cfg_h;
    DXS_INFO("[panel] using the position from the ini");
    return true;
  }
  if (scan.found) {
    *out = scan.second;
    return true;
  }

  const int w = cfg_w > 0 ? cfg_w : 1240;
  const int h = cfg_h > 0 ? cfg_h : 1080;
  const int x = cfg_x >= 0 ? cfg_x : 0;
  const int y = cfg_y >= 0 ? cfg_y : (scan.primary.bottom - scan.primary.top);
  out->left = x; out->top = y; out->right = x + w; out->bottom = y + h;
  DXS_WARN("[panel] no second monitor reported; assuming the bottom panel is at "
           "(%d,%d) %dx%d. If the container reports one tall desktop, set panel.x "
           "and panel.y in DeusExSecondScreen.ini.", x, y, w, h);
  return false;
}

bool Panel::Create(IDXGIFactory* factory, ID3D11Device* device, ID3D11DeviceContext* context,
                   CreateSwapChainFn create_swapchain) {
  if (!factory || !device || !context) return false;
  factory_ = factory;
  device_ = device;
  context_ = context;
  create_swapchain_ = create_swapchain;

  RECT rect;
  ZeroMemory(&rect, sizeof(rect));
  ResolveRect(&rect);
  width_ = rect.right - rect.left;
  height_ = rect.bottom - rect.top;
  if (width_ <= 0 || height_ <= 0) {
    DXS_ERR("[panel] resolved a zero-sized panel");
    return false;
  }

  if (!g_class_registered) {
    WNDCLASSEXW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc)) {
      DXS_ERR("[panel] RegisterClassExW failed (%lu)", GetLastError());
      return false;
    }
    g_class_registered = true;
  }

  // WS_POPUP so the client area is the whole panel and the window's top-left is
  // exactly the monitor origin -- the coordinate WEMU tests. WS_EX_NOACTIVATE
  // so the panel can never steal focus from the game, which is the single most
  // important property of this window: a panel that takes focus stops the
  // player moving.
  window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            kClassName, L"Deus Ex: Human Revolution",
                            WS_POPUP,
                            rect.left, rect.top, width_, height_,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    DXS_ERR("[panel] CreateWindowExW failed (%lu)", GetLastError());
    return false;
  }
  window_owner_thread_ = GetCurrentThreadId();
  // Map the window immediately so WEMU can claim the lower display. Its first
  // frame is deliberately blank; game_loaded_ gates chrome, native UI routing
  // and touch independently. Hiding the HWND itself prevented DXVK from ever
  // producing a buffer and left WEMU's console on the lower panel forever.
  ShowWindow(window_, SW_SHOWNOACTIVATE);

  if (!CreateSurface()) {
    DestroyWindow(window_);
    window_ = nullptr;
    window_owner_thread_ = 0;
    return false;
  }
  if (!TheNativeUiRenderer().Initialize(device_, context_)) {
    DXS_WARN("[ui] native game artwork renderer is unavailable; keeping the fully populated fallback UI");
  }
  SetGuestTouchDispatcher(&DispatchGuestTouch, this);
  SetPageSelectionHandler(&DispatchPageSelection, this);
  Clear();
  last_error_ = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  blank_frame_presented_ = SUCCEEDED(last_error_);
  DXS_INFO("[panel] mapped a blank lower surface while waiting for a loaded save"
           " (0x%08lX)", static_cast<unsigned long>(last_error_));
  DXS_INFO("[panel] ready: %dx%d at (%ld,%ld), hwnd %p, second monitor %s",
           width_, height_, rect.left, rect.top, static_cast<void*>(window_),
           found_second_monitor_ ? "found" : "NOT found");
  return true;
}

bool Panel::RehomeToCurrentThread() {
  const DWORD current = GetCurrentThreadId();
  if (window_ && IsWindow(window_) && window_owner_thread_ == current) {
    return true;
  }

  HWND old_window = window_;
  const DWORD old_owner = window_owner_thread_;
  ReleaseSurface();
  window_ = nullptr;
  window_owner_thread_ = 0;
  if (old_window && IsWindow(old_window)) {
    if (old_owner == current) {
      DestroyWindow(old_window);
    } else {
      DWORD_PTR ignored = 0;
      if (!SendMessageTimeoutW(old_window, kDestroyPanelWindowMessage, 0, 0,
                               SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &ignored)) {
        // The retired owner may still be unwinding its last Present. Its
        // surface is already released, so a queued creator-thread teardown is
        // safe even if it runs after the replacement window is live.
        // Hide it asynchronously first so WEMU cannot retain a second visible
        // lower-window claim if that owner remains alive but never pumps again.
        ShowWindowAsync(old_window, SW_HIDE);
        PostMessageW(old_window, kDestroyPanelWindowMessage, 0, 0);
        retired_windows_.push_back(old_window);
      }
    }
  }

  RECT rect = {};
  ResolveRect(&rect);
  width_ = rect.right - rect.left;
  height_ = rect.bottom - rect.top;
  if (width_ <= 0 || height_ <= 0) {
    DXS_ERR("[panel] cannot re-home to a zero-sized display");
    return false;
  }
  window_ = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
                            kClassName, L"Deus Ex: Human Revolution",
                            WS_POPUP, rect.left, rect.top, width_, height_,
                            nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (!window_) {
    DXS_ERR("[panel] could not re-create its window on thread %lu (%lu)",
            static_cast<unsigned long>(current), GetLastError());
    return false;
  }
  window_owner_thread_ = current;
  ShowWindow(window_, SW_SHOWNOACTIVATE);
  if (!CreateSurface()) {
    DestroyWindow(window_);
    window_ = nullptr;
    window_owner_thread_ = 0;
    return false;
  }
  Clear();
  chrome_dirty_.store(true, std::memory_order_release);
  ui_present_pending_ = true;
  blank_frame_presented_ = false;
  DXS_INFO("[panel] re-homed lower window/surface from thread %lu to %lu",
           static_cast<unsigned long>(old_owner),
           static_cast<unsigned long>(current));
  return true;
}

bool Panel::CreateSurface() {
  DXGI_SWAP_CHAIN_DESC desc;
  ZeroMemory(&desc, sizeof(desc));
  desc.BufferDesc.Width = static_cast<UINT>(width_);
  desc.BufferDesc.Height = static_cast<UINT>(height_);
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.BufferCount = 2;
  desc.OutputWindow = window_;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  last_error_ = create_swapchain_
      ? create_swapchain_(factory_, reinterpret_cast<IUnknown*>(device_), &desc, &swapchain_)
      : factory_->CreateSwapChain(device_, &desc, &swapchain_);
  if (FAILED(last_error_) || !swapchain_) {
    DXS_ERR("[panel] CreateSwapChain failed 0x%08lX", last_error_);
    swapchain_ = nullptr;
    return false;
  }

  // The panel must never take the display into exclusive fullscreen: the game
  // owns the display mode and a second swapchain fighting it for that is how a
  // session ends up with two surfaces and no picture.
  factory_->MakeWindowAssociation(window_, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);

  ID3D11Texture2D* back = nullptr;
  last_error_ = swapchain_->GetBuffer(0, IID_ID3D11Texture2D, reinterpret_cast<void**>(&back));
  if (FAILED(last_error_) || !back) {
    DXS_ERR("[panel] GetBuffer failed 0x%08lX", last_error_);
    ReleaseSurface();
    return false;
  }
  ID3D11RenderTargetView* view = nullptr;
  last_error_ = device_->CreateRenderTargetView(back, nullptr, &view);
  if (FAILED(last_error_)) {
    DXS_ERR("[panel] CreateRenderTargetView failed 0x%08lX", last_error_);
    if (view) view->Release();
    back->Release();
    ReleaseSurface();
    return false;
  }
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    rtv_ = view;
    back_buffer_ = back;
    chrome_height_ = 0;
    chrome_dirty_.store(true, std::memory_order_release);
    surface_ready_.store(true, std::memory_order_release);
  }
  // A replacement swapchain has not published its new buffer, even when the
  // old surface had already presented a blank frame.
  blank_frame_presented_ = false;
  ui_present_pending_ = true;
  last_ui_present_ms_ = 0;
  surface_retry_ms_ = 0;
  return true;
}

void Panel::ReleaseSurface() {
  surface_ready_.store(false, std::memory_order_release);
  ID3D11RenderTargetView* view = nullptr;
  ID3D11Texture2D* back = nullptr;
  ID3D11Texture2D* chrome = nullptr;
  ID3D11RenderTargetView* chrome_view = nullptr;
  ID3D11Texture2D* snapshot = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    view = rtv_;
    rtv_ = nullptr;
    back = back_buffer_;
    back_buffer_ = nullptr;
    chrome = chrome_texture_;
    chrome_texture_ = nullptr;
    chrome_view = chrome_rtv_;
    chrome_rtv_ = nullptr;
    snapshot = hud_snapshot_;
    hud_snapshot_ = nullptr;
    chrome_height_ = 0;
  }
  ui_present_pending_ = true;
  last_ui_present_ms_ = 0;
  if (view) view->Release();
  if (back) back->Release();
  if (chrome_view) chrome_view->Release();
  if (chrome) chrome->Release();
  if (snapshot) snapshot->Release();
  if (swapchain_) { swapchain_->SetFullscreenState(FALSE, nullptr); swapchain_->Release(); swapchain_ = nullptr; }
  ReleaseChromeGdiSurface();
}

void Panel::ReleaseChromeGdiSurface() {
  if (chrome_dc_ && chrome_old_bitmap_) {
    SelectObject(chrome_dc_, chrome_old_bitmap_);
  }
  chrome_old_bitmap_ = nullptr;
  if (chrome_bitmap_) DeleteObject(chrome_bitmap_);
  chrome_bitmap_ = nullptr;
  chrome_dib_bits_ = nullptr;
  if (chrome_dc_) DeleteDC(chrome_dc_);
  chrome_dc_ = nullptr;
  chrome_gdi_width_ = 0;
  chrome_gdi_height_ = 0;
  std::vector<uint32_t>().swap(chrome_upload_pixels_);
}

bool Panel::EnsureChromeGdiSurface() {
  const size_t pixel_count = static_cast<size_t>(width_) *
                             static_cast<size_t>(height_);
  if (chrome_dc_ && chrome_bitmap_ && chrome_dib_bits_ &&
      chrome_gdi_width_ == width_ && chrome_gdi_height_ == height_ &&
      chrome_upload_pixels_.size() == pixel_count) {
    return true;
  }

  ReleaseChromeGdiSurface();
  if (width_ <= 0 || height_ <= 0 || pixel_count == 0) return false;

  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = width_;
  info.bmiHeader.biHeight = -height_;  // top-down
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;

  chrome_dc_ = CreateCompatibleDC(nullptr);
  if (!chrome_dc_) return false;
  chrome_bitmap_ = CreateDIBSection(chrome_dc_, &info, DIB_RGB_COLORS,
                                    &chrome_dib_bits_, nullptr, 0);
  if (!chrome_bitmap_ || !chrome_dib_bits_) {
    ReleaseChromeGdiSurface();
    return false;
  }
  chrome_old_bitmap_ = SelectObject(chrome_dc_, chrome_bitmap_);
  if (!chrome_old_bitmap_ || chrome_old_bitmap_ == HGDI_ERROR) {
    chrome_old_bitmap_ = nullptr;
    ReleaseChromeGdiSurface();
    return false;
  }

  chrome_upload_pixels_.resize(pixel_count);
  chrome_gdi_width_ = width_;
  chrome_gdi_height_ = height_;
  return true;
}

bool Panel::ready() const {
  return surface_ready_.load(std::memory_order_acquire);
}

void Panel::SetGameLoaded(bool loaded, const char* evidence) {
  const bool hide_until_gameplay = Cfg().GetBool("ui.hide_until_gameplay", true);
  if (!hide_until_gameplay) {
    loaded = true;
  } else {
    const uint32_t now = GetTickCount();
    if (!loaded) {
      // Loading and gameplay movies briefly coexist while Scaleform crossfades
      // them. Keep the lower output blank until non-gameplay evidence has been
      // absent for half a second.
      show_after_tick_.store(now + 500u, std::memory_order_release);
    } else {
      const uint32_t deadline = show_after_tick_.load(std::memory_order_acquire);
      if (deadline != 0 && static_cast<int32_t>(now - deadline) < 0) return;
    }
  }
  // Clear before publishing either side of a save/menu boundary. In
  // particular, ServiceGameFrame observes the new true value and drains the
  // queue before Present applies the visual transition; a load-screen tap must
  // never execute against the newly attached player/inventory in that gap.
  if (game_loaded_.load(std::memory_order_acquire) != loaded) {
    ClearQueuedGameActions();
  }
  const bool previous = game_loaded_.exchange(loaded, std::memory_order_acq_rel);
  if (previous == loaded) return;
  game_visibility_dirty_.store(true, std::memory_order_release);
  if (loaded) {
    DXS_INFO("[panel] gameplay HUD detected from %s; lower UI is now visible",
             evidence ? evidence : "native UI");
  } else {
    DXS_INFO("[panel] %s detected; lower UI hidden until a save finishes loading",
             evidence ? evidence : "non-gameplay state");
  }
}

void Panel::ApplyGameLoadedTransition() {
  if (!game_visibility_dirty_.exchange(false, std::memory_order_acq_rel) ||
      !window_) {
    return;
  }
  if (game_loaded()) {
    // Save loading continues to stream render resources briefly after the
    // player graph becomes coherent. Keep the first custom frames entirely
    // independent of that streamer, then use only resident/passive artwork.
    native_artwork_ready_ms_ = GetTickCount64() + 3000;
    native_icon_retry_ms_ = native_artwork_ready_ms_;
    native_icon_retry_delay_ms_ = 1000;
    native_artwork_delay_reported_ = false;
    if (Cfg().GetBool("panel.topmost", false)) {
      SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    chrome_dirty_.store(true, std::memory_order_release);
    ui_present_pending_ = true;
  } else {
    // Touch arrives on the loopback receiver and is consumed one Present
    // later. A title/load transition can therefore retire the live player
    // after a tap was queued but before its native action runs. Snapshots are
    // cleared below, but the action queue is independent; leaving it intact
    // lets an equip/use/purchase intended for the old save replay against the
    // first coherent inventory in the next save. Retire those commands at the
    // same authoritative gameplay boundary.
    ClearQueuedGameActions();
    native_artwork_ready_ms_ = 0;
    native_icon_retry_ms_ = 0;
    native_icon_retry_delay_ms_ = 1000;
    native_artwork_delay_reported_ = false;
    ResetNativeInventorySnapshotReady();
    weapon_equipped_.store(false, std::memory_order_release);
    // These snapshots include the complete inventory and augmentation model.
    // Reconstruct in place so a menu/load transition cannot materialize a
    // second, very large aggregate on DXHR's Present-thread stack.
    inventory_snapshot_.~InventorySnapshot();
    new (&inventory_snapshot_) InventorySnapshot();
    native_map_snapshot_.~NativeMapSnapshot();
    new (&native_map_snapshot_) NativeMapSnapshot();
    native_map_staging_.~NativeMapSnapshot();
    new (&native_map_staging_) NativeMapSnapshot();
    inventory_unavailable_since_ms_ = 0;
    consecutive_inventory_capture_failures_ = 0;
    consecutive_map_capture_failures_ = 0;
    last_map_poll_ms_ = 0;
    last_map_pose_poll_ms_ = 0;
    map_floor_map_id_ = 0;
    map_display_floor_ = 0;
    map_floor_manual_ = false;
    map_follow_player_ = true;
    smartglass_snapshot_ = SmartGlassSnapshot{};
    TheSmartGlass().Clear();
    gameplay_unlock_started_ms_ = 0;
    gameplay_ready_samples_ = 0;
    pressed_header_target_ = kNoPressedHeaderTarget;
    touch_button_down_ = false;
    touch_dragged_ = false;
    selected_item_ = -1;
    selected_item_identity_ = InventoryActionIdentity{};
    pressed_item_ = -1;
    pressed_item_identity_ = InventoryActionIdentity{};
    pressed_quick_slot_ = -1;
    drag_item_ = -1;
    drag_item_identity_ = InventoryActionIdentity{};
    inventory_ui_slots_.clear();
    inventory_ui_pickups_.clear();
    pressed_hit_ = CustomHit{};
    active_tab_.store(3, std::memory_order_release);
    if (Cfg().GetBool("panel.topmost", false)) {
      SetWindowPos(window_, HWND_NOTOPMOST, 0, 0, 0, 0,
                   SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    Clear();
    // Present() owns the retry and marks this only after the cleared replacement
    // buffer has actually reached DXGI.
    blank_frame_presented_ = false;
    ui_present_pending_ = true;
    last_ui_present_ms_ = 0;
  }
}

void Panel::ObserveGameState(bool menu_input_locked) {
  if (!Cfg().GetBool("ui.hide_until_gameplay", true)) {
    SetGameLoaded(true, "hide_until_gameplay=0");
    return;
  }
  if (game_loaded()) return;

  if (menu_input_locked) {
    if (!initial_menu_lock_seen_) {
      initial_menu_lock_seen_ = true;
      DXS_INFO("[panel] title/save menu input lease observed; gameplay gate armed");
    }
    gameplay_unlock_started_ms_ = 0;
    gameplay_ready_samples_ = 0;
    return;
  }
  if (!initial_menu_lock_seen_) return;

  const uint64_t now = GetTickCount64();
  if (gameplay_unlock_started_ms_ == 0) {
    gameplay_unlock_started_ms_ = now;
    return;
  }
  // Page handoffs can briefly release the lease, and DXHR releases it before
  // the player/inventory graph has necessarily finished construction. Wait at
  // least half a second, then require four coherent live-inventory samples.
  // A failed sample resets the streak, preventing transient load objects from
  // opening the SmartGlass and artwork paths.
  if (now - gameplay_unlock_started_ms_ < 500) return;
  if (!NativeInventoryAvailable()) {
    gameplay_ready_samples_ = 0;
    return;
  }
  ++gameplay_ready_samples_;
  if (gameplay_ready_samples_ >= 4) {
    SetGameLoaded(true, "stable live player inventory");
  }
}

ID3D11RenderTargetView* Panel::AcquireRtv() const {
  std::lock_guard<std::mutex> guard(rtv_lock_);
  if (rtv_) rtv_->AddRef();
  return rtv_;
}

bool Panel::EnsureChrome() {
  const int requested_height = TheLayout().HeaderHeight();
  if (!device_ || requested_height <= 0 || width_ <= 0) return false;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    if (chrome_texture_ && chrome_rtv_ &&
        chrome_height_ == requested_height) return true;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = static_cast<UINT>(width_);
  // The same immutable texture carries both the tab strip and the custom HUD
  // furniture underneath Scaleform. It is redrawn only when the selected tab
  // changes, not every game frame.
  desc.Height = static_cast<UINT>(height_);
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  // Native item/augmentation artwork is rendered into this persistent texture
  // once when the page changes, then the completed page is copied to the
  // discard-chain buffer. This removes a full pipeline state save and one draw
  // per icon from every ordinary Present.
  desc.BindFlags = D3D11_BIND_RENDER_TARGET;

  ID3D11Texture2D* texture = nullptr;
  const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr) || !texture) {
    DXS_ERR("[panel] could not create %dx%d UI chrome texture (0x%08lX)",
            width_, height_, hr);
    if (texture) texture->Release();
    return false;
  }

  ID3D11RenderTargetView* texture_view = nullptr;
  const HRESULT view_hr =
      device_->CreateRenderTargetView(texture, nullptr, &texture_view);
  if (FAILED(view_hr) || !texture_view) {
    DXS_ERR("[panel] could not create UI chrome render target (0x%08lX)",
            view_hr);
    if (texture_view) texture_view->Release();
    texture->Release();
    return false;
  }

  ID3D11Texture2D* replaced = nullptr;
  ID3D11RenderTargetView* replaced_view = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    replaced = chrome_texture_;
    replaced_view = chrome_rtv_;
    chrome_texture_ = texture;
    chrome_rtv_ = texture_view;
    chrome_height_ = requested_height;
    chrome_dirty_.store(true, std::memory_order_release);
  }
  if (replaced_view) replaced_view->Release();
  if (replaced) replaced->Release();
  return true;
}

bool Panel::EnsureHudSnapshot() {
  if (!device_ || !back_buffer_) return false;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    if (hud_snapshot_) return true;
  }

  D3D11_TEXTURE2D_DESC desc = {};
  back_buffer_->GetDesc(&desc);
  desc.BindFlags = 0;
  desc.CPUAccessFlags = 0;
  desc.MiscFlags = 0;
  desc.Usage = D3D11_USAGE_DEFAULT;

  ID3D11Texture2D* texture = nullptr;
  const HRESULT hr = device_->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr) || !texture) {
    DXS_ERR("[panel] could not create the live HUD snapshot (0x%08lX)", hr);
    if (texture) texture->Release();
    return false;
  }

  ID3D11Texture2D* replaced = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    replaced = hud_snapshot_;
    hud_snapshot_ = texture;
  }
  if (replaced) replaced->Release();
  return true;
}

void Panel::DrawChrome(ID3D11DeviceContext* context) {
  ID3D11Texture2D* texture = nullptr;
  int bar_height = 0;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    texture = chrome_texture_;
    bar_height = chrome_height_;
    if (texture) texture->AddRef();
  }
  if (!texture || !context || bar_height <= 0) {
    if (texture) texture->Release();
    return;
  }

  if (!EnsureChromeGdiSurface()) {
    texture->Release();
    return;
  }
  HDC dc = chrome_dc_;
  void* dib_bits = chrome_dib_bits_;

  RECT full{0, 0, width_, height_};
  HBRUSH background = CreateSolidBrush(RGB(12, 13, 12));
  FillRect(dc, &full, background);
  DeleteObject(background);

  // A very low-contrast technical grid is visible only in the breathing room
  // between panels. The genuine HUD image replaces it inside every live zone.
  const int grid_step = (std::max)(36, width_ / 24);
  HPEN grid = CreatePen(PS_SOLID, 1, RGB(19, 21, 18));
  HGDIOBJ grid_old_pen = SelectObject(dc, grid);
  for (int x = grid_step; x < width_; x += grid_step) {
    MoveToEx(dc, x, bar_height, nullptr);
    LineTo(dc, x, height_);
  }
  for (int y = bar_height + grid_step; y < height_; y += grid_step) {
    MoveToEx(dc, 0, y, nullptr);
    LineTo(dc, width_, y);
  }
  SelectObject(dc, grid_old_pen);
  DeleteObject(grid);

  const int tab_count = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
  const int selected = (std::max)(0, (std::min)(active_tab_.load(
      std::memory_order_acquire), tab_count - 1));
  if (selected == 1) {
    // Reapply the shared raster-derived limits on every map redraw. This also
    // covers panel resize, display rotation, and a floor whose raster aspect
    // differs from the previously visible floor.
    ClampCustomMapView(width_, height_, bar_height, native_map_snapshot_,
                       &map_zoom_, &map_pan_x_, &map_pan_y_);
  }
  const int font_height = (std::max)(12, static_cast<int>(bar_height * 0.27f));
  HFONT font = CreateFontW(-font_height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE,
                           FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           FF_DONTCARE, L"Arial Narrow");
  HGDIOBJ old_font = font ? SelectObject(dc, font) : nullptr;
  SetBkMode(dc, TRANSPARENT);

  CustomUiState ui_state;
  ui_state.selected_item = selected_item_;
  ui_state.drag_item = drag_item_;
  ui_state.drag_x = touch_last_x_;
  ui_state.drag_y = touch_last_y_;
  ui_state.drag_active = drag_item_ >= 0 && touch_button_down_;
  ui_state.animation_ms = GetTickCount64();
  ui_state.selected_aug = selected_aug_;
  ui_state.selected_aug_node = selected_aug_node_;
  ui_state.selected_objective = selected_objective_;
  ui_state.selected_media = selected_media_;
  ui_state.inventory_page = inventory_page_;
  ui_state.inventory_slots = inventory_ui_slots_;
  ui_state.augmentation_page = augmentation_page_;
  ui_state.objective_page = objective_page_;
  ui_state.media_page = media_page_;
  ui_state.map_zoom = map_zoom_;
  ui_state.map_pan_x = map_pan_x_;
  ui_state.map_pan_y = map_pan_y_;
  ui_state.map_follow_player = map_follow_player_;
  CustomUiFrame next_frame;
  DrawCustomUi(dc, width_, height_, bar_height, selected,
               inventory_snapshot_, smartglass_snapshot_, native_map_snapshot_, ui_state,
               &next_frame);
  custom_ui_frame_ = std::move(next_frame);

  const HeaderGeometry nav = MakeHeaderGeometry(width_, bar_height);
  HGDIOBJ previous_font = nullptr;
  auto draw_button = [&](int left, int right, bool active, const wchar_t* text,
                          int icon_tab) {
    HBRUSH fill = CreateSolidBrush(active ? RGB(91, 64, 20) : RGB(10, 12, 11));
    HPEN edge = CreatePen(PS_SOLID, active ? 2 : 1,
                          active ? RGB(241, 178, 50) : RGB(104, 77, 29));
    HGDIOBJ old_brush = SelectObject(dc, fill);
    HGDIOBJ old_pen = SelectObject(dc, edge);
    Rectangle(dc, left, nav.nav_top, right, nav.nav_bottom);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(edge);
    DeleteObject(fill);

    const COLORREF ink = active ? RGB(255, 225, 158) : RGB(181, 181, 176);
    int label_left = left + 8;
    if (icon_tab >= 0) {
      const int icon_size = (std::max)(26,
          static_cast<int>((nav.nav_bottom - nav.nav_top) * 0.50f));
      RECT icon{left + 14, (nav.nav_top + nav.nav_bottom - icon_size) / 2,
                left + 14 + icon_size,
                (nav.nav_top + nav.nav_bottom + icon_size) / 2};
      DrawTabIcon(dc, icon_tab, icon,
                  active ? RGB(255, 205, 86) : RGB(231, 169, 48));
      label_left = icon.right + 8;
    }
    RECT label{label_left, nav.nav_top + 2, right - 8, nav.nav_bottom - 2};
    SetTextColor(dc, ink);
    DrawTextW(dc, text, -1, &label,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  };

  const int side_left = nav.side_margin;
  const int side_right = width_ - nav.side_margin - nav.side_width;
  draw_button(side_left, side_left + nav.side_width, false, L"", -1);
  draw_button(side_right, side_right + nav.side_width, false, L"", -1);

  // These are native touch controls, not controller prompts. Draw simple
  // chevrons so the row communicates that the three visible pages scroll.
  HPEN arrow_pen = CreatePen(PS_SOLID, (std::max)(3, width_ / 310),
                             RGB(241, 178, 50));
  HGDIOBJ old_arrow_pen = SelectObject(dc, arrow_pen);
  const int arrow_half_h = (nav.nav_bottom - nav.nav_top) / 5;
  const int left_mid_x = side_left + nav.side_width / 2 + 4;
  const int right_mid_x = side_right + nav.side_width / 2 - 4;
  const int arrow_mid_y = (nav.nav_top + nav.nav_bottom) / 2;
  MoveToEx(dc, left_mid_x + 7, arrow_mid_y - arrow_half_h, nullptr);
  LineTo(dc, left_mid_x - 7, arrow_mid_y);
  LineTo(dc, left_mid_x + 7, arrow_mid_y + arrow_half_h);
  MoveToEx(dc, right_mid_x - 7, arrow_mid_y - arrow_half_h, nullptr);
  LineTo(dc, right_mid_x + 7, arrow_mid_y);
  LineTo(dc, right_mid_x - 7, arrow_mid_y + arrow_half_h);
  SelectObject(dc, old_arrow_pen);
  DeleteObject(arrow_pen);

  // Three equal buttons remain visible at every scroll position. The active
  // page stays centered whenever there is a neighbor on both sides.
  const int nav_font_height = (std::max)(14,
      static_cast<int>((nav.nav_bottom - nav.nav_top) * 0.23f));
  HFONT nav_font = CreateFontW(-nav_font_height, 0, 0, 0, FW_SEMIBOLD,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, FF_DONTCARE,
                               L"Arial Narrow");
  previous_font = nav_font ? SelectObject(dc, nav_font) : nullptr;
  const int first_visible = VisibleTabStart(selected);
  for (int visible = 0; visible < 3; ++visible) {
    const int slot = first_visible + visible;
    const int left = nav.tabs_left + visible * (nav.tab_width + nav.gap);
    const int right = left + nav.tab_width;
    const int tab = kVisualTabs[slot];
    const bool active = tab == selected;
    draw_button(left, right, active, kVisualLabels[slot], tab);
    if (active) {
      POINT pointer[] = {{(left + right) / 2 - 11, nav.nav_bottom},
                         {(left + right) / 2 + 11, nav.nav_bottom},
                         {(left + right) / 2, bar_height - 2}};
      HBRUSH gold = CreateSolidBrush(RGB(238, 169, 43));
      HGDIOBJ old_brush = SelectObject(dc, gold);
      HGDIOBJ old_pen = SelectObject(dc, GetStockObject(NULL_PEN));
      Polygon(dc, pointer, 3);
      SelectObject(dc, old_pen);
      SelectObject(dc, old_brush);
      DeleteObject(gold);
    }
  }
  if (previous_font) SelectObject(dc, previous_font);
  if (nav_font) DeleteObject(nav_font);

  HPEN lower = CreatePen(PS_SOLID, 2, RGB(151, 105, 29));
  HGDIOBJ old_pen = SelectObject(dc, lower);
  MoveToEx(dc, 0, bar_height - 2, nullptr);
  LineTo(dc, width_, bar_height - 2);
  SelectObject(dc, old_pen);
  DeleteObject(lower);

  if (old_font) SelectObject(dc, old_font);
  if (font) DeleteObject(font);
  GdiFlush();

  // GDI's DIB is B,G,R,X while the panel surface is R,G,B,A.
  const auto* bgra = static_cast<const uint32_t*>(dib_bits);
  for (size_t i = 0; i < chrome_upload_pixels_.size(); ++i) {
    const uint32_t p = bgra[i];
    const uint32_t r = (p >> 16) & 0xFF;
    const uint32_t g = (p >> 8) & 0xFF;
    const uint32_t b = p & 0xFF;
    chrome_upload_pixels_[i] = r | (g << 8) | (b << 16) | 0xFF000000u;
  }

  context->UpdateSubresource(texture, 0, nullptr, chrome_upload_pixels_.data(),
                             static_cast<UINT>(width_ * sizeof(uint32_t)), 0);
  texture->Release();
  chrome_dirty_.store(false, std::memory_order_release);
}

void Panel::PrepareUiFrame(ID3D11DeviceContext* context) {
  if (!context) return;
  ID3D11RenderTargetView* view = AcquireRtv();
  if (!view) return;
  const float ink[4] = {0.043f, 0.039f, 0.031f, 1.0f};
  context->ClearRenderTargetView(view, ink);
  view->Release();
  // The queued legacy Scaleform pass may execute on the renderer thread.  The
  // opaque custom page is composed later, on the primary Present thread, so it
  // must not touch live model/touch state here.  Present replaces this whole
  // temporary legacy image before the lower swapchain is submitted.
}

void Panel::ComposeHud(ID3D11DeviceContext* context) {
  if (!game_loaded() || !context ||
      active_tab_.load(std::memory_order_acquire) != 0 ||
      !EnsureChrome() || !EnsureHudSnapshot()) return;
  if (chrome_dirty_.load(std::memory_order_acquire)) DrawChrome(context);

  ID3D11Texture2D* back = nullptr;
  ID3D11Texture2D* chrome = nullptr;
  ID3D11Texture2D* snapshot = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    back = back_buffer_;
    chrome = chrome_texture_;
    snapshot = hud_snapshot_;
    if (back) back->AddRef();
    if (chrome) chrome->AddRef();
    if (snapshot) snapshot->AddRef();
  }
  if (!back || !chrome || !snapshot) {
    if (snapshot) snapshot->Release();
    if (chrome) chrome->Release();
    if (back) back->Release();
    return;
  }

  // Capture the genuine game-rendered values, replace the old full-screen HUD
  // with the custom dashboard, then restore only the useful live regions into
  // their matching panels. All copies stay on the GPU; there is no readback.
  context->CopyResource(snapshot, back);
  context->CopyResource(back, chrome);

  const char* live_zones[] = {
      "vitals", "sensor", "weapon", "objectives", "log", "context"};
  for (const char* name : live_zones) {
    const Rect* zone = TheLayout().Zone(name);
    if (!zone) continue;
    const UINT left = static_cast<UINT>((std::max)(0.0f, zone->x + 10.0f));
    const UINT top = static_cast<UINT>((std::max)(0.0f, zone->y + 38.0f));
    const UINT right = static_cast<UINT>((std::min)(static_cast<float>(width_),
                                                     zone->x + zone->w - 10.0f));
    const UINT bottom = static_cast<UINT>((std::min)(static_cast<float>(height_),
                                                      zone->y + zone->h - 10.0f));
    if (right <= left || bottom <= top) continue;
    D3D11_BOX source{left, top, 0, right, bottom, 1};
    context->CopySubresourceRegion(back, 0, left, top, 0, snapshot, 0, &source);
  }

  snapshot->Release();
  chrome->Release();
  back->Release();
}

namespace {

void CopyChromeBox(ID3D11DeviceContext* context, ID3D11Texture2D* back,
                   ID3D11Texture2D* chrome, int width, int height,
                   int left, int top, int right, int bottom) {
  left = (std::max)(0, (std::min)(left, width));
  right = (std::max)(0, (std::min)(right, width));
  top = (std::max)(0, (std::min)(top, height));
  bottom = (std::max)(0, (std::min)(bottom, height));
  if (right <= left || bottom <= top) return;
  D3D11_BOX source{static_cast<UINT>(left), static_cast<UINT>(top), 0,
                   static_cast<UINT>(right), static_cast<UINT>(bottom), 1};
  context->CopySubresourceRegion(back, 0, static_cast<UINT>(left),
                                 static_cast<UINT>(top), 0, chrome, 0, &source);
}

void CopyPanelFurniture(ID3D11DeviceContext* context, ID3D11Texture2D* back,
                        ID3D11Texture2D* chrome, int width, int height,
                        const Rect& panel) {
  const int l = static_cast<int>(panel.x);
  const int t = static_cast<int>(panel.y);
  const int r = static_cast<int>(panel.x + panel.w);
  const int b = static_cast<int>(panel.y + panel.h);
  const int edge = 3;
  // Only the title rail and perimeter are recomposed over the native page.
  // The live Scaleform icons, counts, selection highlights and hit targets are
  // never covered, so the visual shell cannot turn the page into a mock-up.
  CopyChromeBox(context, back, chrome, width, height, l, t, r, t + 44);
  CopyChromeBox(context, back, chrome, width, height, l, b - edge, r, b);
  CopyChromeBox(context, back, chrome, width, height, l, t + 44, l + edge, b);
  CopyChromeBox(context, back, chrome, width, height, r - edge, t + 44, r, b);
}

}  // namespace

void Panel::ComposePageChrome(ID3D11DeviceContext* context) {
  const int selected = active_tab_.load(std::memory_order_acquire);
  if (!game_loaded() || !context || selected <= 0 || !EnsureChrome()) return;
  if (chrome_dirty_.load(std::memory_order_acquire)) DrawChrome(context);

  ID3D11Texture2D* back = nullptr;
  ID3D11Texture2D* chrome = nullptr;
  int header = 0;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    back = back_buffer_;
    chrome = chrome_texture_;
    header = chrome_height_;
    if (back) back->AddRef();
    if (chrome) chrome->AddRef();
  }
  if (!back || !chrome || header <= 0) {
    if (chrome) chrome->Release();
    if (back) back->Release();
    return;
  }

  // Scaleform owns the page body and remains the source of truth. Reapply the
  // navigation and thin reference furniture afterward because an opaque GFx
  // root otherwise hides the custom chrome even though the page itself works.
  CopyChromeBox(context, back, chrome, width_, height_, 0, 0, width_, header);
  if (selected == 3) {
    const InventoryReferenceRects r =
        MakeInventoryReferenceRects(width_, height_, header);
    CopyPanelFurniture(context, back, chrome, width_, height_, r.weapons);
    CopyPanelFurniture(context, back, chrome, width_, height_, r.inventory);
    CopyPanelFurniture(context, back, chrome, width_, height_, r.status);
    CopyPanelFurniture(context, back, chrome, width_, height_, r.quick);
  } else {
    CopyPanelFurniture(context, back, chrome, width_, height_, TheLayout().Page());
  }

  chrome->Release();
  back->Release();
}

void Panel::ActivateTab(int index) {
  const int count = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
  if (index == 0) index = 3;
  if (index < 1 || index >= count) return;
  // Tabs select pages; they are not toggle buttons. Re-sending a page binding
  // would close the page, while re-sending Escape on HUD would open the pause
  // menu. Keeping the selected tab idempotent avoids both surprises.
  if (active_tab_.load(std::memory_order_acquire) == index) return;
  // This is a true second-screen page switch. Opening the PC menu with a key
  // pauses the world, captures controller input, and makes the same binding
  // close the page again. The custom pages instead consume the game's dormant
  // SmartGlass models and invoke explicit native actions.
  SelectTab(index);
}

bool Panel::SelectTab(int index) {
  const int count = static_cast<int>(sizeof(kTabs) / sizeof(kTabs[0]));
  if (index == 0) index = 3;
  if (index < 1 || index >= count) return false;
  active_tab_.store(index, std::memory_order_release);
  // Do not make a newly-opened map wait for the general model cadence before
  // acquiring its current floor, player position, and radar contacts.
  if (index == 1) {
    last_model_poll_ms_ = 0;
    last_map_poll_ms_ = 0;
    last_map_pose_poll_ms_ = 0;
    map_floor_manual_ = false;
    map_display_floor_ = native_map_snapshot_.level;
    map_follow_player_ = true;
    CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                            native_map_snapshot_, &map_zoom_,
                            &map_pan_x_, &map_pan_y_);
  }
  TheSmartGlass().SetPage(index);
  chrome_dirty_.store(true, std::memory_order_release);
  DXS_INFO("[panel] selected tab %d (%ls) from direct guest input",
           index, kTabs[index].label);
  return true;
}

void Panel::ObserveMenuInputState(bool locked) {
  menu_input_locked_.store(locked, std::memory_order_release);
}

bool Panel::HandlePointer(UINT message, int x, int y) {
  if (!game_loaded()) return false;
  const int bar_height = TheLayout().HeaderHeight();
  if (bar_height <= 0 || x < 0 || x >= width_ || y < 0 || y >= height_) return false;
  if (y < bar_height) {
    if (message == WM_LBUTTONDOWN) {
      SetCapture(window_);
      return true;
    }
    if (message == WM_LBUTTONUP) {
      if (GetCapture() == window_) ReleaseCapture();
      const int target = HeaderTargetAt(
          x, y, width_, bar_height,
          active_tab_.load(std::memory_order_acquire));
      if (target == -2) {
        ActivateTab(CycleVisualTab(active_tab_.load(std::memory_order_acquire), -1));
      } else if (target == -3) {
        ActivateTab(CycleVisualTab(active_tab_.load(std::memory_order_acquire), 1));
      } else if (target >= 0) {
        ActivateTab(target);
      }
      return true;
    }
    return message == WM_MOUSEMOVE && GetCapture() == window_;
  }

  const float nx = static_cast<float>(x) / (std::max)(1, width_);
  const float ny = static_cast<float>(y) / (std::max)(1, height_);
  if (message == WM_LBUTTONDOWN) {
    SetCapture(window_);
    return HandleNormalizedTouch(TouchAction::kDown, nx, ny);
  }
  if (message == WM_MOUSEMOVE && GetCapture() == window_) {
    return HandleNormalizedTouch(TouchAction::kMove, nx, ny);
  }
  if (message == WM_LBUTTONUP) {
    const bool handled = HandleNormalizedTouch(TouchAction::kUp, nx, ny);
    if (GetCapture() == window_) ReleaseCapture();
    return handled;
  }
  return false;
}

bool Panel::HandleNormalizedTap(float x, float y) {
  return HandleNormalizedTouch(TouchAction::kTap, x, y);
}

bool Panel::HandleNormalizedTouch(TouchAction action, float x, float y, float value) {
  if (!game_loaded()) return false;
  // Cancellation owns no target, including when the second finger landed in
  // the header. Let the content handler clear the existing drag before header
  // hit testing can swallow it.
  if (action == TouchAction::kCancel) {
    pressed_header_target_ = kNoPressedHeaderTarget;
    return HandleCustomTouch(action, 0, 0, value);
  }

  const bool finite = std::isfinite(x) && std::isfinite(y);
  const bool in_bounds = finite && x >= 0.0f && x <= 1.0f &&
                         y >= 0.0f && y <= 1.0f;
  if (!in_bounds) {
    const bool gesture_active = touch_button_down_ ||
        pressed_header_target_ != kNoPressedHeaderTarget;
    if (!gesture_active || (action != TouchAction::kMove &&
                            action != TouchAction::kUp)) {
      return false;
    }
    if (action == TouchAction::kUp || !finite) {
      // A captured desktop pointer can be released outside the window, and a
      // guest Cancel/Up packet may use sentinel coordinates. Always retire the
      // press without activating the nearest clamped target; otherwise one
      // out-of-range release leaves every later tap attached to a stale drag.
      pressed_header_target_ = kNoPressedHeaderTarget;
      return HandleCustomTouch(TouchAction::kCancel, 0, 0, value);
    }
    if (pressed_header_target_ != kNoPressedHeaderTarget) return true;
    // Keep a content drag responsive at the physical edge, but never turn an
    // outside coordinate into a new Down/Tap target.
    x = (std::max)(0.0f, (std::min)(1.0f, x));
    y = (std::max)(0.0f, (std::min)(1.0f, y));
  }
  const int pixel_x = static_cast<int>(x * width_);
  const int pixel_y = static_cast<int>(y * height_);
  const int bar_height = TheLayout().HeaderHeight();
  const bool in_header =
      bar_height > 0 && pixel_y >= 0 && pixel_y < bar_height;
  auto header_target = [&]() {
    return in_header
        ? HeaderTargetAt(pixel_x, pixel_y, width_, bar_height,
                         active_tab_.load(std::memory_order_acquire))
        : -1;
  };
  auto activate_header = [&](int target) {
    if (target == -2) {
      ActivateTab(CycleVisualTab(
          active_tab_.load(std::memory_order_acquire), -1));
    } else if (target == -3) {
      ActivateTab(CycleVisualTab(
          active_tab_.load(std::memory_order_acquire), 1));
    } else if (target >= 0) {
      ActivateTab(target);
    }
  };

  // Once a header press begins, it owns the complete down/move/up sequence.
  // WEMU sends this modern stream rather than a synthetic `tap`; previously
  // all three packets were swallowed and no page could be selected on Thor.
  if (pressed_header_target_ != kNoPressedHeaderTarget) {
    if (action == TouchAction::kUp) {
      const int pressed = pressed_header_target_;
      pressed_header_target_ = kNoPressedHeaderTarget;
      const int released = header_target();
      if (pressed == released) activate_header(released);
    }
    return true;
  }

  // A content drag remains a content drag if the finger crosses into the
  // header; its Up must reach HandleCustomTouch so the drag state is cleared.
  if (touch_button_down_ &&
      (action == TouchAction::kMove || action == TouchAction::kUp)) {
    return HandleCustomTouch(action, pixel_x, pixel_y, value);
  }

  if (in_header) {
    const int target = header_target();
    if (action == TouchAction::kDown) {
      pressed_header_target_ = target;
    } else if (action == TouchAction::kTap) {
      // Retain compatibility with older WEMU builds that emit one tap packet.
      activate_header(target);
    }
    return true;
  }
  if (action == TouchAction::kDown) {
    pressed_header_target_ = kNoPressedHeaderTarget;
  }
  return HandleCustomTouch(action, pixel_x, pixel_y, value);
}

void Panel::ReconcileInventoryUiSlots() {
  constexpr int kCardsPerPage = 12;
  constexpr int kMinimumPages = 2;
  const int required_slots = (std::max)(
      kCardsPerPage * kMinimumPages,
      ((inventory_snapshot_.item_count + kCardsPerPage - 1) /
       kCardsPerPage) * kCardsPerPage);
  if (static_cast<int>(inventory_ui_slots_.size()) < required_slots) {
    inventory_ui_slots_.resize(required_slots, -1);
  }
  if (inventory_ui_pickups_.size() < inventory_ui_slots_.size()) {
    inventory_ui_pickups_.resize(inventory_ui_slots_.size(), 0xFFFFFFFFu);
  }

  auto live_item = [&](int item_index, uint32_t pickup_id) {
    for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
      if (inventory_snapshot_.items[item].item_index == item_index &&
          inventory_snapshot_.items[item].pickup.pickup_id == pickup_id) {
        return true;
      }
    }
    return false;
  };

  // Remove pickups that were consumed/dropped and collapse duplicate slot
  // references left behind when DXHR reuses a native container index.
  std::vector<int> seen;
  seen.reserve(inventory_snapshot_.item_count);
  for (size_t visual = 0; visual < inventory_ui_slots_.size(); ++visual) {
    int& slot = inventory_ui_slots_[visual];
    uint32_t& pickup = inventory_ui_pickups_[visual];
    if (slot < 0 || !live_item(slot, pickup) ||
        std::find(seen.begin(), seen.end(), slot) != seen.end()) {
      slot = -1;
      pickup = 0xFFFFFFFFu;
      continue;
    }
    seen.push_back(slot);
  }

  // Newly collected pickups enter the first free visual card. Existing items
  // retain their user-selected position across ordinary model refreshes.
  for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
    const int item_index = inventory_snapshot_.items[item].item_index;
    if (std::find(seen.begin(), seen.end(), item_index) != seen.end()) continue;
    auto empty = std::find(inventory_ui_slots_.begin(),
                           inventory_ui_slots_.end(), -1);
    if (empty == inventory_ui_slots_.end()) {
      inventory_ui_slots_.resize(inventory_ui_slots_.size() + kCardsPerPage,
                                  -1);
      inventory_ui_pickups_.resize(inventory_ui_slots_.size(), 0xFFFFFFFFu);
      empty = std::find(inventory_ui_slots_.begin(),
                        inventory_ui_slots_.end(), -1);
    }
    const size_t visual = static_cast<size_t>(
        std::distance(inventory_ui_slots_.begin(), empty));
    *empty = item_index;
    inventory_ui_pickups_[visual] =
        inventory_snapshot_.items[item].pickup.pickup_id;
    seen.push_back(item_index);
  }
}

bool Panel::MoveInventoryUiItem(int item_index, int destination_slot) {
  if (item_index < 0 || destination_slot < 0 ||
      destination_slot >= custom_ui_frame_.inventory_unlocked_slots) {
    return false;
  }
  ReconcileInventoryUiSlots();
  if (destination_slot >= static_cast<int>(inventory_ui_slots_.size())) {
    inventory_ui_slots_.resize(destination_slot + 1, -1);
    inventory_ui_pickups_.resize(destination_slot + 1, 0xFFFFFFFFu);
  }
  const auto source = std::find(inventory_ui_slots_.begin(),
                                inventory_ui_slots_.end(), item_index);
  if (source == inventory_ui_slots_.end()) return false;
  const int source_slot = static_cast<int>(
      std::distance(inventory_ui_slots_.begin(), source));
  if (source_slot == destination_slot) return true;

  // A drop on another card exchanges the two visual positions; a drop on an
  // empty card leaves a real gap. The native pickup objects are untouched, so
  // a presentation-only rearrangement cannot detach or destroy either item.
  std::swap(inventory_ui_slots_[source_slot],
            inventory_ui_slots_[destination_slot]);
  std::swap(inventory_ui_pickups_[source_slot],
            inventory_ui_pickups_[destination_slot]);
  DXS_INFO("[ui] rearranged inventory item=%d visual=%d->%d",
           item_index, source_slot, destination_slot);
  chrome_dirty_.store(true, std::memory_order_release);
  return true;
}

bool Panel::SortInventoryUiSlots() {
  ReconcileInventoryUiSlots();
  std::vector<const InventoryItemSnapshot*> items;
  items.reserve(inventory_snapshot_.item_count);
  for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
    items.push_back(&inventory_snapshot_.items[item]);
  }
  if (items.empty()) return false;

  std::stable_sort(items.begin(), items.end(),
                   [](const InventoryItemSnapshot* left,
                      const InventoryItemSnapshot* right) {
    if (left->pickup.category != right->pickup.category) {
      return left->pickup.category < right->pickup.category;
    }
    const int by_name = std::strcmp(left->pickup.name, right->pickup.name);
    if (by_name != 0) return by_name < 0;
    return left->pickup.pickup_id < right->pickup.pickup_id;
  });
  std::fill(inventory_ui_slots_.begin(), inventory_ui_slots_.end(), -1);
  std::fill(inventory_ui_pickups_.begin(), inventory_ui_pickups_.end(),
            0xFFFFFFFFu);
  for (size_t slot = 0;
       slot < items.size() && slot < inventory_ui_slots_.size(); ++slot) {
    inventory_ui_slots_[slot] = items[slot]->item_index;
    inventory_ui_pickups_[slot] = items[slot]->pickup.pickup_id;
  }
  selected_item_ = -1;
  selected_item_identity_ = InventoryActionIdentity{};
  DXS_INFO("[ui] sorted %u inventory cards", static_cast<unsigned>(items.size()));
  chrome_dirty_.store(true, std::memory_order_release);
  return true;
}

bool Panel::HandleCustomTouch(TouchAction action, int x, int y, float value) {
  const CustomHit hit = HitCustomUi(custom_ui_frame_, x, y);

  auto reset_touch = [&]() {
    touch_button_down_ = false;
    touch_dragged_ = false;
    pressed_item_ = -1;
    pressed_item_identity_ = InventoryActionIdentity{};
    pressed_quick_slot_ = -1;
    drag_item_ = -1;
    drag_item_identity_ = InventoryActionIdentity{};
    pressed_hit_ = CustomHit{};
  };

  if (action == TouchAction::kCancel) {
    reset_touch();
    return true;
  }

  auto find_item = [&](int item_index) -> const InventoryItemSnapshot* {
    for (int i = 0; i < inventory_snapshot_.item_count; ++i) {
      if (inventory_snapshot_.items[i].item_index == item_index)
        return &inventory_snapshot_.items[i];
    }
    return nullptr;
  };

  auto find_pickup = [&](int pickup_id) -> const InventoryItemSnapshot* {
    if (pickup_id < 0) return nullptr;
    for (int i = 0; i < inventory_snapshot_.item_count; ++i) {
      if (inventory_snapshot_.items[i].pickup.pickup_id ==
          static_cast<uint32_t>(pickup_id)) {
        return &inventory_snapshot_.items[i];
      }
    }
    return nullptr;
  };

  auto identity_from_item = [&](const InventoryItemSnapshot* item) {
    InventoryActionIdentity identity;
    if (!item) return identity;
    identity.inventory = inventory_snapshot_.inventory_identity;
    identity.revision = inventory_snapshot_.revision;
    identity.item_index = item->item_index;
    identity.pickup_id = item->pickup.pickup_id;
    return identity;
  };
  auto identity_from_hit = [&](const CustomHit& target) {
    InventoryActionIdentity identity;
    identity.inventory = custom_ui_frame_.inventory_identity;
    identity.revision = custom_ui_frame_.inventory_revision;
    identity.item_index = target.value;
    identity.pickup_id = target.pickup_id;
    return identity;
  };
  auto same_identity = [](const InventoryActionIdentity& left,
                          const InventoryActionIdentity& right) {
    return left.inventory != 0 && left.inventory == right.inventory &&
        left.revision == right.revision &&
        left.item_index == right.item_index &&
        left.pickup_id == right.pickup_id;
  };
  auto find_identity = [&](const InventoryActionIdentity& identity)
      -> const InventoryItemSnapshot* {
    if (identity.inventory != inventory_snapshot_.inventory_identity ||
        identity.revision != inventory_snapshot_.revision) {
      return nullptr;
    }
    const InventoryItemSnapshot* item = find_item(identity.item_index);
    return item && item->pickup.pickup_id == identity.pickup_id ? item : nullptr;
  };
  auto clear_selected_item = [&]() {
    selected_item_ = -1;
    selected_item_identity_ = InventoryActionIdentity{};
  };
  auto select_item = [&](const InventoryActionIdentity& identity) {
    selected_item_identity_ = identity;
    selected_item_ = identity.item_index;
  };

  auto core_augmentation_unlocked = [&](int slot, int tree_index) {
    if (slot < 0 || slot >= kCoreAugmentationCount) return false;
    if (inventory_snapshot_.augmentation_state_valid) {
      return inventory_snapshot_.augmentation_unlocked[slot];
    }
    if (inventory_snapshot_.augmentation_catalog_valid && tree_index >= 0 &&
        tree_index < inventory_snapshot_.augmentation_count) {
      const AugmentationTreeSnapshot& tree =
          inventory_snapshot_.augmentations[tree_index];
      // Retail's SmartGlass serializer treats family presence as ownership.
      // Upgrade-node count is independent and may legitimately be zero for an
      // owned base ability.
      return tree.tree_present;
    }
    constexpr int kCoreAugmentationIds[] = {0x14, 0x39, 0x33, 0x35};
    for (const SmartGlassAugmentation& augmentation :
         smartglass_snapshot_.augmentations) {
      if (augmentation.aug_id == kCoreAugmentationIds[slot] &&
          (augmentation.installed || augmentation.learned)) {
        return true;
      }
    }
    return false;
  };

  auto content_changed = [&]() {
    last_model_poll_ms_ = 0;
    rendered_model_revision_ = 0;
    chrome_dirty_.store(true, std::memory_order_release);
    TheSmartGlass().ForceRefresh();
  };

  auto clamp_map_view = [&]() {
    ClampCustomMapView(width_, height_, TheLayout().HeaderHeight(),
                       native_map_snapshot_, &map_zoom_, &map_pan_x_,
                       &map_pan_y_);
  };

  auto previous_page = [](int& page, int page_count) {
    const int last = (std::max)(0, page_count - 1);
    page = (std::max)(0, (std::min)(page, last));
    if (page_count > 1) page = page > 0 ? page - 1 : last;
  };
  auto next_page = [](int& page, int page_count) {
    const int last = (std::max)(0, page_count - 1);
    page = (std::max)(0, (std::min)(page, last));
    if (page_count > 1) page = page < last ? page + 1 : 0;
  };

  auto drop_item = [&](const InventoryActionIdentity& source_identity,
                       const CustomHit& destination,
                       int drop_x, int drop_y) -> bool {
    const InventoryItemSnapshot* source = find_identity(source_identity);
    if (!source) return false;
    const int source_index = source_identity.item_index;
    if ((destination.kind == CustomHitKind::kInventoryItem ||
         destination.kind == CustomHitKind::kWeapon) &&
        destination.value != source_index) {
      const InventoryActionIdentity target_identity =
          identity_from_hit(destination);
      const InventoryItemSnapshot* target = find_identity(target_identity);
      if (!target) return false;

      // DXHR's Combine routine is destructive by design: it can consume a
      // source and rebuild the target. Never probe it speculatively. The two
      // combinations which the retail inventory exposes are unambiguous in
      // native metadata: a category-5 weapon mod applied to a category-3
      // firearm, and two instances of the same stackable pickup.
      const bool weapon_upgrade =
          source->pickup.category == 5 && target->pickup.category == 3;
      const bool stack_merge =
          source->pickup.pickup_id == target->pickup.pickup_id &&
          source->pickup.max_stack > 1;
      if (weapon_upgrade || stack_merge) {
        int combined_item = -1;
        return TryCombineInventoryItems(source_identity, target_identity,
                                        &combined_item);
      }

      // The WEAPONS column is a duplicate loadout view, not storage. Only an
      // actual weapon-upgrade drop belongs there; an unrelated item must not
      // silently rearrange the native grid behind that duplicate card.
      if (destination.kind == CustomHitKind::kWeapon) return false;

      // Ordinary card rearrangement belongs to the custom one-card-per-item
      // presentation. DXHR's native Container::Move works in variable-size
      // packed rectangles and can detach an occupied target, so never call it
      // for this operation.
      return MoveInventoryUiItem(source_index, destination.secondary);
    }
    if (destination.kind == CustomHitKind::kQuickSlot) {
      return SetInventoryQuickSlot(destination.value, source_identity);
    }
    // Trash is activated only by its explicit hold action below. A drag that
    // ends near a control can therefore never destroy an item accidentally.
    if (destination.kind == CustomHitKind::kDiscard) return false;
    if (destination.kind != CustomHitKind::kInventoryGrid ||
        custom_ui_frame_.inventory_card_columns <= 0 ||
        custom_ui_frame_.inventory_card_rows <= 0 ||
        custom_ui_frame_.inventory_card_size <= 0) {
      return false;
    }

    // Resolve the nearest visual card, including a release in the narrow gap
    // between cards. This uses the exact geometry DrawInventory published.
    const RECT& grid = custom_ui_frame_.inventory_grid;
    const int pitch = custom_ui_frame_.inventory_card_size +
                      custom_ui_frame_.inventory_card_gap;
    const int local_x = (std::max)(
        0, (std::min)(static_cast<int>(grid.right - grid.left - 1),
                      drop_x - static_cast<int>(grid.left)));
    const int local_y = (std::max)(
        0, (std::min)(static_cast<int>(grid.bottom - grid.top - 1),
                      drop_y - static_cast<int>(grid.top)));
    const int column = (std::max)(0, (std::min)(
        custom_ui_frame_.inventory_card_columns - 1,
        (local_x + custom_ui_frame_.inventory_card_gap / 2) / pitch));
    const int row = (std::max)(0, (std::min)(
        custom_ui_frame_.inventory_card_rows - 1,
        (local_y + custom_ui_frame_.inventory_card_gap / 2) / pitch));
    const int cards_per_page = custom_ui_frame_.inventory_card_columns *
                               custom_ui_frame_.inventory_card_rows;
    const int destination_slot =
        custom_ui_frame_.inventory_card_page * cards_per_page +
        row * custom_ui_frame_.inventory_card_columns + column;
    return MoveInventoryUiItem(source_index, destination_slot);
  };

  auto activate = [&](const CustomHit& target, bool held) -> bool {
    switch (target.kind) {
      case CustomHitKind::kInventoryItem:
        // Tap and hold both lift an inventory item. A second tap is a native
        // combine/move drop, which makes the compact grid useful even when the
        // Android touch router emits discrete taps instead of a pointer drag.
        if (selected_item_ >= 0 &&
            !same_identity(selected_item_identity_, identity_from_hit(target))) {
          if (drop_item(selected_item_identity_, target, x, y)) {
            clear_selected_item();
            SendGuestHaptic(GuestHapticCue::kAction);
            content_changed();
          } else {
            chrome_dirty_.store(true, std::memory_order_release);
          }
          return true;
        }
        select_item(identity_from_hit(target));
        SendGuestHaptic(GuestHapticCue::kSelection);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kWeapon:
        if (const InventoryItemSnapshot* weapon =
                find_identity(identity_from_hit(target))) {
          const InventoryActionIdentity weapon_identity =
              identity_from_hit(target);
          if (held) {
            // Reload only a weapon which is actually in Jensen's hands. A hold
            // on any other loadout card lifts it for drag/drop assignment.
            if (weapon->equipped && ReloadEquippedWeapon(weapon_identity)) {
              SendGuestHaptic(GuestHapticCue::kAction);
              content_changed();
            } else {
              select_item(weapon_identity);
              SendGuestHaptic(GuestHapticCue::kSelection);
              chrome_dirty_.store(true, std::memory_order_release);
            }
          } else {
            clear_selected_item();
            const bool activated = weapon->equipped
                ? HolsterEquippedWeapon(weapon_identity)
                : UseInventoryItem(weapon_identity);
            if (activated) {
              SendGuestHaptic(GuestHapticCue::kAction);
              content_changed();
            } else {
              chrome_dirty_.store(true, std::memory_order_release);
            }
          }
        }
        return true;
      case CustomHitKind::kQuickSlot:
        {
        InventoryActionIdentity quick_identity;
        if (target.pickup_id != 0 && target.pickup_id != 0xFFFFFFFFu) {
          const InventoryItemSnapshot* rendered_quick =
              find_pickup(static_cast<int>(target.pickup_id));
          if (rendered_quick &&
              custom_ui_frame_.inventory_identity ==
                  inventory_snapshot_.inventory_identity &&
              custom_ui_frame_.inventory_revision == inventory_snapshot_.revision) {
            quick_identity = identity_from_item(rendered_quick);
          }
        }
        if (held) {
          const InventoryItemSnapshot* quick = find_identity(quick_identity);
          if (quick && quick->equipped &&
              ReloadEquippedWeapon(quick_identity)) {
            SendGuestHaptic(GuestHapticCue::kAction);
            content_changed();
          } else if (quick) {
            select_item(identity_from_item(quick));
            SendGuestHaptic(GuestHapticCue::kSelection);
            chrome_dirty_.store(true, std::memory_order_release);
          }
        } else if (selected_item_ >= 0) {
          const InventoryItemSnapshot* selected =
              find_identity(selected_item_identity_);
          if (selected &&
              SetInventoryQuickSlot(target.value, selected_item_identity_)) {
            clear_selected_item();
            SendGuestHaptic(GuestHapticCue::kAction);
            content_changed();
          } else {
            chrome_dirty_.store(true, std::memory_order_release);
          }
        } else {
          const InventoryItemSnapshot* quick = find_identity(quick_identity);
          const bool consumable = quick &&
              (quick->pickup.category == 8 || quick->pickup.category == 10);
          const bool activated = quick && quick->equipped
              ? HolsterEquippedWeapon(quick_identity)
              : (consumable ? UseInventoryItem(identity_from_item(quick))
                            : ActivateInventoryQuickSlot(target.value,
                                                         quick_identity));
          if (activated) {
            SendGuestHaptic(GuestHapticCue::kAction);
            content_changed();
          } else {
            chrome_dirty_.store(true, std::memory_order_release);
          }
        }
        return true;
        }
      case CustomHitKind::kDiscard:
        // A normal tap is deliberately inert. Destructive input requires
        // either an explicit hold on this target or a dragged live item.
        if (held && selected_item_ >= 0 &&
            DiscardInventoryItem(selected_item_identity_)) {
          clear_selected_item();
          SendGuestHaptic(GuestHapticCue::kAction);
          content_changed();
        } else {
          chrome_dirty_.store(true, std::memory_order_release);
        }
        return true;
      case CustomHitKind::kStatusAugmentation:
        if (core_augmentation_unlocked(target.value, target.secondary)) {
          if (ActivateCoreAugmentation(
                  target.value, custom_ui_frame_.inventory_identity,
                  custom_ui_frame_.inventory_revision)) {
            SendGuestHaptic(GuestHapticCue::kAction);
            content_changed();
          }
          else chrome_dirty_.store(true, std::memory_order_release);
        } else {
          // A locked card is still useful: take the player to the exact native
          // augmentation family where it can be inspected/unlocked.
          selected_aug_ = (std::max)(0, target.secondary);
          selected_aug_node_ = -1;
          constexpr int augmentations_per_page = 24;
          augmentation_page_ = (std::max)(
              0, selected_aug_ / augmentations_per_page);
          ActivateTab(4);
          chrome_dirty_.store(true, std::memory_order_release);
        }
        return true;
      case CustomHitKind::kInventoryPrevious:
        previous_page(inventory_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kInventoryNext:
        next_page(inventory_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kInventorySort:
        if (SortInventoryUiSlots()) {
          SendGuestHaptic(GuestHapticCue::kAction);
        }
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kAugmentation:
        selected_aug_ = target.value;
        selected_aug_node_ = -1;
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kAugmentationNode:
        selected_aug_ = target.value;
        selected_aug_node_ = target.secondary;
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kAugmentationPrevious:
        previous_page(augmentation_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kAugmentationNext:
        next_page(augmentation_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kAugmentationPurchase:
        // Tree and node ids on this page come from the native DTP graph.
        // A validator rejection must fail closed; the retired SmartGlass
        // transport has different semantics and must never retry the action.
        if (PurchaseNativeAugmentation(
                target.value, target.secondary,
                custom_ui_frame_.inventory_identity,
                custom_ui_frame_.inventory_revision)) {
          SendGuestHaptic(GuestHapticCue::kUnlock);
          content_changed();
        } else {
          chrome_dirty_.store(true, std::memory_order_release);
        }
        return true;
      case CustomHitKind::kObjective:
        selected_objective_ = target.value;
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kObjectivePrevious:
        previous_page(objective_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kObjectiveNext:
        next_page(objective_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kObjectiveToggleTracking:
        if (ToggleNativeObjectiveTracking(target.value) ||
            TheSmartGlass().QueueToggleObjectiveTracking(target.value)) {
          content_changed();
        } else {
          chrome_dirty_.store(true, std::memory_order_release);
        }
        return true;
      case CustomHitKind::kMedia:
        selected_media_ = target.value;
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMediaPrevious:
        previous_page(media_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMediaNext:
        next_page(media_page_, target.value);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMapZoomIn:
        map_zoom_ = (std::min)(3.0f, map_zoom_ * 1.18f);
        if (map_follow_player_) {
          CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                  native_map_snapshot_, &map_zoom_,
                                  &map_pan_x_, &map_pan_y_);
        } else {
          clamp_map_view();
        }
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMapZoomOut:
        map_zoom_ = (std::max)(0.55f, map_zoom_ / 1.18f);
        if (map_follow_player_) {
          CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                  native_map_snapshot_, &map_zoom_,
                                  &map_pan_x_, &map_pan_y_);
        } else {
          clamp_map_view();
        }
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMapCenter:
        map_zoom_ = 3.0f;
        map_pan_x_ = 0.0f;
        map_pan_y_ = 0.0f;
        map_floor_manual_ = false;
        map_display_floor_ = native_map_snapshot_.level;
        native_map_snapshot_.display_level = map_display_floor_;
        {
          MapRasterInfo raster;
          if (TheMapRasterCache().Describe(
                  native_map_snapshot_.map_resource_id, map_display_floor_,
                  &raster)) {
            native_map_snapshot_.raster_width = raster.width;
            native_map_snapshot_.raster_height = raster.height;
            native_map_snapshot_.raster_floor = raster.floor;
            native_map_snapshot_.raster_exact_floor = raster.exact_floor;
          }
        }
        map_follow_player_ = true;
        CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                native_map_snapshot_, &map_zoom_,
                                &map_pan_x_, &map_pan_y_);
        chrome_dirty_.store(true, std::memory_order_release);
        return true;
      case CustomHitKind::kMapFloorPrevious:
      case CustomHitKind::kMapFloorNext: {
        const int count = (std::max)(0, (std::min)(
            native_map_snapshot_.floor_count, 65));
        if (count <= 1) return true;
        int selected = 0;
        for (int floor_index = 0; floor_index < count; ++floor_index) {
          if (native_map_snapshot_.floors[floor_index] ==
              native_map_snapshot_.display_level) {
            selected = floor_index;
            break;
          }
        }
        const int delta = target.kind == CustomHitKind::kMapFloorNext ? 1 : -1;
        selected = (selected + delta + count) % count;
        map_display_floor_ = native_map_snapshot_.floors[selected];
        map_floor_manual_ = true;
        native_map_snapshot_.display_level = map_display_floor_;
        MapRasterInfo raster;
        if (TheMapRasterCache().Describe(
                native_map_snapshot_.map_resource_id, map_display_floor_,
                &raster)) {
          native_map_snapshot_.raster_width = raster.width;
          native_map_snapshot_.raster_height = raster.height;
          native_map_snapshot_.raster_floor = raster.floor;
          native_map_snapshot_.raster_exact_floor = raster.exact_floor;
        }
        map_zoom_ = 3.0f;
        map_pan_x_ = 0.0f;
        map_pan_y_ = 0.0f;
        map_follow_player_ = map_display_floor_ == native_map_snapshot_.level;
        if (map_follow_player_) {
          CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                  native_map_snapshot_, &map_zoom_,
                                  &map_pan_x_, &map_pan_y_);
        } else {
          clamp_map_view();
        }
        rendered_model_revision_ = 0;
        chrome_dirty_.store(true, std::memory_order_release);
        DXS_INFO("[map] lower screen selected authored floor %d (%d/%d)",
                 map_display_floor_, selected + 1, count);
        return true;
      }
      case CustomHitKind::kMap:
        return true;
      case CustomHitKind::kInventoryGrid:
        if (selected_item_ >= 0 &&
            drop_item(selected_item_identity_, target, x, y)) {
          clear_selected_item();
          SendGuestHaptic(GuestHapticCue::kAction);
          content_changed();
        }
        return true;
      case CustomHitKind::kNone:
      default:
        return false;
    }
  };

  if (action == TouchAction::kPinch) {
    if (active_tab_.load(std::memory_order_acquire) == 1 &&
        std::isfinite(value) && value > 0.0f) {
      map_zoom_ = (std::max)(0.55f, (std::min)(3.0f, map_zoom_ * value));
      if (map_follow_player_) {
        CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                native_map_snapshot_, &map_zoom_,
                                &map_pan_x_, &map_pan_y_);
      } else {
        clamp_map_view();
      }
      chrome_dirty_.store(true, std::memory_order_release);
    }
    return true;  // lower-panel gestures never leak into gameplay
  }

  if (action == TouchAction::kTap || action == TouchAction::kHold) {
    activate(hit, action == TouchAction::kHold);
    if (action == TouchAction::kHold && touch_button_down_) {
      // Android reports a hold before the physical finger-up. Mark that
      // gesture consumed so the subsequent Up cannot equip/eat/clear twice.
      touch_dragged_ = true;
      pressed_hit_ = CustomHit{};
      pressed_item_ = -1;
      pressed_item_identity_ = InventoryActionIdentity{};
      pressed_quick_slot_ = -1;
      drag_item_ = -1;
      drag_item_identity_ = InventoryActionIdentity{};
    }
    return true;
  }

  if (action == TouchAction::kDown) {
    pressed_hit_ = hit;
    pressed_item_ = (hit.kind == CustomHitKind::kInventoryItem ||
                     hit.kind == CustomHitKind::kWeapon) ? hit.value : -1;
    pressed_item_identity_ = pressed_item_ >= 0
        ? identity_from_hit(hit) : InventoryActionIdentity{};
    if (hit.kind == CustomHitKind::kQuickSlot) {
      if (custom_ui_frame_.inventory_identity ==
              inventory_snapshot_.inventory_identity &&
          custom_ui_frame_.inventory_revision == inventory_snapshot_.revision &&
          hit.pickup_id != 0 && hit.pickup_id != 0xFFFFFFFFu) {
        const InventoryItemSnapshot* quick =
            find_pickup(static_cast<int>(hit.pickup_id));
        if (quick) {
          pressed_item_ = quick->item_index;
          pressed_item_identity_ = identity_from_item(quick);
        }
      }
    }
    // Selection happens on Up. Preselecting here made an ordinary down/up
    // toggle the same item off again, so tap-then-quick-slot looked inert.
    pressed_quick_slot_ = hit.kind == CustomHitKind::kQuickSlot ? hit.value : -1;
    drag_item_ = -1;
    drag_item_identity_ = InventoryActionIdentity{};
    touch_button_down_ = true;
    touch_dragged_ = false;
    touch_down_x_ = touch_last_x_ = static_cast<float>(x);
    touch_down_y_ = touch_last_y_ = static_cast<float>(y);
    return true;
  }

  if (action == TouchAction::kMove) {
    if (!touch_button_down_) return true;
    const float dx = static_cast<float>(x) - touch_last_x_;
    const float dy = static_cast<float>(y) - touch_last_y_;
    const float total_x = static_cast<float>(x) - touch_down_x_;
    const float total_y = static_cast<float>(y) - touch_down_y_;
    if (std::fabs(total_x) + std::fabs(total_y) > 8.0f) touch_dragged_ = true;
    if (pressed_hit_.kind == CustomHitKind::kMap) {
      map_follow_player_ = false;
      map_pan_x_ += dx;
      map_pan_y_ += dy;
      clamp_map_view();
      chrome_dirty_.store(true, std::memory_order_release);
    } else if (pressed_item_ >= 0 && touch_dragged_) {
      drag_item_ = pressed_item_;
      drag_item_identity_ = pressed_item_identity_;
      chrome_dirty_.store(true, std::memory_order_release);
    }
    touch_last_x_ = static_cast<float>(x);
    touch_last_y_ = static_cast<float>(y);
    return true;
  }

  if (action == TouchAction::kUp) {
    if (!touch_button_down_) return true;
    bool changed = false;
    const float swipe_y = static_cast<float>(y) - touch_down_y_;
    if (touch_dragged_ && std::fabs(swipe_y) > 32.0f &&
        (pressed_hit_.kind == CustomHitKind::kObjective ||
         pressed_hit_.kind == CustomHitKind::kMedia ||
         pressed_hit_.kind == CustomHitKind::kAugmentation)) {
      const int direction = swipe_y < 0.0f ? 1 : -1;
      const int maximum_page = (std::max)(0, pressed_hit_.secondary - 1);
      if (pressed_hit_.kind == CustomHitKind::kObjective) {
        objective_page_ = (std::max)(0, (std::min)(maximum_page,
                                                   objective_page_ + direction));
      } else if (pressed_hit_.kind == CustomHitKind::kMedia) {
        media_page_ = (std::max)(0, (std::min)(maximum_page,
                                               media_page_ + direction));
      } else {
        augmentation_page_ = (std::max)(0, (std::min)(maximum_page,
                                                      augmentation_page_ + direction));
      }
      chrome_dirty_.store(true, std::memory_order_release);
    } else if (drag_item_ >= 0) {
      changed = drop_item(drag_item_identity_, hit, x, y);
      if (changed) {
        clear_selected_item();
        SendGuestHaptic(GuestHapticCue::kAction);
        content_changed();
      } else {
        select_item(drag_item_identity_);
        chrome_dirty_.store(true, std::memory_order_release);
      }
    } else if (!touch_dragged_ && hit.kind == pressed_hit_.kind &&
               hit.value == pressed_hit_.value &&
               hit.secondary == pressed_hit_.secondary &&
               hit.pickup_id == pressed_hit_.pickup_id) {
      activate(hit, false);
    }

    reset_touch();
    return true;
  }
  return true;
}

void Panel::RefreshLiveModels() {
  const uint64_t now = GetTickCount64();
  const int active_page = active_tab_.load(std::memory_order_acquire);

  // Jensen's pose is tiny and allocation-free, unlike a complete map/radar
  // refresh. Update it at dashboard cadence so turning in place rotates the
  // directional arrow promptly without walking static markers every frame.
  constexpr uint64_t kMapPoseIntervalMs = 75;
  if (active_page == 1 && native_map_snapshot_.valid &&
      consecutive_map_capture_failures_ == 0 &&
      (!last_map_pose_poll_ms_ ||
       now - last_map_pose_poll_ms_ >= kMapPoseIntervalMs)) {
    last_map_pose_poll_ms_ = now;
    native_map_staging_ = native_map_snapshot_;
    if (RefreshNativePlayerMapPose(&native_map_staging_)) {
      float heading_delta = std::fabs(native_map_staging_.player_heading_degrees -
                                      native_map_snapshot_.player_heading_degrees);
      heading_delta = (std::min)(heading_delta, 360.0f - heading_delta);
      const bool moved =
          std::fabs(native_map_staging_.player_x - native_map_snapshot_.player_x) >=
              (1.0f / 4096.0f) ||
          std::fabs(native_map_staging_.player_y - native_map_snapshot_.player_y) >=
              (1.0f / 4096.0f);
      const bool turned =
          native_map_staging_.player_heading_valid !=
              native_map_snapshot_.player_heading_valid ||
          (native_map_staging_.player_heading_valid && heading_delta >= 0.25f);
      native_map_snapshot_ = native_map_staging_;
      if (moved || turned) {
        if (moved && map_follow_player_) {
          CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                  native_map_snapshot_, &map_zoom_,
                                  &map_pan_x_, &map_pan_y_);
        }
        chrome_dirty_.store(true, std::memory_order_release);
      }
    }
  }

  if (last_model_poll_ms_ && now - last_model_poll_ms_ < 250) return;
  last_model_poll_ms_ = now;

  if (CaptureInventorySnapshot(&inventory_staging_) &&
      inventory_staging_.valid) {
    inventory_unavailable_since_ms_ = 0;
    consecutive_inventory_capture_failures_ = 0;
    inventory_snapshot_ = inventory_staging_;
    bool equipped_weapon = false;
    for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
      if (inventory_snapshot_.items[item].equipped) {
        equipped_weapon = true;
        break;
      }
    }
    weapon_equipped_.store(equipped_weapon, std::memory_order_release);
    ReconcileInventoryUiSlots();
    if (selected_item_ >= 0) {
      bool still_present =
          inventory_snapshot_.inventory_identity ==
              selected_item_identity_.inventory &&
          inventory_snapshot_.revision == selected_item_identity_.revision;
      if (still_present) {
        still_present = false;
        for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
          const InventoryItemSnapshot& live = inventory_snapshot_.items[item];
          if (live.item_index == selected_item_identity_.item_index &&
              live.pickup.pickup_id == selected_item_identity_.pickup_id) {
            still_present = true;
            break;
          }
        }
      }
      if (!still_present) {
        selected_item_ = -1;
        selected_item_identity_ = InventoryActionIdentity{};
      }
    }

    // Live save data is streamed independently. Never carry a selection into
    // a different/shorter augmentation catalog; doing so used to leave an
    // invisible selected family or node after load and could dispatch a stale
    // purchase ID through an otherwise valid touch target.
    const int augmentation_count = (std::max)(0, (std::min)(
        kMaximumAugmentationTrees, inventory_snapshot_.augmentation_count));
    if (augmentation_count == 0) {
      selected_aug_ = 0;
      selected_aug_node_ = -1;
      augmentation_page_ = 0;
    } else {
      selected_aug_ = (std::max)(0, (std::min)(selected_aug_,
                                               augmentation_count - 1));
      const AugmentationTreeSnapshot& selected_tree =
          inventory_snapshot_.augmentations[selected_aug_];
      const int node_count = (std::max)(0, (std::min)(
          kMaximumAugmentationNodesPerTree, selected_tree.node_count));
      bool node_exists = selected_aug_node_ < 0;
      for (int node = 0; node < node_count && !node_exists; ++node) {
        node_exists = selected_tree.nodes[node].node_index ==
                      selected_aug_node_;
      }
      if (!node_exists) selected_aug_node_ = -1;
      const int augmentation_page_count = (std::max)(
          1, (augmentation_count + 23) / 24);
      augmentation_page_ = (std::max)(0, (std::min)(
          augmentation_page_, augmentation_page_count - 1));
    }
  } else {
    if (inventory_unavailable_since_ms_ == 0) {
      inventory_unavailable_since_ms_ = now;
    }
    ++consecutive_inventory_capture_failures_;
    // A brief streaming gap may miss one or two polls. After a full second,
    // however, the old snapshot may belong to a player/save which no longer
    // exists. Remove its hit targets before a delayed finger-up can dispatch a
    // stale item or augmentation ID into the newly attached game state.
    constexpr uint32_t kStaleModelFailureLimit = 4;
    if (consecutive_inventory_capture_failures_ == kStaleModelFailureLimit) {
      DXS_WARN("[model] native inventory unavailable for four polls; "
               "retiring stale lower-screen model and touch targets");
      ResetNativeInventorySnapshotReady();
      weapon_equipped_.store(false, std::memory_order_release);
      inventory_snapshot_.~InventorySnapshot();
      new (&inventory_snapshot_) InventorySnapshot();
      selected_item_ = -1;
      selected_item_identity_ = InventoryActionIdentity{};
      pressed_item_ = -1;
      pressed_item_identity_ = InventoryActionIdentity{};
      pressed_quick_slot_ = -1;
      drag_item_ = -1;
      drag_item_identity_ = InventoryActionIdentity{};
      inventory_ui_slots_.clear();
      inventory_ui_pickups_.clear();
      pressed_hit_ = CustomHit{};
      touch_button_down_ = false;
      touch_dragged_ = false;
      custom_ui_frame_.hits.clear();
      custom_ui_frame_.hit_rects.clear();
      rendered_model_revision_ = 0;
      chrome_dirty_.store(true, std::memory_order_release);
    }
    constexpr uint64_t kUnloadEvidenceMs = 5000;
    if (now - inventory_unavailable_since_ms_ >= kUnloadEvidenceMs &&
        menu_input_locked_.load(std::memory_order_acquire)) {
      SetGameLoaded(false,
                    "sustained player inventory detachment during menu/load");
      return;
    }
  }
  SmartGlassSnapshot next_smartglass = TheSmartGlass().Snapshot();
  if (active_page == 2 || active_page == 5) {
    MergeNativeJournalSnapshot(&next_smartglass, active_page == 2,
                               active_page == 5);
  }
  smartglass_snapshot_ = std::move(next_smartglass);
  if (smartglass_snapshot_.objectives.empty()) {
    selected_objective_ = 0;
    objective_page_ = 0;
  } else {
    selected_objective_ = (std::max)(0, (std::min)(
        selected_objective_,
        static_cast<int>(smartglass_snapshot_.objectives.size()) - 1));
  }
  if (smartglass_snapshot_.media.empty()) {
    selected_media_ = 0;
    media_page_ = 0;
  } else {
    selected_media_ = (std::max)(0, (std::min)(
        selected_media_,
        static_cast<int>(smartglass_snapshot_.media.size()) - 1));
  }

  // The vanilla HUD radar has already filtered actors for visibility and
  // attitude. Poll it only while this page can be seen: inventory/status pages
  // pay no cost for moving contacts, and map updates remain outside the
  // per-frame rendering hot path.
  if (active_tab_.load(std::memory_order_acquire) == 1 &&
      (!last_map_poll_ms_ || now - last_map_poll_ms_ >= 250)) {
    last_map_poll_ms_ = now;
    if (CaptureNativeMapSnapshot(&native_map_staging_) &&
        native_map_staging_.valid) {
      consecutive_map_capture_failures_ = 0;
      const bool map_changed = map_floor_map_id_ !=
          native_map_staging_.map_resource_id;
      native_map_staging_.floor_count = TheMapRasterCache().AvailableFloors(
          native_map_staging_.map_resource_id, native_map_staging_.floors,
          static_cast<int>(sizeof(native_map_staging_.floors) /
                           sizeof(native_map_staging_.floors[0])));
      if (native_map_staging_.floor_count <= 0) {
        native_map_staging_.floor_count = 1;
        native_map_staging_.floors[0] = native_map_staging_.level;
      }
      if (map_changed || !map_floor_manual_) {
        map_display_floor_ = native_map_staging_.level;
        map_floor_manual_ = false;
      }
      bool selected_floor_exists = false;
      for (int floor_index = 0;
           floor_index < native_map_staging_.floor_count; ++floor_index) {
        if (native_map_staging_.floors[floor_index] == map_display_floor_) {
          selected_floor_exists = true;
          break;
        }
      }
      if (!selected_floor_exists) {
        map_display_floor_ = native_map_staging_.level;
        map_floor_manual_ = false;
      }
      map_floor_map_id_ = native_map_staging_.map_resource_id;
      native_map_staging_.display_level = map_display_floor_;
      MapRasterInfo raster;
      if (TheMapRasterCache().Describe(native_map_staging_.map_resource_id,
                                       native_map_staging_.display_level,
                                       &raster)) {
        native_map_staging_.raster_width = raster.width;
        native_map_staging_.raster_height = raster.height;
        native_map_staging_.raster_floor = raster.floor;
        native_map_staging_.raster_exact_floor = raster.exact_floor;
      }
      native_map_snapshot_ = native_map_staging_;
      if (map_changed) {
        map_zoom_ = 3.0f;
        map_pan_x_ = 0.0f;
        map_pan_y_ = 0.0f;
        map_follow_player_ = true;
      }
      if (map_follow_player_) {
        CenterCustomMapOnPlayer(width_, height_, TheLayout().HeaderHeight(),
                                native_map_snapshot_, &map_zoom_,
                                &map_pan_x_, &map_pan_y_);
      }
    } else {
      ++consecutive_map_capture_failures_;
      constexpr uint32_t kStaleMapFailureLimit = 4;
      if (consecutive_map_capture_failures_ == kStaleMapFailureLimit &&
          native_map_snapshot_.valid) {
        DXS_WARN("[map] native map unavailable for four polls; "
                 "retiring stale floor, markers, and player pose");
        native_map_snapshot_.~NativeMapSnapshot();
        new (&native_map_snapshot_) NativeMapSnapshot();
        map_pan_x_ = 0.0f;
        map_pan_y_ = 0.0f;
        map_zoom_ = 3.0f;
        map_follow_player_ = true;
        map_floor_map_id_ = 0;
        map_display_floor_ = 0;
        map_floor_manual_ = false;
        last_map_pose_poll_ms_ = 0;
        rendered_model_revision_ = 0;
        chrome_dirty_.store(true, std::memory_order_release);
      }
    }
  }

  if (last_model_diagnostic_ms_ == 0 ||
      now - last_model_diagnostic_ms_ >= 15000) {
    last_model_diagnostic_ms_ = now;
    int equipped_magazine = -1;
    int equipped_capacity = -1;
    for (int item = 0; item < inventory_snapshot_.item_count; ++item) {
      const InventoryItemSnapshot& candidate = inventory_snapshot_.items[item];
      if (candidate.equipped && candidate.magazine_state_valid) {
        equipped_magazine = candidate.magazine_ammo;
        equipped_capacity = candidate.magazine_capacity;
        break;
      }
    }
    DXS_INFO("[model] inventory valid=%d items=%d grid=%dx%d credits=%u "
             "health=%.0f/%.0f energy=%.0f/%.0f xp=%d next=%d praxis=%d; "
             "equippedMag=%d/%d; "
             "sgui valid=%d raw=%u items=%u "
             "energy=%.0f/%.0f xp=%d praxis=%d credits=%d; "
             "objectives=%u media=%u map=%d id=%u floor=%d view=%d "
             "floors=%d markers=%d missionPins=%d "
             "raster=%ux%u player=%.4f,%.4f heading=%.1f valid=%d",
             inventory_snapshot_.valid ? 1 : 0,
             inventory_snapshot_.item_count,
             inventory_snapshot_.grid_width,
             inventory_snapshot_.grid_height,
             inventory_snapshot_.credits,
             inventory_snapshot_.health,
             inventory_snapshot_.max_health,
             inventory_snapshot_.energy,
             inventory_snapshot_.max_energy,
             inventory_snapshot_.total_xp,
             inventory_snapshot_.next_praxis,
             inventory_snapshot_.praxis_points,
             equipped_magazine,
             equipped_capacity,
             smartglass_snapshot_.valid ? 1 : 0,
             static_cast<unsigned>(smartglass_snapshot_.raw_messages.size()),
             static_cast<unsigned>(smartglass_snapshot_.inventory_items.size()),
             smartglass_snapshot_.status.energy,
             smartglass_snapshot_.status.max_energy,
             smartglass_snapshot_.status.total_xp,
             smartglass_snapshot_.status.praxis_points,
             smartglass_snapshot_.status.credits,
             static_cast<unsigned>(smartglass_snapshot_.objectives.size()),
             static_cast<unsigned>(smartglass_snapshot_.media.size()),
             native_map_snapshot_.valid ? 1 : 0,
             static_cast<unsigned>(native_map_snapshot_.map_resource_id),
             native_map_snapshot_.level,
             native_map_snapshot_.display_level,
             native_map_snapshot_.floor_count,
             native_map_snapshot_.marker_count,
             native_map_snapshot_.objective_marker_count,
             static_cast<unsigned>(native_map_snapshot_.raster_width),
             static_cast<unsigned>(native_map_snapshot_.raster_height),
             native_map_snapshot_.player_x,
             native_map_snapshot_.player_y,
             native_map_snapshot_.player_heading_degrees,
             native_map_snapshot_.player_heading_valid ? 1 : 0);
  }

  // The game's inventory revision does not change when Jensen merely takes
  // damage or fires the equipped weapon. Include every live presentation
  // field which can change without a container mutation; otherwise the fresh
  // magazine snapshot is copied above but the existing GDI frame continues to
  // show the value captured when the weapon was full.
  auto quantize = [](double value) -> uint64_t {
    return value >= 0.0 && std::isfinite(value)
        ? static_cast<uint64_t>(std::llround(value * 16.0)) : 0;
  };
  uint64_t revision = smartglass_snapshot_.revision;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.revision) << 32;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.credits) * 0x9E3779B185EBCA87ull;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.item_count) << 17;
  revision ^= quantize(inventory_snapshot_.health) << 1;
  revision ^= quantize(inventory_snapshot_.max_health) << 13;
  revision ^= quantize(inventory_snapshot_.energy) << 7;
  revision ^= quantize(inventory_snapshot_.max_energy) << 19;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.total_xp + 1) << 23;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.next_praxis + 1) << 37;
  revision ^= static_cast<uint64_t>(inventory_snapshot_.praxis_points + 1) << 51;
  auto mix_inventory_revision = [&revision](uint64_t value) {
    revision ^= value + 0x9E3779B97F4A7C15ull + (revision << 6) +
                (revision >> 2);
  };
  const int inventory_item_count = (std::max)(
      0, (std::min)(static_cast<int>(
                        sizeof(inventory_snapshot_.items) /
                        sizeof(inventory_snapshot_.items[0])),
                    inventory_snapshot_.item_count));
  for (int index = 0; index < inventory_item_count; ++index) {
    const InventoryItemSnapshot& item = inventory_snapshot_.items[index];
    mix_inventory_revision(
        (static_cast<uint64_t>(item.pickup.pickup_id) << 32) |
        (static_cast<uint64_t>(static_cast<uint16_t>(item.item_index)) << 16) |
        static_cast<uint16_t>(item.count));
    mix_inventory_revision(
        (static_cast<uint64_t>(static_cast<uint16_t>(
             item.magazine_ammo + 1)) << 48) |
        (static_cast<uint64_t>(static_cast<uint16_t>(
             item.magazine_capacity + 1)) << 32) |
        (static_cast<uint64_t>(item.ammo_upgrade_level) << 16) |
        (item.magazine_state_valid ? 1ull : 0ull) |
        (item.equipped ? 2ull : 0ull) |
        (item.rotated ? 4ull : 0ull));
  }
  if (inventory_snapshot_.augmentation_state_valid) {
    revision ^= 0xA631B4D20F19C85Dull;
    for (int ability = 0; ability < kCoreAugmentationCount; ++ability) {
      if (inventory_snapshot_.augmentation_unlocked[ability]) {
        revision ^= 0x1000000000000000ull >> ability;
      }
    }
  }
  if (inventory_snapshot_.augmentation_active_valid) {
    revision ^= 0x51B78A2D9C6E403Full;
    for (int ability = 0; ability < kCoreAugmentationCount; ++ability) {
      if (inventory_snapshot_.augmentation_active[ability]) {
        revision ^= 0x0800000000000000ull >> ability;
      }
    }
  }
  if (inventory_snapshot_.augmentation_catalog_valid) {
    auto mix_augmentation_revision = [&revision](uint64_t value) {
      revision ^= value + 0x9E3779B97F4A7C15ull + (revision << 6) +
                  (revision >> 2);
    };
    mix_augmentation_revision(static_cast<uint64_t>(
        inventory_snapshot_.augmentation_count + 1) *
        0xD6E8FEB86659FD93ull);
    for (int index = 0; index < inventory_snapshot_.augmentation_count; ++index) {
      const AugmentationTreeSnapshot& augmentation =
          inventory_snapshot_.augmentations[index];
      mix_augmentation_revision(
          (static_cast<uint64_t>(augmentation.tree_id) << 32) |
          static_cast<uint32_t>(index + 1));
      mix_augmentation_revision(
          (static_cast<uint64_t>(augmentation.installed_nodes + 1) << 48) |
          (static_cast<uint64_t>(augmentation.node_count + 1) << 32) |
          (static_cast<uint64_t>(augmentation.next_node_index + 2) << 16) |
          static_cast<uint16_t>(augmentation.next_praxis_cost + 2));
      mix_augmentation_revision(
          (static_cast<uint64_t>(augmentation.state) << 8) |
          (augmentation.installed ? 1ull : 0ull) |
          (augmentation.tree_present ? 2ull : 0ull) |
          (augmentation.purchase_available ? 4ull : 0ull) |
          (augmentation.affordable ? 8ull : 0ull));
      for (int node_index = 0; node_index < augmentation.node_count;
           ++node_index) {
        const AugmentationNodeSnapshot& node =
            augmentation.nodes[node_index];
        mix_augmentation_revision(
            (static_cast<uint64_t>(node.node_index + 1) << 40) |
            (static_cast<uint64_t>(node.praxis_cost + 1) << 24) |
            (static_cast<uint64_t>(node.state) << 16) |
            (static_cast<uint64_t>(node.tier & 0xff) << 8) |
            static_cast<uint64_t>(node.order_in_tier & 0xff));
        mix_augmentation_revision(
            (node.prerequisites_met ? 1ull : 0ull) |
            (node.graph_available ? 2ull : 0ull) |
            (node.affordable ? 4ull : 0ull) |
            (static_cast<uint64_t>(node.prerequisite_count & 0xff) << 8));
      }
    }
  }
  if (native_map_snapshot_.valid) {
    auto map_quantize = [](float value) -> uint64_t {
      return std::isfinite(value)
          ? static_cast<uint64_t>(std::llround(
                (std::max)(0.0f, (std::min)(1.0f, value)) * 1024.0f))
          : 0;
    };
    revision ^= static_cast<uint64_t>(native_map_snapshot_.map_resource_id + 1) *
                0xA24BAED4963EE407ull;
    revision ^= static_cast<uint64_t>(native_map_snapshot_.level + 33) << 41;
    revision ^= static_cast<uint64_t>(
                    native_map_snapshot_.display_level + 33) << 49;
    revision ^= static_cast<uint64_t>(
                    native_map_snapshot_.floor_count + 1) *
                0x27D4EB2F165667C5ull;
    revision ^= map_quantize(native_map_snapshot_.player_x) << 5;
    revision ^= map_quantize(native_map_snapshot_.player_y) << 27;
    if (native_map_snapshot_.player_heading_valid) {
      const uint64_t heading = static_cast<uint64_t>(std::llround(
          native_map_snapshot_.player_heading_degrees * 4.0f));
      revision ^= (heading + 1) * 0xC2B2AE3D27D4EB4Full;
    }
    revision ^= static_cast<uint64_t>(native_map_snapshot_.raster_width + 1) << 11;
    revision ^= static_cast<uint64_t>(native_map_snapshot_.raster_height + 1) << 43;
    const uint64_t map_flags =
        (native_map_snapshot_.player_valid ? 1ull : 0ull) |
        (native_map_snapshot_.player_heading_valid ? 2ull : 0ull) |
        (native_map_snapshot_.radar_live ? 4ull : 0ull) |
        (native_map_snapshot_.raster_exact_floor ? 8ull : 0ull);
    revision ^= (map_flags + 1ull) * 0x165667B19E3779F9ull;
    revision ^= static_cast<uint64_t>(
                    native_map_snapshot_.raster_floor + 65) *
                0x85EBCA77C2B2AE63ull;
    revision ^= static_cast<uint64_t>(native_map_snapshot_.marker_count + 1) *
                0x9FB21C651E98DF25ull;
    for (int index = 0; index < native_map_snapshot_.marker_count; ++index) {
      const NativeMapMarker& marker = native_map_snapshot_.markers[index];
      const uint64_t identity = marker.entity_key
          ? static_cast<uint64_t>(marker.entity_key)
          : static_cast<uint64_t>(index + 1);
      revision ^= (identity * 0xD6E8FEB86659FD93ull) >> (index % 17);
      revision ^= static_cast<uint64_t>(marker.kind) << (index % 37);
      revision ^= map_quantize(marker.normalized_x) << (index % 19);
      revision ^= map_quantize(marker.normalized_y) << ((index + 11) % 23);
    }
    revision ^= static_cast<uint64_t>(
                    native_map_snapshot_.objective_marker_count + 1) *
                0x94D049BB133111EBull;
    const int objective_count = (std::max)(0, (std::min)(
        native_map_snapshot_.objective_marker_count,
        kMaximumNativeObjectiveMarkers));
    for (int index = 0; index < objective_count; ++index) {
      const NativeObjectiveMarker& marker =
          native_map_snapshot_.objective_markers[index];
      revision ^= static_cast<uint64_t>(marker.mission_id + 1) *
                  0xBF58476D1CE4E5B9ull;
      revision ^= map_quantize(marker.normalized_x) << (index % 19);
      revision ^= map_quantize(marker.normalized_y) << ((index + 7) % 23);
      revision ^= static_cast<uint64_t>(marker.level + 33) <<
                  ((index + 3) % 37);
      if (marker.tracked) revision ^= 0xD6E8FEB86659FD93ull >> (index % 13);
    }
  }
  if (revision != rendered_model_revision_) {
    rendered_model_revision_ = revision;
    chrome_dirty_.store(true, std::memory_order_release);
  }
}

void Panel::DrawNativeUiIcons() {
  if (custom_ui_frame_.icons.empty()) {
    native_icon_retry_ms_ = 0;
    native_icon_retry_delay_ms_ = 1000;
    return;
  }
  const uint64_t now = GetTickCount64();
  if (native_artwork_ready_ms_ != 0 && now < native_artwork_ready_ms_) {
    if (!native_artwork_delay_reported_) {
      native_artwork_delay_reported_ = true;
      DXS_INFO("[ui] native item artwork deferred for save-load streaming safety");
    }
    native_icon_retry_ms_ = native_artwork_ready_ms_;
    return;
  }
  ID3D11RenderTargetView* view = nullptr;
  {
    std::lock_guard<std::mutex> guard(rtv_lock_);
    view = chrome_rtv_;
    if (view) view->AddRef();
  }
  if (!view) return;
  NativeUiRenderer& renderer = TheNativeUiRenderer();
  int drawn = 0;
  if (renderer.Begin(view, static_cast<float>(width_), static_cast<float>(height_))) {
    for (const CustomUiIcon& icon : custom_ui_frame_.icons) {
      bool rendered = false;
      switch (icon.kind) {
        case CustomUiIcon::Kind::kMapRaster: {
          MapRasterTexture map_texture;
          if (TheMapRasterCache().Acquire(device_, icon.id, icon.secondary,
                                          &map_texture) &&
              map_texture.view) {
            rendered = renderer.DrawView(map_texture.view, icon.rect,
                                         icon.color, icon.uv);
            map_texture.view->Release();
          }
          break;
        }
        case CustomUiIcon::Kind::kCircle:
          rendered = renderer.DrawCircle(icon.rect, icon.color);
          break;
        case CustomUiIcon::Kind::kDiamond:
          rendered = renderer.DrawDiamond(icon.rect, icon.color);
          break;
        case CustomUiIcon::Kind::kPlayerArrow:
          rendered = renderer.DrawPlayerArrow(icon.rect,
                                              icon.rotation_degrees,
                                              icon.color);
          break;
        case CustomUiIcon::Kind::kLock:
          rendered = renderer.DrawLock(icon.rect, icon.color);
          break;
        case CustomUiIcon::Kind::kPickup:
          rendered = renderer.DrawPickup(icon.id, icon.rotated, icon.rect,
                                         icon.color);
          break;
        case CustomUiIcon::Kind::kTexture:
          rendered = renderer.DrawTexture(icon.id, icon.rect, icon.color,
                                          icon.uv);
          break;
      }
      drawn += rendered ? 1 : 0;
    }
    if (!renderer.End()) drawn = 0;
  }
  view->Release();
  const int requested = static_cast<int>(custom_ui_frame_.icons.size());
  if (drawn < requested) {
    // A live-only resource can become resident after a streaming transition.
    // Retry the completed chrome page with bounded exponential backoff instead
    // of resolving every missing icon on every Present forever.
    native_icon_retry_ms_ = now + native_icon_retry_delay_ms_;
    native_icon_retry_delay_ms_ = (std::min)(
        30000u, native_icon_retry_delay_ms_ * 2u);
  } else {
    native_icon_retry_ms_ = 0;
    native_icon_retry_delay_ms_ = 1000;
  }
  if ((requested != reported_icon_requests_ || drawn != reported_icon_draws_) &&
      (last_icon_diagnostic_ms_ == 0 ||
       now - last_icon_diagnostic_ms_ >= 1000)) {
    reported_icon_requests_ = requested;
    reported_icon_draws_ = drawn;
    last_icon_diagnostic_ms_ = now;
    DXS_INFO("[ui] native artwork drew %d/%d requested layer(s)",
             drawn, requested);
  }
}

void Panel::OnDisplayChanged() {
  if (!window_) return;
  RECT rect;
  ZeroMemory(&rect, sizeof(rect));
  ResolveRect(&rect);
  const int w = rect.right - rect.left;
  const int h = rect.bottom - rect.top;
  if (w <= 0 || h <= 0) {
    DXS_WARN("[panel] ignoring an invalid display geometry %dx%d", w, h);
    return;
  }
  RECT current;
  ZeroMemory(&current, sizeof(current));
  GetWindowRect(window_, &current);
  const bool moved = current.left != rect.left || current.top != rect.top;
  const bool resized = w != width_ || h != height_;
  if (!moved && !resized) return;

  DXS_INFO("[panel] geometry changed: (%ld,%ld) %dx%d -> (%ld,%ld) %dx%d",
           current.left, current.top, width_, height_, rect.left, rect.top, w, h);
  SetWindowPos(window_, nullptr, rect.left, rect.top, w, h,
               SWP_NOACTIVATE | SWP_NOZORDER);
  if (!resized) return;

  width_ = w; height_ = h;
  ReleaseSurface();
  if (!CreateSurface()) {
    DXS_ERR("[panel] could not rebuild the surface after a display change");
    surface_retry_ms_ = GetTickCount64() + 1000;
  }
}

void Panel::Clear() {
  ID3D11RenderTargetView* view = AcquireRtv();
  if (!view || !context_) {
    if (view) view->Release();
    return;
  }
  // Deus Ex's own near-black, not pure black: the interface is drawn to sit on
  // this and pure black loses the panel borders against it.
  const float ink[4] = {0.043f, 0.039f, 0.031f, 1.0f};
  context_->ClearRenderTargetView(view, ink);
  view->Release();
}

bool Panel::Present() {
  if (!swapchain_) {
    const uint64_t now = GetTickCount64();
    if (!window_ || !factory_ || !device_ ||
        (surface_retry_ms_ != 0 && now < surface_retry_ms_)) {
      return false;
    }
    if (!CreateSurface()) {
      surface_retry_ms_ = now + 1000;
      return false;
    }
    DXS_INFO("[panel] recovered the lower-screen rendering surface");
  }
  const uint64_t window_now = GetTickCount64();
  if (!retired_windows_.empty() &&
      (retired_window_retry_ms_ == 0 ||
       window_now - retired_window_retry_ms_ >= 1000)) {
    retired_window_retry_ms_ = window_now;
    auto retired = retired_windows_.begin();
    while (retired != retired_windows_.end()) {
      if (!IsWindow(*retired)) {
        retired = retired_windows_.erase(retired);
        continue;
      }
      ShowWindowAsync(*retired, SW_HIDE);
      DWORD_PTR ignored = 0;
      SendMessageTimeoutW(*retired, kDestroyPanelWindowMessage, 0, 0,
                          SMTO_ABORTIFHUNG | SMTO_BLOCK, 25, &ignored);
      if (!IsWindow(*retired)) {
        retired = retired_windows_.erase(retired);
      } else {
        ++retired;
      }
    }
  }
  // The game may pump messages only for its own HWND. Service this window's
  // queue here so moving displays or a desktop test never leaves the panel (and
  // eventually the process) marked as hung.
  MSG message;
  while (PeekMessageW(&message, window_, 0, 0, PM_REMOVE)) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }

  ApplyGameLoadedTransition();

  // Keep the claimed blank buffer without wasting an auxiliary Present every
  // frame or exposing custom chrome before gameplay begins. If DXVK's first
  // non-blocking attempt raced WEMU's SurfaceControl attachment, retry until
  // one buffer really lands instead of treating WAS_STILL_DRAWING as success.
  if (!game_loaded()) {
    if (blank_frame_presented_) return true;
    last_error_ = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    if (last_error_ == DXGI_ERROR_WAS_STILL_DRAWING) return true;
    blank_frame_presented_ = SUCCEEDED(last_error_);
    if (blank_frame_presented_) {
      ++presents_;
      DXS_INFO("[panel] blank pre-game frame reached the lower display");
    }
    return blank_frame_presented_;
  }

  RefreshLiveModels();
  // RefreshLiveModels can discover a sustained save/menu detachment after
  // this Present began. Apply that transition immediately so one stale
  // interactive frame cannot be submitted after the gameplay gate closes.
  if (!game_loaded()) {
    ApplyGameLoadedTransition();
    last_error_ = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
    if (last_error_ == DXGI_ERROR_WAS_STILL_DRAWING) return true;
    blank_frame_presented_ = SUCCEEDED(last_error_);
    if (blank_frame_presented_) ++presents_;
    return blank_frame_presented_;
  }

  const uint64_t now = GetTickCount64();
  if (native_icon_retry_ms_ != 0 && now >= native_icon_retry_ms_) {
    native_icon_retry_ms_ = 0;
    chrome_dirty_.store(true, std::memory_order_release);
  }
  if (active_tab_.load(std::memory_order_acquire) == 3 &&
      (selected_item_ >= 0 || drag_item_ >= 0) &&
      (!last_drag_animation_ms_ || now - last_drag_animation_ms_ >= 120)) {
    last_drag_animation_ms_ = now;
    chrome_dirty_.store(true, std::memory_order_release);
  }
  if (active_tab_.load(std::memory_order_acquire) == 1) {
    bool tracked_objective = false;
    const int count = (std::max)(0, (std::min)(
        native_map_snapshot_.objective_marker_count,
        kMaximumNativeObjectiveMarkers));
    for (int index = 0; index < count; ++index) {
      if (native_map_snapshot_.objective_markers[index].tracked) {
        tracked_objective = true;
        break;
      }
    }
    if (tracked_objective &&
        (!last_map_animation_ms_ || now - last_map_animation_ms_ >= 350)) {
      last_map_animation_ms_ = now;
      chrome_dirty_.store(true, std::memory_order_release);
    }
  }

  // The custom interface is an event-driven dashboard, not another animated
  // game view. Publish immediately when a model/touch/page change redraws the
  // cached chrome, then leave WEMU's last submitted buffer latched. A one-second
  // heartbeat recovers a compositor reattachment without copying and
  // presenting a 1240x1080 surface sixty times a second while nothing changed.
  if (context_ && EnsureChrome()) {
    const bool redraw = chrome_dirty_.load(std::memory_order_acquire);
    if (redraw) {
      DrawChrome(context_);
      // DrawChrome publishes the new hit geometry and base pixels. Composite
      // native art into that same persistent texture only after a successful
      // upload; ordinary Presents below now need only CopyResource.
      if (!chrome_dirty_.load(std::memory_order_acquire)) {
        DrawNativeUiIcons();
        ui_present_pending_ = true;
      }
    }
    const bool heartbeat = last_ui_present_ms_ == 0 ||
                           now - last_ui_present_ms_ >= 1000;
    if (!ui_present_pending_ && !heartbeat) {
      return true;
    }
    ID3D11Texture2D* back = nullptr;
    ID3D11Texture2D* chrome = nullptr;
    {
      std::lock_guard<std::mutex> guard(rtv_lock_);
      back = back_buffer_;
      chrome = chrome_texture_;
      if (back) back->AddRef();
      if (chrome) chrome->AddRef();
    }
    if (back && chrome) context_->CopyResource(back, chrome);
    const bool copied = back && chrome;
    if (chrome) chrome->Release();
    if (back) back->Release();
    if (!copied) return false;
    ui_present_pending_ = true;
  } else {
    ui_present_pending_ = true;
    return false;
  }

  // Never let the auxiliary swapchain back-pressure the game's own Present.
  // This matters on the Thor: DXVK can accept the first lower-surface frame and
  // then wait indefinitely for its queue while WEMU is still attaching the
  // SurfaceControl consumer. DO_NOT_WAIT turns that condition into a harmless
  // WAS_STILL_DRAWING skip; a later game frame retries the newest image.
  last_error_ = swapchain_->Present(0, DXGI_PRESENT_DO_NOT_WAIT);
  if (last_error_ == DXGI_ERROR_WAS_STILL_DRAWING) {
    return true;
  }
  if (last_error_ == DXGI_ERROR_DEVICE_REMOVED || last_error_ == DXGI_ERROR_DEVICE_RESET) {
    DXS_ERR("[panel] device lost on present (0x%08lX); releasing the surface", last_error_);
    ReleaseSurface();
    surface_retry_ms_ = now + 1000;
    return false;
  }
  if (FAILED(last_error_)) {
    DXS_ERR("[panel] Present failed 0x%08lX at panel frame %u", last_error_, presents_);
    return false;
  }
  ui_present_pending_ = false;
  last_ui_present_ms_ = now;
  ++presents_;
  if (presents_ == 1 || (presents_ % 600) == 0) {
    DXS_INFO("[panel] presented %u frame(s), last result 0x%08lX", presents_, last_error_);
  }
  return SUCCEEDED(last_error_);
}

void Panel::Destroy() {
  SetGuestTouchDispatcher(nullptr, nullptr);
  SetPageSelectionHandler(nullptr, nullptr);
  ClearQueuedGameActions();
  TheMapRasterCache().Shutdown();
  TheNativeUiRenderer().Shutdown();
  // Release archive-created D3D views while the panel device is known alive.
  // The archive deliberately performs no COM work from its static destructor,
  // which can run after d3d11.dll has already unloaded.
  TheGameTextureArchive().Shutdown();
  game_loaded_.store(false, std::memory_order_release);
  weapon_equipped_.store(false, std::memory_order_release);
  game_visibility_dirty_.store(false, std::memory_order_release);
  show_after_tick_.store(0, std::memory_order_release);
  menu_input_locked_.store(false, std::memory_order_release);
  initial_menu_lock_seen_ = false;
  gameplay_unlock_started_ms_ = 0;
  gameplay_ready_samples_ = 0;
  blank_frame_presented_ = false;
  ReleaseSurface();
  if (window_) {
    if (window_owner_thread_ == GetCurrentThreadId()) {
      DestroyWindow(window_);
    } else if (IsWindow(window_)) {
      PostMessageW(window_, kDestroyPanelWindowMessage, 0, 0);
    }
    window_ = nullptr;
  }
  window_owner_thread_ = 0;
  for (HWND retired : retired_windows_) {
    if (IsWindow(retired)) {
      ShowWindowAsync(retired, SW_HIDE);
      PostMessageW(retired, kDestroyPanelWindowMessage, 0, 0);
    }
  }
  retired_windows_.clear();
  retired_window_retry_ms_ = 0;
}

}  // namespace dxs
