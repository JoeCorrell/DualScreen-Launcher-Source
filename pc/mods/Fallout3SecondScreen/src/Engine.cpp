#include "Engine.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "Log.h"
#include "LocalMapCapture.h"
#include "Panel.h"
#include "Perf.h"
#include "Signatures.h"
#include "Snapshot.h"
#include "StateStore.h"

namespace fo3ds {
namespace {

constexpr UINT_PTR kGameTimerId = 0xF03D;
constexpr UINT kGameTimerPeriodMs = 100;
constexpr uint64_t kInventoryScanPeriodMs = 1000;
constexpr uint64_t kDataScanPeriodMs = 5000;
constexpr uint64_t kNoteScanPeriodMs = 1000;
constexpr uint64_t kMapScanPeriodMs = 500;
constexpr uint64_t kRadioScanPeriodMs = 1000;
constexpr uint64_t kRadioBackgroundScanPeriodMs = 5000;
constexpr uint64_t kEffectScanPeriodMs = 1000;
constexpr uint64_t kMarkerScanPeriodMs = 2000;
constexpr uint64_t kConditionScanPeriodMs = 250;
constexpr uint64_t kAttributeScanPeriodMs = 1000;
constexpr uint64_t kGeneralStatsScanPeriodMs = 1000;
constexpr uint32_t kMaxInventoryNodes = 8192;
constexpr uint32_t kMaxDataNodes = 8192;
constexpr int32_t kMaxInventoryStackCount = 1000000;

struct CommandDescriptor {
  const char* long_name;
  const char* short_name;
  uint16_t opcode;
  uint16_t padding;
  const char* help;
  uint16_t needs_parent;
  uint16_t parameter_count;
  const void* parameters;
  const void* execute;
  const void* parse;
  const void* eval;
  uint32_t flags;
};

struct VitalsProbe {
  Lifecycle lifecycle = Lifecycle::kBoot;
  uint32_t player_form_id = 0;
  uint32_t cell_form_id = 0;
  int32_t level = -1;
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
  float special[kSpecialCount] = {};
  float skills[kSkillCount] = {};
  float limbs[kLimbCount] = {};
  int32_t misc_stats[kMiscStatCount] = {};
  char player_name[96] = {};
  char location[128] = {};
};

struct MapProbe {
  uint32_t world_form_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float heading = 0.0f;
  float min_x = 0.0f;
  float max_y = 0.0f;
  float max_x = 0.0f;
  float min_y = 0.0f;
  uint32_t usable_width = 0;
  uint32_t usable_height = 0;
  char texture_path[192] = {};
  bool valid = false;
  bool world_valid = false;
  bool interior = false;
  uint8_t anchor_source = 0;
};

enum MapAnchorSource : uint8_t {
  kMapAnchorPlayer = 0,
  kMapAnchorInteriorDoor = 1,
  kMapAnchorLastExterior = 2,
};

struct ExteriorMapAnchor {
  uint32_t world_form_id = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  bool valid = false;
};

struct WorldMapDataLayout {
  uint32_t usable_width;
  uint32_t usable_height;
  int16_t northwest_x;
  int16_t northwest_y;
  int16_t southeast_x;
  int16_t southeast_y;
};

static_assert(sizeof(WorldMapDataLayout) == 16,
              "Fallout world-map MNAM must remain PE32-shaped");

struct InventoryNode {
  void* item;
  InventoryNode* next;
};

struct InventoryEntryData {
  void* extend_data;
  int32_t count_delta;
  void* form;
};

struct ExtraDataListLayout;

struct ExtraListNode {
  ExtraDataListLayout* item;
  ExtraListNode* next;
};

struct ExtraDataLayout {
  void* vtable;
  uint8_t type;
  uint8_t padding[3];
  ExtraDataLayout* next;
};

struct ExtraDataListLayout {
  void* vtable;
  ExtraDataLayout* data;
  uint8_t presence[12];
};

struct StackState {
  uint16_t flags = 0;
  bool has_health = false;
  float health = -1.0f;
};

struct InventoryFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t node_count = 0;
  uint32_t visible_count = 0;
};

struct DataFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t quest_count = 0;
  uint32_t objective_count = 0;
  uint32_t perk_count = 0;
};

struct NoteFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t note_count = 0;
};

struct RadioFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t station_count = 0;
  uint8_t enabled = 0;
};

struct EffectFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t effect_count = 0;
};

struct MarkerFingerprint {
  uint64_t hash = 1469598103934665603ull;
  uint32_t marker_count = 0;
};

struct RawListNode {
  void* item;
  RawListNode* next;
};

static_assert(sizeof(CommandDescriptor) == 0x28,
              "Fallout command descriptor must remain PE32-shaped");
static_assert(sizeof(ExtraDataLayout) == 0x0C,
              "Fallout extra-data node must remain PE32-shaped");
static_assert(sizeof(ExtraDataListLayout) == 0x14,
              "Fallout extra-data list must remain PE32-shaped");

extern "C" float Fo3dsCallThisFloat(uintptr_t function, void* self,
                                     uint32_t argument);
extern "C" uint32_t Fo3dsCallThisUInt1(uintptr_t function, void* self,
                                        uint32_t argument);
extern "C" void* Fo3dsCallThisPointer(uintptr_t function, void* self);
extern "C" void* Fo3dsCallThisPointer1(uintptr_t function, void* self,
                                         uint32_t argument);
extern "C" uint32_t Fo3dsCallThisUIntNoArgs(uintptr_t function, void* self);
extern "C" void Fo3dsCallThisVoidNoArgs(uintptr_t function, void* self);
extern "C" void Fo3dsCallThisVoid1(uintptr_t function, void* self,
                                    void* argument);
extern "C" void Fo3dsCallThisFree(uintptr_t function, void* self,
                                   void* allocation);
extern "C" void Fo3dsCallCdeclVoid3(uintptr_t function, void* argument1,
                                     void* argument2, uint32_t argument3);
extern "C" void* Fo3dsCallThisPointer2(uintptr_t function, void* self,
                                        void* argument1, uint32_t argument2);
extern "C" void* Fo3dsCallThisPointer3(uintptr_t function, void* self,
                                        void* argument1, uint32_t argument2,
                                        uint32_t argument3);
extern "C" void Fo3dsCallThisAction(uintptr_t function, void* self, void* form,
                                     uint32_t count, void* extra_data,
                                     uint32_t arg4, uint32_t arg5,
                                     uint32_t arg6);

using DynamicCast = void* (*)(void*, uint32_t, const void*, const void*,
                              uint32_t);
using ConditionEval = bool (*)(void*, void*, void*, double*);
// Returns the new enabled byte, not an operation-success boolean.
using RadioEnable = uint8_t (*)(bool);
using RadioTune = bool (*)(void*, bool);

BuildIdentity g_build = {};
uintptr_t g_module_base = 0;
uintptr_t g_player_global = 0;
uintptr_t g_player_vtable = 0;
uintptr_t g_data_handler_global = 0;
uintptr_t g_container_getter = 0;
uintptr_t g_inventory_name_helper = 0;
uintptr_t g_equip_native = 0;
uintptr_t g_unequip_native = 0;
DynamicCast g_dynamic_cast = nullptr;
const void* g_tes_form_type = nullptr;
const void* g_tes_full_name_type = nullptr;
const void* g_tes_icon_type = nullptr;
const void* g_tes_weight_form_type = nullptr;
const void* g_tes_value_form_type = nullptr;
const void* g_tes_health_form_type = nullptr;
const void* g_tes_attack_damage_form_type = nullptr;
const void* g_tes_object_armo_type = nullptr;
ConditionEval g_has_perk_eval = nullptr;
ConditionEval g_get_xp_next_eval = nullptr;
ConditionEval g_get_stage_eval = nullptr;
ConditionEval g_get_pc_misc_stat_eval = nullptr;
RadioEnable g_radio_enable = nullptr;
RadioTune g_radio_tune = nullptr;
uintptr_t g_radio_candidate_builder = 0;
uintptr_t g_list_node_destructor = 0;
uintptr_t g_heap_free = 0;
uintptr_t g_heap_manager = 0;
uintptr_t g_selected_radio_global = 0;
uintptr_t g_radio_enabled_global = 0;
uintptr_t g_radio_transition_global = 0;
uintptr_t g_active_effect_list_getter = 0;
uintptr_t g_world_map_marker_list_builder = 0;
uintptr_t g_map_marker_data_getter = 0;
uintptr_t g_simple_list_destructor = 0;
uintptr_t g_world_map_data_resolver = 0;
uintptr_t g_world_map_texture_resolver = 0;
uintptr_t g_note_image_payload_getter = 0;
uintptr_t g_note_text_payload_getter = 0;
uintptr_t g_active_quest_setter = 0;
uintptr_t g_quest_log_text_resolver = 0;
HWND g_game_window = nullptr;
UINT_PTR g_timer = 0;
DWORD g_game_thread_id = 0;
uint64_t g_previous_game_tick = 0;
bool g_ready = false;
bool g_local_map_ready = false;
void* g_last_player = nullptr;
uint64_t g_sequence = 1;
uint64_t g_world_epoch = 1;
Snapshot g_next = {};
bool g_have_state = false;
VitalsProbe g_last_vitals = {};
VitalsProbe g_vitals = {};
bool g_have_vitals = false;
bool g_have_inventory = false;
uint64_t g_next_inventory_scan_ms = 0;
uint64_t g_next_data_scan_ms = 0;
uint64_t g_next_note_scan_ms = 0;
uint64_t g_next_map_scan_ms = 0;
uint64_t g_next_radio_scan_ms = 0;
uint64_t g_next_condition_scan_ms = 0;
uint64_t g_next_special_scan_ms = 0;
uint64_t g_next_skills_scan_ms = 0;
uint64_t g_next_general_stats_scan_ms = 0;
bool g_condition_requested_last = false;
bool g_special_requested_last = false;
bool g_skills_requested_last = false;
bool g_general_stats_requested_last = false;
bool g_game_data_requested_last = false;
bool g_notes_requested_last = false;
InventoryFingerprint g_last_inventory_fingerprint = {};
DataFingerprint g_last_data_fingerprint = {};
NoteFingerprint g_last_note_fingerprint = {};
ItemState g_inventory_stage[kMaxItems] = {};
uint32_t g_inventory_stage_count = 0;
int32_t g_inventory_stage_caps = 0;
NoteState g_note_stage[kMaxNotes] = {};
uint32_t g_note_stage_count = 0;
bool g_have_notes = false;
QuestState g_quest_stage[kMaxQuests] = {};
uint32_t g_quest_stage_count = 0;
ObjectiveState g_objective_stage[kMaxObjectives] = {};
uint32_t g_objective_stage_count = 0;
PerkState g_perk_stage[kMaxPerks] = {};
uint32_t g_perk_stage_count = 0;
bool g_have_data = false;
bool g_have_general_stats = false;
MapProbe g_last_map = {};
bool g_have_map = false;
ExteriorMapAnchor g_exterior_map_anchor = {};
RadioFingerprint g_last_radio_fingerprint = {};
RadioState g_radio_stage[kMaxStations] = {};
uint32_t g_radio_stage_count = 0;
bool g_have_radio = false;
bool g_radio_scan_disabled = false;
EffectFingerprint g_last_effect_fingerprint = {};
EffectState g_effect_stage[kMaxEffects] = {};
uint32_t g_effect_stage_count = 0;
bool g_have_effects = false;
uint64_t g_next_effect_scan_ms = 0;
MarkerFingerprint g_last_marker_fingerprint = {};
MarkerState g_marker_stage[kMaxMarkers] = {};
uint32_t g_marker_stage_count = 0;
bool g_have_markers = false;
bool g_marker_scan_disabled = false;
uint64_t g_next_marker_scan_ms = 0;
Command g_last_action_command = {};
ActionResult g_last_action_result = ActionResult::kNone;

bool ReadCStringEquals(const char* address, const char* expected) {
  if (!address || !expected) return false;
  const size_t length = std::strlen(expected);
  return IsReadable(address, length + 1) &&
         std::memcmp(address, expected, length + 1) == 0;
}

bool ValidateTypeDescriptor(uintptr_t rva, const char* expected_name) {
  const uintptr_t address = g_module_base + rva;
  // MSVC RTTI TypeDescriptor objects are emitted into Fallout 3's writable
  // .data section.  Requiring .rdata here rejected every otherwise-correct
  // descriptor and kept the live bridge permanently uninitialized.
  return g_build.data.Contains(address, 8 + std::strlen(expected_name) + 1) &&
         ReadCStringEquals(reinterpret_cast<const char*>(address + 8),
                           expected_name);
}

uintptr_t FindUnique(const uint8_t* pattern, const char* mask) {
  const uintptr_t first = FindPattern(g_build.text, pattern, mask);
  if (!first || first + 1 >= g_build.text.end) return first;
  const ModuleRange remainder = {first + 1, g_build.text.end};
  return FindPattern(remainder, pattern, mask) ? 0 : first;
}

bool WritableAddress(uintptr_t address, size_t bytes) {
  if (!g_build.data.Contains(address, bytes) || !IsReadable(
          reinterpret_cast<void*>(address), bytes))
    return false;
  MEMORY_BASIC_INFORMATION info = {};
  if (!VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)))
    return false;
  const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  return (info.Protect & writable) != 0;
}

bool WritableMemory(void* address, size_t bytes) {
  if (!address || !bytes || !IsReadable(address, bytes)) return false;
  const uintptr_t begin = reinterpret_cast<uintptr_t>(address);
  if (bytes > std::numeric_limits<uintptr_t>::max() - begin) return false;
  const uintptr_t end = begin + bytes;
  const DWORD writable = PAGE_READWRITE | PAGE_WRITECOPY |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
  for (uintptr_t cursor = begin; cursor < end;) {
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
        info.State != MEM_COMMIT || (info.Protect & writable) == 0 ||
        (info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0)
      return false;
    const uintptr_t region = reinterpret_cast<uintptr_t>(info.BaseAddress);
    if (info.RegionSize > std::numeric_limits<uintptr_t>::max() - region)
      return false;
    const uintptr_t region_end = region + info.RegionSize;
    if (region_end <= cursor) return false;
    cursor = std::min(end, region_end);
  }
  return true;
}

bool ValidateDescriptor(uintptr_t rva, const char* name, uint16_t opcode,
                        uint16_t parameters, uintptr_t execute_rva) {
  const uintptr_t address = g_module_base + rva;
  if (!g_build.image.Contains(address, sizeof(CommandDescriptor)) ||
      !IsReadable(reinterpret_cast<void*>(address), sizeof(CommandDescriptor)))
    return false;
  const auto* descriptor = reinterpret_cast<const CommandDescriptor*>(address);
  const uintptr_t execute = reinterpret_cast<uintptr_t>(descriptor->execute);
  return ReadCStringEquals(descriptor->long_name, name) &&
         descriptor->opcode == opcode &&
         descriptor->parameter_count == parameters &&
         execute == g_module_base + execute_rva &&
         g_build.text.Contains(execute);
}

bool FiniteBounded(float value) {
  return std::isfinite(value) && value > -10000000.0f && value < 10000000.0f;
}

bool Close(float left, float right) {
  if (!std::isfinite(left) || !std::isfinite(right)) return left == right;
  return std::fabs(left - right) < 0.001f;
}

bool DisplayEqual(float left, float right) {
  if (!std::isfinite(left) || !std::isfinite(right)) return left == right;
  return std::lround(left) == std::lround(right);
}

bool VitalsEqual(const VitalsProbe& left, const VitalsProbe& right) {
  if (left.lifecycle != right.lifecycle ||
      left.player_form_id != right.player_form_id ||
      left.cell_form_id != right.cell_form_id ||
      left.level != right.level ||
      left.xp != right.xp ||
      left.xp_next != right.xp_next ||
      !DisplayEqual(left.health, right.health) ||
      !DisplayEqual(left.max_health, right.max_health) ||
      !DisplayEqual(left.action_points, right.action_points) ||
      !DisplayEqual(left.max_action_points, right.max_action_points) ||
      !DisplayEqual(left.damage_resistance, right.damage_resistance) ||
      !DisplayEqual(left.radiation_resistance,
                    right.radiation_resistance) ||
      !DisplayEqual(left.radiation, right.radiation) ||
      !DisplayEqual(left.carry_weight, right.carry_weight) ||
      !DisplayEqual(left.max_carry_weight, right.max_carry_weight) ||
      !DisplayEqual(left.karma, right.karma) ||
      std::strcmp(left.player_name, right.player_name) != 0 ||
      std::strcmp(left.location, right.location) != 0)
    return false;
  for (uint32_t i = 0; i < kSpecialCount; ++i)
    if (!Close(left.special[i], right.special[i])) return false;
  for (uint32_t i = 0; i < kSkillCount; ++i)
    if (!Close(left.skills[i], right.skills[i])) return false;
  for (uint32_t i = 0; i < kLimbCount; ++i)
    if (!Close(left.limbs[i], right.limbs[i])) return false;
  for (uint32_t i = 0; i < kMiscStatCount; ++i)
    if (left.misc_stats[i] != right.misc_stats[i]) return false;
  return true;
}

bool MapEqual(const MapProbe& left, const MapProbe& right) {
  return left.valid == right.valid &&
         left.world_valid == right.world_valid &&
         left.interior == right.interior &&
         left.anchor_source == right.anchor_source &&
         left.world_form_id == right.world_form_id &&
         std::fabs(left.x - right.x) < 2.0f &&
         std::fabs(left.y - right.y) < 2.0f &&
         std::fabs(left.z - right.z) < 2.0f &&
         std::fabs(left.heading - right.heading) < 0.5f &&
         Close(left.min_x, right.min_x) &&
         Close(left.max_y, right.max_y) &&
         Close(left.max_x, right.max_x) &&
         Close(left.min_y, right.min_y) &&
         left.usable_width == right.usable_width &&
         left.usable_height == right.usable_height &&
         std::strcmp(left.texture_path, right.texture_path) == 0;
}

bool CellIsInterior(void* cell) {
  return cell && IsReadable(reinterpret_cast<uint8_t*>(cell) + 0x24, 1) &&
      ((*reinterpret_cast<const uint8_t*>(
           reinterpret_cast<uint8_t*>(cell) + 0x24) & 1u) != 0);
}

bool AlignedPointer(const void* value);

bool ReadReferencePosition(void* reference, float* x, float* y, float* z,
                           void** parent_cell) {
  if (!reference || !x || !y || !z || !parent_cell ||
      !AlignedPointer(reference) || !IsReadable(reference, 0x40))
    return false;
  std::memcpy(parent_cell, reinterpret_cast<uint8_t*>(reference) + 0x3C,
              sizeof(*parent_cell));
  if (!*parent_cell || !IsReadable(*parent_cell, 0xC4)) return false;

  // MapMenu::UpdatePlayerMarker in 1.7.0.4 reads the selected reference through
  // TESObjectREFR's virtual position accessor (vtable + 0x1F4), not by assuming
  // the base-reference +0x2C fields are the final map position. This matters for
  // references whose effective position is supplied by a derived class.
  uintptr_t vtable = 0;
  std::memcpy(&vtable, reference, sizeof(vtable));
  if (!g_build.rdata.Contains(vtable, 0x1F8) ||
      !IsReadable(reinterpret_cast<void*>(vtable + 0x1F4),
                  sizeof(uintptr_t)))
    return false;
  uintptr_t accessor = 0;
  std::memcpy(&accessor, reinterpret_cast<void*>(vtable + 0x1F4),
              sizeof(accessor));
  if (!g_build.text.Contains(accessor)) return false;
  const void* position = Fo3dsCallThisPointer(accessor, reference);
  if (!position || !IsReadable(position, sizeof(float) * 3)) return false;
  std::memcpy(x, position, sizeof(*x));
  std::memcpy(y, reinterpret_cast<const uint8_t*>(position) + 4, sizeof(*y));
  std::memcpy(z, reinterpret_cast<const uint8_t*>(position) + 8, sizeof(*z));
  return FiniteBounded(*x) && FiniteBounded(*y) && FiniteBounded(*z);
}

