#include "StateStore.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Log.h"

namespace fo3ds {
namespace {

SRWLOCK g_state_lock = SRWLOCK_INIT;
Snapshot g_state = {};
bool g_have_state = false;

constexpr uint32_t kCommandCapacity = 64;
SRWLOCK g_command_lock = SRWLOCK_INIT;
Command g_commands[kCommandCapacity] = {};
uint32_t g_command_read = 0;
uint32_t g_command_write = 0;
uint32_t g_command_count = 0;
std::atomic<uint64_t> g_next_request_id{1};
std::atomic<uint64_t> g_published_sequence{0};

void CopyText(char* destination, size_t capacity, const char* source) {
  if (!destination || !capacity) return;
  std::snprintf(destination, capacity, "%s", source ? source : "");
}

bool ValidCommandType(CommandType type) {
  switch (type) {
    case CommandType::kEquipItem:
    case CommandType::kUnequipItem:
    case CommandType::kUseItem:
    case CommandType::kTrackQuest:
    case CommandType::kTuneRadio:
    case CommandType::kRadioOff:
      return true;
    case CommandType::kNone:
    case CommandType::kDropItem:
    case CommandType::kFastTravel:
      return false;
  }
  return false;
}

template <size_t N>
bool Terminated(const char (&value)[N]) {
  return std::memchr(value, '\0', N) != nullptr;
}

bool ValidSnapshot(const Snapshot& snapshot) {
  if (snapshot.version != kSnapshotVersion ||
      snapshot.size != sizeof(Snapshot) ||
      static_cast<uint32_t>(snapshot.lifecycle) >
          static_cast<uint32_t>(Lifecycle::kUnsupported) ||
      static_cast<uint32_t>(snapshot.last_action_result) >
          static_cast<uint32_t>(ActionResult::kRejected) ||
      snapshot.item_count > kMaxItems || snapshot.effect_count > kMaxEffects ||
      snapshot.quest_count > kMaxQuests ||
      snapshot.objective_count > kMaxObjectives ||
      snapshot.note_count > kMaxNotes || snapshot.marker_count > kMaxMarkers ||
      snapshot.station_count > kMaxStations || snapshot.perk_count > kMaxPerks ||
      !Terminated(snapshot.player_name) || !Terminated(snapshot.location) ||
      !Terminated(snapshot.build_label))
    return false;

  const float scalars[] = {
      snapshot.health, snapshot.max_health, snapshot.action_points,
      snapshot.max_action_points, snapshot.damage_resistance,
      snapshot.radiation_resistance, snapshot.radiation,
      snapshot.carry_weight, snapshot.max_carry_weight,
      snapshot.karma, snapshot.player_x, snapshot.player_y, snapshot.player_z,
      snapshot.player_heading, snapshot.map_min_x, snapshot.map_max_y,
      snapshot.map_max_x, snapshot.map_min_y};
  for (float value : scalars)
    if (!std::isfinite(value)) return false;
  for (float value : snapshot.special)
    if (!std::isfinite(value)) return false;
  for (float value : snapshot.skills)
    if (!std::isfinite(value)) return false;
  for (float value : snapshot.limbs)
    if (!std::isfinite(value)) return false;
  if (!Terminated(snapshot.map_texture_path)) return false;
  if (snapshot.capabilities & kCapWorldMap) {
    if (!(snapshot.capabilities & kCapMap) ||
        snapshot.map_max_x <= snapshot.map_min_x ||
        snapshot.map_max_y <= snapshot.map_min_y ||
        snapshot.map_usable_width == 0 || snapshot.map_usable_width > 8192 ||
        snapshot.map_usable_height == 0 ||
        snapshot.map_usable_height > 8192 || !snapshot.map_texture_path[0])
      return false;
  }
  if (snapshot.capabilities & kCapGeneralStats) {
    for (int32_t value : snapshot.misc_stats)
      if (value < 0) return false;
  }

  constexpr uint16_t kKnownItemFlags =
      kItemEquipped | kItemQuest | kItemFavorite | kItemPlayable;
  for (uint32_t i = 0; i < snapshot.item_count; ++i) {
    const ItemState& item = snapshot.items[i];
    if (static_cast<uint32_t>(item.category) >
            static_cast<uint32_t>(ItemCategory::kUnknown) ||
        (item.flags & ~kKnownItemFlags) || !Terminated(item.name) ||
        !Terminated(item.description) || !Terminated(item.icon_path) ||
        !std::isfinite(item.weight) ||
        !std::isfinite(item.condition) || !std::isfinite(item.damage) ||
        !std::isfinite(item.armor))
      return false;
  }
  for (uint32_t i = 0; i < snapshot.effect_count; ++i) {
    const EffectState& effect = snapshot.effects[i];
    if (!Terminated(effect.name) || !std::isfinite(effect.magnitude) ||
        !std::isfinite(effect.seconds_remaining))
      return false;
  }
  for (uint32_t i = 0; i < snapshot.quest_count; ++i) {
    const QuestState& quest = snapshot.quests[i];
    if (quest.objective_begin > snapshot.objective_count ||
        quest.objective_count >
            snapshot.objective_count - quest.objective_begin ||
        !Terminated(quest.title) || !Terminated(quest.description) ||
        !Terminated(quest.icon_path))
      return false;
  }
  for (uint32_t i = 0; i < snapshot.objective_count; ++i)
    if (!Terminated(snapshot.objectives[i].text)) return false;
  for (uint32_t i = 0; i < snapshot.note_count; ++i)
    if (snapshot.notes[i].kind > 3 || snapshot.notes[i].read > 1 ||
        !Terminated(snapshot.notes[i].title) ||
        !Terminated(snapshot.notes[i].text) ||
        !Terminated(snapshot.notes[i].icon_path))
      return false;
  for (uint32_t i = 0; i < snapshot.marker_count; ++i) {
    const MarkerState& marker = snapshot.markers[i];
    constexpr uint32_t kKnownMarkerFlags =
        kMarkerVisible | kMarkerDiscovered | kMarkerCanTravel | kMarkerDoor;
    if (marker.type > 14 || (marker.flags & ~kKnownMarkerFlags) ||
        !Terminated(marker.name) ||
        !std::isfinite(marker.x) ||
        !std::isfinite(marker.y) || !std::isfinite(marker.z))
      return false;
  }
  for (uint32_t i = 0; i < snapshot.station_count; ++i)
    if (!Terminated(snapshot.stations[i].name) ||
        !std::isfinite(snapshot.stations[i].signal))
      return false;
  for (uint32_t i = 0; i < snapshot.perk_count; ++i)
    if (!Terminated(snapshot.perks[i].name) ||
        !Terminated(snapshot.perks[i].description) ||
        !Terminated(snapshot.perks[i].icon_path))
      return false;
  return true;
}

}  // namespace

void ResetState(Lifecycle lifecycle, const char* build_label,
                bool supported_build) {
  AcquireSRWLockExclusive(&g_state_lock);
  const uint64_t next_sequence = g_have_state ? g_state.sequence + 1 : 1;
  const uint64_t next_epoch = g_have_state ? g_state.world_epoch + 1 : 1;
  // Snapshot is deliberately large. Value-assignment creates a full temporary
  // on the 32-bit Fallout stack and can overflow it as the ABI grows.
  SecureZeroMemory(&g_state, sizeof(g_state));
  g_state.size = sizeof(Snapshot);
  g_state.sequence = next_sequence;
  g_state.world_epoch = next_epoch;
  g_state.lifecycle = lifecycle;
  g_state.supported_build = supported_build ? 1u : 0u;
  CopyText(g_state.build_label, sizeof(g_state.build_label), build_label);
  g_have_state = true;
  g_published_sequence.store(g_state.sequence, std::memory_order_release);
  ReleaseSRWLockExclusive(&g_state_lock);
  RejectStaleCommands(next_epoch);
}

bool PublishSnapshot(const Snapshot& snapshot) {
  if (!ValidSnapshot(snapshot)) {
    LogRateLimited("state-invalid", 5000, LogLevel::kError,
                   "[STATE] rejected malformed snapshot seq=%llu",
                   static_cast<unsigned long long>(snapshot.sequence));
    return false;
  }
  bool accepted = false;
  AcquireSRWLockExclusive(&g_state_lock);
  if (!g_have_state ||
      (snapshot.world_epoch >= g_state.world_epoch &&
       snapshot.sequence > g_state.sequence)) {
    g_state = snapshot;
    g_have_state = true;
    g_published_sequence.store(snapshot.sequence, std::memory_order_release);
    accepted = true;
  }
  ReleaseSRWLockExclusive(&g_state_lock);
  if (!accepted)
    LogRateLimited("state-stale", 5000, LogLevel::kWarning,
                   "[STATE] rejected stale snapshot seq=%llu epoch=%llu",
                   static_cast<unsigned long long>(snapshot.sequence),
                   static_cast<unsigned long long>(snapshot.world_epoch));
  return accepted;
}

bool CopyLatestSnapshot(Snapshot* output) {
  if (!output) return false;
  AcquireSRWLockShared(&g_state_lock);
  const bool available = g_have_state;
  if (available) *output = g_state;
  ReleaseSRWLockShared(&g_state_lock);
  return available;
}

uint64_t LatestSequence() {
  return g_published_sequence.load(std::memory_order_acquire);
}

bool QueueCommand(const Command& input, uint64_t* assigned_request_id) {
  const bool needs_form = input.type != CommandType::kRadioOff;
  if (!ValidCommandType(input.type) || (needs_form && !input.form_id) ||
      !input.world_epoch || input.count != 1 || input.stack_condition != -1)
    return false;
  Command command = input;
  command.version = kSnapshotVersion;
  if (!command.request_id)
    command.request_id = g_next_request_id.fetch_add(1,
                                                     std::memory_order_relaxed);
  AcquireSRWLockExclusive(&g_command_lock);
  if (g_command_count == kCommandCapacity) {
    ReleaseSRWLockExclusive(&g_command_lock);
    LogRateLimited("command-full", 1000, LogLevel::kError,
                   "[ACTION] bounded command queue full; newest action rejected");
    return false;
  }
  g_commands[g_command_write] = command;
  g_command_write = (g_command_write + 1) % kCommandCapacity;
  ++g_command_count;
  const uint32_t depth = g_command_count;
  ReleaseSRWLockExclusive(&g_command_lock);
  if (assigned_request_id) *assigned_request_id = command.request_id;
  Log(LogLevel::kInfo,
      "[ACTION] queued request=%llu type=%u form=%08X epoch=%llu depth=%u",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type), command.form_id,
      static_cast<unsigned long long>(command.world_epoch), depth);
  return true;
}

