#include <cstdio>

#include "PanelStatePolicy.h"

namespace {

int g_failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++g_failures;
}

}  // namespace

int main() {
  using namespace gdds;
  Snapshot waiting = {};
  waiting.lifecycle = Lifecycle::kNoCharacter;
  waiting.world_epoch = 1;
  Snapshot live = waiting;
  live.lifecycle = Lifecycle::kLive;
  live.world_epoch = 2;
  live.stats_revision = 1;

  Check(PanelContentKey(&waiting, true, 0) !=
            PanelContentKey(&live, true, 0),
        "waiting-to-live always invalidates the panel texture");

  Snapshot moved = live;
  moved.map_revision++;
  Check(PanelContentKey(&live, true, 0) ==
            PanelContentKey(&moved, true, 0),
        "movement does not rebuild the Character page texture");
  Check(PanelContentKey(&live, true, 4) !=
            PanelContentKey(&moved, true, 4),
        "movement rebuilds the Map page texture");

  Snapshot damaged = live;
  damaged.stats_revision++;
  Check(PanelContentKey(&live, true, 0) !=
            PanelContentKey(&damaged, true, 0),
        "stat changes rebuild the Character page texture");
  Check(PanelContentKey(&live, true, 2) ==
            PanelContentKey(&damaged, true, 2),
        "stat changes do not rebuild the Skills page texture");
  Check(PanelContentKey(&live, true, 0) !=
            PanelContentKey(&live, true, 1),
        "page changes always invalidate panel content");

  if (g_failures) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
  }
  std::puts("Panel state policy tests passed");
  return 0;
}
