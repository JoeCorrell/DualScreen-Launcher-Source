#pragma once

#include <windows.h>

#include <cstdint>

namespace fomds {

struct FileIdentity {
  bool present = false;
  std::uint64_t size = 0;
  char sha256[65] = {};
  char version[32] = {};
};

struct BuildIdentity {
  bool valid_pe64 = false;
  bool supported = false;
  FileIdentity executable;
  FileIdentity sdl;
  char reason[256] = {};
};

BuildIdentity IdentifyInstalledBuild(HMODULE proxy_module);
BuildIdentity IdentifyBuildFromPaths(const wchar_t* executable_path,
                                     const wchar_t* sdl_path);
FileIdentity IdentifyFile(const wchar_t* path);

}  // namespace fomds
