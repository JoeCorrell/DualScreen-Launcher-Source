#pragma once

#include <cstdint>
#include <type_traits>

namespace fo3ds {

constexpr uint32_t kSnapshotVersion = 0x00010005u;
constexpr uint32_t kMaxItems = 768;
constexpr uint32_t kMaxEffects = 96;
constexpr uint32_t kMaxQuests = 128;
constexpr uint32_t kMaxObjectives = 384;
constexpr uint32_t kMaxNotes = 192;
constexpr uint32_t kMaxMarkers = 384;
constexpr uint32_t kMaxStations = 48;
constexpr uint32_t kMaxPerks = 256;
constexpr uint32_t kSkillCount = 13;
constexpr uint32_t kSpecialCount = 7;
constexpr uint32_t kLimbCount = 6;
constexpr uint32_t kMiscStatCount = 26;

enum class Lifecycle : uint32_t {
  kBoot,
  kMainMenu,
  kLoading,
  kLive,
  kDead,
  kUnsupported,
};

enum class ItemCategory : uint8_t {
  kWeapon,
  kApparel,
  kAid,
  kMisc,
  kAmmo,
  kUnknown,
};

enum CapabilityFlags : uint32_t {
  kCapStats = 1u << 0,
  kCapInventory = 1u << 1,
  kCapQuests = 1u << 2,
  kCapMap = 1u << 3,
  kCapRadio = 1u << 4,
  kCapActions = 1u << 5,
  kCapEffects = 1u << 6,
  kCapNotes = 1u << 7,
  kCapPerks = 1u << 8,
  kCapGeneralStats = 1u << 9,
  kCapWorldMap = 1u << 10,
  kCapLocalMap = 1u << 11,
};

enum class ActionResult : uint32_t {
  kNone,
  kDispatched,
  kRejected,
};

enum ItemFlags : uint16_t {
  kItemEquipped = 1u << 0,
  kItemQuest = 1u << 1,
  kItemFavorite = 1u << 2,
  kItemPlayable = 1u << 3,
};

struct ItemState {
  uint32_t form_id = 0;
  uint32_t ammo_form_id = 0;
  int32_t count = 0;
  int32_t ammo_count = -1;
  int32_t value = -1;
  float weight = -1.0f;
  float condition = -1.0f;
  float damage = -1.0f;
  float armor = -1.0f;
  uint16_t flags = 0;
  ItemCategory category = ItemCategory::kUnknown;
  uint8_t reserved = 0;
  char name[96] = {};
  char description[192] = {};
  char icon_path[192] = {};
};

struct EffectState {
  uint32_t form_id = 0;
  float magnitude = 0.0f;
  float seconds_remaining = -1.0f;
  uint8_t detrimental = 0;
  char name[96] = {};
};

enum ObjectiveFlags : uint32_t {
  kObjectiveDisplayed = 1u << 0,
  kObjectiveCompleted = 1u << 1,
  kObjectiveFailed = 1u << 2,
};

struct ObjectiveState {
  uint32_t quest_form_id = 0;
  uint32_t objective_id = 0;
  uint32_t flags = 0;
  char text[224] = {};
};

enum QuestFlags : uint32_t {
  kQuestActive = 1u << 0,
  kQuestCompleted = 1u << 1,
  kQuestFailed = 1u << 2,
  kQuestTracked = 1u << 3,
};

struct QuestState {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  int32_t stage = 0;
  uint32_t objective_begin = 0;
  uint32_t objective_count = 0;
  char title[128] = {};
  char description[320] = {};
  char icon_path[192] = {};
};

struct NoteState {
  uint32_t form_id = 0;
  uint8_t kind = 0;
  uint8_t read = 0;
  uint16_t reserved = 0;
  char title[128] = {};
  char text[1536] = {};
  char icon_path[192] = {};
};

enum MarkerFlags : uint32_t {
  kMarkerVisible = 1u << 0,
  kMarkerDiscovered = 1u << 1,
  kMarkerCanTravel = 1u << 2,
  kMarkerDoor = 1u << 3,
};

struct MarkerState {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  uint16_t type = 0;
  uint16_t reserved = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  char name[96] = {};
};

enum StationFlags : uint32_t {
  kStationAvailable = 1u << 0,
  kStationTuned = 1u << 1,
};

struct RadioState {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  float signal = -1.0f;
  char name[96] = {};
};

struct PerkState {
  uint32_t form_id = 0;
  uint32_t rank = 0;
  char name[96] = {};
  char description[224] = {};
  char icon_path[192] = {};
};

struct Snapshot {
  uint32_t version = kSnapshotVersion;
  // Set by the producer once the complete object exists. Keeping this at zero
  // prevents a partially initialized snapshot from crossing the thread
  // boundary and avoids taking sizeof an incomplete type here.
  uint32_t size = 0;
  uint64_t sequence = 0;
  uint64_t world_epoch = 0;
  uint64_t vitals_revision = 0;
  uint64_t inventory_revision = 0;
  uint64_t data_revision = 0;
  uint64_t map_revision = 0;
  uint64_t radio_revision = 0;
  uint64_t action_revision = 0;
  uint64_t last_action_request_id = 0;
  uint32_t last_action_type = 0;
  uint32_t last_action_form_id = 0;
  ActionResult last_action_result = ActionResult::kNone;
  Lifecycle lifecycle = Lifecycle::kBoot;
  uint32_t supported_build = 0;
  uint32_t capabilities = 0;
  uint32_t player_form_id = 0;
  uint32_t world_form_id = 0;
  uint32_t cell_form_id = 0;
  int32_t level = -1;
  int32_t caps = -1;
  int32_t xp = -1;
  int32_t xp_next = -1;
  float health = -1.0f;
  float max_health = -1.0f;
  float action_points = -1.0f;
  float max_action_points = -1.0f;
  float damage_resistance = -1.0f;
  float radiation_resistance = -1.0f;
  float radiation = -1.0f;
  float carry_weight = -1.0f;
  float max_carry_weight = -1.0f;
  float karma = 0.0f;
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_z = 0.0f;
  float player_heading = 0.0f;
  float map_min_x = 0.0f;
  float map_max_y = 0.0f;
  float map_max_x = 0.0f;
  float map_min_y = 0.0f;
  uint32_t map_usable_width = 0;
  uint32_t map_usable_height = 0;
  float special[kSpecialCount] = {};
  float skills[kSkillCount] = {};
  float limbs[kLimbCount] = {};
  int32_t misc_stats[kMiscStatCount] = {};
  char player_name[96] = {};
  char location[128] = {};
  char map_texture_path[192] = {};
  char build_label[96] = {};
  uint32_t item_count = 0;
  uint32_t effect_count = 0;
  uint32_t quest_count = 0;
  uint32_t objective_count = 0;
  uint32_t note_count = 0;
  uint32_t marker_count = 0;
  uint32_t station_count = 0;
  uint32_t perk_count = 0;
  ItemState items[kMaxItems] = {};
  EffectState effects[kMaxEffects] = {};
  QuestState quests[kMaxQuests] = {};
  ObjectiveState objectives[kMaxObjectives] = {};
  NoteState notes[kMaxNotes] = {};
  MarkerState markers[kMaxMarkers] = {};
  RadioState stations[kMaxStations] = {};
  PerkState perks[kMaxPerks] = {};
};

enum class CommandType : uint32_t {
  kNone,
  kEquipItem,
  kUnequipItem,
  kUseItem,
  kDropItem,
  kTrackQuest,
  kTuneRadio,
  kRadioOff,
  kFastTravel,
};

struct Command {
  uint32_t version = kSnapshotVersion;
  CommandType type = CommandType::kNone;
  uint64_t world_epoch = 0;
  uint64_t request_id = 0;
  uint32_t form_id = 0;
  int32_t stack_condition = -1;
  int32_t count = 1;
};

static_assert(std::is_trivially_copyable<Snapshot>::value,
              "Snapshots cross threads by bounded copy");
static_assert(std::is_trivially_copyable<Command>::value,
              "Commands cross threads by bounded copy");

}  // namespace fo3ds
