#include "LocalMapFrame.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace fo3ds {
namespace {

SRWLOCK g_frame_lock = SRWLOCK_INIT;
LocalMapFramePtr g_frame;
LocalMapCursor g_cursor;

bool ValidFrame(const LocalMapFrame& frame) {
  const size_t expected =
      static_cast<size_t>(frame.width) * static_cast<size_t>(frame.height);
  return frame.revision != 0 && frame.world_epoch != 0 &&
         frame.cell_form_id != 0 && frame.width == kLocalMapRasterSide &&
         frame.height == kLocalMapRasterSide &&
         expected == frame.bgra.size() &&
         std::isfinite(frame.min_x) && std::isfinite(frame.max_y) &&
         std::isfinite(frame.max_x) && std::isfinite(frame.min_y) &&
         std::isfinite(frame.player_x) && std::isfinite(frame.player_y) &&
         std::isfinite(frame.player_heading) && frame.max_x > frame.min_x &&
         frame.max_y > frame.min_y &&
         frame.markers.size() <= kMaxLocalMapMarkers &&
         std::all_of(frame.markers.begin(), frame.markers.end(),
                     [](const LocalMapMarker& marker) {
                       return marker.form_id != 0 && marker.flags != 0 &&
                              std::isfinite(marker.x) &&
                              std::isfinite(marker.y) &&
                              std::isfinite(marker.z);
                     });
}

}  // namespace

size_t MergeLocalMapMarkers(const std::vector<LocalMapMarker>& source,
                            std::vector<LocalMapMarker>* destination) {
  if (!destination) return 0;
  // One raster pixel represents 16 world units in the 5x5-cell/1280-pixel
  // capture. References closer than that produce the same native pin. Only
  // markers of the same semantic kind are merged unless their FormID is
  // identical (a quest target may legitimately promote an existing door).
  constexpr float kOneRasterPixel = 16.0f;
  constexpr float kMergeDistanceSquared =
      kOneRasterPixel * kOneRasterPixel;
  size_t merged = 0;
  for (const LocalMapMarker& marker : source) {
    auto existing = std::find_if(
        destination->begin(), destination->end(),
        [&marker](const LocalMapMarker& candidate) {
          if (candidate.form_id == marker.form_id) return true;
          if ((candidate.flags & marker.flags) == 0) return false;
          const float dx = candidate.x - marker.x;
          const float dy = candidate.y - marker.y;
          return dx * dx + dy * dy <= kMergeDistanceSquared;
        });
    if (existing != destination->end()) {
      existing->flags |= marker.flags;
      ++merged;
      continue;
    }
    if (destination->size() >= kMaxLocalMapMarkers) break;
    destination->push_back(marker);
  }
  return merged;
}

bool PublishLocalMapFrame(std::shared_ptr<LocalMapFrame> frame) {
  if (!frame || !ValidFrame(*frame)) return false;
  AcquireSRWLockExclusive(&g_frame_lock);
  const bool accepted =
      !g_frame || frame->world_epoch > g_frame->world_epoch ||
      (frame->world_epoch == g_frame->world_epoch &&
       frame->revision > g_frame->revision);
  if (accepted)
    g_frame = std::move(frame);
  ReleaseSRWLockExclusive(&g_frame_lock);
  return accepted;
}

LocalMapFramePtr AcquireLocalMapFrame() {
  AcquireSRWLockShared(&g_frame_lock);
  LocalMapFramePtr frame = g_frame;
  ReleaseSRWLockShared(&g_frame_lock);
  return frame;
}

bool PublishLocalMapCursor(const LocalMapCursor& cursor) {
  if (!cursor.revision || !cursor.world_epoch || !cursor.cell_form_id ||
      !std::isfinite(cursor.x) || !std::isfinite(cursor.y) ||
      !std::isfinite(cursor.heading))
    return false;
  AcquireSRWLockExclusive(&g_frame_lock);
  const bool accepted = !g_cursor.revision ||
      cursor.world_epoch > g_cursor.world_epoch ||
      (cursor.world_epoch == g_cursor.world_epoch &&
       cursor.revision > g_cursor.revision);
  if (accepted) g_cursor = cursor;
  ReleaseSRWLockExclusive(&g_frame_lock);
  return accepted;
}

bool AcquireLocalMapCursor(LocalMapCursor* cursor) {
  if (!cursor) return false;
  AcquireSRWLockShared(&g_frame_lock);
  const bool available = g_cursor.revision != 0;
  if (available) *cursor = g_cursor;
  ReleaseSRWLockShared(&g_frame_lock);
  return available;
}

void ClearLocalMapFrame() {
  AcquireSRWLockExclusive(&g_frame_lock);
  g_frame.reset();
  g_cursor = {};
  ReleaseSRWLockExclusive(&g_frame_lock);
}

}  // namespace fo3ds
