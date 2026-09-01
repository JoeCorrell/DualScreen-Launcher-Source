#include "NativeAssets.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

#include "../../DeadIslandSecondScreen/src/Log.h"

namespace dis {
namespace {

constexpr int kFrameResource = 301;
constexpr int kMeterTrackResource = 302;
constexpr int kMeterFillResource = 303;
constexpr int kMarkerResource = 304;
constexpr int kArrowLeftResource = 305;
constexpr int kArrowRightResource = 306;
constexpr int kQuestMarkerResource = 307;
constexpr int kDiamondResource = 308;
constexpr int kWorldMapResource = 309;
constexpr int kPlayerSetMarkerResource = 310;
constexpr int kTerrainHeightResource = 311;
constexpr int kPageIconFirstResource = 312;
constexpr int kQuestDoorMarkerResource = 317;
constexpr int kMultipleQuestMarkerResource = 318;
constexpr int kLocationMarkerFirstResource = 400;
constexpr int kLocationMarkerUndiscoveredFirstResource = 500;
constexpr int kLocationMarkerCount = 59;
constexpr uint32_t kRgbaMagic = 0x41424752u;  // "RGBA" little endian.
constexpr int kMaxAssetSide = 2048;
constexpr size_t kMaxAssetBytes = 16u * 1024u * 1024u;

struct TerrainProjection {
  float aspect = 1.0f;
  float min_x = 0.0f;
  float max_x = 1.0f;
  float min_y = 0.0f;
  float max_y = 1.0f;
};

void ProjectTerrainRaw(float u, float v, float height,
                       float aspect, float* x, float* y, float* depth) {
  // This is an isolated map camera, not Skyrim's gameplay camera. The
  // Use a bird's-eye camera with only a small 20-degree relief tilt. The old
  // 56-degree oblique camera turned the southern mountains into a foreground
  // wall and made panning look like a perspective warp. This remains a real
  // height-field mesh: elevation affects projection, depth, lighting and
  // occlusion, but north/south coverage now matches Skyrim's map orientation.
  constexpr float kPitchCos = 0.939693f;  // cos(20 degrees).
  constexpr float kPitchSin = 0.342020f;  // sin(20 degrees).
  const float local_x = (u - 0.5f) * 2.0f * aspect;
  const float local_y = (v - 0.5f) * 2.0f;
  const float local_z = (height - 0.30f) * 0.58f;
  const float camera_depth = std::max(
      0.35f, 2.75f - local_y * kPitchSin - local_z * kPitchCos);
  if (x) *x = local_x / camera_depth;
  if (y) *y = (local_y * kPitchCos - local_z * kPitchSin) / camera_depth;
  if (depth) *depth = camera_depth;
}

TerrainProjection MakeTerrainProjection(int width, int height) {
  TerrainProjection projection;
  projection.aspect = static_cast<float>(std::max(1, width)) /
                      static_cast<float>(std::max(1, height));
  projection.min_x = projection.min_y =
      std::numeric_limits<float>::max();
  projection.max_x = projection.max_y =
      -std::numeric_limits<float>::max();
  // Bound the complete legal height field, not merely the current samples.
  // That makes marker placement and the cached raster use identical stable
  // coordinates even when the cropped view changes.
  for (float u : {0.0f, 1.0f}) {
    for (float v : {0.0f, 1.0f}) {
      for (float terrain_height : {0.0f, 1.0f}) {
        float x = 0.0f;
        float y = 0.0f;
        ProjectTerrainRaw(u, v, terrain_height, projection.aspect,
                          &x, &y, nullptr);
        projection.min_x = std::min(projection.min_x, x);
        projection.max_x = std::max(projection.max_x, x);
        projection.min_y = std::min(projection.min_y, y);
        projection.max_y = std::max(projection.max_y, y);
      }
    }
  }
  return projection;
}

void ProjectTerrainScreen(const TerrainProjection& projection,
                          float u, float v, float terrain_height,
                          int width, int height,
                          float* screen_x, float* screen_y, float* depth) {
  float raw_x = 0.0f;
  float raw_y = 0.0f;
  ProjectTerrainRaw(u, v, terrain_height, projection.aspect,
                    &raw_x, &raw_y, depth);
  constexpr float kMargin = 0.025f;
  const float normalized_x =
      (raw_x - projection.min_x) /
      std::max(0.0001f, projection.max_x - projection.min_x);
  const float normalized_y =
      (raw_y - projection.min_y) /
      std::max(0.0001f, projection.max_y - projection.min_y);
  if (screen_x)
    *screen_x = (kMargin + normalized_x * (1.0f - 2.0f * kMargin)) *
                std::max(1, width - 1);
  if (screen_y)
    *screen_y = (kMargin + normalized_y * (1.0f - 2.0f * kMargin)) *
                std::max(1, height - 1);
}

// Process-lifetime by design. The proxy owns worker threads until process exit;
// leaking this tiny cache avoids CRT teardown racing the panel thread.
NativeAssets& g_assets = *new NativeAssets();

uint32_t U32(const uint8_t* value) {
  uint32_t result = 0;
  std::memcpy(&result, value, sizeof(result));
  return result;
}

uint16_t U16(const uint8_t* value) {
  uint16_t result = 0;
  std::memcpy(&result, value, sizeof(result));
  return result;
}

float F32(const uint8_t* value) {
  float result = 0.0f;
  std::memcpy(&result, value, sizeof(result));
  return result;
}

bool ResourceBytes(int id, const uint8_t** bytes, size_t* size) {
  if (!bytes || !size) return false;
  *bytes = nullptr;
  *size = 0;
  HMODULE module = nullptr;
  if (!GetModuleHandleExW(
          GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
          reinterpret_cast<LPCWSTR>(&ResourceBytes), &module) || !module) {
    return false;
  }
  HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(id),
                                 MAKEINTRESOURCEW(10));  // RT_RCDATA
  if (!resource) return false;
  HGLOBAL loaded = LoadResource(module, resource);
  if (!loaded) return false;
  const DWORD resource_size = SizeofResource(module, resource);
  const void* resource_bytes = LockResource(loaded);
  if (!resource_bytes || resource_size == 0) return false;
  *bytes = static_cast<const uint8_t*>(resource_bytes);
  *size = resource_size;
  return true;
}

bool LoadRgbaResource(int id, NativeAssets::Image* image) {
  if (!image) return false;
  const uint8_t* bytes = nullptr;
  size_t size = 0;
  if (!ResourceBytes(id, &bytes, &size) || size < 12 ||
      U32(bytes) != kRgbaMagic) {
    return false;
  }
  const uint32_t width = U32(bytes + 4);
  const uint32_t height = U32(bytes + 8);
  const uint64_t pixels = static_cast<uint64_t>(width) * height;
  const uint64_t byte_count = pixels * 4u;
  if (!width || !height || width > kMaxAssetSide || height > kMaxAssetSide ||
      byte_count > kMaxAssetBytes || size != 12u + byte_count) {
    return false;
  }
  image->width = static_cast<int>(width);
  image->height = static_cast<int>(height);
  image->rgba.assign(bytes + 12, bytes + size);
  return image->valid();
}

void RemoveOpaqueFrameBackground(NativeAssets::Image* image) {
  if (!image || !image->valid()) return;

  // MessageBackground is exported by the SWF rasterizer on an opaque black
  // canvas.  Drawing its outer slices verbatim therefore paints thick black
  // bands over DrawPanel's gradient and makes the usable inner panel appear
  // smaller than its contents.  Keep only the authored silver ornament and
  // antialiasing; the complete panel rectangle remains filled by DrawPanel.
  for (size_t pixel = 0; pixel + 3 < image->rgba.size(); pixel += 4) {
    const uint8_t red = image->rgba[pixel + 0];
    const uint8_t green = image->rgba[pixel + 1];
    const uint8_t blue = image->rgba[pixel + 2];
    const uint8_t luminance = std::max({red, green, blue});
    if (luminance <= 12) {
      image->rgba[pixel + 3] = 0;
    } else if (luminance < 40) {
      const unsigned scaled = static_cast<unsigned>(image->rgba[pixel + 3]) *
                              static_cast<unsigned>(luminance - 12) / 28u;
      image->rgba[pixel + 3] = static_cast<uint8_t>(scaled);
    }
  }
}

}  // namespace

