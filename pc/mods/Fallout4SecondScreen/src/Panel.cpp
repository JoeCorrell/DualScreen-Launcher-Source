#include "Panel.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Config.h"
#include "F4SEBridgeClient.h"
#include "Log.h"
#include "PipboyProtocol.h"
#include "../f4se/BridgeABI.h"

namespace fo4ds {
namespace {

constexpr int kDesignWidth = 1240;
constexpr int kDesignHeight = 1080;
constexpr COLORREF kBackground = RGB(1, 10, 5);
constexpr COLORREF kDark = RGB(8, 49, 24);
constexpr COLORREF kGreen = RGB(20, 210, 87);
constexpr COLORREF kBright = RGB(105, 255, 160);
constexpr COLORREF kDim = RGB(13, 115, 52);

PipboyDatabase* g_database = nullptr;
PipboyClient* g_client = nullptr;
F4SEBridgeClient* g_f4se_bridge = nullptr;
PipboySnapshot g_snapshot;
F4SEBridgeLifecycleState g_f4se_state;
LocalMapState g_local_map;
std::atomic<bool> g_started{false};
std::atomic<bool> g_start_success{false};
HANDLE g_start_event = nullptr;
HWND g_window = nullptr;
SOCKET g_touch = INVALID_SOCKET;
int g_page = 0;
int g_selected_item = 0;
int g_selected_quest = 0;
int g_selected_radio = 0;
int g_item_offset = 0;
int g_quest_offset = 0;
int g_radio_offset = 0;
bool g_local_map_requested = false;
bool g_actions_enabled = false;
bool g_scanlines = true;
uint16_t g_touch_port = 7788;
uint32_t g_refresh_interval_ms = 100;
uint64_t g_next_snapshot_ms = 0;
uint64_t g_observed_generation = 0;
uint64_t g_observed_f4se_generation = 0;

HDC g_back_dc = nullptr;
HBITMAP g_back_bitmap = nullptr;
HGDIOBJ g_back_previous = nullptr;
int g_back_width = 0;
int g_back_height = 0;
HFONT g_font_small = nullptr;
HFONT g_font_body = nullptr;
HFONT g_font_heading = nullptr;
HFONT g_font_title = nullptr;

bool ActionsAvailable() {
  if (!g_actions_enabled || !g_snapshot.ActionsSafe() || !g_client) return false;
  if (g_f4se_state.availability == F4SEBridgeAvailability::kMalformed ||
      g_f4se_state.availability == F4SEBridgeAvailability::kWaitingForPlugin) {
    return false;
  }
  if (g_f4se_state.availability != F4SEBridgeAvailability::kReady) return true;
  constexpr uint32_t required =
      FO4DS_LIFE_PLUGIN_LOADED | FO4DS_LIFE_DATA_READY |
      FO4DS_LIFE_SAVE_ACTIVE;
  return (g_f4se_state.lifecycle_flags & required) == required &&
         !(g_f4se_state.lifecycle_flags & FO4DS_LIFE_SAVE_LOADING);
}

const wchar_t* BridgeLabel() {
  switch (g_f4se_state.availability) {
    case F4SEBridgeAvailability::kReady: return L"F4SE READY";
    case F4SEBridgeAvailability::kWaitingForPlugin: return L"F4SE WAIT";
    case F4SEBridgeAvailability::kMalformed: return L"F4SE INVALID";
    case F4SEBridgeAvailability::kAbsent: return L"F4SE OPTIONAL";
    default: return L"F4SE CHECK";
  }
}

void SignalPanelStart(bool success) {
  g_start_success.store(success);
  if (g_start_event) SetEvent(g_start_event);
}

int X(int value) { return MulDiv(value, g_back_width, kDesignWidth); }
int Y(int value) { return MulDiv(value, g_back_height, kDesignHeight); }

std::wstring Wide(const std::string& text) {
  if (text.empty()) return {};
  int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                                  static_cast<int>(text.size()), nullptr, 0);
  if (count <= 0) {
    count = MultiByteToWideChar(CP_ACP, 0, text.data(),
                                static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) return {};
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()),
                        result.data(), count);
    return result;
  }
  std::wstring result(count, L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(),
                      static_cast<int>(text.size()), result.data(), count);
  return result;
}

void Fill(HDC dc, int left, int top, int right, int bottom, COLORREF color) {
  RECT rect = {X(left), Y(top), X(right), Y(bottom)};
  SetDCBrushColor(dc, color);
  FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color, int width = 1) {
  HPEN pen = width <= 1 ? static_cast<HPEN>(GetStockObject(DC_PEN))
                        : CreatePen(PS_SOLID, std::max(1, X(width)), color);
  if (!pen) return;
  if (width <= 1) SetDCPenColor(dc, color);
  HGDIOBJ old = SelectObject(dc, pen);
  if (!old || old == HGDI_ERROR) {
    if (width > 1) DeleteObject(pen);
    return;
  }
  MoveToEx(dc, X(x1), Y(y1), nullptr);
  LineTo(dc, X(x2), Y(y2));
  SelectObject(dc, old);
  if (width > 1) DeleteObject(pen);
}

void Text(HDC dc, int left, int top, int right, int bottom,
          const std::wstring& value, HFONT font, COLORREF color,
          UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
  RECT rect = {X(left), Y(top), X(right), Y(bottom)};
  HGDIOBJ old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &rect,
            format | DT_NOPREFIX);
  SelectObject(dc, old);
}

void TextUtf8(HDC dc, int left, int top, int right, int bottom,
              const std::string& value, HFONT font, COLORREF color,
              UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS) {
  Text(dc, left, top, right, bottom, Wide(value), font, color, format);
}

void Bar(HDC dc, int left, int top, int right, int bottom, float value,
         float maximum) {
  Fill(dc, left, top, right, bottom, kDark);
  const float ratio = maximum > 0.0f
                          ? std::clamp(value / maximum, 0.0f, 1.0f)
                          : 0.0f;
  Fill(dc, left, top, left + static_cast<int>((right - left) * ratio), bottom,
       kGreen);
}

