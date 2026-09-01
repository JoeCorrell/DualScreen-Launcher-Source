#include "GameApi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "IatHook.h"
#include "LiveStatePolicy.h"
#include "Log.h"
#include "Snapshot.h"
#include "StateStore.h"

#if defined(__GNUC__)
#define GD_FASTCALL __attribute__((fastcall))
#else
#define GD_FASTCALL __fastcall
#endif

namespace gdds {
namespace {

constexpr char kUpdateName[] = "?Update@GameEngine@GAME@@QAEXH@Z";
constexpr char kGetMainPlayerName[] =
    "?GetMainPlayer@GameEngine@GAME@@QBEPAVPlayer@2@XZ";
constexpr char kActorAddToSceneName[] =
    "?AddToScene@Actor@GAME@@UAEXAAVGraphicsScene@2@PBVFrustum@2@@Z";
constexpr char kGameEngineName[] = "?gGameEngine@GAME@@3PAVGameEngine@1@A";
constexpr char kEngineName[] = "?gEngine@GAME@@3PAVEngine@1@A";

struct WorldVec3 {
  void* region;
  float x;
  float y;
  float z;
};
static_assert(sizeof(WorldVec3) == 16, "Grim Dawn I386 WorldVec3 ABI changed");

struct MemVectorU32 {
  const uint32_t* begin;
  const uint32_t* end;
  const uint32_t* capacity;
};

struct MsvcString32 {
  union {
    char inline_data[16];
    const char* heap_data;
  } storage;
  uint32_t size;
  uint32_t capacity;
};

void** g_game_engine_variable = nullptr;
void** g_engine_variable = nullptr;
HMODULE g_game_module = nullptr;
uintptr_t g_update = 0;
uintptr_t g_is_loading = 0;
uintptr_t g_is_online = 0;
uintptr_t g_get_main_player = 0;
uintptr_t g_actor_add_to_scene = 0;
uintptr_t g_get_difficulty = 0;
uintptr_t g_get_level = 0;
uintptr_t g_get_experience = 0;
uintptr_t g_get_money = 0;
uintptr_t g_get_life = 0;
uintptr_t g_get_life_limit = 0;
uintptr_t g_get_mana = 0;
uintptr_t g_get_mana_limit = 0;
uintptr_t g_get_player_name = 0;
uintptr_t g_get_total_attribute = 0;
uintptr_t g_get_inventory = 0;
uintptr_t g_get_skill_points = 0;
uintptr_t g_get_skill_list = 0;
uintptr_t g_get_devotion_points = 0;
uintptr_t g_get_total_devotion_points = 0;
uintptr_t g_get_max_devotion_points = 0;
uintptr_t g_get_teleport_uids = 0;
uintptr_t g_get_position = 0;
uintptr_t g_get_area_tag = 0;
uintptr_t g_is_running_simulation = 0;
uintptr_t g_is_in_teleport_mode = 0;

ULONGLONG g_last_sample_ms = 0;
ULONGLONG g_last_publish_ms = 0;
DWORD g_sample_interval_ms = 100;
unsigned g_telemetry_tier = 0;
void* g_last_player = nullptr;
Snapshot g_last_snapshot = {};
bool g_have_last = false;
volatile LONG g_hook_thread_id = 0;
volatile LONG g_hook_depth = 0;
volatile LONG g_telemetry_thread_fault = 0;
volatile LONG g_safe_mode_tick_logged = 0;
void* volatile g_observed_player = nullptr;
void* volatile g_validated_render_player = nullptr;
volatile LONG g_update_epoch = 0;
volatile LONG g_observed_epoch = 0;
volatile LONG g_observed_tick = 0;
volatile LONG g_observed_thread = 0;
volatile LONG g_validated_epoch = 0;
volatile LONG g_validated_tick = 0;
volatile LONG g_validated_thread = 0;
LARGE_INTEGER g_qpc_frequency = {};
uint32_t g_consecutive_sample_failures = 0;
thread_local unsigned g_custom_character_submission_depth = 0;
volatile LONG g_player_observation_logged = 0;
void** g_actor_hook_slot = nullptr;
void** g_update_hook_slot = nullptr;

uintptr_t ResolveExact(HMODULE module, const char* name, uintptr_t rva) {
  if (!module || !name) return 0;
  const uintptr_t actual =
      reinterpret_cast<uintptr_t>(GetProcAddress(module, name));
  const uintptr_t expected = reinterpret_cast<uintptr_t>(module) + rva;
  if (actual != expected) {
    Log(LogLevel::kError,
        "[API] exact export mismatch name=%s actual=%p expected=%p rva=%08lx",
        name, reinterpret_cast<void*>(actual),
        reinterpret_cast<void*>(expected), static_cast<unsigned long>(rva));
    return 0;
  }
  return actual;
}

extern "C" uint32_t GddsCallThisUIntNoArgs(uintptr_t function,
                                            const void* self);
extern "C" uint32_t GddsCallThisBoolNoArgs(uintptr_t function,
                                            const void* self);
extern "C" void* GddsCallThisPointerNoArgs(uintptr_t function,
                                            const void* self);
extern "C" float GddsCallThisFloatNoArgs(uintptr_t function,
                                          const void* self);
extern "C" double GddsCallThisDoubleNoArgs(uintptr_t function,
                                            const void* self);
extern "C" float GddsCallThisFloat1(uintptr_t function, const void* self,
                                     uint32_t argument);
extern "C" void GddsCallThisVoid1(uintptr_t function, void* self,
                                   uint32_t argument);
extern "C" void GddsCallThisVoid2(uintptr_t function, void* self,
                                   uint32_t argument1,
                                   uint32_t argument2);
extern "C" void GddsCallThisWorldVec(uintptr_t function, const void* self,
                                      WorldVec3* output);

uint32_t PointerBits(const void* value) {
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value));
}

