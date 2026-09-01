#include <winsock2.h>
#include <windows.h>
#include <windowsx.h>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "Bridge.h"
#include "NativeAssets.h"
#include "Ui.h"
#include "../../DeadIslandSecondScreen/src/Log.h"

namespace dis {

void ModOnDxgiUsed();

namespace {

constexpr unsigned kCommandCapacity = 64;
// Native menu-model rendering is serialized by Skyrim and runs through its
// main/render thread. Keep this lane small: the UI requests the selected model
// plus at most one visible thumbnail per composition. One extra slot is
// reserved for the private world-map request.
constexpr unsigned kItemPreviewCommandCapacity = 64;
constexpr unsigned kPreviewCommandCapacity = kItemPreviewCommandCapacity + 1;
constexpr size_t kMaxPanelPixels = 16u * 1024u * 1024u;
// Each entry owns a 192x192 RGBA image. A bounded LRU comfortably retains the
// selected item and several pages of thumbnails without permanently reserving
// more than 130 MiB for every inventory and spell FormID in the snapshot.
constexpr unsigned kItemPreviewCacheCapacity = 128;
constexpr uint64_t kItemPreviewPendingWatchdogMs = 120000;
constexpr int kUiCanvasWidth = 1240;
constexpr int kUiCanvasHeight = 1080;
#ifndef SKYRIM_SECOND_SCREEN_BUILD_ID
#define SKYRIM_SECOND_SCREEN_BUILD_ID "local-" __DATE__ "-" __TIME__
#endif
constexpr const char* kProxyBuildIdentity = SKYRIM_SECOND_SCREEN_BUILD_ID;

HWND g_panel_window = nullptr;
int g_width = 0;
int g_height = 0;
int g_present_x = 0;
int g_present_y = 0;
int g_present_width = 0;
int g_present_height = 0;
std::atomic<unsigned> g_panel_thread_state{0};  // idle, starting, running, failed
std::atomic<bool> g_panel_running{false};
HANDLE g_panel_thread = nullptr;
DWORD g_panel_thread_id = 0;
HMODULE g_proxy_module = nullptr;
SRWLOCK g_panel_control_lock = SRWLOCK_INIT;
uint64_t g_last_panel_paint_ms = 0;
std::once_flag g_start_once;
std::atomic<bool> g_touch_running{false};
std::atomic<bool> g_keyboard_packet_logged{false};
std::atomic<uint64_t> g_touch_overflow_count{0};
uint64_t g_last_bad_touch_log = 0;
HANDLE g_touch_thread = nullptr;
SOCKET g_touch_socket = INVALID_SOCKET;
unsigned short g_touch_port = 7788;
SRWLOCK g_touch_feedback_lock = SRWLOCK_INIT;
sockaddr_in g_touch_feedback_peer = {};
bool g_touch_feedback_peer_valid = false;
std::atomic<uint64_t> g_touch_haptic_success_count{0};
bool g_enabled = true;
wchar_t g_ini_path[MAX_PATH] = {};

SRWLOCK g_state_lock = SRWLOCK_INIT;
Snapshot g_state = {};
bool g_have_state = false;
// Snapshot is intentionally large. Keep bridge sanitization in process-lifetime
// storage instead of placing about 700 KiB on Skyrim's PlayerCharacter::Update
// stack under Wine/Box64.
SRWLOCK g_submit_staging_lock = SRWLOCK_INIT;
Snapshot g_submit_staging = {};
bool g_submit_staging_have_state = false;
std::atomic<uint64_t> g_published_sequence{UINT64_MAX};
std::atomic<uint64_t> g_current_world_epoch{UINT64_MAX};
Snapshot g_panel_state = {};
uint64_t g_panel_state_sequence = UINT64_MAX;
bool g_panel_have_state = false;
bool g_panel_showing_loading = true;
uint64_t g_last_panel_health_log = 0;
uint64_t g_last_snapshot_log = 0;
uint64_t g_last_snapshot_sequence = UINT64_MAX;
uint64_t g_last_snapshot_epoch = UINT64_MAX;
uint32_t g_last_snapshot_in_game = UINT32_MAX;
std::atomic<uint64_t> g_rejected_snapshots{0};
uint64_t g_last_snapshot_delta_log = 0;

enum SnapshotSection : unsigned {
  kSnapshotInventorySection = 0,
  kSnapshotMagicSection,
  kSnapshotQuestSection,
  kSnapshotMapSection,
  kSnapshotSectionCount,
};

struct SnapshotDeltaCounters {
  std::atomic<uint64_t> calls{0};
  std::atomic<uint64_t> forced{0};
  std::atomic<uint64_t> scalar_bytes{0};
  std::atomic<uint64_t> section_bytes_copied{0};
  std::atomic<uint64_t> section_bytes_avoided{0};
  std::atomic<uint64_t> contract_forced{0};
  std::atomic<uint64_t> section_copies[kSnapshotSectionCount] = {};
  std::atomic<uint64_t> section_skips[kSnapshotSectionCount] = {};
};

SnapshotDeltaCounters g_submit_delta_counters;
SnapshotDeltaCounters g_state_delta_counters;
SnapshotDeltaCounters g_panel_delta_counters;
SRWLOCK g_command_lock = SRWLOCK_INIT;
Command g_commands[kCommandCapacity] = {};
unsigned g_command_read = 0;
unsigned g_command_write = 0;
unsigned g_command_count = 0;
std::atomic<unsigned> g_pending_command_count{0};
Command g_preview_commands[kPreviewCommandCapacity] = {};
// Preview completion compacts the queue on the render callback. Keep its
// scratch storage out of Skyrim's comparatively small render-thread stack.
Command g_preview_command_compaction[kPreviewCommandCapacity] = {};
unsigned g_preview_command_read = 0;
unsigned g_preview_command_write = 0;
unsigned g_preview_command_count = 0;

struct ItemPreviewCacheEntry {
  uint32_t form_id = 0;
  uint64_t world_epoch = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t last_request_ms = 0;
  uint64_t generation = 0;
  uint32_t failure_count = 0;
  bool pending = false;
  bool priority_sent = false;
  bool retry_wake_sent = false;
  uint8_t rgba[kNativeItemPreviewSize * kNativeItemPreviewSize * 4] = {};
};
SRWLOCK g_item_preview_lock = SRWLOCK_INIT;
ItemPreviewCacheEntry g_item_previews[kItemPreviewCacheCapacity] = {};
struct ItemPreviewDemand {
  uint32_t form_id = 0;
  uint64_t world_epoch = 0;
  uint64_t last_seen_ms = 0;
};
constexpr unsigned kItemPreviewDemandCapacity = 64;
ItemPreviewDemand g_item_preview_demands[kItemPreviewDemandCapacity] = {};
unsigned g_item_preview_demand_count = 0;
uint64_t g_next_background_preview_dispatch_ms = 0;
uint64_t g_item_preview_lru_clock = 0;
uint64_t g_item_preview_demand_composition = 0;
std::atomic<uint64_t> g_item_preview_generation{0};
uint64_t g_panel_item_preview_generation = 0;
uint64_t g_next_item_preview_retry_scan_ms = 0;
uint8_t g_item_preview_draw_rgba[
    kNativeItemPreviewSize * kNativeItemPreviewSize * 4] = {};

// The custom character renderer lives in the SKSE plugin and publishes only
// the pixels from its private D3D11 render target.  Keeping the bridge cache in
// the proxy prevents either side from sharing a texture, device context, or
// framebuffer lifetime with Skyrim's main renderer.
struct CharacterPreviewCache {
  uint32_t width = 0;
  uint32_t height = 0;
  uint64_t source_revision = 0;
  bool ready = false;
  uint8_t rgba[kCharacterPreviewMaxWidth * kCharacterPreviewMaxHeight * 4] = {};
};
SRWLOCK g_character_preview_lock = SRWLOCK_INIT;
CharacterPreviewCache g_character_preview = {};
uint8_t g_character_preview_draw_rgba[
    kCharacterPreviewMaxWidth * kCharacterPreviewMaxHeight * 4] = {};
std::atomic<uint64_t> g_character_preview_generation{0};
uint64_t g_panel_character_preview_generation = 0;

// Skyrim's native world-map pass publishes CPU pixels only after its private
// render target has completed. Keep the projected markers in the same cache so
// they can never drift away from the exact NiCamera frame that produced them.
struct MapPreviewCache {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t world_id = 0;
  uint32_t marker_count = 0;
  uint64_t source_revision = 0;
  bool ready = false;
  ProjectedMapMarker markers[kMaxMapMarkers] = {};
  uint8_t rgba[kNativeMapMaxWidth * kNativeMapMaxHeight * 4] = {};
};
SRWLOCK g_map_preview_lock = SRWLOCK_INIT;
MapPreviewCache g_map_preview = {};
uint8_t g_map_preview_draw_rgba[
    kNativeMapMaxWidth * kNativeMapMaxHeight * 4] = {};
std::atomic<uint64_t> g_map_preview_generation{0};
uint64_t g_panel_map_preview_generation = 0;
uint64_t g_map_frame_count = 0;
uint64_t g_last_map_frame_log_ms = 0;

bool g_native_item_models_enabled = true;
bool g_native_character_preview_enabled = true;
bool g_native_map_enabled = true;
// Retain the map implementation, but disconnect its UI callback and native
// request path until the feature is deliberately re-enabled.
constexpr bool kNativeMapTemporarilyDisabled = true;

enum class Gesture : uint8_t { kTap, kHold, kDown, kMove, kUp, kPinch, kCancel };
struct TouchEvent { Gesture kind; float x; float y; float value; };
constexpr unsigned kTouchCapacity = 128;
SRWLOCK g_touch_lock = SRWLOCK_INIT;
TouchEvent g_touch[kTouchCapacity] = {};
unsigned g_touch_read = 0, g_touch_write = 0, g_touch_count = 0;
uint64_t g_last_pointer_up_ms = 0;
float g_last_pointer_up_x = -1.0f;
float g_last_pointer_up_y = -1.0f;

float FiniteOrZero(float value) { return std::isfinite(value) ? value : 0.0f; }

Box AspectFit(const Box& destination, int source_width, int source_height) {
  if (destination.w <= 0 || destination.h <= 0 || source_width <= 0 ||
      source_height <= 0) return {};
  const double source_aspect =
      static_cast<double>(source_width) / source_height;
  const double target_aspect =
      static_cast<double>(destination.w) / destination.h;
  if (source_aspect > target_aspect) {
    const int height = std::max(
        1, static_cast<int>(std::lround(destination.w / source_aspect)));
    return Box(destination.x, destination.y + (destination.h - height) / 2,
               destination.w, height);
  }
  const int width = std::max(
      1, static_cast<int>(std::lround(destination.h * source_aspect)));
  return Box(destination.x + (destination.w - width) / 2, destination.y,
             width, destination.h);
}

bool DrawCachedCharacterPreview(Canvas& canvas, const Box& destination) {
  uint32_t width = 0;
  uint32_t height = 0;
  AcquireSRWLockShared(&g_character_preview_lock);
  if (g_character_preview.ready && g_character_preview.width &&
      g_character_preview.height) {
    width = g_character_preview.width;
    height = g_character_preview.height;
    std::memcpy(g_character_preview_draw_rgba, g_character_preview.rgba,
                static_cast<size_t>(width) * height * 4);
  }
  ReleaseSRWLockShared(&g_character_preview_lock);
  if (!width || !height) return false;
  const Box fitted = AspectFit(destination, static_cast<int>(width),
                               static_cast<int>(height));
  canvas.BlitRgba(fitted, g_character_preview_draw_rgba,
                  static_cast<int>(width), static_cast<int>(height),
                  Box(0, 0, static_cast<int>(width),
                      static_cast<int>(height)));
  return true;
}

bool DrawCachedMapPreview(Canvas& canvas, const Box& destination, float,
                          float, float) {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t world_id = 0;
  uint32_t marker_count = 0;
  ProjectedMapMarker markers[kMaxMapMarkers] = {};
  AcquireSRWLockShared(&g_map_preview_lock);
  if (g_map_preview.ready && g_map_preview.width && g_map_preview.height) {
    width = g_map_preview.width;
    height = g_map_preview.height;
    world_id = g_map_preview.world_id;
    marker_count = std::min(g_map_preview.marker_count, kMaxMapMarkers);
    std::memcpy(g_map_preview_draw_rgba, g_map_preview.rgba,
                static_cast<size_t>(width) * height * 4);
    std::memcpy(markers, g_map_preview.markers,
                static_cast<size_t>(marker_count) * sizeof(markers[0]));
  }
  ReleaseSRWLockShared(&g_map_preview_lock);
  if (!width || !height) return false;

  uint32_t current_world = 0;
  AcquireSRWLockShared(&g_state_lock);
  if (g_have_state && g_state.in_game) current_world = g_state.world_form_id;
  ReleaseSRWLockShared(&g_state_lock);
  if (!current_world || current_world != world_id) return false;

  const Box fitted = AspectFit(destination, static_cast<int>(width),
                               static_cast<int>(height));
  canvas.BlitRgba(fitted, g_map_preview_draw_rgba,
                  static_cast<int>(width), static_cast<int>(height),
                  Box(0, 0, static_cast<int>(width),
                      static_cast<int>(height)));
  canvas.SetClip(fitted);
  const int marker_height = std::clamp(fitted.h / 24, 28, 46);
  for (uint32_t i = 0; i < marker_count; ++i) {
    const ProjectedMapMarker& marker = markers[i];
    if ((marker.flags & kMapMarkerVisible) == 0 ||
        !std::isfinite(marker.screen_x) ||
        !std::isfinite(marker.screen_y) || marker.screen_x < 0.0f ||
        marker.screen_x > 1.0f || marker.screen_y < 0.0f ||
        marker.screen_y > 1.0f)
      continue;
    const int x = fitted.x + static_cast<int>(
        std::lround(marker.screen_x * fitted.w));
    const int y = fitted.y + static_cast<int>(
        std::lround(marker.screen_y * fitted.h));
    if (marker.type >= 1 && marker.type <= 59) {
      int source_w = 28;
      int source_h = 41;
      TheNativeAssets().LocationMarkerDimensions(marker.type, &source_w,
                                                  &source_h);
      const int marker_width = std::max(
          1, marker_height * source_w / std::max(1, source_h));
      TheNativeAssets().DrawLocationMarker(
          canvas, Box(x - marker_width / 2, y - marker_height,
                      marker_width, marker_height),
          marker.type, (marker.flags & kMapMarkerFastTravel) != 0,
          Color(255, 255, 255, 255));
    } else if (marker.type >= 62 && marker.type <= 64) {
      const int icon_height = marker_height + 6;
      const int source_w = marker.type == 64 ? 31 : 23;
      const int source_h = marker.type == 63 ? 51 : 42;
      const int icon_width = std::max(1, icon_height * source_w / source_h);
      TheNativeAssets().DrawQuestMarker(
          canvas, Box(x - icon_width / 2, y - icon_height,
                      icon_width, icon_height),
          marker.type, Color(255, 255, 255, 255));
    } else if (marker.type == 65) {
      const int icon_height = marker_height + 6;
      const int icon_width = std::max(20, icon_height * 23 / 43);
      TheNativeAssets().DrawPlayerSetMarker(
          canvas, Box(x - icon_width / 2, y - icon_height,
                      icon_width, icon_height),
          Color(255, 255, 255, 255));
    } else if (marker.type == 66) {
      const int side = marker_height + 10;
      TheNativeAssets().DrawMarker(
          canvas, Box(x - side / 2, y - side / 2, side, side),
          Color(255, 255, 255, 255), marker.rotation_radians);
    }
  }
  canvas.ClearClip();
  return true;
}

ItemPreviewCacheEntry* FindItemPreview(uint32_t form_id,
                                       uint64_t world_epoch) {
  if (!form_id) return nullptr;
  for (auto& entry : g_item_previews)
    if (entry.form_id == form_id && entry.world_epoch == world_epoch)
      return &entry;
  return nullptr;
}

bool DrawCachedItemPreview(Canvas& canvas, const Box& destination,
                           uint32_t form_id, uint64_t world_epoch) {
  bool drawn = false;
  uint32_t width = 0;
  uint32_t height = 0;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  ItemPreviewCacheEntry* entry = FindItemPreview(form_id, world_epoch);
  if (entry && !entry->pending && entry->width && entry->height) {
    width = entry->width;
    height = entry->height;
    std::memcpy(g_item_preview_draw_rgba, entry->rgba,
                static_cast<size_t>(width) * height * 4);
    // Drawing is a real LRU touch. Selected detail cards and hotbar slots are
    // therefore naturally retained without a separate pin list or duplicate
    // asset copy.
    entry->generation = ++g_item_preview_lru_clock;
    drawn = true;
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  if (drawn) {
    const Box fitted = AspectFit(destination, static_cast<int>(width),
                                 static_cast<int>(height));
    canvas.BlitRgba(fitted, g_item_preview_draw_rgba,
                    static_cast<int>(width), static_cast<int>(height),
                    Box(0, 0, static_cast<int>(width),
                        static_cast<int>(height)));
  }
  return drawn;
}

void RequestCachedItemPreviewNow(uint32_t form_id, uint64_t world_epoch,
                                 bool priority) {
  if (!form_id || world_epoch == UINT64_MAX) return;
  const uint64_t now = GetTickCount64();
  bool queue = false;
  uint32_t evicted_form = 0;
  bool retry = false;
  bool priority_only = false;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  ItemPreviewCacheEntry* entry = FindItemPreview(form_id, world_epoch);
  retry = entry && entry->failure_count != 0;
  if (entry && !entry->pending && entry->width && entry->height) {
    ReleaseSRWLockExclusive(&g_item_preview_lock);
    return;
  }
  if (entry && entry->pending &&
      now - entry->last_request_ms >= kItemPreviewPendingWatchdogMs) {
    const uint64_t age = now - entry->last_request_ms;
    entry->pending = false;
    entry->width = entry->height = 0;
    entry->last_request_ms = now;
    entry->failure_count = std::min<uint32_t>(entry->failure_count + 1, 16);
    entry->priority_sent = false;
    entry->retry_wake_sent = false;
    entry->generation = ++g_item_preview_lru_clock;
    g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
    g_next_background_preview_dispatch_ms = now + 250;
    ReleaseSRWLockExclusive(&g_item_preview_lock);
    DIS_WARN("[item-preview] request watchdog released form=%08X age=%llums",
             form_id, static_cast<unsigned long long>(age));
    return;
  }
  if (entry && !entry->pending && !entry->width && entry->failure_count) {
    // A failed native render used to be requeued every five seconds forever.
    // Apart from log spam this kept Skyrim's Inventory3DManager and GPU busy
    // even while the user stayed on one page. Retry slowly, with a bounded
    // backoff, while the vector fallback remains visible.
    constexpr uint64_t kPriorityRetryMs[] = {2000, 8000, 30000, 120000};
    const uint64_t retry_delay = priority
        ? kPriorityRetryMs[std::min<uint32_t>(entry->failure_count - 1, 3)]
        : std::min<uint64_t>(
              120000,
              15000ull << std::min<uint32_t>(entry->failure_count - 1, 3));
    if (now - entry->last_request_ms < retry_delay) {
      ReleaseSRWLockExclusive(&g_item_preview_lock);
      return;
    }
  }
  if (entry && entry->pending &&
      now - entry->last_request_ms < kItemPreviewPendingWatchdogMs) {
    // Promote a background demand exactly once when it becomes selected.
    // Repaint-driven 750 ms wakes previously kept recycling the same FormID
    // through the command/player-update path while its native render was
    // already active.
    if (!priority || entry->priority_sent) {
      ReleaseSRWLockExclusive(&g_item_preview_lock);
      return;
    }
    entry->priority_sent = true;
    queue = true;
    priority_only = true;
  }
  if (!priority_only && entry && entry->pending) retry = true;
  if (!priority_only && !entry) {
    for (auto& candidate : g_item_previews) {
      if (!candidate.form_id) {
        entry = &candidate;
        break;
      }
    }
  }
  if (!priority_only && !entry) {
    ItemPreviewCacheEntry* oldest_ready = nullptr;
    for (auto& candidate : g_item_previews) {
      if (!candidate.pending &&
          (!oldest_ready || candidate.generation < oldest_ready->generation))
        oldest_ready = &candidate;
    }
    entry = oldest_ready;
  }
  if (!priority_only && entry) {
    if (entry->form_id && entry->form_id != form_id) {
      evicted_form = entry->form_id;
      entry->failure_count = 0;
    }
    entry->form_id = form_id;
    entry->world_epoch = world_epoch;
    entry->width = 0;
    entry->height = 0;
    entry->pending = true;
    entry->retry_wake_sent = true;
    entry->last_request_ms = now;
    entry->priority_sent = priority;
    queue = true;
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  if (queue) {
    DIS_INFO("[item-preview] UI request form=%08X retry=%u evicted=%08X",
             form_id, retry ? 1u : 0u, evicted_form);
    Command command;
    command.type = CommandType::kRequestItemPreview;
    command.form_id = form_id;
    if (priority) command.flags |= kCommandPreviewPriority;
    command.world_epoch = world_epoch;
    if (!PushUiCommand(command)) {
      if (priority_only) {
        AcquireSRWLockExclusive(&g_item_preview_lock);
        if (ItemPreviewCacheEntry* pending =
                FindItemPreview(form_id, world_epoch);
            pending && pending->pending)
          pending->priority_sent = false;
        ReleaseSRWLockExclusive(&g_item_preview_lock);
        DIS_WARN("[item-preview] priority promotion dropped form=%08X",
                 form_id);
        return;
      }
      // The cache entry was marked pending before entering the bounded command
      // lane. A rejected command can never receive a completion callback, so
      // roll it back immediately instead of displaying a watchdog-length phantom
      // pending state.
      AcquireSRWLockExclusive(&g_item_preview_lock);
      if (ItemPreviewCacheEntry* pending =
              FindItemPreview(form_id, world_epoch);
          pending && pending->pending) {
        pending->pending = false;
        pending->last_request_ms = now;
        pending->failure_count = std::min<uint32_t>(
            pending->failure_count + 1, 4);
        pending->priority_sent = false;
        pending->retry_wake_sent = false;
        pending->generation = ++g_item_preview_lru_clock;
        g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
        g_next_background_preview_dispatch_ms = now + 250;
      }
      ReleaseSRWLockExclusive(&g_item_preview_lock);
      DIS_WARN("[item-preview] UI request dropped; pending cleared form=%08X",
               form_id);
    }
  } else {
    static uint64_t last_saturation_log = 0;
    if (now - last_saturation_log >= 5000) {
      DIS_WARN("[item-preview] UI cache saturated; request dropped form=%08X",
               form_id);
      last_saturation_log = now;
    }
  }
}

void BeginCachedItemPreviewComposition(uint64_t world_epoch,
                                       uint64_t composition_revision) {
  if (!composition_revision) return;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  for (auto& demand : g_item_preview_demands) demand = {};
  g_item_preview_demand_count = 0;
  g_item_preview_demand_composition = composition_revision;
  // The epoch is checked again when each demand is admitted and dispatched.
  (void)world_epoch;
  ReleaseSRWLockExclusive(&g_item_preview_lock);
}

void RequestCachedItemPreview(uint32_t form_id, uint64_t world_epoch,
                              bool priority,
                              uint64_t composition_revision) {
  if (!form_id || world_epoch == UINT64_MAX) return;
  if (priority) {
    RequestCachedItemPreviewNow(form_id, world_epoch, true);
    return;
  }
  const uint64_t now = GetTickCount64();
  AcquireSRWLockExclusive(&g_item_preview_lock);
  if (!composition_revision ||
      composition_revision != g_item_preview_demand_composition) {
    ReleaseSRWLockExclusive(&g_item_preview_lock);
    return;
  }
  ItemPreviewCacheEntry* cached = FindItemPreview(form_id, world_epoch);
  if (cached && !cached->pending && cached->width && cached->height) {
    ReleaseSRWLockExclusive(&g_item_preview_lock);
    return;
  }
  for (unsigned i = 0; i < g_item_preview_demand_count; ++i) {
    ItemPreviewDemand& demand = g_item_preview_demands[i];
    if (demand.form_id == form_id && demand.world_epoch == world_epoch) {
      demand.last_seen_ms = now;
      ReleaseSRWLockExclusive(&g_item_preview_lock);
      return;
    }
  }
  if (g_item_preview_demand_count < kItemPreviewDemandCapacity) {
    g_item_preview_demands[g_item_preview_demand_count++] =
        {form_id, world_epoch, now};
  } else {
    // Replace the stalest off-page demand. The current composition refreshes
    // every visible/hotbar demand timestamp, so active UI assets win without
    // growing an unbounded native queue.
    unsigned stalest = 0;
    for (unsigned i = 1; i < g_item_preview_demand_count; ++i)
      if (g_item_preview_demands[i].last_seen_ms <
          g_item_preview_demands[stalest].last_seen_ms)
        stalest = i;
    g_item_preview_demands[stalest] = {form_id, world_epoch, now};
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
}

void ServiceBackgroundItemPreviewDemand(uint64_t now) {
  const uint64_t current_epoch =
      g_current_world_epoch.load(std::memory_order_acquire);
  uint32_t form_id = 0;
  uint64_t world_epoch = 0;
  uint32_t timed_out_form = 0;
  uint64_t timed_out_age = 0;
  bool active_pending = false;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  for (auto& entry : g_item_previews) {
    if (!entry.pending) continue;
    const uint64_t age = now - entry.last_request_ms;
    if (age < kItemPreviewPendingWatchdogMs) {
      active_pending = true;
      continue;
    }
    // Queueing, native-menu ownership and safe unload can legitimately outlive
    // the bounded capture window. Use a conservative two-minute watchdog: it
    // breaks a truly orphaned pending entry without treating a slow but valid
    // translated transaction as failed and adding retry churn.
    if (!timed_out_form) {
      timed_out_form = entry.form_id;
      timed_out_age = age;
    }
    entry.pending = false;
    entry.width = entry.height = 0;
    entry.last_request_ms = now;
    entry.failure_count = std::min<uint32_t>(entry.failure_count + 1, 16);
    entry.priority_sent = false;
    entry.retry_wake_sent = false;
    entry.generation = ++g_item_preview_lru_clock;
    g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
    g_next_background_preview_dispatch_ms = now + 250;
  }
  if (!active_pending && now >= g_next_background_preview_dispatch_ms) {
    while (g_item_preview_demand_count) {
    unsigned newest = 0;
    for (unsigned i = 1; i < g_item_preview_demand_count; ++i)
      if (g_item_preview_demands[i].last_seen_ms >
          g_item_preview_demands[newest].last_seen_ms)
        newest = i;
    const ItemPreviewDemand demand = g_item_preview_demands[newest];
    g_item_preview_demands[newest] =
        g_item_preview_demands[--g_item_preview_demand_count];
    if (!demand.form_id || demand.world_epoch != current_epoch) continue;
    ItemPreviewCacheEntry* cached =
        FindItemPreview(demand.form_id, demand.world_epoch);
    if (cached && !cached->pending && cached->width && cached->height)
      continue;
    form_id = demand.form_id;
    world_epoch = demand.world_epoch;
    break;
    }
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  if (timed_out_form)
    DIS_WARN("[item-preview] pending watchdog released form=%08X age=%llums",
             timed_out_form,
             static_cast<unsigned long long>(timed_out_age));
  if (form_id)
    RequestCachedItemPreviewNow(form_id, world_epoch, false);
}

bool WakeDueItemPreviewRetry(uint64_t now) {
  bool wake = false;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  for (auto& entry : g_item_previews) {
    if (!entry.form_id || entry.pending || entry.width || entry.height ||
        !entry.failure_count || entry.retry_wake_sent)
      continue;
    const uint64_t retry_delay = std::min<uint64_t>(
        120000, 15000ull << std::min<uint32_t>(entry.failure_count - 1, 3));
    if (now - entry.last_request_ms < retry_delay) continue;
    entry.retry_wake_sent = true;
    wake = true;
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  return wake;
}

const char* LoadingReasonName(LoadingReason reason) {
  switch (reason) {
    case LoadingReason::kStartup: return "startup";
    case LoadingReason::kPreLoadGame: return "pre-load";
    case LoadingReason::kPostLoadFailed: return "post-load-failed";
    case LoadingReason::kNewGame: return "new-game";
    default: return "none";
  }
}

bool ValidLoadingReason(LoadingReason reason) {
  const uint32_t value = static_cast<uint32_t>(reason);
  return value <= static_cast<uint32_t>(LoadingReason::kNewGame);
}

bool ExplicitLoadingReason(LoadingReason reason) {
  return reason == LoadingReason::kStartup ||
         reason == LoadingReason::kPreLoadGame ||
         reason == LoadingReason::kPostLoadFailed ||
         reason == LoadingReason::kNewGame;
}

void ClearWorldBoundCaches() {
  AcquireSRWLockExclusive(&g_item_preview_lock);
  for (auto& entry : g_item_previews) entry = {};
  for (auto& demand : g_item_preview_demands) demand = {};
  g_item_preview_demand_count = 0;
  g_next_background_preview_dispatch_ms = 0;
  g_item_preview_lru_clock = 0;
  g_item_preview_demand_composition = 0;
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
  g_next_item_preview_retry_scan_ms = 0;

  AcquireSRWLockExclusive(&g_character_preview_lock);
  g_character_preview = {};
  ReleaseSRWLockExclusive(&g_character_preview_lock);
  g_character_preview_generation.fetch_add(1, std::memory_order_acq_rel);

  AcquireSRWLockExclusive(&g_map_preview_lock);
  g_map_preview = {};
  ReleaseSRWLockExclusive(&g_map_preview_lock);
  g_map_preview_generation.fetch_add(1, std::memory_order_acq_rel);

  AcquireSRWLockExclusive(&g_command_lock);
  for (auto& command : g_commands) command = {};
  for (auto& command : g_preview_commands) command = {};
  g_command_read = g_command_write = g_command_count = 0;
  g_preview_command_read = g_preview_command_write =
      g_preview_command_count = 0;
  g_pending_command_count.store(0, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_command_lock);
}

void ClearMapWorldspaceCache() {
  // A worldspace transition can happen without a save/world epoch change.
  // Discard only native-map commands; inventory and explicit player actions
  // remain valid in the same save.
  unsigned retained_count = 0;
  AcquireSRWLockExclusive(&g_command_lock);
  for (unsigned i = 0; i < g_preview_command_count; ++i) {
    const unsigned at =
        (g_preview_command_read + i) % kPreviewCommandCapacity;
    if (g_preview_commands[at].type == CommandType::kRequestMapPreview)
      continue;
    g_preview_command_compaction[retained_count++] =
        g_preview_commands[at];
  }
  for (auto& command : g_preview_commands) command = {};
  for (unsigned i = 0; i < retained_count; ++i)
    g_preview_commands[i] = g_preview_command_compaction[i];
  g_preview_command_read = 0;
  g_preview_command_count = retained_count;
  g_preview_command_write = retained_count % kPreviewCommandCapacity;
  g_pending_command_count.store(g_command_count + g_preview_command_count,
                                 std::memory_order_release);
  ReleaseSRWLockExclusive(&g_command_lock);
  AcquireSRWLockExclusive(&g_map_preview_lock);
  g_map_preview = {};
  ReleaseSRWLockExclusive(&g_map_preview_lock);
  g_map_preview_generation.fetch_add(1, std::memory_order_acq_rel);
}

constexpr size_t kSnapshotScalarBytes = offsetof(Snapshot, items);
constexpr size_t kInventorySectionBytes = sizeof(Snapshot::items);
constexpr size_t kMagicSectionBytes =
    sizeof(Snapshot::magic) + sizeof(Snapshot::favorites);
constexpr size_t kQuestSectionBytes = sizeof(Snapshot::quests);
constexpr size_t kMapSectionBytes = sizeof(Snapshot::map_markers);

struct SnapshotDeltaBaseline {
  uint64_t world_epoch = 0;
  uint64_t inventory_revision = 0;
  uint64_t magic_revision = 0;
  uint64_t quest_revision = 0;
  uint64_t map_revision = 0;
  uint32_t in_game = 0;
  uint32_t item_count = 0;
  uint32_t magic_count = 0;
  uint32_t quest_count = 0;
  uint32_t favorite_count = 0;
  uint32_t map_marker_count = 0;
};

void CopySnapshotScalars(Snapshot& destination, const Snapshot& source) {
  destination.version = source.version;
  destination.size = source.size;
  destination.sequence = source.sequence;
  destination.in_game = source.in_game;
  destination.paused = source.paused;
  destination.world_epoch = source.world_epoch;
  destination.loading_reason = source.loading_reason;
  destination.reserved_lifecycle = source.reserved_lifecycle;
  destination.health = source.health;
  destination.max_health = source.max_health;
  destination.magicka = source.magicka;
  destination.max_magicka = source.max_magicka;
  destination.stamina = source.stamina;
  destination.max_stamina = source.max_stamina;
  destination.carry = source.carry;
  destination.max_carry = source.max_carry;
  destination.level_xp = source.level_xp;
  destination.level_xp_max = source.level_xp_max;
  destination.level = source.level;
  destination.perk_points = source.perk_points;
  destination.gold = source.gold;
  destination.armor_rating = source.armor_rating;
  destination.damage_rating = source.damage_rating;
  destination.player_x = source.player_x;
  destination.player_y = source.player_y;
  destination.player_z = source.player_z;
  destination.heading = source.heading;
  destination.world_form_id = source.world_form_id;
  std::memcpy(destination.location, source.location,
              sizeof(destination.location));
  destination.item_count = source.item_count;
  destination.magic_count = source.magic_count;
  destination.quest_count = source.quest_count;
  destination.favorite_count = source.favorite_count;
  destination.map_marker_count = source.map_marker_count;
  destination.reserved = source.reserved;
  destination.inventory_revision = source.inventory_revision;
  destination.magic_revision = source.magic_revision;
  destination.quest_revision = source.quest_revision;
  destination.map_revision = source.map_revision;
}

void CountSnapshotSection(SnapshotDeltaCounters& counters,
                          SnapshotSection section, size_t bytes,
                          bool copied, bool contract_forced) {
  if (copied) {
    counters.section_copies[section].fetch_add(1, std::memory_order_relaxed);
    counters.section_bytes_copied.fetch_add(bytes, std::memory_order_relaxed);
    if (contract_forced)
      counters.contract_forced.fetch_add(1, std::memory_order_relaxed);
  } else {
    counters.section_skips[section].fetch_add(1, std::memory_order_relaxed);
    counters.section_bytes_avoided.fetch_add(bytes, std::memory_order_relaxed);
  }
}

uint32_t CopySnapshotDelta(Snapshot& destination, const Snapshot& source,
                           bool destination_valid, bool force_requested,
                           SnapshotDeltaCounters& counters) {
  const SnapshotDeltaBaseline before = {
      destination.world_epoch,
      destination.inventory_revision,
      destination.magic_revision,
      destination.quest_revision,
      destination.map_revision,
      destination.in_game,
      destination.item_count,
      destination.magic_count,
      destination.quest_count,
      destination.favorite_count,
      destination.map_marker_count,
  };
  const bool force = force_requested || !destination_valid ||
                     source.world_epoch != before.world_epoch ||
                     !source.in_game || !before.in_game;

  // Snapshot has no scalar fields after its first bounded array. This prefix
  // is only a few hundred bytes and is intentionally refreshed on every
  // publish; the four large sections below are revision-gated.
  CopySnapshotScalars(destination, source);
  counters.calls.fetch_add(1, std::memory_order_relaxed);
  counters.scalar_bytes.fetch_add(kSnapshotScalarBytes,
                                  std::memory_order_relaxed);
  if (force) counters.forced.fetch_add(1, std::memory_order_relaxed);
  uint32_t copied_sections = 0;

  bool contract_forced = !force &&
      source.inventory_revision == before.inventory_revision &&
      source.item_count != before.item_count;
  bool copied = force ||
                source.inventory_revision != before.inventory_revision ||
                contract_forced;
  if (copied)
    std::memcpy(destination.items, source.items, sizeof(destination.items));
  if (copied) copied_sections |= 1u << kSnapshotInventorySection;
  CountSnapshotSection(counters, kSnapshotInventorySection,
                       kInventorySectionBytes, copied, contract_forced);

  contract_forced = !force &&
      source.magic_revision == before.magic_revision &&
      (source.magic_count != before.magic_count ||
       source.favorite_count != before.favorite_count);
  copied = force || source.magic_revision != before.magic_revision ||
           contract_forced;
  if (copied) {
    std::memcpy(destination.magic, source.magic, sizeof(destination.magic));
    std::memcpy(destination.favorites, source.favorites,
                sizeof(destination.favorites));
    copied_sections |= 1u << kSnapshotMagicSection;
  }
  CountSnapshotSection(counters, kSnapshotMagicSection,
                       kMagicSectionBytes, copied, contract_forced);

  contract_forced = !force &&
      source.quest_revision == before.quest_revision &&
      source.quest_count != before.quest_count;
  copied = force || source.quest_revision != before.quest_revision ||
           contract_forced;
  if (copied)
    std::memcpy(destination.quests, source.quests,
                sizeof(destination.quests));
  if (copied) copied_sections |= 1u << kSnapshotQuestSection;
  CountSnapshotSection(counters, kSnapshotQuestSection,
                       kQuestSectionBytes, copied, contract_forced);

  contract_forced = !force &&
      source.map_revision == before.map_revision &&
      source.map_marker_count != before.map_marker_count;
  copied = force || source.map_revision != before.map_revision ||
           contract_forced;
  if (copied)
    std::memcpy(destination.map_markers, source.map_markers,
                sizeof(destination.map_markers));
  if (copied) copied_sections |= 1u << kSnapshotMapSection;
  CountSnapshotSection(counters, kSnapshotMapSection,
                       kMapSectionBytes, copied, contract_forced);
  return copied_sections;
}

uint64_t SnapshotCounterValue(const std::atomic<uint64_t>& counter) {
  return counter.load(std::memory_order_relaxed);
}

void MaybeLogSnapshotDeltaCounters(uint64_t now) {
  if (now - g_last_snapshot_delta_log < 10000) return;
  g_last_snapshot_delta_log = now;
  SnapshotDeltaCounters* lanes[] = {
      &g_submit_delta_counters, &g_state_delta_counters,
      &g_panel_delta_counters};
  uint64_t copied[kSnapshotSectionCount] = {};
  uint64_t skipped[kSnapshotSectionCount] = {};
  uint64_t scalar_bytes = 0;
  uint64_t section_bytes_copied = 0;
  uint64_t section_bytes_avoided = 0;
  uint64_t contract_forced = 0;
  for (SnapshotDeltaCounters* lane : lanes) {
    scalar_bytes += SnapshotCounterValue(lane->scalar_bytes);
    section_bytes_copied +=
        SnapshotCounterValue(lane->section_bytes_copied);
    section_bytes_avoided +=
        SnapshotCounterValue(lane->section_bytes_avoided);
    contract_forced += SnapshotCounterValue(lane->contract_forced);
    for (unsigned section = 0; section < kSnapshotSectionCount; ++section) {
      copied[section] += SnapshotCounterValue(lane->section_copies[section]);
      skipped[section] += SnapshotCounterValue(lane->section_skips[section]);
    }
  }
  DIS_INFO(
      "[bridge-perf] snapshot-delta calls={submit:%llu state:%llu panel:%llu} "
      "forced={submit:%llu state:%llu panel:%llu} "
      "copies={inventory:%llu magic:%llu quest:%llu map:%llu} "
      "skips={inventory:%llu magic:%llu quest:%llu map:%llu} "
      "bytes={scalar:%llu section_copied:%llu avoided:%llu} "
      "contract_forced=%llu",
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_submit_delta_counters.calls)),
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_state_delta_counters.calls)),
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_panel_delta_counters.calls)),
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_submit_delta_counters.forced)),
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_state_delta_counters.forced)),
      static_cast<unsigned long long>(
          SnapshotCounterValue(g_panel_delta_counters.forced)),
      static_cast<unsigned long long>(copied[kSnapshotInventorySection]),
      static_cast<unsigned long long>(copied[kSnapshotMagicSection]),
      static_cast<unsigned long long>(copied[kSnapshotQuestSection]),
      static_cast<unsigned long long>(copied[kSnapshotMapSection]),
      static_cast<unsigned long long>(skipped[kSnapshotInventorySection]),
      static_cast<unsigned long long>(skipped[kSnapshotMagicSection]),
      static_cast<unsigned long long>(skipped[kSnapshotQuestSection]),
      static_cast<unsigned long long>(skipped[kSnapshotMapSection]),
      static_cast<unsigned long long>(scalar_bytes),
      static_cast<unsigned long long>(section_bytes_copied),
      static_cast<unsigned long long>(section_bytes_avoided),
      static_cast<unsigned long long>(contract_forced));
}

