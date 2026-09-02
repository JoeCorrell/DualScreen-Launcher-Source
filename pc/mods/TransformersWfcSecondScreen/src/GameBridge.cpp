#include "GameBridge.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "IatHook.h"
#include "Log.h"
#include "StateStore.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {

constexpr uintptr_t kGameEngineTickRva = 0x00D9C130u;
constexpr uintptr_t kGameEngineTickVtableSlotRva = 0x0203BA1Cu;
constexpr uint8_t kGameEngineTickPrologue[] = {
    0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF8};
constexpr ULONGLONG kPublishIntervalMs = 500;
constexpr ULONGLONG kResolveIntervalMs = 250;
constexpr ULONGLONG kWorldStabilizeMs = 750;
constexpr ULONGLONG kResolveDiagnosticIntervalMs = 5000;
constexpr ULONGLONG kLiveSummaryIntervalMs = 10000;

extern "C" void WfcCallThisTick(void* function, void* self, float delta);

struct CharacterPresentation {
  const char* key;
  const char* display;
  Faction faction;
  const char* portrait;
};

struct CampaignPresentation {
  int32_t map_id;
  const char* map_stem;
  const char* checkpoint_prefix;
  const char* chapter;
  const char* title;
  const char* image;
};

constexpr CharacterPresentation kCharacters[] = {
    {"airraid", "AIR RAID", Faction::kAutobot, "AirRaid"},
    {"arcee", "ARCEE", Faction::kAutobot, "Arcee"},
    {"bumblebee", "BUMBLEBEE", Faction::kAutobot, "Bumblebee"},
    {"ironhide", "IRONHIDE", Faction::kAutobot, "Ironhide"},
    {"jazz", "JAZZ", Faction::kAutobot, "Jazz"},
    {"jetfire", "JETFIRE", Faction::kAutobot, "Jetfire"},
    {"optimusprime", "OPTIMUS PRIME", Faction::kAutobot, "Optimus Prime"},
    {"optimus", "OPTIMUS", Faction::kAutobot, "Optimus"},
    {"ratchet", "RATCHET", Faction::kAutobot, "Ratchet"},
    {"scattershot", "SCATTERSHOT", Faction::kAutobot, "Scattershot"},
    {"sideswipe", "SIDESWIPE", Faction::kAutobot, "Sideswipe"},
    {"silverbolt", "SILVERBOLT", Faction::kAutobot, "Silverbolt"},
    {"warpath", "WARPATH", Faction::kAutobot, "Warpath"},
    {"zetaprime", "ZETA PRIME", Faction::kAutobot, "Zeta Prime"},
    {"barricade", "BARRICADE", Faction::kDecepticon, "Barricade"},
    {"brawl", "BRAWL", Faction::kDecepticon, "Brawl"},
    {"breakdown", "BREAKDOWN", Faction::kDecepticon, "Breakdown"},
    {"deadend", "DEAD END", Faction::kDecepticon, "Deadend"},
    {"demolishor", "DEMOLISHOR", Faction::kDecepticon, "Demolishor"},
    {"megatron", "MEGATRON", Faction::kDecepticon, "Megatron"},
    {"onslaught", "ONSLAUGHT", Faction::kDecepticon, "Onslaught"},
    {"shockwave", "SHOCKWAVE", Faction::kDecepticon, "Shockwave"},
    {"skywarp", "SKYWARP", Faction::kDecepticon, "Skywarp"},
    {"slipstream", "SLIPSTREAM", Faction::kDecepticon, "Slipstream"},
    {"soundwave", "SOUNDWAVE", Faction::kDecepticon, "Soundwave"},
    {"starscream", "STARSCREAM", Faction::kDecepticon, "Starscream"},
    {"thundercracker", "THUNDERCRACKER", Faction::kDecepticon,
     "Thundercracker"},
};

// These identifiers and titles come from this build's TransLevels.ini and
// localized TransGame.int records. Active checkpoint data is used as a
// fallback when the live replication state does not provide a selected ID.
constexpr CampaignPresentation kCampaign[] = {
    {101, "D1_ORB_Base_m", "D1", "CHAPTER I", "DARK ENERGON", "Chapter_D1"},
    {102, "D2_COR_Base_m", "D2", "CHAPTER II", "FUEL OF WAR", "Chapter_D2"},
    {103, "D3A_IAC_Base_m", "D3", "CHAPTER III", "IACON DESTROYED", "Chapter_D3"},
    {104, "D3B_IAC_Base_m", "D4", "CHAPTER IV", "DEATH OF HOPE", "Chapter_D4"},
    {105, "D5_OME_BASE_m", "D5", "CHAPTER V", "THE FINAL GUARDIAN", "Chapter_D5"},
    {96, "A1_IAC_Base_m", "A1", "CHAPTER VI", "DEFEND IACON", "Chapter_A1"},
    {97, "A2_KON_Base_m", "A2", "CHAPTER VII", "KAON PRISON BREAK", "Chapter_A2"},
    {98, "A3_COR_Base_m", "A3", "CHAPTER VIII", "TO THE CORE", "Chapter_A3"},
    {99, "A4_ORB_Base_m", "A4", "CHAPTER IX", "AERIAL ASSAULT", "Chapter_A4"},
    {100, "A5_TRY_Base_m", "A5", "CHAPTER X", "ONE SHALL STAND...", "Chapter_A5"},
};

struct AssetAlias { const char* token; const char* asset; };
constexpr AssetAlias kWeaponAssets[] = {
    {"ionblaster", "IonBlaster"}, {"neutronassaultrifle", "AssaultRifle"},
    {"assaultrifle", "AssaultRifle"}, {"burstrifle", "BURSTRIFLE"},
    {"scatterblaster", "Shotgun"}, {"shotgun", "Shotgun"},
    {"empshotgun", "EMPSHOTGUN"}, {"sniperrifle", "SniperRifle"},
    {"nullray", "SniperRifle"}, {"heavymachinegun", "HEAVYMG"},
    {"heavymg", "HEAVYMG"}, {"heavypistol", "HEAVYPISTOL"},
    {"plasmacannon", "PLASMACANNON"}, {"bazooka", "BAZOOKA"},
    {"homingrocket", "HOMINGROCKET"}, {"rocketlauncher", "HOMINGROCKET"},
    {"grenadelauncher", "GrenadeLauncher"}, {"repairray", "RepairRay"},
};
constexpr AssetAlias kAbilityAssets[] = {
    {"abilityjammer", "AbilityJammer"}, {"aoeheal", "AoeHeal"},
    {"barrier", "Shield"}, {"shield", "Shield"},
    {"cloaking", "Cloaking"}, {"decoytrap", "DecoyTrap"},
    {"disguise", "Disguise"}, {"dodge", "Dodge"},
    {"drain", "Drain"}, {"guidedmissile", "GuidedMissile"},
    {"hardlock", "HardLock"}, {"hover", "Hover"},
    {"rollersphere", "RollerSphere"}, {"shockwave", "Shockwave"},
    {"spawnammocrate", "SpawnAmmoCrate"}, {"spawnsentry", "SpawnSentry"},
    {"transformdisruptor", "TransformDisruptor"}, {"warcry", "Warcry"},
    {"whirlwind", "Whirlwind"},
};
constexpr AssetAlias kGrenadeAssets[] = {
    {"flashbang", "FlashBangs"}, {"flak", "FlakGrenades"},
    {"healgrenade", "HealGrenades"}, {"healinggrenade", "HealGrenades"},
    {"kamikaze", "KamikazeMine"}, {"mine", "KamikazeMine"},
    {"grenade", "Grenade"},
};

HMODULE g_executable = nullptr;
void** g_tick_slot = nullptr;
void* g_original_tick = nullptr;
void** g_purecall_slot = nullptr;
void* g_original_purecall = nullptr;
volatile LONG g_tick_entry_logged = 0;
volatile LONG g_tick_return_logged = 0;
volatile LONG g_purecall_logged = 0;
volatile LONG g_tick_in_progress = 0;
DWORD g_game_thread = 0;
ULONGLONG g_last_pulse = 0;
ULONGLONG g_last_publish = 0;
ULONGLONG g_resolve_retry_at = 0;
ULONGLONG g_world_ready_since = 0;
ULONGLONG g_last_resolve_diagnostic = 0;
ULONGLONG g_last_slow_pulse_log = 0;
ULONGLONG g_last_snapshot_reject_log = 0;
ULONGLONG g_last_live_summary_log = 0;
void* g_game_engine = nullptr;
void* g_world_info = nullptr;
void* g_controller = nullptr;
void* g_pawn = nullptr;
void* g_campaign_game = nullptr;
void* g_game_replication_info = nullptr;
uint64_t g_world_epoch = 1;
std::array<void*, kSquadCapacity> g_squad_controllers = {};
std::array<void*, kSquadCapacity> g_squad_pawns = {};
bool g_switch_in_progress = false;
const char* volatile g_snapshot_stage = "idle";

struct PendingAbilityRelease {
  bool pending = false;
  int32_t slot = -1;
  uint64_t world_epoch = 0;
  uint32_t controller_object_index = 0;
  uint32_t pawn_object_index = 0;
  uint8_t attempts = 0;
  ULONGLONG due_at = 0;
};

PendingAbilityRelease g_pending_ability_release;

using PurecallFn = int (__cdecl*)();
using CaptureStackBackTraceFn = USHORT (WINAPI*)(ULONG, ULONG, PVOID*,
                                                 PULONG);