void Button(HDC dc, int left, int top, int right, int bottom,
            const wchar_t* label, bool enabled) {
  Fill(dc, left, top, right, bottom, enabled ? kDark : kBackground);
  Line(dc, left, top, right, top, enabled ? kGreen : kDim, 2);
  Line(dc, right, top, right, bottom, enabled ? kGreen : kDim, 2);
  Line(dc, right, bottom, left, bottom, enabled ? kGreen : kDim, 2);
  Line(dc, left, bottom, left, top, enabled ? kGreen : kDim, 2);
  Text(dc, left + 8, top, right - 8, bottom, label, g_font_body,
       enabled ? kBright : kDim, DT_CENTER | DT_VCENTER);
}

HFONT Font(int pixels, int weight = FW_NORMAL) {
  return CreateFontW(-std::max(8, Y(pixels)), 0, 0, 0, weight, FALSE, FALSE,
                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                     CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                     FIXED_PITCH | FF_MODERN, L"Lucida Console");
}

void DestroyBackbuffer() {
  if (g_back_dc && g_back_previous) SelectObject(g_back_dc, g_back_previous);
  if (g_back_bitmap) DeleteObject(g_back_bitmap);
  if (g_back_dc) DeleteDC(g_back_dc);
  g_back_dc = nullptr;
  g_back_bitmap = nullptr;
  g_back_previous = nullptr;
  for (HFONT* font : {&g_font_small, &g_font_body, &g_font_heading, &g_font_title}) {
    if (*font) DeleteObject(*font);
    *font = nullptr;
  }
  g_back_width = g_back_height = 0;
}

bool EnsureBackbuffer(HWND window, HDC target) {
  RECT client = {};
  GetClientRect(window, &client);
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) return false;
  if (g_back_dc && width == g_back_width && height == g_back_height) return true;
  DestroyBackbuffer();
  g_back_width = width;
  g_back_height = height;
  g_back_dc = CreateCompatibleDC(target);
  g_back_bitmap = CreateCompatibleBitmap(target, width, height);
  if (!g_back_dc || !g_back_bitmap) {
    DestroyBackbuffer();
    return false;
  }
  g_back_previous = SelectObject(g_back_dc, g_back_bitmap);
  if (!g_back_previous || g_back_previous == HGDI_ERROR) {
    g_back_previous = nullptr;
    DestroyBackbuffer();
    return false;
  }
  g_font_small = Font(22);
  g_font_body = Font(29);
  g_font_heading = Font(39, FW_BOLD);
  g_font_title = Font(55, FW_BOLD);
  if (!g_font_small || !g_font_body || !g_font_heading || !g_font_title) {
    DestroyBackbuffer();
    return false;
  }
  return true;
}

void Header(HDC dc) {
  Text(dc, 38, 18, 920, 84, L"ROBCO INDUSTRIES (TM) PIP-BOY",
       g_font_heading, kBright);
  wchar_t level[64] = {};
  std::swprintf(level, std::size(level), L"LEVEL %u", g_snapshot.level);
  Text(dc, 960, 18, 1200, 84, level, g_font_body, kGreen, DT_RIGHT | DT_VCENTER);
  Line(dc, 38, 88, 1202, 88, kGreen, 2);
  static constexpr const wchar_t* names[] = {L"STAT", L"INV", L"DATA", L"MAP", L"RADIO"};
  for (int i = 0; i < 5; ++i) {
    const int left = 38 + i * 233;
    if (i == g_page) Fill(dc, left, 105, left + 205, 169, kDark);
    Text(dc, left, 105, left + 205, 169, names[i], g_font_heading,
         i == g_page ? kBright : kDim, DT_CENTER | DT_VCENTER);
  }
  Line(dc, 38, 182, 1202, 182, kGreen, 2);
}

void Footer(HDC dc) {
  Line(dc, 38, 1003, 1202, 1003, kDim, 1);
  TextUtf8(dc, 42, 1008, 585, 1063, g_snapshot.status, g_font_small,
           g_snapshot.connected && g_snapshot.root_ready ? kGreen : kDim);
  Text(dc, 590, 1008, 755, 1063, BridgeLabel(), g_font_small,
       g_f4se_state.availability == F4SEBridgeAvailability::kReady ? kGreen
                                                                  : kDim,
       DT_CENTER | DT_VCENTER);
  Text(dc, 760, 1008, 970, 1063,
       g_actions_enabled ? (ActionsAvailable() ? L"ACTIONS READY"
                                               : L"ACTIONS PAUSED")
                         : L"ACTIONS LOCKED",
       g_font_small,
       ActionsAvailable() ? kBright : kDim,
       DT_CENTER | DT_VCENTER);
  wchar_t revision[96] = {};
  std::swprintf(revision, std::size(revision), L"GEN %llu  REV %llu",
                static_cast<unsigned long long>(g_snapshot.generation),
                static_cast<unsigned long long>(g_snapshot.revision));
  Text(dc, 975, 1008, 1198, 1063, revision, g_font_small, kDim,
       DT_RIGHT | DT_VCENTER);
}

void StatPage(HDC dc) {
  TextUtf8(dc, 54, 210, 740, 282,
           g_snapshot.player_name.empty() ? "WAITING FOR PLAYER" : g_snapshot.player_name,
           g_font_title, kBright);
  wchar_t hp[96] = {};
  wchar_t ap[96] = {};
  std::swprintf(hp, std::size(hp), L"HP  %.0f / %.0f", g_snapshot.hp,
                g_snapshot.max_hp);
  std::swprintf(ap, std::size(ap), L"AP  %.0f / %.0f", g_snapshot.ap,
                g_snapshot.max_ap);
  Text(dc, 58, 306, 480, 352, hp, g_font_body, kGreen);
  Bar(dc, 58, 358, 520, 382, g_snapshot.hp, g_snapshot.max_hp);
  Text(dc, 58, 408, 480, 454, ap, g_font_body, kGreen);
  Bar(dc, 58, 460, 520, 484, g_snapshot.ap, g_snapshot.max_ap);
  wchar_t facts[256] = {};
  std::swprintf(facts, std::size(facts),
                L"CAPS  %u\nWEIGHT  %.1f / %.1f\nXP  %.1f%%\nSTIMPAKS  %u   RADAWAY  %u",
                g_snapshot.caps, g_snapshot.carry_weight, g_snapshot.max_weight,
                g_snapshot.xp_progress * 100.0f, g_snapshot.stimpaks,
                g_snapshot.radaway);
  Text(dc, 58, 525, 535, 720, facts, g_font_body, kGreen,
       DT_LEFT | DT_TOP | DT_WORDBREAK);

  Text(dc, 650, 220, 1160, 274, L"S.P.E.C.I.A.L.", g_font_heading, kBright);
  for (size_t i = 0; i < g_snapshot.special.size(); ++i) {
    const int top = 290 + static_cast<int>(i) * 72;
    std::wstring name = g_snapshot.special_names[i].empty()
                            ? std::wstring(1, L"SPECIAL"[i])
                            : Wide(g_snapshot.special_names[i]);
    Text(dc, 660, top, 1060, top + 58, name, g_font_body, kGreen);
    wchar_t value[24] = {};
    std::swprintf(value, std::size(value), L"%d", g_snapshot.special[i]);
    Text(dc, 1060, top, 1160, top + 58, value, g_font_heading, kBright,
         DT_RIGHT | DT_VCENTER);
  }
  wchar_t effects[96] = {};
  std::swprintf(effects, std::size(effects), L"ACTIVE EFFECTS  %zu",
                g_snapshot.effects.size());
  Text(dc, 650, 835, 1160, 894, effects, g_font_body, kGreen);
  if (!g_snapshot.effects.empty())
    TextUtf8(dc, 650, 895, 1160, 955, g_snapshot.effects.front().name,
             g_font_body, kBright);
}

