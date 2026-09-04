#include "CampaignClassSystem.h"

#include <array>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Log.h"
#include "UnrealBridge.h"

namespace wfcds {
namespace {

constexpr size_t kSquadSize = 3;
constexpr size_t kMaxRosterSize = 32;
constexpr size_t kCharacterNameCapacity = 64;
constexpr size_t kMaxCharacterFactions = 4;
constexpr size_t kLoadoutTextCapacity = 512;
constexpr size_t kPersistentCallStorageSize = 1024;
constexpr int32_t kCatalogScanPerTick = 256;
constexpr ULONGLONG kHookRetryMs = 15000;
constexpr ULONGLONG kUiRetryMs = 100;
constexpr ULONGLONG kUiTimeoutMs = 5000;
constexpr ULONGLONG kUiDisappearGraceMs = 300;
constexpr ULONGLONG kPromptDelayMs = 250;
constexpr ULONGLONG kPromptRetryMs = 500;
constexpr uint8_t kPromptAttempts = 3;
constexpr ULONGLONG kVerifyRetryMs = 100;
constexpr ULONGLONG kConfigurationObserveIntervalMs = 1000;
constexpr uint8_t kFactionAutobot = 0;
constexpr uint8_t kFactionDecepticon = 1;
constexpr uint8_t kFactionAll = 2;

struct CharacterName {
  std::array<char, kCharacterNameCapacity> text = {};
};

struct CharacterCatalogEntry {
  CharacterName name = {};
  std::array<uint8_t, kMaxCharacterFactions> factions = {};
  size_t faction_count = 0;
};

struct LoadoutText {
  std::array<char, kLoadoutTextCapacity> text = {};
};

struct CharacterLoadout {
  LoadoutText weapons = {};
  LoadoutText vehicle_weapons = {};
  LoadoutText abilities = {};
  LoadoutText skills = {};
};

struct ResolvedCharacterCache {
  alignas(16) std::array<uint8_t, kPersistentCallStorageSize>
      iconic_call_storage = {};
  alignas(16) std::array<uint8_t, kPersistentCallStorageSize>
      resolve_call_storage = {};
  UnrealStructValueView iconic_data = {};
  UnrealStructValueView resolved_data = {};
  bool attempted = false;
  bool ready = false;
};

struct CampaignClassConfiguration {
  bool active = false;
  bool campaign_world_seen = false;
  std::array<CharacterName, kSquadSize> mission_shells = {};
  std::array<CharacterName, kSquadSize> selected_characters = {};
  std::array<CharacterLoadout, kSquadSize> loadouts = {};
  std::array<ResolvedCharacterCache, kSquadSize> resolved = {};
  size_t initial_slot = 0;
};

enum class SelectionStage : uint8_t {
  kIdle,
  kSquadMember0,
  kLoadout0,
  kSquadMember1,
  kLoadout1,
  kSquadMember2,
  kLoadout2,
  kInitialCharacter,
  kVerifyInitialCharacter,
  kPrepareConfiguration,
  kComplete,
  kFailed,
};

enum class SelectionMenuKind : uint8_t {
  kNone,
  kCharacterSelect,
  kLoadout,
};

struct CampaignClassFlow {
  bool active = false;
  bool selection_locked = false;
  bool menu_open = false;
  bool open_pending = false;
  bool close_pending = false;
  bool prompt_pending = false;
  bool release_travel = false;
  bool travel_forwarded = false;
  bool character_select_seen = false;
  bool customization_seen = false;
  bool customization_close_requested = false;
  uint8_t prompt_attempts = 0;
  SelectionStage stage = SelectionStage::kIdle;
  SelectionMenuKind menu_kind = SelectionMenuKind::kNone;
  DWORD game_thread = 0;
  void* lobby_game = nullptr;
  uint32_t lobby_game_index = 0;
  void* controller = nullptr;
  uint32_t controller_index = 0;
  void* ui_controller = nullptr;
  uint32_t ui_controller_index = 0;
  void* current_ui = nullptr;
  uint32_t current_ui_index = 0;
  void* game_replication_info = nullptr;
  uint32_t game_replication_info_index = 0;
  void* selection_binding = nullptr;
  uint32_t selection_binding_index = 0;
  ULONGLONG action_started_at = 0;
  ULONGLONG retry_at = 0;
  ULONGLONG prompt_at = 0;
  ULONGLONG character_select_missing_since = 0;
  std::array<CharacterName, kMaxRosterSize> roster = {};
  size_t roster_count = 0;
  std::array<CharacterName, kSquadSize> squad = {};
  std::array<CharacterLoadout, kSquadSize> loadouts = {};
  CharacterName initial = {};
  CharacterName native_initial = {};
};

CampaignClassFlow g_flow;
UnrealFunctionHandle g_start_level;
UnrealFunctionHandle g_select_character;
UnrealFunctionHandle g_get_playable_characters;
UnrealFunctionHandle g_is_character_unlocked;
UnrealFunctionHandle g_find_iconic_character;
UnrealFunctionHandle g_find_custom_character;
UnrealFunctionHandle g_get_custom_characters;
UnrealFunctionHandle g_get_current_character;
UnrealFunctionHandle g_commit_character;
UnrealFunctionHandle g_clear_character;
UnrealFunctionHandle g_get_character_weapon_types;
UnrealFunctionHandle g_get_character_vehicle_weapon;
UnrealFunctionHandle g_get_character_abilities;
UnrealFunctionHandle g_get_character_skills;
UnrealFunctionHandle g_reset_character;
UnrealFunctionHandle g_write_customization_file;
UnrealFunctionHandle g_mark_skill_as_old;
UnrealFunctionHandle g_unlock_prime_mode;
UnrealFunctionHandle g_check_custom_character_data_loaded;
UnrealFunctionHandle g_resolve_character_data;
UnrealFunctionHandle g_spawn_ai_character;
UnrealFunctionHandle g_get_ai_character_definition;
UnrealFunctionExecDetour g_start_level_detour;
UnrealFunctionExecDetour g_select_character_detour;
UnrealFunctionExecDetour g_get_playable_characters_detour;
UnrealFunctionExecDetour g_find_iconic_character_detour;
UnrealFunctionExecDetour g_find_custom_character_detour;
UnrealFunctionExecDetour g_get_custom_characters_detour;
UnrealFunctionExecDetour g_get_current_character_detour;
UnrealFunctionExecDetour g_commit_character_detour;
UnrealFunctionExecDetour g_clear_character_detour;
UnrealFunctionExecDetour g_get_character_weapon_types_detour;
UnrealFunctionExecDetour g_get_character_vehicle_weapon_detour;
UnrealFunctionExecDetour g_get_character_abilities_detour;
UnrealFunctionExecDetour g_get_character_skills_detour;
UnrealFunctionExecDetour g_reset_character_detour;
UnrealFunctionExecDetour g_write_customization_file_detour;
UnrealFunctionExecDetour g_mark_skill_as_old_detour;
UnrealFunctionExecDetour g_unlock_prime_mode_detour;
UnrealFunctionExecDetour g_check_custom_character_data_loaded_detour;
UnrealFunctionExecDetour g_spawn_ai_character_detour;
uint32_t g_select_character_name_offset = 0;
uint32_t g_find_iconic_character_name_offset = 0;
uint32_t g_find_iconic_character_return_size = 0;
void* g_find_iconic_character_return_definition = nullptr;
uint32_t g_find_custom_character_name_offset = 0;
uint32_t g_find_custom_character_return_size = 0;
void* g_find_custom_character_return_definition = nullptr;
uint32_t g_commit_character_name_offset = 0;
uint32_t g_commit_weapons_offset = 0;
uint32_t g_commit_vehicle_weapons_offset = 0;
uint32_t g_commit_abilities_offset = 0;
uint32_t g_commit_skills_offset = 0;
uint32_t g_get_character_weapon_name_offset = 0;
uint32_t g_get_character_vehicle_weapon_name_offset = 0;
uint32_t g_get_character_abilities_name_offset = 0;
uint32_t g_get_character_skills_name_offset = 0;
uint32_t g_spawn_ai_character_name_offset = 0;
void* g_character_data_helpers = nullptr;
uint32_t g_character_data_helpers_index = 0;
int g_direct_resolution_slot = -1;
bool g_ai_definition_substitution_active = false;
std::array<CharacterCatalogEntry, kMaxRosterSize> g_catalog = {};
size_t g_catalog_count = 0;
int32_t g_catalog_scan_cursor = 0;
bool g_catalog_ready = false;
bool g_catalog_scan_exhausted = false;
CampaignClassConfiguration g_configuration;
ULONGLONG g_configuration_observe_at = 0;
ULONGLONG g_hook_retry_at = 0;
DWORD g_game_thread = 0;
bool g_hooks_ready = false;
bool g_stopping = false;

bool SameName(const CharacterName& left, const char* right) {
  return right && std::strcmp(left.text.data(), right) == 0;
}

bool StructCharacterNameMatches(const UnrealStructValueView& value,
                                const CharacterName& expected) {
  CharacterName actual;
  return value.data && value.size && expected.text[0] &&
         UnrealReadStructStringField(value, L"CharacterName",
                                     actual.text.data(), actual.text.size()) &&
         SameName(expected, actual.text.data());
}

bool CopyName(const char* source, size_t length, CharacterName* output) {
  if (!source || !output || !length ||
      length >= output->text.size())
    return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    if (value < 0x20 || value > 0x7E || value == ',') return false;
  }
  output->text.fill('\0');
  std::memcpy(output->text.data(), source, length);
  return true;
}

bool ToWideName(const CharacterName& source,
                std::array<wchar_t, kCharacterNameCapacity>* output) {
  if (!output || !source.text[0]) return false;
  output->fill(L'\0');
  const size_t length = std::strlen(source.text.data());
  if (!length || length >= output->size()) return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value =
        static_cast<unsigned char>(source.text[index]);
    if (value < 0x20 || value > 0x7E) return false;
    (*output)[index] = static_cast<wchar_t>(value);
  }
  return true;
}

const CharacterCatalogEntry* CatalogEntryFor(const char* name) {
  if (!name || !*name) return nullptr;
  for (size_t index = 0; index < g_catalog_count; ++index)
    if (SameName(g_catalog[index].name, name)) return &g_catalog[index];
  return nullptr;
}

bool SharesFaction(const CharacterCatalogEntry& left,
                   const CharacterCatalogEntry& right) {
  for (size_t i = 0; i < left.faction_count; ++i)
    for (size_t j = 0; j < right.faction_count; ++j)
      if (left.factions[i] == right.factions[j]) return true;
  return false;
}

bool HasFaction(const CharacterCatalogEntry& character, uint8_t faction) {
  for (size_t index = 0; index < character.faction_count; ++index)
    if (character.factions[index] == faction) return true;
  return false;
}

bool CharacterEligibleForChapter(const char* name) {
  const CharacterCatalogEntry* candidate = CatalogEntryFor(name);
  if (!candidate) return false;
  // EFactionType::kFactionAll is a wildcard, not a concrete faction. Comparing
  // arrays for any common value allowed an Autobot and a Decepticon through if
  // both records also exposed kFactionAll. Derive the chapter's authored side
  // from concrete roster entries and accept only that side (or a true wildcard
  // candidate). If old data contains no concrete side, retain the conservative
  // native roster relationship as a compatibility fallback.
  bool concrete_chapter_faction_seen = false;
  for (size_t index = 0; index < g_flow.roster_count; ++index) {
    const CharacterCatalogEntry* chapter =
        CatalogEntryFor(g_flow.roster[index].text.data());
    if (!chapter) continue;
    for (size_t faction_index = 0;
         faction_index < chapter->faction_count; ++faction_index) {
      const uint8_t faction = chapter->factions[faction_index];
      if (faction != kFactionAutobot && faction != kFactionDecepticon)
        continue;
      concrete_chapter_faction_seen = true;
      if (HasFaction(*candidate, faction) ||
          HasFaction(*candidate, kFactionAll))
        return true;
    }
  }
  if (!concrete_chapter_faction_seen)
    for (size_t index = 0; index < g_flow.roster_count; ++index) {
      const CharacterCatalogEntry* chapter =
          CatalogEntryFor(g_flow.roster[index].text.data());
      if (chapter && SharesFaction(*candidate, *chapter)) return true;
    }
  return false;
}

bool ReadIconicCatalog(void* profile_settings) {
  if (!profile_settings ||
      !UnrealObjectIsA(profile_settings, L"TnProfileSettings"))
    return false;
  UnrealArrayView presets;
  if (!UnrealReadArrayProperty(profile_settings, L"PresetIconicCharacters",
                               &presets) ||
      presets.count < static_cast<int32_t>(kSquadSize) ||
      presets.count > static_cast<int32_t>(kMaxRosterSize))
    return false;

  std::array<CharacterCatalogEntry, kMaxRosterSize> parsed = {};
  size_t parsed_count = 0;
  for (int32_t index = 0; index < presets.count; ++index) {
    void* preset = nullptr;
    bool play_in_editor_only = false;
    UnrealStructValueView data;
    CharacterCatalogEntry entry;
    if (!UnrealReadObjectArrayElement(presets, index, &preset) || !preset ||
        !UnrealObjectIsA(preset, L"TnPlayerCharacterData") ||
        !UnrealReadBoolProperty(preset, L"PlayInEditorOnly",
                                &play_in_editor_only))
      return false;
    // Match TnProfileSettings.GetAllIconicCharacterNames rather than exposing
    // the raw PresetIconicCharacters table. The latter includes editor/test
    // pawns which do not have safe retail campaign assets or vehicle data.
    if (play_in_editor_only) continue;
    if (!UnrealReadStructProperty(preset, L"Data", &data) ||
        !UnrealReadStructStringField(data, L"CharacterName",
                                     entry.name.text.data(),
                                     entry.name.text.size()) ||
        !entry.name.text[0])
      return false;
    for (int32_t faction = 0;
         faction < static_cast<int32_t>(entry.factions.size()); ++faction) {
      uint8_t value = 0;
      if (!UnrealReadStructByteArrayElement(data, L"Factions", faction,
                                            &value))
        break;
      entry.factions[entry.faction_count++] = value;
    }
    if (!entry.faction_count) return false;
    bool duplicate = false;
    for (size_t previous = 0; previous < parsed_count; ++previous)
      duplicate = duplicate ||
          SameName(parsed[previous].name, entry.name.text.data());
    if (!duplicate) parsed[parsed_count++] = entry;
  }
  if (parsed_count < kSquadSize) return false;
  g_catalog = parsed;
  g_catalog_count = parsed_count;
  g_catalog_ready = true;
  Log(LogLevel::kInfo,
      "[CampaignClass] loaded native iconic character catalog count=%u",
      static_cast<unsigned>(parsed_count));
  return true;
}