bool ValidPlayer(void* raw_player) {
  if (!raw_player || !IsReadable(raw_player, 0xA0)) return false;
  const uintptr_t player = reinterpret_cast<uintptr_t>(raw_player);
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(player);
  const uint32_t form_id = *reinterpret_cast<const uint32_t*>(player + 0x0C);
  return vtable == g_player_vtable && form_id == 0x00000014;
}

bool ReadActorValue(void* player, uint32_t actor_value, bool current,
                    float* output) {
  if (!output || actor_value > 0x48) return false;
  void* owner = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player) + 0x9C);
  if (!IsReadable(owner, sizeof(uintptr_t))) return false;
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(owner);
  if (!g_build.rdata.Contains(vtable, 4 * sizeof(uintptr_t)) ||
      !IsReadable(reinterpret_cast<void*>(vtable), 4 * sizeof(uintptr_t)))
    return false;
  const uintptr_t function =
      reinterpret_cast<const uintptr_t*>(vtable)[current ? 3 : 0];
  if (!g_build.text.Contains(function)) return false;
  // ActorValueOwner slot 3 returns the current value in ST(0), while slot 0
  // returns the integer base value in EAX. Treating slot 0 as an x87 float
  // consumed stale register contents, which made max HP/AP/carry capacity
  // fluctuate every tick and forced needless panel recomposition.
  const float value = current
      ? Fo3dsCallThisFloat(function, owner, actor_value)
      : static_cast<float>(Fo3dsCallThisUInt1(function, owner, actor_value));
  if (!FiniteBounded(value)) return false;
  *output = value;
  return true;
}

bool ReadPlayerLevel(void* player, int32_t* output) {
  if (!player || !output) return false;
  void* owner = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player) +
                                        0x9C);
  if (!IsReadable(owner, sizeof(uintptr_t))) return false;
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(owner);
  if (!g_build.rdata.Contains(vtable, 11 * sizeof(uintptr_t)) ||
      !IsReadable(reinterpret_cast<const void*>(vtable),
                  11 * sizeof(uintptr_t)))
    return false;
  const uintptr_t function = reinterpret_cast<const uintptr_t*>(vtable)[10];
  if (!g_build.text.Contains(function)) return false;
  const uint32_t level = Fo3dsCallThisUIntNoArgs(function, owner) & 0xFFFFu;
  if (!level || level > 255) return false;
  *output = static_cast<int32_t>(level);
  return true;
}

uint32_t ReadFormId(const void* object) {
  if (!object || !IsReadable(object, 0x10)) return 0;
  return *reinterpret_cast<const uint32_t*>(
      reinterpret_cast<uintptr_t>(object) + 0x0C);
}

bool AlignedPointer(const void* value) {
  return !value ||
         (reinterpret_cast<uintptr_t>(value) & (alignof(void*) - 1)) == 0;
}

void MixFingerprint(uint64_t* hash, uintptr_t value) {
  if (!hash) return;
  for (size_t i = 0; i < sizeof(value); ++i) {
    *hash ^= static_cast<uint8_t>(value >> (i * 8));
    *hash *= 1099511628211ull;
  }
}

bool FingerprintsEqual(const InventoryFingerprint& left,
                       const InventoryFingerprint& right) {
  return left.hash == right.hash && left.node_count == right.node_count;
}

bool ReadInventoryNode(const InventoryNode* node, void** item,
                       InventoryNode** next) {
  if (!node || !item || !next || !AlignedPointer(node) ||
      !IsReadable(node, sizeof(InventoryNode)))
    return false;
  InventoryNode copy = {};
  std::memcpy(&copy, node, sizeof(copy));
  if (!AlignedPointer(copy.item) || !AlignedPointer(copy.next)) return false;
  *item = copy.item;
  *next = copy.next;
  return true;
}

bool AdvanceInventoryNode(InventoryNode* node, InventoryNode** next) {
  if (!next) return false;
  if (!node) {
    *next = nullptr;
    return true;
  }
  void* ignored = nullptr;
  return ReadInventoryNode(node, &ignored, next);
}

struct CaptureBudget {
  uint64_t started = PerfNow();
  uint64_t maximum_us = 0;
  uint32_t maximum_nodes = 0;
  uint32_t nodes = 0;

  bool Consume() {
    if (++nodes > maximum_nodes) return false;
    return (nodes & 31u) != 0 || PerfElapsedMicros(started) <= maximum_us;
  }
};

bool InspectStackState(void* raw_extend_data, StackState* output,
                       uint64_t* fingerprint, CaptureBudget* budget) {
  if (!output || !fingerprint || !budget) return false;
  *output = {};
  if (!raw_extend_data) {
    MixFingerprint(fingerprint, 0);
    return true;
  }
  if (!AlignedPointer(raw_extend_data)) return false;

  auto* cursor = reinterpret_cast<ExtraListNode*>(raw_extend_data);
  constexpr uint32_t kMaxStackNodes = 4096;
  constexpr uint32_t kMaxExtraNodes = 256;
  bool equipped_health_found = false;
  uint32_t stack_count = 0;
  while (cursor) {
    if (++stack_count > kMaxStackNodes || !budget->Consume() ||
        !IsReadable(cursor, sizeof(*cursor)))
      return false;
    ExtraListNode node = {};
    std::memcpy(&node, cursor, sizeof(node));
    if (!AlignedPointer(node.item) || !AlignedPointer(node.next)) return false;

    bool stack_equipped = false;
    bool stack_has_health = false;
    float stack_health = -1.0f;
    if (node.item) {
      if (!IsReadable(node.item, sizeof(ExtraDataListLayout))) return false;
      ExtraDataListLayout list = {};
      std::memcpy(&list, node.item, sizeof(list));
      if (!AlignedPointer(list.vtable) || !AlignedPointer(list.data) ||
          !g_build.rdata.Contains(reinterpret_cast<uintptr_t>(list.vtable),
                                  sizeof(void*)))
        return false;
      ExtraDataLayout* extra = list.data;
      uint32_t extra_count = 0;
      while (extra) {
        if (++extra_count > kMaxExtraNodes || !budget->Consume() ||
            !IsReadable(extra, sizeof(ExtraDataLayout)))
          return false;
        ExtraDataLayout value = {};
        std::memcpy(&value, extra, sizeof(value));
        if (!AlignedPointer(value.vtable) || !AlignedPointer(value.next) ||
            !g_build.rdata.Contains(reinterpret_cast<uintptr_t>(value.vtable),
                                    sizeof(void*)))
          return false;
        if (value.type == 0x16 || value.type == 0x17)
          stack_equipped = true;
        if (value.type == 0x25) {
          const uintptr_t health_address =
              reinterpret_cast<uintptr_t>(extra) + 0x0C;
          if (!IsReadable(reinterpret_cast<void*>(health_address),
                          sizeof(float)))
            return false;
          std::memcpy(&stack_health,
                      reinterpret_cast<const void*>(health_address),
                      sizeof(stack_health));
          if (!FiniteBounded(stack_health) || stack_health < 0.0f)
            return false;
          stack_has_health = true;
        }
        extra = value.next;
      }
    }

    if (stack_equipped) output->flags |= kItemEquipped;
    if (stack_has_health &&
        (!output->has_health || (stack_equipped && !equipped_health_found))) {
      output->has_health = true;
      output->health = stack_health;
      equipped_health_found = stack_equipped;
    }
    MixFingerprint(fingerprint, stack_equipped ? 1u : 0u);
    uint32_t health_bits = 0;
    if (stack_has_health) std::memcpy(&health_bits, &stack_health, sizeof(health_bits));
    MixFingerprint(fingerprint, stack_has_health ? health_bits : 0u);
    cursor = node.next;
  }
  MixFingerprint(fingerprint, stack_count);
  return true;
}

void* CastForm(void* form, const void* target_type) {
  if (!form || !target_type || !g_dynamic_cast || !g_tes_form_type)
    return nullptr;
  if (!AlignedPointer(form) || !IsReadable(form, 0x10)) return nullptr;
  const uintptr_t address = reinterpret_cast<uintptr_t>(form);
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(address);
  const uint8_t type = *reinterpret_cast<const uint8_t*>(address + 4);
  const uint32_t form_id = *reinterpret_cast<const uint32_t*>(address + 0x0C);
  if (!g_build.rdata.Contains(vtable, sizeof(uintptr_t)) || !type ||
      type > 0x7F || !form_id || form_id == 0xFFFFFFFFu)
    return nullptr;
  return g_dynamic_cast(form, 0, g_tes_form_type, target_type, 0);
}

bool ReadIconPath(void* form, char* destination, size_t capacity);
bool ReadApparelIconPath(void* form, char* destination, size_t capacity);

void PopulateItemStats(void* form, const StackState& stack, ItemState* item) {
  if (!form || !item) return;
  item->flags = stack.flags;
  // ARMO/CLOT records do not expose their Pip-Boy artwork through the direct
  // TESIcon base used by ordinary inventory forms.  The native inventory reads
  // the gendered TESIcon embedded in TESBipedModelForm, so resolve that first
  // for apparel and retain the generic component only as a compatibility
  // fallback for unusual records supplied by plugins.
  const bool apparel_icon =
      item->category == ItemCategory::kApparel &&
      ReadApparelIconPath(form, item->icon_path, sizeof(item->icon_path));
  if (!apparel_icon)
    ReadIconPath(form, item->icon_path, sizeof(item->icon_path));

  void* weight = CastForm(form, g_tes_weight_form_type);
  if (weight && IsReadable(reinterpret_cast<uint8_t*>(weight) + 4,
                           sizeof(float))) {
    float value = -1.0f;
    std::memcpy(&value, reinterpret_cast<uint8_t*>(weight) + 4, sizeof(value));
    if (FiniteBounded(value) && value >= 0.0f) item->weight = value;
  } else {
    item->weight = 0.0f;
  }

  void* value_form = CastForm(form, g_tes_value_form_type);
  if (value_form && IsReadable(reinterpret_cast<uint8_t*>(value_form) + 4,
                               sizeof(uint32_t))) {
    uint32_t value = 0;
    std::memcpy(&value, reinterpret_cast<uint8_t*>(value_form) + 4,
                sizeof(value));
    if (value <= static_cast<uint32_t>(INT32_MAX))
      item->value = static_cast<int32_t>(value);
  } else {
    item->value = 0;
  }

  void* health_form = CastForm(form, g_tes_health_form_type);
  if (health_form && IsReadable(reinterpret_cast<uint8_t*>(health_form) + 4,
                                sizeof(uint32_t))) {
    uint32_t base_health = 0;
    std::memcpy(&base_health, reinterpret_cast<uint8_t*>(health_form) + 4,
                sizeof(base_health));
    if (base_health) {
      const float current = stack.has_health ? stack.health
                                             : static_cast<float>(base_health);
      item->condition = std::clamp(
          current * 100.0f / static_cast<float>(base_health), 0.0f, 100.0f);
    }
  }

  void* damage_form = CastForm(form, g_tes_attack_damage_form_type);
  if (damage_form && IsReadable(reinterpret_cast<uint8_t*>(damage_form) + 4,
                                sizeof(uint16_t))) {
    uint16_t damage = 0;
    std::memcpy(&damage, reinterpret_cast<uint8_t*>(damage_form) + 4,
                sizeof(damage));
    item->damage = static_cast<float>(damage);
  }

  void* armor_form = CastForm(form, g_tes_object_armo_type);
  if (armor_form && IsReadable(reinterpret_cast<uint8_t*>(armor_form) + 0x178,
                               sizeof(uint16_t))) {
    uint16_t armor = 0;
    std::memcpy(&armor, reinterpret_cast<uint8_t*>(armor_form) + 0x178,
                sizeof(armor));
    item->armor = static_cast<float>(armor);
  }

  if (item->category == ItemCategory::kWeapon &&
      IsReadable(reinterpret_cast<uint8_t*>(form) + 0xA8, sizeof(void*))) {
    void* ammo = nullptr;
    std::memcpy(&ammo, reinterpret_cast<uint8_t*>(form) + 0xA8,
                sizeof(ammo));
    item->ammo_form_id = ReadFormId(ammo);
  }
}

ItemCategory CategoryForFormType(uint8_t type) {
  switch (type) {
    case 0x28: return ItemCategory::kWeapon;
    case 0x18:
    case 0x1A: return ItemCategory::kApparel;
    case 0x1D:
    case 0x2F:
    case 0x19: return ItemCategory::kAid;
    case 0x29: return ItemCategory::kAmmo;
    // Ordinary miscellaneous objects and notes reach the vanilla Misc tab.
    // Keys (0x2E) deliberately remain excluded because vanilla routes them
    // through its separate keyring path.
    case 0x1F:
    case 0x31: return ItemCategory::kMisc;
    default: return ItemCategory::kUnknown;
  }
}

bool CopyBoundedEngineString(const char* source, char* destination,
                             size_t capacity) {
  if (!source || !destination || capacity < 2) return false;
  constexpr size_t kMaximumEngineString = 4096;
  size_t length = 0;
  while (length < kMaximumEngineString) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(source) + length;
    MEMORY_BASIC_INFORMATION info = {};
    if (!VirtualQuery(reinterpret_cast<const void*>(address), &info,
                      sizeof(info)))
      return false;
    const uintptr_t region_end =
        reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
    if (region_end <= address) return false;
    const size_t chunk = std::min<size_t>(
        kMaximumEngineString - length, region_end - address);
    if (!IsReadable(reinterpret_cast<const void*>(address), chunk)) return false;
    const void* terminator = std::memchr(reinterpret_cast<const void*>(address),
                                         '\0', chunk);
    if (terminator) {
      const size_t total = reinterpret_cast<const char*>(terminator) - source;
      if (!total) return false;
      const size_t copy = std::min(total, capacity - 1);
      std::memcpy(destination, source, copy);
      destination[copy] = '\0';
      return std::strcmp(destination, "<Missing Name>") != 0;
    }
    length += chunk;
  }
  return false;
}

bool CopyComponentString(const void* component, char* destination,
                         size_t capacity) {
  if (!component || !destination || capacity < 2 ||
      !IsReadable(component, 0x0C))
    return false;
  const char* text = nullptr;
  uint16_t length = 0;
  std::memcpy(&text, reinterpret_cast<const uint8_t*>(component) + 4,
              sizeof(text));
  std::memcpy(&length, reinterpret_cast<const uint8_t*>(component) + 8,
              sizeof(length));
  if (!text || !length) return false;
  return CopyBoundedEngineString(text, destination, capacity);
}

bool ReadFullName(void* form, char* destination, size_t capacity) {
  if (!form || !destination || capacity < 2 || !g_tes_full_name_type)
    return false;
  void* component = CastForm(form, g_tes_full_name_type);
  return component && CopyComponentString(component, destination, capacity);
}

bool ReadIconPath(void* form, char* destination, size_t capacity) {
  if (!form || !destination || capacity < 2 || !g_tes_icon_type)
    return false;
  void* component = CastForm(form, g_tes_icon_type);
  return component && CopyComponentString(component, destination, capacity);
}

bool ReadActiveEffectDisplayName(void* effect_item, void* setting,
                                 char* destination, size_t capacity,
                                 uint8_t* detrimental) {
  if (!effect_item || !destination || capacity < 2 || !detrimental ||
      !IsReadable(effect_item, 0x20))
    return false;
  *detrimental = 0;

  // EffectItem::ScriptEffectInfo is the source used by the Pip-Boy for a
  // scripted effect's player-facing name. Falling straight through to the
  // MGEF full name turns many real effects into the generic "Script Effect".
  // The exact 32-bit layout is: EffectItem+0x18 -> ScriptEffectInfo,
  // BSString::data at +0x08, and isHostile at +0x14.
  void* script_info = nullptr;
  std::memcpy(&script_info,
              reinterpret_cast<uint8_t*>(effect_item) + 0x18,
              sizeof(script_info));
  if (script_info && IsReadable(script_info, 0x18)) {
    const char* script_name = nullptr;
    uint32_t hostile = 0;
    std::memcpy(&script_name,
                reinterpret_cast<uint8_t*>(script_info) + 0x08,
                sizeof(script_name));
    std::memcpy(&hostile,
                reinterpret_cast<uint8_t*>(script_info) + 0x14,
                sizeof(hostile));
    *detrimental = hostile ? 1 : 0;
    if (script_name &&
        CopyBoundedEngineString(script_name, destination, capacity))
      return true;
  }
  return setting && ReadFullName(setting, destination, capacity);
}

bool ReadApparelIconPath(void* form, char* destination, size_t capacity) {
  if (!form || !destination || capacity < 2 || !IsReadable(form, 0x10))
    return false;

  const uint8_t type = *reinterpret_cast<const uint8_t*>(
      reinterpret_cast<const uint8_t*>(form) + 4);
  // Supported 32-bit Fallout3.exe record layout:
  //   TESObjectARMO::bipedModel = +0x70
  //   TESObjectCLOT::bipedModel = +0x68
  //   TESBipedModelForm::icon[0] = +0x8C, icon[1] = +0x98
  // icon[0] is the Pip-Boy inventory artwork selected by the normal menu.  The
  // second gender slot is a legitimate fallback when a mod only populated it.
  uintptr_t biped_offset = 0;
  if (type == 0x18)
    biped_offset = 0x70;
  else if (type == 0x1A)
    biped_offset = 0x68;
  else
    return false;

  const uint8_t* biped = reinterpret_cast<const uint8_t*>(form) + biped_offset;
  constexpr uintptr_t kBipedIconOffset = 0x8C;
  constexpr uintptr_t kTesIconSize = 0x0C;
  const void* primary = biped + kBipedIconOffset;
  if (CopyComponentString(primary, destination, capacity)) return true;
  const void* alternate = biped + kBipedIconOffset + kTesIconSize;
  return CopyComponentString(alternate, destination, capacity);
}

bool ReadPlayerName(void* player, char* destination, size_t capacity) {
  if (!player || !destination || capacity < 2 ||
      !IsReadable(reinterpret_cast<uint8_t*>(player) + 0x1C,
                  sizeof(void*)))
    return false;
  void* base_form = nullptr;
  std::memcpy(&base_form, reinterpret_cast<uint8_t*>(player) + 0x1C,
              sizeof(base_form));
  return base_form && ReadFullName(base_form, destination, capacity);
}

bool ReadCellName(void* cell, char* destination, size_t capacity) {
  if (!cell || !destination || capacity < 2) return false;
  if (ReadFullName(cell, destination, capacity)) return true;
  if (!IsReadable(reinterpret_cast<uint8_t*>(cell) + 0xC0, sizeof(void*)))
    return false;
  void* world = nullptr;
  std::memcpy(&world, reinterpret_cast<uint8_t*>(cell) + 0xC0,
              sizeof(world));
  return world && ReadFullName(world, destination, capacity);
}

bool ReadDescription(void* form, uintptr_t component_address,
                     char* destination, size_t capacity) {
  if (!form || !component_address || !destination || capacity < 2 ||
      !IsReadable(reinterpret_cast<void*>(component_address),
                  sizeof(uintptr_t)))
    return false;
  const uintptr_t vtable =
      *reinterpret_cast<const uintptr_t*>(component_address);
  if (!g_build.rdata.Contains(vtable, 5 * sizeof(uintptr_t)) ||
      !IsReadable(reinterpret_cast<void*>(vtable), 5 * sizeof(uintptr_t)))
    return false;
  const uintptr_t get = reinterpret_cast<const uintptr_t*>(vtable)[4];
  if (!g_build.text.Contains(get)) return false;
  constexpr uint32_t kDescriptionChunk = 0x43534544u;  // 'DESC'
  const char* text = reinterpret_cast<const char*>(Fo3dsCallThisPointer2(
      get, reinterpret_cast<void*>(component_address), form,
      kDescriptionChunk));
  return CopyBoundedEngineString(text, destination, capacity);
}

