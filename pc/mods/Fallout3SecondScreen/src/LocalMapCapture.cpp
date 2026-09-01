#include "LocalMapCapture.h"

#include <windows.h>
#include <d3d9.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "LocalMapFrame.h"
#include "Log.h"
#include "Perf.h"
#include "Signatures.h"

namespace fo3ds {
namespace {

constexpr uint64_t kChunkRetryDelayMs = 100;
constexpr uint32_t kMaxTextureDimension = 4096;
constexpr uint32_t kFowGridSide = 17;
constexpr uint32_t kTileCount = kLocalMapGridSide * kLocalMapGridSide;

// Fallout's native local-map renderer produces one cell texture at a time.
// Capture the cell containing the player first, then expand in rings.  The old
// row-major order did twelve unrelated cells before it reached the player and
// exposed no frame at all until every cell was complete.
constexpr uint8_t kCenterOutTileOrder[kTileCount] = {
    12,
    7, 8, 13, 18, 17, 16, 11, 6,
    2, 3, 4, 9, 14, 19, 24, 23, 22, 21, 20, 15, 10, 5, 0, 1,
};

extern "C" void* Fo3dsCallThisPointer(uintptr_t function, void* self);
extern "C" void* Fo3dsCallThisPointer1(uintptr_t function, void* self,
                                       void* argument);
extern "C" void* Fo3dsCallThisPointer2(uintptr_t function, void* self,
                                       void* argument1, uint32_t argument2);
extern "C" void* Fo3dsCallThisPointer3(uintptr_t function, void* self,
                                       void* argument1, uint32_t argument2,
                                       uint32_t argument3);
extern "C" void Fo3dsCallThisVoidNoArgs(uintptr_t function, void* self);
extern "C" void Fo3dsCallThisVoid1(uintptr_t function, void* self,
                                   void* argument);
extern "C" void Fo3dsCallThisVoid3(uintptr_t function, void* self,
                                   uintptr_t argument1, uintptr_t argument2,
                                   uintptr_t argument3);
using LocalMapFowCount = int(__cdecl*)(const float* point);

struct Builder {
  bool active = false;
  bool interior = false;
  uint64_t epoch = 0;
  uint32_t cell_form_id = 0;
  uint32_t chunk = 0;
  uint32_t coalesced_markers = 0;
  int32_t base_x = 0;
  int32_t base_y = 0;
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_heading = 0.0f;
  uint64_t next_attempt_ms = 0;
  std::vector<LocalMapMarker> markers;
  std::vector<uint32_t> pixels;
};

BuildIdentity g_build = {};
uintptr_t g_module_base = 0;
uintptr_t g_transform_point = 0;
uintptr_t g_prepare_exterior = 0;
uintptr_t g_prepare_interior = 0;
uintptr_t g_exterior_texture = 0;
uintptr_t g_interior_texture = 0;
uintptr_t g_cell_lookup = 0;
uintptr_t g_quest_target_list_getter = 0;
uintptr_t g_quest_target_reference_getter = 0;
uintptr_t g_local_map_lock_global = 0;
uintptr_t g_texture_data_rtti = 0;
LocalMapFowCount g_fow_count = nullptr;
Builder g_builder;
uint64_t g_next_revision = 1;
uint64_t g_next_cursor_revision = 1;
uint64_t g_reset_epoch = 0;
bool g_ready = false;
std::atomic<LocalMapCaptureStatus> g_status{
    LocalMapCaptureStatus::kUnavailable};

bool ExactBytes(uintptr_t address, const uint8_t* bytes, size_t count) {
  return g_build.text.Contains(address, count) && IsReadable(
      reinterpret_cast<const void*>(address), count) &&
      std::memcmp(reinterpret_cast<const void*>(address), bytes, count) == 0;
}

uint32_t ReadFormId(const void* form) {
  if (!form || !IsReadable(form, 0x10)) return 0;
  return *reinterpret_cast<const uint32_t*>(
      reinterpret_cast<uintptr_t>(form) + 0x0C);
}

int32_t FloorCell(int32_t coordinate) {
  if (coordinate >= 0) return coordinate / 4096;
  return -static_cast<int32_t>(
      (static_cast<uint32_t>(-static_cast<int64_t>(coordinate)) + 4095u) /
      4096u);
}

struct RawListNode {
  void* item;
  RawListNode* next;
};

bool ReadListNode(const RawListNode* node, void** item,
                  RawListNode** next) {
  if (!node || !item || !next || !IsReadable(node, sizeof(*node)))
    return false;
  std::memcpy(item, &node->item, sizeof(*item));
  std::memcpy(next, &node->next, sizeof(*next));
  return !*next || IsReadable(*next, sizeof(RawListNode));
}

bool ReadReferenceMapPosition(void* reference, float* output) {
  if (!reference || !output || !IsReadable(reference, 0x40)) return false;
  uintptr_t vtable = 0;
  std::memcpy(&vtable, reference, sizeof(vtable));
  if (!g_build.rdata.Contains(vtable, 0x1F8) ||
      !IsReadable(reinterpret_cast<void*>(vtable + 0x1F4),
                  sizeof(uintptr_t)))
    return false;
  uintptr_t accessor = 0;
  std::memcpy(&accessor, reinterpret_cast<void*>(vtable + 0x1F4),
              sizeof(accessor));
  if (!g_build.text.Contains(accessor)) return false;
  const void* position = Fo3dsCallThisPointer(accessor, reference);
  if (!position || !IsReadable(position, sizeof(float) * 3)) return false;
  std::memcpy(output, position, sizeof(float) * 3);
  return std::isfinite(output[0]) && std::isfinite(output[1]) &&
         std::isfinite(output[2]);
}

// MapMenu's 1.7.0.4 local-marker producer walks TESObjectCELL+0xAC and
// admits door references after these exact three native predicates:
// reference flag 0x00800000, reference flag 0x40, and door flag 0x04 must all
// be clear. Reproducing those predicates here supplies the same door pins
// without allocating or opening a hidden MapMenu.
bool CaptureLocalDoorMarkers(void* cell, bool interior,
                             std::vector<LocalMapMarker>* output) {
  if (!cell || !output || !IsReadable(cell, 0xB4)) return false;
  output->clear();
  auto* cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(cell) + 0xAC);
  for (uint32_t visited = 0;
       cursor && visited < 8192 && output->size() < kMaxLocalMapMarkers;
       ++visited) {
    void* reference = nullptr;
    RawListNode* next = nullptr;
    if (!ReadListNode(cursor, &reference, &next)) return false;
    if (reference && IsReadable(reference, 0x44)) {
      void* base = nullptr;
      uint32_t reference_flags = 0;
      std::memcpy(&base, reinterpret_cast<uint8_t*>(reference) + 0x1C,
                  sizeof(base));
      std::memcpy(&reference_flags,
                  reinterpret_cast<uint8_t*>(reference) + 0x08,
                  sizeof(reference_flags));
      if (base && IsReadable(base, 0x85) &&
          *reinterpret_cast<const uint8_t*>(
              reinterpret_cast<uint8_t*>(base) + 0x04) == 0x1C &&
          !(reference_flags & 0x00800000u) &&
          !(reference_flags & 0x00000040u) &&
          !(*reinterpret_cast<const uint8_t*>(
                reinterpret_cast<uint8_t*>(base) + 0x84) & 0x04u)) {
        float native_position[3] = {};
        if (!ReadReferenceMapPosition(reference, native_position)) {
          cursor = next;
          continue;
        }
        float mapped[3] = {native_position[0], native_position[1],
                           native_position[2]};
        if (interior)
          Fo3dsCallThisVoid3(g_transform_point, cell,
                            reinterpret_cast<uintptr_t>(native_position),
                            reinterpret_cast<uintptr_t>(mapped), 1);
        const uint32_t form_id = ReadFormId(reference);
        if (form_id && std::isfinite(mapped[0]) &&
            std::isfinite(mapped[1]) && std::isfinite(mapped[2])) {
          LocalMapMarker marker = {};
          marker.form_id = form_id;
          marker.flags = kLocalMapMarkerDoor;
          marker.x = mapped[0];
          marker.y = mapped[1];
          marker.z = mapped[2];
          output->push_back(marker);
        }
      }
    }
    cursor = next;
  }
  return cursor == nullptr || output->size() == kMaxLocalMapMarkers;
}

// This is the same source used by MapMenu::UpdateQuestMarkers in the exact
// 1.7.0.4 executable: PlayerCharacter::GetQuestTargetList (0x008B9170), then
// BGSQuestTarget::GetRef(true) (0x00776440). Only targets in the current local
// cell belong on this raster. Cross-cell objectives are represented by the
// native door pins until their route enters the current cell, matching the
// Local Map's cell-local coordinate domain.
bool CaptureLocalQuestMarkers(void* player, void* cell, bool interior,
                              std::vector<LocalMapMarker>* output) {
  if (!player || !cell || !output || !g_quest_target_list_getter ||
      !g_quest_target_reference_getter)
    return false;
  output->clear();
  auto* cursor = reinterpret_cast<RawListNode*>(Fo3dsCallThisPointer(
      g_quest_target_list_getter, player));
  for (uint32_t visited = 0;
       cursor && visited < 8192 && output->size() < kMaxLocalMapMarkers;
       ++visited) {
    void* target_data = nullptr;
    RawListNode* next = nullptr;
    if (!ReadListNode(cursor, &target_data, &next)) return false;
    if (target_data && IsReadable(target_data, 0x10)) {
      void* reference = Fo3dsCallThisPointer1(
          g_quest_target_reference_getter, target_data,
          reinterpret_cast<void*>(static_cast<uintptr_t>(1)));
      if (reference && IsReadable(reference, 0x40)) {
        void* parent_cell = nullptr;
        std::memcpy(&parent_cell,
                    reinterpret_cast<uint8_t*>(reference) + 0x3C,
                    sizeof(parent_cell));
        if (parent_cell == cell) {
          float native_position[3] = {};
          if (ReadReferenceMapPosition(reference, native_position)) {
            float mapped[3] = {native_position[0], native_position[1],
                               native_position[2]};
            if (interior)
              Fo3dsCallThisVoid3(
                  g_transform_point, cell,
                  reinterpret_cast<uintptr_t>(native_position),
                  reinterpret_cast<uintptr_t>(mapped), 1);
            const uint32_t form_id = ReadFormId(reference);
            if (form_id && std::isfinite(mapped[0]) &&
                std::isfinite(mapped[1]) && std::isfinite(mapped[2])) {
              LocalMapMarker marker = {};
              marker.form_id = form_id;
              marker.flags = kLocalMapMarkerQuest;
              marker.x = mapped[0];
              marker.y = mapped[1];
              marker.z = mapped[2];
              output->push_back(marker);
            }
          }
        }
      }
    }
    cursor = next;
  }
  return cursor == nullptr || output->size() == kMaxLocalMapMarkers;
}

void ReleaseNiReference(void* object) {
  if (!object || !IsReadable(object, 2 * sizeof(uintptr_t))) return;
  auto* reference_count = reinterpret_cast<volatile LONG*>(
      reinterpret_cast<uint8_t*>(object) + sizeof(uintptr_t));
  const LONG previous = InterlockedExchangeAdd(reference_count, -1);
  if (previous != 1) {
    // A non-positive count is not a valid owned NiPointer. Restore it instead
    // of underflowing memory that was only borrowed from the renderer.
    if (previous <= 0) InterlockedIncrement(reference_count);
    return;
  }
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(object);
  if (!g_build.rdata.Contains(vtable, 2 * sizeof(uintptr_t)) ||
      !IsReadable(reinterpret_cast<const void*>(vtable),
                  2 * sizeof(uintptr_t)))
    return;
  const uintptr_t destroy = reinterpret_cast<const uintptr_t*>(vtable)[1];
  if (g_build.text.Contains(destroy))
    Fo3dsCallThisVoidNoArgs(destroy, object);
}

class ScopedNiReference {
 public:
  explicit ScopedNiReference(void* object) : object_(object) {}
  ~ScopedNiReference() { ReleaseNiReference(object_); }
  ScopedNiReference(const ScopedNiReference&) = delete;
  ScopedNiReference& operator=(const ScopedNiReference&) = delete;