const wchar_t* Category(uint32_t flag) {
  if (flag & 2) return L"WEAPON";
  if (flag & 4) return L"APPAREL";
  if (flag & 8) return L"AID";
  if (flag & 8192) return L"HOLOTAPE";
  if (flag & 128) return L"NOTE";
  if (flag & 512) return L"MISC";
  if (flag & 1024) return L"JUNK";
  if (flag & 2048) return L"MOD";
  if (flag & 4096) return L"AMMO";
  if (flag & 1) return L"FAVORITE";
  return L"ITEM";
}

bool DrawLocalMap(HDC dc, int left, int top, int right, int bottom) {
  if (g_local_map.pixels.empty() || !g_local_map.width || !g_local_map.height ||
      g_local_map.width > 32768 || g_local_map.height > 32768) {
    return false;
  }
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
  const size_t row_stride = (static_cast<size_t>(g_local_map.width) + 3u) & ~3u;
  info.header.biSizeImage = static_cast<DWORD>(row_stride * g_local_map.height);
  for (int i = 0; i < 256; ++i) {
    info.colors[i].rgbRed = static_cast<BYTE>(i / 4);
    info.colors[i].rgbGreen = static_cast<BYTE>(i);
    info.colors[i].rgbBlue = static_cast<BYTE>(i / 6);
  }
  const int available_width = right - left;
  const int available_height = bottom - top;
  const float scale = std::min(
      static_cast<float>(available_width) / g_local_map.width,
      static_cast<float>(available_height) / g_local_map.height);
  const int draw_width = std::max(1, static_cast<int>(g_local_map.width * scale));
  const int draw_height = std::max(1, static_cast<int>(g_local_map.height * scale));
  const int draw_left = left + (available_width - draw_width) / 2;
  const int draw_top = top + (available_height - draw_height) / 2;
  const uint8_t* dib_pixels = g_local_map.pixels.data();
  static uint64_t packed_revision = 0;
  static uint64_t packed_generation = 0;
  static std::vector<uint8_t> packed_pixels;
  if (row_stride != g_local_map.width) {
    if (packed_revision != g_local_map.revision ||
        packed_generation != g_snapshot.generation) {
      packed_pixels.assign(row_stride * g_local_map.height, 0);
      for (uint32_t row = 0; row < g_local_map.height; ++row) {
        std::memcpy(packed_pixels.data() + static_cast<size_t>(row) * row_stride,
                    g_local_map.pixels.data() +
                        static_cast<size_t>(row) * g_local_map.width,
                    g_local_map.width);
      }
      packed_revision = g_local_map.revision;
      packed_generation = g_snapshot.generation;
    }
    dib_pixels = packed_pixels.data();
  }
  SetStretchBltMode(dc, HALFTONE);
  const int result = StretchDIBits(
      dc, X(draw_left), Y(draw_top), X(draw_width), Y(draw_height), 0, 0,
      static_cast<int>(g_local_map.width), static_cast<int>(g_local_map.height),
      dib_pixels, reinterpret_cast<const BITMAPINFO*>(&info),
      DIB_RGB_COLORS, SRCCOPY);
  if (result == static_cast<int>(GDI_ERROR)) return false;

  const float edge_x = g_local_map.northeast_x - g_local_map.northwest_x;
  const float edge_y = g_local_map.northeast_y - g_local_map.northwest_y;
  const float down_x = g_local_map.southwest_x - g_local_map.northwest_x;
  const float down_y = g_local_map.southwest_y - g_local_map.northwest_y;
  const float map_player_x = g_snapshot.local_player_valid
                                 ? g_snapshot.local_player_x
                                 : g_snapshot.player_x;
  const float map_player_y = g_snapshot.local_player_valid
                                 ? g_snapshot.local_player_y
                                 : g_snapshot.player_y;
  const float map_player_rotation = g_snapshot.local_player_valid
                                        ? g_snapshot.local_player_rotation
                                        : g_snapshot.player_rotation;
  const float delta_x = map_player_x - g_local_map.northwest_x;
  const float delta_y = map_player_y - g_local_map.northwest_y;
  const float determinant = edge_x * down_y - edge_y * down_x;
  if (std::fabs(determinant) > 0.0001f) {
    const float u = (delta_x * down_y - delta_y * down_x) / determinant;
    const float v = (edge_x * delta_y - edge_y * delta_x) / determinant;
    if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
      const int px = draw_left + static_cast<int>(u * draw_width);
      const int py = draw_top + static_cast<int>(v * draw_height);
      const float angle = map_player_rotation * 3.14159265f / 180.0f;
      Line(dc, px, py, px + static_cast<int>(std::sin(angle) * 28.0f),
           py - static_cast<int>(std::cos(angle) * 28.0f), kBright, 3);
      Fill(dc, px - 6, py - 6, px + 7, py + 7, kBright);
    }
  }
  return true;
}

