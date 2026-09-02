#include "StateStore.h"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace wfcds {
namespace {

SRWLOCK g_snapshot_lock = SRWLOCK_INIT;
Snapshot g_snapshot = {};
bool g_have_snapshot = false;

constexpr uint32_t kCommandCapacity = 32;
SRWLOCK g_command_lock = SRWLOCK_INIT;
Command g_commands[kCommandCapacity] = {};
uint32_t g_read = 0;
uint32_t g_write = 0;
uint32_t g_count = 0;
std::atomic<uint64_t> g_next_request{1};

template <size_t N>
bool Terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

void CopyText(char* destination, size_t capacity, const char* source) {
  if (!destination || capacity == 0) return;
  std::snprintf(destination, capacity, "%s", source ? source : "");
}

bool FiniteOrUnavailable(float value) {
  return std::isfinite(value) && value >= -1.0f;
}

bool ValidCommand(CommandType type) {
  return type >= CommandType::kTransform &&
         type <= CommandType::kSwitchSquadMember;
}

bool ValidSnapshot(const Snapshot& value) {
  if (value.version != kSnapshotVersion || value.size != sizeof(Snapshot) ||
      value.squad_count > kSquadCapacity ||
      static_cast<uint32_t>(value.lifecycle) >
          static_cast<uint32_t>(Lifecycle::kBridgeError) ||
      !Terminated(value.character_name) || !Terminated(value.build_label) ||
      !Terminated(value.grenade_name) || !Terminated(value.portrait_key) ||
      !Terminated(value.grenade_icon_key) ||
      !FiniteOrUnavailable(value.health) ||
      !FiniteOrUnavailable(value.max_health) ||
      !FiniteOrUnavailable(value.resource) ||
      !FiniteOrUnavailable(value.max_resource))
    return false;
  for (const WeaponState& weapon : value.weapons)
    if (!Terminated(weapon.name) || !Terminated(weapon.icon_key)) return false;
  for (const AbilityState& ability : value.abilities)
    if (!Terminated(ability.name) || !Terminated(ability.icon_key) ||
        !FiniteOrUnavailable(ability.resource_cost) ||
        !FiniteOrUnavailable(ability.cooldown_remaining) ||
        !FiniteOrUnavailable(ability.cooldown_duration))
      return false;
  for (uint32_t i = 0; i < value.squad_count; ++i) {
    const SquadMemberState& member = value.squad[i];
    if (!member.occupied || !Terminated(member.name) ||
        !Terminated(member.weapon_name) || !Terminated(member.portrait_key) ||
        !FiniteOrUnavailable(member.health) ||
        !FiniteOrUnavailable(member.max_health))
      return false;
  }
  const ObjectiveState& objective = value.objective;
  return Terminated(objective.chapter) && Terminated(objective.mission) &&
         Terminated(objective.text) && Terminated(objective.image_key) &&
         FiniteOrUnavailable(objective.distance_meters);
}

}  // namespace

void ResetState(Lifecycle lifecycle, const char* build_label,
                bool supported_build) {
  AcquireSRWLockExclusive(&g_snapshot_lock);
  const uint64_t sequence = g_have_snapshot ? g_snapshot.sequence + 1 : 1;
  const uint64_t epoch = g_have_snapshot ? g_snapshot.world_epoch + 1 : 1;
  g_snapshot = {};
  g_snapshot.size = sizeof(Snapshot);
  g_snapshot.sequence = sequence;
  g_snapshot.world_epoch = epoch;
  g_snapshot.lifecycle = lifecycle;
  g_snapshot.supported_build = supported_build ? 1u : 0u;
  CopyText(g_snapshot.build_label, sizeof(g_snapshot.build_label), build_label);
  g_have_snapshot = true;
  ReleaseSRWLockExclusive(&g_snapshot_lock);
  RejectStaleCommands(epoch);
}