int __cdecl HookedPurecall() {
  if (InterlockedCompareExchange(&g_purecall_logged, 1, 0) == 0) {
    Log(LogLevel::kError,
        "[Crash] MSVCR90 _purecall tid=%lu gameTid=%lu snapshotStage=%s",
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<unsigned long>(g_game_thread), g_snapshot_stage);
    HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    const FARPROC capture_address =
        kernel ? GetProcAddress(kernel, "RtlCaptureStackBackTrace") : nullptr;
    CaptureStackBackTraceFn capture = nullptr;
    static_assert(sizeof(capture) == sizeof(capture_address),
                  "function pointer size mismatch");
    std::memcpy(&capture, &capture_address, sizeof(capture));
    void* frames[32] = {};
    const USHORT count = capture ? capture(0, 32, frames, nullptr) : 0;
    for (USHORT index = 0; index < count; ++index) {
      MEMORY_BASIC_INFORMATION memory = {};
      if (!VirtualQuery(frames[index], &memory, sizeof(memory))) continue;
      const auto module = static_cast<HMODULE>(memory.AllocationBase);
      char path[MAX_PATH] = {};
      GetModuleFileNameA(module, path, MAX_PATH);
      const uintptr_t rva = reinterpret_cast<uintptr_t>(frames[index]) -
                            reinterpret_cast<uintptr_t>(module);
      Log(LogLevel::kError,
          "[Crash] purecall frame=%u address=%p module=%p rva=%08lX path=%s",
          static_cast<unsigned>(index), frames[index], module,
          static_cast<unsigned long>(rva), path[0] ? path : "<unknown>");
    }
  }
  return g_original_purecall
             ? reinterpret_cast<PurecallFn>(g_original_purecall)()
             : 0;
}

void CopyText(char* destination, size_t capacity, const char* source) {
  if (destination && capacity)
    std::snprintf(destination, capacity, "%s", source ? source : "");
}

std::string Token(const char* text) {
  std::string result;
  if (!text) return result;
  for (const unsigned char ch : std::string(text))
    if (std::isalnum(ch)) result.push_back(static_cast<char>(std::tolower(ch)));
  return result;
}

std::string Humanize(const char* text, const char* prefix) {
  std::string source = text ? text : "";
  if (prefix && source.rfind(prefix, 0) == 0) source.erase(0, std::strlen(prefix));
  for (size_t i = 1; i < source.size(); ++i) {
    if (std::isupper(static_cast<unsigned char>(source[i])) &&
        std::islower(static_cast<unsigned char>(source[i - 1])))
      source.insert(i++, 1, ' ');
  }
  for (char& ch : source)
    ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return source;
}

const CharacterPresentation* CharacterFor(const char* raw) {
  const std::string token = Token(raw);
  for (const CharacterPresentation& value : kCharacters)
    if (token.find(value.key) != std::string::npos) return &value;
  return nullptr;
}

const char* AssetFor(const char* raw, const AssetAlias* aliases, size_t count) {
  const std::string token = Token(raw);
  for (size_t i = 0; i < count; ++i)
    if (token.find(aliases[i].token) != std::string::npos) return aliases[i].asset;
  return "";
}

uint32_t IdentityHash(const char* name, uint32_t pawn_index) {
  uint32_t hash = 2166136261u;
  for (const unsigned char ch : std::string(name ? name : "")) {
    hash ^= ch;
    hash *= 16777619u;
  }
  hash = (hash ^ pawn_index) * 16777619u;
  return hash ? hash : 1u;
}

void* ControllerPawn(void* controller) {
  void* pawn = nullptr;
  return controller && UnrealReadObjectProperty(controller, L"Pawn", &pawn)
             ? pawn : nullptr;
}

void* PawnController(void* pawn) {
  void* controller = nullptr;
  return pawn && UnrealReadObjectProperty(pawn, L"Controller", &controller)
             ? controller : nullptr;
}

bool PendingDestroy(void* object) {
  bool pending = false;
  return (UnrealReadBoolProperty(object, L"bDeleteMe", &pending) && pending) ||
         (UnrealReadBoolProperty(object, L"bPendingDelete", &pending) &&
          pending);
}

void* TeamFor(void* controller, void* pawn) {
  void* pri = nullptr;
  void* team = nullptr;
  if (controller)
    UnrealReadObjectProperty(controller, L"PlayerReplicationInfo", &pri);
  if (!UnrealObjectValid(pri) && pawn)
    UnrealReadObjectProperty(pawn, L"PlayerReplicationInfo", &pri);
  return pri && UnrealReadObjectProperty(pri, L"Team", &team) ? team
                                                               : nullptr;
}

void LogResolveWaiting(ULONGLONG now, const char* stage) {
  if (now - g_last_resolve_diagnostic < kResolveDiagnosticIntervalMs) return;
  g_last_resolve_diagnostic = now;
  Log(LogLevel::kInfo, "[Resolve] waiting for %s", stage);
}

void ClearResolvedWorld(const char* reason) {
  if (!g_controller && !g_pawn && !g_world_info && !g_campaign_game &&
      !g_game_replication_info)
    return;
  g_controller = nullptr;
  g_pawn = nullptr;
  g_world_info = nullptr;
  g_campaign_game = nullptr;
  g_game_replication_info = nullptr;
  g_pending_ability_release = {};
  g_world_ready_since = 0;
  ++g_world_epoch;
  RejectStaleCommands(g_world_epoch);
  Log(LogLevel::kInfo, "[Lifecycle] world invalidated reason=%s epoch=%llu",
      reason ? reason : "transition",
      static_cast<unsigned long long>(g_world_epoch));
}

void ResolveStableObjects(ULONGLONG now) {
  if (now < g_resolve_retry_at) return;
  g_resolve_retry_at = now + kResolveIntervalMs;
  if (!UnrealObjectValid(g_game_engine)) {
    ClearResolvedWorld("game engine unavailable");
    LogResolveWaiting(now, "game engine");
    return;
  }

  UnrealArrayView players;
  if (!UnrealReadArrayProperty(g_game_engine, L"GamePlayers", &players) ||
      players.count <= 0) {
    ClearResolvedWorld("local player unavailable");
    LogResolveWaiting(now, "local player");
    return;
  }
  void* local_player = nullptr;
  if (!UnrealReadObjectArrayElement(players, 0, &local_player) ||
      !local_player || !UnrealObjectIsA(local_player, L"LocalPlayer")) {
    ClearResolvedWorld("invalid local player");
    LogResolveWaiting(now, "valid local player");
    return;
  }

  void* controller = nullptr;
  if (!UnrealReadObjectProperty(local_player, L"Actor", &controller) ||
      !controller ||
      !UnrealObjectIsA(controller, L"TnPlayerControllerCampaign") ||
      PendingDestroy(controller)) {
    ClearResolvedWorld("campaign controller unavailable");
    LogResolveWaiting(now, "campaign controller");
    return;
  }
  void* pawn = ControllerPawn(controller);
  void* world_info = nullptr;
  if (!pawn || !UnrealObjectIsA(pawn, L"TnPlayerPawn") ||
      PawnController(pawn) != controller || PendingDestroy(pawn) ||
      !UnrealReadObjectProperty(controller, L"WorldInfo", &world_info) ||
      !world_info || !UnrealObjectIsA(world_info, L"WorldInfo")) {
    ClearResolvedWorld("player world not ready");
    LogResolveWaiting(now, "stable player world");
    return;
  }

  void* campaign_game = nullptr;
  if (!UnrealReadObjectProperty(world_info, L"Game", &campaign_game) ||
      !campaign_game ||
      !UnrealObjectIsA(campaign_game, L"TnCampaignGameBase"))
    campaign_game = nullptr;
  void* replication_info = nullptr;
  if (!UnrealReadObjectProperty(world_info, L"GRI", &replication_info) ||
      !replication_info) {
    if (campaign_game)
      UnrealReadObjectProperty(campaign_game, L"GameReplicationInfo",
                               &replication_info);
  }
  if (replication_info &&
      !UnrealObjectIsA(replication_info, L"TnGameReplicationInfoGameBase"))
    replication_info = nullptr;

  const bool world_changed = controller != g_controller || pawn != g_pawn ||
                             world_info != g_world_info;
  g_controller = controller;
  g_pawn = pawn;
  g_world_info = world_info;
  g_campaign_game = campaign_game;
  g_game_replication_info = replication_info;
  if (world_changed) {
    g_world_ready_since = now;
    g_pending_ability_release = {};
    ++g_world_epoch;
    RejectStaleCommands(g_world_epoch);
    Log(LogLevel::kInfo,
        "[Resolve] direct player controller=%p pawn=%p world=%p game=%p gri=%p epoch=%llu",
        controller, pawn, world_info, campaign_game, replication_info,
        static_cast<unsigned long long>(g_world_epoch));
  } else if (!g_world_ready_since) {
    g_world_ready_since = now;
  }
}

bool ReadCharacterStructName(void* owner, const wchar_t* property_name,
                             char* output, size_t capacity) {
  UnrealStructValueView character;
  return owner && UnrealReadStructProperty(owner, property_name, &character) &&
         UnrealReadStructStringField(character, L"CharacterName", output,
                                     capacity) && output[0];
}

bool ResolveCharacter(void* controller, void* pawn, TransformerMode mode,
                      char* name, size_t name_capacity, Faction* faction,
                      char* portrait, size_t portrait_capacity,
                      const char* authoritative_name = nullptr) {
  char raw[128] = {};
  if (authoritative_name && authoritative_name[0])
    CopyText(raw, sizeof(raw), authoritative_name);
  void* pri = nullptr;
  if (!raw[0] && controller)
    UnrealReadObjectProperty(controller, L"PlayerReplicationInfo", &pri);
  if (!pri && pawn) UnrealReadObjectProperty(pawn, L"PlayerReplicationInfo", &pri);
  if (!raw[0] && pri)
    ReadCharacterStructName(pri, L"_CurrentCharacter", raw, sizeof(raw));
  if (!raw[0] && pri)
    UnrealReadStringProperty(pri, L"PlayerIconicCharacterName", raw,
                             sizeof(raw));
  if (!raw[0] && pawn)
    ReadCharacterStructName(pawn, L"_CurrentCharacter", raw, sizeof(raw));
  if (!raw[0] && pri)
    UnrealReadStringProperty(pri, L"CurrentCharacterString", raw,
                             sizeof(raw));
  if (!raw[0] && controller)
    UnrealReadStringProperty(controller, L"CharacterName", raw, sizeof(raw));
  if (!raw[0] && pawn) UnrealObjectName(pawn, raw, sizeof(raw));
  const CharacterPresentation* known = CharacterFor(raw);
  if (known) {
    CopyText(name, name_capacity, known->display);
    if (faction) *faction = known->faction;
    std::snprintf(portrait, portrait_capacity, "%s_%s", known->portrait,
                  mode == TransformerMode::kVehicle ? "Vehicle" : "Robot");
    return true;
  }
  const std::string display = Humanize(raw, "Tn");
  CopyText(name, name_capacity, display.c_str());
  if (faction) *faction = Faction::kUnknown;
  if (portrait && portrait_capacity) portrait[0] = '\0';
  return raw[0] != '\0';
}

