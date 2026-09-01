#include "NativeMap.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <utility>

#include "Mem.h"

namespace dis {
namespace {

NativeMap g_native_map;

constexpr float kMinimumTerrainSpan = 0.001f;
constexpr float kMinimumDirectionLengthSquared = 0.000001f;
constexpr float kRadiansToDegrees = 57.29577951308232f;

// MenuMap::OnShow/initialization at gamedll+0x490B60 first reads these four
// floats from the active world record and treats them as minX, minZ, maxX,
// maxZ. If that authored box is absent it falls back to GetTerrainSize with a
// zero origin. Keep the private read behind both the shipped PE identity and
// the exact function prologue that proves this layout.
constexpr uint32_t kGameTimestamp = 0x5745B64C;
constexpr uint32_t kGameImageSize = 0x014C8000;
constexpr uintptr_t kMenuMapBoundsInitRva = 0x00490B60;
constexpr uintptr_t kWorldRootRva = 0x0127C2A8;
constexpr size_t kActiveWorldOffset = 0x04A0;
constexpr size_t kMapBoundsOffset = 0x07A8;
constexpr uint8_t kMenuMapBoundsInitSignature[] = {
    0x40, 0x55, 0x53, 0x41, 0x57, 0x48, 0x8D, 0x6C,
    0x24, 0xB9, 0x48, 0x81, 0xEC, 0xB0, 0x00, 0x00, 0x00,
};

bool Finite(float value) {
  return std::isfinite(value);
}

bool Finite(const Vec3& value) {
  return Finite(value.x) && Finite(value.y) && Finite(value.z);
}

bool UsableTerrainSize(const Vec3& size) {
  return Finite(size) && size.x > kMinimumTerrainSpan &&
         size.z > kMinimumTerrainSpan;
}

bool UsableMapExtents(const NativeMapExtents& extents) {
  const float width = extents.max_x - extents.min_x;
  const float height = extents.max_z - extents.min_z;
  return Finite(extents.min_x) && Finite(extents.min_z) &&
         Finite(extents.max_x) && Finite(extents.max_z) &&
         width > kMinimumTerrainSpan && height > kMinimumTerrainSpan &&
         width < 10000000.0f && height < 10000000.0f;
}

uint8_t* VerifiedMenuMapImage() {
  // Module identity and executable code cannot change during a normal game
  // process. NativeMap samples at 10 Hz, so reparsing the PE headers and
  // rereading the 17-byte signature on every position update was needless
  // work on the render thread. Do not cache "module absent", because gamedll
  // can map after the DXGI proxy; once it is present, cache the immutable
  // supported/unsupported result.
  static bool checked = false;
  static uint8_t* verified = nullptr;
  if (checked) return verified;

  HMODULE module = GetModuleHandleW(L"gamedll_x64_rwdi.dll");
  if (!module) return nullptr;
  auto* image = reinterpret_cast<uint8_t*>(module);
  IMAGE_DOS_HEADER dos{};
  IMAGE_NT_HEADERS64 nt{};
  uint8_t signature[sizeof(kMenuMapBoundsInitSignature)]{};
  if (!Read(image, &dos) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
      dos.e_lfanew <= 0 || !Read(image + dos.e_lfanew, &nt) ||
      nt.Signature != IMAGE_NT_SIGNATURE ||
      nt.FileHeader.TimeDateStamp != kGameTimestamp ||
      nt.OptionalHeader.SizeOfImage != kGameImageSize ||
      !Read(image + kMenuMapBoundsInitRva, &signature) ||
      std::memcmp(signature, kMenuMapBoundsInitSignature,
                  sizeof(signature)) != 0) {
    checked = true;
    return nullptr;
  }
  verified = image;
  checked = true;
  return verified;
}

bool NativeMenuMapExtents(NativeMapExtents* out) {
  if (!out) return false;
  uint8_t* image = VerifiedMenuMapImage();
  if (!image) return false;

  void* world_root = nullptr;
  void* active_world = nullptr;
  NativeMapExtents candidate{};
  if (!Read(image + kWorldRootRva, &world_root) || !world_root ||
      !Read(static_cast<uint8_t*>(world_root) + kActiveWorldOffset,
            &active_world) ||
      !active_world ||
      !Read(static_cast<uint8_t*>(active_world) + kMapBoundsOffset,
            &candidate.min_x) ||
      !Read(static_cast<uint8_t*>(active_world) + kMapBoundsOffset + 4,
            &candidate.min_z) ||
      !Read(static_cast<uint8_t*>(active_world) + kMapBoundsOffset + 8,
            &candidate.max_x) ||
      !Read(static_cast<uint8_t*>(active_world) + kMapBoundsOffset + 12,
            &candidate.max_z) ||
      !UsableMapExtents(candidate)) {
    return false;
  }
  *out = candidate;
  return true;
}

float NormalizedHeading(const Vec3& forward) {
  float heading = std::atan2(forward.x, forward.z) * kRadiansToDegrees;
  if (heading < 0.0f) heading += 360.0f;
  if (heading >= 360.0f) heading = std::fmod(heading, 360.0f);
  return heading;
}

int64_t Quantize(float value, float scale) {
  return Finite(value) ? static_cast<int64_t>(std::llround(value * scale)) : 0;
}

bool SameVisibleSnapshot(const NativeMapSnapshot& a,
                         const NativeMapSnapshot& b) {
  if (a.availability != b.availability || a.space != b.space ||
      a.level_name != b.level_name || a.has_position != b.has_position ||
      a.has_heading != b.has_heading || a.has_terrain != b.has_terrain ||
      a.position_inside_schematic != b.position_inside_schematic) {
    return false;
  }
  // Match what the page can actually display: coordinates are printed to one
  // decimal place and heading to a whole degree. This suppresses stationary
  // transform jitter without making visible movement or rotation lag.
  if (a.has_position &&
      (Quantize(a.world_position.x, 10.0f) !=
           Quantize(b.world_position.x, 10.0f) ||
       Quantize(a.world_position.y, 10.0f) !=
           Quantize(b.world_position.y, 10.0f) ||
       Quantize(a.world_position.z, 10.0f) !=
           Quantize(b.world_position.z, 10.0f))) {
    return false;
  }
  if (a.has_heading &&
      Quantize(a.heading_degrees, 1.0f) !=
          Quantize(b.heading_degrees, 1.0f)) {
    return false;
  }
  if (a.has_terrain &&
      (Quantize(a.terrain_size.x, 10.0f) !=
           Quantize(b.terrain_size.x, 10.0f) ||
       Quantize(a.terrain_size.z, 10.0f) !=
           Quantize(b.terrain_size.z, 10.0f))) {
    return false;
  }
  if (a.has_terrain &&
      (Quantize(a.extents.min_x, 10.0f) !=
           Quantize(b.extents.min_x, 10.0f) ||
       Quantize(a.extents.min_z, 10.0f) !=
           Quantize(b.extents.min_z, 10.0f) ||
       Quantize(a.extents.max_x, 10.0f) !=
           Quantize(b.extents.max_x, 10.0f) ||
       Quantize(a.extents.max_z, 10.0f) !=
           Quantize(b.extents.max_z, 10.0f))) {
    return false;
  }
  return true;
}

}  // namespace

NativeMap& TheNativeMap() { return g_native_map; }

const char* NativeMapAvailabilityText(
    NativeMapAvailability availability) {
  switch (availability) {
    case NativeMapAvailability::kNoLevel:
      return "Waiting for a level";
    case NativeMapAvailability::kNoPlayer:
      return "Locating the player";
    case NativeMapAvailability::kPositionUnavailable:
      return "Player position unavailable";
    case NativeMapAvailability::kTerrainUnavailable:
      return "Map unavailable for this area";
    case NativeMapAvailability::kReady:
      return "Ready";
  }
  return "Map unavailable";
}

void NativeMap::Update(ILevel* level, const IControlObject* player_control,
                       const Engine& engine) {
  NativeMapSnapshot next;

  if (!level) {
    next.availability = NativeMapAvailability::kNoLevel;
    Publish(std::move(next));
    return;
  }

  // LevelName and TerrainSize are sampled even while player discovery is in
  // progress. The unavailable page can therefore name the area without
  // retaining a pointer to it or displaying coordinates from the last save.
  next.level_name = engine.LevelName(level);

  Vec3 terrain_size{};
  const bool has_engine_terrain =
      engine.TerrainSize(level, &terrain_size) &&
      UsableTerrainSize(terrain_size);
  NativeMapExtents native_extents{};
  if (NativeMenuMapExtents(&native_extents)) {
    next.has_terrain = true;
    next.extents = native_extents;
    next.space = NativeMapSpace::kNativeMenuMapBounds;
    next.terrain_size = has_engine_terrain
        ? terrain_size
        : Vec3{native_extents.max_x - native_extents.min_x, 0.0f,
               native_extents.max_z - native_extents.min_z};
  } else if (has_engine_terrain) {
    // This is MenuMap's own fallback, recovered from the shipped
    // implementation: its terrain spans [0,size], never [-size/2,+size/2].
    next.has_terrain = true;
    next.terrain_size = terrain_size;
    next.space = NativeMapSpace::kTerrainOriginSchematic;
    next.extents.min_x = 0.0f;
    next.extents.max_x = terrain_size.x;
    next.extents.min_z = 0.0f;
    next.extents.max_z = terrain_size.z;
  }

  if (!player_control) {
    next.availability = NativeMapAvailability::kNoPlayer;
    Publish(std::move(next));
    return;
  }

  Vec3 position{};
  if (!engine.WorldPosition(player_control, &position) || !Finite(position)) {
    next.availability = NativeMapAvailability::kPositionUnavailable;
    Publish(std::move(next));
    return;
  }
  next.has_position = true;
  next.world_position = position;

  Vec3 forward{};
  if (engine.ForwardVector(player_control, &forward) && Finite(forward)) {
    const float planar_length_squared =
        forward.x * forward.x + forward.z * forward.z;
    if (Finite(planar_length_squared) &&
        planar_length_squared > kMinimumDirectionLengthSquared) {
      next.has_heading = true;
      next.forward = forward;
      next.heading_degrees = NormalizedHeading(forward);
    }
  }

  if (!next.has_terrain) {
    next.availability = NativeMapAvailability::kTerrainUnavailable;
    Publish(std::move(next));
    return;
  }

  const float map_width = next.extents.max_x - next.extents.min_x;
  const float map_height = next.extents.max_z - next.extents.min_z;
  // Match the native MenuMap projection before MapAtlas flips world +Z into
  // top-down texture coordinates. This sample is independent of the custom
  // full-map camera, so panning or zooming that page cannot move the minimap.
  next.schematic_u =
      (position.x - next.extents.min_x) / map_width;
  next.schematic_v =
      (position.z - next.extents.min_z) / map_height;
  next.position_inside_schematic =
      next.schematic_u >= 0.0f && next.schematic_u <= 1.0f &&
      next.schematic_v >= 0.0f && next.schematic_v <= 1.0f;
  next.availability = NativeMapAvailability::kReady;
  Publish(std::move(next));
}

void NativeMap::Reset() {
  NativeMapSnapshot next;
  next.availability = NativeMapAvailability::kNoLevel;
  Publish(std::move(next));
}

NativeMapSnapshot NativeMap::Snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_;
}

uint64_t NativeMap::Revision() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return snapshot_.revision;
}

void NativeMap::Publish(NativeMapSnapshot next) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (revision_ != 0 && SameVisibleSnapshot(snapshot_, next)) return;

  // Zero is reserved for a snapshot that has never been sampled. Wrapping is
  // practically unreachable, but skipping zero keeps the contract exact.
  ++revision_;
  if (revision_ == 0) ++revision_;
  next.revision = revision_;
  snapshot_ = std::move(next);
}

}  // namespace dis
