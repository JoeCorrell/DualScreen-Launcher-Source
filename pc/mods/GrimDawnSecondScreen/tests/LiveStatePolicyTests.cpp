#include <cstdio>
#include <cstdint>

#include "LiveStatePolicy.h"

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

  Check(IsRecentRenderObservation(100, 100, 0),
        "same-frame render observation accepted");
  Check(IsRecentRenderObservation(100, 99, 100),
        "one-update render gap accepted");
  Check(IsRecentRenderObservation(100, 98, 113),
        "two-update WEMU render gap accepted");
  Check(!IsRecentRenderObservation(100, 97, 10),
        "larger epoch gap rejected");
  Check(!IsRecentRenderObservation(100, 100,
                                   kMaximumRenderObservationAgeMs + 1),
        "old render observation rejected");
  Check(!IsRecentRenderObservation(100, 101, 0),
        "future render epoch rejected through unsigned distance");

  Check(ShouldPreserveLiveSnapshot(Lifecycle::kLive, 6000, 1000),
        "live snapshot survives the complete grace interval");
  Check(!ShouldPreserveLiveSnapshot(Lifecycle::kLive, 6001, 1000),
        "live snapshot expires after sustained absence");
  Check(!ShouldPreserveLiveSnapshot(Lifecycle::kNoCharacter, 1100, 1000),
        "waiting state is never converted into stale live state");
  Check(!ShouldPreserveLiveSnapshot(Lifecycle::kLive, 1100, 0),
        "missing observation tick cannot preserve live state");
  Check(ShouldPreserveLiveSnapshot(Lifecycle::kLive, 0x20u, 0xfffffff0u),
        "32-bit tick wrap keeps recent observations valid");

  if (g_failures) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
  }
  std::puts("Live-state lifecycle policy tests passed");
  return 0;
}
