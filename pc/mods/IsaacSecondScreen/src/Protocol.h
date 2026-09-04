#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace isaacds {

struct Consumables {
  int coins = 0;
  int bombs = 0;
  int keys = 0;
};

struct Health {
  int hearts = 0;
  int max_hearts = 0;
  int soul_hearts = 0;
  uint64_t black_heart_mask = 0;
  int eternal_hearts = 0;
  int golden_hearts = 0;
  int bone_hearts = 0;
  int rotten_hearts = 0;
  int broken_hearts = 0;
};

struct Stats {
  double damage = 0.0;
  double tears = 0.0;
  double range = 0.0;
  double shot_speed = 0.0;
  double move_speed = 0.0;
  double luck = 0.0;
};

struct Room {
  int grid = -1;
  int safe_grid = -1;
  int display_flags = 0;
  int visited = 0;
  int type = 1;
  int shape = 1;
  int subtype = 0;
};

struct MapState {
  std::string floor;
  int stage = 0;
  int stage_type = 0;
  int curses = 0;
  int current_grid = -1;
  uint32_t dungeon_seed = 0;
  std::vector<Room> rooms;
  bool received = false;
};

struct CollectedItem {
  int id = 0;
  int count = 0;
  std::string name;
  std::string description;
  int quality = -1;
};

struct ActiveItem {
  int id = 0;
  int slot = 0;
  int charge = 0;
  int battery_charge = 0;
};

struct Trinket {
  int id = 0;
  int slot = 0;
  bool golden = false;
};

enum class PocketKind : uint8_t { kCard, kPill };

struct PocketItem {
  PocketKind kind = PocketKind::kCard;
  int id = 0;
  int color = 0;
  int slot = 0;
  std::string name;
  std::string hud_animation;
};

struct InventoryState {
  std::vector<CollectedItem> collectibles;
  std::vector<ActiveItem> actives;
  std::vector<Trinket> trinkets;
  std::vector<PocketItem> pockets;
  int active_capacity = 1;
  int trinket_capacity = 1;
  int pocket_capacity = 1;
  bool received = false;
};

struct RunInfo {
  std::string seed;
  std::string floor;
  int stage = 0;
  int stage_type = 0;
  int curses = 0;
  uint64_t timer_frames = 0;
  double devil_chance = 0.0;
  double angel_chance = 0.0;
  double planetarium_chance = 0.0;
  int devil_deals = 0;
  std::vector<int> transformations;
  bool received = false;
};

struct LiveState {
  uint64_t last_sequence = 0;
  uint64_t game_frame = 0;
  ULONGLONG last_packet_at = 0;
  bool in_run = false;
  int character_id = 0;
  std::string character_name;
  Consumables consumables;
  Health health;
  Stats stats;
  MapState map;
  InventoryState inventory;
  RunInfo run;
};

enum class PacketTopic : uint8_t { kNone, kStatus, kMap, kInventory, kRun };

bool PercentDecode(std::string_view input, std::string* output);
bool ApplyStatePacket(const char* bytes, size_t size, LiveState* state,
                      PacketTopic* topic = nullptr);

}  // namespace isaacds
