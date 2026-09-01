#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>

#include "Panel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <cstdio>
#include <cstring>
#include <string>

#include "Log.h"
#include "LocalMapFrame.h"
#include "LocalMapCapture.h"
#include "MapProjection.h"
#include "Perf.h"
#include "Snapshot.h"
#include "StateStore.h"
#include "VanillaUiAssets.h"

namespace fo3ds {
namespace {

constexpr int kLogicalWidth = 1240;
constexpr int kLogicalHeight = 1080;
constexpr size_t kMaxPanelPixels = 16u * 1024u * 1024u;
constexpr uint16_t kDefaultTouchPort = 7788;
constexpr COLORREF kBlack = RGB(4, 19, 10);
constexpr COLORREF kDim = RGB(43, 126, 73);
constexpr COLORREF kGreen = RGB(86, 246, 146);
constexpr COLORREF kBright = RGB(190, 255, 215);
constexpr COLORREF kSelection = RGB(18, 74, 42);

// These are the three physical Pip-Boy modes.  Keep their order spatially
// stable: a left swipe advances ITEMS -> DATA -> STATS and a right swipe moves
// back.  The five tabs inside each mode are deliberately separate state.
enum class Page : uint8_t { kItems, kData, kStat };
enum class StatPage : uint8_t { kStatus, kSpecial, kSkills, kPerks, kGeneral };
enum class StatusMode : uint8_t { kCondition, kRadiation, kEffects };
enum class DataPage : uint8_t {
  kLocalMap,
  kWorldMap,
  kQuests,
  kNotes,
  kRadio,
};

HMODULE g_module = nullptr;
HANDLE g_thread = nullptr;
std::atomic<DWORD> g_thread_id{0};
std::atomic<bool> g_running{false};
std::atomic<bool> g_radio_page_requested{false};
std::atomic<bool> g_effects_page_requested{false};
std::atomic<bool> g_map_page_requested{false};
std::atomic<bool> g_local_map_page_requested{false};
std::atomic<bool> g_notes_page_requested{false};
std::atomic<bool> g_condition_stats_requested{false};
std::atomic<bool> g_special_stats_requested{false};
std::atomic<bool> g_skills_stats_requested{false};
std::atomic<bool> g_general_stats_requested{false};
std::atomic<bool> g_game_data_requested{false};
HWND g_window = nullptr;
int g_width = 0;
int g_height = 0;
int g_present_x = 0;
int g_present_y = 0;
int g_present_w = 0;
int g_present_h = 0;
HDC g_back_dc = nullptr;
HBITMAP g_back_bitmap = nullptr;
HGDIOBJ g_back_previous = nullptr;
void* g_back_bits = nullptr;
HDC g_background_dc = nullptr;
HBITMAP g_background_bitmap = nullptr;
HGDIOBJ g_background_previous = nullptr;
HDC g_transition_from_dc = nullptr;
HBITMAP g_transition_from_bitmap = nullptr;
HGDIOBJ g_transition_from_previous = nullptr;
HDC g_transition_to_dc = nullptr;
HBITMAP g_transition_to_bitmap = nullptr;
HGDIOBJ g_transition_to_previous = nullptr;
HFONT g_font_small = nullptr;
HFONT g_font_detail = nullptr;
HFONT g_font_body = nullptr;
HFONT g_font_title = nullptr;
HFONT g_font_large = nullptr;
struct CachedPen {
  COLORREF color = 0;
  int width = 0;
  HPEN handle = nullptr;
};
constexpr size_t kCachedPenCount = 12;
CachedPen g_cached_pens[kCachedPenCount] = {};
SOCKET g_touch_socket = INVALID_SOCKET;
sockaddr_in g_touch_peer = {};
bool g_have_touch_peer = false;
bool g_winsock = false;
uint16_t g_touch_port = kDefaultTouchPort;
wchar_t g_ini_path[MAX_PATH] = {};
bool g_pointer_down = false;
enum class PointerSource : uint8_t { kNone, kMouse, kUdp };
PointerSource g_pointer_source = PointerSource::kNone;
float g_pointer_down_x = 0.0f;
float g_pointer_down_y = 0.0f;
float g_pointer_last_x = 0.0f;
float g_pointer_last_y = 0.0f;
bool g_pointer_map_dragging = false;
uint64_t g_last_activation_ms = 0;
float g_last_activation_x = -1.0f;
float g_last_activation_y = -1.0f;
Snapshot g_snapshot = {};
bool g_have_snapshot = false;
uint64_t g_snapshot_sequence = 0;
Page g_page = Page::kItems;
StatPage g_stat_page = StatPage::kStatus;
StatusMode g_status_mode = StatusMode::kCondition;
DataPage g_data_page = DataPage::kLocalMap;
ItemCategory g_item_category = ItemCategory::kWeapon;
uint32_t g_selected_item = 0;
uint32_t g_selected_quest = 0;
uint32_t g_selected_station = 0;
uint32_t g_selected_note = 0;
uint32_t g_selected_perk = 0;
uint32_t g_selected_special = 0;
uint32_t g_selected_skill = 0;
uint32_t g_selected_misc_stat = 0;
uint32_t g_item_scroll[5] = {};
uint32_t g_quest_scroll = 0;
uint32_t g_note_scroll = 0;
uint32_t g_station_scroll = 0;
uint32_t g_perk_scroll = 0;
uint32_t g_skill_scroll = 0;
uint32_t g_effect_scroll = 0;
uint32_t g_general_scroll = 0;
uint32_t g_quest_objective_scroll = 0;
uint32_t g_note_text_scroll = 0;
uint32_t g_note_text_scroll_max = 0;
float g_map_pan_x[2] = {};
float g_map_pan_y[2] = {};
float g_map_zoom[2] = {1.0f, 1.0f};
float g_map_anchor_x[2] = {};
float g_map_anchor_y[2] = {};
uint64_t g_map_anchor_epoch[2] = {};
uint32_t g_map_anchor_space[2] = {};
bool g_map_anchor_valid[2] = {};
uint64_t g_last_radio_revision = 0;
uint32_t g_pending_action_form = 0;
uint64_t g_pending_action_started_ms = 0;
uint64_t g_pending_action_request_id = 0;
int32_t g_pending_action_item_count = -1;
uint64_t g_action_error_until_ms = 0;
uint32_t g_action_error_form = 0;
CommandType g_pending_action_type = CommandType::kNone;
bool g_transition_active = false;
Page g_transition_from = Page::kItems;
Page g_transition_to = Page::kItems;
int g_transition_direction = 1;
uint64_t g_transition_started_ms = 0;
constexpr uint64_t kTransitionDurationMs = 230;
constexpr uint64_t kActionTimeoutMs = 3000;
bool g_dirty = true;
bool g_vanilla_assets_initialized = false;
bool g_vanilla_status_ready = false;
uint64_t g_next_asset_diagnostic_ms = 0;
uint64_t g_next_radio_animation_ms = 0;
VanillaUiAssetCacheStats g_last_asset_diagnostics = {};
bool g_have_asset_diagnostics = false;

void UpdateRequestedDomains() {
  g_radio_page_requested.store(
      (g_page == Page::kData && g_data_page == DataPage::kRadio) ||
          (g_pending_action_request_id != 0 &&
           (g_pending_action_type == CommandType::kTuneRadio ||
            g_pending_action_type == CommandType::kRadioOff)),
      std::memory_order_release);
  g_map_page_requested.store(
      g_page == Page::kData &&
          (g_data_page == DataPage::kLocalMap ||
           g_data_page == DataPage::kWorldMap),
      std::memory_order_release);
  g_local_map_page_requested.store(
      g_page == Page::kData && g_data_page == DataPage::kLocalMap,
      std::memory_order_release);
  g_notes_page_requested.store(
      g_page == Page::kData && g_data_page == DataPage::kNotes,
      std::memory_order_release);
  g_effects_page_requested.store(
      g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
          g_status_mode == StatusMode::kEffects,
      std::memory_order_release);
  g_condition_stats_requested.store(
      g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
          g_status_mode == StatusMode::kCondition,
      std::memory_order_release);
  g_special_stats_requested.store(
      g_page == Page::kStat && g_stat_page == StatPage::kSpecial,
      std::memory_order_release);
  g_skills_stats_requested.store(
      g_page == Page::kStat && g_stat_page == StatPage::kSkills,
      std::memory_order_release);
  g_general_stats_requested.store(
      g_page == Page::kStat && g_stat_page == StatPage::kGeneral,
      std::memory_order_release);
  g_game_data_requested.store(
      (g_page == Page::kData && g_data_page == DataPage::kQuests) ||
          (g_page == Page::kStat &&
           g_stat_page == StatPage::kPerks) ||
          (g_pending_action_request_id != 0 &&
           g_pending_action_type == CommandType::kTrackQuest),
      std::memory_order_release);
}

void ClearPendingAction() {
  g_pending_action_form = 0;
  g_pending_action_started_ms = 0;
  g_pending_action_request_id = 0;
  g_pending_action_item_count = -1;
  g_pending_action_type = CommandType::kNone;
  UpdateRequestedDomains();
}

const ItemState* FindItem(const Snapshot& state, uint32_t form_id) {
  for (uint32_t i = 0; i < state.item_count && i < kMaxItems; ++i)
    if (state.items[i].form_id == form_id) return &state.items[i];
  return nullptr;
}

bool PendingActionConfirmed(const Snapshot& state) {
  switch (g_pending_action_type) {
    case CommandType::kEquipItem: {
      const ItemState* item = FindItem(state, g_pending_action_form);
      return item && (item->flags & kItemEquipped);
    }
    case CommandType::kUnequipItem: {
      const ItemState* item = FindItem(state, g_pending_action_form);
      return !item || !(item->flags & kItemEquipped);
    }
    case CommandType::kUseItem: {
      const ItemState* item = FindItem(state, g_pending_action_form);
      return !item || item->count < g_pending_action_item_count;
    }
    case CommandType::kTrackQuest:
      for (uint32_t i = 0; i < state.quest_count && i < kMaxQuests; ++i)
        if (state.quests[i].form_id == g_pending_action_form)
          return (state.quests[i].flags & kQuestTracked) != 0;
      return false;
    case CommandType::kTuneRadio:
      for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
        if (state.stations[i].form_id == g_pending_action_form)
          return (state.stations[i].flags & kStationTuned) != 0;
      return false;
    case CommandType::kRadioOff:
      for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
        if (state.stations[i].flags & kStationTuned) return false;
      return true;
    default:
      return false;
  }
}

void BeginPendingAction(const Command& command, uint64_t request_id,
                        const ItemState* item = nullptr) {
  g_pending_action_form = command.form_id;
  g_pending_action_started_ms = GetTickCount64();
  g_pending_action_request_id = request_id;
  g_pending_action_item_count = item ? item->count : -1;
  g_pending_action_type = command.type;
  g_action_error_until_ms = 0;
  g_action_error_form = 0;
  UpdateRequestedDomains();
}

bool ActionPending() {
  return g_pending_action_request_id != 0 &&
         GetTickCount64() - g_pending_action_started_ms < kActionTimeoutMs;
}

struct Box {
  int x, y, w, h;
  bool Contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

void ResetMapViewState(int index) {
  if (index < 0 || index > 1) return;
  g_map_pan_x[index] = 0.0f;
  g_map_pan_y[index] = 0.0f;
  g_map_zoom[index] = 1.0f;
  g_map_anchor_x[index] = 0.0f;
  g_map_anchor_y[index] = 0.0f;
  g_map_anchor_epoch[index] = 0;
  g_map_anchor_space[index] = 0;
  g_map_anchor_valid[index] = false;
}

void SendHaptic(const char* cue) {
  if (g_touch_socket == INVALID_SOCKET || !g_have_touch_peer || !cue) return;
  sendto(g_touch_socket, cue, static_cast<int>(std::strlen(cue)), 0,
         reinterpret_cast<const sockaddr*>(&g_touch_peer),
         sizeof(g_touch_peer));
}

std::wstring Wide(const char* value) {
  if (!value || !*value) return {};
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1,
                                  nullptr, 0);
  UINT page = CP_UTF8;
  DWORD flags = MB_ERR_INVALID_CHARS;
  if (count <= 0) {
    page = CP_ACP;
    flags = 0;
    count = MultiByteToWideChar(page, flags, value, -1, nullptr, 0);
  }
  if (count <= 1) return {};
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(page, flags, value, -1, result.data(), count);
  result.pop_back();
  return result;
}

std::wstring RecordIconAssetPath(const char* value) {
  std::wstring path = Wide(value);
  if (path.empty()) return {};
  std::replace(path.begin(), path.end(), L'/', L'\\');
  while (!path.empty() && path.front() == L'\\') path.erase(path.begin());
  if (path.size() >= 5 && _wcsnicmp(path.c_str(), L"data\\", 5) == 0)
    path.erase(0, 5);
  if (path.size() < 9 || _wcsnicmp(path.c_str(), L"textures\\", 9) != 0)
    path.insert(0, L"textures\\");
  return path;
}

bool DrawRecordIcon(HDC dc, const char* icon_path, const Box& bounds,
                    BYTE opacity = 255,
                    bool trim_transparent_padding = false) {
  const std::wstring path = RecordIconAssetPath(icon_path);
  if (path.empty()) return false;
  const UiAssetBox destination = {bounds.x, bounds.y, bounds.w, bounds.h};
  return trim_transparent_padding
      ? DrawVanillaUiAssetContentFit(dc, path.c_str(), destination, kGreen,
                                     opacity)
      : DrawVanillaUiAssetFit(dc, path.c_str(), destination, kGreen, opacity);
}

void Fill(HDC dc, const Box& box, COLORREF color) {
  RECT rect = {box.x, box.y, box.x + box.w, box.y + box.h};
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

HPEN DrawingPen(HDC dc, COLORREF color, int width, bool* temporary) {
  if (temporary) *temporary = false;
  if (width <= 1) {
    SetDCPenColor(dc, color);
    return static_cast<HPEN>(GetStockObject(DC_PEN));
  }
  for (CachedPen& cached : g_cached_pens) {
    if (cached.handle && cached.color == color && cached.width == width)
      return cached.handle;
  }
  for (CachedPen& cached : g_cached_pens) {
    if (cached.handle) continue;
    cached.handle = CreatePen(PS_SOLID, width, color);
    if (!cached.handle) return nullptr;
    cached.color = color;
    cached.width = width;
    return cached.handle;
  }
  if (temporary) *temporary = true;
  return CreatePen(PS_SOLID, width, color);
}

void Stroke(HDC dc, const Box& box, COLORREF color, int width = 2) {
  bool temporary = false;
  HPEN pen = DrawingPen(dc, color, width, &temporary);
  if (!pen) return;
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, box.x, box.y, box.x + box.w, box.y + box.h);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  if (temporary) DeleteObject(pen);
}

void Line(HDC dc, int x0, int y0, int x1, int y1, COLORREF color,
          int width = 2) {
  bool temporary = false;
  HPEN pen = DrawingPen(dc, color, width, &temporary);
  if (!pen) return;
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, nullptr);
  LineTo(dc, x1, y1);
  SelectObject(dc, old);
  if (temporary) DeleteObject(pen);
}

void Text(HDC dc, const Box& box, const std::wstring& value, HFONT font,
          COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  if (value.empty()) return;
  RECT rect = {box.x, box.y, box.x + box.w, box.y + box.h};
  HGDIOBJ old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  if (color == kGreen || color == kBright) {
    RECT glow = rect;
    OffsetRect(&glow, 1, 1);
    SetTextColor(dc, RGB(22, 101, 55));
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &glow,
              format | DT_NOPREFIX | DT_END_ELLIPSIS);
  }
  SetTextColor(dc, color);
  DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
            format | DT_NOPREFIX | DT_END_ELLIPSIS);
  SelectObject(dc, old);
}

