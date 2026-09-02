#include "Panel.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "AssetCache.h"
#include "Log.h"
#include "PanelInput.h"
#include "PanelLayout.h"
#include "StateStore.h"

namespace wfcds {
namespace {

constexpr int kWidth = kPanelWidth;
constexpr int kHeight = kPanelHeight;
constexpr UINT_PTR kRenderTimer = 1;
constexpr UINT kShutdownMessage = WM_APP + 0x5746;
constexpr unsigned short kInputPort = 7788;

constexpr COLORREF kBackground = RGB(1, 8, 13);
constexpr COLORREF kPanel = RGB(4, 17, 25);
constexpr COLORREF kPanelAlt = RGB(6, 22, 31);
constexpr COLORREF kCyan = RGB(0, 183, 235);
constexpr COLORREF kCyanDim = RGB(10, 74, 96);
constexpr COLORREF kOrange = RGB(255, 151, 0);
constexpr COLORREF kTransformYellow = RGB(255, 193, 7);
constexpr COLORREF kTransformYellowPressed = RGB(255, 211, 64);
constexpr COLORREF kWhite = RGB(226, 232, 235);
constexpr COLORREF kMuted = RGB(148, 164, 171);

HMODULE g_module = nullptr;
HANDLE g_thread = nullptr;
DWORD g_thread_id = 0;
HWND g_window = nullptr;
HDC g_memory_dc = nullptr;
HBITMAP g_bitmap = nullptr;
HGDIOBJ g_old_bitmap = nullptr;
HFONT g_font_label = nullptr;
HFONT g_font_body = nullptr;
HFONT g_font_title = nullptr;
HFONT g_font_action = nullptr;
HFONT g_font_mode = nullptr;
HFONT g_font_transform = nullptr;
SOCKET g_socket = INVALID_SOCKET;
bool g_winsock = false;
PanelPage g_page = PanelPage::kCombat;
HitTarget g_pressed = HitTarget::kNone;
HitTarget g_focus = HitTarget::kTransform;
ULONGLONG g_pressed_until = 0;
std::array<ULONGLONG, 16> g_last_action_by_target = {};
uint64_t g_drawn_sequence = 0;

RECT Rect(int left, int top, int right, int bottom) {
  RECT value = {left, top, right, bottom};
  return value;
}

RECT Rect(const PanelPixelRect& source) {
  return Rect(source.left, source.top, source.right, source.bottom);
}

void Fill(HDC dc, const RECT& rect, COLORREF color) {
  HBRUSH brush = CreateSolidBrush(color);
  FillRect(dc, &rect, brush);
  DeleteObject(brush);
}

void Frame(HDC dc, const RECT& rect, COLORREF color, int width = 2) {
  HPEN pen = CreatePen(PS_SOLID, width, color);
  HGDIOBJ old_pen = SelectObject(dc, pen);
  HGDIOBJ old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, rect.left, rect.top, rect.right, rect.bottom);
  SelectObject(dc, old_brush);
  SelectObject(dc, old_pen);
  DeleteObject(pen);
}

std::wstring Wide(const char* text) {
  if (!text || !*text) return L"";
  const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text,
                                        -1, nullptr, 0);
  if (count <= 1) return L"";
  std::wstring result(static_cast<size_t>(count), L'\0');
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1,
                           result.data(), count))
    return L"";
  result.pop_back();
  return result;
}

void Text(HDC dc, const wchar_t* value, RECT rect, HFONT font, COLORREF color,
          UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
  HGDIOBJ old = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  DrawTextW(dc, value ? value : L"", -1, &rect, flags | DT_NOPREFIX);
  SelectObject(dc, old);
}

void TextUtf8(HDC dc, const char* value, RECT rect, HFONT font,
              COLORREF color, UINT flags = DT_LEFT | DT_VCENTER |
                                            DT_SINGLELINE) {
  const std::wstring wide = Wide(value);
  Text(dc, wide.empty() ? L"--" : wide.c_str(), rect, font, color, flags);
}

void PanelBox(HDC dc, RECT rect, bool active = false) {
  Fill(dc, rect, active ? RGB(8, 27, 35) : kPanel);
  Frame(dc, rect, active ? kOrange : kCyanDim, active ? 3 : 2);
}

const wchar_t* FactionText(Faction faction) {
  if (faction == Faction::kAutobot) return L"AUTOBOT";
  if (faction == Faction::kDecepticon) return L"DECEPTICON";
  return L"--";
}