NativeAssets& TheNativeAssets() { return g_assets; }

bool NativeAssets::ready() { return attempted_ ? frame_.valid() : Load(); }

bool NativeAssets::Load() {
  if (attempted_) return frame_.valid();
  attempted_ = true;
  const bool frame = LoadRgbaResource(kFrameResource, &frame_);
  if (frame) RemoveOpaqueFrameBackground(&frame_);
  const bool meter_track = LoadRgbaResource(kMeterTrackResource, &meter_track_);
  const bool meter_fill = LoadRgbaResource(kMeterFillResource, &meter_fill_);
  const bool marker = LoadRgbaResource(kMarkerResource, &marker_);
  bool locations = true;
  for (int index = 0; index < kLocationMarkerCount; ++index) {
    locations = LoadRgbaResource(kLocationMarkerFirstResource + index,
                                 &location_markers_[index]) && locations;
    locations = LoadRgbaResource(
                    kLocationMarkerUndiscoveredFirstResource + index,
                    &location_markers_undiscovered_[index]) && locations;
  }
  const bool left = LoadRgbaResource(kArrowLeftResource, &arrow_left_);
  const bool right = LoadRgbaResource(kArrowRightResource, &arrow_right_);
  const bool quest = LoadRgbaResource(kQuestMarkerResource, &quest_marker_);
  const bool quest_door = LoadRgbaResource(kQuestDoorMarkerResource,
                                           &quest_door_marker_);
  const bool multiple_quest = LoadRgbaResource(kMultipleQuestMarkerResource,
                                               &multiple_quest_marker_);
  const bool player_set =
      LoadRgbaResource(kPlayerSetMarkerResource, &player_set_marker_);
  const bool diamond = LoadRgbaResource(kDiamondResource, &diamond_);
  bool page_icons = true;
  for (int page = 0; page < 5; ++page)
    page_icons = LoadRgbaResource(kPageIconFirstResource + page,
                                  &page_icons_[page]) && page_icons;
  if (!frame || !meter_track || !meter_fill || !marker || !locations ||
      !left || !right || !quest || !quest_door || !multiple_quest ||
      !player_set || !diamond || !page_icons) {
    DIS_WARN("[assets] one or more embedded Skyrim interface assets failed validation");
  } else {
    DIS_INFO("[assets] loaded native Skyrim meters, markers, and interface icons");
  }
  return frame;
}