TransformerMode ReadMode(void* pawn) {
  // Polling must never enter script/native ProcessEvent.  These are the
  // backing fields used by TnPlayerPawn's get_Is* accessors in this build.
  void* transformation = nullptr;
  float blend_remaining = 0.0f;
  if ((UnrealReadObjectProperty(pawn, L"_CurrentTransformation",
                                &transformation) && transformation) ||
      (UnrealReadFloatProperty(pawn, L"_TransformBlendTimeRemaining",
                               &blend_remaining) &&
       std::isfinite(blend_remaining) && blend_remaining > 0.0f))
    return TransformerMode::kTransitioning;

  void* current = nullptr;
  void* vehicle = nullptr;
  void* robot = nullptr;
  UnrealReadObjectProperty(pawn, L"_CurrentForm", &current);
  UnrealReadObjectProperty(pawn, L"_VehicleForm", &vehicle);
  UnrealReadObjectProperty(pawn, L"_RobotForm", &robot);
  if (current && current == vehicle)
    return TransformerMode::kVehicle;
  if (current && current == robot)
    return TransformerMode::kRobot;
  return TransformerMode::kUnknown;
}

void ReadVitals(void* pawn, float* health, float* maximum, bool* downed,
                bool* alive) {
  *health = -1.0f;
  *maximum = -1.0f;
  *downed = false;
  *alive = UnrealObjectValid(pawn) && !PendingDestroy(pawn);
  UnrealReadFloatProperty(pawn, L"Health", health);
  UnrealReadFloatProperty(pawn, L"HealthMax", maximum);
  UnrealReadBoolProperty(pawn, L"_IsDowned", downed);
  bool dead = false;
  UnrealReadBoolProperty(pawn, L"_DeferredDied", &dead);
  if (*health >= 0.0f) dead = dead || *health <= 0.0f;
  *alive = *alive && !dead;
}

bool ReadRestrictions(Snapshot* state) {
  bool paused = false, combat = false, move_ignored = false, look_ignored = false;
  bool movie = false, cinematic = false, nonmovie = false;
  bool disable_move = false, disable_look = false, frozen = false;
  bool matinee = false;
  void* pauser = nullptr;
  UnrealReadObjectProperty(g_world_info, L"Pauser", &pauser);
  paused = pauser != nullptr;
  UnrealReadBoolProperty(g_controller, L"bFullScreenMoviePlaying", &movie);
  UnrealReadBoolProperty(g_controller, L"bCinematicMode", &cinematic);
  UnrealReadBoolProperty(g_controller, L"bNonMovieCinematicMode", &nonmovie);
  UnrealReadBoolProperty(g_controller, L"bCinemaDisableInputMove", &disable_move);
  UnrealReadBoolProperty(g_controller, L"bCinemaDisableInputLook", &disable_look);
  UnrealReadBoolProperty(g_controller, L"bFrozen", &frozen);
  UnrealReadBoolProperty(g_pawn, L"_MatineeAnimationPlaying", &matinee);
  UnrealReadBoolProperty(g_controller, L"bIgnoreMoveInput", &move_ignored);
  UnrealReadBoolProperty(g_controller, L"bIgnoreLookInput", &look_ignored);
  void* enemy = nullptr;
  combat = UnrealReadObjectProperty(g_controller, L"Enemy", &enemy) &&
           enemy && !PendingDestroy(enemy);
  float last_take_hit_timeout = -1.0f;
  float world_time = -1.0f;
  if (UnrealReadFloatProperty(g_pawn, L"LastTakeHitTimeout",
                              &last_take_hit_timeout) &&
      UnrealReadFloatProperty(g_world_info, L"TimeSeconds", &world_time) &&
      std::isfinite(last_take_hit_timeout) && std::isfinite(world_time) &&
      last_take_hit_timeout > world_time)
    combat = true;
  UnrealArrayView pending_damage;
  if (UnrealReadArrayProperty(g_controller, L"PendingDamageIndicators",
                              &pending_damage) && pending_damage.count > 0)
    combat = true;
  void* ui_controller = nullptr;
  void* current_ui = nullptr;
  const bool menu =
      UnrealReadObjectProperty(g_controller, L"UIController", &ui_controller) &&
      ui_controller &&
      UnrealReadObjectProperty(ui_controller, L"_CurrentUI", &current_ui) &&
      current_ui;
  void* view_target = nullptr;
  UnrealReadObjectProperty(g_controller, L"ViewTarget", &view_target);
  const bool foreign_view = view_target && view_target != g_pawn;
  uint8_t interact_state = 0;
  const bool interacting =
      UnrealReadByteProperty(g_controller, L"PlayerInteractState",
                             &interact_state) && interact_state >= 2;
  void* turret = nullptr;
  const bool using_turret =
      UnrealReadObjectProperty(g_controller, L"TurretInUse", &turret) &&
      turret && !PendingDestroy(turret);
  state->paused = paused;
  state->in_combat = combat;
  state->in_cutscene = movie || cinematic || nonmovie || matinee || foreign_view;
  state->in_menu = menu;
  state->in_scripted_sequence = disable_move || disable_look || frozen ||
                                move_ignored || look_ignored || interacting ||
                                using_turret;
  // Combat is a normal state for the combat-page actions.  Only possession is
  // prohibited while fighting; transform, reload, weapon, ability, and grenade
  // requests still use their native per-action availability checks below.
  return !(paused || state->in_cutscene || menu ||
           state->in_scripted_sequence);
}

void ReadAbilityState(void* ability, float cooldown, float duration,
                      bool cooldown_pending, AbilityState* output) {
  if (!ability) return;
  output->object_index = UnrealObjectIndex(ability);
  char class_name[128] = {};
  UnrealObjectClassName(ability, class_name, sizeof(class_name));
  const std::string display = Humanize(class_name, "TnAbility");
  CopyText(output->name, sizeof(output->name), display.c_str());
  CopyText(output->icon_key, sizeof(output->icon_key),
           AssetFor(class_name, kAbilityAssets,
                    sizeof(kAbilityAssets) / sizeof(kAbilityAssets[0])));
  UnrealReadFloatProperty(ability, L"ResourceRequired",
                          &output->resource_cost);
  output->cooldown_remaining = cooldown_pending ? -1.0f : cooldown;
  output->cooldown_duration = duration;
  output->cooldown_pending = cooldown_pending ? 1 : 0;
}

void ReadAbilities(Snapshot* state, void** manager_output) {
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"AbilityManager", &manager) ||
      !manager)
    return;
  if (manager_output) *manager_output = manager;
  const bool have_resource =
      UnrealReadFloatProperty(manager, L"Resource", &state->resource) &&
      std::isfinite(state->resource);
  bool abilities_enabled = false;
  UnrealReadBoolProperty(g_pawn, L"_AbilitiesEnabled", &abilities_enabled);

  UnrealArrayView abilities;
  UnrealArrayView cooldowns;
  UnrealArrayView spam_cooldowns;
  const bool have_abilities =
      UnrealReadArrayProperty(manager, L"Abilities", &abilities);
  const bool have_cooldowns =
      UnrealReadArrayProperty(manager, L"Cooldowns", &cooldowns);
  const bool have_spam_cooldowns =
      UnrealReadArrayProperty(manager, L"SpamPreventionCooldowns",
                              &spam_cooldowns);
  for (uint32_t slot = 0;
       slot < sizeof(state->abilities) / sizeof(state->abilities[0]); ++slot) {
    void* ability = nullptr;
    if (!have_abilities ||
        !UnrealReadObjectArrayElement(abilities, static_cast<int32_t>(slot),
                                      &ability) ||
        !ability)
      continue;
    float cooldown = -1.0f;
    if (have_cooldowns && slot < static_cast<uint32_t>(cooldowns.count))
      UnrealReadFloatArrayElement(cooldowns, static_cast<int32_t>(slot),
                                  &cooldown);
    float spam_cooldown = 0.0f;
    if (have_spam_cooldowns &&
        slot < static_cast<uint32_t>(spam_cooldowns.count))
      UnrealReadFloatArrayElement(spam_cooldowns,
                                  static_cast<int32_t>(slot),
                                  &spam_cooldown);
    float cooldown_duration = -1.0f;
    UnrealArrayView configured_cooldowns;
    if (UnrealReadArrayProperty(ability, L"Cooldown",
                                &configured_cooldowns) &&
        configured_cooldowns.count > 0) {
      // WFC's current skill-data index is 1. Abilities with a single entry
      // use that entry as their fallback for every skill-data index.
      const int32_t duration_index = configured_cooldowns.count > 1 ? 1 : 0;
      UnrealReadFloatArrayElement(configured_cooldowns, duration_index,
                                  &cooldown_duration);
    }
    const bool cooldown_pending = std::isfinite(cooldown) &&
                                  cooldown > 86400.0f;
    ReadAbilityState(ability, cooldown, cooldown_duration, cooldown_pending,
                     &state->abilities[slot]);
    AbilityState& ability_state = state->abilities[slot];
    bool requires_server_start = false;
    bool server_can_start = false;
    UnrealReadBoolProperty(ability, L"RequiresServerStartCooldown",
                           &requires_server_start);
    const bool server_ready = !requires_server_start ||
        (UnrealReadBoolProperty(ability, L"bServerCanStartCooldown",
                                &server_can_start) && server_can_start);
    ability_state.available = abilities_enabled && have_resource &&
        !cooldown_pending && std::isfinite(cooldown) && cooldown <= 0.0f &&
        std::isfinite(spam_cooldown) && spam_cooldown <= 0.0f &&
        server_ready &&
        std::isfinite(ability_state.resource_cost) &&
        ability_state.resource_cost >= 0.0f &&
        state->resource >= ability_state.resource_cost;
  }
  // The game's own TnAbilityManager uses ability slot zero's required
  // resource as the pool denominator. There is no reflected MaxResource
  // field. The raw value is the only safe direct-read approximation; the
  // effective getter applies dynamic skill modifiers through ProcessEvent and
  // must not be called from every game tick.
  if (have_resource && std::isfinite(state->abilities[0].resource_cost) &&
      state->abilities[0].resource_cost > 0.0f) {
    state->max_resource = state->abilities[0].resource_cost;
    state->capabilities |= kCapResource;
  }
  if (state->abilities[0].object_index || state->abilities[1].object_index)
    state->capabilities |= kCapAbilities;
}

