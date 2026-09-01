#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "Engine.h"

namespace dis {

// Coordinate source used to project the live player onto MenuMap's tiled
// artwork. The verified game build exposes the authored map bounds through
// the same level record read by MenuMap. Other builds fail closed to the
// engine's own zero-origin terrain fallback (which is also what MenuMap uses).
enum class NativeMapSpace : uint8_t {
  kNone,
  kTerrainOriginSchematic,
  kNativeMenuMapBounds,
};

enum class NativeMapAvailability : uint8_t {
  kNoLevel,
  kNoPlayer,
  kPositionUnavailable,
  kTerrainUnavailable,
  kReady,
};

struct NativeMapExtents {
  float min_x = 0.0f;
  float min_z = 0.0f;
  float max_x = 0.0f;
  float max_z = 0.0f;
};

// One coherent sample for the custom map page. Callers copy the whole object;
// no pointer in it refers to game-owned memory and no stale player/level object
// can escape into the render thread.
struct NativeMapSnapshot {
  uint64_t revision = 0;
  NativeMapAvailability availability = NativeMapAvailability::kNoLevel;
  NativeMapSpace space = NativeMapSpace::kNone;

  std::string level_name;
  Vec3 world_position{};
  Vec3 forward{};
  Vec3 terrain_size{};
  NativeMapExtents extents{};

  // Heading is clockwise in degrees: +Z is 0, +X is 90. The native
  // hud_compass_pointer artwork needs its own asset-orientation adjustment.
  float heading_degrees = 0.0f;

  // Unclamped coordinates in the native MenuMap world bounds. Values outside
  // [0,1] remain useful diagnostics and are not hidden by a clamp.
  float schematic_u = 0.0f;
  float schematic_v = 0.0f;

  bool has_position = false;
  bool has_heading = false;
  bool has_terrain = false;
  bool position_inside_schematic = false;

  bool available() const {
    return availability == NativeMapAvailability::kReady;
  }
};

const char* NativeMapAvailabilityText(NativeMapAvailability availability);

class NativeMap {
 public:
  // Samples exported Engine APIs plus the signature-gated bounds record read
  // by the shipped MenuMap implementation. `player_control` must be the live
  // IControlObject subobject, not the separately adjusted CRTTIObject pointer.
  // No native UI screen is shown, hidden, focused, or otherwise modified.
  void Update(ILevel* level, const IControlObject* player_control,
              const Engine& engine);

  // Publishes an explicit no-level state and discards all previous values.
  void Reset();

  // Thread-safe value copy for the panel renderer.
  NativeMapSnapshot Snapshot() const;
  uint64_t Revision() const;

 private:
  void Publish(NativeMapSnapshot next);

  mutable std::mutex mutex_;
  NativeMapSnapshot snapshot_{};
  uint64_t revision_ = 0;
};

NativeMap& TheNativeMap();

}  // namespace dis