bool Readable(const void* address, size_t size) {
  if (!address || !size) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) ||
      info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD) ||
      (info.Protect & PAGE_NOACCESS))
    return false;
  const uintptr_t start = reinterpret_cast<uintptr_t>(address);
  const uintptr_t region = reinterpret_cast<uintptr_t>(info.BaseAddress);
  return start >= region && size <= info.RegionSize - (start - region);
}

bool Executable(const void* address) {
  if (!address) return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(address, &info, sizeof(info)) || info.State != MEM_COMMIT)
    return false;
  const DWORD protection = info.Protect & 0xffu;
  return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
         protection == PAGE_EXECUTE_READWRITE ||
         protection == PAGE_EXECUTE_WRITECOPY;
}

bool Object(const void* value) {
  if (!Readable(value, sizeof(void*))) return false;
  void* const table = *reinterpret_cast<void* const*>(value);
  return Readable(table, sizeof(void*)) &&
         Executable(*reinterpret_cast<void* const*>(table));
}

bool ExactPlayer(const void* value) {
  if (!g_game_module || !Readable(value, sizeof(void*))) return false;
  const uintptr_t* const table =
      *reinterpret_cast<const uintptr_t* const*>(value);
  if (table != reinterpret_cast<const uintptr_t*>(
                   reinterpret_cast<uintptr_t>(g_game_module) + 0x005E5BE8) ||
      !Readable(table, 0x60))
    return false;
  return table[0x54 / sizeof(uintptr_t)] ==
             reinterpret_cast<uintptr_t>(g_game_module) + 0x0005D520 &&
         table[0x5C / sizeof(uintptr_t)] ==
             reinterpret_cast<uintptr_t>(g_game_module) + 0x0032E580;
}

void ObserveExactPlayer(void* player, const char* source) {
  if (!ExactPlayer(player)) return;
  InterlockedExchangePointer(&g_observed_player, player);
  InterlockedExchange(&g_observed_tick, static_cast<LONG>(GetTickCount()));
  InterlockedExchange(
      &g_observed_epoch,
      InterlockedCompareExchange(&g_update_epoch, 0, 0));
  InterlockedExchange(&g_observed_thread,
                      static_cast<LONG>(GetCurrentThreadId()));
  if (InterlockedCompareExchange(&g_player_observation_logged, 1, 0) == 0)
    Log(LogLevel::kInfo,
        "[PLAYER] exact live Player observed source=%s pointer=%p vtable=%p epoch=%ld thread=%lu",
        source ? source : "unknown", player,
        *reinterpret_cast<void* const*>(player),
        static_cast<long>(InterlockedCompareExchange(&g_update_epoch, 0, 0)),
        static_cast<unsigned long>(GetCurrentThreadId()));
}

bool Scalar(float value, float maximum = 10000000.0f) {
  return std::isfinite(value) && value >= 0.0f && value <= maximum;
}

bool CopyWide(const wchar_t* source, wchar_t* destination, size_t capacity) {
  if (!source || !destination || capacity < 2) return false;
  for (size_t index = 0; index + 1 < capacity; ++index) {
    if (!Readable(source + index, sizeof(wchar_t))) return false;
    destination[index] = source[index];
    if (!source[index]) return true;
  }
  destination[capacity - 1] = L'\0';
  return true;
}

bool CopyMsvcString(const MsvcString32* source, char* destination,
                    size_t capacity) {
  if (!source || !destination || capacity < 2 ||
      !Readable(source, sizeof(MsvcString32)))
    return false;
  MsvcString32 value = {};
  std::memcpy(&value, source, sizeof(value));
  if (value.size >= capacity || value.size > 4096 || value.capacity < value.size)
    return false;
  const char* text = value.capacity < 16 ? value.storage.inline_data
                                         : value.storage.heap_data;
  if (value.size && !Readable(text, value.size)) return false;
  if (value.size) std::memcpy(destination, text, value.size);
  destination[value.size] = '\0';
  return true;
}

bool VectorCount(const MemVectorU32* source, size_t element_size,
                 uint32_t maximum, uint32_t* count) {
  if (!source || !count || !Readable(source, sizeof(MemVectorU32))) return false;
  const MemVectorU32 value = *source;
  if (!value.begin && !value.end && !value.capacity) {
    *count = 0;
    return true;
  }
  const uintptr_t begin = reinterpret_cast<uintptr_t>(value.begin);
  const uintptr_t end = reinterpret_cast<uintptr_t>(value.end);
  const uintptr_t capacity = reinterpret_cast<uintptr_t>(value.capacity);
  if (!begin || end < begin || capacity < end)
    return false;
  const uintptr_t bytes = end - begin;
  if (!element_size || bytes % element_size) return false;
  const uintptr_t items = bytes / element_size;
  if (items > maximum || (items && !Readable(value.begin, bytes))) return false;
  *count = static_cast<uint32_t>(items);
  return true;
}

bool SamePayload(const Snapshot& left, const Snapshot& right) {
  Snapshot a = left;
  Snapshot b = right;
  a.sequence = b.sequence = 0;
  a.sampled_qpc = b.sampled_qpc = 0;
  a.sample_milliseconds = b.sample_milliseconds = 0.0f;
  return std::memcmp(&a, &b, sizeof(Snapshot)) == 0;
}

