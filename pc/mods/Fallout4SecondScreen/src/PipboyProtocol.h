#pragma once

#include <winsock2.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace fo4ds {

enum class PipValueType : uint8_t {
  kBoolean = 0,
  kInt8 = 1,
  kUInt8 = 2,
  kInt32 = 3,
  kUInt32 = 4,
  kFloat = 5,
  kString = 6,
  kArray = 7,
  kObject = 8,
};

struct PipNode {
  PipValueType type = PipValueType::kBoolean;
  bool boolean = false;
  int64_t integer = 0;
  float number = 0.0f;
  std::string string;
  std::vector<uint32_t> array;
  std::vector<std::pair<std::string, uint32_t>> object;
};

struct ItemCardField {
  std::string label;
  std::string text_value;
  float number_value = 0.0f;
  int value_type = 0;
  int damage_type = 0;
  float duration = -1.0f;
  bool show_as_percent = false;
  bool scale_with_duration = false;
};

struct InventoryComponentState {
  std::string name;
  int32_t count = 0;
  bool tagged_for_search = false;
};

struct InventoryItemState {
  uint32_t node_id = 0;
  uint32_t form_id = 0;
  uint64_t handle_id = 0;
  int32_t stack_id = 0;
  int32_t count = 0;
  int32_t equip_state = 0;
  int32_t favorite = -2;
  uint32_t filter_flag = 0;
  bool can_favorite = false;
  bool legendary = false;
  bool power_armor = false;
  std::string name;
  std::vector<ItemCardField> card;
  std::vector<InventoryComponentState> components;
};

struct QuestObjectiveState {
  uint32_t node_id = 0;
  uint32_t form_id = 0;
  int32_t instance = 0;
  bool active = false;
  std::string text;
  bool enabled = false;
  bool completed = false;
  bool failed = false;
};

struct QuestState {
  uint32_t node_id = 0;
  uint32_t form_id = 0;
  int32_t instance = 0;
  int32_t type = 0;
  bool active = false;
  bool enabled = false;
  std::string name;
  std::string description;
  std::vector<QuestObjectiveState> objectives;
};

struct MapMarkerState {
  enum class Kind : uint8_t {
    kLocation,
    kQuest,
    kCustom,
    kPowerArmor,
    kDoor,
  };

  uint32_t node_id = 0;
  uint32_t form_id = 0;
  int32_t type = 0;
  Kind kind = Kind::kLocation;
  bool local = false;
  bool on_door = false;
  bool visible = false;
  bool discovered = false;
  bool cleared = false;
  float x = 0.0f;
  float y = 0.0f;
  float height = 0.0f;
  std::string name;
};

struct RadioState {
  uint32_t node_id = 0;
  bool active = false;
  bool in_range = false;
  float frequency = 0.0f;
  std::string name;
};

struct PipPerkState {
  uint32_t node_id = 0;
  uint32_t form_id = 0;
  uint32_t clip_name = 0;
  uint32_t rank = 0;
  uint32_t max_rank = 0;
  std::string name;
  std::string description;
  std::string swf_file;
};

struct PipGeneralStatState {
  uint32_t node_id = 0;
  float value = 0.0f;
  bool show_if_zero = false;
  std::string category;
  std::string name;
};

struct EffectState {
  uint32_t node_id = 0;
  uint32_t type = 0;
  std::string name;
  std::string source;
  float magnitude = 0.0f;
  float duration = -1.0f;
  bool show_as_percent = false;
  bool custom_description = false;
  bool detrimental = false;
};

struct PipboySnapshot {
  uint64_t generation = 0;
  uint64_t revision = 0;
  std::array<uint64_t, 5> domain_revisions = {};
  bool connected = false;
  bool busy = false;
  bool root_ready = false;
  bool stats_ready = false;
  bool inventory_ready = false;
  bool data_ready = false;
  bool map_ready = false;
  bool radio_ready = false;
  bool effects_ready = false;
  bool perks_ready = false;
  bool general_stats_ready = false;
  bool status_ready = false;
  std::string server_identity;
  std::string status;

  std::string player_name;
  float hp = 0.0f;
  float max_hp = 0.0f;
  float ap = 0.0f;
  float max_ap = 0.0f;
  float carry_weight = 0.0f;
  float max_weight = 0.0f;
  uint32_t caps = 0;
  uint32_t level = 0;
  float xp_progress = 0.0f;
  uint32_t perk_points = 0;
  std::array<int32_t, 7> special = {};
  std::array<int32_t, 7> special_modifiers = {};
  std::array<std::string, 7> special_names = {};
  std::array<std::string, 7> special_descriptions = {};
  std::array<float, 6> limb_condition = {};
  std::array<float, 6> total_damage = {};
  std::array<float, 6> total_resistance = {};
  uint32_t body_flags = 0;
  uint32_t head_flags = 0;
  uint32_t stimpaks = 0;
  uint32_t radaway = 0;
  std::vector<PipPerkState> perks;
  std::vector<PipGeneralStatState> general_stats;

  uint32_t inventory_version = 0;
  bool inventory_initialized = false;
  uint64_t inventory_structure_revision = 0;
  uint64_t inventory_change_serial = 0;
  uint64_t inventory_full_rebuilds = 0;
  uint64_t inventory_incremental_refreshes = 0;
  std::vector<InventoryItemState> items;
  std::vector<QuestState> quests;
  std::vector<MapMarkerState> markers;
  std::vector<RadioState> radio;
  std::vector<EffectState> effects;