 private:
  void* object_;
};

bool RendererDataTypeValid(void* renderer_data) {
  if (!renderer_data || !IsReadable(renderer_data, sizeof(uintptr_t)))
    return false;
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(renderer_data);
  if (!g_build.rdata.Contains(vtable, 40 * sizeof(uintptr_t)) ||
      !IsReadable(reinterpret_cast<const void*>(vtable),
                  40 * sizeof(uintptr_t)))
    return false;
  const uintptr_t get_rtti = reinterpret_cast<const uintptr_t*>(vtable)[2];
  const uintptr_t get_texture = reinterpret_cast<const uintptr_t*>(vtable)[39];
  if (!g_build.text.Contains(get_rtti) || !g_build.text.Contains(get_texture))
    return false;
  void* rtti = Fo3dsCallThisPointer(get_rtti, renderer_data);
  for (uint32_t depth = 0; rtti && depth < 32; ++depth) {
    if (reinterpret_cast<uintptr_t>(rtti) == g_texture_data_rtti) return true;
    if (!IsReadable(rtti, 2 * sizeof(uintptr_t))) return false;
    std::memcpy(&rtti, reinterpret_cast<const uint8_t*>(rtti) + 4,
                sizeof(rtti));
  }
  return false;
}

uint32_t ConvertPixel(const uint8_t* source, D3DFORMAT format) {
  if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8) {
    const uint32_t value = *reinterpret_cast<const uint32_t*>(source);
    return 0xFF000000u | (value & 0x00FFFFFFu);
  }
  const uint16_t value = *reinterpret_cast<const uint16_t*>(source);
  if (format == D3DFMT_R5G6B5) {
    const uint32_t red = ((value >> 11) & 31u) * 255u / 31u;
    const uint32_t green = ((value >> 5) & 63u) * 255u / 63u;
    const uint32_t blue = (value & 31u) * 255u / 31u;
    return 0xFF000000u | (red << 16) | (green << 8) | blue;
  }
  const uint32_t red = ((value >> 10) & 31u) * 255u / 31u;
  const uint32_t green = ((value >> 5) & 31u) * 255u / 31u;
  const uint32_t blue = (value & 31u) * 255u / 31u;
  return 0xFF000000u | (red << 16) | (green << 8) | blue;
}

