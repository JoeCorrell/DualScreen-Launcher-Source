#include "GameBridge.h"

#include <algorithm>
#include <atomic>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "CharacterRoleAdapter.h"
#include "CharacterSwap.h"
#include "CampaignClassSystem.h"
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
constexpr ULONGLONG kPublishIntervalMs = 1000;
constexpr ULONGLONG kResolveIntervalMs = 500;
constexpr ULONGLONG kStableResolveIntervalMs = 10000;
// Snapshot work runs immediately before the game's native tick. Never execute
// the whole reflected-object walk in one frame: on Box64 the old monolithic
// pass was measured at 19-57 ms and produced periodic FPS spikes.
constexpr ULONGLONG kSnapshotSliceIntervalMs = 16;
// Squad membership and objective marker discovery are the expensive portions
// of a snapshot. They change infrequently and previously caused a visible
// game-thread pulse every other second under Box64.
constexpr ULONGLONG kObjectiveAuditIntervalMs = 30000;
constexpr ULONGLONG kSquadAuditIntervalMs = 5000;
constexpr ULONGLONG kWeaponAuditIntervalMs = 15000;
constexpr ULONGLONG kWorldStabilizeMs = 750;
constexpr ULONGLONG kResolveDiagnosticIntervalMs = 5000;
constexpr ULONGLONG kDisplayRetryMs = 2000;
constexpr ULONGLONG kDisplayStartupDelayMs = 1000;
constexpr int kUpperViewportWidth = 1280;
constexpr int kUpperViewportHeight = 720;
#ifndef NDEBUG
constexpr ULONGLONG kLiveSummaryIntervalMs = 10000;
#endif

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
void** g_exit_process_slot = nullptr;
void* g_original_exit_process = nullptr;
void** g_terminate_process_slot = nullptr;
void* g_original_terminate_process = nullptr;
void** g_abort_slot = nullptr;
void* g_original_abort = nullptr;
void** g_crt_exit_slot = nullptr;
void* g_original_crt_exit = nullptr;
std::atomic<bool> g_tick_entry_logged{false};
std::atomic<bool> g_tick_return_logged{false};
volatile LONG g_purecall_logged = 0;
std::atomic<LONG> g_tick_in_progress{0};
std::atomic<bool> g_hook_stopping{false};
std::atomic<DWORD> g_game_thread{0};
static_assert(std::atomic<bool>::is_always_lock_free &&
                  std::atomic<LONG>::is_always_lock_free &&
                  std::atomic<DWORD>::is_always_lock_free &&
                  std::atomic<const char*>::is_always_lock_free,
              "The per-frame WFC tick gate requires lock-free atomics");
ULONGLONG g_last_pulse = 0;
ULONGLONG g_last_publish = 0;
ULONGLONG g_last_objective_discovery = 0;
ULONGLONG g_last_squad_discovery = 0;
ULONGLONG g_resolve_retry_at = 0;
ULONGLONG g_world_ready_since = 0;
ULONGLONG g_last_resolve_diagnostic = 0;
ULONGLONG g_last_slow_pulse_log = 0;
ULONGLONG g_last_snapshot_reject_log = 0;
ULONGLONG g_display_retry_at = 0;
ULONGLONG g_display_focus_confirm_at = 0;
bool g_display_resolution_applied = false;
bool g_display_focus_applied = false;
HWND g_display_game_window = nullptr;
#ifndef NDEBUG
ULONGLONG g_last_live_summary_log = 0;
#endif
void* g_game_engine = nullptr;
void* g_world_info = nullptr;
void* g_controller = nullptr;
void* g_pawn = nullptr;
void* g_campaign_game = nullptr;
void* g_game_replication_info = nullptr;
uint64_t g_world_epoch = 1;
std::array<void*, kSquadCapacity> g_squad_controllers = {};
std::array<void*, kSquadCapacity> g_squad_pawns = {};
std::array<void*, kSquadCapacity> g_squad_weapons = {};
std::array<int32_t, kSquadCapacity> g_squad_record_indices = {{-1, -1, -1}};
uint32_t g_next_squad_refresh_slot = 0;
void* g_objective_manager = nullptr;
void* g_objective_marker_base = nullptr;
int32_t g_objective_number = -1;
void* g_cached_inventory_manager = nullptr;
void* g_cached_current_weapon = nullptr;
void* g_cached_stored_vehicle_weapon = nullptr;
void* g_cached_stored_robot_weapon = nullptr;
void* g_cached_inventory_head = nullptr;
void* g_cached_grenade = nullptr;
void* g_cached_display_weapon = nullptr;
std::array<void*, 2> g_cached_robot_weapons = {};
bool g_cached_has_alternate_weapon = false;
ULONGLONG g_last_weapon_discovery = 0;
bool g_switch_in_progress = false;
bool g_switch_snapshot_pending = false;
bool g_role_proxy_failed_closed = false;
bool g_role_proxy_death_pending = false;
bool g_world_command_gate_pending = false;
uint8_t g_role_proxy_consecutive_failures = 0;
uint8_t g_role_proxy_consecutive_successes = 0;
uint64_t g_switch_snapshot_sequence = 0;
ULONGLONG g_swap_recovery_retry_at = 0;
struct CheckpointRestoreJournal {
  DeferredCheckpointRequest checkpoint = {};
  void* world = nullptr;
  void* controller = nullptr;
  void* physical_player_pawn = nullptr;
  uint32_t world_object_index = 0;
  uint32_t controller_object_index = 0;
  uint32_t physical_player_pawn_object_index = 0;
  uint64_t swap_request_id = 0;
  bool active = false;
};
CheckpointRestoreJournal g_checkpoint_restore;
std::atomic<const char*> g_snapshot_stage{"idle"};
std::atomic<DWORD> g_tick_started_ms{0};
volatile LONG g_watchdog_reported_start = 0;
volatile LONG g_watchdog_stop = 0;
volatile LONG g_last_action_type = 0;
volatile LONG g_last_action_phase = 0;
volatile LONG g_last_action_tick = 0;
volatile LONG g_last_action_request_low = 0;
HANDLE g_watchdog_thread = nullptr;
DWORD g_watchdog_thread_id = 0;
std::array<ULONGLONG, 8> g_last_accepted_action = {};
ULONGLONG g_last_accepted_native_action = 0;

void SetSnapshotStage(const char* stage) {
  g_snapshot_stage.store(stage ? stage : "unknown",
                         std::memory_order_relaxed);
}

const char* SnapshotStage() {
  return g_snapshot_stage.load(std::memory_order_relaxed);
}

bool SwapWorldMutationActive();

enum class SnapshotBuildPhase : uint8_t {
  kIdle,
  kIdentityVitals,
  kRestrictions,
  kAbilities,
  kWeapons,
  kObjective,
  kObjectiveLocalMarkers,
  kObjectiveFixedMarkers,
  kSquad,
  kCapabilities,
  kPublish,
};

enum class SquadDiscoveryPhase : uint8_t {
  kIdle,
  kRecords,
  kControllers,
  kComplete,
};

struct ObjectiveMarkerCandidate {
  bool found = false;
  int priority = 0;
  void* marker_base = nullptr;
  float distance_units = 0.0f;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct SnapshotBuild {
  SnapshotBuildPhase phase = SnapshotBuildPhase::kIdle;
  Snapshot state = {};
  Snapshot previous = {};
  bool have_previous = false;
  bool reuse_identity = false;
  bool reuse_objective = false;
  bool reuse_squad = false;
  bool unrestricted = false;
  bool alive = false;
  bool downed = false;
  bool has_alternate_robot_weapon = false;
  bool weapon_discovery_active = false;
  bool weapon_discovery_complete = false;
  void* controller = nullptr;
  void* pawn = nullptr;
  void* world_info = nullptr;
  void* game_replication_info = nullptr;
  uint32_t objective_manager_object_index = 0;
  int32_t objective_marker_index = 0;
  float objective_player_x = 0.0f;
  float objective_player_y = 0.0f;
  float objective_player_z = 0.0f;
  std::string objective_token;
  ObjectiveMarkerCandidate objective_marker;
  void* weapon_manager = nullptr;
  uint32_t weapon_manager_object_index = 0;
  void* weapon_active = nullptr;
  uint32_t weapon_active_object_index = 0;
  void* weapon_stored_vehicle = nullptr;
  uint32_t weapon_stored_vehicle_object_index = 0;
  void* weapon_stored_robot = nullptr;
  uint32_t weapon_stored_robot_object_index = 0;
  void* weapon_inventory_head = nullptr;
  uint32_t weapon_inventory_head_object_index = 0;
  void* weapon_inventory_cursor = nullptr;
  uint32_t weapon_inventory_cursor_object_index = 0;
  void* weapon_inventory_previous = nullptr;
  uint32_t weapon_inventory_previous_object_index = 0;
  void* weapon_grenade = nullptr;
  uint32_t weapon_grenade_object_index = 0;
  std::array<void*, 8> weapon_candidates = {};
  std::array<uint32_t, 8> weapon_candidate_object_indices = {};
  uint32_t weapon_candidate_count = 0;
  uint32_t weapon_nodes_examined = 0;
  SquadDiscoveryPhase squad_discovery_phase = SquadDiscoveryPhase::kIdle;
  void* squad_campaign_game = nullptr;
  uint32_t squad_campaign_game_object_index = 0;
  void* squad_object = nullptr;
  uint32_t squad_object_index = 0;
  UnrealStructArrayView squad_records = {};
  bool squad_records_available = false;
  uint32_t squad_record_struct_object_index = 0;
  int32_t squad_record_index = 0;
  int32_t squad_record_count = 0;
  void* squad_player_team = nullptr;
  uint32_t squad_player_team_object_index = 0;
  bool squad_controller_scan_initialized = false;
  void* squad_controller_head = nullptr;
  uint32_t squad_controller_head_object_index = 0;
  void* squad_controller_cursor = nullptr;
  uint32_t squad_controller_cursor_object_index = 0;
  void* squad_controller_previous = nullptr;
  uint32_t squad_controller_previous_object_index = 0;
  uint32_t squad_controllers_examined = 0;
  std::array<void*, kSquadCapacity> squad_controllers = {};
  std::array<void*, kSquadCapacity> squad_pawns = {};
  std::array<void*, kSquadCapacity> squad_weapons = {};
  std::array<int32_t, kSquadCapacity> squad_record_indices = {{-1, -1, -1}};
  uint64_t world_epoch = 0;
  ULONGLONG started_at = 0;
  ULONGLONG next_slice_at = 0;
};

SnapshotBuild g_snapshot_build;

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
using ExitProcessFn = void (WINAPI*)(UINT);
using TerminateProcessFn = BOOL (WINAPI*)(HANDLE, UINT);
using AbortFn = void (__cdecl*)();
using CrtExitFn = void (__cdecl*)(int);
using CaptureStackBackTraceFn = USHORT (WINAPI*)(ULONG, ULONG, PVOID*,
                                                 PULONG);

void LogCrashAddress(const char* label, void* address) {
  MEMORY_BASIC_INFORMATION memory = {};
  if (!address || !VirtualQuery(address, &memory, sizeof(memory))) {
    LogEmergency(LogLevel::kError, "[Crash] %s address=%p module=<unknown>",
                 label, address);
    return;
  }
  const auto module = static_cast<HMODULE>(memory.AllocationBase);
  char path[MAX_PATH] = {};
  GetModuleFileNameA(module, path, MAX_PATH);
  const uintptr_t rva = reinterpret_cast<uintptr_t>(address) -
                        reinterpret_cast<uintptr_t>(module);
  LogEmergency(LogLevel::kError,
               "[Crash] %s address=%p module=%p rva=%08lX path=%s", label,
               address, module, static_cast<unsigned long>(rva),
               path[0] ? path : "<unknown>");
}

int __cdecl HookedPurecall() {
  // Box64's RtlCaptureStackBackTrace currently stops at this hook. Capture the
  // ABI return address before doing any other work: because TWFC calls
  // _purecall through its MSVCR90 IAT, this is the actual game-side call site
  // that triggered R6025 rather than another address inside DINPUT8.dll.
  void* const direct_caller = __builtin_return_address(0);
  if (InterlockedCompareExchange(&g_purecall_logged, 1, 0) == 0) {
    const LONG action_tick =
        InterlockedCompareExchange(&g_last_action_tick, 0, 0);
    const DWORD action_age = action_tick
                                 ? GetTickCount() -
                                       static_cast<DWORD>(action_tick)
                                 : 0;
    LogEmergency(
        LogLevel::kError,
        "[Crash] MSVCR90 _purecall tid=%lu gameTid=%lu snapshotStage=%s actionType=%ld actionPhase=%ld actionRequestLow=%lu actionAgeMs=%lu",
        static_cast<unsigned long>(GetCurrentThreadId()),
        static_cast<unsigned long>(
            g_game_thread.load(std::memory_order_relaxed)),
        SnapshotStage(),
        InterlockedCompareExchange(&g_last_action_type, 0, 0),
        InterlockedCompareExchange(&g_last_action_phase, 0, 0),
        static_cast<unsigned long>(InterlockedCompareExchange(
            &g_last_action_request_low, 0, 0)),
        static_cast<unsigned long>(action_age));
    LogCrashAddress("purecall caller", direct_caller);
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
      char label[32] = {};
      std::snprintf(label, sizeof(label), "purecall frame=%u",
                    static_cast<unsigned>(index));
      LogCrashAddress(label, frames[index]);
    }
  }
  return g_original_purecall
             ? reinterpret_cast<PurecallFn>(g_original_purecall)()
             : 0;
}

void WINAPI HookedExitProcess(UINT code) {
  void* const caller = __builtin_return_address(0);
  LogEmergency(LogLevel::kError,
               "[Exit] ExitProcess tid=%lu code=%u stage=%s",
               static_cast<unsigned long>(GetCurrentThreadId()),
               static_cast<unsigned>(code),
               SnapshotStage());
  LogCrashAddress("ExitProcess caller", caller);
  if (g_original_exit_process)
    reinterpret_cast<ExitProcessFn>(g_original_exit_process)(code);
}

BOOL WINAPI HookedTerminateProcess(HANDLE process, UINT code) {
  void* const caller = __builtin_return_address(0);
  LogEmergency(LogLevel::kError,
               "[Exit] TerminateProcess tid=%lu process=%p code=%u stage=%s",
               static_cast<unsigned long>(GetCurrentThreadId()), process,
               static_cast<unsigned>(code),
               SnapshotStage());
  LogCrashAddress("TerminateProcess caller", caller);
  return g_original_terminate_process
             ? reinterpret_cast<TerminateProcessFn>(
                   g_original_terminate_process)(process, code)
             : FALSE;
}

void __cdecl HookedAbort() {
  void* const caller = __builtin_return_address(0);
  LogEmergency(LogLevel::kError, "[Exit] abort tid=%lu stage=%s",
               static_cast<unsigned long>(GetCurrentThreadId()),
               SnapshotStage());
  LogCrashAddress("abort caller", caller);
  if (g_original_abort) reinterpret_cast<AbortFn>(g_original_abort)();
}

void __cdecl HookedCrtExit(int code) {
  void* const caller = __builtin_return_address(0);
  LogEmergency(LogLevel::kError, "[Exit] _exit tid=%lu code=%d stage=%s",
               static_cast<unsigned long>(GetCurrentThreadId()), code,
               SnapshotStage());
  LogCrashAddress("_exit caller", caller);
  if (g_original_crt_exit)
    reinterpret_cast<CrtExitFn>(g_original_crt_exit)(code);
}

void RestoreExitDiagnostics() {
  if (g_exit_process_slot)
    RestoreIatHook(g_exit_process_slot,
                   reinterpret_cast<void*>(&HookedExitProcess),
                   g_original_exit_process);
  if (g_terminate_process_slot)
    RestoreIatHook(g_terminate_process_slot,
                   reinterpret_cast<void*>(&HookedTerminateProcess),
                   g_original_terminate_process);
  if (g_abort_slot)
    RestoreIatHook(g_abort_slot, reinterpret_cast<void*>(&HookedAbort),
                   g_original_abort);
  if (g_crt_exit_slot)
    RestoreIatHook(g_crt_exit_slot, reinterpret_cast<void*>(&HookedCrtExit),
                   g_original_crt_exit);
  g_exit_process_slot = nullptr;
  g_original_exit_process = nullptr;
  g_terminate_process_slot = nullptr;
  g_original_terminate_process = nullptr;
  g_abort_slot = nullptr;
  g_original_abort = nullptr;
  g_crt_exit_slot = nullptr;
  g_original_crt_exit = nullptr;
}