void InventoryPage(HDC dc) {
  wchar_t title[128] = {};
  std::swprintf(title, std::size(title), L"INVENTORY  %zu ITEMS  VERSION %u",
                g_snapshot.items.size(), g_snapshot.inventory_version);
  Text(dc, 54, 205, 1180, 270, title, g_font_heading, kBright);
  const int count = static_cast<int>(g_snapshot.items.size());
  g_item_offset = std::clamp(g_item_offset, 0, std::max(0, count - 11));
  if (count) {
    g_selected_item = std::clamp(g_selected_item, 0, count - 1);
    if (g_selected_item < g_item_offset || g_selected_item >= g_item_offset + 11)
      g_selected_item = g_item_offset;
  }
  for (int row = 0; row < 11 && g_item_offset + row < count; ++row) {
    const int index = g_item_offset + row;
    const int top = 286 + row * 57;
    const auto& item = g_snapshot.items[index];
    if (index == g_selected_item) Fill(dc, 48, top, 760, top + 51, kDark);
    Text(dc, 58, top, 190, top + 51, Category(item.filter_flag), g_font_small,
         index == g_selected_item ? kBright : kDim);
    TextUtf8(dc, 195, top, 650, top + 51, item.name, g_font_body,
             index == g_selected_item ? kBright : kGreen);
    wchar_t quantity[32] = {};
    std::swprintf(quantity, std::size(quantity), L"%d", item.count);
    Text(dc, 650, top, 745, top + 51, quantity, g_font_body,
         item.equip_state ? kBright : kGreen, DT_RIGHT | DT_VCENTER);
  }
  Button(dc, 48, 930, 250, 980, L"PREV", g_item_offset > 0);
  Button(dc, 270, 930, 472, 980, L"NEXT", g_item_offset + 11 < count);
  if (count) {
    const auto& item = g_snapshot.items[g_selected_item];
    TextUtf8(dc, 800, 295, 1180, 370, item.name, g_font_heading, kBright,
             DT_LEFT | DT_TOP | DT_WORDBREAK);
    wchar_t state[160] = {};
    std::swprintf(state, std::size(state),
                  L"COUNT  %d\nEQUIPPED  %s\nFAVORITE  %d\nFORM  %08X",
                  item.count, item.equip_state ? L"YES" : L"NO", item.favorite,
                  item.form_id);
    Text(dc, 800, 390, 1180, 555, state, g_font_body, kGreen,
         DT_LEFT | DT_TOP | DT_WORDBREAK);
    int y = 570;
    for (const auto& field : item.card) {
      if (y > 842) break;
      std::wstring line = Wide(field.label);
      line += L"  ";
      if (!field.text_value.empty()) {
        line += Wide(field.text_value);
      } else {
        wchar_t number[48] = {};
        std::swprintf(number, std::size(number), L"%.1f", field.number_value);
        line += number;
      }
      Text(dc, 800, y, 1180, y + 48, line, g_font_small, kGreen);
      y += 45;
    }
    Button(dc, 800, 905, 1180, 975,
           item.equip_state ? L"HOLD: USE / UNEQUIP" : L"HOLD: USE / EQUIP",
           ActionsAvailable());
  }
}

void DataPage(HDC dc) {
  wchar_t title[96] = {};
  std::swprintf(title, std::size(title), L"QUESTS  %zu", g_snapshot.quests.size());
  Text(dc, 54, 205, 1180, 270, title, g_font_heading, kBright);
  const int count = static_cast<int>(g_snapshot.quests.size());
  g_quest_offset = std::clamp(g_quest_offset, 0, std::max(0, count - 10));
  if (count) {
    g_selected_quest = std::clamp(g_selected_quest, 0, count - 1);
    if (g_selected_quest < g_quest_offset ||
        g_selected_quest >= g_quest_offset + 10)
      g_selected_quest = g_quest_offset;
  }
  for (int row = 0; row < 10 && g_quest_offset + row < count; ++row) {
    const int index = g_quest_offset + row;
    const int top = 286 + row * 60;
    const auto& quest = g_snapshot.quests[index];
    if (index == g_selected_quest) Fill(dc, 48, top, 620, top + 54, kDark);
    Text(dc, 58, top, 100, top + 54, quest.active ? L"[X]" : L"[ ]",
         g_font_body, quest.active ? kBright : kDim);
    TextUtf8(dc, 108, top, 605, top + 54, quest.name, g_font_body,
             index == g_selected_quest ? kBright : kGreen);
  }
  Button(dc, 48, 905, 250, 975, L"PREV", g_quest_offset > 0);
  Button(dc, 270, 905, 472, 975, L"NEXT", g_quest_offset + 10 < count);
  if (count) {
    const auto& quest = g_snapshot.quests[g_selected_quest];
    TextUtf8(dc, 660, 286, 1185, 360, quest.name, g_font_heading, kBright,
             DT_LEFT | DT_TOP | DT_WORDBREAK);
    TextUtf8(dc, 660, 372, 1185, 540, quest.description, g_font_small, kGreen,
             DT_LEFT | DT_TOP | DT_WORDBREAK);
    int y = 558;
    for (const auto& objective : quest.objectives) {
      if (y > 830) break;
      std::wstring line = objective.completed ? L"[X] " : L"[ ] ";
      line += Wide(objective.text);
      Text(dc, 660, y, 1185, y + 78, line, g_font_small,
           objective.enabled ? kBright : kDim,
           DT_LEFT | DT_TOP | DT_WORDBREAK);
      y += 76;
    }
    Button(dc, 660, 905, 1185, 975,
           quest.active ? L"HOLD: STOP TRACKING" : L"HOLD: TRACK QUEST",
           ActionsAvailable());
  }
}