bool SupportedFormat(D3DFORMAT format, uint32_t* bytes_per_pixel) {
  if (!bytes_per_pixel) return false;
  if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8) {
    *bytes_per_pixel = 4;
    return true;
  }
  if (format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5 ||
      format == D3DFMT_A1R5G5B5) {
    *bytes_per_pixel = 2;
    return true;
  }
  return false;
}

uint32_t PipGreen(uint32_t bgra, uint8_t fow) {
  const uint32_t blue = bgra & 0xFFu;
  const uint32_t green = (bgra >> 8) & 0xFFu;
  const uint32_t red = (bgra >> 16) & 0xFFu;
  uint32_t luminance = (red * 54u + green * 183u + blue * 19u) >> 8;
  luminance = luminance * std::min<uint32_t>(fow, 4u) / 4u;
  const uint32_t out_red = luminance * 86u / 255u;
  const uint32_t out_green = luminance * 246u / 255u;
  const uint32_t out_blue = luminance * 146u / 255u;
  return 0xFF000000u | (out_red << 16) | (out_green << 8) | out_blue;
}

bool CopyTexture(void* rendered_texture, const uint8_t* fow,
                 std::vector<uint32_t>* output) {
  if (!rendered_texture || !output ||
      !IsReadable(reinterpret_cast<uint8_t*>(rendered_texture) + 0x24,
                  sizeof(void*)))
    return false;
  void* renderer_data = nullptr;
  std::memcpy(&renderer_data,
              reinterpret_cast<uint8_t*>(rendered_texture) + 0x24,
              sizeof(renderer_data));
  if (!RendererDataTypeValid(renderer_data)) return false;
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(renderer_data);
  const uintptr_t get_texture = reinterpret_cast<const uintptr_t*>(vtable)[39];
  auto* borrowed = static_cast<IDirect3DBaseTexture9*>(
      Fo3dsCallThisPointer1(get_texture, renderer_data, nullptr));
  if (!borrowed || borrowed->GetType() != D3DRTYPE_TEXTURE) return false;

  auto* texture = reinterpret_cast<IDirect3DTexture9*>(borrowed);
  texture->AddRef();
  IDirect3DSurface9* source_surface = nullptr;
  IDirect3DSurface9* readback_surface = nullptr;
  IDirect3DDevice9* device = nullptr;
  bool copied = false;
  D3DSURFACE_DESC description = {};
  if (SUCCEEDED(texture->GetLevelDesc(0, &description)) &&
      description.Width > 0 && description.Width <= kMaxTextureDimension &&
      description.Height > 0 && description.Height <= kMaxTextureDimension &&
      SUCCEEDED(texture->GetSurfaceLevel(0, &source_surface)) &&
      source_surface && SUCCEEDED(texture->GetDevice(&device)) && device) {
    uint32_t bytes_per_pixel = 0;
    if (SupportedFormat(description.Format, &bytes_per_pixel) &&
        SUCCEEDED(device->CreateOffscreenPlainSurface(
            description.Width, description.Height, description.Format,
            D3DPOOL_SYSTEMMEM, &readback_surface, nullptr)) &&
        readback_surface &&
        SUCCEEDED(device->GetRenderTargetData(source_surface,
                                              readback_surface))) {
      D3DLOCKED_RECT locked = {};
      if (SUCCEEDED(readback_surface->LockRect(&locked, nullptr,
                                               D3DLOCK_READONLY)) &&
          locked.pBits && locked.Pitch > 0) {
        output->assign(static_cast<size_t>(kLocalMapTileSide) *
                           kLocalMapTileSide,
                       0xFF000000u);
        for (uint32_t y = 0; y < kLocalMapTileSide; ++y) {
          const uint32_t source_y =
              std::min(description.Height - 1,
                       y * description.Height / kLocalMapTileSide);
          const auto* row = static_cast<const uint8_t*>(locked.pBits) +
                            static_cast<size_t>(source_y) * locked.Pitch;
          for (uint32_t x = 0; x < kLocalMapTileSide; ++x) {
            const uint32_t source_x =
                std::min(description.Width - 1,
                         x * description.Width / kLocalMapTileSide);
            const uint32_t mask_x = x * (kFowGridSide - 1) /
                                    (kLocalMapTileSide - 1);
            const uint32_t mask_y = (kLocalMapTileSide - 1 - y) *
                                    (kFowGridSide - 1) /
                                    (kLocalMapTileSide - 1);
            const uint8_t coverage =
                fow ? fow[mask_y * kFowGridSide + mask_x] : 4;
            (*output)[static_cast<size_t>(y) * kLocalMapTileSide + x] =
                PipGreen(ConvertPixel(row +
                                      static_cast<size_t>(source_x) *
                                          bytes_per_pixel,
                                      description.Format),
                         coverage);
          }
        }
        readback_surface->UnlockRect();
        copied = true;
      }
    }
  }
  if (readback_surface) readback_surface->Release();
  if (source_surface) source_surface->Release();
  if (device) device->Release();
  texture->Release();
  return copied;
}