bool ReadNotePayload(void* form, uint8_t kind, NoteState* note) {
  if (!form || !note) return false;
  if (kind == 1) {
    if (!g_note_text_payload_getter) return false;
    void* payload = Fo3dsCallThisPointer(g_note_text_payload_getter, form);
    if (!payload || !IsReadable(payload, sizeof(uintptr_t))) return false;
    const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(payload);
    if (!g_build.rdata.Contains(vtable, 5 * sizeof(uintptr_t)) ||
        !IsReadable(reinterpret_cast<const void*>(vtable),
                    5 * sizeof(uintptr_t)))
      return false;
    const uintptr_t get_text = reinterpret_cast<const uintptr_t*>(vtable)[4];
    if (!g_build.text.Contains(get_text)) return false;
    constexpr uint32_t kTranscriptChunk = 0x4D414E54u;  // 'TNAM'
    const char* text = reinterpret_cast<const char*>(Fo3dsCallThisPointer3(
        get_text, payload, form, kTranscriptChunk, 1));
    return CopyBoundedEngineString(text, note->text, sizeof(note->text));
  }
  if (kind == 2) {
    if (!g_note_image_payload_getter) return false;
    void* payload = Fo3dsCallThisPointer(g_note_image_payload_getter, form);
    if (!payload || !IsReadable(payload, 2 * sizeof(void*))) return false;
    const char* image_path = nullptr;
    std::memcpy(&image_path, reinterpret_cast<uint8_t*>(payload) + 4,
                sizeof(image_path));
    return CopyBoundedEngineString(image_path, note->icon_path,
                                   sizeof(note->icon_path));
  }
  return false;
}

bool ReadRawListNode(const RawListNode* node, void** item,
                     RawListNode** next) {
  if (!node || !item || !next || !AlignedPointer(node) ||
      !IsReadable(node, sizeof(RawListNode)))
    return false;
  RawListNode value = {};
  std::memcpy(&value, node, sizeof(value));
  if (!AlignedPointer(value.item) || !AlignedPointer(value.next)) return false;
  *item = value.item;
  *next = value.next;
  return true;
}

bool ValidFormType(void* form, uint8_t expected_type) {
  if (!form || !IsReadable(form, 0x10)) return false;
  const uintptr_t address = reinterpret_cast<uintptr_t>(form);
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(address);
  return g_build.rdata.Contains(vtable, sizeof(uintptr_t)) &&
         *reinterpret_cast<const uint8_t*>(address + 4) == expected_type &&
         ReadFormId(form) != 0;
}

bool NoteFingerprintsEqual(const NoteFingerprint& left,
                           const NoteFingerprint& right) {
  return left.hash == right.hash && left.note_count == right.note_count;
}

bool AdvanceRawListNode(RawListNode* node, RawListNode** next) {
  if (!next) return false;
  if (!node) {
    *next = nullptr;
    return true;
  }
  void* ignored = nullptr;
  return ReadRawListNode(node, &ignored, next);
}

bool CaptureNotes(void* player, bool populate, NoteFingerprint* fingerprint) {
  if (!player || !fingerprint ||
      !IsReadable(reinterpret_cast<uint8_t*>(player) + 0x570,
                  sizeof(RawListNode)))
    return false;
  if (populate) g_note_stage_count = 0;

  NoteFingerprint result = {};
  auto* head = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(player) + 0x570);
  RawListNode* cursor = head;
  RawListNode* slow = head;
  RawListNode* fast = head;
  CaptureBudget budget = {PerfNow(), 3000, kMaxDataNodes, 0};
  while (cursor) {
    if (!budget.Consume()) return false;
    void* form = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &form, &next)) return false;
    MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(cursor));
    MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(next));
    if (form) {
      if (!ValidFormType(form, 0x31) || !IsReadable(form, 0x7E))
        return false;
      const uint32_t form_id = ReadFormId(form);
      const uint8_t kind = *reinterpret_cast<const uint8_t*>(
          reinterpret_cast<uintptr_t>(form) + 0x7C);
      const uint8_t read = *reinterpret_cast<const uint8_t*>(
          reinterpret_cast<uintptr_t>(form) + 0x7D);
      if (kind > 3) return false;
      void* payload = nullptr;
      void* auxiliary = nullptr;
      std::memcpy(&payload, reinterpret_cast<uint8_t*>(form) + 0x6C,
                  sizeof(payload));
      std::memcpy(&auxiliary, reinterpret_cast<uint8_t*>(form) + 0x70,
                  sizeof(auxiliary));
      MixFingerprint(&result.hash, form_id);
      MixFingerprint(&result.hash, kind);
      MixFingerprint(&result.hash, read != 0);
      MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(payload));
      MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(auxiliary));
      ++result.note_count;
      if (populate && g_note_stage_count < kMaxNotes) {
        NoteState note = {};
        note.form_id = form_id;
        note.kind = kind;
        note.read = read != 0;
        if (!ReadFullName(form, note.title, sizeof(note.title)))
          std::snprintf(note.title, sizeof(note.title), "Untitled Note");
        ReadIconPath(form, note.icon_path, sizeof(note.icon_path));
        ReadNotePayload(form, kind, &note);
        g_note_stage[g_note_stage_count++] = note;
      }
    }
    cursor = next;
    if (!AdvanceRawListNode(slow, &slow) ||
        !AdvanceRawListNode(fast, &fast) ||
        (fast && !AdvanceRawListNode(fast, &fast)))
      return false;
    if (slow && fast && slow == fast) return false;
  }
  *fingerprint = result;
  return true;
}

bool CaptureActiveEffects(void* player, bool populate,
                          EffectFingerprint* fingerprint) {
  if (!player || !fingerprint || !g_active_effect_list_getter) return false;
  if (populate) g_effect_stage_count = 0;

  void* magic_target = reinterpret_cast<void*>(
      reinterpret_cast<uintptr_t>(player) + 0x8C);
  if (!IsReadable(magic_target, sizeof(uintptr_t))) return false;
  const uintptr_t vtable =
      *reinterpret_cast<const uintptr_t*>(magic_target);
  if (vtable != g_module_base + sig::kPlayerMagicTargetVtableRva)
    return false;

  void* raw_list = Fo3dsCallThisPointer(g_active_effect_list_getter,
                                        magic_target);
  EffectFingerprint result = {};
  auto* cursor = reinterpret_cast<RawListNode*>(raw_list);
  for (uint32_t visited = 0;
       cursor && visited < kMaxDataNodes; ++visited) {
    void* raw_effect = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &raw_effect, &next)) return false;
    if (raw_effect) {
      if (!AlignedPointer(raw_effect) || !IsReadable(raw_effect, 0x48))
        return false;
      const uintptr_t effect_address =
          reinterpret_cast<uintptr_t>(raw_effect);
      const uintptr_t effect_vtable =
          *reinterpret_cast<const uintptr_t*>(effect_address);
      if (!g_build.rdata.Contains(effect_vtable, sizeof(uintptr_t)))
        return false;

      uint8_t active = 0;
      uint8_t terminated = 0;
      void* effect_item = nullptr;
      void* target = nullptr;
      float elapsed = 0.0f;
      float magnitude = 0.0f;
      float duration = 0.0f;
      std::memcpy(&elapsed, reinterpret_cast<void*>(effect_address + 0x04),
                  sizeof(elapsed));
      std::memcpy(&effect_item,
                  reinterpret_cast<void*>(effect_address + 0x0C),
                  sizeof(effect_item));
      std::memcpy(&active, reinterpret_cast<void*>(effect_address + 0x10), 1);
      std::memcpy(&terminated,
                  reinterpret_cast<void*>(effect_address + 0x13), 1);
      std::memcpy(&magnitude,
                  reinterpret_cast<void*>(effect_address + 0x1C),
                  sizeof(magnitude));
      std::memcpy(&duration,
                  reinterpret_cast<void*>(effect_address + 0x20),
                  sizeof(duration));
      std::memcpy(&target, reinterpret_cast<void*>(effect_address + 0x24),
                  sizeof(target));
      if (!FiniteBounded(elapsed) || !FiniteBounded(magnitude) ||
          !FiniteBounded(duration) || (target && target != magic_target))
        return false;
      if (active && !terminated && effect_item) {
        if (!AlignedPointer(effect_item) || !IsReadable(effect_item, 0x20))
          return false;
        void* setting = nullptr;
        std::memcpy(&setting,
                    reinterpret_cast<uint8_t*>(effect_item) + 0x1C,
                    sizeof(setting));
        if (setting && ValidFormType(setting, 0x10)) {
          char name[96] = {};
          uint8_t detrimental = 0;
          if (ReadActiveEffectDisplayName(effect_item, setting, name,
                                          sizeof(name), &detrimental) &&
              result.effect_count < kMaxEffects) {
            const uint32_t form_id = ReadFormId(setting);
            uint32_t magnitude_bits = 0;
            std::memcpy(&magnitude_bits, &magnitude,
                        sizeof(magnitude_bits));
            const int32_t remaining_seconds = duration > 0.0f
                ? static_cast<int32_t>(std::max(
                      0.0f, std::floor(duration - elapsed)))
                : -1;
            MixFingerprint(&result.hash, form_id);
            MixFingerprint(&result.hash, magnitude_bits);
            MixFingerprint(&result.hash,
                           static_cast<uint32_t>(remaining_seconds));
            MixFingerprint(&result.hash, detrimental);
            ++result.effect_count;
            if (populate) {
              EffectState effect = {};
              effect.form_id = form_id;
              effect.magnitude = magnitude;
              effect.seconds_remaining =
                  static_cast<float>(remaining_seconds);
              effect.detrimental = detrimental;
              std::snprintf(effect.name, sizeof(effect.name), "%s", name);
              g_effect_stage[g_effect_stage_count++] = effect;
            }
          }
        }
      }
    }
    cursor = next;
  }
  if (cursor) return false;
  *fingerprint = result;
  return true;
}

bool ValidateRawListChain(RawListNode* list,
                          const RawListNode* forbidden = nullptr) {
  RawListNode* cursor = list;
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    if (cursor == forbidden) return false;
    void* ignored = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &ignored, &next) ||
        !WritableMemory(cursor, sizeof(RawListNode)))
      return false;
    cursor = next;
  }
  return cursor == nullptr;
}

void ReleaseTemporarySimpleList(RawListNode* list) {
  if (!list || !g_simple_list_destructor || !g_heap_free || !g_heap_manager)
    return;
  Fo3dsCallThisVoidNoArgs(g_simple_list_destructor, list);
  Fo3dsCallThisFree(g_heap_free,
                    reinterpret_cast<void*>(g_heap_manager), list);
}

bool CaptureMapMarkers(void* player, bool populate,
                       MarkerFingerprint* fingerprint) {
  if (!player || !fingerprint || !g_world_map_marker_list_builder ||
      !g_map_marker_data_getter || !g_simple_list_destructor ||
      !g_heap_free || !g_heap_manager)
    return false;
  if (g_marker_scan_disabled) return false;
  if (populate) g_marker_stage_count = 0;
  MarkerFingerprint result = {};

  void* cell = nullptr;
  const uintptr_t player_address = reinterpret_cast<uintptr_t>(player);
  if (!IsReadable(reinterpret_cast<void*>(player_address + 0x3C),
                  sizeof(cell)))
    return false;
  std::memcpy(&cell, reinterpret_cast<void*>(player_address + 0x3C),
              sizeof(cell));
  if (!cell || !IsReadable(reinterpret_cast<uint8_t*>(cell) + 0xC0,
                           sizeof(void*))) {
    *fingerprint = result;
    return true;
  }
  void* world = nullptr;
  std::memcpy(&world, reinterpret_cast<uint8_t*>(cell) + 0xC0,
              sizeof(world));
  if (!world && IsReadable(reinterpret_cast<void*>(g_data_handler_global),
                           sizeof(void*))) {
    void* data_handler = nullptr;
    std::memcpy(&data_handler, reinterpret_cast<void*>(g_data_handler_global),
                sizeof(data_handler));
    if (data_handler &&
        IsReadable(reinterpret_cast<uint8_t*>(data_handler) + 0x0C,
                   sizeof(world)))
      std::memcpy(&world, reinterpret_cast<uint8_t*>(data_handler) + 0x0C,
                  sizeof(world));
  }
  if (!world) {
    *fingerprint = result;
    return true;
  }
  if (!ValidFormType(world, 0x41)) return false;

  auto* list = reinterpret_cast<RawListNode*>(Fo3dsCallThisPointer1(
      g_world_map_marker_list_builder, world, 0));
  if (!list) return false;
  if (!ValidateRawListChain(list)) {
    // The engine destructor recursively follows the chain. Calling it after
    // failed validation can turn a corrupt temporary list into a game crash;
    // retain the allocation and retry on a later scan instead.
    LogRateLimited("map-list-chain", 5000, LogLevel::kError,
                   "[MAP] temporary marker chain is invalid; unsafe destructor skipped");
    g_marker_scan_disabled = true;
    return false;
  }
  bool valid = true;
  RawListNode* cursor = list;
  for (uint32_t visited = 0;
       cursor && visited < kMaxDataNodes; ++visited) {
    void* reference = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &reference, &next)) {
      valid = false;
      break;
    }
    if (reference) {
      if (!AlignedPointer(reference) || !IsReadable(reference, 0x44)) {
        valid = false;
        break;
      }
      const uintptr_t reference_address =
          reinterpret_cast<uintptr_t>(reference);
      const uintptr_t reference_vtable =
          *reinterpret_cast<const uintptr_t*>(reference_address);
      void* base = nullptr;
      std::memcpy(&base,
                  reinterpret_cast<void*>(reference_address + 0x1C),
                  sizeof(base));
      if (!g_build.rdata.Contains(reference_vtable, sizeof(uintptr_t)) ||
          ReadFormId(reference) == 0 || ReadFormId(base) != 0x00000010u) {
        valid = false;
        break;
      }
      void* data = Fo3dsCallThisPointer(g_map_marker_data_getter, reference);
      if (!data || !AlignedPointer(data) || !IsReadable(data, 0x10)) {
        valid = false;
        break;
      }
      uint8_t flags = 0;
      uint16_t marker_type = 0;
      std::memcpy(&flags, reinterpret_cast<uint8_t*>(data) + 0x0C,
                  sizeof(flags));
      std::memcpy(&marker_type, reinterpret_cast<uint8_t*>(data) + 0x0E,
                  sizeof(marker_type));
      if (flags & 1u) {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        void* marker_cell = nullptr;
        // MapMenu asks the reference's virtual position accessor. Raw +0x2C
        // is not authoritative for derived/moved references and produced the
        // visible marker jumps reported by users.
        if (!ReadReferencePosition(reference, &x, &y, &z, &marker_cell)) {
          valid = false;
          break;
        }
        const uint32_t form_id = ReadFormId(reference);
        uint32_t x_bits = 0;
        uint32_t y_bits = 0;
        std::memcpy(&x_bits, &x, sizeof(x_bits));
        std::memcpy(&y_bits, &y, sizeof(y_bits));
        MixFingerprint(&result.hash, form_id);
        MixFingerprint(&result.hash, flags);
        MixFingerprint(&result.hash, marker_type);
        MixFingerprint(&result.hash, x_bits);
        MixFingerprint(&result.hash, y_bits);
        // The native list contains visible-but-undiscovered locations because
        // the stock menu needs them for discovery checks. They are not rows
        // or pins in the player-facing World Map, so do not publish them.
        if ((flags & 2u) && result.marker_count < kMaxMarkers) {
          ++result.marker_count;
        } else {
          cursor = next;
          continue;
        }
        if (populate) {
          MarkerState marker = {};
          marker.form_id = form_id;
          marker.flags = kMarkerVisible | kMarkerDiscovered;
          marker.type = marker_type <= 14 ? marker_type : 0;
          marker.x = x;
          marker.y = y;
          marker.z = z;
          CopyComponentString(data, marker.name, sizeof(marker.name));
          g_marker_stage[g_marker_stage_count++] = marker;
        }
      }
    }
    cursor = next;
  }
  if (cursor) valid = false;
  ReleaseTemporarySimpleList(list);
  if (!valid) return false;
  *fingerprint = result;
  return true;
}

