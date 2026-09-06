#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "CharacterSwap.h"

namespace {

using wfcds::CharacterSwapApplyResult;
using wfcds::CharacterSwapCapture;
using wfcds::CharacterSwapFailure;
using wfcds::CharacterSwapMutation;
using wfcds::CharacterSwapObjectIdentity;
using wfcds::CharacterSwapOperations;
using wfcds::CharacterSwapPhase;
using wfcds::CharacterSwapPreflight;
using wfcds::CharacterSwapRequest;
using wfcds::CharacterSwapSubmitResult;
using wfcds::CharacterSwapTransaction;

CharacterSwapObjectIdentity Object(uint32_t index) {
  CharacterSwapObjectIdentity result;
  result.address = static_cast<uintptr_t>(0x100000u + index * 0x100u);
  result.object_index = index;
  result.object_serial = index + 1000u;
  return result;
}

CharacterSwapCapture Capture() {
  CharacterSwapCapture result;
  result.world_generation = 41;
  result.world = Object(10);
  result.player_controller = Object(20);
  result.player_pawn = Object(21);
  result.target_ai_controller = Object(30);
  result.target_ai_pawn = Object(31);
  result.player_controller_pawn_link = result.player_pawn;
  result.player_pawn_controller_link = result.player_controller;
  result.target_controller_pawn_link = result.target_ai_pawn;
  result.target_pawn_controller_link = result.target_ai_controller;
  result.player_identity_hash = 0x11111111u;
  result.target_identity_hash = 0x22222222u;
  result.squad_slot = 1;
  result.squad_record_generation = 7;
  result.safety_generation = 12;
  return result;
}

CharacterSwapRequest Request(uint64_t request_id, uint64_t now) {
  CharacterSwapRequest result;
  result.request_id = request_id;
  result.requested_at_millis = now;
  result.world_generation = 41;
  result.expected_world_object_index = 10;
  result.expected_player_controller_object_index = 20;
  result.expected_player_pawn_object_index = 21;
  result.expected_target_controller_object_index = 30;
  result.expected_target_pawn_object_index = 31;
  result.expected_player_identity_hash = 0x11111111u;
  result.expected_target_identity_hash = 0x22222222u;
  result.squad_slot = 1;
  return result;
}

class MockOperations : public CharacterSwapOperations {
 public:
  CharacterSwapPreflight preflight = CharacterSwapPreflight::kReady;
  CharacterSwapPreflight quiesced = CharacterSwapPreflight::kReady;
  CharacterSwapPreflight rollback_ready = CharacterSwapPreflight::kReady;
  int rollback_prepare_calls = 0;
  bool leave_rejected_capture_empty = false;
  int quiesced_calls = 0;
  CharacterSwapCapture capture = Capture();
  CharacterSwapMutation fail_mutation = CharacterSwapMutation::kCount;
  CharacterSwapMutation rollback_fail_mutation =
      CharacterSwapMutation::kCount;
  bool fail_is_dirty = true;
  bool verify_restored = true;
  bool exit_failed_closed = true;
  int verify_fail_call = -1;
  int capture_calls = 0;
  int verify_calls = 0;
  int failed_closed_calls = 0;
  int success_calls = 0;
  bool failed_closed_rollback_complete = false;
  CharacterSwapFailure failed_closed_cause = CharacterSwapFailure::kNone;
  std::vector<CharacterSwapMutation> apply_trace;
  std::vector<CharacterSwapMutation> revert_trace;

  CharacterSwapPreflight CapturePreflight(
      const CharacterSwapRequest&,
      CharacterSwapCapture* output) noexcept override {
    ++capture_calls;
    if (preflight == CharacterSwapPreflight::kReject &&
        leave_rejected_capture_empty) {
      *output = {};
      return preflight;
    }
    *output = capture;
    return preflight;
  }

  CharacterSwapPreflight VerifyQuiesced(
      const CharacterSwapRequest&,
      const CharacterSwapCapture&) noexcept override {
    ++quiesced_calls;
    return quiesced;
  }