bool ScanIconicCatalogSlice() {
  if (g_catalog_ready) return true;
  if (g_catalog_scan_exhausted) return false;
  const int32_t count = UnrealObjectCount();
  if (count <= 0) return false;
  int32_t examined = 0;
  while (g_catalog_scan_cursor < count &&
         examined++ < kCatalogScanPerTick) {
    void* object = UnrealObjectAt(g_catalog_scan_cursor++);
    if (!object || !UnrealObjectValid(object) ||
        !UnrealObjectIsA(object, L"TnProfileSettings"))
      continue;
    char name[96] = {};
    if (UnrealObjectName(object, name, sizeof(name)) &&
        std::strcmp(name, "Default__TnProfileSettings") == 0 &&
        ReadIconicCatalog(object))
      return true;
  }
  if (g_catalog_scan_cursor >= count) {
    g_catalog_scan_exhausted = true;
    Log(LogLevel::kWarning,
        "[CampaignClass] native iconic catalog was not available; using the chapter roster");
  }
  return false;
}

void ClearFlowObjects() {
  g_flow.controller = nullptr;
  g_flow.controller_index = 0;
  g_flow.ui_controller = nullptr;
  g_flow.ui_controller_index = 0;
  g_flow.current_ui = nullptr;
  g_flow.current_ui_index = 0;
  g_flow.game_replication_info = nullptr;
  g_flow.game_replication_info_index = 0;
  g_flow.selection_binding = nullptr;
  g_flow.selection_binding_index = 0;
}

void ResetFlow() {
  g_flow = {};
}

bool CurrentGameForEngine(void* game_engine, void** output) {
  if (!output) return false;
  *output = nullptr;
  if (!UnrealObjectValid(game_engine)) return false;
  UnrealArrayView players;
  void* local_player = nullptr;
  void* controller = nullptr;
  void* world = nullptr;
  return UnrealReadArrayProperty(game_engine, L"GamePlayers", &players) &&
         players.count > 0 &&
         UnrealReadObjectArrayElement(players, 0, &local_player) &&
         local_player && UnrealObjectIsA(local_player, L"LocalPlayer") &&
         UnrealReadObjectProperty(local_player, L"Actor", &controller) &&
         controller && UnrealObjectValid(controller) &&
         UnrealReadObjectProperty(controller, L"WorldInfo", &world) &&
         world && UnrealObjectValid(world) &&
         UnrealReadObjectProperty(world, L"Game", output) && *output &&
         UnrealObjectValid(*output);
}

bool ObserveConfigurationWorld(void* game_engine, ULONGLONG now) {
  if (!g_configuration.active ||
      (g_flow.active && !g_flow.travel_forwarded))
    return false;
  if (now < g_configuration_observe_at) return false;
  g_configuration_observe_at = now + kConfigurationObserveIntervalMs;
  void* game = nullptr;
  if (!CurrentGameForEngine(game_engine, &game)) return true;
  if (UnrealObjectIsA(game, L"TnCampaignGameBase")) {
    g_configuration.campaign_world_seen = true;
    return true;
  }
  // Keep the configuration across the lobby-to-level gap, but retire it once
  // a completed campaign world has returned to a real non-campaign game. This
  // prevents old shell-name substitutions from leaking into the front end,
  // multiplayer, or a later chapter which has not run its own selection flow.
  if (g_configuration.campaign_world_seen) {
    Log(LogLevel::kInfo,
        "[CampaignClass] retired completed campaign squad configuration");
    g_configuration = {};
    g_configuration_observe_at = 0;
  }
  return true;
}

bool ContextBelongsToConfiguredCampaign(void* context) {
  if (!context || !UnrealObjectValid(context)) return false;
  void* world = nullptr;
  void* game = nullptr;
  if (!UnrealReadObjectProperty(context, L"WorldInfo", &world) || !world ||
      !UnrealObjectValid(world) ||
      !UnrealReadObjectProperty(world, L"Game", &game) || !game ||
      !UnrealObjectValid(game))
    return false;
  if (g_flow.active && !g_flow.travel_forwarded)
    return game == g_flow.lobby_game;
  const bool campaign = UnrealObjectIsA(game, L"TnCampaignGameBase");
  if (campaign) g_configuration.campaign_world_seen = true;
  return campaign;
}

bool PublishedObjectValid(void* object, uint32_t index) {
  return object && index && UnrealObjectValid(object) &&
         UnrealObjectIndex(object) == index;
}

void AbortFlow(const char* reason) {
  if (!g_flow.active || g_flow.stage == SelectionStage::kFailed) return;
  g_flow.stage = SelectionStage::kFailed;
  g_flow.selection_locked = true;
  g_flow.open_pending = false;
  g_flow.prompt_pending = false;
  // Failure must never strand FinalCountdown behind an overlay that can no
  // longer be addressed. ServerTravel retires the lobby movie immediately,
  // so release the original path even when a best-effort close was impossible.
  g_flow.close_pending = false;
  g_flow.menu_open = false;
  g_flow.release_travel = true;
  g_configuration = {};
  Log(LogLevel::kError,
      "[CampaignClass] native selection aborted: %s; original campaign travel will resume",
      reason ? reason : "unknown failure");
}

void BeginFlow(void* lobby_game) {
  ResetFlow();
  g_configuration = {};
  g_configuration_observe_at = 0;
  g_character_data_helpers = nullptr;
  g_character_data_helpers_index = 0;
  if (!g_catalog_ready) {
    g_catalog = {};
    g_catalog_count = 0;
    g_catalog_scan_cursor = 0;
    g_catalog_scan_exhausted = false;
  }
  g_flow.active = true;
  g_flow.stage = SelectionStage::kSquadMember0;
  g_flow.game_thread = g_game_thread;
  g_flow.lobby_game = lobby_game;
  g_flow.lobby_game_index = UnrealObjectIndex(lobby_game);
  g_flow.open_pending = true;
  g_flow.action_started_at = GetTickCount64();
  g_flow.retry_at = g_flow.action_started_at;
  Log(LogLevel::kInfo,
      "[CampaignClass] campaign travel paused for native three-member squad selection lobby=%u",
      g_flow.lobby_game_index);
}

bool IsFlowThread() {
  return g_flow.active && g_flow.game_thread &&
         GetCurrentThreadId() == g_flow.game_thread;
}

bool IsSelectedMember(const char* name, size_t selected_count);

bool IsRosterMember(const char* name) {
  if (g_flow.stage == SelectionStage::kInitialCharacter ||
      g_flow.stage == SelectionStage::kVerifyInitialCharacter)
    return IsSelectedMember(name, kSquadSize);
  if (g_catalog_ready && CharacterEligibleForChapter(name)) return true;
  for (size_t index = 0; index < g_flow.roster_count; ++index) {
    if (SameName(g_flow.roster[index], name)) return true;
  }
  return false;
}

bool IsSelectedMember(const char* name, size_t selected_count) {
  selected_count = selected_count < kSquadSize ? selected_count : kSquadSize;
  for (size_t index = 0; index < selected_count; ++index) {
    if (SameName(g_flow.squad[index], name)) return true;
  }
  return false;
}

int LoadoutSlotForStage(SelectionStage stage) {
  switch (stage) {
    case SelectionStage::kLoadout0: return 0;
    case SelectionStage::kLoadout1: return 1;
    case SelectionStage::kLoadout2: return 2;
    default: return -1;
  }
}

bool IsLoadoutStage() {
  return LoadoutSlotForStage(g_flow.stage) >= 0;
}

const CharacterName* CurrentLoadoutCharacter() {
  const int slot = LoadoutSlotForStage(g_flow.stage);
  return slot >= 0 ? &g_flow.squad[static_cast<size_t>(slot)] : nullptr;
}

CharacterLoadout* CurrentLoadout() {
  const int slot = LoadoutSlotForStage(g_flow.stage);
  return slot >= 0
      ? &g_flow.loadouts[static_cast<size_t>(slot)] : nullptr;
}

SelectionStage StageAfterLoadout(SelectionStage stage) {
  switch (stage) {
    case SelectionStage::kLoadout0: return SelectionStage::kSquadMember1;
    case SelectionStage::kLoadout1: return SelectionStage::kSquadMember2;
    case SelectionStage::kLoadout2: return SelectionStage::kInitialCharacter;
    default: return SelectionStage::kFailed;
  }
}

bool CopyLoadoutText(const char* source, LoadoutText* output) {
  if (!source || !output) return false;
  const size_t length = std::strlen(source);
  if (!length || length >= output->text.size()) return false;
  for (size_t index = 0; index < length; ++index) {
    const unsigned char value = static_cast<unsigned char>(source[index]);
    if (value < 0x20 || value > 0x7E) return false;
  }
  output->text.fill('\0');
  std::memcpy(output->text.data(), source, length);
  return true;
}

bool ToWideLoadout(const LoadoutText& source,
                   std::array<wchar_t, kLoadoutTextCapacity>* output) {
  if (!output || !source.text[0]) return false;
  output->fill(L'\0');
  const size_t length = std::strlen(source.text.data());
  if (!length || length >= output->size()) return false;
  for (size_t index = 0; index < length; ++index)
    (*output)[index] =
        static_cast<unsigned char>(source.text[index]);
  return true;
}

size_t SelectedCountForStage() {
  switch (g_flow.stage) {
    case SelectionStage::kSquadMember0:
      return 0;
    case SelectionStage::kLoadout0:
    case SelectionStage::kSquadMember1:
      return 1;
    case SelectionStage::kLoadout1:
    case SelectionStage::kSquadMember2:
      return 2;
    case SelectionStage::kLoadout2:
    case SelectionStage::kInitialCharacter:
    case SelectionStage::kVerifyInitialCharacter:
    case SelectionStage::kPrepareConfiguration:
    case SelectionStage::kComplete:
      return 3;
    default:
      return 0;
  }
}

bool ParseRoster(const char* csv) {
  if (!csv || !*csv) return false;
  std::array<CharacterName, kMaxRosterSize> parsed = {};
  size_t parsed_count = 0;
  const char* cursor = csv;
  while (*cursor) {
    while (*cursor && std::isspace(static_cast<unsigned char>(*cursor)))
      ++cursor;
    const char* start = cursor;
    while (*cursor && *cursor != ',') ++cursor;
    const char* end = cursor;
    while (end > start &&
           std::isspace(static_cast<unsigned char>(end[-1])))
      --end;
    if (end == start || parsed_count >= parsed.size() ||
        !CopyName(start, static_cast<size_t>(end - start),
                  &parsed[parsed_count]))
      return false;
    for (size_t index = 0; index < parsed_count; ++index) {
      if (SameName(parsed[index], parsed[parsed_count].text.data()))
        return false;
    }
    ++parsed_count;
    if (*cursor == ',') ++cursor;
  }
  if (parsed_count < kSquadSize) return false;
  g_flow.roster = parsed;
  g_flow.roster_count = parsed_count;
  Log(LogLevel::kInfo,
      "[CampaignClass] captured native chapter roster count=%u",
      static_cast<unsigned>(parsed_count));
  return true;
}

bool AppendWideName(const CharacterName& name,
                    std::array<wchar_t, 2048>* output, size_t* used) {
  if (!output || !used) return false;
  const char* source = name.text.data();
  const size_t length = std::strlen(source);
  if (!length || *used + length + 1 >= output->size()) return false;
  for (size_t index = 0; index < length; ++index)
    (*output)[(*used)++] = static_cast<unsigned char>(source[index]);
  return true;
}

bool BuildFilteredRoster(void* selection_binding,
                         std::array<wchar_t, 2048>* output) {
  if (!selection_binding || !UnrealObjectValid(selection_binding) ||
      !UnrealObjectIsA(selection_binding, L"TnCharacterScriptBinding") ||
      !output || !g_flow.roster_count)
    return false;
  output->fill(L'\0');
  size_t used = 0;
  size_t emitted = 0;
  const size_t selected_count = SelectedCountForStage();
  if (g_flow.stage == SelectionStage::kInitialCharacter ||
      g_flow.stage == SelectionStage::kVerifyInitialCharacter) {
    for (size_t index = 0; index < kSquadSize; ++index) {
      if (emitted && used + 1 < output->size()) (*output)[used++] = L',';
      if (!AppendWideName(g_flow.squad[index], output, &used)) return false;
      ++emitted;
    }
  } else {
    const size_t available_count =
        g_catalog_ready ? g_catalog_count : g_flow.roster_count;
    for (size_t index = 0; index < available_count; ++index) {
      const CharacterName& candidate = g_catalog_ready
          ? g_catalog[index].name : g_flow.roster[index];
      if ((g_catalog_ready &&
           !CharacterEligibleForChapter(candidate.text.data())) ||
          IsSelectedMember(candidate.text.data(), selected_count))
        continue;
      // CharacterSelect normally invokes its private lock predicate after it
      // receives the iconic list. Our expanded campaign list bypasses that
      // native filtering step, so call the exact same game predicate here and
      // fail closed if its reflected signature or data-store state is invalid.
      if (g_catalog_ready) {
        std::array<wchar_t, kCharacterNameCapacity> wide = {};
        bool unlocked = false;
        if (!ToWideName(candidate, &wide) ||
            !UnrealCallExactStringBoolResult(
                selection_binding, g_is_character_unlocked,
                L"CharacterName", wide.data(), &unlocked))
          return false;
        if (!unlocked) continue;
      }
      if (emitted && used + 1 < output->size()) (*output)[used++] = L',';
      if (!AppendWideName(candidate, output, &used)) return false;
      ++emitted;
    }
  }
  (*output)[used] = L'\0';
  return emitted != 0;
}