bool PollCommand(Command* output) {
  if (!output) return false;
  AcquireSRWLockExclusive(&g_command_lock);
  const bool available = g_command_count != 0;
  if (available) {
    *output = g_commands[g_command_read];
    g_commands[g_command_read] = {};
    g_command_read = (g_command_read + 1) % kCommandCapacity;
    --g_command_count;
  }
  ReleaseSRWLockExclusive(&g_command_lock);
  return available;
}

void RejectStaleCommands(uint64_t world_epoch) {
  AcquireSRWLockExclusive(&g_command_lock);
  Command retained[kCommandCapacity] = {};
  uint32_t retained_count = 0;
  for (uint32_t index = 0; index < g_command_count; ++index) {
    const Command& command =
        g_commands[(g_command_read + index) % kCommandCapacity];
    if (command.world_epoch == world_epoch)
      retained[retained_count++] = command;
  }
  std::fill_n(g_commands, kCommandCapacity, Command{});
  for (uint32_t index = 0; index < retained_count; ++index)
    g_commands[index] = retained[index];
  g_command_read = 0;
  g_command_count = retained_count;
  g_command_write = retained_count % kCommandCapacity;
  ReleaseSRWLockExclusive(&g_command_lock);
}

uint32_t PendingCommandCount() {
  AcquireSRWLockShared(&g_command_lock);
  const uint32_t count = g_command_count;
  ReleaseSRWLockShared(&g_command_lock);
  return count;
}

}  // namespace fo3ds