  CharacterSwapApplyResult ApplyMutation(
      CharacterSwapMutation mutation, const CharacterSwapRequest&,
      CharacterSwapCapture*) noexcept override {
    apply_trace.push_back(mutation);
    if (mutation != fail_mutation) {
      return CharacterSwapApplyResult::kApplied;
    }
    return fail_is_dirty
               ? CharacterSwapApplyResult::kFailedNeedsRollback
               : CharacterSwapApplyResult::kFailedNoMutation;
  }

  bool RevertMutation(CharacterSwapMutation mutation,
                      const CharacterSwapRequest&,
                      CharacterSwapCapture*) noexcept override {
    revert_trace.push_back(mutation);
    return mutation != rollback_fail_mutation;
  }

  CharacterSwapPreflight PrepareRollbackGraph(
      const CharacterSwapRequest&, const CharacterSwapCapture&) noexcept override {
    ++rollback_prepare_calls;
    return rollback_ready;
  }

  bool VerifyCommittedGraph(const CharacterSwapRequest&,
                            const CharacterSwapCapture&) noexcept override {
    ++verify_calls;
    return verify_calls != verify_fail_call;
  }

  bool VerifyRestoredGraph(const CharacterSwapRequest&,
                           const CharacterSwapCapture&) noexcept override {
    return verify_restored;
  }

  void EnterFailedClosed(const CharacterSwapRequest&,
                         const CharacterSwapCapture&,
                         CharacterSwapFailure cause,
                         bool rollback_complete) noexcept override {
    ++failed_closed_calls;
    failed_closed_cause = cause;
    failed_closed_rollback_complete = rollback_complete;
  }

  bool ExitFailedClosed(const CharacterSwapRequest&,
                        const CharacterSwapCapture&) noexcept override {
    return exit_failed_closed;
  }

  void OnSucceeded(const CharacterSwapRequest&,
                   const CharacterSwapCapture&) noexcept override {
    ++success_calls;
  }
};

uint64_t TickUntilTerminal(CharacterSwapTransaction* transaction,
                           uint64_t now) {
  for (int i = 0; i < 32; ++i) {
    const auto status = transaction->Tick(++now);
    if (status.phase == CharacterSwapPhase::kDone ||
        status.phase == CharacterSwapPhase::kFailedClosed) {
      return now;
    }
  }
  assert(false && "transaction did not reach a terminal state");
  return now;
}

void AssertReverse(const std::vector<CharacterSwapMutation>& applied,
                   const std::vector<CharacterSwapMutation>& reverted) {
  assert(applied.size() == reverted.size());
  for (size_t i = 0; i < applied.size(); ++i) {
    assert(applied[i] == reverted[applied.size() - i - 1]);
  }
}

void TestSuccessRequiresStableAndSettleFrames() {
  MockOperations operations;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(1, 100), 100) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.command_gate_closed());

  auto status = transaction.Tick(101);
  assert(status.phase == CharacterSwapPhase::kPreflightStable);
  assert(status.stable_preflight_frames == 1);
  status = transaction.Tick(102);
  assert(status.phase == CharacterSwapPhase::kQuiesce);
  assert(status.stable_preflight_frames == 2);
  status = transaction.Tick(103);
  assert(status.phase == CharacterSwapPhase::kQuiesceVerify);
  status = transaction.Tick(104);
  assert(status.phase == CharacterSwapPhase::kPrepared);
  assert(operations.quiesced_calls == 1);
  status = transaction.Tick(105);
  assert(status.phase == CharacterSwapPhase::kAtomicCommit);
  status = transaction.Tick(106);
  assert(status.phase == CharacterSwapPhase::kSettleVerify);

  status = transaction.Tick(107);
  assert(status.phase == CharacterSwapPhase::kSettleVerify);
  assert(status.successful_settle_ticks == 1);
  status = transaction.Tick(108);
  assert(status.phase == CharacterSwapPhase::kSettleVerify);
  assert(status.successful_settle_ticks == 2);
  status = transaction.Tick(109);
  assert(status.phase == CharacterSwapPhase::kDone);
  assert(status.successful_settle_ticks == 3);
  assert(operations.verify_calls == 4);
  assert(operations.success_calls == 1);
  assert(operations.apply_trace.size() ==
         static_cast<size_t>(CharacterSwapMutation::kCount));
  assert(operations.revert_trace.empty());
  assert(transaction.command_gate_closed());
  assert(transaction.AcknowledgeDone());
  assert(transaction.status().phase == CharacterSwapPhase::kIdle);
  assert(!transaction.command_gate_closed());
}