void ScrollIndicators(HDC dc, const Box& list, uint32_t offset,
                      uint32_t total, uint32_t visible) {
  if (offset)
    Text(dc, {list.x + list.w - 32, list.y - 6, 28, 34}, L"^",
         g_font_small, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (offset + visible < total)
    Text(dc, {list.x + list.w - 32, list.y + list.h - 28, 28, 34}, L"v",
         g_font_small, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawDetailHeading(HDC dc, const Box& box, const std::wstring& title,
                       const std::wstring& subtitle = {}) {
  const auto fits = [dc, &title, &box](HFONT font) {
    if (!dc || !font) return false;
    HGDIOBJ old = SelectObject(dc, font);
    SIZE measured = {};
    const bool result = GetTextExtentPoint32W(
        dc, title.c_str(), static_cast<int>(title.size()), &measured) &&
        measured.cx <= box.w - 16;
    SelectObject(dc, old);
    return result;
  };
  HFONT heading_font = fits(g_font_title) ? g_font_title
                      : fits(g_font_body) ? g_font_body
                      : fits(g_font_small) ? g_font_small
                                           : g_font_detail;
  Text(dc, {box.x + 8, box.y, box.w - 16, 62}, title, heading_font, kBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (!subtitle.empty())
    Text(dc, {box.x, box.y + 66, box.w, 38}, subtitle, g_font_small, kGreen,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Line(dc, box.x + 12, box.y + (subtitle.empty() ? 70 : 106),
       box.x + box.w - 12, box.y + (subtitle.empty() ? 70 : 106), kDim, 2);
}

void DrawWrappedTextScrollable(HDC dc, const Box& box,
                               const std::wstring& value, HFONT font,
                               COLORREF color, uint32_t offset,
                               uint32_t* maximum_offset) {
  if (maximum_offset) *maximum_offset = 0;
  if (!dc || value.empty() || box.w <= 0 || box.h <= 0) return;
  HGDIOBJ old = SelectObject(dc, font);
  RECT measure = {0, 0, box.w, 0};
  DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &measure,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
  const uint32_t content_height = static_cast<uint32_t>(
      std::max<LONG>(0, measure.bottom - measure.top));
  const uint32_t maximum = content_height > static_cast<uint32_t>(box.h)
      ? content_height - static_cast<uint32_t>(box.h) : 0u;
  if (maximum_offset) *maximum_offset = maximum;
  const uint32_t clamped = std::min(offset, maximum);
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, box.x, box.y, box.x + box.w, box.y + box.h);
  RECT target = {box.x, box.y - static_cast<int>(clamped),
                 box.x + box.w,
                 box.y - static_cast<int>(clamped) +
                     static_cast<int>(std::max<uint32_t>(content_height,
                                                         box.h))};
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &target,
            DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX);
  RestoreDC(dc, saved);
  SelectObject(dc, old);
  if (clamped)
    Text(dc, {box.x + box.w - 28, box.y - 2, 24, 28}, L"^", g_font_small,
         kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (clamped < maximum)
    Text(dc, {box.x + box.w - 28, box.y + box.h - 26, 24, 28}, L"v",
         g_font_small, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void CrtBackground(HDC dc) {
  Fill(dc, {0, 0, kLogicalWidth, kLogicalHeight}, kBlack);
  // The reference screen is not flat black. Its phosphor is brightest in the
  // middle and falls away at the curved glass edges. Keep that effect behind
  // the UI; painting scanlines after composition used to erase one fifth of
  // every glyph and was the main reason the panel looked too dark.
  for (int inset = 0; inset < 18; ++inset) {
    const int green = 21 + inset;
    Stroke(dc, {inset, inset, kLogicalWidth - inset * 2,
                kLogicalHeight - inset * 2}, RGB(5, green, 12), 1);
  }
  for (int y = 2; y < kLogicalHeight; y += 5)
    Line(dc, 0, y, kLogicalWidth, y, RGB(5, 25, 13), 1);
}

void Brackets(HDC dc, const Box& box, COLORREF color, int length = 24,
              int width = 2) {
  const int right = box.x + box.w;
  const int bottom = box.y + box.h;
  Line(dc, box.x, box.y, box.x + length, box.y, color, width);
  Line(dc, box.x, box.y, box.x, box.y + length, color, width);
  Line(dc, right - length, box.y, right, box.y, color, width);
  Line(dc, right, box.y, right, box.y + length, color, width);
  Line(dc, box.x, bottom - length, box.x, bottom, color, width);
  Line(dc, box.x, bottom, box.x + length, bottom, color, width);
  Line(dc, right, bottom - length, right, bottom, color, width);
  Line(dc, right - length, bottom, right, bottom, color, width);
}

std::wstring FormatValue(float value, float maximum = -1.0f) {
  if (!std::isfinite(value) || value < 0.0f) return L"--";
  wchar_t buffer[64] = {};
  if (maximum >= 0.0f && std::isfinite(maximum))
    std::swprintf(buffer, 64, L"%.0f/%.0f", value, maximum);
  else
    std::swprintf(buffer, 64, L"%.0f", value);
  return buffer;
}

std::wstring FormatDecimal(float value) {
  if (!std::isfinite(value) || value < 0.0f) return L"--";
  wchar_t buffer[64] = {};
  std::swprintf(buffer, 64, L"%.1f", value);
  return buffer;
}

std::wstring FormatPercent(float value) {
  if (!std::isfinite(value) || value < 0.0f) return L"--";
  wchar_t buffer[64] = {};
  std::swprintf(buffer, 64, L"%.0f%%", value);
  return buffer;
}

std::wstring FormatSigned(float value) {
  if (!std::isfinite(value)) return L"--";
  wchar_t buffer[64] = {};
  std::swprintf(buffer, 64, L"%.0f", value);
  return buffer;
}

const wchar_t* KarmaArtworkPath(float karma) {
  if (karma <= -750.0f)
    return L"textures\\interface\\icons\\pipboyimages\\karma icons\\karma_evil.dds";
  if (karma <= -250.0f)
    return L"textures\\interface\\icons\\pipboyimages\\karma icons\\karma_bad.dds";
  if (karma < 250.0f)
    return L"textures\\interface\\icons\\pipboyimages\\karma icons\\karma_neutral.dds";
  if (karma < 750.0f)
    return L"textures\\interface\\icons\\pipboyimages\\karma icons\\karma_good.dds";
  return L"textures\\interface\\icons\\pipboyimages\\karma icons\\karma_saintly.dds";
}

const wchar_t* KarmaRank(int32_t level, float karma) {
  static constexpr const wchar_t* kGood[30] = {
      L"Vault Guardian", L"Vault Martyr", L"Sentinel", L"Defender",
      L"Dignitary", L"Peacekeeper", L"Ranger of the Wastes", L"Protector",
      L"Urban Avenger", L"Exemplar", L"Capitol Crusader", L"Paladin",
      L"Vault Legend", L"Ambassador of Peace", L"Urban Legend",
      L"Hero of the Wastes", L"Paragon", L"Wasteland Savior", L"Saint",
      L"Last, Best Hope of Humanity", L"Restorer of Faith",
      L"Model of Selflessness", L"Shepherd", L"Friend of the People",
      L"Champion of Justice", L"Symbol of Order", L"Herald of Tranquility",
      L"Lightbringer", L"Earthly Angel", L"Messiah"};
  static constexpr const wchar_t* kNeutral[30] = {
      L"Vault Dweller", L"Vault Renegade", L"Seeker", L"Wanderer",
      L"Citizen", L"Adventurer", L"Vagabond of the Wastes", L"Mercenary",
      L"Urban Ranger", L"Observer", L"Capitol Councilor", L"Keeper",
      L"Vault Descendant", L"Pinnacle of Survival", L"Urban Myth",
      L"Strider of the Wastes", L"Beholder", L"Wasteland Watcher",
      L"Super-Human", L"Paradigm of Humanity", L"Soldier of Fortune",
      L"Profiteer", L"Egocentric", L"Loner", L"Hero for Hire",
      L"Model of Apathy", L"Person of Refinement", L"Moneygrubber",
      L"Gray Stranger", L"True Mortal"};
  static constexpr const wchar_t* kEvil[30] = {
      L"Vault Delinquent", L"Vault Outlaw", L"Opportunist", L"Plunderer",
      L"Fat Cat", L"Marauder", L"Pirate of the Wastes", L"Reaver",
      L"Urban Invader", L"Ne'er-do-well", L"Capitol Crimelord", L"Defiler",
      L"Vault Boogeyman", L"Harbinger of War", L"Urban Superstition",
      L"Villain of the Wastes", L"Fiend", L"Wasteland Destroyer",
      L"Evil Incarnate", L"Scourge of Humanity", L"Architect of Doom",
      L"Bringer of Sorrow", L"Deceiver", L"Consort of Discord",
      L"Stuff of Nightmares", L"Agent of Chaos", L"Instrument of Ruin",
      L"Soultaker", L"Demon's Spawn", L"Devil"};
  const uint32_t index = static_cast<uint32_t>(
      std::clamp(level > 0 ? level : 1, 1, 30) - 1);
  return karma > 249.0f ? kGood[index]
       : karma < -249.0f ? kEvil[index]
                         : kNeutral[index];
}

std::wstring FormatInteger(int32_t value) {
  if (value < 0) return L"--";
  wchar_t buffer[32] = {};
  std::swprintf(buffer, 32, L"%d", value);
  return buffer;
}

void Meter(HDC dc, const Box& box, float value, float maximum) {
  Fill(dc, box, RGB(5, 27, 14));
  Stroke(dc, box, kDim, 2);
  if (value < 0.0f || maximum <= 0.0f) {
    for (int x = box.x - box.h; x < box.x + box.w; x += 12)
      Line(dc, x, box.y + box.h, x + box.h, box.y, kDim, 1);
    return;
  }
  const float ratio = std::clamp(value / maximum, 0.0f, 1.0f);
  Fill(dc, {box.x + 3, box.y + 3,
            static_cast<int>((box.w - 6) * ratio), box.h - 6}, kGreen);
}

constexpr const wchar_t* kSpecialArtwork[kSpecialCount] = {
    L"textures\\interface\\icons\\stats\\special_strength.dds",
    L"textures\\interface\\icons\\stats\\special_perception.dds",
    L"textures\\interface\\icons\\stats\\special_endurance.dds",
    L"textures\\interface\\icons\\stats\\special_charisma.dds",
    L"textures\\interface\\icons\\stats\\special_intelligence.dds",
    L"textures\\interface\\icons\\stats\\special_agility.dds",
    L"textures\\interface\\icons\\stats\\special_luck.dds"};

constexpr const wchar_t* kSkillArtwork[kSkillCount] = {
    L"textures\\interface\\icons\\stats\\skills_barter.dds",
    L"textures\\interface\\icons\\stats\\skills_big_guns.dds",
    L"textures\\interface\\icons\\stats\\skills_energy_weapons.dds",
    L"textures\\interface\\icons\\stats\\skills_explosives.dds",
    L"textures\\interface\\icons\\stats\\skills_lockpick.dds",
    L"textures\\interface\\icons\\stats\\skills_medicine.dds",
    L"textures\\interface\\icons\\stats\\skills_melee_weapons.dds",
    L"textures\\interface\\icons\\stats\\skills_repair.dds",
    L"textures\\interface\\icons\\stats\\skills_science.dds",
    L"textures\\interface\\icons\\stats\\skills_small_guns.dds",
    L"textures\\interface\\icons\\stats\\skills_sneak.dds",
    L"textures\\interface\\icons\\stats\\skills_speech.dds",
    L"textures\\interface\\icons\\stats\\skills_unarmed.dds"};

std::wstring StatusPartPath(const wchar_t* part, bool broken) {
  std::wstring path = L"textures\\interface\\stats\\";
  path += part;
  if (broken) path += L"_broken";
  path += L".dds";
  return path;
}

void InitializeStatArtwork() {
  // DDS decoding of every STATS image at panel startup made initialization
  // contend with save loading. The cache now resolves only the artwork that
  // the visible subpage asks for; each decoded asset remains persistent.
  g_vanilla_assets_initialized = true;
  g_vanilla_status_ready = false;
  Log(LogLevel::kInfo,
      "[UI-ASSET] vanilla Pip-Boy artwork configured for lazy persistent loading");
}

void FlushAssetDiagnostics() {
  const uint64_t now = GetTickCount64();
  if (now < g_next_asset_diagnostic_ms) return;
  g_next_asset_diagnostic_ms = now + 30000;
  const VanillaUiAssetCacheStats stats = GetVanillaUiAssetCacheStats();
  const bool changed = !g_have_asset_diagnostics ||
      stats.cached_assets != g_last_asset_diagnostics.cached_assets ||
      stats.cached_misses != g_last_asset_diagnostics.cached_misses ||
      stats.cached_tints != g_last_asset_diagnostics.cached_tints ||
      stats.cached_bytes != g_last_asset_diagnostics.cached_bytes ||
      stats.file_reads != g_last_asset_diagnostics.file_reads ||
      stats.dds_decodes != g_last_asset_diagnostics.dds_decodes;
  if (changed) {
    Log(LogLevel::kInfo,
        "[UI-ASSET-PERF] cached=%u misses=%u tints=%u bytes=%llu file_reads=%llu dds_decodes=%llu",
        static_cast<unsigned>(stats.cached_assets),
        static_cast<unsigned>(stats.cached_misses),
        static_cast<unsigned>(stats.cached_tints),
        static_cast<unsigned long long>(stats.cached_bytes),
        static_cast<unsigned long long>(stats.file_reads),
        static_cast<unsigned long long>(stats.dds_decodes));
    g_last_asset_diagnostics = stats;
    g_have_asset_diagnostics = true;
  }
}

bool DrawStatusPart(HDC dc, const wchar_t* part, bool broken,
                    const Box& destination, float asset_scale) {
  std::wstring path = StatusPartPath(part, broken);
  if (DrawVanillaUiAssetPixelScaled(dc, path.c_str(), destination.x,
                                    destination.y, asset_scale))
    return true;
  if (!broken) return false;
  path = StatusPartPath(part, false);
  return DrawVanillaUiAssetPixelScaled(dc, path.c_str(), destination.x,
                                       destination.y, asset_scale);
}

bool PrepareStatusPart(const wchar_t* part, bool broken) {
  std::wstring path = StatusPartPath(part, broken);
  if (PreloadVanillaUiAsset(path.c_str())) return true;
  if (!broken) return false;
  path = StatusPartPath(part, false);
  return PreloadVanillaUiAsset(path.c_str());
}

struct NativeStatusFigureLayout {
  Box head;
  Box face;
  Box torso;
  Box left_arm;
  Box right_arm;
  Box left_leg;
  Box right_leg;
  float asset_scale = 1.0f;
};

NativeStatusFigureLayout BuildNativeStatusFigureLayout(const Box& bounds) {
  // Evaluated directly from the installed game's stats_menu.xml. The head
  // owns zoom=145 and every body child inherits it. Face X is the sole literal
  // screen-space value; face Y uses the already-scaled parent/child heights.
  // Fitting this tree once prevents parent-space rounding from separating the
  // face from the head when the complete figure is resized.
  constexpr double zoom = 1.45;
  constexpr double head_width = 85.0 * zoom;
  constexpr double head_height = 92.0 * zoom;
  constexpr double face_width = 48.0 * zoom;
  constexpr double face_height = 64.0 * zoom;
  constexpr double face_x = 21.0;
  constexpr double face_y =
      (head_height - face_height) * 0.5 + 16.0 * zoom;
  constexpr double torso_x = -14.0 * zoom;
  constexpr double torso_y = 86.0 * zoom;
  constexpr double torso_width = 102.0 * zoom;
  constexpr double torso_height = 128.0 * zoom;
  constexpr double left_arm_x = torso_x + 90.0 * zoom;
  constexpr double left_arm_y = torso_y;
  constexpr double left_arm_width = 100.0 * zoom;
  constexpr double left_arm_height = 52.0 * zoom;
  constexpr double right_arm_x = torso_x - 88.0 * zoom;
  constexpr double right_arm_y = torso_y - 5.0 * zoom;
  constexpr double right_arm_width = 96.0 * zoom;
  constexpr double right_arm_height = 54.0 * zoom;
  constexpr double left_leg_x = torso_x + 47.0 * zoom;
  constexpr double left_leg_y = torso_y + 86.0 * zoom;
  constexpr double left_leg_width = 72.0 * zoom;
  constexpr double left_leg_height = 112.0 * zoom;
  constexpr double right_leg_x = torso_x - 37.0 * zoom;
  constexpr double right_leg_y = torso_y + 83.0 * zoom;
  constexpr double right_leg_width = 84.0 * zoom;
  constexpr double right_leg_height = 112.0 * zoom;
  constexpr double composite_left = right_arm_x;
  constexpr double composite_top = 0.0;
  constexpr double composite_right = left_arm_x + left_arm_width;
  constexpr double composite_bottom = left_leg_y + left_leg_height;
  constexpr double composite_width = composite_right - composite_left;
  constexpr double composite_height = composite_bottom - composite_top;

  const double scale = std::min(bounds.w / composite_width,
                                bounds.h / composite_height);
  const double origin_x =
      bounds.x + (bounds.w - composite_width * scale) * 0.5 -
      composite_left * scale;
  const double origin_y =
      bounds.y + (bounds.h - composite_height * scale) * 0.5;
  const auto place = [=](double x, double y, double width, double height) {
    const int left = static_cast<int>(std::lround(origin_x + x * scale));
    const int top = static_cast<int>(std::lround(origin_y + y * scale));
    const int right = static_cast<int>(
        std::lround(origin_x + (x + width) * scale));
    const int bottom = static_cast<int>(
        std::lround(origin_y + (y + height) * scale));
    return Box{left, top, std::max(1, right - left),
               std::max(1, bottom - top)};
  };

  NativeStatusFigureLayout result = {};
  result.asset_scale = static_cast<float>(zoom * scale);
  result.head = place(0.0, 0.0, head_width, head_height);
  result.face = place(face_x, face_y, face_width, face_height);
  result.torso = place(torso_x, torso_y, torso_width, torso_height);
  result.left_arm = place(left_arm_x, left_arm_y, left_arm_width,
                          left_arm_height);
  result.right_arm = place(right_arm_x, right_arm_y, right_arm_width,
                           right_arm_height);
  result.left_leg = place(left_leg_x, left_leg_y, left_leg_width,
                          left_leg_height);
  result.right_leg = place(right_leg_x, right_leg_y, right_leg_width,
                           right_leg_height);
  return result;
}

bool DrawVanillaConditionFigure(HDC dc, const Snapshot& state,
                                NativeStatusFigureLayout* output_layout) {
  // Preserve the native XML composition but use 80% of the previous fitted
  // size.  Scaling the common source-space tree keeps the repaired face, head,
  // and limbs registered with each other at every panel resolution.
  constexpr Box viewport = {206, 128, 840, 740};
  constexpr double figure_scale = 0.80;
  const int fitted_width =
      static_cast<int>(std::lround(viewport.w * figure_scale));
  const int fitted_height =
      static_cast<int>(std::lround(viewport.h * figure_scale));
  const Box figure_bounds = {
      viewport.x + (viewport.w - fitted_width) / 2,
      viewport.y + (viewport.h - fitted_height) / 2,
      fitted_width,
      fitted_height};
  const NativeStatusFigureLayout layout =
      BuildNativeStatusFigureLayout(figure_bounds);
  if (output_layout) *output_layout = layout;

  const auto broken = [&state](uint32_t limb) {
    return limb < kLimbCount && std::isfinite(state.limbs[limb]) &&
           state.limbs[limb] >= 0.0f && state.limbs[limb] <= 0.0f;
  };
  constexpr const wchar_t* parts[kLimbCount] = {
      L"head", L"torso", L"left_arm", L"right_arm", L"left_leg",
      L"right_leg"};
  bool ready = true;
  for (uint32_t limb = 0; limb < kLimbCount; ++limb)
    ready = PrepareStatusPart(parts[limb], broken(limb)) && ready;
  const wchar_t* face_path = broken(0)
                                 ? L"textures\\interface\\stats\\face_10.dds"
                                 : L"textures\\interface\\stats\\face_00.dds";
  ready = PreloadVanillaUiAsset(face_path) && ready;
  if (!ready) {
    LogRateLimited("status-artwork", 5000, LogLevel::kWarning,
                   "[UI-ASSET] condition figure is incomplete; coherent vector fallback selected");
    return false;
  }

  // The native XML paints the head parent, then its face child, followed by
  // the torso child and the torso's limb children. Unknown limb state uses
  // intact art instead of falsely showing a crippled player while loading.
  const int saved = SaveDC(dc);
  if (saved)
    IntersectClipRect(dc, viewport.x, viewport.y,
                      viewport.x + viewport.w, viewport.y + viewport.h);
  bool drawn = DrawStatusPart(dc, L"head", broken(0), layout.head,
                              layout.asset_scale);
  drawn = DrawVanillaUiAssetPixelScaled(
              dc, face_path, layout.face.x, layout.face.y,
              layout.asset_scale) &&
          drawn;

  drawn = DrawStatusPart(dc, L"torso", broken(1), layout.torso,
                         layout.asset_scale) && drawn;
  drawn = DrawStatusPart(dc, L"left_arm", broken(2), layout.left_arm,
                         layout.asset_scale) && drawn;
  drawn = DrawStatusPart(dc, L"right_arm", broken(3), layout.right_arm,
                         layout.asset_scale) && drawn;
  drawn = DrawStatusPart(dc, L"left_leg", broken(4), layout.left_leg,
                         layout.asset_scale) && drawn;
  drawn = DrawStatusPart(dc, L"right_leg", broken(5), layout.right_leg,
                         layout.asset_scale) && drawn;
  if (saved) RestoreDC(dc, saved);
  if (!drawn)
    LogRateLimited("status-artwork-draw", 5000, LogLevel::kWarning,
                   "[UI-ASSET] condition figure composition failed during GDI submission");
  return drawn;
}

[[maybe_unused]] void DrawVaultBoyArtwork(HDC dc, const Box& box, int scene) {
  if (!dc || box.w <= 0 || box.h <= 0) return;
  const auto point = [&box](int x, int y) {
    return POINT{box.x + x * box.w / 240, box.y + y * box.h / 300};
  };
  const auto line = [&](int x0, int y0, int x1, int y1, int width = 4) {
    const POINT a = point(x0, y0);
    const POINT b = point(x1, y1);
    Line(dc, a.x, a.y, b.x, b.y, kGreen, width);
  };
  const auto ellipse = [&](int x0, int y0, int x1, int y1, int width = 4) {
    const POINT a = point(x0, y0);
    const POINT b = point(x1, y1);
    bool temporary = false;
    HPEN pen = DrawingPen(dc, kGreen, width, &temporary);
    if (!pen) return;
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, a.x, a.y, b.x, b.y);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    if (temporary) DeleteObject(pen);
  };
  const auto polyline = [&](const POINT* source, int count, int width = 4) {
    if (!source || count < 2) return;
    POINT scaled[16] = {};
    count = std::min(count, 16);
    for (int i = 0; i < count; ++i)
      scaled[i] = point(source[i].x, source[i].y);
    bool temporary = false;
    HPEN pen = DrawingPen(dc, kGreen, width, &temporary);
    if (!pen) return;
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Polyline(dc, scaled, count);
    SelectObject(dc, old_pen);
    if (temporary) DeleteObject(pen);
  };

  // The panel owns this lightweight vector rendition.  It follows the same
  // visual grammar as the Pip-Boy artwork (bright monochrome outline, large
  // readable gesture and one contextual prop) without capturing or opening
  // the game's real Pip-Boy menu.
  ellipse(84, 18, 158, 94, 5);
  const POINT hair[] = {{87, 46}, {96, 27}, {108, 34}, {119, 20},
                        {130, 32}, {145, 26}, {155, 48}};
  polyline(hair, 7, 5);
  ellipse(103, 48, 111, 57, 3);
  ellipse(132, 48, 140, 57, 3);
  line(120, 54, 116, 65, 3);
  const POINT smile[] = {{105, 70}, {116, 78}, {135, 69}};
  polyline(smile, 3, 3);
  line(109, 93, 108, 108, 4);
  line(136, 93, 137, 108, 4);

  const POINT torso[] = {{101, 106}, {86, 123}, {91, 202}, {115, 214},
                         {146, 204}, {151, 123}, {136, 106}, {101, 106}};
  polyline(torso, 8, 5);
  line(92, 151, 150, 151, 3);
  line(110, 214, 92, 285, 6);
  line(129, 214, 148, 285, 6);
  line(80, 287, 100, 287, 6);
  line(140, 287, 160, 287, 6);

  if (scene < 0) {
    // CND's familiar inspection pose: arms fully extended so every limb has a
    // clear, unambiguous health bar target.
    line(87, 126, 31, 154, 6);
    line(31, 154, 12, 147, 5);
    line(151, 126, 207, 154, 6);
    line(207, 154, 228, 147, 5);
    ellipse(7, 138, 23, 157, 3);
    ellipse(218, 138, 234, 157, 3);
    return;
  }

  switch (scene % 4) {
    case 0:  // proud/flexed
      line(89, 127, 55, 151, 6);
      line(55, 151, 42, 119, 6);
      line(151, 127, 185, 151, 6);
      line(185, 151, 198, 119, 6);
      ellipse(34, 105, 50, 122, 3);
      ellipse(190, 105, 206, 122, 3);
      break;
    case 1:  // examining/pointing
      line(88, 128, 55, 171, 6);
      line(55, 171, 38, 185, 6);
      line(151, 128, 177, 92, 6);
      line(177, 92, 205, 76, 5);
      ellipse(29, 177, 45, 194, 3);
      break;
    case 2:  // hands on hips
      line(88, 128, 64, 166, 6);
      line(64, 166, 95, 178, 6);
      line(151, 128, 176, 166, 6);
      line(176, 166, 146, 178, 6);
      break;
    default:  // friendly wave
      line(89, 128, 55, 171, 6);
      line(55, 171, 38, 185, 6);
      line(151, 128, 181, 105, 6);
      line(181, 105, 181, 65, 5);
      ellipse(173, 50, 190, 68, 3);
      break;
  }

  // A distinct prop makes the selected attribute/skill readable even before
  // its description is read.  The shapes remain deliberately simple so they
  // stay crisp on the Thor's 1240x1080 lower display.
  switch (scene) {
    case 0:  // Strength: dumbbell
      line(14, 86, 68, 86, 6);
      line(18, 68, 18, 104, 7);
      line(30, 63, 30, 109, 7);
      line(54, 63, 54, 109, 7);
      line(66, 68, 66, 104, 7);
      break;
    case 1:  // Perception: magnifier
      ellipse(16, 66, 57, 108, 5);
      line(52, 104, 73, 128, 6);
      break;
    case 2: {  // Endurance: heart
      const POINT heart[] = {{18, 78}, {28, 66}, {41, 78}, {54, 66},
                             {66, 80}, {42, 112}, {18, 78}};
      polyline(heart, 7, 5);
      break;
    }
    case 3:  // Charisma: speech bubble
      line(12, 58, 72, 58, 4); line(72, 58, 72, 98, 4);
      line(72, 98, 42, 98, 4); line(42, 98, 30, 112, 4);
      line(30, 112, 30, 98, 4); line(30, 98, 12, 98, 4);
      line(12, 98, 12, 58, 4);
      break;
    case 4:  // Intelligence: light bulb
      ellipse(20, 55, 64, 99, 5);
      line(31, 98, 34, 116, 5); line(53, 98, 50, 116, 5);
      line(33, 116, 51, 116, 5);
      break;
    case 5:  // Agility: motion streaks
      line(8, 65, 62, 65, 4); line(18, 83, 68, 83, 4);
      line(6, 101, 53, 101, 4);
      break;
    case 6:  // Luck: clover
      ellipse(14, 62, 41, 89, 4); ellipse(40, 62, 67, 89, 4);
      ellipse(14, 87, 41, 114, 4); ellipse(40, 87, 67, 114, 4);
      line(41, 104, 58, 128, 4);
      break;
    case 7:  // Barter
      Text(dc, {point(5, 52).x, point(5, 52).y,
                box.w * 70 / 240, box.h * 76 / 300}, L"$",
           g_font_large, kGreen, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      break;
    case 8:  // Big guns
      line(7, 82, 72, 82, 10); line(56, 82, 78, 69, 6);
      line(24, 82, 17, 112, 6);
      break;
    case 9: {  // Energy weapons
      const POINT bolt[] = {{42, 52}, {19, 91}, {40, 89}, {25, 128},
                            {67, 77}, {46, 80}, {42, 52}};
      polyline(bolt, 7, 6);
      break;
    }
    case 10:  // Explosives
      ellipse(20, 74, 63, 119, 5); line(41, 72, 51, 55, 5);
      line(49, 55, 64, 61, 4);
      break;
    case 11:  // Lockpick
      ellipse(17, 72, 51, 106, 5); line(49, 89, 77, 89, 6);
      line(66, 89, 66, 103, 5); line(76, 89, 76, 98, 5);
      break;
    case 12:  // Medicine
      line(39, 56, 39, 116, 13); line(12, 86, 67, 86, 13);
      break;
    case 13:  // Melee
      line(17, 110, 67, 60, 10); line(11, 116, 27, 122, 6);
      break;
    case 14:  // Repair
      line(18, 112, 65, 66, 8); ellipse(54, 54, 76, 76, 5);
      line(10, 120, 24, 124, 6);
      break;
    case 15:  // Science
      ellipse(10, 59, 72, 113, 3); ellipse(10, 59, 72, 113, 3);
      line(13, 64, 69, 108, 3); line(69, 64, 13, 108, 3);
      ellipse(37, 82, 45, 91, 3);
      break;
    case 16:  // Small guns
      line(12, 75, 69, 75, 9); line(48, 78, 39, 111, 8);
      line(65, 75, 78, 86, 5);
      break;
    case 17:  // Sneak
      ellipse(10, 70, 75, 108, 4); ellipse(34, 80, 50, 98, 4);
      break;
    case 18:  // Speech
      line(10, 59, 75, 59, 4); line(75, 59, 75, 101, 4);
      line(75, 101, 44, 101, 4); line(44, 101, 28, 118, 4);
      line(28, 118, 28, 101, 4); line(28, 101, 10, 101, 4);
      line(10, 101, 10, 59, 4);
      break;
    case 19:  // Unarmed
      ellipse(17, 70, 37, 94, 5); ellipse(34, 62, 54, 91, 5);
      ellipse(51, 68, 70, 96, 5); line(19, 93, 59, 119, 7);
      break;
  }
}

void DrawAttributeRow(HDC dc, const Box& row, const wchar_t* label,
                      const std::wstring& value, bool selected) {
  if (selected) {
    Fill(dc, row, kSelection);
    Stroke(dc, row, kBright, 2);
  }
  Text(dc, {row.x + 14, row.y, row.w - 126, row.h}, label, g_font_body,
       selected ? kBright : kGreen);
  Text(dc, {row.x + row.w - 104, row.y, 86, row.h}, value,
       g_font_body, selected ? kBright : kGreen,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

const wchar_t* PageName(Page page) {
  switch (page) {
    case Page::kItems: return L"ITEMS";
    case Page::kData: return L"DATA";
    case Page::kStat: return L"STATS";
  }
  return L"";
}

void Header(HDC dc, const Snapshot& state, Page page) {
  // The phosphor vignette and 216 scanlines are immutable. Blitting the
  // pre-rendered layer avoids hundreds of GDI calls whenever a live value
  // changes during combat. Fall back only during partial initialization.
  if (g_background_dc) {
    BitBlt(dc, 0, 0, kLogicalWidth, kLogicalHeight, g_background_dc, 0, 0,
           SRCCOPY);
  } else {
    CrtBackground(dc);
  }
  Brackets(dc, {30, 24, 1180, 1044}, kGreen, 30, 3);
  Text(dc, {54, 28, 230, 58}, PageName(page), g_font_title, kBright);

  if (page == Page::kStat) {
    wchar_t level[48] = {};
    std::swprintf(level, 48, L"LVL  %ls", FormatInteger(state.level).c_str());
    Text(dc, {292, 29, 180, 54}, level, g_font_small, kGreen);

    wchar_t hp[72] = {};
    std::swprintf(hp, 72, L"HP  %ls",
                  FormatValue(state.health, state.max_health).c_str());
    Text(dc, {484, 29, 258, 54}, hp, g_font_small, kGreen);

    wchar_t ap[72] = {};
    std::swprintf(ap, 72, L"AP  %ls",
                  FormatValue(state.action_points,
                              state.max_action_points).c_str());
    Text(dc, {750, 29, 246, 54}, ap, g_font_small, kGreen);

    wchar_t xp[72] = {};
    if (state.xp >= 0 && state.xp_next >= 0)
      std::swprintf(xp, 72, L"XP  %d / %d", state.xp, state.xp_next);
    else
      std::swprintf(xp, 72, L"XP  %ls", FormatInteger(state.xp).c_str());
    Text(dc, {992, 29, 186, 54}, xp, g_font_small, kGreen,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    Line(dc, 52, 91, 1188, 91, kGreen, 2);
    return;
  }

  if (page == Page::kData) {
    std::wstring location = Wide(state.location);
    if (location.empty()) location = L"LOCATION UNAVAILABLE";
    Text(dc, {292, 29, 566, 54}, location, g_font_small, kGreen,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    wchar_t position[96] = {};
    if (state.capabilities & kCapMap)
      std::swprintf(position, 96, L"X %.0f   Y %.0f", state.player_x,
                    state.player_y);
    Text(dc, {878, 29, 300, 54}, position, g_font_small, kGreen,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    Line(dc, 52, 91, 1188, 91, kGreen, 2);
    return;
  }

  wchar_t weight[72] = {};
  std::swprintf(weight, 72, L"Wg  %ls",
                FormatValue(state.carry_weight, state.max_carry_weight).c_str());
  Text(dc, {292, 29, 238, 54}, weight, g_font_small, kGreen);

  wchar_t hp[72] = {};
  std::swprintf(hp, 72, L"HP  %ls",
                FormatValue(state.health, state.max_health).c_str());
  Text(dc, {530, 29, 240, 54}, hp, g_font_small, kGreen);

  wchar_t dr[48] = {};
  std::swprintf(dr, 48, L"DR  %ls",
                FormatValue(state.damage_resistance).c_str());
  Text(dc, {776, 29, 154, 54}, dr, g_font_small, kGreen);

  wchar_t caps[48] = {};
  std::swprintf(caps, 48, L"Caps  %ls", FormatInteger(state.caps).c_str());
  Text(dc, {934, 29, 244, 54}, caps, g_font_small, kGreen,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  Line(dc, 52, 91, 1188, 91, kGreen, 2);
}

void SubNavigation(HDC dc, const wchar_t* const* labels, int count,
                   int active) {
  constexpr int x = 52;
  // The hardware reserves the center of the lowest 6% for WEMU's console
  // gesture.  This still reads as the Pip-Boy's bottom rail while keeping all
  // five tabs above that system gesture instead of making the middle tab dead.
  constexpr int y = 950;
  constexpr int total_width = 1136;
  const int width = total_width / count;
  Line(dc, x, y + 3, x + total_width, y + 3, kGreen, 2);
  for (int i = 0; i < count; ++i) {
    Box tab = {x + i * width, y, width, 55};
    if (i == active) {
      Fill(dc, {tab.x + 8, tab.y + 2, tab.w - 16, tab.h - 4}, kSelection);
      Stroke(dc, {tab.x + 8, tab.y, tab.w - 16, tab.h}, kBright, 2);
    }
    Text(dc, tab, labels[i], g_font_small, i == active ? kBright : kGreen,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void RenderDomainUnavailable(HDC dc, const wchar_t* label) {
  Brackets(dc, {48, 108, 1144, 858}, kGreen, 28, 2);
  Text(dc, {100, 456, 1040, 120}, label, g_font_body, kDim,
       DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

void RenderStat(HDC dc, const Snapshot& state) {
  const wchar_t* pages[] = {L"STATUS", L"S.P.E.C.I.A.L.", L"SKILLS",
                            L"PERKS", L"GENERAL"};
  SubNavigation(dc, pages, 5, static_cast<int>(g_stat_page));
  if (!(state.capabilities & kCapStats)) {
    RenderDomainUnavailable(dc, L"PLAYER STATUS IS LOADING");
    return;
  }
  if (g_stat_page == StatPage::kStatus) {
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    static const wchar_t* status_modes[] = {L"CND", L"RAD", L"EFF"};
    for (int i = 0; i < 3; ++i) {
      Box mode = {68, 158 + i * 72, 116, 56};
      const bool active = i == static_cast<int>(g_status_mode);
      if (active) {
        Fill(dc, mode, kSelection);
        Stroke(dc, mode, kBright, 2);
      }
      Text(dc, mode, status_modes[i], g_font_body,
           active ? kBright : kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_status_mode == StatusMode::kCondition) {
      NativeStatusFigureLayout figure = {};
      g_vanilla_status_ready = DrawVanillaConditionFigure(dc, state, &figure);
      if (!g_vanilla_status_ready) {
        Text(dc, {300, 430, 650, 80}, L"VAULT BOY ARTWORK UNAVAILABLE",
             g_font_body, kDim,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      // Meter dimensions and offsets are reduced by the same 20% as the
      // composed artwork.  Each meter is anchored to its transformed body part
      // rather than screen coordinates, so every bar stays on its limb.
      constexpr float condition_scale = 0.80f;
      const int meter_height =
          static_cast<int>(std::lround(22.0f * condition_scale));
      const auto centered_meter = [meter_height](const Box& part, int y,
                                                  int width) {
        return Box{part.x + (part.w - width) / 2, y, width, meter_height};
      };
      const int narrow_meter =
          static_cast<int>(std::lround(106.0f * condition_scale));
      const int limb_meter =
          static_cast<int>(std::lround(112.0f * condition_scale));
      const int head_offset =
          static_cast<int>(std::lround(10.0f * condition_scale));
      const int arm_offset =
          static_cast<int>(std::lround(18.0f * condition_scale));
      const int leg_offset =
          static_cast<int>(std::lround(14.0f * condition_scale));
      // Put the two lower meters visibly below the feet. The earlier 13px
      // source-space adjustment nearly cancelled leg_offset and left the bars
      // on the same visual edge, so it was effectively imperceptible.
      const int lower_leg_drop =
          static_cast<int>(std::lround(28.0f * condition_scale));
      Meter(dc, centered_meter(figure.head, figure.head.y - head_offset,
                               narrow_meter),
            state.limbs[0], 100.0f);
      Meter(dc, centered_meter(figure.torso,
                               figure.torso.y + figure.torso.h / 3,
                               narrow_meter),
            state.limbs[1], 100.0f);
      Meter(dc, centered_meter(figure.left_arm,
                               figure.left_arm.y - arm_offset, limb_meter),
            state.limbs[2], 100.0f);
      Meter(dc, centered_meter(figure.right_arm,
                               figure.right_arm.y - arm_offset, limb_meter),
            state.limbs[3], 100.0f);
      Meter(dc, centered_meter(figure.left_leg,
                               figure.left_leg.y + figure.left_leg.h -
                                   leg_offset + lower_leg_drop,
                               limb_meter), state.limbs[4], 100.0f);
      Meter(dc, centered_meter(figure.right_leg,
                               figure.right_leg.y + figure.right_leg.h -
                                   leg_offset + lower_leg_drop,
                               limb_meter), state.limbs[5], 100.0f);

      std::wstring identity = Wide(state.player_name);
      if (identity.empty()) identity = L"LONE WANDERER";
      if (state.level >= 0)
        identity += L"  -  LEVEL " + std::to_wstring(state.level);
      Text(dc, {282, 872, 676, 48}, identity, g_font_body, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (g_status_mode == StatusMode::kRadiation) {
      if (state.radiation < 0.0f || state.radiation_resistance < 0.0f) {
        Text(dc, {250, 438, 840, 100}, L"RADIATION DATA IS LOADING",
             g_font_body, kDim,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      } else {
        // stats_menu.xml authors these elements at zoom 130. Keep its exact
        // 512x128 bracket ratio and the native 186..482 pointer travel instead
        // of drawing an approximate scale.
        constexpr float native_zoom = 1.30f;
        constexpr Box resist_box = {446, 342, 666, 166};
        constexpr Box meter_box = {446, 556, 666, 166};
        const bool resist_art = DrawVanillaUiAsset(
            dc, L"textures\\interface\\stats\\rad_effects_bracket.dds",
            UiAssetBox{resist_box.x, resist_box.y,
                       resist_box.w, resist_box.h}, kGreen, 255);
        const bool meter_art = DrawVanillaUiAsset(
            dc, L"textures\\interface\\stats\\rad_meter_bracket.dds",
            UiAssetBox{meter_box.x, meter_box.y,
                       meter_box.w, meter_box.h}, kGreen, 255);
        if (!resist_art || !meter_art) {
          LogRateLimited("radiation-assets", 10000, LogLevel::kWarning,
                         "[STATUS-RAD] native radiation artwork unavailable");
        }

        Text(dc, {resist_box.x + 26, resist_box.y + 28, 330, 56},
             L"RAD RESIST", g_font_body, kBright,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Text(dc, {resist_box.x + 370, resist_box.y + 28, 258, 56},
             FormatPercent(state.radiation_resistance), g_font_body, kBright,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);

        constexpr float zero_source_x = 186.0f;
        constexpr float full_source_x = 482.0f;
        const float rads = std::clamp(state.radiation, 0.0f, 1000.0f);
        const float pointer_center = meter_box.x +
            zero_source_x * native_zoom +
            (full_source_x - zero_source_x) * native_zoom *
                (rads / 1000.0f);
        constexpr int pointer_width = 29;
        constexpr int pointer_height = 52;
        const int pointer_x = static_cast<int>(std::lround(pointer_center)) -
                              pointer_width / 2;
        const int pointer_y = meter_box.y + 39;
        if (!DrawVanillaUiAsset(
                dc,
                L"textures\\interface\\shared\\arrow\\pointer_tail.dds",
                UiAssetBox{pointer_x, pointer_y,
                           pointer_width, pointer_height}, kBright, 255)) {
          LogRateLimited("radiation-pointer", 10000, LogLevel::kWarning,
                         "[STATUS-RAD] native radiation pointer unavailable");
        }
        Text(dc, {pointer_x - 30, pointer_y + pointer_height - 5,
                  pointer_width + 60, 48},
             FormatValue(state.radiation), g_font_small, kBright,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        Text(dc, {meter_box.x + 16, meter_box.y + 18, 120, 52},
             L"RADS", g_font_body, kBright,
             DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      }
    } else if (!(state.capabilities & kCapEffects)) {
      Text(dc, {250, 438, 840, 100}, L"SCANNING ACTIVE EFFECTS",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else if (!state.effect_count) {
      Text(dc, {250, 438, 840, 100}, L"NO ACTIVE EFFECTS",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      const uint32_t count = std::min(state.effect_count, kMaxEffects);
      constexpr uint32_t kVisibleEffects = 12;
      g_effect_scroll = std::min(
          g_effect_scroll,
          count > kVisibleEffects ? count - kVisibleEffects : 0u);
      const uint32_t visible = std::min<uint32_t>(
          count - g_effect_scroll, kVisibleEffects);
      for (uint32_t row = 0; row < visible; ++row) {
        const uint32_t i = g_effect_scroll + row;
        const EffectState& effect = state.effects[i];
        const int y = 154 + static_cast<int>(row) * 58;
        Text(dc, {238, y, 570, 50}, Wide(effect.name), g_font_body,
             effect.detrimental ? kBright : kGreen);
        std::wstring detail = effect.magnitude != 0.0f
            ? FormatSigned(effect.magnitude) : L"";
        if (effect.seconds_remaining >= 0.0f) {
          if (!detail.empty()) detail += L"   ";
          detail += FormatValue(effect.seconds_remaining) + L" SEC";
        }
        Text(dc, {816, y, 300, 50}, detail, g_font_small, kGreen,
             DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        Line(dc, 230, y + 52, 1124, y + 52, kDim, 1);
      }
      ScrollIndicators(dc, {226, 146, 908, 710}, g_effect_scroll, count,
                       kVisibleEffects);
    }
  } else if (g_stat_page == StatPage::kSpecial) {
    static const wchar_t* names[] = {L"STRENGTH", L"PERCEPTION", L"ENDURANCE",
                                     L"CHARISMA", L"INTELLIGENCE", L"AGILITY",
                                     L"LUCK"};
    static const wchar_t* descriptions[] = {
        L"Strength measures raw physical power. It affects carrying capacity and melee effectiveness.",
        L"Perception represents awareness of nearby danger and improves skills that depend on sharp senses.",
        L"Endurance measures overall toughness and helps determine health and resistance to environmental hazards.",
        L"Charisma represents personal magnetism and improves how people respond during conversation and trade.",
        L"Intelligence represents knowledge and reasoning, improving technical skills and skill points gained.",
        L"Agility measures coordination and reflexes. It influences Action Points and movement-oriented skills.",
        L"Luck quietly influences many outcomes, including critical hits and fortunate discoveries."};
    g_selected_special = std::min<uint32_t>(g_selected_special,
                                             kSpecialCount - 1);
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    for (uint32_t i = 0; i < kSpecialCount; ++i) {
      Box row = {72, 142 + static_cast<int>(i) * 94, 500, 72};
      DrawAttributeRow(dc, row, names[i], FormatValue(state.special[i]),
                       i == g_selected_special);
    }
    if (!DrawVanillaUiAssetContentFramed(
            dc, kSpecialArtwork[g_selected_special],
            UiAssetBox{628, 118, 522, 420}, 256, 231, kGreen, 255)) {
      Text(dc, {646, 276, 486, 66}, L"ARTWORK UNAVAILABLE", g_font_body,
           kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    Brackets(dc, {620, 550, 534, 334}, kGreen, 22, 2);
    Text(dc, {646, 574, 486, 60}, names[g_selected_special], g_font_title,
         kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {654, 654, 470, 190}, descriptions[g_selected_special],
         g_font_body, kGreen, DT_CENTER | DT_TOP | DT_WORDBREAK);
  } else if (g_stat_page == StatPage::kSkills) {
    static const wchar_t* skills[] = {
        L"BARTER", L"BIG GUNS", L"ENERGY WEAPONS", L"EXPLOSIVES",
        L"LOCKPICK", L"MEDICINE", L"MELEE WEAPONS", L"REPAIR",
        L"SCIENCE", L"SMALL GUNS", L"SNEAK", L"SPEECH", L"UNARMED"};
    static const wchar_t* descriptions[] = {
        L"Barter affects the prices received when buying and selling items.",
        L"Big Guns improves effectiveness with heavy weapons such as miniguns and missile launchers.",
        L"Energy Weapons governs laser, plasma and other advanced energy-based weapons.",
        L"Explosives improves mines, grenades and other explosive weapons.",
        L"Lockpick determines which mechanical locks can be opened and how easily they yield.",
        L"Medicine improves healing from Stimpaks and the effectiveness of many medical items.",
        L"Melee Weapons improves attacks made with close-range weapons held in the hands.",
        L"Repair restores item condition and helps keep weapons and apparel performing properly.",
        L"Science represents technical knowledge and is used to access restricted computer systems.",
        L"Small Guns improves conventional pistols, rifles, shotguns and similar firearms.",
        L"Sneak reduces detection and improves attacks made while hidden.",
        L"Speech helps persuade others and unlocks additional dialogue outcomes.",
        L"Unarmed improves fighting with fists and weapons designed for hand-to-hand combat."};
    constexpr uint32_t kVisibleSkills = 11;
    g_selected_skill = std::min<uint32_t>(g_selected_skill,
                                           kSkillCount - 1);
    g_skill_scroll = std::min<uint32_t>(
        g_skill_scroll,
        kSkillCount > kVisibleSkills ? kSkillCount - kVisibleSkills : 0);
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    for (uint32_t visible = 0; visible < kVisibleSkills; ++visible) {
      const uint32_t i = g_skill_scroll + visible;
      if (i >= kSkillCount) break;
      Box row = {72, 132 + static_cast<int>(visible) * 68, 510, 58};
      DrawAttributeRow(dc, row, skills[i], FormatValue(state.skills[i]),
                       i == g_selected_skill);
    }
    if (g_skill_scroll)
      Text(dc, {42, 124, 28, 38}, L"^", g_font_body, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (g_skill_scroll + kVisibleSkills < kSkillCount)
      Text(dc, {42, 842, 28, 38}, L"v", g_font_body, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!DrawVanillaUiAssetContentFramed(
            dc, kSkillArtwork[g_selected_skill],
            UiAssetBox{628, 118, 522, 420}, 256, 231, kGreen, 255)) {
      Text(dc, {646, 276, 486, 66}, L"ARTWORK UNAVAILABLE", g_font_body,
           kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    Brackets(dc, {620, 540, 534, 344}, kGreen, 22, 2);
    Text(dc, {646, 562, 486, 58}, skills[g_selected_skill], g_font_title,
         kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {650, 642, 478, 206}, descriptions[g_selected_skill],
         g_font_body, kGreen, DT_CENTER | DT_TOP | DT_WORDBREAK);
  } else if (g_stat_page == StatPage::kPerks) {
    if (!(state.capabilities & kCapPerks)) {
      RenderDomainUnavailable(dc, L"PERK DATA IS LOADING");
      return;
    }
    Brackets(dc, {54, 194, 1132, 766}, kDim, 24, 2);
    Line(dc, 596, 206, 596, 946, kDim, 2);
    const PerkState* selected = nullptr;
    if (state.perk_count) {
      for (uint32_t i = 0; i < state.perk_count && i < kMaxPerks; ++i)
        if (state.perks[i].form_id == g_selected_perk)
          selected = &state.perks[i];
      if (!selected) {
        selected = &state.perks[0];
        g_selected_perk = selected->form_id;
        g_perk_scroll = 0;
      }
    }
    const uint32_t perk_total = std::min(state.perk_count, kMaxPerks);
    g_perk_scroll = std::min(g_perk_scroll,
                             perk_total > 14 ? perk_total - 14 : 0u);
    for (uint32_t i = 0; i < state.perk_count && i < kMaxPerks; ++i) {
      const PerkState& perk = state.perks[i];
      if (i < g_perk_scroll) continue;
      const uint32_t visible = i - g_perk_scroll;
      if (visible >= 14) continue;
      Box row = {68, 212 + static_cast<int>(visible) * 51, 510, 45};
      if (perk.form_id == g_selected_perk) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      Text(dc, {row.x + 8, row.y, 390, row.h}, Wide(perk.name), g_font_body,
           perk.form_id == g_selected_perk ? kBright : kGreen);
      const std::wstring rank = perk.rank ? L"RANK " + std::to_wstring(perk.rank) : L"";
      Text(dc, {row.x + 398, row.y, 100, row.h}, rank, g_font_small, kGreen,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ScrollIndicators(dc, {64, 204, 518, 728}, g_perk_scroll, perk_total, 14);
    if (selected) {
      DrawDetailHeading(dc, {622, 222, 538, 120}, Wide(selected->name),
                        selected->rank
                            ? L"RANK " + std::to_wstring(selected->rank)
                            : L"");
      const bool drew_icon = DrawRecordIcon(
          dc, selected->icon_path, {736, 344, 310, 270});
      const int description_y = drew_icon ? 636 : 362;
      const int description_h = drew_icon ? 212 : 432;
      if (selected->description[0])
        Text(dc, {646, description_y, 490, description_h},
             Wide(selected->description), g_font_body, kGreen,
             DT_CENTER | DT_TOP | DT_WORDBREAK);
      else if (!drew_icon)
        Text(dc, {646, 478, 490, 90}, L"NO PERK DESCRIPTION AVAILABLE",
             g_font_small, kDim,
             DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    } else {
      Text(dc, {90, 478, 1060, 90}, L"NO PERKS",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  } else {
    static const wchar_t* labels[kMiscStatCount] = {
        L"QUESTS COMPLETED", L"LOCATIONS DISCOVERED", L"PEOPLE KILLED",
        L"CREATURES KILLED", L"LOCKS PICKED", L"COMPUTERS HACKED",
        L"STIMPAKS TAKEN", L"RAD-X TAKEN", L"RADAWAY TAKEN",
        L"CHEMS TAKEN", L"TIMES ADDICTED", L"MINES DISARMED",
        L"SPEECH SUCCESSES", L"POCKETS PICKED", L"PANTS EXPLODED",
        L"BOOKS READ", L"BOBBLEHEADS FOUND", L"WEAPONS CREATED",
        L"PEOPLE MEZZED", L"CAPTIVES RESCUED", L"SANDMAN KILLS",
        L"PARALYZING PUNCHES", L"ROBOTS DISABLED",
        L"CONTRACTS COMPLETED", L"CORPSES EATEN",
        L"MYSTERIOUS STRANGER VISITS"};
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    if (!(state.capabilities & kCapGeneralStats)) {
      Text(dc, {98, 452, 1044, 90}, L"GENERAL STATISTICS ARE LOADING",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }
    constexpr uint32_t kVisibleGeneral = 11;
    g_selected_misc_stat = std::min(g_selected_misc_stat,
                                    kMiscStatCount - 1);
    if (g_selected_misc_stat < g_general_scroll)
      g_general_scroll = g_selected_misc_stat;
    if (g_selected_misc_stat >= g_general_scroll + kVisibleGeneral)
      g_general_scroll = g_selected_misc_stat - kVisibleGeneral + 1;
    g_general_scroll = std::min(
        g_general_scroll, kMiscStatCount - kVisibleGeneral);
    for (uint32_t visible = 0; visible < kVisibleGeneral; ++visible) {
      const uint32_t i = g_general_scroll + visible;
      const Box row = {72, 132 + static_cast<int>(visible) * 68, 640, 58};
      const bool selected = i == g_selected_misc_stat;
      if (selected) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      Text(dc, {row.x + 14, row.y, row.w - 132, row.h}, labels[i],
           g_font_body, selected ? kBright : kGreen);
      Text(dc, {row.x + row.w - 106, row.y, 84, row.h},
           FormatInteger(state.misc_stats[i]), g_font_body,
           selected ? kBright : kGreen,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ScrollIndicators(dc, {66, 126, 654, 754}, g_general_scroll,
                     kMiscStatCount, kVisibleGeneral);

    const wchar_t* karma = state.karma <= -750.0f ? L"VERY EVIL" :
                           state.karma <= -250.0f ? L"EVIL" :
                           state.karma < 250.0f ? L"NEUTRAL" :
                           state.karma < 750.0f ? L"GOOD" : L"VERY GOOD";
    Text(dc, {760, 224, 354, 58}, karma, g_font_title, kBright,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!DrawVanillaUiAssetContentFit(
            dc, KarmaArtworkPath(state.karma),
            UiAssetBox{782, 286, 312, 392}, kGreen, 255)) {
      LogRateLimited("general-karma-art", 10000, LogLevel::kWarning,
                     "[GENERAL] installed Pip-Boy karma artwork unavailable");
    }
    Text(dc, {740, 688, 394, 58}, KarmaRank(state.level, state.karma),
         g_font_body, kBright,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {760, 750, 354, 48},
         L"KARMA  " + FormatSigned(state.karma), g_font_small, kGreen,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

const wchar_t* CategoryName(ItemCategory category) {
  switch (category) {
    case ItemCategory::kWeapon: return L"WEAPONS";
    case ItemCategory::kApparel: return L"APPAREL";
    case ItemCategory::kAid: return L"AID";
    case ItemCategory::kMisc: return L"MISC";
    case ItemCategory::kAmmo: return L"AMMO";
    default: return L"OTHER";
  }
}

bool ItemSupportsAction(ItemCategory category) {
  return category == ItemCategory::kWeapon ||
         category == ItemCategory::kApparel ||
         category == ItemCategory::kAid;
}

void ItemStatCell(HDC dc, const Box& box, const wchar_t* label,
                  const std::wstring& value) {
  Text(dc, {box.x, box.y, box.w / 2, box.h}, label, g_font_small, kBright);
  Text(dc, {box.x + box.w / 2, box.y, box.w / 2, box.h}, value,
       g_font_body, kBright, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  Line(dc, box.x, box.y + box.h, box.x + box.w, box.y + box.h, kGreen, 2);
}

void RenderItems(HDC dc, const Snapshot& state) {
  const ItemCategory categories[] = {ItemCategory::kWeapon, ItemCategory::kApparel,
                                     ItemCategory::kAid, ItemCategory::kMisc,
                                     ItemCategory::kAmmo};
  const wchar_t* category_names[] = {CategoryName(categories[0]),
                                     CategoryName(categories[1]),
                                     CategoryName(categories[2]),
                                     CategoryName(categories[3]),
                                     CategoryName(categories[4])};
  int active_category = 0;
  for (int i = 0; i < 5; ++i)
    if (categories[i] == g_item_category) active_category = i;
  SubNavigation(dc, category_names, 5, active_category);
  if (!(state.capabilities & kCapInventory)) {
    RenderDomainUnavailable(dc, L"INVENTORY IS LOADING");
    return;
  }
  // Keep the inventory content frame clear of the bottom category rail.  The
  // rail starts at y=950, so the old y=966 frame edge placed both lower corner
  // marks inside the tab buttons.
  Brackets(dc, {48, 108, 1144, 816}, kGreen, 28, 2);
  Line(dc, 728, 120, 728, 924, kDim, 2);
  const ItemState* selected = nullptr;
  const ItemState* first = nullptr;
  uint32_t available = 0;
  for (uint32_t i = 0; i < state.item_count && i < kMaxItems; ++i) {
    const ItemState& item = state.items[i];
    if (item.category != g_item_category || item.count <= 0) continue;
    ++available;
    if (!first) first = &item;
    if (item.form_id == g_selected_item) selected = &item;
  }
  if (!selected && first) {
    selected = first;
    g_selected_item = first->form_id;
    g_item_scroll[active_category] = 0;
  }
  g_item_scroll[active_category] = std::min(
      g_item_scroll[active_category], available > 16 ? available - 16 : 0u);

  uint32_t shown = 0;
  uint32_t matching = 0;
  const uint32_t scroll = g_item_scroll[active_category];
  for (uint32_t i = 0; i < state.item_count && i < kMaxItems; ++i) {
    const ItemState& item = state.items[i];
    if (item.category != g_item_category || item.count <= 0) continue;
    if (matching++ < scroll) continue;
    if (shown < 16) {
      Box row = {62, 122 + static_cast<int>(shown) * 48, 648, 43};
      if (item.form_id == g_selected_item) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      std::wstring name = Wide(item.name);
      if (item.flags & kItemEquipped) name = L"\x25A0  " + name;
      const bool apparel_icon =
          g_item_category == ItemCategory::kApparel && item.icon_path[0] &&
          DrawRecordIcon(dc, item.icon_path,
                         {row.x + 6, row.y + 3, 38, row.h - 6}, 255, true);
      const int name_inset = apparel_icon ? 50 : 10;
      Text(dc, {row.x + name_inset, row.y, 530 - name_inset, row.h}, name,
           g_font_body,
           item.form_id == g_selected_item ? kBright : kGreen);
      wchar_t count[24] = {};
      if (item.count > 1) std::swprintf(count, 24, L"(%d)", item.count);
      Text(dc, {row.x + 536, row.y, 92, row.h}, count, g_font_small, kGreen,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ++shown;
  }
  ScrollIndicators(dc, {58, 116, 660, 786}, scroll, available, 16);
  if (!matching)
    Text(dc, {84, 438, 598, 100}, L"NO ITEMS IN THIS CATEGORY",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
  if (selected) {
    const bool has_description = selected->description[0] != '\0';
    Brackets(dc, {746, 132, 420, 394}, kDim, 18, 1);
    const bool drew_icon = DrawRecordIcon(
        dc, selected->icon_path, {758, 144, 396, 364}, 255, true);
    if (!drew_icon && has_description)
      Text(dc, {770, 154, 372, 344}, Wide(selected->description), g_font_small,
           kGreen, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    else if (!drew_icon)
      Text(dc, {770, 286, 372, 90}, L"NO IMAGE DATA", g_font_small, kDim,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int stat_y = 536;
    const auto stat = [&](const wchar_t* label, const std::wstring& value) {
      ItemStatCell(dc, {766, stat_y, 388, 36}, label, value);
      stat_y += 41;
    };
    if (selected->damage >= 0.0f) stat(L"DAMAGE", FormatValue(selected->damage));
    if (selected->armor >= 0.0f) stat(L"DR", FormatValue(selected->armor));
    stat(L"WEIGHT", FormatDecimal(selected->weight));
    stat(L"VALUE", FormatInteger(selected->value));
    if (selected->ammo_count >= 0)
      stat(L"AMMO", FormatInteger(selected->ammo_count));
    if (selected->condition >= 0.0f)
      stat(L"CONDITION", FormatPercent(selected->condition));
    stat(L"QUANTITY", FormatInteger(selected->count));

    const bool any_pending = ActionPending();
    const bool pending = any_pending &&
                         g_pending_action_form == selected->form_id;
    const bool failed = g_action_error_form == selected->form_id &&
                        GetTickCount64() < g_action_error_until_ms;
    const bool actions = !any_pending && !failed &&
                         (state.capabilities & kCapActions) != 0 &&
                         ItemSupportsAction(selected->category);
    Box action = {780, 842, 360, 64};
    Stroke(dc, action, actions ? kGreen : kDim, 2);
    const bool equipped = (selected->flags & kItemEquipped) != 0;
    const wchar_t* action_label = L"NO ACTION";
    if (pending)
      action_label = L"APPLYING...";
    else if (any_pending)
      action_label = L"WAITING...";
    else if (failed)
      action_label = L"ACTION FAILED";
    else if (!actions && ItemSupportsAction(selected->category))
      action_label = L"ACTION UNAVAILABLE";
    else if (actions && equipped)
      action_label = L"UNEQUIP";
    else if (actions && selected->category == ItemCategory::kAid)
      action_label = L"USE";
    else if (actions)
      action_label = L"EQUIP";
    Text(dc, action, action_label,
         g_font_body, actions ? kBright : kDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void RenderRadio(HDC dc, const Snapshot& state);

bool SelectedRadioIsPlaying(const Snapshot& state) {
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
    if (state.stations[i].form_id == g_selected_station &&
        (state.stations[i].flags & kStationTuned))
      return true;
  return false;
}

void DrawNativeRadioWaveform(HDC dc, const Box& graph, bool playing) {
  if (!dc || graph.w <= 32 || graph.h <= 32) return;
  constexpr const wchar_t* kHorizontalTicks =
      L"textures\\interface\\shared\\meter\\tickmark_horiz.dds";
  constexpr const wchar_t* kVerticalTicks =
      L"textures\\interface\\shared\\meter\\tickmark_vert.dds";
  constexpr const wchar_t* kWave =
      L"textures\\interface\\radio wave\\wave.dds";

  const bool horizontal = DrawVanillaUiAsset(
      dc, kHorizontalTicks,
      UiAssetBox{graph.x, graph.y + graph.h - 32, graph.w, 32}, kGreen, 255);
  const bool vertical = DrawVanillaUiAsset(
      dc, kVerticalTicks,
      UiAssetBox{graph.x + graph.w - 32, graph.y, 32, graph.h}, kGreen, 255);
  if (!horizontal || !vertical) {
    LogRateLimited("radio-meter-assets", 10000, LogLevel::kWarning,
                   "[RADIO] native waveform tickmark assets unavailable");
  }

  const int wave_right = graph.x + graph.w - 5;
  const int center_y = graph.y + (graph.h - 2) / 2 - 8;
  if (!playing) {
    // MapMenu shows only this center carrier line while MM_Waveform is hidden.
    Line(dc, graph.x, center_y, wave_right, center_y, kGreen, 2);
    return;
  }

  // Fallout's radiowave.nif animates a texture-translation controller over
  // wave.dds. Reuse that exact installed DDS and reproduce the wrapping U
  // translation rather than synthesizing an unrelated sine curve.
  const float phase = static_cast<float>(GetTickCount64() % 2400u) / 2400.0f;
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, graph.x, graph.y, graph.x + graph.w,
                    graph.y + graph.h);
  const Box wave_box = {graph.x, graph.y - 40, graph.w, 400};
  const int first_width = std::max(
      1, static_cast<int>(std::lround(wave_box.w * (1.0f - phase))));
  bool drew = DrawVanillaUiAssetRegion(
      dc, kWave,
      UiAssetBox{wave_box.x, wave_box.y, first_width, wave_box.h},
      phase, 0.0f, 1.0f, 1.0f, kBright, 255);
  if (phase > 0.0001f && first_width < wave_box.w) {
    drew = DrawVanillaUiAssetRegion(
               dc, kWave,
               UiAssetBox{wave_box.x + first_width, wave_box.y,
                          wave_box.w - first_width, wave_box.h},
               0.0f, 0.0f, phase, 1.0f, kBright, 255) &&
           drew;
  }
  if (saved) RestoreDC(dc, saved);
  if (!drew) {
    LogRateLimited("radio-wave-asset", 10000, LogLevel::kWarning,
                   "[RADIO] native radiowave texture unavailable");
  }
}

using WorldMapView = MapProjection;

bool ComputeWorldMapView(const Snapshot& state, const Box& map_box,
                          WorldMapView* output) {
  if (!g_map_anchor_valid[1] ||
      g_map_anchor_epoch[1] != state.world_epoch ||
      g_map_anchor_space[1] != state.world_form_id) {
    g_map_anchor_x[1] = state.player_x;
    g_map_anchor_y[1] = state.player_y;
    g_map_anchor_epoch[1] = state.world_epoch;
    g_map_anchor_space[1] = state.world_form_id;
    g_map_anchor_valid[1] = true;
    g_map_pan_x[1] = 0.0f;
    g_map_pan_y[1] = 0.0f;
    g_map_zoom[1] = 1.0f;
  }
  return BuildMapProjection(
      state.map_min_x, state.map_max_y, state.map_max_x, state.map_min_y,
      static_cast<float>(state.map_usable_width),
      static_cast<float>(state.map_usable_height), map_box.w, map_box.h,
      g_map_anchor_x[1], g_map_anchor_y[1], g_map_pan_x[1], g_map_pan_y[1],
      g_map_zoom[1], output);
}

bool ComputeLocalMapView(const LocalMapFrame& frame, float player_x,
                         float player_y, const Box& map_box,
                         WorldMapView* output) {
  if (!g_map_anchor_valid[0] ||
      g_map_anchor_epoch[0] != frame.world_epoch ||
      g_map_anchor_space[0] != frame.cell_form_id) {
    g_map_anchor_x[0] = player_x;
    g_map_anchor_y[0] = player_y;
    g_map_anchor_epoch[0] = frame.world_epoch;
    g_map_anchor_space[0] = frame.cell_form_id;
    g_map_anchor_valid[0] = true;
    g_map_pan_x[0] = 0.0f;
    g_map_pan_y[0] = 0.0f;
    g_map_zoom[0] = 1.0f;
  }
  return BuildMapProjection(
      frame.min_x, frame.max_y, frame.max_x, frame.min_y,
      static_cast<float>(frame.width), static_cast<float>(frame.height),
      map_box.w, map_box.h, g_map_anchor_x[0], g_map_anchor_y[0],
      g_map_pan_x[0],
      g_map_pan_y[0], g_map_zoom[0], output);
}

bool DrawLocalMapFrameRegion(HDC dc, const LocalMapFrame& frame,
                             const Box& destination,
                             const WorldMapView& view) {
  if (!dc || destination.w <= 0 || destination.h <= 0 ||
      frame.bgra.size() !=
          static_cast<size_t>(frame.width) * frame.height)
    return false;
  const int source_x = std::clamp(
      static_cast<int>(std::floor(view.left * frame.width)), 0,
      static_cast<int>(frame.width) - 1);
  const int source_y = std::clamp(
      static_cast<int>(std::floor(view.top * frame.height)), 0,
      static_cast<int>(frame.height) - 1);
  const int source_right = std::clamp(
      static_cast<int>(std::ceil(view.right * frame.width)), source_x + 1,
      static_cast<int>(frame.width));
  const int source_bottom = std::clamp(
      static_cast<int>(std::ceil(view.bottom * frame.height)), source_y + 1,
      static_cast<int>(frame.height));
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = static_cast<LONG>(frame.width);
  info.bmiHeader.biHeight = -static_cast<LONG>(frame.height);
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  const int previous_mode = SetStretchBltMode(dc, HALFTONE);
  SetBrushOrgEx(dc, 0, 0, nullptr);
  const int rows = StretchDIBits(
      dc, destination.x, destination.y, destination.w, destination.h,
      source_x, source_y, source_right - source_x,
      source_bottom - source_y, frame.bgra.data(), &info, DIB_RGB_COLORS,
      SRCCOPY);
  if (previous_mode) SetStretchBltMode(dc, previous_mode);
  return rows > 0;
}

const wchar_t* WorldMarkerAsset(uint16_t type, bool discovered) {
  if (!discovered)
    return L"textures\\interface\\icons\\world map\\icon_map_undiscovered.dds";
  static constexpr const wchar_t* kAssets[15] = {
      nullptr,
      L"textures\\interface\\icons\\world map\\icon_map_city.dds",
      L"textures\\interface\\icons\\world map\\icon_map_settlement.dds",
      L"textures\\interface\\icons\\world map\\icon_map_encampment.dds",
      L"textures\\interface\\icons\\world map\\icon_map_natural_landmark.dds",
      L"textures\\interface\\icons\\world map\\icon_map_cave.dds",
      L"textures\\interface\\icons\\world map\\icon_map_factory.dds",
      L"textures\\interface\\icons\\world map\\icon_map_monument.dds",
      L"textures\\interface\\icons\\world map\\icon_map_military.dds",
      L"textures\\interface\\icons\\world map\\icon_map_office.dds",
      L"textures\\interface\\icons\\world map\\icon_map_ruins_town.dds",
      L"textures\\interface\\icons\\world map\\icon_map_ruins_urban.dds",
      L"textures\\interface\\icons\\world map\\icon_map_ruins_sewer.dds",
      L"textures\\interface\\icons\\world map\\icon_map_metro.dds",
      L"textures\\interface\\icons\\world map\\icon_map_vault.dds"};
  return type < 15 ? kAssets[type] : nullptr;
}

void RenderMapData(HDC dc, const Snapshot& state, bool local) {
  const Box map_box = {62, 114, 1116, 810};
  const Box map_content = {map_box.x + 2, map_box.y + 2,
                           map_box.w - 4, map_box.h - 4};
  const int map_right = map_box.x + map_box.w;
  const int map_bottom = map_box.y + map_box.h;
  Stroke(dc, map_box, kDim, 2);

  LocalMapFramePtr local_frame;
  LocalMapCursor local_cursor = {};
  if (local) {
    local_frame = AcquireLocalMapFrame();
    if (!(state.capabilities & kCapLocalMap) || !local_frame ||
        local_frame->world_epoch != state.world_epoch ||
        local_frame->cell_form_id != state.cell_form_id) {
      const LocalMapCaptureStatus status = GetLocalMapCaptureStatus();
      const wchar_t* message = status == LocalMapCaptureStatus::kUnavailable
          ? L"LOCAL MAP RENDERER UNAVAILABLE"
          : status == LocalMapCaptureStatus::kRetrying
              ? L"LOCAL MAP RENDER RETRYING"
              : L"PREPARING LOCAL MAP";
      Text(dc, {110, 430, 1020, 90}, message,
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }
    if (!AcquireLocalMapCursor(&local_cursor) ||
        local_cursor.world_epoch != state.world_epoch ||
        local_cursor.cell_form_id != state.cell_form_id) {
      local_cursor.x = local_frame->player_x;
      local_cursor.y = local_frame->player_y;
      local_cursor.heading = local_frame->player_heading;
    }
  } else if (!(state.capabilities & kCapMap)) {
    Text(dc, {110, 458, 1020, 90}, L"MAP DATA UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  if (!local && !(state.capabilities & kCapWorldMap)) {
    Text(dc, {110, 458, 1020, 90}, L"WORLD MAP DATA UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }

  const int saved_map_dc = SaveDC(dc);
  IntersectClipRect(dc, map_box.x + 1, map_box.y + 1,
                    map_right, map_bottom);
  WorldMapView view = {};
  if ((local && !ComputeLocalMapView(*local_frame, local_cursor.x,
                                     local_cursor.y, map_content, &view)) ||
      (!local && !ComputeWorldMapView(state, map_content, &view))) {
    RestoreDC(dc, saved_map_dc);
    return;
  }
  if (local) {
    if (!DrawLocalMapFrameRegion(
            dc, *local_frame,
            map_content, view)) {
      LogRateLimited("local-map-frame", 10000, LogLevel::kWarning,
                     "[LOCAL-MAP] immutable frame could not be presented revision=%llu",
                     static_cast<unsigned long long>(local_frame->revision));
    }
  } else {
    const std::wstring texture_path =
        RecordIconAssetPath(state.map_texture_path);
    if (texture_path.empty() ||
        !DrawVanillaUiAssetRegion(
            dc, texture_path.c_str(),
            UiAssetBox{map_content.x, map_content.y,
                       map_content.w, map_content.h},
            view.left, view.top, view.right, view.bottom, kGreen, 170)) {
      LogRateLimited("world-map-texture", 10000, LogLevel::kWarning,
                     "[MAP] installed world-map texture unavailable path=%s",
                     state.map_texture_path);
    }
  }

  const float map_min_x = local ? local_frame->min_x : state.map_min_x;
  const float map_max_x = local ? local_frame->max_x : state.map_max_x;
  const float map_min_y = local ? local_frame->min_y : state.map_min_y;
  const float map_max_y = local ? local_frame->max_y : state.map_max_y;

  const auto screen_point = [&](float world_x, float world_y, int* x, int* y) {
    return ProjectMapPoint(map_min_x, map_max_y, map_max_x, map_min_y,
                           view, map_content.x, map_content.y,
                           map_content.w, map_content.h, world_x, world_y,
                           x, y);
  };

  uint32_t drawn = 0;
  if (local) {
    for (const LocalMapMarker& marker : local_frame->markers) {
      int x = 0;
      int y = 0;
      if (!screen_point(marker.x, marker.y, &x, &y)) continue;
      if (marker.flags & kLocalMapMarkerQuest) {
        // This is the exact glow_hud_compass_objective_marker.dds subtexture
        // entry from Fallout 3's InterfaceShared.tai. It is atlas-only, so
        // address it through its native atlas coordinates rather than drawing
        // a replacement symbol.
        if (!DrawVanillaUiAssetRegion(
                dc, L"textures\\interface\\interfaceshared0.dds",
                UiAssetBox{x - 30, y - 30, 60, 60},
                0.625488f, 0.687988f, 0.687011f, 0.749511f,
                kBright, 255)) {
          LogRateLimited(
              "local-map-quest-asset", 10000, LogLevel::kWarning,
              "[LOCAL-MAP] native objective marker atlas region could not be drawn");
        }
      } else if (marker.flags & kLocalMapMarkerDoor) {
        if (!DrawVanillaUiAssetContentFit(
                dc,
                L"textures\\interface\\icons\\local map\\icon_local_door.dds",
                UiAssetBox{x - 27, y - 27, 54, 54}, kBright, 255)) {
          LogRateLimited(
              "local-map-door-asset", 10000, LogLevel::kWarning,
              "[LOCAL-MAP] native door marker asset could not be drawn");
        }
      } else {
        continue;
      }
      ++drawn;
    }
  }
  for (uint32_t i = 0; !local && i < state.marker_count && i < kMaxMarkers;
       ++i) {
    const MarkerState& marker = state.markers[i];
    if (!(marker.flags & kMarkerVisible) ||
        !(marker.flags & kMarkerDiscovered))
      continue;
    int x = 0;
    int y = 0;
    if (!ProjectMapPointInset(
            map_min_x, map_max_y, map_max_x, map_min_y, view,
            map_content.x, map_content.y, map_content.w, map_content.h,
            30, 30, marker.x, marker.y, &x, &y))
      continue;
    const bool discovered = (marker.flags & kMarkerDiscovered) != 0;
    const COLORREF marker_color = discovered ? kBright : kDim;
    const wchar_t* marker_asset = WorldMarkerAsset(marker.type, discovered);
    if (!marker_asset ||
        !DrawVanillaUiAssetContentFit(dc, marker_asset,
                                      UiAssetBox{x - 25, y - 25, 50, 50},
                                      marker_color, 255)) {
      LogRateLimited("world-marker-asset", 10000, LogLevel::kWarning,
                     "[MAP] native discovered-location marker asset unavailable type=%u",
                     static_cast<unsigned>(marker.type));
      continue;
    }
    if (drawn < 12 && marker.name[0]) {
      const int label_x = std::clamp(
          x + 28, map_content.x + 4, map_content.x + map_content.w - 234);
      const int label_y = std::clamp(
          y - 23, map_content.y + 4, map_content.y + map_content.h - 44);
      Text(dc, {label_x, label_y, 230, 40}, Wide(marker.name), g_font_small,
           discovered ? kGreen : kDim);
    }
    ++drawn;
  }

  int player_x = 0;
  int player_y = 0;
  const float player_world_x = local ? local_cursor.x : state.player_x;
  const float player_world_y = local ? local_cursor.y : state.player_y;
  if (screen_point(player_world_x, player_world_y, &player_x, &player_y)) {
    // The immutable local frame retains TESObjectREFR's native radians. The
    // snapshot world-map probe normalizes its public value to degrees.
    const float heading_degrees = local
        ? local_cursor.heading * 57.2957795131f
        : state.player_heading;
    if (!DrawVanillaUiAssetContentFitRotated(
            dc, L"textures\\interface\\icons\\misc\\glow_cursor.dds",
            UiAssetBox{player_x - 28, player_y - 35, 56, 70},
            heading_degrees, kBright, 255)) {
      LogRateLimited("map-player-cursor", 10000, LogLevel::kWarning,
                     "[MAP] Fallout player cursor asset could not be drawn");
    }
  }

  // End the map-content layer before drawing fixed Pip-Boy chrome. Pan and
  // zoom only change the raster/marker projection above; location text and
  // controls live in panel space and can never inherit map clipping or a
  // transform left by an asset draw.
  RestoreDC(dc, saved_map_dc);

  Fill(dc, {map_box.x + 2, map_bottom - 60, map_box.w - 4, 58},
       RGB(5, 31, 16));
  std::wstring footer = Wide(state.location);
  if (footer.empty()) footer = L"CURRENT LOCATION";
  Text(dc, {map_box.x + 16, map_bottom - 60, map_box.w - 32, 56},
       footer, g_font_body, kBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  const Box zoom_out = {1050, 130, 52, 52};
  const Box zoom_in = {1110, 130, 52, 52};
  Fill(dc, zoom_out, kSelection);
  Fill(dc, zoom_in, kSelection);
  Stroke(dc, zoom_out, kGreen, 2);
  Stroke(dc, zoom_in, kGreen, 2);
  Text(dc, zoom_out, L"-", g_font_title, kBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, zoom_in, L"+", g_font_title, kBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void RenderData(HDC dc, const Snapshot& state) {
  const wchar_t* pages[] = {L"LOCAL MAP", L"WORLD MAP", L"QUESTS",
                            L"NOTES", L"RADIO"};
  SubNavigation(dc, pages, 5, static_cast<int>(g_data_page));
  Brackets(dc, {48, 108, 1144, 858}, kGreen, 28, 2);
  if (g_data_page == DataPage::kLocalMap) {
    RenderMapData(dc, state, true);
    return;
  }
  if (g_data_page == DataPage::kWorldMap) {
    RenderMapData(dc, state, false);
    return;
  }
  if (g_data_page == DataPage::kQuests) {
    if (!(state.capabilities & kCapQuests)) {
      RenderDomainUnavailable(dc, L"QUEST DATA IS LOADING");
      return;
    }
    Line(dc, 596, 120, 596, 952, kDim, 2);
    uint32_t first_active = 0;
    bool selected_active = false;
    for (uint32_t i = 0; i < state.quest_count && i < kMaxQuests; ++i) {
      const QuestState& quest = state.quests[i];
      if (!(quest.flags & kQuestActive)) continue;
      if (!first_active) first_active = quest.form_id;
      if (quest.form_id == g_selected_quest) selected_active = true;
    }
    if (!selected_active) {
      g_selected_quest = first_active;
      g_quest_scroll = 0;
      g_quest_objective_scroll = 0;
    }
    uint32_t active_total = 0;
    for (uint32_t i = 0; i < state.quest_count && i < kMaxQuests; ++i)
      if (state.quests[i].flags & kQuestActive) ++active_total;
    g_quest_scroll = std::min(g_quest_scroll,
                              active_total > 16 ? active_total - 16 : 0u);
    uint32_t row = 0;
    uint32_t active_index = 0;
    for (uint32_t i = 0; i < state.quest_count && i < kMaxQuests && row < 16; ++i) {
      const QuestState& quest = state.quests[i];
      if (!(quest.flags & kQuestActive)) continue;
      if (active_index++ < g_quest_scroll) continue;
      Box box = {62, 122 + static_cast<int>(row) * 48, 516, 43};
      if (quest.form_id == g_selected_quest) {
        Fill(dc, box, kSelection);
        Stroke(dc, box, kBright, 2);
      }
      std::wstring quest_label =
          (quest.flags & kQuestTracked) ? L">> " : L"   ";
      quest_label += Wide(quest.title);
      Text(dc, {box.x + 8, box.y, box.w - 16, box.h}, quest_label,
           g_font_body, quest.form_id == g_selected_quest ? kBright : kGreen);
      if (quest.form_id == g_selected_quest) {
        Text(dc, {630, 132, 514, 46},
             L"STAGE " + std::to_wstring(quest.stage), g_font_small,
             kGreen, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        Line(dc, 622, 184, 1160, 184, kDim, 2);
        if (quest.description[0])
          Text(dc, {630, 200, 514, 146},
               Wide(quest.description), g_font_small, kGreen,
               DT_LEFT | DT_TOP | DT_WORDBREAK);
        else
          Text(dc, {632, 248, 502, 48}, L"CURRENT OBJECTIVES",
               g_font_small, kGreen,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        constexpr int objective_top = 368;
        Line(dc, 622, objective_top, 1160, objective_top, kDim, 2);
        int y = objective_top + 18;
        const uint32_t objective_total =
            std::min(state.objective_count, kMaxObjectives);
        const uint32_t begin = std::min(quest.objective_begin,
                                        objective_total);
        const uint32_t available = objective_total - begin;
        const uint32_t end = begin + std::min(quest.objective_count,
                                               available);
        constexpr uint32_t kVisibleObjectives = 5;
        const uint32_t objective_count = end - begin;
        g_quest_objective_scroll = std::min(
            g_quest_objective_scroll,
            objective_count > kVisibleObjectives
                ? objective_count - kVisibleObjectives : 0u);
        const uint32_t visible_begin = begin + g_quest_objective_scroll;
        const uint32_t visible_end = std::min(
            end, visible_begin + kVisibleObjectives);
        for (uint32_t objective = visible_begin;
             objective < visible_end; ++objective) {
          const ObjectiveState& value = state.objectives[objective];
          const wchar_t* marker = (value.flags & kObjectiveFailed) ? L"[!] " :
                                  (value.flags & kObjectiveCompleted) ? L"[X] " :
                                                                       L"[ ] ";
          Text(dc, {630, y, 522, 62}, marker + Wide(value.text), g_font_small,
               (value.flags & kObjectiveCompleted) ? kDim : kBright,
               DT_LEFT | DT_TOP | DT_WORDBREAK);
          y += 68;
        }
        ScrollIndicators(dc, {624, objective_top + 8, 532, 362},
                         g_quest_objective_scroll, objective_count,
                         kVisibleObjectives);
        const bool any_pending = ActionPending();
        const bool tracking_pending = any_pending &&
            g_pending_action_form == quest.form_id;
        const bool tracking_failed =
            g_action_error_form == quest.form_id &&
            GetTickCount64() < g_action_error_until_ms;
        const bool can_track = (state.capabilities & kCapQuests) != 0 &&
                               !(quest.flags & kQuestTracked) &&
                               !any_pending && !tracking_failed;
        Box track = {850, 842, 294, 62};
        Stroke(dc, track, can_track ? kGreen : kDim, 2);
        Text(dc, track,
             tracking_pending ? L"TRACKING..." :
             any_pending ? L"WAITING..." :
             tracking_failed ? L"ACTION FAILED" :
             ((quest.flags & kQuestTracked) ? L"TRACKED" : L"TRACK QUEST"),
             g_font_small, can_track ? kBright : kDim,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      ++row;
    }
    ScrollIndicators(dc, {58, 116, 524, 786}, g_quest_scroll,
                     active_total, 16);
    if (!row) Text(dc, {90, 440, 1070, 90}, L"NO ACTIVE QUESTS",
                   g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else if (g_data_page == DataPage::kNotes) {
    if (!(state.capabilities & kCapNotes)) {
      RenderDomainUnavailable(dc, L"NOTE DATA IS LOADING");
      return;
    }
    Line(dc, 596, 120, 596, 952, kDim, 2);
    const NoteState* selected = nullptr;
    if (state.note_count) {
      for (uint32_t i = 0; i < state.note_count && i < kMaxNotes; ++i)
        if (state.notes[i].form_id == g_selected_note)
          selected = &state.notes[i];
      if (!selected) {
        selected = &state.notes[0];
        g_selected_note = selected->form_id;
        g_note_scroll = 0;
        g_note_text_scroll = 0;
      }
    }
    const uint32_t note_total = std::min(state.note_count, kMaxNotes);
    g_note_scroll = std::min(g_note_scroll,
                             note_total > 16 ? note_total - 16 : 0u);
    for (uint32_t i = 0; i < state.note_count && i < kMaxNotes; ++i) {
      const NoteState& note = state.notes[i];
      if (i < g_note_scroll) continue;
      const uint32_t visible = i - g_note_scroll;
      if (visible >= 16) continue;
      Box row = {62, 122 + static_cast<int>(visible) * 48, 516, 43};
      if (note.form_id == g_selected_note) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      std::wstring note_label = note.read ? L"   " : L"\x25A0  ";
      note_label += Wide(note.title);
      Text(dc, {row.x + 8, row.y, row.w - 16, row.h}, note_label,
           g_font_body, note.form_id == g_selected_note ? kBright : kGreen);
    }
    ScrollIndicators(dc, {58, 116, 524, 786}, g_note_scroll, note_total, 16);
    if (selected) {
      DrawDetailHeading(dc, {622, 132, 538, 94}, Wide(selected->title));
      if (selected->kind == 2 && selected->icon_path[0]) {
        Brackets(dc, {642, 244, 498, 612}, kDim, 20, 1);
        if (!DrawRecordIcon(dc, selected->icon_path,
                            {658, 260, 466, 580}, 255, true))
          Text(dc, {668, 470, 446, 90}, L"IMAGE DATA UNAVAILABLE",
               g_font_small, kDim,
               DT_CENTER | DT_VCENTER | DT_WORDBREAK);
        g_note_text_scroll = 0;
        g_note_text_scroll_max = 0;
      } else if (selected->text[0]) {
        const Box text_box = {632, 244, 518, 632};
        DrawWrappedTextScrollable(dc, text_box, Wide(selected->text),
                                  g_font_body, kGreen, g_note_text_scroll,
                                  &g_note_text_scroll_max);
        g_note_text_scroll = std::min(g_note_text_scroll,
                                      g_note_text_scroll_max);
      } else {
        g_note_text_scroll = 0;
        g_note_text_scroll_max = 0;
        const wchar_t* unavailable =
            (selected->kind == 0 || selected->kind == 3)
                ? L"AUDIO NOTE\nTRANSCRIPT NOT PROVIDED"
                : L"NO TRANSCRIPT AVAILABLE";
        Text(dc, {642, 438, 498, 110}, unavailable,
             g_font_small, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
      }
    } else {
      Text(dc, {90, 440, 1070, 90}, L"NO NOTES",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  } else {
    RenderRadio(dc, state);
  }
}

void RenderRadio(HDC dc, const Snapshot& state) {
  Text(dc, {66, 118, 640, 56}, L"AVAILABLE SIGNALS", g_font_title, kBright);
  if (!(state.capabilities & kCapRadio)) {
    Text(dc, {90, 440, 1070, 90}, L"RADIO STATE UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  if (!state.station_count) {
    Text(dc, {90, 440, 1070, 90}, L"NO RADIO SIGNALS",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  if (state.radio_revision != g_last_radio_revision) {
    g_last_radio_revision = state.radio_revision;
    for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i) {
      if (state.stations[i].flags & kStationTuned) {
        g_selected_station = state.stations[i].form_id;
        break;
      }
    }
  }
  bool selection_exists = false;
  const RadioState* selected_station = nullptr;
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
    if (state.stations[i].form_id == g_selected_station) {
      selection_exists = true;
      selected_station = &state.stations[i];
    }
  if (!selection_exists) {
    g_selected_station = state.stations[0].form_id;
    g_station_scroll = 0;
  }
  if (!selected_station) selected_station = &state.stations[0];
  const uint32_t station_total = std::min(state.station_count, kMaxStations);
  g_station_scroll = std::min(g_station_scroll,
      station_total > 13 ? station_total - 13 : 0u);
  Line(dc, 728, 120, 728, 952, kDim, 2);
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i) {
    const RadioState& station = state.stations[i];
    if (i < g_station_scroll) continue;
    const uint32_t visible = i - g_station_scroll;
    if (visible >= 13) continue;
    Box row = {68, 194 + static_cast<int>(visible) * 47, 630, 43};
    const bool selected = station.form_id == g_selected_station;
    if (selected) {
      Fill(dc, row, kSelection);
      Stroke(dc, row, kBright, 2);
    }
    std::wstring label = (station.flags & kStationTuned) ? L">> " : L"   ";
    label += Wide(station.name);
    Text(dc, {row.x + 8, row.y, 500, row.h}, label, g_font_body,
         selected ? kBright : kGreen);
    wchar_t signal[32] = {};
    if (station.signal >= 0.0f) std::swprintf(signal, 32, L"%.0f%%", station.signal);
    Text(dc, {row.x + 514, row.y, 96, row.h}, signal, g_font_small, kDim,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  ScrollIndicators(dc, {62, 184, 642, 626}, g_station_scroll,
                   station_total, 13);
  const bool any_pending = ActionPending();
  const bool pending = any_pending &&
      g_pending_action_form == selected_station->form_id &&
      (g_pending_action_type == CommandType::kTuneRadio ||
       g_pending_action_type == CommandType::kRadioOff);
  const bool failed = g_action_error_form == selected_station->form_id &&
                      GetTickCount64() < g_action_error_until_ms;
  const bool can_act = !any_pending && !failed &&
                       (state.capabilities & kCapRadio);
  const bool tuned = (selected_station->flags & kStationTuned) != 0;
  // MapMenu authors the waveform as a 350x350 square. Preserve that layout
  // and its installed meter/wave assets in the bottom-screen detail panel.
  constexpr Box graph = {778, 246, 350, 350};
  DrawNativeRadioWaveform(dc, graph, tuned);
  Text(dc, {graph.x, graph.y + graph.h + 22, graph.w, 48},
       selected_station->signal >= 0.0f
           ? L"SIGNAL  " + FormatPercent(selected_station->signal)
           : L"SIGNAL RECEIVED",
       g_font_small, kGreen, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Box action = {894, 126, 250, 62};
  Stroke(dc, action, can_act ? kGreen : kDim, 2);
  Text(dc, action,
       pending ? L"APPLYING..." :
       any_pending ? L"WAITING..." :
       failed ? L"ACTION FAILED" :
       (tuned ? L"RADIO OFF" : L"TUNE"),
       g_font_small, can_act ? kBright : kDim,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void RenderWaiting(HDC dc, const wchar_t* title, const wchar_t* detail) {
  Brackets(dc, {48, 108, 1144, 858}, kGreen, 28, 2);
  Text(dc, {100, detail && *detail ? 432 : 472, 1040, 100}, title,
       g_font_title, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (detail && *detail)
    Text(dc, {120, 540, 1000, 90}, detail, g_font_body, kDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void RenderPage(HDC dc, Page page, const Snapshot& state) {
  if (!dc) return;
  Header(dc, state, page);
  if (state.lifecycle == Lifecycle::kUnsupported) {
    const std::wstring build = Wide(state.build_label);
    RenderWaiting(dc, L"UNSUPPORTED GAME BUILD", build.c_str());
    return;
  }
  if (state.lifecycle != Lifecycle::kLive) {
    const wchar_t* title = L"INITIALIZING";
    if (state.lifecycle == Lifecycle::kMainMenu) title = L"WAITING FOR SAVE";
    else if (state.lifecycle == Lifecycle::kLoading) title = L"LOADING";
    else if (state.lifecycle == Lifecycle::kDead) title = L"WAITING FOR RELOAD";
    RenderWaiting(dc, title, nullptr);
    return;
  }
  switch (page) {
    case Page::kItems: RenderItems(dc, state); break;
    case Page::kData: RenderData(dc, state); break;
    case Page::kStat: RenderStat(dc, state); break;
  }
}

bool StartPageTransition(int delta) {
  if (!delta || g_transition_active || !g_transition_from_dc ||
      !g_transition_to_dc)
    return false;
  constexpr int kPageCount = 3;
  const int from = static_cast<int>(g_page);
  const int to = (from + (delta > 0 ? 1 : kPageCount - 1)) % kPageCount;
  g_transition_from = g_page;
  g_transition_to = static_cast<Page>(to);
  g_transition_direction = delta > 0 ? 1 : -1;
  if (g_transition_to == Page::kData &&
      (g_data_page == DataPage::kLocalMap ||
       g_data_page == DataPage::kWorldMap))
    ResetMapViewState(g_data_page == DataPage::kLocalMap ? 0 : 1);
  RenderPage(g_transition_from_dc, g_transition_from, g_snapshot);
  RenderPage(g_transition_to_dc, g_transition_to, g_snapshot);
  g_transition_started_ms = GetTickCount64();
  g_transition_active = true;
  g_dirty = true;
  Log(LogLevel::kInfo, "[PANEL] mode transition %ls -> %ls source=%s",
      PageName(g_transition_from), PageName(g_transition_to),
      delta > 0 ? "next" : "previous");
  return true;
}

void RenderTransition() {
  const uint64_t elapsed = GetTickCount64() - g_transition_started_ms;
  float progress = std::min(1.0f,
      elapsed / static_cast<float>(kTransitionDurationMs));
  // Smoothstep gives the CRT page motion a soft start and stop without the
  // sluggish tail of a long ease-out animation.
  progress = progress * progress * (3.0f - 2.0f * progress);
  const int travel = static_cast<int>(kLogicalWidth * progress);
  const int from_x = g_transition_direction > 0 ? -travel : travel;
  const int to_x = g_transition_direction > 0
                       ? kLogicalWidth - travel
                       : -kLogicalWidth + travel;
  Fill(g_back_dc, {0, 0, kLogicalWidth, kLogicalHeight}, kBlack);
  BitBlt(g_back_dc, from_x, 0, kLogicalWidth, kLogicalHeight,
         g_transition_from_dc, 0, 0, SRCCOPY);
  BitBlt(g_back_dc, to_x, 0, kLogicalWidth, kLogicalHeight,
         g_transition_to_dc, 0, 0, SRCCOPY);
  if (elapsed >= kTransitionDurationMs) {
    g_page = g_transition_to;
    UpdateRequestedDomains();
    g_transition_active = false;
    RenderPage(g_back_dc, g_page, g_snapshot);
    g_dirty = false;
  } else {
    g_dirty = true;
  }
}

void Render() {
  ScopedPerformance compose(PerfDomain::kPanelCompose);
  if (!g_back_dc) return;
  if (g_transition_active) {
    RenderTransition();
    return;
  }
  const Snapshot& state = g_snapshot;
  RenderPage(g_back_dc, g_page, state);
  g_dirty = false;
}

bool Present(HDC target) {
  if (!target || !g_back_dc) return false;
  RECT rect = {0, 0, g_width, g_height};
  FillRect(target, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  if (g_present_w == kLogicalWidth && g_present_h == kLogicalHeight)
    return BitBlt(target, g_present_x, g_present_y, kLogicalWidth,
                  kLogicalHeight, g_back_dc, 0, 0, SRCCOPY) != FALSE;
  SetStretchBltMode(target, COLORONCOLOR);
  return StretchBlt(target, g_present_x, g_present_y, g_present_w, g_present_h,
                    g_back_dc, 0, 0, kLogicalWidth, kLogicalHeight,
                    SRCCOPY) != FALSE;
}

void PresentNow() {
  if (!g_window || !IsWindow(g_window)) return;
  HDC target = GetDC(g_window);
  if (!target) return;
  const uint64_t present_start = PerfNow();
  Present(target);
  RecordPerformance(PerfDomain::kPanelPresent,
                    PerfElapsedMicros(present_start));
  ReleaseDC(g_window, target);
}

void ResetSelections() {
  g_selected_item = 0;
  g_selected_quest = 0;
  g_selected_station = 0;
  g_selected_note = 0;
  g_selected_perk = 0;
  g_selected_special = 0;
  g_selected_skill = 0;
  g_selected_misc_stat = 0;
  std::fill(g_item_scroll, g_item_scroll + 5, 0u);
  g_quest_scroll = 0;
  g_note_scroll = 0;
  g_station_scroll = 0;
  g_perk_scroll = 0;
  g_skill_scroll = 0;
  g_effect_scroll = 0;
  g_general_scroll = 0;
  g_quest_objective_scroll = 0;
  g_note_text_scroll = 0;
  g_note_text_scroll_max = 0;
  ResetMapViewState(0);
  ResetMapViewState(1);
  g_last_radio_revision = 0;
  ClearPendingAction();
  g_action_error_until_ms = 0;
  g_action_error_form = 0;
}

void HandleTap(int x, int y) {
  if (g_transition_active) return;
  if (!g_have_snapshot || !g_snapshot.supported_build) return;
  if (g_page == Page::kStat && x >= 52 && x < 1184 &&
      y >= 950 && y < 1008) {
    g_stat_page = static_cast<StatPage>(std::clamp((x - 52) / 227, 0, 4));
    UpdateRequestedDomains();
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  } else if (g_page == Page::kStat &&
             g_stat_page == StatPage::kStatus &&
             x >= 68 && x < 184 && y >= 158 && y < 374) {
    g_status_mode = static_cast<StatusMode>(
        std::clamp((y - 158) / 72, 0, 2));
    g_effect_scroll = 0;
    UpdateRequestedDomains();
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSpecial &&
             x >= 72 && x < 572 && y >= 142 && y < 800) {
    const uint32_t row = static_cast<uint32_t>((y - 142) / 94);
    if (row < kSpecialCount) {
      g_selected_special = row;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSkills &&
             x >= 72 && x < 582 && y >= 132 && y < 880) {
    const uint32_t row = g_skill_scroll +
                         static_cast<uint32_t>((y - 132) / 68);
    if (row < kSkillCount) {
      g_selected_skill = row;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kPerks &&
             x >= 68 && x < 578 && y >= 212 && y < 926) {
    const uint32_t row = g_perk_scroll +
                         static_cast<uint32_t>((y - 212) / 51);
    if (row < g_snapshot.perk_count && row < kMaxPerks) {
      g_selected_perk = g_snapshot.perks[row].form_id;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kGeneral &&
             x >= 72 && x < 712 && y >= 132 && y < 880) {
    const uint32_t row = g_general_scroll +
                         static_cast<uint32_t>((y - 132) / 68);
    if (row < kMiscStatCount) {
      g_selected_misc_stat = row;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kItems) {
    if (x >= 52 && x < 1184 && y >= 950 && y < 1008) {
      const int tab = std::clamp((x - 52) / 227, 0, 4);
      const ItemCategory categories[] = {ItemCategory::kWeapon, ItemCategory::kApparel,
                                         ItemCategory::kAid, ItemCategory::kMisc,
                                         ItemCategory::kAmmo};
      g_item_category = categories[tab];
      g_selected_item = 0;
      g_dirty = true;
      SendHaptic("haptic selection");
      return;
    }
    if (x >= 62 && x < 710 && y >= 122 && y < 890) {
      const uint32_t requested_row = g_item_scroll[static_cast<int>(g_item_category)] +
          static_cast<uint32_t>((y - 122) / 48);
      uint32_t row = 0;
      for (uint32_t i = 0; i < g_snapshot.item_count && i < kMaxItems; ++i) {
        const ItemState& item = g_snapshot.items[i];
        if (item.category != g_item_category || item.count <= 0) continue;
        if (row++ == requested_row) {
          g_selected_item = item.form_id;
          g_dirty = true;
          SendHaptic("haptic selection");
          return;
        }
      }
    }
    if (Box{780, 842, 360, 64}.Contains(x, y) &&
        (g_snapshot.capabilities & kCapActions) && g_selected_item) {
      if (ActionPending()) return;
      for (uint32_t i = 0; i < g_snapshot.item_count && i < kMaxItems; ++i) {
        const ItemState& item = g_snapshot.items[i];
        if (item.form_id != g_selected_item) continue;
        Command command = {};
        command.world_epoch = g_snapshot.world_epoch;
        command.form_id = item.form_id;
        if (!ItemSupportsAction(item.category)) break;
        command.type = (item.flags & kItemEquipped)
            ? CommandType::kUnequipItem
            : (item.category == ItemCategory::kAid ? CommandType::kUseItem
                                                   : CommandType::kEquipItem);
        uint64_t request_id = 0;
        if (QueueCommand(command, &request_id)) {
          BeginPendingAction(command, request_id, &item);
          g_dirty = true;
          SendHaptic("haptic action");
        } else {
          g_action_error_form = item.form_id;
          g_action_error_until_ms = GetTickCount64() + 2000;
          g_dirty = true;
          SendHaptic("haptic error");
        }
        break;
      }
    }
  } else if (g_page == Page::kData) {
    if (x >= 52 && x < 1184 && y >= 950 && y < 1008) {
      const DataPage requested = static_cast<DataPage>(
          std::clamp((x - 52) / 227, 0, 4));
      if (requested != g_data_page &&
          (requested == DataPage::kLocalMap ||
           requested == DataPage::kWorldMap))
        ResetMapViewState(requested == DataPage::kLocalMap ? 0 : 1);
      g_data_page = requested;
      g_quest_objective_scroll = 0;
      g_note_text_scroll = 0;
      UpdateRequestedDomains();
      g_dirty = true;
      SendHaptic("haptic selection");
      return;
    }
    if (g_data_page == DataPage::kLocalMap ||
        g_data_page == DataPage::kWorldMap) {
      const int map_index = g_data_page == DataPage::kLocalMap ? 0 : 1;
      if (Box{1050, 130, 52, 52}.Contains(x, y)) {
        g_map_zoom[map_index] =
            std::max(1.0f, g_map_zoom[map_index] / 1.25f);
        g_dirty = true;
        SendHaptic("haptic selection");
        return;
      }
      if (Box{1110, 130, 52, 52}.Contains(x, y)) {
        g_map_zoom[map_index] =
            std::min(6.0f, g_map_zoom[map_index] * 1.25f);
        g_dirty = true;
        SendHaptic("haptic selection");
        return;
      }
    }
    if (g_data_page == DataPage::kQuests && x >= 62 && x < 578 &&
        y >= 122 && y < 890) {
      const uint32_t requested_row = g_quest_scroll +
          static_cast<uint32_t>((y - 122) / 48);
      uint32_t row = 0;
      for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
        const QuestState& quest = g_snapshot.quests[i];
        if (!(quest.flags & kQuestActive)) continue;
        if (row++ == requested_row) {
          g_selected_quest = quest.form_id;
          g_quest_objective_scroll = 0;
          g_dirty = true;
          SendHaptic("haptic selection");
          return;
        }
      }
    } else if (g_data_page == DataPage::kNotes && x >= 62 && x < 578 &&
               y >= 122 && y < 890) {
      const uint32_t row = g_note_scroll +
                           static_cast<uint32_t>((y - 122) / 48);
      if (row < g_snapshot.note_count && row < kMaxNotes) {
        g_selected_note = g_snapshot.notes[row].form_id;
        g_note_text_scroll = 0;
        g_dirty = true;
        SendHaptic("haptic selection");
      }
    } else if (g_data_page == DataPage::kRadio && x >= 68 && x < 698 &&
               y >= 194 && y < 805) {
      const uint32_t row = g_station_scroll +
                           static_cast<uint32_t>((y - 194) / 47);
      if (row < g_snapshot.station_count && row < kMaxStations) {
        g_selected_station = g_snapshot.stations[row].form_id;
        g_dirty = true;
        SendHaptic("haptic selection");
      }
    }
    if (g_data_page == DataPage::kQuests &&
        Box{850, 842, 294, 62}.Contains(x, y) && g_selected_quest &&
        (g_snapshot.capabilities & kCapQuests)) {
      if (ActionPending()) return;
      for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
        const QuestState& quest = g_snapshot.quests[i];
        if (quest.form_id != g_selected_quest ||
            (quest.flags & kQuestTracked))
          continue;
        Command command = {};
        command.type = CommandType::kTrackQuest;
        command.world_epoch = g_snapshot.world_epoch;
        command.form_id = quest.form_id;
        uint64_t request_id = 0;
        if (QueueCommand(command, &request_id)) {
          BeginPendingAction(command, request_id);
          g_dirty = true;
          SendHaptic("haptic action");
        } else {
          g_action_error_form = quest.form_id;
          g_action_error_until_ms = GetTickCount64() + 2000;
          g_dirty = true;
          SendHaptic("haptic error");
        }
        break;
      }
    }
    if (g_data_page == DataPage::kRadio &&
        Box{894, 126, 250, 62}.Contains(x, y) && g_selected_station &&
        (g_snapshot.capabilities & kCapRadio)) {
      if (ActionPending()) return;
      for (uint32_t i = 0; i < g_snapshot.station_count && i < kMaxStations; ++i) {
        const RadioState& station = g_snapshot.stations[i];
        if (station.form_id != g_selected_station) continue;
        Command command = {};
        command.type = (station.flags & kStationTuned)
            ? CommandType::kRadioOff : CommandType::kTuneRadio;
        command.world_epoch = g_snapshot.world_epoch;
        command.form_id = station.form_id;
        uint64_t request_id = 0;
        if (QueueCommand(command, &request_id)) {
          BeginPendingAction(command, request_id);
          g_dirty = true;
          SendHaptic("haptic action");
        } else {
          g_action_error_form = station.form_id;
          g_action_error_until_ms = GetTickCount64() + 2000;
          g_dirty = true;
          SendHaptic("haptic error");
        }
        break;
      }
    }
  }
}

bool AdjustScroll(uint32_t* offset, uint32_t total, uint32_t visible,
                  int delta) {
  if (!offset) return false;
  const uint32_t maximum = total > visible ? total - visible : 0;
  const int64_t next = std::clamp<int64_t>(
      static_cast<int64_t>(*offset) + delta, 0,
      static_cast<int64_t>(maximum));
  if (static_cast<uint32_t>(next) == *offset) return false;
  *offset = static_cast<uint32_t>(next);
  g_dirty = true;
  return true;
}

bool ScrollCurrentList(float dy, float start_x) {
  if (!std::isfinite(dy) || std::fabs(dy) < 0.055f) return false;
  const int magnitude = std::max(1, static_cast<int>(std::fabs(dy) * 18.0f));
  const int delta = dy < 0.0f ? magnitude : -magnitude;
  if (g_page == Page::kItems) {
    uint32_t total = 0;
    for (uint32_t i = 0; i < g_snapshot.item_count && i < kMaxItems; ++i) {
      const ItemState& item = g_snapshot.items[i];
      if (item.category == g_item_category && item.count > 0) ++total;
    }
    return AdjustScroll(&g_item_scroll[static_cast<int>(g_item_category)],
                        total, 16, delta);
  }
  if (g_page == Page::kStat && g_stat_page == StatPage::kPerks)
    return AdjustScroll(&g_perk_scroll,
                        std::min(g_snapshot.perk_count, kMaxPerks), 14, delta);
  if (g_page == Page::kStat && g_stat_page == StatPage::kSkills)
    return AdjustScroll(&g_skill_scroll, kSkillCount, 11, delta);
  if (g_page == Page::kStat && g_stat_page == StatPage::kGeneral)
    return AdjustScroll(&g_general_scroll, kMiscStatCount, 11, delta);
  if (g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
      g_status_mode == StatusMode::kEffects)
    return AdjustScroll(&g_effect_scroll,
                        std::min(g_snapshot.effect_count, kMaxEffects), 12,
                        delta);
  if (g_page != Page::kData) return false;
  if (g_data_page == DataPage::kQuests) {
    if (start_x >= 596.0f / kLogicalWidth) {
      for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
        const QuestState& quest = g_snapshot.quests[i];
        if (quest.form_id == g_selected_quest)
          return AdjustScroll(&g_quest_objective_scroll,
                              quest.objective_count, 5, delta);
      }
      return false;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i)
      if (g_snapshot.quests[i].flags & kQuestActive) ++total;
    return AdjustScroll(&g_quest_scroll, total, 16, delta);
  }
  if (g_data_page == DataPage::kNotes) {
    if (start_x >= 596.0f / kLogicalWidth) {
      const int pixels = std::max(24, magnitude * 18);
      const int64_t next = std::clamp<int64_t>(
          static_cast<int64_t>(g_note_text_scroll) +
              (dy < 0.0f ? pixels : -pixels),
          0, static_cast<int64_t>(g_note_text_scroll_max));
      if (static_cast<uint32_t>(next) == g_note_text_scroll) return false;
      g_note_text_scroll = static_cast<uint32_t>(next);
      g_dirty = true;
      return true;
    }
    return AdjustScroll(&g_note_scroll,
                        std::min(g_snapshot.note_count, kMaxNotes), 16, delta);
  }
  if (g_data_page == DataPage::kRadio)
    return AdjustScroll(&g_station_scroll,
                        std::min(g_snapshot.station_count, kMaxStations), 13,
                        delta);
  return false;
}

void ChangeSubPage(int direction) {
  if (!direction) return;
  const auto advance = [direction](int value) {
    return (value + (direction > 0 ? 1 : 4)) % 5;
  };
  if (g_page == Page::kItems) {
    const ItemCategory categories[] = {
        ItemCategory::kWeapon, ItemCategory::kApparel, ItemCategory::kAid,
        ItemCategory::kMisc, ItemCategory::kAmmo};
    int current = 0;
    for (int i = 0; i < 5; ++i)
      if (categories[i] == g_item_category) current = i;
    g_item_category = categories[advance(current)];
    g_selected_item = 0;
  } else if (g_page == Page::kData) {
    g_data_page = static_cast<DataPage>(advance(static_cast<int>(g_data_page)));
    if (g_data_page == DataPage::kLocalMap ||
        g_data_page == DataPage::kWorldMap)
      ResetMapViewState(g_data_page == DataPage::kLocalMap ? 0 : 1);
    g_quest_objective_scroll = 0;
    g_note_text_scroll = 0;
  } else {
    g_stat_page = static_cast<StatPage>(advance(static_cast<int>(g_stat_page)));
  }
  UpdateRequestedDomains();
  g_dirty = true;
  SendHaptic("haptic selection");
}

uint32_t RelativeIndex(uint32_t selected, uint32_t total, int direction) {
  if (!total) return 0;
  if (direction > 0) return std::min(selected + 1, total - 1);
  return selected ? selected - 1 : 0;
}

void MoveListSelection(int direction) {
  if (!direction) return;
  if (g_page == Page::kItems) {
    uint32_t total = 0;
    uint32_t current = 0;
    bool found = false;
    for (uint32_t i = 0; i < g_snapshot.item_count && i < kMaxItems; ++i) {
      const ItemState& item = g_snapshot.items[i];
      if (item.category != g_item_category || item.count <= 0) continue;
      if (item.form_id == g_selected_item) {
        current = total;
        found = true;
      }
      ++total;
    }
    if (!total) return;
    const uint32_t target = RelativeIndex(found ? current : 0, total, direction);
    uint32_t row = 0;
    for (uint32_t i = 0; i < g_snapshot.item_count && i < kMaxItems; ++i) {
      const ItemState& item = g_snapshot.items[i];
      if (item.category != g_item_category || item.count <= 0) continue;
      if (row++ == target) {
        g_selected_item = item.form_id;
        const int category = static_cast<int>(g_item_category);
        if (target < g_item_scroll[category]) g_item_scroll[category] = target;
        if (target >= g_item_scroll[category] + 16)
          g_item_scroll[category] = target - 15;
        break;
      }
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kStatus) {
    g_status_mode = static_cast<StatusMode>(RelativeIndex(
        static_cast<uint32_t>(g_status_mode), 3, direction));
    g_effect_scroll = 0;
    UpdateRequestedDomains();
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSpecial) {
    g_selected_special = RelativeIndex(g_selected_special, kSpecialCount,
                                       direction);
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSkills) {
    g_selected_skill = RelativeIndex(g_selected_skill, kSkillCount,
                                     direction);
    if (g_selected_skill < g_skill_scroll)
      g_skill_scroll = g_selected_skill;
    if (g_selected_skill >= g_skill_scroll + 11)
      g_skill_scroll = g_selected_skill - 10;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kPerks) {
    const uint32_t total = std::min(g_snapshot.perk_count, kMaxPerks);
    if (!total) return;
    uint32_t current = 0;
    for (uint32_t i = 0; i < total; ++i)
      if (g_snapshot.perks[i].form_id == g_selected_perk) current = i;
    const uint32_t target = RelativeIndex(current, total, direction);
    g_selected_perk = g_snapshot.perks[target].form_id;
    if (target < g_perk_scroll) g_perk_scroll = target;
    if (target >= g_perk_scroll + 14) g_perk_scroll = target - 13;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kGeneral) {
    g_selected_misc_stat = RelativeIndex(g_selected_misc_stat,
                                         kMiscStatCount, direction);
    if (g_selected_misc_stat < g_general_scroll)
      g_general_scroll = g_selected_misc_stat;
    if (g_selected_misc_stat >= g_general_scroll + 11)
      g_general_scroll = g_selected_misc_stat - 10;
  } else if (g_page == Page::kData &&
             g_data_page == DataPage::kQuests) {
    uint32_t total = 0;
    uint32_t current = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
      const QuestState& quest = g_snapshot.quests[i];
      if (!(quest.flags & kQuestActive)) continue;
      if (quest.form_id == g_selected_quest) current = total;
      ++total;
    }
    if (!total) return;
    const uint32_t target = RelativeIndex(current, total, direction);
    uint32_t row = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
      const QuestState& quest = g_snapshot.quests[i];
      if (!(quest.flags & kQuestActive)) continue;
      if (row++ == target) {
        g_selected_quest = quest.form_id;
        g_quest_objective_scroll = 0;
        break;
      }
    }
    if (target < g_quest_scroll) g_quest_scroll = target;
    if (target >= g_quest_scroll + 16) g_quest_scroll = target - 15;
  } else if (g_page == Page::kData && g_data_page == DataPage::kNotes) {
    const uint32_t total = std::min(g_snapshot.note_count, kMaxNotes);
    if (!total) return;
    uint32_t current = 0;
    for (uint32_t i = 0; i < total; ++i)
      if (g_snapshot.notes[i].form_id == g_selected_note) current = i;
    const uint32_t target = RelativeIndex(current, total, direction);
    g_selected_note = g_snapshot.notes[target].form_id;
    g_note_text_scroll = 0;
    if (target < g_note_scroll) g_note_scroll = target;
    if (target >= g_note_scroll + 16) g_note_scroll = target - 15;
  } else if (g_page == Page::kData && g_data_page == DataPage::kRadio) {
    const uint32_t total = std::min(g_snapshot.station_count, kMaxStations);
    if (!total) return;
    uint32_t current = 0;
    for (uint32_t i = 0; i < total; ++i)
      if (g_snapshot.stations[i].form_id == g_selected_station) current = i;
    const uint32_t target = RelativeIndex(current, total, direction);
    g_selected_station = g_snapshot.stations[target].form_id;
    if (target < g_station_scroll) g_station_scroll = target;
    if (target >= g_station_scroll + 13) g_station_scroll = target - 12;
  } else {
    return;
  }
  g_dirty = true;
  SendHaptic("haptic selection");
}

void ActivateSelection() {
  if (g_page == Page::kItems)
    HandleTap(960, 874);
  else if (g_page == Page::kData && g_data_page == DataPage::kQuests)
    HandleTap(997, 873);
  else if (g_page == Page::kData && g_data_page == DataPage::kRadio)
    HandleTap(1018, 157);
}

void HandleNormalizedTap(float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f ||
      y < 0.0f || y > 1.0f)
    return;
  const uint64_t now = GetTickCount64();
  if (now - g_last_activation_ms < 180 &&
      std::fabs(x - g_last_activation_x) +
              std::fabs(y - g_last_activation_y) < 0.012f)
    return;
  g_last_activation_ms = now;
  g_last_activation_x = x;
  g_last_activation_y = y;
  HandleTap(std::clamp(static_cast<int>(x * kLogicalWidth), 0,
                       kLogicalWidth - 1),
            std::clamp(static_cast<int>(y * kLogicalHeight), 0,
                       kLogicalHeight - 1));
}

void ApplyMapPan(float dx, float dy) {
  if (!std::isfinite(dx) || !std::isfinite(dy) ||
      g_page != Page::kData ||
      (g_data_page != DataPage::kLocalMap &&
       g_data_page != DataPage::kWorldMap))
    return;
  // This is the exact content inset used to crop the raster and project every
  // overlay. Pointer deltas arrive normalized to the whole 1240x1080 panel,
  // so convert them into map-viewport fractions before applying world span.
  const Box map_box = {64, 116, 1112, 806};
  WorldMapView view = {};
  float world_width = 0.0f;
  float world_height = 0.0f;
  const int map_index = g_data_page == DataPage::kLocalMap ? 0 : 1;
  if (map_index == 0) {
    const LocalMapFramePtr frame = AcquireLocalMapFrame();
    LocalMapCursor cursor = {};
    if (!(g_snapshot.capabilities & kCapLocalMap) || !frame ||
        frame->world_epoch != g_snapshot.world_epoch ||
        frame->cell_form_id != g_snapshot.cell_form_id ||
        !AcquireLocalMapCursor(&cursor) ||
        cursor.world_epoch != g_snapshot.world_epoch ||
        cursor.cell_form_id != g_snapshot.cell_form_id ||
        !ComputeLocalMapView(*frame, cursor.x, cursor.y, map_box, &view))
      return;
    world_width = frame->max_x - frame->min_x;
    world_height = frame->max_y - frame->min_y;
  } else {
    if (!(g_snapshot.capabilities & kCapWorldMap) ||
        !ComputeWorldMapView(g_snapshot, map_box, &view))
      return;
    world_width = g_snapshot.map_max_x - g_snapshot.map_min_x;
    world_height = g_snapshot.map_max_y - g_snapshot.map_min_y;
  }
  const float visible_width = world_width * (view.right - view.left);
  const float visible_height = world_height * (view.bottom - view.top);
  const float map_dx = dx * kLogicalWidth / static_cast<float>(map_box.w);
  const float map_dy = dy * kLogicalHeight / static_cast<float>(map_box.h);
  g_map_pan_x[map_index] =
      std::clamp(g_map_pan_x[map_index] - map_dx * visible_width,
                 -world_width, world_width);
  g_map_pan_y[map_index] =
      std::clamp(g_map_pan_y[map_index] + map_dy * visible_height,
                 -world_height, world_height);
  g_dirty = true;
}

void HandlePointerPacket(PointerSource source, const char* kind,
                         float x, float y) {
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0.0f || x > 1.0f ||
      y < 0.0f || y > 1.0f)
    return;
  if (std::strcmp(kind, "down") == 0) {
    if (g_transition_active) return;
    if (g_pointer_down && g_pointer_source != source) return;
    g_pointer_down = true;
    g_pointer_source = source;
    g_pointer_down_x = x;
    g_pointer_down_y = y;
    g_pointer_last_x = x;
    g_pointer_last_y = y;
    g_pointer_map_dragging = false;
  } else if (std::strcmp(kind, "move") == 0) {
    if (g_pointer_down && g_pointer_source == source) {
      const bool map_page = g_page == Page::kData &&
          (g_data_page == DataPage::kLocalMap ||
           g_data_page == DataPage::kWorldMap);
      const bool began_in_map = g_pointer_down_x >= 0.05f &&
          g_pointer_down_x <= 0.95f && g_pointer_down_y >= 0.106f &&
          g_pointer_down_y <= 0.856f;
      const float total_dx = x - g_pointer_down_x;
      const float total_dy = y - g_pointer_down_y;
      if (map_page && began_in_map &&
          (g_pointer_map_dragging || std::fabs(total_dx) >= 0.012f ||
           std::fabs(total_dy) >= 0.012f)) {
        g_pointer_map_dragging = true;
        ApplyMapPan(x - g_pointer_last_x, y - g_pointer_last_y);
      }
      g_pointer_last_x = x;
      g_pointer_last_y = y;
    }
  } else if (std::strcmp(kind, "up") == 0) {
    if (g_pointer_down && g_pointer_source == source) {
      const float dx = x - g_pointer_down_x;
      const float dy = y - g_pointer_down_y;
      // Commit only after direction lock.  Vertical list motion can therefore
      // never turn into a page change, and a small tap remains a tap.
      const bool map_page = g_page == Page::kData &&
          (g_data_page == DataPage::kLocalMap ||
           g_data_page == DataPage::kWorldMap);
      const bool began_in_map = g_pointer_down_x >= 0.05f &&
          g_pointer_down_x <= 0.95f && g_pointer_down_y >= 0.106f &&
          g_pointer_down_y <= 0.856f;
      if (g_pointer_map_dragging) {
        ApplyMapPan(x - g_pointer_last_x, y - g_pointer_last_y);
      } else if (map_page && began_in_map &&
          (std::fabs(dx) >= 0.025f || std::fabs(dy) >= 0.025f)) {
        ApplyMapPan(dx, dy);
      } else if (std::fabs(dx) >= 0.105f &&
          std::fabs(dx) > std::fabs(dy) * 1.35f) {
        StartPageTransition(dx < 0.0f ? 1 : -1);
      } else if (std::fabs(dy) >= 0.055f &&
                 std::fabs(dy) > std::fabs(dx) * 1.15f) {
        ScrollCurrentList(dy, g_pointer_down_x);
      } else if (std::fabs(dx) < 0.025f && std::fabs(dy) < 0.025f) {
        HandleNormalizedTap(x, y);
      }
    }
    if (g_pointer_source == source) {
      g_pointer_down = false;
      g_pointer_map_dragging = false;
      g_pointer_source = PointerSource::kNone;
    }
  } else if (std::strcmp(kind, "cancel") == 0) {
    if (g_pointer_source == source || source == PointerSource::kNone) {
      g_pointer_down = false;
      g_pointer_map_dragging = false;
      g_pointer_source = PointerSource::kNone;
    }
  }
}

void PumpTouchSocket() {
  if (g_touch_socket == INVALID_SOCKET) return;
  for (int packet = 0; packet < 16; ++packet) {
    char buffer[256] = {};
    sockaddr_in source = {};
    int source_size = sizeof(source);
    const int count = recvfrom(g_touch_socket, buffer, sizeof(buffer) - 1, 0,
                               reinterpret_cast<sockaddr*>(&source), &source_size);
    if (count == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error != WSAEWOULDBLOCK)
        LogRateLimited("touch-recv", 5000, LogLevel::kWarning,
                       "[INPUT] touch recv failed error=%d", error);
      break;
    }
    buffer[count] = '\0';
    if (ntohl(source.sin_addr.s_addr) == INADDR_LOOPBACK) {
      g_touch_peer = source;
      g_have_touch_peer = true;
    }
    float x = 0.0f, y = 0.0f;
    float value = 0.0f;
    if (std::sscanf(buffer, "tap %f %f", &x, &y) == 2 ||
        std::sscanf(buffer, "select %f %f", &x, &y) == 2) {
      HandleNormalizedTap(x, y);
    } else if (std::sscanf(buffer, "hold %f %f %f", &x, &y, &value) >= 2) {
      // A hold is intentionally not a tap.  The old behavior could equip or
      // consume an item merely while the user was trying to scroll.
      continue;
    } else if (std::sscanf(buffer, "down %f %f", &x, &y) == 2) {
      HandlePointerPacket(PointerSource::kUdp, "down", x, y);
    } else if (std::sscanf(buffer, "move %f %f", &x, &y) == 2) {
      HandlePointerPacket(PointerSource::kUdp, "move", x, y);
    } else if (std::sscanf(buffer, "up %f %f", &x, &y) == 2) {
      HandlePointerPacket(PointerSource::kUdp, "up", x, y);
    } else if (std::strncmp(buffer, "cancel", 6) == 0) {
      HandlePointerPacket(PointerSource::kUdp, "cancel", 0.0f, 0.0f);
    } else if (std::strcmp(buffer, "page next") == 0) {
      StartPageTransition(1);
    } else if (std::strcmp(buffer, "page previous") == 0) {
      StartPageTransition(-1);
    } else if (std::strcmp(buffer, "nav left") == 0) {
      ChangeSubPage(-1);
    } else if (std::strcmp(buffer, "nav right") == 0) {
      ChangeSubPage(1);
    } else if (std::strcmp(buffer, "nav up") == 0) {
      MoveListSelection(-1);
    } else if (std::strcmp(buffer, "nav down") == 0) {
      MoveListSelection(1);
    } else if (std::strcmp(buffer, "nav activate") == 0) {
      ActivateSelection();
    } else {
      LogRateLimited("touch-malformed", 5000, LogLevel::kWarning,
                     "[INPUT] ignored malformed touch packet bytes=%d payload=%.80s",
                     count, buffer);
    }
  }
}

void RefreshSnapshot() {
  const uint64_t now = GetTickCount64();
  if (g_action_error_until_ms && now >= g_action_error_until_ms) {
    g_action_error_until_ms = 0;
    g_action_error_form = 0;
    g_dirty = true;
  }
  if (g_pending_action_request_id &&
      now - g_pending_action_started_ms >= kActionTimeoutMs) {
    g_action_error_form = g_pending_action_form;
    g_action_error_until_ms = now + 2000;
    Log(LogLevel::kWarning,
        "[ACTION] request=%llu type=%u form=%08X timed out without confirmation",
        static_cast<unsigned long long>(g_pending_action_request_id),
        static_cast<unsigned>(g_pending_action_type), g_pending_action_form);
    ClearPendingAction();
    g_dirty = true;
  }
  const uint64_t sequence = LatestSequence();
  if (!sequence || sequence == g_snapshot_sequence) return;
  const uint64_t previous_epoch = g_have_snapshot ? g_snapshot.world_epoch : 0;
  const uint32_t previous_cell = g_have_snapshot ? g_snapshot.cell_form_id : 0;
  const uint64_t previous_vitals = g_snapshot.vitals_revision;
  const uint64_t previous_inventory = g_snapshot.inventory_revision;
  const uint64_t previous_data = g_snapshot.data_revision;
  const uint64_t previous_map = g_snapshot.map_revision;
  const uint64_t previous_radio = g_snapshot.radio_revision;
  const Lifecycle previous_lifecycle = g_snapshot.lifecycle;
  const uint32_t previous_supported = g_snapshot.supported_build;
  if (!CopyLatestSnapshot(&g_snapshot)) return;
  g_have_snapshot = true;
  g_snapshot_sequence = g_snapshot.sequence;
  const bool epoch_changed = previous_epoch != g_snapshot.world_epoch;
  if (epoch_changed) ResetSelections();
  else if (previous_cell != g_snapshot.cell_form_id) {
    g_map_pan_x[0] = 0.0f;
    g_map_pan_y[0] = 0.0f;
    g_map_zoom[0] = 1.0f;
  }

  bool relevant = epoch_changed ||
                  previous_lifecycle != g_snapshot.lifecycle ||
                  previous_supported != g_snapshot.supported_build;
  if (g_page == Page::kItems)
    relevant |= previous_vitals != g_snapshot.vitals_revision ||
                previous_inventory != g_snapshot.inventory_revision;
  else if (g_page == Page::kData)
    relevant |= previous_vitals != g_snapshot.vitals_revision ||
                previous_data != g_snapshot.data_revision ||
                previous_map != g_snapshot.map_revision ||
                previous_radio != g_snapshot.radio_revision;
  else
    relevant |= previous_vitals != g_snapshot.vitals_revision ||
                previous_data != g_snapshot.data_revision;

  if (g_pending_action_request_id &&
      g_snapshot.last_action_request_id == g_pending_action_request_id) {
    if (g_snapshot.last_action_result == ActionResult::kRejected) {
      g_action_error_form = g_pending_action_form;
      g_action_error_until_ms = GetTickCount64() + 2000;
      Log(LogLevel::kWarning,
          "[ACTION] request=%llu type=%u form=%08X rejected by game",
          static_cast<unsigned long long>(g_pending_action_request_id),
          static_cast<unsigned>(g_pending_action_type), g_pending_action_form);
      ClearPendingAction();
      relevant = true;
    } else if (g_snapshot.last_action_result == ActionResult::kDispatched &&
               PendingActionConfirmed(g_snapshot)) {
      Log(LogLevel::kInfo,
          "[ACTION] request=%llu type=%u form=%08X confirmed",
          static_cast<unsigned long long>(g_pending_action_request_id),
          static_cast<unsigned>(g_pending_action_type), g_pending_action_form);
      ClearPendingAction();
      relevant = true;
    }
  }
  g_dirty |= relevant;
}

void HandleMousePointer(const char* kind, LPARAM lparam) {
  const int px = GET_X_LPARAM(lparam) - g_present_x;
  const int py = GET_Y_LPARAM(lparam) - g_present_y;
  if (g_present_w <= 0 || g_present_h <= 0) return;
  const float x = std::clamp(px / static_cast<float>(g_present_w), 0.0f, 1.0f);
  const float y = std::clamp(py / static_cast<float>(g_present_h), 0.0f, 1.0f);
  HandlePointerPacket(PointerSource::kMouse, kind, x, y);
}

LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM wparam,
                           LPARAM lparam) {
  switch (message) {
    case WM_CLOSE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_CANCELMODE:
    case WM_CAPTURECHANGED:
      HandlePointerPacket(PointerSource::kMouse, "cancel", 0.0f, 0.0f);
      return 0;
    case WM_NCDESTROY:
      if (window == g_window) g_window = nullptr;
      break;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC target = BeginPaint(window, &paint);
      Present(target);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_LBUTTONDOWN:
      SetCapture(window);
      HandleMousePointer("down", lparam);
      return 0;
    case WM_MOUSEMOVE:
      if (wparam & MK_LBUTTON) HandleMousePointer("move", lparam);
      return 0;
    case WM_LBUTTONUP:
      HandleMousePointer("up", lparam);
      if (GetCapture() == window) ReleaseCapture();
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

struct MonitorChoice {
  RECT rect = {};
  bool found = false;
  int score = -1;
};

BOOL CALLBACK MonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  auto* choice = reinterpret_cast<MonitorChoice*>(data);
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info) || (info.dwFlags & MONITORINFOF_PRIMARY))
    return TRUE;
  const int width = info.rcMonitor.right - info.rcMonitor.left;
  const int height = info.rcMonitor.bottom - info.rcMonitor.top;
  const int score = (width == 1240 && height == 1080) ? 100 : 1;
  if (!choice->found || score > choice->score) {
    choice->found = true;
    choice->score = score;
    choice->rect = info.rcMonitor;
  }
  return TRUE;
}

bool CreateTransitionLayer(HDC* dc, HBITMAP* bitmap, HGDIOBJ* previous) {
  if (!dc || !bitmap || !previous) return false;
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = kLogicalWidth;
  info.bmiHeader.biHeight = -kLogicalHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  *dc = CreateCompatibleDC(g_back_dc);
  if (!*dc) return false;
  *bitmap = CreateDIBSection(*dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!*bitmap || !bits) return false;
  *previous = SelectObject(*dc, *bitmap);
  return *previous != nullptr && *previous != HGDI_ERROR;
}

bool CreateBackBuffer() {
  BITMAPINFO info = {};
  info.bmiHeader.biSize = sizeof(info.bmiHeader);
  info.bmiHeader.biWidth = kLogicalWidth;
  info.bmiHeader.biHeight = -kLogicalHeight;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  HDC screen = GetDC(nullptr);
  g_back_dc = CreateCompatibleDC(screen);
  ReleaseDC(nullptr, screen);
  if (!g_back_dc) return false;
  g_back_bitmap = CreateDIBSection(g_back_dc, &info, DIB_RGB_COLORS,
                                   &g_back_bits, nullptr, 0);
  if (!g_back_bitmap || !g_back_bits) return false;
  g_back_previous = SelectObject(g_back_dc, g_back_bitmap);
  g_font_small = CreateFontW(-22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Lucida Console");
  g_font_detail = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                              FIXED_PITCH | FF_MODERN, L"Lucida Console");
  g_font_body = CreateFontW(-28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                            FIXED_PITCH | FF_MODERN, L"Lucida Console");
  g_font_title = CreateFontW(-36, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Lucida Console");
  g_font_large = CreateFontW(-52, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                             FIXED_PITCH | FF_MODERN, L"Lucida Console");
  if (!g_font_small || !g_font_detail || !g_font_body || !g_font_title ||
      !g_font_large)
    return false;
  if (!CreateTransitionLayer(&g_background_dc, &g_background_bitmap,
                             &g_background_previous))
    return false;
  CrtBackground(g_background_dc);
  return CreateTransitionLayer(&g_transition_from_dc,
                               &g_transition_from_bitmap,
                               &g_transition_from_previous) &&
         CreateTransitionLayer(&g_transition_to_dc,
                               &g_transition_to_bitmap,
                               &g_transition_to_previous);
}

void DestroyBackBuffer() {
  g_transition_active = false;
  if (g_background_dc && g_background_previous)
    SelectObject(g_background_dc, g_background_previous);
  if (g_transition_from_dc && g_transition_from_previous)
    SelectObject(g_transition_from_dc, g_transition_from_previous);
  if (g_transition_to_dc && g_transition_to_previous)
    SelectObject(g_transition_to_dc, g_transition_to_previous);
  if (g_background_bitmap) DeleteObject(g_background_bitmap);
  if (g_transition_from_bitmap) DeleteObject(g_transition_from_bitmap);
  if (g_transition_to_bitmap) DeleteObject(g_transition_to_bitmap);
  if (g_background_dc) DeleteDC(g_background_dc);
  if (g_transition_from_dc) DeleteDC(g_transition_from_dc);
  if (g_transition_to_dc) DeleteDC(g_transition_to_dc);
  if (g_back_dc && g_back_previous) SelectObject(g_back_dc, g_back_previous);
  if (g_font_small) DeleteObject(g_font_small);
  if (g_font_detail) DeleteObject(g_font_detail);
  if (g_font_body) DeleteObject(g_font_body);
  if (g_font_title) DeleteObject(g_font_title);
  if (g_font_large) DeleteObject(g_font_large);
  for (CachedPen& cached : g_cached_pens) {
    if (cached.handle) DeleteObject(cached.handle);
    cached = {};
  }
  if (g_back_bitmap) DeleteObject(g_back_bitmap);
  if (g_back_dc) DeleteDC(g_back_dc);
  g_font_small = g_font_detail = g_font_body = g_font_title =
      g_font_large = nullptr;
  g_back_bitmap = nullptr;
  g_back_dc = nullptr;
  g_back_previous = nullptr;
  g_back_bits = nullptr;
  g_background_dc = nullptr;
  g_background_bitmap = nullptr;
  g_background_previous = nullptr;
  g_transition_from_dc = g_transition_to_dc = nullptr;
  g_transition_from_bitmap = g_transition_to_bitmap = nullptr;
  g_transition_from_previous = g_transition_to_previous = nullptr;
}

void StartTouchSocket() {
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return;
  g_winsock = true;
  g_touch_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_touch_socket == INVALID_SOCKET) return;
  u_long nonblocking = 1;
  if (ioctlsocket(g_touch_socket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    Log(LogLevel::kWarning,
        "[INPUT] failed to make touch socket nonblocking error=%d",
        WSAGetLastError());
    closesocket(g_touch_socket);
    g_touch_socket = INVALID_SOCKET;
    return;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(g_touch_port);
  if (bind(g_touch_socket, reinterpret_cast<sockaddr*>(&address),
           sizeof(address)) == SOCKET_ERROR) {
    Log(LogLevel::kWarning, "[INPUT] touch port %u unavailable error=%d",
        g_touch_port, WSAGetLastError());
    closesocket(g_touch_socket);
    g_touch_socket = INVALID_SOCKET;
    return;
  }
  Log(LogLevel::kInfo, "[INPUT] isolated panel touch listening 127.0.0.1:%u",
      g_touch_port);
}

bool CreatePanelWindow() {
  SecureZeroMemory(&g_snapshot, sizeof(g_snapshot));
  g_snapshot.size = sizeof(Snapshot);
  std::snprintf(g_snapshot.build_label,
                sizeof(g_snapshot.build_label),
                "bootstrap is initializing");
  MonitorChoice monitor = {};
  EnumDisplayMonitors(nullptr, nullptr, MonitorProc,
                      reinterpret_cast<LPARAM>(&monitor));
  RECT rect = monitor.rect;
  if (!monitor.found) {
    const LONG x = static_cast<LONG>(
        GetPrivateProfileIntW(L"panel", L"x", 0, g_ini_path));
    const LONG y = static_cast<LONG>(
        GetPrivateProfileIntW(L"panel", L"y", 720, g_ini_path));
    const LONG width = static_cast<LONG>(
        GetPrivateProfileIntW(L"panel", L"width", 1240, g_ini_path));
    const LONG height = static_cast<LONG>(
        GetPrivateProfileIntW(L"panel", L"height", 1080, g_ini_path));
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        x > LONG_MAX - width || y > LONG_MAX - height)
      return false;
    rect = {x, y, x + width, y + height};
  }
  g_width = rect.right - rect.left;
  g_height = rect.bottom - rect.top;
  if (g_width <= 0 || g_height <= 0 ||
      static_cast<size_t>(g_width) * g_height > kMaxPanelPixels)
    return false;
  const double scale = std::min(g_width / static_cast<double>(kLogicalWidth),
                                g_height / static_cast<double>(kLogicalHeight));
  g_present_w = std::max(1, static_cast<int>(kLogicalWidth * scale));
  g_present_h = std::max(1, static_cast<int>(kLogicalHeight * scale));
  g_present_x = (g_width - g_present_w) / 2;
  g_present_y = (g_height - g_present_h) / 2;
  WNDCLASSEXW klass = {};
  klass.cbSize = sizeof(klass);
  klass.lpfnWndProc = PanelProc;
  klass.hInstance = g_module;
  klass.lpszClassName = L"Fallout3SecondScreenPanel";
  klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  g_window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      klass.lpszClassName, L"Fallout 3 Dual-Screen Pip-Boy", WS_POPUP,
      rect.left, rect.top, g_width, g_height, nullptr, nullptr, g_module, nullptr);
  if (!g_window || !CreateBackBuffer()) return false;
  InitializeStatArtwork();
  StartTouchSocket();
  RefreshSnapshot();
  Render();
  ShowWindow(g_window, SW_SHOWNOACTIVATE);
  PresentNow();
  Log(LogLevel::kInfo,
      "[PANEL] independent CPU/GDI surface ready physical=%dx%d logical=%dx%d monitor=%s",
      g_width, g_height, kLogicalWidth, kLogicalHeight,
      monitor.found ? "secondary" : "configured-fallback");
  return true;
}

void CleanupPanel() {
  if (g_touch_socket != INVALID_SOCKET) {
    closesocket(g_touch_socket);
    g_touch_socket = INVALID_SOCKET;
  }
  if (g_winsock) {
    WSACleanup();
    g_winsock = false;
  }
  if (g_window) {
    DestroyWindow(g_window);
    g_window = nullptr;
  }
  DestroyBackBuffer();
  ShutdownVanillaUiAssets();
  g_vanilla_assets_initialized = false;
  g_vanilla_status_ready = false;
}

DWORD WINAPI PanelThread(void*) {
  g_thread_id.store(GetCurrentThreadId(), std::memory_order_release);
  if (!CreatePanelWindow()) {
    Log(LogLevel::kError, "[PANEL] failed to create independent lower window error=%lu",
        GetLastError());
    CleanupPanel();
    g_running.store(false, std::memory_order_release);
    return 0;
  }
  while (g_running.load(std::memory_order_acquire)) {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        g_running.store(false, std::memory_order_release);
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    PumpTouchSocket();
    RefreshSnapshot();
    FlushAssetDiagnostics();
    const bool animate_radio =
        !g_transition_active && g_have_snapshot &&
        g_snapshot.lifecycle == Lifecycle::kLive && g_page == Page::kData &&
        g_data_page == DataPage::kRadio &&
        SelectedRadioIsPlaying(g_snapshot);
    if (animate_radio) {
      const uint64_t now = GetTickCount64();
      if (!g_next_radio_animation_ms || now >= g_next_radio_animation_ms) {
        g_next_radio_animation_ms = now + 50;
        g_dirty = true;
      }
    } else {
      g_next_radio_animation_ms = 0;
    }
    if (g_dirty) {
      Render();
      PresentNow();
    }
    MsgWaitForMultipleObjects(0, nullptr, FALSE,
                              g_transition_active ? 16 : 50, QS_ALLINPUT);
  }
  CleanupPanel();
  return 0;
}

}  // namespace

bool StartPanel(HMODULE module) {
  if (g_running.exchange(true, std::memory_order_acq_rel)) return true;
  if (g_thread) {
    if (WaitForSingleObject(g_thread, 0) != WAIT_OBJECT_0) {
      Log(LogLevel::kError,
          "[PANEL] previous worker is still cleaning up; duplicate start rejected");
      g_running.store(false, std::memory_order_release);
      return false;
    }
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  g_thread_id.store(0, std::memory_order_release);
  SecureZeroMemory(&g_snapshot, sizeof(g_snapshot));
  g_snapshot.size = sizeof(Snapshot);
  g_have_snapshot = false;
  g_snapshot_sequence = 0;
  g_have_touch_peer = false;
  g_pointer_down = false;
  g_pointer_source = PointerSource::kNone;
  g_last_activation_ms = 0;
  g_next_asset_diagnostic_ms = 0;
  g_last_asset_diagnostics = {};
  g_have_asset_diagnostics = false;
  ResetSelections();
  UpdateRequestedDomains();
  g_module = module;
  const DWORD path_length = GetModuleFileNameW(module, g_ini_path, MAX_PATH);
  if (path_length && path_length < MAX_PATH) {
    wchar_t* slash = wcsrchr(g_ini_path, L'\\');
    if (slash)
      wcscpy_s(slash + 1,
               static_cast<size_t>(MAX_PATH - (slash + 1 - g_ini_path)),
               L"Fallout3SecondScreen.ini");
  }
  g_touch_port = static_cast<uint16_t>(std::clamp(
      GetPrivateProfileIntW(L"input", L"touch_port", kDefaultTouchPort,
                            g_ini_path),
      1024u, 65535u));
  DWORD created_thread_id = 0;
  g_thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0,
                          &created_thread_id);
  if (!g_thread) {
    g_running.store(false, std::memory_order_release);
    return false;
  }
  g_thread_id.store(created_thread_id, std::memory_order_release);
  if (!SetThreadPriority(g_thread, THREAD_PRIORITY_BELOW_NORMAL))
    Log(LogLevel::kWarning,
        "[PANEL] could not lower panel worker priority error=%lu",
        static_cast<unsigned long>(GetLastError()));
  return true;
}

void RequestPanelStop() {
  g_radio_page_requested.store(false, std::memory_order_release);
  g_effects_page_requested.store(false, std::memory_order_release);
  g_map_page_requested.store(false, std::memory_order_release);
  g_local_map_page_requested.store(false, std::memory_order_release);
  g_notes_page_requested.store(false, std::memory_order_release);
  g_condition_stats_requested.store(false, std::memory_order_release);
  g_special_stats_requested.store(false, std::memory_order_release);
  g_skills_stats_requested.store(false, std::memory_order_release);
  g_general_stats_requested.store(false, std::memory_order_release);
  g_game_data_requested.store(false, std::memory_order_release);
  g_running.store(false, std::memory_order_release);
  const DWORD thread_id = g_thread_id.load(std::memory_order_acquire);
  if (thread_id) PostThreadMessageW(thread_id, WM_QUIT, 0, 0);
  HANDLE thread = g_thread;
  if (thread && GetCurrentThreadId() != thread_id) {
    const DWORD wait = WaitForSingleObject(thread, 2000);
    if (wait == WAIT_OBJECT_0) {
      CloseHandle(thread);
      if (g_thread == thread) g_thread = nullptr;
      g_thread_id.store(0, std::memory_order_release);
    } else {
      Log(LogLevel::kWarning, "[PANEL] stop wait result=%lu", wait);
      // Keep the handle and thread id live. Closing and forgetting a worker
      // that still owns the panel/GDI/socket globals would allow a later start
      // to race its cleanup and can turn an orderly shutdown into a crash.
    }
  }
}

bool IsRadioPageRequested() {
  return g_radio_page_requested.load(std::memory_order_acquire);
}

bool IsEffectsPageRequested() {
  return g_effects_page_requested.load(std::memory_order_acquire);
}

bool IsMapPageRequested() {
  return g_map_page_requested.load(std::memory_order_acquire);
}

bool IsLocalMapPageRequested() {
  return g_local_map_page_requested.load(std::memory_order_acquire);
}

bool IsNotesPageRequested() {
  return g_notes_page_requested.load(std::memory_order_acquire);
}

bool IsConditionStatsRequested() {
  return g_condition_stats_requested.load(std::memory_order_acquire);
}

bool IsSpecialStatsRequested() {
  return g_special_stats_requested.load(std::memory_order_acquire);
}

bool IsSkillsStatsRequested() {
  return g_skills_stats_requested.load(std::memory_order_acquire);
}

bool IsGeneralStatsRequested() {
  return g_general_stats_requested.load(std::memory_order_acquire);
}

bool IsGameDataRequested() {
  return g_game_data_requested.load(std::memory_order_acquire);
}

}  // namespace fo3ds