DWORD WINAPI TickWatchdog(void*) {
  Log(LogLevel::kInfo, "[Lifecycle] game-tick watchdog ready tid=%lu",
      static_cast<unsigned long>(GetCurrentThreadId()));
  while (InterlockedCompareExchange(&g_watchdog_stop, 0, 0) == 0) {
    Sleep(250);
    if (g_tick_in_progress.load(std::memory_order_acquire) == 0) continue;
    const DWORD started =
        g_tick_started_ms.load(std::memory_order_acquire);
    if (!started) continue;
    const DWORD elapsed = GetTickCount() - started;
    if (elapsed < 2000) continue;
    const LONG started_bits = static_cast<LONG>(started);
    if (InterlockedCompareExchange(&g_watchdog_reported_start, started_bits,
                                   started_bits) == started_bits)
      continue;
    InterlockedExchange(&g_watchdog_reported_start, started_bits);
    const char* stage = SnapshotStage();
    Log(LogLevel::kError,
        "[Hang] game tick blocked for %lums tid=%lu stage=%s",
        static_cast<unsigned long>(elapsed),
        static_cast<unsigned long>(
            g_game_thread.load(std::memory_order_relaxed)),
        stage ? stage : "unknown");
  }
  return 0;
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

bool SupportedPlayerPair(void* controller, void* pawn) {
  if (!controller || !pawn || PawnController(pawn) != controller ||
      ControllerPawn(controller) != pawn)
    return false;
  // A respawn/new-world native TnPlayerPawn is valid even while the retained
  // mapping is still needed by the independent AI-controller/TnPlayerPawn
  // cross pair. Adapter hooks validate their own reciprocal pair before
  // substituting, so an unrelated native player pair is not intercepted.
  if (UnrealObjectIsA(pawn, L"TnPlayerPawn")) return true;
  return UnrealObjectIsA(pawn, L"TnAiPawn") &&
         IsMappedPlayerController(controller) &&
         IsPlayerControlledAiPawn(pawn);
}

bool SupportedAiPair(void* controller, void* pawn) {
  if (!controller || !pawn ||
      !UnrealObjectIsA(controller, L"TnAiController") ||
      PawnController(pawn) != controller || ControllerPawn(controller) != pawn)
    return false;
  if (UnrealObjectIsA(pawn, L"TnAiPawn")) return true;
  return UnrealObjectIsA(pawn, L"TnPlayerPawn") &&
         IsMappedAiController(controller) && IsAiControlledPlayerPawn(pawn);
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
  if (!pri || PendingDestroy(pri) ||
      !UnrealReadObjectProperty(pri, L"Team", &team) || !team ||
      PendingDestroy(team))
    return nullptr;
  return team;
}

void LogResolveWaiting(ULONGLONG now, const char* stage) {
  if (now - g_last_resolve_diagnostic < kResolveDiagnosticIntervalMs) return;
  g_last_resolve_diagnostic = now;
  Log(LogLevel::kInfo, "[Resolve] waiting for %s", stage);
}

void ClearWeaponTelemetryCache() {
  g_cached_inventory_manager = nullptr;
  g_cached_current_weapon = nullptr;
  g_cached_stored_vehicle_weapon = nullptr;
  g_cached_stored_robot_weapon = nullptr;
  g_cached_inventory_head = nullptr;
  g_cached_grenade = nullptr;
  g_cached_display_weapon = nullptr;
  g_cached_robot_weapons.fill(nullptr);
  g_cached_has_alternate_weapon = false;
  g_last_weapon_discovery = 0;
}

bool RetireCharacterRoleMapping(const char* reason) {
  if (!CharacterRoleMappingActive()) return true;
  if (!InvalidateCharacterRoleMapping()) {
    SetCommandAcceptance(false);
    DiscardAllCommands();
    Log(LogLevel::kError,
        "[RoleAdapter] failed to retire mapping during lifecycle invalidation reason=%s",
        reason ? reason : "unknown");
    return false;
  }
  // Lifecycle retirement deliberately supersedes any token retained by the
  // last completed transaction through the mapping generation. The adapters
  // stay installed for later worlds.
  Log(LogLevel::kInfo,
      "[RoleAdapter] retired stale mapping reason=%s",
      reason ? reason : "world transition");
  return true;
}

bool MappedPlayerCrossPairLive(const CharacterRoleMapping& mapping) {
  return UnrealObjectValid(mapping.player_controller) &&
         !PendingDestroy(mapping.player_controller) &&
         UnrealObjectIsA(mapping.player_controller, L"TnPlayerController") &&
         UnrealObjectValid(mapping.player_controlled_ai_pawn) &&
         !PendingDestroy(mapping.player_controlled_ai_pawn) &&
         UnrealObjectIsA(mapping.player_controlled_ai_pawn, L"TnAiPawn") &&
         ControllerPawn(mapping.player_controller) ==
             mapping.player_controlled_ai_pawn &&
         PawnController(mapping.player_controlled_ai_pawn) ==
             mapping.player_controller;
}

bool MappedAiCrossPairLive(const CharacterRoleMapping& mapping) {
  return UnrealObjectValid(mapping.mapped_ai_controller) &&
         !PendingDestroy(mapping.mapped_ai_controller) &&
         UnrealObjectIsA(mapping.mapped_ai_controller, L"TnAiController") &&
         UnrealObjectValid(mapping.ai_controlled_player_pawn) &&
         !PendingDestroy(mapping.ai_controlled_player_pawn) &&
         UnrealObjectIsA(mapping.ai_controlled_player_pawn, L"TnPlayerPawn") &&
         ControllerPawn(mapping.mapped_ai_controller) ==
             mapping.ai_controlled_player_pawn &&
         PawnController(mapping.ai_controlled_player_pawn) ==
             mapping.mapped_ai_controller;
}

bool RetireInvalidCharacterRoleMapping() {
  CharacterRoleMapping mapping;
  if (!GetCharacterRoleMapping(&mapping)) return true;
  if (MappedPlayerCrossPairLive(mapping) || MappedAiCrossPairLive(mapping))
    return true;
  return RetireCharacterRoleMapping("no mapped cross-role pair remains live");
}

bool RetireMappingForNativePlayerPair(void* controller, void* pawn) {
  if (!controller || !pawn || !UnrealObjectIsA(pawn, L"TnPlayerPawn") ||
      ControllerPawn(controller) != pawn || PawnController(pawn) != controller)
    return true;
  const auto reopen_after_native_recovery = []() {
    if (!g_role_proxy_failed_closed || g_switch_in_progress ||
        DeferredCheckpointFailedClosed())
      return;
    ResetCharacterRoleProxy();
    g_role_proxy_failed_closed = false;
    g_role_proxy_death_pending = false;
    g_role_proxy_consecutive_failures = 0;
    g_role_proxy_consecutive_successes = 0;
    g_snapshot_build = {};
    g_last_publish = 0;
    SetCommandAcceptance(true);
    Log(LogLevel::kInfo,
        "[RoleAdapter] coherent native player pair recovered; mod input reopened");
  };
  CharacterRoleMapping mapping;
  if (!GetCharacterRoleMapping(&mapping)) {
    reopen_after_native_recovery();
    return true;
  }
  if (MappedPlayerCrossPairLive(mapping) || MappedAiCrossPairLive(mapping))
    return true;
  // A valid native player pair means respawn/new-world setup has superseded
  // the old cross-role graph only after both halves have been dismantled. An
  // empty/transient GamePlayers array alone is not such evidence.
  const bool retired = RetireCharacterRoleMapping(
      "native player pair replaced mapped graph");
  if (retired) reopen_after_native_recovery();
  return retired;
}

void ClearResolvedWorld(const char* reason) {
  if (SwapWorldMutationActive()) {
    LogRateLimited("swap-world-clear", 1000, LogLevel::kWarning,
                   "[SquadSwitch] world invalidation deferred during control transaction");
    return;
  }
  if (!g_controller && !g_pawn && !g_world_info && !g_campaign_game &&
      !g_game_replication_info)
    return;
  // A disappearing world is a hard command boundary. Close the producer
  // gate before dropping the identities it protects and reopen it only after
  // ResolveStableObjects proves a new reciprocal controller/pawn graph.
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_world_command_gate_pending = true;
  if (DeferredCheckpointPending()) {
    FailDeferredCheckpointClosed();
    g_checkpoint_restore = {};
    Log(LogLevel::kError,
        "[Checkpoint] world invalidated before deferred save recovery completed");
  }
  if (!QuiesceCharacterRoleProxy())
    Log(LogLevel::kWarning,
        "[RoleAdapter] proxy quiesce was incomplete during world invalidation");
  g_controller = nullptr;
  g_pawn = nullptr;
  g_world_info = nullptr;
  g_campaign_game = nullptr;
  g_game_replication_info = nullptr;
  g_squad_controllers.fill(nullptr);
  g_squad_pawns.fill(nullptr);
  g_squad_weapons.fill(nullptr);
  g_squad_record_indices.fill(-1);
  g_next_squad_refresh_slot = 0;
  g_objective_manager = nullptr;
  g_objective_marker_base = nullptr;
  g_objective_number = -1;
  ClearWeaponTelemetryCache();
  g_pending_ability_release = {};
  ResetCharacterRoleProxy();
  g_role_proxy_failed_closed = false;
  g_role_proxy_death_pending = false;
  g_role_proxy_consecutive_failures = 0;
  g_role_proxy_consecutive_successes = 0;
  g_swap_recovery_retry_at = 0;
  g_world_ready_since = 0;
  g_last_objective_discovery = 0;
  g_last_squad_discovery = 0;
  g_last_accepted_action.fill(0);
  g_last_accepted_native_action = 0;
  g_snapshot_build = {};
  ++g_world_epoch;
  UnrealResetWorldCaches();
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
  // This resolver runs at the outer game-thread Tick boundary, before the
  // engine submits the current frame. It is therefore the safe lifecycle
  // boundary for retiring a mapping whose actors have actually been
  // destroyed. Do not retire merely because GamePlayers is momentarily empty
  // during streaming.
  if (!RetireInvalidCharacterRoleMapping()) return;
  if (!RetireMappingForNativePlayerPair(g_controller, g_pawn)) return;

  // The local-player walk is necessary while loading or after a transition,
  // but repeating it twice a second in a stable world needlessly traverses
  // GamePlayers, LocalPlayer, controller class ancestry, WorldInfo and GRI.
  // Validate the already-resolved reciprocal relationships first. A changed
  // pawn, world, or replication object deliberately falls through to the full
  // resolver below and advances the world epoch as before.
  if (UnrealObjectValid(g_controller) && UnrealObjectValid(g_pawn) &&
      UnrealObjectValid(g_world_info) && !PendingDestroy(g_controller) &&
      !PendingDestroy(g_pawn) && SupportedPlayerPair(g_controller, g_pawn)) {
    void* current_world = nullptr;
    void* current_gri = nullptr;
    UnrealReadObjectProperty(g_controller, L"WorldInfo", &current_world);
    UnrealReadObjectProperty(g_world_info, L"GRI", &current_gri);
    if (!current_gri && UnrealObjectValid(g_campaign_game))
      UnrealReadObjectProperty(g_campaign_game, L"GameReplicationInfo",
                               &current_gri);
    if (current_world == g_world_info &&
        current_gri == g_game_replication_info) {
      g_resolve_retry_at = now + kStableResolveIntervalMs;
      return;
    }
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
  if (!RetireMappingForNativePlayerPair(controller, pawn)) return;
  void* world_info = nullptr;
  if (!pawn || !SupportedPlayerPair(controller, pawn) ||
      PendingDestroy(pawn) ||
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
                             world_info != g_world_info ||
                             campaign_game != g_campaign_game ||
                             replication_info != g_game_replication_info;
  g_controller = controller;
  g_pawn = pawn;
  g_world_info = world_info;
  g_campaign_game = campaign_game;
  g_game_replication_info = replication_info;
  if (world_changed) {
    g_world_ready_since = now;
    g_last_objective_discovery = 0;
    g_last_squad_discovery = 0;
    g_pending_ability_release = {};
    g_last_accepted_action.fill(0);
    g_last_accepted_native_action = 0;
    g_squad_controllers.fill(nullptr);
    g_squad_pawns.fill(nullptr);
    g_squad_weapons.fill(nullptr);
    g_squad_record_indices.fill(-1);
    g_next_squad_refresh_slot = 0;
    g_objective_manager = nullptr;
    g_objective_marker_base = nullptr;
    g_objective_number = -1;
    ClearWeaponTelemetryCache();
    g_snapshot_build = {};
    ++g_world_epoch;
    UnrealResetWorldCaches();
    RejectStaleCommands(g_world_epoch);
    Log(LogLevel::kInfo,
        "[Resolve] direct player controller=%p pawn=%p world=%p game=%p gri=%p epoch=%llu",
        controller, pawn, world_info, campaign_game, replication_info,
        static_cast<unsigned long long>(g_world_epoch));
  } else if (!g_world_ready_since) {
    g_world_ready_since = now;
  }
  if (g_world_command_gate_pending && !g_switch_in_progress &&
      !g_role_proxy_failed_closed && !g_role_proxy_death_pending &&
      !DeferredCheckpointFailedClosed() &&
      (!CharacterRoleMappingActive() ||
       CharacterRoleMappingObjectsValid())) {
    g_world_command_gate_pending = false;
    SetCommandAcceptance(true);
    Log(LogLevel::kInfo,
        "[Lifecycle] stable player world resolved; mod input reopened");
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
  char selected_identity[128] = {};
  if (CampaignClassSelectedCharacterName(raw, selected_identity,
                                         sizeof(selected_identity)))
    CopyText(raw, sizeof(raw), selected_identity);
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

bool ReadVitals(void* pawn, float* health, float* maximum, bool* downed,
                bool* alive) {
  *health = -1.0f;
  *maximum = -1.0f;
  *downed = false;
  *alive = false;
  const bool valid = UnrealObjectValid(pawn) && !PendingDestroy(pawn);
  const bool have_health = UnrealReadFloatProperty(pawn, L"Health", health);
  const bool have_maximum =
      UnrealReadFloatProperty(pawn, L"HealthMax", maximum);
  const bool have_downed =
      UnrealReadBoolProperty(pawn, L"_IsDowned", downed);
  bool dead = false;
  const bool have_dead =
      UnrealReadBoolProperty(pawn, L"_DeferredDied", &dead);
  const bool complete = valid && have_health && have_maximum && have_downed &&
                        have_dead && std::isfinite(*health) &&
                        std::isfinite(*maximum) && *maximum > 0.0f;
  if (complete) dead = dead || *health <= 0.0f;
  *alive = complete && !dead;
  return complete;
}

bool ReadRestrictions(Snapshot* state, bool require_complete = false) {
  bool complete = UnrealObjectValid(g_world_info) &&
                  UnrealObjectValid(g_controller) &&
                  UnrealObjectValid(g_pawn);
  bool paused = false, combat = false;
  bool movie = false, cinematic = false, nonmovie = false;
  bool disable_move = false, disable_look = false, frozen = false;
  bool matinee = false;
  void* pauser = nullptr;
  complete = UnrealReadObjectProperty(g_world_info, L"Pauser", &pauser) &&
             complete;
  paused = pauser != nullptr;
  complete = UnrealReadBoolProperty(g_controller, L"bFullScreenMoviePlaying",
                                    &movie) && complete;
  complete = UnrealReadBoolProperty(g_controller, L"bCinematicMode",
                                    &cinematic) && complete;
  complete = UnrealReadBoolProperty(g_controller, L"bNonMovieCinematicMode",
                                    &nonmovie) && complete;
  complete = UnrealReadBoolProperty(g_controller, L"bCinemaDisableInputMove",
                                    &disable_move) && complete;
  complete = UnrealReadBoolProperty(g_controller, L"bCinemaDisableInputLook",
                                    &disable_look) && complete;
  complete = UnrealReadBoolProperty(g_controller, L"bFrozen", &frozen) &&
             complete;
  complete = UnrealReadBoolProperty(g_pawn, L"_MatineeAnimationPlaying",
                                    &matinee) && complete;
  uint8_t move_ignored_value = 0;
  uint8_t look_ignored_value = 0;
  complete = UnrealReadByteProperty(g_controller, L"bIgnoreMoveInput",
                                    &move_ignored_value) && complete;
  complete = UnrealReadByteProperty(g_controller, L"bIgnoreLookInput",
                                    &look_ignored_value) && complete;
  const bool move_ignored = move_ignored_value != 0;
  const bool look_ignored = look_ignored_value != 0;
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
  const bool have_ui_controller =
      UnrealReadObjectProperty(g_controller, L"UIController", &ui_controller);
  complete = have_ui_controller && complete;
  bool have_current_ui = true;
  if (ui_controller)
    have_current_ui =
        UnrealReadObjectProperty(ui_controller, L"_CurrentUI", &current_ui);
  complete = have_current_ui && complete;
  const bool menu = current_ui != nullptr;
  void* view_target = nullptr;
  complete = UnrealReadObjectProperty(g_controller, L"ViewTarget",
                                      &view_target) && complete;
  const bool foreign_view = view_target && view_target != g_pawn;
  uint8_t interact_state = 0;
  const bool have_interact = UnrealReadByteProperty(
      g_controller, L"PlayerInteractState", &interact_state);
  complete = have_interact && complete;
  const bool interacting = have_interact && interact_state >= 2;
  void* turret = nullptr;
  const bool have_turret =
      UnrealReadObjectProperty(g_controller, L"TurretInUse", &turret);
  complete = have_turret && complete;
  const bool using_turret = have_turret && turret && !PendingDestroy(turret);
  state->paused = paused;
  state->in_combat = combat;
  state->in_cutscene = movie || cinematic || nonmovie || matinee || foreign_view;
  state->in_menu = menu;
  state->in_scripted_sequence = disable_move || disable_look || frozen ||
                                move_ignored || look_ignored || interacting ||
                                using_turret;
  // Combat is ordinary live gameplay. Character transfer serializes and
  // suspends the selected AI's current plan before changing ownership, so it
  // is gated by the same cutscene/menu/script restrictions as other actions.
  return (!require_complete || complete) &&
         !(paused || state->in_cutscene || menu ||
           state->in_scripted_sequence);
}

void ReadAbilityState(void* ability, float cooldown, float duration,
                      bool cooldown_pending, AbilityState* output) {
  if (!ability || !UnrealObjectIsA(ability, L"TnAbility") ||
      PendingDestroy(ability))
    return;
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

bool ReadAbilities(Snapshot* state, void** manager_output) {
  if (!state) return false;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"AbilityManager", &manager) ||
      !manager || !UnrealObjectIsA(manager, L"TnAbilityManager") ||
      PendingDestroy(manager))
    return false;
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
        !ability || !UnrealObjectIsA(ability, L"TnAbility") ||
        PendingDestroy(ability)) {
      state->abilities[slot] = {};
      continue;
    }
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
    const uint32_t ability_index = UnrealObjectIndex(ability);
    const AbilityState previous = state->abilities[slot];
    const bool same_ability = previous.object_index == ability_index;
    float cooldown_duration = same_ability
                                  ? previous.cooldown_duration
                                  : -1.0f;
    if (!same_ability) {
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
    }
    const bool cooldown_pending =
        (std::isfinite(cooldown) && cooldown > 86400.0f) ||
        (std::isfinite(spam_cooldown) && spam_cooldown > 86400.0f);
    float effective_cooldown = -1.0f;
    if (std::isfinite(cooldown) && cooldown >= 0.0f)
      effective_cooldown = cooldown;
    if (std::isfinite(spam_cooldown) && spam_cooldown >= 0.0f)
      effective_cooldown = std::max(effective_cooldown, spam_cooldown);
    // SpamPreventionCooldowns is ticked by the same native ability manager as
    // Cooldowns. Exposing their maximum keeps the indicator counting down for
    // the whole interval in which the game itself rejects another press.
    AbilityState refreshed = same_ability ? previous : AbilityState{};
    if (same_ability) {
      refreshed.cooldown_remaining = cooldown_pending ? -1.0f
                                                       : effective_cooldown;
      refreshed.cooldown_duration = cooldown_duration;
      refreshed.cooldown_pending = cooldown_pending ? 1 : 0;
    } else {
      ReadAbilityState(ability, effective_cooldown, cooldown_duration,
                       cooldown_pending, &refreshed);
    }
    state->abilities[slot] = refreshed;
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
  return have_abilities;
}

void FillWeapon(void* weapon, void* active, WeaponState* output) {
  if (!output || !weapon || !UnrealObjectIsA(weapon, L"TnWeapon") ||
      PendingDestroy(weapon))
    return;
  const uint32_t object_index = UnrealObjectIndex(weapon);
  const bool same_weapon = output->object_index == object_index;
  if (!same_weapon) *output = {};
  output->object_index = object_index;
  output->available = 1;
  output->equipped = weapon == active;
  if (!same_weapon) {
    char class_name[128] = {};
    UnrealObjectClassName(weapon, class_name, sizeof(class_name));
    CopyText(output->name, sizeof(output->name),
             Humanize(class_name, "TnWeapon").c_str());
    CopyText(output->icon_key, sizeof(output->icon_key),
             AssetFor(class_name, kWeaponAssets,
                      sizeof(kWeaponAssets) / sizeof(kWeaponAssets[0])));
    void* data = nullptr;
    if (UnrealReadObjectProperty(weapon, L"Data", &data) && data &&
        UnrealObjectIsA(data, L"TnWeaponData") && !PendingDestroy(data)) {
      UnrealReadIntProperty(data, L"MaxAmmoClipCount",
                            &output->magazine_capacity);
      UnrealReadIntProperty(data, L"MaxAmmoCount", &output->reserve_capacity);
      bool unlimited = false;
      if (UnrealReadBoolProperty(data, L"bHasUnlimitedReserveAmmo",
                                 &unlimited))
        output->unlimited_reserve = unlimited ? 1 : 0;
      UnrealReadStructFloatProperty(data, L"HeatProperties", L"HeatMax",
                                    &output->heat_capacity);
    }
    int32_t ammo_increase = 0;
    if (output->reserve_capacity >= 0 &&
        UnrealReadIntProperty(weapon, L"AmmoIncrease", &ammo_increase)) {
      const int64_t adjusted = static_cast<int64_t>(output->reserve_capacity) +
                               static_cast<int64_t>(ammo_increase);
      if (adjusted >= 0 && adjusted <= 1000000)
        output->reserve_capacity = static_cast<int32_t>(adjusted);
    }
    output->clipless = output->magazine_capacity == 0 ? 1 : 0;
  }
  UnrealReadIntProperty(weapon, L"AmmoClipCount", &output->magazine);
  UnrealReadIntProperty(weapon, L"ReserveAmmoCount", &output->reserve);
  void* weapon_heat = nullptr;
  if (UnrealReadObjectProperty(weapon, L"WeaponHeat", &weapon_heat) &&
      weapon_heat && UnrealObjectIsA(weapon_heat, L"TnWeaponHeat") &&
      !PendingDestroy(weapon_heat)) {
    UnrealReadFloatProperty(weapon_heat, L"CurrentHeat", &output->heat);
    float live_heat_capacity = -1.0f;
    if (UnrealReadStructFloatProperty(weapon_heat, L"HeatProperties",
                                      L"HeatMax", &live_heat_capacity) &&
        std::isfinite(live_heat_capacity) && live_heat_capacity > 0.0f)
      output->heat_capacity = live_heat_capacity;
  }
  output->heat_based = std::isfinite(output->heat_capacity) &&
                               output->heat_capacity > 0.0f
                           ? 1
                           : 0;
}

bool IsRangedWeapon(void* inventory) {
  if (!inventory || !UnrealObjectIsA(inventory, L"TnWeapon") ||
      UnrealObjectIsA(inventory, L"TnGrenadeBag") ||
      UnrealObjectIsA(inventory, L"TnMeleeWeapon"))
    return false;
  bool melee_weapon = false;
  if (UnrealReadBoolProperty(inventory, L"bMeleeWeapon", &melee_weapon) &&
      melee_weapon)
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

bool IsRobotWeapon(void* weapon, void* stored_vehicle_weapon) {
  if (!IsRangedWeapon(weapon) || weapon == stored_vehicle_weapon)
    return false;
  char class_name[128] = {};
  if (!UnrealObjectClassName(weapon, class_name, sizeof(class_name)))
    return false;
  // StoreVehicleWeapon leaves the vehicle gun in the native inventory chain
  // on this build. The authoritative StoredVehicleWeapon pointer catches the
  // active instance; the class guard catches additional vehicle-form weapons
  // in loadouts that expose more than one.
  return Token(class_name).find("vehicle") == std::string::npos;
}

bool IsCycleableRobotWeapon(void* weapon, void* stored_vehicle_weapon) {
  if (!IsRobotWeapon(weapon, stored_vehicle_weapon)) return false;
  bool excluded = false;
  bool switching_disallowed = false;
  return UnrealReadBoolProperty(weapon, L"ExcludeFromWeaponCycling",
                                &excluded) &&
         UnrealReadBoolProperty(weapon, L"DisallowWeaponSwitching",
                                &switching_disallowed) &&
         !excluded && !switching_disallowed;
}

enum class DiscoverySliceResult : uint8_t {
  kPending,
  kComplete,
  kInvalid,
};

struct WeaponRoots {
  void* manager = nullptr;
  void* active = nullptr;
  void* stored_vehicle = nullptr;
  void* stored_robot = nullptr;
  void* inventory_head = nullptr;
};

bool RetainedObjectMatches(void* retained, uint32_t retained_index,
                           void* current) {
  if (retained != current) return false;
  if (!retained) return retained_index == 0;
  return retained_index && UnrealObjectValid(retained) &&
         UnrealObjectIndex(retained) == retained_index &&
         !PendingDestroy(retained);
}

bool ReadWeaponRoots(void* pawn, WeaponRoots* output) {
  if (!output) return false;
  *output = {};
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(pawn, L"InvManager", &manager)) return false;
  if (!manager || !UnrealObjectIsA(manager, L"TnInventoryManager") ||
      PendingDestroy(manager))
    return false;
  void* active = nullptr;
  // TnInventoryManager has a GetCurrentWeapon() function, not a reflected
  // CurrentWeapon field. The function ultimately exposes Pawn(Owner).Weapon;
  // StoreRobotWeapon/StoreVehicleWeapon use that same Pawn.Weapon root.
  if (!UnrealReadObjectProperty(pawn, L"Weapon", &active)) return false;
  if (active && (!UnrealObjectIsA(active, L"TnWeapon") ||
                 PendingDestroy(active)))
    active = nullptr;
  void* stored_vehicle_weapon = nullptr;
  void* stored_robot_weapon = nullptr;
  UnrealReadObjectProperty(manager, L"StoredVehicleWeapon",
                           &stored_vehicle_weapon);
  UnrealReadObjectProperty(manager, L"StoredRobotWeapon",
                           &stored_robot_weapon);
  if (stored_vehicle_weapon &&
      (!UnrealObjectIsA(stored_vehicle_weapon, L"TnWeapon") ||
       PendingDestroy(stored_vehicle_weapon)))
    stored_vehicle_weapon = nullptr;
  if (stored_robot_weapon &&
      (!UnrealObjectIsA(stored_robot_weapon, L"TnWeapon") ||
       PendingDestroy(stored_robot_weapon)))
    stored_robot_weapon = nullptr;
  void* inventory = nullptr;
  UnrealReadObjectProperty(manager, L"InventoryChain", &inventory);
  if (inventory &&
      (!UnrealObjectValid(inventory) || PendingDestroy(inventory)))
    return false;
  output->manager = manager;
  output->active = active;
  output->stored_vehicle = stored_vehicle_weapon;
  output->stored_robot = stored_robot_weapon;
  output->inventory_head = inventory;
  return true;
}

bool WeaponRootsStillCurrent(const SnapshotBuild& build,
                             WeaponRoots* current_output) {
  WeaponRoots current;
  if (!ReadWeaponRoots(build.pawn, &current) ||
      !RetainedObjectMatches(build.weapon_manager,
                             build.weapon_manager_object_index,
                             current.manager) ||
      !RetainedObjectMatches(build.weapon_active,
                             build.weapon_active_object_index,
                             current.active) ||
      !RetainedObjectMatches(build.weapon_stored_vehicle,
                             build.weapon_stored_vehicle_object_index,
                             current.stored_vehicle) ||
      !RetainedObjectMatches(build.weapon_stored_robot,
                             build.weapon_stored_robot_object_index,
                             current.stored_robot) ||
      !RetainedObjectMatches(build.weapon_inventory_head,
                             build.weapon_inventory_head_object_index,
                             current.inventory_head))
    return false;
  if (build.weapon_grenade &&
      !RetainedObjectMatches(build.weapon_grenade,
                             build.weapon_grenade_object_index,
                             build.weapon_grenade))
    return false;
  for (uint32_t index = 0; index < build.weapon_candidate_count; ++index) {
    if (!RetainedObjectMatches(
            build.weapon_candidates[index],
            build.weapon_candidate_object_indices[index],
            build.weapon_candidates[index]))
      return false;
  }
  if (current_output) *current_output = current;
  return true;
}

void AddWeaponCandidate(SnapshotBuild* build, void* candidate) {
  if (!build || !candidate ||
      !IsCycleableRobotWeapon(candidate, build->weapon_stored_vehicle))
    return;
  for (uint32_t index = 0; index < build->weapon_candidate_count; ++index)
    if (build->weapon_candidates[index] == candidate) return;
  if (build->weapon_candidate_count < build->weapon_candidates.size()) {
    const uint32_t slot = build->weapon_candidate_count++;
    build->weapon_candidates[slot] = candidate;
    build->weapon_candidate_object_indices[slot] =
        UnrealObjectIndex(candidate);
  }
}

bool BeginWeaponDiscovery(SnapshotBuild* build) {
  if (!build) return false;
  WeaponRoots roots;
  if (!ReadWeaponRoots(build->pawn, &roots)) return false;
  build->weapon_discovery_active = true;
  build->weapon_discovery_complete = roots.inventory_head == nullptr;
  build->weapon_manager = roots.manager;
  build->weapon_manager_object_index = UnrealObjectIndex(roots.manager);
  build->weapon_active = roots.active;
  build->weapon_active_object_index = UnrealObjectIndex(roots.active);
  build->weapon_stored_vehicle = roots.stored_vehicle;
  build->weapon_stored_vehicle_object_index =
      UnrealObjectIndex(roots.stored_vehicle);
  build->weapon_stored_robot = roots.stored_robot;
  build->weapon_stored_robot_object_index =
      UnrealObjectIndex(roots.stored_robot);
  build->weapon_inventory_head = roots.inventory_head;
  build->weapon_inventory_head_object_index =
      UnrealObjectIndex(roots.inventory_head);
  build->weapon_inventory_cursor = roots.inventory_head;
  build->weapon_inventory_cursor_object_index =
      UnrealObjectIndex(roots.inventory_head);
  // TnInventoryManager.AddInventory names a function-local variable
  // ActiveGrenadeBag; the class has no persistent property with that name.
  // Discover the live TnGrenadeBag from InventoryChain below, exactly as the
  // native AddInventory implementation does via FindInventoryType.
  build->weapon_grenade = nullptr;
  build->weapon_grenade_object_index = 0;
  return true;
}

DiscoverySliceResult ScanWeaponDiscoverySlice(SnapshotBuild* build) {
  if (!build || !build->weapon_discovery_active)
    return DiscoverySliceResult::kInvalid;
  if (build->weapon_discovery_complete)
    return DiscoverySliceResult::kComplete;
  if (!WeaponRootsStillCurrent(*build, nullptr))
    return DiscoverySliceResult::kInvalid;

  void* inventory = build->weapon_inventory_cursor;
  if (!RetainedObjectMatches(inventory,
                             build->weapon_inventory_cursor_object_index,
                             inventory))
    return DiscoverySliceResult::kInvalid;
  if (build->weapon_inventory_previous) {
    if (!RetainedObjectMatches(build->weapon_inventory_previous,
                               build->weapon_inventory_previous_object_index,
                               build->weapon_inventory_previous))
      return DiscoverySliceResult::kInvalid;
    void* linked = nullptr;
    if (!UnrealReadObjectProperty(build->weapon_inventory_previous,
                                  L"Inventory", &linked) ||
        linked != inventory)
      return DiscoverySliceResult::kInvalid;
  } else if (inventory != build->weapon_inventory_head) {
    return DiscoverySliceResult::kInvalid;
  }

  if (!build->weapon_grenade &&
      UnrealObjectIsA(inventory, L"TnGrenadeBag")) {
    build->weapon_grenade = inventory;
    build->weapon_grenade_object_index = UnrealObjectIndex(inventory);
  }
  AddWeaponCandidate(build, inventory);
  void* next = nullptr;
  if (!UnrealReadObjectProperty(inventory, L"Inventory", &next))
    return DiscoverySliceResult::kInvalid;
  ++build->weapon_nodes_examined;
  if (!next || next == inventory || build->weapon_nodes_examined >= 32) {
    build->weapon_discovery_complete = true;
    build->weapon_inventory_cursor = nullptr;
    build->weapon_inventory_cursor_object_index = 0;
    return DiscoverySliceResult::kComplete;
  }
  build->weapon_inventory_previous = inventory;
  build->weapon_inventory_previous_object_index = UnrealObjectIndex(inventory);
  build->weapon_inventory_cursor = next;
  build->weapon_inventory_cursor_object_index = UnrealObjectIndex(next);
  return DiscoverySliceResult::kPending;
}

bool FinishWeaponDiscovery(SnapshotBuild* build, Snapshot* state,
                           void** manager_output, void** active_output,
                           void** grenade_output,
                           bool* has_alternate_output) {
  if (!build || !state || !build->weapon_discovery_active ||
      !build->weapon_discovery_complete ||
      !WeaponRootsStillCurrent(*build, nullptr))
    return false;
  if (has_alternate_output) *has_alternate_output = false;

  // In vehicle form CurrentWeapon is the vehicle gun while StoredRobotWeapon
  // is the robot weapon that must return after transforming. Add both possible
  // robot pointers after the chain so ordinary two-weapon order stays stable.
  AddWeaponCandidate(build, build->weapon_stored_robot);
  AddWeaponCandidate(build, build->weapon_active);
  void* const display_active =
      IsRobotWeapon(build->weapon_active, build->weapon_stored_vehicle)
          ? build->weapon_active
          : build->weapon_stored_robot;
  std::array<void*, 2> weapons = {};
  uint32_t weapon_count = 0;
  // InventoryChain also contains ranged utility/vehicle entries which are not
  // part of the player's two robot-form weapon cycle. Only those accepted by
  // NextWeapon belong on the two cards.
  for (uint32_t index = 0;
       index < build->weapon_candidate_count && weapon_count < weapons.size();
       ++index) {
    void* const candidate = build->weapon_candidates[index];
    if (IsCycleableRobotWeapon(candidate, build->weapon_stored_vehicle))
      weapons[weapon_count++] = candidate;
  }
  // In vehicle form CurrentWeapon is the vehicle gun while StoredRobotWeapon
  // is the robot weapon that must return after transforming. A malformed or
  // special chain must never push that actual robot weapon off both cards.
  if (display_active) {
    bool represented = false;
    for (void* weapon : weapons) represented |= weapon == display_active;
    if (!represented) {
      if (weapon_count < weapons.size())
        weapons[weapon_count++] = display_active;
      else
        weapons.back() = display_active;
    }
  }
  if (has_alternate_output && display_active && weapon_count == weapons.size() &&
      IsCycleableRobotWeapon(display_active, build->weapon_stored_vehicle))
    *has_alternate_output = weapons[0] != weapons[1] &&
                            (weapons[0] == display_active ||
                             weapons[1] == display_active);
  if (manager_output) *manager_output = build->weapon_manager;
  if (active_output) *active_output = build->weapon_active;
  if (grenade_output) *grenade_output = build->weapon_grenade;
  for (uint32_t i = 0; i < weapons.size(); ++i) {
    if (weapons[i])
      FillWeapon(weapons[i], display_active, &state->weapons[i]);
    else
      state->weapons[i] = {};
  }
  if (weapon_count) state->capabilities |= kCapWeapons;
  if (build->weapon_grenade) {
    if (!UnrealReadIntProperty(build->weapon_grenade, L"AmmoCount",
                               &state->grenade_count))
      UnrealReadIntProperty(build->weapon_grenade, L"ReserveAmmoCount",
                            &state->grenade_count);
    char class_name[128] = {};
    UnrealObjectClassName(build->weapon_grenade, class_name,
                          sizeof(class_name));
    CopyText(state->grenade_name, sizeof(state->grenade_name),
             Humanize(class_name, "TnWeapon").c_str());
    CopyText(state->grenade_icon_key, sizeof(state->grenade_icon_key),
             AssetFor(class_name, kGrenadeAssets,
                      sizeof(kGrenadeAssets) / sizeof(kGrenadeAssets[0])));
    state->capabilities |= kCapGrenades;
  }
  g_cached_inventory_manager = build->weapon_manager;
  g_cached_current_weapon = build->weapon_active;
  g_cached_stored_vehicle_weapon = build->weapon_stored_vehicle;
  g_cached_stored_robot_weapon = build->weapon_stored_robot;
  g_cached_inventory_head = build->weapon_inventory_head;
  g_cached_grenade = build->weapon_grenade;
  g_cached_display_weapon = display_active;
  g_cached_robot_weapons = weapons;
  g_cached_has_alternate_weapon =
      has_alternate_output ? *has_alternate_output : false;
  g_last_weapon_discovery = GetTickCount64();
  return true;
}

bool RefreshCachedWeapons(Snapshot* state, void** manager_output,
                          void** active_output, void** grenade_output,
                          bool* has_alternate_output) {
  if (!state || !g_cached_inventory_manager || !g_last_weapon_discovery ||
      GetTickCount64() - g_last_weapon_discovery >= kWeaponAuditIntervalMs)
    return false;
  void* manager = nullptr;
  void* active = nullptr;
  void* stored_vehicle = nullptr;
  void* stored_robot = nullptr;
  void* inventory_head = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"InvManager", &manager) ||
      manager != g_cached_inventory_manager || PendingDestroy(manager) ||
      !UnrealReadObjectProperty(g_pawn, L"Weapon", &active))
    return false;
  UnrealReadObjectProperty(manager, L"StoredVehicleWeapon", &stored_vehicle);
  UnrealReadObjectProperty(manager, L"StoredRobotWeapon", &stored_robot);
  UnrealReadObjectProperty(manager, L"InventoryChain", &inventory_head);
  if (active != g_cached_current_weapon ||
      stored_vehicle != g_cached_stored_vehicle_weapon ||
      stored_robot != g_cached_stored_robot_weapon ||
      inventory_head != g_cached_inventory_head)
    return false;
  void* const grenade = g_cached_grenade;
  if (grenade &&
      (!UnrealObjectValid(grenade) || PendingDestroy(grenade) ||
       !UnrealObjectIsA(grenade, L"TnGrenadeBag")))
    return false;

  if (g_cached_display_weapon &&
      (!UnrealObjectValid(g_cached_display_weapon) ||
       PendingDestroy(g_cached_display_weapon) ||
       !UnrealObjectIsA(g_cached_display_weapon, L"TnWeapon")))
    return false;
  for (uint32_t slot = 0; slot < g_cached_robot_weapons.size(); ++slot) {
    void* weapon = g_cached_robot_weapons[slot];
    if (weapon &&
        (!UnrealObjectValid(weapon) || PendingDestroy(weapon) ||
         !UnrealObjectIsA(weapon, L"TnWeapon")))
      return false;
    if (weapon)
      FillWeapon(weapon, g_cached_display_weapon, &state->weapons[slot]);
    else
      state->weapons[slot] = {};
  }
  if (g_cached_robot_weapons[0] || g_cached_robot_weapons[1])
    state->capabilities |= kCapWeapons;
  else
    state->capabilities &= ~kCapWeapons;
  if (grenade && !PendingDestroy(grenade)) {
    if (!UnrealReadIntProperty(grenade, L"AmmoCount", &state->grenade_count))
      UnrealReadIntProperty(grenade, L"ReserveAmmoCount",
                            &state->grenade_count);
    state->capabilities |= kCapGrenades;
  } else {
    state->grenade_count = -1;
    state->capabilities &= ~kCapGrenades;
  }
  if (manager_output) *manager_output = manager;
  if (active_output) *active_output = active;
  if (grenade_output) *grenade_output = grenade;
  if (has_alternate_output)
    *has_alternate_output = g_cached_has_alternate_weapon;
  return true;
}

void FillSquadMember(void* controller, void* pawn, bool controlled,
                     bool switching_allowed, const char* authoritative_name,
                     SquadMemberState* output) {
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
  UnrealReadObjectProperty(pawn, L"Weapon", &weapon);
  if (weapon) {
    char weapon_class[128] = {};
    if (UnrealObjectClassName(weapon, weapon_class, sizeof(weapon_class)))
      CopyText(output->weapon_name, sizeof(output->weapon_name),
               Humanize(weapon_class, "TnWeapon").c_str());
  }
  output->identity_hash = IdentityHash(output->name, output->pawn_object_index);
  output->switchable = switching_allowed && !controlled && alive && !downed &&
                       output->mode != TransformerMode::kUnknown &&
                       output->mode != TransformerMode::kTransitioning;
}

void* CurrentPawnWeapon(void* pawn) {
  void* weapon = nullptr;
  if (!pawn || !UnrealReadObjectProperty(pawn, L"Weapon", &weapon) ||
      !weapon || PendingDestroy(weapon))
    return nullptr;
  return weapon;
}

bool ReadAiSquadObject(void* campaign_game, void** output) {
  if (!output) return false;
  *output = nullptr;
  if (!campaign_game) return true;
  if (!UnrealObjectValid(campaign_game) || PendingDestroy(campaign_game))
    return false;
  void* squad = nullptr;
  // TnCampaignGameBase exposes the authoritative squad through native
  // GetAiSquad/get_AiSquad accessors; AiSquad is not a reflected field on this
  // build. The field fallbacks are retained for derived/campaign variants.
  bool resolved = UnrealCallObject(campaign_game, L"GetAiSquad", &squad);
  if ((!resolved || !squad) &&
      UnrealCallObject(campaign_game, L"get_AiSquad", &squad))
    resolved = true;
  if ((!resolved || !squad) &&
      UnrealReadObjectProperty(campaign_game, L"AiSquad", &squad))
    resolved = true;
  if ((!resolved || !squad) &&
      UnrealReadObjectProperty(campaign_game, L"_AiSquad", &squad))
    resolved = true;
  if (!resolved) return true;
  if (squad && (!UnrealObjectIsA(squad, L"TnAiSquad") ||
                PendingDestroy(squad)))
    return false;
  *output = squad;
  return true;
}

bool SameStructArrayView(const UnrealStructArrayView& retained,
                         uint32_t retained_struct_index,
                         const UnrealStructArrayView& current) {
  return retained.data == current.data && retained.count == current.count &&
         retained.capacity == current.capacity &&
         retained.element_size == current.element_size &&
         RetainedObjectMatches(retained.struct_definition,
                               retained_struct_index,
                               current.struct_definition);
}

bool SquadRootsStillCurrent(const SnapshotBuild& build) {
  if (!RetainedObjectMatches(build.squad_campaign_game,
                             build.squad_campaign_game_object_index,
                             g_campaign_game))
    return false;
  void* const current_team = TeamFor(build.controller, build.pawn);
  if (!RetainedObjectMatches(build.squad_player_team,
                             build.squad_player_team_object_index,
                             current_team))
    return false;
  void* current_squad = nullptr;
  if (!ReadAiSquadObject(build.squad_campaign_game, &current_squad) ||
      !RetainedObjectMatches(build.squad_object, build.squad_object_index,
                             current_squad))
    return false;
  if (!build.squad_object) return !build.squad_records_available;
  UnrealStructArrayView current_records;
  const bool records_available = UnrealReadStructArrayProperty(
      build.squad_object, L"Members", &current_records);
  if (records_available != build.squad_records_available) return false;
  return !records_available ||
         SameStructArrayView(build.squad_records,
                             build.squad_record_struct_object_index,
                             current_records);
}

bool SquadCandidateDuplicate(const SnapshotBuild& build, void* controller,
                             void* pawn) {
  for (uint32_t slot = 0; slot < build.state.squad_count; ++slot) {
    if (build.squad_controllers[slot] == controller ||
        build.squad_pawns[slot] == pawn)
      return true;
  }
  return false;
}

int32_t FindSquadRecordIndexForController(
    const SnapshotBuild& build, void* expected_controller) {
  if (!expected_controller || !build.squad_records_available ||
      build.squad_record_count < 1)
    return -1;
  for (int32_t index = 0; index < build.squad_record_count; ++index) {
    void* controller = nullptr;
    if (UnrealReadStructArrayObjectField(build.squad_records, index,
                                         L"CharacterController",
                                         &controller) &&
        controller == expected_controller)
      return index;
  }
  return -1;
}

bool SquadCandidatesStillCurrent(const SnapshotBuild& build) {
  if (!build.state.squad_count ||
      build.state.squad_count > kSquadCapacity)
    return false;
  for (uint32_t slot = 0; slot < build.state.squad_count; ++slot) {
    void* controller = build.squad_controllers[slot];
    void* pawn = build.squad_pawns[slot];
    const SquadMemberState& member = build.state.squad[slot];
    if (!RetainedObjectMatches(controller, member.controller_object_index,
                               controller) ||
        !RetainedObjectMatches(pawn, member.pawn_object_index, pawn) ||
        ControllerPawn(controller) != pawn || PawnController(pawn) != controller)
      return false;
    if ((slot == 0 && !SupportedPlayerPair(controller, pawn)) ||
        (slot != 0 && !SupportedAiPair(controller, pawn)))
      return false;
  }
  return true;
}

bool SquadControllerRootStillCurrent(const SnapshotBuild& build) {
  if (!build.squad_controller_scan_initialized) return true;
  void* current_head = nullptr;
  return UnrealReadObjectProperty(build.world_info, L"ControllerList",
                                  &current_head) &&
         RetainedObjectMatches(build.squad_controller_head,
                               build.squad_controller_head_object_index,
                               current_head);
}

bool BeginSquadControllerDiscovery(SnapshotBuild* build) {
  if (!build) return false;
  if (build->state.squad_count >= kSquadCapacity) {
    build->squad_discovery_phase = SquadDiscoveryPhase::kComplete;
    return true;
  }
  void* controller = nullptr;
  if (!UnrealReadObjectProperty(build->world_info, L"ControllerList",
                                &controller))
    return false;
  if (controller &&
      (!UnrealObjectValid(controller) || PendingDestroy(controller)))
    return false;
  build->squad_controller_scan_initialized = true;
  build->squad_controller_head = controller;
  build->squad_controller_head_object_index = UnrealObjectIndex(controller);
  build->squad_controller_cursor = controller;
  build->squad_controller_cursor_object_index = UnrealObjectIndex(controller);
  build->squad_discovery_phase = controller
                                     ? SquadDiscoveryPhase::kControllers
                                     : SquadDiscoveryPhase::kComplete;
  return true;
}

bool BeginSquadDiscovery(SnapshotBuild* build) {
  if (!build) return false;
  for (SquadMemberState& member : build->state.squad) member = {};
  build->squad_controllers.fill(nullptr);
  build->squad_pawns.fill(nullptr);
  build->squad_weapons.fill(nullptr);
  build->squad_record_indices.fill(-1);
  FillSquadMember(build->controller, build->pawn, true, false, nullptr,
                  &build->state.squad[0]);
  build->squad_controllers[0] = build->controller;
  build->squad_pawns[0] = build->pawn;
  build->squad_weapons[0] = CurrentPawnWeapon(build->pawn);
  build->state.squad_count = 1;
  build->squad_campaign_game = g_campaign_game;
  build->squad_campaign_game_object_index =
      UnrealObjectIndex(g_campaign_game);
  build->squad_player_team = TeamFor(build->controller, build->pawn);
  build->squad_player_team_object_index =
      UnrealObjectIndex(build->squad_player_team);
  if (!ReadAiSquadObject(build->squad_campaign_game, &build->squad_object))
    return false;
  build->squad_object_index = UnrealObjectIndex(build->squad_object);
  if (build->squad_object) {
    build->squad_records_available = UnrealReadStructArrayProperty(
        build->squad_object, L"Members", &build->squad_records);
    if (build->squad_records_available) {
      build->squad_record_struct_object_index =
          UnrealObjectIndex(build->squad_records.struct_definition);
      build->squad_record_count =
          std::min<int32_t>(build->squad_records.count, 32);
    }
  }
  if (build->squad_records_available && build->squad_record_count > 0) {
    build->squad_discovery_phase = SquadDiscoveryPhase::kRecords;
    return true;
  }
  return BeginSquadControllerDiscovery(build);
}

DiscoverySliceResult ScanSquadRecordSlice(SnapshotBuild* build) {
  if (!build || build->squad_discovery_phase !=
                    SquadDiscoveryPhase::kRecords ||
      !SquadRootsStillCurrent(*build))
    return DiscoverySliceResult::kInvalid;
  if (!SquadCandidatesStillCurrent(*build))
    return DiscoverySliceResult::kInvalid;
  if (build->squad_record_index >= build->squad_record_count ||
      build->state.squad_count >= kSquadCapacity) {
    if (!BeginSquadControllerDiscovery(build))
      return DiscoverySliceResult::kInvalid;
    return build->squad_discovery_phase == SquadDiscoveryPhase::kComplete
               ? DiscoverySliceResult::kComplete
               : DiscoverySliceResult::kPending;
  }

  // Exactly one native record is examined per snapshot slice. The array was
  // freshly re-read above, so a reallocation or size change between engine
  // ticks invalidates the staged snapshot instead of leaving a stale cursor.
  const int32_t record_index = build->squad_record_index++;
  void* controller = nullptr;
  if (UnrealReadStructArrayObjectField(build->squad_records, record_index,
                                       L"CharacterController", &controller) &&
      controller && controller != build->controller &&
      UnrealObjectValid(controller) && !PendingDestroy(controller)) {
    void* pawn = ControllerPawn(controller);
    if (pawn && pawn != build->pawn && !PendingDestroy(pawn) &&
        SupportedAiPair(controller, pawn) &&
        !SquadCandidateDuplicate(*build, controller, pawn)) {
      char authoritative_name[128] = {};
      const bool member_record_ready = UnrealReadStructArrayStringField(
          build->squad_records, record_index, L"CharacterName",
          authoritative_name, sizeof(authoritative_name)) &&
          authoritative_name[0];
      char resolved_name[80] = {};
      char resolved_portrait[128] = {};
      Faction resolved_faction = Faction::kUnknown;
      if (ResolveCharacter(
              controller, pawn, ReadMode(pawn), resolved_name,
              sizeof(resolved_name), &resolved_faction, resolved_portrait,
              sizeof(resolved_portrait),
              member_record_ready ? authoritative_name : nullptr)) {
        const bool teammate =
            (build->squad_player_team &&
             TeamFor(controller, pawn) == build->squad_player_team) ||
            (build->state.faction != Faction::kUnknown &&
             resolved_faction == build->state.faction);
        if (teammate) {
          const uint32_t slot = build->state.squad_count++;
          FillSquadMember(
              controller, pawn, false, false,
              member_record_ready ? authoritative_name : resolved_name,
              &build->state.squad[slot]);
          build->squad_controllers[slot] = controller;
          build->squad_pawns[slot] = pawn;
          build->squad_weapons[slot] = CurrentPawnWeapon(pawn);
          build->squad_record_indices[slot] =
              member_record_ready ? record_index : -1;
        }
      }
    }
  }

  if (build->squad_record_index >= build->squad_record_count ||
      build->state.squad_count >= kSquadCapacity) {
    if (!BeginSquadControllerDiscovery(build))
      return DiscoverySliceResult::kInvalid;
    if (build->squad_discovery_phase == SquadDiscoveryPhase::kComplete)
      return DiscoverySliceResult::kComplete;
  }
  return DiscoverySliceResult::kPending;
}

DiscoverySliceResult ScanSquadControllerSlice(SnapshotBuild* build) {
  if (!build || build->squad_discovery_phase !=
                    SquadDiscoveryPhase::kControllers ||
      !SquadRootsStillCurrent(*build))
    return DiscoverySliceResult::kInvalid;
  if (!SquadCandidatesStillCurrent(*build))
    return DiscoverySliceResult::kInvalid;
  if (!SquadControllerRootStillCurrent(*build))
    return DiscoverySliceResult::kInvalid;
  void* controller = build->squad_controller_cursor;
  if (!RetainedObjectMatches(controller,
                             build->squad_controller_cursor_object_index,
                             controller))
    return DiscoverySliceResult::kInvalid;
  if (build->squad_controller_previous) {
    if (!RetainedObjectMatches(
            build->squad_controller_previous,
            build->squad_controller_previous_object_index,
            build->squad_controller_previous))
      return DiscoverySliceResult::kInvalid;
    void* linked = nullptr;
    if (!UnrealReadObjectProperty(build->squad_controller_previous,
                                  L"NextController", &linked) ||
        linked != controller)
      return DiscoverySliceResult::kInvalid;
  } else if (controller != build->squad_controller_head) {
    return DiscoverySliceResult::kInvalid;
  }

  // Some campaign game instances do not expose the AiSquad backing property.
  // Examine one WorldInfo controller per slice and accept only recognized,
  // same-team TnAiPawn companions; ordinary combat AI remains excluded.
  void* next = nullptr;
  if (!UnrealReadObjectProperty(controller, L"NextController", &next))
    return DiscoverySliceResult::kInvalid;
  void* pawn = ControllerPawn(controller);
  if (controller != build->controller && pawn && pawn != build->pawn &&
      UnrealObjectValid(pawn) && !PendingDestroy(pawn) &&
      SupportedAiPair(controller, pawn) &&
      !SquadCandidateDuplicate(*build, controller, pawn)) {
    char iconic_name[80] = {};
    char portrait[128] = {};
    Faction faction = Faction::kUnknown;
    if (ResolveCharacter(controller, pawn, ReadMode(pawn), iconic_name,
                         sizeof(iconic_name), &faction, portrait,
                         sizeof(portrait)) &&
        faction != Faction::kUnknown &&
        ((build->squad_player_team &&
          TeamFor(controller, pawn) == build->squad_player_team) ||
         (build->state.faction != Faction::kUnknown &&
          faction == build->state.faction))) {
      const uint32_t slot = build->state.squad_count++;
      FillSquadMember(controller, pawn, false, false, iconic_name,
                      &build->state.squad[slot]);
      build->squad_controllers[slot] = controller;
      build->squad_pawns[slot] = pawn;
      build->squad_weapons[slot] = CurrentPawnWeapon(pawn);
      // The staged record pass can observe a member before its controller or
      // character data has finished initializing. If the controller-list
      // fallback finds that same live companion later in the snapshot, retain
      // its exact authoritative record identity. Previously this slot stayed
      // at -1, so the Squad card was visible but every tap was silently made
      // unswitchable.
      build->squad_record_indices[slot] =
          FindSquadRecordIndexForController(*build, controller);
    }
  }

  ++build->squad_controllers_examined;
  if (!next || next == controller || build->squad_controllers_examined >= 32 ||
      build->state.squad_count >= kSquadCapacity) {
    build->squad_discovery_phase = SquadDiscoveryPhase::kComplete;
    build->squad_controller_cursor = nullptr;
    build->squad_controller_cursor_object_index = 0;
    return DiscoverySliceResult::kComplete;
  }
  build->squad_controller_previous = controller;
  build->squad_controller_previous_object_index =
      UnrealObjectIndex(controller);
  build->squad_controller_cursor = next;
  build->squad_controller_cursor_object_index = UnrealObjectIndex(next);
  return DiscoverySliceResult::kPending;
}

bool FinishSquadDiscovery(SnapshotBuild* build, ULONGLONG now) {
  if (!build ||
      build->squad_discovery_phase != SquadDiscoveryPhase::kComplete ||
      !SquadRootsStillCurrent(*build) ||
      !SquadControllerRootStillCurrent(*build) ||
      !SquadCandidatesStillCurrent(*build))
    return false;
  build->state.capabilities |= kCapSquad;
  g_squad_controllers = build->squad_controllers;
  g_squad_pawns = build->squad_pawns;
  g_squad_weapons = build->squad_weapons;
  g_squad_record_indices = build->squad_record_indices;
  g_next_squad_refresh_slot = 0;
  g_last_squad_discovery = now;
  return true;
}

bool RefreshCachedSquad(Snapshot* state) {
  if (!state || state->squad_count == 0 ||
      state->squad_count > kSquadCapacity)
    return false;
  // Refresh one card per requested Squad-page sample. Membership discovery is
  // audited separately; re-resolving names, class ancestry and weapons for all
  // three actors in one game frame caused periodic Box64 spikes.
  const uint32_t slot = g_next_squad_refresh_slot++ % state->squad_count;
  void* controller = g_squad_controllers[slot];
  void* pawn = g_squad_pawns[slot];
  const SquadMemberState previous = state->squad[slot];
  const bool controlled = slot == 0;
  if (!UnrealObjectValid(controller) || !UnrealObjectValid(pawn) ||
      PendingDestroy(controller) || PendingDestroy(pawn) ||
      ControllerPawn(controller) != pawn || PawnController(pawn) != controller ||
      UnrealObjectIndex(controller) != previous.controller_object_index ||
      UnrealObjectIndex(pawn) != previous.pawn_object_index ||
      (controlled && (controller != g_controller || pawn != g_pawn)))
    return false;

  SquadMemberState refreshed = previous;
  refreshed.controlled = controlled ? 1 : 0;
  refreshed.mode = ReadMode(pawn);
  bool downed = false;
  bool alive = false;
  ReadVitals(pawn, &refreshed.health, &refreshed.max_health, &downed, &alive);
  refreshed.downed = downed ? 1 : 0;
  refreshed.alive = alive ? 1 : 0;
  if (refreshed.mode != previous.mode) {
    const CharacterPresentation* known = CharacterFor(previous.name);
    if (known)
      std::snprintf(refreshed.portrait_key, sizeof(refreshed.portrait_key),
                    "%s_%s", known->portrait,
                    refreshed.mode == TransformerMode::kVehicle
                        ? "Vehicle" : "Robot");
  }
  void* weapon = CurrentPawnWeapon(pawn);
  if (weapon != g_squad_weapons[slot]) {
    refreshed.weapon_name[0] = '\0';
    char weapon_class[128] = {};
    if (weapon && UnrealObjectClassName(weapon, weapon_class,
                                        sizeof(weapon_class)))
      CopyText(refreshed.weapon_name, sizeof(refreshed.weapon_name),
               Humanize(weapon_class, "TnWeapon").c_str());
    g_squad_weapons[slot] = weapon;
  }
  state->squad[slot] = refreshed;
  for (uint32_t slot = state->squad_count; slot < kSquadCapacity; ++slot) {
    state->squad[slot] = {};
    g_squad_controllers[slot] = nullptr;
    g_squad_pawns[slot] = nullptr;
    g_squad_weapons[slot] = nullptr;
    g_squad_record_indices[slot] = -1;
  }
  state->capabilities |= kCapSquad;
  return true;
}

bool ReadLocation(void* actor, float* x, float* y, float* z) {
  return UnrealReadStructFloatProperty(actor, L"Location", L"X", x) &&
         UnrealReadStructFloatProperty(actor, L"Location", L"Y", y) &&
         UnrealReadStructFloatProperty(actor, L"Location", L"Z", z) &&
         std::isfinite(*x) && std::isfinite(*y) && std::isfinite(*z);
}

void ConsiderObjectiveMarkers(const UnrealStructArrayView& markers,
                              const std::string& objective_token,
                              float player_x, float player_y, float player_z,
                              int32_t first, int32_t last,
                              ObjectiveMarkerCandidate* best) {
  if (!best) return;
  // The native HUD only needs the nearest relevant marker. A corrupt or very
  // large transient array must not turn one companion snapshot into a frame
  // hitch on Box64.
  const int32_t marker_count = markers.count < 32 ? markers.count : 32;
  first = std::clamp<int32_t>(first, 0, marker_count);
  last = std::clamp<int32_t>(last, first, marker_count);
  for (int32_t index = first; index < last; ++index) {
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
    best->marker_base = marker_base;
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

bool BeginObjectiveRead(SnapshotBuild* build) {
  if (!build || !UnrealObjectValid(build->game_replication_info)) return false;
  Snapshot* state = &build->state;
  ReadCampaignContext(state);
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(build->game_replication_info,
                                L"_ObjectiveManager", &manager) || !manager ||
      !UnrealObjectIsA(manager, L"TnObjectiveManager") ||
      PendingDestroy(manager))
    return false;
  char objective[sizeof(state->objective.text)] = {};
  int32_t current_objective = -1;
  const bool current_objective_known =
      UnrealReadIntProperty(manager, L"CurrentObjective", &current_objective);
  if (current_objective_known && current_objective < 0) return false;
  if (current_objective_known)
    UnrealReadStringArrayElementProperty(manager, L"ObjectiveBank",
                                         current_objective, objective,
                                         sizeof(objective));
  // ActiveObjectives is a HUD-updated cache and may be empty when the native
  // HUD is not observing. Keep it only as a compatibility fallback.
  if (!objective[0])
    UnrealReadStringProperty(build->game_replication_info,
                             L"ActiveObjectives", objective,
                             sizeof(objective));
  if (!objective[0]) return false;
  g_objective_manager = manager;
  g_objective_number = current_objective_known ? current_objective : -1;
  g_objective_marker_base = nullptr;
  state->objective.objective_object_index = UnrealObjectIndex(manager);
  state->objective.active = 1;
  CopyText(state->objective.text, sizeof(state->objective.text), objective);
  state->capabilities |= kCapObjective;

  build->objective_manager_object_index = UnrealObjectIndex(manager);
  build->objective_marker_index = 0;
  build->objective_token = Token(objective);
  build->objective_marker = {};
  return ReadLocation(build->pawn, &build->objective_player_x,
                      &build->objective_player_y,
                      &build->objective_player_z);
}

bool ObjectiveIdentityStillCurrent(const SnapshotBuild& build) {
  if (!g_objective_manager || !UnrealObjectValid(g_objective_manager) ||
      PendingDestroy(g_objective_manager) ||
      !UnrealObjectValid(build.game_replication_info))
    return false;
  void* manager = nullptr;
  int32_t current = -1;
  return UnrealReadObjectProperty(build.game_replication_info,
                                  L"_ObjectiveManager", &manager) &&
         manager == g_objective_manager &&
         UnrealReadIntProperty(manager, L"CurrentObjective", &current) &&
         current == g_objective_number;
}

void* CurrentObjectiveManager(const SnapshotBuild& build) {
  void* manager = nullptr;
  if (!build.objective_manager_object_index ||
      !UnrealObjectValid(build.game_replication_info) ||
      !UnrealReadObjectProperty(build.game_replication_info,
                                L"_ObjectiveManager", &manager) ||
      !manager || !UnrealObjectIsA(manager, L"TnObjectiveManager") ||
      PendingDestroy(manager) ||
      UnrealObjectIndex(manager) != build.objective_manager_object_index)
    return nullptr;
  return manager;
}

bool ScanObjectiveMarkerSlice(SnapshotBuild* build, bool fixed_markers) {
  if (!build) return true;
  void* manager = CurrentObjectiveManager(*build);
  if (!manager) return true;
  UnrealStructArrayView markers;
  const bool have_markers = fixed_markers
      ? UnrealReadFixedStructArrayProperty(manager, L"ObjectiveMarkers",
                                           &markers)
      : UnrealReadStructArrayProperty(manager, L"LocalObjectiveMarkers",
                                      &markers);
  if (!have_markers) return true;
  constexpr int32_t kMarkersPerSlice = 1;
  const int32_t marker_count = std::min<int32_t>(markers.count, 32);
  const int32_t first = std::clamp<int32_t>(build->objective_marker_index,
                                            0, marker_count);
  const int32_t last = std::min(first + kMarkersPerSlice, marker_count);
  ConsiderObjectiveMarkers(markers, build->objective_token,
                           build->objective_player_x,
                           build->objective_player_y,
                           build->objective_player_z, first, last,
                           &build->objective_marker);
  build->objective_marker_index = last;
  return last >= marker_count;
}

void FinishObjectiveRead(SnapshotBuild* build) {
  if (!build || !build->objective_marker.found) return;
  Snapshot* state = &build->state;
  state->objective.has_world_location = 1;
  state->objective.world_x = build->objective_marker.x;
  state->objective.world_y = build->objective_marker.y;
  state->objective.world_z = build->objective_marker.z;
  // The native objective-marker label bytecode divides FVector distance by
  // 10.0 before converting it to the displayed integer distance.
  state->objective.distance_meters =
      build->objective_marker.distance_units / 10.0f;
  g_objective_marker_base = build->objective_marker.marker_base;
}

void RefreshObjectiveDistance(Snapshot* state) {
  if (!state || !state->objective.active ||
      !state->objective.has_world_location)
    return;
  if (g_objective_marker_base &&
      UnrealObjectValid(g_objective_marker_base) &&
      !PendingDestroy(g_objective_marker_base)) {
    float marker_x = 0.0f, marker_y = 0.0f, marker_z = 0.0f;
    if (ReadLocation(g_objective_marker_base, &marker_x, &marker_y,
                     &marker_z)) {
      state->objective.world_x = marker_x;
      state->objective.world_y = marker_y;
      state->objective.world_z = marker_z;
    }
  }
  float player_x = 0.0f, player_y = 0.0f, player_z = 0.0f;
  if (!ReadLocation(g_pawn, &player_x, &player_y, &player_z)) return;
  const float dx = state->objective.world_x - player_x;
  const float dy = state->objective.world_y - player_y;
  const float dz = state->objective.world_z - player_z;
  state->objective.distance_meters =
      std::sqrt(dx * dx + dy * dy + dz * dz) / 10.0f;
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
  for (WeaponState& weapon : state->weapons) {
    sanitize(&weapon.heat);
    sanitize(&weapon.heat_capacity);
  }
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

void BeginSnapshotBuild(ULONGLONG now) {
  g_snapshot_build = {};
  SnapshotBuild& build = g_snapshot_build;
  build.started_at = now;
  build.have_previous = CopyLatestSnapshot(&build.previous);
  const bool can_reuse_previous =
      build.have_previous && build.previous.world_epoch == g_world_epoch &&
      build.previous.lifecycle != Lifecycle::kWaitingForWorld &&
      build.previous.player_pawn_object_index == UnrealObjectIndex(g_pawn);
  if (can_reuse_previous) build.state = build.previous;
  build.state.size = sizeof(build.state);
  build.state.sequence = build.have_previous ? build.previous.sequence + 1 : 1;
  build.state.world_epoch = g_world_epoch;
  build.state.switch_in_progress = g_switch_in_progress;
  build.state.supported_build = 1;
  if (build.have_previous)
    CopyText(build.state.build_label, sizeof(build.state.build_label),
             build.previous.build_label);

  build.controller = g_controller;
  build.pawn = g_pawn;
  build.world_info = g_world_info;
  build.game_replication_info = g_game_replication_info;
  build.world_epoch = g_world_epoch;
  if (!UnrealObjectValid(build.controller) || !UnrealObjectValid(build.pawn) ||
      !UnrealObjectValid(build.world_info) || !g_world_ready_since ||
      now - g_world_ready_since < kWorldStabilizeMs) {
    build.state.lifecycle = Lifecycle::kWaitingForWorld;
    build.phase = SnapshotBuildPhase::kPublish;
    return;
  }
  build.state.lifecycle = Lifecycle::kLive;
  build.state.local_controller_object_index = UnrealObjectIndex(build.controller);
  build.state.player_pawn_object_index = UnrealObjectIndex(build.pawn);
  build.state.world_object_index = UnrealObjectIndex(build.world_info);
  build.reuse_identity = can_reuse_previous &&
                         (build.previous.capabilities & kCapIdentity);
  build.reuse_objective = can_reuse_previous &&
      g_last_objective_discovery &&
      now - g_last_objective_discovery < kObjectiveAuditIntervalMs;
  build.reuse_squad = can_reuse_previous && g_last_squad_discovery &&
      now - g_last_squad_discovery < kSquadAuditIntervalMs;
  build.has_alternate_robot_weapon =
      build.state.weapons[0].object_index &&
      build.state.weapons[1].object_index &&
      build.state.weapons[0].object_index != build.state.weapons[1].object_index;
  build.phase = SnapshotBuildPhase::kIdentityVitals;
}

bool SnapshotBuildStillCurrent(const SnapshotBuild& build) {
  return build.phase != SnapshotBuildPhase::kIdle &&
         build.world_epoch == g_world_epoch &&
         build.controller == g_controller && build.pawn == g_pawn &&
         build.world_info == g_world_info &&
         build.game_replication_info == g_game_replication_info &&
         UnrealObjectValid(build.controller) &&
         UnrealObjectValid(build.pawn) &&
         UnrealObjectValid(build.world_info) &&
         (!build.game_replication_info ||
          UnrealObjectValid(build.game_replication_info)) &&
         !PendingDestroy(build.controller) && !PendingDestroy(build.pawn) &&
         ControllerPawn(build.controller) == build.pawn &&
         PawnController(build.pawn) == build.controller;
}

void ReadActionCapabilities(SnapshotBuild* build) {
  if (!build) return;
  Snapshot& state = build->state;
  state.capabilities &= ~(kCapTransformAction | kCapReloadAction |
                          kCapSwapAction | kCapAbilityActions |
                          kCapGrenadeAction | kCapSquadSwitchAction);
  const PanelPage requested_page = RequestedPanelPage();
  if (requested_page == PanelPage::kObjectives) return;
  const bool can_act = build->unrestricted && build->alive && !build->downed;
  if (requested_page == PanelPage::kCombat) {
    bool transforming_enabled = false;
    UnrealReadBoolProperty(build->pawn, L"_TransformingEnabled",
                           &transforming_enabled);
    if (can_act && transforming_enabled &&
        state.mode != TransformerMode::kTransitioning)
      state.capabilities |= kCapTransformAction;

    const WeaponState* active_weapon = nullptr;
    for (const WeaponState& weapon : state.weapons)
      if (weapon.object_index == state.active_weapon_object_index)
        active_weapon = &weapon;
    if (can_act && state.mode == TransformerMode::kRobot &&
        state.inventory_manager_object_index && active_weapon &&
        active_weapon->reserve > 0 && active_weapon->magazine >= 0 &&
        active_weapon->magazine_capacity > 0 &&
        active_weapon->magazine < active_weapon->magazine_capacity)
      state.capabilities |= kCapReloadAction;
    if (can_act && state.mode == TransformerMode::kRobot &&
        state.active_weapon_object_index && state.weapons[0].object_index &&
        state.weapons[1].object_index && build->has_alternate_robot_weapon)
      state.capabilities |= kCapSwapAction;
    if (can_act && state.ability_manager_object_index &&
        (state.abilities[0].available || state.abilities[1].available))
      state.capabilities |= kCapAbilityActions;
    if (can_act && state.mode == TransformerMode::kRobot &&
        state.grenade_object_index && state.grenade_count > 0)
      state.capabilities |= kCapGrenadeAction;
    return;
  }

  const bool can_switch = can_act &&
      !g_switch_in_progress && !g_pending_ability_release.pending &&
      !SwapWorldMutationActive() &&
      SupportedPlayerPair(build->controller, build->pawn);
  bool any_switchable = false;
  for (uint32_t slot = 0; slot < kSquadCapacity; ++slot) {
    SquadMemberState& member = state.squad[slot];
    member.switchable = 0;
    if (!can_switch || slot == 0 || slot >= state.squad_count ||
        !member.occupied || !member.alive || member.downed ||
        member.mode == TransformerMode::kUnknown ||
        member.mode == TransformerMode::kTransitioning ||
        g_squad_record_indices[slot] < 0 ||
        member.controller_object_index !=
            UnrealObjectIndex(g_squad_controllers[slot]) ||
        member.pawn_object_index != UnrealObjectIndex(g_squad_pawns[slot]) ||
        !SupportedAiPair(g_squad_controllers[slot], g_squad_pawns[slot]))
      continue;
    member.switchable = 1;
    any_switchable = true;
  }
  if (any_switchable) state.capabilities |= kCapSquadSwitchAction;
}

void PublishSnapshotBuild(ULONGLONG now) {
  SnapshotBuild& build = g_snapshot_build;
  if (build.state.lifecycle == Lifecycle::kLive &&
      !SnapshotBuildStillCurrent(build)) {
    // A stale staged build must not restart on every following frame merely
    // because no snapshot was published. Give the native graph one normal
    // publish interval to settle while the resolver refreshes immediately.
    build = {};
    g_last_publish = now;
    g_resolve_retry_at = 0;
    return;
  }
  SanitizeSnapshotMetrics(&build.state);
  const bool changed = !build.have_previous ||
                       !SnapshotContentEqual(build.state, build.previous);
  const bool published = changed && PublishSnapshot(build.state);
  if (changed && !published &&
      now - g_last_snapshot_reject_log >= kResolveDiagnosticIntervalMs) {
    g_last_snapshot_reject_log = now;
    Log(LogLevel::kWarning,
        "[State] snapshot rejected sequence=%llu epoch=%llu lifecycle=%u",
        static_cast<unsigned long long>(build.state.sequence),
        static_cast<unsigned long long>(build.state.world_epoch),
        static_cast<unsigned>(build.state.lifecycle));
  }
#ifndef NDEBUG
  if (build.state.lifecycle == Lifecycle::kLive &&
      now - g_last_live_summary_log >= kLiveSummaryIntervalMs) {
    g_last_live_summary_log = now;
    Log(LogLevel::kInfo,
        "[State] live character=%s hp=%.1f/%.1f mode=%u resource=%.1f weapons=[%s,%s] abilities=[%s,%s] squad=%u [%s,%s,%s] caps=%llX",
        build.state.character_name, build.state.health,
        build.state.max_health, static_cast<unsigned>(build.state.mode),
        build.state.resource, build.state.weapons[0].name,
        build.state.weapons[1].name, build.state.abilities[0].name,
        build.state.abilities[1].name, build.state.squad_count,
        build.state.squad[0].name, build.state.squad[1].name,
        build.state.squad[2].name,
        static_cast<unsigned long long>(build.state.capabilities));
  }
#endif
  // Deliberately sliced objective discovery can span several seconds of wall
  // time while doing only one bounded record read every 50 ms. Reporting that
  // elapsed wall time as game-thread cost was misleading and also generated
  // needless log traffic. Individual resolver/snapshot slices are timed in
  // GameThreadPulse, which is the value that can actually produce a hitch.
  g_last_publish = now;
  build = {};
}

void AdvanceSnapshotBuild(ULONGLONG now) {
  if (g_snapshot_build.phase == SnapshotBuildPhase::kIdle) {
    if (now - g_last_publish < kPublishIntervalMs) return;
    SetSnapshotStage("snapshot-begin");
    BeginSnapshotBuild(now);
    return;
  }
  SnapshotBuild& build = g_snapshot_build;
  // A staged build yields back to the native engine between every phase. The
  // world can travel, GC can recycle a UObject slot, or a controller can
  // replace its pawn while we are yielded.  Validate the complete ownership
  // chain before touching any value retained by the previous slice; waiting
  // until Publish left objective/array cursors able to read a stale world for
  // several seconds.
  if (build.state.lifecycle == Lifecycle::kLive &&
      !SnapshotBuildStillCurrent(build)) {
    build = {};
    g_last_publish = now;
    g_resolve_retry_at = 0;
    return;
  }
  Snapshot& state = build.state;
  switch (build.phase) {
    case SnapshotBuildPhase::kIdentityVitals: {
      SetSnapshotStage("identity-vitals");
      state.mode = ReadMode(build.pawn);
      ReadVitals(build.pawn, &state.health, &state.max_health, &build.downed,
                 &build.alive);
      state.downed = build.downed;
      state.alive = build.alive;
      if (!build.alive) state.lifecycle = Lifecycle::kDead;
      if (state.health >= 0.0f && state.max_health > 0.0f)
        state.capabilities |= kCapVitals;
      if (state.mode != TransformerMode::kUnknown)
        state.capabilities |= kCapMode;
      if (build.reuse_identity) {
        state.faction = build.previous.faction;
        CopyText(state.character_name, sizeof(state.character_name),
                 build.previous.character_name);
        CopyText(state.portrait_key, sizeof(state.portrait_key),
                 build.previous.portrait_key);
        if (build.previous.capabilities & kCapIdentity)
          state.capabilities |= kCapIdentity;
      } else if (ResolveCharacter(build.controller, build.pawn, state.mode,
                                  state.character_name,
                                  sizeof(state.character_name), &state.faction,
                                  state.portrait_key,
                                  sizeof(state.portrait_key))) {
        state.capabilities |= kCapIdentity;
      }
      build.phase = SnapshotBuildPhase::kRestrictions;
      break;
    }
    case SnapshotBuildPhase::kRestrictions:
      SetSnapshotStage("restrictions");
      build.unrestricted = ReadRestrictions(&state);
      build.phase = RequestedPanelPage() == PanelPage::kCombat
                        ? SnapshotBuildPhase::kAbilities
                        : SnapshotBuildPhase::kObjective;
      break;
    case SnapshotBuildPhase::kAbilities:
      SetSnapshotStage("abilities");
      {
        Snapshot candidate = state;
        candidate.resource = -1.0f;
        candidate.max_resource = -1.0f;
        candidate.capabilities &= ~(kCapResource | kCapAbilities |
                                    kCapAbilityActions);
        void* ability_manager = nullptr;
        if (ReadAbilities(&candidate, &ability_manager)) {
          // Cooldown interpolation is anchored only to a complete ability
          // sample. A transient reflected read preserves the last good section
          // instead of blanking it or moving the panel's time anchor.
          candidate.captured_at_millis = now;
          candidate.ability_manager_object_index =
              UnrealObjectIndex(ability_manager);
          state.resource = candidate.resource;
          state.max_resource = candidate.max_resource;
          state.abilities[0] = candidate.abilities[0];
          state.abilities[1] = candidate.abilities[1];
          state.ability_manager_object_index =
              candidate.ability_manager_object_index;
          state.captured_at_millis = candidate.captured_at_millis;
          state.capabilities =
              (state.capabilities & ~(kCapResource | kCapAbilities)) |
              (candidate.capabilities & (kCapResource | kCapAbilities));
        }
      }
      build.phase = SnapshotBuildPhase::kWeapons;
      break;
    case SnapshotBuildPhase::kWeapons:
      SetSnapshotStage("weapons");
      if (!build.weapon_discovery_active) {
        Snapshot candidate = state;
        candidate.capabilities &= ~(kCapWeapons | kCapGrenades |
                                    kCapReloadAction | kCapSwapAction |
                                    kCapGrenadeAction);
        void* inventory_manager = nullptr;
        void* active_weapon = nullptr;
        void* grenade = nullptr;
        bool has_alternate = false;
        bool complete = RefreshCachedWeapons(
            &candidate, &inventory_manager, &active_weapon, &grenade,
            &has_alternate);
        if (complete) {
          candidate.inventory_manager_object_index =
              UnrealObjectIndex(inventory_manager);
          candidate.active_weapon_object_index =
              UnrealObjectIndex(active_weapon);
          candidate.grenade_object_index = UnrealObjectIndex(grenade);
          state.weapons[0] = candidate.weapons[0];
          state.weapons[1] = candidate.weapons[1];
          state.grenade_count = candidate.grenade_count;
          CopyText(state.grenade_name, sizeof(state.grenade_name),
                   candidate.grenade_name);
          CopyText(state.grenade_icon_key, sizeof(state.grenade_icon_key),
                   candidate.grenade_icon_key);
          state.inventory_manager_object_index =
              candidate.inventory_manager_object_index;
          state.active_weapon_object_index =
              candidate.active_weapon_object_index;
          state.grenade_object_index = candidate.grenade_object_index;
          state.capabilities =
              (state.capabilities & ~(kCapWeapons | kCapGrenades)) |
              (candidate.capabilities & (kCapWeapons | kCapGrenades));
          build.has_alternate_robot_weapon = has_alternate;
          build.phase = SnapshotBuildPhase::kObjective;
          break;
        }
        // Cache misses and the 15-second integrity audit use a retained
        // cursor, but do not consume any list node in this initialization
        // slice. Subsequent ticks examine one node apiece.
        if (!BeginWeaponDiscovery(&build)) {
          build.phase = SnapshotBuildPhase::kObjective;
          break;
        }
        break;
      }
      {
        const DiscoverySliceResult result =
            ScanWeaponDiscoverySlice(&build);
        if (result == DiscoverySliceResult::kInvalid) {
          build = {};
          g_last_publish = now;
          break;
        }
        if (result == DiscoverySliceResult::kPending) break;

        Snapshot candidate = state;
        candidate.weapons[0] = {};
        candidate.weapons[1] = {};
        candidate.grenade_count = -1;
        candidate.grenade_name[0] = '\0';
        candidate.grenade_icon_key[0] = '\0';
        candidate.capabilities &= ~(kCapWeapons | kCapGrenades |
                                    kCapReloadAction | kCapSwapAction |
                                    kCapGrenadeAction);
        void* inventory_manager = nullptr;
        void* active_weapon = nullptr;
        void* grenade = nullptr;
        bool has_alternate = false;
        if (!FinishWeaponDiscovery(&build, &candidate, &inventory_manager,
                                   &active_weapon, &grenade,
                                   &has_alternate)) {
          build = {};
          g_last_publish = now;
          break;
        }
        candidate.inventory_manager_object_index =
            UnrealObjectIndex(inventory_manager);
        candidate.active_weapon_object_index =
            UnrealObjectIndex(active_weapon);
        candidate.grenade_object_index = UnrealObjectIndex(grenade);
        state.weapons[0] = candidate.weapons[0];
        state.weapons[1] = candidate.weapons[1];
        state.grenade_count = candidate.grenade_count;
        CopyText(state.grenade_name, sizeof(state.grenade_name),
                 candidate.grenade_name);
        CopyText(state.grenade_icon_key, sizeof(state.grenade_icon_key),
                 candidate.grenade_icon_key);
        state.inventory_manager_object_index =
            candidate.inventory_manager_object_index;
        state.active_weapon_object_index =
            candidate.active_weapon_object_index;
        state.grenade_object_index = candidate.grenade_object_index;
        state.capabilities =
            (state.capabilities & ~(kCapWeapons | kCapGrenades)) |
            (candidate.capabilities & (kCapWeapons | kCapGrenades));
        build.has_alternate_robot_weapon = has_alternate;
        build.phase = SnapshotBuildPhase::kObjective;
      }
      break;
    case SnapshotBuildPhase::kObjective:
      SetSnapshotStage("objective");
      if (RequestedPanelPage() == PanelPage::kSquad) {
        build.phase = SnapshotBuildPhase::kSquad;
      } else if (build.reuse_objective &&
                 ObjectiveIdentityStillCurrent(build)) {
        state.objective = build.previous.objective;
        RefreshObjectiveDistance(&state);
        if (build.previous.capabilities & kCapObjective)
          state.capabilities |= kCapObjective;
        build.phase = SnapshotBuildPhase::kSquad;
      } else {
        state.objective = {};
        state.objective.distance_meters = -1.0f;
        state.capabilities &= ~kCapObjective;
        const bool started = BeginObjectiveRead(&build);
        g_last_objective_discovery = now;
        build.phase = started ? SnapshotBuildPhase::kObjectiveLocalMarkers
                              : SnapshotBuildPhase::kSquad;
      }
      break;
    case SnapshotBuildPhase::kObjectiveLocalMarkers:
      SetSnapshotStage("objective-local-markers");
      if (RequestedPanelPage() == PanelPage::kSquad) {
        build.next_slice_at = 0;
        build.phase = SnapshotBuildPhase::kSquad;
      } else if (ScanObjectiveMarkerSlice(&build, false)) {
        build.objective_marker_index = 0;
        build.next_slice_at = 0;
        build.phase = SnapshotBuildPhase::kObjectiveFixedMarkers;
      } else {
        build.next_slice_at = now + 50;
      }
      break;
    case SnapshotBuildPhase::kObjectiveFixedMarkers:
      SetSnapshotStage("objective-fixed-markers");
      if (RequestedPanelPage() == PanelPage::kSquad) {
        build.next_slice_at = 0;
        build.phase = SnapshotBuildPhase::kSquad;
      } else if (ScanObjectiveMarkerSlice(&build, true)) {
        FinishObjectiveRead(&build);
        build.next_slice_at = 0;
        build.phase = SnapshotBuildPhase::kSquad;
      } else {
        build.next_slice_at = now + 50;
      }
      break;
    case SnapshotBuildPhase::kSquad:
      SetSnapshotStage("squad");
      if (RequestedPanelPage() != PanelPage::kSquad) {
        if (build.squad_discovery_phase != SquadDiscoveryPhase::kIdle) {
          // Do not publish the partially assembled member list when the user
          // changes pages during a yielded audit.
          build = {};
          g_last_publish = now;
          break;
        }
        build.phase = SnapshotBuildPhase::kCapabilities;
      } else if (build.reuse_squad) {
        state.squad_count = build.previous.squad_count;
        for (uint32_t slot = 0; slot < kSquadCapacity; ++slot)
          state.squad[slot] = build.previous.squad[slot];
        if (!RefreshCachedSquad(&state)) {
          build.reuse_squad = false;
          if (!BeginSquadDiscovery(&build)) {
            build = {};
            g_last_publish = now;
            break;
          }
          break;
        }
        build.phase = SnapshotBuildPhase::kCapabilities;
      } else if (build.squad_discovery_phase == SquadDiscoveryPhase::kIdle) {
        if (!BeginSquadDiscovery(&build)) {
          build = {};
          g_last_publish = now;
          break;
        }
        break;
      } else {
        DiscoverySliceResult result = DiscoverySliceResult::kInvalid;
        if (build.squad_discovery_phase == SquadDiscoveryPhase::kRecords)
          result = ScanSquadRecordSlice(&build);
        else if (build.squad_discovery_phase ==
                 SquadDiscoveryPhase::kControllers)
          result = ScanSquadControllerSlice(&build);
        else if (build.squad_discovery_phase ==
                 SquadDiscoveryPhase::kComplete)
          result = DiscoverySliceResult::kComplete;
        if (result == DiscoverySliceResult::kInvalid) {
          build = {};
          g_last_publish = now;
          break;
        }
        if (result == DiscoverySliceResult::kPending) break;
        if (!FinishSquadDiscovery(&build, now)) {
          build = {};
          g_last_publish = now;
          break;
        }
        build.phase = SnapshotBuildPhase::kCapabilities;
      }
      break;
    case SnapshotBuildPhase::kCapabilities:
      SetSnapshotStage("capabilities");
      ReadActionCapabilities(&build);
      build.phase = SnapshotBuildPhase::kPublish;
      break;
    case SnapshotBuildPhase::kPublish:
      SetSnapshotStage("publish");
      PublishSnapshotBuild(now);
      break;
    case SnapshotBuildPhase::kIdle:
      break;
  }
}

CharacterSwapObjectIdentity SwapIdentity(void* object) {
  CharacterSwapObjectIdentity identity;
  if (!UnrealObjectValid(object)) return identity;
  identity.address = reinterpret_cast<uintptr_t>(object);
  identity.object_index = UnrealObjectIndex(object);
  return identity;
}

bool SameRoleMapping(const CharacterRoleMapping& left,
                     const CharacterRoleMapping& right) {
  return left.player_controller == right.player_controller &&
         left.player_controlled_ai_pawn ==
             right.player_controlled_ai_pawn &&
         left.mapped_ai_controller == right.mapped_ai_controller &&
         left.ai_controlled_player_pawn == right.ai_controlled_player_pawn;
}

uint32_t SquadGraphFingerprint() {
  uint32_t value = 2166136261u;
  for (uint32_t slot = 0; slot < kSquadCapacity; ++slot) {
    value = (value ^ UnrealObjectIndex(g_squad_controllers[slot])) * 16777619u;
    value = (value ^ UnrealObjectIndex(g_squad_pawns[slot])) * 16777619u;
    value = (value ^ static_cast<uint32_t>(g_squad_record_indices[slot] + 1)) *
            16777619u;
  }
  return value ? value : 1u;
}

bool ResolveLiveCharacterHash(void* controller, void* pawn,
                              uint32_t* output) {
  if (!output) return false;
  char name[80] = {};
  char portrait[128] = {};
  Faction faction = Faction::kUnknown;
  if (!ResolveCharacter(controller, pawn, ReadMode(pawn), name, sizeof(name),
                        &faction, portrait, sizeof(portrait)))
    return false;
  *output = IdentityHash(name, UnrealObjectIndex(pawn));
  return *output != 0;
}

bool ResolveSquadRecord(void* expected_controller, void** squad_output,
                        UnrealStructArrayView* records_output,
                        int32_t* record_index_output) {
  if (!squad_output || !records_output || !record_index_output) return false;
  *squad_output = nullptr;
  *records_output = {};
  *record_index_output = -1;
  void* squad = nullptr;
  if (!ReadAiSquadObject(g_campaign_game, &squad) || !squad)
    return false;
  UnrealStructArrayView records;
  if (!UnrealReadStructArrayProperty(squad, L"Members", &records) ||
      records.count < 1 || records.count > 32)
    return false;
  for (int32_t index = 0; index < records.count; ++index) {
    void* controller = nullptr;
    if (UnrealReadStructArrayObjectField(records, index,
                                         L"CharacterController",
                                         &controller) &&
        controller == expected_controller) {
      *squad_output = squad;
      *records_output = records;
      *record_index_output = index;
      return true;
    }
  }
  return false;
}

struct WfcSwapJournal {
  struct InventoryWeaponRole {
    void* weapon = nullptr;
    uint32_t object_index = 0;
    uint8_t previous_data_type = 0;
    void* previous_data = nullptr;
    void* desired_data = nullptr;
    void* weapon_heat = nullptr;
    uint32_t weapon_heat_index = 0;
    float current_heat = 0.0f;
    bool heat_state_captured = false;
    bool changed = false;
  };

  uint64_t request_id = 0;
  void* world = nullptr;
  void* player_controller = nullptr;
  void* player_pawn = nullptr;
  void* target_controller = nullptr;
  void* target_pawn = nullptr;
  void* player_pri = nullptr;
  void* target_pri = nullptr;
  void* game_replication_info = nullptr;
  void* squad = nullptr;
  UnrealStructArrayView squad_records = {};
  int32_t squad_record_index = -1;
  uint32_t squad_fingerprint = 0;
  TransformerMode player_mode = TransformerMode::kUnknown;
  TransformerMode target_mode = TransformerMode::kUnknown;
  void* previous_view_target = nullptr;
  void* pickup_manager = nullptr;
  void* contextual_commander = nullptr;
  void* fine_aim_manager = nullptr;
  void* player_targeting = nullptr;
  void* previous_targeting_owner = nullptr;
  void* camera_manager = nullptr;
  void* previous_camera_owner = nullptr;
  void* dialog_manager = nullptr;
  void* target_dialog_manager = nullptr;
  void* previous_target_dialog_owner = nullptr;
  void* previous_target_dialog_ai_owner = nullptr;
  void* previous_target_vehicle_ai_control = nullptr;
  void* replacement_target_vehicle_ai_control = nullptr;
  void* restored_target_vehicle_ai_control = nullptr;
  uint32_t previous_target_vehicle_ai_control_index = 0;
  void* target_plan_runner = nullptr;
  void* target_weapon_shooter = nullptr;
  void* target_weapon_aimer = nullptr;
  void* previous_target_shooter_weapon = nullptr;
  void* target_action_set = nullptr;
  void* target_goal_set = nullptr;
  void* target_tactical_analyzer = nullptr;
  void* target_sensor_set = nullptr;
  void* point_of_interest_manager = nullptr;
  void* hud = nullptr;
  void* player_ability_manager = nullptr;
  void* player_buff_manager = nullptr;
  void* target_ability_manager = nullptr;
  void* target_buff_manager = nullptr;
  void* player_ability_hud = nullptr;
  void* player_buff_hud = nullptr;
  void* target_ability_hud = nullptr;
  void* target_buff_hud = nullptr;
  CharacterRoleMapping previous_mapping = {};
  CharacterRoleMapping desired_mapping = {};
  CharacterRoleMappingRestorePoint mapping_restore = {};
  bool role_adapter_installed_here = false;
  UnrealFunctionHandle player_base_unpossess = {};
  UnrealFunctionHandle player_game_possess = {};
  UnrealFunctionHandle ai_unpossess = {};
  UnrealFunctionHandle ai_possess = {};
  UnrealFunctionHandle ai_create_vehicle_control = {};
  UnrealFunctionHandle plan_clear_solution = {};
  UnrealFunctionHandle plan_set_action_set = {};
  UnrealFunctionHandle plan_set_goal_set = {};
  UnrealFunctionHandle ai_set_sensor_set = {};
  UnrealFunctionHandle player_select_character = {};
  UnrealFunctionHandle player_set_current_character = {};
  UnrealFunctionHandle player_client_start_character_load = {};
  UnrealFunctionHandle player_confirm_selected_character = {};
  UnrealFunctionHandle player_refresh_provider = {};
  std::array<InventoryWeaponRole, 32> player_inventory_weapons = {};
  std::array<InventoryWeaponRole, 32> target_inventory_weapons = {};
  uint32_t player_inventory_weapon_count = 0;
  uint32_t target_inventory_weapon_count = 0;
  void* player_inventory_manager = nullptr;
  void* target_inventory_manager = nullptr;
  void* player_inventory_head = nullptr;
  void* target_inventory_head = nullptr;
  uint32_t player_inventory_manager_index = 0;
  uint32_t target_inventory_manager_index = 0;
  uint32_t player_inventory_head_index = 0;
  uint32_t target_inventory_head_index = 0;
  uint64_t player_inventory_fingerprint = 0;
  uint64_t target_inventory_fingerprint = 0;
  void* player_collision_controller = nullptr;
  void* target_collision_controller = nullptr;
  bool target_lifecycle_was_proxy = false;
  bool former_player_requires_proxy = false;
  bool target_plan_was_suspended = false;
  bool player_move_ignored = false;
  bool player_look_ignored = false;
  bool target_ai_suspended = false;
  bool target_shooter_quiesced = false;
  bool target_shooter_rebound = false;
  bool player_detached = false;
  bool target_detached = false;
  bool player_bound = false;
  bool target_bound = false;
  bool squad_name_updated = false;
  bool identity_swapped = false;
  bool player_current_identity_updated = false;
  bool selected_character_updated = false;
  bool services_rebound = false;
  bool runtime_services_restore_pending = false;
  bool identity_restore_pending = false;
  bool squad_restore_pending = false;
  bool camera_restore_pending = false;
  // ClearSolution/set_ActionSet/set_GoalSet/SetSensorSet all cross engine
  // mutation boundaries. Once the first one is attempted, rollback must
  // retarget the same live objects after restoring the original graph.
  bool target_ai_lifecycle_rebind_started = false;
  bool target_ai_lifecycle_rebound = false;
  bool target_sensor_rebind_started = false;
  bool target_sensor_rebound = false;
  bool target_vehicle_ai_control_rebuilt = false;
  bool target_vehicle_ai_control_detached = false;
  bool target_dialog_rebound = false;
  bool player_collision_role_changed = false;
  bool target_collision_role_changed = false;
  bool camera_rebound = false;
  bool armed = false;
  // Once reciprocal controller/pawn restoration cannot be proven, later
  // rollback steps must leave the input/AI guards and executor adapters in
  // place. Resuming gameplay or removing the cross-role shims over an
  // uncertain graph is more dangerous than remaining deliberately closed.
  bool graph_restore_failed = false;
};

WfcSwapJournal g_swap_journal;

bool ReadRequiredObject(void* owner, const wchar_t* property, void** output) {
  return output && UnrealReadObjectProperty(owner, property, output) &&
         *output && UnrealObjectValid(*output) && !PendingDestroy(*output);
}

bool ReadOptionalObject(void* owner, const wchar_t* property, void** output) {
  if (!output || !UnrealReadObjectProperty(owner, property, output))
    return false;
  return !*output || (UnrealObjectValid(*output) && !PendingDestroy(*output));
}

bool CaptureInventoryWeaponRoles(
    void* pawn, std::array<WfcSwapJournal::InventoryWeaponRole, 32>* output,
    uint32_t* count_output, uint8_t desired_role, void** manager_output,
    uint32_t* manager_index_output, void** head_output,
    uint32_t* head_index_output, uint64_t* fingerprint_output) {
  if (!pawn || !output || !count_output || !manager_output ||
      !manager_index_output || !head_output || !head_index_output ||
      !fingerprint_output)
    return false;
  output->fill({});
  *count_output = 0;
  *manager_output = nullptr;
  *manager_index_output = 0;
  *head_output = nullptr;
  *head_index_output = 0;
  *fingerprint_output = 0;
  const auto fail = [&](const char* stage, void* item) {
    char class_name[96] = {};
    if (item) UnrealObjectClassName(item, class_name, sizeof(class_name));
    Log(LogLevel::kWarning,
        "[SquadSwitch] inventory capture failed stage=%s pawn=%lu item=%lu class=%s desired-role=%u captured=%lu",
        stage ? stage : "unknown",
        static_cast<unsigned long>(UnrealObjectIndex(pawn)),
        static_cast<unsigned long>(UnrealObjectIndex(item)),
        class_name[0] ? class_name : "none", static_cast<unsigned>(desired_role),
        static_cast<unsigned long>(*count_output));
    return false;
  };
  void* manager = nullptr;
  if (!ReadRequiredObject(pawn, L"InvManager", &manager) ||
      !UnrealObjectIsA(manager, L"TnInventoryManager"))
    return fail("inventory-manager", manager);
  void* item = nullptr;
  if (!UnrealReadObjectProperty(manager, L"InventoryChain", &item))
    return fail("inventory-chain", manager);
  const void* initial_head = item;
  std::array<void*, 32> visited = {};
  uint32_t visited_count = 0;
  uint64_t fingerprint = 1469598103934665603ull;
  const auto capture_weapon = [&](void* weapon) {
    if (!weapon || !UnrealObjectIsA(weapon, L"TnWeapon")) return true;
    for (uint32_t index = 0; index < *count_output; ++index)
      if ((*output)[index].weapon == weapon) return true;

    WfcSwapJournal::InventoryWeaponRole candidate = {};
    WfcSwapJournal::InventoryWeaponRole& entry = candidate;
    entry.weapon = weapon;
    entry.object_index = UnrealObjectIndex(weapon);
    void* current_data = nullptr;
    void* instigator = nullptr;
    bool is_firing = false;
    if (!entry.object_index) return fail("weapon-identity", weapon);
    if (!UnrealReadByteProperty(weapon, L"WeaponDataType",
                                &entry.previous_data_type))
      return fail("weapon-data-type", weapon);
    if (!UnrealReadBoolProperty(weapon, L"bIsFiring", &is_firing))
      return fail("weapon-firing-state", weapon);
    if (is_firing) return fail("weapon-firing", weapon);
    if (!UnrealReadObjectProperty(weapon, L"Data", &current_data))
      return fail("weapon-current-data", weapon);

    // Decompiled TnWeapon.GetWeaponDataByType is a four-way property lookup.
    // Read the same authoritative fields directly during preflight instead
    // of executing script while the inventory graph is being journaled.
    const auto read_role_data = [weapon](uint8_t role, void** output_data) {
      if (!output_data) return false;
      *output_data = nullptr;
      const wchar_t* property = nullptr;
      switch (role) {
        case 1: property = L"PlayerData"; break;
        case 2: property = L"AiData"; break;
        case 3: property = L"MultiplayerData"; break;
        case 4: property = L"SurvivalData"; break;
        default: return true;
      }
      return UnrealReadObjectProperty(weapon, property, output_data);
    };
    if (!read_role_data(entry.previous_data_type, &entry.previous_data))
      return fail("weapon-previous-role-data", weapon);
    if (!read_role_data(desired_role, &entry.desired_data))
      return fail("weapon-desired-role-data", weapon);
    if (!UnrealReadObjectProperty(weapon, L"Instigator", &instigator))
      return fail("weapon-instigator", weapon);
    // WFC clears Instigator on an unequipped/holstered inventory weapon.
    // A validated manager root is as authoritative as InventoryChain;
    // reject only a weapon actively attributed to a different pawn.
    if (instigator && instigator != pawn)
      return fail("weapon-foreign-instigator", weapon);

    const bool previous_data_valid =
        entry.previous_data && UnrealObjectValid(entry.previous_data) &&
        UnrealObjectIsA(entry.previous_data, L"TnWeaponData");
    const bool desired_data_valid =
        entry.desired_data && UnrealObjectValid(entry.desired_data) &&
        UnrealObjectIsA(entry.desired_data, L"TnWeaponData");
    const bool role_neutral_without_data =
        !current_data && !entry.previous_data && !entry.desired_data;
    if (!role_neutral_without_data &&
        (!previous_data_valid || current_data != entry.previous_data))
      return fail("current-role-data", weapon);

    // Grenade bags, melee helpers, ability weapons, and empty placeholders
    // share TnWeapon's inventory base but do not necessarily publish both
    // Player and AI tuning records. Only role-convertible weapons belong in
    // the mutation journal.
    if (!desired_data_valid) {
      char class_name[96] = {};
      UnrealObjectClassName(weapon, class_name, sizeof(class_name));
      Log(LogLevel::kInfo,
          "[SquadSwitch] preserving role-neutral inventory item pawn=%lu item=%lu class=%s current-role=%u desired-role=%u",
          static_cast<unsigned long>(UnrealObjectIndex(pawn)),
          static_cast<unsigned long>(entry.object_index),
          class_name[0] ? class_name : "TnWeapon",
          static_cast<unsigned>(entry.previous_data_type),
          static_cast<unsigned>(desired_role));
      return true;
    }

    // WeaponHeat is an optional TnWeapon subobject. Ammo-fed campaign
    // weapons such as Bumblebee's heavy pistol legitimately publish a null
    // WeaponHeat pointer even though they expose PlayerData and AiData.
    if (!UnrealReadObjectProperty(weapon, L"WeaponHeat", &entry.weapon_heat))
      return fail("weapon-heat-property", weapon);
    if (entry.weapon_heat) {
      if (!UnrealObjectValid(entry.weapon_heat) ||
          PendingDestroy(entry.weapon_heat) ||
          !UnrealObjectIsA(entry.weapon_heat, L"TnWeaponHeat") ||
          !(entry.weapon_heat_index = UnrealObjectIndex(entry.weapon_heat)) ||
          !UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                   &entry.current_heat) ||
          !std::isfinite(entry.current_heat) || entry.current_heat < 0.0f)
        return fail("weapon-heat-state", weapon);
      entry.heat_state_captured = true;
    }
    if (*count_output >= output->size())
      return fail("weapon-capacity", weapon);
    (*output)[(*count_output)++] = entry;
    return true;
  };
  for (uint32_t examined = 0; item && examined < output->size(); ++examined) {
    if (!UnrealObjectValid(item) || PendingDestroy(item))
      return fail("invalid-chain-item", item);
    for (uint32_t previous = 0; previous < visited_count; ++previous)
      if (visited[previous] == item) return fail("inventory-cycle", item);
    visited[visited_count++] = item;
    if (!capture_weapon(item)) return false;
    void* next = nullptr;
    if (!UnrealReadObjectProperty(item, L"Inventory", &next) || next == item)
      return fail("inventory-next", item);
    fingerprint =
        (fingerprint ^ static_cast<uint64_t>(UnrealObjectIndex(item))) *
        1099511628211ull;
    fingerprint =
        (fingerprint ^ static_cast<uint64_t>(UnrealObjectIndex(next))) *
        1099511628211ull;
    item = next;
  }
  // A non-null cursor here means the chain exceeded the exact-build safety
  // bound. Never partially convert a pawn's inventory role.
  if (item != nullptr) return fail("inventory-bound", item);
  // StoreRobotWeapon/StoreVehicleWeapon deliberately keep an authoritative
  // form-specific weapon outside InventoryChain. Requiring every manager root
  // to be reachable from that list rejected valid campaign inventories before
  // UnPossess. Journal those detached roots in a stable order instead, so they
  // receive the same role conversion and rollback protection as chain weapons.
  const auto capture_root = [&](const wchar_t* property, void* owner,
                                const wchar_t* expected_class,
                                const char* stage, uint64_t discriminator) {
    void* root = nullptr;
    if (!UnrealReadObjectProperty(owner, property, &root))
      return fail(stage, owner);
    const uint32_t root_index = root ? UnrealObjectIndex(root) : 0;
    fingerprint = (fingerprint ^ discriminator) * 1099511628211ull;
    fingerprint = (fingerprint ^ static_cast<uint64_t>(root_index)) *
                  1099511628211ull;
    if (!root) return true;
    if (!UnrealObjectValid(root) || PendingDestroy(root) ||
        !root_index ||
        (expected_class && !UnrealObjectIsA(root, expected_class)))
      return fail(stage, root);
    bool in_chain = false;
    for (uint32_t index = 0; index < visited_count; ++index)
      in_chain |= visited[index] == root;
    if (!in_chain) {
      char class_name[96] = {};
      UnrealObjectClassName(root, class_name, sizeof(class_name));
      Log(LogLevel::kInfo,
          "[SquadSwitch] journaling detached inventory root stage=%s pawn=%lu item=%lu class=%s",
          stage, static_cast<unsigned long>(UnrealObjectIndex(pawn)),
          static_cast<unsigned long>(root_index),
          class_name[0] ? class_name : "unknown");
    }
    return capture_weapon(root);
  };
  if (!capture_root(L"Weapon", pawn, L"TnWeapon", "pawn-weapon-root", 1u) ||
      !capture_root(L"StoredRobotWeapon", manager, L"TnWeapon",
                    "stored-robot-root", 3u) ||
      !capture_root(L"StoredVehicleWeapon", manager, L"TnWeapon",
                    "stored-vehicle-root", 4u))
    return false;
  *manager_output = manager;
  *manager_index_output = UnrealObjectIndex(manager);
  *head_output = const_cast<void*>(initial_head);
  *head_index_output = UnrealObjectIndex(*head_output);
  *fingerprint_output = fingerprint ? fingerprint : 1u;
  if (!*manager_index_output || (*head_output && !*head_index_output))
    return fail("inventory-identity", manager);
  return true;
}

bool InventoryCaptureStillCurrent(
    void* pawn,
    const std::array<WfcSwapJournal::InventoryWeaponRole, 32>& expected,
    uint32_t expected_count, uint8_t desired_role, void* expected_manager,
    uint32_t expected_manager_index, void* expected_head,
    uint32_t expected_head_index, uint64_t expected_fingerprint) {
  std::array<WfcSwapJournal::InventoryWeaponRole, 32> current = {};
  uint32_t current_count = 0;
  void* current_manager = nullptr;
  uint32_t current_manager_index = 0;
  void* current_head = nullptr;
  uint32_t current_head_index = 0;
  uint64_t current_fingerprint = 0;
  if (!CaptureInventoryWeaponRoles(
          pawn, &current, &current_count, desired_role, &current_manager,
          &current_manager_index, &current_head, &current_head_index,
          &current_fingerprint) ||
      current_count != expected_count || current_manager != expected_manager ||
      current_manager_index != expected_manager_index ||
      current_head != expected_head || current_head_index != expected_head_index ||
      current_fingerprint != expected_fingerprint)
    return false;
  for (uint32_t index = 0; index < expected_count; ++index) {
    if (current[index].weapon != expected[index].weapon ||
        current[index].object_index != expected[index].object_index ||
        current[index].previous_data_type != expected[index].previous_data_type ||
        current[index].previous_data != expected[index].previous_data ||
        current[index].desired_data != expected[index].desired_data ||
        current[index].heat_state_captured !=
            expected[index].heat_state_captured ||
        current[index].weapon_heat != expected[index].weapon_heat ||
        current[index].weapon_heat_index != expected[index].weapon_heat_index ||
        (current[index].heat_state_captured &&
         std::fabs(current[index].current_heat - expected[index].current_heat) >
             0.001f))
      return false;
  }
  return true;
}

bool RoleMappingMatches(const CharacterRoleMapping& expected) {
  CharacterRoleMapping current;
  const bool active = GetCharacterRoleMapping(&current);
  const bool expected_active = expected.player_controller != nullptr;
  return active == expected_active &&
         (!expected_active ||
          (CharacterRoleMappingObjectsValid() &&
           SameRoleMapping(current, expected)));
}

bool PriIdentityMatchesPawn(void* pri, void* pawn) {
  UnrealStructValueView pawn_current;
  UnrealStructValueView pawn_compressed;
  UnrealStructValueView pri_current;
  UnrealStructValueView selected;
  UnrealStructValueView replicated_wrapper;
  UnrealStructValueView replicated;
  char pawn_current_name[128] = {};
  char pawn_current_specialty[128] = {};
  char pawn_compressed_name[128] = {};
  char pawn_compressed_specialty[128] = {};
  char pri_current_name[128] = {};
  char pri_current_specialty[128] = {};
  char selected_name[128] = {};
  char replicated_name[128] = {};
  char replicated_specialty[128] = {};
  char canonical_name[128] = {};
  char iconic_name[128] = {};
  char selected_specialty[128] = {};
  char current_specialty[128] = {};
  return pri && pawn &&
         UnrealReadStructProperty(pawn, L"_CurrentCharacter",
                                  &pawn_current) &&
         UnrealReadStructProperty(pawn, L"_CurrentCharacterCompressed",
                                  &pawn_compressed) &&
         UnrealReadStructProperty(pri, L"_CurrentCharacter",
                                  &pri_current) &&
         UnrealReadStructProperty(pri, L"_SelectedCharacter",
                                  &selected) &&
         UnrealReadStructProperty(pri, L"ReplicatedCharacterData",
                                  &replicated_wrapper) &&
         UnrealReadStructStructField(replicated_wrapper, L"CD",
                                     &replicated) &&
         UnrealReadStructStringField(pawn_current, L"CharacterName",
                                     pawn_current_name,
                                     sizeof(pawn_current_name)) &&
         UnrealReadStructStringField(pawn_current, L"Specialty",
                                     pawn_current_specialty,
                                     sizeof(pawn_current_specialty)) &&
         UnrealReadStructStringField(pawn_compressed, L"CharacterName",
                                     pawn_compressed_name,
                                     sizeof(pawn_compressed_name)) &&
         UnrealReadStructStringField(pawn_compressed, L"Specialty",
                                     pawn_compressed_specialty,
                                     sizeof(pawn_compressed_specialty)) &&
         UnrealReadStructStringField(pri_current, L"CharacterName",
                                     pri_current_name,
                                     sizeof(pri_current_name)) &&
         UnrealReadStructStringField(pri_current, L"Specialty",
                                     pri_current_specialty,
                                     sizeof(pri_current_specialty)) &&
         UnrealReadStructStringField(selected, L"CharacterName",
                                     selected_name, sizeof(selected_name)) &&
         UnrealReadStructStringField(replicated, L"CharacterName",
                                     replicated_name,
                                     sizeof(replicated_name)) &&
         UnrealReadStructStringField(replicated, L"Specialty",
                                     replicated_specialty,
                                     sizeof(replicated_specialty)) &&
         UnrealCallString(pri, L"GetCharacterName", canonical_name,
                          sizeof(canonical_name)) &&
         UnrealReadStringProperty(pri, L"PlayerIconicCharacterName",
                                  iconic_name, sizeof(iconic_name)) &&
         UnrealReadStringProperty(pri, L"SelectedCharacterString",
                                  selected_specialty,
                                  sizeof(selected_specialty)) &&
         UnrealReadStringProperty(pri, L"CurrentCharacterString",
                                  current_specialty,
                                  sizeof(current_specialty)) &&
         pawn_current_name[0] && pawn_compressed_name[0] &&
         Token(pawn_current_name) == Token(pri_current_name) &&
         Token(pawn_current_specialty) == Token(pri_current_specialty) &&
         Token(pawn_current_specialty) == Token(current_specialty) &&
         Token(pawn_compressed_name) == Token(selected_name) &&
         Token(pawn_compressed_name) == Token(replicated_name) &&
         Token(pawn_compressed_name) == Token(canonical_name) &&
         Token(pawn_compressed_name) == Token(iconic_name) &&
         Token(pawn_compressed_specialty) == Token(replicated_specialty) &&
         Token(pawn_compressed_specialty) == Token(selected_specialty);
}

bool AssignPriIdentityStrings(void* pri, void* pawn) {
  UnrealStructValueView current;
  UnrealStructValueView compressed;
  return pri && pawn &&
         UnrealReadStructProperty(pri, L"_CurrentCharacter", &current) &&
         UnrealReadStructProperty(pawn, L"_CurrentCharacterCompressed",
                                  &compressed) &&
         UnrealAssignObjectStringPropertyFromStruct(
             pri, L"CurrentCharacterString", current, L"Specialty") &&
         UnrealAssignObjectStringPropertyFromStruct(
             pri, L"SelectedCharacterString", compressed, L"Specialty") &&
         UnrealAssignObjectStringPropertyFromStruct(
             pri, L"PlayerIconicCharacterName", compressed,
             L"CharacterName");
}

bool PublishPriIdentityPair(void* first_pri, void* first_pawn,
                            void* second_pri, void* second_pawn,
                            void* game_replication_info,
                            const UnrealFunctionHandle& refresh_provider) {
  // Publish only after both PRI identity graphs and both sets of derived
  // strings are coherent. A provider callback must never observe the first
  // PRI updated while the second still contains its previous character.
  return AssignPriIdentityStrings(first_pri, first_pawn) &&
         AssignPriIdentityStrings(second_pri, second_pawn) &&
         UnrealCallVoid(game_replication_info, L"RefreshClaimedCharacters") &&
         UnrealCallExactVoid(first_pri, refresh_provider) &&
         UnrealCallExactVoid(second_pri, refresh_provider) &&
         PriIdentityMatchesPawn(first_pri, first_pawn) &&
         PriIdentityMatchesPawn(second_pri, second_pawn);
}

bool PublishPlayerIdentity(void* player_pri, void* player_pawn,
                           void* game_replication_info,
                           const UnrealFunctionHandle& refresh_provider) {
  return AssignPriIdentityStrings(player_pri, player_pawn) &&
         UnrealCallVoid(game_replication_info, L"RefreshClaimedCharacters") &&
         UnrealCallExactVoid(player_pri, refresh_provider) &&
         PriIdentityMatchesPawn(player_pri, player_pawn);
}

class WfcSwapOperations final : public CharacterSwapOperations {
 public:
  CharacterSwapPreflight CapturePreflight(
      const CharacterSwapRequest& request,
      CharacterSwapCapture* output) noexcept override;
  CharacterSwapPreflight VerifyQuiesced(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept override;
  CharacterSwapApplyResult ApplyMutation(
      CharacterSwapMutation mutation, const CharacterSwapRequest& request,
      CharacterSwapCapture* capture) noexcept override;
  bool RevertMutation(CharacterSwapMutation mutation,
                      const CharacterSwapRequest& request,
                      CharacterSwapCapture* capture) noexcept override;
  bool VerifyCommittedGraph(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept override;
  bool VerifyRestoredGraph(
      const CharacterSwapRequest& request,
      const CharacterSwapCapture& capture) noexcept override;
  void EnterFailedClosed(const CharacterSwapRequest& request,
                         const CharacterSwapCapture& capture,
                         CharacterSwapFailure cause,
                         bool rollback_complete) noexcept override;
  bool ExitFailedClosed(const CharacterSwapRequest& request,
                        const CharacterSwapCapture& capture) noexcept override;
  void OnSucceeded(const CharacterSwapRequest& request,
                   const CharacterSwapCapture& capture) noexcept override;

 private:
  bool CaptureJournal(const CharacterSwapRequest& request) noexcept;
  bool RestorePreviousMapping() noexcept;
  bool RestoreOriginalGraph() noexcept;
  bool TargetSensorsCoherent(void* expected_pawn) noexcept;
  bool TargetProxyLifecycleCoherent(void* expected_pawn) noexcept;
  bool TargetAiLifecycleCoherent(void* expected_pawn) noexcept;
  bool RebindTargetSensors(void* expected_pawn) noexcept;
  bool RebindTargetAiLifecycle(void* expected_pawn) noexcept;
  bool RebindPlayerServices(void* pawn, void* ability_hud,
                             void* buff_hud) noexcept;
  bool ResetTargetWeaponShooter() noexcept;
  bool ActivateTargetWeaponShooter(void* pawn) noexcept;
  bool ApplyInventoryWeaponRoles() noexcept;
  bool RestoreInventoryWeaponRoles() noexcept;
  bool RestoreDeferredRuntimeState() noexcept;
};

WfcSwapOperations g_swap_operations;
CharacterSwapTransaction g_swap_transaction(&g_swap_operations);

bool SwapWorldMutationActive() {
  const CharacterSwapPhase phase = g_swap_transaction.status().phase;
  return phase != CharacterSwapPhase::kIdle &&
         phase != CharacterSwapPhase::kDone;
}

bool WfcSwapOperations::CaptureJournal(
    const CharacterSwapRequest& request) noexcept {
  const auto capture_failed = [&request](const char* requirement) noexcept {
    Log(LogLevel::kWarning,
        "[SquadSwitch] request=%llu preflight journal requirement failed: %s",
        static_cast<unsigned long long>(request.request_id), requirement);
    return false;
  };
#define WFC_SWAP_CAPTURE_REQUIRE(condition) \
  do { if (!(condition)) return capture_failed(#condition); } while (false)

  WfcSwapJournal journal;
  journal.request_id = request.request_id;
  journal.world = g_world_info;
  journal.player_controller = g_controller;
  journal.player_pawn = g_pawn;
  journal.target_controller = g_squad_controllers[request.squad_slot];
  journal.target_pawn = g_squad_pawns[request.squad_slot];
  journal.player_mode = ReadMode(journal.player_pawn);
  journal.target_mode = ReadMode(journal.target_pawn);
  journal.squad_fingerprint = SquadGraphFingerprint();
  journal.game_replication_info = g_game_replication_info;
  uint8_t net_mode = 0xFF;
  // The synchronous identity path intentionally suppresses replication. It
  // is valid only for WFC's local campaign world.
  WFC_SWAP_CAPTURE_REQUIRE(
      UnrealReadByteProperty(journal.world, L"NetMode", &net_mode));
  WFC_SWAP_CAPTURE_REQUIRE(net_mode == 0);
  WFC_SWAP_CAPTURE_REQUIRE(UnrealObjectValid(journal.game_replication_info));
  WFC_SWAP_CAPTURE_REQUIRE(UnrealObjectIsA(
      journal.game_replication_info, L"TnGameReplicationInfoGameBase"));
  const bool mapping_active =
      GetCharacterRoleMapping(&journal.previous_mapping);
  WFC_SWAP_CAPTURE_REQUIRE(!mapping_active ||
                           CharacterRoleMappingObjectsValid());
  if (mapping_active) {
    if (journal.previous_mapping.player_controller !=
            journal.player_controller ||
        journal.previous_mapping.player_controlled_ai_pawn !=
            journal.player_pawn)
      return capture_failed("previous role mapping player pair");
    if (UnrealObjectIsA(journal.target_pawn, L"TnPlayerPawn")) {
      if (journal.previous_mapping.mapped_ai_controller !=
              journal.target_controller ||
          journal.previous_mapping.ai_controlled_player_pawn !=
              journal.target_pawn)
        return capture_failed("previous role mapping AI pair");
      journal.target_lifecycle_was_proxy = true;
      journal.desired_mapping = {};
    } else {
      journal.desired_mapping.player_controller = journal.player_controller;
      journal.desired_mapping.player_controlled_ai_pawn = journal.target_pawn;
      journal.desired_mapping.mapped_ai_controller =
          journal.previous_mapping.mapped_ai_controller;
      journal.desired_mapping.ai_controlled_player_pawn =
          journal.previous_mapping.ai_controlled_player_pawn;
    }
  } else {
    if (!UnrealObjectIsA(journal.player_pawn, L"TnPlayerPawn") ||
        !UnrealObjectIsA(journal.target_pawn, L"TnAiPawn"))
      return capture_failed("initial TnPlayerPawn/TnAiPawn class pair");
    journal.desired_mapping.player_controller = journal.player_controller;
    journal.desired_mapping.player_controlled_ai_pawn = journal.target_pawn;
    journal.desired_mapping.mapped_ai_controller = journal.target_controller;
    journal.desired_mapping.ai_controlled_player_pawn = journal.player_pawn;
  }
  journal.former_player_requires_proxy =
      UnrealObjectIsA(journal.player_pawn, L"TnPlayerPawn");

  // Native GOAP actions cache a statically typed TnAiPawn and directly touch
  // subclass-owned storage. Initial control transfer therefore uses the
  // bounded role proxy for the former physical TnPlayerPawn. Its vehicle-form
  // branch uses native form inputs rather than an AiPawn strategy; later
  // AiPawn-to-AiPawn transfers retain the complete native lifecycle.
  WFC_SWAP_CAPTURE_REQUIRE(
      SupportedPlayerPair(journal.player_controller, journal.player_pawn));
  WFC_SWAP_CAPTURE_REQUIRE(
      SupportedAiPair(journal.target_controller, journal.target_pawn));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"PlayerReplicationInfo", &journal.player_pri));
  // Campaign squad AI is spawned by TnAiSquad and is not required to own a
  // PlayerReplicationInfo. Some game modes do create one, so preserve the
  // paired path when present, but never manufacture a player record for a
  // campaign bot.
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.target_controller, L"PlayerReplicationInfo", &journal.target_pri));
  WFC_SWAP_CAPTURE_REQUIRE(!journal.target_pri ||
                           UnrealObjectIsA(journal.target_pri,
                                           L"TnPlayerReplicationInfo"));
  WFC_SWAP_CAPTURE_REQUIRE(ResolveSquadRecord(
      journal.target_controller, &journal.squad, &journal.squad_records,
      &journal.squad_record_index));
  WFC_SWAP_CAPTURE_REQUIRE(journal.squad_record_index ==
                           g_squad_record_indices[request.squad_slot]);
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.player_controller, L"ViewTarget", &journal.previous_view_target));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"_PickupManager", &journal.pickup_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"ContextualCommander",
      &journal.contextual_commander));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"_FineAimManager", &journal.fine_aim_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"PlayerTargeting", &journal.player_targeting));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_targeting, L"_TargetingOwner",
      &journal.previous_targeting_owner));
  WFC_SWAP_CAPTURE_REQUIRE(journal.previous_targeting_owner ==
                           journal.player_pawn);
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"_CameraStrategyManager",
      &journal.camera_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.camera_manager, L"_TransformerPawn",
      &journal.previous_camera_owner));
  WFC_SWAP_CAPTURE_REQUIRE(journal.previous_camera_owner == journal.player_pawn);
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"_DialogManager", &journal.dialog_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_controller, L"_DialogManager",
      &journal.target_dialog_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_dialog_manager, L"OwnerPawn",
      &journal.previous_target_dialog_owner));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_dialog_manager, L"OwnerAiPawn",
      &journal.previous_target_dialog_ai_owner));
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.target_controller, L"_VehicleAiControl",
      &journal.previous_target_vehicle_ai_control));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_controller, L"WeaponShooter", &journal.target_weapon_shooter));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_controller, L"WeaponAimer", &journal.target_weapon_aimer));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_controller, L"PlanRunner", &journal.target_plan_runner));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_plan_runner, L"_ActionSet", &journal.target_action_set));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_plan_runner, L"_GoalSet", &journal.target_goal_set));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_controller, L"TacticalAnalyzer",
      &journal.target_tactical_analyzer));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_tactical_analyzer, L"SensorSet", &journal.target_sensor_set));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"_PointOfInterestManager",
      &journal.point_of_interest_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_controller, L"myHUD", &journal.hud));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_pawn, L"AbilityManager", &journal.player_ability_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_pawn, L"_BuffManager", &journal.player_buff_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_pawn, L"AbilityManager", &journal.target_ability_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_pawn, L"_BuffManager", &journal.target_buff_manager));
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.player_ability_manager, L"OwnerHud", &journal.player_ability_hud));
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.player_buff_manager, L"OwnerHud", &journal.player_buff_hud));
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.target_ability_manager, L"OwnerHud", &journal.target_ability_hud));
  WFC_SWAP_CAPTURE_REQUIRE(ReadOptionalObject(
      journal.target_buff_manager, L"OwnerHud", &journal.target_buff_hud));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.player_pawn, L"_CollisionController",
      &journal.player_collision_controller));
  WFC_SWAP_CAPTURE_REQUIRE(ReadRequiredObject(
      journal.target_pawn, L"_CollisionController",
      &journal.target_collision_controller));
  WFC_SWAP_CAPTURE_REQUIRE(CaptureInventoryWeaponRoles(
      journal.player_pawn, &journal.player_inventory_weapons,
      &journal.player_inventory_weapon_count, 2u,
      &journal.player_inventory_manager, &journal.player_inventory_manager_index,
      &journal.player_inventory_head, &journal.player_inventory_head_index,
      &journal.player_inventory_fingerprint));
  WFC_SWAP_CAPTURE_REQUIRE(CaptureInventoryWeaponRoles(
      journal.target_pawn, &journal.target_inventory_weapons,
      &journal.target_inventory_weapon_count, 1u,
      &journal.target_inventory_manager, &journal.target_inventory_manager_index,
      &journal.target_inventory_head, &journal.target_inventory_head_index,
      &journal.target_inventory_fingerprint));
  WFC_SWAP_CAPTURE_REQUIRE(
      PriIdentityMatchesPawn(journal.player_pri, journal.player_pawn));
  WFC_SWAP_CAPTURE_REQUIRE(
      !journal.target_pri ||
      PriIdentityMatchesPawn(journal.target_pri, journal.target_pawn));
  {
    UnrealStructValueView player_character;
    UnrealStructValueView target_character;
    char player_name[128] = {};
    char target_name[128] = {};
    if (!UnrealReadStructProperty(journal.player_pawn,
                                  L"_CurrentCharacterCompressed",
                                  &player_character) ||
        !UnrealReadStructProperty(journal.target_pawn,
                                  L"_CurrentCharacterCompressed",
                                  &target_character) ||
        player_character.struct_definition !=
            target_character.struct_definition ||
        player_character.size != target_character.size ||
        !UnrealReadStructStringField(player_character, L"CharacterName",
                                     player_name, sizeof(player_name)) ||
        !UnrealReadStructStringField(target_character, L"CharacterName",
                                     target_name, sizeof(target_name)) ||
        !player_name[0] || !target_name[0])
      return capture_failed("compatible player/target character structs");
  }

  void* previous_shooter_owner = nullptr;
  void* previous_aimer_owner = nullptr;
  void* previous_aimer_controller = nullptr;
  bool target_shooter_firing = false;
  if (!UnrealReadObjectProperty(journal.target_weapon_shooter, L"Owner",
                                &previous_shooter_owner) ||
      previous_shooter_owner != journal.target_pawn ||
      !UnrealReadObjectProperty(journal.target_weapon_aimer, L"_Owner",
                                &previous_aimer_owner) ||
      previous_aimer_owner != journal.target_pawn ||
      !UnrealReadObjectProperty(journal.target_weapon_aimer,
                                L"_OwnerController",
                                &previous_aimer_controller) ||
      previous_aimer_controller != journal.target_controller ||
       !UnrealCallBool(journal.target_weapon_shooter, L"IsFiring",
                       &target_shooter_firing) ||
      target_shooter_firing ||
       !UnrealCallObject(journal.target_weapon_shooter, L"get_ActiveWeapon",
                         &journal.previous_target_shooter_weapon) ||
      (journal.previous_target_shooter_weapon &&
       (!UnrealObjectIsA(journal.previous_target_shooter_weapon,
                         L"TnWeapon") ||
        PendingDestroy(journal.previous_target_shooter_weapon))) ||
      journal.previous_target_shooter_weapon !=
          CurrentPawnWeapon(journal.target_pawn))
    return capture_failed("target weapon shooter/aimer graph and idle state");

  const void* expected_previous_dialog_owner =
      journal.target_lifecycle_was_proxy ? journal.player_pawn
                                         : journal.target_pawn;
  if (journal.previous_target_dialog_owner != expected_previous_dialog_owner ||
      journal.previous_target_dialog_ai_owner !=
          expected_previous_dialog_owner)
    return capture_failed("target dialog owner graph");

  journal.previous_target_vehicle_ai_control_index =
      UnrealObjectIndex(journal.previous_target_vehicle_ai_control);
  bool plan_runner_suspended = false;
  bool plan_runner_suspend_pending = false;
  void* plan_runner_owner = nullptr;
  void* tactical_analyzer_owner = nullptr;
  void* pending_action_set = nullptr;
  void* pending_goal_set = nullptr;
  if (!UnrealReadBoolProperty(journal.target_plan_runner, L"_Suspended",
                              &plan_runner_suspended) ||
      !UnrealReadBoolProperty(journal.target_plan_runner, L"_SuspendPending",
                              &plan_runner_suspend_pending) ||
      !UnrealReadObjectProperty(journal.target_plan_runner, L"_Owner",
                                &plan_runner_owner) ||
      !UnrealReadObjectProperty(journal.target_plan_runner,
                                L"_PendingActionSet", &pending_action_set) ||
      !UnrealReadObjectProperty(journal.target_plan_runner,
                                L"_PendingGoalSet", &pending_goal_set) ||
      !UnrealReadObjectProperty(journal.target_tactical_analyzer, L"Owner",
                                &tactical_analyzer_owner) ||
       plan_runner_suspend_pending ||
       plan_runner_suspended != journal.target_lifecycle_was_proxy ||
       plan_runner_owner != journal.target_controller || pending_action_set ||
       pending_goal_set || tactical_analyzer_owner != journal.target_controller)
    return capture_failed("target GOAP plan/sensor graph");
  journal.target_plan_was_suspended = plan_runner_suspended;

  if (!UnrealResolveExactScriptFunctionForObject(
          journal.player_controller, L"Engine.PlayerController", L"UnPossess",
          &journal.player_base_unpossess) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_controller, L"TransGame.TnPlayerController",
          L"Possess", &journal.player_game_possess) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_controller, L"TransGame.TnAiController",
          L"UnPossess", &journal.ai_unpossess) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_controller, L"TransGame.TnAiController", L"Possess",
          &journal.ai_possess) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_controller, L"TransGame.TnAiController",
          L"CreateAndInitializeVehicleAiControlStrategy",
          &journal.ai_create_vehicle_control) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_plan_runner, L"HM_Engine.HmGoapPlanRunner",
          L"ClearSolution", &journal.plan_clear_solution) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_plan_runner, L"HM_Engine.HmGoapPlanRunner",
          L"set_ActionSet", &journal.plan_set_action_set) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_plan_runner, L"HM_Engine.HmGoapPlanRunner",
          L"set_GoalSet", &journal.plan_set_goal_set) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.target_controller, L"TransGame.TnAiController",
          L"SetSensorSet", &journal.ai_set_sensor_set) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_pri, L"TransGame.TnPlayerReplicationInfo",
          L"SelectCharacter", &journal.player_select_character) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_pri, L"TransGame.TnPlayerReplicationInfo",
          L"set_CurrentCharacter",
          &journal.player_set_current_character) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_pri, L"TransGame.TnPlayerReplicationInfo",
          L"ClientStartCharacterLoad",
          &journal.player_client_start_character_load) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_pri, L"TransGame.TnPlayerReplicationInfo",
          L"ConfirmSelectedCharacter",
          &journal.player_confirm_selected_character) ||
      !UnrealResolveExactScriptFunctionForObject(
          journal.player_pri, L"TransGame.TnPlayerReplicationInfo",
          L"RefreshProvider", &journal.player_refresh_provider))
    return capture_failed("exact script function resolution");

  g_swap_journal = journal;
  const bool lifecycle_coherent =
      journal.target_lifecycle_was_proxy
          ? TargetProxyLifecycleCoherent(journal.target_pawn)
          : TargetAiLifecycleCoherent(journal.target_pawn);
  if (!lifecycle_coherent) {
    g_swap_journal = {};
    return capture_failed("target AI lifecycle coherence");
  }