void SanitizeSnapshot(Snapshot& state, uint32_t copied_sections) {
  state.in_game = state.in_game ? 1u : 0u;
  state.paused = state.paused ? 1u : 0u;
  state.item_count = std::min(state.item_count, kMaxItems);
  state.magic_count = std::min(state.magic_count, kMaxMagic);
  state.quest_count = std::min(state.quest_count, kMaxQuests);
  state.favorite_count = std::min(state.favorite_count, kMaxFavorites);
  state.map_marker_count = std::min(state.map_marker_count, kMaxMapMarkers);

  float* values[] = {
      &state.health, &state.max_health, &state.magicka, &state.max_magicka,
      &state.stamina, &state.max_stamina, &state.carry, &state.max_carry,
      &state.level_xp, &state.level_xp_max, &state.armor_rating,
      &state.damage_rating, &state.player_x, &state.player_y, &state.player_z,
      &state.heading,
  };
  for (float* value : values) *value = FiniteOrZero(*value);
  state.location[sizeof(state.location) - 1] = 0;

  if (copied_sections & (1u << kSnapshotInventorySection)) {
    for (uint32_t i = 0; i < state.item_count; ++i) {
      ItemState& item = state.items[i];
      item.weight = FiniteOrZero(item.weight);
      item.damage = FiniteOrZero(item.damage);
      item.armor = FiniteOrZero(item.armor);
      if (static_cast<uint32_t>(item.equip_slot) >
          static_cast<uint32_t>(EquipmentSlot::kOther)) {
        item.equip_slot = EquipmentSlot::kNone;
      }
      if (static_cast<uint32_t>(item.icon_kind) >
          static_cast<uint32_t>(ItemIconKind::kBook)) {
        item.icon_kind = ItemIconKind::kMisc;
      }
      item.name[sizeof(item.name) - 1] = 0;
      item.description[sizeof(item.description) - 1] = 0;
    }
  }
  if (copied_sections & (1u << kSnapshotMagicSection)) {
    for (uint32_t i = 0; i < state.magic_count; ++i) {
      state.magic[i].cost = FiniteOrZero(state.magic[i].cost);
      state.magic[i].name[sizeof(state.magic[i].name) - 1] = 0;
      state.magic[i].description[sizeof(state.magic[i].description) - 1] = 0;
    }
  }
  if (copied_sections & (1u << kSnapshotQuestSection)) {
    for (uint32_t i = 0; i < state.quest_count; ++i) {
      QuestState& quest = state.quests[i];
      quest.objective_stored = std::min<uint16_t>(
          quest.objective_stored,
          std::min<uint16_t>(quest.objective_count,
                             static_cast<uint16_t>(kMaxQuestObjectives)));
      quest.name[sizeof(quest.name) - 1] = 0;
      quest.objective[sizeof(quest.objective) - 1] = 0;
      quest.lore[sizeof(quest.lore) - 1] = 0;
      for (uint16_t objective = 0; objective < quest.objective_stored;
           ++objective) {
        QuestObjectiveState& entry = quest.objectives[objective];
        if (entry.state > 5) entry.state = 0;
        entry.text[sizeof(entry.text) - 1] = 0;
      }
    }
  }
  if (copied_sections & (1u << kSnapshotMapSection)) {
    for (uint32_t i = 0; i < state.map_marker_count; ++i) {
      MapMarkerState& marker = state.map_markers[i];
      marker.x = FiniteOrZero(marker.x);
      marker.y = FiniteOrZero(marker.y);
      marker.z = FiniteOrZero(marker.z);
      marker.name[sizeof(marker.name) - 1] = 0;
    }
  }
}