void FillWeapon(void* weapon, void* active, WeaponState* output) {
  output->object_index = UnrealObjectIndex(weapon);
  output->available = 1;
  output->equipped = weapon == active;
  char class_name[128] = {};
  UnrealObjectClassName(weapon, class_name, sizeof(class_name));
  CopyText(output->name, sizeof(output->name),
           Humanize(class_name, "TnWeapon").c_str());
  CopyText(output->icon_key, sizeof(output->icon_key),
           AssetFor(class_name, kWeaponAssets,
                    sizeof(kWeaponAssets) / sizeof(kWeaponAssets[0])));
  UnrealReadIntProperty(weapon, L"AmmoClipCount", &output->magazine);
  UnrealReadIntProperty(weapon, L"ReserveAmmoCount", &output->reserve);
  UnrealReadIntProperty(weapon, L"MaxAmmoClipCount",
                        &output->magazine_capacity);
}

bool IsRangedWeapon(void* inventory) {
  if (!inventory || !UnrealObjectIsA(inventory, L"TnWeapon") ||
      UnrealObjectIsA(inventory, L"TnGrenadeBag") ||
      UnrealObjectIsA(inventory, L"TnMeleeWeapon"))
    return false;
  uint8_t melee_type = 0;
  if (UnrealReadByteProperty(inventory, L"MeleeWeaponType", &melee_type) &&
      melee_type != 0)
    return false;
  char class_name[128] = {};
  UnrealObjectClassName(inventory, class_name, sizeof(class_name));
  const std::string token = Token(class_name);
  constexpr const char* kMeleeTokens[] = {
      "sword", "axe", "club", "hammer", "melee"};
  for (const char* melee : kMeleeTokens)
    if (token.find(melee) != std::string::npos) return false;
  return true;
}

void ReadWeapons(Snapshot* state, void** manager_output, void** active_output,
                 void** grenade_output) {
  void* manager = nullptr;
  UnrealReadObjectProperty(g_pawn, L"InvManager", &manager);
  if (!manager) return;
  if (manager_output) *manager_output = manager;
  void* active = nullptr;
  UnrealReadObjectProperty(manager, L"CurrentWeapon", &active);
  if (!active) UnrealReadObjectProperty(g_pawn, L"Weapon", &active);
  if (active_output) *active_output = active;
  void* grenade = nullptr;
  UnrealReadObjectProperty(manager, L"ActiveGrenadeBag", &grenade);
  std::array<void*, 2> weapons = {};
  uint32_t weapon_count = 0;

  // Walk the native Inventory linked list directly. GetWeaponByType and
  // GetCurrentWeapon both enter ProcessEvent, which is deliberately forbidden
  // from the recurring snapshot path. Preserve list order so the two cards do
  // not trade places whenever the player swaps the equipped weapon.
  void* inventory = nullptr;
  UnrealReadObjectProperty(manager, L"InventoryChain", &inventory);
  for (uint32_t examined = 0; inventory && examined < 64; ++examined) {
    if (!grenade && UnrealObjectIsA(inventory, L"TnGrenadeBag"))
      grenade = inventory;
    if (weapon_count < weapons.size() && IsRangedWeapon(inventory))
      weapons[weapon_count++] = inventory;
    void* next = nullptr;
    if (!UnrealReadObjectProperty(inventory, L"Inventory", &next) ||
        next == inventory)
      break;
    inventory = next;
  }
  // Some native loadouts do not expose the current weapon through
  // InventoryChain. Keep it visible without disturbing a complete list.
  if (active && weapon_count < weapons.size() && IsRangedWeapon(active)) {
    bool already_present = false;
    for (uint32_t index = 0; index < weapon_count; ++index)
      already_present = already_present || weapons[index] == active;
    if (!already_present) weapons[weapon_count++] = active;
  }
  if (grenade_output) *grenade_output = grenade;
  for (uint32_t i = 0; i < weapons.size(); ++i)
    if (weapons[i]) {
      FillWeapon(weapons[i], active, &state->weapons[i]);
    }
  if (weapon_count) state->capabilities |= kCapWeapons;
  if (grenade) {
    if (!UnrealReadIntProperty(grenade, L"AmmoCount", &state->grenade_count))
      UnrealReadIntProperty(grenade, L"ReserveAmmoCount",
                            &state->grenade_count);
    char class_name[128] = {};
    UnrealObjectClassName(grenade, class_name, sizeof(class_name));
    CopyText(state->grenade_name, sizeof(state->grenade_name),
             Humanize(class_name, "TnWeapon").c_str());
    CopyText(state->grenade_icon_key, sizeof(state->grenade_icon_key),
             AssetFor(class_name, kGrenadeAssets,
                      sizeof(kGrenadeAssets) / sizeof(kGrenadeAssets[0])));
    state->capabilities |= kCapGrenades;
  }
}

void FillSquadMember(void* controller, void* pawn, bool controlled,
                     bool switching_allowed, const char* authoritative_name,
                     SquadMemberState* output) {
  (void)switching_allowed;
  output->occupied = 1;
  output->controlled = controlled;
  output->controller_object_index = UnrealObjectIndex(controller);
  output->pawn_object_index = UnrealObjectIndex(pawn);
  output->mode = ReadMode(pawn);
  bool downed = false, alive = false;
  ReadVitals(pawn, &output->health, &output->max_health, &downed, &alive);
  output->downed = downed;
  output->alive = alive;
  ResolveCharacter(controller, pawn, output->mode, output->name,
                   sizeof(output->name), &output->faction,
                   output->portrait_key, sizeof(output->portrait_key),
                   authoritative_name);
  void* weapon = nullptr;
  void* inventory_manager = nullptr;
  if (UnrealReadObjectProperty(pawn, L"InvManager", &inventory_manager) &&
      inventory_manager)
    UnrealReadObjectProperty(inventory_manager, L"CurrentWeapon", &weapon);
  if (weapon) {
    char weapon_class[128] = {};
    if (UnrealObjectClassName(weapon, weapon_class, sizeof(weapon_class)))
      CopyText(output->weapon_name, sizeof(output->weapon_name),
               Humanize(weapon_class, "TnWeapon").c_str());
  }
  output->identity_hash = IdentityHash(output->name, output->pawn_object_index);
  // Live character switching remains unavailable in the stability payload.
  // Never advertise an action that could enter the unproven squad transaction.
  output->switchable = 0;
}