#undef WFC_SWAP_CAPTURE_REQUIRE
  return true;
}

bool WfcSwapOperations::TargetSensorsCoherent(
    void* expected_pawn) noexcept {
  if (!expected_pawn || !UnrealObjectValid(expected_pawn) ||
      PendingDestroy(expected_pawn))
    return false;

  void* analyzer = nullptr;
  void* analyzer_owner = nullptr;
  void* sensor_set = nullptr;
  if (!ReadRequiredObject(g_swap_journal.target_controller,
                          L"TacticalAnalyzer", &analyzer) ||
      analyzer != g_swap_journal.target_tactical_analyzer ||
      !UnrealReadObjectProperty(analyzer, L"Owner", &analyzer_owner) ||
      analyzer_owner != g_swap_journal.target_controller ||
      !ReadRequiredObject(analyzer, L"SensorSet", &sensor_set) ||
      sensor_set != g_swap_journal.target_sensor_set)
    return false;

  UnrealArrayView sensors;
  if (!UnrealReadArrayProperty(sensor_set, L"Sensors", &sensors) ||
      sensors.count < 0 || sensors.count > 128)
    return false;
  for (int32_t index = 0; index < sensors.count; ++index) {
    void* sensor = nullptr;
    if (!UnrealReadObjectArrayElement(sensors, index, &sensor) || !sensor ||
        !UnrealObjectValid(sensor) || PendingDestroy(sensor))
      return false;
    if (UnrealObjectIsA(sensor, L"TnAiSensor")) {
      void* owner_controller = nullptr;
      void* owner_pawn = nullptr;
      if (!UnrealReadObjectProperty(sensor, L"_OwnerController",
                                    &owner_controller) ||
          owner_controller != g_swap_journal.target_controller ||
          !UnrealReadObjectProperty(sensor, L"_OwnerPawn", &owner_pawn) ||
          owner_pawn != expected_pawn)
        return false;
    }
  }
  return true;
}