const wchar_t* ModeText(TransformerMode mode) {
  if (mode == TransformerMode::kRobot) return L"ROBOT MODE";
  if (mode == TransformerMode::kVehicle) return L"VEHICLE MODE";
  if (mode == TransformerMode::kTransitioning) return L"TRANSFORMING";
  return L"--";
}

void Segments(HDC dc, RECT rect, float current, float maximum,
              COLORREF color) {
  constexpr int count = 15;
  const float ratio = maximum > 0.0f && current >= 0.0f
                          ? std::clamp(current / maximum, 0.0f, 1.0f)
                          : 0.0f;
  const int gap = 5;
  const int segment_width = (rect.right - rect.left - gap * (count - 1)) / count;
  for (int i = 0; i < count; ++i) {
    RECT segment = Rect(rect.left + i * (segment_width + gap), rect.top,
                        rect.left + i * (segment_width + gap) + segment_width,
                        rect.bottom);
    const float threshold = static_cast<float>(i + 1) / count;
    Fill(dc, segment, ratio + 0.001f >= threshold ? color : RGB(12, 39, 48));
    Frame(dc, segment, ratio + 0.001f >= threshold ? color : kCyanDim, 1);
  }
}

bool Pressed(HitTarget target) {
  return g_pressed == target && GetTickCount64() < g_pressed_until;
}

bool Focused(HitTarget target) {
  return g_focus == target;
}

void DrawArrow(HDC dc, RECT rect, bool right, bool pressed) {
  PanelBox(dc, rect, pressed);
  const int cy = (rect.top + rect.bottom) / 2;
  const int cx = (rect.left + rect.right) / 2;
  HPEN pen = CreatePen(PS_SOLID, 6, kWhite);
  HGDIOBJ old = SelectObject(dc, pen);
  MoveToEx(dc, right ? cx - 18 : cx + 18, cy - 20, nullptr);
  LineTo(dc, right ? cx + 6 : cx - 6, cy);
  LineTo(dc, right ? cx - 18 : cx + 18, cy + 20);
  SelectObject(dc, old);
  DeleteObject(pen);
}

