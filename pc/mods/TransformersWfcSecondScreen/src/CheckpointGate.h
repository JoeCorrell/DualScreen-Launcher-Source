#pragma once

#include <cstdint>

namespace wfcds {

// Deferred disk-write request bookkeeping, NOT a checkpoint-capture barrier.
// HM_Engine.CheckpointManager.SetActiveCheckpoint/OnStartingFromCheckpoint
// call GenerateCheckpointData BEFORE SaveActiveCheckpoint writes the already
// captured CheckpointData. Restoring the native graph at disk-write time alone
// cannot repair that snapshot. Integration must guard the native capture
// boundary as well; this gate must never be used as proof of capture safety.
struct DeferredCheckpointRequest {
  uintptr_t manager = 0;
  uint32_t manager_object_index = 0;
  int32_t checkpoint_index = 0;
  uint64_t generation = 0;
};

enum class CheckpointInterceptResult : uint8_t {
  kRunOriginal,
  kDeferred,
  kSuppressed,
  kRunDeferredOriginal,
  kFailedClosed,
};

enum class CheckpointGateState : uint8_t {
  kIdle,
  kAwaitingNativeRestore,
  kNativeRestoreRequested,
  kReadyToReplay,
  kReplayArmed,
  kReplayEntered,
  kFailedClosed,
};

class CheckpointGate {
 public:
  CheckpointInterceptResult Intercept(uintptr_t manager,
                                      uint32_t manager_object_index,
                                      int32_t checkpoint_index,
                                      bool role_mapping_active,
                                      bool role_mapping_valid) noexcept;

  // Returns the retained request once. The caller must serialize a verified
  // reverse swap back to the native TnPlayerController/TnPlayerPawn graph.
  bool ConsumeNativeRestoreRequest(
      DeferredCheckpointRequest* output) noexcept;

  // Called only after the reverse transaction has committed and both
  // reciprocal native Controller/Pawn links have been re-read successfully.
  bool MarkNativeGraphVerified(const DeferredCheckpointRequest& request,
                               bool role_mapping_active,
                               bool native_player_graph_valid) noexcept;

  // Arms replay of the retained request. The subsequent reflected call must
  // re-enter Intercept with the exact manager, UObject index, and checkpoint
  // index before the original executor is allowed to run.
  bool BeginReplay(DeferredCheckpointRequest* output) noexcept;
  // Capture before entering the original call; never fetch a replacement
  // request after a callback/reset. Tokens remain unique across Reset().
  bool CaptureReplayToken(DeferredCheckpointRequest* output) const noexcept;
  bool CompleteReplay(const DeferredCheckpointRequest& request,
                      bool original_returned) noexcept;
  bool ReplayCompleted(const DeferredCheckpointRequest& request) const noexcept;
  void AbortReplay(const DeferredCheckpointRequest& request) noexcept;

  void FailClosed() noexcept;
  void Reset() noexcept;

  CheckpointGateState state() const noexcept { return state_; }
  bool RequiresInterception(bool role_mapping_active) const noexcept {
    return role_mapping_active || state_ != CheckpointGateState::kIdle;
  }
  bool failed_closed() const noexcept {
    return state_ == CheckpointGateState::kFailedClosed;
  }
  bool pending() const noexcept {
    return state_ != CheckpointGateState::kIdle && !failed_closed();
  }

 private:
  bool RequestMatches(uintptr_t manager, uint32_t manager_object_index,
                      int32_t checkpoint_index) const noexcept;
  bool TokenMatches(const DeferredCheckpointRequest& request) const noexcept;

  CheckpointGateState state_ = CheckpointGateState::kIdle;
  DeferredCheckpointRequest request_ = {};
  uint64_t next_generation_ = 0;
  DeferredCheckpointRequest completed_ = {};
};

}  // namespace wfcds
