#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace fo3ds {

constexpr uint32_t kLocalMapGridSide = 5;
constexpr uint32_t kLocalMapTileSide = 256;
constexpr uint32_t kLocalMapRasterSide =
    kLocalMapGridSide * kLocalMapTileSide;
constexpr uint32_t kMaxLocalMapMarkers = 256;

enum LocalMapMarkerFlags : uint32_t {
  kLocalMapMarkerDoor = 1u << 0,
  kLocalMapMarkerQuest = 1u << 1,
};

struct LocalMapMarker {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

// Immutable, mod-owned copy of Fallout's native per-cell local-map render.
// No engine pointer, D3D object, or MapMenu object may cross this boundary.
struct LocalMapFrame {
  uint64_t revision = 0;
  uint64_t world_epoch = 0;
  uint32_t cell_form_id = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  float min_x = 0.0f;
  float max_y = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_heading = 0.0f;
  // Native cell-reference markers copied on the game thread. Engine pointers
  // never cross this boundary and every point uses the frame's map space.
  std::vector<LocalMapMarker> markers;
  std::vector<uint32_t> bgra;
};

struct LocalMapCursor {
  uint64_t revision = 0;
  uint64_t world_epoch = 0;
  uint32_t cell_form_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float heading = 0.0f;
};

// MapMenu can expose the same logical door through more than one participating
// cell list.  Coalesce those records in native map space before publishing a
// frame so the panel never has to guess which duplicate icon to hide.
size_t MergeLocalMapMarkers(const std::vector<LocalMapMarker>& source,
                            std::vector<LocalMapMarker>* destination);

using LocalMapFramePtr = std::shared_ptr<const LocalMapFrame>;

bool PublishLocalMapFrame(std::shared_ptr<LocalMapFrame> frame);
LocalMapFramePtr AcquireLocalMapFrame();
bool PublishLocalMapCursor(const LocalMapCursor& cursor);
bool AcquireLocalMapCursor(LocalMapCursor* cursor);
void ClearLocalMapFrame();

}  // namespace fo3ds
