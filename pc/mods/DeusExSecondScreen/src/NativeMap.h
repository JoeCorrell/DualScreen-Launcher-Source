// Native, pause-free map data for the custom lower-screen interface.
//
// DXHR's normal map is a Scaleform child movie owned by GameMenu. Opening that
// controller also enters the modal/pause path, so the lower screen must not use
// it as a data source. The routines below instead read the same dtp::Map record
// and the already-running HUD radar that vanilla uses.

#pragma once

#include <cstdint>

namespace dxs {

constexpr int kMaximumNativeObjectiveMarkers = 32;

class HookSet;
class RttiIndex;

enum class NativeMapMarkerKind : uint8_t {
  kStatic = 0,
  kFriend,
  kNeutral,
  kCautious,
  kAlert,
  kHostile,
};

struct NativeMapMarker {
  NativeMapMarkerKind kind = NativeMapMarkerKind::kStatic;
  uint16_t radar_type = 0;
  uint16_t radar_state = 0;
  uint32_t entity_key = 0;
  int level = -1;
  float world_x = 0.0f;
  float world_y = 0.0f;
  float world_z = 0.0f;
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
  char name[96] = {};
  char icon[128] = {};
};

struct NativeObjectiveMarker {
  int mission_id = -1;
  // -1 means the game supplied a valid map position but no safe floor anchor
  // could be matched. Such pins remain visible rather than disappearing.
  int level = -1;
  bool primary = false;
  bool tracked = false;
  float world_x = 0.0f;
  float world_y = 0.0f;
  float world_z = 0.0f;
  float normalized_x = 0.0f;
  float normalized_y = 0.0f;
  char title[128] = {};
};

struct NativeMapSnapshot {
  bool valid = false;
  bool player_valid = false;
  bool player_heading_valid = false;
  bool radar_live = false;
  uint32_t map_resource_id = 0;
  // `level` is Jensen's live floor. `display_level` is the floor selected on
  // the lower screen and may differ while the player browses other levels.
  int level = 0;
  int display_level = 0;
  int floor_count = 0;
  int floors[65] = {};

  // Vanilla normalizes from bound_a and takes the absolute quotient. Keeping
  // both endpoints as well as width/height makes that transform explicit.
  float bound_a_x = 0.0f;
  float bound_a_y = 0.0f;
  float bound_b_x = 0.0f;
  float bound_b_y = 0.0f;
  float width = 0.0f;
  float height = 0.0f;

  float player_world_x = 0.0f;
  float player_world_y = 0.0f;
  float player_world_z = 0.0f;
  float player_x = 0.0f;
  float player_y = 0.0f;
  // Clockwise screen-space degrees, using the same live transform and map-axis
  // correction as SmartGlassMap::SerializePlayer in DXHRDC 2.0.66.0.
  float player_heading_degrees = 0.0f;

  // Filled by the panel from the selected per-floor .dxm cache entry. These
  // are the actual raster dimensions, not the world-space map bounds.
  uint32_t raster_width = 0;
  uint32_t raster_height = 0;
  int raster_floor = 0;
  bool raster_exact_floor = false;

  char location[96] = {};
  char sublocation[96] = {};
  // Full source path and the basename sent by the retail SmartGlass map
  // serializer. The resource is a vector Scaleform SWF, not a PCD9 texture.
  char image_path[260] = {};
  char image_name[96] = {};

  int marker_count = 0;
  NativeMapMarker markers[256] = {};
  int objective_marker_count = 0;
  NativeObjectiveMarker objective_markers[kMaximumNativeObjectiveMarkers] = {};
};

// Installs one exact-build vtable hook on NsRadarMovieController::Update. The
// hook calls vanilla first and then copies already-filtered contact state. It
// neither changes radar/gameplay state nor scans the world actor collection.
bool InstallNativeRadarCapture(RttiIndex* rtti, HookSet* hooks);

// Call on the same Present/game thread used by CaptureInventorySnapshot. No
// game-menu or SmartGlass controller is entered, so this never pauses the game.
bool CaptureNativeMapSnapshot(NativeMapSnapshot* out);

// Refresh only Jensen's position and facing against bounds already captured in
// `snapshot`. This avoids walking static markers merely to animate the arrow.
bool RefreshNativePlayerMapPose(NativeMapSnapshot* snapshot);

// Stop accepting new radar snapshots before teardown. Hook removal remains
// owned by HookSet; call DisableNativeMapCapture(), then HookSet::RemoveAll(),
// then ResetNativeMapCapture().
void DisableNativeMapCapture();

// Waits for a callback already inside the hook and clears borrowed controller
// state. This must be called only after HookSet restored the radar vtable.
void ResetNativeMapCapture();

}  // namespace dxs
