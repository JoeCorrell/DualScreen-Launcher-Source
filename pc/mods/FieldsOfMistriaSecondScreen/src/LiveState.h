#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

#include "ActionTransport.h"

namespace fomds {

// Schema 1 is deliberately bounded. A malformed bridge file can therefore
// never make the native panel allocate in proportion to attacker-controlled
// counts or string lengths.
constexpr std::size_t kLiveTopicCount = 20;
constexpr std::size_t kMaxInventorySlots = 96;
constexpr std::size_t kMaxArmorSlots = 24;
constexpr std::size_t kMaxItemRequiredTags = 16;
constexpr std::size_t kMaxItemTags = 32;
constexpr std::size_t kMaxSkills = 16;
constexpr std::size_t kMaxPerks = 192;
constexpr std::size_t kMaxStatusEffects = 64;
constexpr std::size_t kMaxQuests = 96;
constexpr std::size_t kMaxVillagers = 128;
constexpr std::size_t kMaxLivestock = 128;
constexpr std::size_t kMaxSpells = 32;
constexpr std::size_t kMaxSpellCooldowns = 16;
constexpr std::size_t kMaxAlmanacCategories = 32;
constexpr std::size_t kMaxCollectionItems = 1024;
constexpr std::size_t kMaxMuseumWings = 16;
constexpr std::size_t kMaxMuseumSets = 128;
constexpr std::size_t kMaxRenownRewards = 128;
constexpr std::size_t kMaxCalendarEvents = 512;
constexpr std::size_t kMaxMapPages = 16;
constexpr std::size_t kMaxMapHubs = 512;
constexpr std::size_t kMaxMapHubLocations = 1024;
constexpr std::size_t kMaxMapMarkers = 1024;
constexpr std::size_t kMaxQuestRequirements = 1024;
constexpr std::size_t kMaxQuestTargets = 512;
constexpr std::size_t kMaxQuestRewards = 512;

enum class LiveTopic : std::uint8_t {
  kSession,
  kStatus,
  kProgression,
  kInventory,
  kArmor,
  kHotbar,
  kLocation,
  kPlayer,
  kMap,
  kVillagers,
  kAnimals,
  kMagic,
  kAlmanac,
  kMuseum,
  kRenown,
  kCalendarEvents,
  kCalendar,
  kClock,
  kWeather,
  kQuests,
  kCount,
};

static_assert(static_cast<std::size_t>(LiveTopic::kCount) == kLiveTopicCount);
static_assert(kLiveTopicCount < 32, "present_topics uses a 32-bit mask");
// ActionTransport sessions depend on these revisions. The finalized bridge
// inserts read-only topics later in the order, but these two established
// action-bearing indexes are protocol invariants.
static_assert(static_cast<std::size_t>(LiveTopic::kInventory) == 3);
static_assert(static_cast<std::size_t>(LiveTopic::kArmor) == 4);
static_assert(static_cast<std::size_t>(LiveTopic::kHotbar) == 5);
static_assert(static_cast<std::size_t>(LiveTopic::kMap) == 8);
static_assert(static_cast<std::size_t>(LiveTopic::kMagic) == 11);

template <std::size_t N>
struct FixedUtf8 {
  char value[N] = {};
  bool truncated = false;
};

struct OptionalNumber {
  bool present = false;
  double value = 0.0;
};

struct OptionalInteger {
  bool present = false;
  std::int64_t value = 0;
};

struct OptionalBoolean {
  bool present = false;
  bool value = false;
};

template <std::size_t N>
struct OptionalText {
  bool present = false;
  FixedUtf8<N> text;
};

struct CapabilityState {
  bool select_hotbar = false;
  bool swap_slots = false;
  bool consume_item = false;
  bool equip_armor = false;
  bool unequip_armor = false;
  bool pin_spell = false;
  bool unpin_spell = false;
  bool cast_pinned_spell = false;
  // The protocol field is generic_item_use. The shorter member name is kept
  // for source compatibility with the first schema-1 native panel.
  bool item_use = false;
  bool drop_item = false;
  bool quest_mutation = false;
  bool quest_tracking = false;
};

struct SessionTopic {
  bool in_game = false;
  FixedUtf8<32> game_version;
  FixedUtf8<96> game_identifier;
  CapabilityState capabilities;
};

struct StatusTopic {
  FixedUtf8<96> name;
  FixedUtf8<96> farm_name;
  double health = 0.0;
  double health_max = 0.0;
  double stamina = 0.0;
  double stamina_max = 0.0;
  double mana = 0.0;
  double mana_max = 0.0;
  double tesserae = 0.0;
  double essence = 0.0;
  double renown = 0.0;
};

struct SkillState {
  FixedUtf8<64> id;
  double xp = 0.0;
  std::int64_t level = 0;
  FixedUtf8<128> icon_asset;
};

struct PerkState {
  FixedUtf8<96> id;
  bool active = false;
};

struct StatusEffectState {
  std::uint32_t source_index = 0;
  FixedUtf8<96> type;
  double amount = 0.0;
  double start = 0.0;
  double finish = 0.0;
  std::int64_t stacks = 0;
  double last_update = 0.0;
};

struct ProgressionTopic {
  std::uint32_t skill_count = 0;
  SkillState skills[kMaxSkills] = {};
  std::uint32_t perk_count = 0;
  PerkState perks[kMaxPerks] = {};
  std::uint32_t status_effect_count = 0;
  StatusEffectState status_effects[kMaxStatusEffects] = {};
};

struct ItemMetadata {
  FixedUtf8<128> item_id;
  OptionalText<128> inner_item;
  OptionalNumber gold_to_gain;
  OptionalText<128> cosmetic;
  bool animal_cosmetic_present = false;
  FixedUtf8<64> animal_kind;
  FixedUtf8<128> animal_cosmetic;
  OptionalText<64> infusion;
  bool auto_use = false;
  OptionalText<128> pet_cosmetic_set_name;
  OptionalInteger date_photo;
};

struct ItemSlotState {
  std::int64_t index = -1;
  bool empty = true;
  double count = 0.0;
  bool equipped = false;
  std::uint32_t required_tag_count = 0;
  FixedUtf8<96> required_tags[kMaxItemRequiredTags] = {};
  FixedUtf8<128> item_id;
  FixedUtf8<192> display_name;
  FixedUtf8<768> description;
  ItemMetadata instance_metadata;
  FixedUtf8<2048> instance_fingerprint;
  OptionalInteger use;
  OptionalText<96> use_name;
  bool can_consume_from_bridge = false;
  FixedUtf8<64> ui_category;
  std::uint32_t tag_count = 0;
  FixedUtf8<96> tags[kMaxItemTags] = {};
  OptionalText<128> icon_asset;
  std::int64_t icon_frame = 0;
  OptionalText<128> sub_icon_asset;
  bool icon_lut_present = false;
  FixedUtf8<128> icon_lut_asset;
  std::int64_t icon_lut_index = 0;
  bool museum_eligible = false;
  bool museum_donated = false;
  OptionalNumber max_stack;
  OptionalNumber damage;
  OptionalNumber defense;
  OptionalNumber stamina_modifier;
  OptionalNumber max_stamina_modifier;
  OptionalNumber health_modifier;
  OptionalNumber max_health_modifier;
  OptionalNumber mana_modifier;
  OptionalNumber quality;
  OptionalNumber stars;
  OptionalNumber value_bin;
  OptionalNumber value_store;
};

template <std::size_t N>
struct InventoryTopic {
  std::uint32_t declared_size = 0;
  std::uint32_t slot_count = 0;
  ItemSlotState slots[N] = {};
};

struct HotbarTopic {
  std::int64_t selected_index = -1;
};

struct LocationTopic {
  FixedUtf8<96> id;
  std::int64_t dynamic_index = 0;
  FixedUtf8<128> room;
  FixedUtf8<192> diagnostic_name;
  FixedUtf8<192> display_name;
  bool outdoor = false;
};

struct LivePosition {
  bool present = false;
  FixedUtf8<96> id;
  OptionalInteger dynamic_index;
  OptionalNumber x;
  OptionalNumber y;
};

struct MineState {
  bool active = false;
  OptionalInteger floor_index;
  OptionalInteger display_floor;
  OptionalInteger biome_id;
  OptionalText<192> biome_name;
};

struct PlayerTopic {
  double x = 0.0;
  double y = 0.0;
  double facing_degrees = 0.0;
  FixedUtf8<32> facing;
  FixedUtf8<32> context;
  MineState mine;
};

struct MapMarkerState {
  FixedUtf8<32> kind;
  OptionalText<96> npc_id;
  FixedUtf8<128> icon_asset;
  std::int64_t hub_index = 0;
  double offset_x = 0.0;
  double offset_y = 0.0;
};

struct MapHubState {
  FixedUtf8<128> id;
  std::int64_t index = 0;
  FixedUtf8<32> type;
  double world_x = 0.0;
  double world_y = 0.0;
  double map_x = 0.0;
  double map_y = 0.0;
  std::uint32_t first_location = 0;
  std::uint32_t location_count = 0;
  std::uint32_t first_marker = 0;
  std::uint32_t marker_count = 0;
};

struct MapPageState {
  FixedUtf8<96> id;
  FixedUtf8<192> display_name;
  bool locked = false;
  FixedUtf8<128> map_asset;
  FixedUtf8<128> tab_icon_asset;
  std::uint32_t first_hub = 0;
  std::uint32_t hub_count = 0;
  OptionalText<128> player_hub_id;
};

struct MapTopic {
  FixedUtf8<96> selected_map_id;
  FixedUtf8<96> base_map_id;
  std::uint32_t page_count = 0;
  MapPageState pages[kMaxMapPages] = {};
  std::uint32_t hub_count = 0;
  MapHubState hubs[kMaxMapHubs] = {};
  std::uint32_t hub_location_count = 0;
  FixedUtf8<96> hub_locations[kMaxMapHubLocations] = {};
  std::uint32_t marker_count = 0;
  MapMarkerState markers[kMaxMapMarkers] = {};
  bool authored_coordinates = false;
  bool continuous_world_projection_supported = false;
};

struct VillagerState {
  FixedUtf8<96> id;
  FixedUtf8<192> display_name;
  FixedUtf8<128> icon_asset;
  OptionalText<128> portrait_asset;
  double portrait_offset[2] = {};
  std::int64_t portrait_background_rgb[3] = {};
  double heart_points = 0.0;
  std::int64_t heart_level = 0;
  FixedUtf8<32> relationship;
  bool talked_today = false;
  bool gifted_today = false;
  LivePosition location;
};

struct VillagersTopic {
  std::uint32_t villager_count = 0;
  VillagerState villagers[kMaxVillagers] = {};
  std::uint32_t declared_count = 0;
  FixedUtf8<64> discovery_policy;
  bool gift_preferences_exposed = false;
};

struct AnimalAttention {
  bool active = false;
  OptionalBoolean fed_today;
  OptionalBoolean petted_today;
  bool needs_food = false;
  bool needs_pet = false;
  bool needed = false;
};

struct LivestockState {
  std::int64_t id = 0;
  FixedUtf8<32> category;
  FixedUtf8<128> name;
  FixedUtf8<64> kind;
  FixedUtf8<128> kind_name;
  FixedUtf8<96> variant;
  OptionalText<128> icon_asset;
  OptionalText<128> icon_lut_asset;
  OptionalInteger icon_lut_index;
  FixedUtf8<32> sex;
  bool baby = false;
  std::int64_t days_old = 0;
  double heart_points = 0.0;
  std::int64_t heart_level = 0;
  bool in_daycare = false;
  bool at_home = false;
  bool outside_today = false;
  bool incubating = false;
  LivePosition location;
  AnimalAttention attention;
};

struct PetState {
  bool present = false;
  std::int64_t id = 0;
  FixedUtf8<32> category;
  FixedUtf8<128> name;
  FixedUtf8<96> variant;
  OptionalText<128> icon_asset;
  OptionalText<128> map_icon_asset;
  OptionalText<128> icon_lut_asset;
  OptionalInteger icon_lut_index;
  std::int64_t kind_id = 0;
  FixedUtf8<32> sex;
  double heart_points = 0.0;
  std::int64_t heart_level = 0;
  LivePosition location;
  AnimalAttention attention;
};

struct AnimalsTopic {
  std::uint32_t livestock_count = 0;
  LivestockState livestock[kMaxLivestock] = {};
  std::uint32_t declared_livestock_count = 0;
  PetState pet;
};

struct SpellState {
  std::int64_t index = -1;
  FixedUtf8<96> id;
  FixedUtf8<192> display_name;
  FixedUtf8<128> type;
  FixedUtf8<768> description;
  OptionalText<128> upper_icon_asset;
  FixedUtf8<128> lower_icon_key;
  OptionalText<128> lower_icon_asset;
  OptionalText<128> ribbon_asset;
  double mana_cost = 0.0;
  std::int64_t display_cost_orbs = 0;
};

struct SpellCooldownState {
  std::int64_t index = -1;
  FixedUtf8<96> id;
  double remaining_frames = 0.0;
  bool ready = false;
};

struct MagicTopic {
  std::uint32_t spell_count = 0;
  SpellState spells[kMaxSpells] = {};
  std::uint32_t unlocked_count = 0;
  OptionalText<96> pinned_spell;
  OptionalInteger pinned_spell_index;
  std::uint32_t cooldown_count = 0;
  SpellCooldownState cooldowns[kMaxSpellCooldowns] = {};
  bool actions_supported = false;
  bool supports_pin_spell = false;
  bool supports_unpin_spell = false;
  bool supports_cast_pinned_spell = false;
};

struct CollectionItemState {
  FixedUtf8<128> id;
  FixedUtf8<192> display_name;
  FixedUtf8<768> description;
  OptionalText<128> icon_asset;
  std::int64_t icon_frame = 0;
  bool donated = false;
};

struct AlmanacCategoryState {
  FixedUtf8<96> key;
  FixedUtf8<192> display_name;
  std::uint32_t discovered_count = 0;
  std::uint32_t total_count = 0;
  std::uint32_t first_item = 0;
  std::uint32_t item_count = 0;
};

struct AlmanacTopic {
  std::uint32_t category_count = 0;
  AlmanacCategoryState categories[kMaxAlmanacCategories] = {};
  std::uint32_t item_count = 0;
  CollectionItemState discovered_items[kMaxCollectionItems] = {};
  std::uint32_t discovered_count = 0;
  std::uint32_t total_count = 0;
  bool unknown_entries_exposed = false;
};

struct MuseumSetState {
  FixedUtf8<96> key;
  FixedUtf8<192> display_name;
  FixedUtf8<768> description;
  std::uint32_t discovered_count = 0;
  std::uint32_t donated_count = 0;
  std::uint32_t total_count = 0;
  bool complete = false;
  std::uint32_t first_item = 0;
  std::uint32_t item_count = 0;
};

struct MuseumWingState {
  FixedUtf8<96> id;
  FixedUtf8<192> display_name;
  std::uint32_t discovered_count = 0;
  std::uint32_t donated_count = 0;
  std::uint32_t total_count = 0;
  std::uint32_t first_set = 0;
  std::uint32_t set_count = 0;
};

struct MuseumTopic {
  std::uint32_t wing_count = 0;
  MuseumWingState wings[kMaxMuseumWings] = {};
  std::uint32_t set_count = 0;
  MuseumSetState sets[kMaxMuseumSets] = {};
  std::uint32_t item_count = 0;
  CollectionItemState known_items[kMaxCollectionItems] = {};
  std::uint32_t discovered_count = 0;
  std::uint32_t donated_count = 0;
  std::uint32_t total_count = 0;
  bool unknown_entries_exposed = false;
  bool future_rewards_exposed = false;
};

struct RenownRankState {
  std::int64_t rank_index = 0;
  std::int64_t level = 0;
  FixedUtf8<192> display_name;
};

struct RenownTopic {
  double total = 0.0;
  double pending = 0.0;
  double projected_total = 0.0;
  std::int64_t projected_level = 0;
  std::int64_t level = 0;
  std::int64_t max_level = 0;
  bool at_max = false;
  double level_start_total = 0.0;
  double next_level_total = 0.0;
  double progress = 0.0;
  double progress_required = 0.0;
  double progress_ratio = 0.0;
  RenownRankState rank;
  bool next_rank_present = false;
  RenownRankState next_rank;
  InventoryTopic<kMaxRenownRewards> claimable_rewards;
  bool future_rewards_exposed = false;
  bool actions_supported = false;
};

struct CalendarEventState {
  FixedUtf8<32> type;
  FixedUtf8<128> id;
  FixedUtf8<256> display_name;
  std::int64_t days_from_now = 0;
  double raw_date = 0.0;
  std::int64_t year = 0;
  std::int64_t display_year = 0;
  FixedUtf8<32> season;
  std::int64_t season_index = 0;
  std::int64_t day = 0;
  std::int64_t display_day = 0;
  std::int64_t weekday = 0;
};

struct CalendarEventsTopic {
  std::int64_t horizon_days = 0;
  std::uint32_t event_count = 0;
  CalendarEventState events[kMaxCalendarEvents] = {};
  std::uint32_t declared_count = 0;
  bool private_date_availability_exposed = false;
  bool unreleased_festivals_exposed = false;
};

struct CalendarTopic {
  double raw = 0.0;
  std::int64_t year = 0;
  std::int64_t display_year = 0;
  FixedUtf8<32> season;
  std::int64_t season_index = 0;
  std::int64_t day = 0;
  std::int64_t display_day = 0;
  std::int64_t weekday = 0;
  double total_days = 0.0;
};

struct ClockTopic {
  double raw = 0.0;
  std::int64_t hour = 0;
  std::int64_t minute = 0;
  FixedUtf8<32> formatted;
  bool is_day = false;
  bool is_night = false;
};

struct WeatherTopic {
  FixedUtf8<64> current;
  std::int64_t current_id = 0;
  FixedUtf8<64> tomorrow;
  std::int64_t tomorrow_id = 0;
  bool inclement = false;
  bool under_spell = false;
};

struct QuestRequirementState {
  FixedUtf8<32> type;
  OptionalText<32> source;
  LivePosition source_location;
  OptionalText<128> item_id;
  OptionalText<128> tag;
  OptionalInteger monster_id;
  OptionalText<192> display_name;
  OptionalText<128> icon_asset;
  OptionalNumber required;
  OptionalNumber current;
  OptionalBoolean complete;
  OptionalBoolean identities_exposed;
};

struct QuestTargetState {
  FixedUtf8<32> type;
  OptionalText<96> npc_id;
  OptionalText<96> location_id;
  LivePosition location;
};

struct QuestRewardState {
  FixedUtf8<32> type;
  OptionalText<128> item_id;
  OptionalText<192> display_name;
  OptionalText<128> icon_asset;
  double amount = 0.0;
  bool instance_metadata_present = false;
  ItemMetadata instance_metadata;
};

struct QuestNpcIconState {
  FixedUtf8<96> id;
  FixedUtf8<128> icon_asset;
};

struct QuestState {
  FixedUtf8<128> id;
  FixedUtf8<192> name;
  FixedUtf8<768> description;
  std::int64_t category = 0;
  std::int64_t current_stage = 0;
  std::int64_t stage_count = 0;
  FixedUtf8<512> objective;
  std::uint32_t first_requirement = 0;
  std::uint32_t requirement_count = 0;
  std::uint32_t first_target = 0;
  std::uint32_t target_count = 0;
  bool ready = false;
  QuestNpcIconState npc_for_icon;
  std::uint32_t first_reward = 0;
  std::uint32_t reward_count = 0;
};

struct QuestsTopic {
  std::uint32_t quest_count = 0;
  QuestState active[kMaxQuests] = {};
  std::uint32_t requirement_count = 0;
  QuestRequirementState requirements[kMaxQuestRequirements] = {};
  std::uint32_t target_count = 0;
  QuestTargetState targets[kMaxQuestTargets] = {};
  std::uint32_t reward_count = 0;
  QuestRewardState rewards[kMaxQuestRewards] = {};
  std::uint32_t active_count = 0;
  std::uint32_t completed_count = 0;
  bool tracked_supported = false;
};

struct LiveStateSnapshot {
  std::uint32_t schema = 0;
  FixedUtf8<128> epoch;
  std::uint64_t sequence = 0;
  double generated_tick = 0.0;
  std::uint64_t revisions[kLiveTopicCount] = {};
  std::uint32_t present_topics = 0;
  bool any_text_truncated = false;