void DrawNavigation(HDC dc) {
  DrawArrow(dc, Rect(kPreviousPageRect), false,
            Pressed(HitTarget::kPreviousPage));
  DrawArrow(dc, Rect(kNextPageRect), true,
            Pressed(HitTarget::kNextPage));
  struct Tab { RECT rect; const wchar_t* text; PanelPage page; HitTarget hit; };
  const Tab tabs[] = {
      {Rect(kCombatPageRect), L"COMBAT", PanelPage::kCombat,
       HitTarget::kCombatPage},
      {Rect(kSquadPageRect), L"SQUAD", PanelPage::kSquad,
       HitTarget::kSquadPage},
      {Rect(kObjectivesPageRect), L"OBJECTIVES", PanelPage::kObjectives,
       HitTarget::kObjectivesPage},
  };
  for (const Tab& tab : tabs) {
    const bool active = tab.page == g_page;
    PanelBox(dc, tab.rect, active || Pressed(tab.hit));
    Text(dc, tab.text, tab.rect, g_font_title, active ? kOrange : kCyan,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void DrawStatus(HDC dc, const Snapshot& state) {
  const RECT strip = Rect(14, 96, 1226, 205);
  PanelBox(dc, strip);
  if (state.faction == Faction::kAutobot)
    DrawCachedAsset(dc, "AutobotEmblem", Rect(27, 108, 96, 193));
  else
    DrawCachedAsset(dc, state.portrait_key, Rect(27, 108, 96, 193));
  TextUtf8(dc, state.character_name, Rect(107, 112, 350, 160), g_font_title,
           kWhite);
  Text(dc, FactionText(state.faction), Rect(107, 157, 350, 193),
       g_font_label, kCyan);
  Text(dc, L"HEALTH", Rect(370, 111, 500, 145), g_font_label, kWhite);
  Segments(dc, Rect(370, 153, 660, 180), state.health, state.max_health, kCyan);
  Text(dc, L"ENERGON", Rect(700, 111, 830, 145), g_font_label, kWhite);
  Segments(dc, Rect(700, 153, 955, 180), state.resource,
           state.max_resource, kOrange);
  Text(dc, L"MODE", Rect(985, 111, 1070, 145), g_font_label, kWhite);
  Text(dc, ModeText(state.mode), Rect(985, 146, 1134, 192), g_font_mode,
       kCyan);
  DrawCachedAsset(dc, "OptimusPortrait", Rect(1145, 103, 1216, 198));
}

void DrawWeapon(HDC dc, RECT rect, const wchar_t* label,
                const WeaponState& weapon) {
  PanelBox(dc, rect);
  Text(dc, label, Rect(rect.left + 24, rect.top + 8, rect.right - 20,
                       rect.top + 46), g_font_label, kCyan);
  RECT art = Rect(rect.left + 24, rect.top + 55, rect.right - 24,
                  rect.bottom - 76);
  if (!weapon.available || !DrawCachedAsset(dc, weapon.icon_key, art))
    Text(dc, L"--", art, g_font_title, kCyanDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  TextUtf8(dc, weapon.name,
           Rect(rect.left + 24, rect.bottom - 72, rect.right - 150,
                rect.bottom - 23), g_font_title, kWhite);
  wchar_t ammo[64] = L"--";
  if (weapon.magazine >= 0 && weapon.reserve >= 0)
    std::swprintf(ammo, 64, L"%d / %d", weapon.magazine, weapon.reserve);
  Text(dc, ammo, Rect(rect.right - 145, rect.bottom - 72, rect.right - 20,
                      rect.bottom - 23), g_font_title, kWhite,
       DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
}

void DrawAbility(HDC dc, RECT rect, const wchar_t* label,
                 const AbilityState& ability, HitTarget target) {
  const bool active = Pressed(target) || Focused(target);
  const bool assigned = ability.object_index != 0;
  PanelBox(dc, rect, active);
  Text(dc, label, Rect(rect.left + 20, rect.top + 8, rect.right - 20,
                       rect.top + 44), g_font_label, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT icon = Rect(rect.left + 60, rect.top + 54, rect.right - 60,
                   rect.bottom - 100);
  Fill(dc, icon, kPanelAlt);
  Frame(dc, icon, assigned ? kCyan : kCyanDim, 2);
  if (!assigned || !DrawCachedAsset(dc, ability.icon_key, icon))
    Text(dc, L"--", icon, g_font_label,
         ability.available ? kCyan : kMuted,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  TextUtf8(dc, ability.name,
           Rect(rect.left + 12, rect.bottom - 94, rect.right - 12,
                rect.bottom - 53), g_font_title,
           assigned ? kWhite : kMuted,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  wchar_t status[96] = L"UNAVAILABLE";
  if (assigned && ability.cooldown_pending)
    std::swprintf(status, 96, L"NOT READY");
  else if (assigned && ability.cooldown_remaining > 0.01f)
    std::swprintf(status, 96, L"COOLDOWN  %.1f SEC",
                  ability.cooldown_remaining);
  else if (assigned && ability.resource_cost > 0.0f)
    std::swprintf(status, 96, L"ENERGY COST  %.0f", ability.resource_cost);
  else if (ability.available)
    std::swprintf(status, 96, L"READY");
  else if (assigned)
    std::swprintf(status, 96, L"NOT READY");
  Text(dc, status,
       Rect(rect.left + 15, rect.bottom - 51, rect.right - 15,
            rect.bottom - 12), g_font_body,
       ability.available ? kCyan : kMuted,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawAction(HDC dc, RECT rect, const wchar_t* label, const char* icon_key,
                HitTarget target, bool enabled, bool orange = false) {
  const bool active = Pressed(target) || Focused(target);
  if (orange) {
    Fill(dc, rect, active ? kTransformYellowPressed : kTransformYellow);
    Frame(dc, rect, active ? kWhite : kOrange, active ? 4 : 3);
  } else {
    PanelBox(dc, rect, active);
  }
  if (active && !orange) {
    RECT inset = rect;
    InflateRect(&inset, -4, -4);
    Fill(dc, inset, RGB(15, 39, 47));
    Frame(dc, rect, orange ? kOrange : kCyan, 4);
  }
  const COLORREF color = orange ? kBackground
                                : (enabled ? kCyan : kMuted);
  if (icon_key && *icon_key)
    DrawCachedAsset(dc, icon_key,
                    Rect(rect.left + 55, rect.top + 18, rect.right - 55,
                         rect.bottom - 72));
  Text(dc, label,
       Rect(rect.left + 8, rect.bottom - 72, rect.right - 8,
            rect.bottom - 14),
       orange ? g_font_transform : g_font_title, color,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawObjectivePanel(HDC dc, RECT rect, const Snapshot& state) {
  PanelBox(dc, rect);
  TextUtf8(dc, state.objective.chapter,
           Rect(rect.left + 20, rect.top + 10, rect.right - 20,
                rect.top + 45), g_font_label, kOrange,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  TextUtf8(dc, state.objective.mission,
           Rect(rect.left + 20, rect.top + 43, rect.right - 20,
                rect.top + 87), g_font_title, kWhite,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT image = Rect(rect.left + 1, rect.top + 92, rect.right - 1,
                     rect.bottom - 151);
  Fill(dc, image, RGB(3, 13, 20));
  if (!state.objective.image_key[0] ||
      !DrawCachedAssetCover(dc, state.objective.image_key, image))
    Text(dc, L"--", image, g_font_title, kCyanDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, L"CURRENT OBJECTIVE",
       Rect(rect.left + 18, rect.bottom - 145, rect.right - 18,
            rect.bottom - 104), g_font_label, kOrange);
  TextUtf8(dc, state.objective.text,
           Rect(rect.left + 20, rect.bottom - 100, rect.right - 20,
                rect.bottom - 18), g_font_body, kWhite,
           DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void DrawCombat(HDC dc, const Snapshot& state) {
  DrawWeapon(dc, Rect(14, 220, 475, 520), L"PRIMARY WEAPON",
             state.weapons[0]);
  DrawWeapon(dc, Rect(14, 532, 475, 832), L"SECONDARY WEAPON",
             state.weapons[1]);
  DrawAbility(dc, Rect(kAbility1Rect), L"ABILITY 1",
              state.abilities[0], HitTarget::kAbility1);
  DrawAbility(dc, Rect(kAbility2Rect), L"ABILITY 2",
              state.abilities[1], HitTarget::kAbility2);
  DrawObjectivePanel(dc, Rect(798, 220, 1226, 832), state);

  const bool grenade_enabled = (state.capabilities & kCapGrenadeAction) &&
                               state.grenade_count > 0;
  PanelBox(dc, Rect(kGrenadeRect),
           grenade_enabled &&
               (Pressed(HitTarget::kGrenade) || Focused(HitTarget::kGrenade)));
  DrawCachedAsset(dc, state.grenade_icon_key, Rect(55, 900, 192, 1005));
  const std::wstring grenade_name = Wide(state.grenade_name);
  wchar_t grenade_count[32] = L"--";
  if (state.grenade_count >= 0)
    std::swprintf(grenade_count, 32, L"%02d", state.grenade_count);
  Text(dc, grenade_name.empty() ? L"--" : grenade_name.c_str(),
       Rect(22, 858, 225, 900), g_font_label, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, grenade_count, Rect(22, 1009, 225, 1056), g_font_body, kWhite,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  const RECT objective_range = Rect(kObjectiveRangeRect);
  PanelBox(dc, objective_range);
  Text(dc, L"OBJECTIVE\nRANGE",
       Rect(objective_range.left + 6, objective_range.top + 10,
            objective_range.right - 6, objective_range.top + 68), g_font_label,
       kCyan, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
  DrawCachedAsset(dc, "ObjectiveMarker",
                  Rect(objective_range.left + 70, objective_range.top + 67,
                       objective_range.right - 70,
                       objective_range.bottom - 65));
  wchar_t distance[64] = L"--";
  if (state.objective.distance_meters >= 0.0f)
    std::swprintf(distance, 64, L"%.0f m", state.objective.distance_meters);
  Text(dc, distance,
       Rect(objective_range.left + 6, objective_range.bottom - 64,
            objective_range.right - 6, objective_range.bottom - 10),
       g_font_title, kWhite,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);

  DrawAction(dc, Rect(kTransformRect), L"TRANSFORM", "AutobotEmblem",
             HitTarget::kTransform,
             (state.capabilities & kCapTransformAction) != 0, true);
  DrawAction(dc, Rect(kReloadRect), L"RELOAD", "Reload",
             HitTarget::kReload,
             (state.capabilities & kCapReloadAction) != 0);
  DrawAction(dc, Rect(kSwapRect), L"SWAP", "Swap", HitTarget::kSwap,
             (state.capabilities & kCapSwapAction) != 0);
}

void DrawSquadCard(HDC dc, RECT rect, const SquadMemberState& member,
                   uint32_t slot) {
  const HitTarget target = static_cast<HitTarget>(
      static_cast<uint8_t>(HitTarget::kSquad0) + slot);
  PanelBox(dc, rect, member.controlled || Pressed(target) || Focused(target));
  TextUtf8(dc, member.name,
           Rect(rect.left + 18, rect.top + 15, rect.right - 18,
                rect.top + 63), g_font_title,
           member.controlled ? kOrange : kWhite,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, FactionText(member.faction),
       Rect(rect.left + 18, rect.top + 62, rect.right - 18, rect.top + 98),
       g_font_label, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT portrait = Rect(rect.left + 24, rect.top + 114, rect.right - 24,
                        rect.top + 410);
  Fill(dc, portrait, kPanelAlt);
  Frame(dc, portrait, member.controlled ? kOrange : kCyanDim, 2);
  if (!DrawCachedAsset(dc, member.portrait_key, portrait))
    Text(dc, L"--", portrait, g_font_label, kCyanDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, L"HEALTH", Rect(rect.left + 24, rect.top + 430,
                            rect.right - 24, rect.top + 464),
       g_font_label, kCyan);
  Segments(dc, Rect(rect.left + 24, rect.top + 470, rect.right - 24,
                    rect.top + 493), member.health, member.max_health, kCyan);
  TextUtf8(dc, member.weapon_name,
           Rect(rect.left + 24, rect.top + 515, rect.right - 24,
                rect.top + 558), g_font_body, kWhite,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, ModeText(member.mode),
       Rect(rect.left + 24, rect.top + 563, rect.right - 24,
            rect.top + 607), g_font_label, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  const wchar_t* status = member.controlled ? L"CURRENT" :
                          (!member.alive ? L"UNAVAILABLE" :
                           (member.downed ? L"DOWNED" :
                            (member.switchable ? L"TAP TO CONTROL" :
                             L"LIVE TEAMMATE")));
  Text(dc, status,
       Rect(rect.left + 24, rect.bottom - 70, rect.right - 24,
            rect.bottom - 18), g_font_label,
       member.controlled ? kOrange : (member.switchable ? kCyan : kMuted),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

void DrawSquad(HDC dc, const Snapshot& state) {
  Text(dc, (state.capabilities & kCapSquadSwitchAction)
               ? L"TAP A SQUAD MEMBER TO TAKE CONTROL"
               : L"CURRENT CAMPAIGN SQUAD",
       Rect(14, 218, 1226, 264), g_font_label, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  for (uint32_t slot = 0; slot < kSquadCapacity; ++slot) {
    RECT card = Rect(SquadCardRect(slot));
    if (slot < state.squad_count && state.squad[slot].occupied)
      DrawSquadCard(dc, card, state.squad[slot], slot);
    else {
      PanelBox(dc, card);
      Text(dc, L"NO ACTIVE MEMBER", card, g_font_title, kMuted,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
  }
}

void DrawObjectives(HDC dc, const Snapshot& state) {
  const RECT main = Rect(95, 260, 1145, 925);
  PanelBox(dc, main);
  TextUtf8(dc, state.objective.chapter, Rect(145, 300, 1095, 355),
           g_font_title, kOrange,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  TextUtf8(dc, state.objective.mission, Rect(145, 365, 1095, 430),
           g_font_action, kWhite,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT chapter_art = Rect(145, 445, 1095, 705);
  Fill(dc, chapter_art, RGB(3, 13, 20));
  if (!state.objective.image_key[0] ||
      !DrawCachedAssetCover(dc, state.objective.image_key, chapter_art))
    Text(dc, L"--", chapter_art, g_font_title, kCyanDim,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  Text(dc, L"CURRENT OBJECTIVE", Rect(170, 715, 1070, 760),
       g_font_title, kCyan,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  TextUtf8(dc, state.objective.text, Rect(180, 765, 1060, 835),
           g_font_body, kWhite, DT_CENTER | DT_TOP | DT_WORDBREAK);
  wchar_t distance[96] = L"RANGE  --";
  if (state.objective.distance_meters >= 0.0f)
    std::swprintf(distance, 96, L"RANGE  %.0f m",
                  state.objective.distance_meters);
  DrawCachedAsset(dc, "ObjectiveMarker", Rect(395, 844, 455, 904));
  Text(dc, distance, Rect(465, 844, 845, 904), g_font_title, kOrange,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

const wchar_t* LifecycleText(Lifecycle lifecycle) {
  switch (lifecycle) {
    case Lifecycle::kBoot: return L"STARTING DUAL-SCREEN SYSTEM";
    case Lifecycle::kWaitingForWorld: return L"WAITING FOR GAME";
    case Lifecycle::kLoading: return L"LOADING MISSION";
    case Lifecycle::kDead: return L"PLAYER DOWN";
    case Lifecycle::kUnsupportedBuild: return L"UNSUPPORTED GAME BUILD";
    case Lifecycle::kBridgeError: return L"GAME BRIDGE UNAVAILABLE";
    case Lifecycle::kLive: return L"";
  }
  return L"WAITING FOR GAME";
}

void Render() {
  if (!g_memory_dc) return;
  Fill(g_memory_dc, Rect(0, 0, kWidth, kHeight), kBackground);
  DrawNavigation(g_memory_dc);
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) {
    Text(g_memory_dc, L"WAITING FOR GAME", Rect(0, 100, kWidth, kHeight),
         g_font_action, kWhite, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  g_drawn_sequence = state.sequence;
  if (state.lifecycle != Lifecycle::kLive && state.lifecycle != Lifecycle::kDead) {
    Text(g_memory_dc, LifecycleText(state.lifecycle),
         Rect(0, 100, kWidth, kHeight), g_font_action,
         state.lifecycle == Lifecycle::kUnsupportedBuild ? kOrange : kWhite,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  DrawStatus(g_memory_dc, state);
  if (g_page == PanelPage::kCombat) DrawCombat(g_memory_dc, state);
  else if (g_page == PanelPage::kSquad) DrawSquad(g_memory_dc, state);
  else DrawObjectives(g_memory_dc, state);
}

void SetPage(PanelPage page) {
  g_page = page;
  if (page == PanelPage::kCombat)
    g_focus = HitTarget::kTransform;
  else if (page == PanelPage::kSquad)
    g_focus = HitTarget::kSquad0;
  else
    g_focus = HitTarget::kNone;
  Render();
  InvalidateRect(g_window, nullptr, FALSE);
}

void ChangePage(int delta) {
  int page = static_cast<int>(g_page) + delta;
  if (page < 0) page = 2;
  if (page > 2) page = 0;
  SetPage(static_cast<PanelPage>(page));
}

bool QueueAction(HitTarget target, const Snapshot& state) {
  Command command;
  command.world_epoch = state.world_epoch;
  command.queued_at_millis = GetTickCount64();
  command.expected_controller_object_index =
      state.local_controller_object_index;
  command.expected_player_pawn_object_index = state.player_pawn_object_index;
  switch (target) {
    case HitTarget::kTransform:
      if (!(state.capabilities & kCapTransformAction)) return false;
      command.type = CommandType::kTransform;
      command.expected_target_object_index = state.player_pawn_object_index;
      break;
    case HitTarget::kReload:
      if (!(state.capabilities & kCapReloadAction)) return false;
      command.type = CommandType::kReload;
      command.expected_target_object_index = state.active_weapon_object_index;
      break;
    case HitTarget::kSwap:
      if (!(state.capabilities & kCapSwapAction)) return false;
      command.type = CommandType::kSwapWeapon;
      command.expected_target_object_index = state.active_weapon_object_index;
      break;
    case HitTarget::kAbility1:
      if (!(state.capabilities & kCapAbilityActions) ||
          !state.abilities[0].available)
        return false;
      command.type = CommandType::kAbility1;
      command.expected_target_object_index = state.abilities[0].object_index;
      break;
    case HitTarget::kAbility2:
      if (!(state.capabilities & kCapAbilityActions) ||
          !state.abilities[1].available)
        return false;
      command.type = CommandType::kAbility2;
      command.expected_target_object_index = state.abilities[1].object_index;
      break;
    case HitTarget::kGrenade:
      if (!(state.capabilities & kCapGrenadeAction) ||
          state.grenade_count <= 0)
        return false;
      command.type = CommandType::kGrenade;
      command.expected_target_object_index = state.grenade_object_index;
      break;
    case HitTarget::kSquad0:
    case HitTarget::kSquad1:
    case HitTarget::kSquad2: {
      const uint32_t slot = static_cast<uint32_t>(target) -
                            static_cast<uint32_t>(HitTarget::kSquad0);
      if (slot >= state.squad_count || !state.squad[slot].switchable ||
          state.squad[slot].controlled)
        return false;
      command.type = CommandType::kSwitchSquadMember;
      command.expected_target_object_index =
          state.squad[slot].pawn_object_index;
      command.squad_slot = slot;
      command.expected_pawn_object_index =
          state.squad[slot].pawn_object_index;
      command.expected_identity_hash = state.squad[slot].identity_hash;
      break;
    }
    default: return false;
  }
  return QueueCommand(command);
}

bool QueueTargetWithFeedback(HitTarget target, const Snapshot& state) {
  const ULONGLONG now = GetTickCount64();
  const size_t target_index = static_cast<size_t>(target);
  if (target_index >= g_last_action_by_target.size() ||
      (g_last_action_by_target[target_index] &&
       now - g_last_action_by_target[target_index] < 650))
    return false;
  if (!QueueAction(target, state)) return false;
  g_last_action_by_target[target_index] = now;
  g_pressed = target;
  g_pressed_until = now + 140;
  Render();
  InvalidateRect(g_window, nullptr, FALSE);
  return true;
}

void ChangeFocus(int delta) {
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) return;
  constexpr HitTarget combat_targets[] = {
      HitTarget::kAbility1, HitTarget::kAbility2, HitTarget::kGrenade,
      HitTarget::kTransform, HitTarget::kReload, HitTarget::kSwap};
  HitTarget targets[kSquadCapacity] = {};
  const HitTarget* begin = nullptr;
  size_t count = 0;
  if (g_page == PanelPage::kCombat) {
    begin = combat_targets;
    count = sizeof(combat_targets) / sizeof(combat_targets[0]);
  } else if (g_page == PanelPage::kSquad) {
    count = std::min<uint32_t>(state.squad_count, kSquadCapacity);
    for (size_t slot = 0; slot < count; ++slot)
      targets[slot] = static_cast<HitTarget>(
          static_cast<uint8_t>(HitTarget::kSquad0) + slot);
    begin = targets;
  }
  if (!begin || !count) {
    g_focus = HitTarget::kNone;
    return;
  }
  size_t current = 0;
  while (current < count && begin[current] != g_focus) ++current;
  if (current == count) current = delta >= 0 ? count - 1 : 0;
  const int64_t next = (static_cast<int64_t>(current) + delta +
                        static_cast<int64_t>(count)) %
                       static_cast<int64_t>(count);
  g_focus = begin[next];
  Render();
  InvalidateRect(g_window, nullptr, FALSE);
}

void ActivateFocus() {
  if (g_focus == HitTarget::kNone) return;
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) return;
  QueueTargetWithFeedback(g_focus, state);
}

void HandleTap(float x, float y) {
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) return;
  const HitTarget target = HitTestPanel(g_page, x, y, state.squad_count);
  if (target == HitTarget::kNone) return;
  switch (target) {
    case HitTarget::kPreviousPage:
      g_pressed = target; g_pressed_until = GetTickCount64() + 140;
      ChangePage(-1); return;
    case HitTarget::kNextPage:
      g_pressed = target; g_pressed_until = GetTickCount64() + 140;
      ChangePage(1); return;
    case HitTarget::kCombatPage:
      g_pressed = target; g_pressed_until = GetTickCount64() + 140;
      SetPage(PanelPage::kCombat); return;
    case HitTarget::kSquadPage:
      g_pressed = target; g_pressed_until = GetTickCount64() + 140;
      SetPage(PanelPage::kSquad); return;
    case HitTarget::kObjectivesPage:
      g_pressed = target; g_pressed_until = GetTickCount64() + 140;
      SetPage(PanelPage::kObjectives); return;
    default: break;
  }
  QueueTargetWithFeedback(target, state);
}

void HandleInput(const PanelInput& input) {
  if (input.kind == PanelInputKind::kTap)
    HandleTap(input.x, input.y);
  else if (input.kind == PanelInputKind::kPageNext ||
           input.kind == PanelInputKind::kNavRight)
    ChangePage(1);
  else if (input.kind == PanelInputKind::kPagePrevious ||
           input.kind == PanelInputKind::kNavLeft)
    ChangePage(-1);
  else if (input.kind == PanelInputKind::kNavUp)
    ChangeFocus(-1);
  else if (input.kind == PanelInputKind::kNavDown)
    ChangeFocus(1);
  else if (input.kind == PanelInputKind::kNavActivate)
    ActivateFocus();
}

void PollSocket() {
  if (g_socket == INVALID_SOCKET) return;
  for (;;) {
    char bytes[256] = {};
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    const int received = recvfrom(g_socket, bytes, sizeof(bytes), 0,
                                  reinterpret_cast<sockaddr*>(&sender),
                                  &sender_size);
    if (received == SOCKET_ERROR) {
      if (WSAGetLastError() != WSAEWOULDBLOCK)
        LogRateLimited("touch-recv", 5000, LogLevel::kWarning,
                       "[Touch] UDP receive failed error=%d", WSAGetLastError());
      return;
    }
    PanelInput input;
    if (ParsePanelInput(bytes, static_cast<size_t>(received), &input))
      HandleInput(input);
  }
}

bool OpenSocket() {
  WSADATA data = {};
  if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
  g_winsock = true;
  g_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_socket == INVALID_SOCKET) return false;
  u_long nonblocking = 1;
  ioctlsocket(g_socket, FIONBIO, &nonblocking);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_port = htons(kInputPort);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (bind(g_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
      SOCKET_ERROR) {
    closesocket(g_socket);
    g_socket = INVALID_SOCKET;
    return false;
  }
  return true;
}

LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wparam,
                                 LPARAM lparam) {
  switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_NCHITTEST: return HTCLIENT;
    case WM_LBUTTONUP:
      HandleTap(static_cast<float>(GET_X_LPARAM(lparam)) / kWidth,
                static_cast<float>(GET_Y_LPARAM(lparam)) / kHeight);
      return 0;
    case WM_TIMER:
      PollSocket();
      {
        Snapshot state;
        const ULONGLONG now = GetTickCount64();
        const bool feedback_active = g_pressed != HitTarget::kNone;
        if (feedback_active && now >= g_pressed_until) g_pressed = HitTarget::kNone;
        if ((CopyLatestSnapshot(&state) && state.sequence != g_drawn_sequence) ||
            feedback_active) {
          Render();
          InvalidateRect(window, nullptr, FALSE);
        }
      }
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(window, &paint);
      BitBlt(dc, 0, 0, kWidth, kHeight, g_memory_dc, 0, 0, SRCCOPY);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_CLOSE:
      ShowWindow(window, SW_HIDE);
      return 0;
    case kShutdownMessage:
      DestroyWindow(window);
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default: return DefWindowProcW(window, message, wparam, lparam);
  }
}

HFONT Font(int height, int weight) {
  return CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, FF_DONTCARE, L"Bahnschrift Condensed");
}

DWORD WINAPI PanelThread(void*) {
  WNDCLASSEXW cls = {};
  cls.cbSize = sizeof(cls);
  cls.lpfnWndProc = WindowProcedure;
  cls.hInstance = g_module;
  cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  cls.hbrBackground = nullptr;
  cls.lpszClassName = L"TransformersWfcSecondScreenPanel";
  if (!RegisterClassExW(&cls)) return 1;
  g_window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE |
                                 WS_EX_TOPMOST,
                             cls.lpszClassName,
                             L"Transformers: War for Cybertron Dual-Screen",
                             WS_POPUP, 0, 1080, kWidth, kHeight, nullptr,
                             nullptr, g_module, nullptr);
  if (!g_window) return 2;
  HDC window_dc = GetDC(g_window);
  g_memory_dc = CreateCompatibleDC(window_dc);
  g_bitmap = CreateCompatibleBitmap(window_dc, kWidth, kHeight);
  g_old_bitmap = SelectObject(g_memory_dc, g_bitmap);
  ReleaseDC(g_window, window_dc);
  g_font_label = Font(25, FW_SEMIBOLD);
  g_font_body = Font(28, FW_NORMAL);
  g_font_title = Font(35, FW_BOLD);
  g_font_action = Font(50, FW_BOLD);
  g_font_mode = Font(21, FW_SEMIBOLD);
  g_font_transform = Font(40, FW_BOLD);
  if (!InitializeAssetCache(g_module))
    Log(LogLevel::kWarning,
        "[DualScreen] GDI+ asset cache unavailable; text fallbacks active");
  if (!OpenSocket())
    Log(LogLevel::kWarning,
        "[Touch] UDP listener unavailable port=%u", kInputPort);
  SetTimer(g_window, kRenderTimer, 33, nullptr);
  Render();
  ShowWindow(g_window, SW_SHOWNOACTIVATE);
  SetWindowPos(g_window, HWND_TOPMOST, 0, 1080, kWidth, kHeight,
               SWP_NOACTIVATE | SWP_SHOWWINDOW);
  Log(LogLevel::kInfo,
      "[DualScreen] panel ready class=TransformersWfcSecondScreenPanel size=1240x1080 port=%u",
      kInputPort);
  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  KillTimer(g_window, kRenderTimer);
  if (g_socket != INVALID_SOCKET) closesocket(g_socket);
  if (g_winsock) WSACleanup();
  if (g_memory_dc && g_old_bitmap) SelectObject(g_memory_dc, g_old_bitmap);
  if (g_bitmap) DeleteObject(g_bitmap);
  if (g_memory_dc) DeleteDC(g_memory_dc);
  if (g_font_label) DeleteObject(g_font_label);
  if (g_font_body) DeleteObject(g_font_body);
  if (g_font_title) DeleteObject(g_font_title);
  if (g_font_action) DeleteObject(g_font_action);
  if (g_font_mode) DeleteObject(g_font_mode);
  if (g_font_transform) DeleteObject(g_font_transform);
  ShutdownAssetCache();
  g_window = nullptr;
  UnregisterClassW(cls.lpszClassName, g_module);
  return 0;
}

}  // namespace

bool StartPanel(HMODULE module) {
  if (g_thread || !module) return g_thread != nullptr;
  g_module = module;
  g_thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0, &g_thread_id);
  return g_thread != nullptr;
}

void StopPanel() {
  if (g_window) PostMessageW(g_window, kShutdownMessage, 0, 0);
  if (g_thread) {
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
  }
  g_thread = nullptr;
  g_thread_id = 0;
}

}  // namespace wfcds