void TestQuiesceMustSettleBeforeAtomicCommit() {
  class DelayedQuiesceOperations final : public MockOperations {
   public:
    CharacterSwapPreflight VerifyQuiesced(
        const CharacterSwapRequest& request,
        const CharacterSwapCapture& capture) noexcept override {
      MockOperations::VerifyQuiesced(request, capture);
      return quiesced_calls < 4 ? CharacterSwapPreflight::kWait
                                : CharacterSwapPreflight::kReady;
    }
  } operations;

  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(19, 1900), 1900) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.Tick(1901).phase ==
         CharacterSwapPhase::kPreflightStable);
  assert(transaction.Tick(1902).phase == CharacterSwapPhase::kQuiesce);
  assert(transaction.Tick(1903).phase ==
         CharacterSwapPhase::kQuiesceVerify);
  for (uint64_t now = 1904; now < 1907; ++now) {
    const auto status = transaction.Tick(now);
    assert(status.phase == CharacterSwapPhase::kQuiesceVerify);
    assert(operations.apply_trace.size() == 2);
  }
  assert(transaction.Tick(1907).phase == CharacterSwapPhase::kPrepared);
  assert(operations.apply_trace.size() == 2);
  assert(transaction.Tick(1908).phase == CharacterSwapPhase::kAtomicCommit);
}

void TestQuiesceRejectionRollsBackGuardsOnly() {
  MockOperations operations;
  operations.quiesced = CharacterSwapPreflight::kReject;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(20, 2000), 2000) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.Tick(2001).phase ==
         CharacterSwapPhase::kPreflightStable);
  assert(transaction.Tick(2002).phase == CharacterSwapPhase::kQuiesce);
  assert(transaction.Tick(2003).phase ==
         CharacterSwapPhase::kQuiesceVerify);
  const auto status = transaction.Tick(2004);
  assert(status.phase == CharacterSwapPhase::kFailedClosed);
  assert(status.failure ==
         CharacterSwapFailure::kQuiesceVerificationFailed);
  assert(operations.apply_trace.size() == 2);
  AssertReverse(operations.apply_trace, operations.revert_trace);
}

void TestChangedPreflightRestartsTwoFrameRequirement() {
  class ChangingCaptureOperations final : public MockOperations {
   public:
    CharacterSwapPreflight CapturePreflight(
        const CharacterSwapRequest& request,
        CharacterSwapCapture* output) noexcept override {
      const CharacterSwapPreflight result =
          MockOperations::CapturePreflight(request, output);
      if (capture_calls >= 2) {
        output->safety_generation = 13;
      }
      return result;
    }
  } operations;

  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(2, 200), 200) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.Tick(201).stable_preflight_frames == 1);
  assert(transaction.Tick(202).stable_preflight_frames == 1);
  const auto status = transaction.Tick(203);
  assert(status.stable_preflight_frames == 2);
  assert(status.phase == CharacterSwapPhase::kQuiesce);
}

void TestEveryMutationFailureRollsBackInReverse() {
  const size_t mutation_count =
      static_cast<size_t>(CharacterSwapMutation::kCount);
  for (size_t failed = 0; failed < mutation_count; ++failed) {
    MockOperations operations;
    operations.fail_mutation = static_cast<CharacterSwapMutation>(failed);
    operations.fail_is_dirty = true;
    CharacterSwapTransaction transaction(&operations);
    const uint64_t start = 1000 + failed * 100;
    assert(transaction.Submit(Request(100 + failed, start), start) ==
           CharacterSwapSubmitResult::kAccepted);
    TickUntilTerminal(&transaction, start);
    const auto status = transaction.status();
    assert(status.phase == CharacterSwapPhase::kFailedClosed);
    assert(status.failure == CharacterSwapFailure::kMutationFailed);
    assert(status.failed_mutation ==
           static_cast<CharacterSwapMutation>(failed));
    assert(status.rollback_complete);
    assert(operations.failed_closed_calls == 1);
    assert(operations.failed_closed_rollback_complete);
    AssertReverse(operations.apply_trace, operations.revert_trace);
    assert(transaction.RecoverFailedClosed());
    assert(transaction.status().phase == CharacterSwapPhase::kIdle);
  }
}