bool WfcSwapOperations::TargetProxyLifecycleCoherent(
    void* expected_pawn) noexcept {
  if (!UnrealObjectIsA(expected_pawn, L"TnPlayerPawn") ||
      !TargetSensorsCoherent(expected_pawn))
    return false;
  void* runner = nullptr;
  void* action_set = nullptr;
  void* goal_set = nullptr;
  void* runner_owner = nullptr;
  bool suspended = false;
  bool suspend_pending = false;
  return ReadRequiredObject(g_swap_journal.target_controller, L"PlanRunner",
                            &runner) &&
         runner == g_swap_journal.target_plan_runner &&
         ReadRequiredObject(runner, L"_ActionSet", &action_set) &&
         action_set == g_swap_journal.target_action_set &&
         ReadRequiredObject(runner, L"_GoalSet", &goal_set) &&
         goal_set == g_swap_journal.target_goal_set &&
         UnrealReadObjectProperty(runner, L"_Owner", &runner_owner) &&
         runner_owner == g_swap_journal.target_controller &&
         UnrealReadBoolProperty(runner, L"_Suspended", &suspended) &&
         UnrealReadBoolProperty(runner, L"_SuspendPending", &suspend_pending) &&
         suspended && !suspend_pending;
}

bool WfcSwapOperations::TargetAiLifecycleCoherent(
    void* expected_pawn) noexcept {
  if (!UnrealObjectIsA(expected_pawn, L"TnAiPawn") ||
      !TargetSensorsCoherent(expected_pawn))
    return false;

  void* runner = nullptr;
  void* action_set = nullptr;
  void* goal_set = nullptr;
  void* runner_owner = nullptr;
  if (!ReadRequiredObject(g_swap_journal.target_controller, L"PlanRunner",
                          &runner) ||
      runner != g_swap_journal.target_plan_runner ||
      !ReadRequiredObject(runner, L"_ActionSet", &action_set) ||
      action_set != g_swap_journal.target_action_set ||
      !ReadRequiredObject(runner, L"_GoalSet", &goal_set) ||
      goal_set != g_swap_journal.target_goal_set ||
      !UnrealReadObjectProperty(runner, L"_Owner", &runner_owner) ||
      runner_owner != g_swap_journal.target_controller)
    return false;

  UnrealArrayView actions;
  if (!UnrealReadArrayProperty(action_set, L"Actions", &actions) ||
      actions.count < 0 || actions.count > 256)
    return false;
  for (int32_t index = 0; index < actions.count; ++index) {
    void* action = nullptr;
    void* bound_controller = nullptr;
    if (!UnrealReadObjectArrayElement(actions, index, &action) || !action ||
        !UnrealObjectValid(action) || PendingDestroy(action) ||
        !UnrealReadObjectProperty(action, L"_BoundController",
                                  &bound_controller) ||
        bound_controller != g_swap_journal.target_controller)
      return false;
    if (UnrealObjectIsA(action, L"TnGoapAction")) {
      void* transformer_controller = nullptr;
      void* transformer_pawn = nullptr;
      if (!UnrealReadObjectProperty(action, L"_TransformerController",
                                    &transformer_controller) ||
          transformer_controller != g_swap_journal.target_controller ||
          !UnrealReadObjectProperty(action, L"_TransformerPawn",
                                    &transformer_pawn) ||
          transformer_pawn != expected_pawn)
        return false;
    }
  }

  return true;
}

