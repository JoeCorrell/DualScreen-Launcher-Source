#include "ReflectedCallPolicy.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

using namespace wfcds;

int main() {
  // Exact cooked flags from TransGame.xxx and Engine.xxx, not mocked flags
  // chosen to make the policy pass.
  assert(ClassifyReflectedCall(0x02020000, 0, false) == ReflectedCallKind::kEmpty); // IsFiring, CancelMove
  assert(ClassifyReflectedCall(0x04020000, 0, false) == ReflectedCallKind::kEmpty); // Stopped.StopFire
  assert(ClassifyReflectedCall(0x04020002, 0, false) == ReflectedCallKind::kScript); // Firing.IsFiring
  assert(ClassifyReflectedCall(0x02020400, 0, true) == ReflectedCallKind::kNative); // SetViewTarget
  assert(ClassifyReflectedCall(0x02020400, 0, false) == ReflectedCallKind::kRejected);
  assert(ClassifyReflectedCall(0x02020400, 1, true) == ReflectedCallKind::kRejected);
  for (uint32_t prohibited : {0x4u, 0x8u, 0x08000000u, 0x10000000u}) {
    assert(ClassifyReflectedCall(2u | prohibited, 0, false) == ReflectedCallKind::kRejected);
    assert(ClassifyReflectedCall(0x400u | prohibited, 0, true) == ReflectedCallKind::kRejected);
  }
  assert(IntDelegateCallLayoutValid(2, 20, 0xFFFF, 0, 4));
  assert(IntDelegateCallLayoutValid(2, 20, 0xFFFF, 16, 0));
  assert(!IntDelegateCallLayoutValid(2, 12, 0xFFFF, 0, 4)); // old 8-byte UE3 delegate
  assert(!IntDelegateCallLayoutValid(2, 20, 0xFFFF, 8, 0)); // overlap in native cache
  assert(!IntDelegateCallLayoutValid(1, 4, 0xFFFF, 0, 4)); // old wrong call
  assert(!IntDelegateCallLayoutValid(2, 12, 0xFFFF, 0, 0)); // overlap
  assert(!IntDelegateCallLayoutValid(2, 12, 0xFFFF, 0, 8)); // truncated callback
  assert(!IntDelegateCallLayoutValid(2, 12, 4, 0, 4)); // not void
  assert(!IntDelegateCallLayoutValid(2, 12, 0xFFFF, UINT32_MAX, 4));
  assert(!IntDelegateCallLayoutValid(2, 12, 0xFFFF, 0, UINT32_MAX));
  std::puts("ReflectedCallPolicyTests passed");
}