void EnqueueTouch(Gesture kind, float x, float y, float value = 0.0f) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(value)) return;
  x = std::clamp(x, 0.0f, 1.0f);
  y = std::clamp(y, 0.0f, 1.0f);
  AcquireSRWLockExclusive(&g_touch_lock);
  if (kind == Gesture::kMove && g_touch_count) {
    unsigned last = (g_touch_write + kTouchCapacity - 1) % kTouchCapacity;
    if (g_touch[last].kind == Gesture::kMove) {
      g_touch[last] = {kind, x, y, value};
      ReleaseSRWLockExclusive(&g_touch_lock);
      return;
    }
  }
  if (g_touch_count == kTouchCapacity) {
    const uint64_t dropped = g_touch_overflow_count.fetch_add(1) + 1;
    if (dropped == 1 || dropped % 100 == 0)
      DIS_WARN("[touch] input queue overflow; gesture stream resynchronized "
               "count=%llu",
               static_cast<unsigned long long>(dropped));
    if (kind == Gesture::kMove) {
      // Move events are disposable. Never evict a down/up/cancel transition,
      // because doing so can leave the UI in a permanent drag state.
      ReleaseSRWLockExclusive(&g_touch_lock);
      return;
    }
    std::memset(g_touch, 0, sizeof(g_touch));
    g_touch_read = 0;
    g_touch_write = 1;
    g_touch_count = 1;
    g_touch[0] = {Gesture::kCancel, x, y, 0.0f};
  }
  g_touch[g_touch_write] = {kind, x, y, value};
  g_touch_write = (g_touch_write + 1) % kTouchCapacity;
  ++g_touch_count;
  ReleaseSRWLockExclusive(&g_touch_lock);
}

