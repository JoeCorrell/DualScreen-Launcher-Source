#include "CharacterSwap.h"

#include <limits>

namespace wfcds {
namespace {

constexpr CharacterSwapMutation kQuiesceMutations[] = {
    CharacterSwapMutation::kQuiescePlayerInput,
    CharacterSwapMutation::kQuiesceTargetAi,
};

constexpr CharacterSwapMutation kCommitMutations[] = {
    CharacterSwapMutation::kDetachOriginalControlLinks,
    CharacterSwapMutation::kBindPlayerControllerToTarget,
    CharacterSwapMutation::kBindAiControllerToFormerPlayer,
    CharacterSwapMutation::kUpdateSquadMembership,
    CharacterSwapMutation::kUpdateCharacterIdentity,
    CharacterSwapMutation::kRebindRuntimeServices,
    CharacterSwapMutation::kUpdateCameraAndViewTarget,
    CharacterSwapMutation::kArmControlTransfer,
};

bool ElapsedAtLeast(uint64_t now, uint64_t then, uint64_t duration) noexcept {
  return now >= then && now - then >= duration;
}

}  // namespace

CharacterSwapTransaction::CharacterSwapTransaction(
    CharacterSwapOperations* operations, CharacterSwapConfig config) noexcept
    : operations_(operations), config_(config) {
  if (config_.maximum_request_age_millis == 0) {
    config_.maximum_request_age_millis = 1;
  }
  if (config_.maximum_transaction_millis == 0) {
    config_.maximum_transaction_millis = 1;
  }
}

bool CharacterSwapTransaction::in_progress() const noexcept {
  return status_.phase != CharacterSwapPhase::kIdle &&
         status_.phase != CharacterSwapPhase::kDone &&
         status_.phase != CharacterSwapPhase::kFailedClosed;
}

CharacterSwapSubmitResult CharacterSwapTransaction::Submit(
    const CharacterSwapRequest& request, uint64_t now_millis) noexcept {
  if (status_.phase != CharacterSwapPhase::kIdle) {
    return CharacterSwapSubmitResult::kBusy;
  }
  if (!IsRequestShapeValid(request) || operations_ == nullptr) {
    return CharacterSwapSubmitResult::kInvalid;
  }
  if (request.requested_at_millis > now_millis) {
    return CharacterSwapSubmitResult::kClockRegression;
  }
  if (now_millis - request.requested_at_millis >
      config_.maximum_request_age_millis) {
    return CharacterSwapSubmitResult::kExpired;
  }
  if (has_last_accepted_time_ && !request.checkpoint_native_restore) {
    if (now_millis < last_accepted_at_millis_) {
      return CharacterSwapSubmitResult::kClockRegression;
    }
    if (!ElapsedAtLeast(now_millis, last_accepted_at_millis_,
                        config_.debounce_millis)) {
      return CharacterSwapSubmitResult::kDebounced;
    }
  }

  request_ = request;
  capture_ = {};
  applied_count_ = 0;
  has_capture_ = false;
  transaction_started_at_millis_ = now_millis;
  last_accepted_at_millis_ = now_millis;
  has_last_accepted_time_ = true;
  status_ = {};
  status_.phase = CharacterSwapPhase::kPreflightStable;
  status_.request_id = request.request_id;
  return CharacterSwapSubmitResult::kAccepted;
}

CharacterSwapStatus CharacterSwapTransaction::Tick(
    uint64_t now_millis) noexcept {
  if (!in_progress()) {
    return status_;
  }
  if (now_millis < transaction_started_at_millis_) {
    FailAndRollback(CharacterSwapFailure::kClockRegression,
                    CharacterSwapMutation::kCount);
    return status_;
  }
  if (now_millis - transaction_started_at_millis_ >
      config_.maximum_transaction_millis) {
    FailAndRollback(CharacterSwapFailure::kTransactionTimedOut,
                    CharacterSwapMutation::kCount);
    return status_;
  }

  switch (status_.phase) {
    case CharacterSwapPhase::kPreflightStable: {
      CharacterSwapCapture sample = {};
      const CharacterSwapPreflight result =
          operations_->CapturePreflight(request_, &sample);
      if (result == CharacterSwapPreflight::kReject) {
        if (IsInternallyValid(sample) && CaptureMatchesRequest(sample)) {
          capture_ = sample;
          has_capture_ = true;
        }
        FailAndRollback(CharacterSwapFailure::kPreflightRejected,
                        CharacterSwapMutation::kCount);
        break;
      }
      if (result == CharacterSwapPreflight::kWait) {
        status_.stable_preflight_frames = 0;
        has_capture_ = false;
        capture_ = {};
        break;
      }
      if (!IsInternallyValid(sample) || !CaptureMatchesRequest(sample)) {
        FailAndRollback(CharacterSwapFailure::kPreflightCaptureInvalid,
                        CharacterSwapMutation::kCount);
        break;
      }

      if (!has_capture_ || !StableCaptureEqual(capture_, sample)) {
        capture_ = sample;
        has_capture_ = true;
        status_.stable_preflight_frames = 1;
        break;
      }

      capture_ = sample;
      if (status_.stable_preflight_frames <
          std::numeric_limits<uint8_t>::max()) {
        ++status_.stable_preflight_frames;
      }
      if (status_.stable_preflight_frames >=
          kRequiredStablePreflightFrames) {
        status_.phase = CharacterSwapPhase::kQuiesce;
      }
      break;
    }

    case CharacterSwapPhase::kQuiesce:
      for (CharacterSwapMutation mutation : kQuiesceMutations) {
        if (!ApplyOne(mutation)) {
          return status_;
        }
      }
      status_.phase = CharacterSwapPhase::kQuiesceVerify;
      break;

    case CharacterSwapPhase::kQuiesceVerify: {
      const CharacterSwapPreflight result =
          operations_->VerifyQuiesced(request_, capture_);
      if (result == CharacterSwapPreflight::kReject) {
        FailAndRollback(CharacterSwapFailure::kQuiesceVerificationFailed,
                        CharacterSwapMutation::kCount);
      } else if (result == CharacterSwapPreflight::kReady) {
        status_.phase = CharacterSwapPhase::kPrepared;
      }
      break;
    }

    case CharacterSwapPhase::kPrepared:
      if (!ApplyOne(CharacterSwapMutation::kPrepareRoleAdapters)) {
        return status_;
      }
      status_.phase = CharacterSwapPhase::kAtomicCommit;
      break;

    case CharacterSwapPhase::kAtomicCommit:
      // All graph mutations intentionally happen in this one game-thread tick.
      for (CharacterSwapMutation mutation : kCommitMutations) {
        if (!ApplyOne(mutation)) {
          return status_;
        }
      }
      status_.successful_settle_ticks = 0;
      status_.phase = CharacterSwapPhase::kSettleVerify;
      break;

    case CharacterSwapPhase::kSettleVerify:
      if (!operations_->VerifyCommittedGraph(request_, capture_)) {
        FailAndRollback(CharacterSwapFailure::kSettleVerificationFailed,
                        CharacterSwapMutation::kCount);
        break;
      }
      if (status_.successful_settle_ticks <
          std::numeric_limits<uint8_t>::max()) {
        ++status_.successful_settle_ticks;
      }
      if (status_.successful_settle_ticks >=
          kRequiredSettleVerificationTicks) {
        if (!ApplyOne(CharacterSwapMutation::kReleaseTransitionGuards)) {
          return status_;
        }
        // Releasing transition guards must not invalidate the graph.
        if (!operations_->VerifyCommittedGraph(request_, capture_)) {
          FailAndRollback(CharacterSwapFailure::kSettleVerificationFailed,
                          CharacterSwapMutation::kCount);
          break;
        }
        status_.phase = CharacterSwapPhase::kDone;
        operations_->OnSucceeded(request_, capture_);
      }
      break;

    case CharacterSwapPhase::kIdle:
    case CharacterSwapPhase::kDone:
    case CharacterSwapPhase::kRollback:
    case CharacterSwapPhase::kFailedClosed:
      break;
  }
  return status_;
}

bool CharacterSwapTransaction::ApplyOne(
    CharacterSwapMutation mutation) noexcept {
  if (applied_count_ >= kMutationCapacity) {
    FailAndRollback(CharacterSwapFailure::kMutationFailed, mutation);
    return false;
  }

  const CharacterSwapApplyResult result =
      operations_->ApplyMutation(mutation, request_, &capture_);
  if (result == CharacterSwapApplyResult::kApplied ||
      result == CharacterSwapApplyResult::kFailedNeedsRollback) {
    applied_[applied_count_++] = mutation;
    status_.applied_mutation_count = static_cast<uint8_t>(applied_count_);
  }
  if (result != CharacterSwapApplyResult::kApplied) {
    FailAndRollback(CharacterSwapFailure::kMutationFailed, mutation);
    return false;
  }
  return true;
}

void CharacterSwapTransaction::FailAndRollback(
    CharacterSwapFailure cause,
    CharacterSwapMutation failed_mutation) noexcept {
  status_.phase = CharacterSwapPhase::kRollback;
  status_.failure = cause;
  status_.failed_mutation = failed_mutation;
  status_.rollback_complete = true;
  status_.rollback_failure_count = 0;

  while (applied_count_ != 0) {
    const CharacterSwapMutation mutation = applied_[--applied_count_];
    if (!operations_->RevertMutation(mutation, request_, &capture_)) {
      status_.rollback_complete = false;
      if (status_.rollback_failure_count <
          std::numeric_limits<uint8_t>::max()) {
        ++status_.rollback_failure_count;
      }
    }
  }
  status_.applied_mutation_count = 0;
  if (!status_.rollback_complete) {
    status_.failure = CharacterSwapFailure::kRollbackFailed;
  }
  status_.phase = CharacterSwapPhase::kFailedClosed;
  operations_->EnterFailedClosed(request_, capture_, cause,
                                 status_.rollback_complete);
}

bool CharacterSwapTransaction::AcknowledgeDone() noexcept {
  if (status_.phase != CharacterSwapPhase::kDone) {
    return false;
  }
  ClearCurrent();
  return true;
}

bool CharacterSwapTransaction::RecoverFailedClosed() noexcept {
  if (status_.phase != CharacterSwapPhase::kFailedClosed ||
      !status_.rollback_complete || operations_ == nullptr) {
    return false;
  }
  if (has_capture_ &&
      !operations_->VerifyRestoredGraph(request_, capture_)) {
    status_.failure = CharacterSwapFailure::kRecoveryVerificationFailed;
    return false;
  }
  if (!operations_->ExitFailedClosed(request_, capture_)) {
    status_.failure = CharacterSwapFailure::kRecoveryVerificationFailed;
    return false;
  }
  ClearCurrent();
  return true;
}

bool CharacterSwapTransaction::CaptureMatchesRequest(
    const CharacterSwapCapture& capture) const noexcept {
  return capture.world_generation == request_.world_generation &&
         capture.world.object_index == request_.expected_world_object_index &&
         capture.player_controller.object_index ==
             request_.expected_player_controller_object_index &&
         capture.player_pawn.object_index ==
             request_.expected_player_pawn_object_index &&
         capture.target_ai_controller.object_index ==
             request_.expected_target_controller_object_index &&
         capture.target_ai_pawn.object_index ==
             request_.expected_target_pawn_object_index &&
         capture.player_identity_hash ==
             request_.expected_player_identity_hash &&
         capture.target_identity_hash ==
             request_.expected_target_identity_hash &&
         capture.squad_slot == request_.squad_slot;
}

bool CharacterSwapTransaction::IsInternallyValid(
    const CharacterSwapCapture& capture) noexcept {
  if (capture.world_generation == 0 || !IsValidObject(capture.world) ||
      !IsValidObject(capture.player_controller) ||
      !IsValidObject(capture.player_pawn) ||
      !IsValidObject(capture.target_ai_controller) ||
      !IsValidObject(capture.target_ai_pawn) ||
      capture.player_identity_hash == 0 || capture.target_identity_hash == 0) {
    return false;
  }
  if (SameObject(capture.player_controller, capture.target_ai_controller) ||
      SameObject(capture.player_pawn, capture.target_ai_pawn)) {
    return false;
  }
  return SameObject(capture.player_controller_pawn_link,
                    capture.player_pawn) &&
         SameObject(capture.player_pawn_controller_link,
                    capture.player_controller) &&
         SameObject(capture.target_controller_pawn_link,
                    capture.target_ai_pawn) &&
         SameObject(capture.target_pawn_controller_link,
                    capture.target_ai_controller);
}

bool CharacterSwapTransaction::StableCaptureEqual(
    const CharacterSwapCapture& left,
    const CharacterSwapCapture& right) noexcept {
  return left.world_generation == right.world_generation &&
         SameObject(left.world, right.world) &&
         SameObject(left.player_controller, right.player_controller) &&
         SameObject(left.player_pawn, right.player_pawn) &&
         SameObject(left.target_ai_controller,
                    right.target_ai_controller) &&
         SameObject(left.target_ai_pawn, right.target_ai_pawn) &&
         SameObject(left.player_controller_pawn_link,
                    right.player_controller_pawn_link) &&
         SameObject(left.player_pawn_controller_link,
                    right.player_pawn_controller_link) &&
         SameObject(left.target_controller_pawn_link,
                    right.target_controller_pawn_link) &&
         SameObject(left.target_pawn_controller_link,
                    right.target_pawn_controller_link) &&
         left.player_identity_hash == right.player_identity_hash &&
         left.target_identity_hash == right.target_identity_hash &&
         left.squad_slot == right.squad_slot &&
         left.squad_record_generation == right.squad_record_generation &&
         left.safety_generation == right.safety_generation;
}

bool CharacterSwapTransaction::SameObject(
    const CharacterSwapObjectIdentity& left,
    const CharacterSwapObjectIdentity& right) noexcept {
  return left.address == right.address &&
         left.object_index == right.object_index &&
         left.object_serial == right.object_serial;
}

bool CharacterSwapTransaction::IsValidObject(
    const CharacterSwapObjectIdentity& object) noexcept {
  return object.address != 0 && object.object_index != 0;
}

bool CharacterSwapTransaction::IsRequestShapeValid(
    const CharacterSwapRequest& request) noexcept {
  return request.request_id != 0 && request.world_generation != 0 &&
         request.expected_world_object_index != 0 &&
         request.expected_player_controller_object_index != 0 &&
         request.expected_player_pawn_object_index != 0 &&
         request.expected_target_controller_object_index != 0 &&
         request.expected_target_pawn_object_index != 0 &&
         request.expected_player_identity_hash != 0 &&
         request.expected_target_identity_hash != 0 &&
         request.squad_slot < 3 &&
         request.expected_player_controller_object_index !=
             request.expected_target_controller_object_index &&
         request.expected_player_pawn_object_index !=
             request.expected_target_pawn_object_index;
}

void CharacterSwapTransaction::ClearCurrent() noexcept {
  status_ = {};
  request_ = {};
  capture_ = {};
  applied_count_ = 0;
  has_capture_ = false;
  transaction_started_at_millis_ = 0;
}

}  // namespace wfcds
