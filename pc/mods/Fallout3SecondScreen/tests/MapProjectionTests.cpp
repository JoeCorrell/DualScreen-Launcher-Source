#include <cstdio>
#include <cstdlib>

#include "MapProjection.h"

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

bool Project(const fo3ds::MapProjection& view, float x, float y,
             int* screen_x, int* screen_y) {
  return fo3ds::ProjectMapPoint(-1000.0f, 1000.0f, 1000.0f, -1000.0f,
                                view, 64, 116, 1112, 806, x, y,
                                screen_x, screen_y);
}

}  // namespace

int main() {
  fo3ds::MapProjection centered = {};
  Check(fo3ds::BuildMapProjection(
            -1000.0f, 1000.0f, 1000.0f, -1000.0f, 1280.0f, 1280.0f,
            1112, 806, 0.0f, 0.0f, 0.0f, 0.0f, 2.0f, &centered),
        "centered map projection builds");
  int player_x = 0;
  int player_y = 0;
  int door_x = 0;
  int door_y = 0;
  Check(Project(centered, 0.0f, 0.0f, &player_x, &player_y),
        "player projects in centered map");
  Check(Project(centered, 150.0f, -120.0f, &door_x, &door_y),
        "door marker projects in centered map");
  const int centered_delta_x = door_x - player_x;
  const int centered_delta_y = door_y - player_y;

  fo3ds::MapProjection panned = {};
  Check(fo3ds::BuildMapProjection(
            -1000.0f, 1000.0f, 1000.0f, -1000.0f, 1280.0f, 1280.0f,
            1112, 806, 0.0f, 0.0f, -150.0f, 80.0f, 2.0f, &panned),
        "panned map projection builds");
  int panned_player_x = 0;
  int panned_player_y = 0;
  int panned_door_x = 0;
  int panned_door_y = 0;
  Check(Project(panned, 0.0f, 0.0f, &panned_player_x, &panned_player_y) &&
            Project(panned, 150.0f, -120.0f, &panned_door_x,
                    &panned_door_y),
        "player and door use the panned projection");
  Check(panned_player_x != player_x || panned_player_y != player_y,
        "player marker is map-space, not pinned to the UI");
  Check(std::abs((panned_door_x - panned_player_x) - centered_delta_x) <= 1 &&
            std::abs((panned_door_y - panned_player_y) - centered_delta_y) <= 1,
        "pan preserves player-to-marker alignment");

  fo3ds::MapProjection zoomed = {};
  Check(fo3ds::BuildMapProjection(
            -1000.0f, 1000.0f, 1000.0f, -1000.0f, 1280.0f, 1280.0f,
            1112, 806, 0.0f, 0.0f, 0.0f, 0.0f, 4.0f, &zoomed),
        "zoomed map projection builds");
  int zoom_player_x = 0;
  int zoom_player_y = 0;
  int zoom_door_x = 0;
  int zoom_door_y = 0;
  Check(Project(zoomed, 0.0f, 0.0f, &zoom_player_x, &zoom_player_y) &&
            Project(zoomed, 150.0f, -120.0f, &zoom_door_x, &zoom_door_y),
        "player and door use the zoomed projection");
  Check(std::abs(zoom_door_x - zoom_player_x) > std::abs(centered_delta_x) &&
            std::abs(zoom_door_y - zoom_player_y) > std::abs(centered_delta_y),
        "zoom scales every overlay around the same map origin");

  int edge_x = 0;
  int edge_y = 0;
  const float visible_edge_world_x = -1000.0f + centered.right * 2000.0f;
  Check(Project(centered, visible_edge_world_x, 0.0f, &edge_x, &edge_y),
        "ordinary projection admits a point on the visible edge");
  Check(!fo3ds::ProjectMapPointInset(
            -1000.0f, 1000.0f, 1000.0f, -1000.0f, centered,
            64, 116, 1112, 806, 30, 30, visible_edge_world_x, 0.0f,
            &edge_x, &edge_y),
        "icon-aware projection rejects markers that would cross map bounds");

  if (failures) return 1;
  std::puts("Map projection tests passed");
  return 0;
}