bool StartBuild(void* player, uint64_t epoch) {
  if (!player || !IsReadable(player, 0x40)) return false;
  void* cell = nullptr;
  std::memcpy(&cell, reinterpret_cast<uint8_t*>(player) + 0x3C,
              sizeof(cell));
  if (!cell || !IsReadable(cell, 0xC4)) return false;
  const uint32_t cell_form_id = ReadFormId(cell);
  if (!cell_form_id) return false;
  float position[3] = {};
  // MapMenu does not read TESObjectREFR+0x2C directly.  It asks the
  // reference's virtual position accessor (vtable + 0x1F4), then applies the
  // interior-cell transform to that result.  PlayerCharacter can expose a
  // transformed position through that accessor, so mixing the raw actor
  // fields with virtual-position door/quest markers puts the cursor, pins and
  // generated raster in different coordinate domains.
  if (!ReadReferenceMapPosition(player, position)) return false;
  float raw_position[3] = {};
  std::memcpy(raw_position, reinterpret_cast<uint8_t*>(player) + 0x2C,
              sizeof(raw_position));
  if (std::isfinite(raw_position[0]) && std::isfinite(raw_position[1]) &&
      (std::fabs(raw_position[0] - position[0]) > 0.25f ||
       std::fabs(raw_position[1] - position[1]) > 0.25f)) {
    LogRateLimited(
        "local-map-player-domain", 10000, LogLevel::kDebug,
        "[LOCAL-MAP] native virtual player position selected raw=%.2f,%.2f map=%.2f,%.2f",
        raw_position[0], raw_position[1], position[0], position[1]);
  }
  float heading = 0.0f;
  std::memcpy(&heading, reinterpret_cast<uint8_t*>(player) + 0x28,
              sizeof(heading));
  if (!std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2]) || !std::isfinite(heading))
    return false;
  const bool interior =
      (*reinterpret_cast<const uint8_t*>(
          reinterpret_cast<uintptr_t>(cell) + 0x24) & 1u) != 0;
  float mapped[3] = {position[0], position[1], position[2]};
  if (interior)
    Fo3dsCallThisVoid3(g_transform_point, cell,
                      reinterpret_cast<uintptr_t>(position),
                      reinterpret_cast<uintptr_t>(mapped), 1);
  if (!std::isfinite(mapped[0]) || !std::isfinite(mapped[1])) return false;
  const int32_t x = static_cast<int32_t>(std::lrint(mapped[0]));
  const int32_t y = static_cast<int32_t>(std::lrint(mapped[1]));
  const int32_t half = static_cast<int32_t>(kLocalMapGridSide / 2);
  g_builder.active = true;
  g_builder.interior = interior;
  g_builder.epoch = epoch;
  g_builder.cell_form_id = cell_form_id;
  g_builder.chunk = 0;
  g_builder.base_x = FloorCell(interior ? x - 2048 : x) - half;
  g_builder.base_y = FloorCell(interior ? y - 2048 : y) - half;
  g_builder.player_x = mapped[0];
  g_builder.player_y = mapped[1];
  g_builder.player_heading = heading;
  g_builder.next_attempt_ms = 0;
  std::vector<LocalMapMarker> current_cell_markers;
  if (!CaptureLocalDoorMarkers(cell, interior, &current_cell_markers)) {
    LogRateLimited("local-map-markers", 5000, LogLevel::kWarning,
                   "[LOCAL-MAP] native door-marker list was unstable; frame will retry markers on its next build");
  } else {
    g_builder.coalesced_markers += static_cast<uint32_t>(
        MergeLocalMapMarkers(current_cell_markers, &g_builder.markers));
  }
  std::vector<LocalMapMarker> quest_markers;
  if (!CaptureLocalQuestMarkers(player, cell, interior, &quest_markers)) {
    LogRateLimited("local-map-quest-markers", 5000, LogLevel::kWarning,
                   "[LOCAL-MAP] native quest-target list was unstable; objective pins will retry on the next build");
  } else {
    g_builder.coalesced_markers += static_cast<uint32_t>(
        MergeLocalMapMarkers(quest_markers, &g_builder.markers));
  }
  g_builder.pixels.assign(
      static_cast<size_t>(kLocalMapRasterSide) * kLocalMapRasterSide,
      0xFF000000u);
  g_status.store(LocalMapCaptureStatus::kBuilding,
                 std::memory_order_release);
  Log(LogLevel::kInfo,
      "[LOCAL-MAP] isolated native-cell build started epoch=%llu cell=%08X interior=%u base=%d,%d chunks=%u markers=%u",
      static_cast<unsigned long long>(epoch), cell_form_id,
      interior ? 1u : 0u, g_builder.base_x, g_builder.base_y,
      kLocalMapGridSide * kLocalMapGridSide,
      static_cast<unsigned>(g_builder.markers.size()));
  return true;
}

