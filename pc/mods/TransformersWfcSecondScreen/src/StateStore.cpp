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

template <size_t N>
bool SameText(const char (&left)[N], const char (&right)[N]) {
  return std::strncmp(left, right, N) == 0;
}

bool SameWeapon(const WeaponState& left, const WeaponState& right) {
  return left.object_index == right.object_index &&
         left.magazine == right.magazine && left.reserve == right.reserve &&
         left.magazine_capacity == right.magazine_capacity &&
         left.reserve_capacity == right.reserve_capacity &&
         left.heat == right.heat &&
         left.heat_capacity == right.heat_capacity &&
         left.equipped == right.equipped &&
         left.available == right.available &&
         left.unlimited_reserve == right.unlimited_reserve &&
         left.clipless == right.clipless &&
         left.heat_based == right.heat_based &&
         SameText(left.name, right.name) &&
         SameText(left.icon_key, right.icon_key);
}

bool SameAbility(const AbilityState& left, const AbilityState& right) {
  return left.object_index == right.object_index &&
         left.resource_cost == right.resource_cost &&
         left.cooldown_remaining == right.cooldown_remaining &&
         left.cooldown_duration == right.cooldown_duration &&
         left.available == right.available && left.active == right.active &&
         left.cooldown_pending == right.cooldown_pending &&
         SameText(left.name, right.name) &&
         SameText(left.icon_key, right.icon_key);
}

bool SameSquadMember(const SquadMemberState& left,
                     const SquadMemberState& right) {
  return left.pawn_object_index == right.pawn_object_index &&
         left.controller_object_index == right.controller_object_index &&
         left.identity_hash == right.identity_hash &&
         left.faction == right.faction && left.mode == right.mode &&
         left.occupied == right.occupied &&
         left.controlled == right.controlled && left.alive == right.alive &&
         left.downed == right.downed &&
         left.switchable == right.switchable && left.health == right.health &&
         left.max_health == right.max_health &&
         SameText(left.name, right.name) &&
         SameText(left.weapon_name, right.weapon_name) &&
         SameText(left.portrait_key, right.portrait_key);
}

bool SameObjective(const ObjectiveState& left, const ObjectiveState& right) {
  return left.objective_object_index == right.objective_object_index &&
         left.active == right.active && left.completed == right.completed &&
         left.failed == right.failed &&
         left.has_world_location == right.has_world_location &&
         left.world_x == right.world_x && left.world_y == right.world_y &&
         left.world_z == right.world_z &&
         left.distance_meters == right.distance_meters &&
         SameText(left.chapter, right.chapter) &&
         SameText(left.mission, right.mission) &&
         SameText(left.text, right.text) &&
         SameText(left.image_key, right.image_key);
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
    if (!Terminated(weapon.name) || !Terminated(weapon.icon_key) ||
        !FiniteOrUnavailable(weapon.heat) ||
        !FiniteOrUnavailable(weapon.heat_capacity))
      return false;
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

bool SnapshotContentEqual(const Snapshot& left, const Snapshot& right) {
  if (left.version != right.version || left.size != right.size ||
      left.world_epoch != right.world_epoch ||
      left.capabilities != right.capabilities ||
      left.lifecycle != right.lifecycle ||
      left.supported_build != right.supported_build ||
      left.local_controller_object_index !=
          right.local_controller_object_index ||
      left.player_pawn_object_index != right.player_pawn_object_index ||
      left.world_object_index != right.world_object_index ||
      left.inventory_manager_object_index !=
          right.inventory_manager_object_index ||
      left.active_weapon_object_index != right.active_weapon_object_index ||
      left.ability_manager_object_index !=
          right.ability_manager_object_index ||
      left.grenade_object_index != right.grenade_object_index ||
      left.faction != right.faction || left.mode != right.mode ||
      left.alive != right.alive || left.downed != right.downed ||
      left.paused != right.paused || left.in_combat != right.in_combat ||
      left.in_cutscene != right.in_cutscene || left.in_menu != right.in_menu ||
      left.in_scripted_sequence != right.in_scripted_sequence ||
      left.switch_in_progress != right.switch_in_progress ||
      left.health != right.health || left.max_health != right.max_health ||
      left.resource != right.resource ||
      left.max_resource != right.max_resource ||
      left.grenade_count != right.grenade_count ||
      left.squad_count != right.squad_count ||
      !SameText(left.character_name, right.character_name) ||
      !SameText(left.grenade_name, right.grenade_name) ||
      !SameText(left.portrait_key, right.portrait_key) ||
      !SameText(left.grenade_icon_key, right.grenade_icon_key) ||
      !SameText(left.build_label, right.build_label))
    return false;
  for (size_t index = 0; index < 2; ++index) {
    if (!SameWeapon(left.weapons[index], right.weapons[index]) ||
        !SameAbility(left.abilities[index], right.abilities[index]))
      return false;
  }
  for (size_t index = 0; index < kSquadCapacity; ++index)
    if (!SameSquadMember(left.squad[index], right.squad[index])) return false;
  return SameObjective(left.objective, right.objective);
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