  SessionTopic session;
  StatusTopic status;
  ProgressionTopic progression;
  InventoryTopic<kMaxInventorySlots> inventory;
  InventoryTopic<kMaxArmorSlots> armor;
  HotbarTopic hotbar;
  LocationTopic location;
  PlayerTopic player;
  MapTopic map;
  VillagersTopic villagers;
  AnimalsTopic animals;
  CalendarTopic calendar;
  ClockTopic clock;
  WeatherTopic weather;
  QuestsTopic quests;
  MagicTopic magic;
  AlmanacTopic almanac;
  MuseumTopic museum;
  RenownTopic renown;
  CalendarEventsTopic calendar_events;
};

enum class LivePollResult {
  kNoFile,
  kNoChange,
  kPublished,
  kNeedsResync,
  kInvalidState,
  kIoError,
};

class LiveStateIngestor {
 public:
  // config_directory is CONFIG_DIRECTORY, not its "dualscreen" child. Passing
  // an empty path enables bounded LOCALAPPDATA discovery. The
  // FOMDS_CONFIG_DIRECTORY environment variable takes priority when present.
  explicit LiveStateIngestor(const wchar_t* config_directory = nullptr);
  ~LiveStateIngestor();

  LiveStateIngestor(const LiveStateIngestor&) = delete;
  LiveStateIngestor& operator=(const LiveStateIngestor&) = delete;

  LivePollResult Poll();
  // Reads the bridge-owned ack.json without claiming, moving, or deleting it.
  // kPublished means output contains a fully validated acknowledgement for the
  // currently cached epoch. Unchanged acknowledgements return kNoChange.
  LivePollResult PollAcknowledgement(BridgeActionAck* output);
  bool CopyLatest(LiveStateSnapshot* output) const;
  bool NeedsResync() const;
  bool CopyStatePath(wchar_t* output, std::size_t capacity) const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

const char* LivePollResultName(LivePollResult result);

}  // namespace fomds