bool NativeAssets::LoadWorldMap() {
  if (world_map_attempted_) return world_map_.valid();
  world_map_attempted_ = true;
  if (!LoadRgbaResource(kWorldMapResource, &world_map_)) {
    DIS_WARN("[assets] embedded Tamriel terrain failed validation");
    return false;
  }
  DIS_INFO("[assets] loaded Tamriel terrain on first map-page use");
  return true;
}

bool NativeAssets::LoadTerrainHeight() {
  if (terrain_height_attempted_) return !terrain_height_.empty();
  terrain_height_attempted_ = true;
  const uint8_t* bytes = nullptr;
  size_t size = 0;
  if (!ResourceBytes(kTerrainHeightResource, &bytes, &size) || size < 32 ||
      std::memcmp(bytes, "SDSH", 4) != 0 || U32(bytes + 4) != 1) {
    DIS_WARN("[assets] embedded Tamriel 3D height resource failed validation");
    return false;
  }
  const uint32_t width = U32(bytes + 8);
  const uint32_t height = U32(bytes + 12);
  const float minimum = F32(bytes + 24);
  const float maximum = F32(bytes + 28);
  const uint64_t samples = static_cast<uint64_t>(width) * height;
  if (width < 2 || height < 2 || width > 2048 || height > 2048 ||
      samples > 4u * 1024u * 1024u || size != 32u + samples * 2u ||
      !std::isfinite(minimum) || !std::isfinite(maximum) ||
      maximum - minimum < 1.0f) {
    DIS_WARN("[assets] rejected malformed Tamriel height grid %ux%u bytes=%llu",
             width, height, static_cast<unsigned long long>(size));
    return false;
  }
  terrain_width_ = static_cast<int>(width);
  terrain_height_rows_ = static_cast<int>(height);
  terrain_minimum_ = minimum;
  terrain_maximum_ = maximum;
  terrain_height_.resize(static_cast<size_t>(samples));
  for (size_t i = 0; i < terrain_height_.size(); ++i)
    terrain_height_[i] = U16(bytes + 32 + i * 2);
  DIS_INFO("[assets] loaded Tamriel LOD16 software-fallback grid %dx%d height=%.0f..%.0f",
           terrain_width_, terrain_height_rows_, terrain_minimum_,
           terrain_maximum_);
  return true;
}

bool NativeAssets::Draw(Canvas& canvas, const Box& destination,
                        const Image& image, const Color& tint) {
  if (!image.valid() || destination.w <= 0 || destination.h <= 0) return false;
  canvas.BlitRgba(destination, image.rgba.data(), image.width, image.height,
                  Box(0, 0, image.width, image.height), tint);
  return true;
}

