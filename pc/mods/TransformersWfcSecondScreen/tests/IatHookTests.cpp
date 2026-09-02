#include <windows.h>

#include <cassert>

#include "IatHook.h"

namespace {

int Original(int value) { return value + 1; }
int Replacement(int value) { return value + 10; }

}  // namespace

int main() {
  auto* slot = static_cast<void**>(VirtualAlloc(
      nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(slot);
  *slot = reinterpret_cast<void*>(&Original);

  DWORD previous = 0;
  assert(VirtualProtect(slot, 4096, PAGE_READONLY, &previous));

  void* original = nullptr;
  assert(wfcds::InstallIatHook(slot, reinterpret_cast<void*>(&Original),
                              reinterpret_cast<void*>(&Replacement),
                              &original));
  assert(original == reinterpret_cast<void*>(&Original));
  auto active = reinterpret_cast<int (*)(int)>(*slot);
  assert(active(5) == 15);

  assert(wfcds::RestoreIatHook(slot, reinterpret_cast<void*>(&Replacement),
                              original));
  active = reinterpret_cast<int (*)(int)>(*slot);
  assert(active(5) == 6);

  assert(VirtualFree(slot, 0, MEM_RELEASE));
  return 0;
}