void MapPage(HDC dc) {
  TextUtf8(dc, 54, 205, 890, 270,
           g_snapshot.current_worldspace.empty() ? g_snapshot.current_cell
                                                 : g_snapshot.current_worldspace,
           g_font_heading, kBright);
  wchar_t position[160] = {};
  std::swprintf(position, std::size(position), L"X %.1f   Y %.1f   HDG %.1f",
                g_snapshot.player_x, g_snapshot.player_y,
                g_snapshot.player_rotation);
  Text(dc, 54, 260, 890, 315, position, g_font_body, kGreen);
  Fill(dc, 54, 330, 1186, 936, RGB(2, 19, 9));
  if (g_local_map_requested && DrawLocalMap(dc, 54, 330, 1186, 936)) {
    wchar_t raster[96] = {};
    std::swprintf(raster, std::size(raster), L"LOCAL %u x %u  REV %llu",
                  g_local_map.width, g_local_map.height,
                  static_cast<unsigned long long>(g_local_map.revision));
    Text(dc, 54, 940, 700, 990, raster, g_font_small, kGreen);
    Text(dc, 820, 270, 1180, 320, L"LOCAL MAP ACTIVE", g_font_small, kBright,
         DT_RIGHT | DT_VCENTER);
    return;
  }
  float min_x = g_snapshot.player_x - 1.0f;
  float max_x = g_snapshot.player_x + 1.0f;
  float min_y = g_snapshot.player_y - 1.0f;
  float max_y = g_snapshot.player_y + 1.0f;
  for (const auto& marker : g_snapshot.markers) {
    if (!marker.visible) continue;
    min_x = std::min(min_x, marker.x);
    max_x = std::max(max_x, marker.x);
    min_y = std::min(min_y, marker.y);
    max_y = std::max(max_y, marker.y);
  }
  auto map_x = [&](float value) {
    return 72 + static_cast<int>((value - min_x) / std::max(1.0f, max_x - min_x) * 1092.0f);
  };
  auto map_y = [&](float value) {
    return 916 - static_cast<int>((value - min_y) / std::max(1.0f, max_y - min_y) * 566.0f);
  };
  for (const auto& marker : g_snapshot.markers) {
    if (!marker.visible) continue;
    const int mx = map_x(marker.x);
    const int my = map_y(marker.y);
    Fill(dc, mx - 4, my - 4, mx + 5, my + 5,
         marker.discovered ? kGreen : kDim);
  }
  const int px = map_x(g_snapshot.player_x);
  const int py = map_y(g_snapshot.player_y);
  const float angle = g_snapshot.player_rotation * 3.14159265f / 180.0f;
  Line(dc, px, py, px + static_cast<int>(std::sin(angle) * 28.0f),
       py - static_cast<int>(std::cos(angle) * 28.0f), kBright, 3);
  Fill(dc, px - 6, py - 6, px + 7, py + 7, kBright);
  wchar_t marker_count[64] = {};
  std::swprintf(marker_count, std::size(marker_count), L"%zu LOCATIONS",
                g_snapshot.markers.size());
  Text(dc, 940, 210, 1180, 270, marker_count, g_font_small, kGreen,
       DT_RIGHT | DT_VCENTER);
  Text(dc, 920, 270, 1180, 320,
       g_local_map_requested ? L"LOCAL MAP REQUESTED" : L"TAP FOR LOCAL MAP",
       g_font_small, g_local_map_requested ? kBright : kDim,
       DT_RIGHT | DT_VCENTER);
}

void RadioPage(HDC dc) {
  wchar_t title[96] = {};
  std::swprintf(title, std::size(title), L"RADIO STATIONS  %zu", g_snapshot.radio.size());
  Text(dc, 54, 205, 1180, 270, title, g_font_heading, kBright);
  const int count = static_cast<int>(g_snapshot.radio.size());
  g_radio_offset = std::clamp(g_radio_offset, 0, std::max(0, count - 10));
  if (count) {
    g_selected_radio = std::clamp(g_selected_radio, 0, count - 1);
    if (g_selected_radio < g_radio_offset ||
        g_selected_radio >= g_radio_offset + 10)
      g_selected_radio = g_radio_offset;
  }
  for (int row = 0; row < 10 && g_radio_offset + row < count; ++row) {
    const int index = g_radio_offset + row;
    const int top = 300 + row * 58;
    const auto& station = g_snapshot.radio[index];
    if (index == g_selected_radio) Fill(dc, 52, top, 1180, top + 52, kDark);
    Text(dc, 66, top, 120, top + 52, station.active ? L">" : L" ",
         g_font_heading, station.active ? kBright : kDim);
    TextUtf8(dc, 135, top, 850, top + 52, station.name, g_font_body,
             index == g_selected_radio ? kBright :
             (station.in_range ? kGreen : kDim));
    wchar_t frequency[64] = {};
    std::swprintf(frequency, std::size(frequency), L"%.1f MHz",
                  station.frequency);
    Text(dc, 900, top, 1155, top + 52, frequency, g_font_body,
         station.in_range ? kGreen : kDim, DT_RIGHT | DT_VCENTER);
  }
  Button(dc, 52, 905, 250, 975, L"PREV", g_radio_offset > 0);
  Button(dc, 270, 905, 468, 975, L"NEXT", g_radio_offset + 10 < count);
  if (count) {
    const auto& station = g_snapshot.radio[g_selected_radio];
    Button(dc, 830, 905, 1180, 975,
           station.active ? L"HOLD: RADIO OFF" : L"HOLD: TUNE",
           ActionsAvailable() && station.in_range);
  }
}

void Render(HWND window, HDC target) {
  if (!EnsureBackbuffer(window, target)) return;
  const uint64_t begin = GetTickCount64();
  Fill(g_back_dc, 0, 0, kDesignWidth, kDesignHeight, kBackground);
  Header(g_back_dc);
  switch (g_page) {
    case 0: StatPage(g_back_dc); break;
    case 1: InventoryPage(g_back_dc); break;
    case 2: DataPage(g_back_dc); break;
    case 3: MapPage(g_back_dc); break;
    case 4: RadioPage(g_back_dc); break;
  }
  Footer(g_back_dc);
  if (g_scanlines) {
    for (int y = 2; y < kDesignHeight; y += 5)
      Line(g_back_dc, 0, y, kDesignWidth, y, RGB(0, 19, 8));
  }
  BitBlt(target, 0, 0, g_back_width, g_back_height, g_back_dc, 0, 0, SRCCOPY);
  const uint64_t elapsed = GetTickCount64() - begin;
  if (elapsed >= 8)
    LogRateLimited("render-slow", 2000, LogLevel::kWarning,
                   "[PERF] panel render ms=%llu size=%dx%d items=%zu markers=%zu",
                   static_cast<unsigned long long>(elapsed), g_back_width,
                   g_back_height, g_snapshot.items.size(), g_snapshot.markers.size());
}