bool SendVirtualKey(WORD key) {
  INPUT input[2] = {};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wVk = key;
  input[1] = input[0];
  input[1].ki.dwFlags = KEYEVENTF_KEYUP;
  return SendInput(2, input, sizeof(INPUT)) == 2;
}

bool SendUnicodeUnit(wchar_t unit) {
  INPUT input[2] = {};
  input[0].type = INPUT_KEYBOARD;
  input[0].ki.wScan = static_cast<WORD>(unit);
  input[0].ki.dwFlags = KEYEVENTF_UNICODE;
  input[1] = input[0];
  input[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
  return SendInput(2, input, sizeof(INPUT)) == 2;
}

bool SendUnicodeCodePoint(unsigned code_point) {
  if (code_point == 0 || code_point > 0x10FFFFu ||
      (code_point >= 0xD800u && code_point <= 0xDFFFu)) return false;
  if (code_point <= 0xFFFFu) return SendUnicodeUnit(static_cast<wchar_t>(code_point));
  code_point -= 0x10000u;
  const wchar_t high = static_cast<wchar_t>(0xD800u + (code_point >> 10));
  const wchar_t low = static_cast<wchar_t>(0xDC00u + (code_point & 0x3FFu));
  INPUT input[4] = {};
  for (int i = 0; i < 4; ++i) input[i].type = INPUT_KEYBOARD;
  input[0].ki.wScan = static_cast<WORD>(high);
  input[0].ki.dwFlags = KEYEVENTF_UNICODE;
  input[1] = input[0];
  input[1].ki.dwFlags |= KEYEVENTF_KEYUP;
  input[2].ki.wScan = static_cast<WORD>(low);
  input[2].ki.dwFlags = KEYEVENTF_UNICODE;
  input[3] = input[2];
  input[3].ki.dwFlags |= KEYEVENTF_KEYUP;
  return SendInput(4, input, sizeof(INPUT)) == 4;
}

bool SkyrimOwnsForeground() {
  const HWND foreground = GetForegroundWindow();
  if (!foreground) return false;
  DWORD process = 0;
  GetWindowThreadProcessId(foreground, &process);
  return process == GetCurrentProcessId();
}

DWORD WINAPI TouchThread(void*) {
  char packet[160] = {};
  while (g_touch_running.load(std::memory_order_acquire)) {
    sockaddr_in sender = {};
    int sender_size = sizeof(sender);
    int n = recvfrom(g_touch_socket, packet, sizeof(packet) - 1, 0,
                     reinterpret_cast<sockaddr*>(&sender), &sender_size);
    if (n <= 0) continue;
    // Remember the ephemeral Android source port used by WEMU's ordered
    // gesture socket. Replies to this exact peer are collected by Android and
    // converted to device haptics; no desktop/controller input is involved.
    if (sender.sin_family == AF_INET &&
        sender.sin_addr.s_addr == htonl(INADDR_LOOPBACK)) {
      AcquireSRWLockExclusive(&g_touch_feedback_lock);
      g_touch_feedback_peer = sender;
      g_touch_feedback_peer_valid = true;
      ReleaseSRWLockExclusive(&g_touch_feedback_lock);
    }
    packet[n] = 0;
    float x = 0, y = 0, v = 0;
    unsigned code_point = 0;
    if (std::sscanf(packet, "text %u", &code_point) == 1) {
      const bool focused = SkyrimOwnsForeground();
      const bool sent = focused && SendUnicodeCodePoint(code_point);
      if (!g_keyboard_packet_logged.exchange(true)) {
        HWND foreground = GetForegroundWindow();
        DWORD process = 0;
        if (foreground) GetWindowThreadProcessId(foreground, &process);
        DIS_INFO("[keyboard] first text packet foreground=%p pid=%lu current=%lu "
                 "focused=%u sent=%u",
                 foreground, static_cast<unsigned long>(process),
                 static_cast<unsigned long>(GetCurrentProcessId()),
                 focused ? 1u : 0u, sent ? 1u : 0u);
      }
    } else if (std::strcmp(packet, "key backspace") == 0) {
      const bool sent = SkyrimOwnsForeground() && SendVirtualKey(VK_BACK);
      if (!g_keyboard_packet_logged.exchange(true))
        DIS_INFO("[keyboard] first control packet key=backspace sent=%u", sent ? 1u : 0u);
    } else if (std::strcmp(packet, "key enter") == 0) {
      const bool sent = SkyrimOwnsForeground() && SendVirtualKey(VK_RETURN);
      if (!g_keyboard_packet_logged.exchange(true))
        DIS_INFO("[keyboard] first control packet key=enter sent=%u", sent ? 1u : 0u);
    } else if ((std::sscanf(packet, "tap %f %f", &x, &y) == 2 ||
         std::sscanf(packet, "select %f %f", &x, &y) == 2) &&
        x >= 0 && x <= 1 && y >= 0 && y <= 1) EnqueueTouch(Gesture::kTap, x, y);
    else if (std::sscanf(packet, "hold %f %f %f", &x, &y, &v) >= 2)
      EnqueueTouch(Gesture::kHold, x, y, v);
    else if (std::sscanf(packet, "down %f %f", &x, &y) == 2)
      EnqueueTouch(Gesture::kDown, x, y);
    else if (std::sscanf(packet, "move %f %f", &x, &y) == 2)
      EnqueueTouch(Gesture::kMove, x, y);
    else if (std::sscanf(packet, "up %f %f", &x, &y) == 2)
      EnqueueTouch(Gesture::kUp, x, y);
    else if (std::sscanf(packet, "pinch %f %f %f", &x, &y, &v) == 3)
      EnqueueTouch(Gesture::kPinch, x, y, v);
    else if (std::strncmp(packet, "cancel", 6) == 0)
      EnqueueTouch(Gesture::kCancel, 0, 0);
    else {
      const uint64_t now = GetTickCount64();
      if (now - g_last_bad_touch_log >= 5000) {
        DIS_WARN("[touch] ignored malformed packet bytes=%d payload=%.80s", n,
                 packet);
        g_last_bad_touch_log = now;
      }
    }
  }
  return 0;
}

void StartTouch() {
  if (g_touch_running.exchange(true)) return;
  AcquireSRWLockExclusive(&g_touch_feedback_lock);
  g_touch_feedback_peer = {};
  g_touch_feedback_peer_valid = false;
  ReleaseSRWLockExclusive(&g_touch_feedback_lock);
  WSADATA wsa = {};
  const int startup_error = WSAStartup(MAKEWORD(2, 2), &wsa);
  if (startup_error != 0) {
    DIS_ERR("[touch] Winsock startup failed (%d)", startup_error);
    g_touch_running = false;
    return;
  }
  g_touch_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (g_touch_socket == INVALID_SOCKET) {
    DIS_ERR("[touch] UDP socket creation failed (%d)", WSAGetLastError());
    WSACleanup(); g_touch_running = false; return;
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(g_touch_port);
  if (bind(g_touch_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
    DIS_WARN("[touch] port %u unavailable (%d)", g_touch_port, WSAGetLastError());
    closesocket(g_touch_socket); g_touch_socket = INVALID_SOCKET; WSACleanup();
    g_touch_running = false; return;
  }
  g_touch_thread = CreateThread(nullptr, 0, TouchThread, nullptr, 0, nullptr);
  if (!g_touch_thread) {
    DIS_WARN("[touch] listener thread could not be created");
    closesocket(g_touch_socket);
    g_touch_socket = INVALID_SOCKET;
    WSACleanup();
    g_touch_running.store(false, std::memory_order_release);
    return;
  }
  DIS_INFO("[touch] listening on 127.0.0.1:%u", g_touch_port);
}

void PumpTouch() {
  for (unsigned i = 0; i < 32; ++i) {
    TouchEvent e = {};
    bool found = false;
    AcquireSRWLockExclusive(&g_touch_lock);
    if (g_touch_count) {
      e = g_touch[g_touch_read]; g_touch_read = (g_touch_read + 1) % kTouchCapacity;
      --g_touch_count; found = true;
    }
    ReleaseSRWLockExclusive(&g_touch_lock);
    if (!found) break;
    if (e.kind != Gesture::kCancel) {
      const float physical_x = e.x * static_cast<float>(g_width);
      const float physical_y = e.y * static_cast<float>(g_height);
      if (g_present_width <= 0 || g_present_height <= 0 ||
          physical_x < g_present_x || physical_y < g_present_y ||
          physical_x >= g_present_x + g_present_width ||
          physical_y >= g_present_y + g_present_height) {
        TheUi().CancelPointer();
        continue;
      }
      e.x = (physical_x - g_present_x) /
            static_cast<float>(g_present_width);
      e.y = (physical_y - g_present_y) /
            static_cast<float>(g_present_height);
      // Ui boxes are half-open. Keep an exact normalized 1.0 inside the last
      // logical pixel instead of producing x==canvas.width().
      const float max_x = 1.0f - 0.5f / kUiCanvasWidth;
      const float max_y = 1.0f - 0.5f / kUiCanvasHeight;
      e.x = std::clamp(e.x, 0.0f, max_x);
      e.y = std::clamp(e.y, 0.0f, max_y);
    }
    bool terminal = false;
    switch (e.kind) {
      case Gesture::kTap: {
        const uint64_t now = GetTickCount64();
        const float distance = std::abs(e.x - g_last_pointer_up_x) +
                               std::abs(e.y - g_last_pointer_up_y);
        // Some Android senders emit both an explicit tap and a down/up stream.
        // Suppress the duplicate so page arrows and actions run only once.
        if (now - g_last_pointer_up_ms > 180 || distance > 0.012f)
          TheUi().Tap(e.x, e.y);
        terminal = true;
        break;
      }
      case Gesture::kHold: {
        const uint64_t now = GetTickCount64();
        const float distance = std::abs(e.x - g_last_pointer_up_x) +
                               std::abs(e.y - g_last_pointer_up_y);
        // A few Android gesture sources report HOLD after the corresponding
        // down/up stream.  The up already activated the rendered control, so
        // ignore that delayed duplicate instead of using/equipping twice.
        if (now - g_last_pointer_up_ms > 350 || distance > 0.012f)
          TheUi().Hold(e.x, e.y);
        terminal = true;
        break;
      }
      case Gesture::kDown: TheUi().Pointer(WM_LBUTTONDOWN, e.x, e.y); break;
      case Gesture::kMove: TheUi().Pointer(WM_MOUSEMOVE, e.x, e.y); break;
      case Gesture::kUp:
        TheUi().Pointer(WM_LBUTTONUP, e.x, e.y);
        g_last_pointer_up_ms = GetTickCount64();
        g_last_pointer_up_x = e.x;
        g_last_pointer_up_y = e.y;
        terminal = true;
        break;
      case Gesture::kPinch: TheUi().Pinch(e.x, e.y, e.value); break;
      case Gesture::kCancel: TheUi().CancelPointer(); break;
    }
    // Tap/hold/up can change a page, category, or selected row.  Leave any
    // subsequently queued datagrams for the next freshly rendered frame so
    // they cannot hit-test controls that were never shown to the user.
    if (terminal) break;
  }
}

bool PaintPanel(HDC target) {
  if (!target || !TheUi().canvas().ready() || g_width <= 0 || g_height <= 0)
    return false;
  if (g_present_x == 0 && g_present_y == 0 &&
      g_present_width == TheUi().canvas().width() &&
      g_present_height == TheUi().canvas().height()) {
    return BitBlt(target, 0, 0, g_width, g_height,
                  TheUi().canvas().dc(), 0, 0, SRCCOPY) != FALSE;
  }
  RECT background = {0, 0, g_width, g_height};
  FillRect(target, &background,
           static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
  SetStretchBltMode(target, HALFTONE);
  SetBrushOrgEx(target, 0, 0, nullptr);
  return StretchBlt(target, g_present_x, g_present_y,
                    g_present_width, g_present_height,
                    TheUi().canvas().dc(), 0, 0,
                    TheUi().canvas().width(), TheUi().canvas().height(),
                    SRCCOPY) != FALSE;
}

bool PresentPanelCanvas() {
  const HWND panel = g_panel_window;
  if (!panel || !IsWindow(panel)) return false;
  HDC target = GetDC(panel);
  if (!target) return false;
  const bool painted = PaintPanel(target);
  ReleaseDC(panel, target);
  if (painted) GdiFlush();
  return painted;
}

LRESULT CALLBACK PanelProc(HWND h, UINT message, WPARAM w, LPARAM l) {
  switch (message) {
    case WM_CLOSE: return 0;
    case WM_ERASEBKGND: return 1;
    case WM_NCDESTROY:
      if (h == g_panel_window) g_panel_window = nullptr;
      break;
    case WM_PAINT: {
      PAINTSTRUCT paint = {};
      HDC target = BeginPaint(h, &paint);
      PaintPanel(target);
      EndPaint(h, &paint);
      return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MOUSEMOVE:
      if (g_width > 0 && g_height > 0) {
        if (message == WM_MOUSEMOVE && !(w & MK_LBUTTON) &&
            GetCapture() != h) return 0;
        const int local_x = GET_X_LPARAM(l) - g_present_x;
        const int local_y = GET_Y_LPARAM(l) - g_present_y;
        if (g_present_width <= 0 || g_present_height <= 0 || local_x < 0 ||
            local_y < 0 || local_x >= g_present_width ||
            local_y >= g_present_height)
          return 0;
        const float x = std::clamp(
            local_x / static_cast<float>(g_present_width), 0.0f, 1.0f);
        const float y = std::clamp(
            local_y / static_cast<float>(g_present_height), 0.0f, 1.0f);
        // Window messages can be dispatched on a different thread from the
        // game's Present hook under Wine. Keep every Ui mutation on the panel
        // pump by using the same ordered queue as Thor touch input.
        const Gesture gesture = message == WM_LBUTTONDOWN ? Gesture::kDown :
                                message == WM_LBUTTONUP ? Gesture::kUp : Gesture::kMove;
        if (message == WM_LBUTTONDOWN) SetCapture(h);
        EnqueueTouch(gesture, x, y);
        if (message == WM_LBUTTONUP && GetCapture() == h) ReleaseCapture();
        return 0;
      }
      break;
    case WM_CAPTURECHANGED:
      EnqueueTouch(Gesture::kCancel, 0.0f, 0.0f);
      return 0;
  }
  return DefWindowProcW(h, message, w, l);
}

struct MonitorChoice {
  RECT rect = {};
  bool found = false;
  int score = -1;
};
BOOL CALLBACK MonitorProc(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
  auto* choice = reinterpret_cast<MonitorChoice*>(data);
  MONITORINFOEXW info = {};
  info.cbSize = sizeof(info);
  if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO*>(&info)) ||
      (info.dwFlags & MONITORINFOF_PRIMARY))
    return TRUE;
  const int width = info.rcMonitor.right - info.rcMonitor.left;
  const int height = info.rcMonitor.bottom - info.rcMonitor.top;
  // Prefer the Thor's real 1240x1080 lower panel instead of whichever
  // non-primary monitor Wine happens to enumerate first. Keep a generic
  // secondary as the desktop test fallback.
  const int score = (width == kUiCanvasWidth && height == kUiCanvasHeight)
                        ? 100
                        : 1;
  if (!choice->found || score > choice->score) {
    choice->rect = info.rcMonitor;
    choice->found = true;
    choice->score = score;
  }
  return TRUE;
}

bool CreatePanel() {
  MonitorChoice monitor;
  EnumDisplayMonitors(nullptr, nullptr, MonitorProc, reinterpret_cast<LPARAM>(&monitor));
  RECT rect = monitor.rect;
  if (!monitor.found) {
    const int64_t x = static_cast<int32_t>(
        GetPrivateProfileIntW(L"panel", L"x", 0, g_ini_path));
    const int64_t y = static_cast<int32_t>(
        GetPrivateProfileIntW(L"panel", L"y", 720, g_ini_path));
    const int64_t width = static_cast<int32_t>(
        GetPrivateProfileIntW(L"panel", L"width", 1240, g_ini_path));
    const int64_t height = static_cast<int32_t>(
        GetPrivateProfileIntW(L"panel", L"height", 1080, g_ini_path));
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096 ||
        x < LONG_MIN || y < LONG_MIN || x + width > LONG_MAX ||
        y + height > LONG_MAX) {
      DIS_ERR("[panel] rejected unsafe fallback geometry");
      return false;
    }
    rect.left = static_cast<LONG>(x);
    rect.top = static_cast<LONG>(y);
    rect.right = static_cast<LONG>(x + width);
    rect.bottom = static_cast<LONG>(y + height);
  }
  g_width = rect.right - rect.left; g_height = rect.bottom - rect.top;
  DIS_INFO("[panel] display selected source=%s bounds=%ld,%ld %dx%d",
           monitor.found ? "secondary-monitor" : "configured-fallback",
           rect.left, rect.top, g_width, g_height);
  if (g_width <= 0 || g_height <= 0 ||
      static_cast<size_t>(g_width) * static_cast<size_t>(g_height) > kMaxPanelPixels) {
    DIS_ERR("[panel] rejected unsafe dimensions %dx%d", g_width, g_height);
    g_width = g_height = 0;
    return false;
  }
  const Box fitted = AspectFit(Box(0, 0, g_width, g_height),
                               kUiCanvasWidth, kUiCanvasHeight);
  g_present_x = fitted.x;
  g_present_y = fitted.y;
  g_present_width = fitted.w;
  g_present_height = fitted.h;
  DIS_INFO("[panel] canvas fit output=%d,%d %dx%d logical=%dx%d",
           g_present_x, g_present_y, g_present_width, g_present_height,
           kUiCanvasWidth, kUiCanvasHeight);

  WNDCLASSEXW wc = {}; wc.cbSize = sizeof(wc); wc.lpfnWndProc = PanelProc;
  wc.hInstance = g_proxy_module ? g_proxy_module : GetModuleHandleW(nullptr);
  wc.lpszClassName = L"SkyrimSecondScreenPanel";
  wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
  if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
    DIS_ERR("[panel] window class registration failed (%lu)", GetLastError());
    return false;
  }
  g_panel_window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
      wc.lpszClassName, L"Skyrim Dual Screen", WS_POPUP,
      rect.left, rect.top, g_width, g_height, nullptr, nullptr, wc.hInstance, nullptr);
  if (!g_panel_window) return false;
  if (!TheUi().Create(kUiCanvasWidth, kUiCanvasHeight)) {
    DIS_ERR("[panel] UI canvas allocation failed");
    DestroyWindow(g_panel_window); g_panel_window = nullptr;
    return false;
  }
  DIS_INFO("[ui] initialized canvas=%dx%d output=%dx%d bridge=0x%08X "
           "snapshot_bytes=%u",
           kUiCanvasWidth, kUiCanvasHeight, g_width, g_height, kBridgeVersion,
           static_cast<unsigned>(sizeof(Snapshot)));
  StartTouch();
  TheUi().RenderLoading();
  // Fill the backing store before mapping the window so neither screen sees a
  // default white client frame during Wine's first ConfigureNotify.
  PresentPanelCanvas();
  ShowWindow(g_panel_window, SW_SHOWNOACTIVATE);
  if (!PresentPanelCanvas()) {
    DIS_ERR("[panel] initial GDI frame failed");
    DestroyWindow(g_panel_window);
    g_panel_window = nullptr;
    return false;
  }
  g_last_panel_paint_ms = GetTickCount64();
  DIS_INFO("[panel] isolated GDI surface ready %dx%d at %ld,%ld", g_width, g_height,
           rect.left, rect.top);
  return true;
}