void TestEveryCleanMutationFailureOmitsFailedStepFromRollback() {
  const size_t mutation_count =
      static_cast<size_t>(CharacterSwapMutation::kCount);
  for (size_t failed = 0; failed < mutation_count; ++failed) {
    MockOperations operations;
    operations.fail_mutation = static_cast<CharacterSwapMutation>(failed);
    operations.fail_is_dirty = false;
    CharacterSwapTransaction transaction(&operations);
    const uint64_t start = 3000 + failed * 100;
    assert(transaction.Submit(Request(300 + failed, start), start) ==
           CharacterSwapSubmitResult::kAccepted);
    TickUntilTerminal(&transaction, start);
    assert(operations.apply_trace.back() ==
           static_cast<CharacterSwapMutation>(failed));
    assert(operations.revert_trace.size() + 1 ==
           operations.apply_trace.size());
    std::vector<CharacterSwapMutation> completed = operations.apply_trace;
    completed.pop_back();
    AssertReverse(completed, operations.revert_trace);
  }
}

void TestEverySettleVerificationBoundaryRollsBack() {
  for (int failed_verification = 1; failed_verification <= 4;
       ++failed_verification) {
    MockOperations operations;
    operations.verify_fail_call = failed_verification;
    CharacterSwapTransaction transaction(&operations);
    const uint64_t start = 5000 + failed_verification * 100;
    assert(transaction.Submit(Request(400 + failed_verification, start),
                              start) ==
           CharacterSwapSubmitResult::kAccepted);
    TickUntilTerminal(&transaction, start);
    const auto status = transaction.status();
    assert(status.phase == CharacterSwapPhase::kFailedClosed);
    assert(status.failure ==
           CharacterSwapFailure::kSettleVerificationFailed);
    const size_t expected_applied =
        static_cast<size_t>(CharacterSwapMutation::kCount) -
        (failed_verification == 4 ? 0u : 1u);
    assert(operations.apply_trace.size() == expected_applied);
    AssertReverse(operations.apply_trace, operations.revert_trace);
  }
}

void TestEveryRevertFailureStillAttemptsRemainingRollback() {
  const size_t mutation_count =
      static_cast<size_t>(CharacterSwapMutation::kCount);
  for (size_t failed_revert = 0; failed_revert < mutation_count;
       ++failed_revert) {
    MockOperations operations;
    // The fourth verification runs immediately after transition guards are
    // released, so every mutation is on the rollback stack.
    operations.verify_fail_call = 4;
    operations.rollback_fail_mutation =
        static_cast<CharacterSwapMutation>(failed_revert);
    CharacterSwapTransaction transaction(&operations);
    const uint64_t start = 6000 + failed_revert * 100;
    assert(transaction.Submit(Request(500 + failed_revert, start), start) ==
           CharacterSwapSubmitResult::kAccepted);
    TickUntilTerminal(&transaction, start);
    const auto status = transaction.status();
    assert(status.phase == CharacterSwapPhase::kFailedClosed);
    assert(status.failure == CharacterSwapFailure::kRollbackFailed);
    assert(!status.rollback_complete);
    assert(status.rollback_failure_count == 1);
    assert(operations.revert_trace.size() == mutation_count);
    AssertReverse(operations.apply_trace, operations.revert_trace);
    assert(!transaction.RecoverFailedClosed());
  }
}

void TestRapidRequestsAreSerializedAndDebounced() {
  MockOperations operations;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(5, 500), 500) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.Submit(Request(6, 501), 501) ==
         CharacterSwapSubmitResult::kBusy);
  const uint64_t done_at = TickUntilTerminal(&transaction, 500);
  assert(transaction.Submit(Request(7, done_at), done_at) ==
         CharacterSwapSubmitResult::kBusy);
  assert(transaction.AcknowledgeDone());
  assert(transaction.Submit(Request(8, 600), 600) ==
         CharacterSwapSubmitResult::kDebounced);
  assert(transaction.Submit(Request(9, 1150), 1150) ==
         CharacterSwapSubmitResult::kAccepted);
}