bool NativeAssets::DrawFrame(Canvas& canvas, const Box& destination,
                             const Color& tint) {
  if (!ready() || !frame_.valid() || destination.w < 18 || destination.h < 18)
    return false;

  // The game's MessageBackground is a vector-rendered ornamental frame. Draw
  // only its border as a nine-slice so wide panels do not stretch the knots or
  // overwrite the custom translucent panel fill.
  const int source_x = std::min(36, frame_.width / 3);
  const int source_y = std::min(36, frame_.height / 3);
  const int dest_x = std::min(source_x, destination.w / 3);
  const int dest_y = std::min(source_y, destination.h / 3);
  auto blit = [&](const Box& dst, const Box& src) {
    if (dst.w > 0 && dst.h > 0 && src.w > 0 && src.h > 0)
      canvas.BlitRgba(dst, frame_.rgba.data(), frame_.width, frame_.height, src,
                      tint);
  };

  blit(Box(destination.x, destination.y, dest_x, dest_y),
       Box(0, 0, source_x, source_y));
  blit(Box(destination.right() - dest_x, destination.y, dest_x, dest_y),
       Box(frame_.width - source_x, 0, source_x, source_y));
  blit(Box(destination.x, destination.bottom() - dest_y, dest_x, dest_y),
       Box(0, frame_.height - source_y, source_x, source_y));
  blit(Box(destination.right() - dest_x, destination.bottom() - dest_y,
           dest_x, dest_y),
       Box(frame_.width - source_x, frame_.height - source_y, source_x,
           source_y));
  blit(Box(destination.x + dest_x, destination.y,
           destination.w - 2 * dest_x, dest_y),
       Box(source_x, 0, frame_.width - 2 * source_x, source_y));
  blit(Box(destination.x + dest_x, destination.bottom() - dest_y,
           destination.w - 2 * dest_x, dest_y),
       Box(source_x, frame_.height - source_y,
           frame_.width - 2 * source_x, source_y));
  blit(Box(destination.x, destination.y + dest_y, dest_x,
           destination.h - 2 * dest_y),
       Box(0, source_y, source_x, frame_.height - 2 * source_y));
  blit(Box(destination.right() - dest_x, destination.y + dest_y, dest_x,
           destination.h - 2 * dest_y),
       Box(frame_.width - source_x, source_y, source_x,
           frame_.height - 2 * source_y));
  return true;
}

bool NativeAssets::DrawMeter(Canvas& canvas, const Box& destination, float ratio,
                             const Color& tint) {
  if (!ready() || !meter_track_.valid() || !meter_fill_.valid() ||
      destination.w <= 0 || destination.h <= 0) return false;
  ratio = std::clamp(ratio, 0.0f, 1.0f);
  Draw(canvas, destination, meter_track_, Color(255, 255, 255, 235));
  const int dest_width = static_cast<int>(destination.w * ratio + 0.5f);
  // The native fill sprite has ten transparent rows above its authored meter.
  // Crop those rows and align the remaining 23px strip to the native track.
  const int fill_y = meter_fill_.height >= 33 ? 10 : 0;
  const int fill_height = meter_fill_.height - fill_y;
  const int source_width = static_cast<int>(meter_fill_.width * ratio + 0.5f);
  if (dest_width > 0 && source_width > 0) {
    canvas.BlitRgba(Box(destination.x, destination.y, dest_width, destination.h),
                    meter_fill_.rgba.data(), meter_fill_.width,
                    meter_fill_.height,
                    Box(0, fill_y, source_width, fill_height), tint);
  }
  return true;
}

bool NativeAssets::DrawMarker(Canvas& canvas, const Box& destination,
                              const Color& tint, float heading_radians) {
  if (!ready() || !marker_.valid() || destination.w <= 0 ||
      destination.h <= 0 || !std::isfinite(heading_radians)) {
    return false;
  }
  // YouAreHereMarker frame 15 includes transparent animation padding around
  // the authored 16x38 player arrow. Sample only that authored area, preserve
  // its aspect ratio, and rotate the pixels on the CPU. The game's map arrow
  // points north in the source frame; Skyrim's heading is clockwise from
  // north, matching the screen-space rotation below.
  const Box source = marker_.width >= 30 && marker_.height >= 43
                         ? Box(14, 5, 16, 38)
                         : Box(0, 0, marker_.width, marker_.height);

  marker_scratch_.width = destination.w;
  marker_scratch_.height = destination.h;
  marker_scratch_.rgba.assign(
      static_cast<size_t>(destination.w) * destination.h * 4u, 0);

  const int glyph_height =
      std::max(1, std::min(destination.w, destination.h) - 4);
  const int glyph_width = std::max(
      1, static_cast<int>(std::lround(
             static_cast<double>(glyph_height) * source.w / source.h)));
  const float center_x = (destination.w - 1) * 0.5f;
  const float center_y = (destination.h - 1) * 0.5f;
  const float cosine = std::cos(heading_radians);
  const float sine = std::sin(heading_radians);

  for (int y = 0; y < destination.h; ++y) {
    for (int x = 0; x < destination.w; ++x) {
      const float dx = x - center_x;
      const float dy = y - center_y;
      // Inverse of a clockwise screen-space rotation.
      const float unrotated_x = cosine * dx + sine * dy;
      const float unrotated_y = -sine * dx + cosine * dy;
      const float glyph_x = unrotated_x + glyph_width * 0.5f;
      const float glyph_y = unrotated_y + glyph_height * 0.5f;
      if (glyph_x < 0.0f || glyph_y < 0.0f || glyph_x >= glyph_width ||
          glyph_y >= glyph_height) {
        continue;
      }
      const int source_x = source.x + std::min(
          source.w - 1, static_cast<int>(glyph_x * source.w / glyph_width));
      const int source_y = source.y + std::min(
          source.h - 1, static_cast<int>(glyph_y * source.h / glyph_height));
      const size_t from =
          (static_cast<size_t>(source_y) * marker_.width + source_x) * 4u;
      const size_t to =
          (static_cast<size_t>(y) * destination.w + x) * 4u;
      std::memcpy(marker_scratch_.rgba.data() + to,
                  marker_.rgba.data() + from, 4u);
    }
  }
  return Draw(canvas, destination, marker_scratch_, tint);
}