bool PublishSnapshot(const Snapshot& snapshot) {
  if (!ValidSnapshot(snapshot)) return false;
  bool accepted = false;
  AcquireSRWLockExclusive(&g_snapshot_lock);
  if (!g_have_snapshot ||
      (snapshot.world_epoch >= g_snapshot.world_epoch &&
       snapshot.sequence > g_snapshot.sequence)) {
    g_snapshot = snapshot;
    g_have_snapshot = true;
    accepted = true;
  }
  ReleaseSRWLockExclusive(&g_snapshot_lock);
  return accepted;
}

bool CopyLatestSnapshot(Snapshot* output) {
  if (!output) return false;
  AcquireSRWLockShared(&g_snapshot_lock);
  const bool available = g_have_snapshot;
  if (available) *output = g_snapshot;
  ReleaseSRWLockShared(&g_snapshot_lock);
  return available;
}

bool QueueCommand(const Command& input, uint64_t* request_id) {
  if (!ValidCommand(input.type) || !input.world_epoch ||
      !input.queued_at_millis ||
      !input.expected_controller_object_index ||
      !input.expected_player_pawn_object_index ||
      !input.expected_target_object_index ||
      (input.type == CommandType::kSwitchSquadMember &&
       (input.squad_slot >= kSquadCapacity ||
        !input.expected_pawn_object_index || !input.expected_identity_hash)))
    return false;
  Command command = input;
  command.version = kSnapshotVersion;
  if (!command.request_id)
    command.request_id = g_next_request.fetch_add(1, std::memory_order_relaxed);
  AcquireSRWLockExclusive(&g_command_lock);
  if (g_count == kCommandCapacity) {
    ReleaseSRWLockExclusive(&g_command_lock);
    return false;
  }
  // Android may deliver the same physical touch through more than one input
  // route. Keep only one equivalent request pending so a single tap cannot
  // trigger a native action twice on consecutive game ticks.
  for (uint32_t i = 0; i < g_count; ++i) {
    const Command& pending = g_commands[(g_read + i) % kCommandCapacity];
    if (pending.type == command.type &&
        pending.world_epoch == command.world_epoch &&
        pending.expected_controller_object_index ==
            command.expected_controller_object_index &&
        pending.expected_player_pawn_object_index ==
            command.expected_player_pawn_object_index &&
        pending.expected_target_object_index ==
            command.expected_target_object_index &&
        pending.squad_slot == command.squad_slot) {
      ReleaseSRWLockExclusive(&g_command_lock);
      return false;
    }
  }
  g_commands[g_write] = command;
  g_write = (g_write + 1) % kCommandCapacity;
  ++g_count;
  ReleaseSRWLockExclusive(&g_command_lock);
  if (request_id) *request_id = command.request_id;
  return true;
}

bool PollCommand(Command* output) {
  if (!output) return false;
  AcquireSRWLockExclusive(&g_command_lock);
  const bool available = g_count != 0;
  if (available) {
    *output = g_commands[g_read];
    g_commands[g_read] = {};
    g_read = (g_read + 1) % kCommandCapacity;
    --g_count;
  }
  ReleaseSRWLockExclusive(&g_command_lock);
  return available;
}

void RejectStaleCommands(uint64_t world_epoch) {
  AcquireSRWLockExclusive(&g_command_lock);
  Command retained[kCommandCapacity] = {};
  uint32_t retained_count = 0;
  for (uint32_t i = 0; i < g_count; ++i) {
    const Command& command = g_commands[(g_read + i) % kCommandCapacity];
    if (command.world_epoch == world_epoch)
      retained[retained_count++] = command;
  }
  for (Command& command : g_commands) command = {};
  for (uint32_t i = 0; i < retained_count; ++i) g_commands[i] = retained[i];
  g_read = 0;
  g_count = retained_count;
  g_write = retained_count % kCommandCapacity;
  ReleaseSRWLockExclusive(&g_command_lock);
}

uint32_t PendingCommandCount() {
  AcquireSRWLockShared(&g_command_lock);
  const uint32_t count = g_count;
  ReleaseSRWLockShared(&g_command_lock);
  return count;
}

}  // namespace wfcds