bool WfcSwapOperations::RebindTargetSensors(
    void* expected_pawn) noexcept {
  if (!UnrealObjectIsA(expected_pawn, L"TnPawn")) return false;
  g_swap_journal.target_sensor_rebind_started = true;
  if (!UnrealCallExactVoidObject(g_swap_journal.target_controller,
                                 g_swap_journal.ai_set_sensor_set,
                                 L"InSensorSet", nullptr))
    return false;
  void* detached_sensor_set = g_swap_journal.target_sensor_set;
  if (!UnrealReadObjectProperty(g_swap_journal.target_tactical_analyzer,
                                L"SensorSet", &detached_sensor_set) ||
      detached_sensor_set != nullptr ||
      !UnrealCallExactVoidObject(
          g_swap_journal.target_controller,
          g_swap_journal.ai_set_sensor_set, L"InSensorSet",
          g_swap_journal.target_sensor_set))
    return false;
  g_swap_journal.target_sensor_rebound =
      TargetSensorsCoherent(expected_pawn);
  return g_swap_journal.target_sensor_rebound;
}

bool WfcSwapOperations::RebindTargetAiLifecycle(
    void* expected_pawn) noexcept {
  // TnGoapAction._TransformerPawn is statically TnAiPawn, and common action
  // bytecode directly accesses TnAiPawn-owned fields (_LockAimOffset,
  // _HunkerDown, and others). TnPlayerPawn is a sibling, not a subclass.
  // Until every such action access has an exact-build compatibility adapter,
  // never bind this native action set to a TnPlayerPawn-shaped allocation.
  if (!UnrealObjectIsA(expected_pawn, L"TnAiPawn")) return false;

  // Clear the currently executing solution before rebinding the action set;
  // an action may retain callbacks and pawn-owned services from its former
  // actor. The existing action/goal/sensor objects are reused. In particular,
  // this avoids InitializeFromTemplate/ApplyCharacter and never destroys or
  // reconstructs the pawn's inventory.
  g_swap_journal.target_ai_lifecycle_rebind_started = true;
  if (!UnrealCallExactVoid(g_swap_journal.target_plan_runner,
                           g_swap_journal.plan_clear_solution))
    return false;

  void* current_solution = nullptr;
  void* pending_solution = nullptr;
  if (!UnrealReadObjectProperty(g_swap_journal.target_plan_runner,
                                L"_CurrentSolution", &current_solution) ||
      !UnrealReadObjectProperty(g_swap_journal.target_plan_runner,
                                L"_PendingSolution", &pending_solution) ||
      current_solution || pending_solution)
    return false;

  // HmGoapPlanRunner.set_ActionSet performs the native Unbind/Bind lifecycle
  // immediately. Calling the setter directly is intentional: the public
  // ReplaceActionSet path may defer replacement while the runner is
  // suspended. First publish null and prove it became null so even a setter
  // with same-value suppression cannot skip the subsequent bind of the
  // original set.
  if (!UnrealCallExactVoidObject(g_swap_journal.target_plan_runner,
                                 g_swap_journal.plan_set_action_set, L"Value",
                                 nullptr))
    return false;
  void* detached_action_set = g_swap_journal.target_action_set;
  if (!UnrealReadObjectProperty(g_swap_journal.target_plan_runner,
                                L"_ActionSet", &detached_action_set) ||
      detached_action_set != nullptr)
    return false;

  if (!UnrealCallExactVoidObject(
          g_swap_journal.target_plan_runner,
          g_swap_journal.plan_set_action_set, L"Value",
          g_swap_journal.target_action_set))
    return false;

  // Force the goal and sensor setters across a distinct value as well. This
  // removes any dependency on undocumented same-object suppression. The
  // decompiled TnTacticalAnalyzer.SetSensorSet path then initializes every
  // sensor with Owner.get_TransformerPawn() for the newly mapped actor.
  if (!UnrealCallExactVoidObject(g_swap_journal.target_plan_runner,
                                 g_swap_journal.plan_set_goal_set, L"Value",
                                 nullptr))
    return false;
  void* detached_goal_set = g_swap_journal.target_goal_set;
  if (!UnrealReadObjectProperty(g_swap_journal.target_plan_runner,
                                L"_GoalSet", &detached_goal_set) ||
      detached_goal_set != nullptr ||
      !UnrealCallExactVoidObject(
          g_swap_journal.target_plan_runner,
          g_swap_journal.plan_set_goal_set, L"Value",
          g_swap_journal.target_goal_set))
    return false;

  if (!RebindTargetSensors(expected_pawn))
    return false;

  g_swap_journal.target_ai_lifecycle_rebound =
      TargetAiLifecycleCoherent(expected_pawn);
  return g_swap_journal.target_ai_lifecycle_rebound;
}

CharacterSwapPreflight WfcSwapOperations::CapturePreflight(
    const CharacterSwapRequest& request,
    CharacterSwapCapture* output) noexcept {
  if (!output) return CharacterSwapPreflight::kReject;
  *output = {};
  const auto reject = [&request](const char* reason) noexcept {
    Log(LogLevel::kWarning,
        "[SquadSwitch] request=%llu preflight rejected: %s slot=%lu epoch=%llu pending=%lu",
        static_cast<unsigned long long>(request.request_id), reason,
        static_cast<unsigned long>(request.squad_slot),
        static_cast<unsigned long long>(g_world_epoch),
        static_cast<unsigned long>(PendingCommandCount()));
    return CharacterSwapPreflight::kReject;
  };
  if (request.squad_slot == 0 || request.squad_slot >= kSquadCapacity)
    return reject("invalid squad slot");
  if (request.world_generation != g_world_epoch)
    return reject("world generation changed");
  if (request.expected_world_object_index != UnrealObjectIndex(g_world_info))
    return reject("world object changed");
  if (request.expected_player_controller_object_index !=
      UnrealObjectIndex(g_controller))
    return reject("player controller changed");
  if (request.expected_player_pawn_object_index != UnrealObjectIndex(g_pawn))
    return reject("player pawn changed");
  if (request.expected_target_controller_object_index !=
      UnrealObjectIndex(g_squad_controllers[request.squad_slot]))
    return reject("target controller changed");
  if (request.expected_target_pawn_object_index !=
      UnrealObjectIndex(g_squad_pawns[request.squad_slot]))
    return reject("target pawn changed");
  if (g_squad_record_indices[request.squad_slot] < 0)
    return reject("target squad record unavailable");
  if (g_pending_ability_release.pending)
    return reject("ability release pending");
  if (PendingCommandCount() != 0)
    return reject("another command pending");

  Snapshot state;
  Snapshot restrictions;
  if (!request.checkpoint_native_restore) {
    if (!CopyLatestSnapshot(&state)) return reject("snapshot unavailable");
    if (state.lifecycle != Lifecycle::kLive)
      return reject("snapshot lifecycle is not live");
    if (state.world_epoch != g_world_epoch)
      return reject("snapshot world changed");
    if (request.squad_slot >= state.squad_count)
      return reject("snapshot squad slot disappeared");
    if (!state.squad[request.squad_slot].switchable)
      return reject("snapshot target is not switchable");
    const bool restrictions_read = ReadRestrictions(&restrictions, true);
    if (!restrictions_read || restrictions.paused ||
        restrictions.in_cutscene || restrictions.in_menu ||
        restrictions.in_scripted_sequence) {
      Log(LogLevel::kWarning,
          "[SquadSwitch] request=%llu restriction state read=%u combat=%u paused=%u cutscene=%u menu=%u scripted=%u",
          static_cast<unsigned long long>(request.request_id),
          restrictions_read ? 1u : 0u, restrictions.in_combat ? 1u : 0u,
          restrictions.paused ? 1u : 0u,
          restrictions.in_cutscene ? 1u : 0u,
          restrictions.in_menu ? 1u : 0u,
          restrictions.in_scripted_sequence ? 1u : 0u);
      return reject("live gameplay restrictions");
    }
  } else if (!DeferredCheckpointPending() ||
             !CharacterRoleMappingActive() ||
             !CharacterRoleMappingObjectsValid()) {
    return reject("checkpoint restore prerequisites");
  }

  float player_health = -1.0f;
  float player_max = -1.0f;
  float target_health = -1.0f;
  float target_max = -1.0f;
  bool player_downed = false;
  bool player_alive = false;
  bool target_downed = false;
  bool target_alive = false;
  void* target_pawn = g_squad_pawns[request.squad_slot];
  if (!ReadVitals(g_pawn, &player_health, &player_max, &player_downed,
                  &player_alive))
    return reject("player vitals unavailable");
  if (!ReadVitals(target_pawn, &target_health, &target_max, &target_downed,
                  &target_alive))
    return reject("target vitals unavailable");
  if (!player_alive || player_downed)
    return reject("player is dead or downed");
  if (!target_alive || target_downed)
    return reject("target is dead or downed");
  const TransformerMode player_mode = ReadMode(g_pawn);
  const TransformerMode target_mode = ReadMode(target_pawn);
  if (player_mode == TransformerMode::kUnknown ||
      player_mode == TransformerMode::kTransitioning)
    return reject("player form is unstable");
  if (target_mode == TransformerMode::kUnknown ||
      target_mode == TransformerMode::kTransitioning)
    return reject("target form is unstable");
  void* player_team = TeamFor(g_controller, g_pawn);
  void* target_team = TeamFor(
      g_squad_controllers[request.squad_slot], target_pawn);
  if (!player_team) return reject("player team unavailable");
  if (player_team != target_team) return reject("target team mismatch");
  if (!CaptureJournal(request)) return reject("journal capture failed");

  uint32_t player_hash = 0;
  uint32_t target_hash = 0;
  if (!ResolveLiveCharacterHash(g_swap_journal.player_controller,
                                g_swap_journal.player_pawn, &player_hash))
    return reject("player identity unavailable");
  if (!ResolveLiveCharacterHash(g_swap_journal.target_controller,
                                g_swap_journal.target_pawn, &target_hash))
    return reject("target identity unavailable");
  if (player_hash != request.expected_player_identity_hash)
    return reject("player identity changed");
  if (target_hash != request.expected_target_identity_hash)
    return reject("target identity changed");

  output->world_generation = g_world_epoch;
  output->world = SwapIdentity(g_swap_journal.world);
  output->player_controller = SwapIdentity(g_swap_journal.player_controller);
  output->player_pawn = SwapIdentity(g_swap_journal.player_pawn);
  output->target_ai_controller = SwapIdentity(g_swap_journal.target_controller);
  output->target_ai_pawn = SwapIdentity(g_swap_journal.target_pawn);
  output->player_controller_pawn_link = SwapIdentity(
      ControllerPawn(g_swap_journal.player_controller));
  output->player_pawn_controller_link = SwapIdentity(
      PawnController(g_swap_journal.player_pawn));
  output->target_controller_pawn_link = SwapIdentity(
      ControllerPawn(g_swap_journal.target_controller));
  output->target_pawn_controller_link = SwapIdentity(
      PawnController(g_swap_journal.target_pawn));
  output->player_identity_hash = player_hash;
  output->target_identity_hash = target_hash;
  output->squad_slot = request.squad_slot;
  output->squad_record_generation = g_swap_journal.squad_fingerprint;
  uint64_t safety = 1469598103934665603ull;
  const auto mix = [&safety](uint64_t value) {
    safety = (safety ^ value) * 1099511628211ull;
  };
  mix(static_cast<uint64_t>(g_swap_journal.player_mode));
  mix(static_cast<uint64_t>(g_swap_journal.target_mode));
  mix(static_cast<uint64_t>(UnrealObjectIndex(
      TeamFor(g_swap_journal.player_controller, g_swap_journal.player_pawn))));
  mix(static_cast<uint64_t>(g_pending_ability_release.pending ? 1 : 0));
  mix(static_cast<uint64_t>(PendingCommandCount()));
  output->safety_generation = safety ? safety : 1;
  output->adapter_cookie = static_cast<uintptr_t>(request.request_id);
  output->adapter_words[0] =
      reinterpret_cast<uintptr_t>(&g_swap_journal);
  return CharacterSwapPreflight::kReady;
}

CharacterSwapPreflight WfcSwapOperations::VerifyQuiesced(
    const CharacterSwapRequest& request,
    const CharacterSwapCapture& capture) noexcept {
  if (request.request_id != g_swap_journal.request_id ||
      capture.adapter_cookie != static_cast<uintptr_t>(request.request_id) ||
      capture.adapter_words[0] !=
          reinterpret_cast<uintptr_t>(&g_swap_journal) ||
      capture.world_generation != g_world_epoch ||
      g_swap_journal.world != g_world_info ||
      g_swap_journal.player_controller != g_controller ||
      g_swap_journal.player_pawn != g_pawn ||
      SquadGraphFingerprint() != g_swap_journal.squad_fingerprint ||
      !SupportedPlayerPair(g_swap_journal.player_controller,
                           g_swap_journal.player_pawn) ||
      !SupportedAiPair(g_swap_journal.target_controller,
                       g_swap_journal.target_pawn) ||
      !RoleMappingMatches(g_swap_journal.previous_mapping) ||
      !(g_swap_journal.target_lifecycle_was_proxy
            ? TargetProxyLifecycleCoherent(g_swap_journal.target_pawn)
            : TargetAiLifecycleCoherent(g_swap_journal.target_pawn)) ||
      g_pending_ability_release.pending || PendingCommandCount() != 0)
    return CharacterSwapPreflight::kReject;

  if (!request.checkpoint_native_restore) {
    Snapshot restrictions;
    if (!ReadRestrictions(&restrictions, true) ||
        restrictions.paused || restrictions.in_cutscene ||
        restrictions.in_menu || restrictions.in_scripted_sequence)
      return CharacterSwapPreflight::kReject;
  } else if (!DeferredCheckpointPending()) {
    return CharacterSwapPreflight::kReject;
  }

  bool suspended = false;
  bool suspend_pending = false;
  if (!UnrealReadBoolProperty(g_swap_journal.target_plan_runner,
                              L"_Suspended", &suspended) ||
      !UnrealReadBoolProperty(g_swap_journal.target_plan_runner,
                              L"_SuspendPending", &suspend_pending))
    return CharacterSwapPreflight::kReject;
  // Suspend() can enter through a pending state while the running action is
  // stopping. No ownership edge is allowed to change until the runner itself
  // reports the stable suspended state on a later engine tick.
  return suspended && !suspend_pending ? CharacterSwapPreflight::kReady
                                       : CharacterSwapPreflight::kWait;
}

bool WfcSwapOperations::RestorePreviousMapping() noexcept {
  if (g_swap_journal.mapping_restore.valid)
    return RestoreCharacterRoleMapping(&g_swap_journal.mapping_restore);
  return RoleMappingMatches(g_swap_journal.previous_mapping);
}

bool WfcSwapOperations::RestoreOriginalGraph() noexcept {
  bool ok = true;
  void* current = ControllerPawn(g_swap_journal.player_controller);
  if (current && current != g_swap_journal.player_pawn)
    ok = UnrealCallExactVoid(g_swap_journal.player_controller,
                             g_swap_journal.player_base_unpossess) && ok;
  current = ControllerPawn(g_swap_journal.target_controller);
  if (current && current != g_swap_journal.target_pawn)
    ok = UnrealCallExactVoid(g_swap_journal.target_controller,
                             g_swap_journal.ai_unpossess) && ok;

  // Do not restore/remove the role mapping while either cross-role link (or
  // an incoherent half-link) is still live. The adapter is the only thing
  // making those actor/controller combinations safe enough to inspect.
  void* player_controller_pawn =
      ControllerPawn(g_swap_journal.player_controller);
  void* target_controller_pawn =
      ControllerPawn(g_swap_journal.target_controller);
  void* player_pawn_controller = PawnController(g_swap_journal.player_pawn);
  void* target_pawn_controller = PawnController(g_swap_journal.target_pawn);
  const bool player_link_restorable =
      (player_controller_pawn == nullptr && player_pawn_controller == nullptr) ||
      (player_controller_pawn == g_swap_journal.player_pawn &&
       player_pawn_controller == g_swap_journal.player_controller);
  const bool target_link_restorable =
      (target_controller_pawn == nullptr && target_pawn_controller == nullptr) ||
      (target_controller_pawn == g_swap_journal.target_pawn &&
       target_pawn_controller == g_swap_journal.target_controller);
  if (!ok || !player_link_restorable || !target_link_restorable) {
    g_swap_journal.graph_restore_failed = true;
    return false;
  }

  if (!RestorePreviousMapping()) {
    g_swap_journal.graph_restore_failed = true;
    return false;
  }
  if (ControllerPawn(g_swap_journal.player_controller) !=
      g_swap_journal.player_pawn) {
    ok = UnrealCallExactVoidObjectBool(
             g_swap_journal.player_controller,
             g_swap_journal.player_game_possess, L"aPawn",
             g_swap_journal.player_pawn, L"bVehicleTransition", false) && ok;
  }
  if (ControllerPawn(g_swap_journal.target_controller) !=
      g_swap_journal.target_pawn)
    ok = UnrealCallExactVoidObjectBool(
             g_swap_journal.target_controller, g_swap_journal.ai_possess,
             L"inPawn", g_swap_journal.target_pawn,
             L"bVehicleTransition", false) && ok;
  if (ok && g_swap_journal.target_vehicle_ai_control_detached) {
    void* current_strategy = nullptr;
    void* const previous_strategy =
        g_swap_journal.previous_target_vehicle_ai_control;
    const bool previous_strategy_still_valid =
        previous_strategy && UnrealObjectValid(previous_strategy) &&
        UnrealObjectIndex(previous_strategy) ==
            g_swap_journal.previous_target_vehicle_ai_control_index;
    bool restored =
        previous_strategy_still_valid &&
        ReadOptionalObject(g_swap_journal.target_controller,
                           L"_VehicleAiControl", &current_strategy) &&
        current_strategy == nullptr &&
        UnrealWriteObjectPropertyExpected(
            g_swap_journal.target_controller, L"_VehicleAiControl", nullptr,
            previous_strategy);
    if (restored) {
      g_swap_journal.restored_target_vehicle_ai_control = previous_strategy;
    }
    ok = restored && ok;
  }
  if (ok && g_swap_journal.target_vehicle_ai_control_rebuilt) {
    // The cross-role bind creates a strategy for the former player pawn. Once
    // the original target is restored, rebuild from that pawn/controller pair
    // instead of retaining the now-wrong strategy or a possibly GC'd raw
    // pointer captured before the transaction.
    void* current_strategy = nullptr;
    bool pointer_restored =
        ReadOptionalObject(g_swap_journal.target_controller,
                           L"_VehicleAiControl", &current_strategy);
    void* const previous_strategy =
        g_swap_journal.previous_target_vehicle_ai_control;
    const bool previous_strategy_still_valid =
        previous_strategy && UnrealObjectValid(previous_strategy) &&
        UnrealObjectIndex(previous_strategy) ==
            g_swap_journal.previous_target_vehicle_ai_control_index;
    if (pointer_restored && previous_strategy_still_valid &&
        current_strategy != previous_strategy) {
      pointer_restored = UnrealWriteObjectPropertyExpected(
          g_swap_journal.target_controller, L"_VehicleAiControl",
          current_strategy, previous_strategy);
      current_strategy = pointer_restored ? previous_strategy
                                          : current_strategy;
    } else if (pointer_restored && !previous_strategy && current_strategy) {
      pointer_restored = UnrealWriteObjectPropertyExpected(
          g_swap_journal.target_controller, L"_VehicleAiControl",
          current_strategy, nullptr);
      if (pointer_restored) current_strategy = nullptr;
    } else if (previous_strategy && !previous_strategy_still_valid) {
      pointer_restored = false;
    }

    if (!pointer_restored) {
      ok = UnrealCallExactVoid(g_swap_journal.target_controller,
                               g_swap_journal.ai_create_vehicle_control) && ok;
      current_strategy = nullptr;
      ok = ReadOptionalObject(g_swap_journal.target_controller,
                              L"_VehicleAiControl", &current_strategy) && ok;
      if (previous_strategy && !current_strategy) ok = false;
      if (!previous_strategy && current_strategy) {
        const bool cleared = UnrealWriteObjectPropertyExpected(
            g_swap_journal.target_controller, L"_VehicleAiControl",
            current_strategy, nullptr);
        ok = cleared && ok;
        if (cleared) current_strategy = nullptr;
      }
    }
    g_swap_journal.restored_target_vehicle_ai_control = current_strategy;
  }
  if (ok && (g_swap_journal.target_ai_lifecycle_rebind_started ||
             g_swap_journal.target_sensor_rebind_started)) {
    // The same controller is back on its original pawn and the previous role
    // mapping is active. Re-run the cache-only planning/sensor lifecycle now;
    // doing it in kRebindRuntimeServices rollback would be too early because
    // that step executes while the cross-role graph is still live.
    const bool lifecycle_restored =
        g_swap_journal.target_lifecycle_was_proxy
            ? RebindTargetSensors(g_swap_journal.target_pawn)
            : (g_swap_journal.target_ai_lifecycle_rebind_started
                   ? RebindTargetAiLifecycle(g_swap_journal.target_pawn)
                   : RebindTargetSensors(g_swap_journal.target_pawn));
    ok = lifecycle_restored && ok;
    if (lifecycle_restored) {
      g_swap_journal.target_ai_lifecycle_rebind_started = false;
      g_swap_journal.target_ai_lifecycle_rebound = false;
      g_swap_journal.target_sensor_rebind_started = false;
      g_swap_journal.target_sensor_rebound = false;
      // ClearSolution deliberately discards the old actor's running action.
      // Queue a fresh plan against the restored pawn before the later reverse
      // quiesce step resumes the runner.
      if (!g_swap_journal.target_lifecycle_was_proxy)
        ok = UnrealCallVoidByte(g_swap_journal.target_controller,
                                L"RequestReplan", L"Priority", 1u) && ok;
    }
  }
  g_swap_journal.player_detached = false;
  g_swap_journal.target_detached = false;
  g_swap_journal.player_bound = false;
  g_swap_journal.target_bound = false;
  const bool restored =
      ok && SupportedPlayerPair(g_swap_journal.player_controller,
                                g_swap_journal.player_pawn) &&
      SupportedAiPair(g_swap_journal.target_controller,
                      g_swap_journal.target_pawn);
  g_swap_journal.graph_restore_failed = !restored;
  return restored;
}

bool WfcSwapOperations::RebindPlayerServices(void* pawn, void* ability_hud,
                                              void* buff_hud) noexcept {
  void* ability_manager = pawn == g_swap_journal.target_pawn
                              ? g_swap_journal.target_ability_manager
                              : g_swap_journal.player_ability_manager;
  void* buff_manager = pawn == g_swap_journal.target_pawn
                           ? g_swap_journal.target_buff_manager
                           : g_swap_journal.player_buff_manager;
  void* targeting_owner = pawn == g_swap_journal.target_pawn
                              ? pawn
                              : g_swap_journal.previous_targeting_owner;
  bool ok = UnrealCallVoidObject(g_swap_journal.player_targeting,
                                 L"set_TargetingOwner", L"Value",
                                 targeting_owner);
  ok = UnrealCallVoidObjectObject(
      g_swap_journal.pickup_manager, L"Initialize", L"OwnerP", pawn,
      L"ContextualCommander_", g_swap_journal.contextual_commander) && ok;
  ok = UnrealCallVoidObjectObjectObject(
           g_swap_journal.fine_aim_manager, L"Initialize", L"P", pawn,
           L"pt", g_swap_journal.player_targeting, L"CSM",
           g_swap_journal.camera_manager) && ok;
  ok = UnrealCallVoidObject(g_swap_journal.camera_manager, L"set_CameraOwner",
                            L"Value", pawn) && ok;
  ok = UnrealCallVoidObject(g_swap_journal.dialog_manager,
                            L"InitializeWithPlayerPawn", L"_OwnerPawn",
                            pawn) && ok;
  ok = UnrealCallVoidObject(ability_manager, L"SetHud", L"NewHud",
                            ability_hud) && ok;
  ok = UnrealCallVoidObject(buff_manager, L"SetHud", L"NewHud", buff_hud) &&
       ok;
  ok = UnrealCallVoid(g_swap_journal.point_of_interest_manager, L"Reset") &&
       ok;
  return ok;
}

bool WfcSwapOperations::ResetTargetWeaponShooter() noexcept {
  if (!UnrealObjectValid(g_swap_journal.target_weapon_shooter) ||
      !UnrealCallVoid(g_swap_journal.target_weapon_shooter, L"Deactivate") ||
      !UnrealCallVoid(g_swap_journal.target_weapon_shooter, L"Reset"))
    return false;
  void* active_weapon = reinterpret_cast<void*>(1);
  return UnrealCallObject(g_swap_journal.target_weapon_shooter,
                          L"get_ActiveWeapon", &active_weapon) &&
         active_weapon == nullptr;
}

bool WfcSwapOperations::ActivateTargetWeaponShooter(void* pawn) noexcept {
  if (!pawn || !UnrealObjectValid(pawn) || PendingDestroy(pawn)) return false;
  void* shooter_owner = nullptr;
  void* aimer_owner = nullptr;
  void* aimer_controller = nullptr;
  void* manager = nullptr;
  void* pawn_weapon = nullptr;
  if (!UnrealReadObjectProperty(g_swap_journal.target_weapon_shooter, L"Owner",
                                &shooter_owner) ||
      shooter_owner != pawn ||
      !UnrealReadObjectProperty(g_swap_journal.target_weapon_aimer, L"_Owner",
                                &aimer_owner) ||
      aimer_owner != pawn ||
      !UnrealReadObjectProperty(g_swap_journal.target_weapon_aimer,
                                L"_OwnerController", &aimer_controller) ||
      aimer_controller != g_swap_journal.target_controller ||
      !ReadRequiredObject(pawn, L"InvManager", &manager) ||
      !UnrealObjectIsA(manager, L"TnInventoryManager") ||
      !UnrealReadObjectProperty(pawn, L"Weapon", &pawn_weapon))
    return false;
  void* weapon = pawn_weapon;
  if (!weapon) {
    void* active_weapon = reinterpret_cast<void*>(1);
    return UnrealCallObject(g_swap_journal.target_weapon_shooter,
                            L"get_ActiveWeapon", &active_weapon) &&
           active_weapon == nullptr;
  }
  if (!UnrealObjectIsA(weapon, L"TnWeapon") || PendingDestroy(weapon))
    return false;
  void* weapon_class = UnrealObjectClass(weapon);
  if (!weapon_class ||
      !UnrealCallVoidObject(g_swap_journal.target_weapon_shooter, L"Activate",
                            L"WeaponClass", weapon_class))
    return false;
  void* active_weapon = nullptr;
  return UnrealCallObject(g_swap_journal.target_weapon_shooter,
                          L"get_ActiveWeapon", &active_weapon) &&
         active_weapon == weapon;
}