bool PublishPlayerCursor(void* player, uint64_t epoch,
                         LocalMapCursor* captured = nullptr) {
  if (!player || !IsReadable(player, 0x40)) return false;
  void* cell = nullptr;
  std::memcpy(&cell, reinterpret_cast<uint8_t*>(player) + 0x3C,
              sizeof(cell));
  if (!cell || !IsReadable(cell, 0xC4)) return false;
  LocalMapCursor cursor = {};
  cursor.revision = g_next_cursor_revision++;
  cursor.world_epoch = epoch;
  cursor.cell_form_id = ReadFormId(cell);
  float native_position[3] = {};
  if (!ReadReferenceMapPosition(player, native_position)) return false;
  std::memcpy(&cursor.heading, reinterpret_cast<uint8_t*>(player) + 0x28,
              sizeof(cursor.heading));
  if (!cursor.cell_form_id || !std::isfinite(native_position[0]) ||
      !std::isfinite(native_position[1]) ||
      !std::isfinite(native_position[2]) || !std::isfinite(cursor.heading))
    return false;
  float mapped[3] = {native_position[0], native_position[1],
                     native_position[2]};
  const bool interior =
      (*reinterpret_cast<const uint8_t*>(
          reinterpret_cast<uintptr_t>(cell) + 0x24) & 1u) != 0;
  if (interior)
    Fo3dsCallThisVoid3(g_transform_point, cell,
                      reinterpret_cast<uintptr_t>(native_position),
                      reinterpret_cast<uintptr_t>(mapped), 1);
  if (!std::isfinite(mapped[0]) || !std::isfinite(mapped[1])) return false;
  cursor.x = mapped[0];
  cursor.y = mapped[1];
  if (!PublishLocalMapCursor(cursor)) return false;
  if (captured) *captured = cursor;
  return true;
}

void BuildFow(int32_t cell_x, int32_t cell_y, uint8_t* output) {
  if (!output) return;
  std::fill_n(output, kFowGridSide * kFowGridSide, static_cast<uint8_t>(4));
  if (!g_fow_count) return;
  const float origin_offset = g_builder.interior ? 2048.0f : 0.0f;
  for (uint32_t y = 0; y < kFowGridSide; ++y) {
    for (uint32_t x = 0; x < kFowGridSide; ++x) {
      const float point[3] = {
          static_cast<float>(cell_x) * 4096.0f + origin_offset +
              static_cast<float>(x) * 256.0f,
          static_cast<float>(cell_y) * 4096.0f + origin_offset +
              static_cast<float>(y) * 256.0f,
          0.0f};
      output[y * kFowGridSide + x] = static_cast<uint8_t>(
          std::clamp(g_fow_count(point), 0, 4));
    }
  }
}

void PasteTile(uint32_t row, uint32_t column,
               const std::vector<uint32_t>& tile) {
  if (tile.size() !=
      static_cast<size_t>(kLocalMapTileSide) * kLocalMapTileSide)
    return;
  const uint32_t destination_y =
      (kLocalMapGridSide - 1 - row) * kLocalMapTileSide;
  const uint32_t destination_x = column * kLocalMapTileSide;
  for (uint32_t y = 0; y < kLocalMapTileSide; ++y) {
    const size_t destination =
        static_cast<size_t>(destination_y + y) * kLocalMapRasterSide +
        destination_x;
    std::copy_n(tile.data() + static_cast<size_t>(y) * kLocalMapTileSide,
                kLocalMapTileSide, g_builder.pixels.data() + destination);
  }
}