bool NativeAssets::DrawLocationMarker(Canvas& canvas,
                                      const Box& destination,
                                      uint16_t marker_type, bool discovered,
                                      const Color& tint) {
  if (!ready() || marker_type < 1 || marker_type > kLocationMarkerCount ||
      destination.w <= 0 || destination.h <= 0)
    return false;
  const size_t index = static_cast<size_t>(marker_type - 1);
  if (!location_markers_[index].valid()) return false;
  const bool drawn = Draw(canvas, destination, location_markers_[index], tint);
  // Vanilla attaches the undiscovered symbol on top of the same IconClip;
  // retain that exact composition instead of substituting a generic marker.
  if (!discovered && location_markers_undiscovered_[index].valid()) {
    Draw(canvas, destination, location_markers_undiscovered_[index], tint);
  }
  return drawn;
}

bool NativeAssets::LocationMarkerDimensions(uint16_t marker_type, int* width,
                                            int* height) const {
  if (!width || !height || marker_type < 1 ||
      marker_type > kLocationMarkerCount)
    return false;
  const Image& marker = location_markers_[marker_type - 1];
  if (!marker.valid()) return false;
  *width = marker.width;
  *height = marker.height;
  return true;
}

bool NativeAssets::DrawQuestMarker(Canvas& canvas, const Box& destination,
                                   uint16_t marker_type,
                                   const Color& tint) {
  const Image* image = marker_type == 63
      ? &quest_door_marker_
      : marker_type == 64 ? &multiple_quest_marker_ : &quest_marker_;
  if (!ready() || !image->valid() || destination.w <= 0 ||
      destination.h <= 0) return false;
  Box source(0, 0, image->width, image->height);
  if (marker_type == 62 && image->width >= 33 && image->height >= 46) {
    source = Box(10, 4, 23, 42);
  } else if (marker_type == 63 && image->width >= 33 &&
             image->height >= 55) {
    source = Box(10, 4, 23, 51);
  } else if (marker_type == 64 && image->width >= 33 &&
             image->height >= 42) {
    source = Box(2, 0, 31, 42);
  }
  canvas.BlitRgba(destination, image->rgba.data(), image->width,
                  image->height, source, tint);
  return true;
}

bool NativeAssets::DrawPlayerSetMarker(Canvas& canvas,
                                       const Box& destination,
                                       const Color& tint) {
  if (!ready() || !player_set_marker_.valid() || destination.w <= 0 ||
      destination.h <= 0) return false;
  // The selected animation frame is a 243px-wide timeline strip with the
  // actual cyan pin authored at its left edge.  Crop the transparent timeline
  // padding so the marker keeps the same proportions as Skyrim's map menu.
  const Box source = player_set_marker_.width >= 33 &&
                             player_set_marker_.height >= 47
                         ? Box(10, 4, 23, 43)
                         : Box(0, 0, player_set_marker_.width,
                               player_set_marker_.height);
  canvas.BlitRgba(destination, player_set_marker_.rgba.data(),
                  player_set_marker_.width, player_set_marker_.height,
                  source, tint);
  return true;
}

bool NativeAssets::DrawDiamond(Canvas& canvas, const Box& destination,
                               const Color& tint) {
  if (!ready() || !diamond_.valid()) return false;
  return Draw(canvas, destination, diamond_, tint);
}

bool NativeAssets::DrawArrow(Canvas& canvas, const Box& destination, bool right,
                             const Color& tint) {
  if (!ready()) return false;
  return Draw(canvas, destination, right ? arrow_right_ : arrow_left_, tint);
}