void ReadSquad(Snapshot* state, bool switching_allowed) {
  g_squad_controllers.fill(nullptr);
  g_squad_pawns.fill(nullptr);
  FillSquadMember(g_controller, g_pawn, true, false, nullptr,
                  &state->squad[0]);
  g_squad_controllers[0] = g_controller;
  g_squad_pawns[0] = g_pawn;
  state->squad_count = 1;
  void* squad = nullptr;
  if (UnrealObjectValid(g_campaign_game) &&
      !UnrealReadObjectProperty(g_campaign_game, L"AiSquad", &squad))
    UnrealReadObjectProperty(g_campaign_game, L"_AiSquad", &squad);
  UnrealStructArrayView records;
  if (squad && UnrealReadStructArrayProperty(squad, L"Members", &records)) {
    for (int32_t i = 0;
         i < records.count && state->squad_count < kSquadCapacity; ++i) {
      void* controller = nullptr;
      if (!UnrealReadStructArrayObjectField(records, i,
                                            L"CharacterController",
                                            &controller))
        continue;
      if (!UnrealObjectValid(controller) || controller == g_controller) continue;
      void* pawn = ControllerPawn(controller);
      if (!pawn || pawn == g_pawn ||
          !UnrealObjectIsA(controller, L"TnAiController") ||
          !UnrealObjectIsA(pawn, L"TnAiPawn") ||
          PawnController(pawn) != controller || PendingDestroy(controller) ||
          PendingDestroy(pawn))
        continue;
      char authoritative_name[128] = {};
      const bool member_record_ready = UnrealReadStructArrayStringField(
          records, i, L"CharacterName", authoritative_name,
          sizeof(authoritative_name)) && authoritative_name[0];
      char resolved_name[80] = {};
      char resolved_portrait[128] = {};
      Faction resolved_faction = Faction::kUnknown;
      if (!ResolveCharacter(controller, pawn, ReadMode(pawn), resolved_name,
                            sizeof(resolved_name), &resolved_faction,
                            resolved_portrait, sizeof(resolved_portrait),
                            member_record_ready ? authoritative_name : nullptr))
        continue;
      void* player_team = TeamFor(g_controller, g_pawn);
      const bool teammate =
          (player_team && TeamFor(controller, pawn) == player_team) ||
          (state->faction != Faction::kUnknown &&
           resolved_faction == state->faction);
      if (!teammate) continue;
      const uint32_t slot = state->squad_count++;
      FillSquadMember(controller, pawn, false, switching_allowed,
                      member_record_ready ? authoritative_name : resolved_name,
                      &state->squad[slot]);
      if (!member_record_ready)
        state->squad[slot].switchable = 0;
      g_squad_controllers[slot] = controller;
      g_squad_pawns[slot] = pawn;
    }
  }

  // Some campaign game instances do not expose the AiSquad backing property
  // even though GetAiSquad exists.  Polling may not call that accessor, so fill
  // missing companion slots from WorldInfo's native controller list.  Only
  // recognized iconic TnAiPawn allies are accepted; ordinary combat AI is
  // deliberately excluded even when it happens to share a controller base.
  void* controller = nullptr;
  void* player_team = TeamFor(g_controller, g_pawn);
  UnrealReadObjectProperty(g_world_info, L"ControllerList", &controller);
  for (uint32_t examined = 0;
       controller && examined < 128 && state->squad_count < kSquadCapacity;
       ++examined) {
    void* next = nullptr;
    UnrealReadObjectProperty(controller, L"NextController", &next);
    bool duplicate = controller == g_controller;
    for (uint32_t slot = 0; !duplicate && slot < state->squad_count; ++slot)
      duplicate = g_squad_controllers[slot] == controller;

    void* pawn = duplicate ? nullptr : ControllerPawn(controller);
    for (uint32_t slot = 0; pawn && slot < state->squad_count; ++slot)
      if (g_squad_pawns[slot] == pawn) pawn = nullptr;

    if (pawn && UnrealObjectValid(controller) && UnrealObjectValid(pawn) &&
        !PendingDestroy(controller) && !PendingDestroy(pawn) &&
        UnrealObjectIsA(controller, L"TnAiController") &&
        UnrealObjectIsA(pawn, L"TnAiPawn") && pawn != g_pawn &&
        PawnController(pawn) == controller) {
      char iconic_name[80] = {};
      char portrait[128] = {};
      Faction faction = Faction::kUnknown;
      if (ResolveCharacter(controller, pawn, ReadMode(pawn), iconic_name,
                           sizeof(iconic_name), &faction, portrait,
                           sizeof(portrait)) &&
          faction != Faction::kUnknown &&
          ((player_team && TeamFor(controller, pawn) == player_team) ||
           (state->faction != Faction::kUnknown &&
            faction == state->faction))) {
        const uint32_t slot = state->squad_count++;
        FillSquadMember(controller, pawn, false, false, iconic_name,
                        &state->squad[slot]);
        g_squad_controllers[slot] = controller;
        g_squad_pawns[slot] = pawn;
      }
    }
    if (!next || next == controller) break;
    controller = next;
  }
  state->capabilities |= kCapSquad;
}

struct ObjectiveMarkerCandidate {
  bool found = false;
  int priority = 0;
  float distance_units = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

bool ReadLocation(void* actor, float* x, float* y, float* z) {
  return UnrealReadStructFloatProperty(actor, L"Location", L"X", x) &&
         UnrealReadStructFloatProperty(actor, L"Location", L"Y", y) &&
         UnrealReadStructFloatProperty(actor, L"Location", L"Z", z) &&
         std::isfinite(*x) && std::isfinite(*y) && std::isfinite(*z);
}

void ConsiderObjectiveMarkers(const UnrealStructArrayView& markers,
                              const std::string& objective_token,
                              float player_x, float player_y, float player_z,
                              ObjectiveMarkerCandidate* best) {
  if (!best) return;
  // The native HUD only needs the nearest relevant marker. A corrupt or very
  // large transient array must not turn one companion snapshot into a frame
  // hitch on Box64.
  const int32_t marker_count = markers.count < 128 ? markers.count : 128;
  for (int32_t index = 0; index < marker_count; ++index) {
    int32_t marker_id = 0;
    if (!UnrealReadStructArrayIntField(markers, index, L"MarkerId",
                                       &marker_id) ||
        marker_id == 0)
      continue;
    void* marker_base = nullptr;
    if (!UnrealReadStructArrayObjectField(markers, index, L"MarkerBase",
                                          &marker_base) ||
        !marker_base || PendingDestroy(marker_base))
      continue;
    char description[256] = {};
    UnrealReadStructArrayStringField(markers, index, L"Description",
                                     description, sizeof(description));
    void* marker_type = nullptr;
    UnrealReadStructArrayObjectField(markers, index, L"MarkerType",
                                     &marker_type);
    void* marker_type_class = nullptr;
    UnrealReadStructArrayObjectField(markers, index, L"MarkerTypeClass",
                                     &marker_type_class);
    char marker_type_class_name[128] = {};
    if (marker_type_class)
      UnrealObjectName(marker_type_class, marker_type_class_name,
                       sizeof(marker_type_class_name));
    const bool description_match = !objective_token.empty() &&
                                   Token(description) == objective_token;
    const bool objective_marker = marker_type &&
        UnrealObjectIsA(marker_type, L"TnObjectiveMarkerTypeObjective");
    const bool objective_marker_class =
        Token(marker_type_class_name) == "tnobjectivemarkertypeobjective";
    if (!description_match && !objective_marker && !objective_marker_class)
      continue;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    if (!ReadLocation(marker_base, &x, &y, &z)) continue;
    const float dx = x - player_x;
    const float dy = y - player_y;
    const float dz = z - player_z;
    const float distance = std::sqrt(dx * dx + dy * dy + dz * dz);
    const int priority = description_match ? 2 : 1;
    if (best->found && (priority < best->priority ||
        (priority == best->priority && distance >= best->distance_units)))
      continue;
    best->found = true;
    best->priority = priority;
    best->distance_units = distance;
    best->x = x;
    best->y = y;
    best->z = z;
  }
}

const CampaignPresentation* CampaignFromMapId(int32_t map_id) {
  for (const CampaignPresentation& campaign : kCampaign)
    if (campaign.map_id == map_id) return &campaign;
  return nullptr;
}

const CampaignPresentation* CampaignFromCheckpoint(const char* checkpoint) {
  const std::string checkpoint_token = Token(checkpoint);
  if (checkpoint_token.empty()) return nullptr;
  for (const CampaignPresentation& campaign : kCampaign) {
    const std::string prefix = Token(campaign.checkpoint_prefix);
    if (checkpoint_token.rfind(prefix, 0) == 0) return &campaign;
  }
  return nullptr;
}

void ReadCampaignContext(Snapshot* state) {
  const CampaignPresentation* campaign = nullptr;
  if (UnrealObjectValid(g_world_info))
    state->world_object_index = UnrealObjectIndex(g_world_info);
  int32_t selected_map_id = -1;
  if (UnrealReadIntProperty(g_game_replication_info, L"SelectedMapID",
                            &selected_map_id))
    campaign = CampaignFromMapId(selected_map_id);
  char checkpoint[160] = {};
  if (!campaign &&
      UnrealReadStringProperty(g_game_replication_info,
                               L"ActiveCheckpointName", checkpoint,
                               sizeof(checkpoint)))
    campaign = CampaignFromCheckpoint(checkpoint);
  if (!campaign) return;
  CopyText(state->objective.chapter, sizeof(state->objective.chapter),
           campaign->chapter);
  CopyText(state->objective.mission, sizeof(state->objective.mission),
           campaign->title);
  CopyText(state->objective.image_key, sizeof(state->objective.image_key),
           campaign->image);
}

void ReadObjective(Snapshot* state) {
  if (!UnrealObjectValid(g_game_replication_info)) return;
  ReadCampaignContext(state);
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_game_replication_info,
                                L"_ObjectiveManager", &manager) || !manager ||
      PendingDestroy(manager))
    return;
  char objective[sizeof(state->objective.text)] = {};
  int32_t current_objective = -1;
  const bool current_objective_known =
      UnrealReadIntProperty(manager, L"CurrentObjective", &current_objective);
  if (current_objective_known && current_objective < 0) return;
  if (current_objective_known)
    UnrealReadStringArrayElementProperty(manager, L"ObjectiveBank",
                                         current_objective, objective,
                                         sizeof(objective));
  // ActiveObjectives is a HUD-updated cache and may be empty when the native
  // HUD is not observing. Keep it only as a compatibility fallback.
  if (!objective[0])
    UnrealReadStringProperty(g_game_replication_info, L"ActiveObjectives",
                             objective, sizeof(objective));
  if (!objective[0]) return;
  state->objective.objective_object_index = UnrealObjectIndex(manager);
  state->objective.active = 1;
  CopyText(state->objective.text, sizeof(state->objective.text), objective);
  state->capabilities |= kCapObjective;

  float player_x = 0.0f, player_y = 0.0f, player_z = 0.0f;
  if (!ReadLocation(g_pawn, &player_x, &player_y, &player_z)) return;
  ObjectiveMarkerCandidate best;
  UnrealStructArrayView markers;
  if (UnrealReadStructArrayProperty(manager, L"LocalObjectiveMarkers",
                                    &markers))
    ConsiderObjectiveMarkers(markers, Token(objective), player_x, player_y,
                             player_z, &best);
  if (UnrealReadFixedStructArrayProperty(manager, L"ObjectiveMarkers",
                                         &markers))
    ConsiderObjectiveMarkers(markers, Token(objective), player_x, player_y,
                             player_z, &best);
  if (!best.found) return;
  state->objective.has_world_location = 1;
  state->objective.world_x = best.x;
  state->objective.world_y = best.y;
  state->objective.world_z = best.z;
  // The native objective-marker label bytecode divides FVector distance by
  // 10.0 before converting it to the displayed integer distance.
  state->objective.distance_meters = best.distance_units / 10.0f;
}

