#include "CheckpointGate.h"

namespace wfcds {

bool CheckpointGate::RequestMatches(uintptr_t manager,
                                    uint32_t manager_object_index,
                                    int32_t checkpoint_index) const noexcept {
  return request_.manager == manager && manager != 0 &&
         request_.manager_object_index == manager_object_index &&
         manager_object_index != 0 &&
         request_.checkpoint_index == checkpoint_index;
}

CheckpointInterceptResult CheckpointGate::Intercept(
    uintptr_t manager, uint32_t manager_object_index,
    int32_t checkpoint_index, bool role_mapping_active,
    bool role_mapping_valid) noexcept {
  if (state_ == CheckpointGateState::kFailedClosed)
    return CheckpointInterceptResult::kFailedClosed;

  if (state_ == CheckpointGateState::kReplayArmed) {
    if (!RequestMatches(manager, manager_object_index, checkpoint_index) ||
        role_mapping_active) {
      FailClosed();
      return CheckpointInterceptResult::kFailedClosed;
    }
    state_ = CheckpointGateState::kReplayEntered;
    return CheckpointInterceptResult::kRunDeferredOriginal;
  }

  // The original function must not recursively enter while the retained save
  // is running. Suppress recursion without changing the outer replay state;
  // CompleteReplay remains owned by the outer executor hook.
  if (state_ == CheckpointGateState::kReplayEntered)
    return CheckpointInterceptResult::kSuppressed;

  if (state_ != CheckpointGateState::kIdle)
    return CheckpointInterceptResult::kSuppressed;

  if (!role_mapping_active)
    return CheckpointInterceptResult::kRunOriginal;

  if (!role_mapping_valid || manager == 0 || manager_object_index == 0) {
    FailClosed();
    return CheckpointInterceptResult::kFailedClosed;
  }

  uint64_t generation = ++next_generation_;
  if (generation == 0) generation = ++next_generation_;
  request_.manager = manager;
  request_.manager_object_index = manager_object_index;
  request_.checkpoint_index = checkpoint_index;
  request_.generation = generation;
  state_ = CheckpointGateState::kAwaitingNativeRestore;
  return CheckpointInterceptResult::kDeferred;
}

bool CheckpointGate::ConsumeNativeRestoreRequest(
    DeferredCheckpointRequest* output) noexcept {
  if (!output || state_ != CheckpointGateState::kAwaitingNativeRestore)
    return false;
  *output = request_;
  state_ = CheckpointGateState::kNativeRestoreRequested;
  return true;
}

bool CheckpointGate::MarkNativeGraphVerified(
    bool role_mapping_active, bool native_player_graph_valid) noexcept {
  if (state_ != CheckpointGateState::kNativeRestoreRequested) return false;
  if (role_mapping_active || !native_player_graph_valid) {
    FailClosed();
    return false;
  }
  state_ = CheckpointGateState::kReadyToReplay;
  return true;
}

bool CheckpointGate::BeginReplay(
    DeferredCheckpointRequest* output) noexcept {
  if (!output || state_ != CheckpointGateState::kReadyToReplay) return false;
  *output = request_;
  state_ = CheckpointGateState::kReplayArmed;
  return true;
}

bool CheckpointGate::CompleteReplay(uintptr_t manager,
                                    uint32_t manager_object_index,
                                    bool original_returned) noexcept {
  if (state_ != CheckpointGateState::kReplayEntered ||
      manager != request_.manager ||
      manager_object_index != request_.manager_object_index ||
      !original_returned) {
    FailClosed();
    return false;
  }
  state_ = CheckpointGateState::kIdle;
  request_ = {};
  return true;
}

void CheckpointGate::AbortReplay() noexcept {
  if (state_ == CheckpointGateState::kReplayArmed ||
      state_ == CheckpointGateState::kReplayEntered)
    FailClosed();
}

void CheckpointGate::FailClosed() noexcept {
  state_ = CheckpointGateState::kFailedClosed;
}

void CheckpointGate::Reset() noexcept {
  state_ = CheckpointGateState::kIdle;
  request_ = {};
}

}  // namespace wfcds
