#pragma once

#include <windows.h>

#include <cstdint>

namespace gdds {

struct FileIdentity {
  bool present = false;
  uint64_t size = 0;
  char sha256[65] = {};
};

struct BuildIdentity {
  bool valid_pe = false;
  bool is_i386 = false;
  bool supported = false;
  uint32_t timestamp = 0;
  FileIdentity executable;
  FileIdentity game;
  FileIdentity engine;
  char label[64] = {};
  wchar_t install_directory[MAX_PATH] = {};
};

BuildIdentity IdentifyInstalledBuild();
BuildIdentity IdentifyLoadedBuild(HMODULE executable, HMODULE game,
                                  HMODULE engine);

}  // namespace gdds