void SanitizeSnapshotMetrics(Snapshot* state) {
  if (!state) return;
  const auto sanitize = [](float* value) {
    if (!std::isfinite(*value) || *value < -1.0f) *value = -1.0f;
  };
  sanitize(&state->health);
  sanitize(&state->max_health);
  sanitize(&state->resource);
  sanitize(&state->max_resource);
  for (AbilityState& ability : state->abilities) {
    sanitize(&ability.resource_cost);
    sanitize(&ability.cooldown_remaining);
    sanitize(&ability.cooldown_duration);
  }
  for (uint32_t index = 0; index < state->squad_count &&
                           index < kSquadCapacity; ++index) {
    sanitize(&state->squad[index].health);
    sanitize(&state->squad[index].max_health);
  }
  sanitize(&state->objective.distance_meters);
}

Snapshot BuildSnapshot() {
  g_snapshot_stage = "snapshot-begin";
  Snapshot previous;
  const bool have_previous = CopyLatestSnapshot(&previous);
  Snapshot state;
  state.size = sizeof(state);
  state.sequence = have_previous ? previous.sequence + 1 : 1;
  state.world_epoch = g_world_epoch;
  state.switch_in_progress = g_switch_in_progress;
  state.supported_build = 1;
  if (have_previous) CopyText(state.build_label, sizeof(state.build_label),
                              previous.build_label);
  const ULONGLONG now = GetTickCount64();
  if (!UnrealObjectValid(g_controller) || !UnrealObjectValid(g_pawn) ||
      !UnrealObjectValid(g_world_info) || !g_world_ready_since ||
      now - g_world_ready_since < kWorldStabilizeMs) {
    state.lifecycle = Lifecycle::kWaitingForWorld;
    g_snapshot_stage = "idle";
    return state;
  }
  g_snapshot_stage = "identity-vitals";
  state.lifecycle = Lifecycle::kLive;
  state.local_controller_object_index = UnrealObjectIndex(g_controller);
  state.player_pawn_object_index = UnrealObjectIndex(g_pawn);
  state.mode = ReadMode(g_pawn);
  bool downed = false, alive = false;
  ReadVitals(g_pawn, &state.health, &state.max_health, &downed, &alive);
  state.downed = downed;
  state.alive = alive;
  if (!alive) state.lifecycle = Lifecycle::kDead;
  if (state.health >= 0.0f && state.max_health > 0.0f)
    state.capabilities |= kCapVitals;
  if (state.mode != TransformerMode::kUnknown) state.capabilities |= kCapMode;
  if (ResolveCharacter(g_controller, g_pawn, state.mode, state.character_name,
                       sizeof(state.character_name), &state.faction,
                       state.portrait_key, sizeof(state.portrait_key)))
    state.capabilities |= kCapIdentity;
  g_snapshot_stage = "restrictions";
  const bool unrestricted = ReadRestrictions(&state);
  g_snapshot_stage = "abilities";
  void* ability_manager = nullptr;
  ReadAbilities(&state, &ability_manager);
  state.ability_manager_object_index = UnrealObjectIndex(ability_manager);
  g_snapshot_stage = "weapons";
  void* inventory_manager = nullptr;
  void* active_weapon = nullptr;
  void* grenade = nullptr;
  ReadWeapons(&state, &inventory_manager, &active_weapon, &grenade);
  state.inventory_manager_object_index = UnrealObjectIndex(inventory_manager);
  state.active_weapon_object_index = UnrealObjectIndex(active_weapon);
  state.grenade_object_index = UnrealObjectIndex(grenade);
  g_snapshot_stage = "objective";
  ReadObjective(&state);
  g_snapshot_stage = "squad";
  ReadSquad(&state, false);

  // Capability polling is direct-state-only.  ProcessEvent is entered solely
  // by ExecuteCommand after a user tap, never while producing a snapshot.
  const bool can_act = unrestricted && alive && !downed;
  bool transforming_enabled = false;
  g_snapshot_stage = "capabilities";
  UnrealReadBoolProperty(g_pawn, L"_TransformingEnabled",
                         &transforming_enabled);
  if (can_act && transforming_enabled &&
      state.mode != TransformerMode::kTransitioning &&
      UnrealCanCallVoid(g_controller, L"PressTransform"))
    state.capabilities |= kCapTransformAction;

  int32_t active_reserve = -1;
  int32_t active_clip = -1;
  int32_t active_clip_max = -1;
  if (active_weapon) {
    UnrealReadIntProperty(active_weapon, L"ReserveAmmoCount", &active_reserve);
    UnrealReadIntProperty(active_weapon, L"AmmoClipCount", &active_clip);
    UnrealReadIntProperty(active_weapon, L"MaxAmmoClipCount", &active_clip_max);
  }
  if (can_act && inventory_manager && active_weapon && active_reserve > 0 &&
      active_clip >= 0 && active_clip_max > 0 && active_clip < active_clip_max &&
      IsRangedWeapon(active_weapon) &&
      UnrealCanCallVoid(g_controller, L"ReloadWeapon"))
    state.capabilities |= kCapReloadAction;
  if (can_act && active_weapon && IsRangedWeapon(active_weapon) &&
      state.weapons[0].object_index &&
      state.weapons[1].object_index &&
      UnrealCanCallVoid(g_controller, L"NextWeapon"))
    state.capabilities |= kCapSwapAction;
  if (can_act && ability_manager &&
      (state.abilities[0].available || state.abilities[1].available) &&
      UnrealCanCallVoidInt(g_controller, L"TriggerAbility", L"number") &&
      UnrealCanCallVoidInt(g_controller, L"UnTriggerAbility", L"number"))
    state.capabilities |= kCapAbilityActions;
  if (can_act && grenade && state.grenade_count > 0 &&
      UnrealCanCallVoid(g_controller, L"TossGrenade"))
    state.capabilities |= kCapGrenadeAction;

  // Squad possession remains disabled unless the native squad record and its
  // complete identity/camera transition path have been proven safe.
  g_snapshot_stage = "idle";
  return state;
}

bool SwitchSquadMember(const Command& command, const Snapshot& state) {
  (void)command;
  (void)state;
  // Player controllers only accept TnPlayerPawn, while campaign companions
  // are TnAiPawn and require TnAiController. The previous possession exchange
  // violated both native class contracts and could crash in OnSetPawn or
  // PossessedBy. Keep this command fail-closed until the game's own complete
  // character-selection transaction has been proven on the target build.
  LogRateLimited("unsafe-squad-switch", 5000, LogLevel::kWarning,
                 "[SquadSwitch] request rejected: native character-selection transaction is not yet verified");
  return false;
}

template <typename Callback>
bool InvokeAction(const Command& command, const char* stage, Callback callback) {
  g_snapshot_stage = stage;
  Log(LogLevel::kInfo,
      "[Action] request=%llu type=%u invocation=begin target=%lu",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type),
      static_cast<unsigned long>(command.expected_target_object_index));
  const bool result = callback();
  g_snapshot_stage = "idle";
  Log(result ? LogLevel::kInfo : LogLevel::kWarning,
      "[Action] request=%llu type=%u invocation=end result=%s",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type), result ? "executed" : "failed");
  return result;
}

bool CurrentPlayableState() {
  Snapshot restrictions;
  bool downed = false;
  bool alive = false;
  float health = -1.0f;
  float maximum = -1.0f;
  ReadVitals(g_pawn, &health, &maximum, &downed, &alive);
  return alive && !downed && ReadRestrictions(&restrictions);
}

bool CurrentInventory(void** manager_output, void** active_output) {
  if (!manager_output || !active_output) return false;
  *manager_output = nullptr;
  *active_output = nullptr;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"InvManager", &manager) ||
      !manager)
    return false;
  void* active = nullptr;
  UnrealReadObjectProperty(manager, L"CurrentWeapon", &active);
  if (!active) UnrealReadObjectProperty(g_pawn, L"Weapon", &active);
  if (!active || !UnrealObjectValid(active)) return false;
  *manager_output = manager;
  *active_output = active;
  return true;
}

bool CurrentAbilityReady(int32_t slot, const Command& command,
                         const Snapshot& state) {
  if (slot < 0 || slot >= 2 || !state.ability_manager_object_index)
    return false;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"AbilityManager", &manager) ||
      !manager || UnrealObjectIndex(manager) !=
                      state.ability_manager_object_index)
    return false;
  UnrealArrayView abilities;
  void* ability = nullptr;
  if (!UnrealReadArrayProperty(manager, L"Abilities", &abilities) ||
      !UnrealReadObjectArrayElement(abilities, slot, &ability) || !ability ||
      UnrealObjectIndex(ability) != command.expected_target_object_index)
    return false;
  bool abilities_enabled = false;
  if (!UnrealReadBoolProperty(g_pawn, L"_AbilitiesEnabled",
                              &abilities_enabled) || !abilities_enabled)
    return false;
  bool requires_robot = false;
  if (UnrealReadBoolProperty(ability, L"RequiresRobotForm",
                             &requires_robot) && requires_robot &&
      ReadMode(g_pawn) != TransformerMode::kRobot)
    return false;
  bool requires_server_start = false;
  if (UnrealReadBoolProperty(ability, L"RequiresServerStartCooldown",
                             &requires_server_start) &&
      requires_server_start) {
    bool server_can_start = false;
    if (!UnrealReadBoolProperty(ability, L"bServerCanStartCooldown",
                                &server_can_start) || !server_can_start)
      return false;
  }
  float resource = -1.0f;
  float cost = -1.0f;
  if (!UnrealReadFloatProperty(manager, L"Resource", &resource) ||
      !UnrealReadFloatProperty(ability, L"ResourceRequired", &cost) ||
      !std::isfinite(resource) || !std::isfinite(cost) || cost < 0.0f ||
      resource < cost)
    return false;
  UnrealArrayView cooldowns;
  UnrealArrayView spam_cooldowns;
  float cooldown = -1.0f;
  float spam_cooldown = -1.0f;
  if (!UnrealReadArrayProperty(manager, L"Cooldowns", &cooldowns) ||
      !UnrealReadFloatArrayElement(cooldowns, slot, &cooldown) ||
      !std::isfinite(cooldown) || cooldown > 0.0f ||
      !UnrealReadArrayProperty(manager, L"SpamPreventionCooldowns",
                               &spam_cooldowns) ||
      !UnrealReadFloatArrayElement(spam_cooldowns, slot, &spam_cooldown) ||
      !std::isfinite(spam_cooldown) || spam_cooldown > 0.0f)
    return false;
  return UnrealCanCallVoidInt(g_controller, L"TriggerAbility", L"number") &&
         UnrealCanCallVoidInt(g_controller, L"UnTriggerAbility", L"number");
}