void AdvanceRevisions(Snapshot* value, const Snapshot& previous) {
  const bool stats_changed =
      value->health != previous.health || value->max_health != previous.max_health ||
      value->energy != previous.energy || value->max_energy != previous.max_energy ||
      value->level != previous.level || value->experience != previous.experience ||
      value->physique != previous.physique || value->cunning != previous.cunning ||
      value->spirit != previous.spirit ||
      value->offensive_ability != previous.offensive_ability ||
      value->defensive_ability != previous.defensive_ability;
  const bool map_changed = value->player_x != previous.player_x ||
                           value->player_y != previous.player_y ||
                           value->player_z != previous.player_z ||
                           value->riftgate_count != previous.riftgate_count ||
                           std::strcmp(value->area_tag, previous.area_tag) != 0;
  value->stats_revision = previous.stats_revision + (stats_changed ? 1u : 0u);
  value->inventory_revision = previous.inventory_revision +
      (value->inventory_count != previous.inventory_count ||
               value->iron_bits != previous.iron_bits
           ? 1u
           : 0u);
  const bool skills_changed = value->skill_points != previous.skill_points ||
                              value->skill_count != previous.skill_count ||
                              value->devotion_points_available !=
                                  previous.devotion_points_available ||
                              value->devotion_points_total !=
                                  previous.devotion_points_total;
  value->map_revision = previous.map_revision + (map_changed ? 1u : 0u);
  value->equipment_revision = previous.equipment_revision;
  value->skills_revision = previous.skills_revision +
                            (skills_changed ? 1u : 0u);
  value->quest_revision = previous.quest_revision;
}

void LogPublishedState(const Snapshot& value, const Snapshot& previous,
                       bool first_live_sample) {
  if (first_live_sample) {
    Log(LogLevel::kInfo,
        "[STATE] live epoch=%llu caps=%llx level=%u xp=%u hp=%.1f/%.1f "
        "energy=%.1f/%.1f iron=%u inventory=%u skills=%u points=%u "
        "devotion=%u/%u/%u difficulty=%u",
        static_cast<unsigned long long>(value.world_epoch),
        static_cast<unsigned long long>(value.capabilities), value.level,
        value.experience, value.health, value.max_health, value.energy,
        value.max_energy, value.iron_bits, value.inventory_count,
        value.skill_count, value.skill_points,
        value.devotion_points_available, value.devotion_points_total,
        value.devotion_points_max, value.difficulty);
    Log(LogLevel::kInfo,
        "[STATE] position caps=%llx xyz=(%.2f,%.2f,%.2f) area='%s' "
        "riftgates=%u attributes=(%.1f,%.1f,%.1f,%.1f,%.1f)",
        static_cast<unsigned long long>(value.capabilities), value.player_x,
        value.player_y, value.player_z, value.area_tag, value.riftgate_count,
        value.physique, value.cunning, value.spirit,
        value.offensive_ability, value.defensive_ability);
    return;
  }
  if (value.inventory_revision != previous.inventory_revision) {
    Log(LogLevel::kInfo,
        "[STATE] inventory revision=%u object-count=%u iron=%u",
        value.inventory_revision, value.inventory_count, value.iron_bits);
  }
  if (value.skills_revision != previous.skills_revision) {
    Log(LogLevel::kInfo,
        "[STATE] skills revision=%u count=%u points=%u devotion=%u/%u/%u",
        value.skills_revision, value.skill_count, value.skill_points,
        value.devotion_points_available, value.devotion_points_total,
        value.devotion_points_max);
  }
  if (value.map_revision != previous.map_revision &&
      (value.riftgate_count != previous.riftgate_count ||
       std::strcmp(value.area_tag, previous.area_tag) != 0)) {
    Log(LogLevel::kInfo,
        "[STATE] map revision=%u xyz=(%.2f,%.2f,%.2f) area='%s' "
        "riftgates=%u",
        value.map_revision, value.player_x, value.player_y, value.player_z,
        value.area_tag, value.riftgate_count);
  }
  if (value.stats_revision != previous.stats_revision) {
    LogRateLimited("state-vitals", 2000, LogLevel::kInfo,
                   "[STATE] vitals revision=%u level=%u xp=%u hp=%.1f/%.1f "
                   "energy=%.1f/%.1f sample=%.3fms",
                   value.stats_revision, value.level, value.experience,
                   value.health, value.max_health, value.energy,
                   value.max_energy, value.sample_milliseconds);
  }
}

void ClearSampleFailures() {
  g_consecutive_sample_failures = 0;
}

void RejectLiveSample(const char* reason) {
  if (g_consecutive_sample_failures < UINT32_MAX)
    ++g_consecutive_sample_failures;
  LogRateLimited("sample-rejected", 2000, LogLevel::kError,
                 "[STATE] sample rejected reason=%s consecutive=%u",
                 reason ? reason : "unknown", g_consecutive_sample_failures);
  if (g_consecutive_sample_failures < 3) return;
  ResetStateIfChanged(
      Lifecycle::kFaulted, "Grim Dawn 1.3.0.7 I386",
      "Telemetry validation failed repeatedly; live actions are disabled.",
      false);
  g_have_last = false;
}

void InvalidateRenderPlayer(bool clear_observation) {
  InterlockedExchangePointer(&g_validated_render_player, nullptr);
  InterlockedExchange(&g_validated_epoch, 0);
  InterlockedExchange(&g_validated_tick, 0);
  InterlockedExchange(&g_validated_thread, 0);
  if (clear_observation) {
    InterlockedExchangePointer(&g_observed_player, nullptr);
    InterlockedExchange(&g_observed_epoch, 0);
    InterlockedExchange(&g_observed_tick, 0);
    InterlockedExchange(&g_observed_thread, 0);
  }
}