const wchar_t* StagePrompt() {
  switch (g_flow.stage) {
    case SelectionStage::kSquadMember0:
      return L"SELECT SQUAD MEMBER 1";
    case SelectionStage::kLoadout0:
      return L"CONFIGURE SQUAD MEMBER 1";
    case SelectionStage::kSquadMember1:
      return L"SELECT SQUAD MEMBER 2";
    case SelectionStage::kLoadout1:
      return L"CONFIGURE SQUAD MEMBER 2";
    case SelectionStage::kSquadMember2:
      return L"SELECT SQUAD MEMBER 3";
    case SelectionStage::kLoadout2:
      return L"CONFIGURE SQUAD MEMBER 3";
    case SelectionStage::kInitialCharacter:
    case SelectionStage::kVerifyInitialCharacter:
      return L"SELECT STARTING CHARACTER";
    default:
      return L"SELECT CHARACTER";
  }
}

void CallOriginal(UnrealFunctionExecDetour& detour, void* context,
                  void* unused_edx, void* frame, void* result) {
  if (detour.original_executor)
    detour.original_executor(context, unused_edx, frame, result);
}

int ConfigurationSlotForMissionShell(const char* shell) {
  if (!g_configuration.active || !shell || !*shell) return -1;
  for (size_t index = 0; index < kSquadSize; ++index)
    if (SameName(g_configuration.mission_shells[index], shell))
      return static_cast<int>(index);
  return -1;
}

bool ParseLoadoutTokens(const LoadoutText& source,
                        std::array<CharacterName, 4>* output,
                        size_t* output_count) {
  if (!output || !output_count || !source.text[0]) return false;
  *output = {};
  *output_count = 0;
  const char* cursor = source.text.data();
  while (true) {
    const char* start = cursor;
    while (*cursor && *cursor != ',') ++cursor;
    const char* end = cursor;
    while (end > start && std::isspace(static_cast<unsigned char>(end[-1])))
      --end;
    while (start < end &&
           std::isspace(static_cast<unsigned char>(*start)))
      ++start;
    if (*output_count >= output->size())
      return false;
    // WFC's native Strings.Split preserves empty loadout slots. Preserve
    // them too: skill and ability commits can legally contain leading,
    // middle, or trailing empty entries (for example "SkillA,,SkillC").
    if (end != start &&
        !CopyName(start, static_cast<size_t>(end - start),
                  &(*output)[*output_count]))
      return false;
    ++*output_count;
    if (*cursor != ',') break;
    ++cursor;
  }
  return *output_count != 0;
}

bool ApplyLoadoutArrayOverride(const UnrealStructValueView& character,
                               const wchar_t* field_name,
                               const LoadoutText& source,
                               size_t maximum_elements) {
  if (!source.text[0]) return true;
  if (std::strcmp(source.text.data(), "Empty") == 0)
    return UnrealShrinkStructArray(character, field_name, 0);
  std::array<CharacterName, 4> tokens = {};
  size_t token_count = 0;
  if (!ParseLoadoutTokens(source, &tokens, &token_count)) return false;
  token_count = token_count < maximum_elements ? token_count
                                                : maximum_elements;
  for (size_t index = 0; index < token_count; ++index) {
    std::array<wchar_t, kCharacterNameCapacity> wide = {};
    if ((tokens[index].text[0] && !ToWideName(tokens[index], &wide)) ||
        !UnrealAssignStructStringArrayElementWide(
            character, field_name, static_cast<int32_t>(index),
            wide.data()))
      return false;
  }
  return UnrealShrinkStructArray(character, field_name,
                                 static_cast<int32_t>(token_count));
}

bool ApplyConfiguredLoadout(const UnrealStructValueView& character,
                            size_t slot) {
  if (slot >= kSquadSize) return false;
  const CharacterLoadout& loadout = g_configuration.loadouts[slot];
  return ApplyLoadoutArrayOverride(character, L"WeaponTypes",
                                   loadout.weapons, 2) &&
         ApplyLoadoutArrayOverride(character, L"VehicleWeapons",
                                   loadout.vehicle_weapons, 2) &&
         ApplyLoadoutArrayOverride(character, L"Abilities",
                                   loadout.abilities, 2) &&
         ApplyLoadoutArrayOverride(character, L"Skills", loadout.skills, 3);
}

bool CommitConfiguration(const CharacterName& initial) {
  if (g_flow.roster_count < kSquadSize) return false;
  size_t initial_slot = kSquadSize;
  for (size_t index = 0; index < kSquadSize; ++index)
    if (SameName(g_flow.squad[index], initial.text.data())) {
      initial_slot = index;
      break;
    }
  if (initial_slot >= kSquadSize) return false;
  CampaignClassConfiguration configuration;
  configuration.active = true;
  configuration.initial_slot = initial_slot;
  for (size_t index = 0; index < kSquadSize; ++index) {
    configuration.mission_shells[index] = g_flow.roster[index];
    configuration.selected_characters[index] = g_flow.squad[index];
    configuration.loadouts[index] = g_flow.loadouts[index];
  }
  g_configuration = configuration;
  g_configuration_observe_at = 0;
  g_flow.native_initial = configuration.mission_shells[initial_slot];
  return g_flow.native_initial.text[0] != '\0';
}