bool WfcSwapOperations::ApplyInventoryWeaponRoles() noexcept {
  if (!InventoryCaptureStillCurrent(
          g_swap_journal.player_pawn,
          g_swap_journal.player_inventory_weapons,
          g_swap_journal.player_inventory_weapon_count, 2u,
          g_swap_journal.player_inventory_manager,
          g_swap_journal.player_inventory_manager_index,
          g_swap_journal.player_inventory_head,
          g_swap_journal.player_inventory_head_index,
          g_swap_journal.player_inventory_fingerprint) ||
      !InventoryCaptureStillCurrent(
          g_swap_journal.target_pawn,
          g_swap_journal.target_inventory_weapons,
          g_swap_journal.target_inventory_weapon_count, 1u,
          g_swap_journal.target_inventory_manager,
          g_swap_journal.target_inventory_manager_index,
          g_swap_journal.target_inventory_head,
          g_swap_journal.target_inventory_head_index,
          g_swap_journal.target_inventory_fingerprint))
    return false;
  const auto validate = [](const auto& entries, uint32_t count) {
    for (uint32_t index = 0; index < count; ++index) {
      const auto& entry = entries[index];
      uint8_t current_type = 0;
      void* current_data = nullptr;
      float current_heat = 0.0f;
      if (!entry.weapon || !entry.object_index ||
          !UnrealObjectValid(entry.weapon) || PendingDestroy(entry.weapon) ||
          UnrealObjectIndex(entry.weapon) != entry.object_index ||
          !UnrealObjectIsA(entry.weapon, L"TnWeapon") ||
          !entry.previous_data || !UnrealObjectValid(entry.previous_data) ||
          !entry.desired_data || !UnrealObjectValid(entry.desired_data) ||
          !UnrealReadByteProperty(entry.weapon, L"WeaponDataType",
                                  &current_type) ||
          !UnrealReadObjectProperty(entry.weapon, L"Data", &current_data) ||
          current_type != entry.previous_data_type ||
          current_data != entry.previous_data)
        return false;
      if (entry.heat_state_captured &&
          (!entry.weapon_heat || !UnrealObjectValid(entry.weapon_heat) ||
           UnrealObjectIndex(entry.weapon_heat) != entry.weapon_heat_index ||
           !UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                    &current_heat) ||
           std::fabs(current_heat - entry.current_heat) > 0.001f))
        return false;
    }
    return true;
  };
  if (!validate(g_swap_journal.player_inventory_weapons,
                g_swap_journal.player_inventory_weapon_count) ||
      !validate(g_swap_journal.target_inventory_weapons,
                g_swap_journal.target_inventory_weapon_count))
    return false;

  const auto apply = [](auto* entries, uint32_t count, uint8_t role) {
    for (uint32_t index = 0; index < count; ++index) {
      auto& entry = (*entries)[index];
      if (entry.previous_data_type == role) continue;
      // Mark before crossing the native mutation boundary so rollback also
      // covers a setter whose postcondition check fails after SetData ran.
      entry.changed = true;
      uint8_t current_type = 0;
      void* current_data = nullptr;
      float current_heat = 0.0f;
      if (!UnrealCallVoidByte(entry.weapon, L"SetWeaponDataType", L"Type",
                              role))
        return false;
      if (entry.heat_state_captured) {
        void* owned_heat = nullptr;
        uint32_t owned_heat_index = 0;
        if (!ReadRequiredObject(entry.weapon, L"WeaponHeat", &owned_heat) ||
            !UnrealObjectIsA(owned_heat, L"TnWeaponHeat") ||
            !(owned_heat_index = UnrealObjectIndex(owned_heat)))
          return false;
        // SetData may rebuild the heat service. Continue through the component
        // currently owned by the weapon, never a detached pre-conversion object.
        entry.weapon_heat = owned_heat;
        entry.weapon_heat_index = owned_heat_index;
        if (!UnrealCallVoidFloat(entry.weapon_heat, L"SyncCurrentHeat",
                                 L"SyncHeat", entry.current_heat) ||
            !UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                     &current_heat) ||
            std::fabs(current_heat - entry.current_heat) > 0.001f)
          return false;
      }
      if (!UnrealReadByteProperty(entry.weapon, L"WeaponDataType",
                                  &current_type) ||
          !UnrealReadObjectProperty(entry.weapon, L"Data", &current_data) ||
          current_type != role || current_data != entry.desired_data)
        return false;
    }
    return true;
  };
  return apply(&g_swap_journal.target_inventory_weapons,
               g_swap_journal.target_inventory_weapon_count, 1u) &&
         apply(&g_swap_journal.player_inventory_weapons,
               g_swap_journal.player_inventory_weapon_count, 2u);
}

bool WfcSwapOperations::RestoreInventoryWeaponRoles() noexcept {
  bool ok = true;
  const auto restore = [&ok](auto* entries, uint32_t count) {
    while (count != 0) {
      auto& entry = (*entries)[--count];
      if (!entry.changed) continue;
      uint8_t restored_type = 0;
      void* restored_data = nullptr;
      float restored_heat = 0.0f;
      void* currently_owned_heat = nullptr;
      bool restored =
          entry.weapon && entry.object_index &&
          UnrealObjectValid(entry.weapon) && !PendingDestroy(entry.weapon) &&
          UnrealObjectIndex(entry.weapon) == entry.object_index;
      if (restored && entry.heat_state_captured) {
        restored = ReadRequiredObject(entry.weapon, L"WeaponHeat",
                                      &currently_owned_heat) &&
                   currently_owned_heat == entry.weapon_heat &&
                   UnrealObjectIndex(currently_owned_heat) ==
                       entry.weapon_heat_index;
      }
      restored = restored &&
                 UnrealCallVoidByte(entry.weapon, L"SetWeaponDataType", L"Type",
                                    entry.previous_data_type);
      if (restored && entry.heat_state_captured) {
        void* restored_heat_object = nullptr;
        uint32_t restored_heat_index = 0;
        restored =
            ReadRequiredObject(entry.weapon, L"WeaponHeat",
                               &restored_heat_object) &&
            UnrealObjectIsA(restored_heat_object, L"TnWeaponHeat") &&
            (restored_heat_index = UnrealObjectIndex(restored_heat_object)) !=
                0;
        if (restored) {
          entry.weapon_heat = restored_heat_object;
          entry.weapon_heat_index = restored_heat_index;
          restored =
              UnrealCallVoidFloat(entry.weapon_heat, L"SyncCurrentHeat",
                                  L"SyncHeat", entry.current_heat) &&
              UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                      &restored_heat) &&
              std::fabs(restored_heat - entry.current_heat) <= 0.001f;
        }
      }
      restored = restored &&
                 UnrealReadByteProperty(entry.weapon, L"WeaponDataType",
                                        &restored_type) &&
                 UnrealReadObjectProperty(entry.weapon, L"Data",
                                          &restored_data) &&
                 restored_type == entry.previous_data_type &&
                 restored_data == entry.previous_data;
      if (restored) entry.changed = false;
      ok = restored && ok;
    }
  };
  restore(&g_swap_journal.player_inventory_weapons,
          g_swap_journal.player_inventory_weapon_count);
  restore(&g_swap_journal.target_inventory_weapons,
          g_swap_journal.target_inventory_weapon_count);
  return ok;
}

bool WfcSwapOperations::RestoreDeferredRuntimeState() noexcept {
  // These native services inspect Controller/Pawn ownership while they run.
  // Rollback records their work in reverse transaction order, but performs it
  // only after RestoreOriginalGraph has rebuilt both reciprocal native pairs.
  if (!SupportedPlayerPair(g_swap_journal.player_controller,
                           g_swap_journal.player_pawn) ||
      !SupportedAiPair(g_swap_journal.target_controller,
                       g_swap_journal.target_pawn) ||
      !RoleMappingMatches(g_swap_journal.previous_mapping))
    return false;

  bool ok = true;
  if (g_swap_journal.identity_restore_pending) {
    bool restored = true;
    if (g_swap_journal.selected_character_updated) {
      UnrealStructValueView player_character;
      UnrealStructValueView target_character;
      bool rollback_assignment_started = false;
      bool replicated_restored =
          UnrealReadStructProperty(g_swap_journal.player_pawn,
                                   L"_CurrentCharacterCompressed",
                                   &player_character);
      if (replicated_restored && g_swap_journal.target_pri) {
        replicated_restored =
            UnrealReadStructProperty(g_swap_journal.target_pawn,
                                     L"_CurrentCharacterCompressed",
                                     &target_character) &&
            UnrealSynchronouslyAssignCharacterPair(
                g_swap_journal.player_pri, g_swap_journal.target_pri,
                g_swap_journal.player_select_character,
                g_swap_journal.player_client_start_character_load,
                g_swap_journal.player_confirm_selected_character,
                g_swap_journal.player_refresh_provider,
                player_character, target_character,
                g_game_thread.load(std::memory_order_relaxed),
                &rollback_assignment_started);
      } else if (replicated_restored) {
        replicated_restored = UnrealSynchronouslyAssignCharacter(
            g_swap_journal.player_pri,
            g_swap_journal.player_select_character,
            g_swap_journal.player_client_start_character_load,
            g_swap_journal.player_confirm_selected_character,
            g_swap_journal.player_refresh_provider, player_character,
            g_game_thread.load(std::memory_order_relaxed),
            &rollback_assignment_started);
      }
      (void)rollback_assignment_started;
      if (replicated_restored)
        g_swap_journal.selected_character_updated = false;
      restored = replicated_restored && restored;
    }
    if (g_swap_journal.identity_swapped && g_swap_journal.target_pri) {
      const bool resolved_restored = UnrealSwapStructProperties(
          g_swap_journal.player_pri, L"_CurrentCharacter",
          g_swap_journal.target_pri, L"_CurrentCharacter");
      if (resolved_restored) g_swap_journal.identity_swapped = false;
      restored = resolved_restored && restored;
    }
    if (g_swap_journal.player_current_identity_updated) {
      UnrealStructValueView player_current;
      const bool current_restored =
          UnrealReadStructProperty(g_swap_journal.player_pawn,
                                   L"_CurrentCharacter", &player_current) &&
          UnrealCallExactVoidStruct(
              g_swap_journal.player_pri,
              g_swap_journal.player_set_current_character, L"Value",
              player_current);
      if (current_restored)
        g_swap_journal.player_current_identity_updated = false;
      restored = current_restored && restored;
    }
    restored = restored &&
               (g_swap_journal.target_pri
                    ? PublishPriIdentityPair(
                          g_swap_journal.player_pri,
                          g_swap_journal.player_pawn,
                          g_swap_journal.target_pri,
                          g_swap_journal.target_pawn,
                          g_swap_journal.game_replication_info,
                          g_swap_journal.player_refresh_provider)
                    : PublishPlayerIdentity(
                          g_swap_journal.player_pri,
                          g_swap_journal.player_pawn,
                          g_swap_journal.game_replication_info,
                          g_swap_journal.player_refresh_provider));
    if (restored) g_swap_journal.identity_restore_pending = false;
    ok = restored && ok;
  }

  if (g_swap_journal.squad_restore_pending) {
    UnrealStructValueView target_character;
    const bool restored =
        UnrealReadStructProperty(g_swap_journal.target_pawn,
                                 L"_CurrentCharacter", &target_character) &&
        UnrealAssignStructArrayStringField(
            g_swap_journal.squad_records,
            g_swap_journal.squad_record_index, L"CharacterName",
            target_character, L"CharacterName");
    if (restored) {
      g_swap_journal.squad_name_updated = false;
      g_swap_journal.squad_restore_pending = false;
    }
    ok = restored && ok;
  }

  if (g_swap_journal.runtime_services_restore_pending) {
    bool restored = true;
    if (g_swap_journal.target_shooter_rebound) {
      const bool shooter_reset = ResetTargetWeaponShooter();
      if (shooter_reset) g_swap_journal.target_shooter_rebound = false;
      restored = shooter_reset && restored;
    }
    restored = RestoreInventoryWeaponRoles() && restored;
    if (g_swap_journal.player_collision_role_changed) {
      const bool collision_restored = UnrealCallVoidByteBool(
          g_swap_journal.player_collision_controller,
          L"SetRBCollidesWithChannel", L"Channel", 15u, L"Value", true);
      if (collision_restored)
        g_swap_journal.player_collision_role_changed = false;
      restored = collision_restored && restored;
    }
    if (g_swap_journal.target_collision_role_changed) {
      const bool collision_restored = UnrealCallVoidByteBool(
          g_swap_journal.target_collision_controller,
          L"SetRBCollidesWithChannel", L"Channel", 15u, L"Value", false);
      if (collision_restored)
        g_swap_journal.target_collision_role_changed = false;
      restored = collision_restored && restored;
    }
    restored = RebindPlayerServices(
                   g_swap_journal.player_pawn,
                   g_swap_journal.player_ability_hud,
                   g_swap_journal.player_buff_hud) &&
               restored;
    if (g_swap_journal.target_dialog_rebound) {
      const bool dialog_restored = UnrealCallVoidObject(
          g_swap_journal.target_dialog_manager, L"InitializeWithAiPawn",
          L"_OwnerPawn", g_swap_journal.previous_target_dialog_owner);
      if (dialog_restored) g_swap_journal.target_dialog_rebound = false;
      restored = dialog_restored && restored;
    }
    restored = UnrealCallVoidObject(g_swap_journal.target_ability_manager,
                                    L"SetHud", L"NewHud",
                                    g_swap_journal.target_ability_hud) &&
               restored;
    restored = UnrealCallVoidObject(g_swap_journal.target_buff_manager,
                                    L"SetHud", L"NewHud",
                                    g_swap_journal.target_buff_hud) &&
               restored;
    if (restored) {
      g_swap_journal.services_rebound = false;
      g_swap_journal.runtime_services_restore_pending = false;
    }
    ok = restored && ok;
  }

  if (g_swap_journal.camera_restore_pending) {
    bool restored = UnrealCallVoidObject(
        g_swap_journal.camera_manager, L"set_CameraOwner", L"Value",
        g_swap_journal.previous_camera_owner);
    restored = UnrealCallVoidObject(
                   g_swap_journal.player_controller, L"SetViewTarget",
                   L"NewViewTarget", g_swap_journal.previous_view_target) &&
               restored;
    const uint8_t form =
        g_swap_journal.player_mode == TransformerMode::kVehicle ? 1u : 0u;
    restored = UnrealCallVoidByte(g_swap_journal.player_controller,
                                  L"GotoNewFormState", L"TargetForm", form) &&
               restored;
    if (restored) {
      g_swap_journal.camera_rebound = false;
      g_swap_journal.camera_restore_pending = false;
    }
    ok = restored && ok;
  }
  return ok;
}

CharacterSwapApplyResult WfcSwapOperations::ApplyMutation(
    CharacterSwapMutation mutation, const CharacterSwapRequest& request,
    CharacterSwapCapture* capture) noexcept {
  if (!capture || request.request_id != g_swap_journal.request_id ||
      capture->adapter_cookie != static_cast<uintptr_t>(request.request_id) ||
      capture->adapter_words[0] !=
          reinterpret_cast<uintptr_t>(&g_swap_journal))
    return CharacterSwapApplyResult::kFailedNoMutation;

  switch (mutation) {
    case CharacterSwapMutation::kQuiescePlayerInput: {
      if (!UnrealCallVoidBool(g_swap_journal.player_controller,
                              L"IgnoreMoveInput", L"bNewMoveInput", true))
        return CharacterSwapApplyResult::kFailedNoMutation;
      g_swap_journal.player_move_ignored = true;
      if (!UnrealCallVoidBool(g_swap_journal.player_controller,
                              L"IgnoreLookInput", L"bNewLookInput", true)) {
        const bool restored = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreMoveInput",
            L"bNewMoveInput", false);
        g_swap_journal.player_move_ignored = !restored;
        return restored ? CharacterSwapApplyResult::kFailedNoMutation
                        : CharacterSwapApplyResult::kFailedNeedsRollback;
      }
      g_swap_journal.player_look_ignored = true;
      // ClearInputs is intentionally last: if either reversible guard cannot
      // be acquired, no irreversible input-edge mutation has happened. Once
      // both guards are active, any later failure belongs on the rollback
      // stack even though clearing an already-held edge is not reconstructible.
      if (!UnrealCallVoid(g_swap_journal.player_controller, L"ClearInputs"))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      return CharacterSwapApplyResult::kApplied;
    }
    case CharacterSwapMutation::kQuiesceTargetAi:
      {
        bool suspended = false;
        bool suspend_pending = false;
        if (!UnrealReadBoolProperty(g_swap_journal.target_plan_runner,
                                    L"_Suspended", &suspended) ||
            !UnrealReadBoolProperty(g_swap_journal.target_plan_runner,
                                    L"_SuspendPending", &suspend_pending) ||
            suspend_pending ||
            suspended != g_swap_journal.target_plan_was_suspended)
          return CharacterSwapApplyResult::kFailedNoMutation;
        if (suspended) {
          g_swap_journal.target_ai_suspended = true;
          return CharacterSwapApplyResult::kApplied;
        }
      }
      if (!UnrealCallVoid(g_swap_journal.target_controller,
                          L"SuspendPlanRunner"))
        return CharacterSwapApplyResult::kFailedNoMutation;
      g_swap_journal.target_ai_suspended = true;
      return CharacterSwapApplyResult::kApplied;
    case CharacterSwapMutation::kPrepareRoleAdapters:
      {
        // Possess does not detach TnAiWeaponShooter callbacks from the old
        // weapon. Quiesce/reset them before either pawn ownership edge moves,
        // otherwise the newly player-controlled weapon can still invoke the
        // AI controller's reload/fire callbacks.
        g_swap_journal.target_shooter_quiesced = true;
        if (!ResetTargetWeaponShooter())
          return CharacterSwapApplyResult::kFailedNeedsRollback;
        bool move_delegates_cleared = false;
        if (!UnrealCallVoid(g_swap_journal.target_controller, L"CancelMove") ||
            (!g_swap_journal.target_lifecycle_was_proxy &&
             !UnrealCallVoid(g_swap_journal.target_controller,
                             L"StopMovement")) ||
            !UnrealCallBool(g_swap_journal.target_controller,
                            L"MoveDelegatesCleared",
                            &move_delegates_cleared) ||
            !move_delegates_cleared)
          return CharacterSwapApplyResult::kFailedNeedsRollback;
        const bool was_ready = CharacterRoleAdapterReady();
        if (!PrepareCharacterRoleAdapter(g_swap_journal.player_controller,
                                         g_swap_journal.target_controller,
                                         g_swap_journal.target_pawn)) {
          // A first-time install can fail after placing one or more executor
          // detours. Attempt immediate teardown; if that cannot be proved,
          // include this step in the transaction rollback stack.
          if (!was_ready && !ShutdownCharacterRoleAdapter()) {
            g_swap_journal.role_adapter_installed_here = true;
            return CharacterSwapApplyResult::kFailedNeedsRollback;
          }
          return CharacterSwapApplyResult::kFailedNoMutation;
        }
        g_swap_journal.role_adapter_installed_here = !was_ready;
      }
      // Preparation must remain behaviorally inert. Publishing the cross-role
      // mapping here would expose it to a complete original engine tick before
      // either controller/pawn graph has been detached.
      if (!RoleMappingMatches(g_swap_journal.previous_mapping))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      return CharacterSwapApplyResult::kApplied;
    case CharacterSwapMutation::kDetachOriginalControlLinks: {
      // Keep the previous role map active while detaching the graph it
      // describes. This matters on B->C and B->A swaps: publishing the new map
      // first makes get_TransformerPawn report C (or removes the adapter for
      // A) while UnPossess is still dismantling B's reciprocal links.
      if (!RoleMappingMatches(g_swap_journal.previous_mapping))
        return CharacterSwapApplyResult::kFailedNoMutation;
      if (!UnrealCallExactVoid(g_swap_journal.player_controller,
                               g_swap_journal.player_base_unpossess))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      g_swap_journal.player_detached = true;
      if (ControllerPawn(g_swap_journal.player_controller) != nullptr ||
          PawnController(g_swap_journal.player_pawn) != nullptr)
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      if (!UnrealCallExactVoid(g_swap_journal.target_controller,
                               g_swap_journal.ai_unpossess))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      g_swap_journal.target_detached = true;
      if (ControllerPawn(g_swap_journal.target_controller) != nullptr ||
          PawnController(g_swap_journal.target_pawn) != nullptr)
        return CharacterSwapApplyResult::kFailedNeedsRollback;

      // Both old pairs are now fully detached. Publish the new map immediately
      // before the two bind mutations; the whole sequence remains within this
      // atomic-commit game-thread tick, so no native frame observes an empty
      // controller graph or a map/graph mismatch.
      if (!SetCharacterRoleMapping(g_swap_journal.desired_mapping,
                                   &g_swap_journal.mapping_restore) ||
          !RoleMappingMatches(g_swap_journal.desired_mapping))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      return CharacterSwapApplyResult::kApplied;
    }
    case CharacterSwapMutation::kBindPlayerControllerToTarget: {
      if (!UnrealCallExactVoidObjectBool(
              g_swap_journal.player_controller,
              g_swap_journal.player_game_possess, L"aPawn",
              g_swap_journal.target_pawn, L"bVehicleTransition", false))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      g_swap_journal.player_bound = true;
      return ControllerPawn(g_swap_journal.player_controller) ==
                     g_swap_journal.target_pawn &&
                 PawnController(g_swap_journal.target_pawn) ==
                     g_swap_journal.player_controller
             ? CharacterSwapApplyResult::kApplied
             : CharacterSwapApplyResult::kFailedNeedsRollback;
    }
    case CharacterSwapMutation::kBindAiControllerToFormerPlayer:
      if (!UnrealCallExactVoidObjectBool(
              g_swap_journal.target_controller, g_swap_journal.ai_possess,
              L"inPawn", g_swap_journal.player_pawn,
              L"bVehicleTransition", false))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      g_swap_journal.target_bound = true;
      return ControllerPawn(g_swap_journal.target_controller) ==
                     g_swap_journal.player_pawn &&
                 PawnController(g_swap_journal.player_pawn) ==
                     g_swap_journal.target_controller
             ? CharacterSwapApplyResult::kApplied
             : CharacterSwapApplyResult::kFailedNeedsRollback;
    case CharacterSwapMutation::kUpdateSquadMembership: {
      UnrealStructValueView player_character;
      if (!UnrealReadStructProperty(g_swap_journal.player_pawn,
                                    L"_CurrentCharacter",
                                    &player_character))
        return CharacterSwapApplyResult::kFailedNoMutation;
      // FString assignment can cross its mutation boundary before a helper
      // reports a failed postcondition, so arm rollback before invoking it.
      g_swap_journal.squad_name_updated = true;
      if (!UnrealAssignStructArrayStringField(
              g_swap_journal.squad_records,
              g_swap_journal.squad_record_index, L"CharacterName",
              player_character, L"CharacterName"))
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      return CharacterSwapApplyResult::kApplied;
    }
    case CharacterSwapMutation::kUpdateCharacterIdentity: {
      UnrealStructValueView target_character;
      UnrealStructValueView player_character;
      UnrealStructValueView target_current;
      bool identity_assignment_started = false;
      if (!UnrealReadStructProperty(
              g_swap_journal.target_pawn,
              L"_CurrentCharacterCompressed", &target_character) ||
          !UnrealReadStructProperty(
              g_swap_journal.player_pawn,
              L"_CurrentCharacterCompressed", &player_character) ||
          !UnrealReadStructProperty(g_swap_journal.target_pawn,
                                    L"_CurrentCharacter",
                                    &target_current))
        return CharacterSwapApplyResult::kFailedNoMutation;

      if (g_swap_journal.target_pri) {
        if (!UnrealSwapStructProperties(
                g_swap_journal.player_pri, L"_CurrentCharacter",
                g_swap_journal.target_pri, L"_CurrentCharacter"))
          return CharacterSwapApplyResult::kFailedNeedsRollback;
        g_swap_journal.identity_swapped = true;
      } else {
        // Campaign squad identities live on their pawn/squad record. Move
        // only the real local-player PRI to the selected existing pawn.
        if (!UnrealCallExactVoidStruct(
                g_swap_journal.player_pri,
                g_swap_journal.player_set_current_character, L"Value",
                target_current))
          return CharacterSwapApplyResult::kFailedNeedsRollback;
        g_swap_journal.player_current_identity_updated = true;
      }

      const bool selected_updated =
          g_swap_journal.target_pri
              ? UnrealSynchronouslyAssignCharacterPair(
                    g_swap_journal.player_pri,
                    g_swap_journal.target_pri,
                    g_swap_journal.player_select_character,
                    g_swap_journal.player_client_start_character_load,
                    g_swap_journal.player_confirm_selected_character,
                    g_swap_journal.player_refresh_provider,
                    target_character, player_character,
                    g_game_thread.load(std::memory_order_relaxed),
                    &identity_assignment_started)
              : UnrealSynchronouslyAssignCharacter(
                    g_swap_journal.player_pri,
                    g_swap_journal.player_select_character,
                    g_swap_journal.player_client_start_character_load,
                    g_swap_journal.player_confirm_selected_character,
                    g_swap_journal.player_refresh_provider,
                    target_character,
                    g_game_thread.load(std::memory_order_relaxed),
                    &identity_assignment_started);
      if (!selected_updated) {
        g_swap_journal.selected_character_updated =
            identity_assignment_started;
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      }
      g_swap_journal.selected_character_updated = true;
      const bool published =
          g_swap_journal.target_pri
              ? PublishPriIdentityPair(
                    g_swap_journal.player_pri,
                    g_swap_journal.target_pawn,
                    g_swap_journal.target_pri,
                    g_swap_journal.player_pawn,
                    g_swap_journal.game_replication_info,
                    g_swap_journal.player_refresh_provider)
              : PublishPlayerIdentity(
                    g_swap_journal.player_pri,
                    g_swap_journal.target_pawn,
                    g_swap_journal.game_replication_info,
                    g_swap_journal.player_refresh_provider);
      if (!published)
        return CharacterSwapApplyResult::kFailedNeedsRollback;
      return CharacterSwapApplyResult::kApplied;
    }
    case CharacterSwapMutation::kRebindRuntimeServices: {
      bool ok = RebindPlayerServices(g_swap_journal.target_pawn,
                                     g_swap_journal.hud,
                                     g_swap_journal.hud);
      if (g_swap_journal.former_player_requires_proxy) {
        // Vehicle AI strategies and GOAP actions contain direct TnAiPawn-only
        // field accesses. Keep the planner suspended, detach its old vehicle
        // strategy, and rebind only the audited TnPawn-safe sensors.
        void* current_vehicle_control = nullptr;
        const bool vehicle_read = ReadOptionalObject(
            g_swap_journal.target_controller, L"_VehicleAiControl",
            &current_vehicle_control);
        bool vehicle_detached = vehicle_read;
        if (vehicle_read && current_vehicle_control) {
          g_swap_journal.target_vehicle_ai_control_detached = true;
          vehicle_detached = UnrealWriteObjectPropertyExpected(
              g_swap_journal.target_controller, L"_VehicleAiControl",
              current_vehicle_control, nullptr);
        }
        g_swap_journal.replacement_target_vehicle_ai_control = nullptr;
        ok = vehicle_detached && RebindTargetSensors(
                                     g_swap_journal.player_pawn) && ok;
      } else {
        const bool vehicle_control_rebuilt = UnrealCallExactVoid(
            g_swap_journal.target_controller,
            g_swap_journal.ai_create_vehicle_control);
        g_swap_journal.target_vehicle_ai_control_rebuilt =
            vehicle_control_rebuilt;
        void* replacement_vehicle_control = nullptr;
        const bool vehicle_control_valid =
            vehicle_control_rebuilt &&
            ReadOptionalObject(g_swap_journal.target_controller,
                               L"_VehicleAiControl",
                               &replacement_vehicle_control) &&
            (!g_swap_journal.previous_target_vehicle_ai_control ||
             replacement_vehicle_control != nullptr);
        g_swap_journal.replacement_target_vehicle_ai_control =
            replacement_vehicle_control;
        ok = vehicle_control_valid &&
             RebindTargetAiLifecycle(g_swap_journal.player_pawn) && ok;
        if (g_swap_journal.previous_target_dialog_owner !=
            g_swap_journal.player_pawn) {
          g_swap_journal.target_dialog_rebound = true;
          ok = UnrealCallVoidObject(g_swap_journal.target_dialog_manager,
                                    L"InitializeWithAiPawn", L"_OwnerPawn",
                                    g_swap_journal.player_pawn) && ok;
        }
      }
      ok = ApplyInventoryWeaponRoles() && ok;
      g_swap_journal.target_shooter_rebound = true;
      ok = ActivateTargetWeaponShooter(g_swap_journal.player_pawn) && ok;
      g_swap_journal.target_collision_role_changed = true;
      ok = UnrealCallVoidByteBool(g_swap_journal.target_collision_controller,
                                  L"SetRBCollidesWithChannel", L"Channel",
                                  15u, L"Value", true) && ok;
      g_swap_journal.player_collision_role_changed = true;
      ok = UnrealCallVoidByteBool(g_swap_journal.player_collision_controller,
                                  L"SetRBCollidesWithChannel", L"Channel",
                                  15u, L"Value", false) && ok;
      ok = UnrealCallVoidObject(g_swap_journal.player_ability_manager,
                                L"SetHud", L"NewHud", nullptr) && ok;
      ok = UnrealCallVoidObject(g_swap_journal.player_buff_manager,
                                L"SetHud", L"NewHud", nullptr) && ok;
      g_swap_journal.services_rebound = true;
      return ok ? CharacterSwapApplyResult::kApplied
                : CharacterSwapApplyResult::kFailedNeedsRollback;
    }
    case CharacterSwapMutation::kUpdateCameraAndViewTarget: {
      // Arm rollback before the first native owner mutation. Either call may
      // succeed even if a later postcondition fails.
      g_swap_journal.camera_rebound = true;
      bool ok = UnrealCallVoidObject(g_swap_journal.camera_manager,
                                     L"set_CameraOwner", L"Value",
                                     g_swap_journal.target_pawn);
      ok = UnrealCallVoidObject(g_swap_journal.player_controller,
                                L"SetViewTarget", L"NewViewTarget",
                                g_swap_journal.target_pawn) && ok;
      const uint8_t form = g_swap_journal.target_mode ==
                                   TransformerMode::kVehicle
                               ? 1u
                               : 0u;
      ok = UnrealCallVoidByte(g_swap_journal.player_controller,
                              L"GotoNewFormState", L"TargetForm", form) && ok;
      return ok ? CharacterSwapApplyResult::kApplied
                : CharacterSwapApplyResult::kFailedNeedsRollback;
    }
    case CharacterSwapMutation::kArmControlTransfer: {
      if (!RoleMappingMatches(g_swap_journal.desired_mapping))
        return CharacterSwapApplyResult::kFailedNoMutation;
      g_pawn = g_swap_journal.target_pawn;
      g_squad_controllers[0] = g_swap_journal.player_controller;
      g_squad_pawns[0] = g_swap_journal.target_pawn;
      g_squad_weapons[0] = CurrentPawnWeapon(g_swap_journal.target_pawn);
      g_squad_record_indices[0] = -1;
      g_squad_controllers[request.squad_slot] =
          g_swap_journal.target_controller;
      g_squad_pawns[request.squad_slot] = g_swap_journal.player_pawn;
      g_squad_weapons[request.squad_slot] =
          CurrentPawnWeapon(g_swap_journal.player_pawn);
      g_swap_journal.armed = true;
      g_snapshot_build = {};
      return CharacterSwapApplyResult::kApplied;
    }
    case CharacterSwapMutation::kReleaseTransitionGuards: {
      bool ok = true;
      if (g_swap_journal.target_ai_suspended &&
          !g_swap_journal.former_player_requires_proxy) {
        const bool resumed = UnrealCallVoid(g_swap_journal.target_controller,
                                            L"ResumePlanRunner");
        g_swap_journal.target_ai_suspended = !resumed;
        ok = resumed && ok;
        if (resumed)
          ok = UnrealCallVoidByte(g_swap_journal.target_controller,
                                  L"RequestReplan", L"Priority", 1u) && ok;
      }
      if (g_swap_journal.player_look_ignored) {
        const bool released = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreLookInput",
            L"bNewLookInput", false);
        g_swap_journal.player_look_ignored = !released;
        ok = released && ok;
      }
      if (g_swap_journal.player_move_ignored) {
        const bool released = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreMoveInput",
            L"bNewMoveInput", false);
        g_swap_journal.player_move_ignored = !released;
        ok = released && ok;
      }
      return ok ? CharacterSwapApplyResult::kApplied
                : CharacterSwapApplyResult::kFailedNeedsRollback;
    }
    case CharacterSwapMutation::kCount:
      break;
  }
  return CharacterSwapApplyResult::kFailedNoMutation;
}

bool WfcSwapOperations::RevertMutation(
    CharacterSwapMutation mutation, const CharacterSwapRequest& request,
    CharacterSwapCapture* capture) noexcept {
  (void)capture;
  if (request.request_id != g_swap_journal.request_id) return false;
  bool ok = true;
  switch (mutation) {
    case CharacterSwapMutation::kReleaseTransitionGuards:
      if (!g_swap_journal.player_move_ignored) {
        const bool ignored = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreMoveInput",
            L"bNewMoveInput", true);
        g_swap_journal.player_move_ignored = ignored;
        ok = ignored && ok;
      }
      if (!g_swap_journal.player_look_ignored) {
        const bool ignored = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreLookInput",
            L"bNewLookInput", true);
        g_swap_journal.player_look_ignored = ignored;
        ok = ignored && ok;
      }
      if (!g_swap_journal.target_ai_suspended) {
        const bool suspended = UnrealCallVoid(
            g_swap_journal.target_controller, L"SuspendPlanRunner");
        g_swap_journal.target_ai_suspended = suspended;
        ok = suspended && ok;
      }
      return ok;
    case CharacterSwapMutation::kArmControlTransfer:
      g_pawn = g_swap_journal.player_pawn;
      g_squad_controllers[0] = g_swap_journal.player_controller;
      g_squad_pawns[0] = g_swap_journal.player_pawn;
      g_squad_weapons[0] = CurrentPawnWeapon(g_swap_journal.player_pawn);
      g_squad_record_indices[0] = -1;
      g_squad_controllers[request.squad_slot] =
          g_swap_journal.target_controller;
      g_squad_pawns[request.squad_slot] = g_swap_journal.target_pawn;
      g_squad_weapons[request.squad_slot] =
          CurrentPawnWeapon(g_swap_journal.target_pawn);
      g_swap_journal.armed = false;
      // The desired mapping must remain active until the later bind reversals
      // have detached the cross-role graph. RestoreOriginalGraph performs the
      // generation-checked mapping restore at the safe point.
      return true;
    case CharacterSwapMutation::kUpdateCameraAndViewTarget:
      g_swap_journal.camera_restore_pending = true;
      return true;
    case CharacterSwapMutation::kRebindRuntimeServices:
      g_swap_journal.runtime_services_restore_pending = true;
      return true;
    case CharacterSwapMutation::kUpdateCharacterIdentity:
      g_swap_journal.identity_restore_pending = true;
      return true;
    case CharacterSwapMutation::kUpdateSquadMembership:
      g_swap_journal.squad_restore_pending = true;
      return true;
    case CharacterSwapMutation::kBindAiControllerToFormerPlayer:
      if (ControllerPawn(g_swap_journal.target_controller) ==
          g_swap_journal.player_pawn)
        ok = UnrealCallExactVoid(g_swap_journal.target_controller,
                                 g_swap_journal.ai_unpossess);
      g_swap_journal.target_bound = false;
      return ok && ControllerPawn(g_swap_journal.target_controller) == nullptr;
    case CharacterSwapMutation::kBindPlayerControllerToTarget:
      if (ControllerPawn(g_swap_journal.player_controller) ==
          g_swap_journal.target_pawn)
        ok = UnrealCallExactVoid(g_swap_journal.player_controller,
                                 g_swap_journal.player_base_unpossess);
      g_swap_journal.player_bound = false;
      return ok && ControllerPawn(g_swap_journal.player_controller) == nullptr;
    case CharacterSwapMutation::kDetachOriginalControlLinks:
      return RestoreOriginalGraph() && RestoreDeferredRuntimeState();
    case CharacterSwapMutation::kPrepareRoleAdapters:
      if (g_swap_journal.graph_restore_failed) return false;
      if (!RoleMappingMatches(g_swap_journal.previous_mapping)) return false;
      if (g_swap_journal.target_shooter_quiesced) {
        bool restored = true;
        if (g_swap_journal.previous_target_shooter_weapon) {
          restored =
              CurrentPawnWeapon(g_swap_journal.target_pawn) ==
                  g_swap_journal.previous_target_shooter_weapon &&
              ActivateTargetWeaponShooter(g_swap_journal.target_pawn);
        } else {
          void* active_weapon = reinterpret_cast<void*>(1);
          restored = UnrealCallObject(g_swap_journal.target_weapon_shooter,
                                      L"get_ActiveWeapon",
                                      &active_weapon) &&
                     active_weapon == nullptr;
        }
        if (restored) g_swap_journal.target_shooter_quiesced = false;
        ok = restored && ok;
      }
      if (g_swap_journal.role_adapter_installed_here) {
        const bool stopped = ShutdownCharacterRoleAdapter();
        if (stopped) g_swap_journal.role_adapter_installed_here = false;
        return stopped;
      }
      return true;
    case CharacterSwapMutation::kQuiesceTargetAi:
      if (g_swap_journal.graph_restore_failed) return false;
      if (g_swap_journal.target_ai_suspended &&
          !g_swap_journal.target_plan_was_suspended) {
        ok = UnrealCallVoid(g_swap_journal.target_controller,
                            L"ResumePlanRunner");
        if (ok) g_swap_journal.target_ai_suspended = false;
      }
      return ok;
    case CharacterSwapMutation::kQuiescePlayerInput:
      if (g_swap_journal.graph_restore_failed) return false;
      if (g_swap_journal.player_look_ignored) {
        const bool released = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreLookInput",
            L"bNewLookInput", false);
        if (released) g_swap_journal.player_look_ignored = false;
        ok = released && ok;
      }
      if (g_swap_journal.player_move_ignored) {
        const bool released = UnrealCallVoidBool(
            g_swap_journal.player_controller, L"IgnoreMoveInput",
            L"bNewMoveInput", false);
        if (released) g_swap_journal.player_move_ignored = false;
        ok = released && ok;
      }
      return ok;
    case CharacterSwapMutation::kCount:
      return false;
  }
  return false;
}