  std::string current_cell;
  std::string current_worldspace;
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_rotation = 0.0f;
  bool local_player_valid = false;
  float local_player_x = 0.0f;
  float local_player_y = 0.0f;
  float local_player_rotation = 0.0f;
  uint32_t workshop_count = 0;
  bool is_loading = false;
  bool is_player_dead = false;
  bool is_in_dialogue = false;
  bool is_menu_open = false;
  bool is_movement_locked = false;
  bool is_pipboy_locked = false;
  bool is_data_unavailable = false;
  bool is_in_animation = false;
  bool is_in_vats = false;
  bool is_in_vats_playback = false;
  bool is_pipboy_not_equipped = false;
  bool ActionsSafe() const {
    return connected && root_ready && status_ready && !busy && !is_loading &&
           !is_player_dead && !is_in_dialogue && !is_menu_open &&
           !is_movement_locked && !is_pipboy_locked &&
           !is_data_unavailable && !is_in_animation && !is_in_vats &&
           !is_in_vats_playback && !is_pipboy_not_equipped;
  }
};

struct LocalMapState {
  uint64_t revision = 0;
  uint32_t width = 0;
  uint32_t height = 0;
  float northwest_x = 0.0f;
  float northwest_y = 0.0f;
  float northeast_x = 0.0f;
  float northeast_y = 0.0f;
  float southwest_x = 0.0f;
  float southwest_y = 0.0f;
  std::vector<uint8_t> pixels;
};

class PipboyDatabase {
 public:
  void SetConnection(bool connected, bool busy, const std::string& identity,
                     const std::string& status);
  void Reset(const std::string& reason);
  void InvalidateTree(const std::string& reason);
  bool ApplyDataUpdate(const uint8_t* data, size_t size, std::string* error);
  bool ApplyLocalMap(const uint8_t* data, size_t size, std::string* error);
  PipboySnapshot Capture() const;
  bool CaptureIfChanged(uint64_t revision, PipboySnapshot* snapshot) const;
  LocalMapState CaptureLocalMap() const;
  size_t NodeCount() const;
  size_t RetainedBytes() const;
  // The in-process bridge owns a complete authoritative tree and may discard
  // protocol nodes no longer reachable from root. Socket clients do not call
  // this because their protocol permits temporarily unattached values.
  size_t PruneUnreachableNodes();

 private:
  const PipNode* Child(const PipNode* object, const char* key) const;
  const PipNode* Resolve(uint32_t id) const;
  const PipNode* RootBranch(const char* key) const;
  bool Bool(const PipNode* object, const char* key, bool fallback = false) const;
  int64_t Int(const PipNode* object, const char* key, int64_t fallback = 0) const;
  float Float(const PipNode* object, const char* key, float fallback = 0.0f) const;
  std::string String(const PipNode* object, const char* key) const;
  void BuildSnapshotLocked(PipboySnapshot* out) const;
  void AddParent(uint32_t child, uint32_t parent);
  void RemoveParent(uint32_t child, uint32_t parent);
  uint32_t ClassifyDomain(uint32_t id) const;
  uint32_t FindInventoryItemAncestor(uint32_t id) const;
  bool IsInventoryMetadataNode(uint32_t id) const;
  bool BuildInventoryItem(uint32_t id, InventoryItemState* item) const;

  mutable std::mutex mutex_;
  std::unordered_map<uint32_t, PipNode> nodes_;
  std::unordered_map<uint32_t, std::vector<uint32_t>> parents_;
  LocalMapState local_map_;
  size_t node_bytes_ = 0;
  uint64_t generation_ = 0;
  uint64_t revision_ = 0;
  std::array<uint64_t, 5> domain_revisions_ = {};
  struct InventoryChange {
    uint64_t serial = 0;
    uint32_t item_id = 0;
  };
  uint64_t inventory_structure_revision_ = 0;
  uint64_t inventory_change_serial_ = 0;
  std::vector<InventoryChange> inventory_changes_;
  bool connected_ = false;
  bool busy_ = false;
  std::string identity_;
  std::string status_ = "Waiting for Fallout 4";
};

class PipboyClient {
 public:
  explicit PipboyClient(PipboyDatabase* database, uint16_t port = 27000);
  ~PipboyClient();
  bool Start();
  void Stop();
  bool SendCommand(uint32_t type, const std::string& json_args);
  void DiscardCommands(const char* reason);

 private:
  void Run();
  bool Connect();
  bool ReceiveOne();
  bool SendFrame(uint8_t type, const void* data, uint32_t size);
  bool ReceiveExact(void* data, size_t size);
  void Close(const char* reason);
  void DrainCommands();

  struct QueuedCommand {
    uint64_t epoch = 0;
    std::string json;
  };

  PipboyDatabase* database_ = nullptr;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::atomic<SOCKET> socket_{INVALID_SOCKET};
  uint16_t port_ = 27000;
  std::mutex command_mutex_;
  std::vector<QueuedCommand> commands_;
  uint64_t command_epoch_ = 1;
  std::atomic<uint32_t> next_command_id_{1};
};

}  // namespace fo4ds