bool CurrentGrenade(const Command& command, const Snapshot& state) {
  if (!state.inventory_manager_object_index || !state.grenade_object_index)
    return false;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"InvManager", &manager) || !manager ||
      UnrealObjectIndex(manager) != state.inventory_manager_object_index)
    return false;
  void* grenade = nullptr;
  UnrealReadObjectProperty(manager, L"ActiveGrenadeBag", &grenade);
  void* inventory = nullptr;
  UnrealReadObjectProperty(manager, L"InventoryChain", &inventory);
  for (uint32_t examined = 0;
       (!grenade || UnrealObjectIndex(grenade) !=
                        command.expected_target_object_index) &&
       inventory && examined < 64; ++examined) {
    if (UnrealObjectIsA(inventory, L"TnGrenadeBag") &&
        UnrealObjectIndex(inventory) == command.expected_target_object_index) {
      grenade = inventory;
      break;
    }
    void* next = nullptr;
    if (!UnrealReadObjectProperty(inventory, L"Inventory", &next) ||
        !next || next == inventory)
      break;
    inventory = next;
  }
  if (!grenade || !UnrealObjectIsA(grenade, L"TnGrenadeBag") ||
      UnrealObjectIndex(grenade) != command.expected_target_object_index)
    return false;
  int32_t count = -1;
  if (!UnrealReadIntProperty(grenade, L"AmmoCount", &count))
    UnrealReadIntProperty(grenade, L"ReserveAmmoCount", &count);
  return count > 0 && UnrealCanCallVoid(g_controller, L"TossGrenade");
}

void ScheduleAbilityRelease(int32_t slot) {
  g_pending_ability_release.pending = true;
  g_pending_ability_release.slot = slot;
  g_pending_ability_release.world_epoch = g_world_epoch;
  g_pending_ability_release.controller_object_index =
      UnrealObjectIndex(g_controller);
  g_pending_ability_release.pawn_object_index = UnrealObjectIndex(g_pawn);
  // Never issue press and release from the same executor frame. The first
  // outermost game tick after this deadline performs the matching release.
  g_pending_ability_release.due_at = GetTickCount64() + 1;
}

bool AuthoritativeIdentityMatches(const PendingAbilityRelease& release) {
  if (!UnrealObjectValid(g_game_engine)) return false;
  UnrealArrayView players;
  void* local_player = nullptr;
  void* controller = nullptr;
  void* pawn = nullptr;
  void* world = nullptr;
  return UnrealReadArrayProperty(g_game_engine, L"GamePlayers", &players) &&
         players.count > 0 &&
         UnrealReadObjectArrayElement(players, 0, &local_player) &&
         local_player && UnrealObjectIsA(local_player, L"LocalPlayer") &&
         UnrealReadObjectProperty(local_player, L"Actor", &controller) &&
         controller && !PendingDestroy(controller) &&
         UnrealObjectIsA(controller, L"TnPlayerControllerCampaign") &&
         controller == g_controller &&
         UnrealObjectIndex(controller) == release.controller_object_index &&
         (pawn = ControllerPawn(controller)) != nullptr &&
         !PendingDestroy(pawn) &&
         UnrealObjectIsA(pawn, L"TnPlayerPawn") &&
         pawn == g_pawn &&
         UnrealObjectIndex(pawn) == release.pawn_object_index &&
         PawnController(pawn) == controller &&
         UnrealReadObjectProperty(controller, L"WorldInfo", &world) &&
         world && UnrealObjectIsA(world, L"WorldInfo") &&
         world == g_world_info && release.world_epoch == g_world_epoch;
}

void ProcessPendingAbilityRelease() {
  if (!g_pending_ability_release.pending ||
      GetTickCount64() < g_pending_ability_release.due_at)
    return;
  UnrealBeginGameThreadPulse();
  const PendingAbilityRelease release = g_pending_ability_release;
  const bool identity_matches = release.slot >= 0 && release.slot < 2 &&
      AuthoritativeIdentityMatches(release) && g_controller && g_pawn &&
      UnrealObjectIndex(g_controller) == release.controller_object_index &&
      UnrealObjectIndex(g_pawn) == release.pawn_object_index;
  if (!identity_matches) {
    g_pending_ability_release = {};
    Log(LogLevel::kWarning,
        "[Action] ability release dropped slot=%d epoch=%llu identity=%u",
        release.slot, static_cast<unsigned long long>(release.world_epoch),
        identity_matches ? 1u : 0u);
    return;
  }
  if (!UnrealCanCallVoidInt(g_controller, L"UnTriggerAbility", L"number")) {
    if (++g_pending_ability_release.attempts >= 3) {
      g_pending_ability_release = {};
      Log(LogLevel::kWarning,
          "[Action] ability release abandoned slot=%d after preflight retries",
          release.slot);
    } else {
      g_pending_ability_release.due_at = GetTickCount64() + 1;
    }
    return;
  }
  g_snapshot_stage = "command-ability-release";
  const bool released = UnrealCallVoidInt(g_controller, L"UnTriggerAbility",
                                           L"number", release.slot);
  g_snapshot_stage = "idle";
  if (released) {
    g_pending_ability_release = {};
  } else if (++g_pending_ability_release.attempts >= 3) {
    g_pending_ability_release = {};
  } else {
    g_pending_ability_release.due_at = GetTickCount64() + 1;
  }
  Log(released ? LogLevel::kInfo : LogLevel::kWarning,
      "[Action] ability release slot=%d result=%s", release.slot,
      released ? "executed" : "failed");
}

bool ExecuteCommand(const Command& command, const Snapshot& state) {
  const ULONGLONG now = GetTickCount64();
  if (!command.queued_at_millis || now < command.queued_at_millis ||
      now - command.queued_at_millis > 1000 ||
      command.world_epoch != state.world_epoch ||
      state.lifecycle != Lifecycle::kLive ||
      command.expected_controller_object_index !=
          state.local_controller_object_index ||
      command.expected_player_pawn_object_index !=
          state.player_pawn_object_index ||
      !UnrealObjectValid(g_controller) || !UnrealObjectValid(g_pawn) ||
      UnrealObjectIndex(g_controller) !=
          command.expected_controller_object_index ||
      UnrealObjectIndex(g_pawn) != command.expected_player_pawn_object_index ||
      ControllerPawn(g_controller) != g_pawn ||
      PawnController(g_pawn) != g_controller || !CurrentPlayableState())
    return false;
  switch (command.type) {
    case CommandType::kTransform:
      if (!(state.capabilities & kCapTransformAction) ||
          command.expected_target_object_index != state.player_pawn_object_index)
        return false;
      {
        bool transforming_enabled = false;
        if (!UnrealReadBoolProperty(g_pawn, L"_TransformingEnabled",
                                    &transforming_enabled) ||
            !transforming_enabled ||
            ReadMode(g_pawn) == TransformerMode::kTransitioning ||
            !UnrealCanCallVoid(g_controller, L"PressTransform"))
          return false;
      }
      return InvokeAction(command, "command-transform", [] {
        return UnrealCallVoid(g_controller, L"PressTransform");
      });
    case CommandType::kReload: {
      void* manager = nullptr;
      void* active = nullptr;
      int32_t reserve = -1;
      int32_t clip = -1;
      int32_t clip_max = -1;
      if (!(state.capabilities & kCapReloadAction) ||
          command.expected_target_object_index !=
              state.active_weapon_object_index ||
          !CurrentInventory(&manager, &active) ||
          UnrealObjectIndex(manager) !=
                          state.inventory_manager_object_index ||
          UnrealObjectIndex(active) != command.expected_target_object_index ||
          !IsRangedWeapon(active) ||
          !UnrealReadIntProperty(active, L"ReserveAmmoCount", &reserve) ||
          !UnrealReadIntProperty(active, L"AmmoClipCount", &clip) ||
          !UnrealReadIntProperty(active, L"MaxAmmoClipCount", &clip_max) ||
          reserve <= 0 || clip < 0 || clip_max <= 0 || clip >= clip_max ||
          !UnrealCanCallVoid(g_controller, L"ReloadWeapon"))
        return false;
      return InvokeAction(command, "command-reload", [] {
        return UnrealCallVoid(g_controller, L"ReloadWeapon");
      });
    }
    case CommandType::kSwapWeapon: {
      void* manager = nullptr;
      void* active = nullptr;
      if (!(state.capabilities & kCapSwapAction) ||
          command.expected_target_object_index !=
              state.active_weapon_object_index ||
          !CurrentInventory(&manager, &active) ||
          UnrealObjectIndex(manager) != state.inventory_manager_object_index ||
          UnrealObjectIndex(active) != command.expected_target_object_index ||
          !IsRangedWeapon(active) ||
          !UnrealCanCallVoid(g_controller, L"NextWeapon"))
        return false;
      return InvokeAction(command, "command-swap", [] {
        return UnrealCallVoid(g_controller, L"NextWeapon");
      });
    }
    case CommandType::kAbility1:
    case CommandType::kAbility2: {
      const int32_t slot = command.type == CommandType::kAbility1 ? 0 : 1;
      // A tap is represented by a native Trigger/UnTrigger pair on adjacent
      // game ticks. Never overwrite a still-pending release with another
      // ability press, even if the UI managed to queue it in the meantime.
      if (g_pending_ability_release.pending ||
          !(state.capabilities & kCapAbilityActions) ||
          !state.abilities[slot].available ||
          command.expected_target_object_index !=
              state.abilities[slot].object_index ||
          !CurrentAbilityReady(slot, command, state))
        return false;
      const bool triggered = InvokeAction(
          command, slot == 0 ? "command-ability-1" : "command-ability-2",
          [slot] {
        return UnrealCallVoidInt(g_controller, L"TriggerAbility", L"number",
                                 slot);
      });
      if (triggered) ScheduleAbilityRelease(slot);
      return triggered;
    }
    case CommandType::kGrenade:
      if (!(state.capabilities & kCapGrenadeAction) ||
          command.expected_target_object_index != state.grenade_object_index ||
          !CurrentGrenade(command, state))
        return false;
      return InvokeAction(command, "command-grenade", [] {
        return UnrealCallVoid(g_controller, L"TossGrenade");
      });
    case CommandType::kSwitchSquadMember:
      return SwitchSquadMember(command, state);
    case CommandType::kNone: return false;
  }
  return false;
}

