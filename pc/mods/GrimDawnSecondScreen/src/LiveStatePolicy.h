#pragma once

#include <cstdint>

#include "Snapshot.h"

namespace gdds {

// Primary-world submission and Present normally share an update epoch, but
// WEMU can legitimately present after more than one Update while a world is
// activating. Keep the epoch bound small and require a very recent render
// observation so a stale Player can never become a telemetry source.
constexpr uint32_t kMaximumRenderObservationAgeMs = 250;
constexpr uint32_t kMaximumRenderObservationEpochGap = 2;

// A single absent render submission is not proof that a save was unloaded.
// Preserve an already-published live snapshot across short loading/render
// gaps. The Player pointer is never dereferenced during this grace period.
constexpr uint32_t kLiveSnapshotGraceMs = 5000;

inline uint32_t TickAge(uint32_t now, uint32_t then) {
  return now - then;
}

inline bool IsRecentRenderObservation(uint32_t current_epoch,
                                      uint32_t observed_epoch,
                                      uint32_t age_ms) {
  const uint32_t epoch_gap = current_epoch - observed_epoch;
  return age_ms <= kMaximumRenderObservationAgeMs &&
         epoch_gap <= kMaximumRenderObservationEpochGap;
}

inline bool ShouldPreserveLiveSnapshot(Lifecycle lifecycle,
                                       uint32_t now_tick,
                                       uint32_t last_observed_tick) {
  return lifecycle == Lifecycle::kLive && last_observed_tick != 0 &&
         TickAge(now_tick, last_observed_tick) <= kLiveSnapshotGraceMs;
}

}  // namespace gdds
