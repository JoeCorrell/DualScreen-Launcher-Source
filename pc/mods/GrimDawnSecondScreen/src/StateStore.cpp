#include "StateStore.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace gdds {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
Snapshot g_snapshot = {};
bool g_available = false;
std::atomic<uint64_t> g_sequence{0};

template <size_t N>
bool Terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

template <size_t N>
bool Terminated(const wchar_t (&value)[N]) {
  for (size_t index = 0; index < N; ++index)
    if (value[index] == L'\0') return true;
  return false;
}

void Copy(char* destination, size_t capacity, const char* source) {
  if (!destination || !capacity) return;
  std::snprintf(destination, capacity, "%s", source ? source : "");
}

void ResetLocked(Lifecycle lifecycle, const char* build_label,
                 const char* status, bool supported) {
  const uint64_t next_sequence = g_available ? g_snapshot.sequence + 1 : 1;
  const uint64_t next_epoch = g_available ? g_snapshot.world_epoch + 1 : 1;
  g_snapshot = {};
  g_snapshot.version = kSnapshotVersion;
  g_snapshot.size = sizeof(Snapshot);
  g_snapshot.sequence = next_sequence;
  g_snapshot.world_epoch = next_epoch;
  g_snapshot.lifecycle = lifecycle;
  // Kept in the API because callers distinguish supported lifecycle states
  // from fail-closed states. Both deliberately publish zero capabilities.
  (void)supported;
  Copy(g_snapshot.build_label, sizeof(g_snapshot.build_label), build_label);
  Copy(g_snapshot.status, sizeof(g_snapshot.status), status);
  g_available = true;
  g_sequence.store(next_sequence, std::memory_order_release);
}

bool Valid(const Snapshot& value) {
  constexpr uint64_t kKnownCapabilities = (kCapDifficulty << 1) - 1;
  if (value.version != kSnapshotVersion || value.size != sizeof(Snapshot) ||
      !value.sequence || !value.world_epoch ||
      static_cast<uint32_t>(value.lifecycle) >
          static_cast<uint32_t>(Lifecycle::kFaulted) ||
      !Terminated(value.build_label) || !Terminated(value.character_name) ||
      !Terminated(value.area_tag) || !Terminated(value.status) ||
      value.inventory_count > 4096 || value.skill_count > 2048 ||
      value.skill_points > 10000 || value.devotion_points_available > 10000 ||
      value.devotion_points_total > 10000 || value.devotion_points_max > 10000 ||
      value.riftgate_count > 1024 || value.level > 250 ||
      (value.capabilities & ~kKnownCapabilities) != 0 ||
      value.difficulty > 2)
    return false;
  const float scalars[] = {
      value.health, value.max_health, value.energy, value.max_energy,
      value.physique, value.cunning, value.spirit, value.offensive_ability,
      value.defensive_ability, value.player_x, value.player_y, value.player_z,
      value.player_w, value.sample_milliseconds};
  for (float scalar : scalars)
    if (!std::isfinite(scalar) || std::fabs(scalar) > 100000000.0f)
      return false;
  return value.health >= 0.0f && value.max_health >= 0.0f &&
         value.energy >= 0.0f && value.max_energy >= 0.0f &&
         value.sample_milliseconds >= 0.0f &&
         (!(value.capabilities & kCapDevotion) ||
          (value.devotion_points_available <= value.devotion_points_total &&
           value.devotion_points_total <= value.devotion_points_max)) &&
         (!(value.capabilities & kCapIdentity) || value.level != 0) &&
         (!(value.capabilities & kCapVitals) ||
          (value.health <= value.max_health &&
           value.energy <= value.max_energy));
}

}  // namespace

void ResetState(Lifecycle lifecycle, const char* build_label,
                const char* status, bool supported) {
  AcquireSRWLockExclusive(&g_lock);
  ResetLocked(lifecycle, build_label, status, supported);
  ReleaseSRWLockExclusive(&g_lock);
}

bool ResetStateIfChanged(Lifecycle lifecycle, const char* build_label,
                         const char* status, bool supported) {
  const char* expected_build = build_label ? build_label : "";
  const char* expected_status = status ? status : "";
  bool changed = false;
  AcquireSRWLockExclusive(&g_lock);
  if (!g_available || g_snapshot.lifecycle != lifecycle ||
      std::strcmp(g_snapshot.build_label, expected_build) != 0 ||
      std::strcmp(g_snapshot.status, expected_status) != 0) {
    ResetLocked(lifecycle, expected_build, expected_status, supported);
    changed = true;
  }
  ReleaseSRWLockExclusive(&g_lock);
  return changed;
}

bool PublishSnapshot(const Snapshot& snapshot) {
  if (!Valid(snapshot)) return false;
  bool accepted = false;
  AcquireSRWLockExclusive(&g_lock);
  if (!g_available ||
      (snapshot.world_epoch >= g_snapshot.world_epoch &&
       snapshot.sequence > g_snapshot.sequence)) {
    g_snapshot = snapshot;
    g_available = true;
    g_sequence.store(snapshot.sequence, std::memory_order_release);
    accepted = true;
  }
  ReleaseSRWLockExclusive(&g_lock);
  return accepted;
}

bool CopyLatestSnapshot(Snapshot* output) {
  if (!output) return false;
  AcquireSRWLockShared(&g_lock);
  const bool available = g_available;
  if (available) *output = g_snapshot;
  ReleaseSRWLockShared(&g_lock);
  return available;
}

uint64_t LatestSequence() {
  return g_sequence.load(std::memory_order_acquire);
}

}  // namespace gdds