void ValidateRenderPlayer(void* game_engine, LONG epoch) {
  bool roots_match = Readable(g_game_engine_variable, sizeof(void*)) &&
                     *g_game_engine_variable == game_engine &&
                     Object(game_engine) &&
                     Readable(g_engine_variable, sizeof(void*)) &&
                     Object(*g_engine_variable);
  // A Player that the primary world submitted to Actor::AddToScene in the
  // current frame is stronger lifecycle evidence than IsGameLoading. The
  // latter remains asserted during portions of world activation on this
  // build and was clearing the real Player before the snapshot could publish.
  // The primary-render observer is exact-vtable filtered, epoch bounded and
  // age bounded; our custom scene is explicitly excluded from observation.
  if (!roots_match) {
    LogRateLimited("player-roots", 3000, LogLevel::kWarning,
                   "[PLAYER] validation waiting for stable engine roots epoch=%ld",
                   static_cast<long>(epoch));
    InvalidateRenderPlayer(true);
    return;
  }
  void* player =
      InterlockedCompareExchangePointer(&g_observed_player, nullptr, nullptr);
  const LONG observed_epoch =
      InterlockedCompareExchange(&g_observed_epoch, 0, 0);
  const DWORD observed_tick = static_cast<DWORD>(
      InterlockedCompareExchange(&g_observed_tick, 0, 0));
  const DWORD now = GetTickCount();
  const DWORD age = TickAge(now, observed_tick);
  const LONG observed_thread =
      InterlockedCompareExchange(&g_observed_thread, 0, 0);
  const bool recent_observation = IsRecentRenderObservation(
      static_cast<uint32_t>(epoch), static_cast<uint32_t>(observed_epoch),
      age) && observed_thread == static_cast<LONG>(GetCurrentThreadId());
  // Test generation and age before dereferencing the candidate. An old
  // address may still be readable after a character unload, but it is never
  // a valid source for live calls.
  const bool exact = recent_observation && ExactPlayer(player);
  if (!exact) {
    LogRateLimited(
        "player-validation", 3000, LogLevel::kInfo,
        "[PLAYER] validation pending pointer=%p exact=%u observed_epoch=%ld current_epoch=%ld age_ms=%lu",
        player, exact ? 1u : 0u,
        static_cast<long>(observed_epoch), static_cast<long>(epoch),
        static_cast<unsigned long>(age));
    InvalidateRenderPlayer(false);
    return;
  }
  InterlockedExchangePointer(&g_validated_render_player, player);
  InterlockedExchange(&g_validated_tick, static_cast<LONG>(now));
  InterlockedExchange(&g_validated_epoch, epoch);
  InterlockedExchange(&g_validated_thread,
                      static_cast<LONG>(GetCurrentThreadId()));
}

void GD_FASTCALL UpdateHook(void* self, void*, int delta) {
  InterlockedIncrement(&g_update_epoch);
  InterlockedIncrement(&g_hook_depth);
  const LONG thread_id = static_cast<LONG>(GetCurrentThreadId());
  const LONG owner = InterlockedCompareExchange(&g_hook_thread_id, thread_id, 0);
  bool newly_faulted = false;
  if (!owner) {
    Log(LogLevel::kInfo, "[HOOK] first Update callback thread=%lu",
        static_cast<unsigned long>(thread_id));
  } else if (owner != thread_id) {
    newly_faulted =
        InterlockedCompareExchange(&g_telemetry_thread_fault, 1, 0) == 0;
    if (newly_faulted)
      Log(LogLevel::kError,
          "[HOOK] fail-closed: Update callback changed thread from %lu to %lu",
          static_cast<unsigned long>(owner),
          static_cast<unsigned long>(thread_id));
  }
  GddsCallThisVoid1(g_update, self, static_cast<uint32_t>(delta));
  if (newly_faulted)
    ResetStateIfChanged(
        Lifecycle::kFaulted, "Grim Dawn 1.3.0.7 I386",
        "Update thread invariant violated; telemetry is disabled.", false);
  // The exact Player is observed while the primary world renders, which is
  // after Update. Sampling here used to invalidate a valid player before that
  // render and repeatedly overwrite kLive with kNoCharacter. Present tail is
  // now the sole live-state sampling point.
  InterlockedDecrement(&g_hook_depth);
}

void GD_FASTCALL ActorAddToSceneHook(void* self, void*, void* scene,
                                     const void* frustum) {
  // Character::AddToScene reaches this Engine import with ECX still holding
  // the concrete Character. The exact Player vtable filter makes this a
  // passive, allocation-free player discovery seam. Ignore our own custom
  // scene submission so a pointer that has left the primary world cannot
  // keep itself alive through the second-screen preview.
  if (g_custom_character_submission_depth == 0)
    ObserveExactPlayer(self, "primary-render");
  GddsCallThisVoid2(g_actor_add_to_scene, self, PointerBits(scene),
                    PointerBits(frustum));
}

}  // namespace