void ProcessOneQueuedCommand() {
  Command command;
  if (!PollCommand(&command)) return;
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) {
    Log(LogLevel::kWarning,
        "[Action] request=%llu type=%u result=rejected reason=no-snapshot",
        static_cast<unsigned long long>(command.request_id),
        static_cast<unsigned>(command.type));
    return;
  }
  UnrealBeginGameThreadPulse();
  const bool accepted = ExecuteCommand(command, state);
  Log(accepted ? LogLevel::kInfo : LogLevel::kWarning,
      "[Action] request=%llu type=%u result=%s epoch=%llu",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type), accepted ? "accepted" : "rejected",
      static_cast<unsigned long long>(command.world_epoch));
}

void GameThreadPulse() {
  if (!g_game_thread) {
    g_game_thread = GetCurrentThreadId();
    Log(LogLevel::kInfo, "[Lifecycle] game-thread tick established tid=%lu",
        static_cast<unsigned long>(g_game_thread));
  }
  if (GetCurrentThreadId() != g_game_thread) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_last_pulse < kResolveIntervalMs) return;
  g_last_pulse = now;
  UnrealBeginGameThreadPulse();
  if (!UnrealBridgeReady()) return;
  const ULONGLONG work_started = now;
  g_snapshot_stage = "resolve";
  ResolveStableObjects(now);
  g_snapshot_stage = "idle";
  if (now - g_last_publish < kPublishIntervalMs) {
    const ULONGLONG elapsed = GetTickCount64() - work_started;
    if (elapsed >= 8 && now - g_last_slow_pulse_log >= 5000) {
      g_last_slow_pulse_log = now;
      Log(LogLevel::kWarning,
          "[Performance] direct resolver used %llums of game tick",
          static_cast<unsigned long long>(elapsed));
    }
    return;
  }
  Snapshot state = BuildSnapshot();
  SanitizeSnapshotMetrics(&state);
  const bool published = PublishSnapshot(state);
  if (!published &&
      now - g_last_snapshot_reject_log >= kResolveDiagnosticIntervalMs) {
    g_last_snapshot_reject_log = now;
    Log(LogLevel::kWarning,
        "[State] snapshot rejected sequence=%llu epoch=%llu lifecycle=%u",
        static_cast<unsigned long long>(state.sequence),
        static_cast<unsigned long long>(state.world_epoch),
        static_cast<unsigned>(state.lifecycle));
  }
  if (published && state.lifecycle == Lifecycle::kLive &&
      now - g_last_live_summary_log >= kLiveSummaryIntervalMs) {
    g_last_live_summary_log = now;
    Log(LogLevel::kInfo,
        "[State] live character=%s hp=%.1f/%.1f mode=%u resource=%.1f weapons=[%s,%s] abilities=[%s,%s] squad=%u [%s,%s,%s] caps=%llX",
        state.character_name, state.health, state.max_health,
        static_cast<unsigned>(state.mode), state.resource,
        state.weapons[0].name, state.weapons[1].name,
        state.abilities[0].name, state.abilities[1].name, state.squad_count,
        state.squad[0].name, state.squad[1].name, state.squad[2].name,
        static_cast<unsigned long long>(state.capabilities));
  }
  g_last_publish = now;
  const ULONGLONG elapsed = GetTickCount64() - work_started;
  if (elapsed >= 8 && now - g_last_slow_pulse_log >= 5000) {
    g_last_slow_pulse_log = now;
    Log(LogLevel::kWarning,
        "[Performance] bridge snapshot used %llums of game tick",
        static_cast<unsigned long long>(elapsed));
  }
}

void __attribute__((fastcall)) HookedTick(void* self, void*, float delta) {
  const bool outermost =
      InterlockedCompareExchange(&g_tick_in_progress, 1, 0) == 0;
  if (InterlockedCompareExchange(&g_tick_entry_logged, 1, 0) == 0) {
    Log(LogLevel::kInfo,
        "[Lifecycle] first detoured game tick entered tid=%lu self=%p original=%p",
        static_cast<unsigned long>(GetCurrentThreadId()), self, g_original_tick);
  }
  WfcCallThisTick(g_original_tick, self, delta);
  if (InterlockedCompareExchange(&g_tick_return_logged, 1, 0) == 0) {
    Log(LogLevel::kInfo, "[Lifecycle] first original game tick returned");
  }
  g_game_engine = self;
  if (outermost) {
    GameThreadPulse();
    // UGameEngine::Tick is expected on the game thread, but never allow a
    // reflected action if this virtual is unexpectedly reached elsewhere.
    if (g_game_thread && GetCurrentThreadId() == g_game_thread) {
      ProcessPendingAbilityRelease();
      ProcessOneQueuedCommand();
    }
    InterlockedExchange(&g_tick_in_progress, 0);
  }
}

}  // namespace

bool InitializeGameBridge(HMODULE executable) {
  if (!executable || g_tick_slot) return g_tick_slot != nullptr;
  g_executable = executable;
  if (!InitializeUnrealBridge(executable)) return false;
  void** purecall_slot = FindIatSlot(executable, "MSVCR90.dll", "_purecall");
  if (purecall_slot &&
      InstallIatHook(purecall_slot, *purecall_slot,
                     reinterpret_cast<void*>(&HookedPurecall),
                     &g_original_purecall)) {
    g_purecall_slot = purecall_slot;
    Log(LogLevel::kInfo,
        "[Lifecycle] MSVCR90 _purecall crash logger installed slot=%p original=%p",
        g_purecall_slot, g_original_purecall);
  } else {
    Log(LogLevel::kWarning,
        "[Lifecycle] MSVCR90 _purecall import unavailable; crash stack logging disabled");
  }
  void* target = reinterpret_cast<uint8_t*>(executable) + kGameEngineTickRva;
  void** slot = reinterpret_cast<void**>(
      reinterpret_cast<uint8_t*>(executable) + kGameEngineTickVtableSlotRva);
  if (std::memcmp(target, kGameEngineTickPrologue,
                  sizeof(kGameEngineTickPrologue)) != 0 || *slot != target) {
    Log(LogLevel::kError,
        "[Lifecycle] UGameEngine::Tick signature/vtable mismatch function=%08lX slot=%08lX; hooks disabled",
        static_cast<unsigned long>(kGameEngineTickRva),
        static_cast<unsigned long>(kGameEngineTickVtableSlotRva));
    if (g_purecall_slot)
      RestoreIatHook(g_purecall_slot,
                     reinterpret_cast<void*>(&HookedPurecall),
                     g_original_purecall);
    g_purecall_slot = nullptr;
    g_original_purecall = nullptr;
    ShutdownUnrealBridge();
    return false;
  }
  if (!InstallIatHook(slot, target, reinterpret_cast<void*>(&HookedTick),
                      &g_original_tick)) {
    Log(LogLevel::kError, "[Lifecycle] UGameEngine::Tick vtable hook failed");
    if (g_purecall_slot)
      RestoreIatHook(g_purecall_slot,
                     reinterpret_cast<void*>(&HookedPurecall),
                     g_original_purecall);
    g_purecall_slot = nullptr;
    g_original_purecall = nullptr;
    ShutdownUnrealBridge();
    return false;
  }
  g_tick_slot = slot;
  Log(LogLevel::kInfo,
      "[Lifecycle] exact-build UGameEngine::Tick vtable hook installed function=%08lX slot=%08lX",
      static_cast<unsigned long>(kGameEngineTickRva),
      static_cast<unsigned long>(kGameEngineTickVtableSlotRva));
  return true;
}

void ShutdownGameBridge() {
  if (g_tick_slot) {
    RestoreIatHook(g_tick_slot, reinterpret_cast<void*>(&HookedTick),
                   g_original_tick);
  }
  if (g_purecall_slot) {
    RestoreIatHook(g_purecall_slot, reinterpret_cast<void*>(&HookedPurecall),
                   g_original_purecall);
  }
  ShutdownUnrealBridge();
  g_tick_slot = nullptr;
  g_original_tick = nullptr;
  g_purecall_slot = nullptr;
  g_original_purecall = nullptr;
  g_tick_entry_logged = 0;
  g_tick_return_logged = 0;
  g_purecall_logged = 0;
  g_tick_in_progress = 0;
  g_executable = nullptr;
  g_game_thread = 0;
  g_last_pulse = 0;
  g_last_publish = 0;
  g_resolve_retry_at = 0;
  g_world_ready_since = 0;
  g_last_resolve_diagnostic = 0;
  g_last_slow_pulse_log = 0;
  g_last_snapshot_reject_log = 0;
  g_last_live_summary_log = 0;
  g_game_engine = nullptr;
  g_world_info = nullptr;
  g_controller = nullptr;
  g_pawn = nullptr;
  g_campaign_game = nullptr;
  g_game_replication_info = nullptr;
  g_squad_controllers.fill(nullptr);
  g_squad_pawns.fill(nullptr);
  g_switch_in_progress = false;
  g_pending_ability_release = {};
  g_snapshot_stage = "idle";
}

}  // namespace wfcds