void WFCDS_UNREAL_EXEC_CALL HookedFindIconicCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  CharacterName requested;
  const bool can_substitute =
      !g_stopping && g_hooks_ready && g_configuration.active &&
      g_game_thread && GetCurrentThreadId() == g_game_thread && result &&
      ContextBelongsToConfiguredCampaign(context) &&
      UnrealReadFrameStringParameter(frame,
                                     g_find_iconic_character_name_offset,
                                     requested.text.data(),
                                     requested.text.size());
  const int configured_slot = can_substitute
      ? (g_direct_resolution_slot >= 0 &&
                 g_direct_resolution_slot < static_cast<int>(kSquadSize)
             ? g_direct_resolution_slot
             : ConfigurationSlotForMissionShell(requested.text.data()))
      : -1;
  const CharacterName* selected = configured_slot >= 0
      ? &g_configuration.selected_characters[
            static_cast<size_t>(configured_slot)] : nullptr;
  if (!selected) {
    CallOriginal(g_find_iconic_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  const UnrealStructValueView returned = {
      result, g_find_iconic_character_return_size,
      g_find_iconic_character_return_definition};
  if (SameName(*selected, requested.text.data())) {
    CallOriginal(g_find_iconic_character_detour, context, unused_edx, frame,
                 result);
    if (!StructCharacterNameMatches(returned, *selected)) {
      Log(LogLevel::kError,
          "[CampaignClass] native iconic lookup returned invalid data name=%s",
          requested.text.data());
      return;
    }
    if (!ApplyConfiguredLoadout(returned,
                                static_cast<size_t>(configured_slot)))
      Log(LogLevel::kError,
          "[CampaignClass] selected-character loadout override failed name=%s",
          requested.text.data());
    return;
  }

  std::array<wchar_t, kCharacterNameCapacity> selected_wide = {};
  std::array<wchar_t, kCharacterNameCapacity> shell_wide = {};
  if (!ToWideName(*selected, &selected_wide) ||
      !ToWideName(requested, &shell_wide) ||
      !UnrealAssignFrameStringParameter(
          frame, g_find_iconic_character_name_offset,
          selected_wide.data())) {
    CallOriginal(g_find_iconic_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  CallOriginal(g_find_iconic_character_detour, context, unused_edx, frame,
               result);
  const bool parameter_restored = UnrealAssignFrameStringParameter(
      frame, g_find_iconic_character_name_offset, shell_wide.data());
  if (parameter_restored && StructCharacterNameMatches(returned, *selected) &&
      UnrealAssignStructWideStringField(returned, L"CharacterName",
                                        shell_wide.data())) {
    if (!ApplyConfiguredLoadout(returned,
                                static_cast<size_t>(configured_slot)))
      Log(LogLevel::kError,
          "[CampaignClass] selected-character loadout override failed shell=%s selected=%s",
          requested.text.data(), selected->text.data());
    return;
  }

  // Never let a selected cosmetic identity escape as the mission identity.
  // Re-running the original with the restored shell overwrites the result
  // through WFC's own struct assignment path and preserves script contracts.
  if (parameter_restored)
    CallOriginal(g_find_iconic_character_detour, context, unused_edx, frame,
                 result);
  Log(LogLevel::kError,
      "[CampaignClass] mission-shell substitution failed closed shell=%s selected=%s",
      requested.text.data(), selected->text.data());
}

bool ResolveCharacterDataHelpers() {
  if (PublishedObjectValid(g_character_data_helpers,
                           g_character_data_helpers_index) &&
      UnrealObjectIsA(g_character_data_helpers, L"TnCharacterDataHelpers"))
    return true;
  g_character_data_helpers = nullptr;
  g_character_data_helpers_index = 0;
  if (!PublishedObjectValid(g_flow.controller, g_flow.controller_index))
    return false;
  void* helpers = nullptr;
  if (!UnrealReadObjectProperty(g_flow.controller, L"CharacterDataHelpers",
                                &helpers) ||
      !helpers || !UnrealObjectValid(helpers) ||
      !UnrealObjectIsA(helpers, L"TnCharacterDataHelpers"))
    return false;
  g_character_data_helpers = helpers;
  g_character_data_helpers_index = UnrealObjectIndex(helpers);
  return g_character_data_helpers_index != 0;
}

bool EnsureResolvedCharacter(size_t slot) {
  if (!g_configuration.active || slot >= kSquadSize ||
      !PublishedObjectValid(g_flow.controller, g_flow.controller_index) ||
      !ResolveCharacterDataHelpers() ||
      !UnrealExactScriptFunctionValid(g_find_iconic_character) ||
      !UnrealExactScriptFunctionValid(g_resolve_character_data))
    return false;
  ResolvedCharacterCache& cache = g_configuration.resolved[slot];
  if (cache.attempted) return cache.ready;
  cache.attempted = true;

  std::array<wchar_t, kCharacterNameCapacity> selected = {};
  if (!ToWideName(g_configuration.selected_characters[slot], &selected))
    return false;

  // Both results live in chapter-lifetime storage. This is important because
  // UE3's returned structs contain FString/TArray members whose backing data
  // must not be tied to a temporary reflected-call buffer.
  g_direct_resolution_slot = static_cast<int>(slot);
  const bool iconic_ready = UnrealCallExactStringStructResultPersistent(
      g_flow.controller, g_find_iconic_character, L"CharacterName",
      selected.data(), cache.iconic_call_storage.data(),
      cache.iconic_call_storage.size(), &cache.iconic_data);
  g_direct_resolution_slot = -1;
  if (!iconic_ready ||
      !UnrealCallExactStructResultPersistent(
          g_character_data_helpers, g_resolve_character_data, L"CDIn",
          cache.iconic_data, cache.resolve_call_storage.data(),
          cache.resolve_call_storage.size(), &cache.resolved_data))
    return false;

  CharacterName resolved_name;
  std::array<wchar_t, kCharacterNameCapacity> shell = {};
  if (!UnrealReadStructStringField(cache.resolved_data, L"CharacterName",
                                   resolved_name.text.data(),
                                   resolved_name.text.size()) ||
      !SameName(g_configuration.selected_characters[slot],
                resolved_name.text.data()) ||
      !ToWideName(g_configuration.mission_shells[slot], &shell) ||
      !UnrealAssignStructWideStringField(cache.resolved_data,
                                         L"CharacterName", shell.data()))
    return false;

  CharacterName published_name;
  cache.ready = UnrealReadStructStringField(
                    cache.resolved_data, L"CharacterName",
                    published_name.text.data(), published_name.text.size()) &&
                SameName(g_configuration.mission_shells[slot],
                         published_name.text.data());
  if (cache.ready)
    Log(LogLevel::kInfo,
        "[CampaignClass] prepared native character slot=%u selected=%s mission-shell=%s",
        static_cast<unsigned>(slot),
        g_configuration.selected_characters[slot].text.data(),
        g_configuration.mission_shells[slot].text.data());
  return cache.ready;
}

bool PrepareNextResolvedCharacter(ULONGLONG now) {
  for (size_t slot = 0; slot < kSquadSize; ++slot) {
    ResolvedCharacterCache& cache = g_configuration.resolved[slot];
    if (cache.ready) continue;
    if (!EnsureResolvedCharacter(slot)) {
      AbortFlow("selected campaign character data could not be resolved safely");
      return false;
    }
    // Resolve at most one character per game tick and only in the lobby.
    return true;
  }
  g_flow.stage = SelectionStage::kComplete;
  g_flow.close_pending = true;
  g_flow.action_started_at = now;
  g_flow.retry_at = now;
  Log(LogLevel::kInfo,
      "[CampaignClass] all native squad character data prepared; closing selector");
  return true;
}

void WFCDS_UNREAL_EXEC_CALL HookedSpawnAiCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  if (g_ai_definition_substitution_active || g_stopping || !g_hooks_ready ||
      !g_configuration.active || !g_game_thread ||
      GetCurrentThreadId() != g_game_thread || !UnrealObjectValid(context) ||
      !UnrealObjectIsA(context, L"TnAiSquad") ||
      !ContextBelongsToConfiguredCampaign(context)) {
    CallOriginal(g_spawn_ai_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  CharacterName mission_shell;
  const bool readable_name = UnrealReadFrameStringParameter(
      frame, g_spawn_ai_character_name_offset, mission_shell.text.data(),
      mission_shell.text.size());
  const int slot = readable_name
      ? ConfigurationSlotForMissionShell(mission_shell.text.data()) : -1;
  if (slot < 0 || slot >= static_cast<int>(kSquadSize) ||
      !g_configuration.resolved[static_cast<size_t>(slot)].ready) {
    CallOriginal(g_spawn_ai_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  void* coop_info = nullptr;
  void* ai_definition = nullptr;
  std::array<wchar_t, kCharacterNameCapacity> shell_wide = {};
  UnrealStructValueView original;
  alignas(16) std::array<uint8_t, kPersistentCallStorageSize>
      original_storage = {};
  ResolvedCharacterCache& selected =
      g_configuration.resolved[static_cast<size_t>(slot)];
  if (!UnrealReadObjectProperty(context, L"_CoopInfo", &coop_info) ||
      !coop_info || !UnrealObjectIsA(coop_info, L"TnCoopInfo") ||
      !ToWideName(mission_shell, &shell_wide) ||
      !UnrealCallExactStringObjectResult(
          coop_info, g_get_ai_character_definition, L"CharacterName",
          shell_wide.data(), &ai_definition) ||
      !ai_definition ||
      !UnrealObjectIsA(ai_definition, L"TnAiCharacterDefinition") ||
      !UnrealReadStructProperty(ai_definition, L"AiCharacterDataResolved",
                                &original) ||
      original.size != selected.resolved_data.size ||
      original.struct_definition != selected.resolved_data.struct_definition ||
      original.size > original_storage.size()) {
    Log(LogLevel::kWarning,
        "[CampaignClass] pre-spawn AI definition substitution skipped shell=%s",
        mission_shell.text.data());
    CallOriginal(g_spawn_ai_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  std::memcpy(original_storage.data(), original.data, original.size);
  const UnrealStructValueView saved = {
      original_storage.data(), original.size, original.struct_definition};
  // TnScriptPawnFactory passes ChassisType to WorldInfo.Spawn as the pawn
  // archetype. Replace the mission definition synchronously before that read,
  // while retaining every mission-authored AI behavior field outside the
  // character-data struct. InitializeFromTemplate clones the temporary data;
  // the shared CoopInfo definition is restored as soon as SpawnAiCharacter
  // returns. Preserve the shell's authored form (and any later squad override
  // is still applied natively by InitializeFromTemplate).
  if (!UnrealCopyStructByteField(saved, selected.resolved_data,
                                 L"StartingForm") ||
      !UnrealWriteStructProperty(ai_definition, L"AiCharacterDataResolved",
                                 selected.resolved_data)) {
    Log(LogLevel::kWarning,
        "[CampaignClass] pre-spawn AI character data could not be applied shell=%s",
        mission_shell.text.data());
    CallOriginal(g_spawn_ai_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  g_ai_definition_substitution_active = true;
  CallOriginal(g_spawn_ai_character_detour, context, unused_edx, frame,
               result);
  void* spawned_controller = result ? *static_cast<void**>(result) : nullptr;
  const bool restored = UnrealWriteStructProperty(
      ai_definition, L"AiCharacterDataResolved", saved);
  g_ai_definition_substitution_active = false;
  if (!restored) {
    // Retain the resolved cache because the definition now aliases its nested
    // FString/TArray storage. Dropping the cache would turn a recoverable
    // identity leak into dangling mission data.
    Log(LogLevel::kError,
        "[CampaignClass] CRITICAL: shared AI definition restoration failed shell=%s",
        mission_shell.text.data());
    return;
  }
  void* spawned_pawn = nullptr;
  if (!spawned_controller || !UnrealObjectValid(spawned_controller) ||
      !UnrealObjectIsA(spawned_controller, L"TnAiController") ||
      !UnrealReadObjectProperty(spawned_controller, L"Pawn", &spawned_pawn) ||
      !spawned_pawn || !UnrealObjectIsA(spawned_pawn, L"TnAiPawn")) {
    Log(LogLevel::kError,
        "[CampaignClass] native selected-buddy spawn returned no valid controller/pawn shell=%s selected=%s",
        mission_shell.text.data(),
        g_configuration.selected_characters[static_cast<size_t>(slot)]
            .text.data());
    return;
  }
  Log(LogLevel::kInfo,
      "[CampaignClass] spawned selected AI buddy shell=%s selected=%s with native chassis",
      mission_shell.text.data(),
      g_configuration.selected_characters[static_cast<size_t>(slot)]
          .text.data());
}

void WFCDS_UNREAL_EXEC_CALL HookedStartLevel(
    void* context, void* unused_edx, void* frame, void* result) {
  if (g_stopping || !g_hooks_ready || !g_game_thread ||
      GetCurrentThreadId() != g_game_thread || !UnrealObjectValid(context) ||
      !UnrealObjectIsA(context, L"TnGameLobbyGameCampaign")) {
    CallOriginal(g_start_level_detour, context, unused_edx, frame, result);
    return;
  }

  const uint32_t context_index = UnrealObjectIndex(context);
  if (!g_flow.active || g_flow.lobby_game != context ||
      g_flow.lobby_game_index != context_index) {
    bool multiplayer = false;
    if (!UnrealCallBool(context, L"IsMultiplayerGame", &multiplayer)) {
      Log(LogLevel::kWarning,
          "[CampaignClass] native player-count state was unavailable; preserving original StartLevel");
      CallOriginal(g_start_level_detour, context, unused_edx, frame, result);
      return;
    }
    if (multiplayer) {
      // This system creates a local three-AI campaign squad. Never suppress or
      // rewrite a real co-op lobby's character ownership/replication path.
      Log(LogLevel::kInfo,
          "[CampaignClass] online co-op lobby detected; preserving native StartLevel");
      CallOriginal(g_start_level_detour, context, unused_edx, frame, result);
      return;
    }
    BeginFlow(context);
    return;
  }
  if (g_flow.release_travel && !g_flow.travel_forwarded) {
    g_flow.travel_forwarded = true;
    Log(LogLevel::kInfo,
        "[CampaignClass] native selection complete; resuming original StartLevel");
    CallOriginal(g_start_level_detour, context, unused_edx, frame, result);
  }
  // FinalCountdown can request StartLevel again before seamless travel has
  // retired this lobby. Suppress every duplicate after the one forwarded
  // call so statistics and ServerTravel are never issued twice.
}

void WFCDS_UNREAL_EXEC_CALL HookedGetPlayableCharacters(
    void* context, void* unused_edx, void* frame, void* result) {
  CallOriginal(g_get_playable_characters_detour, context, unused_edx, frame,
               result);
  if (g_stopping || !g_hooks_ready || !IsFlowThread() ||
      !g_flow.menu_open || g_flow.stage == SelectionStage::kComplete ||
      g_flow.stage == SelectionStage::kFailed || !result)
    return;

  if (!g_flow.roster_count) {
    std::array<char, 2048> native_roster = {};
    if (!UnrealReadStringValue(result, native_roster.data(),
                               native_roster.size()) ||
        !ParseRoster(native_roster.data())) {
      AbortFlow("native campaign roster was missing or invalid");
      return;
    }
  }

  std::array<wchar_t, 2048> filtered = {};
  if (!BuildFilteredRoster(context, &filtered) ||
      !UnrealAssignWideStringValue(result, filtered.data())) {
    AbortFlow("native CharacterSelect roster filter failed");
  }
}

bool AssignCharacterNameResult(void* result, const CharacterName& name) {
  std::array<wchar_t, kCharacterNameCapacity> wide = {};
  return result && ToWideName(name, &wide) &&
         UnrealAssignWideStringValue(result, wide.data());
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCustomCharacters(
    void* context, void* unused_edx, void* frame, void* result) {
  const CharacterName* selected = CurrentLoadoutCharacter();
  if (!g_stopping && g_hooks_ready && IsFlowThread() &&
      g_flow.menu_open && IsLoadoutStage() && selected &&
      AssignCharacterNameResult(result, *selected))
    return;
  CallOriginal(g_get_custom_characters_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCurrentCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  const CharacterName* selected = CurrentLoadoutCharacter();
  if (!g_stopping && g_hooks_ready && IsFlowThread() &&
      g_flow.menu_open && IsLoadoutStage() && selected &&
      AssignCharacterNameResult(result, *selected))
    return;
  CallOriginal(g_get_current_character_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedClearCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  if (!g_stopping && g_hooks_ready && IsFlowThread() &&
      g_flow.menu_open && IsLoadoutStage()) {
    // Campaign edits are held in this flow. Do not clear or rewrite the
    // player's persistent multiplayer customization buffer when the reused
    // native editor closes. In its normal full-screen host the movie removes
    // a parent container; our direct lobby host has no such container, so use
    // this native close notification to remove only customCharMenu_mc on the
    // next outer tick. PollCampaignLoadout clears the stock parentMenu
    // reference after the child finishes loading, so closeCustomize cannot
    // remove the campaign lobby which hosts our overlay.
    g_flow.customization_close_requested = true;
    g_flow.action_started_at = GetTickCount64();
    g_flow.retry_at = g_flow.action_started_at;
    return;
  }
  CallOriginal(g_clear_character_detour, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL HookedCommitCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  if (g_stopping || !g_hooks_ready || !IsFlowThread() ||
      !g_flow.menu_open || !IsLoadoutStage()) {
    CallOriginal(g_commit_character_detour, context, unused_edx, frame,
                 result);
    return;
  }
  CharacterName name;
  const CharacterName* expected = CurrentLoadoutCharacter();
  CharacterLoadout* loadout = CurrentLoadout();
  if (!expected || !loadout ||
      !UnrealReadFrameStringParameter(frame, g_commit_character_name_offset,
                                      name.text.data(), name.text.size()) ||
      !SameName(*expected, name.text.data())) {
    Log(LogLevel::kWarning,
        "[CampaignClass] rejected a loadout commit for the wrong character");
    return;
  }

  std::array<char, kLoadoutTextCapacity> text = {};
  bool changed = false;
  if (UnrealReadFrameStringParameter(frame, g_commit_weapons_offset,
                                     text.data(), text.size()) && text[0])
    changed = CopyLoadoutText(text.data(), &loadout->weapons) || changed;
  text.fill('\0');
  if (UnrealReadFrameStringParameter(frame, g_commit_vehicle_weapons_offset,
                                     text.data(), text.size()) && text[0])
    changed =
        CopyLoadoutText(text.data(), &loadout->vehicle_weapons) || changed;
  text.fill('\0');
  if (UnrealReadFrameStringParameter(frame, g_commit_abilities_offset,
                                     text.data(), text.size()) && text[0])
    changed = CopyLoadoutText(text.data(), &loadout->abilities) || changed;
  text.fill('\0');
  if (UnrealReadFrameStringParameter(frame, g_commit_skills_offset,
                                     text.data(), text.size()) && text[0])
    changed = CopyLoadoutText(text.data(), &loadout->skills) || changed;
  Log(LogLevel::kInfo,
      "[CampaignClass] campaign loadout commit name=%s changed=%u",
      name.text.data(), changed ? 1u : 0u);
}

void HookedLoadoutGetter(UnrealFunctionExecDetour& detour,
                         uint32_t name_offset,
                         const LoadoutText CharacterLoadout::*field,
                         void* context, void* unused_edx, void* frame,
                         void* result) {
  CallOriginal(detour, context, unused_edx, frame, result);
  if (g_stopping || !g_hooks_ready || !IsFlowThread() ||
      !g_flow.menu_open || !IsLoadoutStage() || !result)
    return;
  CharacterName requested;
  const CharacterName* expected = CurrentLoadoutCharacter();
  CharacterLoadout* loadout = CurrentLoadout();
  if (!expected || !loadout ||
      !UnrealReadFrameStringParameter(frame, name_offset,
                                      requested.text.data(),
                                      requested.text.size()) ||
      !SameName(*expected, requested.text.data()))
    return;
  const LoadoutText& override_value = loadout->*field;
  if (!override_value.text[0]) return;
  std::array<wchar_t, kLoadoutTextCapacity> wide = {};
  if (!ToWideLoadout(override_value, &wide) ||
      !UnrealAssignWideStringValue(result, wide.data()))
    AbortFlow("native loadout getter override failed");
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCharacterWeaponTypes(
    void* context, void* unused_edx, void* frame, void* result) {
  HookedLoadoutGetter(g_get_character_weapon_types_detour,
                      g_get_character_weapon_name_offset,
                      &CharacterLoadout::weapons, context, unused_edx, frame,
                      result);
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCharacterVehicleWeapon(
    void* context, void* unused_edx, void* frame, void* result) {
  HookedLoadoutGetter(g_get_character_vehicle_weapon_detour,
                      g_get_character_vehicle_weapon_name_offset,
                      &CharacterLoadout::vehicle_weapons, context,
                      unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCharacterAbilities(
    void* context, void* unused_edx, void* frame, void* result) {
  HookedLoadoutGetter(g_get_character_abilities_detour,
                      g_get_character_abilities_name_offset,
                      &CharacterLoadout::abilities, context, unused_edx,
                      frame, result);
}

void WFCDS_UNREAL_EXEC_CALL HookedGetCharacterSkills(
    void* context, void* unused_edx, void* frame, void* result) {
  HookedLoadoutGetter(g_get_character_skills_detour,
                      g_get_character_skills_name_offset,
                      &CharacterLoadout::skills, context, unused_edx, frame,
                      result);
}

bool IsCampaignLoadoutMutation() {
  return !g_stopping && g_hooks_ready && IsFlowThread() &&
         g_flow.menu_open && IsLoadoutStage();
}

void WFCDS_UNREAL_EXEC_CALL HookedFindCustomCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  CharacterName requested;
  const CharacterName* expected = CurrentLoadoutCharacter();
  if (!IsCampaignLoadoutMutation() || !expected || !result ||
      !UnrealReadFrameStringParameter(
          frame, g_find_custom_character_name_offset,
          requested.text.data(), requested.text.size()) ||
      !SameName(*expected, requested.text.data())) {
    CallOriginal(g_find_custom_character_detour, context, unused_edx, frame,
                 result);
    return;
  }

  // CustomTransformers.swf is the game's native multiplayer editor. Several
  // binding methods bypass FindCharacter and call FindCustomCharacter
  // directly for the friendly name, chassis, colors, and palettes. Campaign
  // selections are iconic records, so redirect only the active transient
  // campaign character to the equivalent native iconic lookup. Both native
  // functions return the same reflected CharacterData layout.
  std::array<wchar_t, kCharacterNameCapacity> requested_wide = {};
  alignas(16) std::array<uint8_t, kPersistentCallStorageSize> storage = {};
  UnrealStructValueView iconic;
  if (ToWideName(requested, &requested_wide) &&
      UnrealCallExactStringStructResultPersistent(
          context, g_find_iconic_character, L"CharacterName",
          requested_wide.data(), storage.data(), storage.size(), &iconic) &&
      iconic.data &&
      iconic.size == g_find_custom_character_return_size &&
      iconic.struct_definition ==
          g_find_custom_character_return_definition &&
      StructCharacterNameMatches(iconic, requested)) {
    std::memcpy(result, iconic.data, iconic.size);
    return;
  }

  Log(LogLevel::kError,
      "[CampaignClass] native iconic-to-custom editor adapter failed name=%s",
      requested.text.data());
  CallOriginal(g_find_custom_character_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedResetCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  if (IsCampaignLoadoutMutation()) {
    CharacterLoadout* loadout = CurrentLoadout();
    if (loadout) *loadout = {};
    Log(LogLevel::kInfo,
        "[CampaignClass] reset transient campaign loadout to iconic defaults slot=%d",
        LoadoutSlotForStage(g_flow.stage));
    return;
  }
  CallOriginal(g_reset_character_detour, context, unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL HookedWriteCustomizationFile(
    void* context, void* unused_edx, void* frame, void* result) {
  if (IsCampaignLoadoutMutation()) return;
  CallOriginal(g_write_customization_file_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedMarkSkillAsOld(
    void* context, void* unused_edx, void* frame, void* result) {
  if (IsCampaignLoadoutMutation()) return;
  CallOriginal(g_mark_skill_as_old_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedUnlockPrimeMode(
    void* context, void* unused_edx, void* frame, void* result) {
  if (IsCampaignLoadoutMutation()) return;
  CallOriginal(g_unlock_prime_mode_detour, context, unused_edx, frame,
               result);
}

void WFCDS_UNREAL_EXEC_CALL HookedCheckCustomCharacterDataLoaded(
    void* context, void* unused_edx, void* frame, void* result) {
  if (IsCampaignLoadoutMutation()) {
    // The campaign adapter supplies an iconic record directly and never reads
    // the custom-character file. The stock check can otherwise open a storage
    // modal over the reused editor on a new profile and strand this flow.
    return;
  }
  CallOriginal(g_check_custom_character_data_loaded_detour, context,
               unused_edx, frame, result);
}

void WFCDS_UNREAL_EXEC_CALL HookedSelectCharacter(
    void* context, void* unused_edx, void* frame, void* result) {
  // Outside the live pre-travel selector this is WFC's function, not ours.
  // In particular, seamless travel and checkpoint initialization can invoke
  // SelectCharacter while the old lobby UObject is still alive. The previous
  // guard swallowed those calls merely because g_flow.active had not yet been
  // reset, which could leave campaign character state half-initialized.
  if (g_stopping || !g_hooks_ready || !IsFlowThread() ||
      !g_flow.active || g_flow.travel_forwarded) {
    CallOriginal(g_select_character_detour, context, unused_edx, frame,
                 result);
    return;
  }
  // While our selector is active, ignore only re-entrant/double-submit events
  // after a choice has been accepted and calls made while no selection movie
  // is open. Those must not mutate WFC's lobby state behind the transaction.
  if (!g_flow.menu_open || g_flow.selection_locked ||
      g_flow.stage == SelectionStage::kComplete ||
      g_flow.stage == SelectionStage::kFailed) {
    return;
  }

  CharacterName selected;
  if (!UnrealReadFrameStringParameter(frame,
                                      g_select_character_name_offset,
                                      selected.text.data(),
                                      selected.text.size())) {
    Log(LogLevel::kWarning,
        "[CampaignClass] rejected an unreadable native character selection");
    return;
  }
  if (IsLoadoutStage()) {
    const CharacterName* expected = CurrentLoadoutCharacter();
    if (!expected || !SameName(*expected, selected.text.data())) {
      Log(LogLevel::kWarning,
          "[CampaignClass] rejected loadout selection outside the active squad slot name=%s",
          selected.text.data());
      return;
    }
    g_flow.selection_binding = context;
    g_flow.selection_binding_index = UnrealObjectIndex(context);
    Log(LogLevel::kInfo,
        "[CampaignClass] native loadout editor entered name=%s slot=%d",
        selected.text.data(), LoadoutSlotForStage(g_flow.stage));
    return;
  }
  if (!IsRosterMember(selected.text.data())) {
    Log(LogLevel::kWarning,
        "[CampaignClass] rejected a CharacterSelect value outside the native chapter roster");
    return;
  }
  // The native callback itself proves CharacterSelect reached its interactive
  // frame even if the 100 ms visibility poll did not run before a fast accept.
  g_flow.character_select_seen = true;
  g_flow.character_select_missing_since = 0;

  const size_t selected_count = SelectedCountForStage();
  if (g_flow.stage != SelectionStage::kInitialCharacter &&
      IsSelectedMember(selected.text.data(), selected_count)) {
    Log(LogLevel::kWarning,
        "[CampaignClass] rejected duplicate squad selection name=%s",
        selected.text.data());
    return;
  }

  g_flow.selection_locked = true;
  g_flow.selection_binding = context;
  g_flow.selection_binding_index = UnrealObjectIndex(context);
  g_flow.action_started_at = GetTickCount64();
  g_flow.retry_at = g_flow.action_started_at;

  switch (g_flow.stage) {
    case SelectionStage::kSquadMember0:
    case SelectionStage::kSquadMember1:
    case SelectionStage::kSquadMember2: {
      const size_t slot = selected_count;
      g_flow.squad[slot] = selected;
      g_flow.stage = slot == 0 ? SelectionStage::kLoadout0
                   : slot == 1 ? SelectionStage::kLoadout1
                               : SelectionStage::kLoadout2;
      g_flow.close_pending = true;
      Log(LogLevel::kInfo,
          "[CampaignClass] squad slot=%u selected=%s",
          static_cast<unsigned>(slot), selected.text.data());
      return;
    }
    case SelectionStage::kInitialCharacter:
      g_flow.initial = selected;
      if (!CommitConfiguration(selected)) {
        AbortFlow("selected squad could not be mapped to mission character shells");
        return;
      }
      {
        std::array<wchar_t, kCharacterNameCapacity> native_initial = {};
        std::array<wchar_t, kCharacterNameCapacity> selected_initial = {};
        if (!ToWideName(g_flow.native_initial, &native_initial) ||
            !ToWideName(g_flow.initial, &selected_initial) ||
            !UnrealAssignFrameStringParameter(
                frame, g_select_character_name_offset,
                native_initial.data())) {
          g_configuration = {};
          AbortFlow("native initial-character parameter could not be prepared");
          return;
        }
        CallOriginal(g_select_character_detour, context, unused_edx, frame,
                     result);
        if (!UnrealAssignFrameStringParameter(
                frame, g_select_character_name_offset,
                selected_initial.data())) {
          g_configuration = {};
          AbortFlow("native initial-character parameter could not be restored");
          return;
        }
      }
      g_flow.stage = SelectionStage::kVerifyInitialCharacter;
      Log(LogLevel::kInfo,
          "[CampaignClass] native initial-character assignment requested selected=%s mission-shell=%s slot=%u",
          selected.text.data(), g_flow.native_initial.text.data(),
          static_cast<unsigned>(g_configuration.initial_slot));
      return;
    default:
      return;
  }
}

bool CachedUiContextStillCurrent() {
  if (!PublishedObjectValid(g_flow.controller, g_flow.controller_index) ||
      !PublishedObjectValid(g_flow.ui_controller,
                            g_flow.ui_controller_index) ||
      !PublishedObjectValid(g_flow.current_ui, g_flow.current_ui_index) ||
      !PublishedObjectValid(g_flow.game_replication_info,
                            g_flow.game_replication_info_index))
    return false;
  void* world = nullptr;
  void* game = nullptr;
  void* game_replication_info = nullptr;
  void* ui_controller = nullptr;
  void* current_ui = nullptr;
  return UnrealReadObjectProperty(g_flow.controller, L"WorldInfo", &world) &&
         world && UnrealObjectValid(world) &&
         UnrealReadObjectProperty(world, L"Game", &game) &&
         game == g_flow.lobby_game &&
         UnrealReadObjectProperty(world, L"GRI", &game_replication_info) &&
         game_replication_info == g_flow.game_replication_info &&
         UnrealReadObjectProperty(g_flow.controller, L"UIController",
                                  &ui_controller) &&
         ui_controller == g_flow.ui_controller &&
         UnrealReadObjectProperty(g_flow.ui_controller, L"_CurrentUI",
                                  &current_ui) &&
         current_ui == g_flow.current_ui;
}

bool ResolveUiContext(void* game_engine) {
  if (CachedUiContextStillCurrent()) return true;

  ClearFlowObjects();
  if (!UnrealObjectValid(game_engine)) return false;
  UnrealArrayView players;
  void* local_player = nullptr;
  void* controller = nullptr;
  void* world = nullptr;
  void* game = nullptr;
  void* game_replication_info = nullptr;
  void* ui_controller = nullptr;
  void* current_ui = nullptr;
  if (!UnrealReadArrayProperty(game_engine, L"GamePlayers", &players) ||
      !UnrealReadObjectArrayElement(players, 0, &local_player) ||
      !local_player || !UnrealObjectIsA(local_player, L"LocalPlayer") ||
      !UnrealReadObjectProperty(local_player, L"Actor", &controller) ||
      !controller || !UnrealObjectIsA(controller, L"TnPlayerController") ||
      !UnrealReadObjectProperty(controller, L"WorldInfo", &world) ||
      !world || !UnrealObjectIsA(world, L"WorldInfo") ||
      !UnrealReadObjectProperty(world, L"Game", &game) ||
      game != g_flow.lobby_game ||
      !UnrealReadObjectProperty(world, L"GRI", &game_replication_info) ||
      !game_replication_info ||
      !UnrealObjectIsA(game_replication_info, L"TnGameReplicationInfo") ||
      !UnrealReadObjectProperty(controller, L"UIController",
                                &ui_controller) ||
      !ui_controller || !UnrealObjectIsA(ui_controller, L"TnUIController") ||
      !UnrealReadObjectProperty(ui_controller, L"_CurrentUI", &current_ui) ||
      !current_ui || !UnrealObjectIsA(current_ui, L"GFxMovie"))
    return false;
  g_flow.controller = controller;
  g_flow.controller_index = UnrealObjectIndex(controller);
  g_flow.ui_controller = ui_controller;
  g_flow.ui_controller_index = UnrealObjectIndex(ui_controller);
  g_flow.current_ui = current_ui;
  g_flow.current_ui_index = UnrealObjectIndex(current_ui);
  g_flow.game_replication_info = game_replication_info;
  g_flow.game_replication_info_index =
      UnrealObjectIndex(game_replication_info);
  return g_flow.controller_index && g_flow.ui_controller_index &&
         g_flow.current_ui_index && g_flow.game_replication_info_index;
}

bool RestoreHook(UnrealFunctionExecDetour* detour) {
  return !detour || !detour->installed ||
         UnrealRestoreFunctionExecDetour(detour);
}

bool InstallHooks() {
  if (g_hooks_ready) return true;
  UnrealFunctionHandle start_level;
  UnrealFunctionHandle select_character;
  UnrealFunctionHandle get_playable_characters;
  UnrealFunctionHandle is_character_unlocked;
  UnrealFunctionHandle find_iconic_character;
  UnrealFunctionHandle find_custom_character;
  UnrealFunctionHandle get_custom_characters;
  UnrealFunctionHandle get_current_character;
  UnrealFunctionHandle commit_character;
  UnrealFunctionHandle clear_character;
  UnrealFunctionHandle get_character_weapon_types;
  UnrealFunctionHandle get_character_vehicle_weapon;
  UnrealFunctionHandle get_character_abilities;
  UnrealFunctionHandle get_character_skills;
  UnrealFunctionHandle reset_character;
  UnrealFunctionHandle write_customization_file;
  UnrealFunctionHandle mark_skill_as_old;
  UnrealFunctionHandle unlock_prime_mode;
  UnrealFunctionHandle check_custom_character_data_loaded;
  UnrealFunctionHandle resolve_character_data;
  UnrealFunctionHandle spawn_ai_character;
  UnrealFunctionHandle get_ai_character_definition;
  uint32_t character_name_offset = 0;
  uint32_t find_iconic_name_offset = 0;
  uint32_t find_iconic_return_size = 0;
  void* find_iconic_return_definition = nullptr;
  uint32_t find_custom_name_offset = 0;
  uint32_t find_custom_return_size = 0;
  void* find_custom_return_definition = nullptr;
  uint32_t commit_character_name_offset = 0;
  uint32_t commit_weapons_offset = 0;
  uint32_t commit_vehicle_weapons_offset = 0;
  uint32_t commit_abilities_offset = 0;
  uint32_t commit_skills_offset = 0;
  uint32_t get_character_weapon_name_offset = 0;
  uint32_t get_character_vehicle_weapon_name_offset = 0;
  uint32_t get_character_abilities_name_offset = 0;
  uint32_t get_character_skills_name_offset = 0;
  uint32_t resolve_character_input_offset = 0;
  void* resolve_character_input_definition = nullptr;
  uint32_t resolved_character_size = 0;
  void* resolved_character_definition = nullptr;
  uint32_t spawn_ai_character_name_offset = 0;
  // StartLevel is referenced by the FinalCountdown state, but the cooked
  // UFunction itself is owned directly by TnGameLobbyGame (export 20991).
  // Resolving it as a state child makes the entire hook set fail closed and
  // leaves the campaign class flow permanently inactive.
  if (!UnrealResolveExactScriptFunction(
          0, L"TransGame.TnGameLobbyGame", L"StartLevel", &start_level) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"SelectCharacter",
          &select_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"GetPlayableCharacters", &get_playable_characters) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"IsCharacterUnlocked", &is_character_unlocked) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnPlayerController", L"FindIconicCharacter",
          &find_iconic_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnPlayerController", L"FindCustomCharacter",
          &find_custom_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"GetCustomCharacters",
          &get_custom_characters) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"GetCurrentCharacter",
          &get_current_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"CommitCharacter",
          &commit_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"ClearCharacter",
          &clear_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"GetCharacterWeaponTypes", &get_character_weapon_types) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"GetCharacterVehicleWeapon", &get_character_vehicle_weapon) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"GetCharacterAbilities", &get_character_abilities) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"GetCharacterSkills",
          &get_character_skills) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"ResetCharacter",
          &reset_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"WriteCustomizationFile", &write_customization_file) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"MarkSkillAsOld",
          &mark_skill_as_old) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding", L"UnlockPrimeMode",
          &unlock_prime_mode) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterScriptBinding",
          L"CheckCustomCharacterDataLoaded",
          &check_custom_character_data_loaded) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCharacterDataHelpers", L"ResolveCharacterData",
          &resolve_character_data) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnAiSquad", L"SpawnAiCharacter",
          &spawn_ai_character) ||
      !UnrealResolveExactScriptFunction(
          0, L"TransGame.TnCoopInfo", L"GetAiCharacterDefinition",
          &get_ai_character_definition) ||
      !UnrealResolveExactStringParameterOffset(
          select_character, L"CharacterName", &character_name_offset) ||
      !UnrealResolveExactStringParameterOffset(
          find_iconic_character, L"CharacterName",
          &find_iconic_name_offset) ||
      !UnrealResolveExactStructReturnLayout(
          find_iconic_character, &find_iconic_return_size,
          &find_iconic_return_definition) ||
      !UnrealResolveExactStringParameterOffset(
          find_custom_character, L"CharacterName",
          &find_custom_name_offset) ||
      !UnrealResolveExactStructReturnLayout(
          find_custom_character, &find_custom_return_size,
          &find_custom_return_definition) ||
      !UnrealResolveExactStringParameterOffset(
          commit_character, L"CharacterName",
          &commit_character_name_offset) ||
      !UnrealResolveExactStringParameterOffset(
          commit_character, L"WeaponTypes_", &commit_weapons_offset) ||
      !UnrealResolveExactStringParameterOffset(
          commit_character, L"VehicleWeapons",
          &commit_vehicle_weapons_offset) ||
      !UnrealResolveExactStringParameterOffset(
          commit_character, L"Abilities", &commit_abilities_offset) ||
      !UnrealResolveExactStringParameterOffset(
          commit_character, L"Skills", &commit_skills_offset) ||
      !UnrealResolveExactStringParameterOffset(
          get_character_weapon_types, L"CharacterName",
          &get_character_weapon_name_offset) ||
      !UnrealResolveExactStringParameterOffset(
          get_character_vehicle_weapon, L"CharacterName",
          &get_character_vehicle_weapon_name_offset) ||
      !UnrealResolveExactStringParameterOffset(
          get_character_abilities, L"CharacterName",
          &get_character_abilities_name_offset) ||
      !UnrealResolveExactStringParameterOffset(
          get_character_skills, L"CharacterName",
          &get_character_skills_name_offset) ||
      !UnrealResolveExactStructParameterLayout(
          resolve_character_data, L"CDIn", find_iconic_return_size,
          &resolve_character_input_offset,
          &resolve_character_input_definition) ||
      !UnrealResolveExactStructReturnLayout(
          resolve_character_data, &resolved_character_size,
          &resolved_character_definition) ||
      !UnrealResolveExactStringParameterOffset(
          spawn_ai_character, L"CharacterName",
          &spawn_ai_character_name_offset)) {
    Log(LogLevel::kError,
        "[CampaignClass] reflected hook metadata discovery failed");
    return false;
  }
  if (find_custom_return_size != find_iconic_return_size ||
      find_custom_return_definition != find_iconic_return_definition) {
    Log(LogLevel::kError,
        "[CampaignClass] CharacterData return layout mismatch iconic-size=%lu custom-size=%lu iconic-definition=%lu custom-definition=%lu",
        static_cast<unsigned long>(find_iconic_return_size),
        static_cast<unsigned long>(find_custom_return_size),
        static_cast<unsigned long>(UnrealObjectIndex(
            find_iconic_return_definition)),
        static_cast<unsigned long>(UnrealObjectIndex(
            find_custom_return_definition)));
    return false;
  }
  if (resolve_character_input_definition !=
      find_iconic_return_definition) {
    Log(LogLevel::kError,
        "[CampaignClass] ResolveCharacterData input definition mismatch iconic=%lu resolve=%lu",
        static_cast<unsigned long>(UnrealObjectIndex(
            find_iconic_return_definition)),
        static_cast<unsigned long>(UnrealObjectIndex(
            resolve_character_input_definition)));
    return false;
  }

  g_start_level = start_level;
  g_select_character = select_character;
  g_get_playable_characters = get_playable_characters;
  g_is_character_unlocked = is_character_unlocked;
  g_find_iconic_character = find_iconic_character;
  g_find_custom_character = find_custom_character;
  g_select_character_name_offset = character_name_offset;
  g_find_iconic_character_name_offset = find_iconic_name_offset;
  g_find_iconic_character_return_size = find_iconic_return_size;
  g_find_iconic_character_return_definition =
      find_iconic_return_definition;
  g_find_custom_character_name_offset = find_custom_name_offset;
  g_find_custom_character_return_size = find_custom_return_size;
  g_find_custom_character_return_definition =
      find_custom_return_definition;
  g_get_custom_characters = get_custom_characters;
  g_get_current_character = get_current_character;
  g_commit_character = commit_character;
  g_clear_character = clear_character;
  g_get_character_weapon_types = get_character_weapon_types;
  g_get_character_vehicle_weapon = get_character_vehicle_weapon;
  g_get_character_abilities = get_character_abilities;
  g_get_character_skills = get_character_skills;
  g_reset_character = reset_character;
  g_write_customization_file = write_customization_file;
  g_mark_skill_as_old = mark_skill_as_old;
  g_unlock_prime_mode = unlock_prime_mode;
  g_check_custom_character_data_loaded =
      check_custom_character_data_loaded;
  g_resolve_character_data = resolve_character_data;
  g_spawn_ai_character = spawn_ai_character;
  g_get_ai_character_definition = get_ai_character_definition;
  g_commit_character_name_offset = commit_character_name_offset;
  g_commit_weapons_offset = commit_weapons_offset;
  g_commit_vehicle_weapons_offset = commit_vehicle_weapons_offset;
  g_commit_abilities_offset = commit_abilities_offset;
  g_commit_skills_offset = commit_skills_offset;
  g_get_character_weapon_name_offset = get_character_weapon_name_offset;
  g_get_character_vehicle_weapon_name_offset =
      get_character_vehicle_weapon_name_offset;
  g_get_character_abilities_name_offset =
      get_character_abilities_name_offset;
  g_get_character_skills_name_offset = get_character_skills_name_offset;
  g_spawn_ai_character_name_offset = spawn_ai_character_name_offset;
  const auto install_hook = [](const char* name,
                               const UnrealFunctionHandle& function,
                               UnrealFunctionExec replacement,
                               UnrealFunctionExecDetour* detour) {
    if (UnrealInstallFunctionExecDetour(function, replacement, detour))
      return true;
    Log(LogLevel::kError,
        "[CampaignClass] hook installation failed function=%s object=%lu",
        name ? name : "unknown",
        static_cast<unsigned long>(function.function_object_index));
    return false;
  };
  if (!install_hook("GetPlayableCharacters",
          g_get_playable_characters, &HookedGetPlayableCharacters,
          &g_get_playable_characters_detour) ||
      !install_hook("GetCustomCharacters",
          g_get_custom_characters, &HookedGetCustomCharacters,
          &g_get_custom_characters_detour) ||
      !install_hook("GetCurrentCharacter",
          g_get_current_character, &HookedGetCurrentCharacter,
          &g_get_current_character_detour) ||
      !install_hook("CommitCharacter",
          g_commit_character, &HookedCommitCharacter,
          &g_commit_character_detour) ||
      !install_hook("ClearCharacter",
          g_clear_character, &HookedClearCharacter,
          &g_clear_character_detour) ||
      !install_hook("GetCharacterWeaponTypes",
          g_get_character_weapon_types, &HookedGetCharacterWeaponTypes,
          &g_get_character_weapon_types_detour) ||
      !install_hook("GetCharacterVehicleWeapon",
          g_get_character_vehicle_weapon, &HookedGetCharacterVehicleWeapon,
          &g_get_character_vehicle_weapon_detour) ||
      !install_hook("GetCharacterAbilities",
          g_get_character_abilities, &HookedGetCharacterAbilities,
          &g_get_character_abilities_detour) ||
      !install_hook("GetCharacterSkills",
          g_get_character_skills, &HookedGetCharacterSkills,
          &g_get_character_skills_detour) ||
      !install_hook("ResetCharacter",
          g_reset_character, &HookedResetCharacter,
          &g_reset_character_detour) ||
      !install_hook("WriteCustomizationFile",
          g_write_customization_file, &HookedWriteCustomizationFile,
          &g_write_customization_file_detour) ||
      !install_hook("MarkSkillAsOld",
          g_mark_skill_as_old, &HookedMarkSkillAsOld,
          &g_mark_skill_as_old_detour) ||
      !install_hook("UnlockPrimeMode",
          g_unlock_prime_mode, &HookedUnlockPrimeMode,
          &g_unlock_prime_mode_detour) ||
      !install_hook("CheckCustomCharacterDataLoaded",
          g_check_custom_character_data_loaded,
          &HookedCheckCustomCharacterDataLoaded,
          &g_check_custom_character_data_loaded_detour) ||
      !install_hook("SelectCharacter",
          g_select_character, &HookedSelectCharacter,
          &g_select_character_detour) ||
      !install_hook("FindIconicCharacter",
          g_find_iconic_character, &HookedFindIconicCharacter,
          &g_find_iconic_character_detour) ||
      !install_hook("FindCustomCharacter",
          g_find_custom_character, &HookedFindCustomCharacter,
          &g_find_custom_character_detour) ||
      !install_hook("SpawnAiCharacter",
          g_spawn_ai_character, &HookedSpawnAiCharacter,
          &g_spawn_ai_character_detour) ||
      !install_hook("StartLevel",
          g_start_level, &HookedStartLevel, &g_start_level_detour)) {
    bool restored_all = true;
    restored_all = RestoreHook(&g_start_level_detour) && restored_all;
    restored_all = RestoreHook(&g_spawn_ai_character_detour) && restored_all;
    restored_all = RestoreHook(&g_find_custom_character_detour) && restored_all;
    restored_all = RestoreHook(&g_find_iconic_character_detour) && restored_all;
    restored_all = RestoreHook(&g_select_character_detour) && restored_all;
    restored_all =
        RestoreHook(&g_check_custom_character_data_loaded_detour) &&
        restored_all;
    restored_all = RestoreHook(&g_unlock_prime_mode_detour) && restored_all;
    restored_all = RestoreHook(&g_mark_skill_as_old_detour) && restored_all;
    restored_all =
        RestoreHook(&g_write_customization_file_detour) && restored_all;
    restored_all = RestoreHook(&g_reset_character_detour) && restored_all;
    restored_all = RestoreHook(&g_get_character_skills_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_character_abilities_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_character_vehicle_weapon_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_character_weapon_types_detour) && restored_all;
    restored_all = RestoreHook(&g_clear_character_detour) && restored_all;
    restored_all = RestoreHook(&g_commit_character_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_current_character_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_custom_characters_detour) && restored_all;
    restored_all =
        RestoreHook(&g_get_playable_characters_detour) && restored_all;
    if (!restored_all) {
      // At least one executor still targets this module. Keep every detour and
      // original-executor record intact for shutdown, and turn the remaining
      // hooks into permanent pass-throughs. Retrying installation over an
      // unknown partial hook set could otherwise create a recursive executor
      // chain or strand StartLevel behind a half-installed selector.
      g_stopping = true;
      Log(LogLevel::kError,
          "[CampaignClass] CRITICAL: partial hook installation could not be rolled back; adapters disabled in pass-through mode");
      return false;
    }
    g_start_level = {};
    g_select_character = {};
    g_get_playable_characters = {};
    g_is_character_unlocked = {};
    g_find_iconic_character = {};
    g_find_custom_character = {};
    g_get_custom_characters = {};
    g_get_current_character = {};
    g_commit_character = {};
    g_clear_character = {};
    g_get_character_weapon_types = {};
    g_get_character_vehicle_weapon = {};
    g_get_character_abilities = {};
    g_get_character_skills = {};
    g_reset_character = {};
    g_write_customization_file = {};
    g_mark_skill_as_old = {};
    g_unlock_prime_mode = {};
    g_check_custom_character_data_loaded = {};
    g_resolve_character_data = {};
    g_spawn_ai_character = {};
    g_get_ai_character_definition = {};
    g_select_character_name_offset = 0;
    g_find_iconic_character_name_offset = 0;
    g_find_iconic_character_return_size = 0;
    g_find_iconic_character_return_definition = nullptr;
    g_find_custom_character_name_offset = 0;
    g_find_custom_character_return_size = 0;
    g_find_custom_character_return_definition = nullptr;
    g_commit_character_name_offset = 0;
    g_commit_weapons_offset = 0;
    g_commit_vehicle_weapons_offset = 0;
    g_commit_abilities_offset = 0;
    g_commit_skills_offset = 0;
    g_get_character_weapon_name_offset = 0;
    g_get_character_vehicle_weapon_name_offset = 0;
    g_get_character_abilities_name_offset = 0;
    g_get_character_skills_name_offset = 0;
    g_spawn_ai_character_name_offset = 0;
    return false;
  }
  g_hooks_ready = true;
  Log(LogLevel::kInfo,
      "[CampaignClass] native campaign selection and multiplayer-loadout adapters installed");
  return true;
}

bool InvokeCurrentUi(const wchar_t* method) {
  return method && PublishedObjectValid(g_flow.ui_controller,
                                        g_flow.ui_controller_index) &&
         UnrealCallVoidString(g_flow.ui_controller, L"InvokeOnCurrentUI",
                              L"MethodName", method);
}

bool CloseCharacterSelect() {
  if (g_flow.menu_kind == SelectionMenuKind::kLoadout)
    return InvokeCurrentUi(L"_root.customCharMenu_mc.removeMovieClip");
  return InvokeCurrentUi(L"_root.charSelect_mc.removeMovieClip");
}

bool ReadSelectionMenuVisible(bool* visible) {
  if (!visible || !PublishedObjectValid(g_flow.current_ui,
                                        g_flow.current_ui_index))
    return false;
  const wchar_t* path = g_flow.menu_kind == SelectionMenuKind::kLoadout
      ? L"_root.customCharMenu_mc._visible"
      : L"_root.charSelect_mc._visible";
  return UnrealCallBoolString(g_flow.current_ui, L"GetVariableBool", L"Path",
                              path, visible);
}

bool ConfigureCampaignLoadoutUi() {
  if (!PublishedObjectValid(g_flow.current_ui, g_flow.current_ui_index))
    return false;
  const std::array<const wchar_t*, 5> restricted_enabled = {{
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.chassisButtonA_mc.enabled",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.chassisButtonD_mc.enabled",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.profileButton_mc.enabled",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.renameButton_mc.enabled",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.resetButton_mc.enabled",
  }};
  const std::array<const wchar_t*, 5> restricted_display_only = {{
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.chassisButtonA_mc.displayOnly",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.chassisButtonD_mc.displayOnly",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.profileButton_mc.displayOnly",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.renameButton_mc.displayOnly",
      L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.resetButton_mc.displayOnly",
  }};
  bool configured = UnrealSetVariableUndefined(
      g_flow.current_ui, L"_root.customCharMenu_mc.parentMenu");
  configured =
      UnrealCallVoidStringBool(
          g_flow.current_ui, L"SetVariableBool", L"Path",
          L"_root.customCharMenu_mc.selectOnly", L"B", false) &&
      configured;
  configured =
      UnrealCallVoidStringBool(
          g_flow.current_ui, L"SetVariableBool", L"Path",
          L"_root.customCharMenu_mc.backout", L"B", true) &&
      configured;
  for (const wchar_t* path : restricted_enabled)
    configured = UnrealCallVoidStringBool(
                     g_flow.current_ui, L"SetVariableBool", L"Path", path,
                     L"B", false) &&
                 configured;
  // displayOnly is not changed by CustomTransformers' asynchronous preview
  // callback. Set it once after the child controls exist so HmMenu continues
  // to skip these invisible controls even during the callback's short
  // enabled=true window.
  if (!g_flow.customization_seen)
    for (const wchar_t* path : restricted_display_only)
      configured = UnrealCallVoidStringBool(
                       g_flow.current_ui, L"SetVariableBool", L"Path", path,
                       L"B", true) &&
                   configured;
  // The stock overview points Down from the third upgrade to an invisible
  // chassis button, while the first weapon points Up through the entire
  // invisible profile/name/reset chain. Named campaign characters keep their
  // authored chassis, so make the visible controls a closed navigation loop.
  configured = UnrealCallVoidStringString(
                   g_flow.current_ui, L"SetVariableString", L"Path",
                   L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.upgrade3_mc.focusDown",
                   L"S", L"weaponSlot1_mc") &&
               configured;
  configured = UnrealCallVoidStringString(
                   g_flow.current_ui, L"SetVariableString", L"Path",
                   L"_root.customCharMenu_mc.characterOverview_mc.charOverviewMenu_mc.weaponSlot1_mc.focusUp",
                   L"S", L"upgrade3_mc") &&
               configured;
  return configured;
}

bool RestoreCampaignIconicMode() {
  // The bool shares a packed UE3 property word. A native movie callback can
  // update another bit in that word while the compare/exchange observes it,
  // so retry from a freshly read word instead of treating one CAS miss as a
  // permanent campaign-mode failure.
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    bool iconic_mode = false;
    if (!PublishedObjectValid(g_flow.game_replication_info,
                              g_flow.game_replication_info_index) ||
        !UnrealReadBoolProperty(g_flow.game_replication_info,
                                L"_bIconicMode", &iconic_mode))
      return false;
    if (iconic_mode) return true;
    UnrealWriteBoolPropertyExpected(g_flow.game_replication_info,
                                    L"_bIconicMode", false, true);
  }
  bool iconic_mode = false;
  return UnrealReadBoolProperty(g_flow.game_replication_info,
                                L"_bIconicMode", &iconic_mode) &&
         iconic_mode;
}

bool OpenCharacterSelect(ULONGLONG now) {
  // The movie queries GetPlayableCharacters synchronously from inside this
  // Invoke. Publish the transaction first so its initial data pass sees the
  // campaign roster adapter.
  g_flow.menu_open = true;
  g_flow.menu_kind = SelectionMenuKind::kCharacterSelect;
  g_flow.character_select_seen = false;
  g_flow.character_select_missing_since = 0;
  g_flow.open_pending = false;
  g_flow.selection_locked = false;
  g_flow.prompt_pending = true;
  g_flow.prompt_attempts = 0;
  g_flow.prompt_at = now + kPromptDelayMs;
  if (!InvokeCurrentUi(L"_global.HmUtility.OpenCharacterSelect")) {
    g_flow.menu_open = false;
    g_flow.menu_kind = SelectionMenuKind::kNone;
    g_flow.open_pending = true;
    g_flow.prompt_pending = false;
    return false;
  }
  // GetPlayableCharacters is called synchronously by the loaded movie and may
  // abort the transaction if WFC did not publish a usable chapter roster.
  if (g_flow.stage == SelectionStage::kFailed) {
    CloseCharacterSelect();
    return false;
  }
  g_flow.action_started_at = now;
  g_flow.retry_at = now + kUiRetryMs;
  Log(LogLevel::kInfo,
      "[CampaignClass] opened native CharacterSelect stage=%u",
      static_cast<unsigned>(g_flow.stage));
  return true;
}

bool OpenCampaignLoadout(ULONGLONG now) {
  if (!PublishedObjectValid(g_flow.game_replication_info,
                            g_flow.game_replication_info_index))
    return false;
  bool iconic_mode = false;
  if (!UnrealReadBoolProperty(g_flow.game_replication_info,
                              L"_bIconicMode", &iconic_mode) ||
      !iconic_mode ||
      !UnrealWriteBoolPropertyExpected(g_flow.game_replication_info,
                                       L"_bIconicMode", true, false))
    return false;

  // CustomTransformers.swf performs its first custom-character and preview
  // lookups synchronously during the Invoke call. Arm the transient campaign
  // adapters before opening it, while the native iconic-mode gate is flipped
  // to the multiplayer editor.
  g_flow.menu_open = true;
  g_flow.menu_kind = SelectionMenuKind::kLoadout;
  g_flow.customization_seen = false;
  g_flow.customization_close_requested = false;
  g_flow.open_pending = false;
  g_flow.selection_locked = false;
  g_flow.prompt_pending = false;
  const bool invoked =
      InvokeCurrentUi(L"_global.HmUtility.OpenCharacterSelect");
  // OpenCharacterSelect creates customCharMenu_mc before MovieClipLoader
  // begins loading CustomTransformers.swf. Seed these values on that empty
  // host immediately so frame 1 never applies the movie's select-only
  // defaults. PollCampaignLoadout reasserts them after load as a fallback for
  // Scaleform builds which delay variable publication.
  if (invoked) {
    const bool seeded =
        UnrealCallVoidStringBool(
            g_flow.current_ui, L"SetVariableBool", L"Path",
            L"_root.customCharMenu_mc.selectOnly", L"B", false) &&
        UnrealCallVoidStringBool(
            g_flow.current_ui, L"SetVariableBool", L"Path",
            L"_root.customCharMenu_mc.backout", L"B", true);
    if (!seeded)
      Log(LogLevel::kInfo,
          "[CampaignClass] native loadout host variables will be applied after asynchronous movie load");
  }
  const bool restored = RestoreCampaignIconicMode();
  if (!restored) {
    CloseCharacterSelect();
    AbortFlow(
        "campaign iconic-mode flag could not be restored after loadout launch");
    return false;
  }
  if (g_flow.stage == SelectionStage::kFailed) {
    CloseCharacterSelect();
    return false;
  }
  if (!invoked) {
    g_flow.menu_open = false;
    g_flow.menu_kind = SelectionMenuKind::kNone;
    g_flow.open_pending = true;
    g_flow.customization_seen = false;
    g_flow.customization_close_requested = false;
    return false;
  }
  g_flow.action_started_at = now;
  g_flow.retry_at = now + kUiRetryMs;
  Log(LogLevel::kInfo,
      "[CampaignClass] opened native multiplayer loadout editor for campaign slot=%d",
      LoadoutSlotForStage(g_flow.stage));
  return true;
}

bool FinishCampaignLoadout(ULONGLONG now) {
  const SelectionStage next = StageAfterLoadout(g_flow.stage);
  if (next == SelectionStage::kFailed) {
    AbortFlow("invalid campaign loadout stage transition");
    return false;
  }
  Log(LogLevel::kInfo,
      "[CampaignClass] campaign loadout editor closed slot=%d",
      LoadoutSlotForStage(g_flow.stage));
  g_flow.stage = next;
  g_flow.menu_open = false;
  g_flow.menu_kind = SelectionMenuKind::kNone;
  g_flow.customization_seen = false;
  g_flow.customization_close_requested = false;
  g_flow.open_pending = true;
  g_flow.action_started_at = now;
  g_flow.retry_at = now + kUiRetryMs;
  return true;
}

bool PollCharacterSelect(ULONGLONG now) {
  if (now < g_flow.retry_at) return true;
  g_flow.retry_at = now + kUiRetryMs;

  bool visible = false;
  const bool read =
      PublishedObjectValid(g_flow.current_ui, g_flow.current_ui_index) &&
      UnrealCallBoolString(g_flow.current_ui, L"GetVariableBool", L"Path",
                           L"_root.charSelect_mc._visible", &visible);
  if (read && visible) {
    g_flow.character_select_seen = true;
    g_flow.character_select_missing_since = 0;
    return true;
  }

  if (!g_flow.character_select_seen) {
    if (now - g_flow.action_started_at >= kUiTimeoutMs) {
      AbortFlow("native CharacterSelect did not appear");
      return false;
    }
    return true;
  }

  // CharacterSelect has no native cancellation callback when it is hosted
  // directly in the campaign lobby. Pressing Back can therefore remove the
  // movie without invoking SelectCharacter. Give Scaleform a short grace
  // period for transient publication gaps, then release the untouched native
  // campaign travel instead of leaving FinalCountdown paused forever.
  if (!g_flow.character_select_missing_since)
    g_flow.character_select_missing_since = now;
  if (now - g_flow.character_select_missing_since >= kUiDisappearGraceMs) {
    AbortFlow("native CharacterSelect was cancelled before selection completed");
    return false;
  }
  return true;
}

bool PollCampaignLoadout(ULONGLONG now) {
  if (now < g_flow.retry_at) return true;
  g_flow.retry_at = now + kUiRetryMs;
  bool visible = false;
  const bool read = ReadSelectionMenuVisible(&visible);
  if (g_flow.customization_close_requested) {
    // closeCustomize calls ClearCharacter and can remove its host movie before
    // the next native tick. ClearCharacter is reached only from that native
    // close path, so an unreadable/hidden movie is already closed. If the clip
    // is still visibly published, require its explicit removal before opening
    // the next selector so two input owners can never overlap.
    if (read && visible) {
      if (CloseCharacterSelect()) return FinishCampaignLoadout(now);
      if (now - g_flow.action_started_at >= kUiTimeoutMs) {
        AbortFlow("native campaign loadout editor could not be closed");
        return false;
      }
      return true;
    }
    return FinishCampaignLoadout(now);
  }
  if (!read) {
    if (now - g_flow.action_started_at >= kUiTimeoutMs) {
      AbortFlow("native campaign loadout editor state could not be read");
      return false;
    }
    return true;
  }
  if (visible) {
    // Reassert the restrictions while this menu is open. Its asynchronous
    // preview callback enables chassis controls after frame 1, so a one-shot
    // write leaves an input race where an invisible unsupported control can
    // be focused.
    const bool configured = ConfigureCampaignLoadoutUi();
    if (!configured) {
      if (!g_flow.customization_seen &&
          now - g_flow.action_started_at >= kUiTimeoutMs) {
        AbortFlow("native campaign loadout editor could not be configured");
        return false;
      }
      return true;
    }
    if (!g_flow.customization_seen) {
      g_flow.customization_seen = true;
    }
    return true;
  }
  if (!g_flow.customization_seen) {
    if (now - g_flow.action_started_at >= kUiTimeoutMs)
      AbortFlow("native campaign loadout editor did not appear");
    return true;
  }

  return FinishCampaignLoadout(now);
}

bool ApplyStagePrompt() {
  return PublishedObjectValid(g_flow.current_ui, g_flow.current_ui_index) &&
         UnrealCallVoidStringString(
             g_flow.current_ui, L"SetVariableString", L"Path",
             L"_root.charSelect_mc.chooseChar_txt.text", L"S",
             StagePrompt());
}

bool VerifyInitialCharacter(ULONGLONG now) {
  if (now < g_flow.retry_at) return true;
  g_flow.retry_at = now + kVerifyRetryMs;
  if (!PublishedObjectValid(g_flow.selection_binding,
                            g_flow.selection_binding_index)) {
    AbortFlow("CharacterSelect binding expired during native assignment");
    return false;
  }
  CharacterName current;
  if (UnrealCallString(g_flow.selection_binding, L"GetCurrentCharacter",
                       current.text.data(), current.text.size()) &&
      SameName(g_flow.native_initial, current.text.data())) {
    g_flow.stage = SelectionStage::kPrepareConfiguration;
    g_flow.action_started_at = now;
    g_flow.retry_at = now;
    Log(LogLevel::kInfo,
        "[CampaignClass] native initial-character assignment verified mission-shell=%s selected=%s; preparing squad data",
        current.text.data(), g_flow.initial.text.data());
    return true;
  }
  if (now - g_flow.action_started_at >= kUiTimeoutMs) {
    // CharacterSelect removes itself after calling SelectCharacter. Reopen a
    // fresh native movie for retry; the old implementation re-enabled a clip
    // which no longer existed and permanently stranded FinalCountdown.
    g_flow.stage = SelectionStage::kInitialCharacter;
    g_flow.selection_locked = false;
    g_configuration = {};
    g_flow.initial = {};
    g_flow.menu_open = false;
    g_flow.menu_kind = SelectionMenuKind::kNone;
    g_flow.open_pending = true;
    g_flow.character_select_seen = false;
    g_flow.character_select_missing_since = 0;
    g_flow.prompt_pending = true;
    g_flow.prompt_attempts = 0;
    g_flow.prompt_at = now;
    Log(LogLevel::kWarning,
        "[CampaignClass] native character assignment did not commit; selector re-enabled");
  }
  return true;
}

bool ResumeCampaignTravel(ULONGLONG now) {
  if (!g_flow.release_travel || g_flow.travel_forwarded) return true;
  if (now < g_flow.retry_at) return true;
  g_flow.retry_at = now + kUiRetryMs;
  if (!PublishedObjectValid(g_flow.lobby_game,
                            g_flow.lobby_game_index) ||
      !UnrealExactScriptFunctionValid(g_start_level)) {
    return false;
  }

  // StartLevel has no parameters, so it is safe to invoke again from the
  // outer game tick. The executor detour sees release_travel, forwards this
  // invocation to the saved original executor exactly once, and marks the
  // flow as forwarded before the original can initiate seamless travel.
  // This explicit re-entry is required: FinalCountdown normally calls
  // StartLevel only once, so waiting for a second native call can deadlock
  // campaign travel behind the selector.
  if (!UnrealCallExactVoid(g_flow.lobby_game, g_start_level)) {
    Log(LogLevel::kWarning,
        "[CampaignClass] original StartLevel resume call was rejected; retrying");
    return false;
  }
  return g_flow.travel_forwarded;
}

}  // namespace

bool TickCampaignClassSystem(void* game_engine, DWORD game_thread,
                             ULONGLONG now, bool* did_work) {
  if (did_work) *did_work = false;
  if (g_stopping || !game_thread || GetCurrentThreadId() != game_thread ||
      !UnrealBridgeReady())
    return false;
  g_game_thread = game_thread;

  if (!g_hooks_ready) {
    if (now < g_hook_retry_at) return true;
    g_hook_retry_at = now + kHookRetryMs;
    const bool installed = InstallHooks();
    if (installed && did_work) *did_work = true;
    // A package can become available later in startup, so keep a bounded
    // retry. A failed optional hook probe is not a fatal bridge failure and
    // must not emit an error or consume a snapshot slice every game tick.
    return installed || !g_stopping;
  }
  if (ObserveConfigurationWorld(game_engine, now) && did_work)
    *did_work = true;
  if (!g_flow.active) return true;
  if (!PublishedObjectValid(g_flow.lobby_game, g_flow.lobby_game_index)) {
    ResetFlow();
    return true;
  }
  if (g_flow.travel_forwarded) return true;

  if (g_flow.release_travel) {
    if (did_work) *did_work = true;
    ResumeCampaignTravel(now);
    return true;
  }

  if (!ResolveUiContext(game_engine)) {
    if (now - g_flow.action_started_at >= kUiTimeoutMs)
      AbortFlow("campaign lobby UI could not be resolved");
    return true;
  }

  if (!g_catalog_ready && !g_catalog_scan_exhausted) {
    if (did_work) *did_work = true;
    bool catalog_resolved = false;
    if (g_catalog_scan_cursor == 0) {
      // The live player profile inherits the same native iconic presets and
      // is already reachable from the resolved lobby controller. Prefer this
      // direct root over walking the global UObject table under Box64.
      void* profile = nullptr;
      catalog_resolved =
          UnrealReadObjectProperty(g_flow.controller, L"ProfileSettings",
                                   &profile) &&
          ReadIconicCatalog(profile);
    }
    if (!catalog_resolved) ScanIconicCatalogSlice();
    if (!g_catalog_ready && !g_catalog_scan_exhausted) return true;
    // Catalog discovery can legitimately span multiple yielded slices. Do
    // not charge that time against the first native movie-open attempt.
    g_flow.action_started_at = now;
    g_flow.retry_at = now;
  }

  if (g_flow.stage == SelectionStage::kVerifyInitialCharacter) {
    if (did_work) *did_work = true;
    return VerifyInitialCharacter(now);
  }

  if (g_flow.stage == SelectionStage::kPrepareConfiguration) {
    if (did_work) *did_work = true;
    return PrepareNextResolvedCharacter(now);
  }

  if (g_flow.close_pending) {
    if (now < g_flow.retry_at) return true;
    if (did_work) *did_work = true;
    bool visible = false;
    const bool read = ReadSelectionMenuVisible(&visible);
    // CharacterSelect invokes SelectCharacter and then removes itself in the
    // same ActionScript callback. A locked selection is therefore definitive
    // close progress even when GetVariableBool can no longer resolve the
    // destroyed clip. Still remove a clip which remains visibly published.
    const bool accepted_character_select =
        g_flow.menu_kind == SelectionMenuKind::kCharacterSelect &&
        g_flow.selection_locked;
    const bool already_closed =
        accepted_character_select && (!read || !visible);
    if (!already_closed && !CloseCharacterSelect()) {
      g_flow.retry_at = now + kUiRetryMs;
      if (now - g_flow.action_started_at >= kUiTimeoutMs)
        AbortFlow("native CharacterSelect could not be closed");
      return true;
    }
    g_flow.close_pending = false;
    g_flow.menu_open = false;
    g_flow.menu_kind = SelectionMenuKind::kNone;
    g_flow.prompt_pending = false;
    g_flow.action_started_at = now;
    g_flow.retry_at = now + kUiRetryMs;
    if (g_flow.stage == SelectionStage::kComplete ||
        g_flow.stage == SelectionStage::kFailed) {
      g_flow.release_travel = true;
    } else {
      g_flow.open_pending = true;
    }
    return true;
  }

  if (g_flow.open_pending) {
    if (now < g_flow.retry_at) return true;
    if (did_work) *did_work = true;
    const bool opened = IsLoadoutStage()
        ? OpenCampaignLoadout(now) : OpenCharacterSelect(now);
    if (!opened) {
      g_flow.retry_at = now + kUiRetryMs;
      if (now - g_flow.action_started_at >= kUiTimeoutMs)
        AbortFlow("native CharacterSelect could not be opened");
    }
    return true;
  }

  if (g_flow.menu_open &&
      g_flow.menu_kind == SelectionMenuKind::kLoadout) {
    if (did_work) *did_work = true;
    return PollCampaignLoadout(now);
  }

  if (g_flow.menu_open &&
      g_flow.menu_kind == SelectionMenuKind::kCharacterSelect) {
    if (did_work) *did_work = true;
    if (!PollCharacterSelect(now)) return false;
  }

  if (g_flow.prompt_pending && now >= g_flow.prompt_at) {
    if (did_work) *did_work = true;
    ApplyStagePrompt();
    // MovieClipLoader is asynchronous and is noticeably slower through
    // Box64. A small fixed retry budget lets the child text field appear
    // without creating a permanent UI polling loop.
    ++g_flow.prompt_attempts;
    if (g_flow.prompt_attempts >= kPromptAttempts) {
      g_flow.prompt_pending = false;
    } else {
      g_flow.prompt_at = now + kPromptRetryMs;
    }
  }
  return true;
}

bool CampaignClassSelectionActive() {
  return g_flow.active && !g_flow.travel_forwarded;
}

bool CampaignClassSelectedCharacterName(const char* mission_shell,
                                        char* output, size_t capacity) {
  if (!mission_shell || !*mission_shell || !output || !capacity ||
      !g_configuration.active)
    return false;
  const int slot = ConfigurationSlotForMissionShell(mission_shell);
  if (slot < 0 || slot >= static_cast<int>(kSquadSize)) return false;
  const char* selected =
      g_configuration.selected_characters[static_cast<size_t>(slot)]
          .text.data();
  const size_t length = std::strlen(selected);
  if (!length || length >= capacity) return false;
  std::memcpy(output, selected, length + 1);
  return true;
}

bool ShutdownCampaignClassSystem() {
  g_stopping = true;
  ResetFlow();
  bool restored = true;
  restored = RestoreHook(&g_start_level_detour) && restored;
  restored = RestoreHook(&g_spawn_ai_character_detour) && restored;
  restored = RestoreHook(&g_find_custom_character_detour) && restored;
  restored = RestoreHook(&g_find_iconic_character_detour) && restored;
  restored = RestoreHook(&g_select_character_detour) && restored;
  restored = RestoreHook(&g_check_custom_character_data_loaded_detour) &&
             restored;
  restored = RestoreHook(&g_unlock_prime_mode_detour) && restored;
  restored = RestoreHook(&g_mark_skill_as_old_detour) && restored;
  restored = RestoreHook(&g_write_customization_file_detour) && restored;
  restored = RestoreHook(&g_reset_character_detour) && restored;
  restored = RestoreHook(&g_get_character_skills_detour) && restored;
  restored = RestoreHook(&g_get_character_abilities_detour) && restored;
  restored = RestoreHook(&g_get_character_vehicle_weapon_detour) && restored;
  restored = RestoreHook(&g_get_character_weapon_types_detour) && restored;
  restored = RestoreHook(&g_clear_character_detour) && restored;
  restored = RestoreHook(&g_commit_character_detour) && restored;
  restored = RestoreHook(&g_get_current_character_detour) && restored;
  restored = RestoreHook(&g_get_custom_characters_detour) && restored;
  restored = RestoreHook(&g_get_playable_characters_detour) && restored;
  g_start_level = {};
  g_select_character = {};
  g_get_playable_characters = {};
  g_is_character_unlocked = {};
  g_find_iconic_character = {};
  g_find_custom_character = {};
  g_get_custom_characters = {};
  g_get_current_character = {};
  g_commit_character = {};
  g_clear_character = {};
  g_get_character_weapon_types = {};
  g_get_character_vehicle_weapon = {};
  g_get_character_abilities = {};
  g_get_character_skills = {};
  g_reset_character = {};
  g_write_customization_file = {};
  g_mark_skill_as_old = {};
  g_unlock_prime_mode = {};
  g_check_custom_character_data_loaded = {};
  g_resolve_character_data = {};
  g_spawn_ai_character = {};
  g_get_ai_character_definition = {};
  g_select_character_name_offset = 0;
  g_find_iconic_character_name_offset = 0;
  g_find_iconic_character_return_size = 0;
  g_find_iconic_character_return_definition = nullptr;
  g_find_custom_character_name_offset = 0;
  g_find_custom_character_return_size = 0;
  g_find_custom_character_return_definition = nullptr;
  g_commit_character_name_offset = 0;
  g_commit_weapons_offset = 0;
  g_commit_vehicle_weapons_offset = 0;
  g_commit_abilities_offset = 0;
  g_commit_skills_offset = 0;
  g_get_character_weapon_name_offset = 0;
  g_get_character_vehicle_weapon_name_offset = 0;
  g_get_character_abilities_name_offset = 0;
  g_get_character_skills_name_offset = 0;
  g_spawn_ai_character_name_offset = 0;
  g_character_data_helpers = nullptr;
  g_character_data_helpers_index = 0;
  g_direct_resolution_slot = -1;
  g_ai_definition_substitution_active = false;
  g_configuration = {};
  g_configuration_observe_at = 0;
  g_hooks_ready = false;
  g_game_thread = 0;
  return restored;
}

}  // namespace wfcds
