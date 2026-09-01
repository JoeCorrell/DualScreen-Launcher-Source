#include <cstdio>
#include <cstring>

#include "BuildIdentity.h"
#include "ExpectedBuild.h"

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const fomds::FileIdentity executable = fomds::IdentifyFile(argv[1]);
  const fomds::FileIdentity sdl = fomds::IdentifyFile(argv[2]);
  if (!executable.present || !sdl.present ||
      executable.size != fomds::kExpectedGameSize ||
      sdl.size != fomds::kExpectedSdlSize ||
      std::strcmp(executable.sha256, fomds::kExpectedGameSha256) != 0 ||
      std::strcmp(sdl.sha256, fomds::kExpectedSdlSha256) != 0) {
    std::fputs("heap-backed SHA-256 identity mismatch\n", stderr);
    return 1;
  }
  const fomds::FileIdentity missing = fomds::IdentifyFile(L"Z:\\missing.bin");
  if (missing.present) return 1;
  std::puts("HashTests passed: exact SHA-256 and missing-file behavior");
  return 0;
}