bool SquadRecordNameMatchesPawn(void* pawn,
                                const UnrealStructArrayView& records,
                                int32_t record_index) {
  UnrealStructValueView character;
  char expected[128] = {};
  char actual[128] = {};
  return UnrealReadStructProperty(pawn, L"_CurrentCharacter", &character) &&
         UnrealReadStructStringField(character, L"CharacterName", expected,
                                     sizeof(expected)) &&
         expected[0] &&
         UnrealReadStructArrayStringField(records, record_index,
                                           L"CharacterName", actual,
                                           sizeof(actual)) &&
         actual[0] && Token(expected) == Token(actual);
}

bool ServiceOwnerIs(void* service, const wchar_t* property, void* expected) {
  void* actual = nullptr;
  return UnrealReadObjectProperty(service, property, &actual) &&
         actual == expected;
}

bool CollisionRoleIs(void* collision_controller, bool expected_player) {
  void* robot_mesh = nullptr;
  void* vehicle_mesh = nullptr;
  bool robot_player_channel = false;
  bool vehicle_player_channel = false;
  if (!ReadRequiredObject(collision_controller, L"_RobotMeshComponent",
                          &robot_mesh) ||
      !ReadOptionalObject(collision_controller, L"_VehicleMeshComponent",
                          &vehicle_mesh) ||
      !UnrealReadStructBoolProperty(robot_mesh, L"RBCollideWithChannels",
                                    L"PlayerOnlyBlockingVolume",
                                    &robot_player_channel) ||
      robot_player_channel != expected_player)
    return false;
  return !vehicle_mesh ||
         (UnrealReadStructBoolProperty(vehicle_mesh,
                                       L"RBCollideWithChannels",
                                       L"PlayerOnlyBlockingVolume",
                                       &vehicle_player_channel) &&
          vehicle_player_channel == expected_player);
}

bool InventoryWeaponRolesMatch(
    const std::array<WfcSwapJournal::InventoryWeaponRole, 32>& entries,
    uint32_t count, uint8_t role) {
  for (uint32_t index = 0; index < count; ++index) {
    const auto& entry = entries[index];
    uint8_t current_type = 0;
    void* current_data = nullptr;
    void* current_heat_object = nullptr;
    float current_heat = 0.0f;
    if (!entry.weapon || !UnrealObjectValid(entry.weapon) ||
        PendingDestroy(entry.weapon) ||
        UnrealObjectIndex(entry.weapon) != entry.object_index ||
        !UnrealReadByteProperty(entry.weapon, L"WeaponDataType",
                                &current_type) ||
        !UnrealReadObjectProperty(entry.weapon, L"Data", &current_data) ||
        current_type != role || current_data != entry.desired_data)
      return false;
    if (entry.heat_state_captured &&
        (!ReadRequiredObject(entry.weapon, L"WeaponHeat",
                             &current_heat_object) ||
         current_heat_object != entry.weapon_heat ||
         UnrealObjectIndex(current_heat_object) != entry.weapon_heat_index ||
         !UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                  &current_heat) ||
         std::fabs(current_heat - entry.current_heat) > 0.001f))
      return false;
  }
  return true;
}

bool InventoryWeaponPreviousRolesMatch(
    const std::array<WfcSwapJournal::InventoryWeaponRole, 32>& entries,
    uint32_t count) {
  for (uint32_t index = 0; index < count; ++index) {
    const auto& entry = entries[index];
    uint8_t current_type = 0;
    void* current_data = nullptr;
    void* current_heat_object = nullptr;
    float current_heat = 0.0f;
    if (entry.changed || !entry.weapon || !UnrealObjectValid(entry.weapon) ||
        PendingDestroy(entry.weapon) ||
        UnrealObjectIndex(entry.weapon) != entry.object_index ||
        !UnrealReadByteProperty(entry.weapon, L"WeaponDataType",
                                &current_type) ||
        !UnrealReadObjectProperty(entry.weapon, L"Data", &current_data) ||
        current_type != entry.previous_data_type ||
        current_data != entry.previous_data)
      return false;
    if (entry.heat_state_captured &&
        (!ReadRequiredObject(entry.weapon, L"WeaponHeat",
                             &current_heat_object) ||
         current_heat_object != entry.weapon_heat ||
         UnrealObjectIndex(current_heat_object) != entry.weapon_heat_index ||
         !UnrealReadFloatProperty(entry.weapon_heat, L"CurrentHeat",
                                  &current_heat) ||
         std::fabs(current_heat - entry.current_heat) > 0.001f))
      return false;
  }
  return true;
}

bool WfcSwapOperations::VerifyCommittedGraph(
    const CharacterSwapRequest& request,
    const CharacterSwapCapture& capture) noexcept {
  if (request.request_id != g_swap_journal.request_id ||
      capture.world_generation != g_world_epoch ||
      !UnrealObjectValid(g_swap_journal.world) ||
      !UnrealObjectValid(g_swap_journal.player_controller) ||
      !UnrealObjectValid(g_swap_journal.player_pawn) ||
      !UnrealObjectValid(g_swap_journal.target_controller) ||
      !UnrealObjectValid(g_swap_journal.target_pawn) ||
      g_swap_journal.world != g_world_info ||
      g_swap_journal.player_controller != g_controller ||
      g_swap_journal.target_pawn != g_pawn ||
      !SupportedPlayerPair(g_swap_journal.player_controller,
                           g_swap_journal.target_pawn) ||
      !SupportedAiPair(g_swap_journal.target_controller,
                       g_swap_journal.player_pawn) ||
      !RoleMappingMatches(g_swap_journal.desired_mapping) ||
      g_squad_controllers[request.squad_slot] !=
          g_swap_journal.target_controller ||
      g_squad_pawns[request.squad_slot] != g_swap_journal.player_pawn ||
      g_squad_record_indices[request.squad_slot] !=
          g_swap_journal.squad_record_index ||
      !(g_swap_journal.former_player_requires_proxy
            ? (g_swap_journal.target_sensor_rebound &&
               !g_swap_journal.target_ai_lifecycle_rebound &&
               TargetProxyLifecycleCoherent(g_swap_journal.player_pawn))
            : (g_swap_journal.target_ai_lifecycle_rebound &&
               TargetAiLifecycleCoherent(g_swap_journal.player_pawn))) ||
      !PriIdentityMatchesPawn(g_swap_journal.player_pri,
                              g_swap_journal.target_pawn) ||
      (g_swap_journal.target_pri &&
       !PriIdentityMatchesPawn(g_swap_journal.target_pri,
                               g_swap_journal.player_pawn)) ||
      g_pending_ability_release.pending)
    return false;

  void* record_controller = nullptr;
  void* view_target = nullptr;
  void* vehicle_ai_control = nullptr;
  void* shooter_weapon = nullptr;
  uint32_t player_hash = 0;
  uint32_t ai_hash = 0;
  if (!UnrealReadStructArrayObjectField(
          g_swap_journal.squad_records, g_swap_journal.squad_record_index,
          L"CharacterController", &record_controller) ||
      record_controller != g_swap_journal.target_controller ||
      !SquadRecordNameMatchesPawn(g_swap_journal.player_pawn,
                                  g_swap_journal.squad_records,
                                  g_swap_journal.squad_record_index) ||
      !ResolveLiveCharacterHash(g_swap_journal.player_controller,
                                g_swap_journal.target_pawn, &player_hash) ||
      player_hash != request.expected_target_identity_hash ||
      !ResolveLiveCharacterHash(g_swap_journal.target_controller,
                                g_swap_journal.player_pawn, &ai_hash) ||
      ai_hash != request.expected_player_identity_hash ||
      !UnrealReadObjectProperty(g_swap_journal.player_controller,
                                L"ViewTarget", &view_target) ||
      view_target != g_swap_journal.target_pawn ||
      !ServiceOwnerIs(g_swap_journal.pickup_manager, L"OwnerPawn",
                      g_swap_journal.target_pawn) ||
      !ServiceOwnerIs(g_swap_journal.fine_aim_manager, L"Owner",
                       g_swap_journal.target_pawn) ||
      !ServiceOwnerIs(g_swap_journal.player_targeting, L"_TargetingOwner",
                      g_swap_journal.target_pawn) ||
      !ServiceOwnerIs(g_swap_journal.camera_manager, L"_TransformerPawn",
                       g_swap_journal.target_pawn) ||
      !ServiceOwnerIs(g_swap_journal.dialog_manager, L"OwnerPawn",
                       g_swap_journal.target_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_dialog_manager, L"OwnerPawn",
                       g_swap_journal.former_player_requires_proxy
                           ? g_swap_journal.previous_target_dialog_owner
                           : g_swap_journal.player_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_dialog_manager, L"OwnerAiPawn",
                       g_swap_journal.former_player_requires_proxy
                           ? g_swap_journal.previous_target_dialog_ai_owner
                           : g_swap_journal.player_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_weapon_shooter, L"Owner",
                       g_swap_journal.player_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_weapon_aimer, L"_Owner",
                       g_swap_journal.player_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_weapon_aimer,
                       L"_OwnerController",
                       g_swap_journal.target_controller) ||
      !UnrealCallObject(g_swap_journal.target_weapon_shooter,
                        L"get_ActiveWeapon", &shooter_weapon) ||
      shooter_weapon != CurrentPawnWeapon(g_swap_journal.player_pawn) ||
      !ServiceOwnerIs(g_swap_journal.target_ability_manager, L"OwnerHud",
                      g_swap_journal.hud) ||
      !ServiceOwnerIs(g_swap_journal.target_buff_manager, L"OwnerHud",
                      g_swap_journal.hud) ||
      !ServiceOwnerIs(g_swap_journal.player_ability_manager, L"OwnerHud",
                      nullptr) ||
      !ServiceOwnerIs(g_swap_journal.player_buff_manager, L"OwnerHud",
                      nullptr) ||
      !ReadOptionalObject(g_swap_journal.target_controller,
                          L"_VehicleAiControl", &vehicle_ai_control) ||
      vehicle_ai_control !=
          g_swap_journal.replacement_target_vehicle_ai_control ||
      !CollisionRoleIs(g_swap_journal.target_collision_controller, true) ||
      !CollisionRoleIs(g_swap_journal.player_collision_controller, false) ||
      !InventoryWeaponRolesMatch(
          g_swap_journal.target_inventory_weapons,
          g_swap_journal.target_inventory_weapon_count, 1u) ||
      !InventoryWeaponRolesMatch(
          g_swap_journal.player_inventory_weapons,
          g_swap_journal.player_inventory_weapon_count, 2u))
    return false;

  return UnrealObjectIndex(g_swap_journal.world) ==
             capture.world.object_index &&
         UnrealObjectIndex(g_swap_journal.player_controller) ==
             capture.player_controller.object_index &&
         UnrealObjectIndex(g_swap_journal.player_pawn) ==
             capture.player_pawn.object_index &&
         UnrealObjectIndex(g_swap_journal.target_controller) ==
             capture.target_ai_controller.object_index &&
         UnrealObjectIndex(g_swap_journal.target_pawn) ==
             capture.target_ai_pawn.object_index;
}

bool WfcSwapOperations::VerifyRestoredGraph(
    const CharacterSwapRequest& request,
    const CharacterSwapCapture& capture) noexcept {
  if (request.request_id != g_swap_journal.request_id ||
      capture.world_generation != g_world_epoch ||
      g_controller != g_swap_journal.player_controller ||
      g_pawn != g_swap_journal.player_pawn ||
      !SupportedPlayerPair(g_swap_journal.player_controller,
                           g_swap_journal.player_pawn) ||
      !SupportedAiPair(g_swap_journal.target_controller,
                       g_swap_journal.target_pawn) ||
      !RoleMappingMatches(g_swap_journal.previous_mapping) ||
      g_squad_controllers[request.squad_slot] !=
          g_swap_journal.target_controller ||
      g_squad_pawns[request.squad_slot] != g_swap_journal.target_pawn ||
      g_swap_journal.target_ai_lifecycle_rebind_started ||
      g_swap_journal.target_ai_lifecycle_rebound ||
      g_swap_journal.target_sensor_rebind_started ||
      g_swap_journal.target_sensor_rebound ||
      g_swap_journal.runtime_services_restore_pending ||
      g_swap_journal.identity_restore_pending ||
      g_swap_journal.squad_restore_pending ||
      g_swap_journal.camera_restore_pending ||
      !(g_swap_journal.target_lifecycle_was_proxy
            ? TargetProxyLifecycleCoherent(g_swap_journal.target_pawn)
            : TargetAiLifecycleCoherent(g_swap_journal.target_pawn)) ||
      !PriIdentityMatchesPawn(g_swap_journal.player_pri,
                              g_swap_journal.player_pawn) ||
      (g_swap_journal.target_pri &&
       !PriIdentityMatchesPawn(g_swap_journal.target_pri,
                               g_swap_journal.target_pawn)))
    return false;
  uint32_t player_hash = 0;
  uint32_t target_hash = 0;
  void* view_target = nullptr;
  void* vehicle_ai_control = nullptr;
  void* shooter_weapon = nullptr;
  const bool vehicle_strategy_restored =
      (!g_swap_journal.target_vehicle_ai_control_rebuilt &&
       !g_swap_journal.target_vehicle_ai_control_detached) ||
      (ReadOptionalObject(g_swap_journal.target_controller,
                          L"_VehicleAiControl", &vehicle_ai_control) &&
       vehicle_ai_control ==
           g_swap_journal.restored_target_vehicle_ai_control);
  return vehicle_strategy_restored &&
         ResolveLiveCharacterHash(g_swap_journal.player_controller,
                                  g_swap_journal.player_pawn, &player_hash) &&
         player_hash == request.expected_player_identity_hash &&
         ResolveLiveCharacterHash(g_swap_journal.target_controller,
                                  g_swap_journal.target_pawn, &target_hash) &&
         target_hash == request.expected_target_identity_hash &&
         SquadRecordNameMatchesPawn(g_swap_journal.target_pawn,
                                    g_swap_journal.squad_records,
                                    g_swap_journal.squad_record_index) &&
         UnrealReadObjectProperty(g_swap_journal.player_controller,
                                  L"ViewTarget", &view_target) &&
         view_target == g_swap_journal.previous_view_target &&
         ServiceOwnerIs(g_swap_journal.pickup_manager, L"OwnerPawn",
                        g_swap_journal.player_pawn) &&
         ServiceOwnerIs(g_swap_journal.fine_aim_manager, L"Owner",
                        g_swap_journal.player_pawn) &&
         ServiceOwnerIs(g_swap_journal.player_targeting, L"_TargetingOwner",
                        g_swap_journal.previous_targeting_owner) &&
         ServiceOwnerIs(g_swap_journal.camera_manager, L"_TransformerPawn",
                        g_swap_journal.previous_camera_owner) &&
         ServiceOwnerIs(g_swap_journal.dialog_manager, L"OwnerPawn",
                        g_swap_journal.player_pawn) &&
         ServiceOwnerIs(g_swap_journal.target_dialog_manager, L"OwnerPawn",
                        g_swap_journal.previous_target_dialog_owner) &&
         ServiceOwnerIs(g_swap_journal.target_dialog_manager, L"OwnerAiPawn",
                        g_swap_journal.previous_target_dialog_ai_owner) &&
         ServiceOwnerIs(g_swap_journal.target_weapon_shooter, L"Owner",
                        g_swap_journal.target_pawn) &&
         ServiceOwnerIs(g_swap_journal.target_weapon_aimer, L"_Owner",
                        g_swap_journal.target_pawn) &&
         ServiceOwnerIs(g_swap_journal.target_weapon_aimer,
                        L"_OwnerController",
                        g_swap_journal.target_controller) &&
         UnrealCallObject(g_swap_journal.target_weapon_shooter,
                          L"get_ActiveWeapon", &shooter_weapon) &&
         shooter_weapon == g_swap_journal.previous_target_shooter_weapon &&
         CollisionRoleIs(g_swap_journal.player_collision_controller, true) &&
         CollisionRoleIs(g_swap_journal.target_collision_controller, false) &&
         InventoryWeaponPreviousRolesMatch(
             g_swap_journal.player_inventory_weapons,
             g_swap_journal.player_inventory_weapon_count) &&
         InventoryWeaponPreviousRolesMatch(
             g_swap_journal.target_inventory_weapons,
             g_swap_journal.target_inventory_weapon_count) &&
         ServiceOwnerIs(g_swap_journal.player_ability_manager, L"OwnerHud",
                        g_swap_journal.player_ability_hud) &&
         ServiceOwnerIs(g_swap_journal.player_buff_manager, L"OwnerHud",
                        g_swap_journal.player_buff_hud) &&
         ServiceOwnerIs(g_swap_journal.target_ability_manager, L"OwnerHud",
                        g_swap_journal.target_ability_hud) &&
         ServiceOwnerIs(g_swap_journal.target_buff_manager, L"OwnerHud",
                        g_swap_journal.target_buff_hud);
}

void WfcSwapOperations::EnterFailedClosed(
    const CharacterSwapRequest& request, const CharacterSwapCapture& capture,
    CharacterSwapFailure cause, bool rollback_complete) noexcept {
  (void)capture;
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_switch_in_progress = true;
  g_snapshot_build = {};
  Log(LogLevel::kError,
      "[SquadSwitch] request=%llu failed cause=%u rollback=%s",
      static_cast<unsigned long long>(request.request_id),
      static_cast<unsigned>(cause), rollback_complete ? "complete" : "dirty");
}

bool WfcSwapOperations::ExitFailedClosed(
    const CharacterSwapRequest& request,
    const CharacterSwapCapture& capture) noexcept {
  // CapturePreflight can reject a stale/restricted UI request before it
  // retains any graph identity. No mutation has run in that case, so there is
  // no journaled graph to verify or restore. Treat it as a clean cancellation
  // and reopen the gate instead of permanently failing closed.
  if (capture.adapter_cookie == 0) {
    g_switch_in_progress = false;
    g_switch_snapshot_pending = false;
    g_role_proxy_consecutive_failures = 0;
    g_role_proxy_consecutive_successes = 0;
    g_swap_recovery_retry_at = 0;
    g_snapshot_build = {};
    g_last_publish = 0;
    if (!g_role_proxy_failed_closed && !g_role_proxy_death_pending &&
        !g_world_command_gate_pending && !DeferredCheckpointFailedClosed())
      SetCommandAcceptance(true);
    g_swap_journal = {};
    return true;
  }
  if (!VerifyRestoredGraph(request, capture)) return false;
  g_switch_in_progress = false;
  g_switch_snapshot_pending = false;
  g_role_proxy_consecutive_failures = 0;
  g_role_proxy_consecutive_successes = 0;
  g_swap_recovery_retry_at = 0;
  g_snapshot_build = {};
  g_last_publish = 0;
  g_resolve_retry_at = GetTickCount64() + kStableResolveIntervalMs;
  if (!g_role_proxy_failed_closed && !g_role_proxy_death_pending &&
      !g_world_command_gate_pending && !DeferredCheckpointFailedClosed())
    SetCommandAcceptance(true);
  g_swap_journal = {};
  return true;
}

void WfcSwapOperations::OnSucceeded(
    const CharacterSwapRequest& request,
    const CharacterSwapCapture& capture) noexcept {
  (void)capture;
  // The desired mapping is now persistent. Discarding the rollback token is
  // deliberate; a later swap captures this mapping as its own prior state.
  g_swap_journal.mapping_restore = {};
  g_pending_ability_release = {};
  ResetCharacterRoleProxy();
  g_role_proxy_consecutive_failures = 0;
  g_role_proxy_consecutive_successes = 0;
  g_swap_recovery_retry_at = 0;
  g_snapshot_build = {};
  ClearWeaponTelemetryCache();
  ++g_world_epoch;
  UnrealResetWorldCaches();
  RejectStaleCommands(g_world_epoch);
  Snapshot previous;
  g_switch_snapshot_sequence =
      CopyLatestSnapshot(&previous) ? previous.sequence : 0;
  g_switch_snapshot_pending = true;
  g_last_publish = 0;
  g_resolve_retry_at = GetTickCount64() + kStableResolveIntervalMs;
  Log(LogLevel::kInfo,
      "[SquadSwitch] request=%llu committed pawn=%lu former=%lu epoch=%llu; awaiting synchronized UI snapshot",
      static_cast<unsigned long long>(request.request_id),
      static_cast<unsigned long>(UnrealObjectIndex(g_swap_journal.target_pawn)),
      static_cast<unsigned long>(UnrealObjectIndex(g_swap_journal.player_pawn)),
      static_cast<unsigned long long>(g_world_epoch));
}

bool BeginDeferredCheckpointRestore(ULONGLONG now) {
  if (g_checkpoint_restore.active ||
      g_swap_transaction.command_gate_closed())
    return false;
  DeferredCheckpointRequest checkpoint;
  if (!ConsumeDeferredCheckpointNativeRestore(&checkpoint)) return false;

  CharacterRoleMapping mapping;
  uint32_t target_slot = kSquadCapacity;
  uint32_t player_hash = 0;
  uint32_t target_hash = 0;
  if (!GetCharacterRoleMapping(&mapping) ||
      !CharacterRoleMappingObjectsValid() ||
      mapping.player_controller != g_controller ||
      mapping.player_controlled_ai_pawn != g_pawn ||
      !UnrealObjectValid(reinterpret_cast<void*>(checkpoint.manager)) ||
      UnrealObjectIndex(reinterpret_cast<void*>(checkpoint.manager)) !=
          checkpoint.manager_object_index) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    DiscardAllCommands();
    return false;
  }
  for (uint32_t slot = 1; slot < kSquadCapacity; ++slot) {
    if (g_squad_controllers[slot] == mapping.mapped_ai_controller &&
        g_squad_pawns[slot] == mapping.ai_controlled_player_pawn) {
      target_slot = slot;
      break;
    }
  }
  if (target_slot >= kSquadCapacity ||
      g_squad_record_indices[target_slot] < 0 ||
      !ResolveLiveCharacterHash(g_controller, g_pawn, &player_hash) ||
      !ResolveLiveCharacterHash(mapping.mapped_ai_controller,
                                mapping.ai_controlled_player_pawn,
                                &target_hash)) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    DiscardAllCommands();
    return false;
  }

  CharacterSwapRequest request;
  request.request_id = 0xC000000000000000ull | checkpoint.generation;
  request.requested_at_millis = now;
  request.world_generation = g_world_epoch;
  request.expected_world_object_index = UnrealObjectIndex(g_world_info);
  request.expected_player_controller_object_index =
      UnrealObjectIndex(g_controller);
  request.expected_player_pawn_object_index = UnrealObjectIndex(g_pawn);
  request.expected_target_controller_object_index =
      UnrealObjectIndex(mapping.mapped_ai_controller);
  request.expected_target_pawn_object_index =
      UnrealObjectIndex(mapping.ai_controlled_player_pawn);
  request.expected_player_identity_hash = player_hash;
  request.expected_target_identity_hash = target_hash;
  request.squad_slot = target_slot;
  request.checkpoint_native_restore = true;

  CheckpointRestoreJournal journal;
  journal.checkpoint = checkpoint;
  journal.world = g_world_info;
  journal.controller = g_controller;
  journal.physical_player_pawn = mapping.ai_controlled_player_pawn;
  journal.world_object_index = UnrealObjectIndex(g_world_info);
  journal.controller_object_index = UnrealObjectIndex(g_controller);
  journal.physical_player_pawn_object_index =
      UnrealObjectIndex(mapping.ai_controlled_player_pawn);
  journal.swap_request_id = request.request_id;
  journal.active = true;
  if (!journal.world_object_index || !journal.controller_object_index ||
      !journal.physical_player_pawn_object_index ||
      g_swap_transaction.Submit(request, now) !=
          CharacterSwapSubmitResult::kAccepted) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    DiscardAllCommands();
    return false;
  }

  g_checkpoint_restore = journal;
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_pending_ability_release = {};
  g_snapshot_build = {};
  g_switch_in_progress = true;
  g_switch_snapshot_pending = false;
  g_swap_recovery_retry_at = 0;
  Log(LogLevel::kInfo,
      "[Checkpoint] restoring physical player pawn before save request=%llu slot=%lu",
      static_cast<unsigned long long>(request.request_id),
      static_cast<unsigned long>(target_slot));
  return true;
}

bool CompleteDeferredCheckpointRestore() {
  if (!g_checkpoint_restore.active) return false;
  const CharacterSwapRequest& request = g_swap_transaction.request();
  if (!request.checkpoint_native_restore ||
      request.request_id != g_checkpoint_restore.swap_request_id ||
      CharacterRoleMappingActive() ||
      g_world_info != g_checkpoint_restore.world ||
      g_controller != g_checkpoint_restore.controller ||
      g_pawn != g_checkpoint_restore.physical_player_pawn ||
      UnrealObjectIndex(g_world_info) !=
          g_checkpoint_restore.world_object_index ||
      UnrealObjectIndex(g_controller) !=
          g_checkpoint_restore.controller_object_index ||
      UnrealObjectIndex(g_pawn) !=
          g_checkpoint_restore.physical_player_pawn_object_index ||
      !UnrealObjectIsA(g_controller, L"TnPlayerController") ||
      !UnrealObjectIsA(g_pawn, L"TnPlayerPawn") ||
      ControllerPawn(g_controller) != g_pawn ||
      PawnController(g_pawn) != g_controller) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    DiscardAllCommands();
    return false;
  }
  if (!g_swap_transaction.AcknowledgeDone()) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    return false;
  }
  g_switch_in_progress = false;
  g_switch_snapshot_pending = false;
  g_swap_journal = {};
  if (!MarkDeferredCheckpointNativeGraphVerified(true) ||
      !ReplayDeferredCheckpoint()) {
    FailDeferredCheckpointClosed();
    SetCommandAcceptance(false);
    DiscardAllCommands();
    return false;
  }
  g_checkpoint_restore = {};
  g_snapshot_build = {};
  g_last_publish = 0;
  if (!g_role_proxy_failed_closed && !g_role_proxy_death_pending &&
      !g_world_command_gate_pending) {
    SetCommandAcceptance(true);
  }
  Log(LogLevel::kInfo,
      "[Checkpoint] native player graph restored and deferred save completed");
  return true;
}

void DriveCharacterSwap(ULONGLONG now) {
  CharacterSwapStatus status = g_swap_transaction.status();
  if (g_swap_transaction.in_progress()) {
    SetSnapshotStage("squad-switch");
    status = g_swap_transaction.Tick(now);
    SetSnapshotStage("idle");
  }
  if (status.phase == CharacterSwapPhase::kDone) {
    if (g_checkpoint_restore.active &&
        g_swap_transaction.request().checkpoint_native_restore) {
      CompleteDeferredCheckpointRestore();
      return;
    }
    Snapshot synchronized;
    if (g_switch_snapshot_pending &&
        LatestSequence() > g_switch_snapshot_sequence &&
        CopyLatestSnapshot(&synchronized) &&
        synchronized.sequence > g_switch_snapshot_sequence &&
        synchronized.world_epoch == g_world_epoch &&
        synchronized.lifecycle == Lifecycle::kLive &&
        synchronized.player_pawn_object_index == UnrealObjectIndex(g_pawn)) {
      g_switch_snapshot_pending = false;
      g_switch_in_progress = false;
      g_snapshot_build = {};
      g_last_publish = 0;
      if (g_swap_transaction.AcknowledgeDone()) {
        const bool safe_to_reopen =
            !g_role_proxy_failed_closed && !g_role_proxy_death_pending &&
            !g_world_command_gate_pending &&
            !DeferredCheckpointFailedClosed() &&
            (!CharacterRoleMappingActive() ||
             CharacterRoleMappingObjectsValid());
        if (safe_to_reopen) {
          SetCommandAcceptance(true);
          Log(LogLevel::kInfo,
              "[SquadSwitch] synchronized snapshot published; input gate reopened");
        } else {
          Log(LogLevel::kWarning,
              "[SquadSwitch] synchronized snapshot published; input remains closed for lifecycle recovery");
        }
        g_swap_journal = {};
      }
    }
  } else if (status.phase == CharacterSwapPhase::kFailedClosed &&
             status.rollback_complete && now >= g_swap_recovery_retry_at) {
    if (g_checkpoint_restore.active &&
        g_swap_transaction.request().checkpoint_native_restore) {
      FailDeferredCheckpointClosed();
      SetCommandAcceptance(false);
      DiscardAllCommands();
    }
    // A failed restored-graph verification is reflection-heavy and cannot
    // become true multiple times within one frame. Retry at a bounded cadence
    // so a genuinely failed-closed transaction cannot become an FPS collapse.
    g_swap_recovery_retry_at = now + 1000;
    if (g_swap_transaction.RecoverFailedClosed())
      g_swap_recovery_retry_at = 0;
  }
}

bool SwitchSquadMember(const Command& command, const Snapshot& state) {
  const auto reject = [&command](const char* reason) {
    Log(LogLevel::kWarning,
        "[SquadSwitch] request=%llu rejected before transaction: %s slot=%lu",
        static_cast<unsigned long long>(command.request_id), reason,
        static_cast<unsigned long>(command.squad_slot));
    return false;
  };
  if (command.squad_slot == 0 || command.squad_slot >= kSquadCapacity)
    return reject("invalid squad slot");
  if (command.squad_slot >= state.squad_count)
    return reject("slot absent from synchronized snapshot");
  if (!(state.capabilities & kCapSquadSwitchAction))
    return reject("snapshot has no squad-switch capability");
  if (!state.squad[command.squad_slot].switchable)
    return reject("target is not switchable");
  if (state.squad[command.squad_slot].controlled)
    return reject("target is already controlled");
  if (command.expected_target_object_index !=
      state.squad[command.squad_slot].pawn_object_index)
    return reject("target pawn identity changed");
  if (command.expected_pawn_object_index !=
      state.squad[command.squad_slot].pawn_object_index)
    return reject("command pawn identity changed");
  if (command.expected_identity_hash !=
      state.squad[command.squad_slot].identity_hash)
    return reject("target character identity changed");
  if (g_squad_record_indices[command.squad_slot] < 0)
    return reject("authoritative squad record unavailable");

  CharacterSwapRequest request;
  request.request_id = command.request_id;
  request.requested_at_millis = command.queued_at_millis;
  request.world_generation = command.world_epoch;
  request.expected_world_object_index = state.world_object_index;
  request.expected_player_controller_object_index =
      command.expected_controller_object_index;
  request.expected_player_pawn_object_index =
      command.expected_player_pawn_object_index;
  request.expected_target_controller_object_index =
      state.squad[command.squad_slot].controller_object_index;
  request.expected_target_pawn_object_index =
      state.squad[command.squad_slot].pawn_object_index;
  request.expected_player_identity_hash = state.squad[0].identity_hash;
  request.expected_target_identity_hash = command.expected_identity_hash;
  request.squad_slot = command.squad_slot;
  const CharacterSwapSubmitResult result =
      g_swap_transaction.Submit(request, GetTickCount64());
  if (result != CharacterSwapSubmitResult::kAccepted) {
    Log(LogLevel::kWarning,
        "[SquadSwitch] request=%llu rejected submit=%u",
        static_cast<unsigned long long>(command.request_id),
        static_cast<unsigned>(result));
    return false;
  }
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_pending_ability_release = {};
  g_snapshot_build = {};
  g_switch_in_progress = true;
  g_switch_snapshot_pending = false;
  g_swap_recovery_retry_at = 0;
  g_last_accepted_action[static_cast<size_t>(CommandType::kSwitchSquadMember)] =
      GetTickCount64();
  g_last_accepted_native_action = GetTickCount64();
  Log(LogLevel::kInfo,
      "[SquadSwitch] request=%llu accepted slot=%lu target=%lu",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned long>(command.squad_slot),
      static_cast<unsigned long>(command.expected_pawn_object_index));
  return true;
}

ULONGLONG ActionMinimumInterval(CommandType type) {
  switch (type) {
    case CommandType::kTransform: return 1250;
    case CommandType::kReload: return 500;
    case CommandType::kSwapWeapon: return 600;
    case CommandType::kAbility1:
    case CommandType::kAbility2: return 400;
    case CommandType::kGrenade: return 600;
    case CommandType::kSwitchSquadMember: return 3000;
    case CommandType::kNone: return 0;
  }
  return 0;
}

bool ActionRateLimitElapsed(CommandType type, ULONGLONG now) {
  const size_t index = static_cast<size_t>(type);
  if (!index || index >= g_last_accepted_action.size()) return false;
  if (g_last_accepted_native_action &&
      now - g_last_accepted_native_action < 300)
    return false;
  const ULONGLONG last = g_last_accepted_action[index];
  return !last || now - last >= ActionMinimumInterval(type);
}

template <typename Callback>
bool InvokeAction(const Command& command, const char* stage, Callback callback) {
  SetSnapshotStage(stage);
  InterlockedExchange(&g_last_action_type,
                      static_cast<LONG>(command.type));
  InterlockedExchange(&g_last_action_request_low,
                      static_cast<LONG>(command.request_id & 0xFFFFFFFFu));
  InterlockedExchange(&g_last_action_tick,
                      static_cast<LONG>(GetTickCount()));
  // 2 = native invocation entered, 3 = returned successfully, 4 = returned
  // false. The purecall hook can read these without doing game-thread I/O.
  InterlockedExchange(&g_last_action_phase, 2);
  // Rate-limit attempts, not only callbacks that report success. Native
  // functions can reject an input edge after partially entering their state
  // machine; immediately retrying that edge is not safe.
  const ULONGLONG attempted_at = GetTickCount64();
  const size_t action_index = static_cast<size_t>(command.type);
  if (action_index < g_last_accepted_action.size())
    g_last_accepted_action[action_index] = attempted_at;
  g_last_accepted_native_action = attempted_at;
  Log(LogLevel::kInfo,
      "[Action] request=%llu type=%u invocation=begin target=%lu",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type),
      static_cast<unsigned long>(command.expected_target_object_index));
  const bool result = callback();
  InterlockedExchange(&g_last_action_phase, result ? 3 : 4);
  SetSnapshotStage("idle");
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
  const bool vitals_complete =
      ReadVitals(g_pawn, &health, &maximum, &downed, &alive);
  return vitals_complete && alive && !downed &&
         ReadRestrictions(&restrictions, true);
}

bool CurrentInventory(void** manager_output, void** active_output) {
  if (!manager_output || !active_output) return false;
  *manager_output = nullptr;
  *active_output = nullptr;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"InvManager", &manager) ||
      !manager || !UnrealObjectIsA(manager, L"TnInventoryManager") ||
      PendingDestroy(manager))
    return false;
  void* active = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"Weapon", &active)) return false;
  if (!active || !UnrealObjectIsA(active, L"TnWeapon") ||
      PendingDestroy(active))
    return false;
  *manager_output = manager;
  *active_output = active;
  return true;
}

bool CachedWeaponRootsMatchSnapshot(const Snapshot& state,
                                    WeaponRoots* roots_output) {
  if (!roots_output || !state.inventory_manager_object_index ||
      !g_cached_inventory_manager || !g_last_weapon_discovery ||
      GetTickCount64() - g_last_weapon_discovery >= kWeaponAuditIntervalMs)
    return false;
  WeaponRoots roots;
  if (!ReadWeaponRoots(g_pawn, &roots) ||
      roots.manager != g_cached_inventory_manager ||
      roots.active != g_cached_current_weapon ||
      roots.stored_vehicle != g_cached_stored_vehicle_weapon ||
      roots.stored_robot != g_cached_stored_robot_weapon ||
      roots.inventory_head != g_cached_inventory_head ||
      UnrealObjectIndex(roots.manager) !=
          state.inventory_manager_object_index ||
      UnrealObjectIndex(roots.active) != state.active_weapon_object_index)
    return false;
  *roots_output = roots;
  return true;
}

