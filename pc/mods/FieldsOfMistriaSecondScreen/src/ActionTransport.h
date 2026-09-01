#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>

namespace fomds {

inline constexpr char kBridgeProtocol[] = "fields-of-mistria-dualscreen";
inline constexpr std::uint32_t kBridgeSchema = 1;
inline constexpr std::uint32_t kMaxBridgeInventorySlots = 512;
inline constexpr std::uint32_t kMaxBridgeArmorSlots = 64;
inline constexpr std::uint32_t kMaxBridgeSpells = 64;
inline constexpr std::size_t kMaxExpectedItemFingerprintBytes = 4096;

struct ActionTransportConfig {
  std::size_t max_queue = 16;
  std::uint64_t min_write_interval_ms = 75;
  std::uint64_t acknowledgement_timeout_ms = 3000;
};

// This is deliberately populated only after the state consumer has validated a
// full snapshot/delta chain. ActionTransport does not treat unparsed JSON as a
// trusted session.
struct BridgeActionSession {
  bool validated = false;
  std::string protocol;
  std::uint32_t schema = 0;
  std::string epoch;
  std::uint64_t state_sequence = 0;
  std::uint64_t inventory_revision = 0;
  std::uint64_t armor_revision = 0;
  std::uint64_t hotbar_revision = 0;
  std::uint64_t magic_revision = 0;
  // Latest validated ack sequence if one already exists when attaching.
  // Zero means no prior acknowledgement is known for this epoch.
  std::uint64_t command_sequence_floor = 0;
  std::uint32_t inventory_size = 0;
  std::uint32_t armor_size = 0;
  std::int32_t selected_hotbar_index = -1;
  std::uint32_t learned_spell_count = 0;
  std::int32_t learned_spell_indexes[kMaxBridgeSpells] = {};
  std::int32_t pinned_spell_index = -1;
  bool in_game = false;
  bool can_select_hotbar = false;
  bool can_swap_slots = false;
  bool can_consume_item = false;
  bool can_equip_armor = false;
  bool can_unequip_armor = false;
  bool can_pin_spell = false;
  bool can_unpin_spell = false;
  bool can_cast_pinned_spell = false;
};

struct BridgeActionAck {
  bool validated = false;
  std::string protocol;
  std::uint32_t schema = 0;
  std::string epoch;
  std::uint64_t state_sequence = 0;
  std::uint64_t command_sequence = 0;
  std::string action;
  bool ok = false;
  std::string code;
};

enum class ActionKind : std::uint8_t {
  kResync,
  kSelectHotbar,
  kSwapSlots,
  kEquipArmor,
  kUnequipArmor,
  kConsumeItem,
  kPinSpell,
  kUnpinSpell,
  kCastPinnedSpell,
};

enum class SubmitCode : std::uint8_t {
  kQueued,
  kNoValidSession,
  kCapabilityUnavailable,
  kInvalidArgument,
  kStaleView,
  kQueueFull,
};

struct SubmitResult {
  SubmitCode code = SubmitCode::kNoValidSession;
  std::uint64_t request_id = 0;
};

enum class PumpCode : std::uint8_t {
  kIdle,
  kWaitingForAcknowledgement,
  kRateLimited,
  kCommandPathBusy,
  kWritten,
  kDroppedStaleView,
  kIoError,
  kAcknowledgementTimedOut,
  kNoValidSession,
};

struct PumpResult {
  PumpCode code = PumpCode::kIdle;
  std::uint64_t request_id = 0;
  std::uint64_t command_sequence = 0;
  std::uint32_t system_error = 0;
};

enum class AckCode : std::uint8_t {
  kAccepted,
  kNoOutstandingCommand,
  kInvalid,
  kDoesNotMatch,
};

// A single-producer bridge writer. It never synthesizes keyboard, mouse,
// controller, or SDL input. Its sole mutation is an atomic same-directory
// rename to CONFIG_DIRECTORY/dualscreen/command.json.
class ActionTransport {
 public:
  explicit ActionTransport(ActionTransportConfig config = {});

  bool Initialize(const std::wstring& bridge_directory);
  bool UpdateSession(const BridgeActionSession& session);
  void ClearSession();
  bool HasValidSession() const;

  // Transport-only recovery request. It never passes the GML gameplay action
  // gate and does not mutate game state.
  SubmitResult QueueResync();
  SubmitResult QueueSelectHotbar(std::int64_t index,
                                 std::uint64_t inventory_revision,
                                 std::uint64_t hotbar_revision);
  SubmitResult QueueSwapSlots(std::int64_t first, std::int64_t second,
                              std::uint64_t inventory_revision);
  SubmitResult QueueEquipArmor(
      std::int64_t inventory_index, std::int64_t armor_index,
      const std::string& expected_item,
      const std::string& expected_equipped_item,
      std::uint64_t inventory_revision, std::uint64_t armor_revision);
  SubmitResult QueueUnequipArmor(
      std::int64_t armor_index, std::int64_t inventory_index,
      const std::string& expected_item,
      const std::string& expected_destination_item,
      std::uint64_t inventory_revision, std::uint64_t armor_revision);
  SubmitResult QueueConsumeItem(std::int64_t index,
                                const std::string& expected_item,
                                std::uint64_t inventory_revision,
                                std::uint64_t hotbar_revision);
  SubmitResult QueuePinSpell(std::int64_t spell,
                             std::uint64_t magic_revision);
  SubmitResult QueueUnpinSpell(std::int64_t spell,
                               std::uint64_t magic_revision);
  SubmitResult QueueCastPinnedSpell(std::int64_t spell,
                                    std::uint64_t magic_revision);

  PumpResult Pump(std::uint64_t now_ms);
  AckCode HandleAcknowledgement(const BridgeActionAck& acknowledgement);

  std::size_t PendingCount() const;
  bool HasOutstandingCommand() const;

 private:
  struct Request {
    ActionKind kind = ActionKind::kSelectHotbar;
    std::uint64_t request_id = 0;
    std::uint64_t inventory_revision = 0;
    std::uint64_t armor_revision = 0;
    std::uint64_t hotbar_revision = 0;
    std::uint64_t magic_revision = 0;
    std::uint32_t first = 0;
    std::uint32_t second = 0;
    std::string expected_item;
    std::string expected_other_item;
  };

  struct Outstanding {
    bool active = false;
    Request request;
    std::uint64_t command_sequence = 0;
    std::uint64_t written_at_ms = 0;
  };

  static bool IsSessionValid(const BridgeActionSession& session);
  static std::string BuildCommand(const BridgeActionSession& session,
                                  const Request& request,
                                  std::uint64_t sequence);
  bool IsRequestCurrent(const Request& request) const;
  void ClearSessionLocked();
  SubmitResult QueueLocked(Request request);

  mutable std::mutex mutex_;
  ActionTransportConfig config_;
  std::wstring directory_;
  std::wstring command_path_;
  bool initialized_ = false;
  bool session_valid_ = false;
  BridgeActionSession session_;
  std::deque<Request> queue_;
  Outstanding outstanding_;
  std::uint64_t next_command_sequence_ = 0;
  std::uint64_t producer_nonce_ = 0;
  std::uint64_t last_attempt_ms_ = 0;
  bool attempted_ = false;
};

}  // namespace fomds
