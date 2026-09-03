#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace wfcds {

// An engine object identity must contain both the address observed during the
// transaction and the UObject index/serial used to detect recycled objects.
// A zero serial is permitted on builds where the adapter cannot expose it, but
// address and object_index are always required.
struct CharacterSwapObjectIdentity {
  uintptr_t address = 0;
  uint32_t object_index = 0;
  uint32_t object_serial = 0;
};

struct CharacterSwapRequest {
  uint64_t request_id = 0;
  uint64_t requested_at_millis = 0;
  uint64_t world_generation = 0;
  uint32_t expected_world_object_index = 0;
  uint32_t expected_player_controller_object_index = 0;
  uint32_t expected_player_pawn_object_index = 0;
  uint32_t expected_target_controller_object_index = 0;
  uint32_t expected_target_pawn_object_index = 0;
  uint32_t expected_player_identity_hash = 0;
  uint32_t expected_target_identity_hash = 0;
  uint32_t squad_slot = 0;
  // Internal checkpoint recovery may cross combat/menu/script restrictions
  // because the game has already requested persistence. It still passes all
  // UObject, reciprocal-link, vitals, form, weapon, and graph validations.
  bool checkpoint_native_restore = false;
};

// Authoritative engine state sampled before a transfer. The four explicit
// link fields are intentional: preserving just the four actor identities is
// not enough to prove that both controller/pawn pairs are reciprocal.
struct CharacterSwapCapture {
  uint64_t world_generation = 0;
  CharacterSwapObjectIdentity world = {};

  CharacterSwapObjectIdentity player_controller = {};
  CharacterSwapObjectIdentity player_pawn = {};
  CharacterSwapObjectIdentity target_ai_controller = {};
  CharacterSwapObjectIdentity target_ai_pawn = {};

  CharacterSwapObjectIdentity player_controller_pawn_link = {};
  CharacterSwapObjectIdentity player_pawn_controller_link = {};
  CharacterSwapObjectIdentity target_controller_pawn_link = {};
  CharacterSwapObjectIdentity target_pawn_controller_link = {};

  uint32_t player_identity_hash = 0;
  uint32_t target_identity_hash = 0;
  uint32_t squad_slot = 0;
  uint32_t squad_record_generation = 0;

  // The adapter changes this whenever a safety-relevant engine state changes
  // (combat/script/form transition, death/downed state, squad availability,
  // pending destruction, and so on). It is part of the two-frame stability
  // comparison.
  uint64_t safety_generation = 0;

  // Non-owning scalar tokens for the exact-build adapter. They must never hold
  // raw FString/UStruct copies or other values requiring destruction.
  uintptr_t adapter_cookie = 0;
  uintptr_t adapter_words[8] = {};
};

enum class CharacterSwapPhase : uint8_t {
  kIdle,
  kPreflightStable,
  kQuiesce,
  // Waits for asynchronous engine quiescence (notably GOAP suspension) to
  // become observable before any controller/pawn link is changed.
  kQuiesceVerify,
  kPrepared,
  kAtomicCommit,
  kSettleVerify,
  kDone,
  kRollback,
  kFailedClosed,
};

enum class CharacterSwapMutation : uint8_t {
  kQuiescePlayerInput,
  kQuiesceTargetAi,
  kPrepareRoleAdapters,
  // Detachment is an exact-build adapter operation. It must bypass the
  // incompatible TnPlayerController/TnAiPawn and TnAiController/TnPlayerPawn
  // script callbacks; this core never substitutes raw UE3 Possess calls.
  kDetachOriginalControlLinks,
  kBindPlayerControllerToTarget,
  kBindAiControllerToFormerPlayer,
  kUpdateSquadMembership,
  kUpdateCharacterIdentity,
  kRebindRuntimeServices,
  kUpdateCameraAndViewTarget,
  kArmControlTransfer,
  kReleaseTransitionGuards,
  kCount,
};

enum class CharacterSwapPreflight : uint8_t {
  kReady,
  kWait,
  kReject,
};

// A mutation which reports kFailedNeedsRollback is included in the rollback
// stack. This allows an adapter to report a failure after it crossed an engine
// mutation boundary. kFailedNoMutation is only valid when the engine graph is
// provably unchanged.
enum class CharacterSwapApplyResult : uint8_t {
  kApplied,
  kFailedNoMutation,
  kFailedNeedsRollback,
};

enum class CharacterSwapFailure : uint8_t {
  kNone,
  kInvalidRequest,
  kRequestExpired,
  kClockRegression,
  kPreflightRejected,
  kPreflightCaptureInvalid,
  kQuiesceVerificationFailed,
  kTransactionTimedOut,
  kMutationFailed,
  kSettleVerificationFailed,
  kRollbackFailed,
  kRecoveryVerificationFailed,
};

enum class CharacterSwapSubmitResult : uint8_t {
  kAccepted,
  kBusy,
  kDebounced,
  kInvalid,
  kExpired,
  kClockRegression,
};

struct CharacterSwapConfig {
  uint64_t debounce_millis = 650;
  uint64_t maximum_request_age_millis = 1000;
  uint64_t maximum_transaction_millis = 5000;
};

struct CharacterSwapStatus {
  CharacterSwapPhase phase = CharacterSwapPhase::kIdle;
  CharacterSwapFailure failure = CharacterSwapFailure::kNone;
  CharacterSwapMutation failed_mutation = CharacterSwapMutation::kCount;
  uint64_t request_id = 0;
  uint8_t stable_preflight_frames = 0;
  uint8_t successful_settle_ticks = 0;
  uint8_t applied_mutation_count = 0;
  uint8_t rollback_failure_count = 0;
  bool rollback_complete = true;
};

class CharacterSwapOperations {
 public:
  virtual ~CharacterSwapOperations() = default;