void TestCheckpointRestoreBypassesOnlyDebounce() {
  MockOperations operations;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(60, 500), 500) ==
         CharacterSwapSubmitResult::kAccepted);
  TickUntilTerminal(&transaction, 500);
  assert(transaction.AcknowledgeDone());

  CharacterSwapRequest checkpoint = Request(61, 600);
  checkpoint.checkpoint_native_restore = true;
  assert(transaction.Submit(checkpoint, 600) ==
         CharacterSwapSubmitResult::kAccepted);
  // Internal recovery remains serialized exactly like a UI request.
  CharacterSwapRequest competing = Request(62, 601);
  competing.checkpoint_native_restore = true;
  assert(transaction.Submit(competing, 601) ==
         CharacterSwapSubmitResult::kBusy);
}

void TestRollbackFailureCannotRecover() {
  MockOperations operations;
  operations.fail_mutation = CharacterSwapMutation::kRebindRuntimeServices;
  operations.rollback_fail_mutation =
      CharacterSwapMutation::kBindPlayerControllerToTarget;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(10, 1000), 1000) ==
         CharacterSwapSubmitResult::kAccepted);
  TickUntilTerminal(&transaction, 1000);
  const auto status = transaction.status();
  assert(status.phase == CharacterSwapPhase::kFailedClosed);
  assert(status.failure == CharacterSwapFailure::kRollbackFailed);
  assert(!status.rollback_complete);
  assert(status.rollback_failure_count == 1);
  assert(!transaction.RecoverFailedClosed());
  assert(transaction.Submit(Request(11, 2000), 2000) ==
         CharacterSwapSubmitResult::kBusy);
}

void TestRecoveryRequiresOriginalGraphVerification() {
  MockOperations operations;
  operations.fail_mutation = CharacterSwapMutation::kQuiesceTargetAi;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(12, 1200), 1200) ==
         CharacterSwapSubmitResult::kAccepted);
  TickUntilTerminal(&transaction, 1200);
  operations.verify_restored = false;
  assert(!transaction.RecoverFailedClosed());
  assert(transaction.status().failure ==
         CharacterSwapFailure::kRecoveryVerificationFailed);
  operations.verify_restored = true;
  operations.exit_failed_closed = false;
  assert(!transaction.RecoverFailedClosed());
  operations.exit_failed_closed = true;
  assert(transaction.RecoverFailedClosed());
}

void TestInvalidReciprocalGraphFailsBeforeMutation() {
  MockOperations operations;
  operations.capture.target_pawn_controller_link = Object(99);
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(13, 1300), 1300) ==
         CharacterSwapSubmitResult::kAccepted);
  const auto status = transaction.Tick(1301);
  assert(status.phase == CharacterSwapPhase::kFailedClosed);
  assert(status.failure == CharacterSwapFailure::kPreflightCaptureInvalid);
  assert(operations.apply_trace.empty());
  assert(operations.revert_trace.empty());
  assert(transaction.RecoverFailedClosed());
}

void TestPreflightRejectionWithoutCaptureCanRecover() {
  MockOperations operations;
  operations.preflight = CharacterSwapPreflight::kReject;
  operations.leave_rejected_capture_empty = true;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(21, 2100), 2100) ==
         CharacterSwapSubmitResult::kAccepted);
  const auto status = transaction.Tick(2101);
  assert(status.phase == CharacterSwapPhase::kFailedClosed);
  assert(status.failure == CharacterSwapFailure::kPreflightRejected);
  assert(status.rollback_complete);
  assert(operations.apply_trace.empty());
  assert(operations.revert_trace.empty());
  assert(operations.failed_closed_calls == 1);
  assert(transaction.RecoverFailedClosed());
  assert(transaction.status().phase == CharacterSwapPhase::kIdle);
  assert(!transaction.command_gate_closed());
}

void TestWaitAndTimeoutRemainFailClosed() {
  MockOperations operations;
  operations.preflight = CharacterSwapPreflight::kWait;
  wfcds::CharacterSwapConfig config;
  config.maximum_transaction_millis = 5;
  CharacterSwapTransaction transaction(&operations, config);
  assert(transaction.Submit(Request(14, 1400), 1400) ==
         CharacterSwapSubmitResult::kAccepted);
  assert(transaction.Tick(1401).phase ==
         CharacterSwapPhase::kPreflightStable);
  const auto status = transaction.Tick(1406);
  assert(status.phase == CharacterSwapPhase::kFailedClosed);
  assert(status.failure == CharacterSwapFailure::kTransactionTimedOut);
  assert(transaction.command_gate_closed());
}