bool CaptureMap(void* player, MapProbe* output) {
  if (!player || !output || !IsReadable(player, 0x40)) return false;
  MapProbe map = {};
  const uintptr_t address = reinterpret_cast<uintptr_t>(player);
  std::memcpy(&map.heading, reinterpret_cast<void*>(address + 0x28),
              sizeof(float));
  std::memcpy(&map.x, reinterpret_cast<void*>(address + 0x2C), sizeof(float));
  std::memcpy(&map.y, reinterpret_cast<void*>(address + 0x30), sizeof(float));
  std::memcpy(&map.z, reinterpret_cast<void*>(address + 0x34), sizeof(float));
  if (!FiniteBounded(map.x) || !FiniteBounded(map.y) ||
      !FiniteBounded(map.z) || !FiniteBounded(map.heading) ||
      std::fabs(map.heading) > 1000.0f)
    return false;
  map.heading = std::fmod(map.heading * 57.2957795131f, 360.0f);
  if (map.heading < 0.0f) map.heading += 360.0f;
  void* cell = nullptr;
  std::memcpy(&cell, reinterpret_cast<void*>(address + 0x3C), sizeof(cell));
  map.interior = CellIsInterior(cell);
  void* map_cell = cell;
  if (map.interior) {
    // MapMenu 1.7.0.4 first uses PlayerCharacter+0x588 as its world-map
    // reference while indoors. That reference is the exterior door/location
    // anchor selected by Fallout's own transition/path state. If it is not yet
    // available (early load), retain the last published exterior point. Raw
    // interior actor coordinates are never exposed as world-map coordinates.
    void* native_anchor = nullptr;
    if (IsReadable(reinterpret_cast<void*>(address + 0x588),
                   sizeof(native_anchor)))
      std::memcpy(&native_anchor, reinterpret_cast<void*>(address + 0x588),
                  sizeof(native_anchor));
    float anchor_x = 0.0f;
    float anchor_y = 0.0f;
    float anchor_z = 0.0f;
    void* anchor_cell = nullptr;
    void* anchor_world = nullptr;
    if (ReadReferencePosition(native_anchor, &anchor_x, &anchor_y, &anchor_z,
                              &anchor_cell) &&
        !CellIsInterior(anchor_cell) &&
        IsReadable(reinterpret_cast<uint8_t*>(anchor_cell) + 0xC0,
                   sizeof(anchor_world))) {
      std::memcpy(&anchor_world,
                  reinterpret_cast<uint8_t*>(anchor_cell) + 0xC0,
                  sizeof(anchor_world));
      if (anchor_world && ValidFormType(anchor_world, 0x41)) {
        map.x = anchor_x;
        map.y = anchor_y;
        map.z = anchor_z;
        map_cell = anchor_cell;
        map.anchor_source = kMapAnchorInteriorDoor;
      }
    }
    if (map.anchor_source != kMapAnchorInteriorDoor &&
        g_exterior_map_anchor.valid) {
      map.x = g_exterior_map_anchor.x;
      map.y = g_exterior_map_anchor.y;
      map.z = g_exterior_map_anchor.z;
      map.anchor_source = kMapAnchorLastExterior;
    }
    if (map.anchor_source == kMapAnchorPlayer) {
      // Never leak raw interior coordinates to the world map. During the short
      // load window before Fallout publishes PlayerCharacter+0x588, retaining
      // the previous immutable map snapshot is both stable and faithful; the
      // next bounded sample will publish the native exterior anchor.
      LogRateLimited("world-map-interior-anchor", 5000, LogLevel::kDebug,
                     "[MAP] interior world-map anchor is not ready; previous exterior position retained");
      return false;
    }
  }
  if (map_cell && IsReadable(reinterpret_cast<uint8_t*>(map_cell) + 0xC0,
                             sizeof(void*))) {
    void* world = nullptr;
    std::memcpy(&world, reinterpret_cast<uint8_t*>(map_cell) + 0xC0,
                sizeof(world));
    // Vanilla falls back to TESDataHandler's first worldspace while the
    // player is in an interior. This keeps the world map available without
    // opening MapMenu or touching its global state.
    if (!world && IsReadable(reinterpret_cast<void*>(g_data_handler_global),
                             sizeof(void*))) {
      void* data_handler = nullptr;
      std::memcpy(&data_handler,
                  reinterpret_cast<void*>(g_data_handler_global),
                  sizeof(data_handler));
      if (data_handler &&
          IsReadable(reinterpret_cast<uint8_t*>(data_handler) + 0x0C,
                     sizeof(world)))
        std::memcpy(&world, reinterpret_cast<uint8_t*>(data_handler) + 0x0C,
                    sizeof(world));
    }
    if (world && ValidFormType(world, 0x41) &&
        g_world_map_data_resolver && g_world_map_texture_resolver) {
      auto* data = reinterpret_cast<const WorldMapDataLayout*>(
          Fo3dsCallThisPointer(g_world_map_data_resolver, world));
      const char* texture = reinterpret_cast<const char*>(
          Fo3dsCallThisPointer(g_world_map_texture_resolver, world));
      if (data && IsReadable(data, sizeof(*data))) {
        WorldMapDataLayout value = {};
        std::memcpy(&value, data, sizeof(value));
        const float min_x = static_cast<float>(value.northwest_x) * 4096.0f;
        const float max_y =
            static_cast<float>(static_cast<int32_t>(value.northwest_y) + 1) *
            4096.0f;
        const float max_x =
            static_cast<float>(static_cast<int32_t>(value.southeast_x) + 1) *
            4096.0f;
        const float min_y = static_cast<float>(value.southeast_y) * 4096.0f;
        if (value.usable_width > 0 && value.usable_width <= 8192 &&
            value.usable_height > 0 && value.usable_height <= 8192 &&
            max_x > min_x && max_y > min_y) {
          map.world_form_id = ReadFormId(world);
          map.min_x = min_x;
          map.max_y = max_y;
          map.max_x = max_x;
          map.min_y = min_y;
          map.usable_width = value.usable_width;
          map.usable_height = value.usable_height;
          if (!CopyBoundedEngineString(texture, map.texture_path,
                                       sizeof(map.texture_path)))
            std::snprintf(map.texture_path, sizeof(map.texture_path),
                          "Interface\\Worldmap\\Wasteland_1024_no_map.dds");
           map.world_valid = true;
          if (!map.interior) {
            g_exterior_map_anchor.world_form_id = map.world_form_id;
            g_exterior_map_anchor.x = map.x;
            g_exterior_map_anchor.y = map.y;
            g_exterior_map_anchor.z = map.z;
            g_exterior_map_anchor.valid = true;
          }
        }
      }
    }
  }
  map.valid = true;
  *output = map;
  return true;
}

bool ReadInventoryEntry(const void* raw_entry, InventoryEntryData* entry,
                        uint8_t* form_type, uint32_t* form_id) {
  if (!raw_entry || !entry || !form_type || !form_id ||
      !AlignedPointer(raw_entry) ||
      !IsReadable(raw_entry, sizeof(InventoryEntryData)))
    return false;
  std::memcpy(entry, raw_entry, sizeof(*entry));
  if (!AlignedPointer(entry->extend_data) || !AlignedPointer(entry->form) ||
      entry->count_delta < -kMaxInventoryStackCount ||
      entry->count_delta > kMaxInventoryStackCount || !entry->form ||
      !IsReadable(entry->form, 0x10))
    return false;
  const uintptr_t form = reinterpret_cast<uintptr_t>(entry->form);
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(form);
  const uint8_t type = *reinterpret_cast<const uint8_t*>(form + 0x04);
  const uint32_t id = *reinterpret_cast<const uint32_t*>(form + 0x0C);
  if (!g_build.rdata.Contains(vtable, sizeof(uintptr_t)) || type == 0 ||
      type > 0x7F || id == 0 || id == 0xFFFFFFFFu)
    return false;
  *form_type = type;
  *form_id = id;
  return true;
}

bool CaptureInventory(void* player, bool populate,
                       InventoryFingerprint* fingerprint) {
  if (!player || !fingerprint || !g_container_getter ||
      !g_inventory_name_helper)
    return false;
  if (populate) {
    g_inventory_stage_count = 0;
    g_inventory_stage_caps = 0;
  }
  InventoryFingerprint result = {};
  CaptureBudget budget = {PerfNow(), 5000, 65536, 0};
  void* payload = Fo3dsCallThisPointer(
      g_container_getter,
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player) + 0x40));
  if (!payload) {
    *fingerprint = result;
    return true;
  }
  if (!AlignedPointer(payload) || !IsReadable(payload, 2 * sizeof(void*)))
    return false;
  void* list_head_raw = nullptr;
  void* owner = nullptr;
  std::memcpy(&list_head_raw, payload, sizeof(list_head_raw));
  std::memcpy(&owner, reinterpret_cast<const uint8_t*>(payload) + 4,
              sizeof(owner));
  if (!AlignedPointer(list_head_raw) || !AlignedPointer(owner) ||
      (owner && owner != player))
    return false;

  auto* head = reinterpret_cast<InventoryNode*>(list_head_raw);
  InventoryNode* cursor = head;
  InventoryNode* slow = head;
  InventoryNode* fast = head;
  uint32_t skipped_names = 0;
  while (cursor) {
    if (result.node_count >= kMaxInventoryNodes || !budget.Consume())
      return false;
    void* raw_entry = nullptr;
    InventoryNode* next = nullptr;
    if (!ReadInventoryNode(cursor, &raw_entry, &next)) return false;
    MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(cursor));
    MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(raw_entry));
    MixFingerprint(&result.hash, reinterpret_cast<uintptr_t>(next));
    ++result.node_count;

    if (raw_entry) {
      InventoryEntryData entry = {};
      uint8_t type = 0;
      uint32_t form_id = 0;
      if (!ReadInventoryEntry(raw_entry, &entry, &type, &form_id)) return false;
      MixFingerprint(&result.hash, form_id);
      MixFingerprint(&result.hash, type);
      MixFingerprint(&result.hash,
                     static_cast<uint32_t>(entry.count_delta));
      StackState stack = {};
      if (!InspectStackState(entry.extend_data, &stack, &result.hash,
                             &budget))
        return false;
      MixFingerprint(&result.hash, stack.flags);
      if (populate && form_id == 0x0000000Fu && entry.count_delta > 0)
        g_inventory_stage_caps = entry.count_delta;
      const ItemCategory category = CategoryForFormType(type);
      if (category != ItemCategory::kUnknown && entry.count_delta > 0) {
        ++result.visible_count;
        if (populate && g_inventory_stage_count < kMaxItems) {
          ItemState item = {};
          item.form_id = form_id;
          item.count = entry.count_delta;
          item.category = category;
          PopulateItemStats(entry.form, stack, &item);
          const char* name = reinterpret_cast<const char*>(
              Fo3dsCallThisPointer(g_inventory_name_helper, raw_entry));
          if (CopyBoundedEngineString(name, item.name, sizeof(item.name))) {
            g_inventory_stage[g_inventory_stage_count++] = item;
          } else {
            ++skipped_names;
          }
        }
      }
    }
    cursor = next;

    if (!AdvanceInventoryNode(slow, &slow) ||
        !AdvanceInventoryNode(fast, &fast) ||
        (fast && !AdvanceInventoryNode(fast, &fast)))
      return false;
    if (slow && fast && slow == fast) return false;
  }

  void* stable_head = nullptr;
  std::memcpy(&stable_head, payload, sizeof(stable_head));
  if (stable_head != head) return false;
  if (populate) {
    for (uint32_t weapon = 0; weapon < g_inventory_stage_count; ++weapon) {
      ItemState& item = g_inventory_stage[weapon];
      if (item.category != ItemCategory::kWeapon || !item.ammo_form_id)
        continue;
      item.ammo_count = 0;
      for (uint32_t ammo = 0; ammo < g_inventory_stage_count; ++ammo) {
        const ItemState& candidate = g_inventory_stage[ammo];
        if (candidate.category == ItemCategory::kAmmo &&
            candidate.form_id == item.ammo_form_id) {
          item.ammo_count = candidate.count;
          break;
        }
      }
    }
  }
  if (populate && skipped_names)
    LogRateLimited("inventory-name-skip", 5000, LogLevel::kWarning,
                   "[INVENTORY] omitted %u entries with unavailable or malformed localized names",
                   skipped_names);
  *fingerprint = result;
  return true;
}

bool DataFingerprintsEqual(const DataFingerprint& left,
                           const DataFingerprint& right) {
  return left.hash == right.hash && left.quest_count == right.quest_count &&
         left.objective_count == right.objective_count &&
         left.perk_count == right.perk_count;
}

bool RadioFingerprintsEqual(const RadioFingerprint& left,
                            const RadioFingerprint& right) {
  return left.hash == right.hash &&
         left.station_count == right.station_count &&
         left.enabled == right.enabled;
}

bool EffectFingerprintsEqual(const EffectFingerprint& left,
                             const EffectFingerprint& right) {
  return left.hash == right.hash &&
         left.effect_count == right.effect_count;
}

bool MarkerFingerprintsEqual(const MarkerFingerprint& left,
                             const MarkerFingerprint& right) {
  return left.hash == right.hash && left.marker_count == right.marker_count;
}

bool ReadRadioReference(void* reference, void** base_form,
                        uint32_t* reference_id, uint32_t* base_id) {
  if (!reference || !base_form || !reference_id || !base_id ||
      !AlignedPointer(reference) || !IsReadable(reference, 0x20))
    return false;
  const uintptr_t address = reinterpret_cast<uintptr_t>(reference);
  const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(address);
  if (!g_build.rdata.Contains(vtable, sizeof(uintptr_t))) return false;
  void* base = nullptr;
  std::memcpy(&base, reinterpret_cast<void*>(address + 0x1C), sizeof(base));
  const uint32_t ref_id = ReadFormId(reference);
  const uint32_t form_id = ReadFormId(base);
  if (!base || !AlignedPointer(base) || !ref_id || !form_id) return false;
  *base_form = base;
  *reference_id = ref_id;
  *base_id = form_id;
  return true;
}

bool DestroyRadioCandidateNodes(RawListNode* first) {
  if (!ValidateRawListChain(first)) return false;
  RawListNode* cursor = first;
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    void* ignored = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &ignored, &next) ||
        !WritableMemory(cursor, sizeof(RawListNode))) {
      LogRateLimited("radio-list-cleanup", 5000, LogLevel::kError,
                     "[RADIO] temporary candidate list failed cleanup validation");
      return false;
    }
    RawListNode* null_next = nullptr;
    std::memcpy(reinterpret_cast<uint8_t*>(cursor) + sizeof(void*),
                &null_next, sizeof(null_next));
    Fo3dsCallThisVoidNoArgs(g_list_node_destructor, cursor);
    Fo3dsCallThisFree(g_heap_free,
                      reinterpret_cast<void*>(g_heap_manager), cursor);
    cursor = next;
  }
  return cursor == nullptr;
}

bool CaptureRadio(void* player, bool populate,
                  RadioFingerprint* fingerprint) {
  if (!player || !fingerprint || !g_radio_candidate_builder ||
      !g_list_node_destructor || !g_heap_free || !g_heap_manager ||
      !g_selected_radio_global || !g_radio_enabled_global)
    return false;
  if (g_radio_scan_disabled) return false;
  if (populate) g_radio_stage_count = 0;

  uint8_t enabled = 0;
  std::memcpy(&enabled, reinterpret_cast<void*>(g_radio_enabled_global), 1);
  enabled = enabled ? 1 : 0;
  void* selected_entry = nullptr;
  void* selected_reference = nullptr;
  void* selected_base = nullptr;
  uint32_t ignored_ref_id = 0;
  uint32_t ignored_base_id = 0;
  std::memcpy(&selected_entry,
              reinterpret_cast<void*>(g_selected_radio_global),
              sizeof(selected_entry));
  if (enabled && selected_entry && AlignedPointer(selected_entry) &&
      IsReadable(selected_entry, sizeof(void*))) {
    std::memcpy(&selected_reference, selected_entry,
                sizeof(selected_reference));
    ReadRadioReference(selected_reference, &selected_base, &ignored_ref_id,
                       &ignored_base_id);
  }

  RawListNode candidates = {};
  Fo3dsCallCdeclVoid3(g_radio_candidate_builder, player, &candidates, 0);
  if (!ValidateRawListChain(candidates.next, &candidates)) {
    g_radio_scan_disabled = true;
    LogRateLimited("radio-list-chain", 5000, LogLevel::kError,
                   "[RADIO] temporary candidate chain is invalid; scans disabled for this save generation");
    return false;
  }
  RadioFingerprint result = {};
  result.enabled = enabled;
  MixFingerprint(&result.hash, enabled);
  uint32_t seen_base_ids[kMaxStations] = {};
  uint32_t seen_count = 0;
  RawListNode* cursor = &candidates;
  bool valid = true;
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    void* reference = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &reference, &next)) {
      valid = false;
      break;
    }
    if (reference) {
      void* base = nullptr;
      uint32_t reference_id = 0;
      uint32_t base_id = 0;
      char name[96] = {};
      if (!ReadRadioReference(reference, &base, &reference_id, &base_id) ||
          !ReadFullName(base, name, sizeof(name))) {
        valid = false;
        break;
      }
      uint32_t existing = kMaxStations;
      for (uint32_t index = 0; index < seen_count; ++index)
        if (seen_base_ids[index] == base_id) existing = index;
      const bool tuned = enabled &&
          (reference == selected_reference || base == selected_base);
      if (existing < seen_count) {
        if (tuned) {
          MixFingerprint(&result.hash, 0x80000000u | base_id);
          if (populate) g_radio_stage[existing].flags |= kStationTuned;
        }
      } else if (seen_count < kMaxStations) {
        seen_base_ids[seen_count] = base_id;
        MixFingerprint(&result.hash, base_id);
        MixFingerprint(&result.hash, reference_id);
        MixFingerprint(&result.hash, tuned ? 1u : 0u);
        if (populate) {
          RadioState station = {};
          station.form_id = reference_id;
          station.flags = kStationAvailable | (tuned ? kStationTuned : 0u);
          station.signal = -1.0f;
          std::snprintf(station.name, sizeof(station.name), "%s", name);
          g_radio_stage[seen_count] = station;
        }
        ++seen_count;
      }
    }
    cursor = next;
  }
  if (cursor) valid = false;
  if (!DestroyRadioCandidateNodes(candidates.next)) {
    valid = false;
    g_radio_scan_disabled = true;
  }
  candidates.next = nullptr;
  result.station_count = seen_count;
  if (populate) g_radio_stage_count = seen_count;
  *fingerprint = result;
  return valid;
}

void* FindRadioCandidateByFormId(void* player, uint32_t form_id) {
  if (!player || !form_id || !g_radio_candidate_builder ||
      g_radio_scan_disabled)
    return nullptr;
  RawListNode candidates = {};
  Fo3dsCallCdeclVoid3(g_radio_candidate_builder, player, &candidates, 0);
  if (!ValidateRawListChain(candidates.next, &candidates)) {
    g_radio_scan_disabled = true;
    return nullptr;
  }
  void* found = nullptr;
  RawListNode* cursor = &candidates;
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    void* reference = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &reference, &next)) break;
    if (reference) {
      void* base = nullptr;
      uint32_t reference_id = 0;
      uint32_t base_id = 0;
      if (ReadRadioReference(reference, &base, &reference_id, &base_id) &&
          reference_id == form_id) {
        found = reference;
        break;
      }
    }
    cursor = next;
  }
  if (!DestroyRadioCandidateNodes(candidates.next)) {
    g_radio_scan_disabled = true;
    found = nullptr;
  }
  candidates.next = nullptr;
  return found;
}

void* ReadDataHandler() {
  if (!g_data_handler_global ||
      !IsReadable(reinterpret_cast<void*>(g_data_handler_global),
                  sizeof(void*)))
    return nullptr;
  void* handler = nullptr;
  std::memcpy(&handler, reinterpret_cast<void*>(g_data_handler_global),
              sizeof(handler));
  return handler && AlignedPointer(handler) && IsReadable(handler, 0x138)
             ? handler
             : nullptr;
}

bool CaptureObjectives(void* quest, bool populate, uint32_t quest_form_id,
                       DataFingerprint* result, uint32_t* begin,
                       uint32_t* count, bool* has_incomplete,
                       CaptureBudget* budget) {
  if (!quest || !result || !begin || !count || !has_incomplete ||
      !budget ||
      !IsReadable(reinterpret_cast<uint8_t*>(quest) + 0x4C,
                  sizeof(RawListNode)))
    return false;
  *begin = populate ? g_objective_stage_count : result->objective_count;
  *count = 0;
  *has_incomplete = false;
  auto* cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(quest) + 0x4C);
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    if (!budget->Consume()) return false;
    void* raw = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &raw, &next)) return false;
    if (raw) {
      if (!IsReadable(raw, 0x24)) return false;
      const uintptr_t address = reinterpret_cast<uintptr_t>(raw);
      const uintptr_t vtable = *reinterpret_cast<const uintptr_t*>(address);
      void* owner = nullptr;
      std::memcpy(&owner, reinterpret_cast<void*>(address + 0x10),
                  sizeof(owner));
      if (!g_build.rdata.Contains(vtable, sizeof(uintptr_t)) ||
          owner != quest)
        return false;
      uint32_t objective_id = 0;
      uint32_t status = 0;
      std::memcpy(&objective_id, reinterpret_cast<void*>(address + 4), 4);
      std::memcpy(&status, reinterpret_cast<void*>(address + 0x20), 4);
      // Bit 0 is the exact flag tested by vanilla GetObjectiveDisplayed;
      // bit 1 is the completed state tested by GetObjectiveCompleted.
      if (status & 1u) {
        if (!(status & 2u)) *has_incomplete = true;
        if (result->objective_count >= kMaxObjectives) {
          cursor = next;
          continue;
        }
        MixFingerprint(&result->hash, objective_id);
        MixFingerprint(&result->hash, status);
        ++result->objective_count;
        ++*count;
        if (populate) {
          ObjectiveState objective = {};
          objective.quest_form_id = quest_form_id;
          objective.objective_id = objective_id;
          objective.flags = kObjectiveDisplayed;
          if (status & 2u) objective.flags |= kObjectiveCompleted;
          CopyComponentString(reinterpret_cast<void*>(address + 4),
                              objective.text, sizeof(objective.text));
          g_objective_stage[g_objective_stage_count++] = objective;
        }
      }
    }
    cursor = next;
  }
  return !cursor;
}