bool CanAct(const char* action) {
  if (ActionsAvailable()) return true;
  LogRateLimited("action-blocked", 2000, LogLevel::kWarning,
                 "[ACTION] blocked action=%s enabled=%u connected=%u root=%u "
                 "busy=%u loading=%u dead=%u dialogue=%u menu=%u movement=%u "
                 "pipboy=%u data_unavailable=%u animation=%u vats=%u "
                 "vats_playback=%u not_equipped=%u f4se=%u life=0x%08X",
                 action, g_actions_enabled ? 1u : 0u,
                 g_snapshot.connected ? 1u : 0u,
                 g_snapshot.root_ready ? 1u : 0u, g_snapshot.busy ? 1u : 0u,
                 g_snapshot.is_loading ? 1u : 0u,
                 g_snapshot.is_player_dead ? 1u : 0u,
                 g_snapshot.is_in_dialogue ? 1u : 0u,
                 g_snapshot.is_menu_open ? 1u : 0u,
                 g_snapshot.is_movement_locked ? 1u : 0u,
                 g_snapshot.is_pipboy_locked ? 1u : 0u,
                 g_snapshot.is_data_unavailable ? 1u : 0u,
                 g_snapshot.is_in_animation ? 1u : 0u,
                 g_snapshot.is_in_vats ? 1u : 0u,
                 g_snapshot.is_in_vats_playback ? 1u : 0u,
                 g_snapshot.is_pipboy_not_equipped ? 1u : 0u,
                 static_cast<unsigned>(g_f4se_state.availability),
                 g_f4se_state.lifecycle_flags);
  return false;
}

void UseSelectedItem() {
  if (g_selected_item < 0 ||
      g_selected_item >= static_cast<int>(g_snapshot.items.size()) ||
      !CanAct("use-item")) {
    return;
  }
  const auto& item = g_snapshot.items[g_selected_item];
  std::ostringstream args;
  args << '[' << item.handle_id << ",0,"
       << g_snapshot.inventory_version << ']';
  if (!g_client->SendCommand(0, args.str())) {
    Log(LogLevel::kWarning, "[ACTION] use-item queue rejected form=%08X",
        item.form_id);
  }
}

void ToggleSelectedQuest() {
  if (g_selected_quest < 0 ||
      g_selected_quest >= static_cast<int>(g_snapshot.quests.size()) ||
      !CanAct("toggle-quest")) {
    return;
  }
  const auto& quest = g_snapshot.quests[g_selected_quest];
  std::ostringstream args;
  args << '[' << quest.form_id << ',' << quest.instance << ',' << quest.type
       << ']';
  if (!g_client->SendCommand(5, args.str())) {
    Log(LogLevel::kWarning, "[ACTION] quest queue rejected form=%08X",
        quest.form_id);
  }
}

void ToggleSelectedRadio() {
  if (g_selected_radio < 0 ||
      g_selected_radio >= static_cast<int>(g_snapshot.radio.size()) ||
      !CanAct("toggle-radio")) {
    return;
  }
  const auto& station = g_snapshot.radio[g_selected_radio];
  if (!station.active && !station.in_range) {
    LogRateLimited("radio-out-of-range", 2000, LogLevel::kWarning,
                   "[ACTION] radio station out of range node=%u",
                   station.node_id);
    return;
  }
  std::ostringstream args;
  args << '[' << station.node_id << ']';
  if (!g_client->SendCommand(12, args.str())) {
    Log(LogLevel::kWarning, "[ACTION] radio queue rejected node=%u",
        station.node_id);
  }
}

void Activate(float normalized_x, float normalized_y, bool held) {
  if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) ||
      normalized_x < 0.0f || normalized_x > 1.0f || normalized_y < 0.0f ||
      normalized_y > 1.0f) {
    return;
  }
  const int x = static_cast<int>(normalized_x * kDesignWidth);
  const int y = static_cast<int>(normalized_y * kDesignHeight);
  if (y >= 95 && y <= 182 && x >= 32 && x <= 1210) {
    g_page = std::clamp((x - 38) / 233, 0, 4);
    Log(LogLevel::kInfo, "[INPUT] touch tab=%d", g_page);
    InvalidateRect(g_window, nullptr, FALSE);
    return;
  }
  if (g_page == 1 && x >= 800 && x <= 1180 && y >= 895 && y <= 985) {
    if (held) UseSelectedItem();
  } else if (g_page == 2 && x >= 660 && x <= 1185 && y >= 895 && y <= 985) {
    if (held) ToggleSelectedQuest();
  } else if (g_page == 4 && x >= 830 && x <= 1180 && y >= 895 && y <= 985) {
    if (held) ToggleSelectedRadio();
  } else if (g_page == 1 && y >= 920 && y <= 990 && x >= 48 && x <= 250) {
    g_item_offset = std::max(0, g_item_offset - 11);
    g_selected_item = g_item_offset;
  } else if (g_page == 1 && y >= 920 && y <= 990 && x >= 270 && x <= 472) {
    g_item_offset = std::min(
        std::max(0, static_cast<int>(g_snapshot.items.size()) - 11),
        g_item_offset + 11);
    g_selected_item = g_item_offset;
  } else if (g_page == 2 && y >= 895 && y <= 985 && x >= 48 && x <= 250) {
    g_quest_offset = std::max(0, g_quest_offset - 10);
    g_selected_quest = g_quest_offset;
  } else if (g_page == 2 && y >= 895 && y <= 985 && x >= 270 && x <= 472) {
    g_quest_offset = std::min(
        std::max(0, static_cast<int>(g_snapshot.quests.size()) - 10),
        g_quest_offset + 10);
    g_selected_quest = g_quest_offset;
  } else if (g_page == 4 && y >= 895 && y <= 985 && x >= 52 && x <= 250) {
    g_radio_offset = std::max(0, g_radio_offset - 10);
    g_selected_radio = g_radio_offset;
  } else if (g_page == 4 && y >= 895 && y <= 985 && x >= 270 && x <= 468) {
    g_radio_offset = std::min(
        std::max(0, static_cast<int>(g_snapshot.radio.size()) - 10),
        g_radio_offset + 10);
    g_selected_radio = g_radio_offset;
  } else if (g_page == 1 && x >= 45 && x <= 770 && y >= 286 && y < 913) {
    const int visible = std::min(11, static_cast<int>(g_snapshot.items.size()));
    if (visible > 0)
      g_selected_item = g_item_offset + std::min((y - 286) / 57, visible - 1);
  } else if (g_page == 2 && x >= 45 && x <= 630 && y >= 286 && y < 886) {
    const int visible = std::min(10, static_cast<int>(g_snapshot.quests.size()));
    if (visible > 0)
      g_selected_quest = g_quest_offset + std::min((y - 286) / 60, visible - 1);
  } else if (g_page == 3 && y >= 205 && y <= 325) {
    if (!g_local_map_requested && g_snapshot.connected && g_snapshot.root_ready &&
        !g_snapshot.busy && g_client && g_client->SendCommand(13, "[]")) {
      g_local_map_requested = true;
      Log(LogLevel::kInfo, "[MAP] requested local-map stream");
    } else if (!g_local_map_requested) {
      LogRateLimited("local-map-blocked", 2000, LogLevel::kWarning,
                     "[MAP] local-map request blocked connected=%u root=%u busy=%u",
                     g_snapshot.connected ? 1u : 0u,
                     g_snapshot.root_ready ? 1u : 0u,
                     g_snapshot.busy ? 1u : 0u);
    }
  } else if (g_page == 4 && y >= 300 && y < 880) {
    const int visible = std::min(10, static_cast<int>(g_snapshot.radio.size()));
    if (visible > 0)
      g_selected_radio = g_radio_offset + std::min((y - 300) / 58, visible - 1);
  }
  InvalidateRect(g_window, nullptr, FALSE);
}