void TestRequestValidationAndAge() {
  MockOperations operations;
  CharacterSwapTransaction transaction(&operations);
  CharacterSwapRequest invalid = Request(15, 1500);
  invalid.expected_target_pawn_object_index =
      invalid.expected_player_pawn_object_index;
  assert(transaction.Submit(invalid, 1500) ==
         CharacterSwapSubmitResult::kInvalid);
  invalid = Request(16, 1500);
  invalid.squad_slot = 3;
  assert(transaction.Submit(invalid, 1500) ==
         CharacterSwapSubmitResult::kInvalid);
  assert(transaction.Submit(Request(17, 2000), 1999) ==
         CharacterSwapSubmitResult::kClockRegression);
  assert(transaction.Submit(Request(18, 1000), 2001) ==
         CharacterSwapSubmitResult::kExpired);
}

class RepeatedSwapOperations final : public MockOperations {
 public:
  static constexpr uint32_t kPlayerController = 20;
  static constexpr uint32_t kPawnIndexes[3] = {21, 31, 41};
  static constexpr uint32_t kIdentityHashes[3] = {
      0xA1000001u, 0xB1000002u, 0xC1000003u};

  RepeatedSwapOperations() {
    controller_for_actor_[0] = kPlayerController;
    controller_for_actor_[1] = 30;
    controller_for_actor_[2] = 40;
  }

  CharacterSwapRequest PrepareRequest(uint32_t target_actor,
                                      uint32_t squad_slot,
                                      uint64_t request_id,
                                      uint64_t now) {
    assert(target_actor < 3);
    assert(target_actor != player_actor_);
    assert(controller_for_actor_[target_actor] != kPlayerController);
    requested_target_actor_ = target_actor;

    capture = {};
    capture.world_generation = 41;
    capture.world = Object(10);
    capture.player_controller = Object(kPlayerController);
    capture.player_pawn = Object(kPawnIndexes[player_actor_]);
    capture.target_ai_controller =
        Object(controller_for_actor_[target_actor]);
    capture.target_ai_pawn = Object(kPawnIndexes[target_actor]);
    capture.player_controller_pawn_link = capture.player_pawn;
    capture.player_pawn_controller_link = capture.player_controller;
    capture.target_controller_pawn_link = capture.target_ai_pawn;
    capture.target_pawn_controller_link = capture.target_ai_controller;
    capture.player_identity_hash = kIdentityHashes[player_actor_];
    capture.target_identity_hash = kIdentityHashes[target_actor];
    capture.squad_slot = squad_slot;
    capture.squad_record_generation = graph_generation_;
    capture.safety_generation = graph_generation_;

    CharacterSwapRequest request;
    request.request_id = request_id;
    request.requested_at_millis = now;
    request.world_generation = capture.world_generation;
    request.expected_world_object_index = capture.world.object_index;
    request.expected_player_controller_object_index = kPlayerController;
    request.expected_player_pawn_object_index = kPawnIndexes[player_actor_];
    request.expected_target_controller_object_index =
        controller_for_actor_[target_actor];
    request.expected_target_pawn_object_index = kPawnIndexes[target_actor];
    request.expected_player_identity_hash = kIdentityHashes[player_actor_];
    request.expected_target_identity_hash = kIdentityHashes[target_actor];
    request.squad_slot = squad_slot;
    return request;
  }

  void OnSucceeded(const CharacterSwapRequest& request,
                   const CharacterSwapCapture& completed) noexcept override {
    MockOperations::OnSucceeded(request, completed);
    assert(requested_target_actor_ < 3);
    const uint32_t former_player = player_actor_;
    controller_for_actor_[former_player] =
        controller_for_actor_[requested_target_actor_];
    controller_for_actor_[requested_target_actor_] = kPlayerController;
    player_actor_ = requested_target_actor_;
    requested_target_actor_ = 3;
    ++graph_generation_;
  }