bool CaptureChunk(void* player, bool* copied_tile) {
  if (copied_tile) *copied_tile = false;
  if (!player || !IsReadable(player, 0x40)) return false;
  void* current_cell = nullptr;
  std::memcpy(&current_cell, reinterpret_cast<uint8_t*>(player) + 0x3C,
              sizeof(current_cell));
  if (!current_cell || ReadFormId(current_cell) != g_builder.cell_form_id ||
      !IsReadable(current_cell, 0xC4))
    return false;
  if (g_builder.chunk >= kTileCount) return false;
  const uint32_t tile_index = kCenterOutTileOrder[g_builder.chunk];
  const uint32_t row = tile_index / kLocalMapGridSide;
  const uint32_t column = tile_index % kLocalMapGridSide;
  const int32_t cell_x = g_builder.base_x + static_cast<int32_t>(column);
  const int32_t cell_y = g_builder.base_y + static_cast<int32_t>(row);
  void* render_cell = current_cell;
  if (g_builder.interior) {
    Fo3dsCallThisPointer3(
        g_prepare_interior, current_cell,
        reinterpret_cast<void*>(static_cast<uintptr_t>(cell_x)),
        static_cast<uint32_t>(cell_y), 0);
  } else {
    void* world = nullptr;
    std::memcpy(&world, reinterpret_cast<uint8_t*>(current_cell) + 0xC0,
                sizeof(world));
    if (!world) return false;
    render_cell = Fo3dsCallThisPointer2(
        g_cell_lookup, world,
        reinterpret_cast<void*>(static_cast<uintptr_t>(cell_x)),
        static_cast<uint32_t>(cell_y));
    if (!render_cell) {
      ++g_builder.chunk;
      return true;
    }
    Fo3dsCallThisVoidNoArgs(g_prepare_exterior, render_cell);
  }

  // The exterior local-map image is a 5x5 cell mosaic. Capturing markers only
  // from the player's current cell left valid doors in the other 24 rendered
  // cells unlabelled. MapMenu walks each participating cell; do the same while
  // that cell is already resolved for the current bounded chunk.
  std::vector<LocalMapMarker> cell_markers;
  if (CaptureLocalDoorMarkers(render_cell, g_builder.interior,
                              &cell_markers)) {
    g_builder.coalesced_markers += static_cast<uint32_t>(
        MergeLocalMapMarkers(cell_markers, &g_builder.markers));
  } else {
    LogRateLimited("local-map-marker-cell", 5000, LogLevel::kWarning,
                   "[LOCAL-MAP] marker list changed while cell=%08X was sampled",
                   ReadFormId(render_cell));
  }

  if (!IsReadable(reinterpret_cast<void*>(g_local_map_lock_global),
                  sizeof(void*)))
    return false;
  CRITICAL_SECTION* lock = nullptr;
  std::memcpy(&lock, reinterpret_cast<void*>(g_local_map_lock_global),
              sizeof(lock));
  if (!lock || !IsReadable(reinterpret_cast<uint8_t*>(lock) + 0x80,
                           sizeof(CRITICAL_SECTION)))
    return false;
  lock = reinterpret_cast<CRITICAL_SECTION*>(
      reinterpret_cast<uint8_t*>(lock) + 0x80);
  void* rendered_texture = nullptr;
  if (!TryEnterCriticalSection(lock)) return false;
  if (g_builder.interior)
    Fo3dsCallThisVoid3(g_interior_texture, current_cell,
                      static_cast<uintptr_t>(cell_x),
                      static_cast<uintptr_t>(cell_y),
                      reinterpret_cast<uintptr_t>(&rendered_texture));
  else
    Fo3dsCallThisVoid1(g_exterior_texture, render_cell, &rendered_texture);
  LeaveCriticalSection(lock);
  if (!rendered_texture) {
    ++g_builder.chunk;
    return true;
  }
  ScopedNiReference texture_reference(rendered_texture);
  uint8_t fow[kFowGridSide * kFowGridSide] = {};
  BuildFow(cell_x, cell_y, fow);
  std::vector<uint32_t> tile;
  if (!CopyTexture(rendered_texture, fow, &tile)) return false;
  PasteTile(row, column, tile);
  const size_t lit_pixels = static_cast<size_t>(std::count_if(
      tile.begin(), tile.end(), [](uint32_t pixel) {
        return (pixel & 0x00FFFFFFu) != 0;
      }));
  if (g_builder.chunk == 0 || lit_pixels == 0) {
    Log(lit_pixels ? LogLevel::kInfo : LogLevel::kWarning,
        "[LOCAL-MAP] native tile captured ordinal=%u grid=%u,%u lit=%llu/%llu",
        g_builder.chunk, column, row,
        static_cast<unsigned long long>(lit_pixels),
        static_cast<unsigned long long>(tile.size()));
  }
  if (copied_tile) *copied_tile = true;
  ++g_builder.chunk;
  return true;
}

