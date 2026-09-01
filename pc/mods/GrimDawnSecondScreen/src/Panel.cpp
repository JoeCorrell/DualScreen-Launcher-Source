#include <winsock2.h>
#include <ws2tcpip.h>

#include "Panel.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "Log.h"
#include "PageCarousel.h"
#include "PanelInput.h"
#include "PanelStatePolicy.h"
#include "Snapshot.h"
#include "StateStore.h"

namespace gdds {
namespace {

constexpr wchar_t kWindowClass[] = L"GrimDawnSecondScreenPanel";
constexpr wchar_t kWindowTitle[] = L"Grim Dawn Dual-Screen";
constexpr wchar_t const* kPages[] = {
    L"CHARACTER", L"INVENTORY", L"SKILLS", L"DEVOTION",
    L"MAP", L"QUESTS", L"FACTIONS", L"CRAFTING"};
constexpr int kPageCount = static_cast<int>(sizeof(kPages) / sizeof(kPages[0]));
constexpr int kDesignWidth = 1338;
constexpr int kDesignHeight = 1170;
constexpr int kDesignHeaderHeight = 92;
constexpr int kDesignArrowWidth = 96;

HMODULE g_module = nullptr;
HWND g_window = nullptr;
HBITMAP g_bitmap = nullptr;
HGDIOBJ g_previous_bitmap = nullptr;
HDC g_memory_dc = nullptr;
HFONT g_heading_font = nullptr;
HFONT g_body_font = nullptr;
HFONT g_small_font = nullptr;
void* g_pixels = nullptr;
int g_width = 0;
int g_height = 0;
int g_page = 0;
int g_focus = 0;
uint64_t g_drawn_sequence = 0;
uint64_t g_drawn_content_key = 0;
bool g_have_drawn_content = false;
Lifecycle g_logged_lifecycle = Lifecycle::kBoot;
bool g_have_logged_lifecycle = false;
SOCKET g_socket = INVALID_SOCKET;
bool g_winsock_started = false;
ULONGLONG g_last_socket_retry_ms = 0;
unsigned short g_input_port = 7788;
PanelGestureArbiter g_gestures;
SRWLOCK g_frame_lock = SRWLOCK_INIT;
uint32_t g_frame_revision = 0;
bool g_character_stage = false;
volatile LONG g_gpu_active = 0;

struct MonitorChoice {
  RECT bounds = {};
  bool found = false;
};

BOOL CALLBACK FindSecondary(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
  auto* choice = reinterpret_cast<MonitorChoice*>(parameter);
  MONITORINFO info = {};
  info.cbSize = sizeof(info);
  if (GetMonitorInfoW(monitor, &info) && !(info.dwFlags & MONITORINFOF_PRIMARY) &&
      !choice->found) {
    choice->bounds = info.rcMonitor;
    choice->found = true;
  }
  return TRUE;
}

void Color(HDC dc, COLORREF color) { SetTextColor(dc, color); }

int X(int value) {
  return g_width > 0 ? MulDiv(value, g_width, kDesignWidth) : value;
}

int Y(int value) {
  return g_height > 0 ? MulDiv(value, g_height, kDesignHeight) : value;
}

int HeaderHeight() { return Y(kDesignHeaderHeight); }

void Text(HDC dc, int left, int top, int right, int bottom,
          const wchar_t* value, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  RECT area = {left, top, right, bottom};
  DrawTextW(dc, value ? value : L"", -1, &area, flags | DT_NOPREFIX);
}

void Fill(HDC dc, int left, int top, int right, int bottom, COLORREF color) {
  RECT area = {left, top, right, bottom};
  SetDCBrushColor(dc, color);
  FillRect(dc, &area, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
}

void Outline(HDC dc, int left, int top, int right, int bottom,
             COLORREF color, int thickness = 1) {
  if (right <= left || bottom <= top || thickness <= 0) return;
  Fill(dc, left, top, right, std::min(bottom, top + thickness), color);
  Fill(dc, left, std::max(top, bottom - thickness), right, bottom, color);
  Fill(dc, left, top, std::min(right, left + thickness), bottom, color);
  Fill(dc, std::max(left, right - thickness), top, right, bottom, color);
}

void GothicFrame(HDC dc, int left, int top, int right, int bottom,
                 COLORREF interior, bool selected = false) {
  Fill(dc, left, top, right, bottom, interior);
  Outline(dc, left, top, right, bottom,
          selected ? RGB(155, 108, 32) : RGB(70, 58, 39));
  if (right - left > 12 && bottom - top > 12)
    Outline(dc, left + 4, top + 4, right - 4, bottom - 4,
            selected ? RGB(102, 72, 26) : RGB(43, 38, 29));
  const COLORREF corner = selected ? RGB(188, 137, 43) : RGB(104, 79, 39);
  const int mark = std::max(3, std::min(right - left, bottom - top) / 10);
  Fill(dc, left + 2, top + 2, std::min(right, left + 2 + mark), top + 3,
       corner);
  Fill(dc, left + 2, top + 2, left + 3, std::min(bottom, top + 2 + mark),
       corner);
  Fill(dc, std::max(left, right - 2 - mark), top + 2, right - 2, top + 3,
       corner);
  Fill(dc, right - 3, top + 2, right - 2,
       std::min(bottom, top + 2 + mark), corner);
}

void SimpleSlot(HDC dc, int left, int top, int right, int bottom,
                const wchar_t* label = nullptr) {
  Fill(dc, left, top, right, bottom, RGB(14, 13, 11));
  Outline(dc, left, top, right, bottom, RGB(76, 65, 47));
  Outline(dc, left + 3, top + 3, right - 3, bottom - 3, RGB(37, 34, 28));
  if (!label || !label[0]) return;
  HFONT previous = g_small_font
                       ? static_cast<HFONT>(SelectObject(dc, g_small_font))
                       : nullptr;
  Color(dc, RGB(105, 99, 88));
  Text(dc, left + 4, top + 4, right - 4, bottom - 4, label,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (previous) SelectObject(dc, previous);
}

void WideFromUtf8(const char* source, wchar_t* destination, int capacity) {
  if (!destination || capacity < 1) return;
  destination[0] = L'\0';
  if (!source || !source[0]) return;
  int result = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, source, -1,
                                   destination, capacity);
  if (!result)
    MultiByteToWideChar(CP_ACP, 0, source, -1, destination, capacity);
  destination[capacity - 1] = L'\0';
}

const wchar_t* LifecycleText(Lifecycle lifecycle) {
  switch (lifecycle) {
    case Lifecycle::kBoot: return L"INITIALIZING";
    case Lifecycle::kWaitingForGame: return L"Waiting For Save";
    case Lifecycle::kLoading: return L"LOADING / ACTIONS LOCKED";
    case Lifecycle::kLive: return L"LIVE GAME STATE";
    case Lifecycle::kNoCharacter: return L"NO CHARACTER LOADED";
    case Lifecycle::kUnsupported: return L"UNSUPPORTED BUILD";
    case Lifecycle::kFaulted: return L"BRIDGE FAULTED";
  }
  return L"UNKNOWN";
}

void DrawCenteredMessage(HDC dc, const wchar_t* message) {
  Color(dc, RGB(218, 186, 112));
  Text(dc, X(48), HeaderHeight(), g_width - X(48), g_height, message,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawUnavailable(HDC dc, const wchar_t* title, const wchar_t* explanation) {
  Color(dc, RGB(218, 186, 112));
  Text(dc, X(52), Y(180), g_width - X(52), Y(240), title,
       DT_LEFT | DT_TOP | DT_WORDBREAK);
  Color(dc, RGB(185, 180, 170));
  Text(dc, X(52), Y(250), g_width - X(52), Y(380), explanation,
       DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void DrawSectionTitle(HDC dc, int left, int top, int right,
                      const wchar_t* title) {
  Color(dc, RGB(196, 168, 111));
  Text(dc, X(left), Y(top), X(right), Y(top + 30), title,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Fill(dc, X(left + 10), Y(top + 29), X(right - 10), Y(top + 30),
       RGB(70, 57, 36));
}

void DrawEquipmentSlot(HDC dc, int left, int top, int right, int bottom,
                       const wchar_t* label) {
  SimpleSlot(dc, X(left), Y(top), X(right), Y(bottom), label);
}

void DrawCharacterViewport(HDC dc, const Snapshot& state) {
  const int left = X(178);
  const int top = Y(116);
  const int right = X(508);
  const int bottom = Y(674);
  Fill(dc, left, top, right, bottom, RGB(12, 12, 10));
  Outline(dc, left, top, right, bottom, RGB(47, 42, 31));

  HFONT previous = g_small_font
                       ? static_cast<HFONT>(SelectObject(dc, g_small_font))
                       : nullptr;
  if (state.character_name[0]) {
    Color(dc, RGB(201, 176, 121));
    Text(dc, left + X(16), bottom - Y(48), right - X(16), bottom - Y(12),
         state.character_name, DT_CENTER | DT_VCENTER | DT_SINGLELINE |
                                   DT_END_ELLIPSIS);
  }
  if (previous) SelectObject(dc, previous);
}

void DrawStatRow(HDC dc, int left, int top, int right,
                 const wchar_t* label, const wchar_t* value,
                 COLORREF label_color = RGB(195, 187, 171)) {
  Color(dc, label_color);
  Text(dc, X(left), Y(top), X(right - 78), Y(top + 28), label);
  Color(dc, RGB(216, 203, 172));
  Text(dc, X(right - 82), Y(top), X(right), Y(top + 28), value,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawCharacterStats(HDC dc, const Snapshot& state) {
  const bool attributes = (state.capabilities & kCapAttributes) != 0;
  const bool vitals = (state.capabilities & kCapVitals) != 0;
  wchar_t value[64] = {};
  GothicFrame(dc, X(21), Y(746), X(336), Y(992), RGB(17, 16, 13));
  GothicFrame(dc, X(340), Y(746), X(660), Y(992), RGB(17, 16, 13));
  DrawSectionTitle(dc, 28, 752, 328, L"ATTRIBUTES");
  DrawSectionTitle(dc, 348, 752, 652, L"COMBAT STATS");

  HFONT previous = g_small_font
                       ? static_cast<HFONT>(SelectObject(dc, g_small_font))
                       : nullptr;
  if (attributes) std::swprintf(value, 64, L"%.0f", state.physique);
  else std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 45, 790, 314, L"Physique", value);
  if (attributes) std::swprintf(value, 64, L"%.0f", state.cunning);
  else std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 45, 818, 314, L"Cunning", value);
  if (attributes) std::swprintf(value, 64, L"%.0f", state.spirit);
  else std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 45, 846, 314, L"Spirit", value);
  if (vitals)
    std::swprintf(value, 64, L"%.0f / %.0f", state.health, state.max_health);
  else
    std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 45, 885, 314, L"Health", value, RGB(184, 67, 55));
  if (vitals)
    std::swprintf(value, 64, L"%.0f / %.0f", state.energy, state.max_energy);
  else
    std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 45, 917, 314, L"Energy", value, RGB(54, 148, 125));

  if (attributes)
    std::swprintf(value, 64, L"%.0f", state.offensive_ability);
  else
    std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 360, 790, 635, L"Offensive Ability", value);
  if (attributes)
    std::swprintf(value, 64, L"%.0f", state.defensive_ability);
  else
    std::swprintf(value, 64, L"\u2014");
  DrawStatRow(dc, 360, 824, 635, L"Defensive Ability", value);

  Color(dc, RGB(196, 168, 111));
  Text(dc, X(360), Y(862), X(635), Y(892), L"RESISTANCES",
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  constexpr wchar_t const* resistance_names[] = {
      L"FIRE", L"COLD", L"LIGHT", L"POISON", L"AETHER", L"CHAOS"};
  const int resistance_left = 354;
  const int resistance_width = 47;
  for (int index = 0; index < 6; ++index) {
    const int item_left = resistance_left + index * resistance_width;
    const COLORREF accent = index == 0   ? RGB(181, 71, 34)
                            : index == 1 ? RGB(83, 144, 191)
                            : index == 2 ? RGB(89, 145, 204)
                            : index == 3 ? RGB(105, 142, 52)
                            : index == 4 ? RGB(149, 63, 55)
                                         : RGB(126, 74, 145);
    Fill(dc, X(item_left + 18), Y(903), X(item_left + 27), Y(912), accent);
    Color(dc, RGB(130, 123, 109));
    Text(dc, X(item_left), Y(916), X(item_left + resistance_width), Y(938),
         resistance_names[index], DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    Color(dc, RGB(202, 190, 163));
    Text(dc, X(item_left), Y(940), X(item_left + resistance_width), Y(968),
         L"\u2014", DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  if (previous) SelectObject(dc, previous);
}

void DrawEquipmentPane(HDC dc, const Snapshot& state) {
  const bool equipment_available =
      (state.capabilities & kCapEquipment) != 0;
  const wchar_t* unavailable = equipment_available ? nullptr : L"\u2014";
  GothicFrame(dc, X(21), Y(99), X(660), Y(740), RGB(16, 15, 12));
  DrawEquipmentSlot(dc, 68, 121, 173, 292,
                    unavailable ? unavailable : L"WEAPON");
  DrawEquipmentSlot(dc, 68, 307, 173, 438,
                    unavailable ? unavailable : L"CHEST");
  DrawEquipmentSlot(dc, 68, 452, 173, 559,
                    unavailable ? unavailable : L"LEGS");
  DrawEquipmentSlot(dc, 68, 576, 173, 655,
                    unavailable ? unavailable : L"FEET");
  DrawEquipmentSlot(dc, 517, 121, 609, 229,
                    unavailable ? unavailable : L"AMULET");
  DrawEquipmentSlot(dc, 517, 244, 609, 334,
                    unavailable ? unavailable : L"RING");
  DrawEquipmentSlot(dc, 517, 349, 609, 439,
                    unavailable ? unavailable : L"HANDS");
  DrawEquipmentSlot(dc, 517, 454, 609, 559,
                    unavailable ? unavailable : L"RING");
  DrawEquipmentSlot(dc, 517, 575, 609, 655,
                    unavailable ? unavailable : L"BELT");
  DrawEquipmentSlot(dc, 220, 662, 318, 725,
                    unavailable ? unavailable : L"MEDAL");
  DrawEquipmentSlot(dc, 369, 662, 467, 725,
                    unavailable ? unavailable : L"RELIC");
  DrawCharacterViewport(dc, state);
  if (!equipment_available) {
    Color(dc, RGB(128, 120, 104));
    Text(dc, X(190), Y(105), X(496), Y(133),
         L"EQUIPMENT DATA UNAVAILABLE",
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void DrawInventoryPane(HDC dc, const Snapshot& state) {
  GothicFrame(dc, X(671), Y(99), X(1320), Y(992), RGB(16, 15, 12));
  const bool count_available =
      (state.capabilities & kCapInventoryCount) != 0;
  wchar_t title[96] = {};
  if (count_available)
    std::swprintf(title, 96, L"INVENTORY  %u OBJECTS",
                  state.inventory_count);
  else
    std::swprintf(title, 96, L"INVENTORY");
  DrawSectionTitle(dc, 688, 105, 1303, title);

  const int large_left = 713;
  const int large_top = 143;
  const int large_width = 132;
  const int large_height = 142;
  const int large_gap = 15;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int left = large_left + column * (large_width + large_gap);
      const int top = large_top + row * (large_height + 15);
      SimpleSlot(dc, X(left), Y(top), X(left + large_width),
                 Y(top + large_height));
    }
  }

  const int small_left = 713;
  const int small_top = 460;
  const int small_width = 132;
  const int small_height = 88;
  for (int row = 0; row < 2; ++row) {
    for (int column = 0; column < 4; ++column) {
      const int left = small_left + column * (small_width + large_gap);
      const int top = small_top + row * (small_height + 13);
      SimpleSlot(dc, X(left), Y(top), X(left + small_width),
                 Y(top + small_height));
    }
  }

  const int grid_left = 713;
  const int grid_top = 663;
  const int grid_width = 94;
  const int grid_height = 61;
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 6; ++column) {
      const int left = grid_left + column * grid_width;
      const int top = grid_top + row * grid_height;
      SimpleSlot(dc, X(left), Y(top), X(left + grid_width),
                 Y(top + grid_height));
    }
  }

  GothicFrame(dc, X(690), Y(925), X(1301), Y(978), RGB(13, 12, 10));
  wchar_t money[64] = {};
  if (state.capabilities & kCapEconomy)
    std::swprintf(money, 64, L"IRON BITS   %u", state.iron_bits);
  else
    std::swprintf(money, 64, L"IRON BITS   \u2014");
  Color(dc, RGB(202, 169, 97));
  Text(dc, X(704), Y(929), X(1287), Y(975), money,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  if (!(state.capabilities & kCapInventory)) {
    Color(dc, RGB(128, 120, 104));
    Text(dc, X(713), Y(610), X(1277), Y(650),
         L"ITEM RECORDS NOT YET AVAILABLE",
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void DrawQuickBar(HDC dc, const Snapshot& state) {
  GothicFrame(dc, X(10), Y(1001), X(1328), Y(1161), RGB(13, 12, 10));
  if (!(state.capabilities & kCapActions)) {
    Color(dc, RGB(128, 120, 104));
    Text(dc, X(30), Y(1014), X(1308), Y(1147),
         L"QUICK ACCESS ACTIONS UNAVAILABLE",
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const int left = 30;
  const int gap = 10;
  const int width = 134;
  for (int index = 0; index < 9; ++index) {
    const int slot_left = left + index * (width + gap);
    SimpleSlot(dc, X(slot_left), Y(1014), X(slot_left + width), Y(1147));
    wchar_t number[8] = {};
    std::swprintf(number, 8, L"%d", index + 1);
    Color(dc, RGB(199, 187, 158));
    Text(dc, X(slot_left), Y(1118), X(slot_left + width), Y(1145), number,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void DrawCharacter(HDC dc, const Snapshot& state) {
  DrawEquipmentPane(dc, state);
  DrawCharacterStats(dc, state);
  DrawInventoryPane(dc, state);
  DrawQuickBar(dc, state);
}

void DrawMap(HDC dc, const Snapshot& state) {
  wchar_t area[160] = {};
  wchar_t line[256] = {};
  WideFromUtf8(state.area_tag, area, 160);
  const bool position_available =
      (state.capabilities & kCapWorldPosition) != 0;
  const bool map_available = (state.capabilities & kCapMap) != 0;
  const bool area_available = (state.capabilities & kCapAreaTag) != 0;
  Color(dc, RGB(218, 186, 112));
  Text(dc, X(52), Y(170), g_width - X(52), Y(220),
       L"LIVE WORLD POSITION");
  Color(dc, RGB(220, 216, 205));
  if (position_available) {
    std::swprintf(line, 256, L"X  %.3f\nY  %.3f\nZ  %.3f",
                  state.player_x, state.player_y, state.player_z);
  } else {
    std::swprintf(line, 256, L"Position unavailable");
  }
  const size_t used = std::wcslen(line);
  std::swprintf(line + used, 256 - used,
                map_available ? L"\n\nKnown Riftgate IDs  %u"
                              : L"\n\nRiftgate state unavailable",
                state.riftgate_count);
  Text(dc, X(52), Y(235), g_width / 2, Y(390), line,
       DT_LEFT | DT_TOP);
  Color(dc, RGB(190, 184, 169));
  Text(dc, g_width / 2, Y(235), g_width - X(52), Y(285),
       area_available && area[0] ? area : L"Area tag unavailable");
  Color(dc, RGB(151, 145, 133));
  Text(dc, X(52), Y(430), g_width - X(52), Y(560),
       L"The independent map renderer, discovery state, markers, and Riftgate records are capability-gated pending ARZ/ARC cache integration and live structure validation.",
       DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void Render() {
  if (!g_memory_dc || !g_width || !g_height) return;
  AcquireSRWLockExclusive(&g_frame_lock);
  LARGE_INTEGER frequency = {};
  LARGE_INTEGER start = {};
  LARGE_INTEGER end = {};
  QueryPerformanceFrequency(&frequency);
  QueryPerformanceCounter(&start);
  Fill(g_memory_dc, 0, 0, g_width, g_height, RGB(10, 10, 9));
  Fill(g_memory_dc, 0, 0, g_width, HeaderHeight(), RGB(18, 17, 14));
  Fill(g_memory_dc, 0, HeaderHeight() - Y(3), g_width, HeaderHeight(),
       RGB(89, 65, 31));
  SetBkMode(g_memory_dc, TRANSPARENT);
  HFONT heading = g_heading_font ? g_heading_font
                                 : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  HFONT body = g_body_font ? g_body_font
                           : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
  HFONT old_font = static_cast<HFONT>(SelectObject(g_memory_dc, heading));
  const int arrow_width = X(kDesignArrowWidth);
  GothicFrame(g_memory_dc, X(10), Y(8), arrow_width - X(5), Y(84),
              RGB(24, 22, 18));
  GothicFrame(g_memory_dc, g_width - arrow_width + X(5), Y(8),
              g_width - X(10), Y(84), RGB(24, 22, 18));
  Color(g_memory_dc, RGB(232, 202, 132));
  Text(g_memory_dc, X(10), Y(8), arrow_width - X(5), Y(84), L"\u25c0",
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(g_memory_dc, g_width - arrow_width + X(5), Y(8), g_width - X(10),
       Y(84), L"\u25b6",
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  const int carousel_width = g_width - 2 * arrow_width;
  const int tab_width = carousel_width / 3;
  for (int slot = 0; slot < 3; ++slot) {
    const int index = CarouselPage(g_page, slot, kPageCount);
    const int left = arrow_width + slot * tab_width + X(3);
    const int right = (slot == 2 ? g_width - arrow_width : left + tab_width) -
                      X(3);
    GothicFrame(g_memory_dc, left, Y(8), right, Y(84),
                slot == 1 ? RGB(40, 31, 20) : RGB(19, 18, 15), slot == 1);
    Color(g_memory_dc, slot == 1 ? RGB(232, 202, 132)
                                  : RGB(157, 150, 137));
    Text(g_memory_dc, left + X(4), Y(8), right - X(4), Y(84), kPages[index],
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  SelectObject(g_memory_dc, body);
  Snapshot state = {};
  const bool state_available = CopyLatestSnapshot(&state);
  g_character_stage = false;
  if (!state_available) {
    DrawCenteredMessage(g_memory_dc, L"Waiting For Save");
  } else if (state.lifecycle == Lifecycle::kBoot ||
             state.lifecycle == Lifecycle::kWaitingForGame ||
             state.lifecycle == Lifecycle::kNoCharacter) {
    DrawCenteredMessage(g_memory_dc, L"Waiting For Save");
  } else if (state.lifecycle != Lifecycle::kLive) {
    DrawCenteredMessage(g_memory_dc, LifecycleText(state.lifecycle));
  } else if (g_page == 0) {
    DrawCharacter(g_memory_dc, state);
    g_character_stage = true;
  } else if (g_page == 1) {
    wchar_t line[256] = {};
    if (state.capabilities & kCapInventoryCount)
      std::swprintf(line, 256,
                    L"Live inventory-object count: %u\n\nFull bag enumeration, item record resolution, icons, stacks, components, augments, and safe game-thread actions are not yet validated.",
                    state.inventory_count);
    else
      std::swprintf(line, 256,
                    L"Inventory count unavailable.\n\nNo empty inventory is being inferred or displayed.");
    DrawUnavailable(g_memory_dc, L"INVENTORY DOMAIN - LIVE COUNT ONLY", line);
  } else if (g_page == 2) {
    wchar_t line[256] = {};
    if (state.capabilities & kCapSkills)
      std::swprintf(
          line, 256,
          L"Available skill points: %u\nRuntime skill-vector entries: %u\n\nRanks, resource identifiers, modifiers, assigned abilities, and cooldowns remain disabled until each Skill pointer and Name digest path is live-validated.",
          state.skill_points, state.skill_count);
    else
      std::swprintf(line, 256,
                    L"Skill state unavailable.\n\nNo zero ranks or empty skill tree are being inferred.");
    DrawUnavailable(g_memory_dc, L"SKILLS - LIVE PRIMITIVES", line);
  } else if (g_page == 3) {
    wchar_t line[256] = {};
    if (state.capabilities & kCapDevotion)
      std::swprintf(
          line, 256,
          L"Available Devotion points: %u\nTotal earned: %u\nCurrent maximum: %u\n\nConstellation records, affinity rules, node completion, and assignments require the static database cache and live Skill correlation.",
          state.devotion_points_available, state.devotion_points_total,
          state.devotion_points_max);
    else
      std::swprintf(line, 256,
                    L"Devotion state unavailable.\n\nNo zero-point constellation state is being inferred.");
    DrawUnavailable(g_memory_dc, L"DEVOTION - LIVE POINT STATE", line);
  } else if (g_page == 4) {
    DrawMap(g_memory_dc, state);
  } else {
    DrawUnavailable(
        g_memory_dc, kPages[g_page],
        L"This domain is deliberately unavailable in the diagnostic build. No values are synthesized and no normal game menu is being scraped or opened.");
  }
  SelectObject(g_memory_dc, old_font);
  g_drawn_sequence = state.sequence;
  g_drawn_content_key = PanelContentKey(&state, state_available, g_page);
  g_have_drawn_content = true;
  if (state_available &&
      (!g_have_logged_lifecycle || state.lifecycle != g_logged_lifecycle)) {
    Log(LogLevel::kInfo, "[PANEL] lifecycle=%u sequence=%llu page=%d",
        static_cast<unsigned>(state.lifecycle),
        static_cast<unsigned long long>(state.sequence), g_page);
    g_logged_lifecycle = state.lifecycle;
    g_have_logged_lifecycle = true;
  }
  ++g_frame_revision;
  if (!g_frame_revision) ++g_frame_revision;
  ReleaseSRWLockExclusive(&g_frame_lock);
  QueryPerformanceCounter(&end);
  if (frequency.QuadPart > 0) {
    const double milliseconds =
        1000.0 * static_cast<double>(end.QuadPart - start.QuadPart) /
        static_cast<double>(frequency.QuadPart);
    if (milliseconds > 8.0)
      LogRateLimited("panel-render-slow", 1000, LogLevel::kWarning,
                     "[PERF] panel render took %.3f ms page=%d", milliseconds,
                     g_page);
  }
}

void ChangePage(int delta) {
  g_page = WrapPage(g_page, delta, kPageCount);
  g_focus = 0;
  Render();
  InvalidateRect(g_window, nullptr, FALSE);
  Log(LogLevel::kInfo, "[INPUT] panel page=%d name=%ls", g_page,
      kPages[g_page]);
}

void HandleGesture(const PanelGesture& gesture) {
  if (gesture.kind == PanelGestureKind::kPage) {
    ChangePage(gesture.direction);
  } else if (gesture.kind == PanelGestureKind::kNavLeft) {
    ChangePage(-1);
  } else if (gesture.kind == PanelGestureKind::kNavRight) {
    ChangePage(1);
  } else if (gesture.kind == PanelGestureKind::kTap &&
             gesture.y <= (g_height > 0
                               ? static_cast<float>(HeaderHeight()) / g_height
                               : 0.12f)) {
    const float arrow_fraction =
        g_width > 0 ? static_cast<float>(X(kDesignArrowWidth)) / g_width
                    : 0.08f;
    const int delta = CarouselTapDelta(gesture.x, arrow_fraction);
    if (delta) ChangePage(delta);
  } else if (gesture.kind == PanelGestureKind::kNavUp) {
    g_focus = std::max(0, g_focus - 1);
  } else if (gesture.kind == PanelGestureKind::kNavDown) {
    g_focus = std::min(g_focus + 1, 1024);
  }
}

void PollInput() {
  if (g_socket == INVALID_SOCKET) return;
  for (int packet_index = 0; packet_index < 32; ++packet_index) {
    char bytes[256] = {};
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    const int received = recvfrom(g_socket, bytes, sizeof(bytes), 0,
                                  reinterpret_cast<sockaddr*>(&sender),
                                  &sender_size);
    if (received == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error != WSAEWOULDBLOCK)
        LogRateLimited("panel-udp-read", 5000, LogLevel::kWarning,
                       "[INPUT] UDP receive failed error=%d", error);
      break;
    }
    PanelPacket packet;
    if (!ParsePanelPacket(bytes, static_cast<size_t>(received), &packet)) {
      LogRateLimited("panel-udp-invalid", 1000, LogLevel::kWarning,
                     "[INPUT] rejected malformed panel packet bytes=%d",
                     received);
      continue;
    }
    HandleGesture(g_gestures.Consume(packet));
  }
}

bool OpenInputSocket(unsigned short port) {
  if (g_socket != INVALID_SOCKET) return true;
  if (!g_winsock_started) {
    WSADATA data = {};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    g_winsock_started = true;
  }
  g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_socket == INVALID_SOCKET) return false;
  u_long nonblocking = 1;
  ioctlsocket(g_socket, FIONBIO, &nonblocking);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (bind(g_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
      SOCKET_ERROR) {
    closesocket(g_socket);
    g_socket = INVALID_SOCKET;
    return false;
  }
  Log(LogLevel::kInfo, "[INPUT] isolated panel UDP bound to 127.0.0.1:%u",
      static_cast<unsigned>(port));
  return true;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE: return 0;
    case WM_TIMER:
      if (g_socket == INVALID_SOCKET) {
        const ULONGLONG now = GetTickCount64();
        if (now - g_last_socket_retry_ms >= 5000) {
          g_last_socket_retry_ms = now;
          if (!OpenInputSocket(g_input_port))
            LogRateLimited("panel-udp-bind", 5000, LogLevel::kWarning,
                           "[INPUT] panel UDP bind unavailable; retrying");
        }
      }
      PollInput();
      if (LatestSequence() != g_drawn_sequence) {
        Snapshot latest = {};
        const bool available = CopyLatestSnapshot(&latest);
        const uint64_t content_key =
            PanelContentKey(&latest, available, g_page);
        if (!g_have_drawn_content || content_key != g_drawn_content_key) {
          Render();
          InvalidateRect(window, nullptr, FALSE);
        } else {
          g_drawn_sequence = latest.sequence;
        }
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(window, &paint);
      if (InterlockedCompareExchange(&g_gpu_active, 0, 0) == 0) {
        AcquireSRWLockShared(&g_frame_lock);
        BitBlt(dc, 0, 0, g_width, g_height, g_memory_dc, 0, 0, SRCCOPY);
        ReleaseSRWLockShared(&g_frame_lock);
      }
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_NCHITTEST: return HTCLIENT;
    default: return DefWindowProcW(window, message, wparam, lparam);
  }
}

DWORD WINAPI PanelThread(void*) {
  MonitorChoice monitor;
  for (unsigned attempt = 0; attempt < 1200 && !monitor.found; ++attempt) {
    monitor = {};
    EnumDisplayMonitors(nullptr, nullptr, FindSecondary,
                        reinterpret_cast<LPARAM>(&monitor));
    if (!monitor.found) {
      if (attempt == 0)
        Log(LogLevel::kInfo,
            "[PANEL] secondary monitor not published yet; waiting for Wine display-topology readiness");
      Sleep(50);
    }
  }
  if (!monitor.found) {
    Log(LogLevel::kError,
        "[PANEL] no non-primary monitor advertised after 60 seconds; panel not created");
    return 0;
  }
  g_width = monitor.bounds.right - monitor.bounds.left;
  g_height = monitor.bounds.bottom - monitor.bounds.top;
  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = g_module;
  window_class.lpfnWndProc = WindowProc;
  window_class.lpszClassName = kWindowClass;
  window_class.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassExW(&window_class) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    Log(LogLevel::kError, "[PANEL] class registration failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return 0;
  }
  g_window = CreateWindowExW(
      WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kWindowClass, kWindowTitle, WS_POPUP,
      monitor.bounds.left, monitor.bounds.top, g_width, g_height, nullptr, nullptr,
      g_module, nullptr);
  if (!g_window) {
    Log(LogLevel::kError, "[PANEL] window creation failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    return 0;
  }
  HDC window_dc = GetDC(g_window);
  g_memory_dc = CreateCompatibleDC(window_dc);
  BITMAPINFO bitmap = {};
  bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap.bmiHeader.biWidth = g_width;
  bitmap.bmiHeader.biHeight = -g_height;
  bitmap.bmiHeader.biPlanes = 1;
  bitmap.bmiHeader.biBitCount = 32;
  bitmap.bmiHeader.biCompression = BI_RGB;
  g_bitmap = CreateDIBSection(window_dc, &bitmap, DIB_RGB_COLORS, &g_pixels,
                              nullptr, 0);
  ReleaseDC(g_window, window_dc);
  if (!g_memory_dc || !g_bitmap || !g_pixels) {
    Log(LogLevel::kError, "[PANEL] DIB backbuffer allocation failed");
    return 0;
  }
  g_previous_bitmap = SelectObject(g_memory_dc, g_bitmap);
  g_heading_font = CreateFontW(
      -28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_ROMAN, L"Georgia");
  g_body_font = CreateFontW(
      -22, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_ROMAN, L"Georgia");
  g_small_font = CreateFontW(
      -17, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
      OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
      DEFAULT_PITCH | FF_ROMAN, L"Georgia");
  Render();
  SetWindowPos(g_window, HWND_NOTOPMOST, monitor.bounds.left, monitor.bounds.top,
               g_width, g_height, SWP_NOACTIVATE | SWP_SHOWWINDOW);
  ShowWindow(g_window, SW_SHOWNOACTIVATE);
  SetTimer(g_window, 1, 50, nullptr);
  if (!OpenInputSocket(g_input_port))
    Log(LogLevel::kWarning,
        "[INPUT] initial panel UDP bind failed; background retry enabled");
  Log(LogLevel::kInfo,
      "[PANEL] initialized class=%ls title=%ls bounds=%ld,%ld %dx%d",
      kWindowClass, kWindowTitle, monitor.bounds.left, monitor.bounds.top,
      g_width, g_height);
  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  if (g_heading_font) DeleteObject(g_heading_font);
  if (g_body_font) DeleteObject(g_body_font);
  if (g_small_font) DeleteObject(g_small_font);
  g_heading_font = nullptr;
  g_body_font = nullptr;
  g_small_font = nullptr;
  if (g_socket != INVALID_SOCKET) {
    closesocket(g_socket);
    g_socket = INVALID_SOCKET;
  }
  if (g_winsock_started) {
    WSACleanup();
    g_winsock_started = false;
  }
  if (g_memory_dc && g_previous_bitmap)
    SelectObject(g_memory_dc, g_previous_bitmap);
  g_previous_bitmap = nullptr;
  if (g_bitmap) DeleteObject(g_bitmap);
  if (g_memory_dc) DeleteDC(g_memory_dc);
  g_bitmap = nullptr;
  g_memory_dc = nullptr;
  g_pixels = nullptr;
  return 0;
}

}  // namespace

bool StartPanel(HMODULE module, unsigned short input_port) {
  if (!module) return false;
  g_module = module;
  g_input_port = input_port;
  HANDLE thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0, nullptr);
  if (!thread) return false;
  CloseHandle(thread);
  return true;
}

HWND PanelWindow() {
  return static_cast<HWND>(InterlockedCompareExchangePointer(
      reinterpret_cast<void* volatile*>(&g_window), nullptr, nullptr));
}

bool CopyPanelFrame(uint32_t* pixels, size_t pixel_capacity,
                    PanelFrameInfo* info) {
  if (!info) return false;
  AcquireSRWLockShared(&g_frame_lock);
  const size_t required = g_width > 0 && g_height > 0
                              ? static_cast<size_t>(g_width) *
                                    static_cast<size_t>(g_height)
                              : 0;
  info->window = g_window;
  info->width = g_width;
  info->height = g_height;
  info->revision = g_frame_revision;
  info->character_stage = g_character_stage;
  // Only the scene interior is transparent. The frame and character-name
  // footer remain opaque and are composited above the native 3D character.
  info->character_bounds = {X(184), Y(122), X(502), Y(624)};
  const bool ready = g_pixels && required &&
                     (!pixels || pixel_capacity >= required);
  if (ready && pixels)
    std::memcpy(pixels, g_pixels, required * sizeof(uint32_t));
  ReleaseSRWLockShared(&g_frame_lock);
  if (!ready || !pixels) return ready;

  for (size_t index = 0; index < required; ++index) pixels[index] |= 0xff000000u;
  if (info->character_stage) {
    const int left = std::max(0, static_cast<int>(info->character_bounds.left));
    const int top = std::max(0, static_cast<int>(info->character_bounds.top));
    const int right =
        std::min(info->width, static_cast<int>(info->character_bounds.right));
    const int bottom =
        std::min(info->height, static_cast<int>(info->character_bounds.bottom));
    for (int y = top; y < bottom; ++y) {
      uint32_t* row = pixels + static_cast<size_t>(y) * info->width;
      for (int x = left; x < right; ++x) row[x] &= 0x00ffffffu;
    }
  }
  return true;
}

void SetPanelGpuActive(bool active) {
  const LONG desired = active ? 1 : 0;
  InterlockedExchange(&g_gpu_active, desired);
  // WEMU's lower-output compositor uses the guest window's damage cadence to
  // keep the game-owned secondary swapchain latched. Invalidating only on the
  // first CPU/GPU transition lets the newly selected DXGI window go black at
  // the main-menu handoff even though PresentSurface keeps succeeding.
  HWND window = PanelWindow();
  if (window) InvalidateRect(window, nullptr, FALSE);
}

}  // namespace gdds