void DestroyPanelForRecovery() {
  DIS_INFO("[panel] destroying isolated panel for recovery window=%p size=%dx%d",
           g_panel_window, g_width, g_height);
  if (g_panel_window) { DestroyWindow(g_panel_window); g_panel_window = nullptr; }
  g_width = 0;
  g_height = 0;
  g_present_x = g_present_y = 0;
  g_present_width = g_present_height = 0;
  g_last_panel_paint_ms = 0;
  TheUi().MarkDirty();
}

void RefreshPanelSnapshot() {
  const uint64_t published = g_published_sequence.load(std::memory_order_acquire);
  if (published == UINT64_MAX ||
      (g_panel_have_state && published == g_panel_state_sequence)) return;
  // Copy directly into the panel's process-lifetime buffer; avoid another
  // large Snapshot local on the optional worker thread.
  AcquireSRWLockShared(&g_state_lock);
  if (g_have_state && g_state.sequence == published) {
    CopySnapshotDelta(g_panel_state, g_state, g_panel_have_state, false,
                      g_panel_delta_counters);
    g_panel_state_sequence = g_state.sequence;
    g_panel_have_state = true;
  }
  ReleaseSRWLockShared(&g_state_lock);
}

void PumpPanel() {
  if (!g_panel_window || !TheUi().canvas().ready()) return;
  const uint64_t now = GetTickCount64();
  if (now >= g_next_item_preview_retry_scan_ms) {
    g_next_item_preview_retry_scan_ms = now + 1000;
    if (WakeDueItemPreviewRetry(now)) TheUi().MarkDirty();
  }
  const uint64_t preview_generation =
      g_item_preview_generation.load(std::memory_order_acquire);
  if (preview_generation != g_panel_item_preview_generation) {
    g_panel_item_preview_generation = preview_generation;
    TheUi().MarkDirty();
  }
  const uint64_t character_generation =
      g_character_preview_generation.load(std::memory_order_acquire);
  if (character_generation != g_panel_character_preview_generation) {
    g_panel_character_preview_generation = character_generation;
    TheUi().MarkDirty();
  }
  const uint64_t map_generation =
      g_map_preview_generation.load(std::memory_order_acquire);
  if (map_generation != g_panel_map_preview_generation) {
    g_panel_map_preview_generation = map_generation;
    TheUi().MarkDirty();
  }
  RefreshPanelSnapshot();
  const bool have = g_panel_have_state;
  const Snapshot& state = g_panel_state;
  bool updated = false;
  if (!have || !state.in_game) {
    updated = TheUi().RenderLoading();
    if (!g_panel_showing_loading) {
      DIS_INFO("[ui] live -> waiting seq=%llu epoch=%llu reason=%s have=%u",
               static_cast<unsigned long long>(state.sequence),
               static_cast<unsigned long long>(state.world_epoch),
               LoadingReasonName(state.loading_reason), have ? 1u : 0u);
      g_panel_showing_loading = true;
    }
  } else {
    updated = TheUi().Render(state);
    if (g_panel_showing_loading) {
      DIS_INFO("[ui] waiting -> live seq=%llu epoch=%llu page=%u items=%u magic=%u "
               "quests=%u markers=%u",
               static_cast<unsigned long long>(state.sequence),
               static_cast<unsigned long long>(state.world_epoch),
               static_cast<unsigned>(TheUi().page()), state.item_count,
               state.magic_count, state.quest_count, state.map_marker_count);
      g_panel_showing_loading = false;
    }
  }

  // Run after composition: BeginItemPreviewRequestFrame first retires stale
  // off-page demand and the active page then reports its complete visible set.
  // Exactly one cache miss is dispatched; selected detail still preempts it.
  ServiceBackgroundItemPreviewDemand(now);

  // SurfaceControl retains the most recently submitted panel buffer.  A
  // heartbeat therefore copied and uploaded an unchanged 1240x1080 frame once
  // per second for no visual benefit, producing a recurring game-frame stall.
  if (updated) {
    if (PresentPanelCanvas()) {
      g_last_panel_paint_ms = now;
    } else {
      // Preserve the dirty revision so a transient presentation failure is
      // retried without manufacturing unchanged heartbeat traffic.
      TheUi().MarkDirty();
      if (now - g_last_panel_health_log >= 5000) {
        DIS_WARN("[panel] present failed window=%p valid=%u canvas=%u",
                 g_panel_window, IsWindow(g_panel_window) ? 1u : 0u,
                 TheUi().canvas().ready() ? 1u : 0u);
        g_last_panel_health_log = now;
      }
    }
  }
  // Process input only after the newest published snapshot has been painted.
  // Ui hit-testing retains that exact rendered snapshot, so a tap can never
  // target a newer invisible inventory/magic/quest ordering. Any resulting
  // visual change is presented on the next panel tick (normally <16 ms).
  PumpTouch();
}


