#include <cassert>
#include <cstdint>

#include "CheckpointGate.h"

namespace {

using wfcds::CheckpointGate;
using wfcds::CheckpointGateState;
using wfcds::CheckpointInterceptResult;
using wfcds::DeferredCheckpointRequest;

constexpr uintptr_t kManager = 0x12340000u;
constexpr uint32_t kManagerIndex = 77u;
constexpr int32_t kCheckpointIndex = 4;

void TestNativeSavePassesThrough() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) ==
         CheckpointInterceptResult::kRunOriginal);
  assert(gate.state() == CheckpointGateState::kIdle);
}

void TestMappedSaveRestoresAndReplaysExactlyOnce() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        true) == CheckpointInterceptResult::kDeferred);
  assert(gate.pending());

  // Further native requests cannot bypass the retained save while the graph
  // is being restored, even if the mapping has already become empty.
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) == CheckpointInterceptResult::kSuppressed);

  DeferredCheckpointRequest request;
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(request.manager == kManager);
  assert(request.manager_object_index == kManagerIndex);
  assert(request.checkpoint_index == kCheckpointIndex);
  assert(request.generation != 0);
  assert(!gate.ConsumeNativeRestoreRequest(&request));

  assert(gate.MarkNativeGraphVerified(false, true));
  assert(gate.BeginReplay(&request));
  assert(!gate.BeginReplay(&request));
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) ==
         CheckpointInterceptResult::kRunDeferredOriginal);

  // A recursive call from the original executor is suppressed.
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) == CheckpointInterceptResult::kSuppressed);
  assert(gate.CompleteReplay(kManager, kManagerIndex, true));
  assert(!gate.pending());

  // The retained request cannot be replayed a second time.
  assert(!gate.BeginReplay(&request));
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) ==
         CheckpointInterceptResult::kRunOriginal);
}

void TestInvalidMappingFailsClosed() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        false) == CheckpointInterceptResult::kFailedClosed);
  assert(gate.failed_closed());
  DeferredCheckpointRequest request;
  assert(!gate.ConsumeNativeRestoreRequest(&request));
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) == CheckpointInterceptResult::kFailedClosed);
}

void TestUnverifiedNativeGraphFailsClosed() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        true) == CheckpointInterceptResult::kDeferred);
  DeferredCheckpointRequest request;
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(!gate.MarkNativeGraphVerified(true, true));
  assert(gate.failed_closed());
}

void TestWrongReplayCannotEnterOriginal() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        true) == CheckpointInterceptResult::kDeferred);
  DeferredCheckpointRequest request;
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(gate.MarkNativeGraphVerified(false, true));
  assert(gate.BeginReplay(&request));
  assert(gate.Intercept(kManager, kManagerIndex + 1, kCheckpointIndex, false,
                        false) == CheckpointInterceptResult::kFailedClosed);
  assert(gate.failed_closed());
}

void TestReplayInvocationFailureFailsClosed() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        true) == CheckpointInterceptResult::kDeferred);
  DeferredCheckpointRequest request;
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(gate.MarkNativeGraphVerified(false, true));
  assert(gate.BeginReplay(&request));
  gate.AbortReplay();
  assert(gate.failed_closed());
}

}  // namespace

int main() {
  TestNativeSavePassesThrough();
  TestMappedSaveRestoresAndReplaysExactlyOnce();
  TestInvalidMappingFailsClosed();
  TestUnverifiedNativeGraphFailsClosed();
  TestWrongReplayCannotEnterOriginal();
  TestReplayInvocationFailureFailsClosed();
  return 0;
}