bool CaptureQuestLogText(void* quest, int32_t current_stage,
                         char* destination, size_t capacity,
                         CaptureBudget* budget) {
  if (!quest || !destination || capacity < 2 || !budget ||
      !g_quest_log_text_resolver || current_stage < 0 ||
      !IsReadable(reinterpret_cast<uint8_t*>(quest) + 0x44,
                  sizeof(RawListNode)))
    return false;
  destination[0] = '\0';
  int32_t best_stage = -1;
  auto* stage_cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(quest) + 0x44);
  for (uint32_t visited = 0;
       stage_cursor && visited < kMaxDataNodes; ++visited) {
    if (!budget->Consume()) return false;
    void* raw_stage = nullptr;
    RawListNode* next_stage = nullptr;
    if (!ReadRawListNode(stage_cursor, &raw_stage, &next_stage)) return false;
    if (raw_stage) {
      if (!IsReadable(raw_stage, 0x0C)) return false;
      const uint8_t stage = *reinterpret_cast<const uint8_t*>(raw_stage);
      const uint8_t done = *reinterpret_cast<const uint8_t*>(
          reinterpret_cast<uint8_t*>(raw_stage) + 1);
      if (done && stage <= current_stage && stage >= best_stage) {
        auto* log_cursor = reinterpret_cast<RawListNode*>(
            reinterpret_cast<uint8_t*>(raw_stage) + 4);
        for (uint32_t log_visited = 0;
             log_cursor && log_visited < 64; ++log_visited) {
          if (!budget->Consume()) return false;
          void* entry = nullptr;
          RawListNode* next_log = nullptr;
          if (!ReadRawListNode(log_cursor, &entry, &next_log)) return false;
          if (entry) {
            if (!IsReadable(entry, 0x70)) return false;
            const uintptr_t address = reinterpret_cast<uintptr_t>(entry);
            uint32_t file_offset = 0;
            uint8_t has_log_text = 0;
            void* date = nullptr;
            void* owner = nullptr;
            std::memcpy(&file_offset, reinterpret_cast<void*>(address + 0x60),
                        sizeof(file_offset));
            std::memcpy(&has_log_text,
                        reinterpret_cast<void*>(address + 0x65),
                        sizeof(has_log_text));
            std::memcpy(&date, reinterpret_cast<void*>(address + 0x68),
                        sizeof(date));
            std::memcpy(&owner, reinterpret_cast<void*>(address + 0x6C),
                        sizeof(owner));
            if (file_offset && has_log_text && date && owner == quest) {
              const char* text = reinterpret_cast<const char*>(
                  Fo3dsCallThisPointer1(
                      g_quest_log_text_resolver, entry,
                      static_cast<uint32_t>(reinterpret_cast<uintptr_t>(quest))));
              char candidate[320] = {};
              if (CopyBoundedEngineString(text, candidate,
                                          sizeof(candidate))) {
                std::snprintf(destination, capacity, "%s", candidate);
                best_stage = stage;
              }
            }
          }
          log_cursor = next_log;
        }
        if (log_cursor) return false;
      }
    }
    stage_cursor = next_stage;
  }
  return !stage_cursor && destination[0] != '\0';
}

bool CaptureGameData(void* player, bool populate,
                     DataFingerprint* fingerprint) {
  if (!player || !fingerprint || !g_has_perk_eval || !g_get_stage_eval)
    return false;
  void* handler = ReadDataHandler();
  if (!handler) return false;
  if (populate) {
    g_quest_stage_count = 0;
    g_objective_stage_count = 0;
    g_perk_stage_count = 0;
  }
  DataFingerprint result = {};
  CaptureBudget budget = {PerfNow(), 10000, 65536, 0};
  void* tracked_quest = nullptr;
  if (IsReadable(reinterpret_cast<uint8_t*>(player) + 0x614,
                 sizeof(void*)))
    std::memcpy(&tracked_quest,
                reinterpret_cast<uint8_t*>(player) + 0x614,
                sizeof(tracked_quest));

  auto* quest_cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(handler) + 0xD4);
  for (uint32_t visited = 0;
       quest_cursor && visited < kMaxDataNodes; ++visited) {
    if (!budget.Consume()) return false;
    void* quest = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(quest_cursor, &quest, &next)) return false;
    if (quest) {
      if (!ValidFormType(quest, 0x47) || !IsReadable(quest, 0x6C))
        return false;
      char title[128] = {};
      if (ReadFullName(quest, title, sizeof(title))) {
        if (result.quest_count >= kMaxQuests) {
          quest_cursor = next;
          continue;
        }
        const uint32_t form_id = ReadFormId(quest);
        const bool tracked = quest == tracked_quest;
        // +0x3C contains definition flags, not a proven runtime-active byte.
        // The stable player-facing condition is the same state the Pip-Boy
        // exposes: a tracked quest or at least one displayed, incomplete
        // objective. This includes legitimate flag-zero quests and excludes
        // completed/system records without guessing unknown flag meanings.
        DataFingerprint quest_result = result;
        const uint32_t objective_stage_before = g_objective_stage_count;
        uint32_t objective_begin = 0;
        uint32_t objective_count = 0;
        bool has_incomplete = false;
        if (!CaptureObjectives(quest, populate, form_id, &quest_result,
                               &objective_begin, &objective_count,
                               &has_incomplete, &budget))
          return false;
        if (!tracked && !has_incomplete) {
          if (populate) g_objective_stage_count = objective_stage_before;
          quest_cursor = next;
          continue;
        }
        double evaluated_stage = 0.0;
        if (!g_get_stage_eval(nullptr, quest, nullptr, &evaluated_stage) ||
            !std::isfinite(evaluated_stage) || evaluated_stage < 0.0 ||
            evaluated_stage > static_cast<double>(INT32_MAX))
          return false;
        const int32_t current_stage =
            static_cast<int32_t>(std::lround(evaluated_stage));
        MixFingerprint(&quest_result.hash, form_id);
        MixFingerprint(&quest_result.hash, tracked ? 1u : 0u);
        MixFingerprint(&quest_result.hash, current_stage);
        ++quest_result.quest_count;
        result = quest_result;
        if (populate) {
          QuestState value = {};
          value.form_id = form_id;
          value.flags = kQuestActive | (tracked ? kQuestTracked : 0u);
          value.stage = current_stage;
          value.objective_begin = objective_begin;
          value.objective_count = objective_count;
          std::snprintf(value.title, sizeof(value.title), "%s", title);
          ReadIconPath(quest, value.icon_path, sizeof(value.icon_path));
          CaptureQuestLogText(quest, current_stage, value.description,
                              sizeof(value.description), &budget);
          g_quest_stage[g_quest_stage_count++] = value;
        }
      }
    }
    quest_cursor = next;
  }
  if (quest_cursor) return false;

  auto* perk_cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(handler) + 0x11C);
  for (uint32_t visited = 0;
       perk_cursor && visited < kMaxDataNodes; ++visited) {
    if (!budget.Consume()) return false;
    void* perk = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(perk_cursor, &perk, &next)) return false;
    if (perk) {
      if (!ValidFormType(perk, 0x56) || !IsReadable(perk, 0x50))
        return false;
      double rank = 0.0;
      if (g_has_perk_eval(player, perk, nullptr, &rank) &&
          std::isfinite(rank) && rank > 0.0 && rank <= 100.0 &&
          result.perk_count < kMaxPerks) {
        const uint32_t form_id = ReadFormId(perk);
        const uint32_t integer_rank =
            static_cast<uint32_t>(std::lround(rank));
        char name[96] = {};
        if (!ReadFullName(perk, name, sizeof(name))) {
          perk_cursor = next;
          continue;
        }
        MixFingerprint(&result.hash, form_id);
        MixFingerprint(&result.hash, integer_rank);
        ++result.perk_count;
        if (populate) {
          PerkState value = {};
          value.form_id = form_id;
          value.rank = integer_rank;
          std::snprintf(value.name, sizeof(value.name), "%s", name);
          ReadDescription(perk, reinterpret_cast<uintptr_t>(perk) + 0x24,
                          value.description, sizeof(value.description));
          ReadIconPath(perk, value.icon_path, sizeof(value.icon_path));
          g_perk_stage[g_perk_stage_count++] = value;
        }
      }
    }
    perk_cursor = next;
  }
  if (perk_cursor) return false;
  *fingerprint = result;
  return true;
}

void* FindQuestByFormId(uint32_t form_id) {
  void* handler = ReadDataHandler();
  if (!handler || !form_id) return nullptr;
  auto* cursor = reinterpret_cast<RawListNode*>(
      reinterpret_cast<uint8_t*>(handler) + 0xD4);
  for (uint32_t visited = 0; cursor && visited < kMaxDataNodes; ++visited) {
    void* quest = nullptr;
    RawListNode* next = nullptr;
    if (!ReadRawListNode(cursor, &quest, &next)) return nullptr;
    if (quest && ValidFormType(quest, 0x47) && ReadFormId(quest) == form_id)
      return quest;
    cursor = next;
  }
  return nullptr;
}

bool ResolveInventoryActionTarget(void* player, uint32_t requested_form_id,
                                  void** form, ItemCategory* category,
                                  uint16_t* flags) {
  if (!player || !requested_form_id || !form || !category || !flags)
    return false;
  void* payload = Fo3dsCallThisPointer(
      g_container_getter,
      reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(player) + 0x40));
  if (!payload || !AlignedPointer(payload) ||
      !IsReadable(payload, 2 * sizeof(void*)))
    return false;
  void* list_head_raw = nullptr;
  void* owner = nullptr;
  std::memcpy(&list_head_raw, payload, sizeof(list_head_raw));
  std::memcpy(&owner, reinterpret_cast<const uint8_t*>(payload) + 4,
              sizeof(owner));
  if (!AlignedPointer(list_head_raw) || !AlignedPointer(owner) ||
      (owner && owner != player))
    return false;

  auto* cursor = reinterpret_cast<InventoryNode*>(list_head_raw);
  CaptureBudget budget = {PerfNow(), 3000, 32768, 0};
  for (uint32_t node_count = 0; cursor && node_count < kMaxInventoryNodes;
       ++node_count) {
    if (!budget.Consume()) return false;
    void* raw_entry = nullptr;
    InventoryNode* next = nullptr;
    if (!ReadInventoryNode(cursor, &raw_entry, &next)) return false;
    if (raw_entry) {
      InventoryEntryData entry = {};
      uint8_t type = 0;
      uint32_t form_id = 0;
      if (!ReadInventoryEntry(raw_entry, &entry, &type, &form_id)) return false;
      if (form_id == requested_form_id && entry.count_delta > 0) {
        StackState stack = {};
        uint64_t ignored_fingerprint = 1469598103934665603ull;
        if (!InspectStackState(entry.extend_data, &stack,
                               &ignored_fingerprint, &budget))
          return false;
        *form = entry.form;
        *category = CategoryForFormType(type);
        *flags = stack.flags;
        return *category != ItemCategory::kUnknown;
      }
    }
    cursor = next;
  }
  return false;
}

bool ProcessPendingCommands(void* player) {
  // Publish one acknowledgement per snapshot. Processing several requests in
  // one tick would overwrite earlier acknowledgements and make the panel
  // guess whether its action ran.
  constexpr uint32_t kMaxCommandsPerTick = 1;
  bool refresh_inventory = false;
  bool action_changed = false;
  for (uint32_t processed = 0; processed < kMaxCommandsPerTick; ++processed) {
    Command command = {};
    if (!PollCommand(&command)) break;
    action_changed = true;
    g_last_action_command = command;
    g_last_action_result = ActionResult::kRejected;
    if (command.version != kSnapshotVersion ||
        command.world_epoch != g_world_epoch) {
      Log(LogLevel::kWarning,
          "[ACTION] rejected stale request=%llu form=%08X request_epoch=%llu current_epoch=%llu",
          static_cast<unsigned long long>(command.request_id), command.form_id,
          static_cast<unsigned long long>(command.world_epoch),
          static_cast<unsigned long long>(g_world_epoch));
      continue;
    }

    if (command.type == CommandType::kTrackQuest) {
      void* quest = FindQuestByFormId(command.form_id);
      if (!quest || !g_active_quest_setter) {
        Log(LogLevel::kWarning,
            "[ACTION] track-quest rejected request=%llu form=%08X live quest/native setter unavailable",
            static_cast<unsigned long long>(command.request_id),
            command.form_id);
        continue;
      }
      Fo3dsCallThisVoid1(g_active_quest_setter, player, quest);
      g_last_action_result = ActionResult::kDispatched;
      g_have_data = false;
      g_next_data_scan_ms = 0;
      Log(LogLevel::kInfo,
          "[ACTION] dispatched request=%llu operation=track_quest form=%08X",
          static_cast<unsigned long long>(command.request_id),
          command.form_id);
      continue;
    }

    if (command.type == CommandType::kTuneRadio ||
        command.type == CommandType::kRadioOff) {
      const uint64_t started = PerfNow();
      bool accepted = false;
      if (g_radio_enable && g_radio_tune) {
        uint8_t was_enabled = 0;
        std::memcpy(&was_enabled,
                    reinterpret_cast<void*>(g_radio_enabled_global), 1);
        if (command.type == CommandType::kRadioOff) {
          g_radio_enable(false);
          uint8_t enabled = 1;
          std::memcpy(&enabled,
                      reinterpret_cast<void*>(g_radio_enabled_global), 1);
          accepted = enabled == 0;
        } else {
          void* station = FindRadioCandidateByFormId(player, command.form_id);
          if (station) {
            g_radio_enable(true);
            uint8_t enabled = 0;
            std::memcpy(&enabled,
                        reinterpret_cast<void*>(g_radio_enabled_global), 1);
            accepted = enabled != 0 && g_radio_tune(station, true);
            if (!accepted && !was_enabled) g_radio_enable(false);
          }
        }
      }
      g_have_radio = false;
      g_next_radio_scan_ms = 0;
      if (accepted) g_last_action_result = ActionResult::kDispatched;
      Log(accepted ? LogLevel::kInfo : LogLevel::kWarning,
          "[ACTION] %s request=%llu operation=%s form=%08X elapsed_us=%llu",
          accepted ? "dispatched" : "rejected",
          static_cast<unsigned long long>(command.request_id),
          command.type == CommandType::kRadioOff ? "radio_off" :
                                                   "tune_radio",
          command.form_id,
          static_cast<unsigned long long>(PerfElapsedMicros(started)));
      continue;
    }

    void* form = nullptr;
    ItemCategory category = ItemCategory::kUnknown;
    uint16_t flags = 0;
    if (!ResolveInventoryActionTarget(player, command.form_id, &form,
                                      &category, &flags)) {
      Log(LogLevel::kWarning,
          "[ACTION] request=%llu form=%08X is no longer in the live inventory",
          static_cast<unsigned long long>(command.request_id), command.form_id);
      refresh_inventory = true;
      continue;
    }

    const bool equipped = (flags & kItemEquipped) != 0;
    uintptr_t native = 0;
    const char* operation = nullptr;
    if (command.type == CommandType::kUnequipItem &&
        (category == ItemCategory::kWeapon ||
         category == ItemCategory::kApparel)) {
      if (!equipped) {
        Log(LogLevel::kInfo,
            "[ACTION] request=%llu form=%08X already unequipped; refreshing UI",
            static_cast<unsigned long long>(command.request_id), command.form_id);
        refresh_inventory = true;
        g_last_action_result = ActionResult::kDispatched;
        continue;
      }
      native = g_unequip_native;
      operation = "unequip";
    } else if ((command.type == CommandType::kEquipItem ||
                command.type == CommandType::kUseItem) &&
               (category == ItemCategory::kWeapon ||
                category == ItemCategory::kApparel ||
                category == ItemCategory::kAid)) {
      if (category != ItemCategory::kAid && equipped) {
        Log(LogLevel::kInfo,
            "[ACTION] request=%llu form=%08X already equipped; refreshing UI",
            static_cast<unsigned long long>(command.request_id), command.form_id);
        refresh_inventory = true;
        g_last_action_result = ActionResult::kDispatched;
        continue;
      }
      native = g_equip_native;
      operation = category == ItemCategory::kAid ? "use" : "equip";
    } else {
      Log(LogLevel::kWarning,
          "[ACTION] rejected unsupported request=%llu type=%u category=%u form=%08X",
          static_cast<unsigned long long>(command.request_id),
          static_cast<unsigned>(command.type), static_cast<unsigned>(category),
          command.form_id);
      continue;
    }

    const uint64_t started = PerfNow();
    Fo3dsCallThisAction(native, player, form, 1, nullptr, 1, 0, 1);
    const uint64_t elapsed = PerfElapsedMicros(started);
    refresh_inventory = true;
    g_last_action_result = ActionResult::kDispatched;
    Log(LogLevel::kInfo,
        "[ACTION] dispatched request=%llu operation=%s form=%08X elapsed_us=%llu",
        static_cast<unsigned long long>(command.request_id), operation,
        command.form_id, static_cast<unsigned long long>(elapsed));
  }
  if (refresh_inventory) {
    g_have_inventory = false;
    g_next_inventory_scan_ms = 0;
  }
  return action_changed;
}

void PublishUnavailable(Lifecycle lifecycle, const char* reason) {
  if (g_have_state && g_next.lifecycle == lifecycle &&
      g_next.world_epoch == g_world_epoch && g_next.capabilities == 0)
    return;
  SecureZeroMemory(&g_next, sizeof(g_next));
  g_next.size = sizeof(Snapshot);
  g_next.sequence = ++g_sequence;
  g_next.world_epoch = g_world_epoch;
  g_next.lifecycle = lifecycle;
  g_next.supported_build = 1;
  std::snprintf(g_next.build_label, sizeof(g_next.build_label), "%s", reason);
  if (PublishSnapshot(g_next)) {
    g_have_state = true;
    g_have_vitals = false;
    g_have_inventory = false;
    g_have_data = false;
    g_have_notes = false;
    g_have_general_stats = false;
    g_have_map = false;
    g_exterior_map_anchor = {};
    g_have_radio = false;
    g_have_effects = false;
    g_have_markers = false;
    g_next_inventory_scan_ms = 0;
    g_next_data_scan_ms = 0;
    g_next_note_scan_ms = 0;
    g_next_map_scan_ms = 0;
    g_next_radio_scan_ms = 0;
    g_next_effect_scan_ms = 0;
    g_next_marker_scan_ms = 0;
    g_next_condition_scan_ms = 0;
    g_next_special_scan_ms = 0;
    g_next_skills_scan_ms = 0;
    g_next_general_stats_scan_ms = 0;
  }
}

VOID CALLBACK GameTimerProc(HWND, UINT, UINT_PTR timer_id, DWORD) {
  if (timer_id != kGameTimerId) return;
  // SetTimer executes on Fallout's main window thread.  Its callback duration
  // proves how much work this mod contributes; the interval between callbacks
  // separately exposes time the game thread spends blocked elsewhere.  This
  // distinction is essential for the enemy/combat slowdown: a cheap callback
  // with a large gap cannot honestly be blamed on the state sampler.
  const uint64_t now = PerfNow();
  if (g_previous_game_tick)
    RecordPerformance(PerfDomain::kGameTickGap,
                      PerfElapsedMicros(g_previous_game_tick));
  g_previous_game_tick = now;
  OnGameThreadTick();
}

}  // namespace