bool NativeAssets::DrawPageIcon(Canvas& canvas, const Box& destination,
                                int page_index, const Color& tint) {
  if (!ready() || page_index < 0 || page_index >= 5) return false;
  return Draw(canvas, destination, page_icons_[page_index], tint);
}

bool NativeAssets::DrawWorldMap(Canvas& canvas, const Box& destination,
                                const Box& source, const Color& tint) {
  if (!ready() || !LoadWorldMap() || source.w <= 0 || source.h <= 0 ||
      source.x < 0 || source.y < 0 || source.right() > world_map_.width ||
      source.bottom() > world_map_.height) {
    return false;
  }
  canvas.BlitRgba(destination, world_map_.rgba.data(), world_map_.width,
                  world_map_.height, source, tint);
  return true;
}

bool NativeAssets::DrawWorldMap3D(Canvas& canvas, const Box& destination,
                                  const Box& source, const Color& tint) {
  if (!ready() || !LoadWorldMap() || !LoadTerrainHeight() ||
      destination.w <= 0 || destination.h <= 0 || source.w <= 0 ||
      source.h <= 0 || source.x < 0 || source.y < 0 ||
      source.right() > world_map_.width ||
      source.bottom() > world_map_.height) {
    return false;
  }
  const bool cache_matches = projected_world_map_.valid() &&
      projected_width_ == destination.w && projected_height_ == destination.h &&
      projected_source_.x == source.x && projected_source_.y == source.y &&
      projected_source_.w == source.w && projected_source_.h == source.h;
  if (!cache_matches) {
    const uint64_t started = GetTickCount64();
    // Rasterize Skyrim's real Tamriel LOD height field as a perspective mesh.
    // The former implementation only multiplied the flat diffuse bitmap by a
    // normal-derived shade, so every mountain remained geometrically flat.
    // This retained buffer is rebuilt only when the crop or viewport changes.
    int render_width = std::min(destination.w, 960);
    int render_height = std::max(1, static_cast<int>(std::lround(
        static_cast<double>(render_width) * destination.h /
        std::max(1, destination.w))));
    if (render_height > 720) {
      render_height = 720;
      render_width = std::max(1, static_cast<int>(std::lround(
          static_cast<double>(render_height) * destination.w /
          std::max(1, destination.h))));
    }
    projected_width_ = destination.w;
    projected_height_ = destination.h;
    projected_source_ = source;
    projected_world_map_.width = render_width;
    projected_world_map_.height = render_height;
    projected_world_map_.rgba.resize(
        static_cast<size_t>(render_width) * render_height * 4u);
    for (size_t pixel = 0;
         pixel < static_cast<size_t>(render_width) * render_height;
         ++pixel) {
      projected_world_map_.rgba[pixel * 4] = 7;
      projected_world_map_.rgba[pixel * 4 + 1] = 10;
      projected_world_map_.rgba[pixel * 4 + 2] = 12;
      projected_world_map_.rgba[pixel * 4 + 3] = 255;
    }
    std::vector<float> depth_buffer(
        static_cast<size_t>(render_width) * render_height,
        std::numeric_limits<float>::max());

    struct Vertex {
      float x = 0.0f;
      float y = 0.0f;
      float depth = 1.0f;
      float atlas_x = 0.0f;
      float atlas_y = 0.0f;
      float shade = 1.0f;
    };

    const int columns = std::clamp(
        std::min(source.w, std::max(32, render_width / 5)), 32, 192);
    const int rows = std::clamp(
        std::min(source.h, std::max(24, render_height / 5)), 24, 144);
    const TerrainProjection projection =
        MakeTerrainProjection(render_width, render_height);
    std::vector<Vertex> vertices(
        static_cast<size_t>(columns + 1) * (rows + 1));
    auto vertex_at = [&](int x, int y) -> Vertex& {
      return vertices[static_cast<size_t>(y) * (columns + 1) + x];
    };
    const float height_span =
        std::max(1.0f, terrain_maximum_ - terrain_minimum_);
    for (int row = 0; row <= rows; ++row) {
      const float v = static_cast<float>(row) / rows;
      const float atlas_y = source.y + v * source.h;
      for (int column = 0; column <= columns; ++column) {
        const float u = static_cast<float>(column) / columns;
        const float atlas_x = source.x + u * source.w;
        const float height = SampleTerrainHeight(atlas_x, atlas_y);
        const float normalized_height = std::clamp(
            (height - terrain_minimum_) / height_span, 0.0f, 1.0f);
        Vertex& vertex = vertex_at(column, row);
        ProjectTerrainScreen(projection, u, v, normalized_height,
                             render_width, render_height,
                             &vertex.x, &vertex.y, &vertex.depth);
        vertex.atlas_x = atlas_x;
        vertex.atlas_y = atlas_y;
        const float step_x = std::max(
            1.0f, static_cast<float>(source.w) / columns);
        const float step_y = std::max(
            1.0f, static_cast<float>(source.h) / rows);
        const float dx = (SampleTerrainHeight(atlas_x - step_x, atlas_y) -
                          SampleTerrainHeight(atlas_x + step_x, atlas_y)) /
                         std::max(1.0f, step_x * 6800.0f);
        const float dy = (SampleTerrainHeight(atlas_x, atlas_y + step_y) -
                          SampleTerrainHeight(atlas_x, atlas_y - step_y)) /
                         std::max(1.0f, step_y * 6800.0f);
        const float inverse = 1.0f / std::sqrt(dx * dx + dy * dy + 1.0f);
        const float light = std::clamp(
            (dx * -0.46f + dy * -0.31f + 0.83f) * inverse,
            -0.25f, 1.0f);
        vertex.shade = std::clamp(0.66f + light * 0.39f,
                                  0.54f, 1.10f);
      }
    }

    auto edge = [](const Vertex& a, const Vertex& b, float x, float y) {
      return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
    };
    auto rasterize = [&](const Vertex& a, const Vertex& b,
                         const Vertex& c) {
      const float area = edge(a, b, c.x, c.y);
      if (!std::isfinite(area) || std::fabs(area) < 0.0001f) return;
      const int min_x = std::clamp(
          static_cast<int>(std::floor(std::min({a.x, b.x, c.x}))),
          0, render_width - 1);
      const int max_x = std::clamp(
          static_cast<int>(std::ceil(std::max({a.x, b.x, c.x}))),
          0, render_width - 1);
      const int min_y = std::clamp(
          static_cast<int>(std::floor(std::min({a.y, b.y, c.y}))),
          0, render_height - 1);
      const int max_y = std::clamp(
          static_cast<int>(std::ceil(std::max({a.y, b.y, c.y}))),
          0, render_height - 1);
      const float sign = area < 0.0f ? -1.0f : 1.0f;
      const float inverse_area = sign / std::fabs(area);
      const float aq = 1.0f / std::max(0.001f, a.depth);
      const float bq = 1.0f / std::max(0.001f, b.depth);
      const float cq = 1.0f / std::max(0.001f, c.depth);
      for (int y = min_y; y <= max_y; ++y) {
        for (int x = min_x; x <= max_x; ++x) {
          const float sample_x = x + 0.5f;
          const float sample_y = y + 0.5f;
          const float wa = edge(b, c, sample_x, sample_y) * inverse_area;
          const float wb = edge(c, a, sample_x, sample_y) * inverse_area;
          const float wc = edge(a, b, sample_x, sample_y) * inverse_area;
          if (wa < -0.0001f || wb < -0.0001f || wc < -0.0001f) continue;
          const float q = wa * aq + wb * bq + wc * cq;
          if (q <= 0.0f || !std::isfinite(q)) continue;
          const float depth = 1.0f / q;
          const size_t pixel = static_cast<size_t>(y) * render_width + x;
          if (depth >= depth_buffer[pixel]) continue;
          const float atlas_x =
              (wa * a.atlas_x * aq + wb * b.atlas_x * bq +
               wc * c.atlas_x * cq) / q;
          const float atlas_y =
              (wa * a.atlas_y * aq + wb * b.atlas_y * bq +
               wc * c.atlas_y * cq) / q;
          const float shade =
              (wa * a.shade * aq + wb * b.shade * bq +
               wc * c.shade * cq) / q;
          const int texture_x = std::clamp(
              static_cast<int>(std::lround(atlas_x)),
              0, world_map_.width - 1);
          const int texture_y = std::clamp(
              static_cast<int>(std::lround(atlas_y)),
              0, world_map_.height - 1);
          const size_t from =
              (static_cast<size_t>(texture_y) * world_map_.width +
               texture_x) * 4u;
          const size_t to = pixel * 4u;
          const uint8_t tint_channels[3] = {tint.r, tint.g, tint.b};
          for (int channel = 0; channel < 3; ++channel) {
            projected_world_map_.rgba[to + channel] = static_cast<uint8_t>(
                std::clamp(world_map_.rgba[from + channel] *
                               (tint_channels[channel] / 255.0f) * shade,
                           0.0f, 255.0f));
          }
          projected_world_map_.rgba[to + 3] = 255;
          depth_buffer[pixel] = depth;
        }
      }
    };
    for (int row = 0; row < rows; ++row) {
      for (int column = 0; column < columns; ++column) {
        const Vertex& top_left = vertex_at(column, row);
        const Vertex& top_right = vertex_at(column + 1, row);
        const Vertex& bottom_left = vertex_at(column, row + 1);
        const Vertex& bottom_right = vertex_at(column + 1, row + 1);
        rasterize(top_left, bottom_left, top_right);
        rasterize(top_right, bottom_left, bottom_right);
      }
    }
    const uint64_t finished = GetTickCount64();
    ++map_renders_since_log_;
    if (last_map_render_log_ == 0 || finished - last_map_render_log_ >= 5000) {
      DIS_INFO("[map] rendered isolated Skyrim LOD16 terrain mesh "
               "source=%d,%d %dx%d target=%dx%d mesh=%dx%d in %llums "
               "views_since_log=%u",
               source.x, source.y, source.w, source.h, destination.w,
               destination.h, columns, rows,
               static_cast<unsigned long long>(finished - started),
               map_renders_since_log_);
      last_map_render_log_ = finished;
      map_renders_since_log_ = 0;
    }
  }
  return Draw(canvas, destination, projected_world_map_,
              Color(255, 255, 255, 255));
}

