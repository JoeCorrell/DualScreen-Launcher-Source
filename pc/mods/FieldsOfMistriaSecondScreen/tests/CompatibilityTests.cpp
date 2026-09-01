#include <windows.h>

#include <cstdio>
#include <cstring>
#include <iterator>

#include "BuildIdentity.h"
#include "ExpectedBuild.h"

int wmain(int argc, wchar_t** argv) {
  if (argc != 3) return 2;
  const fomds::BuildIdentity exact =
      fomds::IdentifyBuildFromPaths(argv[1], argv[2]);
  if (!exact.supported || !exact.valid_pe64 ||
      std::strcmp(exact.executable.sha256, fomds::kExpectedGameSha256) != 0 ||
      std::strcmp(exact.sdl.sha256, fomds::kExpectedSdlSha256) != 0) {
    std::fprintf(stderr, "exact reference rejected: %s\n", exact.reason);
    return 1;
  }
  wchar_t self[32768] = {};
  GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
  const fomds::BuildIdentity unrelated =
      fomds::IdentifyBuildFromPaths(self, argv[2]);
  if (unrelated.supported || !unrelated.reason[0]) {
    std::fprintf(stderr, "unrelated executable did not fail closed\n");
    return 1;
  }
  const fomds::BuildIdentity missing =
      fomds::IdentifyBuildFromPaths(L"Z:\\missing.exe", L"Z:\\missing.dll");
  if (missing.supported) return 1;
  std::puts("CompatibilityTests passed: exact gate and fail-closed negatives");
  return 0;
}
