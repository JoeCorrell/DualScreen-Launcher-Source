#pragma once

#include <cstdint>

namespace wfcds {

// SaveActiveCheckpoint serializes PC.Pawn as a TnPlayerPawn. While the role
// adapter has transferred the player controller to a TnAiPawn, allowing that
// function to run produces a checkpoint with missing player state. This gate
// captures one save request, asks the game-thread owner to restore the native
// controller graph, and permits that exact request to enter the original
// executor once after the graph has been verified.
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
  bool MarkNativeGraphVerified(bool role_mapping_active,
                               bool native_player_graph_valid) noexcept;

  // Arms replay of the retained request. The subsequent reflected call must
  // re-enter Intercept with the exact manager, UObject index, and checkpoint
  // index before the original executor is allowed to run.
  bool BeginReplay(DeferredCheckpointRequest* output) noexcept;
  bool CompleteReplay(uintptr_t manager, uint32_t manager_object_index,
                      bool original_returned) noexcept;
  void AbortReplay() noexcept;

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

  CheckpointGateState state_ = CheckpointGateState::kIdle;
  DeferredCheckpointRequest request_ = {};
  uint64_t next_generation_ = 0;
};

}  // namespace wfcds