bool ResolveGameApi(HMODULE, HMODULE game, HMODULE engine) {
  QueryPerformanceFrequency(&g_qpc_frequency);
  g_game_module = game;
  g_game_engine_variable = reinterpret_cast<void**>(
      ResolveExact(game, kGameEngineName, 0x8090A4));
  g_engine_variable = reinterpret_cast<void**>(
      ResolveExact(engine, kEngineName, 0x368534));
  g_update = ResolveExact(game, kUpdateName, 0x25A620);
  g_is_loading = ResolveExact(
      game, "?IsGameLoading@GameEngine@GAME@@QBE_NXZ", 0x24A390);
  g_is_online = ResolveExact(
      game, "?IsGameEngineOnline@GameEngine@GAME@@QBE_NXZ", 0x035D20);
  g_get_main_player = ResolveExact(
      game, kGetMainPlayerName, 0x269E90);
  g_actor_add_to_scene = ResolveExact(
      engine, kActorAddToSceneName, 0x00025A30);
  g_get_difficulty = ResolveExact(
      game, "?GetGameDifficulty@GameEngine@GAME@@QBE?AW4GameDifficulty@2@XZ",
      0x25CD00);
  g_get_level = ResolveExact(
      game, "?GetCharLevel@Character@GAME@@QBE?BIXZ", 0x04B3D0);
  g_get_experience = ResolveExact(
      game, "?GetExperiencePoints@Character@GAME@@QBE?BIXZ", 0x04B490);
  g_get_money = ResolveExact(
      game, "?GetCurrentMoney@Character@GAME@@QBE?BIXZ", 0x04CB00);
  g_get_life = ResolveExact(
      game, "?GetCurrentLife@Character@GAME@@QBE?BNXZ", 0x056FF0);
  g_get_life_limit = ResolveExact(
      game, "?GetLifeLimit@Character@GAME@@QBE?BMXZ", 0x057110);
  g_get_mana = ResolveExact(
      game, "?GetCurrentMana@Character@GAME@@QBE?BMXZ", 0x056B40);
  g_get_mana_limit = ResolveExact(
      game, "?GetManaLimit@Character@GAME@@QBE?BMXZ", 0x056F50);
  g_get_player_name = ResolveExact(
      game, "?GetPlayerName@Player@GAME@@QBEPBGXZ", 0x3205E0);
  g_get_total_attribute = ResolveExact(
      game,
      "?GetTotalCharAttribute@Character@GAME@@QBEMW4CharAttributeType@2@@Z",
      0x054FA0);
  g_get_inventory = ResolveExact(
      game, "?GetInventoryItems@Character@GAME@@QBEABV?$vector@I@mem@@XZ",
      0x04E100);
  g_get_skill_points = ResolveExact(
      game, "?GetSkillPoints@Character@GAME@@QBE?BIXZ", 0x04B6A0);
  g_get_skill_list = ResolveExact(
      game,
      "?GetSkillList@Character@GAME@@QBEABV?$vector@PAVSkill@GAME@@@mem@@XZ",
      0x058500);
  g_get_devotion_points = ResolveExact(
      game, "?GetDevotionPoints@Character@GAME@@QBE?BIXZ", 0x04B510);
  g_get_total_devotion_points = ResolveExact(
      game, "?GetTotalDevotionPoints@Character@GAME@@QBE?BIXZ", 0x04B780);
  g_get_max_devotion_points = ResolveExact(
      game, "?GetMaxDevotionPoints@Character@GAME@@QBE?BIXZ", 0x04B790);
  g_get_teleport_uids = ResolveExact(
      game,
      "?GetTeleportUIDs@Player@GAME@@QBEABV?$vector@VUniqueId@GAME@@@mem@@XZ",
      0x320B90);
  g_get_position = ResolveExact(
      game, "?GetPathPosition@Character@GAME@@QBE?AVWorldVec3@2@XZ",
      0x048D80);
  g_get_area_tag = ResolveExact(
      engine,
      "?GetAreaNameTag@Engine@GAME@@QBEABV?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@XZ",
      0x068D80);
  g_is_running_simulation = ResolveExact(
      engine, "?IsRunningGameSimulation@Engine@GAME@@QBE_NXZ", 0x066A50);
  g_is_in_teleport_mode = ResolveExact(
      engine, "?IsInTeleportMode@Engine@GAME@@QBE_NXZ", 0x066A90);

  const bool required = g_game_engine_variable && g_update && g_is_loading &&
                        g_is_online &&
                        g_get_main_player && g_actor_add_to_scene &&
                        g_get_level && g_get_life &&
                        g_get_life_limit && g_get_mana && g_get_mana_limit &&
                        g_engine_variable && g_is_running_simulation &&
                        g_is_in_teleport_mode;
  Log(required ? LogLevel::kInfo : LogLevel::kError,
      "[API] exports root=%p update=%p loading=%p player=%p vitals=%u attributes=%u inventory=%u skills=%u devotion=%u riftgates=%u position=%u area=%u",
      static_cast<void*>(g_game_engine_variable), reinterpret_cast<void*>(g_update),
      reinterpret_cast<void*>(g_is_loading),
      reinterpret_cast<void*>(g_get_main_player), required ? 1u : 0u,
      g_get_total_attribute ? 1u : 0u,
      g_get_inventory ? 1u : 0u,
      (g_get_skill_points && g_get_skill_list) ? 1u : 0u,
      (g_get_devotion_points && g_get_total_devotion_points &&
       g_get_max_devotion_points) ? 1u : 0u,
      g_get_teleport_uids ? 1u : 0u,
      g_get_position ? 1u : 0u,
      (g_engine_variable && g_get_area_tag) ? 1u : 0u);
  return required;
}

bool InstallGameUpdateHook(HMODULE executable) {
  void** render_player_slot =
      FindIatSlot(g_game_module, "Engine.dll", kActorAddToSceneName);
  void* original_actor_add = nullptr;
  if (!render_player_slot ||
      !InstallIatHook(render_player_slot,
                      reinterpret_cast<void*>(g_actor_add_to_scene),
                      reinterpret_cast<void*>(&ActorAddToSceneHook),
                      &original_actor_add)) {
    Log(LogLevel::kError,
        "[HOOK] Actor::AddToScene Player observer installation refused slot=%p expected=%p",
        static_cast<void*>(render_player_slot),
        reinterpret_cast<void*>(g_actor_add_to_scene));
    return false;
  }
  Log(LogLevel::kInfo,
      "[HOOK] Actor::AddToScene Player observer installed slot=%p original=%p replacement=%p",
      static_cast<void*>(render_player_slot), original_actor_add,
      reinterpret_cast<void*>(&ActorAddToSceneHook));
  g_actor_hook_slot = render_player_slot;

  void** slot = FindIatSlot(executable, "Game.dll", kUpdateName);
  void* original = nullptr;
  if (!slot || !InstallIatHook(slot, reinterpret_cast<void*>(g_update),
                               reinterpret_cast<void*>(&UpdateHook), &original)) {
    Log(LogLevel::kError,
        "[HOOK] GameEngine::Update IAT installation refused slot=%p expected=%p",
        static_cast<void*>(slot), reinterpret_cast<void*>(g_update));
    if (!RestoreIatHook(g_actor_hook_slot,
                        reinterpret_cast<void*>(&ActorAddToSceneHook),
                        reinterpret_cast<void*>(g_actor_add_to_scene)))
      Log(LogLevel::kError,
          "[HOOK] failed to roll back Actor::AddToScene observer after Update hook refusal");
    g_actor_hook_slot = nullptr;
    return false;
  }
  g_update_hook_slot = slot;
  Log(LogLevel::kInfo,
      "[HOOK] GameEngine::Update IAT installed slot=%p original=%p replacement=%p",
      static_cast<void*>(slot), original, reinterpret_cast<void*>(&UpdateHook));
  return true;
}

