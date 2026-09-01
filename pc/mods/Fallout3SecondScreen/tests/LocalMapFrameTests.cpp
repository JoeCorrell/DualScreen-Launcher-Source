#include <cstdio>
#include <memory>

#include "LocalMapFrame.h"

namespace {

int failures = 0;

void Check(bool condition, const char* name) {
  if (condition) {
    std::printf("PASS: %s\n", name);
  } else {
    std::fprintf(stderr, "FAIL: %s\n", name);
    ++failures;
  }
}

std::shared_ptr<fo3ds::LocalMapFrame> Frame(uint64_t epoch,
                                            uint64_t revision,
                                            uint32_t cell) {
  auto frame = std::make_shared<fo3ds::LocalMapFrame>();
  frame->world_epoch = epoch;
  frame->revision = revision;
  frame->cell_form_id = cell;
  frame->width = fo3ds::kLocalMapRasterSide;
  frame->height = fo3ds::kLocalMapRasterSide;
  frame->min_x = -4096.0f;
  frame->max_y = 4096.0f;
  frame->max_x = 4096.0f;
  frame->min_y = -4096.0f;
  frame->bgra.resize(static_cast<size_t>(frame->width) * frame->height,
                     0xFF00FF00u);
  return frame;
}

}  // namespace

int main() {
  std::vector<fo3ds::LocalMapMarker> merged_markers;
  const std::vector<fo3ds::LocalMapMarker> marker_batch = {
      {0x10u, fo3ds::kLocalMapMarkerDoor, 100.0f, 200.0f, 0.0f},
      {0x11u, fo3ds::kLocalMapMarkerDoor, 108.0f, 204.0f, 4.0f},
      {0x20u, fo3ds::kLocalMapMarkerQuest, 108.0f, 204.0f, 4.0f},
      {0x10u, fo3ds::kLocalMapMarkerQuest, 100.0f, 200.0f, 0.0f}};
  Check(fo3ds::MergeLocalMapMarkers(marker_batch, &merged_markers) == 2,
        "logical local-map duplicates are coalesced at source");
  Check(merged_markers.size() == 2 &&
            merged_markers[0].form_id == 0x10u &&
            (merged_markers[0].flags & fo3ds::kLocalMapMarkerDoor) &&
            (merged_markers[0].flags & fo3ds::kLocalMapMarkerQuest) &&
            merged_markers[1].form_id == 0x20u,
        "same-kind aliases merge while distinct quest pins remain");

  fo3ds::ClearLocalMapFrame();
  auto valid = Frame(2, 1, 0x1234);
  valid->markers.push_back(
      {0xAABBCCDDu, fo3ds::kLocalMapMarkerDoor, 128.0f, -64.0f, 5.0f});
  Check(fo3ds::PublishLocalMapFrame(valid), "valid frame publishes");
  Check(fo3ds::AcquireLocalMapFrame() == valid,
        "published frame is acquired by immutable shared ownership");
  fo3ds::LocalMapCursor cursor = {};
  cursor.revision = 1;
  cursor.world_epoch = 2;
  cursor.cell_form_id = 0x1234;
  cursor.x = 10.0f;
  cursor.y = 20.0f;
  cursor.heading = 0.5f;
  Check(fo3ds::PublishLocalMapCursor(cursor), "valid cursor publishes");
  fo3ds::LocalMapCursor observed = {};
  Check(fo3ds::AcquireLocalMapCursor(&observed) && observed.x == 10.0f &&
            observed.y == 20.0f,
        "cursor position is acquired independently of the image frame");
  Check(!fo3ds::PublishLocalMapCursor(cursor),
        "stale cursor revision is rejected");

  auto stale = Frame(2, 1, 0x5678);
  Check(!fo3ds::PublishLocalMapFrame(stale),
        "same-epoch stale revision is rejected");
  Check(fo3ds::AcquireLocalMapFrame() == valid,
        "stale publish cannot replace current frame");

  auto next = Frame(2, 2, 0x5678);
  Check(fo3ds::PublishLocalMapFrame(next),
        "newer same-epoch revision publishes");
  auto invalid = Frame(3, 1, 0x9999);
  invalid->max_x = invalid->min_x;
  Check(!fo3ds::PublishLocalMapFrame(invalid),
        "degenerate coordinate bounds are rejected");
  auto invalid_marker = Frame(3, 2, 0x9999);
  invalid_marker->markers.push_back(
      {0u, fo3ds::kLocalMapMarkerDoor, 0.0f, 0.0f, 0.0f});
  Check(!fo3ds::PublishLocalMapFrame(invalid_marker),
        "invalid local marker is rejected with its frame");

  fo3ds::ClearLocalMapFrame();
  Check(!fo3ds::AcquireLocalMapFrame(), "clear releases the stored frame");
  Check(!fo3ds::AcquireLocalMapCursor(&observed),
        "clear releases the live cursor");
  if (failures) return 1;
  std::puts("Local-map immutable frame tests passed");
  return 0;
}