DWORD WINAPI PanelThread(void*) {
  g_panel_thread_id = GetCurrentThreadId();
  bool created = false;
  try {
    created = CreatePanel();
  } catch (...) {
    DIS_ERR("[panel] exception while creating isolated lower window");
  }
  if (!created) {
    DIS_ERR("[panel] isolated GDI thread could not create the lower window");
    g_panel_running.store(false, std::memory_order_release);
    g_panel_thread_state.store(3, std::memory_order_release);
    return 0;
  }

  g_panel_thread_state.store(2, std::memory_order_release);
  DIS_INFO("[panel] CPU-only panel thread running; Skyrim D3D is untouched");
  unsigned consecutive_failures = 0;
  while (g_panel_running.load(std::memory_order_acquire)) {
    MSG message = {};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        g_panel_running.store(false, std::memory_order_release);
        break;
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (!g_panel_running.load(std::memory_order_acquire)) break;

    try {
      PumpPanel();
      consecutive_failures = 0;
    } catch (...) {
      // The panel is optional. A localized string or allocation failure must
      // never unwind into Skyrim or affect its render/game threads.
      ++consecutive_failures;
      if (consecutive_failures == 1)
        DIS_ERR("[panel] UI update failed; retaining the previous lower frame");
      TheUi().MarkDirty();
      if (consecutive_failures >= 3) {
        DIS_ERR("[panel] repeated UI failures; stopping optional panel safely");
        g_panel_thread_state.store(3, std::memory_order_release);
        g_panel_running.store(false, std::memory_order_release);
        break;
      }
    }
    const DWORD delay = consecutive_failures ? 250 : 16;
    MsgWaitForMultipleObjects(0, nullptr, FALSE, delay, QS_ALLINPUT);
  }

  DestroyPanelForRecovery();
  if (g_panel_thread_state.load(std::memory_order_acquire) != 3)
    g_panel_thread_state.store(0, std::memory_order_release);
  return 0;
}

bool StartPanelThread() {
  AcquireSRWLockExclusive(&g_panel_control_lock);
  unsigned state = g_panel_thread_state.load(std::memory_order_acquire);
  if (g_panel_thread && WaitForSingleObject(g_panel_thread, 0) == WAIT_OBJECT_0) {
    CloseHandle(g_panel_thread);
    g_panel_thread = nullptr;
    // A signaled handle cannot represent a healthy renderer, even if the
    // thread died before it had a chance to replace its last RUNNING state.
    // Always make the completed worker restartable.
    state = 0;
    g_panel_thread_state.store(0, std::memory_order_release);
  }
  if (state == 0 && !g_panel_thread) {
    g_panel_thread_state.store(1, std::memory_order_release);
    g_panel_running.store(true, std::memory_order_release);
    g_panel_thread = CreateThread(nullptr, 0, PanelThread, nullptr, 0,
                                  &g_panel_thread_id);
    if (!g_panel_thread) {
      g_panel_running.store(false, std::memory_order_release);
      g_panel_thread_state.store(3, std::memory_order_release);
      DIS_ERR("[panel] CreateThread failed (%lu)", GetLastError());
    }
  }
  ReleaseSRWLockExclusive(&g_panel_control_lock);

  // Do not report a merely-starting panel as attached. The plugin retries a
  // false result, while a false-positive would strand it after CreatePanel
  // failed. Panel creation is normally a few milliseconds; keep the bound low.
  for (unsigned wait = 0; wait < 250; ++wait) {
    state = g_panel_thread_state.load(std::memory_order_acquire);
    if (state == 2) return true;
    if (state == 3) return false;
    Sleep(1);
  }
  return g_panel_thread_state.load(std::memory_order_acquire) == 2;
}

}  // namespace

bool CopyLatestSnapshot(Snapshot* out) {
  if (!out) return false;
  AcquireSRWLockShared(&g_state_lock);
  const uint64_t published =
      g_published_sequence.load(std::memory_order_acquire);
  const bool available = g_have_state && published != UINT64_MAX &&
                         g_state.sequence == published;
  if (available) *out = g_state;
  ReleaseSRWLockShared(&g_state_lock);
  return available;
}

bool PushUiCommand(const Command& command) {
  const uint32_t type_value = static_cast<uint32_t>(command.type);
  const bool preview_type =
      command.type == CommandType::kRequestItemPreview ||
      command.type == CommandType::kRequestMapPreview;
  const bool invalid_type =
      type_value <= static_cast<uint32_t>(CommandType::kNone) ||
      type_value > static_cast<uint32_t>(CommandType::kRequestMapPreview);
  const bool invalid_form =
      (command.type == CommandType::kRequestMapPreview)
          ? command.form_id != 0
          : command.form_id == 0;
  const uint32_t allowed_flags = static_cast<uint32_t>(
      kCommandPreviewPriority |
      (command.type == CommandType::kRequestMapPreview
           ? (kCommandRefreshMapMarkers | kCommandResetMapCamera)
           : 0u));
  const bool invalid_flags =
      (command.flags & ~allowed_flags) != 0 ||
      (!preview_type && command.flags != 0);
  const bool finite_map_control = std::isfinite(command.map_pan_x) &&
      std::isfinite(command.map_pan_y) &&
      std::isfinite(command.map_zoom_delta);
  const bool has_map_control = command.map_pan_x != 0.0f ||
      command.map_pan_y != 0.0f || command.map_zoom_delta != 0.0f ||
      command.control_revision != 0;
  const bool invalid_map_control = !finite_map_control ||
      (command.type == CommandType::kRequestMapPreview
           ? command.map_world_id == 0
           : command.map_world_id != 0) ||
      (command.type != CommandType::kRequestMapPreview && has_map_control) ||
      std::abs(command.map_pan_x) > 2.0f ||
      std::abs(command.map_pan_y) > 2.0f ||
      std::abs(command.map_zoom_delta) > 4.0f;
  const bool invalid_hotbar_index =
      command.type == CommandType::kAssignHotbar &&
      (command.index < 0 || command.index >= static_cast<int32_t>(kMaxFavorites));
  if (invalid_type || invalid_form || invalid_flags || invalid_hotbar_index ||
      invalid_map_control) {
    DIS_WARN("[command] rejected malformed enqueue type=%u form=%08X "
             "index=%d flags=%08X map=(%.4f,%.4f,%.4f) revision=%llu",
             type_value, command.form_id, command.index, command.flags,
             command.map_pan_x, command.map_pan_y, command.map_zoom_delta,
             static_cast<unsigned long long>(command.control_revision));
    return false;
  }
  const uint64_t current_epoch =
      g_current_world_epoch.load(std::memory_order_acquire);
  uint32_t current_map_world = 0;
  AcquireSRWLockShared(&g_state_lock);
  if (g_have_state && g_state.in_game)
    current_map_world = g_state.world_form_id;
  ReleaseSRWLockShared(&g_state_lock);
  if (command.version != kBridgeVersion || current_epoch == UINT64_MAX ||
      command.world_epoch != current_epoch ||
      (command.type == CommandType::kRequestMapPreview &&
       command.map_world_id != current_map_world)) {
    DIS_WARN("[command] rejected stale/invalid enqueue type=%u form=%08X "
             "version=%08X epoch=%llu current=%llu map_world=%08X "
             "current_map_world=%08X",
             static_cast<unsigned>(command.type), command.form_id,
             command.version,
             static_cast<unsigned long long>(command.world_epoch),
             static_cast<unsigned long long>(current_epoch),
             command.map_world_id, current_map_world);
    return false;
  }
  AcquireSRWLockExclusive(&g_command_lock);
  const uint64_t locked_epoch =
      g_current_world_epoch.load(std::memory_order_acquire);
  if (locked_epoch == UINT64_MAX || command.world_epoch != locked_epoch) {
    ReleaseSRWLockExclusive(&g_command_lock);
    DIS_WARN("[command] epoch changed while enqueueing type=%u form=%08X "
             "epoch=%llu current=%llu",
             static_cast<unsigned>(command.type), command.form_id,
             static_cast<unsigned long long>(command.world_epoch),
             static_cast<unsigned long long>(locked_epoch));
    return false;
  }
  if (preview_type) {
    unsigned queued_item_previews = 0;
    for (unsigned i = 0; i < g_preview_command_count; ++i) {
      const unsigned at =
          (g_preview_command_read + i) % kPreviewCommandCapacity;
      if (g_preview_commands[at].type ==
          CommandType::kRequestItemPreview)
        ++queued_item_previews;
      if (g_preview_commands[at].type == command.type &&
          g_preview_commands[at].form_id == command.form_id &&
          (command.type != CommandType::kRequestMapPreview ||
           g_preview_commands[at].map_world_id == command.map_world_id)) {
        g_preview_commands[at].flags |= command.flags;
        if (command.type == CommandType::kRequestMapPreview) {
          // Map motion is latest-state work, not a FIFO of stale touch
          // packets. Accumulate an unpolled command in place and let the
          // native camera consume the complete gesture in one world pass.
          g_preview_commands[at].map_pan_x = std::clamp(
              g_preview_commands[at].map_pan_x + command.map_pan_x,
              -8.0f, 8.0f);
          g_preview_commands[at].map_pan_y = std::clamp(
              g_preview_commands[at].map_pan_y + command.map_pan_y,
              -8.0f, 8.0f);
          g_preview_commands[at].map_zoom_delta = std::clamp(
              g_preview_commands[at].map_zoom_delta +
                  command.map_zoom_delta,
              -8.0f, 8.0f);
          g_preview_commands[at].control_revision = std::max(
              g_preview_commands[at].control_revision,
              command.control_revision);
        }
        if ((command.flags & kCommandPreviewPriority) != 0 &&
            at != g_preview_command_read) {
          std::swap(g_preview_commands[at],
                    g_preview_commands[g_preview_command_read]);
        }
        ReleaseSRWLockExclusive(&g_command_lock);
        return true;
      }
    }
    if (command.type == CommandType::kRequestItemPreview &&
        queued_item_previews >= kItemPreviewCommandCapacity) {
      ReleaseSRWLockExclusive(&g_command_lock);
      DIS_WARN("[command] item preview queue full; request rejected form=%08X",
               command.form_id);
      return false;
    }
    if (g_preview_command_count == kPreviewCommandCapacity) {
      ReleaseSRWLockExclusive(&g_command_lock);
      DIS_WARN("[command] preview queue full; request rejected form=%08X",
               command.form_id);
      return false;
    }
    if ((command.flags & kCommandPreviewPriority) != 0) {
      // Insert a newly visible request at the front as well as promoting an
      // existing one above. This keeps the currently selected detail model
      // ahead of progressively requested visible-row thumbnails.
      g_preview_command_read =
          (g_preview_command_read + kPreviewCommandCapacity - 1) %
          kPreviewCommandCapacity;
      g_preview_commands[g_preview_command_read] = command;
    } else {
      g_preview_commands[g_preview_command_write] = command;
      g_preview_command_write =
          (g_preview_command_write + 1) % kPreviewCommandCapacity;
    }
    ++g_preview_command_count;
  } else {
    if (g_command_count == kCommandCapacity) {
      ReleaseSRWLockExclusive(&g_command_lock);
      DIS_ERR("[command] user-action queue full; newest action rejected "
              "type=%u form=%08X",
              static_cast<unsigned>(command.type), command.form_id);
      return false;
    }
    g_commands[g_command_write] = command;
    g_command_write = (g_command_write + 1) % kCommandCapacity;
    ++g_command_count;
  }
  g_pending_command_count.store(g_command_count + g_preview_command_count,
                                std::memory_order_release);
  const unsigned queued = preview_type ? g_preview_command_count
                                       : g_command_count;
  ReleaseSRWLockExclusive(&g_command_lock);
  DIS_INFO("[command] queued type=%u form=%08X index=%d depth=%u lane=%s "
           "map=(%.4f,%.4f,%.4f) revision=%llu",
           static_cast<unsigned>(command.type), command.form_id,
           command.index, queued, preview_type ? "preview" : "user",
           command.map_pan_x, command.map_pan_y, command.map_zoom_delta,
           static_cast<unsigned long long>(command.control_revision));
  return true;
}