bool PublishBuild(bool complete) {
  auto frame = std::make_shared<LocalMapFrame>();
  frame->revision = g_next_revision++;
  frame->world_epoch = g_builder.epoch;
  frame->cell_form_id = g_builder.cell_form_id;
  frame->width = kLocalMapRasterSide;
  frame->height = kLocalMapRasterSide;
  const float offset = g_builder.interior ? 2048.0f : 0.0f;
  frame->min_x = static_cast<float>(g_builder.base_x) * 4096.0f + offset;
  frame->max_x = static_cast<float>(g_builder.base_x +
                                     static_cast<int32_t>(kLocalMapGridSide)) *
                     4096.0f +
                 offset;
  frame->min_y = static_cast<float>(g_builder.base_y) * 4096.0f + offset;
  frame->max_y = static_cast<float>(g_builder.base_y +
                                     static_cast<int32_t>(kLocalMapGridSide)) *
                     4096.0f +
                 offset;
  frame->player_x = g_builder.player_x;
  frame->player_y = g_builder.player_y;
  frame->player_heading = g_builder.player_heading;
  frame->markers = g_builder.markers;
  if (complete)
    frame->bgra = std::move(g_builder.pixels);
  else
    frame->bgra = g_builder.pixels;
  const uint64_t revision = frame->revision;
  const uint32_t cell = frame->cell_form_id;
  const uint32_t marker_count = static_cast<uint32_t>(frame->markers.size());
  const uint32_t coalesced_markers = g_builder.coalesced_markers;
  const bool published = PublishLocalMapFrame(std::move(frame));
  const uint32_t captured = g_builder.chunk;
  if (complete) g_builder = Builder{};
  if (published) {
    Log(LogLevel::kInfo,
        complete
            ? "[LOCAL-MAP] immutable frame published revision=%llu cell=%08X size=%ux%u chunks=%u markers=%u coalesced=%u"
            : "[LOCAL-MAP] partial native frame published revision=%llu cell=%08X size=%ux%u chunks=%u markers=%u coalesced=%u",
        static_cast<unsigned long long>(revision), cell,
        kLocalMapRasterSide, kLocalMapRasterSide, captured,
        marker_count, coalesced_markers);
  }
  g_status.store(
      published
          ? (complete ? LocalMapCaptureStatus::kReady
                      : LocalMapCaptureStatus::kBuilding)
          : LocalMapCaptureStatus::kRetrying,
      std::memory_order_release);
  return published;
}

}  // namespace

bool InitializeLocalMapCapture(const BuildIdentity& build,
                               uintptr_t module_base) {
  g_ready = false;
  g_status.store(LocalMapCaptureStatus::kUnavailable,
                 std::memory_order_release);
  if (!build.known_candidate || !module_base) return false;
  g_build = build;
  g_module_base = module_base;
  g_transform_point = module_base + sig::kLocalMapTransformPointRva;
  g_prepare_exterior = module_base + sig::kLocalMapPrepareExteriorCellRva;
  g_prepare_interior = module_base + sig::kLocalMapPrepareInteriorCellRva;
  g_exterior_texture = module_base + sig::kLocalMapExteriorTextureRva;
  g_interior_texture = module_base + sig::kLocalMapInteriorTextureRva;
  g_cell_lookup = module_base + sig::kWorldSpaceCellLookupRva;
  g_quest_target_list_getter =
      module_base + sig::kQuestTargetListGetterRva;
  g_quest_target_reference_getter =
      module_base + sig::kQuestTargetReferenceGetterRva;
  g_local_map_lock_global = module_base + sig::kLocalMapLockGlobalRva;
  g_texture_data_rtti = module_base + sig::kNiDx9TextureDataRttiRva;
  g_fow_count = reinterpret_cast<LocalMapFowCount>(
      module_base + sig::kLocalMapFowCountRva);
  const uint8_t transform[] = {0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x08,
                               0x80, 0x7D, 0x10, 0x00};
  const uint8_t exterior_texture[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF,
                                      0x68, 0x55, 0xA2, 0xDE, 0x00};
  const uint8_t interior_texture[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF,
                                      0x68, 0x55, 0xA2, 0xDE, 0x00};
  const uint8_t prepare_exterior[] = {0x83, 0xC1, 0x28, 0xE9};
  const uint8_t prepare_interior[] = {0x55, 0x8B, 0xEC, 0x6A, 0xFF,
                                      0x68, 0xA3, 0xA2, 0xDE, 0x00};
  const uint8_t cell_lookup[] = {0x55, 0x8B, 0xEC, 0x8B, 0x55, 0x08,
                                 0x56, 0x8B, 0xF1};
  const uint8_t fow_count[] = {0x55, 0x8B, 0xEC, 0xA1,
                               0x74, 0xC6, 0x23, 0x01};
  const uint8_t quest_target_list[] = {
      0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x14, 0x06, 0x00,
      0x00, 0x85, 0xC9, 0x75, 0x04, 0x33, 0xC0, 0x5E, 0xC3};
  const uint8_t quest_target_reference[] = {
      0x55, 0x8B, 0xEC, 0x56, 0x8B, 0x71, 0x0C, 0x85,
      0xF6, 0x74, 0x1D, 0x8B, 0x46, 0x08, 0xC1, 0xE8,
      0x05, 0xA8, 0x01};
  g_ready = ExactBytes(g_transform_point, transform, sizeof(transform)) &&
            ExactBytes(g_exterior_texture, exterior_texture,
                       sizeof(exterior_texture)) &&
            ExactBytes(g_interior_texture, interior_texture,
                       sizeof(interior_texture)) &&
            ExactBytes(g_prepare_exterior, prepare_exterior,
                       sizeof(prepare_exterior)) &&
            ExactBytes(g_prepare_interior, prepare_interior,
                       sizeof(prepare_interior)) &&
            ExactBytes(g_cell_lookup, cell_lookup, sizeof(cell_lookup)) &&
            ExactBytes(g_quest_target_list_getter, quest_target_list,
                       sizeof(quest_target_list)) &&
            ExactBytes(g_quest_target_reference_getter,
                       quest_target_reference,
                       sizeof(quest_target_reference)) &&
            ExactBytes(reinterpret_cast<uintptr_t>(g_fow_count), fow_count,
                       sizeof(fow_count)) &&
            IsReadable(reinterpret_cast<void*>(g_local_map_lock_global),
                       sizeof(void*)) &&
            (g_build.rdata.Contains(g_texture_data_rtti) ||
             g_build.data.Contains(g_texture_data_rtti));
  Log(g_ready ? LogLevel::kInfo : LogLevel::kError,
      "[LOCAL-MAP] isolated cell-render bridge validation=%u",
      g_ready ? 1u : 0u);
  g_status.store(g_ready ? LocalMapCaptureStatus::kIdle
                         : LocalMapCaptureStatus::kUnavailable,
                 std::memory_order_release);
  return g_ready;
}