bool CurrentCachedAlternateRobotWeapon(const Snapshot& state) {
  if (!g_cached_has_alternate_weapon) return false;
  WeaponRoots roots;
  if (!CachedWeaponRootsMatchSnapshot(state, &roots) || !roots.active ||
      g_cached_display_weapon != roots.active ||
      !IsCycleableRobotWeapon(roots.active, roots.stored_vehicle))
    return false;

  bool active_seen = false;
  bool alternate_seen = false;
  for (uint32_t slot = 0; slot < g_cached_robot_weapons.size(); ++slot) {
    void* weapon = g_cached_robot_weapons[slot];
    const uint32_t expected_index = state.weapons[slot].object_index;
    if (!weapon) {
      if (expected_index) return false;
      continue;
    }
    if (!expected_index || !UnrealObjectValid(weapon) ||
        PendingDestroy(weapon) || UnrealObjectIndex(weapon) != expected_index ||
        !IsCycleableRobotWeapon(weapon, roots.stored_vehicle))
      return false;
    if (weapon == roots.active)
      active_seen = true;
    else
      alternate_seen = true;
  }
  return active_seen && alternate_seen;
}

bool CurrentAbilityReady(int32_t slot, const Command& command,
                         const Snapshot& state) {
  if (slot < 0 || slot >= 2 || !state.ability_manager_object_index)
    return false;
  void* manager = nullptr;
  if (!UnrealReadObjectProperty(g_pawn, L"AbilityManager", &manager) ||
      !manager || !UnrealObjectIsA(manager, L"TnAbilityManager") ||
      PendingDestroy(manager) ||
      UnrealObjectIndex(manager) != state.ability_manager_object_index)
    return false;
  UnrealArrayView abilities;
  void* ability = nullptr;
  if (!UnrealReadArrayProperty(manager, L"Abilities", &abilities) ||
      !UnrealReadObjectArrayElement(abilities, slot, &ability) || !ability ||
      !UnrealObjectIsA(ability, L"TnAbility") || PendingDestroy(ability) ||
      UnrealObjectIndex(ability) != command.expected_target_object_index)
    return false;
  bool abilities_enabled = false;
  if (!UnrealReadBoolProperty(g_pawn, L"_AbilitiesEnabled",
                              &abilities_enabled) || !abilities_enabled)
    return false;
  bool requires_robot = false;
  if (!UnrealReadBoolProperty(ability, L"RequiresRobotForm",
                              &requires_robot) ||
      (requires_robot &&
       ReadMode(g_pawn) != TransformerMode::kRobot))
    return false;
  bool requires_server_start = false;
  if (!UnrealReadBoolProperty(ability, L"RequiresServerStartCooldown",
                              &requires_server_start))
    return false;
  if (requires_server_start) {
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
  // Confirm the matching release exists before issuing the press. Trigger is
  // prepared exactly once by UnrealCallVoidInt in ExecuteCommand.
  return UnrealCanCallVoidInt(g_controller, L"UnTriggerAbility", L"number");
}

bool CurrentCachedGrenade(const Command& command, const Snapshot& state) {
  if (!state.inventory_manager_object_index || !state.grenade_object_index)
    return false;
  if (ReadMode(g_pawn) != TransformerMode::kRobot) return false;
  WeaponRoots roots;
  if (!CachedWeaponRootsMatchSnapshot(state, &roots))
    return false;
  // The staged discovery cursor already proved this bag belonged to the
  // current inventory graph. Revalidate the retained object plus every cheap
  // authoritative root; if any root or object changed, reject this tap and
  // let the next snapshot rediscover it instead of walking 32 list nodes on
  // the action frame.
  void* grenade = g_cached_grenade;
  if (!grenade || !UnrealObjectValid(grenade) || PendingDestroy(grenade) ||
      !UnrealObjectIsA(grenade, L"TnGrenadeBag") ||
      UnrealObjectIndex(grenade) != command.expected_target_object_index)
    return false;
  int32_t count = -1;
  if (!UnrealReadIntProperty(grenade, L"AmmoCount", &count))
    UnrealReadIntProperty(grenade, L"ReserveAmmoCount", &count);
  return count > 0;
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

bool AuthoritativeIdentityMatches(uint64_t world_epoch,
                                  uint32_t controller_object_index,
                                  uint32_t pawn_object_index) {
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
         UnrealObjectIndex(controller) == controller_object_index &&
         (pawn = ControllerPawn(controller)) != nullptr &&
         !PendingDestroy(pawn) &&
         pawn == g_pawn &&
         UnrealObjectIndex(pawn) == pawn_object_index &&
         SupportedPlayerPair(controller, pawn) &&
         UnrealReadObjectProperty(controller, L"WorldInfo", &world) &&
         world && UnrealObjectIsA(world, L"WorldInfo") &&
         world == g_world_info && world_epoch == g_world_epoch;
}

bool AuthoritativeIdentityMatches(const PendingAbilityRelease& release) {
  return AuthoritativeIdentityMatches(
      release.world_epoch, release.controller_object_index,
      release.pawn_object_index);
}

bool ProcessPendingAbilityRelease() {
  if (!g_pending_ability_release.pending) return false;
  // A pending press/release pair owns native command dispatch even when its
  // release edge is not due until the next outer tick. Returning false here
  // previously allowed a second mutation into the same in-flight action.
  if (GetTickCount64() < g_pending_ability_release.due_at) return true;
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
    return true;
  }
  SetSnapshotStage("command-ability-release");
  const bool released = UnrealCallVoidInt(g_controller, L"UnTriggerAbility",
                                           L"number", release.slot);
  SetSnapshotStage("idle");
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
  return true;
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
      !AuthoritativeIdentityMatches(
          command.world_epoch, command.expected_controller_object_index,
          command.expected_player_pawn_object_index) ||
      UnrealObjectIndex(g_controller) !=
          command.expected_controller_object_index ||
      UnrealObjectIndex(g_pawn) != command.expected_player_pawn_object_index ||
      ControllerPawn(g_controller) != g_pawn ||
      PawnController(g_pawn) != g_controller || !CurrentPlayableState())
    return false;
  const TransformerMode current_mode = ReadMode(g_pawn);
  if (current_mode == TransformerMode::kUnknown ||
      current_mode == TransformerMode::kTransitioning)
    return false;
  if (!ActionRateLimitElapsed(command.type, now)) return false;
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
            current_mode == TransformerMode::kTransitioning)
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
          ReadMode(g_pawn) != TransformerMode::kRobot ||
          command.expected_target_object_index !=
              state.active_weapon_object_index ||
          !CurrentInventory(&manager, &active) ||
          UnrealObjectIndex(manager) !=
                          state.inventory_manager_object_index ||
          UnrealObjectIndex(active) != command.expected_target_object_index ||
          !IsRangedWeapon(active) ||
          !UnrealReadIntProperty(active, L"ReserveAmmoCount", &reserve) ||
          !UnrealReadIntProperty(active, L"AmmoClipCount", &clip) ||
          !([&] {
            void* weapon_data = nullptr;
            return UnrealReadObjectProperty(active, L"Data", &weapon_data) &&
                   weapon_data &&
                   UnrealReadIntProperty(weapon_data, L"MaxAmmoClipCount",
                                         &clip_max);
          }()) ||
          reserve <= 0 || clip < 0 || clip_max <= 0 || clip >= clip_max)
        return false;
      return InvokeAction(command, "command-reload", [] {
        return UnrealCallVoid(g_controller, L"ReloadWeapon");
      });
    }
    case CommandType::kSwapWeapon: {
      if (!(state.capabilities & kCapSwapAction) ||
          ReadMode(g_pawn) != TransformerMode::kRobot ||
          command.expected_target_object_index !=
              state.active_weapon_object_index ||
          !CurrentCachedAlternateRobotWeapon(state))
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
          !CurrentCachedGrenade(command, state))
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
  InterlockedExchange(&g_last_action_type,
                      static_cast<LONG>(command.type));
  InterlockedExchange(&g_last_action_request_low,
                      static_cast<LONG>(command.request_id & 0xFFFFFFFFu));
  InterlockedExchange(&g_last_action_tick,
                      static_cast<LONG>(GetTickCount()));
  // 1 = dequeued for authoritative validation.
  InterlockedExchange(&g_last_action_phase, 1);
  Snapshot state;
  if (!CopyLatestSnapshot(&state)) {
    InterlockedExchange(&g_last_action_phase, 5);
    Log(LogLevel::kWarning,
        "[Action] request=%llu type=%u result=rejected reason=no-snapshot",
        static_cast<unsigned long long>(command.request_id),
        static_cast<unsigned>(command.type));
    return;
  }
  UnrealBeginGameThreadPulse();
  const bool accepted = ExecuteCommand(command, state);
  if (InterlockedCompareExchange(&g_last_action_phase, 0, 0) == 1)
    InterlockedExchange(&g_last_action_phase, accepted ? 3 : 5);
  Log(accepted ? LogLevel::kInfo : LogLevel::kWarning,
      "[Action] request=%llu type=%u result=%s epoch=%llu",
      static_cast<unsigned long long>(command.request_id),
      static_cast<unsigned>(command.type), accepted ? "accepted" : "rejected",
      static_cast<unsigned long long>(command.world_epoch));
}

struct GameWindowCandidate {
  HWND window = nullptr;
  unsigned long long client_area = 0;
};

BOOL CALLBACK FindPrimaryGameWindow(HWND window, LPARAM context_value) {
  if (!context_value || !IsWindowVisible(window)) return TRUE;
  DWORD process_id = 0;
  GetWindowThreadProcessId(window, &process_id);
  if (process_id != GetCurrentProcessId()) return TRUE;

  wchar_t class_name[128] = {};
  GetClassNameW(window, class_name,
                static_cast<int>(sizeof(class_name) / sizeof(class_name[0])));
  if (lstrcmpiW(class_name, L"TransformersWfcSecondScreenPanel") == 0)
    return TRUE;
  if ((GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
    return TRUE;

  RECT client = {};
  if (!GetClientRect(window, &client)) return TRUE;
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  if (width <= 0 || height <= 0) return TRUE;

  auto* candidate = reinterpret_cast<GameWindowCandidate*>(context_value);
  const unsigned long long area =
      static_cast<unsigned long long>(width) *
      static_cast<unsigned long long>(height);
  if (area > candidate->client_area) {
    candidate->window = window;
    candidate->client_area = area;
  }
  return TRUE;
}

HWND ResolvePrimaryGameWindow() {
  if (g_display_game_window && IsWindow(g_display_game_window))
    return g_display_game_window;
  GameWindowCandidate candidate;
  EnumWindows(&FindPrimaryGameWindow,
              reinterpret_cast<LPARAM>(&candidate));
  g_display_game_window = candidate.window;
  return candidate.window;
}

bool WindowIsOwnedBy(HWND root, HWND value) {
  return root && value &&
         (value == root || GetAncestor(value, GA_ROOT) == root);
}

bool ActivatePrimaryGameWindow(HWND window, const char* phase) {
  if (!window || !IsWindow(window)) return false;
  const DWORD owner_thread = GetWindowThreadProcessId(window, nullptr);
  const DWORD caller_thread = GetCurrentThreadId();
  ShowWindow(window, SW_SHOW);
  const BOOL raised = BringWindowToTop(window);
  const BOOL foreground_requested = SetForegroundWindow(window);
  SetActiveWindow(window);
  SetFocus(window);
  const HWND foreground = GetForegroundWindow();
  const HWND active = GetActiveWindow();
  const HWND focused = GetFocus();
  const bool verified = WindowIsOwnedBy(window, foreground) &&
                        WindowIsOwnedBy(window, active) &&
                        WindowIsOwnedBy(window, focused);
  Log(verified ? LogLevel::kInfo : LogLevel::kWarning,
      "[Display] game focus phase=%s hwnd=%p ownerTid=%lu callerTid=%lu raised=%u foregroundRequest=%u foreground=%p active=%p focused=%p verified=%u",
      phase ? phase : "unknown", window,
      static_cast<unsigned long>(owner_thread),
      static_cast<unsigned long>(caller_thread), raised ? 1u : 0u,
      foreground_requested ? 1u : 0u, foreground, active, focused,
      verified ? 1u : 0u);
  return verified;
}

// Returns true only when this call changed the real game HWND. The caller then
// yields the rest of this tick so UE3 can process WM_WINDOWPOSCHANGED/WM_SIZE
// and recreate its D3D9 viewport without sharing the frame with reflection.
bool EnsureNativeUpperViewport(ULONGLONG now) {
  if (!g_display_retry_at) {
    g_display_retry_at = now + kDisplayStartupDelayMs;
    return false;
  }
  if (now < g_display_retry_at) return false;
  g_display_retry_at = now + kDisplayRetryMs;

  HWND window = ResolvePrimaryGameWindow();
  if (!window) return false;

  RECT client = {};
  RECT outer = {};
  if (!GetClientRect(window, &client) || !GetWindowRect(window, &outer)) {
    g_display_game_window = nullptr;
    g_display_resolution_applied = false;
    return false;
  }
  const int width = client.right - client.left;
  const int height = client.bottom - client.top;
  const bool correct = width == kUpperViewportWidth &&
                       height == kUpperViewportHeight && outer.left == 0 &&
                       outer.top == 0;
  if (correct) {
    if (!g_display_resolution_applied) {
      Log(LogLevel::kInfo,
          "[Display] real Win32 upper client verified hwnd=%p position=0,0 size=%dx%d",
          window, width, height);
    }
    g_display_resolution_applied = true;
    if (!g_display_focus_applied &&
        (!g_display_focus_confirm_at || now >= g_display_focus_confirm_at)) {
      g_display_focus_applied =
          ActivatePrimaryGameWindow(window, "stable-native-client");
      if (!g_display_focus_applied) {
        g_display_focus_confirm_at = now + 500;
        g_display_retry_at = g_display_focus_confirm_at;
      } else {
        g_display_focus_confirm_at = 0;
      }
    }
    return false;
  }
  g_display_resolution_applied = false;
  g_display_focus_applied = false;

  const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
  const LONG_PTR border_bits = WS_CAPTION | WS_THICKFRAME | WS_BORDER |
                               WS_DLGFRAME | WS_SYSMENU | WS_MINIMIZEBOX |
                               WS_MAXIMIZEBOX;
  const LONG_PTR borderless =
      (style & ~border_bits) | WS_POPUP | WS_VISIBLE;
  if (borderless != style) {
    SetLastError(ERROR_SUCCESS);
    const LONG_PTR previous =
        SetWindowLongPtrW(window, GWL_STYLE, borderless);
    if (!previous && GetLastError() != ERROR_SUCCESS) {
      Log(LogLevel::kWarning,
          "[Display] could not make WFC's game window borderless error=%lu",
          static_cast<unsigned long>(GetLastError()));
      return false;
    }
  }

  // This is the same WEMU compatibility path used for PC games which create a
  // bordered 640x480 fallback inside a 1280x720 virtual desktop. Resizing the
  // HWND (not the Android surface) sends UE3 WM_SIZE, making the game recreate
  // its own D3D client at the requested resolution. Keep the resize itself
  // non-activating; once geometry is committed below, activate the authoritative
  // game HWND explicitly instead of allowing Wine's remap to leave focus on its
  // root window or on the native lower panel.
  if (!SetWindowPos(window, nullptr, 0, 0, kUpperViewportWidth,
                    kUpperViewportHeight,
                    SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                        SWP_FRAMECHANGED)) {
    Log(LogLevel::kWarning,
        "[Display] Win32 upper-client resize failed hwnd=%p error=%lu",
        window, static_cast<unsigned long>(GetLastError()));
    return false;
  }

  RECT verified_client = {};
  RECT verified_outer = {};
  const bool verified = GetClientRect(window, &verified_client) &&
                        GetWindowRect(window, &verified_outer) &&
                        verified_client.right - verified_client.left ==
                            kUpperViewportWidth &&
                        verified_client.bottom - verified_client.top ==
                            kUpperViewportHeight &&
                        verified_outer.left == 0 && verified_outer.top == 0;
  wchar_t class_name[128] = {};
  GetClassNameW(window, class_name,
                static_cast<int>(sizeof(class_name) / sizeof(class_name[0])));
  Log(verified ? LogLevel::kInfo : LogLevel::kWarning,
      "[Display] real Win32 upper client corrected hwnd=%p class=%ls before=%d,%d %dx%d after=%ld,%ld %ldx%ld verified=%u",
      window, class_name, outer.left, outer.top, width, height,
      static_cast<long>(verified_outer.left),
      static_cast<long>(verified_outer.top),
      static_cast<long>(verified_client.right - verified_client.left),
      static_cast<long>(verified_client.bottom - verified_client.top),
      verified ? 1u : 0u);
  g_display_resolution_applied = verified;
  ActivatePrimaryGameWindow(window, "native-client-resized");
  // Wine publishes the resized GPU-backed X client just after SetWindowPos
  // returns. Confirm focus again on a later game tick so that late remap cannot
  // undo this activation and force the user to click the screen manually.
  g_display_focus_applied = false;
  g_display_focus_confirm_at = now + 500;
  g_display_retry_at = g_display_focus_confirm_at;
  return true;
}

void GameThreadPulse(DWORD tick_thread) {
  DWORD game_thread = g_game_thread.load(std::memory_order_relaxed);
  if (!game_thread) {
    game_thread = tick_thread;
    g_game_thread.store(game_thread, std::memory_order_relaxed);
    Log(LogLevel::kInfo, "[Lifecycle] game-thread tick established tid=%lu",
        static_cast<unsigned long>(game_thread));
  }
  if (tick_thread != game_thread) return;
  const ULONGLONG now = GetTickCount64();
  if (now - g_last_pulse < kSnapshotSliceIntervalMs) return;
  // Match the stable PC mods: decide whether work is due before entering the
  // reflection layer. The old order cleared its region cache and validated the
  // full GObjects/GNames backing arrays on virtually every rendered frame,
  // even though telemetry was only published once per second. VirtualQuery is
  // particularly costly under Wine/Box64 and was the source of the constant
  // frame-time tax seen only with this mod enabled.
  const bool display_due = !g_display_retry_at || now >= g_display_retry_at;
  const bool resolver_due = now >= g_resolve_retry_at;
  const bool snapshot_due =
      g_snapshot_build.phase != SnapshotBuildPhase::kIdle
          ? now >= g_snapshot_build.next_slice_at
          : now - g_last_publish >= kPublishIntervalMs;
  if (!display_due && !resolver_due && !snapshot_due) return;
  if (SwapWorldMutationActive()) return;
  g_last_pulse = now;
  if (display_due && EnsureNativeUpperViewport(now)) return;
  if (!resolver_due && !snapshot_due) return;
  // A staged snapshot yields to the native engine between slices, so cached
  // regions must age out while GC and streaming own the game thread. Retire a
  // bounded portion on every due slice; UnrealBridge lazily revalidates those
  // entries without recreating the old all-at-once VirtualQuery hitch.
  UnrealBeginGameThreadPulse();
  if (!UnrealBridgeReady()) return;

  // Resolving the local player and collecting the UI are separate slices. A
  // due resolver never shares a frame with a snapshot stage.
  if (resolver_due) {
    const ULONGLONG work_started = GetTickCount64();
    SetSnapshotStage("resolve");
    ResolveStableObjects(now);
    const ULONGLONG elapsed = GetTickCount64() - work_started;
    if (elapsed >= 8 && now - g_last_slow_pulse_log >= 5000) {
      g_last_slow_pulse_log = now;
      Log(LogLevel::kWarning,
          "[Performance] resolver slice used %llums of game tick",
          static_cast<unsigned long long>(elapsed));
    }
    SetSnapshotStage("idle");
    return;
  }

  const ULONGLONG work_started = GetTickCount64();
  AdvanceSnapshotBuild(now);
  const ULONGLONG elapsed = GetTickCount64() - work_started;
  if (elapsed >= 8 && now - g_last_slow_pulse_log >= 5000) {
    g_last_slow_pulse_log = now;
    Log(LogLevel::kWarning,
        "[Performance] snapshot slice stage=%s used %llums of game tick",
        SnapshotStage(),
        static_cast<unsigned long long>(elapsed));
  }
  SetSnapshotStage("idle");
}

void __attribute__((fastcall)) HookedTick(void* self, void*, float delta) {
  LONG idle = 0;
  bool bridge_work_before_tick = false;
  const bool outermost = g_tick_in_progress.compare_exchange_strong(
      idle, 1, std::memory_order_acq_rel, std::memory_order_relaxed);
  DWORD tick_thread = 0;
  if (outermost &&
      !g_hook_stopping.load(std::memory_order_acquire)) {
    tick_thread = GetCurrentThreadId();
    g_game_engine = self;
    g_tick_started_ms.store(GetTickCount(), std::memory_order_release);
    // Bottom-screen actions are native input edges. Dispatch them before the
    // world tick so the engine can complete controller, pawn, animation,
    // collision and render-state transitions inside its normal frame. The old
    // post-Tick dispatch let Transform mutate render-backed pawn state after
    // the frame had already been submitted, producing an asynchronous R6025
    // on WFC's rendering thread.
    const DWORD game_thread =
        g_game_thread.load(std::memory_order_relaxed);
    if (game_thread && tick_thread == game_thread) {
      bool class_system_did_work = false;
      if (!TickCampaignClassSystem(self, tick_thread, GetTickCount64(),
                                   &class_system_did_work)) {
        Log(LogLevel::kError,
            "[CampaignClass] native campaign selection system failed closed");
      }
      if (class_system_did_work) bridge_work_before_tick = true;
      if (CampaignClassSelectionActive()) {
        SetCommandAcceptance(false);
        DiscardAllCommands();
        bridge_work_before_tick = true;
      }
      if (DeferredCheckpointFailedClosed()) {
        SetCommandAcceptance(false);
        DiscardAllCommands();
        bridge_work_before_tick = true;
      }
      if (ConsumeLogicalPlayerDeathNotification()) {
        g_role_proxy_death_pending = true;
        g_role_proxy_failed_closed = true;
        g_role_proxy_consecutive_failures = 0;
        g_role_proxy_consecutive_successes = 0;
        if (!QuiesceCharacterRoleProxy())
          Log(LogLevel::kWarning,
              "[RoleAdapter] logical-player death proxy quiesce was incomplete");
        SetCommandAcceptance(false);
        DiscardAllCommands();
        bridge_work_before_tick = true;
        Log(LogLevel::kWarning,
            "[RoleAdapter] logical player death entered checkpoint recovery; mod input remains closed until a coherent native player graph is resolved");
      }
      bool swap_gate_closed = g_swap_transaction.command_gate_closed() ||
                              g_role_proxy_failed_closed ||
                              g_role_proxy_death_pending ||
                              DeferredCheckpointPending() ||
                              DeferredCheckpointFailedClosed();
      if (g_swap_transaction.command_gate_closed()) {
        const CharacterSwapPhase swap_phase_before_drive =
            g_swap_transaction.status().phase;
        // Done deliberately keeps command input closed until a freshly
        // synchronized snapshot is published. It performs no world mutation,
        // so reserving the frame here would also suppress GameThreadPulse and
        // make that required snapshot impossible (a permanent post-swap
        // deadlock). Checkpoint completion is different because it can replay
        // a native save and still requires an exclusive frame.
        const bool done_waiting_for_snapshot =
            swap_phase_before_drive == CharacterSwapPhase::kDone &&
            !g_checkpoint_restore.active;
        bridge_work_before_tick =
            bridge_work_before_tick || !done_waiting_for_snapshot;
        DriveCharacterSwap(GetTickCount64());
        swap_gate_closed = g_swap_transaction.command_gate_closed() ||
                           g_role_proxy_failed_closed ||
                           g_role_proxy_death_pending ||
                           DeferredCheckpointPending() ||
                           DeferredCheckpointFailedClosed();
      }
      if (!g_swap_transaction.command_gate_closed() &&
          DeferredCheckpointPending() &&
          !DeferredCheckpointFailedClosed()) {
        bridge_work_before_tick = true;
        BeginDeferredCheckpointRestore(GetTickCount64());
        swap_gate_closed = true;
      }
      if (!SwapWorldMutationActive() && !g_role_proxy_death_pending) {
        SetSnapshotStage("role-proxy");
        bool proxy_did_work = false;
        const bool proxy_ok = TickCharacterRoleProxy(
            GetTickCount64(), &proxy_did_work);
        bridge_work_before_tick = bridge_work_before_tick || proxy_did_work;
        if (proxy_did_work) {
          if (proxy_ok) {
            g_role_proxy_consecutive_failures = 0;
            if (g_role_proxy_failed_closed) {
              if (g_role_proxy_consecutive_successes < 3)
                ++g_role_proxy_consecutive_successes;
              if (g_role_proxy_consecutive_successes >= 3 &&
                  CharacterRoleMappingObjectsValid() &&
                  !g_role_proxy_death_pending &&
                  !g_swap_transaction.command_gate_closed() &&
                  !DeferredCheckpointFailedClosed()) {
                g_role_proxy_failed_closed = false;
                g_role_proxy_consecutive_successes = 0;
                SetCommandAcceptance(true);
                swap_gate_closed = false;
                Log(LogLevel::kInfo,
                    "[RoleAdapter] bounded AI proxy recovered after three coherent slices; mod input reopened");
              }
            }
          } else {
            g_role_proxy_consecutive_successes = 0;
            if (g_role_proxy_consecutive_failures < 3)
              ++g_role_proxy_consecutive_failures;
            // A single reflected read can fail during an equip/streaming edge.
            // Close the persistent gate only after three consecutive due
            // slices, while still suppressing commands on the failed frame.
            swap_gate_closed = true;
            if (g_role_proxy_consecutive_failures >= 3) {
              const bool first_failure = !g_role_proxy_failed_closed;
              g_role_proxy_failed_closed = true;
              SetCommandAcceptance(false);
              DiscardAllCommands();
              if (first_failure)
                Log(LogLevel::kError,
                    "[RoleAdapter] bounded AI proxy failed three consecutive slices; mod input closed to protect the live controller graph");
            }
          }
        }
      }
      bool released_ability = false;
      if (!swap_gate_closed && g_pending_ability_release.pending) {
        SetSnapshotStage("pending-ability-release");
        released_ability = ProcessPendingAbilityRelease();
        bridge_work_before_tick = true;
      }
      // A release is the second edge of one logical ability tap. Let the
      // original engine tick consume it before dispatching any other queued
      // mutation; otherwise rapid mixed taps can issue two state transitions
      // in the same pre-Tick window.
      if (!swap_gate_closed && !released_ability &&
          PendingCommandCount() != 0) {
        SetSnapshotStage("queued-command");
        ProcessOneQueuedCommand();
        bridge_work_before_tick = true;
      }
    }
    // Keep all reflection and telemetry work on the safe side of WFC's
    // native frame submission. UGameEngine::Tick queues polymorphic render
    // commands which the RenderingThread starts consuming immediately. The
    // previous post-Tick pulse delayed this hook's return while that queue was
    // already live; in the failing build the render worker eventually invoked
    // a command whose vtable had reached __purecall (TWFC+0x00C44809), even
    // when no bottom-screen action was pending. Collecting the previous
    // frame's state here avoids racing the queue created by this Tick. It also
    // keeps native input/role mutations and reflection out of the same frame.
    if (!bridge_work_before_tick) {
      SetSnapshotStage("bridge-pulse");
      GameThreadPulse(tick_thread);
    }
    SetSnapshotStage("original-tick");
  }
  if (!g_tick_entry_logged.load(std::memory_order_relaxed)) {
    g_tick_entry_logged.store(true, std::memory_order_relaxed);
    Log(LogLevel::kInfo,
        "[Lifecycle] first detoured game tick entered tid=%lu self=%p original=%p",
        static_cast<unsigned long>(GetCurrentThreadId()), self, g_original_tick);
  }
  WfcCallThisTick(g_original_tick, self, delta);
  if (outermost &&
      g_hook_stopping.load(std::memory_order_acquire)) {
    SetSnapshotStage("idle");
    g_tick_started_ms.store(0, std::memory_order_release);
    g_tick_in_progress.store(0, std::memory_order_release);
    return;
  }
  if (!g_tick_return_logged.load(std::memory_order_relaxed)) {
    g_tick_return_logged.store(true, std::memory_order_relaxed);
    Log(LogLevel::kInfo, "[Lifecycle] first original game tick returned");
  }
  if (outermost) {
    SetSnapshotStage("idle");
    g_tick_started_ms.store(0, std::memory_order_release);
    g_tick_in_progress.store(0, std::memory_order_release);
  }
}

}  // namespace

bool InitializeGameBridge(HMODULE executable) {
  if (!executable || g_tick_slot) return g_tick_slot != nullptr;
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_executable = executable;
  g_hook_stopping.store(false, std::memory_order_release);
  if (!InitializeUnrealBridge(executable)) return false;
  g_exit_process_slot = FindIatSlot(executable, "KERNEL32.dll", "ExitProcess");
  if (!g_exit_process_slot ||
      !InstallIatHook(g_exit_process_slot, *g_exit_process_slot,
                      reinterpret_cast<void*>(&HookedExitProcess),
                      &g_original_exit_process)) {
    g_exit_process_slot = nullptr;
    g_original_exit_process = nullptr;
  }
  g_terminate_process_slot =
      FindIatSlot(executable, "KERNEL32.dll", "TerminateProcess");
  if (!g_terminate_process_slot ||
      !InstallIatHook(g_terminate_process_slot, *g_terminate_process_slot,
                      reinterpret_cast<void*>(&HookedTerminateProcess),
                      &g_original_terminate_process)) {
    g_terminate_process_slot = nullptr;
    g_original_terminate_process = nullptr;
  }
  g_abort_slot = FindIatSlot(executable, "MSVCR90.dll", "abort");
  if (!g_abort_slot ||
      !InstallIatHook(g_abort_slot, *g_abort_slot,
                      reinterpret_cast<void*>(&HookedAbort),
                      &g_original_abort)) {
    g_abort_slot = nullptr;
    g_original_abort = nullptr;
  }
  g_crt_exit_slot = FindIatSlot(executable, "MSVCR90.dll", "_exit");
  if (!g_crt_exit_slot ||
      !InstallIatHook(g_crt_exit_slot, *g_crt_exit_slot,
                      reinterpret_cast<void*>(&HookedCrtExit),
                      &g_original_crt_exit)) {
    g_crt_exit_slot = nullptr;
    g_original_crt_exit = nullptr;
  }
  Log(LogLevel::kInfo,
      "[Lifecycle] process-exit diagnostics installed ExitProcess=%u TerminateProcess=%u abort=%u _exit=%u",
      g_exit_process_slot ? 1u : 0u, g_terminate_process_slot ? 1u : 0u,
      g_abort_slot ? 1u : 0u, g_crt_exit_slot ? 1u : 0u);
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
    RestoreExitDiagnostics();
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
    RestoreExitDiagnostics();
    ShutdownUnrealBridge();
    return false;
  }
  g_tick_slot = slot;
  InterlockedExchange(&g_watchdog_stop, 0);
  g_watchdog_thread_id = 0;
  g_watchdog_thread = CreateThread(nullptr, 0, TickWatchdog, nullptr, 0,
                                   &g_watchdog_thread_id);
  if (!g_watchdog_thread)
    Log(LogLevel::kWarning,
        "[Lifecycle] game-tick watchdog unavailable error=%lu",
        static_cast<unsigned long>(GetLastError()));
  Log(LogLevel::kInfo,
      "[Lifecycle] exact-build UGameEngine::Tick vtable hook installed function=%08lX slot=%08lX",
      static_cast<unsigned long>(kGameEngineTickRva),
      static_cast<unsigned long>(kGameEngineTickVtableSlotRva));
  SetCommandAcceptance(true);
  return true;
}

void ShutdownGameBridge() {
  SetCommandAcceptance(false);
  DiscardAllCommands();
  g_hook_stopping.store(true, std::memory_order_release);
  if (g_tick_slot) {
    RestoreIatHook(g_tick_slot, reinterpret_cast<void*>(&HookedTick),
                   g_original_tick);
  }
  const ULONGLONG wait_until = GetTickCount64() + 2000;
  while (g_tick_in_progress.load(std::memory_order_acquire) != 0 &&
         GetTickCount64() < wait_until)
    Sleep(1);
  const bool tick_idle =
      g_tick_in_progress.load(std::memory_order_acquire) == 0;
  InterlockedExchange(&g_watchdog_stop, 1);
  if (g_watchdog_thread) {
    WaitForSingleObject(g_watchdog_thread, 1000);
    CloseHandle(g_watchdog_thread);
    g_watchdog_thread = nullptr;
  }
  g_watchdog_thread_id = 0;
  if (!tick_idle) {
    Log(LogLevel::kWarning,
        "[Lifecycle] bridge teardown deferred: hooked game tick is still active stage=%s",
        SnapshotStage());
    return;
  }
  if (g_purecall_slot) {
    RestoreIatHook(g_purecall_slot, reinterpret_cast<void*>(&HookedPurecall),
                   g_original_purecall);
  }
  RestoreExitDiagnostics();
  // Executor detours depend on Unreal reflection metadata and therefore must
  // be removed before UnrealBridge is torn down. HookedTick is already stopped
  // here, so clearing a live role mapping cannot be observed by another game
  // frame during process teardown.
  if (!ShutdownCharacterRoleAdapter())
    Log(LogLevel::kWarning,
        "[Lifecycle] one or more character-role executor detours could not be restored");
  if (!ShutdownCampaignClassSystem())
    Log(LogLevel::kWarning,
        "[Lifecycle] one or more campaign class-system executor detours could not be restored");
  ShutdownUnrealBridge();
  g_tick_slot = nullptr;
  g_original_tick = nullptr;
  g_purecall_slot = nullptr;
  g_original_purecall = nullptr;
  g_tick_entry_logged.store(false, std::memory_order_relaxed);
  g_tick_return_logged.store(false, std::memory_order_relaxed);
  g_purecall_logged = 0;
  g_tick_in_progress.store(0, std::memory_order_relaxed);
  g_hook_stopping.store(false, std::memory_order_relaxed);
  g_tick_started_ms.store(0, std::memory_order_relaxed);
  g_watchdog_reported_start = 0;
  g_last_action_type = 0;
  g_last_action_phase = 0;
  g_last_action_tick = 0;
  g_last_action_request_low = 0;
  g_last_accepted_action.fill(0);
  g_last_accepted_native_action = 0;
  g_executable = nullptr;
  g_game_thread.store(0, std::memory_order_relaxed);
  g_last_pulse = 0;
  g_last_publish = 0;
  g_last_objective_discovery = 0;
  g_last_squad_discovery = 0;
  g_resolve_retry_at = 0;
  g_display_retry_at = 0;
  g_display_focus_confirm_at = 0;
  g_display_resolution_applied = false;
  g_display_focus_applied = false;
  g_display_game_window = nullptr;
  g_world_ready_since = 0;
  g_last_resolve_diagnostic = 0;
  g_last_slow_pulse_log = 0;
  g_last_snapshot_reject_log = 0;
#ifndef NDEBUG
  g_last_live_summary_log = 0;
#endif
  g_game_engine = nullptr;
  g_world_info = nullptr;
  g_controller = nullptr;
  g_pawn = nullptr;
  g_campaign_game = nullptr;
  g_game_replication_info = nullptr;
  g_squad_controllers.fill(nullptr);
  g_squad_pawns.fill(nullptr);
  g_squad_weapons.fill(nullptr);
  g_squad_record_indices.fill(-1);
  g_next_squad_refresh_slot = 0;
  g_objective_manager = nullptr;
  g_objective_marker_base = nullptr;
  g_objective_number = -1;
  ClearWeaponTelemetryCache();
  g_snapshot_build = {};
  g_switch_in_progress = false;
  g_switch_snapshot_pending = false;
  g_switch_snapshot_sequence = 0;
  g_role_proxy_failed_closed = false;
  g_role_proxy_death_pending = false;
  g_world_command_gate_pending = false;
  g_checkpoint_restore = {};
  g_swap_journal = {};
  g_pending_ability_release = {};
  SetSnapshotStage("idle");
}

}  // namespace wfcds