bool UninstallGameUpdateHook() {
  bool restored = true;
  if (g_update_hook_slot) {
    restored = RestoreIatHook(g_update_hook_slot,
                              reinterpret_cast<void*>(&UpdateHook),
                              reinterpret_cast<void*>(g_update)) && restored;
    g_update_hook_slot = nullptr;
  }
  if (g_actor_hook_slot) {
    restored = RestoreIatHook(g_actor_hook_slot,
                              reinterpret_cast<void*>(&ActorAddToSceneHook),
                              reinterpret_cast<void*>(g_actor_add_to_scene)) &&
               restored;
    g_actor_hook_slot = nullptr;
  }
  return restored;
}

void SetSampleInterval(DWORD milliseconds) {
  if (milliseconds >= 25 && milliseconds <= 1000)
    g_sample_interval_ms = milliseconds;
}

void SetTelemetryTier(unsigned tier) {
  if (tier <= 6) g_telemetry_tier = tier;
}

void SampleGameState() {
  if (InterlockedCompareExchange(&g_telemetry_thread_fault, 0, 0) != 0)
    return;
  const ULONGLONG now_ms = GetTickCount64();
  if (g_telemetry_tier == 0) {
    ClearSampleFailures();
    if (InterlockedCompareExchange(&g_safe_mode_tick_logged, 1, 0) == 0) {
      ResetState(Lifecycle::kWaitingForGame, "Grim Dawn 1.3.0.7 I386",
                 "Update hook verified; telemetry safe mode tier 0 is active.",
                 true);
      Log(LogLevel::kInfo,
          "[STATE] Update tick observed; tier=0 performs no game-state calls");
    }
    return;
  }
  LARGE_INTEGER started = {};
  LARGE_INTEGER finished = {};
  QueryPerformanceCounter(&started);

  if (!Readable(g_game_engine_variable, sizeof(void*))) {
    InvalidateRenderPlayer(true);
    ResetStateIfChanged(Lifecycle::kWaitingForGame,
                        "Grim Dawn 1.3.0.7 I386",
                        "Game engine root is not available.", false);
    g_last_player = nullptr;
    g_have_last = false;
    ClearSampleFailures();
    return;
  }
  void* game_engine = *g_game_engine_variable;
  if (!Object(game_engine)) {
    InvalidateRenderPlayer(true);
    ResetStateIfChanged(Lifecycle::kWaitingForGame,
                        "Grim Dawn 1.3.0.7 I386",
                        "Game engine is not online yet.", false);
    g_last_player = nullptr;
    g_have_last = false;
    ClearSampleFailures();
    return;
  }
  if (!Readable(g_engine_variable, sizeof(void*))) {
    InvalidateRenderPlayer(true);
    ResetStateIfChanged(Lifecycle::kWaitingForGame,
                        "Grim Dawn 1.3.0.7 I386",
                        "Engine root is not available.", false);
    g_last_player = nullptr;
    g_have_last = false;
    ClearSampleFailures();
    return;
  }
  void* engine = *g_engine_variable;
  if (!Object(engine)) {
    InvalidateRenderPlayer(true);
    ResetStateIfChanged(Lifecycle::kWaitingForGame,
                        "Grim Dawn 1.3.0.7 I386",
                        "Engine is not online yet.", false);
    g_last_player = nullptr;
    g_have_last = false;
    ClearSampleFailures();
    return;
  }
  // Lifecycle tier 1 is deliberately call-free. Calling GetMainPlayer here
  // after Update blocked the game's owning thread under WEMU and eventually
  // terminated the session. The passive Actor::AddToScene observer records
  // the exact Player while the primary world naturally renders it instead.
  void* player = ValidatedRenderPlayer();
  if (!ExactPlayer(player)) {
    Snapshot current = {};
    CopyLatestSnapshot(&current);
    const DWORD now_tick = GetTickCount();
    const DWORD observed_tick = static_cast<DWORD>(
        InterlockedCompareExchange(&g_observed_tick, 0, 0));
    if (ShouldPreserveLiveSnapshot(current.lifecycle, now_tick,
                                   observed_tick)) {
      // Preserve display state only. No game getter is called until a fresh,
      // exact render observation validates the Player again.
      ClearSampleFailures();
      return;
    }

    InvalidateRenderPlayer(true);
    const bool online = g_is_online &&
        GddsCallThisBoolNoArgs(g_is_online, game_engine) != 0;
    const bool simulation_running = g_is_running_simulation &&
        GddsCallThisBoolNoArgs(g_is_running_simulation, engine) != 0;
    const bool teleporting = g_is_in_teleport_mode &&
        GddsCallThisBoolNoArgs(g_is_in_teleport_mode, engine) != 0;
    const bool loading_flag = g_is_loading &&
        GddsCallThisBoolNoArgs(g_is_loading, game_engine) != 0;
    // IsGameLoading remains asserted at Grim Dawn's main menu on this build.
    // Treat it as transitional only once the engine reports an online world;
    // otherwise the correct panel state is Waiting For Save.
    const bool loading = teleporting ||
                         (online && (loading_flag || !simulation_running));
    const Lifecycle lifecycle = loading ? Lifecycle::kLoading
                                        : Lifecycle::kNoCharacter;
    const char* status = loading
        ? "A save or world transition is loading; live actions are locked."
        : "Waiting for the game to publish a loaded character.";
    if (ResetStateIfChanged(lifecycle, "Grim Dawn 1.3.0.7 I386", status,
                            true))
      Log(LogLevel::kInfo,
          "[STATE] lifecycle=%s after sustained absence of a validated Player",
          loading ? "loading" : "no-character");
    g_last_player = nullptr;
    g_have_last = false;
    ClearSampleFailures();
    return;
  }

  Snapshot current = {};
  const bool have_current = CopyLatestSnapshot(&current);
  const bool activation_required =
      player != g_last_player || !have_current ||
      current.lifecycle == Lifecycle::kBoot ||
      current.lifecycle == Lifecycle::kWaitingForGame ||
      current.lifecycle == Lifecycle::kLoading ||
      current.lifecycle == Lifecycle::kNoCharacter;
  if (!activation_required &&
      now_ms - g_last_sample_ms < g_sample_interval_ms)
    return;
  g_last_sample_ms = now_ms;
  if (player != g_last_player) {
    ResetState(Lifecycle::kLive, "Grim Dawn 1.3.0.7 I386",
               "Live exact-build telemetry active; write actions remain gated.",
               true);
    g_last_player = player;
    g_have_last = false;
    ClearSampleFailures();
  }

  if (g_telemetry_tier == 1) {
    Snapshot current = {};
    CopyLatestSnapshot(&current);
    if (current.lifecycle != Lifecycle::kLive)
      ResetState(Lifecycle::kLive, "Grim Dawn 1.3.0.7 I386",
                 "Lifecycle and player discovery validated at tier 1.", true);
    return;
  }

  Snapshot previous = {};
  if (!CopyLatestSnapshot(&previous)) return;
  Snapshot value = {};
  value.version = kSnapshotVersion;
  value.size = sizeof(Snapshot);
  value.sequence = previous.sequence + 1;
  value.world_epoch = previous.world_epoch;
  value.lifecycle = Lifecycle::kLive;
  std::snprintf(value.build_label, sizeof(value.build_label),
                "Grim Dawn 1.3.0.7 I386");
  std::snprintf(value.status, sizeof(value.status),
                "Read-only live diagnostic; unvalidated actions are disabled.");

  const double health = GddsCallThisDoubleNoArgs(g_get_life, player);
  if (!std::isfinite(health) || health < 0.0 || health > 10000000.0) {
    LogRateLimited("health-invalid", 5000, LogLevel::kError,
                   "[STATE] rejected invalid current health %.6f", health);
    RejectLiveSample("current health outside the exact ABI range");
    return;
  }
  value.health = static_cast<float>(health);
  value.max_health = GddsCallThisFloatNoArgs(g_get_life_limit, player);
  value.energy = GddsCallThisFloatNoArgs(g_get_mana, player);
  value.max_energy = GddsCallThisFloatNoArgs(g_get_mana_limit, player);
  if (!Scalar(value.health) || !Scalar(value.max_health) ||
      !Scalar(value.energy) || !Scalar(value.max_energy) ||
      value.max_health < value.health || value.max_energy < value.energy) {
    LogRateLimited("vitals-invalid", 5000, LogLevel::kError,
                   "[STATE] rejected invalid vitals hp=%.3f/%.3f energy=%.3f/%.3f",
                   value.health, value.max_health, value.energy, value.max_energy);
    RejectLiveSample("vitals failed finite/range/order validation");
    return;
  }
  value.capabilities |= kCapVitals;

  value.level = GddsCallThisUIntNoArgs(g_get_level, player);
  if (!value.level || value.level > 250) {
    RejectLiveSample("character level outside the supported game range");
    return;
  }
  value.experience = g_get_experience
                         ? GddsCallThisUIntNoArgs(g_get_experience, player)
                         : 0;
  value.capabilities |= kCapIdentity;
  if (g_telemetry_tier >= 3 && g_get_player_name &&
      CopyWide(static_cast<const wchar_t*>(
                   GddsCallThisPointerNoArgs(g_get_player_name, player)),
               value.character_name,
               sizeof(value.character_name) / sizeof(value.character_name[0])))
    value.capabilities |= kCapIdentity;

  if (g_get_money) {
    value.iron_bits = GddsCallThisUIntNoArgs(g_get_money, player);
    value.capabilities |= kCapEconomy;
  }
  if (g_get_difficulty) {
    const uint32_t difficulty =
        GddsCallThisUIntNoArgs(g_get_difficulty, game_engine);
    if (difficulty <= 2) {
      value.difficulty = difficulty;
      value.capabilities |= kCapDifficulty;
    }
  }

  if (g_telemetry_tier >= 3 && g_get_total_attribute) {
    value.physique = GddsCallThisFloat1(g_get_total_attribute, player, 1);
    value.cunning = GddsCallThisFloat1(g_get_total_attribute, player, 2);
    value.spirit = GddsCallThisFloat1(g_get_total_attribute, player, 3);
    value.offensive_ability =
        GddsCallThisFloat1(g_get_total_attribute, player, 21);
    value.defensive_ability =
        GddsCallThisFloat1(g_get_total_attribute, player, 23);
    if (Scalar(value.physique) && Scalar(value.cunning) &&
        Scalar(value.spirit) && Scalar(value.offensive_ability) &&
        Scalar(value.defensive_ability))
      value.capabilities |= kCapAttributes;
    else
      value.physique = value.cunning = value.spirit =
          value.offensive_ability = value.defensive_ability = 0.0f;
  }

  if (g_telemetry_tier >= 4 && g_get_inventory &&
      VectorCount(static_cast<const MemVectorU32*>(
                      GddsCallThisPointerNoArgs(g_get_inventory, player)),
                  sizeof(uint32_t), 4096, &value.inventory_count))
    value.capabilities |= kCapInventoryCount;

  if (g_telemetry_tier >= 4 && g_get_skill_points && g_get_skill_list) {
    const uint32_t skill_points =
        GddsCallThisUIntNoArgs(g_get_skill_points, player);
    uint32_t skill_count = 0;
    if (skill_points <= 10000 &&
        VectorCount(static_cast<const MemVectorU32*>(
                        GddsCallThisPointerNoArgs(g_get_skill_list, player)),
                    sizeof(uint32_t), 2048, &skill_count)) {
      value.skill_points = skill_points;
      value.skill_count = skill_count;
      value.capabilities |= kCapSkills;
    } else {
      LogRateLimited("skills-invalid", 5000, LogLevel::kWarning,
                     "[STATE] optional skill summary unavailable points=%u",
                     skill_points);
    }
  }
  if (g_telemetry_tier >= 4 && g_get_devotion_points &&
      g_get_total_devotion_points &&
      g_get_max_devotion_points) {
    value.devotion_points_available =
        GddsCallThisUIntNoArgs(g_get_devotion_points, player);
    value.devotion_points_total =
        GddsCallThisUIntNoArgs(g_get_total_devotion_points, player);
    value.devotion_points_max =
        GddsCallThisUIntNoArgs(g_get_max_devotion_points, player);
    if (value.devotion_points_available <= value.devotion_points_total &&
        value.devotion_points_total <= value.devotion_points_max &&
        value.devotion_points_max <= 10000)
      value.capabilities |= kCapDevotion;
    else
      value.devotion_points_available = value.devotion_points_total =
          value.devotion_points_max = 0;
  }
  if (g_telemetry_tier >= 5 && g_get_teleport_uids &&
      VectorCount(static_cast<const MemVectorU32*>(
                      GddsCallThisPointerNoArgs(g_get_teleport_uids, player)),
                  16, 1024, &value.riftgate_count))
    value.capabilities |= kCapMap;

  if (g_telemetry_tier >= 6 && g_get_position) {
    WorldVec3 position = {};
    GddsCallThisWorldVec(g_get_position, player, &position);
    if (position.region && Readable(position.region, sizeof(void*)) &&
        std::isfinite(position.x) && std::isfinite(position.y) &&
        std::isfinite(position.z) &&
        std::fabs(position.x) < 10000000.0f &&
        std::fabs(position.y) < 10000000.0f &&
        std::fabs(position.z) < 10000000.0f) {
      value.player_x = position.x;
      value.player_y = position.y;
      value.player_z = position.z;
      value.player_w = 0.0f;
      value.capabilities |= kCapWorldPosition;
    }
  }

  if (g_telemetry_tier >= 5 && g_engine_variable &&
      Readable(g_engine_variable, sizeof(void*)) &&
      g_get_area_tag) {
    if (Object(engine) &&
        CopyMsvcString(static_cast<const MsvcString32*>(
                           GddsCallThisPointerNoArgs(g_get_area_tag, engine)),
                       value.area_tag,
                       sizeof(value.area_tag)))
      value.capabilities |= kCapAreaTag;
  }

  QueryPerformanceCounter(&finished);
  value.sampled_qpc = static_cast<uint64_t>(finished.QuadPart);
  if (g_qpc_frequency.QuadPart > 0)
  value.sample_milliseconds = static_cast<float>(
        1000.0 * static_cast<double>(finished.QuadPart - started.QuadPart) /
        static_cast<double>(g_qpc_frequency.QuadPart));
  AdvanceRevisions(&value, previous);
  ClearSampleFailures();

  const bool changed = !g_have_last || !SamePayload(value, g_last_snapshot);
  if (changed || now_ms - g_last_publish_ms >= 1000) {
    if (PublishSnapshot(value)) {
      const bool first_live_sample = !g_have_last;
      if (changed)
        LogPublishedState(value, g_last_snapshot, first_live_sample);
      g_last_snapshot = value;
      g_have_last = true;
      g_last_publish_ms = now_ms;
    } else {
      RejectLiveSample("state-store rejected the validated snapshot");
    }
  }
  if (value.sample_milliseconds > 5.0f)
    LogRateLimited("sample-slow", 1000, LogLevel::kWarning,
                   "[PERF] state sample took %.3f ms caps=%llx",
                   value.sample_milliseconds,
                   static_cast<unsigned long long>(value.capabilities));
}