  // Must be read-only. kReady means the adapter has checked all game-specific
  // restrictions and completely initialized output.
  virtual CharacterSwapPreflight CapturePreflight(
      const CharacterSwapRequest& request,
      CharacterSwapCapture* output) noexcept = 0;

  // Read-only validation performed on ticks after the quiesce mutations.
  // kWait keeps the transaction closed without entering AtomicCommit;
  // kReject rolls back the acquired guards. This is deliberately separate
  // from CapturePreflight because adapters may have transaction-owned state
  // which must not be recaptured or overwritten after quiescence begins.
  virtual CharacterSwapPreflight VerifyQuiesced(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept = 0;

  // ApplyMutation must honor the CharacterSwapApplyResult contract above.
  // In particular it must never return kFailedNoMutation after making a
  // partial engine change. kPrepareRoleAdapters must remain behaviorally
  // inert because an original engine tick occurs before kAtomicCommit; publish
  // any live cross-role mapping in kDetachOriginalControlLinks only after the
  // previous mapping's reciprocal links are fully detached.
  virtual CharacterSwapApplyResult ApplyMutation(
      CharacterSwapMutation mutation, const CharacterSwapRequest& request,
      CharacterSwapCapture* capture) noexcept = 0;

  // RevertMutation is called for every applied or dirty-failed mutation, in
  // strict reverse order. A failure does not stop the remaining rollback.
  virtual bool RevertMutation(CharacterSwapMutation mutation,
                              const CharacterSwapRequest& request,
                              CharacterSwapCapture* capture) noexcept = 0;

  // Both verification methods must be read-only.
  virtual bool VerifyCommittedGraph(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept = 0;
  virtual bool VerifyRestoredGraph(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept = 0;

  // Called after synchronous rollback. The adapter should keep external
  // command/input gates closed if rollback_complete is false.
  virtual void EnterFailedClosed(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture, CharacterSwapFailure cause,
      bool rollback_complete) noexcept = 0;
  virtual bool ExitFailedClosed(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept = 0;
  virtual void OnSucceeded(const CharacterSwapRequest& request,
                           const CharacterSwapCapture& capture) noexcept = 0;
};

// GameBridge drives this object exactly once per game tick. AtomicCommit
// intentionally executes all ownership/ledger/service mutations in one call
// to Tick so the engine never receives a frame with a half-committed graph.
class CharacterSwapTransaction {
 public:
  explicit CharacterSwapTransaction(
      CharacterSwapOperations* operations,
      CharacterSwapConfig config = CharacterSwapConfig{}) noexcept;

  CharacterSwapTransaction(const CharacterSwapTransaction&) = delete;
  CharacterSwapTransaction& operator=(const CharacterSwapTransaction&) =
      delete;

  CharacterSwapSubmitResult Submit(const CharacterSwapRequest& request,
                                   uint64_t now_millis) noexcept;
  CharacterSwapStatus Tick(uint64_t now_millis) noexcept;

  // Done remains terminal until explicitly acknowledged, which prevents a
  // queued tap from becoming a second transaction during UI resynchronization.
  bool AcknowledgeDone() noexcept;

  // A failed transaction stays closed. Recovery is allowed only after every
  // mutation rolled back and the adapter verifies the original graph.
  bool RecoverFailedClosed() noexcept;

  CharacterSwapStatus status() const noexcept { return status_; }
  const CharacterSwapRequest& request() const noexcept { return request_; }
  const CharacterSwapCapture& capture() const noexcept { return capture_; }
  bool command_gate_closed() const noexcept {
    return status_.phase != CharacterSwapPhase::kIdle;
  }
  bool in_progress() const noexcept;

 private:
  static constexpr uint8_t kRequiredStablePreflightFrames = 2;
  static constexpr uint8_t kRequiredSettleVerificationTicks = 3;
  static constexpr size_t kMutationCapacity =
      static_cast<size_t>(CharacterSwapMutation::kCount);

  bool ApplyOne(CharacterSwapMutation mutation) noexcept;
  void FailAndRollback(CharacterSwapFailure cause,
                       CharacterSwapMutation failed_mutation) noexcept;
  bool CaptureMatchesRequest(const CharacterSwapCapture& capture) const
      noexcept;
  static bool IsInternallyValid(const CharacterSwapCapture& capture) noexcept;
  static bool StableCaptureEqual(const CharacterSwapCapture& left,
                                 const CharacterSwapCapture& right) noexcept;
  static bool SameObject(const CharacterSwapObjectIdentity& left,
                         const CharacterSwapObjectIdentity& right) noexcept;
  static bool IsValidObject(const CharacterSwapObjectIdentity& object) noexcept;
  static bool IsRequestShapeValid(const CharacterSwapRequest& request) noexcept;
  void ClearCurrent() noexcept;

  CharacterSwapOperations* operations_ = nullptr;
  CharacterSwapConfig config_ = {};
  CharacterSwapStatus status_ = {};
  CharacterSwapRequest request_ = {};
  CharacterSwapCapture capture_ = {};
  CharacterSwapMutation applied_[kMutationCapacity] = {};
  size_t applied_count_ = 0;
  uint64_t transaction_started_at_millis_ = 0;
  uint64_t last_accepted_at_millis_ = 0;
  bool has_last_accepted_time_ = false;
  bool has_capture_ = false;
};

static_assert(std::is_trivially_copyable<CharacterSwapObjectIdentity>::value,
              "object identity must remain a value snapshot");
static_assert(std::is_trivially_copyable<CharacterSwapRequest>::value,
              "swap request must remain a value snapshot");
static_assert(std::is_trivially_copyable<CharacterSwapCapture>::value,
              "swap capture must not own Unreal Engine values");

}  // namespace wfcds