float NativeAssets::SampleTerrainHeight(float atlas_x, float atlas_y) const {
  if (terrain_height_.empty() || terrain_width_ < 2 ||
      terrain_height_rows_ < 2 || world_map_.width < 2 ||
      world_map_.height < 2)
    return terrain_minimum_;
  const float terrain_x = std::clamp(
      atlas_x * (terrain_width_ - 1) / (world_map_.width - 1),
      0.0f, static_cast<float>(terrain_width_ - 1));
  // The diffuse atlas grows north-to-south; the BTR height field is stored in
  // the opposite Y direction.
  const float terrain_y = std::clamp(
      (world_map_.height - 1.0f - atlas_y) *
          (terrain_height_rows_ - 1) / (world_map_.height - 1),
      0.0f, static_cast<float>(terrain_height_rows_ - 1));
  const int x0 = static_cast<int>(std::floor(terrain_x));
  const int y0 = static_cast<int>(std::floor(terrain_y));
  const int x1 = std::min(x0 + 1, terrain_width_ - 1);
  const int y1 = std::min(y0 + 1, terrain_height_rows_ - 1);
  const float fx = terrain_x - x0;
  const float fy = terrain_y - y0;
  auto unpack = [&](int x, int y) {
    const uint16_t packed = terrain_height_[
        static_cast<size_t>(y) * terrain_width_ + x];
    return terrain_minimum_ +
           (terrain_maximum_ - terrain_minimum_) * packed / 65535.0f;
  };
  const float top = unpack(x0, y0) + (unpack(x1, y0) - unpack(x0, y0)) * fx;
  const float bottom =
      unpack(x0, y1) + (unpack(x1, y1) - unpack(x0, y1)) * fx;
  return top + (bottom - top) * fy;
}