void DrainTouch() {
  if (g_touch == INVALID_SOCKET) return;
  for (int packet = 0; packet < 64; ++packet) {
    char buffer[256] = {};
    sockaddr_in from = {};
    int from_size = sizeof(from);
    const int count = recvfrom(g_touch, buffer, sizeof(buffer) - 1, 0,
                               reinterpret_cast<sockaddr*>(&from), &from_size);
    if (count == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error != WSAEWOULDBLOCK)
        LogRateLimited("touch-recv", 5000, LogLevel::kWarning,
                       "[INPUT] touch receive error=%d", error);
      break;
    }
    if (from.sin_family != AF_INET ||
        ntohl(from.sin_addr.s_addr) != INADDR_LOOPBACK) {
      LogRateLimited("touch-non-loopback", 5000, LogLevel::kWarning,
                     "[INPUT] rejected non-loopback touch datagram");
      continue;
    }
    buffer[count] = '\0';
    char command[24] = {};
    float x = 0.0f;
    float y = 0.0f;
    float extra = 0.0f;
    char trailing = '\0';
    const int fields = std::sscanf(buffer, " %23s %f %f %f %c", command, &x,
                                   &y, &extra, &trailing);
    const bool tap = !std::strcmp(command, "tap");
    const bool hold = !std::strcmp(command, "hold");
    if ((fields == 3 || fields == 4) && (tap || hold)) {
      Activate(x, y, hold);
    } else {
      LogRateLimited("touch-malformed", 5000, LogLevel::kWarning,
                     "[INPUT] malformed packet bytes=%d fields=%d", count,
                     fields);
    }
  }
}

BOOL CALLBACK FindLowerMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, &info) || (info.dwFlags & MONITORINFOF_PRIMARY))
    return TRUE;
  RECT* selected = reinterpret_cast<RECT*>(parameter);
  const LONG area = (info.rcMonitor.right - info.rcMonitor.left) *
                    (info.rcMonitor.bottom - info.rcMonitor.top);
  const LONG old_area = (selected->right - selected->left) *
                        (selected->bottom - selected->top);
  if (area > old_area) *selected = info.rcMonitor;
  return TRUE;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
      DestroyBackbuffer();
      return 0;
    case WM_TIMER: {
      DrainTouch();
      const uint64_t now = GetTickCount64();
      if (g_database && now >= g_next_snapshot_ms) {
        g_next_snapshot_ms = now + g_refresh_interval_ms;
        if (g_f4se_bridge) {
          g_f4se_bridge->Poll(now);
          F4SEBridgeLifecycleState next_bridge = g_f4se_bridge->Capture();
          if (next_bridge.revision != g_f4se_state.revision) {
            g_f4se_state = next_bridge;
            if (g_f4se_state.availability == F4SEBridgeAvailability::kReady &&
                g_f4se_state.lifecycle_generation != 0) {
              if (g_observed_f4se_generation == 0) {
                g_observed_f4se_generation =
                    g_f4se_state.lifecycle_generation;
              } else if (g_observed_f4se_generation !=
                         g_f4se_state.lifecycle_generation) {
                g_observed_f4se_generation =
                    g_f4se_state.lifecycle_generation;
                if (g_client) g_client->DiscardCommands("F4SE lifecycle change");
                g_database->InvalidateTree(
                    g_f4se_state.last_transition[0]
                        ? g_f4se_state.last_transition
                        : "F4SE lifecycle change");
                g_local_map_requested = false;
                g_local_map = {};
              }
            }
            InvalidateRect(window, nullptr, FALSE);
          }
        }
        if (g_database->CaptureIfChanged(g_snapshot.revision, &g_snapshot)) {
          if (!g_snapshot.connected ||
              g_snapshot.generation != g_observed_generation) {
            g_observed_generation = g_snapshot.generation;
            g_local_map_requested = false;
            g_local_map = {};
          } else if (g_local_map_requested) {
            LocalMapState next = g_database->CaptureLocalMap();
            if (next.revision != g_local_map.revision) {
              g_local_map = std::move(next);
            }
          }
          InvalidateRect(window, nullptr, FALSE);
        }
      }
      return 0;
    }
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(window, &paint);
      Render(window, dc);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(window, 1);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(window, message, wparam, lparam);
}

