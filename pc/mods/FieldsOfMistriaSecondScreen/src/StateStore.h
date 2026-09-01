#pragma once

#include <cstddef>
#include <cstdint>

#include "BuildIdentity.h"

namespace fomds {

inline constexpr std::uint32_t kDiagnosticSnapshotVersion = 1;

enum class Lifecycle : std::uint32_t {
  kBoot,
  kCompatible,
  kUnsupported,
  kPanelReady,
  kStopping,
};

struct DiagnosticSnapshot {
  std::uint32_t version = kDiagnosticSnapshotVersion;
  std::uint32_t size = sizeof(DiagnosticSnapshot);
  std::uint64_t sequence = 0;
  Lifecycle lifecycle = Lifecycle::kBoot;
  bool supported = false;
  char status[256] = {};
  char executable_version[32] = {};
  char sdl_version[32] = {};
  char executable_sha256[65] = {};
  char sdl_sha256[65] = {};
};

void ResetState();
bool PublishCompatibility(const BuildIdentity& identity, Lifecycle lifecycle,
                          const char* status = nullptr);
bool CopyLatestSnapshot(DiagnosticSnapshot* output);
std::uint64_t LatestSequence();

}  // namespace fomds