void SampleAtPrimaryRenderTail() {
  const LONG update_thread =
      InterlockedCompareExchange(&g_hook_thread_id, 0, 0);
  if (update_thread &&
      update_thread != static_cast<LONG>(GetCurrentThreadId())) {
    LogRateLimited(
        "sample-thread-mismatch", 3000, LogLevel::kError,
        "[STATE] render-tail sampling refused: update thread=%lu render thread=%lu",
        static_cast<unsigned long>(update_thread),
        static_cast<unsigned long>(GetCurrentThreadId()));
    return;
  }
  if (!Readable(g_game_engine_variable, sizeof(void*))) return;
  void* game_engine = *g_game_engine_variable;
  if (!Object(game_engine)) return;
  const LONG epoch = InterlockedCompareExchange(&g_update_epoch, 0, 0);
  ValidateRenderPlayer(game_engine, epoch);
  SampleGameState();
}

void* ValidatedRenderPlayer() {
  const LONG before = InterlockedCompareExchange(&g_update_epoch, 0, 0);
  const LONG validated =
      InterlockedCompareExchange(&g_validated_epoch, 0, 0);
  void* player = InterlockedCompareExchangePointer(
      &g_validated_render_player, nullptr, nullptr);
  const DWORD tick = static_cast<DWORD>(
      InterlockedCompareExchange(&g_validated_tick, 0, 0));
  const LONG validated_thread =
      InterlockedCompareExchange(&g_validated_thread, 0, 0);
  const LONG after = InterlockedCompareExchange(&g_update_epoch, 0, 0);
  if (before != after || validated != after ||
      validated_thread != static_cast<LONG>(GetCurrentThreadId()) ||
      GetTickCount() - tick > 250u || !ExactPlayer(player))
    return nullptr;
  return player;
}

void BeginCustomCharacterSubmission() {
  ++g_custom_character_submission_depth;
}

void EndCustomCharacterSubmission() {
  if (g_custom_character_submission_depth)
    --g_custom_character_submission_depth;
}

}  // namespace gdds