bool OpenTouchSocket() {
  g_touch = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_touch == INVALID_SOCKET) {
    Log(LogLevel::kError, "[INPUT] touch socket creation failed error=%d",
        WSAGetLastError());
    return false;
  }
  u_long nonblocking = 1;
  if (ioctlsocket(g_touch, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    const int error = WSAGetLastError();
    closesocket(g_touch);
    g_touch = INVALID_SOCKET;
    Log(LogLevel::kError,
        "[INPUT] touch nonblocking setup failed error=%d", error);
    return false;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(g_touch_port);
  inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  if (bind(g_touch, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
      SOCKET_ERROR) {
    const int error = WSAGetLastError();
    closesocket(g_touch);
    g_touch = INVALID_SOCKET;
    Log(LogLevel::kError, "[INPUT] touch bind failed port=%u error=%d",
        g_touch_port, error);
    return false;
  }
  Log(LogLevel::kInfo, "[INPUT] isolated touch listening 127.0.0.1:%u",
      g_touch_port);
  return true;
}

void PanelThreadImpl() {
  const Config& config = LoadConfig();
  g_actions_enabled = config.actions_enabled;
  g_scanlines = config.scanlines;
  g_touch_port = config.touch_port;
  g_refresh_interval_ms =
      std::max<uint32_t>(1, 1000u / std::max<uint32_t>(1, config.diagnostic_refresh_hz));
  g_next_snapshot_ms = GetTickCount64();
  g_f4se_bridge = new F4SEBridgeClient();
  WSADATA winsock = {};
  const int winsock_error = WSAStartup(MAKEWORD(2, 2), &winsock);
  if (winsock_error != 0) {
    Log(LogLevel::kError, "[RENDER] Winsock startup failed error=%d",
        winsock_error);
    SignalPanelStart(false);
    return;
  }
  if (g_database) {
    g_snapshot = g_database->Capture();
    g_observed_generation = g_snapshot.generation;
  }
  RECT monitor = {};
  const uint64_t monitor_deadline = GetTickCount64() + 10000;
  while (monitor.right <= monitor.left || monitor.bottom <= monitor.top) {
    EnumDisplayMonitors(nullptr, nullptr, FindLowerMonitor,
                        reinterpret_cast<LPARAM>(&monitor));
    if (!monitor.right) {
      LogRateLimited("monitor-wait", 5000, LogLevel::kWarning,
                     "[RENDER] waiting for non-primary monitor");
      if (GetTickCount64() >= monitor_deadline) break;
      Sleep(500);
    }
  }
  if (monitor.right <= monitor.left || monitor.bottom <= monitor.top) {
    Log(LogLevel::kError,
        "[RENDER] no non-primary monitor appeared before initialization deadline");
    SignalPanelStart(false);
    WSACleanup();
    return;
  }
  WNDCLASSEXW klass = {};
  klass.cbSize = sizeof(klass);
  klass.hInstance = GetModuleHandleW(nullptr);
  klass.lpfnWndProc = WindowProc;
  klass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  klass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  klass.lpszClassName = L"Fallout4SecondScreenPanel";
  if (!RegisterClassExW(&klass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    Log(LogLevel::kError, "[RENDER] window-class registration failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalPanelStart(false);
    WSACleanup();
    return;
  }
  g_window = CreateWindowExW(
      WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, klass.lpszClassName,
      L"Fallout 4 Dual Screen Pip-Boy", WS_POPUP, monitor.left, monitor.top,
      monitor.right - monitor.left, monitor.bottom - monitor.top, nullptr,
      nullptr, klass.hInstance, nullptr);
  if (!g_window) {
    Log(LogLevel::kError, "[RENDER] panel creation failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalPanelStart(false);
    WSACleanup();
    return;
  }
  if (!SetWindowPos(g_window, HWND_TOP, monitor.left, monitor.top,
                    monitor.right - monitor.left, monitor.bottom - monitor.top,
                    SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
    Log(LogLevel::kError, "[RENDER] panel positioning failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalPanelStart(false);
    DestroyWindow(g_window);
    g_window = nullptr;
    WSACleanup();
    return;
  }
  ShowWindow(g_window, SW_SHOWNOACTIVATE);
  if (!OpenTouchSocket()) {
    SignalPanelStart(false);
    DestroyWindow(g_window);
    g_window = nullptr;
    WSACleanup();
    return;
  }
  if (!SetTimer(g_window, 1, 16, nullptr)) {
    Log(LogLevel::kError, "[RENDER] update timer creation failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalPanelStart(false);
    closesocket(g_touch);
    g_touch = INVALID_SOCKET;
    DestroyWindow(g_window);
    g_window = nullptr;
    WSACleanup();
    return;
  }
  Log(LogLevel::kInfo, "[RENDER] panel initialized rect=%ld,%ld %ldx%ld",
      monitor.left, monitor.top, monitor.right - monitor.left,
      monitor.bottom - monitor.top);
  Log(LogLevel::kInfo,
      "[RENDER] refresh_ms=%u scanlines=%u actions_enabled=%u",
      g_refresh_interval_ms, g_scanlines ? 1u : 0u,
      g_actions_enabled ? 1u : 0u);
  SignalPanelStart(true);
  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  DestroyBackbuffer();
  if (g_touch != INVALID_SOCKET) closesocket(g_touch);
  g_touch = INVALID_SOCKET;
  WSACleanup();
}

void PanelThread() noexcept {
  try {
    PanelThreadImpl();
  } catch (const std::exception& error) {
    Log(LogLevel::kError, "[RENDER] panel thread exception: %s", error.what());
    if (!g_start_success.load()) SignalPanelStart(false);
  } catch (...) {
    Log(LogLevel::kError, "[RENDER] panel thread unknown exception");
    if (!g_start_success.load()) SignalPanelStart(false);
  }
}

}  // namespace

bool StartPanel(PipboyDatabase* database, PipboyClient* client) {
  bool expected = false;
  if (!g_started.compare_exchange_strong(expected, true)) return true;
  g_database = database;
  g_client = client;
  g_start_success.store(false);
  g_start_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!g_start_event) {
    g_started = false;
    return false;
  }
  try {
    std::thread(PanelThread).detach();
  } catch (...) {
    CloseHandle(g_start_event);
    g_start_event = nullptr;
    g_started = false;
    return false;
  }
  const DWORD wait = WaitForSingleObject(g_start_event, 15000);
  CloseHandle(g_start_event);
  g_start_event = nullptr;
  if (wait != WAIT_OBJECT_0 || !g_start_success.load()) {
    g_started = false;
    return false;
  }
  return true;
}

}  // namespace fo4ds
