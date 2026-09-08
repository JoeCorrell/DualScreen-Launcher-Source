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
  assert(!gate.RequiresInterception(false));
  assert(gate.RequiresInterception(true));
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
  assert(gate.RequiresInterception(false));

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

  assert(gate.MarkNativeGraphVerified(request, false, true));
  assert(gate.RequiresInterception(false));
  assert(gate.BeginReplay(&request));
  assert(!gate.BeginReplay(&request));
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) ==
         CheckpointInterceptResult::kRunDeferredOriginal);

  // A recursive call from the original executor is suppressed.
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false,
                        false) == CheckpointInterceptResult::kSuppressed);
  assert(gate.CompleteReplay(request, true));
  assert(!gate.pending());
  assert(!gate.RequiresInterception(false));

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
  assert(gate.RequiresInterception(false));
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
  assert(!gate.MarkNativeGraphVerified(request, true, true));
  assert(gate.failed_closed());
}

void TestWrongReplayCannotEnterOriginal() {
  CheckpointGate gate;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true,
                        true) == CheckpointInterceptResult::kDeferred);
  DeferredCheckpointRequest request;
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(gate.MarkNativeGraphVerified(request, false, true));
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
  assert(gate.MarkNativeGraphVerified(request, false, true));
  assert(gate.BeginReplay(&request));
  gate.AbortReplay(request);
  assert(gate.failed_closed());
}

void TestStaleRestoreAndCompletionCannotOwnNewSave() {
  CheckpointGate gate;
  for (int cycle = 0; cycle < 2000; ++cycle) {
    DeferredCheckpointRequest old_request, current, token;
    gate.Reset();
    assert(!gate.CaptureReplayToken(&token));
    assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true, true) == CheckpointInterceptResult::kDeferred);
    assert(gate.ConsumeNativeRestoreRequest(&old_request));
    assert(gate.MarkNativeGraphVerified(old_request, false, true));
    assert(gate.BeginReplay(&old_request));
    assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false, false) == CheckpointInterceptResult::kRunDeferredOriginal);
    assert(gate.CaptureReplayToken(&token));
    assert(token.generation == old_request.generation);

    // Simulate reset/reentry during the original call, with identical UObject
    // and checkpoint identities. Only the generation distinguishes the work.
    gate.Reset();
    assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true, true) == CheckpointInterceptResult::kDeferred);
    assert(gate.ConsumeNativeRestoreRequest(&current));
    assert(current.generation != old_request.generation);
    assert(!gate.MarkNativeGraphVerified(old_request, false, true));
    assert(gate.state() == CheckpointGateState::kNativeRestoreRequested);
    assert(gate.MarkNativeGraphVerified(current, false, true));
    assert(gate.BeginReplay(&current));
    assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false, false) == CheckpointInterceptResult::kRunDeferredOriginal);
    assert(!gate.CompleteReplay(token, true));
    assert(!gate.CompleteReplay(token, false));
    gate.AbortReplay(token);
    assert(!gate.ReplayCompleted(token));
    assert(gate.state() == CheckpointGateState::kReplayEntered);
    for (int field = 0; field < 4; ++field) {
      auto wrong = current;
      if (field == 0) ++wrong.manager;
      if (field == 1) ++wrong.manager_object_index;
      if (field == 2) ++wrong.checkpoint_index;
      if (field == 3) wrong.generation = 0;
      assert(!gate.CompleteReplay(wrong, true));
      assert(gate.state() == CheckpointGateState::kReplayEntered);
    }
    assert(gate.CompleteReplay(current, true));
    assert(gate.ReplayCompleted(current));
    assert(!gate.ReplayCompleted(old_request));
    for (int field = 0; field < 4; ++field) {
      auto wrong = current;
      if (field == 0) ++wrong.manager;
      if (field == 1) ++wrong.manager_object_index;
      if (field == 2) ++wrong.checkpoint_index;
      if (field == 3) wrong.generation = 0;
      assert(!gate.ReplayCompleted(wrong));
    }
    assert(!gate.CompleteReplay(current, true));
    assert(gate.state() == CheckpointGateState::kIdle);
    gate.Reset();
    assert(!gate.ReplayCompleted(current));
  }
}

void TestOwnedCallFailureRemainsClosed() {
  CheckpointGate gate;
  DeferredCheckpointRequest request;
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, true, true) == CheckpointInterceptResult::kDeferred);
  assert(gate.ConsumeNativeRestoreRequest(&request));
  assert(gate.MarkNativeGraphVerified(request, false, true));
  assert(gate.BeginReplay(&request));
  assert(gate.Intercept(kManager, kManagerIndex, kCheckpointIndex, false, false) == CheckpointInterceptResult::kRunDeferredOriginal);
  assert(!gate.CompleteReplay(request, false));
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
  TestStaleRestoreAndCompletionCannotOwnNewSave();
  TestOwnedCallFailureRemainsClosed();
  return 0;
}
