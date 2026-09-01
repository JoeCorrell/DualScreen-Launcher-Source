#include "StateStore.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>

namespace fomds {
namespace {

SRWLOCK g_lock = SRWLOCK_INIT;
DiagnosticSnapshot g_snapshot = {};
bool g_available = false;
std::atomic<std::uint64_t> g_sequence{0};

template <std::size_t N>
bool Terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

bool Valid(const DiagnosticSnapshot& value) {
  return value.version == kDiagnosticSnapshotVersion &&
      value.size == sizeof(DiagnosticSnapshot) && value.sequence != 0 &&
      static_cast<std::uint32_t>(value.lifecycle) <=
          static_cast<std::uint32_t>(Lifecycle::kStopping) &&
      Terminated(value.status) && Terminated(value.executable_version) &&
      Terminated(value.sdl_version) && Terminated(value.executable_sha256) &&
      Terminated(value.sdl_sha256);
}

void Copy(char* destination, std::size_t capacity, const char* source) {
  if (!destination || !capacity) return;
  std::snprintf(destination, capacity, "%s", source ? source : "");
}

}  // namespace

void ResetState() {
  AcquireSRWLockExclusive(&g_lock);
  g_snapshot = {};
  g_snapshot.version = kDiagnosticSnapshotVersion;
  g_snapshot.size = sizeof(DiagnosticSnapshot);
  g_snapshot.sequence = 1;
  g_snapshot.lifecycle = Lifecycle::kBoot;
  Copy(g_snapshot.status, sizeof(g_snapshot.status), "initializing");
  g_available = true;
  g_sequence.store(1, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_lock);
}

bool PublishCompatibility(const BuildIdentity& identity, Lifecycle lifecycle,
                          const char* status) {
  DiagnosticSnapshot next = {};
  next.version = kDiagnosticSnapshotVersion;
  next.size = sizeof(DiagnosticSnapshot);
  next.lifecycle = lifecycle;
  next.supported = identity.supported;
  Copy(next.status, sizeof(next.status), status ? status : identity.reason);
  Copy(next.executable_version, sizeof(next.executable_version),
       identity.executable.version);
  Copy(next.sdl_version, sizeof(next.sdl_version), identity.sdl.version);
  Copy(next.executable_sha256, sizeof(next.executable_sha256),
       identity.executable.sha256);
  Copy(next.sdl_sha256, sizeof(next.sdl_sha256), identity.sdl.sha256);

  AcquireSRWLockExclusive(&g_lock);
  next.sequence = g_available ? g_snapshot.sequence + 1 : 1;
  if (!Valid(next)) {
    ReleaseSRWLockExclusive(&g_lock);
    return false;
  }
  g_snapshot = next;
  g_available = true;
  g_sequence.store(next.sequence, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_lock);
  return true;
}

bool CopyLatestSnapshot(DiagnosticSnapshot* output) {
  if (!output) return false;
  AcquireSRWLockShared(&g_lock);
  const bool available = g_available;
  if (available) *output = g_snapshot;
  ReleaseSRWLockShared(&g_lock);
  return available;
}

std::uint64_t LatestSequence() {
  return g_sequence.load(std::memory_order_acquire);
}

}  // namespace fomds
