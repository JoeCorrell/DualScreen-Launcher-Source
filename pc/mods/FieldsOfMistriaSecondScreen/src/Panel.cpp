#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "Panel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <iterator>
#include <memory>
#include <string>

#include "ActionTransport.h"
#include "ExpectedBuild.h"
#include "InstalledAssetProvider.h"
#include "InstalledAssetRenderer.h"
#include "LiveState.h"
#include "Log.h"
#include "PageCarousel.h"
#include "PanelInput.h"
#include "PanelLifecycle.h"
#include "StateStore.h"

namespace fomds {
namespace {

constexpr UINT kStopMessage = WM_APP + 0x314;
constexpr UINT kAssetReadyMessage = WM_APP + 0x315;
constexpr int kPageCount = 7;

HMODULE g_module = nullptr;
Config g_config;
BuildIdentity g_verified_identity;
HANDLE g_thread = nullptr;
HANDLE g_started_event = nullptr;
std::atomic<bool> g_start_success{false};
std::atomic<bool> g_stop_requested{false};
std::atomic<HWND> g_window{nullptr};
SOCKET g_input = INVALID_SOCKET;
PanelGestureArbiter g_gestures;
int g_page = 0;
int g_previous_page = -1;
int g_page_animation_direction = 0;
ULONGLONG g_page_animation_start = 0;
int g_width = 0;
int g_height = 0;
std::uint64_t g_drawn_sequence = 0;
bool g_page_dirty = true;
int g_selected[kPageCount] = {};
int g_scroll[kPageCount] = {};
int g_swap_source = -1;
int g_armor_selected = 0;
bool g_armor_focus = false;
double g_map_center_x = 0.0;
double g_map_center_y = 0.0;
double g_map_zoom = 1.0;
bool g_map_centered = false;
int g_map_page_selection = -1;
std::unique_ptr<LiveStateIngestor> g_live_ingestor;
std::unique_ptr<ActionTransport> g_action_transport;
LiveStateSnapshot g_live_state = {};
bool g_have_live_state = false;
bool g_action_transport_ready = false;
ULONGLONG g_last_bridge_poll = 0;
ULONGLONG g_last_live_publish = 0;
ULONGLONG g_last_map_animation = 0;
bool g_live_state_stale = false;
std::wstring g_action_feedback;
SRWLOCK g_asset_lock = SRWLOCK_INIT;
std::unique_ptr<InstalledAssetProvider> g_asset_provider;
std::unique_ptr<InstalledAssetRenderer> g_asset_renderer;
HANDLE g_asset_thread = nullptr;
std::atomic<bool> g_asset_stop{false};
std::atomic<bool> g_asset_ready{false};

// GDI objects are owned by the panel thread and retained across frames. The
// old implementation rebuilt a full-screen bitmap, DC, fonts, and brushes for
// every WM_PAINT; that becomes prohibitively allocation-heavy once live lists
// and animation are present.
HDC g_backbuffer_dc = nullptr;
HBITMAP g_backbuffer_bitmap = nullptr;
HGDIOBJ g_backbuffer_previous = nullptr;
int g_backbuffer_width = 0;
int g_backbuffer_height = 0;

struct FontEntry {
  int size = 0;
  int weight = 0;
  HFONT font = nullptr;
};
FontEntry g_fonts[16] = {};

struct BrushEntry {
  COLORREF color = 0;
  HBRUSH brush = nullptr;
};
BrushEntry g_brushes[16] = {};


struct MonitorChoice {
  RECT bounds = {};
  RECT primary_bounds = {};
  bool found = false;
  bool primary_found = false;
  std::uint32_t count = 0;
};

class SlowOperation final {
 public:
  SlowOperation(const char* key, const char* label, ULONGLONG threshold_ms)
      : key_(key), label_(label), threshold_ms_(threshold_ms),
        start_(GetTickCount64()) {}
  ~SlowOperation() {
    const ULONGLONG elapsed = GetTickCount64() - start_;
    if (elapsed >= threshold_ms_) {
      LogRateLimited(key_, 5000, LogLevel::kWarning,
                     "[PERF] %s took %llu ms", label_,
                     static_cast<unsigned long long>(elapsed));
    }
  }

