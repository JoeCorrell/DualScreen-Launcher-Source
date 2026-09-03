#pragma once

#include <cstdint>
#include <type_traits>

namespace wfcds {

constexpr uint32_t kSnapshotVersion = 0x00010004u;
constexpr uint32_t kSquadCapacity = 3;

enum class Lifecycle : uint32_t {
  kBoot,
  kWaitingForWorld,
  kLoading,
  kLive,
  kDead,
  kUnsupportedBuild,
  kBridgeError,
};

enum class Faction : uint8_t { kUnknown, kAutobot, kDecepticon };
enum class TransformerMode : uint8_t {
  kUnknown,
  kRobot,
  kVehicle,
  kTransitioning,
};

enum Capability : uint64_t {
  kCapIdentity = 1ull << 0,
  kCapVitals = 1ull << 1,
  kCapResource = 1ull << 2,
  kCapMode = 1ull << 3,
  kCapWeapons = 1ull << 4,
  kCapAbilities = 1ull << 5,
  kCapGrenades = 1ull << 6,
  kCapObjective = 1ull << 7,
  kCapSquad = 1ull << 8,
  kCapTransformAction = 1ull << 16,
  kCapReloadAction = 1ull << 17,
  kCapSwapAction = 1ull << 18,
  kCapAbilityActions = 1ull << 19,
  kCapGrenadeAction = 1ull << 20,
  kCapSquadSwitchAction = 1ull << 21,
};

struct WeaponState {
  uint32_t object_index = 0;
  int32_t magazine = -1;
  int32_t reserve = -1;
  int32_t magazine_capacity = -1;
  int32_t reserve_capacity = -1;
  float heat = -1.0f;
  float heat_capacity = -1.0f;
  uint8_t equipped = 0;
  uint8_t available = 0;
  uint8_t unlimited_reserve = 0;
  uint8_t clipless = 0;
  uint8_t heat_based = 0;
  char name[80] = {};
  char icon_key[128] = {};
};

struct AbilityState {
  uint32_t object_index = 0;
  float resource_cost = -1.0f;
  float cooldown_remaining = -1.0f;
  float cooldown_duration = -1.0f;
  uint8_t available = 0;
  uint8_t active = 0;
  uint8_t cooldown_pending = 0;
  char name[80] = {};
  char icon_key[128] = {};
};

struct SquadMemberState {
  uint32_t pawn_object_index = 0;
  uint32_t controller_object_index = 0;
  uint32_t identity_hash = 0;
  Faction faction = Faction::kUnknown;
  TransformerMode mode = TransformerMode::kUnknown;
  uint8_t occupied = 0;
  uint8_t controlled = 0;
  uint8_t alive = 0;
  uint8_t downed = 0;
  uint8_t switchable = 0;
  float health = -1.0f;
  float max_health = -1.0f;
  char name[80] = {};
  char weapon_name[80] = {};
  char portrait_key[128] = {};
};

struct ObjectiveState {
  uint32_t objective_object_index = 0;
  uint8_t active = 0;
  uint8_t completed = 0;
  uint8_t failed = 0;
  uint8_t has_world_location = 0;
  float world_x = 0.0f;
  float world_y = 0.0f;
  float world_z = 0.0f;
  float distance_meters = -1.0f;
  char chapter[96] = {};
  char mission[128] = {};
  char text[256] = {};
  char image_key[128] = {};
};

struct Snapshot {
  uint32_t version = kSnapshotVersion;
  uint32_t size = 0;
  uint64_t sequence = 0;
  uint64_t world_epoch = 0;
  // GetTickCount64() on the game thread when the live values were sampled.
  // The panel uses this only for display interpolation; command validation
  // always reads authoritative game state again.
  uint64_t captured_at_millis = 0;
  uint64_t capabilities = 0;
  Lifecycle lifecycle = Lifecycle::kBoot;
  uint32_t supported_build = 0;
  uint32_t local_controller_object_index = 0;
  uint32_t player_pawn_object_index = 0;
  uint32_t world_object_index = 0;
  uint32_t inventory_manager_object_index = 0;
  uint32_t active_weapon_object_index = 0;
  uint32_t ability_manager_object_index = 0;
  uint32_t grenade_object_index = 0;
  Faction faction = Faction::kUnknown;
  TransformerMode mode = TransformerMode::kUnknown;
  uint8_t alive = 0;
  uint8_t downed = 0;
  uint8_t paused = 0;
  uint8_t in_combat = 0;
  uint8_t in_cutscene = 0;
  uint8_t in_menu = 0;
  uint8_t in_scripted_sequence = 0;
  uint8_t switch_in_progress = 0;
  float health = -1.0f;
  float max_health = -1.0f;
  float resource = -1.0f;
  float max_resource = -1.0f;
  int32_t grenade_count = -1;
  char character_name[80] = {};
  char grenade_name[80] = {};
  char portrait_key[128] = {};
  char grenade_icon_key[128] = {};
  char build_label[96] = {};
  WeaponState weapons[2] = {};
  AbilityState abilities[2] = {};
  uint32_t squad_count = 0;
  SquadMemberState squad[kSquadCapacity] = {};
  ObjectiveState objective = {};
};

enum class CommandType : uint32_t {
  kNone,
  kTransform,
  kReload,
  kSwapWeapon,
  kAbility1,
  kAbility2,
  kGrenade,
  kSwitchSquadMember,
};

struct Command {
  uint32_t version = kSnapshotVersion;
  CommandType type = CommandType::kNone;
  uint64_t request_id = 0;
  uint64_t world_epoch = 0;
  uint64_t queued_at_millis = 0;
  uint32_t expected_controller_object_index = 0;
  uint32_t expected_player_pawn_object_index = 0;
  uint32_t expected_target_object_index = 0;
  uint32_t squad_slot = 0;
  uint32_t expected_pawn_object_index = 0;
  uint32_t expected_identity_hash = 0;
};

static_assert(std::is_trivially_copyable<Snapshot>::value,
              "snapshot must cross threads by value");
static_assert(std::is_trivially_copyable<Command>::value,
              "command must cross threads by value");

}  // namespace wfcds
