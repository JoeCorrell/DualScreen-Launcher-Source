#include <cstdio>
#include <cstring>

#include "BuildIdentity.h"
#include "StateStore.h"

int main() {
  fomds::ResetState();
  fomds::DiagnosticSnapshot state = {};
  if (!fomds::CopyLatestSnapshot(&state) || state.sequence != 1 ||
      state.lifecycle != fomds::Lifecycle::kBoot) {
    return 1;
  }
  fomds::BuildIdentity identity;
  identity.supported = true;
  std::snprintf(identity.reason, sizeof(identity.reason), "unit compatible");
  std::snprintf(identity.executable.version, sizeof(identity.executable.version),
                "1.0.4");
  std::snprintf(identity.sdl.version, sizeof(identity.sdl.version), "3.4.8.0");
  std::memset(identity.executable.sha256, 'a', 64);
  identity.executable.sha256[64] = '\0';
  std::memset(identity.sdl.sha256, 'b', 64);
  identity.sdl.sha256[64] = '\0';
  if (!fomds::PublishCompatibility(identity, fomds::Lifecycle::kCompatible) ||
      !fomds::CopyLatestSnapshot(&state) || state.sequence != 2 ||
      !state.supported || std::strcmp(state.status, "unit compatible") != 0 ||
      fomds::LatestSequence() != 2) {
    return 1;
  }
  std::puts("StateStoreTests passed");
  return 0;
}
