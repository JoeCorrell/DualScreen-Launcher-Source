#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace wfcds {

struct InlineHook {
  void* target = nullptr;
  void* replacement = nullptr;
  void* trampoline = nullptr;
  size_t stolen_bytes = 0;
  uint8_t original[16] = {};
};

bool InstallInlineHook(InlineHook* hook, void* target, void* replacement,
                       const uint8_t* expected, size_t stolen_bytes,
                       void** original,
                       std::initializer_list<size_t> relative_calls = {});
bool RemoveInlineHook(InlineHook* hook);

}  // namespace wfcds
