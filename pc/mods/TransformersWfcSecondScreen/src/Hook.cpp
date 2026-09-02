#include "Hook.h"

#include <windows.h>

#include <atomic>
#include <cstring>
#include <limits>

namespace wfcds {
namespace {

bool Rel32(uint8_t* output, const uint8_t* instruction,
           const void* destination) {
  const intptr_t delta = reinterpret_cast<const uint8_t*>(destination) -
                         (instruction + 5);
  if (delta < std::numeric_limits<int32_t>::min() ||
      delta > std::numeric_limits<int32_t>::max())
    return false;
  output[0] = 0xE9;
  const int32_t relative = static_cast<int32_t>(delta);
  std::memcpy(output + 1, &relative, sizeof(relative));
  return true;
}

}  // namespace

bool InstallInlineHook(InlineHook* hook, void* target, void* replacement,
                       const uint8_t* expected, size_t stolen_bytes,
                       void** original) {
  if (!hook || !target || !replacement || !expected || !original ||
      stolen_bytes < 5 ||
      stolen_bytes > sizeof(hook->original) || hook->target ||
      std::memcmp(target, expected, stolen_bytes) != 0)
    return false;
  auto* trampoline = static_cast<uint8_t*>(VirtualAlloc(
      nullptr, stolen_bytes + 5, MEM_COMMIT | MEM_RESERVE,
      PAGE_EXECUTE_READWRITE));
  if (!trampoline) return false;
  std::memcpy(trampoline, target, stolen_bytes);
  if (!Rel32(trampoline + stolen_bytes, trampoline + stolen_bytes,
             static_cast<uint8_t*>(target) + stolen_bytes)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  FlushInstructionCache(GetCurrentProcess(), trampoline, stolen_bytes + 5);
  DWORD trampoline_previous = 0;
  if (!VirtualProtect(trampoline, stolen_bytes + 5, PAGE_EXECUTE_READ,
                      &trampoline_previous)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  uint8_t patch[16] = {};
  if (!Rel32(patch, static_cast<uint8_t*>(target), replacement)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  for (size_t i = 5; i < stolen_bytes; ++i) patch[i] = 0x90;
  DWORD previous = 0;
  if (!VirtualProtect(target, stolen_bytes, PAGE_EXECUTE_READWRITE,
                      &previous)) {
    VirtualFree(trampoline, 0, MEM_RELEASE);
    return false;
  }
  std::memcpy(hook->original, target, stolen_bytes);
  hook->target = target;
  hook->replacement = replacement;
  hook->trampoline = trampoline;
  hook->stolen_bytes = stolen_bytes;
  *original = trampoline;
  // The game thread can enter the replacement as soon as the first target byte
  // is changed. Publish the complete hook and original call target first.
  std::atomic_thread_fence(std::memory_order_release);
  std::memcpy(target, patch, stolen_bytes);
  FlushInstructionCache(GetCurrentProcess(), target, stolen_bytes);
  DWORD ignored = 0;
  VirtualProtect(target, stolen_bytes, previous, &ignored);
  return true;
}

bool RemoveInlineHook(InlineHook* hook) {
  if (!hook || !hook->target || !hook->trampoline || !hook->stolen_bytes)
    return false;
  DWORD previous = 0;
  if (!VirtualProtect(hook->target, hook->stolen_bytes,
                      PAGE_EXECUTE_READWRITE, &previous))
    return false;
  std::memcpy(hook->target, hook->original, hook->stolen_bytes);
  FlushInstructionCache(GetCurrentProcess(), hook->target,
                        hook->stolen_bytes);
  DWORD ignored = 0;
  VirtualProtect(hook->target, hook->stolen_bytes, previous, &ignored);
  VirtualFree(hook->trampoline, 0, MEM_RELEASE);
  *hook = {};
  return true;
}

}  // namespace wfcds