  uint32_t player_actor() const { return player_actor_; }

 private:
  uint32_t controller_for_actor_[3] = {};
  uint32_t player_actor_ = 0;
  uint32_t requested_target_actor_ = 3;
  uint32_t graph_generation_ = 1;
};

constexpr uint32_t RepeatedSwapOperations::kPawnIndexes[3];
constexpr uint32_t RepeatedSwapOperations::kIdentityHashes[3];

void CompleteRepeatedSwap(CharacterSwapTransaction* transaction,
                          RepeatedSwapOperations* operations,
                          uint32_t target_actor, uint32_t squad_slot,
                          uint64_t request_id, uint64_t* now) {
  assert(transaction && operations && now);
  *now += 700;
  const CharacterSwapRequest request = operations->PrepareRequest(
      target_actor, squad_slot, request_id, *now);
  assert(transaction->Submit(request, *now) ==
         CharacterSwapSubmitResult::kAccepted);
  // A second target cannot enter while the first owns the transaction.
  CharacterSwapRequest competing = request;
  ++competing.request_id;
  competing.requested_at_millis = *now + 1;
  assert(transaction->Submit(competing, *now + 1) ==
         CharacterSwapSubmitResult::kBusy);
  *now = TickUntilTerminal(transaction, *now);
  assert(transaction->status().phase == CharacterSwapPhase::kDone);
  assert(transaction->AcknowledgeDone());
  operations->apply_trace.clear();
  operations->revert_trace.clear();
  operations->capture_calls = 0;
  operations->verify_calls = 0;
}

void TestRepeatedAbAAndAbcCycles() {
  RepeatedSwapOperations operations;
  CharacterSwapTransaction transaction(&operations);
  uint64_t now = 10000;
  uint64_t request_id = 1000;

  // A -> B -> A repeatedly exercises transition into and back out of the
  // cross-role mapping while reusing the same live actor/controller objects.
  for (int cycle = 0; cycle < 50; ++cycle) {
    CompleteRepeatedSwap(&transaction, &operations, 1, 1, request_id++, &now);
    assert(operations.player_actor() == 1);
    CompleteRepeatedSwap(&transaction, &operations, 0, 1, request_id++, &now);
    assert(operations.player_actor() == 0);
  }

  // A -> B -> C -> A also exercises a second AI controller taking the former
  // player-controlled TnAiPawn while the original mapped AI pair remains live.
  for (int cycle = 0; cycle < 34; ++cycle) {
    CompleteRepeatedSwap(&transaction, &operations, 1, 1, request_id++, &now);
    CompleteRepeatedSwap(&transaction, &operations, 2, 2, request_id++, &now);
    CompleteRepeatedSwap(&transaction, &operations, 0, 1, request_id++, &now);
    assert(operations.player_actor() == 0);
  }
  assert(operations.success_calls == 202);
}

uint64_t TickUntilRollback(CharacterSwapTransaction* transaction, uint64_t now) {
  for (unsigned n = 0; n < 16; ++n) {
    if (transaction->Tick(++now).phase == CharacterSwapPhase::kRollback) return now;
  }
  assert(false && "expected a pending rollback quiescence barrier");
  return now;
}