bool InitializeEngineBridge(const BuildIdentity& build) {
  ScopedPerformance scan(PerfDomain::kBootstrapScan);
  if (!build.valid_pe || !build.is_i386 || !build.known_candidate) {
    Log(LogLevel::kError,
        "[BRIDGE] executable is not the exact researched 1.7.0.4 file; all engine access disabled");
    return false;
  }
  g_build = build;
  g_module_base = build.image.begin;
  const bool table_in_image = build.data.Contains(
      g_module_base + sig::kScriptTableRva, sizeof(CommandDescriptor));
  const bool actor_value = ValidateDescriptor(
      sig::kGetActorValueDescriptorRva, "GetActorValue", 0x100E, 1,
      sig::kGetActorValueExecuteRva);
  const bool get_stage = ValidateDescriptor(
      sig::kGetStageDescriptorRva, "GetStage", 0x103A, 1,
      sig::kGetStageExecuteRva);
  const bool equip = ValidateDescriptor(sig::kEquipDescriptorRva, "EquipItem",
                                         0x10EE, 3, sig::kEquipExecuteRva);
  const bool unequip = ValidateDescriptor(
      sig::kUnequipDescriptorRva, "UnequipItem", 0x10EF, 3,
      sig::kUnequipExecuteRva);
  const bool has_perk = ValidateDescriptor(
      sig::kHasPerkDescriptorRva, "HasPerk", 0x11C1, 1,
      sig::kHasPerkExecuteRva);
  const bool get_xp_next = ValidateDescriptor(
      sig::kGetXpForNextLevelDescriptorRva, "GetXPForNextLevel", 0x1215, 0,
      sig::kGetXpForNextLevelExecuteRva);
  const bool get_pc_misc_stat = ValidateDescriptor(
      sig::kGetPcMiscStatDescriptorRva, "GetPCMiscStat", 0x1138, 1,
      sig::kGetPcMiscStatExecuteRva);
  const bool pipboy_radio = ValidateDescriptor(
      sig::kPipboyRadioDescriptorRva, "PipboyRadio", 0x11A7, 2,
      sig::kPipboyRadioExecuteRva);
  const bool pipboy_radio_off = ValidateDescriptor(
      sig::kPipboyRadioOffDescriptorRva, "PipBoyRadioOff", 0x1224, 0,
      sig::kPipboyRadioOffExecuteRva);
  const auto* has_perk_descriptor =
      reinterpret_cast<const CommandDescriptor*>(
          g_module_base + sig::kHasPerkDescriptorRva);
  const bool has_perk_eval = has_perk &&
      reinterpret_cast<uintptr_t>(has_perk_descriptor->eval) ==
          g_module_base + sig::kHasPerkEvalRva &&
      g_build.text.Contains(
          reinterpret_cast<uintptr_t>(has_perk_descriptor->eval));
  const auto* get_xp_next_descriptor =
      reinterpret_cast<const CommandDescriptor*>(
          g_module_base + sig::kGetXpForNextLevelDescriptorRva);
  const bool get_xp_next_eval = get_xp_next &&
      reinterpret_cast<uintptr_t>(get_xp_next_descriptor->eval) ==
          g_module_base + sig::kGetXpForNextLevelEvalRva &&
      g_build.text.Contains(
          reinterpret_cast<uintptr_t>(get_xp_next_descriptor->eval));
  const auto* get_pc_misc_stat_descriptor =
      reinterpret_cast<const CommandDescriptor*>(
          g_module_base + sig::kGetPcMiscStatDescriptorRva);
  const bool get_pc_misc_stat_eval = get_pc_misc_stat &&
      reinterpret_cast<uintptr_t>(get_pc_misc_stat_descriptor->eval) ==
          g_module_base + sig::kGetPcMiscStatEvalRva &&
      g_build.text.Contains(
          reinterpret_cast<uintptr_t>(get_pc_misc_stat_descriptor->eval));
  const auto* get_stage_descriptor =
      reinterpret_cast<const CommandDescriptor*>(
          g_module_base + sig::kGetStageDescriptorRva);
  const bool get_stage_eval = get_stage &&
      reinterpret_cast<uintptr_t>(get_stage_descriptor->eval) ==
          g_module_base + sig::kGetStageEvalRva &&
      g_build.text.Contains(
          reinterpret_cast<uintptr_t>(get_stage_descriptor->eval));

  const uintptr_t player_match =
      FindUnique(sig::kPlayerPattern, sig::kPlayerMask);
  int32_t equip_displacement = 0;
  uint32_t player_operand = 0;
  if (player_match) {
    std::memcpy(&equip_displacement,
                reinterpret_cast<const void*>(player_match + 17),
                sizeof(equip_displacement));
    std::memcpy(&player_operand,
                reinterpret_cast<const void*>(player_match + 23),
                sizeof(player_operand));
  }
  const uintptr_t decoded_equip = player_match
      ? player_match + 21 + static_cast<intptr_t>(equip_displacement) : 0;
  const uintptr_t equip_handler = g_module_base + sig::kEquipExecuteRva;
  const bool player_pattern = player_match >= equip_handler &&
      player_match < equip_handler + 0x500 &&
      decoded_equip == g_module_base + sig::kEquipNativeRva &&
      player_operand == g_module_base + sig::kPlayerGlobalRva;

  const uintptr_t unequip_match =
      FindUnique(sig::kUnequipPattern, sig::kUnequipMask);
  int32_t unequip_displacement = 0;
  uint32_t unequip_player_operand = 0;
  if (unequip_match) {
    std::memcpy(&unequip_displacement,
                reinterpret_cast<const void*>(unequip_match + 17),
                sizeof(unequip_displacement));
    std::memcpy(&unequip_player_operand,
                reinterpret_cast<const void*>(unequip_match + 23),
                sizeof(unequip_player_operand));
  }
  const uintptr_t decoded_unequip = unequip_match
      ? unequip_match + 21 + static_cast<intptr_t>(unequip_displacement) : 0;
  const uintptr_t unequip_handler =
      g_module_base + sig::kUnequipExecuteRva;
  const bool unequip_pattern = unequip_match >= unequip_handler &&
      unequip_match < unequip_handler + 0x500 &&
      decoded_unequip == g_module_base + sig::kUnequipNativeRva &&
      unequip_player_operand == g_module_base + sig::kPlayerGlobalRva;

  const uintptr_t actor_match =
      FindUnique(sig::kActorValuePattern, sig::kActorValueMask);
  const auto* actor_descriptor = reinterpret_cast<const CommandDescriptor*>(
      g_module_base + sig::kGetActorValueDescriptorRva);
  const uintptr_t actor_eval = actor_value
      ? reinterpret_cast<uintptr_t>(actor_descriptor->eval) : 0;
  const bool actor_pattern = actor_eval == g_module_base + sig::kGetActorValueEvalRva &&
      actor_match >= actor_eval && actor_match < actor_eval + 0x500;

  const uintptr_t container_match =
      FindUnique(sig::kContainerPattern, sig::kContainerMask);
  const bool container_getter =
      container_match == g_module_base + sig::kContainerChangesGetterRva;

  const uintptr_t name_match = FindUnique(
      sig::kInventoryEntryNamePattern, sig::kInventoryEntryNameMask);
  uint32_t full_name_type = 0;
  uint32_t bound_object_type = 0;
  int32_t cast_displacement = 0;
  if (name_match) {
    std::memcpy(&full_name_type,
                reinterpret_cast<const void*>(name_match + 3), 4);
    std::memcpy(&bound_object_type,
                reinterpret_cast<const void*>(name_match + 8), 4);
    std::memcpy(&cast_displacement,
                reinterpret_cast<const void*>(name_match + 18), 4);
  }
  const uintptr_t decoded_cast = name_match
      ? name_match + 22 + static_cast<intptr_t>(cast_displacement) : 0;
  const bool inventory_name =
      name_match == g_module_base + sig::kInventoryEntryNameRva &&
      full_name_type == g_module_base + sig::kTesFullNameTypeDescriptorRva &&
      bound_object_type ==
          g_module_base + sig::kTesBoundObjectTypeDescriptorRva &&
      decoded_cast == g_module_base + sig::kDynamicCastRva &&
      g_build.text.Contains(decoded_cast);

  const bool item_rtti =
      ValidateTypeDescriptor(sig::kTesFormTypeDescriptorRva,
                             ".?AVTESForm@@") &&
      ValidateTypeDescriptor(sig::kTesIconTypeDescriptorRva,
                             ".?AVTESIcon@@") &&
      ValidateTypeDescriptor(sig::kTesWeightFormTypeDescriptorRva,
                             ".?AVTESWeightForm@@") &&
      ValidateTypeDescriptor(sig::kTesValueFormTypeDescriptorRva,
                             ".?AVTESValueForm@@") &&
      ValidateTypeDescriptor(sig::kTesHealthFormTypeDescriptorRva,
                             ".?AVTESHealthForm@@") &&
      ValidateTypeDescriptor(sig::kTesAttackDamageFormTypeDescriptorRva,
                             ".?AVTESAttackDamageForm@@") &&
      ValidateTypeDescriptor(sig::kTesObjectArmoTypeDescriptorRva,
                             ".?AVTESObjectARMO@@");
  const bool data_rtti =
      ValidateTypeDescriptor(sig::kTesQuestTypeDescriptorRva,
                             ".?AVTESQuest@@") &&
      ValidateTypeDescriptor(sig::kBgsNoteTypeDescriptorRva,
                             ".?AVBGSNote@@") &&
      ValidateTypeDescriptor(sig::kBgsPerkTypeDescriptorRva,
                             ".?AVBGSPerk@@") &&
      ValidateTypeDescriptor(sig::kTesWorldSpaceTypeDescriptorRva,
                             ".?AVTESWorldSpace@@") &&
      ValidateTypeDescriptor(sig::kTesObjectCellTypeDescriptorRva,
                             ".?AVTESObjectCELL@@") &&
      ValidateTypeDescriptor(sig::kBgsQuestObjectiveTypeDescriptorRva,
                             ".?AVBGSQuestObjective@@");

  const uintptr_t data_handler_match =
      FindUnique(sig::kDataHandlerPattern, sig::kDataHandlerMask);
  uint32_t data_handler_operand = 0;
  if (data_handler_match)
    std::memcpy(&data_handler_operand,
                reinterpret_cast<const void*>(data_handler_match + 47), 4);
  const bool data_handler =
      data_handler_match == g_module_base + 0x002DC3A1u &&
      data_handler_operand ==
          g_module_base + sig::kDataHandlerGlobalRva;

  const uintptr_t radio_execute =
      g_module_base + sig::kPipboyRadioExecuteRva;
  int32_t radio_tune_displacement = 0;
  if (g_build.text.Contains(radio_execute + 0x122, 4))
    std::memcpy(&radio_tune_displacement,
                reinterpret_cast<const void*>(radio_execute + 0x122), 4);
  const uintptr_t decoded_radio_tune =
      radio_execute + 0x126 + static_cast<intptr_t>(radio_tune_displacement);
  const uintptr_t radio_off_execute =
      g_module_base + sig::kPipboyRadioOffExecuteRva;
  int32_t radio_off_displacement = 0;
  if (g_build.text.Contains(radio_off_execute + 3, 4))
    std::memcpy(&radio_off_displacement,
                reinterpret_cast<const void*>(radio_off_execute + 3), 4);
  const uintptr_t decoded_radio_off =
      radio_off_execute + 7 + static_cast<intptr_t>(radio_off_displacement);
  const uintptr_t radio_tune_native =
      g_module_base + sig::kRadioTuneRva;
  int32_t builder_displacement = 0;
  int32_t node_destructor_displacement = 0;
  int32_t heap_free_displacement = 0;
  uint32_t heap_manager_operand = 0;
  uint32_t radio_enabled_operand = 0;
  uint32_t radio_transition_operand = 0;
  uint32_t selected_radio_operand = 0;
  if (g_build.text.Contains(radio_tune_native, 0x113)) {
    std::memcpy(&builder_displacement,
                reinterpret_cast<const void*>(radio_tune_native + 0x76), 4);
    std::memcpy(&node_destructor_displacement,
                reinterpret_cast<const void*>(radio_tune_native + 0xB5), 4);
    std::memcpy(&heap_manager_operand,
                reinterpret_cast<const void*>(radio_tune_native + 0xBB), 4);
    std::memcpy(&heap_free_displacement,
                reinterpret_cast<const void*>(radio_tune_native + 0xC7), 4);
    std::memcpy(&radio_enabled_operand,
                reinterpret_cast<const void*>(radio_tune_native + 0x2A), 4);
    std::memcpy(&radio_transition_operand,
                reinterpret_cast<const void*>(radio_tune_native + 0x37), 4);
    std::memcpy(&selected_radio_operand,
                reinterpret_cast<const void*>(radio_tune_native + 0x10F), 4);
  }
  const uintptr_t decoded_builder =
      radio_tune_native + 0x7A + static_cast<intptr_t>(builder_displacement);
  const uintptr_t decoded_node_destructor =
      radio_tune_native + 0xB9 +
      static_cast<intptr_t>(node_destructor_displacement);
  const uintptr_t decoded_heap_free =
      radio_tune_native + 0xCB +
      static_cast<intptr_t>(heap_free_displacement);
  const bool radio_path = pipboy_radio && pipboy_radio_off &&
      decoded_radio_tune == g_module_base + sig::kRadioTuneRva &&
      decoded_radio_off == g_module_base + sig::kRadioEnableRva &&
      decoded_builder == g_module_base + sig::kRadioCandidateBuilderRva &&
      decoded_node_destructor ==
          g_module_base + sig::kListNodeDestructorRva &&
      decoded_heap_free == g_module_base + sig::kHeapFreeRva &&
      heap_manager_operand == g_module_base + sig::kHeapManagerRva &&
      radio_enabled_operand ==
          g_module_base + sig::kRadioEnabledGlobalRva &&
      radio_transition_operand ==
          g_module_base + sig::kRadioTransitionGlobalRva &&
      selected_radio_operand ==
          g_module_base + sig::kSelectedRadioGlobalRva &&
      WritableAddress(radio_enabled_operand, 1) &&
      WritableAddress(radio_transition_operand, 1) &&
      WritableAddress(selected_radio_operand, sizeof(void*)) &&
      WritableAddress(heap_manager_operand, 0x104);

  const uintptr_t magic_target_vtable =
      g_module_base + sig::kPlayerMagicTargetVtableRva;
  const uintptr_t active_effect_list_getter =
      g_module_base + sig::kGetActiveEffectListRva;
  const bool effect_path =
      g_build.rdata.Contains(magic_target_vtable,
                             3 * sizeof(uintptr_t)) &&
      IsReadable(reinterpret_cast<void*>(magic_target_vtable),
                 3 * sizeof(uintptr_t)) &&
      reinterpret_cast<const uintptr_t*>(magic_target_vtable)[2] ==
          active_effect_list_getter &&
      g_build.text.Contains(active_effect_list_getter, 7);
  const uintptr_t world_map_marker_list_builder =
      g_module_base + sig::kWorldMapMarkerListBuilderRva;
  const uintptr_t map_marker_data_getter =
      g_module_base + sig::kMapMarkerDataGetterRva;
  const uintptr_t simple_list_destructor =
      g_module_base + sig::kSimpleListDestructorRva;
  const bool marker_path =
      g_build.text.Contains(world_map_marker_list_builder, 9) &&
      g_build.text.Contains(map_marker_data_getter, 4) &&
      g_build.text.Contains(simple_list_destructor, 5);
  const uintptr_t world_map_data_resolver =
      g_module_base + sig::kWorldMapDataResolverRva;
  const uintptr_t world_map_texture_resolver =
      g_module_base + sig::kWorldMapTextureResolverRva;
  constexpr uint8_t kWorldMapDataResolver[] = {
      0x8B, 0x41, 0x70, 0x85, 0xC0, 0x74, 0x18, 0xF6,
      0x41, 0x4E, 0x04, 0x74, 0x12};
  constexpr uint8_t kWorldMapTextureResolver[] = {
      0x8B, 0x41, 0x70, 0x85, 0xC0, 0x74, 0x14, 0xF6,
      0x41, 0x4E, 0x04, 0x74, 0x0E};
  const bool world_map_path =
      g_build.text.Contains(world_map_data_resolver,
                            sizeof(kWorldMapDataResolver)) &&
      std::memcmp(reinterpret_cast<const void*>(world_map_data_resolver),
                  kWorldMapDataResolver, sizeof(kWorldMapDataResolver)) == 0 &&
      g_build.text.Contains(world_map_texture_resolver,
                            sizeof(kWorldMapTextureResolver)) &&
      std::memcmp(reinterpret_cast<const void*>(world_map_texture_resolver),
                  kWorldMapTextureResolver,
                  sizeof(kWorldMapTextureResolver)) == 0;
  const uintptr_t note_image_payload_getter =
      g_module_base + sig::kNoteImagePayloadGetterRva;
  const uintptr_t note_text_payload_getter =
      g_module_base + sig::kNoteTextPayloadGetterRva;
  constexpr uint8_t kNoteImageAccessor[] = {
      0x80, 0x79, 0x7C, 0x02, 0x75, 0x04, 0x8B, 0x41, 0x6C, 0xC3,
      0x33, 0xC0, 0xC3};
  constexpr uint8_t kNoteTextAccessor[] = {
      0x80, 0x79, 0x7C, 0x01, 0x75, 0x04, 0x8B, 0x41, 0x6C, 0xC3,
      0x33, 0xC0, 0xC3};
  const bool note_path =
      g_build.text.Contains(note_image_payload_getter,
                            sizeof(kNoteImageAccessor)) &&
      std::memcmp(reinterpret_cast<const void*>(note_image_payload_getter),
                  kNoteImageAccessor, sizeof(kNoteImageAccessor)) == 0 &&
      g_build.text.Contains(note_text_payload_getter,
                            sizeof(kNoteTextAccessor)) &&
      std::memcmp(reinterpret_cast<const void*>(note_text_payload_getter),
                  kNoteTextAccessor, sizeof(kNoteTextAccessor)) == 0;
  const uintptr_t active_quest_setter =
      g_module_base + sig::kActiveQuestSetterRva;
  constexpr uint8_t kActiveQuestSetterPrologue[] = {
      0x55, 0x8B, 0xEC, 0x8B, 0x45, 0x08,
      0x3B, 0x81, 0x14, 0x06, 0x00, 0x00};
  const bool active_quest_path =
      g_build.text.Contains(active_quest_setter,
                            sizeof(kActiveQuestSetterPrologue)) &&
      std::memcmp(reinterpret_cast<const void*>(active_quest_setter),
                  kActiveQuestSetterPrologue,
                   sizeof(kActiveQuestSetterPrologue)) == 0;
  const uintptr_t quest_log_text_resolver =
      g_module_base + sig::kQuestLogTextResolverRva;
  const uintptr_t quest_log_call =
      g_module_base + sig::kShowQuestLogTextCallRva;
  int32_t quest_log_displacement = 0;
  if (g_build.text.Contains(quest_log_call, 5))
    std::memcpy(&quest_log_displacement,
                reinterpret_cast<const void*>(quest_log_call + 1), 4);
  const uintptr_t decoded_quest_log =
      quest_log_call + 5 + static_cast<intptr_t>(quest_log_displacement);
  constexpr uint8_t kQuestLogTextResolverPrologue[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xEC, 0x10, 0xA1};
  const bool quest_log_path =
      g_build.text.Contains(quest_log_text_resolver,
                            sizeof(kQuestLogTextResolverPrologue)) &&
      std::memcmp(reinterpret_cast<const void*>(quest_log_text_resolver),
                  kQuestLogTextResolverPrologue,
                  sizeof(kQuestLogTextResolverPrologue)) == 0 &&
      g_build.text.Contains(quest_log_call - 3, 8) &&
      *reinterpret_cast<const uint8_t*>(quest_log_call) == 0xE8 &&
      *reinterpret_cast<const uint8_t*>(quest_log_call - 1) == 0x56 &&
      decoded_quest_log == quest_log_text_resolver;

  g_player_global = g_module_base + sig::kPlayerGlobalRva;
  g_data_handler_global = g_module_base + sig::kDataHandlerGlobalRva;
  g_player_vtable = g_module_base + sig::kPlayerPrimaryVtableRva;
  g_container_getter = g_module_base + sig::kContainerChangesGetterRva;
  g_inventory_name_helper = g_module_base + sig::kInventoryEntryNameRva;
  g_equip_native = g_module_base + sig::kEquipNativeRva;
  g_unequip_native = g_module_base + sig::kUnequipNativeRva;
  g_dynamic_cast = reinterpret_cast<DynamicCast>(
      g_module_base + sig::kDynamicCastRva);
  g_tes_form_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesFormTypeDescriptorRva);
  g_tes_full_name_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesFullNameTypeDescriptorRva);
  g_tes_icon_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesIconTypeDescriptorRva);
  g_tes_weight_form_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesWeightFormTypeDescriptorRva);
  g_tes_value_form_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesValueFormTypeDescriptorRva);
  g_tes_health_form_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesHealthFormTypeDescriptorRva);
  g_tes_attack_damage_form_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesAttackDamageFormTypeDescriptorRva);
  g_tes_object_armo_type = reinterpret_cast<const void*>(
      g_module_base + sig::kTesObjectArmoTypeDescriptorRva);
  g_has_perk_eval = reinterpret_cast<ConditionEval>(
      g_module_base + sig::kHasPerkEvalRva);
  g_get_xp_next_eval = reinterpret_cast<ConditionEval>(
      g_module_base + sig::kGetXpForNextLevelEvalRva);
  g_get_stage_eval = reinterpret_cast<ConditionEval>(
      g_module_base + sig::kGetStageEvalRva);
  g_get_pc_misc_stat_eval = reinterpret_cast<ConditionEval>(
      g_module_base + sig::kGetPcMiscStatEvalRva);
  g_radio_enable = reinterpret_cast<RadioEnable>(
      g_module_base + sig::kRadioEnableRva);
  g_radio_tune = reinterpret_cast<RadioTune>(
      g_module_base + sig::kRadioTuneRva);
  g_radio_candidate_builder =
      g_module_base + sig::kRadioCandidateBuilderRva;
  g_list_node_destructor =
      g_module_base + sig::kListNodeDestructorRva;
  g_heap_free = g_module_base + sig::kHeapFreeRva;
  g_heap_manager = g_module_base + sig::kHeapManagerRva;
  g_selected_radio_global =
      g_module_base + sig::kSelectedRadioGlobalRva;
  g_radio_enabled_global =
      g_module_base + sig::kRadioEnabledGlobalRva;
  g_radio_transition_global =
      g_module_base + sig::kRadioTransitionGlobalRva;
  g_active_effect_list_getter = active_effect_list_getter;
  g_world_map_marker_list_builder = world_map_marker_list_builder;
  g_map_marker_data_getter = map_marker_data_getter;
  g_simple_list_destructor = simple_list_destructor;
  g_world_map_data_resolver = world_map_data_resolver;
  g_world_map_texture_resolver = world_map_texture_resolver;
  g_note_image_payload_getter = note_image_payload_getter;
  g_note_text_payload_getter = note_text_payload_getter;
  g_active_quest_setter = active_quest_setter;
  g_quest_log_text_resolver = quest_log_text_resolver;
  const bool globals = WritableAddress(g_player_global, sizeof(void*)) &&
                       WritableAddress(g_data_handler_global, sizeof(void*)) &&
                       build.rdata.Contains(g_player_vtable, sizeof(void*)) &&
                       IsReadable(reinterpret_cast<void*>(g_player_vtable),
                                  sizeof(void*));
  Log(LogLevel::kInfo,
      "[BRIDGE] anchors command_table=%u get_actor_value=%u get_stage=%u equip=%u unequip=%u has_perk=%u xp_next=%u misc_stats=%u radio=%u effects=%u markers=%u world_map=%u notes=%u active_quest=%u quest_lore=%u equip_oracle=%u unequip_oracle=%u actor_pattern=%u container_getter=%u inventory_name=%u item_rtti=%u data_rtti=%u data_handler=%u globals=%u base=%p delta=%08lX",
      table_in_image ? 1u : 0u, actor_value ? 1u : 0u,
      get_stage_eval ? 1u : 0u, equip ? 1u : 0u, unequip ? 1u : 0u,
      has_perk_eval ? 1u : 0u,
      get_xp_next_eval ? 1u : 0u, get_pc_misc_stat_eval ? 1u : 0u,
      radio_path ? 1u : 0u,
      effect_path ? 1u : 0u,
      marker_path ? 1u : 0u,
      world_map_path ? 1u : 0u,
      note_path ? 1u : 0u,
      active_quest_path ? 1u : 0u,
      quest_log_path ? 1u : 0u,
      player_pattern ? 1u : 0u,
      unequip_pattern ? 1u : 0u, actor_pattern ? 1u : 0u,
      container_getter ? 1u : 0u, inventory_name ? 1u : 0u,
      item_rtti ? 1u : 0u, data_rtti ? 1u : 0u,
      data_handler ? 1u : 0u, globals ? 1u : 0u,
      reinterpret_cast<void*>(g_module_base),
      static_cast<unsigned long>(g_module_base - 0x00400000u));
  g_ready = table_in_image && actor_value && get_stage_eval && equip && unequip &&
             player_pattern && unequip_pattern && actor_pattern &&
             container_getter && inventory_name && item_rtti && data_rtti &&
             data_handler && has_perk_eval && get_xp_next_eval && radio_path &&
             get_pc_misc_stat_eval &&
               effect_path && marker_path && world_map_path && note_path &&
               active_quest_path &&
              quest_log_path && globals;
  if (!g_ready) {
    Log(LogLevel::kError,
        "[BRIDGE] mandatory 1.7.0.4 structural validation failed; reads/actions disabled");
    return false;
  }
  // Local-map rendering is optional so a renderer-specific validation failure
  // cannot disable stats, inventory, actions, notes, or the world map.  Its
  // bridge has its own exact-byte and RTTI gates before any native call runs.
  g_local_map_ready = InitializeLocalMapCapture(build, g_module_base);
  Log(LogLevel::kInfo,
      "[BRIDGE] exact-build stats, inventory, quest/perk/radio/effect data, and native action anchors validated local_map=%u",
      g_local_map_ready ? 1u : 0u);
  return true;
}

