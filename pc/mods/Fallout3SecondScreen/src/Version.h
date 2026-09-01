#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

namespace fo3ds {

struct ModuleRange {
  uintptr_t begin = 0;
  uintptr_t end = 0;

  bool Contains(uintptr_t address, size_t bytes = 1) const;
};

struct BuildIdentity {
  bool valid_pe = false;
  bool is_i386 = false;
  bool known_candidate = false;
  bool signatures_verified = false;
  uint32_t timestamp = 0;
  uint32_t image_size = 0;
  uint32_t text_rva = 0;
  uint32_t text_size = 0;
  uint64_t memory_text_hash = 0;
  uint64_t file_size = 0;
  char file_sha256[65] = {};
  ModuleRange image;
  ModuleRange text;
  ModuleRange rdata;
  ModuleRange data;
  char label[128] = {};
};

BuildIdentity IdentifyExecutable(HMODULE executable);
bool IsReadable(const void* address, size_t bytes);
uintptr_t FindPattern(const ModuleRange& range, const uint8_t* pattern,
                      const char* mask);

}  // namespace fo3ds