extern "C" __declspec(dllexport) void __stdcall SDS_SubmitSnapshot(const Snapshot* snapshot) {
  if (!snapshot) {
    const uint64_t rejected = g_rejected_snapshots.fetch_add(1) + 1;
    if (rejected == 1 || rejected % 100 == 0)
      DIS_ERR("[bridge] rejected null snapshot count=%llu",
              static_cast<unsigned long long>(rejected));
    return;
  }
  if (snapshot->version != kBridgeVersion || snapshot->size != sizeof(Snapshot)) {
    const uint64_t rejected = g_rejected_snapshots.fetch_add(1) + 1;
    if (rejected == 1 || rejected % 100 == 0)
      DIS_ERR("[bridge] rejected snapshot version=0x%08X expected=0x%08X size=%u "
              "expected_size=%u count=%llu", snapshot->version, kBridgeVersion,
              snapshot->size, static_cast<unsigned>(sizeof(Snapshot)),
              static_cast<unsigned long long>(rejected));
    return;
  }
  if (!ValidLoadingReason(snapshot->loading_reason)) {
    const uint64_t rejected = g_rejected_snapshots.fetch_add(1) + 1;
    if (rejected == 1 || rejected % 100 == 0)
      DIS_ERR("[bridge] rejected invalid loading reason=%u count=%llu",
              static_cast<unsigned>(snapshot->loading_reason),
              static_cast<unsigned long long>(rejected));
    return;
  }
  AcquireSRWLockExclusive(&g_submit_staging_lock);
  const uint32_t staging_sections = CopySnapshotDelta(
      g_submit_staging, *snapshot, g_submit_staging_have_state, false,
      g_submit_delta_counters);
  g_submit_staging_have_state = true;
  Snapshot& clean = g_submit_staging;
  SanitizeSnapshot(clean, staging_sections);
  if ((clean.in_game && clean.loading_reason != LoadingReason::kNone) ||
      (!clean.in_game &&
       !ExplicitLoadingReason(clean.loading_reason))) {
    const uint64_t rejected = g_rejected_snapshots.fetch_add(1) + 1;
    if (rejected == 1 || rejected % 100 == 0)
      DIS_WARN("[bridge] rejected contradictory lifecycle in_game=%u "
               "reason=%s count=%llu",
               clean.in_game, LoadingReasonName(clean.loading_reason),
               static_cast<unsigned long long>(rejected));
    ReleaseSRWLockExclusive(&g_submit_staging_lock);
    return;
  }

  bool accepted = true;
  bool clear_world_caches = false;
  bool clear_map_worldspace_cache = false;
  uint64_t previous_sequence = UINT64_MAX;
  uint64_t previous_epoch = UINT64_MAX;
  uint32_t previous_in_game = UINT32_MAX;
  uint32_t previous_world_form = 0;
  AcquireSRWLockExclusive(&g_state_lock);
  if (g_have_state) {
    previous_sequence = g_state.sequence;
    previous_epoch = g_state.world_epoch;
    previous_in_game = g_state.in_game;
    previous_world_form = g_state.world_form_id;
    if (clean.sequence <= g_state.sequence ||
        clean.world_epoch < g_state.world_epoch) {
      accepted = false;
    } else if (g_state.in_game && !clean.in_game &&
               !ExplicitLoadingReason(clean.loading_reason)) {
      // Retain the last valid live frame for transient/invalid samples. Only a
      // real SKSE lifecycle transition is allowed to show the waiting page.
      accepted = false;
    }
  }
  if (accepted) {
    clear_world_caches = !g_have_state || clean.world_epoch != g_state.world_epoch;
    clear_map_worldspace_cache = g_have_state && !clear_world_caches &&
        clean.world_form_id != g_state.world_form_id;
    CopySnapshotDelta(g_state, clean, g_have_state, false,
                      g_state_delta_counters);
    g_have_state = true;
    // Publish the new epoch before releasing the state barrier. Old-world
    // callbacks will now fail their under-lock recheck; anything that slipped
    // through before this store is removed by ClearWorldBoundCaches below.
    g_current_world_epoch.store(clean.world_epoch,
                                std::memory_order_release);
  }
  ReleaseSRWLockExclusive(&g_state_lock);
  if (!accepted) {
    const uint64_t rejected = g_rejected_snapshots.fetch_add(1) + 1;
    if (rejected == 1 || rejected % 100 == 0)
      DIS_WARN("[bridge] retained seq=%llu epoch=%llu; rejected stale/invalid "
               "seq=%llu epoch=%llu in_game=%u reason=%s count=%llu",
               static_cast<unsigned long long>(previous_sequence),
               static_cast<unsigned long long>(previous_epoch),
               static_cast<unsigned long long>(clean.sequence),
               static_cast<unsigned long long>(clean.world_epoch),
               clean.in_game, LoadingReasonName(clean.loading_reason),
               static_cast<unsigned long long>(rejected));
    ReleaseSRWLockExclusive(&g_submit_staging_lock);
    return;
  }
  if (clear_world_caches) {
    ClearWorldBoundCaches();
    DIS_INFO("[bridge] world epoch changed %llu -> %llu; preview/input caches "
             "invalidated",
             static_cast<unsigned long long>(previous_epoch),
             static_cast<unsigned long long>(clean.world_epoch));
  } else if (clear_map_worldspace_cache) {
    ClearMapWorldspaceCache();
    DIS_INFO("[map-native] worldspace changed %08X -> %08X; cached frame "
             "and pending map controls invalidated",
             previous_world_form, clean.world_form_id);
  }
  g_published_sequence.store(clean.sequence, std::memory_order_release);
  const uint64_t now = GetTickCount64();
  MaybeLogSnapshotDeltaCounters(now);
  const bool transition = clean.in_game != previous_in_game ||
                          clean.world_epoch != previous_epoch;
  if (transition || now - g_last_snapshot_log >= 10000) {
    const uint64_t gap = g_last_snapshot_sequence != UINT64_MAX &&
                                 clean.sequence > g_last_snapshot_sequence
                             ? clean.sequence - g_last_snapshot_sequence
                             : 0;
    DIS_INFO("[bridge] snapshot seq=%llu gap=%llu epoch=%llu in_game=%u reason=%s "
             "items=%u magic=%u quests=%u markers=%u",
             static_cast<unsigned long long>(clean.sequence),
             static_cast<unsigned long long>(gap),
             static_cast<unsigned long long>(clean.world_epoch), clean.in_game,
             LoadingReasonName(clean.loading_reason), clean.item_count,
             clean.magic_count, clean.quest_count, clean.map_marker_count);
    g_last_snapshot_log = now;
  }
  g_last_snapshot_sequence = clean.sequence;
  g_last_snapshot_epoch = clean.world_epoch;
  g_last_snapshot_in_game = clean.in_game;
  ReleaseSRWLockExclusive(&g_submit_staging_lock);
}

extern "C" __declspec(dllexport) void __stdcall SDS_SubmitItemPreview(
    uint32_t form_id, uint64_t world_epoch, uint32_t width, uint32_t height,
    const uint8_t* rgba, uint32_t row_pitch) {
  const uint64_t current_epoch =
      g_current_world_epoch.load(std::memory_order_acquire);
  if (form_id && world_epoch == current_epoch) {
    // Completion (success or terminal failure) owns this FormID now. Remove
    // priority wakes which were queued while the serial native renderer was
    // still active, otherwise the same cached model can be rendered repeatedly
    // while unrelated inventory entries starve behind it.
    unsigned retained_count = 0;
    AcquireSRWLockExclusive(&g_command_lock);
    for (unsigned i = 0; i < g_preview_command_count; ++i) {
      const unsigned at =
          (g_preview_command_read + i) % kPreviewCommandCapacity;
      const Command& command = g_preview_commands[at];
      if (command.form_id == form_id && command.world_epoch == world_epoch)
        continue;
      g_preview_command_compaction[retained_count++] = command;
    }
    for (auto& command : g_preview_commands) command = {};
    for (unsigned i = 0; i < retained_count; ++i)
      g_preview_commands[i] = g_preview_command_compaction[i];
    g_preview_command_read = 0;
    g_preview_command_count = retained_count;
    g_preview_command_write = retained_count % kPreviewCommandCapacity;
    g_pending_command_count.store(g_command_count + g_preview_command_count,
                                  std::memory_order_release);
    ReleaseSRWLockExclusive(&g_command_lock);
  }
  if (!form_id || !rgba || width == 0 || height == 0 ||
      width > kNativeItemPreviewSize || height > kNativeItemPreviewSize ||
      row_pitch < width * 4 || row_pitch > kNativeItemPreviewSize * 4 ||
      world_epoch != current_epoch) {
    DIS_WARN("[item-preview] rejected form=%08X epoch=%llu current=%llu "
             "size=%ux%u pitch=%u",
             form_id, static_cast<unsigned long long>(world_epoch),
             static_cast<unsigned long long>(current_epoch), width, height,
             row_pitch);
    AcquireSRWLockExclusive(&g_item_preview_lock);
    if (ItemPreviewCacheEntry* entry =
            FindItemPreview(form_id, world_epoch);
        entry && entry->pending && !entry->width && !entry->height) {
      entry->pending = false;
      entry->last_request_ms = GetTickCount64();
      entry->failure_count = std::min<uint32_t>(entry->failure_count + 1, 16);
      entry->priority_sent = false;
      entry->retry_wake_sent = false;
      entry->generation = ++g_item_preview_lru_clock;
      g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
      g_next_background_preview_dispatch_ms = GetTickCount64() + 250;
    }
    ReleaseSRWLockExclusive(&g_item_preview_lock);
    return;
  }
  bool accepted = false;
  AcquireSRWLockExclusive(&g_item_preview_lock);
  ItemPreviewCacheEntry* entry = FindItemPreview(form_id, world_epoch);
  // Recheck while holding the destination lock so a save transition cannot
  // clear the cache and then receive a late old-world model frame.
  if (entry && entry->pending &&
      world_epoch ==
          g_current_world_epoch.load(std::memory_order_acquire)) {
    std::memset(entry->rgba, 0, sizeof(entry->rgba));
    for (uint32_t y = 0; y < height; ++y) {
      std::memcpy(entry->rgba +
                      static_cast<size_t>(y) * kNativeItemPreviewSize * 4,
                  rgba + static_cast<size_t>(y) * row_pitch,
                  static_cast<size_t>(width) * 4);
    }
    // Cache rows are stored at the fixed texture stride.  The visible width is
    // the complete fixed canvas so Canvas never interprets row padding as the
    // beginning of the next row.
    entry->width = kNativeItemPreviewSize;
    entry->height = kNativeItemPreviewSize;
    entry->pending = false;
    entry->failure_count = 0;
    entry->priority_sent = false;
    entry->retry_wake_sent = false;
    entry->generation = ++g_item_preview_lru_clock;
    g_item_preview_generation.fetch_add(1, std::memory_order_acq_rel);
    g_next_background_preview_dispatch_ms = GetTickCount64() + 250;
    accepted = true;
  }
  ReleaseSRWLockExclusive(&g_item_preview_lock);
  if (accepted) {
    DIS_INFO("[item-preview] cached native model form=%08X epoch=%llu "
             "size=%ux%u",
             form_id, static_cast<unsigned long long>(world_epoch), width,
             height);
  } else {
    DIS_WARN("[item-preview] discarded unsolicited/stale frame form=%08X",
             form_id);
  }
}