void TestRollbackWaitsForReacquiredAiGuard() {
  // Both failure after release and a dirty failure inside release can leave
  // native AI running. Neither may reverse ownership before it stops again.
  for (bool dirty_release : {false, true}) {
    MockOperations operations;
    operations.rollback_ready = CharacterSwapPreflight::kWait;
    if (dirty_release) operations.fail_mutation = CharacterSwapMutation::kReleaseTransitionGuards;
    else operations.verify_fail_call = 4;
    CharacterSwapTransaction transaction(&operations);
    assert(transaction.Submit(Request(8000, 100), 100) == CharacterSwapSubmitResult::kAccepted);
    auto now = TickUntilRollback(&transaction, 100);
    assert(operations.revert_trace.size() == 1);
    assert(operations.revert_trace[0] == CharacterSwapMutation::kReleaseTransitionGuards);
    const auto remaining = transaction.status().applied_mutation_count;
    assert(remaining != 0);
    for (unsigned n = 0; n < 50; ++n) {
      assert(transaction.Tick(++now).phase == CharacterSwapPhase::kRollback);
      assert(transaction.status().applied_mutation_count == remaining);
      assert(!transaction.status().rollback_complete);
      assert(transaction.command_gate_closed() && transaction.in_progress());
      assert(transaction.Submit(Request(8001 + n, now), now) == CharacterSwapSubmitResult::kBusy);
      assert(operations.failed_closed_calls == 0 && operations.revert_trace.size() == 1);
      assert(!transaction.RecoverFailedClosed());
    }
    operations.rollback_ready = CharacterSwapPreflight::kReady;
    const auto result = transaction.Tick(++now);
    assert(result.phase == CharacterSwapPhase::kFailedClosed);
    assert(result.rollback_complete && result.applied_mutation_count == 0);
    assert(operations.rollback_prepare_calls == 52);
    AssertReverse(operations.apply_trace, operations.revert_trace);
    assert(transaction.RecoverFailedClosed());
  }
  // Before release the original guard remains held: no new asynchronous
  // reacquisition is required, and normal reverse-order cleanup is preserved.
  MockOperations operations;
  operations.verify_fail_call = 1;
  operations.rollback_ready = CharacterSwapPreflight::kWait;
  CharacterSwapTransaction transaction(&operations);
  assert(transaction.Submit(Request(8100, 100), 100) == CharacterSwapSubmitResult::kAccepted);
  TickUntilTerminal(&transaction, 100);
  assert(transaction.status().rollback_complete && operations.rollback_prepare_calls == 0);
}

void TestUnsafeRollbackDoesNotDetachLiveGraph() {
  for (unsigned failure = 0; failure < 3; ++failure) {
    MockOperations operations;
    operations.verify_fail_call = 4;
    operations.rollback_ready = CharacterSwapPreflight::kWait;
    CharacterSwapTransaction transaction(&operations);
    assert(transaction.Submit(Request(8200, 100), 100) == CharacterSwapSubmitResult::kAccepted);
    auto now = TickUntilRollback(&transaction, 100);
    if (failure == 0) operations.rollback_ready = CharacterSwapPreflight::kReject;
    if (failure == 1) now += 5001; // bounded rollback stop wait expired
    if (failure == 2) now -= 1; // rollback clock regressed
    const auto result = transaction.Tick(now);
    assert(result.phase == CharacterSwapPhase::kFailedClosed);
    assert(result.failure == CharacterSwapFailure::kRollbackFailed);
    assert(!result.rollback_complete && result.rollback_failure_count == 1);
    assert(result.applied_mutation_count != 0);
    assert(operations.revert_trace.size() == 1); // only reacquired guards
    assert(operations.failed_closed_calls == 1 && !operations.failed_closed_rollback_complete);
    assert(operations.failed_closed_cause == CharacterSwapFailure::kSettleVerificationFailed);
    assert(!transaction.RecoverFailedClosed() && transaction.command_gate_closed());
    transaction.Tick(now + 10000);
    assert(operations.revert_trace.size() == 1 && operations.failed_closed_calls == 1);
  }
}

}  // namespace

int main() {
  TestSuccessRequiresStableAndSettleFrames();
  TestQuiesceMustSettleBeforeAtomicCommit();
  TestQuiesceRejectionRollsBackGuardsOnly();
  TestChangedPreflightRestartsTwoFrameRequirement();
  TestEveryMutationFailureRollsBackInReverse();
  TestEveryCleanMutationFailureOmitsFailedStepFromRollback();
  TestEverySettleVerificationBoundaryRollsBack();
  TestEveryRevertFailureStillAttemptsRemainingRollback();
  TestRapidRequestsAreSerializedAndDebounced();
  TestCheckpointRestoreBypassesOnlyDebounce();
  TestRollbackFailureCannotRecover();
  TestRecoveryRequiresOriginalGraphVerification();
  TestInvalidReciprocalGraphFailsBeforeMutation();
  TestPreflightRejectionWithoutCaptureCanRecover();
  TestWaitAndTimeoutRemainFailClosed();
  TestRequestValidationAndAge();
  TestRepeatedAbAAndAbcCycles();
  TestRollbackWaitsForReacquiredAiGuard();
  TestUnsafeRollbackDoesNotDetachLiveGraph();
  return 0;
}