void ResetLocalMapCapture(uint64_t world_epoch) {
  g_builder = Builder{};
  g_reset_epoch = world_epoch;
  ClearLocalMapFrame();
  g_status.store(g_ready ? LocalMapCaptureStatus::kIdle
                         : LocalMapCaptureStatus::kUnavailable,
                 std::memory_order_release);
}

LocalMapPumpResult PumpLocalMapCapture(void* player, uint64_t world_epoch,
                                       bool requested) {
  // Do not start capture work off-page, but once a bounded build has started,
  // finish it even if the user changes tabs.  Previously a page change could
  // strand a valid 15/25-tile build forever.
  if (!g_ready || !player || (!requested && !g_builder.active))
    return LocalMapPumpResult::kIdle;
  if (world_epoch != g_reset_epoch) ResetLocalMapCapture(world_epoch);
  void* cell = nullptr;
  if (!IsReadable(player, 0x40)) return LocalMapPumpResult::kFailed;
  std::memcpy(&cell, reinterpret_cast<uint8_t*>(player) + 0x3C,
              sizeof(cell));
  const uint32_t cell_form_id = ReadFormId(cell);
  if (!cell_form_id) return LocalMapPumpResult::kFailed;
  LocalMapCursor cursor = {};
  if (!PublishPlayerCursor(player, world_epoch, &cursor))
    return LocalMapPumpResult::kFailed;
  const LocalMapFramePtr ready_frame = AcquireLocalMapFrame();
  const bool frame_matches = ready_frame &&
      ready_frame->world_epoch == world_epoch &&
      ready_frame->cell_form_id == cell_form_id;
  const float border = 4096.0f;
  const bool cursor_inside_rebuild_margin = frame_matches &&
      cursor.x >= ready_frame->min_x + border &&
      cursor.x <= ready_frame->max_x - border &&
      cursor.y >= ready_frame->min_y + border &&
      cursor.y <= ready_frame->max_y - border;
  if (!g_builder.active && frame_matches && cursor_inside_rebuild_margin) {
    g_status.store(LocalMapCaptureStatus::kReady,
                   std::memory_order_release);
    return LocalMapPumpResult::kIdle;
  }
  if (!g_builder.active || g_builder.epoch != world_epoch ||
      g_builder.cell_form_id != cell_form_id) {
    g_builder = Builder{};
    if (!StartBuild(player, world_epoch)) return LocalMapPumpResult::kFailed;
  }
  const uint64_t now = GetTickCount64();
  if (now < g_builder.next_attempt_ms) return LocalMapPumpResult::kBuilding;
  g_builder.next_attempt_ms = now + kChunkRetryDelayMs;
  bool copied_tile = false;
  {
    ScopedPerformance capture(PerfDomain::kLocalMapCapture);
    if (!CaptureChunk(player, &copied_tile)) {
      g_status.store(LocalMapCaptureStatus::kRetrying,
                     std::memory_order_release);
      LogRateLimited("local-map-chunk", 5000, LogLevel::kWarning,
                     "[LOCAL-MAP] native cell render/readback rejected chunk=%u cell=%08X; retrying",
                     g_builder.chunk, g_builder.cell_form_id);
      return LocalMapPumpResult::kFailed;
    }
  }
  if (g_builder.chunk > 0 && g_builder.chunk < kTileCount &&
      g_builder.chunk % 5 == 0) {
    Log(LogLevel::kDebug,
        "[LOCAL-MAP] native build progress cell=%08X chunks=%u/%u",
        g_builder.cell_form_id, g_builder.chunk, kTileCount);
  }
  if (g_builder.chunk < kTileCount) {
    // Publish a useful native frame as soon as the player cell exists, then a
    // denser 3x3 frame.  This avoids a blank page while preserving immutable
    // renderer/UI ownership and keeps large copies out of the per-frame path.
    if (copied_tile && (g_builder.chunk == 1 || g_builder.chunk == 9))
      return PublishBuild(false) ? LocalMapPumpResult::kPublished
                                 : LocalMapPumpResult::kFailed;
    return LocalMapPumpResult::kBuilding;
  }
  return PublishBuild(true) ? LocalMapPumpResult::kPublished
                            : LocalMapPumpResult::kFailed;
}

bool IsLocalMapFrameReady(uint64_t world_epoch, uint32_t cell_form_id) {
  const LocalMapFramePtr frame = AcquireLocalMapFrame();
  return frame && frame->world_epoch == world_epoch &&
         frame->cell_form_id == cell_form_id;
}

LocalMapCaptureStatus GetLocalMapCaptureStatus() {
  return g_status.load(std::memory_order_acquire);
}

void ShutdownLocalMapCapture() {
  g_ready = false;
  g_status.store(LocalMapCaptureStatus::kUnavailable,
                 std::memory_order_release);
  g_builder = Builder{};
  ClearLocalMapFrame();
}

}  // namespace fo3ds