extern "C" __declspec(dllexport) int __stdcall SDS_GetCharacterPreviewSurface(
    CharacterPreviewSurface* surface) {
  if (!surface) return 0;
  *surface = CharacterPreviewSurface{};
  if (!g_native_character_preview_enabled || !g_enabled ||
      !TheUi().character_preview_active() ||
      g_current_world_epoch.load(std::memory_order_acquire) == UINT64_MAX)
    return 0;
  surface->width = 400;
  surface->height = 640;
  surface->visible = 1;
  surface->rotation_degrees = TheUi().character_rotation_degrees();
  surface->zoom = TheUi().character_zoom();
  surface->control_revision = TheUi().character_control_revision();
  return 1;
}

extern "C" __declspec(dllexport) void __stdcall SDS_SetCharacterPreviewReady(
    uint32_t ready) {
  if (ready) return;
  AcquireSRWLockExclusive(&g_character_preview_lock);
  g_character_preview.ready = false;
  g_character_preview.width = 0;
  g_character_preview.height = 0;
  ReleaseSRWLockExclusive(&g_character_preview_lock);
  g_character_preview_generation.fetch_add(1, std::memory_order_acq_rel);
}

extern "C" __declspec(dllexport) void __stdcall SDS_SubmitCharacterPreview(
    uint32_t width, uint32_t height, const uint8_t* rgba,
    uint32_t row_pitch, uint64_t source_revision) {
  if (!g_native_character_preview_enabled || !rgba || !width || !height ||
      width > kCharacterPreviewMaxWidth ||
      height > kCharacterPreviewMaxHeight || row_pitch < width * 4)
    return;
  AcquireSRWLockExclusive(&g_character_preview_lock);
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(g_character_preview.rgba +
                    static_cast<size_t>(y) * width * 4,
                rgba + static_cast<size_t>(y) * row_pitch,
                static_cast<size_t>(width) * 4);
  }
  g_character_preview.width = width;
  g_character_preview.height = height;
  g_character_preview.source_revision = source_revision;
  g_character_preview.ready = true;
  ReleaseSRWLockExclusive(&g_character_preview_lock);
  g_character_preview_generation.fetch_add(1, std::memory_order_acq_rel);
}

extern "C" __declspec(dllexport) void __stdcall SDS_SubmitMapPreview(
    uint32_t width, uint32_t height, const uint8_t* rgba,
    uint32_t row_pitch, uint32_t world_id, uint64_t source_revision,
    const ProjectedMapMarker* markers, uint32_t marker_count) {
  if (!g_native_map_enabled) return;
  const bool invalid_surface = !rgba || !width || !height ||
      width > kNativeMapMaxWidth || height > kNativeMapMaxHeight ||
      row_pitch < width * 4 || row_pitch > kNativeMapMaxWidth * 4 ||
      !world_id || marker_count > kMaxMapMarkers ||
      (marker_count && !markers);
  uint64_t current_epoch = UINT64_MAX;
  uint32_t current_world = 0;
  bool in_game = false;
  AcquireSRWLockShared(&g_state_lock);
  if (g_have_state) {
    current_epoch = g_state.world_epoch;
    current_world = g_state.world_form_id;
    in_game = g_state.in_game != 0;
  }
  ReleaseSRWLockShared(&g_state_lock);
  const uint32_t frame_epoch = static_cast<uint32_t>(source_revision >> 32);
  const bool stale = !in_game || current_epoch == UINT64_MAX ||
      frame_epoch != static_cast<uint32_t>(current_epoch) ||
      current_world != world_id;
  if (invalid_surface || stale) {
    DIS_WARN("[map-native] rejected frame reason=%s world=%08X/%08X "
             "epoch=%u/%llu size=%ux%u pitch=%u markers=%u",
             invalid_surface ? "invalid-surface" : "stale-world", world_id,
             current_world, frame_epoch,
             static_cast<unsigned long long>(current_epoch), width, height,
             row_pitch, marker_count);
    return;
  }

  AcquireSRWLockExclusive(&g_map_preview_lock);
  const bool non_monotonic = g_map_preview.ready &&
      g_map_preview.world_id == world_id &&
      source_revision <= g_map_preview.source_revision;
  if (non_monotonic) {
    ReleaseSRWLockExclusive(&g_map_preview_lock);
    return;
  }
  for (uint32_t y = 0; y < height; ++y) {
    std::memcpy(g_map_preview.rgba +
                    static_cast<size_t>(y) * width * 4,
                rgba + static_cast<size_t>(y) * row_pitch,
                static_cast<size_t>(width) * 4);
  }
  g_map_preview.width = width;
  g_map_preview.height = height;
  g_map_preview.world_id = world_id;
  g_map_preview.source_revision = source_revision;
  g_map_preview.marker_count = marker_count;
  if (marker_count)
    std::memcpy(g_map_preview.markers, markers,
                static_cast<size_t>(marker_count) * sizeof(markers[0]));
  g_map_preview.ready = true;
  ReleaseSRWLockExclusive(&g_map_preview_lock);
  const uint64_t generation =
      g_map_preview_generation.fetch_add(1, std::memory_order_acq_rel) + 1;
  ++g_map_frame_count;
  const uint64_t now = GetTickCount64();
  if (g_map_frame_count == 1 || now - g_last_map_frame_log_ms >= 10000) {
    DIS_INFO("[map-native] accepted frame=%llu generation=%llu world=%08X "
             "revision=%llu size=%ux%u markers=%u",
             static_cast<unsigned long long>(g_map_frame_count),
             static_cast<unsigned long long>(generation), world_id,
             static_cast<unsigned long long>(source_revision), width, height,
             marker_count);
    g_last_map_frame_log_ms = now;
  }
}

extern "C" __declspec(dllexport) int __stdcall SDS_PollCommand(Command*) {
  // ABI 0x0001000F and earlier did not pass a destination size. Command grew
  // when native map controls were added, so writing through this legacy entry
  // point could corrupt an old plugin's stack. Fail closed without touching
  // the caller; a matching current plugin uses SDS_PollCommandSized below.
  return 0;
}

extern "C" __declspec(dllexport) int __stdcall SDS_PollCommandSized(
    Command* command, uint32_t command_size, uint32_t bridge_version) {
  if (!command || command_size != sizeof(Command) ||
      bridge_version != kBridgeVersion)
    return 0;
  bool found = false;
  AcquireSRWLockExclusive(&g_command_lock);
  if (g_command_count) {
    *command = g_commands[g_command_read];
    g_command_read = (g_command_read + 1) % kCommandCapacity;
    --g_command_count; found = true;
  } else if (g_preview_command_count) {
    *command = g_preview_commands[g_preview_command_read];
    g_preview_command_read =
        (g_preview_command_read + 1) % kPreviewCommandCapacity;
    --g_preview_command_count;
    found = true;
  }
  g_pending_command_count.store(g_command_count + g_preview_command_count,
                                std::memory_order_release);
  ReleaseSRWLockExclusive(&g_command_lock);
  return found ? 1 : 0;
}

extern "C" __declspec(dllexport) int __stdcall SDS_HasPendingCommand() {
  return g_pending_command_count.load(std::memory_order_acquire) != 0 ? 1 : 0;
}

extern "C" __declspec(dllexport) void __stdcall SDS_ReportActionSuccess(
    CommandType type, uint32_t form_id, int32_t index) {
  // Only user-authored game mutations deserve an action cue. Preview, map,
  // sampling, and repaint commands are intentionally excluded.
  if (type != CommandType::kActivateItem &&
      type != CommandType::kActivateMagic &&
      type != CommandType::kActivateFavorite &&
      type != CommandType::kAssignHotbar)
    return;

  static constexpr char kMessage[] = "haptic action";
  bool sent = false;
  AcquireSRWLockShared(&g_touch_feedback_lock);
  if (g_touch_socket != INVALID_SOCKET && g_touch_feedback_peer_valid) {
    sent = sendto(g_touch_socket, kMessage,
                  static_cast<int>(sizeof(kMessage) - 1), 0,
                  reinterpret_cast<const sockaddr*>(&g_touch_feedback_peer),
                  sizeof(g_touch_feedback_peer)) ==
           static_cast<int>(sizeof(kMessage) - 1);
  }
  ReleaseSRWLockShared(&g_touch_feedback_lock);

  const uint64_t count = sent
      ? g_touch_haptic_success_count.fetch_add(1, std::memory_order_relaxed) + 1
      : g_touch_haptic_success_count.load(std::memory_order_relaxed);
  if (count <= 3 || (!sent && count == 0)) {
    DIS_INFO("[haptic] action type=%u form=%08X index=%d sent=%u count=%llu",
             static_cast<unsigned>(type), form_id, index, sent ? 1u : 0u,
             static_cast<unsigned long long>(count));
  }
}

extern "C" __declspec(dllexport) int __stdcall SDS_EnableRenderer() {
  ModOnDxgiUsed();
  if (!g_enabled) return 0;
  return StartPanelThread() ? 1 : 0;
}

void InitializeMod() {
  LogOpen(L"SkyrimSecondScreen.log");
  DIS_INFO("Skyrim Special Edition 1.5.97 dual-screen bootstrap");
  DIS_INFO("[build] proxy_id=%s bridge=0x%08X snapshot_bytes=%u",
           kProxyBuildIdentity, kBridgeVersion,
           static_cast<unsigned>(sizeof(Snapshot)));
  // This proxy is a process-lifetime component. Pinning prevents a late
  // FreeLibrary from unmapping the two isolated worker threads; no waiting or
  // synchronization is ever attempted from DllMain.
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&ModOnDxgiUsed),
                          &g_proxy_module)) {
    DIS_WARN("[panel] proxy module could not be pinned (%lu)", GetLastError());
  }
  const DWORD path_length = GetModuleFileNameW(nullptr, g_ini_path, MAX_PATH);
  if (path_length && path_length < MAX_PATH) {
    wchar_t* slash = wcsrchr(g_ini_path, L'\\');
    if (slash) wcscpy_s(slash + 1, static_cast<size_t>(MAX_PATH - (slash + 1 - g_ini_path)),
                        L"SkyrimSecondScreen.ini");
  }
  g_enabled = GetPrivateProfileIntW(L"panel", L"enabled", 1, g_ini_path) != 0;
  g_touch_port = static_cast<unsigned short>(std::clamp<UINT>(
      GetPrivateProfileIntW(L"ui", L"touch_port", 7788, g_ini_path), 1024u, 65535u));
  const bool safe_mode =
      GetPrivateProfileIntW(L"ui", L"safe_mode", 0, g_ini_path) != 0;
  const bool native_items_requested =
      GetPrivateProfileIntW(L"ui", L"enable_native_item_models", 1,
                            g_ini_path) != 0;
  const bool character_preview_requested =
      GetPrivateProfileIntW(L"ui", L"enable_character_preview", 1,
                            g_ini_path) != 0;
  const bool native_map_requested =
      GetPrivateProfileIntW(L"ui", L"enable_native_map", 1,
                            g_ini_path) != 0;
  g_native_item_models_enabled = !safe_mode && native_items_requested;
  // This enables only the isolated custom NIF/DDS renderer. The retired actor
  // clone and shared-game-camera implementation remains permanently gated in
  // the SKSE plugin.
  g_native_character_preview_enabled = !safe_mode && character_preview_requested;
  g_native_map_enabled = !kNativeMapTemporarilyDisabled && !safe_mode &&
                         native_map_requested;
  if (g_native_item_models_enabled) {
    SetNativeItemPreviewCallbacks(&DrawCachedItemPreview,
                                  &RequestCachedItemPreview,
                                  &BeginCachedItemPreviewComposition);
    DIS_INFO("[item-preview] Skyrim-native item models enabled (isolated off-screen target)");
  } else {
    SetNativeItemPreviewCallbacks(nullptr, nullptr, nullptr);
    DIS_WARN("[item-preview] native item models disabled by configuration");
  }
  SetNativeCharacterPreviewCallback(
      g_native_character_preview_enabled ? &DrawCachedCharacterPreview
                                         : nullptr);
  SetNativeMapPreviewCallback(g_native_map_enabled
                                  ? &DrawCachedMapPreview
                                  : nullptr);
  DIS_INFO("[ui] native features safe_mode=%u "
           "requested(items=%u character=%u map=%u) "
           "effective(items=%u character=%u map=%u)",
           safe_mode ? 1u : 0u, native_items_requested ? 1u : 0u,
           character_preview_requested ? 1u : 0u,
           native_map_requested ? 1u : 0u,
           g_native_item_models_enabled ? 1u : 0u,
           g_native_character_preview_enabled ? 1u : 0u,
           g_native_map_enabled ? 1u : 0u);
  if (!g_enabled) {
    DIS_INFO("[panel] disabled by SkyrimSecondScreen.ini");
    return;
  }
  // Keyboard input is needed by Skyrim's pre-world text boxes (notably
  // character naming), before the game-data-safe renderer attach is allowed.
  // Both listeners are isolated from D3D and gameplay memory. The lower window
  // is CPU/GDI-only and never hooks or submits work to Skyrim's Present path.
  StartTouch();
  if (StartPanelThread()) {
    DIS_INFO("[panel] startup waiting screen requested on isolated GDI thread");
  } else {
    DIS_ERR("[panel] startup waiting screen could not be started");
  }
}

void ModOnDxgiUsed() {
  std::call_once(g_start_once, InitializeMod);
}

}  // namespace dis