bool AttachGameThreadTimer(HWND game_window) {
  if (!g_ready || !game_window || !IsWindow(game_window)) return false;
  if (g_timer && g_game_window == game_window && IsWindow(g_game_window))
    return true;
  if (g_timer && g_game_window && IsWindow(g_game_window))
    KillTimer(g_game_window, kGameTimerId);
  g_timer = 0;
  g_game_window = nullptr;
  g_previous_game_tick = 0;
  DWORD process_id = 0;
  g_game_thread_id = GetWindowThreadProcessId(game_window, &process_id);
  if (!g_game_thread_id || process_id != GetCurrentProcessId()) return false;
  g_game_window = game_window;
  g_timer = SetTimer(game_window, kGameTimerId, kGameTimerPeriodMs, GameTimerProc);
  if (!g_timer) {
    Log(LogLevel::kError, "[BRIDGE] game-thread timer installation failed error=%lu",
        GetLastError());
    g_game_window = nullptr;
    g_game_thread_id = 0;
    g_previous_game_tick = 0;
    return false;
  }
  Log(LogLevel::kInfo,
      "[BRIDGE] bounded state sampler attached window=%p owner_tid=%lu period_ms=%u",
      game_window, g_game_thread_id, kGameTimerPeriodMs);
  return true;
}

