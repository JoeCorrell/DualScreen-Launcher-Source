#include <windows.h>

#include <cstdio>

#include "IatHook.h"

namespace {

DWORD WINAPI ReplacementTickCount() { return 42; }

}  // namespace

int main() {
  volatile DWORD force_import = GetTickCount();
  (void)force_import;
  HMODULE executable = GetModuleHandleW(nullptr);
  HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
  FARPROC expected = kernel ? GetProcAddress(kernel, "GetTickCount") : nullptr;
  void** slot = gdds::FindIatSlot(executable, "KERNEL32.dll", "GetTickCount");
  if (!slot || !expected || *slot != reinterpret_cast<void*>(expected)) {
    std::fprintf(stderr, "FAIL: exact named IAT slot was not resolved\n");
    return 1;
  }
  if (gdds::FindIatSlot(executable, "KERNEL32.dll", "DefinitelyMissing")) {
    std::fprintf(stderr, "FAIL: missing import produced a slot\n");
    return 1;
  }
  void* local_slot = reinterpret_cast<void*>(expected);
  void* original = nullptr;
  if (!gdds::InstallIatHook(&local_slot, reinterpret_cast<void*>(expected),
                            reinterpret_cast<void*>(&ReplacementTickCount),
                            &original) ||
      original != reinterpret_cast<void*>(expected) ||
      local_slot != reinterpret_cast<void*>(&ReplacementTickCount)) {
    std::fprintf(stderr, "FAIL: compare-exchange IAT store did not preserve identity\n");
    return 1;
  }
  if (!gdds::RestoreIatHook(
          &local_slot, reinterpret_cast<void*>(&ReplacementTickCount),
          reinterpret_cast<void*>(expected)) ||
      local_slot != reinterpret_cast<void*>(expected)) {
    std::fprintf(stderr, "FAIL: transactional IAT restore failed\n");
    return 1;
  }
  if (gdds::RestoreIatHook(
          &local_slot, reinterpret_cast<void*>(&ReplacementTickCount),
          reinterpret_cast<void*>(expected))) {
    std::fprintf(stderr, "FAIL: restore accepted a mismatched replacement\n");
    return 1;
  }
  std::puts("IatHookTests passed");
  return 0;
}
