#pragma once

#include <windows.h>

#include <cstdint>

namespace wfcds {

struct BuildIdentity {
  bool supported = false;
  uint16_t machine = 0;
  uint32_t timestamp = 0;
  uint32_t image_size = 0;
  uint64_t file_size = 0;
  char sha256[65] = {};
  char label[96] = {};
};

BuildIdentity IdentifyExecutable(HMODULE image);

}  // namespace wfcds
