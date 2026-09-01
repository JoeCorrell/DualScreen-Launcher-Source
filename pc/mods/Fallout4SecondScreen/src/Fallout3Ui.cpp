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
#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

#include "Config.h"
#include "Fallout4UiAssets.h"
#include "Log.h"
#include "NativeActions.h"
#include "PanelInput.h"
#include "PipboyProtocol.h"
#include "UiAdapter.h"
#include "UiPerf.h"
#include "UiSnapshot.h"

namespace fo4ds {
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
enum class StatPage : uint8_t { kStatus, kSpecial, kPerks, kGeneral };
enum class StatusMode : uint8_t { kCondition, kRadiation, kEffects };
enum class DataPage : uint8_t {
  kLocalMap,
  kWorldMap,
  kQuests,
  kNotes,
  kRadio,
};
constexpr ItemCategory kItemPages[] = {
    ItemCategory::kWeapon, ItemCategory::kApparel, ItemCategory::kAid,
    ItemCategory::kMisc, ItemCategory::kJunk, ItemCategory::kMod,
    ItemCategory::kAmmo, ItemCategory::kFavorite};
constexpr int kItemPageCount =
    static_cast<int>(sizeof(kItemPages) / sizeof(kItemPages[0]));
constexpr int kStatPageCount = 4;
constexpr int kDataPageCount = 5;

HMODULE g_module = nullptr;
HANDLE g_thread = nullptr;
DWORD g_thread_id = 0;
std::atomic<bool> g_running{false};
PipboyDatabase* g_database = nullptr;
PipboySnapshot g_live_snapshot;
LocalMapState g_local_map;
bool g_local_map_requested = false;
bool g_actions_enabled = false;
uint32_t g_refresh_interval_ms = 100;
uint64_t g_next_snapshot_ms = 0;
std::atomic<bool> g_radio_page_requested{false};
std::atomic<bool> g_effects_page_requested{false};
std::atomic<bool> g_map_page_requested{false};
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
HFONT g_font_body = nullptr;
HFONT g_font_title = nullptr;
HFONT g_font_large = nullptr;
SOCKET g_touch_socket = INVALID_SOCKET;
sockaddr_in g_touch_peer = {};
bool g_have_touch_peer = false;
bool g_winsock = false;
uint16_t g_touch_port = kDefaultTouchPort;
PanelGestureArbiter g_gesture;
PanelPacketMapper g_packet_mapper;
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
uint32_t g_selected_general = 0;
uint32_t g_item_scroll[kItemPageCount] = {};
uint32_t g_quest_scroll = 0;
uint32_t g_note_scroll = 0;
uint32_t g_station_scroll = 0;
uint32_t g_perk_scroll = 0;
uint32_t g_general_scroll = 0;
uint32_t g_effect_scroll = 0;
uint32_t g_quest_objective_scroll = 0;
uint32_t g_note_text_scroll = 0;
uint32_t g_note_text_scroll_max = 0;
float g_map_pan_x[2] = {};
float g_map_pan_y[2] = {};
float g_map_zoom[2] = {1.0f, 1.0f};
uint32_t g_pending_action_form = 0;
uint64_t g_pending_action_inventory_revision = 0;
uint64_t g_pending_action_data_revision = 0;
uint64_t g_pending_action_radio_revision = 0;
uint64_t g_pending_action_started_ms = 0;
CommandType g_pending_action_type = CommandType::kNone;
uint64_t g_pending_action_token = 0;
uint64_t g_next_action_token = 1;
bool g_transition_active = false;
Page g_transition_from = Page::kItems;
Page g_transition_to = Page::kItems;
int g_transition_direction = 1;
uint64_t g_transition_started_ms = 0;
constexpr uint64_t kTransitionDurationMs = 230;
bool g_dirty = true;

struct PanelStartupContext {
  HANDLE event = nullptr;
  std::atomic<long> references{2};
  std::atomic<int> result{0};
  std::atomic<bool> signaled{false};
};

void ReleaseStartup(PanelStartupContext* context) {
  if (!context || context->references.fetch_sub(1) != 1) return;
  if (context->event) CloseHandle(context->event);
  delete context;
}

void CompleteStartup(PanelStartupContext* context, bool success) {
  if (!context) return;
  bool expected = false;
  if (!context->signaled.compare_exchange_strong(expected, true)) return;
  context->result.store(success ? 1 : -1, std::memory_order_release);
  SetEvent(context->event);
}

void ClearPendingAction() {
  g_pending_action_form = 0;
  g_pending_action_inventory_revision = 0;
  g_pending_action_data_revision = 0;
  g_pending_action_radio_revision = 0;
  g_pending_action_started_ms = 0;
  g_pending_action_type = CommandType::kNone;
  g_pending_action_token = 0;
}

bool QueueCommand(const Command& command) {
  if (!g_actions_enabled || !NativeActionsReady() ||
      !(g_snapshot.capabilities & kCapActions) ||
      !g_live_snapshot.ActionsSafe()) {
    LogRateLimited("action-blocked", 2000, LogLevel::kWarning,
                   "[ACTION] UI action blocked enabled=%u native_ready=%u capabilities=0x%08X safe=%u",
                   g_actions_enabled ? 1u : 0u,
                   NativeActionsReady() ? 1u : 0u, g_snapshot.capabilities,
                   g_live_snapshot.ActionsSafe() ? 1u : 0u);
    return false;
  }
  uint64_t token = g_next_action_token++;
  if (!token) token = g_next_action_token++;
  NativeActionRequest request = {};
  const bool built =
      BuildNativeActionRequest(command, g_live_snapshot, token, &request);
  const bool queued = built && QueueNativeAction(request);
  if (!queued) {
    Log(LogLevel::kWarning,
        "[ACTION] native request rejected type=%u ui_id=%08X built=%u",
        static_cast<unsigned>(command.type), command.form_id,
        built ? 1u : 0u);
    return false;
  }
  g_pending_action_token = token;
  Log(LogLevel::kInfo,
      "[ACTION] native request queued token=%llu type=%u ui_id=%08X",
      static_cast<unsigned long long>(token),
      static_cast<unsigned>(command.type), command.form_id);
  return queued;
}

struct Box {
  int x, y, w, h;
  bool Contains(int px, int py) const {
    return px >= x && py >= y && px < x + w && py < y + h;
  }
};

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

void Fill(HDC dc, const Box& box, COLORREF color) {
  RECT rect = {box.x, box.y, box.x + box.w, box.y + box.h};
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Stroke(HDC dc, const Box& box, COLORREF color, int width = 2) {
  HPEN pen = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, box.x, box.y, box.x + box.w, box.y + box.h);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

void Line(HDC dc, int x0, int y0, int x1, int y1, COLORREF color,
          int width = 2) {
  HPEN pen = width == 1
                 ? static_cast<HPEN>(GetStockObject(DC_PEN))
                 : CreatePen(PS_SOLID, width, color);
  if (width == 1) SetDCPenColor(dc, color);
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, x0, y0, nullptr);
  LineTo(dc, x1, y1);
  SelectObject(dc, old);
  if (width != 1) DeleteObject(pen);
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
  Text(dc, {box.x, box.y, box.w, 64}, title, g_font_title, kBright,
       DT_CENTER | DT_TOP | DT_WORDBREAK);
  if (!subtitle.empty())
    Text(dc, {box.x, box.y + 66, box.w, 38}, subtitle, g_font_small, kGreen,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  const int divider_y = box.y + (subtitle.empty() ? 70 : 106);
  Line(dc, box.x + 12, divider_y, box.x + box.w - 12, divider_y, kDim, 2);
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
  RECT target = {box.x, box.y - static_cast<int>(clamped), box.x + box.w,
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

// Artwork-provider seam for installer-generated rasters derived from the
// player's own Fallout 4 Interface.ba2. Keeping the boundary here avoids any
// dependency on Fallout 3 assets or the vanilla Pip-Boy menu lifecycle.
bool TryDrawFallout4Artwork(HDC dc, uint32_t form_id, const wchar_t* role,
                           const Box& box, uint32_t auxiliary_id = 0,
                           const char* swf_file = nullptr) {
  const Fallout4UiAssetBox bounds = {box.x, box.y, box.w, box.h};
  if (!role) return false;
  if (std::wcscmp(role, L"special") == 0)
    return DrawFallout4SpecialArt(dc, form_id, bounds);
  if (std::wcscmp(role, L"condition") == 0)
    return DrawFallout4ConditionArt(dc, form_id, auxiliary_id, bounds);
  if (std::wcscmp(role, L"perk") == 0)
    return DrawFallout4PerkArt(dc, auxiliary_id, form_id, swf_file, bounds);
  return false;
}

// Custom vector art keeps the updated Pip-Boy composition self-contained.
// It never opens the vanilla menu or loads assets from the Fallout 3 mod.
void DrawVaultBoyArtwork(HDC dc, const Box& box, int scene) {
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
    HPEN pen = CreatePen(PS_SOLID, width, kGreen);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Ellipse(dc, a.x, a.y, b.x, b.y);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
  };
  const auto polyline = [&](const POINT* source, int count, int width = 4) {
    if (!source || count < 2) return;
    POINT scaled[16] = {};
    count = std::min(count, 16);
    for (int i = 0; i < count; ++i)
      scaled[i] = point(source[i].x, source[i].y);
    HPEN pen = CreatePen(PS_SOLID, width, kGreen);
    HGDIOBJ old_pen = SelectObject(dc, pen);
    Polyline(dc, scaled, count);
    SelectObject(dc, old_pen);
    DeleteObject(pen);
  };

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
    line(87, 126, 31, 154, 6);
    line(31, 154, 12, 147, 5);
    line(151, 126, 207, 154, 6);
    line(207, 154, 228, 147, 5);
    ellipse(7, 138, 23, 157, 3);
    ellipse(218, 138, 234, 157, 3);
    return;
  }

  switch (scene % 4) {
    case 0:
      line(89, 127, 55, 151, 6); line(55, 151, 42, 119, 6);
      line(151, 127, 185, 151, 6); line(185, 151, 198, 119, 6);
      ellipse(34, 105, 50, 122, 3); ellipse(190, 105, 206, 122, 3);
      break;
    case 1:
      line(88, 128, 55, 171, 6); line(55, 171, 38, 185, 6);
      line(151, 128, 177, 92, 6); line(177, 92, 205, 76, 5);
      ellipse(29, 177, 45, 194, 3);
      break;
    case 2:
      line(88, 128, 64, 166, 6); line(64, 166, 95, 178, 6);
      line(151, 128, 176, 166, 6); line(176, 166, 146, 178, 6);
      break;
    default:
      line(89, 128, 55, 171, 6); line(55, 171, 38, 185, 6);
      line(151, 128, 181, 105, 6); line(181, 105, 181, 65, 5);
      ellipse(173, 50, 190, 68, 3);
      break;
  }

  switch (scene) {
    case 0:
      line(14, 86, 68, 86, 6); line(18, 68, 18, 104, 7);
      line(30, 63, 30, 109, 7); line(54, 63, 54, 109, 7);
      line(66, 68, 66, 104, 7); break;
    case 1:
      ellipse(16, 66, 57, 108, 5); line(52, 104, 73, 128, 6); break;
    case 2: {
      const POINT heart[] = {{18, 78}, {28, 66}, {41, 78}, {54, 66},
                             {66, 80}, {42, 112}, {18, 78}};
      polyline(heart, 7, 5); break;
    }
    case 3:
      line(12, 58, 72, 58, 4); line(72, 58, 72, 98, 4);
      line(72, 98, 42, 98, 4); line(42, 98, 30, 112, 4);
      line(30, 112, 30, 98, 4); line(30, 98, 12, 98, 4);
      line(12, 98, 12, 58, 4); break;
    case 4:
      ellipse(20, 55, 64, 99, 5); line(31, 98, 34, 116, 5);
      line(53, 98, 50, 116, 5); line(33, 116, 51, 116, 5); break;
    case 5:
      line(8, 65, 62, 65, 4); line(18, 83, 68, 83, 4);
      line(6, 101, 53, 101, 4); break;
    case 6:
      ellipse(14, 62, 41, 89, 4); ellipse(40, 62, 67, 89, 4);
      ellipse(14, 87, 41, 114, 4); ellipse(40, 87, 67, 114, 4);
      line(41, 104, 58, 128, 4); break;
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
      std::swprintf(position, std::size(position), L"X %.0f   Y %.0f",
                    state.player_x, state.player_y);
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

[[maybe_unused]] void RenderStatLegacy(HDC dc, const Snapshot& state) {
  const wchar_t* pages[] = {L"STATUS", L"S.P.E.C.I.A.L.", L"PERKS",
                            L"GENERAL"};
  SubNavigation(dc, pages, kStatPageCount,
                static_cast<int>(g_stat_page));
  Text(dc, {62, 112, 760, 64}, Wide(state.player_name), g_font_large, kBright);
  wchar_t level[96] = {};
  if (state.level >= 0 && state.xp >= 0 && state.xp_next >= 0)
    std::swprintf(level, 96, L"LEVEL %d    XP %d / %d", state.level, state.xp,
                  state.xp_next);
  else if (state.level >= 0 && state.xp >= 0)
    std::swprintf(level, 96, L"LEVEL %d    XP %d", state.level, state.xp);
  else if (state.level >= 0)
    std::swprintf(level, 96, L"LEVEL %d    XP --", state.level);
  else if (state.xp >= 0)
    std::swprintf(level, 96, L"LEVEL --    XP %d", state.xp);
  else
    std::swprintf(level, 96, L"LEVEL --    XP --");
  Text(dc, {760, 120, 420, 54}, level, g_font_body, kGreen,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  const uint32_t required_capability =
      g_stat_page == StatPage::kPerks
          ? kCapPerks
          : (g_stat_page == StatPage::kGeneral ? kCapGeneralStats
                                               : kCapStats);
  if (!(state.capabilities & required_capability)) {
    Brackets(dc, {54, 194, 1132, 766}, kDim, 24, 2);
    Text(dc, {90, 460, 1060, 110}, L"STATE UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  if (g_stat_page == StatPage::kStatus) {
    static const wchar_t* limbs[] = {L"HEAD", L"TORSO", L"LEFT ARM",
                                     L"RIGHT ARM", L"LEFT LEG", L"RIGHT LEG"};
    Brackets(dc, {54, 194, 548, 766}, kDim, 24, 2);
    Brackets(dc, {638, 194, 548, 766}, kDim, 24, 2);
    Text(dc, {78, 210, 500, 48}, L"VITALS", g_font_body, kBright);
    Text(dc, {78, 274, 100, 44}, L"HP", g_font_small, kGreen);
    Meter(dc, {174, 286, 382, 23}, state.health, state.max_health);
    Text(dc, {78, 350, 100, 44}, L"AP", g_font_small, kGreen);
    Meter(dc, {174, 362, 382, 23}, state.action_points,
          state.max_action_points);
    wchar_t supplies[128] = {};
    std::swprintf(supplies, std::size(supplies),
                  L"STIMPAKS  %ls     RADAWAY  %ls",
                  FormatInteger(state.stimpaks).c_str(),
                  FormatInteger(state.radaway).c_str());
    Text(dc, {78, 420, 478, 44}, supplies, g_font_small, kGreen,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {78, 476, 500, 42}, L"DAMAGE / RESISTANCE", g_font_body,
         kBright);
    static const wchar_t* damage_types[] = {
        L"PHYS", L"POISON", L"TYPE 3", L"ENERGY", L"TYPE 5", L"RAD"};
    for (uint32_t i = 0; i < kDamageTypeCount; ++i) {
      const int column = static_cast<int>(i / 3);
      const int row = static_cast<int>(i % 3);
      const int x = 82 + column * 242;
      const int y = 526 + row * 46;
      std::wstring value = FormatDecimal(state.total_damage[i]);
      value += L" / ";
      value += FormatDecimal(state.total_resistance[i]);
      Text(dc, {x, y, 82, 38}, damage_types[i], g_font_small, kGreen);
      Text(dc, {x + 82, y, 142, 38}, value, g_font_small, kBright,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    Text(dc, {78, 676, 500, 42}, L"LIMB CONDITION", g_font_body, kBright);
    for (uint32_t i = 0; i < kLimbCount; ++i) {
      const int column = static_cast<int>(i / 3);
      const int row = static_cast<int>(i % 3);
      const int x = 82 + column * 242;
      const int y = 724 + row * 68;
      Text(dc, {x, y, 224, 30}, limbs[i], g_font_small, kGreen);
      Meter(dc, {x, y + 34, 224, 16}, state.limbs[i], 100.0f);
    }
    Text(dc, {662, 210, 500, 48}, L"ACTIVE EFFECTS", g_font_body, kBright);
    for (uint32_t i = 0; i < state.effect_count && i < kMaxEffects && i < 10; ++i) {
      const UiEffectState& effect = state.effects[i];
      std::wstring label = Wide(effect.name);
      if (std::fabs(effect.magnitude) >= 0.005f)
        label += L"   " + FormatSigned(effect.magnitude) +
                 (effect.show_as_percent ? L"%" : L"");
      if (effect.seconds_remaining >= 0.0f)
        label += L"   " +
                 std::to_wstring(static_cast<int>(
                     std::ceil(effect.seconds_remaining))) + L"s";
      Text(dc, {666, 276 + static_cast<int>(i) * 62, 486, 52},
           label, g_font_small,
           effect.detrimental ? kBright : kGreen,
           DT_LEFT | DT_TOP | DT_WORDBREAK);
    }
    if (!(state.capabilities & kCapEffects))
      Text(dc, {674, 496, 472, 80}, L"LOADING ACTIVE EFFECTS",
           g_font_small, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    else if (!state.effect_count)
      Text(dc, {674, 496, 472, 80}, L"NO ACTIVE EFFECTS",
           g_font_small, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
  } else if (g_stat_page == StatPage::kSpecial) {
    Brackets(dc, {54, 194, 1132, 766}, kDim, 24, 2);
    static const wchar_t* initials[] = {L"S", L"P", L"E", L"C", L"I", L"A", L"L"};
    static const wchar_t* names[] = {L"STRENGTH", L"PERCEPTION", L"ENDURANCE",
                                     L"CHARISMA", L"INTELLIGENCE", L"AGILITY",
                                     L"LUCK"};
    for (uint32_t i = 0; i < kSpecialCount; ++i) {
      const int x = 82 + static_cast<int>(i) * 158;
      Text(dc, {x, 302, 112, 58}, initials[i], g_font_large, kBright,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {x, 388, 112, 60}, FormatValue(state.special[i]), g_font_large,
           kGreen, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {x - 18, 478, 148, 90}, names[i], g_font_small, kGreen,
           DT_CENTER | DT_TOP | DT_WORDBREAK);
    }
  } else if (g_stat_page == StatPage::kPerks) {
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
    if (selected) {
      Text(dc, {626, 240, 530, 66}, Wide(selected->name), g_font_title,
           kBright, DT_CENTER | DT_TOP | DT_WORDBREAK);
      Text(dc, {646, 340, 490, 420}, Wide(selected->description),
           g_font_body, kGreen, DT_CENTER | DT_TOP | DT_WORDBREAK);
      Text(dc, {646, 790, 490, 60},
           selected->rank ? L"RANK " + std::to_wstring(selected->rank) : L"",
           g_font_body, kBright,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
      Text(dc, {90, 478, 1060, 90}, L"NO PERKS",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  } else {
    Brackets(dc, {54, 194, 1132, 766}, kDim, 24, 2);
    const uint32_t total =
        std::min(state.general_stat_count, kMaxGeneralStats);
    g_general_scroll =
        std::min(g_general_scroll, total > 16 ? total - 16 : 0u);
    for (uint32_t visible = 0;
         visible < 16 && g_general_scroll + visible < total; ++visible) {
      const UiGeneralStatState& stat =
          state.general_stats[g_general_scroll + visible];
      const int column = static_cast<int>(visible / 8);
      const int row = static_cast<int>(visible % 8);
      const int x = 82 + column * 552;
      const int y = 220 + row * 86;
      std::wstring label;
      if (stat.category[0]) {
        label = Wide(stat.category);
        label += L" / ";
      }
      label += Wide(stat.name);
      Text(dc, {x, y, 390, 38}, label, g_font_small, kGreen,
           DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {x + 396, y, 106, 38}, FormatDecimal(stat.value),
           g_font_body, kBright, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      Line(dc, x, y + 48, x + 502, y + 48, kDim, 1);
    }
    if (!total)
      Text(dc, {90, 460, 1060, 110}, L"NO GENERAL STATS",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void RenderDomainUnavailable(HDC dc, const wchar_t* label) {
  Brackets(dc, {48, 108, 1144, 816}, kGreen, 28, 2);
  Text(dc, {100, 436, 1040, 120}, label, g_font_body, kDim,
       DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

void RenderStat(HDC dc, const Snapshot& state) {
  const wchar_t* pages[] = {L"STATUS", L"S.P.E.C.I.A.L.", L"PERKS",
                            L"GENERAL"};
  SubNavigation(dc, pages, kStatPageCount, static_cast<int>(g_stat_page));
  if (!(state.capabilities & kCapStats)) {
    RenderDomainUnavailable(dc, L"PLAYER STATUS IS LOADING");
    return;
  }

  if (g_stat_page == StatPage::kStatus) {
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    static const wchar_t* modes[] = {L"CND", L"RAD", L"EFF"};
    for (int i = 0; i < 3; ++i) {
      Box mode = {68, 158 + i * 72, 116, 56};
      const bool active = i == static_cast<int>(g_status_mode);
      if (active) {
        Fill(dc, mode, kSelection);
        Stroke(dc, mode, kBright, 2);
      }
      Text(dc, mode, modes[i], g_font_body, active ? kBright : kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }

    if (g_status_mode == StatusMode::kCondition) {
      if (!TryDrawFallout4Artwork(dc, state.body_flags, L"condition",
                                  {422, 142, 398, 650}, state.head_flags))
        DrawVaultBoyArtwork(dc, {422, 142, 398, 650}, -1);
      Meter(dc, {568, 132, 106, 22}, state.limbs[0], 100.0f);
      Meter(dc, {568, 398, 106, 22}, state.limbs[1], 100.0f);
      Meter(dc, {326, 306, 112, 22}, state.limbs[2], 100.0f);
      Meter(dc, {804, 306, 112, 22}, state.limbs[3], 100.0f);
      Meter(dc, {398, 694, 112, 22}, state.limbs[4], 100.0f);
      Meter(dc, {732, 694, 112, 22}, state.limbs[5], 100.0f);

      std::wstring identity = Wide(state.player_name);
      if (identity.empty()) identity = L"SOLE SURVIVOR";
      if (state.level >= 0)
        identity += L"  -  LEVEL " + std::to_wstring(state.level);
      Text(dc, {320, 816, 602, 54}, identity, g_font_body, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {946, 384, 188, 42}, L"STIMPAKS", g_font_small, kGreen);
      Text(dc, {1010, 384, 124, 42}, FormatInteger(state.stimpaks),
           g_font_small, kBright, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    } else if (g_status_mode == StatusMode::kRadiation) {
      constexpr Box scale = {446, 594, 678, 76};
      Line(dc, 446, 594, 1124, 594, kGreen, 3);
      Line(dc, 1124, 594, 1124, 668, kGreen, 3);
      for (int tick = 0; tick <= 20; ++tick) {
        const int x = scale.x + tick * scale.w / 20;
        const int height = tick % 5 == 0 ? 28 : 14;
        Line(dc, x, scale.y, x, scale.y + height, kGreen, 2);
      }
      Text(dc, {744, 534, 104, 48}, L"500", g_font_small, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {1044, 534, 100, 48}, L"1000", g_font_small, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      if (state.radiation >= 0.0f) {
        const float rads = std::clamp(state.radiation, 0.0f, 1000.0f);
        const int marker_x = scale.x + static_cast<int>(
            rads * static_cast<float>(scale.w) / 1000.0f);
        Line(dc, marker_x, 602, marker_x, 686, kBright, 4);
        Text(dc, {marker_x - 30, 666, 60, 38}, L"^", g_font_body, kBright,
             DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      }
      Line(dc, 218, 594, 430, 594, kGreen, 3);
      Text(dc, {220, 604, 150, 54}, L"RAD RESIST", g_font_body, kBright);
      Text(dc, {356, 604, 72, 54}, FormatValue(state.total_resistance[5]),
           g_font_body, kBright, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {448, 604, 110, 54}, L"RADS", g_font_body, kBright);
      Text(dc, {560, 604, 128, 54}, FormatValue(state.radiation),
           g_font_body, kBright, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
      Text(dc, {438, 744, 464, 48},
           L"RADAWAY  " + FormatInteger(state.radaway), g_font_body, kGreen,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
      const uint32_t visible =
          std::min<uint32_t>(count - g_effect_scroll, kVisibleEffects);
      for (uint32_t row = 0; row < visible; ++row) {
        const UiEffectState& effect = state.effects[g_effect_scroll + row];
        const int y = 154 + static_cast<int>(row) * 58;
        Text(dc, {238, y, 570, 50}, Wide(effect.name), g_font_body,
             effect.detrimental ? kBright : kGreen);
        std::wstring detail;
        if (std::fabs(effect.magnitude) >= 0.005f) {
          detail = FormatSigned(effect.magnitude);
          if (effect.show_as_percent) detail += L"%";
        }
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
    return;
  }

  if (g_stat_page == StatPage::kSpecial) {
    static const wchar_t* names[] = {L"STRENGTH", L"PERCEPTION", L"ENDURANCE",
                                     L"CHARISMA", L"INTELLIGENCE", L"AGILITY",
                                     L"LUCK"};
    static const wchar_t* descriptions[] = {
        L"Strength is a measure of your raw physical power. It affects how much you can carry and the damage of melee attacks.",
        L"Perception is your environmental awareness and sixth sense. It affects weapon accuracy in V.A.T.S.",
        L"Endurance is a measure of your overall physical fitness. It affects total Health and Action Point drain.",
        L"Charisma is your ability to charm and convince others. It affects success in dialogue and prices when bartering.",
        L"Intelligence is a measure of overall mental acuity and affects the number of Experience Points earned.",
        L"Agility is a measure of overall finesse and reflexes. It affects Action Points and your ability to sneak.",
        L"Luck measures your general good fortune and affects the recharge rate of Critical Hits."};
    g_selected_special = std::min<uint32_t>(g_selected_special,
                                             kSpecialCount - 1);
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    for (uint32_t i = 0; i < kSpecialCount; ++i) {
      Box row = {72, 142 + static_cast<int>(i) * 94, 500, 72};
      const std::wstring name = state.special_names[i][0]
                                    ? Wide(state.special_names[i])
                                    : names[i];
      std::wstring value = FormatValue(state.special[i]);
      if (state.special_modifiers[i] > 0)
        value = L"(+) " + value;
      else if (state.special_modifiers[i] < 0)
        value = L"(-) " + value;
      DrawAttributeRow(dc, row, name.c_str(), value,
                       i == g_selected_special);
    }
    if (!TryDrawFallout4Artwork(dc, g_selected_special, L"special",
                                {708, 126, 382, 432}))
      DrawVaultBoyArtwork(dc, {708, 126, 382, 432},
                          static_cast<int>(g_selected_special));
    Brackets(dc, {620, 550, 534, 334}, kGreen, 22, 2);
    const std::wstring selected_name =
        state.special_names[g_selected_special][0]
            ? Wide(state.special_names[g_selected_special])
            : names[g_selected_special];
    const std::wstring selected_description =
        state.special_descriptions[g_selected_special][0]
            ? Wide(state.special_descriptions[g_selected_special])
            : descriptions[g_selected_special];
    Text(dc, {646, 574, 486, 60}, selected_name, g_font_title,
         kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {654, 654, 470, 190}, selected_description,
         g_font_body, kGreen, DT_CENTER | DT_TOP | DT_WORDBREAK);
    return;
  }

  if (g_stat_page == StatPage::kPerks) {
    if (!(state.capabilities & kCapPerks)) {
      RenderDomainUnavailable(dc, L"PERK DATA IS LOADING");
      return;
    }
    Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
    Line(dc, 596, 130, 596, 914, kDim, 2);
    const PerkState* selected = nullptr;
    for (uint32_t i = 0; i < state.perk_count && i < kMaxPerks; ++i)
      if (state.perks[i].form_id == g_selected_perk) selected = &state.perks[i];
    if (!selected && state.perk_count) {
      selected = &state.perks[0];
      g_selected_perk = selected->form_id;
      g_perk_scroll = 0;
    }
    const uint32_t total = std::min(state.perk_count, kMaxPerks);
    constexpr uint32_t kVisiblePerks = 14;
    g_perk_scroll = std::min(
        g_perk_scroll, total > kVisiblePerks ? total - kVisiblePerks : 0u);
    for (uint32_t visible = 0;
         visible < kVisiblePerks && g_perk_scroll + visible < total;
         ++visible) {
      const PerkState& perk = state.perks[g_perk_scroll + visible];
      Box row = {68, 142 + static_cast<int>(visible) * 53, 510, 47};
      if (perk.form_id == g_selected_perk) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      Text(dc, {row.x + 8, row.y, 374, row.h}, Wide(perk.name), g_font_body,
           perk.form_id == g_selected_perk ? kBright : kGreen);
      std::wstring rank = perk.rank ? L"RANK " + std::to_wstring(perk.rank)
                                    : L"";
      if (perk.max_rank)
        rank += L"/" + std::to_wstring(perk.max_rank);
      Text(dc, {row.x + 382, row.y, 116, row.h}, rank, g_font_small, kGreen,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
    ScrollIndicators(dc, {64, 136, 518, 754}, g_perk_scroll, total,
                     kVisiblePerks);
    if (selected) {
      std::wstring rank = selected->rank
          ? L"RANK " + std::to_wstring(selected->rank) : L"";
      if (selected->max_rank)
        rank += L" / " + std::to_wstring(selected->max_rank);
      DrawDetailHeading(dc, {622, 154, 538, 120}, Wide(selected->name), rank);
      if (!TryDrawFallout4Artwork(dc, selected->form_id, L"perk",
                                  {746, 282, 290, 330},
                                  selected->clip_name, selected->swf_file))
        DrawVaultBoyArtwork(dc, {746, 282, 290, 330},
                            static_cast<int>(selected->form_id % 7));
      if (selected->description[0])
        Text(dc, {646, 636, 490, 212}, Wide(selected->description),
             g_font_body, kGreen, DT_CENTER | DT_TOP | DT_WORDBREAK);
      else
        Text(dc, {646, 684, 490, 90}, L"NO PERK DESCRIPTION AVAILABLE",
             g_font_small, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    } else {
      Text(dc, {90, 478, 1060, 90}, L"NO PERKS", g_font_body, kDim,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    return;
  }

  if (!(state.capabilities & kCapGeneralStats)) {
    RenderDomainUnavailable(dc, L"GENERAL STATISTICS ARE LOADING");
    return;
  }
  Brackets(dc, {54, 118, 1132, 810}, kDim, 24, 2);
  Line(dc, 728, 130, 728, 914, kDim, 2);
  const uint32_t total = std::min(state.general_stat_count, kMaxGeneralStats);
  constexpr uint32_t kVisibleGeneral = 11;
  if (total) g_selected_general = std::min(g_selected_general, total - 1);
  g_general_scroll = std::min(
      g_general_scroll,
      total > kVisibleGeneral ? total - kVisibleGeneral : 0u);
  for (uint32_t visible = 0;
       visible < kVisibleGeneral && g_general_scroll + visible < total;
       ++visible) {
    const uint32_t index = g_general_scroll + visible;
    const UiGeneralStatState& stat = state.general_stats[index];
    Box row = {72, 132 + static_cast<int>(visible) * 68, 640, 58};
    std::wstring label;
    if (stat.category[0]) label = Wide(stat.category) + L" / ";
    label += Wide(stat.name);
    DrawAttributeRow(dc, row, label.c_str(), FormatDecimal(stat.value),
                     index == g_selected_general);
  }
  ScrollIndicators(dc, {66, 126, 654, 754}, g_general_scroll, total,
                   kVisibleGeneral);
  if (total) {
    const UiGeneralStatState& selected =
        state.general_stats[g_selected_general];
    const std::wstring category = selected.category[0]
        ? Wide(selected.category) : L"GENERAL";
    DrawDetailHeading(dc, {760, 202, 354, 92}, Wide(selected.name), category);
    if (!TryDrawFallout4Artwork(dc, selected.form_id, L"general-stat",
                                {808, 294, 264, 372}))
      DrawVaultBoyArtwork(dc, {808, 294, 264, 372},
                          static_cast<int>(g_selected_general % 7));
    Text(dc, {760, 688, 354, 48}, L"RECORDED VALUE", g_font_small, kGreen,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Text(dc, {760, 742, 354, 72}, FormatDecimal(selected.value),
         g_font_large, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  } else {
    Text(dc, {90, 460, 1060, 110}, L"NO GENERAL STATS", g_font_body, kDim,
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
    case ItemCategory::kJunk: return L"JUNK";
    case ItemCategory::kMod: return L"MODS";
    case ItemCategory::kFavorite: return L"FAVORITES";
    default: return L"OTHER";
  }
}

int ItemCategoryIndex(ItemCategory category) {
  for (int i = 0; i < kItemPageCount; ++i)
    if (kItemPages[i] == category) return i;
  return 0;
}

bool ItemMatchesCategory(const ItemState& item, ItemCategory category) {
  return category == ItemCategory::kFavorite
             ? (item.flags & kItemFavorite) != 0
             : item.category == category;
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
  const wchar_t* category_names[kItemPageCount] = {};
  for (int i = 0; i < kItemPageCount; ++i)
    category_names[i] = CategoryName(kItemPages[i]);
  const int active_category = ItemCategoryIndex(g_item_category);
  SubNavigation(dc, category_names, kItemPageCount, active_category);
  Brackets(dc, {48, 108, 1144, 816}, kGreen, 28, 2);
  if (!(state.capabilities & kCapInventory)) {
    Text(dc, {84, 438, 1072, 100}, L"INVENTORY STATE UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    return;
  }
  Line(dc, 728, 120, 728, 924, kDim, 2);
  const ItemState* selected = nullptr;
  const ItemState* first = nullptr;
  uint32_t available = 0;
  for (uint32_t i = 0; i < state.item_count && i < kMaxItems; ++i) {
    const ItemState& item = state.items[i];
    if (!ItemMatchesCategory(item, g_item_category) || item.count <= 0)
      continue;
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
    if (!ItemMatchesCategory(item, g_item_category) || item.count <= 0)
      continue;
    if (matching++ < scroll) continue;
    if (shown < 16) {
      Box row = {62, 122 + static_cast<int>(shown) * 48, 648, 43};
      if (item.form_id == g_selected_item) {
        Fill(dc, row, kSelection);
        Stroke(dc, row, kBright, 2);
      }
      std::wstring name = Wide(item.name);
      if (item.flags & kItemEquipped) name = L"\x25A0  " + name;
      Text(dc, {row.x + 10, row.y, 520, row.h}, name, g_font_body,
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
    DrawDetailHeading(dc, {756, 122, 408, 92}, Wide(selected->name),
                      CategoryName(g_item_category));
    const bool has_description = selected->description[0] != '\0';
    Brackets(dc, {774, 232, 372, 190}, kDim, 18, 1);
    if (has_description)
      Text(dc, {792, 246, 336, 162}, Wide(selected->description),
           g_font_small, kGreen, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    else
      Text(dc, {792, 282, 336, 90}, L"NO DESCRIPTION DATA", g_font_small,
           kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    int stat_y = 446;
    const auto stat = [&](const wchar_t* label, const std::wstring& value) {
      ItemStatCell(dc, {766, stat_y, 388, 38}, label, value);
      stat_y += 44;
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

    const bool pending = g_pending_action_form == selected->form_id &&
                         GetTickCount64() - g_pending_action_started_ms < 3000;
    const bool supported = ItemSupportsAction(selected->category);
    const bool native_ready = NativeActionsReady();
    const bool state_safe = (state.capabilities & kCapActions) != 0;
    const bool actions = !pending && supported && g_actions_enabled &&
                         native_ready && state_safe &&
                         (state.capabilities & kCapInventory) != 0;
    Box action = {780, 842, 360, 64};
    Stroke(dc, action, actions ? kGreen : kDim, 2);
    const bool equipped = (selected->flags & kItemEquipped) != 0;
    const wchar_t* action_label = L"NO ACTION";
    if (!supported)
      action_label = L"NO ACTION";
    else if (pending)
      action_label = L"APPLYING...";
    else if (!g_actions_enabled)
      action_label = L"ACTIONS DISABLED";
    else if (!native_ready)
      action_label = L"ACTIONS OFFLINE";
    else if (!state_safe)
      action_label = L"GAME BUSY";
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

float BaseMapRadius(const Snapshot& state, bool local) {
  if (local && g_local_map.width && g_local_map.height) {
    const float across_x = g_local_map.northeast_x - g_local_map.northwest_x;
    const float across_y = g_local_map.northeast_y - g_local_map.northwest_y;
    const float down_x = g_local_map.southwest_x - g_local_map.northwest_x;
    const float down_y = g_local_map.southwest_y - g_local_map.northwest_y;
    const float extent = std::max(std::hypot(across_x, across_y),
                                  std::hypot(down_x, down_y));
    if (std::isfinite(extent) && extent > 1.0f) return extent * 0.55f;
  }
  float radius = local ? 4096.0f : 10000.0f;
  if (!local) {
    for (uint32_t i = 0; i < state.marker_count && i < kMaxMarkers; ++i) {
      const MarkerState& marker = state.markers[i];
      if (!(marker.flags & kMarkerVisible) ||
          (marker.flags & kMarkerLocal))
        continue;
      radius = std::max(radius, std::fabs(marker.x - state.player_x));
      radius = std::max(radius, std::fabs(marker.y - state.player_y));
    }
    radius *= 1.12f;
  }
  return std::max(radius, 1.0f);
}

struct LocalRasterProjection {
  bool valid = false;
  int destination_x = 0;
  int destination_y = 0;
  int destination_width = 0;
  int destination_height = 0;
  float source_x = 0.0f;
  float source_y = 0.0f;
  float source_width = 0.0f;
  float source_height = 0.0f;
};

bool LocalMapUv(float world_x, float world_y, float* u, float* v) {
  if (!u || !v) return false;
  const float edge_x = g_local_map.northeast_x - g_local_map.northwest_x;
  const float edge_y = g_local_map.northeast_y - g_local_map.northwest_y;
  const float down_x = g_local_map.southwest_x - g_local_map.northwest_x;
  const float down_y = g_local_map.southwest_y - g_local_map.northwest_y;
  const float delta_x = world_x - g_local_map.northwest_x;
  const float delta_y = world_y - g_local_map.northwest_y;
  const float determinant = edge_x * down_y - edge_y * down_x;
  if (!std::isfinite(determinant) || std::fabs(determinant) <= 0.0001f)
    return false;
  *u = (delta_x * down_y - delta_y * down_x) / determinant;
  *v = (edge_x * delta_y - edge_y * delta_x) / determinant;
  return std::isfinite(*u) && std::isfinite(*v);
}

bool ProjectLocalMapPoint(const LocalRasterProjection& projection,
                          float world_x, float world_y, int* x, int* y) {
  if (!projection.valid || !x || !y) return false;
  float u = 0.0f;
  float v = 0.0f;
  if (!LocalMapUv(world_x, world_y, &u, &v)) return false;
  const float source_x = u * g_local_map.width;
  const float source_y = v * g_local_map.height;
  const float normalized_x =
      (source_x - projection.source_x) / projection.source_width;
  const float normalized_y =
      (source_y - projection.source_y) / projection.source_height;
  if (normalized_x < 0.0f || normalized_x > 1.0f || normalized_y < 0.0f ||
      normalized_y > 1.0f)
    return false;
  *x = projection.destination_x +
       static_cast<int>(normalized_x * projection.destination_width);
  *y = projection.destination_y +
       static_cast<int>(normalized_y * projection.destination_height);
  return true;
}

bool DrawLocalMapRaster(HDC dc, const Box& box, float view_x, float view_y,
                        float zoom, LocalRasterProjection* projection) {
  if (projection) *projection = {};
  if (!dc || g_local_map.pixels.empty() || !g_local_map.width ||
      !g_local_map.height || g_local_map.width > 32768 ||
      g_local_map.height > 32768)
    return false;
  struct PaletteBitmapInfo {
    BITMAPINFOHEADER header;
    RGBQUAD colors[256];
  } info = {};
  info.header.biSize = sizeof(info.header);
  info.header.biWidth = static_cast<LONG>(g_local_map.width);
  info.header.biHeight = -static_cast<LONG>(g_local_map.height);
  info.header.biPlanes = 1;
  info.header.biBitCount = 8;
  info.header.biCompression = BI_RGB;
  info.header.biClrUsed = 256;
  const size_t stride = (static_cast<size_t>(g_local_map.width) + 3u) & ~3u;
  info.header.biSizeImage = static_cast<DWORD>(stride * g_local_map.height);
  for (int i = 0; i < 256; ++i) {
    info.colors[i].rgbRed = static_cast<BYTE>(i / 4);
    info.colors[i].rgbGreen = static_cast<BYTE>(i);
    info.colors[i].rgbBlue = static_cast<BYTE>(i / 6);
  }
  static uint64_t packed_revision = 0;
  static std::vector<uint8_t> packed;
  const uint8_t* pixels = g_local_map.pixels.data();
  if (stride != g_local_map.width) {
    if (packed_revision != g_local_map.revision) {
      packed.assign(stride * g_local_map.height, 0);
      for (uint32_t row = 0; row < g_local_map.height; ++row)
        std::memcpy(packed.data() + static_cast<size_t>(row) * stride,
                    g_local_map.pixels.data() +
                        static_cast<size_t>(row) * g_local_map.width,
                    g_local_map.width);
      packed_revision = g_local_map.revision;
    }
    pixels = packed.data();
  }
  float center_u = 0.5f;
  float center_v = 0.5f;
  LocalMapUv(view_x, view_y, &center_u, &center_v);
  zoom = std::clamp(zoom, 0.5f, 4.0f);
  const float source_width = std::min<float>(
      g_local_map.width, std::max(1.0f, g_local_map.width / zoom));
  const float source_height = std::min<float>(
      g_local_map.height, std::max(1.0f, g_local_map.height / zoom));
  const float source_left = std::clamp(
      center_u * g_local_map.width - source_width * 0.5f, 0.0f,
      g_local_map.width - source_width);
  const float source_top = std::clamp(
      center_v * g_local_map.height - source_height * 0.5f, 0.0f,
      g_local_map.height - source_height);
  const int inset = 8;
  const int available_width = box.w - inset * 2;
  const int available_height = box.h - inset * 2;
  const float scale = std::min(
      available_width / source_width, available_height / source_height);
  const int width = std::max(1, static_cast<int>(source_width * scale));
  const int height = std::max(1, static_cast<int>(source_height * scale));
  const int left = box.x + (box.w - width) / 2;
  const int top = box.y + (box.h - height) / 2;
  if (StretchDIBits(dc, left, top, width, height,
                    static_cast<int>(source_left),
                    static_cast<int>(source_top),
                    std::max(1, static_cast<int>(source_width)),
                    std::max(1, static_cast<int>(source_height)), pixels,
                    reinterpret_cast<const BITMAPINFO*>(&info),
                    DIB_RGB_COLORS, SRCCOPY) == static_cast<int>(GDI_ERROR))
    return false;
  if (projection) {
    projection->valid = true;
    projection->destination_x = left;
    projection->destination_y = top;
    projection->destination_width = width;
    projection->destination_height = height;
    projection->source_x = source_left;
    projection->source_y = source_top;
    projection->source_width = source_width;
    projection->source_height = source_height;
  }
  return true;
}

void RenderMapData(HDC dc, const Snapshot& state, bool local) {
  const Box map_box = {62, 114, 1116, 810};
  const int map_right = map_box.x + map_box.w;
  const int map_bottom = map_box.y + map_box.h;
  Stroke(dc, map_box, kDim, 2);

  if (!(state.capabilities & kCapMap)) {
    Text(dc, {110, 458, 1020, 90}, L"MAP DATA UNAVAILABLE",
         g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const int saved_map_dc = SaveDC(dc);
  IntersectClipRect(dc, map_box.x + 1, map_box.y + 1, map_right, map_bottom);
  const float origin_x = local && g_live_snapshot.local_player_valid
                             ? g_live_snapshot.local_player_x
                             : state.player_x;
  const float origin_y = local && g_live_snapshot.local_player_valid
                             ? g_live_snapshot.local_player_y
                             : state.player_y;
  const int map_index = local ? 0 : 1;
  const float base_radius = BaseMapRadius(state, local);
  const float zoom = std::clamp(g_map_zoom[map_index], 0.5f, 4.0f);
  const float radius = base_radius / zoom;
  const float view_x = origin_x + g_map_pan_x[map_index];
  const float view_y = origin_y + g_map_pan_y[map_index];
  LocalRasterProjection local_projection;
  const bool drew_local_raster =
      local && DrawLocalMapRaster(dc, map_box, view_x, view_y, zoom,
                                  &local_projection);

  for (int x = map_box.x + 62; x < map_right; x += 92)
    Line(dc, x, map_box.y + 2, x, map_bottom - 2, RGB(20, 70, 39), 1);
  for (int y = map_box.y + 58; y < map_bottom; y += 82)
    Line(dc, map_box.x + 2, y, map_right - 2, y, RGB(20, 70, 39), 1);

  const int center_x = map_box.x + map_box.w / 2;
  const int center_y = map_box.y + map_box.h / 2;
  int player_x = center_x + static_cast<int>(
      (origin_x - view_x) / radius * (map_box.w / 2 - 58));
  int player_y = center_y - static_cast<int>(
      (origin_y - view_y) / radius * (map_box.h / 2 - 58));
  if (drew_local_raster)
    ProjectLocalMapPoint(local_projection, origin_x, origin_y, &player_x,
                         &player_y);
  const float player_rotation =
      local && g_live_snapshot.local_player_valid
          ? g_live_snapshot.local_player_rotation
          : state.player_heading;
  const float heading = player_rotation * 0.0174532925199f;
  const int arrow_x = player_x + static_cast<int>(std::sin(heading) * 34.0f);
  const int arrow_y = player_y - static_cast<int>(std::cos(heading) * 34.0f);
  Line(dc, player_x, player_y, arrow_x, arrow_y, kBright, 6);
  Line(dc, arrow_x, arrow_y,
       arrow_x - static_cast<int>(std::sin(heading + 0.75f) * 15.0f),
       arrow_y + static_cast<int>(std::cos(heading + 0.75f) * 15.0f),
       kBright, 4);
  Line(dc, arrow_x, arrow_y,
       arrow_x - static_cast<int>(std::sin(heading - 0.75f) * 15.0f),
       arrow_y + static_cast<int>(std::cos(heading - 0.75f) * 15.0f),
       kBright, 4);
  Brackets(dc, {player_x - 76, player_y - 76, 152, 152}, kGreen, 25, 2);

  uint32_t drawn = 0;
  for (uint32_t i = 0; i < state.marker_count && i < kMaxMarkers; ++i) {
    const MarkerState& marker = state.markers[i];
    if (!(marker.flags & kMarkerVisible) ||
        ((marker.flags & kMarkerLocal) != 0) != local)
      continue;
    const float dx = marker.x - view_x;
    const float dy = marker.y - view_y;
    if (local && (std::fabs(dx) > radius || std::fabs(dy) > radius)) continue;
    int x = center_x + static_cast<int>(
        std::clamp(dx / radius, -1.0f, 1.0f) * (map_box.w / 2 - 58));
    int y = center_y - static_cast<int>(
        std::clamp(dy / radius, -1.0f, 1.0f) * (map_box.h / 2 - 58));
    if (drew_local_raster &&
        !ProjectLocalMapPoint(local_projection, marker.x, marker.y, &x, &y))
      continue;
    const bool discovered = (marker.flags & kMarkerDiscovered) != 0;
    const COLORREF color = discovered ? kBright : kDim;
    if (marker.flags & kMarkerQuest) {
      Line(dc, x, y - 8, x + 8, y, color, 2);
      Line(dc, x + 8, y, x, y + 8, color, 2);
      Line(dc, x, y + 8, x - 8, y, color, 2);
      Line(dc, x - 8, y, x, y - 8, color, 2);
    } else if (marker.flags & kMarkerPowerArmor) {
      Stroke(dc, {x - 7, y - 7, 15, 15}, color, 2);
    } else {
      Fill(dc, {x - 5, y - 5, 11, 11}, color);
    }
    if (drawn < 12 && marker.name[0])
      Text(dc, {x + 12, y - 23, 230, 40}, Wide(marker.name), g_font_small,
           discovered ? kGreen : kDim);
    ++drawn;
  }

  Fill(dc, {map_box.x + 2, map_bottom - 60, map_box.w - 4, 58},
       RGB(5, 31, 16));
  std::wstring footer = Wide(state.location);
  if (footer.empty()) footer = local ? L"CURRENT CELL" : L"CURRENT LOCATION";
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
  RestoreDC(dc, saved_map_dc);
}

void RenderData(HDC dc, const Snapshot& state) {
  const wchar_t* pages[] = {L"LOCAL MAP", L"WORLD MAP", L"QUESTS",
                            L"NOTES", L"RADIO"};
  SubNavigation(dc, pages, kDataPageCount, static_cast<int>(g_data_page));
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
      Text(dc, {90, 440, 1070, 90}, L"QUEST STATE UNAVAILABLE",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      return;
    }
    Line(dc, 596, 120, 596, 952, kDim, 2);
    uint32_t first_active = 0;
    bool selected_active = false;
    for (uint32_t i = 0; i < state.quest_count && i < kMaxQuests; ++i) {
      const UiQuestState& quest = state.quests[i];
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
      const UiQuestState& quest = state.quests[i];
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
        DrawDetailHeading(dc, {622, 126, 272, 104}, Wide(quest.title),
                          L"STAGE " + std::to_wstring(quest.stage));
        int objective_top = 244;
        if (quest.description[0]) {
          Text(dc, {630, 244, 522, 132}, Wide(quest.description), g_font_small,
               kGreen, DT_LEFT | DT_TOP | DT_WORDBREAK);
          objective_top = 392;
        }
        Line(dc, 622, objective_top, 1160, objective_top, kDim, 2);
        int y = objective_top + 18;
        const uint32_t objective_total =
            std::min(state.objective_count, kMaxObjectives);
        const uint32_t begin = std::min(quest.objective_begin,
                                        objective_total);
        const uint32_t end = begin + std::min(quest.objective_count,
                                               objective_total - begin);
        constexpr uint32_t kVisibleObjectives = 6;
        const uint32_t count = end - begin;
        g_quest_objective_scroll = std::min(
            g_quest_objective_scroll,
            count > kVisibleObjectives ? count - kVisibleObjectives : 0u);
        const uint32_t visible_begin = begin + g_quest_objective_scroll;
        const uint32_t visible_end =
            std::min(end, visible_begin + kVisibleObjectives);
        for (uint32_t objective = visible_begin; objective < visible_end;
             ++objective) {
          const ObjectiveState& value = state.objectives[objective];
          const wchar_t* marker = (value.flags & kObjectiveFailed) ? L"[!] " :
                                  (value.flags & kObjectiveCompleted) ? L"[X] " :
                                                                       L"[ ] ";
          Text(dc, {630, y, 522, 62}, marker + Wide(value.text), g_font_small,
               (value.flags & kObjectiveCompleted) ? kDim : kBright,
               DT_LEFT | DT_TOP | DT_WORDBREAK);
          y += 68;
        }
        ScrollIndicators(dc, {624, objective_top + 8, 532, 416},
                         g_quest_objective_scroll, count,
                         kVisibleObjectives);
        const bool tracking_pending =
            g_pending_action_form == quest.form_id &&
            GetTickCount64() - g_pending_action_started_ms < 3000;
        const bool can_track = (state.capabilities & kCapActions) != 0 &&
                               (state.capabilities & kCapQuests) != 0 &&
                               !tracking_pending;
        Box track = {906, 132, 238, 62};
        Stroke(dc, track, can_track ? kGreen : kDim, 2);
        Text(dc, track,
             tracking_pending ? L"APPLYING..." :
             ((quest.flags & kQuestTracked) ? L"UNTRACK QUEST" : L"TRACK QUEST"),
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
    if (!(state.capabilities & kCapInventory)) {
      Text(dc, {90, 440, 1070, 90}, L"NOTE STATE UNAVAILABLE",
           g_font_body, kDim, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
      Text(dc, {row.x + 8, row.y, row.w - 16, row.h}, Wide(note.title),
           g_font_body, note.form_id == g_selected_note ? kBright : kGreen);
    }
    ScrollIndicators(dc, {58, 116, 524, 786}, g_note_scroll, note_total, 16);
    if (selected) {
      DrawDetailHeading(dc, {622, 132, 538, 94}, Wide(selected->title));
      if (selected->text[0]) {
        DrawWrappedTextScrollable(dc, {632, 244, 518, 632},
                                  Wide(selected->text), g_font_body, kGreen,
                                  g_note_text_scroll,
                                  &g_note_text_scroll_max);
        g_note_text_scroll = std::min(g_note_text_scroll,
                                      g_note_text_scroll_max);
      } else {
        g_note_text_scroll = 0;
        g_note_text_scroll_max = 0;
        Text(dc, {642, 438, 498, 110},
             L"NO TRANSCRIPT AVAILABLE",
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

[[maybe_unused]] void RenderRadioLegacy(HDC dc, const Snapshot& state) {
  Text(dc, {66, 118, 1110, 56}, L"AVAILABLE SIGNALS", g_font_title, kBright);
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
  bool selection_exists = false;
    const UiRadioState* selected_station = nullptr;
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
    if (state.stations[i].form_id == g_selected_station) {
      selection_exists = true;
      selected_station = &state.stations[i];
    }
  if (!selection_exists) g_selected_station = state.stations[0].form_id;
  if (!selected_station) selected_station = &state.stations[0];
  const uint32_t station_total = std::min(state.station_count, kMaxStations);
  g_station_scroll = std::min(g_station_scroll,
      station_total > 13 ? station_total - 13 : 0u);
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i) {
    const UiRadioState& station = state.stations[i];
    if (i < g_station_scroll) continue;
    const uint32_t visible = i - g_station_scroll;
    if (visible >= 13) continue;
    Box row = {68, 194 + static_cast<int>(visible) * 47, 1096, 43};
    const bool selected = station.form_id == g_selected_station;
    if (selected) {
      Fill(dc, row, kSelection);
      Stroke(dc, row, kBright, 2);
    }
    std::wstring label = (station.flags & kStationTuned) ? L">> " : L"   ";
    label += Wide(station.name);
    Text(dc, {row.x + 8, row.y, 850, row.h}, label, g_font_body,
         selected ? kBright : kGreen);
    wchar_t frequency[32] = {};
    if (station.frequency > 0.0f)
      std::swprintf(frequency, 32, L"%.1f MHz", station.frequency);
    Text(dc, {920, row.y, 220, row.h}, frequency, g_font_body, kDim,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  const bool pending = g_pending_action_form == selected_station->form_id &&
      (g_pending_action_type == CommandType::kTuneRadio ||
       g_pending_action_type == CommandType::kRadioOff) &&
      GetTickCount64() - g_pending_action_started_ms < 3000;
  const bool can_act = !pending &&
      (state.capabilities & kCapActions) &&
      (selected_station->flags & kStationAvailable);
  const bool tuned = (selected_station->flags & kStationTuned) != 0;
  Box action = {404, 842, 432, 64};
  Stroke(dc, action, can_act ? kGreen : kDim, 2);
  Text(dc, action,
       pending ? L"APPLYING..." :
       (tuned ? L"TURN RADIO OFF" : L"TUNE STATION"),
       g_font_body, can_act ? kBright : kDim,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void RenderRadio(HDC dc, const Snapshot& state) {
  Text(dc, {66, 118, 640, 56}, L"AVAILABLE SIGNALS", g_font_title, kBright);
  if (!(state.capabilities & kCapRadio)) {
    RenderDomainUnavailable(dc, L"RADIO STATE IS LOADING");
    return;
  }
  if (!state.station_count) {
    Text(dc, {90, 440, 1070, 90}, L"NO RADIO SIGNALS", g_font_body, kDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const UiRadioState* selected = nullptr;
  for (uint32_t i = 0; i < state.station_count && i < kMaxStations; ++i)
    if (state.stations[i].form_id == g_selected_station)
      selected = &state.stations[i];
  if (!selected) {
    selected = &state.stations[0];
    g_selected_station = selected->form_id;
    g_station_scroll = 0;
  }
  const uint32_t total = std::min(state.station_count, kMaxStations);
  constexpr uint32_t kVisibleStations = 13;
  g_station_scroll = std::min(
      g_station_scroll,
      total > kVisibleStations ? total - kVisibleStations : 0u);
  Line(dc, 728, 120, 728, 924, kDim, 2);
  for (uint32_t visible = 0;
       visible < kVisibleStations && g_station_scroll + visible < total;
       ++visible) {
    const UiRadioState& station = state.stations[g_station_scroll + visible];
    Box row = {68, 194 + static_cast<int>(visible) * 47, 630, 43};
    const bool active = station.form_id == g_selected_station;
    if (active) {
      Fill(dc, row, kSelection);
      Stroke(dc, row, kBright, 2);
    }
    std::wstring label = (station.flags & kStationTuned) ? L">> " : L"   ";
    label += Wide(station.name);
    Text(dc, {row.x + 8, row.y, 454, row.h}, label, g_font_body,
         active ? kBright : kGreen);
    wchar_t frequency[32] = {};
    if (station.frequency > 0.0f)
      std::swprintf(frequency, std::size(frequency), L"%.1f", station.frequency);
    Text(dc, {row.x + 470, row.y, 140, row.h}, frequency, g_font_small, kDim,
         DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
  }
  ScrollIndicators(dc, {62, 184, 642, 626}, g_station_scroll, total,
                   kVisibleStations);

  const bool pending = g_pending_action_form == selected->form_id &&
      (g_pending_action_type == CommandType::kTuneRadio ||
       g_pending_action_type == CommandType::kRadioOff) &&
      GetTickCount64() - g_pending_action_started_ms < 3000;
  const bool can_act = !pending && (state.capabilities & kCapActions) &&
                       (selected->flags & kStationAvailable);
  const bool tuned = (selected->flags & kStationTuned) != 0;
  const float signal = selected->signal >= 0.0f
      ? std::clamp(selected->signal, 0.0f, 100.0f) : 100.0f;
  Text(dc, {754, 212, 400, 62}, Wide(selected->name), g_font_title, kBright,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  constexpr int graph_left = 770;
  constexpr int graph_right = 1144;
  constexpr int graph_top = 316;
  constexpr int graph_bottom = 736;
  Line(dc, graph_left, graph_bottom, graph_right, graph_bottom, kGreen, 3);
  Line(dc, graph_right, graph_top, graph_right, graph_bottom, kGreen, 3);
  for (int tick = 0; tick <= 24; ++tick) {
    const int x = graph_left + tick * (graph_right - graph_left) / 24;
    const int height = tick % 4 == 0 ? 28 : 14;
    Line(dc, x, graph_bottom, x, graph_bottom - height, kGreen, 2);
  }
  for (int tick = 0; tick <= 20; ++tick) {
    const int y = graph_top + tick * (graph_bottom - graph_top) / 20;
    const int width = tick % 5 == 0 ? 28 : 14;
    Line(dc, graph_right, y, graph_right - width, y, kGreen, 2);
  }
  const int carrier_y = graph_bottom - static_cast<int>(
      signal * static_cast<float>(graph_bottom - graph_top) / 100.0f);
  Line(dc, graph_left, carrier_y, graph_right, carrier_y, kBright, 4);
  std::wstring signal_label = selected->signal >= 0.0f
      ? L"SIGNAL  " + FormatPercent(selected->signal)
      : L"SIGNAL RECEIVED";
  if (selected->frequency > 0.0f) {
    wchar_t frequency[40] = {};
    std::swprintf(frequency, std::size(frequency), L"   %.1f MHz",
                  selected->frequency);
    signal_label += frequency;
  }
  Text(dc, {graph_left, graph_bottom + 18, graph_right - graph_left, 48},
       signal_label, g_font_small, kGreen,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Box action = {894, 126, 250, 62};
  Stroke(dc, action, can_act ? kGreen : kDim, 2);
  Text(dc, action,
       pending ? L"APPLYING..." :
       (tuned ? L"RADIO OFF" : L"TUNE"),
       g_font_small, can_act ? kBright : kDim,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void RenderUnsupported(HDC dc, const Snapshot& state) {
  Brackets(dc, {48, 108, 1144, 858}, kGreen, 28, 2);
  Text(dc, {100, 286, 1040, 80}, L"STATE UNAVAILABLE",
       g_font_title, kBright, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, {120, 388, 1000, 130}, Wide(state.build_label), g_font_body, kGreen,
       DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

void RenderPage(HDC dc, Page page, const Snapshot& state) {
  if (!dc) return;
  Header(dc, state, page);
  if (!state.supported_build || state.lifecycle == Lifecycle::kUnsupported) {
    RenderUnsupported(dc, state);
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
    g_radio_page_requested.store(
        g_page == Page::kData && g_data_page == DataPage::kRadio,
        std::memory_order_release);
    g_effects_page_requested.store(
        g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
            g_status_mode == StatusMode::kEffects,
        std::memory_order_release);
    g_map_page_requested.store(
        g_page == Page::kData &&
            (g_data_page == DataPage::kLocalMap ||
             g_data_page == DataPage::kWorldMap),
        std::memory_order_release);
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
  SetStretchBltMode(target, HALFTONE);
  SetBrushOrgEx(target, 0, 0, nullptr);
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
  GdiFlush();
}

void ResetSelections() {
  g_selected_item = 0;
  g_selected_quest = 0;
  g_selected_station = 0;
  g_selected_note = 0;
  g_selected_perk = 0;
  g_selected_special = 0;
  g_selected_general = 0;
  std::fill(g_item_scroll, g_item_scroll + kItemPageCount, 0u);
  g_quest_scroll = 0;
  g_note_scroll = 0;
  g_station_scroll = 0;
  g_perk_scroll = 0;
  g_general_scroll = 0;
  g_effect_scroll = 0;
  g_quest_objective_scroll = 0;
  g_note_text_scroll = 0;
  g_note_text_scroll_max = 0;
  std::fill(g_map_pan_x, g_map_pan_x + 2, 0.0f);
  std::fill(g_map_pan_y, g_map_pan_y + 2, 0.0f);
  std::fill(g_map_zoom, g_map_zoom + 2, 1.0f);
  ClearPendingAction();
}

void HandleTap(int x, int y) {
  if (g_transition_active) return;
  if (!g_have_snapshot || !g_snapshot.supported_build) return;
  if (g_page == Page::kStat && x >= 52 && x < 1184 &&
      y >= 950 && y < 1008) {
    g_stat_page = static_cast<StatPage>(std::clamp(
        (x - 52) / (1136 / kStatPageCount), 0, kStatPageCount - 1));
    g_effects_page_requested.store(
                                   g_stat_page == StatPage::kStatus &&
                                       g_status_mode == StatusMode::kEffects,
                                   std::memory_order_release);
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
             x >= 68 && x < 184 && y >= 158 && y < 374) {
    g_status_mode = static_cast<StatusMode>(
        std::clamp((y - 158) / 72, 0, 2));
    g_effects_page_requested.store(g_status_mode == StatusMode::kEffects,
                                   std::memory_order_release);
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSpecial &&
             x >= 72 && x < 572 && y >= 142 && y < 800) {
    g_selected_special = std::min<uint32_t>(
        static_cast<uint32_t>((y - 142) / 94), kSpecialCount - 1);
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kPerks &&
             x >= 68 && x < 578 && y >= 142 && y < 884) {
    const uint32_t row = g_perk_scroll +
                         static_cast<uint32_t>((y - 142) / 53);
    if (row < g_snapshot.perk_count && row < kMaxPerks) {
      g_selected_perk = g_snapshot.perks[row].form_id;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kGeneral &&
             x >= 72 && x < 712 && y >= 132 && y < 880) {
    const uint32_t row = g_general_scroll +
                         static_cast<uint32_t>((y - 132) / 68);
    if (row < g_snapshot.general_stat_count && row < kMaxGeneralStats) {
      g_selected_general = row;
      g_dirty = true;
      SendHaptic("haptic selection");
    }
  } else if (g_page == Page::kItems) {
    if (x >= 52 && x < 1184 && y >= 950 && y < 1008) {
      const int tab = std::clamp(
          (x - 52) / (1136 / kItemPageCount), 0, kItemPageCount - 1);
      g_item_category = kItemPages[tab];
      g_selected_item = 0;
      g_dirty = true;
      SendHaptic("haptic selection");
      return;
    }
    if (x >= 62 && x < 710 && y >= 122 && y < 890) {
      const uint32_t requested_row =
          g_item_scroll[ItemCategoryIndex(g_item_category)] +
          static_cast<uint32_t>((y - 122) / 48);
      uint32_t row = 0;
      for (uint32_t i = 0; i < g_snapshot.item_count; ++i) {
        const ItemState& item = g_snapshot.items[i];
        if (!ItemMatchesCategory(item, g_item_category) || item.count <= 0)
          continue;
        if (row++ == requested_row) {
          g_selected_item = item.form_id;
          g_dirty = true;
          SendHaptic("haptic selection");
          return;
        }
      }
    }
    if (Box{780, 842, 360, 64}.Contains(x, y) &&
        (g_snapshot.capabilities & kCapActions) &&
        (g_snapshot.capabilities & kCapInventory) && g_selected_item) {
      if (g_pending_action_form == g_selected_item &&
          GetTickCount64() - g_pending_action_started_ms < 3000)
        return;
      for (uint32_t i = 0; i < g_snapshot.item_count; ++i) {
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
        if (QueueCommand(command)) {
          g_pending_action_form = item.form_id;
          g_pending_action_inventory_revision =
              g_snapshot.inventory_revision;
          g_pending_action_data_revision = g_snapshot.data_revision;
          g_pending_action_started_ms = GetTickCount64();
          g_pending_action_type = command.type;
          g_dirty = true;
          SendHaptic("haptic action");
        }
        break;
      }
    }
  } else if (g_page == Page::kData) {
    if (x >= 52 && x < 1184 && y >= 950 && y < 1008) {
      g_data_page = static_cast<DataPage>(std::clamp((x - 52) / 227, 0, 4));
      g_radio_page_requested.store(g_data_page == DataPage::kRadio,
                                   std::memory_order_release);
      g_map_page_requested.store(g_data_page == DataPage::kLocalMap ||
                                     g_data_page == DataPage::kWorldMap,
                                 std::memory_order_release);
      g_dirty = true;
      SendHaptic("haptic selection");
      return;
    }
    if (g_data_page == DataPage::kLocalMap ||
        g_data_page == DataPage::kWorldMap) {
      const int map_index = g_data_page == DataPage::kLocalMap ? 0 : 1;
      if (Box{1050, 130, 52, 52}.Contains(x, y)) {
        g_map_zoom[map_index] =
            std::max(0.5f, g_map_zoom[map_index] / 1.25f);
        g_dirty = true;
        SendHaptic("haptic selection");
        return;
      }
      if (Box{1110, 130, 52, 52}.Contains(x, y)) {
        g_map_zoom[map_index] =
            std::min(4.0f, g_map_zoom[map_index] * 1.25f);
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
      for (uint32_t i = 0; i < g_snapshot.quest_count; ++i) {
        const UiQuestState& quest = g_snapshot.quests[i];
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
        Box{906, 132, 238, 62}.Contains(x, y) && g_selected_quest &&
        (g_snapshot.capabilities & kCapQuests)) {
      if (g_pending_action_form == g_selected_quest &&
          GetTickCount64() - g_pending_action_started_ms < 3000)
        return;
      for (uint32_t i = 0; i < g_snapshot.quest_count; ++i) {
        const UiQuestState& quest = g_snapshot.quests[i];
        if (quest.form_id != g_selected_quest) continue;
        Command command = {};
        command.type = CommandType::kTrackQuest;
        command.world_epoch = g_snapshot.world_epoch;
        command.form_id = quest.form_id;
        if (QueueCommand(command)) {
          g_pending_action_form = quest.form_id;
          g_pending_action_inventory_revision =
              g_snapshot.inventory_revision;
          g_pending_action_data_revision = g_snapshot.data_revision;
          g_pending_action_started_ms = GetTickCount64();
          g_pending_action_type = command.type;
          g_dirty = true;
          SendHaptic("haptic action");
        }
        break;
      }
    }
    if (g_data_page == DataPage::kRadio &&
        Box{894, 126, 250, 62}.Contains(x, y) && g_selected_station &&
        (g_snapshot.capabilities & kCapRadio)) {
      if (g_pending_action_form == g_selected_station &&
          (g_pending_action_type == CommandType::kTuneRadio ||
           g_pending_action_type == CommandType::kRadioOff) &&
          GetTickCount64() - g_pending_action_started_ms < 3000)
        return;
      for (uint32_t i = 0; i < g_snapshot.station_count; ++i) {
        const UiRadioState& station = g_snapshot.stations[i];
        if (station.form_id != g_selected_station) continue;
        Command command = {};
        command.type = (station.flags & kStationTuned)
            ? CommandType::kRadioOff : CommandType::kTuneRadio;
        command.world_epoch = g_snapshot.world_epoch;
        command.form_id = station.form_id;
        if (QueueCommand(command)) {
          g_pending_action_form = station.form_id;
          g_pending_action_inventory_revision =
              g_snapshot.inventory_revision;
          g_pending_action_data_revision = g_snapshot.data_revision;
          g_pending_action_radio_revision = g_snapshot.radio_revision;
          g_pending_action_started_ms = GetTickCount64();
          g_pending_action_type = command.type;
          g_dirty = true;
          SendHaptic("haptic action");
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
      if (ItemMatchesCategory(item, g_item_category) && item.count > 0)
        ++total;
    }
    return AdjustScroll(&g_item_scroll[ItemCategoryIndex(g_item_category)],
                        total, 16, delta);
  }
  if (g_page == Page::kStat && g_stat_page == StatPage::kPerks)
    return AdjustScroll(&g_perk_scroll,
                        std::min(g_snapshot.perk_count, kMaxPerks), 14, delta);
  if (g_page == Page::kStat && g_stat_page == StatPage::kGeneral)
    return AdjustScroll(
        &g_general_scroll,
        std::min(g_snapshot.general_stat_count, kMaxGeneralStats), 11, delta);
  if (g_page == Page::kStat && g_stat_page == StatPage::kStatus &&
      g_status_mode == StatusMode::kEffects)
    return AdjustScroll(&g_effect_scroll,
                        std::min(g_snapshot.effect_count, kMaxEffects), 12,
                        delta);
  if (g_page != Page::kData) return false;
  if (g_data_page == DataPage::kQuests) {
    if (start_x >= 0.48f) {
      for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
        const UiQuestState& quest = g_snapshot.quests[i];
        if (quest.form_id == g_selected_quest)
          return AdjustScroll(&g_quest_objective_scroll,
                              std::min(quest.objective_count,
                                       g_snapshot.objective_count),
                              6, delta);
      }
      return false;
    }
    uint32_t total = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i)
      if (g_snapshot.quests[i].flags & kQuestActive) ++total;
    return AdjustScroll(&g_quest_scroll, total, 16, delta);
  }
  if (g_data_page == DataPage::kNotes && start_x >= 0.48f) {
    const int pixel_delta = delta * 48;
    return AdjustScroll(&g_note_text_scroll, g_note_text_scroll_max + 1, 1,
                        pixel_delta);
  }
  if (g_data_page == DataPage::kNotes)
    return AdjustScroll(&g_note_scroll,
                        std::min(g_snapshot.note_count, kMaxNotes), 16, delta);
  if (g_data_page == DataPage::kRadio)
    return AdjustScroll(&g_station_scroll,
                        std::min(g_snapshot.station_count, kMaxStations), 13,
                        delta);
  return false;
}

void ChangeSubPage(int direction) {
  if (!direction) return;
  const auto advance = [direction](int value, int count) {
    return (value + (direction > 0 ? 1 : count - 1)) % count;
  };
  if (g_page == Page::kItems) {
    const int current = ItemCategoryIndex(g_item_category);
    g_item_category =
        kItemPages[advance(current, kItemPageCount)];
    g_selected_item = 0;
  } else if (g_page == Page::kData) {
    g_data_page = static_cast<DataPage>(
        advance(static_cast<int>(g_data_page), kDataPageCount));
    g_radio_page_requested.store(g_data_page == DataPage::kRadio,
                                 std::memory_order_release);
    g_map_page_requested.store(g_data_page == DataPage::kLocalMap ||
                                   g_data_page == DataPage::kWorldMap,
                               std::memory_order_release);
  } else {
    g_stat_page = static_cast<StatPage>(
        advance(static_cast<int>(g_stat_page), kStatPageCount));
    g_effects_page_requested.store(g_stat_page == StatPage::kStatus &&
                                       g_status_mode == StatusMode::kEffects,
                                   std::memory_order_release);
  }
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
      if (!ItemMatchesCategory(item, g_item_category) || item.count <= 0)
        continue;
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
      if (!ItemMatchesCategory(item, g_item_category) || item.count <= 0)
        continue;
      if (row++ == target) {
        g_selected_item = item.form_id;
        const int category = ItemCategoryIndex(g_item_category);
        if (target < g_item_scroll[category]) g_item_scroll[category] = target;
        if (target >= g_item_scroll[category] + 16)
          g_item_scroll[category] = target - 15;
        break;
      }
    }
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kStatus) {
    const uint32_t current = static_cast<uint32_t>(g_status_mode);
    g_status_mode = static_cast<StatusMode>(
        RelativeIndex(current, 3, direction));
    g_effects_page_requested.store(g_status_mode == StatusMode::kEffects,
                                   std::memory_order_release);
  } else if (g_page == Page::kStat && g_stat_page == StatPage::kSpecial) {
    g_selected_special = RelativeIndex(g_selected_special, kSpecialCount,
                                       direction);
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
    const uint32_t total =
        std::min(g_snapshot.general_stat_count, kMaxGeneralStats);
    if (!total) return;
    g_selected_general = RelativeIndex(g_selected_general, total, direction);
    if (g_selected_general < g_general_scroll)
      g_general_scroll = g_selected_general;
    if (g_selected_general >= g_general_scroll + 11)
      g_general_scroll = g_selected_general - 10;
  } else if (g_page == Page::kData &&
             g_data_page == DataPage::kQuests) {
    uint32_t total = 0;
    uint32_t current = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
      const UiQuestState& quest = g_snapshot.quests[i];
      if (!(quest.flags & kQuestActive)) continue;
      if (quest.form_id == g_selected_quest) current = total;
      ++total;
    }
    if (!total) return;
    const uint32_t target = RelativeIndex(current, total, direction);
    uint32_t row = 0;
    for (uint32_t i = 0; i < g_snapshot.quest_count && i < kMaxQuests; ++i) {
      const UiQuestState& quest = g_snapshot.quests[i];
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
  } else if (g_page == Page::kData &&
             (g_data_page == DataPage::kLocalMap ||
              g_data_page == DataPage::kWorldMap)) {
    const int map_index = g_data_page == DataPage::kLocalMap ? 0 : 1;
    g_map_zoom[map_index] = direction < 0
        ? std::min(4.0f, g_map_zoom[map_index] * 1.25f)
        : std::max(0.5f, g_map_zoom[map_index] / 1.25f);
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
    HandleTap(1025, 163);
  else if (g_page == Page::kData && g_data_page == DataPage::kRadio)
    HandleTap(1018, 157);
  else if (g_page == Page::kData &&
           (g_data_page == DataPage::kLocalMap ||
            g_data_page == DataPage::kWorldMap)) {
    const int map_index = g_data_page == DataPage::kLocalMap ? 0 : 1;
    g_map_pan_x[map_index] = 0.0f;
    g_map_pan_y[map_index] = 0.0f;
    g_dirty = true;
    SendHaptic("haptic selection");
  }
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

void DispatchGesture(const PanelGesture& gesture) {
  if (g_transition_active && gesture.kind != PanelGestureKind::kNone) return;
  const bool map_page = g_page == Page::kData &&
      (g_data_page == DataPage::kLocalMap ||
       g_data_page == DataPage::kWorldMap);
  const bool began_in_map = gesture.x >= 0.05f && gesture.x <= 0.95f &&
                            gesture.y >= 0.106f && gesture.y <= 0.856f;
  if (map_page && began_in_map &&
      (gesture.kind == PanelGestureKind::kPage ||
       gesture.kind == PanelGestureKind::kScroll ||
       gesture.kind == PanelGestureKind::kDrag)) {
    const bool local = g_data_page == DataPage::kLocalMap;
    const int map_index = local ? 0 : 1;
    const float base_radius = BaseMapRadius(g_snapshot, local);
    const float radius = base_radius /
        std::clamp(g_map_zoom[map_index], 0.5f, 4.0f);
    g_map_pan_x[map_index] = std::clamp(
        g_map_pan_x[map_index] - gesture.delta_x * 2.0f * radius,
        -base_radius * 2.0f, base_radius * 2.0f);
    g_map_pan_y[map_index] = std::clamp(
        g_map_pan_y[map_index] + gesture.delta_y * 2.0f * radius,
        -base_radius * 2.0f, base_radius * 2.0f);
    g_dirty = true;
    SendHaptic("haptic selection");
    return;
  }
  switch (gesture.kind) {
    case PanelGestureKind::kTap:
      HandleNormalizedTap(gesture.x, gesture.y);
      break;
    case PanelGestureKind::kHold:
      // Holds are reserved for future contextual actions; never synthesize a
      // tap because the input arbiter has already classified them separately.
      break;
    case PanelGestureKind::kPage:
      StartPageTransition(gesture.direction);
      break;
    case PanelGestureKind::kScroll:
      ScrollCurrentList(gesture.value, gesture.x);
      break;
    case PanelGestureKind::kNavLeft:
      ChangeSubPage(-1);
      break;
    case PanelGestureKind::kNavRight:
      ChangeSubPage(1);
      break;
    case PanelGestureKind::kNavUp:
      MoveListSelection(-1);
      break;
    case PanelGestureKind::kNavDown:
      MoveListSelection(1);
      break;
    case PanelGestureKind::kNavActivate:
      ActivateSelection();
      break;
    default:
      break;
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
    if (ntohl(source.sin_addr.s_addr) != INADDR_LOOPBACK) {
      LogRateLimited("touch-nonloopback", 5000, LogLevel::kWarning,
                     "[INPUT] rejected non-loopback touch packet");
      continue;
    }
    PanelPacket parsed;
    if (!ParsePanelPacket(buffer, static_cast<size_t>(count), &parsed)) {
      buffer[count] = '\0';
      LogRateLimited("touch-malformed", 5000, LogLevel::kWarning,
                     "[INPUT] ignored malformed touch packet bytes=%d payload=%.80s",
                     count, buffer);
      continue;
    }
    // Controller commands are sent from a one-shot UDP socket. Do not replace
    // the persistent Android touch socket used for haptic replies with that
    // already-closed ephemeral endpoint (and never trust malformed traffic as
    // a feedback destination).
    if (PanelPacketCarriesPointerInput(parsed.kind)) {
      g_touch_peer = source;
      g_have_touch_peer = true;
    }
    if (parsed.kind == PanelPacketKind::kDown) g_gesture.Reset();
    PanelPacket content_packet;
    const PanelViewport viewport = {g_width, g_height, g_present_x, g_present_y,
                                    g_present_w, g_present_h};
    if (!g_packet_mapper.Map(parsed, viewport, &content_packet)) continue;
    DispatchGesture(g_gesture.Consume(content_packet));
  }
}

void RefreshSnapshot() {
  const uint64_t now = GetTickCount64();
  NativeActionResult action_result = {};
  while (PollNativeActionResult(&action_result)) {
    const bool succeeded =
        action_result.code == NativeActionResultCode::kSucceeded;
    Log(succeeded ? LogLevel::kInfo : LogLevel::kWarning,
        "[ACTION] native result token=%llu kind=%u result=%s state=%u",
        static_cast<unsigned long long>(action_result.token),
        static_cast<unsigned>(action_result.kind),
        NativeActionResultCodeLabel(action_result.code),
        action_result.state ? 1u : 0u);
    if (action_result.token == g_pending_action_token && !succeeded) {
      ClearPendingAction();
      g_dirty = true;
    }
  }
  if (g_pending_action_form &&
      now - g_pending_action_started_ms >= 3000) {
    Log(LogLevel::kWarning,
        "[ACTION] UI confirmation timed out token=%llu type=%u ui_id=%08X",
        static_cast<unsigned long long>(g_pending_action_token),
        static_cast<unsigned>(g_pending_action_type), g_pending_action_form);
    ClearPendingAction();
    g_dirty = true;
  }
  if (!g_database || now < g_next_snapshot_ms) return;
  g_next_snapshot_ms = now + g_refresh_interval_ms;
  if (!g_database->CaptureIfChanged(g_live_snapshot.revision,
                                    &g_live_snapshot))
    return;
  const uint64_t previous_epoch = g_have_snapshot ? g_snapshot.world_epoch : 0;
  const uint64_t previous_vitals = g_snapshot.vitals_revision;
  const uint64_t previous_inventory = g_snapshot.inventory_revision;
  const uint64_t previous_data = g_snapshot.data_revision;
  const uint64_t previous_map = g_snapshot.map_revision;
  const uint64_t previous_radio = g_snapshot.radio_revision;
  const Lifecycle previous_lifecycle = g_snapshot.lifecycle;
  const uint32_t previous_supported = g_snapshot.supported_build;
  AdaptPipboySnapshot(g_live_snapshot,
                      g_actions_enabled && NativeActionsReady(), &g_snapshot);
  g_have_snapshot = true;
  g_snapshot_sequence = g_snapshot.sequence;
  if (previous_vitals != g_snapshot.vitals_revision) {
    LogRateLimited("ui-state-vitals", 2000, LogLevel::kInfo,
                   "[STATE] UI vitals rev=%llu hp=%.1f/%.1f ap=%.1f/%.1f level=%d effects=%u perks=%u",
                   static_cast<unsigned long long>(g_snapshot.vitals_revision),
                   g_snapshot.health, g_snapshot.max_health,
                   g_snapshot.action_points, g_snapshot.max_action_points,
                   g_snapshot.level, g_snapshot.effect_count,
                   g_snapshot.perk_count);
  }
  if (previous_inventory != g_snapshot.inventory_revision) {
    LogRateLimited("ui-state-inventory", 1000, LogLevel::kInfo,
                   "[STATE] UI inventory rev=%llu version=%u items=%u full_rebuilds=%llu incremental=%llu",
                   static_cast<unsigned long long>(g_snapshot.inventory_revision),
                   g_live_snapshot.inventory_version, g_snapshot.item_count,
                   static_cast<unsigned long long>(
                       g_live_snapshot.inventory_full_rebuilds),
                   static_cast<unsigned long long>(
                       g_live_snapshot.inventory_incremental_refreshes));
  }
  if (previous_data != g_snapshot.data_revision) {
    LogRateLimited("ui-state-data", 1000, LogLevel::kInfo,
                   "[STATE] UI data rev=%llu quests=%u objectives=%u notes=%u workshops=%u",
                   static_cast<unsigned long long>(g_snapshot.data_revision),
                   g_snapshot.quest_count, g_snapshot.objective_count,
                   g_snapshot.note_count, g_live_snapshot.workshop_count);
  }
  if (previous_map != g_snapshot.map_revision) {
    LogRateLimited("ui-state-map", 1000, LogLevel::kInfo,
                   "[STATE] UI map rev=%llu markers=%u world=(%.1f,%.1f,%.1f) local_valid=%u",
                   static_cast<unsigned long long>(g_snapshot.map_revision),
                   g_snapshot.marker_count, g_live_snapshot.player_x,
                   g_live_snapshot.player_y, g_live_snapshot.player_rotation,
                   g_live_snapshot.local_player_valid ? 1u : 0u);
  }
  if (previous_radio != g_snapshot.radio_revision) {
    LogRateLimited("ui-state-radio", 1000, LogLevel::kInfo,
                   "[STATE] UI radio rev=%llu stations=%u",
                   static_cast<unsigned long long>(g_snapshot.radio_revision),
                   g_snapshot.station_count);
  }
  const bool epoch_changed = previous_epoch != g_snapshot.world_epoch;
  if (epoch_changed) {
    ResetSelections();
    g_local_map_requested = false;
    g_local_map = {};
  } else if (g_local_map_requested) {
    LocalMapState next = g_database->CaptureLocalMap();
    if (next.revision != g_local_map.revision) g_local_map = std::move(next);
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

  if (g_pending_action_form &&
      (((g_pending_action_type == CommandType::kTrackQuest) &&
        g_snapshot.data_revision != g_pending_action_data_revision) ||
       ((g_pending_action_type == CommandType::kTuneRadio ||
         g_pending_action_type == CommandType::kRadioOff) &&
        g_snapshot.radio_revision != g_pending_action_radio_revision) ||
       ((g_pending_action_type != CommandType::kTrackQuest) &&
        g_pending_action_type != CommandType::kTuneRadio &&
        g_pending_action_type != CommandType::kRadioOff &&
        g_snapshot.inventory_revision !=
            g_pending_action_inventory_revision))) {
    ClearPendingAction();
    relevant = true;
  }
  g_dirty |= relevant;
}

LRESULT CALLBACK PanelProc(HWND window, UINT message, WPARAM wparam,
                           LPARAM lparam) {
  switch (message) {
    case WM_CLOSE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
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
  if (!g_font_small || !g_font_body || !g_font_title || !g_font_large)
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
  ShutdownFallout4UiAssets();
  if (g_background_dc && g_background_previous)
    SelectObject(g_background_dc, g_background_previous);
  if (g_transition_from_dc && g_transition_from_previous)
    SelectObject(g_transition_from_dc, g_transition_from_previous);
  if (g_transition_to_dc && g_transition_to_previous)
    SelectObject(g_transition_to_dc, g_transition_to_previous);
  if (g_transition_from_bitmap) DeleteObject(g_transition_from_bitmap);
  if (g_transition_to_bitmap) DeleteObject(g_transition_to_bitmap);
  if (g_background_bitmap) DeleteObject(g_background_bitmap);
  if (g_transition_from_dc) DeleteDC(g_transition_from_dc);
  if (g_transition_to_dc) DeleteDC(g_transition_to_dc);
  if (g_background_dc) DeleteDC(g_background_dc);
  if (g_back_dc && g_back_previous) SelectObject(g_back_dc, g_back_previous);
  if (g_font_small) DeleteObject(g_font_small);
  if (g_font_body) DeleteObject(g_font_body);
  if (g_font_title) DeleteObject(g_font_title);
  if (g_font_large) DeleteObject(g_font_large);
  if (g_back_bitmap) DeleteObject(g_back_bitmap);
  if (g_back_dc) DeleteDC(g_back_dc);
  g_font_small = g_font_body = g_font_title = g_font_large = nullptr;
  g_back_bitmap = nullptr;
  g_back_dc = nullptr;
  g_back_previous = nullptr;
  g_back_bits = nullptr;
  g_transition_from_dc = g_transition_to_dc = nullptr;
  g_transition_from_bitmap = g_transition_to_bitmap = nullptr;
  g_transition_from_previous = g_transition_to_previous = nullptr;
  g_background_dc = nullptr;
  g_background_bitmap = nullptr;
  g_background_previous = nullptr;
}

void StartTouchSocket() {
  g_packet_mapper.Reset();
  g_gesture.Reset();
  WSADATA data = {};
  const int startup_error = WSAStartup(MAKEWORD(2, 2), &data);
  if (startup_error != 0) {
    Log(LogLevel::kWarning,
        "[INPUT] Winsock startup failed error=%d; touch disabled",
        startup_error);
    return;
  }
  g_winsock = true;
  g_have_touch_peer = false;
  std::memset(&g_touch_peer, 0, sizeof(g_touch_peer));
  g_touch_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_touch_socket == INVALID_SOCKET) {
    Log(LogLevel::kWarning,
        "[INPUT] touch socket creation failed error=%d; touch disabled",
        WSAGetLastError());
    return;
  }
  u_long nonblocking = 1;
  if (ioctlsocket(g_touch_socket, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    closesocket(g_touch_socket);
    g_touch_socket = INVALID_SOCKET;
    Log(LogLevel::kWarning,
        "[INPUT] touch nonblocking setup failed error=%d; touch disabled",
        error);
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
  if (!g_have_snapshot) {
    std::fill_n(reinterpret_cast<unsigned char*>(&g_snapshot),
                sizeof(g_snapshot), 0);
    g_snapshot.size = sizeof(Snapshot);
    std::snprintf(g_snapshot.build_label,
                  sizeof(g_snapshot.build_label),
                  "Fallout 4 state bridge is initializing");
  }
  MonitorChoice monitor = {};
  EnumDisplayMonitors(nullptr, nullptr, MonitorProc,
                      reinterpret_cast<LPARAM>(&monitor));
  RECT rect = monitor.rect;
  if (!monitor.found) {
    const LONG x = 0;
    const LONG y = 720;
    const LONG width = kLogicalWidth;
    const LONG height = kLogicalHeight;
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
  klass.lpszClassName = L"Fallout4SecondScreenPanel";
  klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    return false;
  g_window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      klass.lpszClassName, L"Fallout 4 Dual-Screen Pip-Boy", WS_POPUP,
      rect.left, rect.top, g_width, g_height, nullptr, nullptr, g_module, nullptr);
  if (!g_window || !CreateBackBuffer()) return false;
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
  g_packet_mapper.Reset();
  g_gesture.Reset();
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
}

DWORD WINAPI PanelThread(void* parameter) {
  auto* startup = static_cast<PanelStartupContext*>(parameter);
  g_thread_id = GetCurrentThreadId();
  try {
    if (!CreatePanelWindow()) {
      Log(LogLevel::kError,
          "[PANEL] failed to create independent lower window error=%lu",
          GetLastError());
      CleanupPanel();
      g_running.store(false, std::memory_order_release);
      CompleteStartup(startup, false);
      ReleaseStartup(startup);
      return 0;
    }
    CompleteStartup(startup, true);
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
      if (g_dirty) {
        Render();
        PresentNow();
      }
      MsgWaitForMultipleObjects(0, nullptr, FALSE, 16, QS_ALLINPUT);
    }
  } catch (const std::exception& error) {
    try {
      Log(LogLevel::kError, "[PANEL] thread exception: %s", error.what());
    } catch (...) {
      OutputDebugStringA("Fallout4SecondScreen: panel exception logging failed\n");
    }
    CompleteStartup(startup, false);
  } catch (...) {
    try {
      Log(LogLevel::kError, "[PANEL] thread unknown exception");
    } catch (...) {
      OutputDebugStringA("Fallout4SecondScreen: panel exception logging failed\n");
    }
    CompleteStartup(startup, false);
  }
  CleanupPanel();
  g_thread_id = 0;
  g_running.store(false, std::memory_order_release);
  ReleaseStartup(startup);
  return 0;
}

}  // namespace

bool StartPanel(PipboyDatabase* database, PipboyClient*) {
  if (g_running.exchange(true, std::memory_order_acq_rel)) return true;
  g_module = GetModuleHandleW(nullptr);
  g_database = database;
  const Config& config = LoadConfig();
  g_touch_port = config.touch_port;
  g_actions_enabled = config.actions_enabled;
  g_refresh_interval_ms = std::max<uint32_t>(
      1, 1000u / std::max<uint32_t>(1, config.diagnostic_refresh_hz));
  g_next_snapshot_ms = GetTickCount64() + g_refresh_interval_ms;
  if (g_database) {
    g_live_snapshot = g_database->Capture();
    AdaptPipboySnapshot(g_live_snapshot,
                        g_actions_enabled && NativeActionsReady(), &g_snapshot);
    g_snapshot_sequence = g_snapshot.sequence;
    g_have_snapshot = true;
  }

  auto* startup = new (std::nothrow) PanelStartupContext();
  if (!startup) {
    g_running.store(false, std::memory_order_release);
    return false;
  }
  startup->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!startup->event) {
    startup->references.store(1);
    ReleaseStartup(startup);
    g_running.store(false, std::memory_order_release);
    return false;
  }
  g_thread = CreateThread(nullptr, 0, PanelThread, startup, 0, &g_thread_id);
  if (!g_thread) {
    startup->references.store(1);
    ReleaseStartup(startup);
    g_running.store(false, std::memory_order_release);
    return false;
  }
  const DWORD wait = WaitForSingleObject(startup->event, 15000);
  const bool success = wait == WAIT_OBJECT_0 &&
                       startup->result.load(std::memory_order_acquire) == 1;
  if (!success) {
    g_running.store(false, std::memory_order_release);
    if (g_thread_id) PostThreadMessageW(g_thread_id, WM_QUIT, 0, 0);
    const DWORD stopped = WaitForSingleObject(g_thread, 2000);
    if (stopped != WAIT_OBJECT_0) {
      Log(LogLevel::kWarning,
          "[PANEL] startup failure cleanup still pending wait=%lu stop_wait=%lu",
          static_cast<unsigned long>(wait),
          static_cast<unsigned long>(stopped));
    }
  }
  CloseHandle(g_thread);
  g_thread = nullptr;
  ReleaseStartup(startup);
  if (!success)
    Log(LogLevel::kError,
        "[PANEL] copied UI startup did not complete wait=%lu",
        static_cast<unsigned long>(wait));
  return success;
}

void RequestPanelStop() {
  g_radio_page_requested.store(false, std::memory_order_release);
  g_effects_page_requested.store(false, std::memory_order_release);
  g_map_page_requested.store(false, std::memory_order_release);
  g_running.store(false, std::memory_order_release);
  if (g_thread_id) PostThreadMessageW(g_thread_id, WM_QUIT, 0, 0);
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

}  // namespace fo4ds