void OnGameThreadTick() {
  ScopedPerformance tick(PerfDomain::kGameTick);
  if (!g_ready || GetCurrentThreadId() != g_game_thread_id) {
    LogRateLimited("wrong-game-thread", 5000, LogLevel::kError,
                   "[BRIDGE] state tick rejected caller_tid=%lu expected_tid=%lu",
                   GetCurrentThreadId(), g_game_thread_id);
    return;
  }
  if (!IsReadable(reinterpret_cast<void*>(g_player_global), sizeof(void*))) return;
  void* player = *reinterpret_cast<void**>(g_player_global);
  if (!player) {
    if (g_last_player) {
      g_last_player = nullptr;
      ++g_world_epoch;
      ResetLocalMapCapture(g_world_epoch);
      RejectStaleCommands(g_world_epoch);
      g_have_state = false;
      g_have_vitals = false;
      g_have_inventory = false;
      g_have_data = false;
      g_have_notes = false;
      g_have_general_stats = false;
      g_have_map = false;
      g_exterior_map_anchor = {};
      g_have_radio = false;
      g_radio_scan_disabled = false;
      g_have_effects = false;
      g_have_markers = false;
      g_marker_scan_disabled = false;
      g_next_inventory_scan_ms = 0;
      g_next_data_scan_ms = 0;
      g_next_note_scan_ms = 0;
      g_next_map_scan_ms = 0;
      g_next_radio_scan_ms = 0;
      g_next_effect_scan_ms = 0;
      g_next_marker_scan_ms = 0;
      g_next_condition_scan_ms = 0;
      g_next_special_scan_ms = 0;
      g_next_skills_scan_ms = 0;
      g_next_general_stats_scan_ms = 0;
      Log(LogLevel::kInfo,
          "[LIFECYCLE] player released; generation advanced epoch=%llu",
          static_cast<unsigned long long>(g_world_epoch));
    }
    PublishUnavailable(Lifecycle::kMainMenu,
                       "Fallout 3 1.7.0.4 verified; waiting for player");
    return;
  }
  if (!ValidPlayer(player)) {
    if (g_last_player) {
      g_last_player = nullptr;
      ++g_world_epoch;
      ResetLocalMapCapture(g_world_epoch);
      RejectStaleCommands(g_world_epoch);
      g_have_state = false;
      g_have_vitals = false;
      g_have_inventory = false;
      g_have_data = false;
      g_have_notes = false;
      g_have_general_stats = false;
      g_have_map = false;
      g_exterior_map_anchor = {};
      g_have_radio = false;
      g_radio_scan_disabled = false;
      g_have_effects = false;
      g_have_markers = false;
      g_marker_scan_disabled = false;
      g_next_inventory_scan_ms = 0;
      g_next_data_scan_ms = 0;
      g_next_note_scan_ms = 0;
      g_next_map_scan_ms = 0;
      g_next_radio_scan_ms = 0;
      g_next_effect_scan_ms = 0;
      g_next_marker_scan_ms = 0;
      g_next_condition_scan_ms = 0;
      g_next_special_scan_ms = 0;
      g_next_skills_scan_ms = 0;
      g_next_general_stats_scan_ms = 0;
      Log(LogLevel::kInfo,
          "[LIFECYCLE] player became invalid; generation advanced epoch=%llu",
          static_cast<unsigned long long>(g_world_epoch));
    }
    LogRateLimited("player-invalid", 2000, LogLevel::kWarning,
                   "[PLAYER] singleton is non-null but invariants are not ready pointer=%p",
                   player);
    PublishUnavailable(Lifecycle::kLoading,
                       "Fallout 3 1.7.0.4 verified; player is loading");
    return;
  }
  if (player != g_last_player) {
    g_last_player = player;
    ++g_world_epoch;
    ResetLocalMapCapture(g_world_epoch);
    RejectStaleCommands(g_world_epoch);
    g_have_state = false;
    g_have_vitals = false;
    g_have_inventory = false;
    g_have_data = false;
    g_have_notes = false;
    g_have_general_stats = false;
    g_have_map = false;
    g_exterior_map_anchor = {};
    g_have_radio = false;
    g_radio_scan_disabled = false;
    g_have_effects = false;
    g_have_markers = false;
    g_marker_scan_disabled = false;
    g_next_inventory_scan_ms = 0;
    g_next_data_scan_ms = 0;
    g_next_note_scan_ms = 0;
    g_next_map_scan_ms = 0;
    g_next_radio_scan_ms = 0;
    g_next_effect_scan_ms = 0;
    g_next_marker_scan_ms = 0;
    g_next_condition_scan_ms = 0;
    g_next_special_scan_ms = 0;
    g_next_skills_scan_ms = 0;
    g_next_general_stats_scan_ms = 0;
    Log(LogLevel::kInfo,
        "[LIFECYCLE] validated player acquired pointer=%p epoch=%llu",
        player, static_cast<unsigned long long>(g_world_epoch));
  }

  const bool action_changed = ProcessPendingCommands(player);
  const uint64_t now_ms = GetTickCount64();
  const bool condition_requested = IsConditionStatsRequested();
  const bool special_requested = IsSpecialStatsRequested();
  const bool skills_requested = IsSkillsStatsRequested();
  const bool general_stats_requested = IsGeneralStatsRequested();
  const bool game_data_requested = IsGameDataRequested();
  const bool notes_requested = IsNotesPageRequested();
  if (condition_requested && !g_condition_requested_last)
    g_next_condition_scan_ms = 0;
  if (special_requested && !g_special_requested_last)
    g_next_special_scan_ms = 0;
  if (skills_requested && !g_skills_requested_last)
    g_next_skills_scan_ms = 0;
  if (general_stats_requested && !g_general_stats_requested_last)
    g_next_general_stats_scan_ms = 0;
  if (game_data_requested && !g_game_data_requested_last)
    g_next_data_scan_ms = 0;
  if (notes_requested && !g_notes_requested_last)
    g_next_note_scan_ms = 0;
  g_condition_requested_last = condition_requested;
  g_special_requested_last = special_requested;
  g_skills_requested_last = skills_requested;
  g_general_stats_requested_last = general_stats_requested;
  g_game_data_requested_last = game_data_requested;
  g_notes_requested_last = notes_requested;

  if (g_have_vitals) {
    g_vitals = g_last_vitals;
  } else {
    g_vitals = {};
    std::fill_n(g_vitals.special, kSpecialCount, -1.0f);
    std::fill_n(g_vitals.skills, kSkillCount, -1.0f);
    std::fill_n(g_vitals.limbs, kLimbCount, -1.0f);
    std::fill_n(g_vitals.misc_stats, kMiscStatCount, -1);
  }
  g_vitals.lifecycle = Lifecycle::kLive;
  g_vitals.player_form_id = 0x14;
  const uintptr_t address = reinterpret_cast<uintptr_t>(player);
  void* cell = nullptr;
  if (IsReadable(reinterpret_cast<void*>(address + 0x3C), sizeof(void*))) {
    std::memcpy(&cell, reinterpret_cast<void*>(address + 0x3C), sizeof(cell));
    g_vitals.cell_form_id = ReadFormId(cell);
  }

  if (!g_vitals.player_name[0] &&
      !ReadPlayerName(player, g_vitals.player_name,
                      sizeof(g_vitals.player_name)))
    std::snprintf(g_vitals.player_name, sizeof(g_vitals.player_name),
                  "Lone Wanderer");
  if (!g_have_vitals ||
      g_vitals.cell_form_id != g_last_vitals.cell_form_id) {
    g_vitals.location[0] = '\0';
    ReadCellName(cell, g_vitals.location, sizeof(g_vitals.location));
  }
  int32_t level = -1;
  if (ReadPlayerLevel(player, &level)) g_vitals.level = level;

  const auto update_actor_value = [&](const char* label, uint32_t actor_value,
                                      bool current, float minimum,
                                      float maximum, float* destination) {
    float value = -1.0f;
    if (ReadActorValue(player, actor_value, current, &value) &&
        value >= minimum && value <= maximum) {
      *destination = value;
      return;
    }
    LogRateLimited(label, 5000, LogLevel::kWarning,
                   "[PLAYER] actor value unavailable label=%s av=%u mode=%s; retaining last valid value",
                   label, actor_value, current ? "current" : "base");
  };
  const uint64_t vitals_scan_started = PerfNow();
  update_actor_value("av-health", 16, true, 0.0f, 1000000.0f,
                     &g_vitals.health);
  update_actor_value("av-max-health", 16, false, 0.001f, 1000000.0f,
                     &g_vitals.max_health);
  update_actor_value("av-ap", 12, true, 0.0f, 1000000.0f,
                     &g_vitals.action_points);
  update_actor_value("av-max-ap", 12, false, 0.001f, 1000000.0f,
                     &g_vitals.max_action_points);
  update_actor_value("av-dr", 18, true, 0.0f, 1000.0f,
                     &g_vitals.damage_resistance);
  update_actor_value("av-rad-resist", 20, true, 0.0f, 1000.0f,
                     &g_vitals.radiation_resistance);
  update_actor_value("av-radiation", 54, true, 0.0f, 5000.0f,
                     &g_vitals.radiation);
  update_actor_value("av-inventory-weight", 46, true, 0.0f, 1000000.0f,
                     &g_vitals.carry_weight);
  // Vanilla Pip-Boy obtains the capacity from ActorValueOwner::Fn_01.
  update_actor_value("av-carry-capacity", 13, false, 0.0f, 1000000.0f,
                     &g_vitals.max_carry_weight);
  update_actor_value("av-karma", 23, true, -1000000.0f, 1000000.0f,
                     &g_vitals.karma);
  float xp = -1.0f;
  if (ReadActorValue(player, 24, true, &xp) && xp >= 0.0f &&
      xp <= static_cast<float>(INT32_MAX))
    g_vitals.xp = static_cast<int32_t>(xp);
  else
    LogRateLimited("av-xp", 5000, LogLevel::kWarning,
                   "[PLAYER] XP unavailable; retaining last valid value");
  if (g_get_xp_next_eval && g_vitals.xp >= 0) {
    double remaining = -1.0;
    if (g_get_xp_next_eval(player, nullptr, nullptr, &remaining) &&
        std::isfinite(remaining) && remaining >= 0.0 &&
        remaining <= static_cast<double>(INT32_MAX - g_vitals.xp)) {
      g_vitals.xp_next =
          g_vitals.xp + static_cast<int32_t>(std::lround(remaining));
    } else {
      LogRateLimited("xp-next", 5000, LogLevel::kWarning,
                     "[PLAYER] next-level XP unavailable; retaining last valid value");
    }
  }
  RecordPerformance(PerfDomain::kVitalsScan,
                    PerfElapsedMicros(vitals_scan_started));
  if (special_requested && now_ms >= g_next_special_scan_ms) {
    ScopedPerformance special_scan(PerfDomain::kSpecialScan);
    g_next_special_scan_ms = now_ms + kAttributeScanPeriodMs;
    for (uint32_t i = 0; i < kSpecialCount; ++i)
      update_actor_value("av-special", 5 + i, true, -50.0f, 100.0f,
                         &g_vitals.special[i]);
  }
  if (skills_requested && now_ms >= g_next_skills_scan_ms) {
    ScopedPerformance skills_scan(PerfDomain::kSkillsScan);
    g_next_skills_scan_ms = now_ms + kAttributeScanPeriodMs;
    for (uint32_t i = 0; i < kSkillCount; ++i) {
      const uint32_t actor_value = i == kSkillCount - 1 ? 45 : 32 + i;
      update_actor_value("av-skill", actor_value, true, -50.0f, 1000.0f,
                         &g_vitals.skills[i]);
    }
  }
  if (condition_requested && now_ms >= g_next_condition_scan_ms) {
    ScopedPerformance condition_scan(PerfDomain::kConditionScan);
    g_next_condition_scan_ms = now_ms + kConditionScanPeriodMs;
    for (uint32_t i = 0; i < kLimbCount; ++i)
      update_actor_value("av-limb", 25 + i, true, 0.0f, 10000.0f,
                         &g_vitals.limbs[i]);
  }
  if (general_stats_requested &&
      now_ms >= g_next_general_stats_scan_ms) {
    ScopedPerformance general_scan(PerfDomain::kGeneralStatsScan);
    g_next_general_stats_scan_ms = now_ms + kGeneralStatsScanPeriodMs;
    bool complete = g_get_pc_misc_stat_eval != nullptr;
    int32_t staged[kMiscStatCount] = {};
    for (uint32_t i = 0; i < kMiscStatCount && complete; ++i) {
      double value = -1.0;
      if (!g_get_pc_misc_stat_eval(
              nullptr, reinterpret_cast<void*>(static_cast<uintptr_t>(i)),
              nullptr, &value) ||
          !std::isfinite(value) || value < 0.0 ||
          value > static_cast<double>(INT32_MAX)) {
        complete = false;
        break;
      }
      staged[i] = static_cast<int32_t>(std::llround(value));
    }
    if (complete) {
      std::copy_n(staged, kMiscStatCount, g_vitals.misc_stats);
      if (!g_have_general_stats)
        Log(LogLevel::kInfo,
            "[GENERAL] native MiscStatManager bridge published %u counters",
            kMiscStatCount);
      g_have_general_stats = true;
    } else {
      LogRateLimited("general-stats", 5000, LogLevel::kWarning,
                     "[GENERAL] GetPCMiscStat evaluator rejected a native counter; previous snapshot retained");
    }
  }
  const bool vitals_changed =
      !g_have_vitals || !VitalsEqual(g_vitals, g_last_vitals);
  bool inventory_changed = false;
  bool data_changed = false;
  bool notes_changed = false;
  bool map_changed = false;
  bool local_map_changed = false;
  bool radio_changed = false;
  bool effects_changed = false;
  bool markers_changed = false;
  InventoryFingerprint pending_inventory = {};
  DataFingerprint pending_data = {};
  NoteFingerprint pending_notes = {};
  MapProbe pending_map = g_last_map;
  RadioFingerprint pending_radio = {};
  EffectFingerprint pending_effects = {};
  MarkerFingerprint pending_markers = {};
  if (now_ms >= g_next_inventory_scan_ms) {
    g_next_inventory_scan_ms = now_ms + kInventoryScanPeriodMs;
    ScopedPerformance inventory_scan(PerfDomain::kInventoryScan);
    InventoryFingerprint observed = {};
    if (!CaptureInventory(player, false, &observed)) {
      LogRateLimited("inventory-canary", 5000, LogLevel::kError,
                     "[INVENTORY] root/list/form canary failed; previous snapshot retained");
    } else if (!g_have_inventory ||
               !FingerprintsEqual(observed, g_last_inventory_fingerprint)) {
      InventoryFingerprint populated = {};
      if (CaptureInventory(player, true, &populated) &&
          FingerprintsEqual(observed, populated)) {
        pending_inventory = populated;
        pending_inventory.visible_count = g_inventory_stage_count;
        inventory_changed = true;
      } else {
        LogRateLimited("inventory-unstable", 5000, LogLevel::kWarning,
                       "[INVENTORY] list changed during snapshot; update discarded");
      }
    }
  }
  if (game_data_requested && now_ms >= g_next_data_scan_ms) {
    g_next_data_scan_ms = now_ms + kDataScanPeriodMs;
    ScopedPerformance data_scan(PerfDomain::kDataScan);
    DataFingerprint observed = {};
    if (!CaptureGameData(player, false, &observed)) {
      LogRateLimited("data-canary", 5000, LogLevel::kError,
                     "[DATA] quest/perk list canary failed; previous snapshot retained");
    } else if (!g_have_data ||
               !DataFingerprintsEqual(observed, g_last_data_fingerprint)) {
      DataFingerprint populated = {};
      if (CaptureGameData(player, true, &populated) &&
          DataFingerprintsEqual(observed, populated)) {
        pending_data = populated;
        data_changed = true;
      } else {
        LogRateLimited("data-unstable", 5000, LogLevel::kWarning,
                       "[DATA] quest/perk lists changed during snapshot; update discarded");
      }
    }
  }
  if (notes_requested && now_ms >= g_next_note_scan_ms) {
    g_next_note_scan_ms = now_ms + kNoteScanPeriodMs;
    ScopedPerformance note_scan(PerfDomain::kNoteScan);
    NoteFingerprint observed = {};
    if (!CaptureNotes(player, false, &observed)) {
      LogRateLimited("note-canary", 5000, LogLevel::kError,
                     "[NOTES] acquired-note list canary failed; previous snapshot retained");
    } else if (!g_have_notes ||
               !NoteFingerprintsEqual(observed, g_last_note_fingerprint)) {
      NoteFingerprint populated = {};
      if (CaptureNotes(player, true, &populated) &&
          NoteFingerprintsEqual(observed, populated)) {
        pending_notes = populated;
        notes_changed = true;
      } else {
        LogRateLimited("note-unstable", 5000, LogLevel::kWarning,
                       "[NOTES] acquired-note list changed during snapshot; update discarded");
      }
    }
  }
  if (IsMapPageRequested() &&
      now_ms >= g_next_map_scan_ms) {
    g_next_map_scan_ms = now_ms + kMapScanPeriodMs;
    ScopedPerformance map_scan(PerfDomain::kMapScan);
    MapProbe observed = {};
    if (CaptureMap(player, &observed) &&
        (!g_have_map || !MapEqual(observed, g_last_map))) {
      pending_map = observed;
      map_changed = true;
    }
  }
  if (g_local_map_ready) {
    const LocalMapPumpResult result =
        PumpLocalMapCapture(player, g_world_epoch,
                            IsLocalMapPageRequested());
    local_map_changed = result == LocalMapPumpResult::kPublished;
  }
  if (IsMapPageRequested() &&
      now_ms >= g_next_marker_scan_ms) {
    g_next_marker_scan_ms = now_ms + kMarkerScanPeriodMs;
    ScopedPerformance marker_scan(PerfDomain::kMarkerScan);
    MarkerFingerprint observed = {};
    if (!CaptureMapMarkers(player, false, &observed)) {
      LogRateLimited("marker-canary", 5000, LogLevel::kError,
                     "[MAP] native marker-list canary failed; previous markers retained");
    } else if (!g_have_markers ||
               !MarkerFingerprintsEqual(observed,
                                        g_last_marker_fingerprint)) {
      MarkerFingerprint populated = {};
      if (CaptureMapMarkers(player, true, &populated) &&
          MarkerFingerprintsEqual(observed, populated)) {
        pending_markers = populated;
        markers_changed = true;
      } else {
        LogRateLimited("marker-unstable", 5000, LogLevel::kWarning,
                       "[MAP] native marker list changed during snapshot; update discarded");
      }
    }
  }
  const bool radio_foreground = IsRadioPageRequested();
  if (now_ms >= g_next_radio_scan_ms) {
    g_next_radio_scan_ms = now_ms +
        (radio_foreground ? kRadioScanPeriodMs
                          : kRadioBackgroundScanPeriodMs);
    ScopedPerformance radio_scan(PerfDomain::kRadioScan);
    RadioFingerprint observed = {};
    if (!CaptureRadio(player, false, &observed)) {
      LogRateLimited("radio-canary", 5000, LogLevel::kError,
                     "[RADIO] candidate list canary failed; previous snapshot retained");
    } else if (!g_have_radio ||
               !RadioFingerprintsEqual(observed, g_last_radio_fingerprint)) {
      RadioFingerprint populated = {};
      if (CaptureRadio(player, true, &populated) &&
          RadioFingerprintsEqual(observed, populated)) {
        pending_radio = populated;
        radio_changed = true;
      } else {
        LogRateLimited("radio-unstable", 5000, LogLevel::kWarning,
                       "[RADIO] station list changed during snapshot; update discarded");
      }
    }
  }
  if (IsEffectsPageRequested() &&
      now_ms >= g_next_effect_scan_ms) {
    g_next_effect_scan_ms = now_ms + kEffectScanPeriodMs;
    ScopedPerformance effect_scan(PerfDomain::kEffectScan);
    EffectFingerprint observed = {};
    if (!CaptureActiveEffects(player, false, &observed)) {
      LogRateLimited("effect-canary", 5000, LogLevel::kError,
                     "[EFFECTS] active-effect list canary failed; previous snapshot retained");
    } else if (!g_have_effects ||
               !EffectFingerprintsEqual(observed,
                                        g_last_effect_fingerprint)) {
      EffectFingerprint populated = {};
      if (CaptureActiveEffects(player, true, &populated) &&
          EffectFingerprintsEqual(observed, populated)) {
        pending_effects = populated;
        effects_changed = true;
      } else {
        LogRateLimited("effect-unstable", 5000, LogLevel::kWarning,
                       "[EFFECTS] active-effect list changed during snapshot; update discarded");
      }
    }
  }
  if (!vitals_changed && !inventory_changed && !data_changed &&
      !notes_changed && !map_changed && !local_map_changed &&
      !radio_changed && !effects_changed && !markers_changed &&
      !action_changed)
    return;

  const uint64_t next_vitals_revision = g_have_state
      ? g_next.vitals_revision +
          ((vitals_changed || effects_changed) ? 1 : 0) : 1;
  const uint64_t next_inventory_revision = inventory_changed
      ? (g_have_state ? g_next.inventory_revision + 1 : 1)
      : g_next.inventory_revision;
  const uint64_t next_data_revision = data_changed || notes_changed
      ? (g_have_state ? g_next.data_revision + 1 : 1)
      : g_next.data_revision;
  const uint64_t next_map_revision =
      map_changed || markers_changed || local_map_changed
      ? (g_have_state ? g_next.map_revision + 1 : 1)
      : g_next.map_revision;
  const uint64_t next_radio_revision = radio_changed
      ? (g_have_state ? g_next.radio_revision + 1 : 1)
      : g_next.radio_revision;
  const uint64_t next_action_revision = action_changed
      ? (g_have_state ? g_next.action_revision + 1 : 1)
      : g_next.action_revision;
  if (!g_have_state || g_next.world_epoch != g_world_epoch)
    SecureZeroMemory(&g_next, sizeof(g_next));
  g_next.version = kSnapshotVersion;
  g_next.size = sizeof(Snapshot);
  g_next.sequence = ++g_sequence;
  g_next.world_epoch = g_world_epoch;
  g_next.vitals_revision = next_vitals_revision;
  g_next.data_revision = next_data_revision;
  g_next.map_revision = next_map_revision;
  g_next.radio_revision = next_radio_revision;
  g_next.action_revision = next_action_revision;
  if (action_changed) {
    g_next.last_action_request_id = g_last_action_command.request_id;
    g_next.last_action_type =
        static_cast<uint32_t>(g_last_action_command.type);
    g_next.last_action_form_id = g_last_action_command.form_id;
    g_next.last_action_result = g_last_action_result;
  }
  g_next.lifecycle = g_vitals.lifecycle;
  g_next.supported_build = 1;
  g_next.capabilities |= kCapStats | kCapActions;
  g_next.player_form_id = g_vitals.player_form_id;
  g_next.cell_form_id = g_vitals.cell_form_id;
  g_next.level = g_vitals.level;
  g_next.xp = g_vitals.xp;
  g_next.xp_next = g_vitals.xp_next;
  g_next.health = g_vitals.health;
  g_next.max_health = g_vitals.max_health;
  g_next.action_points = g_vitals.action_points;
  g_next.max_action_points = g_vitals.max_action_points;
  g_next.damage_resistance = g_vitals.damage_resistance;
  g_next.radiation_resistance = g_vitals.radiation_resistance;
  g_next.radiation = g_vitals.radiation;
  g_next.carry_weight = g_vitals.carry_weight;
  g_next.max_carry_weight = g_vitals.max_carry_weight;
  g_next.karma = g_vitals.karma;
  std::snprintf(g_next.player_name, sizeof(g_next.player_name), "%s",
                g_vitals.player_name);
  std::snprintf(g_next.location, sizeof(g_next.location), "%s",
                g_vitals.location);
  std::copy_n(g_vitals.special, kSpecialCount, g_next.special);
  std::copy_n(g_vitals.skills, kSkillCount, g_next.skills);
  std::copy_n(g_vitals.limbs, kLimbCount, g_next.limbs);
  if (g_have_general_stats) {
    std::copy_n(g_vitals.misc_stats, kMiscStatCount, g_next.misc_stats);
    g_next.capabilities |= kCapGeneralStats;
  }
  if (inventory_changed) {
    g_next.item_count = g_inventory_stage_count;
    g_next.caps = g_inventory_stage_caps;
    if (g_inventory_stage_count)
      std::copy_n(g_inventory_stage, g_inventory_stage_count, g_next.items);
    g_next.inventory_revision = next_inventory_revision;
    g_next.capabilities |= kCapInventory;
  }
  if (notes_changed) {
    g_next.note_count = g_note_stage_count;
    if (g_note_stage_count)
      std::copy_n(g_note_stage, g_note_stage_count, g_next.notes);
    g_next.capabilities |= kCapNotes;
  }
  if (data_changed) {
    g_next.quest_count = g_quest_stage_count;
    g_next.objective_count = g_objective_stage_count;
    g_next.perk_count = g_perk_stage_count;
    if (g_quest_stage_count)
      std::copy_n(g_quest_stage, g_quest_stage_count, g_next.quests);
    if (g_objective_stage_count)
      std::copy_n(g_objective_stage, g_objective_stage_count,
                  g_next.objectives);
    if (g_perk_stage_count)
      std::copy_n(g_perk_stage, g_perk_stage_count, g_next.perks);
    g_next.capabilities |= kCapQuests | kCapPerks;
  }
  if (map_changed) {
    g_next.capabilities &= ~(kCapMap | kCapWorldMap | kCapLocalMap);
    g_next.world_form_id = pending_map.world_form_id;
    g_next.player_x = pending_map.x;
    g_next.player_y = pending_map.y;
    g_next.player_z = pending_map.z;
    g_next.player_heading = pending_map.heading;
    g_next.map_min_x = pending_map.min_x;
    g_next.map_max_y = pending_map.max_y;
    g_next.map_max_x = pending_map.max_x;
    g_next.map_min_y = pending_map.min_y;
    g_next.map_usable_width = pending_map.usable_width;
    g_next.map_usable_height = pending_map.usable_height;
    std::snprintf(g_next.map_texture_path,
                  sizeof(g_next.map_texture_path), "%s",
                  pending_map.texture_path);
    if (pending_map.valid) g_next.capabilities |= kCapMap;
    if (pending_map.world_valid) g_next.capabilities |= kCapWorldMap;
  }
  if (markers_changed) {
    g_next.marker_count = g_marker_stage_count;
    if (g_marker_stage_count)
      std::copy_n(g_marker_stage, g_marker_stage_count, g_next.markers);
    g_next.capabilities |= kCapMap;
  }
  if (g_local_map_ready &&
      IsLocalMapFrameReady(g_world_epoch, g_vitals.cell_form_id)) {
    g_next.capabilities |= kCapMap | kCapLocalMap;
  } else {
    g_next.capabilities &= ~kCapLocalMap;
  }
  if (radio_changed) {
    if (g_have_radio) {
      for (uint32_t station = 0; station < g_radio_stage_count; ++station) {
        bool previously_available = false;
        for (uint32_t previous = 0;
             previous < g_next.station_count && previous < kMaxStations;
             ++previous) {
          if (g_next.stations[previous].form_id ==
              g_radio_stage[station].form_id) {
            previously_available = true;
            break;
          }
        }
        if (!previously_available)
          Log(LogLevel::kInfo,
              "[RADIO] signal received form=%08X name=%s",
              g_radio_stage[station].form_id,
              g_radio_stage[station].name);
      }
    }
    g_next.station_count = g_radio_stage_count;
    if (g_radio_stage_count)
      std::copy_n(g_radio_stage, g_radio_stage_count, g_next.stations);
    g_next.capabilities |= kCapRadio;
  }
  if (effects_changed) {
    g_next.effect_count = g_effect_stage_count;
    if (g_effect_stage_count)
      std::copy_n(g_effect_stage, g_effect_stage_count, g_next.effects);
    g_next.capabilities |= kCapEffects;
  }
  std::snprintf(g_next.build_label, sizeof(g_next.build_label),
                 "Fallout 3 1.7.0.4 exact live bridge");
  const uint64_t publish_start = PerfNow();
  const bool published = PublishSnapshot(g_next);
  RecordPerformance(PerfDomain::kSnapshotPublish,
                    PerfElapsedMicros(publish_start));
  if (published) {
    g_last_vitals = g_vitals;
    g_have_state = true;
    g_have_vitals = true;
    if (inventory_changed) {
      g_last_inventory_fingerprint = pending_inventory;
      g_have_inventory = true;
      Log(LogLevel::kInfo,
          "[INVENTORY] snapshot revision=%llu nodes=%u visible=%u",
          static_cast<unsigned long long>(g_next.inventory_revision),
          pending_inventory.node_count, g_next.item_count);
    }
    if (notes_changed) {
      g_last_note_fingerprint = pending_notes;
      g_have_notes = true;
      Log(LogLevel::kInfo,
          "[NOTES] snapshot revision=%llu acquired=%u published=%u",
          static_cast<unsigned long long>(g_next.data_revision),
          pending_notes.note_count, g_next.note_count);
    }
    if (data_changed) {
      g_last_data_fingerprint = pending_data;
      g_have_data = true;
      Log(LogLevel::kInfo,
          "[DATA] snapshot revision=%llu quests=%u objectives=%u perks=%u notes=%u",
          static_cast<unsigned long long>(g_next.data_revision),
          g_next.quest_count, g_next.objective_count, g_next.perk_count,
          g_next.note_count);
    }
    if (map_changed) {
      g_last_map = pending_map;
      g_have_map = true;
      LogRateLimited("map-publish", 5000, LogLevel::kDebug,
                     "[MAP] live position world=%08X cell=%08X interior=%u anchor=%u xyz=%.1f,%.1f,%.1f heading=%.1f bounds=%.0f,%.0f..%.0f,%.0f texture=%s",
                     g_next.world_form_id, g_next.cell_form_id,
                     pending_map.interior ? 1u : 0u,
                     static_cast<unsigned>(pending_map.anchor_source),
                     g_next.player_x, g_next.player_y, g_next.player_z,
                     g_next.player_heading, g_next.map_min_x,
                     g_next.map_min_y, g_next.map_max_x,
                     g_next.map_max_y, g_next.map_texture_path);
    }
    if (markers_changed) {
      g_last_marker_fingerprint = pending_markers;
      g_have_markers = true;
      Log(LogLevel::kInfo,
          "[MAP] native marker snapshot revision=%llu visible=%u",
          static_cast<unsigned long long>(g_next.map_revision),
          g_next.marker_count);
    }
    if (radio_changed) {
      g_last_radio_fingerprint = pending_radio;
      g_have_radio = true;
      Log(LogLevel::kInfo,
          "[RADIO] snapshot revision=%llu stations=%u enabled=%u",
          static_cast<unsigned long long>(g_next.radio_revision),
          g_next.station_count, pending_radio.enabled);
    }
    if (effects_changed) {
      g_last_effect_fingerprint = pending_effects;
      g_have_effects = true;
      Log(LogLevel::kInfo,
          "[EFFECTS] snapshot vitals_revision=%llu active=%u",
          static_cast<unsigned long long>(g_next.vitals_revision),
          g_next.effect_count);
    }
    LogRateLimited("stats-publish", 5000, LogLevel::kDebug,
                    "[PLAYER] stats published seq=%llu hp=%.1f/%.1f ap=%.1f dr=%.1f rad_resist=%.1f weight=%.1f/%.1f caps=%d rad=%.1f xp=%d/%d cell=%08X misc=%u",
                    static_cast<unsigned long long>(g_next.sequence), g_next.health,
                    g_next.max_health, g_next.action_points,
                    g_next.damage_resistance, g_next.radiation_resistance,
                    g_next.carry_weight,
                    g_next.max_carry_weight, g_next.caps, g_next.radiation,
                    g_next.xp, g_next.xp_next,
                    g_next.cell_form_id,
                    (g_next.capabilities & kCapGeneralStats) ? 1u : 0u);
  }
}

void ShutdownEngineBridge() {
  if (g_timer && g_game_window && IsWindow(g_game_window))
    KillTimer(g_game_window, kGameTimerId);
  g_timer = 0;
  g_game_window = nullptr;
  g_previous_game_tick = 0;
  ShutdownLocalMapCapture();
  g_local_map_ready = false;
  g_ready = false;
}

}  // namespace fo3ds