bool NativeAssets::ProjectWorldMapPoint(const Box& destination,
                                        const Box& source,
                                        float atlas_x, float atlas_y,
                                        int* screen_x, int* screen_y) const {
  if (!screen_x || !screen_y || terrain_height_.empty() ||
      destination.w <= 0 || destination.h <= 0 || source.w <= 0 ||
      source.h <= 0 || atlas_x < source.x || atlas_x > source.right() ||
      atlas_y < source.y || atlas_y > source.bottom())
    return false;
  const float u = (atlas_x - source.x) / source.w;
  const float v = (atlas_y - source.y) / source.h;
  const float height_span =
      std::max(1.0f, terrain_maximum_ - terrain_minimum_);
  const float normalized_height = std::clamp(
      (SampleTerrainHeight(atlas_x, atlas_y) - terrain_minimum_) /
          height_span,
      0.0f, 1.0f);
  const TerrainProjection projection =
      MakeTerrainProjection(destination.w, destination.h);
  float x = 0.0f;
  float y = 0.0f;
  ProjectTerrainScreen(projection, u, v, normalized_height,
                       destination.w, destination.h, &x, &y, nullptr);
  *screen_x = destination.x + static_cast<int>(std::lround(x));
  *screen_y = destination.y + static_cast<int>(std::lround(y));
  return *screen_x >= destination.x && *screen_x < destination.right() &&
         *screen_y >= destination.y && *screen_y < destination.bottom();
}

bool NativeAssets::WorldMapSize(int* width, int* height) {
  if (!width || !height || !ready() || !LoadWorldMap()) return false;
  *width = world_map_.width;
  *height = world_map_.height;
  return true;
}

}  // namespace dis
