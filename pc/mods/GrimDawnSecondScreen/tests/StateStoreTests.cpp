#include <windows.h>

#include <cstdio>
#include <cstring>

#include "BuildIdentity.h"
#include "Snapshot.h"
#include "StateStore.h"

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
  Snapshot copy = {};
  ResetState(Lifecycle::kBoot, "unit-test", "booting", false);
  Check(CopyLatestSnapshot(&copy), "reset publishes a snapshot");
  Check(copy.sequence == 1 && copy.world_epoch == 1,
        "reset initializes sequence and epoch");
  Check(copy.size == sizeof(Snapshot), "snapshot ABI size stamped");
  Check(std::strcmp(copy.status, "booting") == 0, "status preserved");
  Check(!ResetStateIfChanged(Lifecycle::kBoot, "unit-test", "booting", false),
        "identical lifecycle reset is suppressed");
  Check(CopyLatestSnapshot(&copy) && copy.sequence == 1 && copy.world_epoch == 1,
        "suppressed lifecycle reset preserves sequence and epoch");
  Check(ResetStateIfChanged(Lifecycle::kWaitingForGame, "unit-test",
                            "waiting", true),
        "changed lifecycle is published");
  Check(CopyLatestSnapshot(&copy) && copy.sequence == 2 && copy.world_epoch == 2,
        "changed lifecycle advances sequence and epoch once");

  Snapshot next = {};
  next.version = kSnapshotVersion;
  next.size = sizeof(Snapshot);
  next.sequence = 3;
  next.world_epoch = copy.world_epoch;
  next.lifecycle = Lifecycle::kLive;
  next.capabilities = kCapVitals;
  next.health = 90.0f;
  next.max_health = 100.0f;
  Check(PublishSnapshot(next), "new live snapshot accepted");
  Check(!PublishSnapshot(next), "duplicate sequence rejected");
  Check(CopyLatestSnapshot(&copy) && copy.health == 90.0f,
        "published value copied exactly");

  next.sequence = 4;
  next.inventory_count = 4097;
  Check(!PublishSnapshot(next), "impossible inventory count rejected");
  next.inventory_count = 0;
  next.sequence = 5;
  next.health = -1.0f;
  Check(!PublishSnapshot(next), "negative health rejected");
  next.health = 90.0f;
  next.sequence = 6;
  next.difficulty = 3;
  Check(!PublishSnapshot(next), "out-of-range difficulty rejected");
  next.difficulty = 0;
  next.sequence = 7;
  next.health = 101.0f;
  Check(!PublishSnapshot(next), "health above maximum rejected");
  next.health = 90.0f;
  next.sequence = 8;
  next.capabilities |= 1ull << 63;
  Check(!PublishSnapshot(next), "unknown capability bit rejected");
  next.capabilities &= ~(1ull << 63);
  next.sequence = 9;
  next.capabilities |= kCapDevotion;
  next.devotion_points_available = 2;
  next.devotion_points_total = 1;
  next.devotion_points_max = 55;
  Check(!PublishSnapshot(next), "invalid devotion point ordering rejected");
  next.capabilities &= ~kCapDevotion;
  next.devotion_points_available = next.devotion_points_total =
      next.devotion_points_max = 0;
  next.sequence = 10;
  std::memset(next.status, 'X', sizeof(next.status));
  Check(!PublishSnapshot(next), "unterminated text rejected");

  const BuildIdentity identity = IdentifyInstalledBuild();
  Check(identity.valid_pe && identity.is_i386,
        "32-bit test executable identified as PE32");
  Check(identity.executable.present && identity.executable.size != 0 &&
            identity.executable.sha256[0],
        "test executable receives stable file identity");
  Check(!identity.supported, "unrelated executable fails exact build gate");

  if (g_failures) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
  }
  std::puts("StateStore/BuildIdentity tests passed");
  return 0;
}
