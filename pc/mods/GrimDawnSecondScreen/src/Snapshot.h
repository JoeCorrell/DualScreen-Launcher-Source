#pragma once

#include <cstdint>

namespace gdds {

constexpr uint32_t kSnapshotVersion = 1;

enum class Lifecycle : uint32_t {
  kBoot,
  kWaitingForGame,
  kLoading,
  kLive,
  kNoCharacter,
  kUnsupported,
  kFaulted,
};

enum Capability : uint64_t {
  kCapVitals = 1ull << 0,
  kCapIdentity = 1ull << 1,
  kCapAttributes = 1ull << 2,
  kCapEconomy = 1ull << 3,
  kCapInventoryCount = 1ull << 4,
  kCapWorldPosition = 1ull << 5,
  kCapAreaTag = 1ull << 6,
  kCapEquipment = 1ull << 7,
  kCapInventory = 1ull << 8,
  kCapSkills = 1ull << 9,
  kCapDevotion = 1ull << 10,
  kCapQuests = 1ull << 11,
  kCapFactions = 1ull << 12,
  kCapMap = 1ull << 13,
  kCapCrafting = 1ull << 14,
  kCapActions = 1ull << 15,
  kCapDifficulty = 1ull << 16,
};

struct Snapshot {
  uint32_t version = kSnapshotVersion;
  uint32_t size = sizeof(Snapshot);
  uint64_t sequence = 0;
  uint64_t world_epoch = 0;
  uint64_t capabilities = 0;
  uint64_t sampled_qpc = 0;
  uint32_t stats_revision = 0;
  uint32_t inventory_revision = 0;
  uint32_t equipment_revision = 0;
  uint32_t skills_revision = 0;
  uint32_t quest_revision = 0;
  uint32_t map_revision = 0;
  Lifecycle lifecycle = Lifecycle::kBoot;
  uint32_t difficulty = 0;
  uint32_t level = 0;
  uint32_t experience = 0;
  uint32_t iron_bits = 0;
  uint32_t inventory_count = 0;
  uint32_t skill_points = 0;
  uint32_t skill_count = 0;
  uint32_t devotion_points_available = 0;
  uint32_t devotion_points_total = 0;
  uint32_t devotion_points_max = 0;
  uint32_t riftgate_count = 0;
  float health = 0.0f;
  float max_health = 0.0f;
  float energy = 0.0f;
  float max_energy = 0.0f;
  float physique = 0.0f;
  float cunning = 0.0f;
  float spirit = 0.0f;
  float offensive_ability = 0.0f;
  float defensive_ability = 0.0f;
  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_z = 0.0f;
  float player_w = 0.0f;
  float sample_milliseconds = 0.0f;
  char build_label[64] = {};
  wchar_t character_name[64] = {};
  char area_tag[128] = {};
  char status[160] = {};
};

}  // namespace gdds