 private:
  const char* key_;
  const char* label_;
  ULONGLONG threshold_ms_;
  ULONGLONG start_;
};

DWORD WINAPI AssetInitializationThread(void*) {
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
  const ULONGLONG started = GetTickCount64();
  InstalledAssetProviderConfig provider_config;
  provider_config.max_cached_items = 48;
  provider_config.max_cached_bytes = 32 * 1024 * 1024;
  auto provider = std::make_unique<InstalledAssetProvider>(provider_config);
  if (g_asset_stop.load(std::memory_order_acquire)) return 0;
  if (!provider->OpenAdjacentToModule(g_module)) {
    Log(LogLevel::kWarning,
        "[ASSET] verified installed-archive provider unavailable: %s",
        provider->LastError().c_str());
    return 0;
  }
  InstalledAssetRendererConfig renderer_config;
  renderer_config.max_cached_items = 48;
  renderer_config.max_cached_bytes = 64 * 1024 * 1024;
  auto renderer =
      std::make_unique<InstalledAssetRenderer>(*provider, renderer_config);
  const InstalledAssetRenderResult initialized = renderer->Initialize();
  if (initialized != InstalledAssetRenderResult::kOk) {
    Log(LogLevel::kWarning,
        "[ASSET] installed renderer initialization failed code=%u error=%s",
        static_cast<unsigned>(initialized), renderer->LastError().c_str());
    return 0;
  }
  constexpr InstalledAssetKey images[] = {
      InstalledAssetKey::kJournalBackplate,
      InstalledAssetKey::kInventoryLeftPage,
      InstalledAssetKey::kInfoHudTop,
      InstalledAssetKey::kMapGrid,
      InstalledAssetKey::kPlayerMapMarker,
      InstalledAssetKey::kRelationshipHeader,
      InstalledAssetKey::kAnimalHeader,
      InstalledAssetKey::kMagicHeader,
      InstalledAssetKey::kAlmanacHeader,
      InstalledAssetKey::kMuseumOn,
  };
  for (InstalledAssetKey key : images) {
    if (g_asset_stop.load(std::memory_order_acquire)) {
      renderer->Shutdown();
      return 0;
    }
    const InstalledAssetRenderResult result = renderer->PrefetchImage(key);
    if (result != InstalledAssetRenderResult::kOk) {
      LogRateLimited("asset-prefetch", 5000, LogLevel::kWarning,
                     "[ASSET] image prefetch failed key=%u code=%u error=%s",
                     static_cast<unsigned>(key),
                     static_cast<unsigned>(result),
                     renderer->LastError().c_str());
    }
  }
  const InstalledAssetRenderResult font_result =
      renderer->PrefetchFont(InstalledAssetKey::kBirdseedFont);
  if (font_result != InstalledAssetRenderResult::kOk) {
    Log(LogLevel::kWarning,
        "[ASSET] installed font prefetch failed code=%u error=%s",
        static_cast<unsigned>(font_result), renderer->LastError().c_str());
  }
  if (g_asset_stop.load(std::memory_order_acquire)) {
    renderer->Shutdown();
    return 0;
  }
  AcquireSRWLockExclusive(&g_asset_lock);
  g_asset_provider = std::move(provider);
  g_asset_renderer = std::move(renderer);
  g_asset_ready.store(true, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_asset_lock);
  const ULONGLONG elapsed = GetTickCount64() - started;
  const InstalledAssetRendererStats stats = g_asset_renderer->Stats();
  Log(LogLevel::kInfo,
      "[ASSET] verified installed UI ready in %llu ms images=%zu fonts=%zu bytes=%zu",
      static_cast<unsigned long long>(elapsed), stats.cached_images,
      stats.cached_fonts, stats.cached_bytes);
  HWND window = g_window.load(std::memory_order_acquire);
  if (window) PostMessageW(window, kAssetReadyMessage, 0, 0);
  return 0;
}

bool StartAssetInitialization() {
  if (g_asset_thread) return true;
  g_asset_stop.store(false, std::memory_order_release);
  g_asset_ready.store(false, std::memory_order_release);
  g_asset_thread =
      CreateThread(nullptr, 0, AssetInitializationThread, nullptr, 0, nullptr);
  if (!g_asset_thread) {
    Log(LogLevel::kWarning,
        "[ASSET] background initialization thread failed error=%lu; using fallback UI",
        static_cast<unsigned long>(GetLastError()));
    return false;
  }
  return true;
}

void StopAssetInitialization() {
  g_asset_stop.store(true, std::memory_order_release);
  if (g_asset_thread) {
    const DWORD wait = WaitForSingleObject(g_asset_thread, 30000);
    if (wait == WAIT_OBJECT_0) {
      CloseHandle(g_asset_thread);
      g_asset_thread = nullptr;
    } else {
      Log(LogLevel::kWarning,
          "[ASSET] initialization worker did not stop within 30 seconds; resources retained");
      return;
    }
  }
  AcquireSRWLockExclusive(&g_asset_lock);
  g_asset_ready.store(false, std::memory_order_release);
  if (g_asset_renderer) g_asset_renderer->Shutdown();
  g_asset_renderer.reset();
  g_asset_provider.reset();
  ReleaseSRWLockExclusive(&g_asset_lock);
}

BOOL CALLBACK FindSecondary(HMONITOR monitor, HDC, LPRECT, LPARAM parameter) {
  auto* choice = reinterpret_cast<MonitorChoice*>(parameter);
  MONITORINFO information = {};
  information.cbSize = sizeof(information);
  if (!choice || !GetMonitorInfoW(monitor, &information)) return TRUE;
  ++choice->count;
  if (information.dwFlags & MONITORINFOF_PRIMARY) {
    choice->primary_bounds = information.rcMonitor;
    choice->primary_found = true;
    return TRUE;
  }
  const long long area =
      static_cast<long long>(information.rcMonitor.right -
                             information.rcMonitor.left) *
      (information.rcMonitor.bottom - information.rcMonitor.top);
  const long long selected_area =
      static_cast<long long>(choice->bounds.right - choice->bounds.left) *
      (choice->bounds.bottom - choice->bounds.top);
  if (!choice->found || area > selected_area) {
    choice->bounds = information.rcMonitor;
    choice->found = true;
  }
  return TRUE;
}

HBRUSH Brush(COLORREF color) {
  for (auto& entry : g_brushes) {
    if (entry.brush && entry.color == color) return entry.brush;
  }
  for (auto& entry : g_brushes) {
    if (entry.brush) continue;
    entry.color = color;
    entry.brush = CreateSolidBrush(color);
    return entry.brush;
  }
  return nullptr;
}

HFONT Font(int size, int weight) {
  for (auto& entry : g_fonts) {
    if (entry.font && entry.size == size && entry.weight == weight)
      return entry.font;
  }
  for (auto& entry : g_fonts) {
    if (entry.font) continue;
    entry.size = size;
    entry.weight = weight;
    entry.font = CreateFontW(
        -size, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY,
        FIXED_PITCH | FF_MODERN, L"Consolas");
    return entry.font;
  }
  return nullptr;
}

void Fill(HDC dc, const RECT& bounds, COLORREF color) {
  HBRUSH brush = Brush(color);
  if (brush) FillRect(dc, &bounds, brush);
}

void Text(HDC dc, const RECT& bounds, const wchar_t* text, int size,
          int weight, COLORREF color, UINT flags = DT_LEFT | DT_TOP) {
  if (!(flags & DT_VCENTER) && g_asset_ready.load(std::memory_order_acquire)) {
    InstalledAssetTextStyle style;
    style.pixel_size = static_cast<float>(size);
    style.argb = 0xff000000U |
        (static_cast<std::uint32_t>(GetRValue(color)) << 16U) |
        (static_cast<std::uint32_t>(GetGValue(color)) << 8U) |
        static_cast<std::uint32_t>(GetBValue(color));
    style.bold = weight >= FW_BOLD;
    style.wrap = (flags & DT_WORDBREAK) != 0;
    style.ellipsis = (flags & DT_END_ELLIPSIS) != 0;
    style.alignment = (flags & DT_CENTER)
        ? InstalledTextAlignment::kCenter
        : ((flags & DT_RIGHT) ? InstalledTextAlignment::kRight
                              : InstalledTextAlignment::kLeft);
    AcquireSRWLockShared(&g_asset_lock);
    const InstalledAssetRenderResult rendered = g_asset_renderer
        ? g_asset_renderer->DrawText(dc, InstalledAssetKey::kBirdseedFont,
                                     text ? text : L"", bounds, style)
        : InstalledAssetRenderResult::kNotInitialized;
    ReleaseSRWLockShared(&g_asset_lock);
    if (rendered == InstalledAssetRenderResult::kOk) return;
  }
  HFONT font = Font(size, weight);
  if (!font) return;
  HGDIOBJ previous = SelectObject(dc, font);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, color);
  RECT copy = bounds;
  DrawTextW(dc, text ? text : L"", -1, &copy, flags);
  SelectObject(dc, previous);
}

bool AssetFrame(HDC dc, InstalledAssetKey key, const RECT& bounds,
                std::uint32_t frame = 0, std::uint8_t opacity = 255) {
  if (!g_asset_ready.load(std::memory_order_acquire)) return false;
  InstalledAssetFrameStyle style;
  style.opacity = opacity;
  style.nearest_neighbor = true;
  AcquireSRWLockShared(&g_asset_lock);
  const InstalledAssetRenderResult result = g_asset_renderer
      ? g_asset_renderer->DrawFrame(dc, key, frame, bounds, style)
      : InstalledAssetRenderResult::kNotInitialized;
  ReleaseSRWLockShared(&g_asset_lock);
  if (result != InstalledAssetRenderResult::kOk) {
    LogRateLimited("asset-draw", 5000, LogLevel::kWarning,
                   "[ASSET] installed asset draw failed key=%u code=%u",
                   static_cast<unsigned>(key),
                   static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool DynamicFrame(HDC dc, InstalledDynamicAssetClass asset_class,
                  const char* logical_id, const RECT& bounds,
                  std::uint32_t frame = 0, std::uint8_t opacity = 255) {
  if (!logical_id || !*logical_id ||
      !g_asset_ready.load(std::memory_order_acquire)) return false;
  InstalledDynamicAnimationRef source;
  source.asset_class = asset_class;
  source.logical_id = logical_id;
  InstalledAssetFrameStyle style;
  style.opacity = opacity;
  style.nearest_neighbor = true;
  AcquireSRWLockShared(&g_asset_lock);
  const InstalledAssetRenderResult result = g_asset_renderer
      ? g_asset_renderer->DrawDynamicFrame(dc, source, frame, bounds, style)
      : InstalledAssetRenderResult::kNotInitialized;
  ReleaseSRWLockShared(&g_asset_lock);
  if (result != InstalledAssetRenderResult::kOk) {
    LogRateLimited("asset-dynamic-draw", 5000, LogLevel::kWarning,
                   "[ASSET] dynamic draw failed class=%u id=%s code=%u",
                   static_cast<unsigned>(asset_class), logical_id,
                   static_cast<unsigned>(result));
    return false;
  }
  return true;
}

bool EnsureBackbuffer(HDC target, int width, int height) {
  if (g_backbuffer_dc && g_backbuffer_bitmap &&
      g_backbuffer_width == width && g_backbuffer_height == height) {
    return true;
  }
  if (g_backbuffer_dc && g_backbuffer_previous)
    SelectObject(g_backbuffer_dc, g_backbuffer_previous);
  if (g_backbuffer_bitmap) DeleteObject(g_backbuffer_bitmap);
  if (g_backbuffer_dc) DeleteDC(g_backbuffer_dc);
  g_backbuffer_dc = CreateCompatibleDC(target);
  g_backbuffer_bitmap =
      g_backbuffer_dc ? CreateCompatibleBitmap(target, width, height) : nullptr;
  if (!g_backbuffer_dc || !g_backbuffer_bitmap) {
    if (g_backbuffer_bitmap) DeleteObject(g_backbuffer_bitmap);
    if (g_backbuffer_dc) DeleteDC(g_backbuffer_dc);
    g_backbuffer_dc = nullptr;
    g_backbuffer_bitmap = nullptr;
    g_backbuffer_previous = nullptr;
    g_backbuffer_width = 0;
    g_backbuffer_height = 0;
    return false;
  }
  g_backbuffer_previous = SelectObject(g_backbuffer_dc, g_backbuffer_bitmap);
  g_backbuffer_width = width;
  g_backbuffer_height = height;
  return true;
}

void ReleaseDrawingResources() {
  if (g_backbuffer_dc && g_backbuffer_previous)
    SelectObject(g_backbuffer_dc, g_backbuffer_previous);
  if (g_backbuffer_bitmap) DeleteObject(g_backbuffer_bitmap);
  if (g_backbuffer_dc) DeleteDC(g_backbuffer_dc);
  g_backbuffer_dc = nullptr;
  g_backbuffer_bitmap = nullptr;
  g_backbuffer_previous = nullptr;
  g_backbuffer_width = 0;
  g_backbuffer_height = 0;
  for (auto& entry : g_fonts) {
    if (entry.font) DeleteObject(entry.font);
    entry = {};
  }
  for (auto& entry : g_brushes) {
    if (entry.brush) DeleteObject(entry.brush);
    entry = {};
  }
}

std::wstring Wide(const char* value) {
  if (!value || !*value) return {};
  const int needed = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);
  if (needed <= 1) return {};
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), needed);
  result.pop_back();
  return result;
}

std::uint64_t TopicRevision(const LiveStateSnapshot& state, LiveTopic topic) {
  return state.revisions[static_cast<std::size_t>(topic)];
}

std::wstring CompactNumber(double value) {
  wchar_t output[48] = {};
  if (std::isfinite(value) && std::fabs(value - std::round(value)) < 0.001) {
    std::swprintf(output, std::size(output), L"%.0f", value);
  } else if (std::isfinite(value)) {
    std::swprintf(output, std::size(output), L"%.1f", value);
  } else {
    std::swprintf(output, std::size(output), L"--");
  }
  return output;
}

void DrawMeter(HDC dc, const RECT& bounds, const wchar_t* label,
               double current, double maximum, COLORREF fill) {
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF track = RGB(63, 67, 57);
  RECT text = bounds;
  text.right = bounds.left + std::max(42, g_width / 26);
  Text(dc, text, label, std::max(12, g_height / 58), FW_BOLD, cream,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
  RECT bar = bounds;
  bar.left = text.right;
  bar.top += std::max(4, static_cast<int>((bounds.bottom - bounds.top) / 3));
  bar.bottom -=
      std::max(4, static_cast<int>((bounds.bottom - bounds.top) / 3));
  Fill(dc, bar, track);
  if (maximum > 0.0 && std::isfinite(current) && std::isfinite(maximum)) {
    const double ratio = std::clamp(current / maximum, 0.0, 1.0);
    RECT active = bar;
    active.right = active.left + static_cast<LONG>(
        static_cast<double>(bar.right - bar.left) * ratio);
    Fill(dc, active, fill);
  }
}

void DrawLiveChrome(HDC dc, const LiveStateSnapshot& state) {
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF ink = RGB(43, 48, 43);
  const COLORREF paper = RGB(224, 204, 157);
  const COLORREF gold = RGB(213, 151, 63);
  const wchar_t* labels[kPageCount] = {
      L"INVENTORY", L"MAP", L"QUESTS", L"VILLAGERS", L"ANIMALS",
      L"SKILLS + MAGIC", L"ALMANAC"};
  const int margin = std::max(8, g_width / 80);
  const int tabs_top = std::max(5, g_height / 80);
  const int tabs_height = std::max(34, g_height / 18);
  const int tab_width = (g_width - margin * 2) / kPageCount;
  for (int index = 0; index < kPageCount; ++index) {
    RECT tab = {margin + tab_width * index, tabs_top,
                margin + tab_width * (index + 1) - 2,
                tabs_top + tabs_height};
    Fill(dc, tab, index == g_page ? paper : ink);
    Text(dc, tab, labels[index], std::max(11, g_height / 62), FW_BOLD,
         index == g_page ? RGB(55, 49, 36) : cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }

  const int strip_top = tabs_top + tabs_height + std::max(5, g_height / 100);
  const int strip_bottom = g_height * 21 / 100;
  RECT strip = {margin, strip_top, g_width - margin, strip_bottom};
  if (!AssetFrame(dc, InstalledAssetKey::kInfoHudTop, strip))
    Fill(dc, strip, RGB(50, 57, 50));
  RECT identity = {strip.left + margin, strip.top,
                   strip.left + (strip.right - strip.left) * 33 / 100,
                   strip.bottom};
  wchar_t identity_text[512] = {};
  std::swprintf(identity_text, std::size(identity_text), L"%ls  |  %ls %lld  |  %ls\n%ls  |  %ls",
                Wide(state.status.name.value).c_str(),
                Wide(state.calendar.season.value).c_str(),
                static_cast<long long>(state.calendar.display_day),
                Wide(state.clock.formatted.value).c_str(),
                Wide(state.weather.current.value).c_str(),
                Wide(state.location.display_name.value).c_str());
  Text(dc, identity, identity_text, std::max(12, g_height / 55), FW_BOLD,
       cream, DT_LEFT | DT_VCENTER | DT_WORDBREAK);

  const int meter_left = strip.left + (strip.right - strip.left) * 35 / 100;
  const int meter_width = (strip.right - meter_left) * 20 / 100;
  RECT hp = {meter_left, strip.top + 2, meter_left + meter_width,
             strip.top + (strip.bottom - strip.top) / 3};
  RECT stamina = hp;
  OffsetRect(&stamina, 0, (strip.bottom - strip.top) / 3);
  RECT mana = stamina;
  OffsetRect(&mana, 0, (strip.bottom - strip.top) / 3);
  DrawMeter(dc, hp, L"HP", state.status.health, state.status.health_max,
            RGB(190, 91, 79));
  DrawMeter(dc, stamina, L"ST", state.status.stamina,
            state.status.stamina_max, RGB(103, 176, 102));
  DrawMeter(dc, mana, L"MP", state.status.mana, state.status.mana_max,
            RGB(91, 132, 192));

  RECT currency = {meter_left + meter_width + margin, strip.top,
                   strip.right - margin, strip.bottom};
  wchar_t currency_text[256] = {};
  std::swprintf(currency_text, std::size(currency_text),
                L"TESSERAE  %ls     ESSENCE  %ls     RENOWN  %ls  %ls",
                CompactNumber(state.status.tesserae).c_str(),
                CompactNumber(state.status.essence).c_str(),
                CompactNumber(state.status.renown).c_str(),
                Wide(state.renown.rank.display_name.value).c_str());
  Text(dc, currency, currency_text, std::max(13, g_height / 50), FW_BOLD,
       gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

RECT LiveContentBounds() {
  const int margin = std::max(12, g_width / 55);
  return {margin, g_height * 23 / 100, g_width - margin,
          g_height * 94 / 100};
}

void DrawInventoryPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  const COLORREF selected = RGB(76, 91, 73);
  const int split = content.left + (content.right - content.left) * 58 / 100;
  RECT list = content;
  list.right = split;
  RECT detail = content;
  detail.left = split + std::max(10, g_width / 80);
  if (!AssetFrame(dc, InstalledAssetKey::kInventoryLeftPage, list))
    Fill(dc, list, RGB(39, 49, 45));
  Fill(dc, detail, RGB(45, 53, 46));

  const int row_height =
      std::max(42, static_cast<int>((list.bottom - list.top) / 8));
  const int visible =
      std::max(1, static_cast<int>((list.bottom - list.top) / row_height));
  const int count = static_cast<int>(state.inventory.slot_count);
  g_selected[0] = std::clamp(g_selected[0], 0, std::max(0, count - 1));
  if (g_selected[0] < g_scroll[0]) g_scroll[0] = g_selected[0];
  if (g_selected[0] >= g_scroll[0] + visible)
    g_scroll[0] = g_selected[0] - visible + 1;
  g_scroll[0] = std::clamp(g_scroll[0], 0, std::max(0, count - visible));

  for (int row = 0; row < visible; ++row) {
    const int index = g_scroll[0] + row;
    if (index >= count) break;
    const ItemSlotState& slot = state.inventory.slots[index];
    RECT row_box = {list.left + 3, list.top + row * row_height,
                    list.right - 3,
                    std::min(list.bottom, list.top + (row + 1) * row_height - 2)};
    if (index == g_selected[0]) Fill(dc, row_box, selected);
    RECT index_box = row_box;
    index_box.right = row_box.left + std::max(40, g_width / 28);
    wchar_t slot_number[24] = {};
    std::swprintf(slot_number, std::size(slot_number), L"%02lld",
                  static_cast<long long>(slot.index + 1));
    Text(dc, index_box, slot_number, std::max(13, g_height / 52), FW_BOLD,
         index == state.hotbar.selected_index ? gold : RGB(174, 161, 129),
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    RECT icon_box = {index_box.right + 2, row_box.top + 3,
                     index_box.right + row_height - 5, row_box.bottom - 3};
    if (!slot.empty && slot.icon_asset.present)
      DynamicFrame(dc, InstalledDynamicAssetClass::kItemIcon,
                   slot.icon_asset.text.value, icon_box,
                   static_cast<std::uint32_t>(std::max<std::int64_t>(
                       0, slot.icon_frame)));
    RECT name_box = row_box;
    name_box.left = icon_box.right + 5;
    name_box.right -= std::max(58, g_width / 22);
    const std::wstring live_name = slot.empty ? std::wstring(L"-- empty --")
                                               : Wide(slot.display_name.value);
    Text(dc, name_box, live_name.c_str(), std::max(14, g_height / 48),
         index == g_selected[0] ? FW_BOLD : FW_NORMAL,
         slot.empty ? RGB(126, 124, 104) : cream,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (!slot.empty) {
      RECT quantity = row_box;
      quantity.left = name_box.right;
      wchar_t amount[32] = {};
      std::swprintf(amount, std::size(amount), L"x%ls",
                    CompactNumber(slot.count).c_str());
      Text(dc, quantity, amount, std::max(13, g_height / 52), FW_BOLD, cream,
           DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    }
  }

  if (count <= 0) {
    Text(dc, list, L"No inventory slots in the validated snapshot.",
         std::max(15, g_height / 44), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    return;
  }
  const ItemSlotState& chosen = state.inventory.slots[g_selected[0]];
  RECT detail_title = detail;
  detail_title.bottom = detail.top + std::max(52, g_height / 11);
  RECT detail_icon = {detail.right - std::max(48, g_height / 10),
                      detail_title.top + 3, detail.right - 3,
                      detail_title.bottom - 3};
  if (!chosen.empty && chosen.icon_asset.present)
    DynamicFrame(dc, InstalledDynamicAssetClass::kItemIcon,
                 chosen.icon_asset.text.value, detail_icon,
                 static_cast<std::uint32_t>(std::max<std::int64_t>(
                     0, chosen.icon_frame)));
  detail_title.right = detail_icon.left - 4;
  const std::wstring chosen_name =
      chosen.empty ? std::wstring(L"EMPTY SLOT") : Wide(chosen.display_name.value);
  Text(dc, detail_title, chosen_name.c_str(), std::max(19, g_height / 35),
       FW_BOLD, gold, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  const int action_height = std::max(50, g_height / 14);
  const int armor_height = std::max(66, g_height / 10);
  const int stats_height = std::max(78, g_height / 9);
  RECT description = detail;
  description.top = detail_title.bottom;
  description.bottom = detail.bottom - action_height - armor_height -
                       stats_height - 6;
  const std::wstring description_text =
      chosen.empty ? std::wstring(L"Select a populated slot to inspect it.")
                   : Wide(chosen.description.value);
  Text(dc, description, description_text.c_str(),
       std::max(14, g_height / 48), FW_NORMAL, cream,
       DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  RECT stats = detail;
  stats.top = description.bottom;
  stats.bottom = stats.top + stats_height;
  std::wstring equipped;
  for (std::uint32_t index = 0; index < state.armor.slot_count; ++index) {
    const ItemSlotState& armor = state.armor.slots[index];
    if (armor.empty) continue;
    if (!equipped.empty()) equipped += L", ";
    equipped += Wide(armor.display_name.value);
  }
  if (equipped.empty()) equipped = L"NONE";
  wchar_t stats_text[1024] = {};
  std::swprintf(stats_text, std::size(stats_text),
                L"COUNT %ls   DAMAGE %ls   DEFENSE %ls\n"
                L"SELL %ls   STORE %ls   QUALITY %ls\n"
                L"MUSEUM %ls   EQUIPPED  %ls",
                CompactNumber(chosen.count).c_str(),
                chosen.damage.present ? CompactNumber(chosen.damage.value).c_str() : L"--",
                chosen.defense.present ? CompactNumber(chosen.defense.value).c_str() : L"--",
                chosen.value_bin.present ? CompactNumber(chosen.value_bin.value).c_str() : L"--",
                chosen.value_store.present ? CompactNumber(chosen.value_store.value).c_str() : L"--",
                chosen.quality.present ? CompactNumber(chosen.quality.value).c_str() : L"--",
                chosen.museum_eligible
                    ? (chosen.museum_donated ? L"DONATED" : L"NOT DONATED")
                    : L"--",
                equipped.c_str());
  Text(dc, stats, stats_text, std::max(13, g_height / 52), FW_BOLD, cream,
       DT_LEFT | DT_VCENTER | DT_WORDBREAK);
  RECT armor_area = detail;
  armor_area.top = stats.bottom + 3;
  armor_area.bottom = detail.bottom - action_height - 3;
  Fill(dc, armor_area, RGB(51, 62, 54));
  const int armor_count = static_cast<int>(state.armor.slot_count);
  g_armor_selected = std::clamp(g_armor_selected, 0,
                                std::max(0, armor_count - 1));
  if (armor_count > 0) {
    const int armor_width = std::max(
        1, static_cast<int>((armor_area.right - armor_area.left) /
                            armor_count));
    for (int index = 0; index < armor_count; ++index) {
      const ItemSlotState& armor = state.armor.slots[index];
      RECT slot_box = {armor_area.left + index * armor_width + 2,
                       armor_area.top + 2,
                       index + 1 == armor_count
                           ? armor_area.right - 2
                           : armor_area.left + (index + 1) * armor_width - 2,
                       armor_area.bottom - 2};
      if (g_armor_focus && index == g_armor_selected)
        Fill(dc, slot_box, RGB(98, 91, 60));
      const std::wstring label = armor.empty
          ? (armor.required_tag_count
                 ? Wide(armor.required_tags[0].value)
                 : std::wstring(L"EMPTY"))
          : Wide(armor.display_name.value);
      Text(dc, slot_box, label.c_str(), std::max(10, g_height / 68),
           g_armor_focus && index == g_armor_selected ? FW_BOLD : FW_NORMAL,
           armor.empty ? RGB(154, 146, 121) : cream,
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  }
  RECT action = detail;
  action.top = detail.bottom - action_height;
  const bool consumable = !chosen.empty && chosen.can_consume_from_bridge;
  const bool equipment = !chosen.empty &&
      std::strcmp(chosen.ui_category.value, "equipment") == 0;
  const wchar_t* action_text = g_armor_focus
      ? L"ACTIVATE: UNEQUIP SELECTED SLOT TO FIRST EMPTY BAG SLOT"
      : (g_swap_source >= 0
          ? L"ACTIVATE: SWAP WITH MARKED SLOT"
          : (consumable
              ? L"ACTIVATE: CONSUME THROUGH VANILLA PLAYER STATE"
              : (equipment
                  ? L"ACTIVATE: EQUIP TO COMPATIBLE ARMOR SLOT"
                  : L"ACTIVATE: SELECT REAL HOTBAR SLOT   |   HOLD: MARK FOR SWAP")));
  Fill(dc, action, chosen.empty && !g_armor_focus
                       ? RGB(62, 64, 57) : RGB(116, 92, 49));
  Text(dc, action, action_text,
       std::max(12, g_height / 56), FW_BOLD,
       chosen.empty && !g_armor_focus ? RGB(151, 143, 119) : cream,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

bool MapAssetKey(const char* logical_id, InstalledAssetKey* output) {
  if (!logical_id || !output) return false;
  struct Mapping {
    const char* id;
    InstalledAssetKey key;
  };
  constexpr Mapping mappings[] = {
      {"spr_ui_map_beach", InstalledAssetKey::kBeachMap},
      {"spr_ui_map_deep_woods", InstalledAssetKey::kDeepWoodsMap},
      {"spr_ui_map_dragonsworn_glade", InstalledAssetKey::kDragonswornGladeMap},
      {"spr_ui_map_eastern_road", InstalledAssetKey::kEasternRoadMap},
      {"spr_ui_map_farm", InstalledAssetKey::kFarmMap},
      {"spr_ui_map_farm_expansion", InstalledAssetKey::kFarmExpansionMap},
      {"spr_ui_map_haydens_farm", InstalledAssetKey::kHaydensFarmMap},
      {"spr_ui_map_narrows", InstalledAssetKey::kNarrowsMap},
      {"spr_ui_map_summit", InstalledAssetKey::kSummitMap},
      {"spr_ui_map_town", InstalledAssetKey::kTownMap},
      {"spr_ui_map_western_ruins", InstalledAssetKey::kWesternRuinsMap},
  };
  for (const Mapping& mapping : mappings) {
    if (std::strcmp(logical_id, mapping.id) == 0) {
      *output = mapping.key;
      return true;
    }
  }
  return false;
}

void DrawMapPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  Fill(dc, content, RGB(42, 53, 48));
  const int page_count = static_cast<int>(state.map.page_count);
  if (page_count <= 0 || !state.map.authored_coordinates) {
    Text(dc, content,
         L"WAITING FOR THE VERIFIED AUTHORED MAP TOPIC.\n\nWorld coordinates are never estimated onto journal art.",
         std::max(17, g_height / 39), FW_BOLD, cream,
         DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    return;
  }
  if (g_map_page_selection < 0 || g_map_page_selection >= page_count) {
    g_map_page_selection = 0;
    for (int index = 0; index < page_count; ++index) {
      if (std::strcmp(state.map.pages[index].id.value,
                      state.map.selected_map_id.value) == 0) {
        g_map_page_selection = index;
        break;
      }
    }
    g_map_centered = false;
  }
  const MapPageState& page = state.map.pages[g_map_page_selection];
  RECT title = content;
  title.bottom = content.top + std::max(42, g_height / 15);
  std::wstring location = Wide(page.display_name.value);
  location += L"  |  " + Wide(state.location.display_name.value);
  if (state.player.mine.active) {
    location += L"  |  MINE";
    if (state.player.mine.display_floor.present)
      location += L" FLOOR " +
                  std::to_wstring(state.player.mine.display_floor.value);
    if (state.player.mine.biome_name.present)
      location += L"  " + Wide(state.player.mine.biome_name.text.value);
  }
  Text(dc, title, location.c_str(), std::max(22, g_height / 31), FW_BOLD,
       gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  RECT tabs = content;
  tabs.top = title.bottom;
  tabs.bottom = tabs.top + std::max(32, g_height / 20);
  const int tab_width = std::max(
      1, static_cast<int>((tabs.right - tabs.left) / page_count));
  for (int index = 0; index < page_count; ++index) {
    RECT tab = {tabs.left + index * tab_width, tabs.top,
                index + 1 == page_count ? tabs.right
                                        : tabs.left + (index + 1) * tab_width - 2,
                tabs.bottom};
    Fill(dc, tab, index == g_map_page_selection ? RGB(106, 91, 58)
                                                 : RGB(51, 61, 52));
    const std::wstring label = Wide(state.map.pages[index].display_name.value);
    Text(dc, tab, label.c_str(), std::max(10, g_height / 68),
         index == g_map_page_selection ? FW_BOLD : FW_NORMAL,
         state.map.pages[index].locked ? RGB(125, 121, 105) : cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  RECT room = content;
  room.top = tabs.bottom;
  room.bottom = room.top + std::max(28, g_height / 23);
  const std::wstring room_name = Wide(state.location.diagnostic_name.value);
  wchar_t room_text[512] = {};
  std::swprintf(room_text, std::size(room_text), L"%ls   |   X %.1f  Y %.1f  |  %ls (%.0f deg)  |  %.1fx",
                room_name.c_str(), state.player.x, state.player.y,
                Wide(state.player.facing.value).c_str(),
                state.player.facing_degrees, g_map_zoom);
  Text(dc, room, room_text, std::max(14, g_height / 47), FW_NORMAL,
       cream, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

  RECT map = content;
  map.top = room.bottom + std::max(4, g_height / 120);
  map.bottom -= std::max(32, g_height / 20);
  if (!AssetFrame(dc, InstalledAssetKey::kMapGrid, map, 0, 210))
    Fill(dc, map, RGB(34, 47, 43));
  if (!g_map_centered) {
    g_map_center_x = 339.0 / 2.0;
    g_map_center_y = 179.0 / 2.0;
    g_map_centered = true;
  }
  const int image_width = static_cast<int>((map.right - map.left) * g_map_zoom);
  const int image_height = static_cast<int>(
      image_width * (179.0 / 339.0));
  RECT image = {
      (map.left + map.right) / 2 -
          static_cast<LONG>((g_map_center_x / 339.0) * image_width),
      (map.top + map.bottom) / 2 -
          static_cast<LONG>((g_map_center_y / 179.0) * image_height),
      0, 0};
  image.right = image.left + image_width;
  image.bottom = image.top + image_height;
  InstalledAssetKey map_key = InstalledAssetKey::kMapGrid;
  bool map_drawn = false;
  const int saved = SaveDC(dc);
  IntersectClipRect(dc, map.left, map.top, map.right, map.bottom);
  if (!page.locked && MapAssetKey(page.map_asset.value, &map_key))
    map_drawn = AssetFrame(dc, map_key, image);
  RestoreDC(dc, saved);
  if (!map_drawn && page.locked) {
    Text(dc, map, L"LOCKED", std::max(28, g_height / 22), FW_BOLD,
         RGB(132, 125, 105), DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
  auto project = [&](double x, double y, POINT* point) {
    if (!point || !std::isfinite(x) || !std::isfinite(y)) return false;
    point->x = image.left + static_cast<LONG>((x / 339.0) * image_width);
    point->y = image.top + static_cast<LONG>((y / 179.0) * image_height);
    return point->x >= map.left && point->x <= map.right &&
           point->y >= map.top && point->y <= map.bottom;
  };
  auto villager_name = [&](const OptionalText<96>& npc_id) {
    if (!npc_id.present) return std::wstring(L"?");
    for (std::uint32_t index = 0; index < state.villagers.villager_count;
         ++index) {
      if (std::strcmp(npc_id.text.value,
                      state.villagers.villagers[index].id.value) == 0)
        return Wide(state.villagers.villagers[index].display_name.value);
    }
    return std::wstring(L"NPC");
  };
  for (std::uint32_t relative = 0; relative < page.hub_count; ++relative) {
    const std::uint32_t hub_index = page.first_hub + relative;
    if (hub_index >= state.map.hub_count) break;
    const MapHubState& hub = state.map.hubs[hub_index];
    for (std::uint32_t marker_offset = 0;
         marker_offset < hub.marker_count; ++marker_offset) {
      const std::uint32_t marker_index = hub.first_marker + marker_offset;
      if (marker_index >= state.map.marker_count) break;
      const MapMarkerState& marker = state.map.markers[marker_index];
      POINT point = {};
      if (!project(hub.map_x + marker.offset_x,
                   hub.map_y + marker.offset_y, &point)) continue;
      const bool player = std::strcmp(marker.kind.value, "player") == 0;
      RECT marker_box = {point.x - (player ? 8 : 5),
                         point.y - (player ? 8 : 5),
                         point.x + (player ? 9 : 6),
                         point.y + (player ? 9 : 6)};
      const std::uint32_t player_frame = static_cast<std::uint32_t>(
          (GetTickCount64() / 180ULL) % 9ULL);
      if (player && !AssetFrame(dc, InstalledAssetKey::kPlayerMapMarker,
                                marker_box, player_frame)) {
        Fill(dc, marker_box, gold);
      } else if (!player) {
        if (!DynamicFrame(dc, InstalledDynamicAssetClass::kNpcMapIcon,
                          marker.icon_asset.value, marker_box)) {
          Fill(dc, marker_box, marker.npc_id.present
                                   ? RGB(123, 187, 173)
                                   : RGB(142, 137, 118));
        }
        RECT label = {point.x + 8, point.y - 10, point.x + 170,
                      point.y + 16};
        const std::wstring name = villager_name(marker.npc_id);
        Text(dc, label, name.c_str(), std::max(10, g_height / 68), FW_BOLD,
             cream, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
      }
    }
  }
  RECT help = content;
  help.top = map.bottom;
  const wchar_t* context =
      L"AUTHORED JOURNAL HUBS + LIVE MARKERS   |   PINCH / DRAG TO ZOOM OR PAN   |   TAP TO RECENTER";
  Text(dc, help, context, std::max(12, g_height / 56), FW_BOLD, cream,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void DrawVillagersPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  const int split = content.left + (content.right - content.left) * 46 / 100;
  RECT list = content;
  list.right = split;
  RECT detail = content;
  detail.left = split + std::max(10, g_width / 80);
  Fill(dc, list, RGB(39, 49, 45));
  Fill(dc, detail, RGB(45, 53, 46));
  const int count = static_cast<int>(state.villagers.villager_count);
  const int row_height =
      std::max(48, static_cast<int>((list.bottom - list.top) / 7));
  const int visible =
      std::max(1, static_cast<int>((list.bottom - list.top) / row_height));
  g_selected[3] = std::clamp(g_selected[3], 0, std::max(0, count - 1));
  if (g_selected[3] < g_scroll[3]) g_scroll[3] = g_selected[3];
  if (g_selected[3] >= g_scroll[3] + visible)
    g_scroll[3] = g_selected[3] - visible + 1;
  g_scroll[3] = std::clamp(g_scroll[3], 0, std::max(0, count - visible));
  for (int row = 0; row < visible; ++row) {
    const int index = g_scroll[3] + row;
    if (index >= count) break;
    const VillagerState& villager = state.villagers.villagers[index];
    RECT box = {list.left + 3, list.top + row * row_height, list.right - 3,
                std::min(list.bottom, list.top + (row + 1) * row_height - 2)};
    if (index == g_selected[3]) Fill(dc, box, RGB(76, 91, 73));
    wchar_t line[320] = {};
    std::swprintf(line, std::size(line), L"%ls   %lld HEARTS%ls%ls",
                  Wide(villager.display_name.value).c_str(),
                  static_cast<long long>(villager.heart_level),
                  villager.talked_today ? L"   TALKED" : L"",
                  villager.gifted_today ? L"   GIFTED" : L"");
    Text(dc, box, line, std::max(14, g_height / 48),
         index == g_selected[3] ? FW_BOLD : FW_NORMAL,
         index == g_selected[3] ? gold : cream,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (count <= 0) {
    Text(dc, content, L"No met and unlocked villagers in the validated snapshot.",
         std::max(16, g_height / 42), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_WORDBREAK);
    return;
  }
  const VillagerState& chosen = state.villagers.villagers[g_selected[3]];
  RECT name_box = detail;
  name_box.bottom = detail.top + std::max(65, g_height / 9);
  RECT relationship_icon = {name_box.left + 5, name_box.top + 5,
                            name_box.left + std::max(38, g_height / 14),
                            name_box.bottom - 5};
  AssetFrame(dc, InstalledAssetKey::kRelationshipHeader,
             relationship_icon);
  RECT portrait = {name_box.right - std::max(62, g_height / 8),
                   name_box.top + 3, name_box.right - 3,
                   name_box.bottom - 3};
  if (chosen.portrait_asset.present)
    DynamicFrame(dc, InstalledDynamicAssetClass::kNpcPortrait,
                 chosen.portrait_asset.text.value, portrait);
  name_box.right = portrait.left - 3;
  const std::wstring name = Wide(chosen.display_name.value);
  Text(dc, name_box, name.c_str(), std::max(23, g_height / 30), FW_BOLD,
       gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT facts = detail;
  facts.top = name_box.bottom;
  wchar_t fact_text[1024] = {};
  const std::wstring relationship = Wide(chosen.relationship.value);
  const std::wstring location = chosen.location.present
      ? Wide(chosen.location.id.value) : std::wstring(L"LOCATION UNAVAILABLE");
  std::swprintf(fact_text, std::size(fact_text),
                L"RELATIONSHIP  %ls\nHEART LEVEL  %lld\nHEART POINTS  %ls\n\nTALKED TODAY  %ls\nGIFTED TODAY  %ls\n\nCURRENT AREA  %ls\nPOSITION  %ls",
                relationship.c_str(), static_cast<long long>(chosen.heart_level),
                CompactNumber(chosen.heart_points).c_str(),
                chosen.talked_today ? L"YES" : L"NO",
                chosen.gifted_today ? L"YES" : L"NO", location.c_str(),
                chosen.location.x.present && chosen.location.y.present
                    ? (L"X " + CompactNumber(chosen.location.x.value) +
                       L"  Y " + CompactNumber(chosen.location.y.value)).c_str()
                    : L"NOT CURRENTLY INSTANTIATED");
  Text(dc, facts, fact_text, std::max(16, g_height / 42), FW_NORMAL, cream,
       DT_LEFT | DT_TOP | DT_WORDBREAK);
  if (state.calendar_events.event_count) {
    RECT upcoming = detail;
    upcoming.top = detail.bottom - std::max(128, g_height / 5);
    Fill(dc, upcoming, RGB(54, 64, 54));
    std::wstring events = L"UPCOMING\n";
    const std::uint32_t count =
        std::min<std::uint32_t>(3, state.calendar_events.event_count);
    for (std::uint32_t index = 0; index < count; ++index) {
      const CalendarEventState& event = state.calendar_events.events[index];
      events += event.days_from_now == 0
          ? L"TODAY  "
          : L"IN " + std::to_wstring(event.days_from_now) + L" DAYS  ";
      events += Wide(event.display_name.value);
      events += L"\n";
    }
    Text(dc, upcoming, events.c_str(), std::max(13, g_height / 52), FW_BOLD,
         gold, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  }
}

void DrawAnimalsPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  const int count = static_cast<int>(state.animals.livestock_count) +
                    (state.animals.pet.present ? 1 : 0);
  const int split = content.left + (content.right - content.left) * 48 / 100;
  RECT list = content;
  list.right = split;
  RECT detail = content;
  detail.left = split + std::max(10, g_width / 80);
  Fill(dc, list, RGB(39, 49, 45));
  Fill(dc, detail, RGB(45, 53, 46));
  const int row_height =
      std::max(48, static_cast<int>((list.bottom - list.top) / 7));
  const int visible =
      std::max(1, static_cast<int>((list.bottom - list.top) / row_height));
  g_selected[4] = std::clamp(g_selected[4], 0, std::max(0, count - 1));
  if (g_selected[4] < g_scroll[4]) g_scroll[4] = g_selected[4];
  if (g_selected[4] >= g_scroll[4] + visible)
    g_scroll[4] = g_selected[4] - visible + 1;
  g_scroll[4] = std::clamp(g_scroll[4], 0, std::max(0, count - visible));
  for (int row = 0; row < visible; ++row) {
    const int index = g_scroll[4] + row;
    if (index >= count) break;
    const bool pet_row = index >= static_cast<int>(state.animals.livestock_count);
    const std::wstring name = pet_row ? Wide(state.animals.pet.name.value)
                                      : Wide(state.animals.livestock[index].name.value);
    const bool needs_attention = pet_row ? state.animals.pet.attention.needed
        : state.animals.livestock[index].attention.needed;
    RECT box = {list.left + 3, list.top + row * row_height, list.right - 3,
                std::min(list.bottom, list.top + (row + 1) * row_height - 2)};
    if (index == g_selected[4]) Fill(dc, box, RGB(76, 91, 73));
    wchar_t line[320] = {};
    std::swprintf(line, std::size(line), L"%ls%ls", name.c_str(),
                  needs_attention ? L"   NEEDS ATTENTION" : L"");
    Text(dc, box, line, std::max(14, g_height / 48),
         index == g_selected[4] ? FW_BOLD : FW_NORMAL,
         needs_attention ? RGB(232, 153, 92) : cream,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (count <= 0) {
    Text(dc, content, L"No owned animals in the validated snapshot.",
         std::max(16, g_height / 42), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const int index = g_selected[4];
  const bool pet_row = index >= static_cast<int>(state.animals.livestock_count);
  std::wstring name;
  std::wstring kind;
  std::wstring variant;
  std::wstring sex;
  std::wstring location;
  std::int64_t hearts = 0;
  std::int64_t days = 0;
  bool baby = false;
  bool daycare = false;
  bool outside = false;
  bool incubating = false;
  AnimalAttention attention;
  if (pet_row) {
    name = Wide(state.animals.pet.name.value);
    kind = L"PET";
    variant = Wide(state.animals.pet.variant.value);
    sex = Wide(state.animals.pet.sex.value);
    hearts = state.animals.pet.heart_level;
    location = state.animals.pet.location.present
        ? Wide(state.animals.pet.location.id.value) : L"LOCATION UNAVAILABLE";
    attention = state.animals.pet.attention;
  } else {
    const LivestockState& animal = state.animals.livestock[index];
    name = Wide(animal.name.value);
    kind = Wide(animal.kind_name.value);
    variant = Wide(animal.variant.value);
    sex = Wide(animal.sex.value);
    hearts = animal.heart_level;
    days = animal.days_old;
    baby = animal.baby;
    daycare = animal.in_daycare;
    outside = animal.outside_today;
    incubating = animal.incubating;
    location = animal.location.present
        ? Wide(animal.location.id.value) : L"LOCATION UNAVAILABLE";
    attention = animal.attention;
  }
  RECT name_box = detail;
  name_box.bottom = detail.top + std::max(65, g_height / 9);
  RECT animal_icon = {name_box.left + 5, name_box.top + 5,
                      name_box.left + std::max(38, g_height / 14),
                      name_box.bottom - 5};
  AssetFrame(dc, InstalledAssetKey::kAnimalHeader, animal_icon);
  const char* live_animal_icon = pet_row
      ? (state.animals.pet.icon_asset.present
             ? state.animals.pet.icon_asset.text.value : "")
      : (state.animals.livestock[index].icon_asset.present
             ? state.animals.livestock[index].icon_asset.text.value : "");
  RECT animal_preview = {name_box.right - std::max(58, g_height / 9),
                         name_box.top + 3, name_box.right - 3,
                         name_box.bottom - 3};
  DynamicFrame(dc, InstalledDynamicAssetClass::kAnimalIcon,
               live_animal_icon, animal_preview);
  name_box.right = animal_preview.left - 3;
  Text(dc, name_box, name.c_str(), std::max(23, g_height / 30), FW_BOLD,
       gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT facts = detail;
  facts.top = name_box.bottom;
  wchar_t fact_text[1024] = {};
  std::swprintf(fact_text, std::size(fact_text),
                L"TYPE  %ls\nVARIANT  %ls\nSEX  %ls\nHEARTS  %lld\nAGE  %ls\n\nFED TODAY  %ls\nPETTED TODAY  %ls\nOUTSIDE  %ls\nDAYCARE  %ls\nINCUBATING  %ls\n\nCURRENT AREA  %ls",
                kind.c_str(), variant.c_str(), sex.c_str(),
                static_cast<long long>(hearts),
                pet_row ? L"--" : (baby ? L"BABY" : CompactNumber(days).c_str()),
                attention.fed_today.present
                    ? (attention.fed_today.value ? L"YES" : L"NO") : L"N/A",
                attention.petted_today.present
                    ? (attention.petted_today.value ? L"YES" : L"NO") : L"N/A",
                outside ? L"YES" : L"NO", daycare ? L"YES" : L"NO",
                incubating ? L"YES" : L"NO", location.c_str());
  Text(dc, facts, fact_text, std::max(15, g_height / 44), FW_NORMAL, cream,
       DT_LEFT | DT_TOP | DT_WORDBREAK);
}

void DrawQuestPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  const int split = content.left + (content.right - content.left) * 45 / 100;
  RECT list = content;
  list.right = split;
  RECT detail = content;
  detail.left = split + std::max(10, g_width / 80);
  Fill(dc, list, RGB(39, 49, 45));
  Fill(dc, detail, RGB(45, 53, 46));
  const int count = static_cast<int>(state.quests.quest_count);
  const int row_height =
      std::max(48, static_cast<int>((list.bottom - list.top) / 7));
  const int visible =
      std::max(1, static_cast<int>((list.bottom - list.top) / row_height));
  g_selected[2] = std::clamp(g_selected[2], 0, std::max(0, count - 1));
  if (g_selected[2] < g_scroll[2]) g_scroll[2] = g_selected[2];
  if (g_selected[2] >= g_scroll[2] + visible)
    g_scroll[2] = g_selected[2] - visible + 1;
  g_scroll[2] = std::clamp(g_scroll[2], 0, std::max(0, count - visible));
  for (int row = 0; row < visible; ++row) {
    const int index = g_scroll[2] + row;
    if (index >= count) break;
    RECT box = {list.left + 3, list.top + row * row_height, list.right - 3,
                std::min(list.bottom, list.top + (row + 1) * row_height - 2)};
    if (index == g_selected[2]) Fill(dc, box, RGB(76, 91, 73));
    const std::wstring name = Wide(state.quests.active[index].name.value);
    Text(dc, box, name.c_str(), std::max(14, g_height / 48),
         index == g_selected[2] ? FW_BOLD : FW_NORMAL,
         state.quests.active[index].ready ? gold : cream,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (count <= 0) {
    Text(dc, list, L"No active quests.", std::max(16, g_height / 42),
         FW_NORMAL, cream, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const QuestState& quest = state.quests.active[g_selected[2]];
  RECT title = detail;
  title.bottom = detail.top + std::max(58, g_height / 10);
  const std::wstring name = Wide(quest.name.value);
  Text(dc, title, name.c_str(), std::max(20, g_height / 34), FW_BOLD, gold,
       DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  RECT objective = detail;
  objective.top = title.bottom;
  objective.bottom = objective.top + std::max(76, g_height / 7);
  const std::wstring objective_text = Wide(quest.objective.value);
  Text(dc, objective, objective_text.c_str(), std::max(16, g_height / 42),
       FW_BOLD, quest.ready ? RGB(132, 205, 137) : cream,
       DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
  RECT description = detail;
  description.top = objective.bottom + 5;
  description.bottom = description.top +
      std::max<LONG>(58, (detail.bottom - description.top) * 27 / 100);
  const std::wstring description_text = Wide(quest.description.value);
  Text(dc, description, description_text.c_str(),
       std::max(14, g_height / 48), FW_NORMAL, cream,
       DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);

  RECT evidence = detail;
  evidence.top = description.bottom + 5;
  std::wstring evidence_text = L"REQUIREMENTS";
  const std::uint32_t requirement_end = std::min<std::uint32_t>(
      state.quests.requirement_count,
      quest.first_requirement + quest.requirement_count);
  std::uint32_t shown_requirements = 0;
  for (std::uint32_t index = quest.first_requirement;
       index < requirement_end && shown_requirements < 5;
       ++index, ++shown_requirements) {
    const QuestRequirementState& requirement =
        state.quests.requirements[index];
    evidence_text += L"\n";
    evidence_text += requirement.complete.present && requirement.complete.value
        ? L"[DONE] " : L"[ ] ";
    if (requirement.display_name.present) {
      evidence_text += Wide(requirement.display_name.text.value);
    } else if (requirement.item_id.present) {
      evidence_text += Wide(requirement.item_id.text.value);
    } else if (requirement.tag.present) {
      evidence_text += Wide(requirement.tag.text.value);
    } else {
      evidence_text += Wide(requirement.type.value);
    }
    if (requirement.current.present || requirement.required.present) {
      evidence_text += L"  ";
      evidence_text += requirement.current.present
          ? CompactNumber(requirement.current.value) : L"--";
      evidence_text += L" / ";
      evidence_text += requirement.required.present
          ? CompactNumber(requirement.required.value) : L"--";
    }
  }
  if (!quest.requirement_count) evidence_text += L"\nNo item requirements.";
  evidence_text += L"\n\nREWARDS";
  const std::uint32_t reward_end = std::min<std::uint32_t>(
      state.quests.reward_count, quest.first_reward + quest.reward_count);
  std::uint32_t shown_rewards = 0;
  for (std::uint32_t index = quest.first_reward;
       index < reward_end && shown_rewards < 4; ++index, ++shown_rewards) {
    const QuestRewardState& reward = state.quests.rewards[index];
    evidence_text += L"\n";
    evidence_text += reward.display_name.present
        ? Wide(reward.display_name.text.value) : Wide(reward.type.value);
    if (reward.amount > 0.0) {
      evidence_text += L"  x";
      evidence_text += CompactNumber(reward.amount);
    }
  }
  if (!quest.reward_count) evidence_text += L"\nNo published rewards.";
  Text(dc, evidence, evidence_text.c_str(), std::max(12, g_height / 56),
       FW_BOLD, cream, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
}

void DrawSkillsMagicPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  Fill(dc, content, RGB(39, 49, 45));
  const int skill_count = static_cast<int>(state.progression.skill_count);
  const int spell_count = static_cast<int>(state.magic.spell_count);
  const int count = skill_count + spell_count;
  const int split = content.left + (content.right - content.left) * 57 / 100;
  RECT skill_area = content;
  skill_area.right = split;
  RECT skill_grid = skill_area;
  skill_grid.bottom = skill_area.top +
      (skill_area.bottom - skill_area.top) * 72 / 100;
  RECT progression_detail = skill_area;
  progression_detail.top = skill_grid.bottom + 4;
  RECT magic_area = content;
  magic_area.left = split + std::max(8, g_width / 100);
  Fill(dc, magic_area, RGB(45, 53, 46));
  const int columns = 3;
  const int rows = std::max(1, (skill_count + columns - 1) / columns);
  const int cell_width = (skill_grid.right - skill_grid.left) / columns;
  const int cell_height = std::max(
      52, static_cast<int>((skill_grid.bottom - skill_grid.top) /
                           std::max(3, rows)));
  g_selected[5] = std::clamp(g_selected[5], 0, std::max(0, count - 1));
  for (int index = 0; index < skill_count; ++index) {
    const int row = index / columns;
    const int column = index % columns;
    RECT cell = {skill_grid.left + column * cell_width + 3,
                 skill_grid.top + row * cell_height + 3,
                 skill_grid.left + (column + 1) * cell_width - 3,
                 std::min(skill_grid.bottom,
                          skill_grid.top + (row + 1) * cell_height - 3)};
    Fill(dc, cell, index == g_selected[5] ? RGB(76, 91, 73)
                                          : RGB(47, 58, 51));
    RECT skill_icon = {cell.left + 4, cell.top + 4,
                       cell.left + std::max(30, g_height / 18),
                       cell.top + std::max(30, g_height / 18)};
    DynamicFrame(dc, InstalledDynamicAssetClass::kSkillIcon,
                 state.progression.skills[index].icon_asset.value,
                 skill_icon);
    wchar_t label[256] = {};
    std::swprintf(label, std::size(label), L"%ls\nLEVEL %lld   XP %ls",
                  Wide(state.progression.skills[index].id.value).c_str(),
                  static_cast<long long>(state.progression.skills[index].level),
                  CompactNumber(state.progression.skills[index].xp).c_str());
    Text(dc, cell, label, std::max(14, g_height / 47),
         index == g_selected[5] ? FW_BOLD : FW_NORMAL,
         index == g_selected[5] ? gold : cream,
         DT_CENTER | DT_VCENTER | DT_WORDBREAK);
  }
  Fill(dc, progression_detail, RGB(45, 53, 46));
  std::wstring progression_text = L"ACTIVE PERKS  ";
  std::uint32_t active_perks = 0;
  for (std::uint32_t index = 0; index < state.progression.perk_count; ++index) {
    if (state.progression.perks[index].active) ++active_perks;
  }
  progression_text += std::to_wstring(active_perks);
  progression_text += L"   ACTIVE EFFECTS  ";
  progression_text += std::to_wstring(state.progression.status_effect_count);
  progression_text += L"\n";
  const std::uint32_t effect_count = std::min<std::uint32_t>(
      4, state.progression.status_effect_count);
  for (std::uint32_t index = 0; index < effect_count; ++index) {
    if (index) progression_text += L"  |  ";
    progression_text += Wide(state.progression.status_effects[index].type.value);
    if (state.progression.status_effects[index].stacks > 1) {
      progression_text += L" x" +
          std::to_wstring(state.progression.status_effects[index].stacks);
    }
  }
  Text(dc, progression_detail, progression_text.c_str(),
       std::max(12, g_height / 56), FW_BOLD, cream,
       DT_CENTER | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
  RECT magic_title = magic_area;
  magic_title.bottom = magic_area.top + std::max(45, g_height / 14);
  RECT magic_icon = {magic_title.left + 4, magic_title.top + 4,
                     magic_title.left + std::max(34, g_height / 16),
                     magic_title.bottom - 4};
  AssetFrame(dc, InstalledAssetKey::kMagicHeader, magic_icon);
  wchar_t magic_header[256] = {};
  std::swprintf(magic_header, std::size(magic_header), L"MAGIC   %u UNLOCKED   PINNED: %ls",
                state.magic.unlocked_count,
                state.magic.pinned_spell.present
                    ? Wide(state.magic.pinned_spell.text.value).c_str() : L"--");
  Text(dc, magic_title, magic_header, std::max(15, g_height / 44), FW_BOLD,
       gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  const int magic_action_height = std::max(44, g_height / 15);
  RECT magic_action = magic_area;
  magic_action.top = magic_area.bottom - magic_action_height;
  const int spell_row_height = std::max(52, g_height / 10);
  const int spell_visible = std::max(
      1, static_cast<int>((magic_action.top - magic_title.bottom) /
                          spell_row_height));
  int spell_scroll = 0;
  if (g_selected[5] >= skill_count) {
    const int selected_spell = g_selected[5] - skill_count;
    spell_scroll = std::max(0, selected_spell - spell_visible + 1);
  }
  for (int row = 0; row < spell_visible; ++row) {
    const int spell_index = spell_scroll + row;
    if (spell_index >= spell_count) break;
    const int flattened = skill_count + spell_index;
    const SpellState& spell = state.magic.spells[spell_index];
    RECT box = {magic_area.left + 3,
                magic_title.bottom + row * spell_row_height + 2,
                magic_area.right - 3,
                std::min(magic_action.top,
                         magic_title.bottom + (row + 1) * spell_row_height - 2)};
    if (flattened == g_selected[5]) Fill(dc, box, RGB(76, 91, 73));
    RECT spell_icon = {box.left + 4, box.top + 4,
                       box.left + std::max(34, g_height / 16),
                       box.bottom - 4};
    if (spell.upper_icon_asset.present)
      DynamicFrame(dc, InstalledDynamicAssetClass::kSpellIcon,
                   spell.upper_icon_asset.text.value, spell_icon);
    const SpellCooldownState* cooldown = nullptr;
    for (std::uint32_t index = 0; index < state.magic.cooldown_count; ++index) {
      if (state.magic.cooldowns[index].index == spell.index) {
        cooldown = &state.magic.cooldowns[index];
        break;
      }
    }
    wchar_t label[512] = {};
    std::swprintf(label, std::size(label), L"%ls   MP %ls   %ls\n%ls",
                  Wide(spell.display_name.value).c_str(),
                  CompactNumber(spell.mana_cost).c_str(),
                  cooldown
                      ? (cooldown->ready
                             ? L"READY"
                             : (std::wstring(L"COOLDOWN ") +
                                CompactNumber(cooldown->remaining_frames))
                                   .c_str())
                      : L"STATE --",
                  Wide(spell.description.value).c_str());
    RECT spell_text = box;
    spell_text.left = spell_icon.right + 5;
    Text(dc, spell_text, label, std::max(13, g_height / 52),
         flattened == g_selected[5] ? FW_BOLD : FW_NORMAL,
         flattened == g_selected[5] ? gold : cream,
         DT_LEFT | DT_VCENTER | DT_WORDBREAK | DT_END_ELLIPSIS);
  }
  const bool spell_selected = g_selected[5] >= skill_count && spell_count > 0;
  const int selected_spell_row = spell_selected ? g_selected[5] - skill_count : -1;
  const bool selected_is_pinned = spell_selected &&
      selected_spell_row < spell_count &&
      state.magic.pinned_spell_index.present &&
      state.magic.spells[selected_spell_row].index ==
          state.magic.pinned_spell_index.value;
  Fill(dc, magic_action, spell_selected ? RGB(116, 92, 49)
                                        : RGB(62, 64, 57));
  Text(dc, magic_action,
       spell_selected
           ? (selected_is_pinned
                  ? L"ACTIVATE: CAST PINNED SPELL   |   HOLD: UNPIN"
                  : L"ACTIVATE: PIN THIS LEARNED SPELL")
           : L"SELECT A LEARNED SPELL TO MANAGE MAGIC",
       std::max(11, g_height / 60), FW_BOLD,
       spell_selected ? cream : RGB(151, 143, 119),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  if (skill_count == 0 && spell_count == 0) {
    Text(dc, content, L"No validated skill or learned-spell records.",
         std::max(16, g_height / 42), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  }
}

void DrawAlmanacPage(HDC dc, const LiveStateSnapshot& state) {
  const RECT content = LiveContentBounds();
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  const int split = content.left + (content.right - content.left) * 42 / 100;
  RECT categories = content;
  categories.right = split;
  RECT detail = content;
  detail.left = split + std::max(10, g_width / 80);
  Fill(dc, categories, RGB(39, 49, 45));
  Fill(dc, detail, RGB(45, 53, 46));
  const int count = static_cast<int>(state.almanac.category_count);
  const int row_height = std::max(
      46, static_cast<int>((categories.bottom - categories.top) / 8));
  const int visible = std::max(
      1, static_cast<int>((categories.bottom - categories.top) / row_height));
  g_selected[6] = std::clamp(g_selected[6], 0, std::max(0, count - 1));
  if (g_selected[6] < g_scroll[6]) g_scroll[6] = g_selected[6];
  if (g_selected[6] >= g_scroll[6] + visible)
    g_scroll[6] = g_selected[6] - visible + 1;
  g_scroll[6] = std::clamp(g_scroll[6], 0, std::max(0, count - visible));
  for (int row = 0; row < visible; ++row) {
    const int index = g_scroll[6] + row;
    if (index >= count) break;
    const AlmanacCategoryState& category = state.almanac.categories[index];
    RECT box = {categories.left + 3, categories.top + row * row_height,
                categories.right - 3,
                std::min(categories.bottom,
                         categories.top + (row + 1) * row_height - 2)};
    if (index == g_selected[6]) Fill(dc, box, RGB(76, 91, 73));
    wchar_t label[320] = {};
    std::swprintf(label, std::size(label), L"%ls   %u/%u",
                  Wide(category.display_name.value).c_str(),
                  category.discovered_count, category.total_count);
    Text(dc, box, label, std::max(14, g_height / 48),
         index == g_selected[6] ? FW_BOLD : FW_NORMAL,
         index == g_selected[6] ? gold : cream,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (count <= 0) {
    Text(dc, content, L"No validated discovery data.",
         std::max(16, g_height / 42), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    return;
  }
  const AlmanacCategoryState& category = state.almanac.categories[g_selected[6]];
  RECT title = detail;
  title.bottom = detail.top + std::max(52, g_height / 11);
  RECT almanac_icon = {title.left + 4, title.top + 4,
                       title.left + std::max(38, g_height / 14),
                       title.bottom - 4};
  AssetFrame(dc, InstalledAssetKey::kAlmanacHeader, almanac_icon);
  wchar_t title_text[320] = {};
  std::swprintf(title_text, std::size(title_text), L"%ls   %u / %u DISCOVERED",
                Wide(category.display_name.value).c_str(),
                category.discovered_count, category.total_count);
  Text(dc, title, title_text, std::max(19, g_height / 35), FW_BOLD, gold,
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  RECT items = detail;
  items.top = title.bottom;
  items.bottom = detail.bottom - std::max(112, g_height / 6);
  const std::uint32_t shown = std::min<std::uint32_t>(category.item_count, 8);
  const int item_row_height = std::max(
      32, static_cast<int>((items.bottom - items.top) /
                           std::max<std::uint32_t>(1, shown)));
  for (std::uint32_t offset = 0; offset < shown; ++offset) {
    const std::uint32_t item_index = category.first_item + offset;
    if (item_index >= state.almanac.item_count) break;
    const CollectionItemState& item = state.almanac.discovered_items[item_index];
    RECT row = {items.left, items.top + static_cast<LONG>(offset) * item_row_height,
                items.right,
                std::min(items.bottom, items.top +
                    static_cast<LONG>(offset + 1) * item_row_height - 2)};
    RECT icon = {row.left + 2, row.top + 2,
                 row.left + item_row_height - 3, row.bottom - 2};
    if (item.icon_asset.present)
      DynamicFrame(dc, InstalledDynamicAssetClass::kItemIcon,
                   item.icon_asset.text.value, icon,
                   static_cast<std::uint32_t>(std::max<std::int64_t>(
                       0, item.icon_frame)));
    RECT label = row;
    label.left = icon.right + 5;
    const std::wstring name = Wide(item.display_name.value);
    Text(dc, label, name.c_str(), std::max(13, g_height / 52), FW_NORMAL,
         cream, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  }
  if (!shown)
    Text(dc, items, L"No discovered entries in this category.",
         std::max(14, g_height / 48), FW_NORMAL, cream,
         DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT museum = detail;
  museum.top = items.bottom + 4;
  Fill(dc, museum, RGB(54, 64, 54));
  wchar_t museum_text[512] = {};
  std::swprintf(museum_text, std::size(museum_text),
                L"MUSEUM   DONATED %u/%u   DISCOVERED %u/%u\nWINGS %u   SETS %u   UNKNOWN IDENTITIES HIDDEN",
                state.museum.donated_count, state.museum.total_count,
                state.museum.discovered_count, state.museum.total_count,
                state.museum.wing_count, state.museum.set_count);
  Text(dc, museum, museum_text, std::max(14, g_height / 48), FW_BOLD,
       cream, DT_CENTER | DT_VCENTER | DT_WORDBREAK);
}

void DrawLivePage(HDC dc, const LiveStateSnapshot& state, int page) {
  switch (page) {
    case 0: DrawInventoryPage(dc, state); break;
    case 1: DrawMapPage(dc, state); break;
    case 2: DrawQuestPage(dc, state); break;
    case 3: DrawVillagersPage(dc, state); break;
    case 4: DrawAnimalsPage(dc, state); break;
    case 5: DrawSkillsMagicPage(dc, state); break;
    case 6: DrawAlmanacPage(dc, state); break;
    default: break;
  }
}

void DrawLiveConnection(HDC dc, const DiagnosticSnapshot& diagnostic) {
  const COLORREF cream = RGB(246, 231, 188);
  const COLORREF gold = RGB(226, 177, 82);
  RECT title = {g_width / 16, g_height / 10, g_width * 15 / 16,
                g_height * 3 / 10};
  Text(dc, title, L"FIELDS OF MISTRIA", std::max(28, g_height / 22),
       FW_BOLD, gold, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
  RECT body = {g_width / 10, g_height * 3 / 10, g_width * 9 / 10,
               g_height * 8 / 10};
  const wchar_t* status = g_have_live_state
      ? L"LIVE BRIDGE CONNECTED\n\nWaiting for a loaded world/save."
      : L"WAITING FOR LIVE GAME STATE\n\nThe panel is active and input-isolated. The bridge begins publishing only after a world/save has finished loading.";
  Text(dc, body, status, std::max(18, g_height / 36), FW_NORMAL, cream,
       DT_CENTER | DT_VCENTER | DT_WORDBREAK);
  RECT footer = {g_width / 10, g_height * 82 / 100, g_width * 9 / 10,
                 g_height * 92 / 100};
  const std::wstring native_status = Wide(diagnostic.status);
  Text(dc, footer, native_status.c_str(), std::max(13, g_height / 52),
       FW_NORMAL, RGB(172, 163, 137),
       DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void Render(HWND window, HDC target) {
  SlowOperation performance("panel-render-slow", "panel render", 16);
  RECT client = {};
  GetClientRect(window, &client);
  g_width = client.right;
  g_height = client.bottom;
  if (g_width <= 0 || g_height <= 0) return;
  if (!EnsureBackbuffer(target, g_width, g_height)) return;
  HDC memory = g_backbuffer_dc;
  Fill(memory, client, RGB(31, 42, 40));
  RECT inner = {g_width / 50, g_height / 50, g_width * 49 / 50,
                g_height * 49 / 50};
  AssetFrame(memory, InstalledAssetKey::kJournalBackplate, inner);
  FrameRect(memory, &inner, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
  DiagnosticSnapshot state = {};
  CopyLatestSnapshot(&state);
  if (g_have_live_state && g_live_state.session.in_game) {
    DrawLiveChrome(memory, g_live_state);
    if (g_previous_page >= 0 && g_page_animation_direction) {
      constexpr double kAnimationMs = 190.0;
      const double linear = std::clamp(
          static_cast<double>(GetTickCount64() - g_page_animation_start) /
              kAnimationMs,
          0.0, 1.0);
      const double inverse = 1.0 - linear;
      const double progress = 1.0 - inverse * inverse * inverse;
      const int distance = g_width;
      const int previous_offset = static_cast<int>(
          -g_page_animation_direction * progress * distance);
      const int current_offset = static_cast<int>(
          g_page_animation_direction * (1.0 - progress) * distance);
      const RECT clip = LiveContentBounds();
      int saved = SaveDC(memory);
      IntersectClipRect(memory, clip.left, clip.top, clip.right, clip.bottom);
      OffsetViewportOrgEx(memory, previous_offset, 0, nullptr);
      DrawLivePage(memory, g_live_state, g_previous_page);
      RestoreDC(memory, saved);
      saved = SaveDC(memory);
      IntersectClipRect(memory, clip.left, clip.top, clip.right, clip.bottom);
      OffsetViewportOrgEx(memory, current_offset, 0, nullptr);
      DrawLivePage(memory, g_live_state, g_page);
      RestoreDC(memory, saved);
      if (linear >= 1.0) {
        g_previous_page = -1;
        g_page_animation_direction = 0;
      }
    } else {
      DrawLivePage(memory, g_live_state, g_page);
    }
    if (!g_action_feedback.empty()) {
      RECT feedback = {g_width / 50, g_height * 95 / 100,
                       g_width * 49 / 50, g_height * 99 / 100};
      Text(memory, feedback, g_action_feedback.c_str(),
           std::max(11, g_height / 62), FW_BOLD, RGB(226, 177, 82),
           DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
  } else {
    DrawLiveConnection(memory, state);
  }
  BitBlt(target, 0, 0, g_width, g_height, memory, 0, 0, SRCCOPY);
  g_drawn_sequence = state.sequence;
  g_page_dirty = false;
}

void ChangePage(int delta, const sockaddr_in* sender, int sender_size) {
  const int next = WrapPage(g_page, delta, kPageCount);
  if (next == g_page) return;
  g_previous_page = g_page;
  g_page_animation_direction = delta > 0 ? 1 : -1;
  g_page_animation_start = GetTickCount64();
  g_page = next;
  g_page_dirty = true;
  Log(LogLevel::kInfo, "[INPUT] live page=%d", g_page);
  if (sender && sender_size >= static_cast<int>(sizeof(sockaddr_in)) &&
      g_input != INVALID_SOCKET) {
    constexpr char response[] = "haptic selection";
    sendto(g_input, response, sizeof(response) - 1, 0,
           reinterpret_cast<const sockaddr*>(sender), sender_size);
  }
}

void SelectPage(int page) {
  if (page < 0 || page >= kPageCount || page == g_page) return;
  g_previous_page = g_page;
  g_page_animation_direction = page > g_page ? 1 : -1;
  g_page_animation_start = GetTickCount64();
  g_page = page;
  g_page_dirty = true;
  Log(LogLevel::kInfo, "[INPUT] live tab=%d", g_page);
}

void ChangeMapSubpage(int delta) {
  if (!g_have_live_state || g_live_state.map.page_count == 0 || !delta) return;
  const int count = static_cast<int>(g_live_state.map.page_count);
  const int current = std::clamp(g_map_page_selection, 0, count - 1);
  g_map_page_selection = WrapPage(current, delta, count);
  g_map_centered = false;
  g_map_zoom = 1.0;
  g_page_dirty = true;
  Log(LogLevel::kInfo, "[INPUT] map subpage=%d id=%s",
      g_map_page_selection,
      g_live_state.map.pages[g_map_page_selection].id.value);
}

int PageItemCount() {
  if (!g_have_live_state || !g_live_state.session.in_game) return 0;
  if (g_page == 0) return static_cast<int>(g_live_state.inventory.slot_count);
  if (g_page == 2) return static_cast<int>(g_live_state.quests.quest_count);
  if (g_page == 3) return static_cast<int>(g_live_state.villagers.villager_count);
  if (g_page == 4) return static_cast<int>(g_live_state.animals.livestock_count) +
                           (g_live_state.animals.pet.present ? 1 : 0);
  if (g_page == 5) return static_cast<int>(g_live_state.progression.skill_count +
                                           g_live_state.magic.spell_count);
  if (g_page == 6) return static_cast<int>(g_live_state.almanac.category_count);
  return 0;
}

void MoveSelection(int delta) {
  if (g_page == 1 && delta) {
    g_map_center_y += 12.0 * static_cast<double>(delta) / g_map_zoom;
    g_page_dirty = true;
    return;
  }
  if (g_page == 0 && g_armor_focus) {
    const int count = static_cast<int>(g_live_state.armor.slot_count);
    if (count > 0 && delta) {
      g_armor_selected =
          std::clamp(g_armor_selected + delta, 0, count - 1);
      g_page_dirty = true;
    }
    return;
  }
  const int count = PageItemCount();
  if (count <= 0 || !delta) return;
  g_selected[g_page] =
      std::clamp(g_selected[g_page] + delta, 0, count - 1);
  g_page_dirty = true;
}

const wchar_t* SubmitCodeText(SubmitCode code) {
  switch (code) {
    case SubmitCode::kQueued: return L"REQUEST QUEUED";
    case SubmitCode::kNoValidSession: return L"ACTION BLOCKED: NO VALID SESSION";
    case SubmitCode::kCapabilityUnavailable: return L"ACTION NOT AVAILABLE";
    case SubmitCode::kInvalidArgument: return L"ACTION BLOCKED: INVALID SLOT";
    case SubmitCode::kStaleView: return L"ACTION BLOCKED: STATE CHANGED";
    case SubmitCode::kQueueFull: return L"ACTION QUEUE FULL";
  }
  return L"ACTION BLOCKED";
}

bool CopyExactFingerprint(const ItemSlotState& slot, std::string* output) {
  if (!output || slot.empty || slot.instance_fingerprint.truncated ||
      slot.instance_fingerprint.value[0] == '\0') {
    return false;
  }
  *output = slot.instance_fingerprint.value;
  return true;
}

bool CopyFingerprintOrEmpty(const ItemSlotState& slot, std::string* output) {
  if (!output) return false;
  if (slot.empty) {
    output->clear();
    return true;
  }
  return CopyExactFingerprint(slot, output);
}

bool ItemHasTag(const ItemSlotState& item, const char* required) {
  if (!required || !required[0]) return false;
  for (std::uint32_t index = 0; index < item.tag_count; ++index) {
    if (!item.tags[index].truncated &&
        std::strcmp(item.tags[index].value, required) == 0) {
      return true;
    }
  }
  return false;
}

bool ItemFitsArmorSlot(const ItemSlotState& item,
                       const ItemSlotState& armor_slot) {
  if (item.empty) return false;
  for (std::uint32_t index = 0; index < armor_slot.required_tag_count;
       ++index) {
    const FixedUtf8<96>& required = armor_slot.required_tags[index];
    if (required.truncated || !ItemHasTag(item, required.value)) return false;
  }
  return true;
}

int FindCompatibleArmorSlot(const ItemSlotState& item) {
  int occupied_match = -1;
  for (std::uint32_t index = 0; index < g_live_state.armor.slot_count;
       ++index) {
    const ItemSlotState& target = g_live_state.armor.slots[index];
    if (!ItemFitsArmorSlot(item, target)) continue;
    if (target.empty) return static_cast<int>(index);
    if (occupied_match < 0) occupied_match = static_cast<int>(index);
  }
  return occupied_match;
}

int FindEmptyInventorySlot() {
  for (std::uint32_t index = 0; index < g_live_state.inventory.slot_count;
       ++index) {
    if (g_live_state.inventory.slots[index].empty)
      return static_cast<int>(index);
  }
  return -1;
}

void ActivateInventory() {
  if (!g_action_transport || !g_have_live_state ||
      !g_live_state.session.in_game || g_live_state.inventory.slot_count == 0) {
    g_action_feedback = L"ACTION BLOCKED: LIVE STATE UNAVAILABLE";
    g_page_dirty = true;
    return;
  }
  if (g_armor_focus) {
    if (!g_live_state.armor.slot_count) {
      g_action_feedback = L"ACTION BLOCKED: NO PUBLISHED ARMOR SLOTS";
      g_page_dirty = true;
      return;
    }
    const int armor_index = std::clamp(
        g_armor_selected, 0,
        static_cast<int>(g_live_state.armor.slot_count) - 1);
    const ItemSlotState& armor = g_live_state.armor.slots[armor_index];
    if (armor.empty) {
      g_action_feedback = L"ACTION BLOCKED: SELECTED ARMOR SLOT IS EMPTY";
      g_page_dirty = true;
      return;
    }
    const int destination = FindEmptyInventorySlot();
    std::string expected_item;
    if (destination < 0) {
      g_action_feedback = L"ACTION BLOCKED: INVENTORY HAS NO EMPTY SLOT";
      g_page_dirty = true;
      return;
    }
    if (!CopyExactFingerprint(armor, &expected_item)) {
      g_action_feedback = L"ACTION BLOCKED: ITEM IDENTITY WAS TRUNCATED";
      g_page_dirty = true;
      return;
    }
    const SubmitResult result = g_action_transport->QueueUnequipArmor(
        armor_index, destination, expected_item, "",
        TopicRevision(g_live_state, LiveTopic::kInventory),
        TopicRevision(g_live_state, LiveTopic::kArmor));
    g_action_feedback = SubmitCodeText(result.code);
    g_page_dirty = true;
    return;
  }
  const int index = std::clamp(
      g_selected[0], 0, static_cast<int>(g_live_state.inventory.slot_count) - 1);
  const ItemSlotState& slot = g_live_state.inventory.slots[index];
  if (slot.empty) {
    g_action_feedback = L"ACTION BLOCKED: EMPTY SLOT";
    g_page_dirty = true;
    return;
  }
  SubmitResult result;
  if (g_swap_source == index) {
    g_swap_source = -1;
    g_action_feedback = L"MOVE SOURCE MARK CANCELLED";
    g_page_dirty = true;
    return;
  }
  if (g_swap_source >= 0 && g_swap_source != index) {
    result = g_action_transport->QueueSwapSlots(
        g_swap_source, index,
        TopicRevision(g_live_state, LiveTopic::kInventory));
    if (result.code == SubmitCode::kQueued) g_swap_source = -1;
  } else if (slot.can_consume_from_bridge) {
    if (g_live_state.hotbar.selected_index != index) {
      result = g_action_transport->QueueSelectHotbar(
          index, TopicRevision(g_live_state, LiveTopic::kInventory),
          TopicRevision(g_live_state, LiveTopic::kHotbar));
      g_action_feedback = result.code == SubmitCode::kQueued
          ? L"HOTBAR CHANGE QUEUED; ACTIVATE AGAIN AFTER IT UPDATES TO CONSUME"
          : SubmitCodeText(result.code);
      g_page_dirty = true;
      return;
    }
    std::string fingerprint;
    if (!CopyExactFingerprint(slot, &fingerprint)) {
      g_action_feedback = L"ACTION BLOCKED: ITEM IDENTITY WAS TRUNCATED";
      g_page_dirty = true;
      return;
    }
    result = g_action_transport->QueueConsumeItem(
        index, fingerprint,
        TopicRevision(g_live_state, LiveTopic::kInventory),
        TopicRevision(g_live_state, LiveTopic::kHotbar));
  } else if (std::strcmp(slot.ui_category.value, "equipment") == 0) {
    const int armor_index = FindCompatibleArmorSlot(slot);
    if (armor_index < 0) {
      g_action_feedback = L"ACTION BLOCKED: NO COMPATIBLE ARMOR SLOT";
      g_page_dirty = true;
      return;
    }
    std::string expected_item;
    std::string expected_equipped;
    if (!CopyExactFingerprint(slot, &expected_item) ||
        !CopyFingerprintOrEmpty(g_live_state.armor.slots[armor_index],
                                &expected_equipped)) {
      g_action_feedback = L"ACTION BLOCKED: ITEM IDENTITY WAS TRUNCATED";
      g_page_dirty = true;
      return;
    }
    result = g_action_transport->QueueEquipArmor(
        index, armor_index, expected_item, expected_equipped,
        TopicRevision(g_live_state, LiveTopic::kInventory),
        TopicRevision(g_live_state, LiveTopic::kArmor));
  } else {
    result = g_action_transport->QueueSelectHotbar(
        index, TopicRevision(g_live_state, LiveTopic::kInventory),
        TopicRevision(g_live_state, LiveTopic::kHotbar));
  }
  g_action_feedback = SubmitCodeText(result.code);
  g_page_dirty = true;
}

void ActivateMagicSpell() {
  if (!g_action_transport || !g_have_live_state ||
      !g_live_state.session.in_game) {
    g_action_feedback = L"ACTION BLOCKED: LIVE STATE UNAVAILABLE";
    g_page_dirty = true;
    return;
  }
  const int skill_count =
      static_cast<int>(g_live_state.progression.skill_count);
  const int spell_row = g_selected[5] - skill_count;
  if (spell_row < 0 ||
      spell_row >= static_cast<int>(g_live_state.magic.spell_count)) {
    g_action_feedback = L"SELECT A LEARNED SPELL FIRST";
    g_page_dirty = true;
    return;
  }
  const std::int64_t spell = g_live_state.magic.spells[spell_row].index;
  const std::uint64_t revision =
      TopicRevision(g_live_state, LiveTopic::kMagic);
  const bool pinned = g_live_state.magic.pinned_spell_index.present &&
      g_live_state.magic.pinned_spell_index.value == spell;
  const SubmitResult result = pinned
      ? g_action_transport->QueueCastPinnedSpell(spell, revision)
      : g_action_transport->QueuePinSpell(spell, revision);
  g_action_feedback = SubmitCodeText(result.code);
  g_page_dirty = true;
}

void UnpinFocusedSpell() {
  if (!g_action_transport || !g_have_live_state ||
      !g_live_state.session.in_game ||
      !g_live_state.magic.pinned_spell_index.present) {
    return;
  }
  const int skill_count =
      static_cast<int>(g_live_state.progression.skill_count);
  const int spell_row = g_selected[5] - skill_count;
  if (spell_row < 0 ||
      spell_row >= static_cast<int>(g_live_state.magic.spell_count)) return;
  const std::int64_t spell = g_live_state.magic.spells[spell_row].index;
  if (spell != g_live_state.magic.pinned_spell_index.value) return;
  const SubmitResult result = g_action_transport->QueueUnpinSpell(
      spell, TopicRevision(g_live_state, LiveTopic::kMagic));
  g_action_feedback = SubmitCodeText(result.code);
  g_page_dirty = true;
}

void ActivateFocused() {
  if (g_page == 0) ActivateInventory();
  else if (g_page == 5) ActivateMagicSpell();
}

void HoldInventoryRow() {
  if (!g_have_live_state || !g_live_state.session.in_game ||
      g_live_state.inventory.slot_count == 0) return;
  const int index = std::clamp(
      g_selected[0], 0, static_cast<int>(g_live_state.inventory.slot_count) - 1);
  if (g_live_state.inventory.slots[index].empty) return;
  g_swap_source = index;
  g_action_feedback = L"MOVE SOURCE MARKED; SELECT DESTINATION AND ACTIVATE";
  g_page_dirty = true;
}

void SelectContentAt(float x, float y, bool hold) {
  if (!g_have_live_state || !g_live_state.session.in_game) return;
  const float top = 0.23f;
  const float bottom = 0.94f;
  if (y < top || y >= bottom) return;
  if (g_page == 0 && x < 0.58f) {
    const int count = static_cast<int>(g_live_state.inventory.slot_count);
    const int visible = 8;
    const int row = std::clamp(
        static_cast<int>(((y - top) / (bottom - top)) * visible), 0,
        visible - 1);
    const int index = g_scroll[0] + row;
    if (index < count) {
      g_selected[0] = index;
      g_armor_focus = false;
      if (hold) HoldInventoryRow();
      g_page_dirty = true;
    }
    return;
  }
  if (g_page == 0 && x >= 0.58f) {
    const int content_bottom = g_height * 94 / 100;
    const int action_height = std::max(50, g_height / 14);
    const int armor_height = std::max(66, g_height / 10);
    const float action_top = static_cast<float>(content_bottom - action_height) /
                             static_cast<float>(std::max(1, g_height));
    const float armor_top = static_cast<float>(
        content_bottom - action_height - armor_height - 3) /
        static_cast<float>(std::max(1, g_height));
    if (y >= action_top) {
      ActivateInventory();
      return;
    }
    if (y >= armor_top && g_live_state.armor.slot_count > 0) {
      const float detail_fraction = std::clamp((x - 0.58f) / 0.42f,
                                               0.0f, 0.9999f);
      g_armor_selected = std::clamp(
          static_cast<int>(detail_fraction *
              static_cast<float>(g_live_state.armor.slot_count)),
          0, static_cast<int>(g_live_state.armor.slot_count) - 1);
      g_armor_focus = true;
      if (hold) ActivateInventory();
      g_page_dirty = true;
      return;
    }
    return;
  }
  if (g_page == 2 && x < 0.45f) {
    const int count = static_cast<int>(g_live_state.quests.quest_count);
    const int visible = 7;
    const int row = std::clamp(
        static_cast<int>(((y - top) / (bottom - top)) * visible), 0,
        visible - 1);
    const int index = g_scroll[2] + row;
    if (index < count) g_selected[2] = index;
    g_page_dirty = true;
    return;
  }
  if ((g_page == 3 && x < 0.46f) || (g_page == 4 && x < 0.48f)) {
    const int count = PageItemCount();
    const int visible = 7;
    const int row = std::clamp(
        static_cast<int>(((y - top) / (bottom - top)) * visible), 0,
        visible - 1);
    const int index = g_scroll[g_page] + row;
    if (index < count) g_selected[g_page] = index;
    g_page_dirty = true;
    return;
  }
  if (g_page == 5) {
    const int skills = static_cast<int>(g_live_state.progression.skill_count);
    const int spells = static_cast<int>(g_live_state.magic.spell_count);
    if (x >= 0.57f && y >= 0.87f) {
      if (hold) UnpinFocusedSpell();
      else ActivateMagicSpell();
      return;
    }
    if (x < 0.57f && skills > 0) {
      const int rows = std::max(1, (skills + 2) / 3);
      const int column = std::clamp(static_cast<int>((x / 0.57f) * 3.0f), 0, 2);
      const int row = std::clamp(
          static_cast<int>(((y - top) / (bottom - top)) * rows), 0,
          rows - 1);
      const int index = row * 3 + column;
      if (index < skills) g_selected[5] = index;
    } else if (x >= 0.57f && spells > 0) {
      const int content_top = g_height * 23 / 100;
      const int content_bottom = g_height * 94 / 100;
      const int title_height = std::max(45, g_height / 14);
      const int action_height = std::max(44, g_height / 15);
      const int row_height = std::max(52, g_height / 10);
      const int visible = std::max(
          1, (content_bottom - action_height -
              (content_top + title_height)) / row_height);
      const int selected_spell = g_selected[5] >= skills
          ? g_selected[5] - skills : 0;
      const int spell_scroll = std::max(0, selected_spell - visible + 1);
      const int row = std::clamp(
          static_cast<int>(((y - top) /
              std::max(0.001f, 0.87f - top)) * visible), 0,
          visible - 1);
      const int spell_row = spell_scroll + row;
      if (spell_row < spells) {
        const int flattened = skills + spell_row;
        const bool already_selected = g_selected[5] == flattened;
        g_selected[5] = flattened;
        if (hold) UnpinFocusedSpell();
        else if (already_selected) ActivateMagicSpell();
      }
    }
    g_page_dirty = true;
    return;
  }
  if (g_page == 6 && x < 0.42f) {
    const int count = static_cast<int>(g_live_state.almanac.category_count);
    const int row = std::clamp(
        static_cast<int>(((y - top) / (bottom - top)) * 8.0f), 0, 7);
    const int index = g_scroll[6] + row;
    if (index < count) g_selected[6] = index;
    g_page_dirty = true;
    return;
  }
  if (g_page == 1) {
    if (y >= 0.29f && y < 0.36f && g_live_state.map.page_count) {
      const int count = static_cast<int>(g_live_state.map.page_count);
      g_map_page_selection = std::clamp(
          static_cast<int>(x * static_cast<float>(count)), 0, count - 1);
      g_map_zoom = 1.0;
    }
    g_map_center_x = 339.0 / 2.0;
    g_map_center_y = 179.0 / 2.0;
    g_map_centered = true;
    g_page_dirty = true;
  }
}

void HandleGesture(const PanelGesture& gesture, const sockaddr_in* sender,
                   int sender_size) {
  if (gesture.kind == PanelGestureKind::kPage) {
    ChangePage(gesture.direction, sender, sender_size);
  } else if (gesture.kind == PanelGestureKind::kNavLeft) {
    if (g_page == 1) {
      ChangeMapSubpage(-1);
    } else if (g_page == 0) {
      g_armor_focus = false;
      g_page_dirty = true;
    } else {
      MoveSelection(-1);
    }
  } else if (gesture.kind == PanelGestureKind::kNavRight) {
    if (g_page == 1) {
      ChangeMapSubpage(1);
    } else if (g_page == 0) {
      if (g_have_live_state && g_live_state.armor.slot_count > 0) {
        g_armor_focus = true;
        g_page_dirty = true;
      }
    } else {
      MoveSelection(1);
    }
  } else if (gesture.kind == PanelGestureKind::kNavUp) {
    const int skill_count = g_have_live_state
        ? static_cast<int>(g_live_state.progression.skill_count) : 0;
    MoveSelection(g_page == 5 && g_selected[5] < skill_count ? -3 : -1);
  } else if (gesture.kind == PanelGestureKind::kNavDown) {
    const int skill_count = g_have_live_state
        ? static_cast<int>(g_live_state.progression.skill_count) : 0;
    MoveSelection(g_page == 5 && g_selected[5] < skill_count ? 3 : 1);
  } else if (gesture.kind == PanelGestureKind::kNavActivate) {
    ActivateFocused();
  } else if (gesture.kind == PanelGestureKind::kScroll) {
    MoveSelection(gesture.value < 0.0f ? 1 : -1);
  } else if (gesture.kind == PanelGestureKind::kPan) {
    if (g_page == 1) {
      g_map_center_x -= gesture.x * 339.0 / g_map_zoom;
      g_map_center_y -= gesture.y * 179.0 / g_map_zoom;
      g_page_dirty = true;
    }
  } else if (gesture.kind == PanelGestureKind::kPinch) {
    if (g_page == 1 && std::isfinite(gesture.value)) {
      g_map_zoom = std::clamp(g_map_zoom * gesture.value, 0.5, 4.0);
      g_page_dirty = true;
    }
  } else if (gesture.kind == PanelGestureKind::kHold) {
    SelectContentAt(gesture.x, gesture.y, true);
  } else if (gesture.kind == PanelGestureKind::kTap) {
    if (gesture.y <= 0.10f) {
      SelectPage(std::clamp(static_cast<int>(gesture.x * kPageCount), 0,
                            kPageCount - 1));
    } else {
      SelectContentAt(gesture.x, gesture.y, false);
    }
  }
}

void PollInput() {
  if (g_input == INVALID_SOCKET) return;
  for (int index = 0; index < 64; ++index) {
    char bytes[256] = {};
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    const int received = recvfrom(g_input, bytes, sizeof(bytes), 0,
                                  reinterpret_cast<sockaddr*>(&sender),
                                  &sender_size);
    if (received == SOCKET_ERROR) {
      const int error = WSAGetLastError();
      if (error != WSAEWOULDBLOCK) {
        LogRateLimited("input-receive", 5000, LogLevel::kWarning,
                       "[INPUT] UDP receive failed error=%d", error);
      }
      break;
    }
    if (sender.sin_family != AF_INET ||
        ntohl(sender.sin_addr.s_addr) != INADDR_LOOPBACK) {
      LogRateLimited("input-remote", 5000, LogLevel::kWarning,
                     "[INPUT] rejected non-loopback datagram");
      continue;
    }
    PanelPacket packet;
    if (!ParsePanelPacket(bytes, static_cast<std::size_t>(received), &packet)) {
      LogRateLimited("input-invalid", 2000, LogLevel::kWarning,
                     "[INPUT] rejected malformed packet bytes=%d", received);
      continue;
    }
    g_gestures.SetMapCapture(g_page == 1);
    HandleGesture(g_gestures.Consume(packet), &sender, sender_size);
  }
}

bool InitializeActionTransportForDiscoveredState() {
  if (g_action_transport_ready || !g_live_ingestor || !g_action_transport)
    return g_action_transport_ready;
  wchar_t state_path[32768] = {};
  if (!g_live_ingestor->CopyStatePath(state_path, std::size(state_path)))
    return false;
  std::wstring directory(state_path);
  const std::size_t slash = directory.find_last_of(L"\\/");
  if (slash == std::wstring::npos) return false;
  directory.resize(slash);
  g_action_transport_ready = g_action_transport->Initialize(directory);
  if (g_action_transport_ready) {
    Log(LogLevel::kInfo, "[ACTION] command transport initialized");
  } else {
    LogRateLimited("action-init", 5000, LogLevel::kWarning,
                   "[ACTION] command transport directory rejected");
  }
  return g_action_transport_ready;
}

void UpdateActionSessionFromLiveState() {
  if (!InitializeActionTransportForDiscoveredState()) return;
  BridgeActionSession session;
  session.validated = true;
  session.protocol = kBridgeProtocol;
  session.schema = g_live_state.schema;
  session.epoch = g_live_state.epoch.value;
  session.state_sequence = g_live_state.sequence;
  session.inventory_revision =
      TopicRevision(g_live_state, LiveTopic::kInventory);
  session.armor_revision =
      TopicRevision(g_live_state, LiveTopic::kArmor);
  session.hotbar_revision = TopicRevision(g_live_state, LiveTopic::kHotbar);
  session.magic_revision = TopicRevision(g_live_state, LiveTopic::kMagic);
  session.inventory_size = g_live_state.inventory.declared_size;
  session.armor_size = g_live_state.armor.declared_size;
  session.selected_hotbar_index = static_cast<std::int32_t>(
      g_live_state.hotbar.selected_index);
  session.learned_spell_count = std::min<std::uint32_t>(
      g_live_state.magic.spell_count, kMaxBridgeSpells);
  for (std::uint32_t index = 0; index < session.learned_spell_count; ++index) {
    session.learned_spell_indexes[index] = static_cast<std::int32_t>(
        g_live_state.magic.spells[index].index);
  }
  session.pinned_spell_index = g_live_state.magic.pinned_spell_index.present
      ? static_cast<std::int32_t>(g_live_state.magic.pinned_spell_index.value)
      : -1;
  session.in_game = g_live_state.session.in_game && !g_live_state_stale;
  session.can_select_hotbar =
      g_live_state.session.capabilities.select_hotbar;
  session.can_swap_slots = g_live_state.session.capabilities.swap_slots;
  session.can_consume_item =
      g_live_state.session.capabilities.consume_item;
  session.can_equip_armor =
      g_live_state.session.capabilities.equip_armor;
  session.can_unequip_armor =
      g_live_state.session.capabilities.unequip_armor;
  session.can_pin_spell = g_live_state.session.capabilities.pin_spell;
  session.can_unpin_spell = g_live_state.session.capabilities.unpin_spell;
  session.can_cast_pinned_spell =
      g_live_state.session.capabilities.cast_pinned_spell;
  if (!g_action_transport->UpdateSession(session) && session.in_game) {
    LogRateLimited("action-session", 5000, LogLevel::kWarning,
                   "[ACTION] validated state did not produce a safe action session");
  }
}

void PumpBridge() {
  if (!g_live_ingestor) return;
  const ULONGLONG now = GetTickCount64();
  if (g_last_bridge_poll &&
      now - g_last_bridge_poll < g_config.bridge_poll_ms) {
    return;
  }
  g_last_bridge_poll = now;
  SlowOperation performance("bridge-pump-slow", "live bridge pump", 12);
  const LivePollResult state_result = g_live_ingestor->Poll();
  if (state_result == LivePollResult::kPublished) {
    LiveStateSnapshot next;
    if (g_live_ingestor->CopyLatest(&next)) {
      const bool epoch_changed = !g_have_live_state ||
          std::strcmp(g_live_state.epoch.value, next.epoch.value) != 0;
      g_live_state = next;
      g_have_live_state = true;
      g_live_state_stale = false;
      g_last_live_publish = now;
      if (epoch_changed) {
        std::fill(std::begin(g_selected), std::end(g_selected), 0);
        std::fill(std::begin(g_scroll), std::end(g_scroll), 0);
        g_swap_source = -1;
        g_armor_selected = 0;
        g_armor_focus = false;
        g_action_feedback.clear();
        g_map_centered = false;
        g_map_zoom = 1.0;
        g_map_page_selection = -1;
        g_previous_page = -1;
        g_page_animation_direction = 0;
      }
      UpdateActionSessionFromLiveState();
      StartAssetInitialization();
      g_page_dirty = true;
    }
  } else if (state_result == LivePollResult::kNeedsResync) {
    g_action_feedback = L"STATE DELTA GAP; WAITING FOR FULL RESYNC";
    if (g_action_transport_ready && g_action_transport) {
      const SubmitResult resync = g_action_transport->QueueResync();
      if (resync.code != SubmitCode::kQueued) {
        LogRateLimited("bridge-resync-submit", 5000, LogLevel::kWarning,
                       "[STATE] resync request rejected code=%u",
                       static_cast<unsigned>(resync.code));
      }
    }
    g_page_dirty = true;
    LogRateLimited("bridge-resync", 5000, LogLevel::kWarning,
                   "[STATE] missed delta; full resync required");
  } else if (state_result == LivePollResult::kInvalidState ||
             state_result == LivePollResult::kIoError) {
    LogRateLimited("bridge-poll", 5000, LogLevel::kWarning,
                   "[STATE] poll result=%s", LivePollResultName(state_result));
  }

  if (g_have_live_state && g_last_live_publish &&
      now - g_last_live_publish > g_config.bridge_stale_ms &&
      !g_live_state_stale) {
    g_live_state_stale = true;
    if (g_action_transport) g_action_transport->ClearSession();
    g_action_feedback = L"LIVE STATE STALE; ACTIONS DISABLED";
    g_page_dirty = true;
    Log(LogLevel::kWarning, "[STATE] stale; actions disabled");
  }

  if (!g_action_transport_ready || !g_action_transport) return;
  BridgeActionAck acknowledgement;
  if (g_live_ingestor->PollAcknowledgement(&acknowledgement) ==
      LivePollResult::kPublished) {
    const AckCode result =
        g_action_transport->HandleAcknowledgement(acknowledgement);
    if (result == AckCode::kAccepted) {
      const std::wstring action = Wide(acknowledgement.action.c_str());
      const std::wstring code = Wide(acknowledgement.code.c_str());
      g_action_feedback = action + (acknowledgement.ok ? L": " : L": REJECTED / ") + code;
      g_page_dirty = true;
      Log(acknowledgement.ok ? LogLevel::kInfo : LogLevel::kWarning,
          "[ACTION] ack action=%s ok=%d code=%s",
          acknowledgement.action.c_str(), acknowledgement.ok,
          acknowledgement.code.c_str());
    }
  }
  const PumpResult pump = g_action_transport->Pump(now);
  if (pump.code == PumpCode::kWritten) {
    g_action_feedback = L"ACTION SENT; WAITING FOR GAME ACKNOWLEDGEMENT";
    g_page_dirty = true;
  } else if (pump.code == PumpCode::kDroppedStaleView) {
    g_action_feedback = L"ACTION CANCELLED: VIEW CHANGED";
    g_page_dirty = true;
  } else if (pump.code == PumpCode::kAcknowledgementTimedOut) {
    g_action_feedback = L"ACTION TIMED OUT; SESSION DISABLED";
    g_page_dirty = true;
    Log(LogLevel::kWarning, "[ACTION] acknowledgement timeout request=%llu",
        static_cast<unsigned long long>(pump.request_id));
  } else if (pump.code == PumpCode::kIoError) {
    g_action_feedback = L"ACTION I/O ERROR";
    g_page_dirty = true;
    LogRateLimited("action-io", 5000, LogLevel::kWarning,
                   "[ACTION] command write failed error=%lu",
                   static_cast<unsigned long>(pump.system_error));
  }
}

bool OpenInputSocket() {
  g_input = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_input == INVALID_SOCKET) return false;
  BOOL exclusive = TRUE;
  if (setsockopt(g_input, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                 reinterpret_cast<const char*>(&exclusive),
                 sizeof(exclusive)) == SOCKET_ERROR) {
    closesocket(g_input);
    g_input = INVALID_SOCKET;
    return false;
  }
  u_long nonblocking = 1;
  if (ioctlsocket(g_input, FIONBIO, &nonblocking) == SOCKET_ERROR) {
    closesocket(g_input);
    g_input = INVALID_SOCKET;
    return false;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(g_config.input_port);
  if (bind(g_input, reinterpret_cast<const sockaddr*>(&address),
           sizeof(address)) == SOCKET_ERROR) {
    closesocket(g_input);
    g_input = INVALID_SOCKET;
    return false;
  }
  Log(LogLevel::kInfo, "[INPUT] isolated UDP bound to 127.0.0.1:%u",
      static_cast<unsigned>(g_config.input_port));
  return true;
}

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wparam,
                            LPARAM lparam) {
  switch (message) {
    case WM_MOUSEACTIVATE: return MA_NOACTIVATEANDEAT;
    case WM_ERASEBKGND: return 1;
    case WM_CLOSE:
    case kStopMessage:
      DestroyWindow(window);
      return 0;
    case kAssetReadyMessage:
      g_page_dirty = true;
      InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_TIMER:
      if (g_stop_requested.load(std::memory_order_acquire)) {
        DestroyWindow(window);
        return 0;
      }
      PollInput();
      PumpBridge();
      if (g_page == 1 && g_have_live_state &&
          GetTickCount64() - g_last_map_animation >= 180) {
        g_last_map_animation = GetTickCount64();
        InvalidateRect(window, nullptr, FALSE);
      }
      if (g_page_dirty || g_previous_page >= 0 ||
          LatestSequence() != g_drawn_sequence)
        InvalidateRect(window, nullptr, FALSE);
      return 0;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC dc = BeginPaint(window, &paint);
      Render(window, dc);
      EndPaint(window, &paint);
      return 0;
    }
    case WM_DESTROY:
      KillTimer(window, 1);
      ReleaseDrawingResources();
      g_window.store(nullptr, std::memory_order_release);
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(window, message, wparam, lparam);
  }
}

void SignalStart(bool success) {
  g_start_success.store(success, std::memory_order_release);
  if (g_started_event) SetEvent(g_started_event);
}

DWORD WINAPI PanelThread(void*) {
  WSADATA winsock = {};
  if (WSAStartup(MAKEWORD(2, 2), &winsock) != 0) {
    Log(LogLevel::kError, "[PANEL] Winsock startup failed");
    SignalStart(false);
    return 0;
  }
  MonitorChoice monitor;
  EnumDisplayMonitors(nullptr, nullptr, FindSecondary,
                      reinterpret_cast<LPARAM>(&monitor));
  if (!monitor.found) {
    monitor.bounds.left = static_cast<LONG>(g_config.panel_x);
    monitor.bounds.top = static_cast<LONG>(g_config.panel_y);
    monitor.bounds.right = monitor.bounds.left +
        static_cast<LONG>(g_config.panel_width);
    monitor.bounds.bottom = monitor.bounds.top +
        static_cast<LONG>(g_config.panel_height);
    Log(LogLevel::kWarning,
        "[PANEL] Wine enumerated %u Win32 monitor(s); using configured "
        "spanning-root lower bounds=%ld,%ld %ux%u primary=%ld,%ld-%ld,%ld",
        static_cast<unsigned>(monitor.count), monitor.bounds.left,
        monitor.bounds.top, static_cast<unsigned>(g_config.panel_width),
        static_cast<unsigned>(g_config.panel_height),
        monitor.primary_bounds.left, monitor.primary_bounds.top,
        monitor.primary_bounds.right, monitor.primary_bounds.bottom);
  }

  WNDCLASSEXW window_class = {};
  window_class.cbSize = sizeof(window_class);
  window_class.hInstance = g_module;
  window_class.lpfnWndProc = WindowProc;
  window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  window_class.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  window_class.lpszClassName = kPanelClassName;
  if (!RegisterClassExW(&window_class)) {
    Log(LogLevel::kError, "[PANEL] class registration failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalStart(false);
    WSACleanup();
    return 0;
  }
  const int width = monitor.bounds.right - monitor.bounds.left;
  const int height = monitor.bounds.bottom - monitor.bounds.top;
  if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
      static_cast<std::uint64_t>(width) *
              static_cast<std::uint64_t>(height) >
          16ULL * 1024ULL * 1024ULL) {
    Log(LogLevel::kError,
        "[PANEL] rejected unsafe bounds=%ld,%ld %dx%d",
        monitor.bounds.left, monitor.bounds.top, width, height);
    SignalStart(false);
    WSACleanup();
    return 0;
  }
  HWND window = CreateWindowExW(
      WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, kPanelClassName, kPanelTitle,
      WS_POPUP, monitor.bounds.left, monitor.bounds.top, width, height, nullptr,
      nullptr, g_module, nullptr);
  if (!window) {
    Log(LogLevel::kError, "[PANEL] window creation failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalStart(false);
    UnregisterClassW(kPanelClassName, g_module);
    WSACleanup();
    return 0;
  }
  g_window.store(window, std::memory_order_release);
  try {
    g_live_ingestor = std::make_unique<LiveStateIngestor>(
        g_config.bridge_directory.empty()
            ? nullptr
            : g_config.bridge_directory.c_str());
    g_action_transport = std::make_unique<ActionTransport>();
    Log(LogLevel::kInfo,
        "[STATE] live bridge client initialized discovery=%s",
        g_config.bridge_directory.empty() ? "auto" : "explicit");
  } catch (const std::exception& error) {
    g_live_ingestor.reset();
    g_action_transport.reset();
    Log(LogLevel::kError, "[STATE] bridge client initialization failed: %s",
        error.what());
  } catch (...) {
    g_live_ingestor.reset();
    g_action_transport.reset();
    Log(LogLevel::kError,
        "[STATE] bridge client initialization failed with unknown exception");
  }
  if (!OpenInputSocket() ||
      !SetWindowPos(window, HWND_TOP, monitor.bounds.left, monitor.bounds.top,
                    width, height, SWP_NOACTIVATE | SWP_SHOWWINDOW) ||
      !SetTimer(window, 1, std::max<UINT>(16, 1000 / g_config.refresh_hz),
                nullptr)) {
    Log(LogLevel::kError, "[PANEL] initialization failed error=%lu",
        static_cast<unsigned long>(GetLastError()));
    SignalStart(false);
    DestroyWindow(window);
  } else {
    ShowWindow(window, SW_SHOWNOACTIVATE);
    PublishCompatibility(g_verified_identity, Lifecycle::kPanelReady,
                         "panel ready; waiting for live game bridge state");
    Log(LogLevel::kInfo,
        "[PANEL] initialized class=FieldsOfMistriaSecondScreenPanel "
        "bounds=%ld,%ld %dx%d source=%s",
        monitor.bounds.left, monitor.bounds.top, width, height,
        monitor.found ? "secondary-monitor" : "configured-spanning-root");
    SignalStart(true);
  }

  MSG message = {};
  while (GetMessageW(&message, nullptr, 0, 0) > 0) {
    TranslateMessage(&message);
    DispatchMessageW(&message);
  }
  StopAssetInitialization();
  if (g_input != INVALID_SOCKET) closesocket(g_input);
  g_input = INVALID_SOCKET;
  if (g_action_transport) g_action_transport->ClearSession();
  g_action_transport.reset();
  g_live_ingestor.reset();
  g_action_transport_ready = false;
  g_have_live_state = false;
  g_live_state_stale = false;
  g_last_bridge_poll = 0;
  g_last_live_publish = 0;
  g_last_map_animation = 0;
  WSACleanup();
  UnregisterClassW(kPanelClassName, g_module);
  Log(LogLevel::kInfo, "[PANEL] thread stopped");
  return 0;
}

}  // namespace

bool StartPanel(HMODULE module, const Config& config,
                const BuildIdentity& verified_identity) {
  if (!module || !verified_identity.supported) return false;
  if (g_thread) return g_start_success.load(std::memory_order_acquire);
  g_module = module;
  g_config = config;
  g_verified_identity = verified_identity;
  g_start_success.store(false, std::memory_order_release);
  g_stop_requested.store(false, std::memory_order_release);
  g_asset_stop.store(false, std::memory_order_release);
  g_started_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
  if (!g_started_event) return false;
  g_thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0, nullptr);
  if (!g_thread) {
    CloseHandle(g_started_event);
    g_started_event = nullptr;
    return false;
  }
  const DWORD wait = WaitForSingleObject(g_started_event, 15000);
  const bool startup_signaled = wait == WAIT_OBJECT_0;
  if (startup_signaled) {
    // SignalStart has published the result. The event is no longer accessed
    // after that single signal, so transfer and close this handle exactly once.
    HANDLE signaled_event = g_started_event;
    g_started_event = nullptr;
    CloseHandle(signaled_event);
  }
  if (startup_signaled && g_start_success.load(std::memory_order_acquire)) {
    return true;
  }
  RequestPanelStop();
  const DWORD stopped = WaitForSingleObject(g_thread, 2000);
  const StartupHandleDecision handles = DecideStartupHandles(wait, stopped);
  if (handles.close_thread_after_join) {
    CloseHandle(g_thread);
    g_thread = nullptr;
  }
  if (handles.close_event_after_join) {
    CloseHandle(g_started_event);
    g_started_event = nullptr;
  }
  if (handles.retain_thread) {
    // A timed-out startup event remains valid because SignalStart may still
    // need it. The thread handle is retained so StopPanel can later prove exit.
    Log(LogLevel::kWarning,
        "[PANEL] startup cancellation did not finish within 2 seconds; "
        "required handles retained for shutdown");
  }
  return false;
}

void RequestPanelStop() {
  SignalPanelStop();
  HWND window = g_window.load(std::memory_order_acquire);
  if (window) PostMessageW(window, kStopMessage, 0, 0);
}

void SignalPanelStop() {
  g_stop_requested.store(true, std::memory_order_release);
}

bool StopPanel(DWORD timeout_ms) {
  if (!g_thread) return true;
  RequestPanelStop();
  const DWORD wait = WaitForSingleObject(g_thread, timeout_ms);
  if (wait != WAIT_OBJECT_0) return false;
  CloseHandle(g_thread);
  g_thread = nullptr;
  if (g_started_event) CloseHandle(g_started_event);
  g_started_event = nullptr;
  return true;
}

HWND PanelWindow() { return g_window.load(std::memory_order_acquire); }

}  // namespace fomds
