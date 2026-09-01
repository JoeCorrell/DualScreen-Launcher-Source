#include "NativeMap.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "Hook.h"
#include "Log.h"
#include "Mem.h"
#include "Rtti.h"

namespace dxs {
namespace {

#if defined(__GNUC__)
#define DXS_GAME_THISCALL __attribute__((thiscall))
#define DXS_GAME_CDECL __attribute__((cdecl))
#define DXS_HOOK_FASTCALL __attribute__((fastcall))
#else
#define DXS_GAME_THISCALL __thiscall
#define DXS_GAME_CDECL __cdecl
#define DXS_HOOK_FASTCALL __fastcall
#endif

// DXHRDC.exe 2.0.66.0, PE timestamp 0x52655004.
constexpr uintptr_t kPlayerListGlobalVA = 0x01A07B1C;
constexpr uintptr_t kCurrentPlayerWrapperVA = 0x00602F60;
constexpr uintptr_t kEntityContextVA = 0x0061D1E0;
constexpr uintptr_t kMapOverrideOwnerVA = 0x007B9CE0;
constexpr uintptr_t kMapOwnerStateVA = 0x007B96A0;
constexpr uintptr_t kMapCurrentLevelVA = 0x0051AD40;
constexpr uintptr_t kMapOverrideIdVA = 0x006D0880;
constexpr uintptr_t kResolveDtpVA = 0x005A4E60;
constexpr uintptr_t kResolveObjectRefVA = 0x00607DF0;
constexpr uintptr_t kObjectRefBucketCountVA = 0x00E04EC0;
constexpr uintptr_t kMapImageResourceVA = 0x0072AFC0;
constexpr uintptr_t kLocalStringVA = 0x004CC930;
constexpr uintptr_t kMapSerializePlayerVA = 0x006B5FA0;
constexpr uintptr_t kMissionLocatorContainerVA = 0x00654730;
constexpr uintptr_t kMissionLocatorRecordsVA = 0x006D7860;
constexpr uintptr_t kMissionShortTextVA = 0x006D5690;
constexpr uintptr_t kMissionIndexVA = 0x006D5750;
constexpr uintptr_t kMissionPrimaryVA = 0x006D5770;

constexpr uintptr_t kRadarControllerVtableVA = 0x00ABDD8C;
constexpr uintptr_t kRadarUpdateVA = 0x00803940;
constexpr uint32_t kRadarUpdateSlot = 6;
constexpr uintptr_t kHandleTableGlobalVA = 0x019E1870;
// Sorted vector/map used by the vanilla radar: {uint32 key, marker*}.
constexpr uintptr_t kRadarMarkerMapVA = 0x00E120D4;

constexpr uint32_t kMaxDtpId = 0x18000u;
constexpr uint32_t kMaxStaticMarkers = 256u;
constexpr uint32_t kMaxMissionLocators = 64u;
constexpr uint32_t kMaxMissionLocatorRecords = 64u;
constexpr uint32_t kMaxRadarContacts = 128u;
constexpr uint64_t kRadarCaptureIntervalMs = 200u;
constexpr uint64_t kRadarRequestWindowMs = 1000u;
constexpr uint64_t kRadarFreshnessMs = 1000u;

template <size_t N>
bool HasCode(uintptr_t preferred_va, const uint8_t (&signature)[N]) {
  return PreferredCodeMatches(reinterpret_cast<const void*>(Rebase(preferred_va)),
                              signature, N);
}

template <size_t N>
void CopyString(const char* source, char (&destination)[N]) {
  destination[0] = '\0';
  source = SafeCStr(source, N * 4);
  if (!source) return;
  size_t length = 0;
  while (length + 1 < N && source[length]) ++length;
  std::memcpy(destination, source, length);
  destination[length] = '\0';
}

const char* Basename(const char* path) {
  if (!path) return nullptr;
  const char* result = path;
  for (const char* cursor = path; *cursor; ++cursor) {
    if (*cursor == '\\' || *cursor == '/') result = cursor + 1;
  }
  return result;
}

float VanillaNormalize(float value, float anchor, float extent) {
  if (!std::isfinite(value) || !std::isfinite(anchor) ||
      !std::isfinite(extent) || extent <= 0.0001f) {
    return 0.0f;
  }
  // SmartGlassMap::SerializePlayer/SerializeMapMarkers uses fabs after
  // subtracting bound A, because authored bounds may run in either direction.
  return std::fabs((value - anchor) / extent);
}

bool MapCodeReady() {
  static const uint8_t current_player_signature[] = {
      0x8B, 0x41, 0x08, 0x8B, 0x04, 0x81, 0xC3};
  static const uint8_t entity_context_signature[] = {
      0x56, 0x8B, 0x74, 0x24, 0x08, 0x33, 0xC0, 0x33,
      0xD2, 0xB9, 0xC0, 0xD2, 0xC5, 0x01};
  static const uint8_t map_owner_signature[] = {
      0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74,
      0x5A, 0x83, 0x7E, 0x58, 0x00};
  static const uint8_t owner_state_signature[] = {
      0x8D, 0x81, 0x24, 0x05, 0x00, 0x00, 0xC3};
  static const uint8_t current_level_signature[] = {
      0x8B, 0x41, 0x04, 0xC3};
  static const uint8_t override_id_signature[] = {
      0x8B, 0x41, 0x08, 0xC3};
  static const uint8_t resolve_dtp_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x80, 0x01,
      0x00, 0x73, 0x15, 0x8B, 0x04, 0x85, 0xE8, 0x06,
      0x98, 0x01};
  static const uint8_t resolve_object_signature[] = {
      0x8B, 0x4C, 0x24, 0x04, 0x33, 0xD2, 0x8B, 0xC1,
      0xF7, 0x35, 0xC0, 0x4E, 0xE0, 0x00};
  // SmartGlassMap::SerializePlayer. This proves the player transform layout
  // used by CapturePlayerPose is the exact supported retail implementation.
  static const uint8_t serialize_player_signature[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC,
      0x50, 0x8B, 0x41, 0x04, 0x8B, 0x50, 0x20};
  return HasCode(kCurrentPlayerWrapperVA, current_player_signature) &&
         HasCode(kEntityContextVA, entity_context_signature) &&
         HasCode(kMapOverrideOwnerVA, map_owner_signature) &&
         HasCode(kMapOwnerStateVA, owner_state_signature) &&
         HasCode(kMapCurrentLevelVA, current_level_signature) &&
         HasCode(kMapOverrideIdVA, override_id_signature) &&
         HasCode(kResolveDtpVA, resolve_dtp_signature) &&
         HasCode(kResolveObjectRefVA, resolve_object_signature) &&
         HasCode(kMapSerializePlayerVA, serialize_player_signature);
}

bool LocalStringCodeReady() {
  // DXHRDC.exe 2.0.66.0 LocalString(int). The old map gate used bytes from a
  // different localization helper here, so it rejected the supported retail
  // executable before any map data could be captured.
  static const uint8_t signature[] = {
      0x8B, 0x15, 0xF8, 0x43, 0xB2, 0x00, 0x85, 0xD2,
      0x74, 0x1B, 0x8B, 0x0D, 0xF4, 0x43, 0xB2, 0x00};
  return HasCode(kLocalStringVA, signature);
}

bool MapImageCodeReady() {
  static const uint8_t signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x8B, 0x08, 0x56, 0x85,
      0xC9, 0x75, 0x20};
  return HasCode(kMapImageResourceVA, signature);
}

bool MissionLocatorCodeReady() {
  static const uint8_t container_signature[] = {
      0x8D, 0x41, 0x0C, 0xC3};
  static const uint8_t records_signature[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0x83, 0xEC,
      0x54, 0x8B, 0x01, 0x53, 0x33, 0xD2, 0x56, 0x57};
  static const uint8_t short_text_signature[] = {
      0x8B, 0x01, 0x85, 0xC0, 0x74, 0x11, 0x0F, 0xBF, 0x00};
  static const uint8_t index_signature[] = {
      0x8B, 0x41, 0x10, 0x83, 0xF8, 0x03, 0x74, 0x0E};
  static const uint8_t primary_signature[] = {
      0x8B, 0x01, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x40, 0x04};
  return HasCode(kMissionLocatorContainerVA, container_signature) &&
      HasCode(kMissionLocatorRecordsVA, records_signature) &&
      HasCode(kMissionShortTextVA, short_text_signature) &&
      HasCode(kMissionIndexVA, index_signature) &&
      HasCode(kMissionPrimaryVA, primary_signature);
}

void* CurrentPlayer(void** wrapper_output = nullptr) {
  if (wrapper_output) *wrapper_output = nullptr;
  void* player_list = nullptr;
  if (!Read(reinterpret_cast<void*>(Rebase(kPlayerListGlobalVA)),
            &player_list) ||
      !player_list || !Readable(player_list, 12)) {
    return nullptr;
  }
  uint32_t index = 0;
  void* expected = nullptr;
  if (!Read(static_cast<uint8_t*>(player_list) + 8, &index) || index > 1 ||
      !Read(static_cast<uint8_t*>(player_list) + index * sizeof(void*),
            &expected) ||
      !expected || !Readable(expected, 8)) {
    return nullptr;
  }
  using Fn = void*(DXS_GAME_THISCALL*)(void*);
  void* wrapper = reinterpret_cast<Fn>(Rebase(kCurrentPlayerWrapperVA))(
      player_list);
  void* player = nullptr;
  if (wrapper != expected || !Readable(wrapper, 8) ||
      !Read(static_cast<uint8_t*>(wrapper) + 4, &player) ||
      !player || !Readable(player, 0x114)) {
    return nullptr;
  }
  if (wrapper_output) *wrapper_output = wrapper;
  return player;
}

bool CapturePlayerPose(void* player, float bound_a_x, float bound_a_y,
                       float bound_b_x, float bound_b_y, float width,
                       float height,
                       NativeMapSnapshot* out) {
  if (!player || !out || !Readable(player, 0xB4) ||
      !std::isfinite(bound_a_x) || !std::isfinite(bound_a_y) ||
      !std::isfinite(bound_b_x) || !std::isfinite(bound_b_y) ||
      !std::isfinite(width) ||
      !std::isfinite(height) || width <= 0.0001f || height <= 0.0001f) {
    return false;
  }

  float position[3] = {};
  if (!Read(static_cast<uint8_t*>(player) + 0x20, &position[0]) ||
      !Read(static_cast<uint8_t*>(player) + 0x24, &position[1]) ||
      !Read(static_cast<uint8_t*>(player) + 0x28, &position[2]) ||
      !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
      !std::isfinite(position[2])) {
    return false;
  }

  out->player_world_x = position[0];
  out->player_world_y = position[1];
  out->player_world_z = position[2];
  out->player_x = VanillaNormalize(position[0], bound_a_x, width);
  out->player_y = VanillaNormalize(position[1], bound_a_y, height);
  out->player_valid = out->player_x <= 1.25f && out->player_y <= 1.25f;

  // Retail SmartGlassMap::SerializePlayer reads this live transform, negates
  // its forward vector, subtracts the authored map-axis angle, then converts
  // the result to clockwise degrees under the "orientation" JSON key. Using
  // that transform means standing rotations remain exact; movement is never
  // used as a fake heading source.
  void* transform = nullptr;
  float transform_x = 0.0f;
  float transform_y = 0.0f;
  float transform_z = 0.0f;
  if (Read(static_cast<uint8_t*>(player) + 0xB0, &transform) && transform &&
      Readable(transform, 0x1C) &&
      Read(static_cast<uint8_t*>(transform) + 0x10, &transform_x) &&
      Read(static_cast<uint8_t*>(transform) + 0x14, &transform_y) &&
      Read(static_cast<uint8_t*>(transform) + 0x18, &transform_z) &&
      std::isfinite(transform_x) && std::isfinite(transform_y) &&
      std::isfinite(transform_z)) {
    const float forward_x = -transform_x;
    const float forward_y = -transform_y;
    const float map_axis_x = bound_b_x - bound_a_x;
    const float map_axis_y = bound_b_y - bound_a_y;
    const float length_squared =
        forward_x * forward_x + forward_y * forward_y;
    const float map_axis_length_squared =
        map_axis_x * map_axis_x + map_axis_y * map_axis_y;
    if (length_squared > 0.000001f &&
        map_axis_length_squared > 0.000001f) {
      constexpr float kRadiansToClockwiseDegrees = -57.29577951308232f;
      float heading = (std::atan2(forward_y, forward_x) -
                       std::atan2(map_axis_y, map_axis_x)) *
                      kRadiansToClockwiseDegrees;
      heading = std::fmod(heading, 360.0f);
      if (heading < 0.0f) heading += 360.0f;
      if (std::isfinite(heading)) {
        out->player_heading_degrees = heading;
        out->player_heading_valid = true;
      }
    }
  }
  return out->player_valid;
}

struct RadarContact {
  uint32_t key = 0;
  uint16_t type = 0;
  uint16_t state = 0;
  int level = -1;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

struct RadarSnapshot {
  bool live = false;
  uint64_t captured_ms = 0;
  uint32_t count = 0;
  RadarContact contacts[kMaxRadarContacts] = {};
};

SRWLOCK g_radar_lock = SRWLOCK_INIT;
RadarSnapshot g_radar;

using RadarUpdateFn = void(DXS_HOOK_FASTCALL*)(void*, void*, float);
std::atomic<RadarUpdateFn> g_original_radar_update{nullptr};
std::atomic<bool> g_radar_capture_enabled{false};
std::atomic<uint32_t> g_radar_callbacks{0};
std::atomic<uint64_t> g_last_radar_capture_ms{0};
std::atomic<uint64_t> g_radar_requested_until_ms{0};

void* FindRadarMarker(uint32_t key) {
  uint32_t count = 0;
  void* pairs = nullptr;
  const uintptr_t map = Rebase(kRadarMarkerMapVA);
  if (!Read(reinterpret_cast<void*>(map), &count) || count > 512 ||
      !Read(reinterpret_cast<void*>(map + 8), &pairs) ||
      (count && (!pairs || !Readable(pairs, static_cast<size_t>(count) * 8)))) {
    return nullptr;
  }
  uint32_t first = 0;
  uint32_t last = count;
  while (first < last) {
    const uint32_t middle = first + (last - first) / 2;
    uint32_t candidate = 0;
    if (!Read(static_cast<uint8_t*>(pairs) + middle * 8, &candidate)) {
      return nullptr;
    }
    if (candidate < key) {
      first = middle + 1;
    } else {
      last = middle;
    }
  }
  if (first >= count) return nullptr;
  uint32_t candidate = 0;
  void* marker = nullptr;
  if (!Read(static_cast<uint8_t*>(pairs) + first * 8, &candidate) ||
      candidate != key ||
      !Read(static_cast<uint8_t*>(pairs) + first * 8 + 4, &marker) ||
      !marker || !Readable(marker, 0x22A)) {
    return nullptr;
  }
  uint32_t marker_key = 0;
  return Read(static_cast<uint8_t*>(marker) + 0x210, &marker_key) &&
                 marker_key == key
             ? marker
             : nullptr;
}

int ObjectLevel(void* object) {
  void* floor = nullptr;
  int level = -1;
  if (object && Readable(object, 0x12C) &&
      Read(static_cast<uint8_t*>(object) + 0x128, &floor) && floor &&
      Readable(floor, 0x60) &&
      Read(static_cast<uint8_t*>(floor) + 0x5C, &level) &&
      level >= -32 && level <= 32) {
    return level;
  }
  return -1;
}

using ResolveObjectFn = void*(DXS_GAME_CDECL*)(uint32_t);

int InferObjectiveLevel(void* map_dtp, ResolveObjectFn resolve_object,
                        float world_x, float world_y, float world_z) {
  if (!map_dtp || !resolve_object) return -1;
  uint32_t anchor_count = 0;
  void* anchor_records = nullptr;
  if (!Read(static_cast<uint8_t*>(map_dtp) + 0x24, &anchor_count) ||
      anchor_count > 1024 ||
      !Read(static_cast<uint8_t*>(map_dtp) + 0x28, &anchor_records) ||
      (anchor_count &&
       (!anchor_records ||
        !Readable(anchor_records, static_cast<size_t>(anchor_count) * 8)))) {
    return -1;
  }

  double closest_distance = std::numeric_limits<double>::infinity();
  int closest_level = -1;
  for (uint32_t index = 0; index < anchor_count; ++index) {
    uint32_t object_ref = 0;
    if (!Read(static_cast<uint8_t*>(anchor_records) + index * 8 + 4,
              &object_ref) || !object_ref) {
      continue;
    }
    void* object = resolve_object(object_ref);
    float position[3] = {};
    const int level = ObjectLevel(object);
    if (level < -32 || level > 32 || !object || !Readable(object, 0x2C) ||
        !Read(static_cast<uint8_t*>(object) + 0x20, &position[0]) ||
        !Read(static_cast<uint8_t*>(object) + 0x24, &position[1]) ||
        !Read(static_cast<uint8_t*>(object) + 0x28, &position[2]) ||
        !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
        !std::isfinite(position[2])) {
      continue;
    }
    const double dx = static_cast<double>(position[0]) - world_x;
    const double dy = static_cast<double>(position[1]) - world_y;
    const double dz = static_cast<double>(position[2]) - world_z;
    const double distance = dx * dx + dy * dy + dz * dz;
    if (std::isfinite(distance) && distance < closest_distance) {
      closest_distance = distance;
      closest_level = level;
    }
  }
  return closest_level;
}

void CaptureMissionLocators(void* player_wrapper, void* map_dtp,
                            ResolveObjectFn resolve_object,
                            NativeMapSnapshot* out) {
  if (!player_wrapper || !map_dtp || !out || !MissionLocatorCodeReady() ||
      !Readable(player_wrapper, 0x34)) {
    return;
  }
  void* manager = nullptr;
  if (!Read(static_cast<uint8_t*>(player_wrapper) + 0x30, &manager) ||
      !manager || !Readable(manager, 0x10)) {
    return;
  }

  using ContainerFn = void*(DXS_GAME_THISCALL*)(void*);
  using RecordsFn = void*(DXS_GAME_THISCALL*)(void*, uint32_t*);
  using TextFn = const char*(DXS_GAME_THISCALL*)(void*);
  using IndexFn = int(DXS_GAME_THISCALL*)(void*);
  using PrimaryFn = bool(DXS_GAME_THISCALL*)(void*);
  auto container = reinterpret_cast<ContainerFn>(
      Rebase(kMissionLocatorContainerVA));
  auto records_for = reinterpret_cast<RecordsFn>(
      Rebase(kMissionLocatorRecordsVA));
  auto short_text = reinterpret_cast<TextFn>(Rebase(kMissionShortTextVA));
  auto mission_index = reinterpret_cast<IndexFn>(Rebase(kMissionIndexVA));
  auto primary = reinterpret_cast<PrimaryFn>(Rebase(kMissionPrimaryVA));

  void* list = container(manager);
  void* sentinel = nullptr;
  void* node = nullptr;
  if (!list || !Readable(list, sizeof(void*)) || !Read(list, &sentinel) ||
      !sentinel || !Readable(sentinel, sizeof(void*)) ||
      !Read(sentinel, &node)) {
    return;
  }

  // Match SmartGlassMap::SerializeMissionMarkers: first ask every locator to
  // refresh its game-owned position buffer, then consume those buffers. The
  // retail serializer copies this intrusive list only to keep iteration
  // stable; Present runs on the same game thread, so bounded direct traversal
  // avoids allocating any game-owned nodes from the mod.
  void* locators[kMaxMissionLocators] = {};
  uint32_t locator_count = 0;
  for (uint32_t visited = 0;
       node && node != sentinel && visited < kMaxMissionLocators; ++visited) {
    if (!Readable(node, 12)) return;
    void* next = nullptr;
    void* locator = nullptr;
    if (!Read(node, &next) ||
        !Read(static_cast<uint8_t*>(node) + 8, &locator)) {
      return;
    }
    if (locator && Readable(locator, 0x34)) {
      locators[locator_count++] = locator;
      uint32_t ignored_count = 0;
      records_for(locator, &ignored_count);
    }
    if (!next || next == node) return;
    node = next;
  }

  for (uint32_t locator_index = 0;
       locator_index < locator_count &&
       out->objective_marker_count < kMaximumNativeObjectiveMarkers;
       ++locator_index) {
    void* locator = locators[locator_index];
    void* mission = nullptr;
    if (!Read(static_cast<uint8_t*>(locator) + 0x10, &mission) || !mission ||
        !Readable(mission, 0x20)) {
      continue;
    }
    uint8_t tracked = 0;
    Read(static_cast<uint8_t*>(mission) + 0x1C, &tracked);
    const bool is_primary = primary(mission);
    const int id = mission_index(mission);
    const char* title = short_text(mission);

    uint32_t record_count = 0;
    void* records = records_for(locator, &record_count);
    if (!record_count || record_count > kMaxMissionLocatorRecords ||
        !records ||
        !Readable(records, static_cast<size_t>(record_count) * 0x20)) {
      continue;
    }
    for (uint32_t record_index = 0;
         record_index < record_count &&
         out->objective_marker_count < kMaximumNativeObjectiveMarkers;
         ++record_index) {
      uint8_t* record = static_cast<uint8_t*>(records) + record_index * 0x20;
      uint8_t valid = 0;
      float position[3] = {};
      if (!Read(record + 0x10, &valid) || !valid ||
          !Read(record + 0x00, &position[0]) ||
          !Read(record + 0x04, &position[1]) ||
          !Read(record + 0x08, &position[2]) ||
          !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
          !std::isfinite(position[2])) {
        continue;
      }
      const float normalized_x = VanillaNormalize(
          position[0], out->bound_a_x, out->width);
      const float normalized_y = VanillaNormalize(
          position[1], out->bound_a_y, out->height);
      if (!std::isfinite(normalized_x) || !std::isfinite(normalized_y) ||
          normalized_x < 0.0f || normalized_x > 1.0f ||
          normalized_y < 0.0f || normalized_y > 1.0f) {
        continue;
      }

      bool duplicate = false;
      for (int old_index = 0; old_index < out->objective_marker_count;
           ++old_index) {
        const NativeObjectiveMarker& old =
            out->objective_markers[old_index];
        if (old.mission_id == id &&
            std::fabs(old.normalized_x - normalized_x) < 0.0001f &&
            std::fabs(old.normalized_y - normalized_y) < 0.0001f) {
          duplicate = true;
          break;
        }
      }
      if (duplicate) continue;

      NativeObjectiveMarker& marker =
          out->objective_markers[out->objective_marker_count++];
      marker.mission_id = id;
      marker.primary = is_primary;
      marker.tracked = tracked != 0;
      marker.world_x = position[0];
      marker.world_y = position[1];
      marker.world_z = position[2];
      marker.normalized_x = normalized_x;
      marker.normalized_y = normalized_y;
      marker.level = InferObjectiveLevel(map_dtp, resolve_object,
                                         position[0], position[1],
                                         position[2]);
      CopyString(title, marker.title);
    }
  }
}

void CaptureRadar(void* controller) {
  RadarSnapshot next;
  if (!controller || !Readable(controller, 0x64)) return;

  uint32_t handle_count = 0;
  uint32_t* handles = nullptr;
  void* handle_table = nullptr;
  if (!Read(static_cast<uint8_t*>(controller) + 0x58, &handle_count) ||
      handle_count > 2048 ||
      !Read(static_cast<uint8_t*>(controller) + 0x60, &handles) ||
      (handle_count &&
       (!handles || !Readable(handles,
                              static_cast<size_t>(handle_count) * 4))) ||
      !Read(reinterpret_cast<void*>(Rebase(kHandleTableGlobalVA)),
            &handle_table) ||
      !handle_table) {
    return;
  }
  next.live = true;

  for (uint32_t index = 0;
       index < handle_count && next.count < kMaxRadarContacts; ++index) {
    uint32_t handle = 0;
    if (!Read(handles + index, &handle) || !handle || handle > 0x00FFFFFFu) {
      continue;
    }
    const uintptr_t entry_address =
        reinterpret_cast<uintptr_t>(handle_table) +
        static_cast<uintptr_t>(handle) * 8u;
    uint32_t flags = 0;
    void* handle_object = nullptr;
    if (!Read(reinterpret_cast<void*>(entry_address), &flags) || (flags & 2u) ||
        !Read(reinterpret_cast<void*>(entry_address + 4), &handle_object) ||
        !handle_object || !Readable(handle_object, 0x24)) {
      continue;
    }
    uint32_t live_cookie = 0;
    void* actor = nullptr;
    if (!Read(static_cast<uint8_t*>(handle_object) + 0x0C, &live_cookie) ||
        !live_cookie ||
        !Read(static_cast<uint8_t*>(handle_object) + 0x20, &actor) ||
        !actor || !Readable(actor, 0x2C)) {
      continue;
    }
    uint32_t key = 0;
    float position[3] = {};
    if (!Read(static_cast<uint8_t*>(actor) + 0x0C, &key) || !key ||
        !Read(static_cast<uint8_t*>(actor) + 0x20, &position[0]) ||
        !Read(static_cast<uint8_t*>(actor) + 0x24, &position[1]) ||
        !Read(static_cast<uint8_t*>(actor) + 0x28, &position[2]) ||
        !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
        !std::isfinite(position[2])) {
      continue;
    }
    void* marker = FindRadarMarker(key);
    if (!marker) continue;
    int16_t type = -1;
    int16_t state = -1;
    if (!Read(static_cast<uint8_t*>(marker) + 0x226, &type) ||
        !Read(static_cast<uint8_t*>(marker) + 0x228, &state) ||
        type < 0 || type > 4 || state < 0 || state > 4) {
      continue;
    }
    RadarContact& contact = next.contacts[next.count++];
    contact.key = key;
    contact.type = static_cast<uint16_t>(type);
    contact.state = static_cast<uint16_t>(state);
    contact.level = ObjectLevel(actor);
    contact.x = position[0];
    contact.y = position[1];
    contact.z = position[2];
  }

  next.captured_ms = GetTickCount64();

  AcquireSRWLockExclusive(&g_radar_lock);
  g_radar = next;
  ReleaseSRWLockExclusive(&g_radar_lock);
}

void DXS_HOOK_FASTCALL RadarUpdateHook(void* self, void* edx, float dt) {
  g_radar_callbacks.fetch_add(1, std::memory_order_acq_rel);
  RadarUpdateFn original =
      g_original_radar_update.load(std::memory_order_acquire);
  if (original) original(self, edx, dt);
  const uint64_t now = GetTickCount64();
  const uint64_t requested_until =
      g_radar_requested_until_ms.load(std::memory_order_acquire);
  if (g_radar_capture_enabled.load(std::memory_order_acquire) &&
      now <= requested_until) {
    uint64_t previous =
        g_last_radar_capture_ms.load(std::memory_order_relaxed);
    if ((previous == 0 || now - previous >= kRadarCaptureIntervalMs) &&
        g_last_radar_capture_ms.compare_exchange_strong(
            previous, now, std::memory_order_acq_rel,
            std::memory_order_relaxed)) {
      CaptureRadar(self);
    }
  }
  g_radar_callbacks.fetch_sub(1, std::memory_order_acq_rel);
}

bool ReadVisible(void* object) {
  void* a = nullptr;
  void* b = nullptr;
  void* c = nullptr;
  uint8_t visible = 0;
  return object && Readable(object, 0x2E8) &&
         Read(static_cast<uint8_t*>(object) + 0x2E4, &a) && a &&
         Readable(a, 8) && Read(static_cast<uint8_t*>(a) + 4, &b) && b &&
         Readable(b, 0x5C) && Read(static_cast<uint8_t*>(b) + 0x58, &c) && c &&
         Readable(c, 0x2D) &&
         Read(static_cast<uint8_t*>(c) + 0x2C, &visible) && visible != 0;
}

NativeMapMarkerKind KindForRadarState(uint16_t state) {
  switch (state) {
    case 0: return NativeMapMarkerKind::kFriend;
    case 1: return NativeMapMarkerKind::kNeutral;
    case 2: return NativeMapMarkerKind::kCautious;
    case 3: return NativeMapMarkerKind::kAlert;
    case 4: return NativeMapMarkerKind::kHostile;
    default: return NativeMapMarkerKind::kNeutral;
  }
}

void ReportMapStage(const char* stage, bool success = false,
                    uint32_t map_id = 0, int level = 0) {
  static const char* last_stage = nullptr;
  static uint64_t last_report_ms = 0;
  static bool reported_success = false;
  const uint64_t now = GetTickCount64();
  if (success) {
    if (!reported_success || last_stage) {
      DXS_INFO("[map] native snapshot ready map=%u floor=%d",
               static_cast<unsigned>(map_id), level);
    }
    last_stage = nullptr;
    last_report_ms = now;
    reported_success = true;
    return;
  }
  if (stage != last_stage || last_report_ms == 0 ||
      now - last_report_ms >= 10000) {
    DXS_WARN("[map] native snapshot waiting at %s", stage);
    last_stage = stage;
    last_report_ms = now;
  }
}

}  // namespace

bool InstallNativeRadarCapture(RttiIndex* rtti, HookSet* hooks) {
  if (!rtti || !hooks ||
      g_original_radar_update.load(std::memory_order_acquire)) {
    return false;
  }
  static const uint8_t update_signature[] = {
      0x55, 0x8B, 0xEC, 0x83, 0xE4, 0xF0, 0xD9, 0x45,
      0x08, 0x83, 0xEC, 0x54, 0x53, 0x56, 0x57, 0x51};
  if (!HasCode(kRadarUpdateVA, update_signature)) {
    DXS_WARN("[map] radar Update signature does not match DXHRDC 2.0.66.0");
    return false;
  }
  const RttiClass* radar = rtti->Find("NsRadarMovieController");
  if (!radar || !radar->vtable || radar->method_count <= kRadarUpdateSlot ||
      radar->vtable != reinterpret_cast<void**>(Rebase(kRadarControllerVtableVA))) {
    DXS_WARN("[map] exact NsRadarMovieController vtable was not recovered");
    return false;
  }
  void* original = nullptr;
  if (!Read(radar->vtable + kRadarUpdateSlot, &original) ||
      original != reinterpret_cast<void*>(Rebase(kRadarUpdateVA))) {
    DXS_WARN("[map] radar Update slot differs from the supported build");
    return false;
  }
  g_original_radar_update.store(reinterpret_cast<RadarUpdateFn>(original),
                                std::memory_order_release);
  g_radar_capture_enabled.store(true, std::memory_order_release);
  if (!hooks->Add(radar->vtable, kRadarUpdateSlot,
                  reinterpret_cast<void*>(&RadarUpdateHook))) {
    g_radar_capture_enabled.store(false, std::memory_order_release);
    g_original_radar_update.store(nullptr, std::memory_order_release);
    return false;
  }
  DXS_INFO("[map] native radar contact capture attached (slot %u)",
           kRadarUpdateSlot);
  return true;
}

bool CaptureNativeMapSnapshot(NativeMapSnapshot* out) {
  if (!out) return false;
  // NativeMapSnapshot contains 256 marker records. Reconstruct the caller's
  // staging object directly instead of putting another ~65 KiB aggregate on
  // DXHR's Present-thread stack at every map poll.
  new (out) NativeMapSnapshot();

  // Refreshing the map page keeps a short capture lease alive. Resetting the
  // throttle when a new lease begins makes the first radar update responsive,
  // while hidden pages incur no handle-table scan at all.
  const uint64_t request_now = GetTickCount64();
  const uint64_t prior_deadline = g_radar_requested_until_ms.exchange(
      request_now + kRadarRequestWindowMs, std::memory_order_acq_rel);
  if (prior_deadline < request_now) {
    g_last_radar_capture_ms.store(0, std::memory_order_release);
  }
  if (!MapCodeReady()) {
    ReportMapStage("supported-code gate");
    return false;
  }

  void* player_wrapper = nullptr;
  void* player = CurrentPlayer(&player_wrapper);
  if (!player) {
    ReportMapStage("current player");
    return false;
  }

  using ContextFn = void*(DXS_GAME_CDECL*)(void*);
  void* entity_key = nullptr;
  void* context_record = nullptr;
  void* context = nullptr;
  void* default_map_holder = nullptr;
  if (!Read(static_cast<uint8_t*>(player) + 0x110, &entity_key) || !entity_key) {
    ReportMapStage("player entity key");
    return false;
  }
  context_record = reinterpret_cast<ContextFn>(Rebase(kEntityContextVA))(
      entity_key);
  if (!context_record || !Readable(context_record, 0x30) ||
      !Read(static_cast<uint8_t*>(context_record) + 0x2C, &context) ||
      !context || !Readable(context, 0x148) ||
      !Read(static_cast<uint8_t*>(context) + 0x144, &default_map_holder) ||
      !default_map_holder || !Readable(default_map_holder, 0x130)) {
    ReportMapStage("world map context");
    return false;
  }
  uint32_t map_id = 0;
  if (!Read(static_cast<uint8_t*>(default_map_holder) + 0x12C, &map_id)) {
    ReportMapStage("default map id");
    return false;
  }

  // Follow SmartGlassMap::Enter's exact retail accessor chain. 0x7B9CE0
  // returns the validated controller owner, 0x7B96A0 selects its live map
  // state, and the final two accessors read the current floor and optional
  // interior-map override. The old code treated the owner itself as that
  // state object and read +0x528/+0x52c from the wrong base, so every valid
  // save failed before the cached map raster could be selected.
  using OwnerFn = void*(DXS_GAME_CDECL*)(void*);
  void* owner = reinterpret_cast<OwnerFn>(Rebase(kMapOverrideOwnerVA))(player);
  if (owner && Readable(owner, 0x52C)) {
    using StateFn = void*(DXS_GAME_THISCALL*)(void*);
    using StateIntFn = int(DXS_GAME_THISCALL*)(void*);
    void* state = reinterpret_cast<StateFn>(Rebase(kMapOwnerStateVA))(owner);
    if (state && Readable(state, 12)) {
      const int current_level =
          reinterpret_cast<StateIntFn>(Rebase(kMapCurrentLevelVA))(state);
      const int override_map =
          reinterpret_cast<StateIntFn>(Rebase(kMapOverrideIdVA))(state);
      if (current_level >= -32 && current_level <= 32) {
        out->level = current_level;
      }
      if (override_map > 0 &&
          static_cast<uint32_t>(override_map) < kMaxDtpId) {
        map_id = static_cast<uint32_t>(override_map);
      }
    }
  }
  if (!map_id || map_id >= kMaxDtpId) {
    ReportMapStage("resolved map id");
    return false;
  }

  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  auto resolve_dtp = reinterpret_cast<ResolveFn>(Rebase(kResolveDtpVA));
  void* map_dtp = resolve_dtp(map_id);
  if (!map_dtp || !Readable(map_dtp, 0x34)) {
    ReportMapStage("map DTP");
    return false;
  }

  uint32_t bound_ref_a = 0;
  uint32_t bound_ref_b = 0;
  if (!Read(static_cast<uint8_t*>(map_dtp) + 0x10, &bound_ref_a) ||
      !Read(static_cast<uint8_t*>(map_dtp) + 0x14, &bound_ref_b) ||
      !bound_ref_a || !bound_ref_b) {
    ReportMapStage("map bounds references");
    return false;
  }
  // ResolveObjectRef starts with an integer divide by the bucket count at
  // 0xE04EC0. During a level transition the table can briefly be uninitialized;
  // calling retail in that window would raise a divide-by-zero exception.
  // Wait for the next 250 ms snapshot poll instead. This is a readiness guard,
  // not part of map identity and therefore never gets cached as a failure.
  uint32_t object_ref_bucket_count = 0;
  if (!Read(reinterpret_cast<void*>(Rebase(kObjectRefBucketCountVA)),
            &object_ref_bucket_count) || object_ref_bucket_count == 0) {
    ReportMapStage("object reference table");
    return false;
  }
  auto resolve_object = reinterpret_cast<ResolveFn>(
      Rebase(kResolveObjectRefVA));
  void* bound_a = resolve_object(bound_ref_a);
  void* bound_b = resolve_object(bound_ref_b);
  float a[4] = {};
  float b[4] = {};
  if (!bound_a || !bound_b || !Readable(bound_a, 0x30) ||
      !Readable(bound_b, 0x30) ||
      !Read(static_cast<uint8_t*>(bound_a) + 0x20, &a[0]) ||
      !Read(static_cast<uint8_t*>(bound_a) + 0x24, &a[1]) ||
      !Read(static_cast<uint8_t*>(bound_a) + 0x28, &a[2]) ||
      !Read(static_cast<uint8_t*>(bound_a) + 0x2C, &a[3]) ||
      !Read(static_cast<uint8_t*>(bound_b) + 0x20, &b[0]) ||
      !Read(static_cast<uint8_t*>(bound_b) + 0x24, &b[1]) ||
      !Read(static_cast<uint8_t*>(bound_b) + 0x28, &b[2]) ||
      !Read(static_cast<uint8_t*>(bound_b) + 0x2C, &b[3])) {
    ReportMapStage("resolved map bounds");
    return false;
  }
  for (int component = 0; component < 4; ++component) {
    if (!std::isfinite(a[component]) || !std::isfinite(b[component])) {
      ReportMapStage("finite map bounds");
      return false;
    }
  }
  const float width = std::fabs(b[0] - a[0]);
  const float height = std::fabs(b[1] - a[1]);
  if (width <= 0.0001f || height <= 0.0001f || width > 10000000.0f ||
      height > 10000000.0f) {
    ReportMapStage("map extent");
    return false;
  }
  out->map_resource_id = map_id;
  out->bound_a_x = a[0];
  out->bound_a_y = a[1];
  out->bound_b_x = b[0];
  out->bound_b_y = b[1];
  out->width = width;
  out->height = height;

  int16_t location_id = -1;
  int16_t sublocation_id = -1;
  Read(static_cast<uint8_t*>(map_dtp) + 0x18, &location_id);
  Read(static_cast<uint8_t*>(map_dtp) + 0x1A, &sublocation_id);
  using LocalStringFn = const char*(DXS_GAME_CDECL*)(int);
  LocalStringFn local_string = nullptr;
  if (LocalStringCodeReady()) {
    local_string = reinterpret_cast<LocalStringFn>(Rebase(kLocalStringVA));
    if (location_id >= 0) CopyString(local_string(location_id), out->location);
    if (sublocation_id >= 0) {
      CopyString(local_string(sublocation_id), out->sublocation);
    }
  }

  using ImageFn = void*(DXS_GAME_CDECL*)(const void*);
  void* image_resource = MapImageCodeReady()
      ? reinterpret_cast<ImageFn>(Rebase(kMapImageResourceVA))(map_dtp)
      : nullptr;
  const char* image_path = nullptr;
  if (image_resource && Readable(image_resource, sizeof(void*)) &&
      Read(image_resource, &image_path) && SafeCStr(image_path, 1024)) {
    CopyString(image_path, out->image_path);
    CopyString(Basename(image_path), out->image_name);
  }

  CapturePlayerPose(player, a[0], a[1], b[0], b[1], width, height, out);
  out->display_level = out->level;

  CaptureMissionLocators(player_wrapper, map_dtp, resolve_object, out);

  uint32_t static_count = 0;
  void* static_records = nullptr;
  if (Read(static_cast<uint8_t*>(map_dtp) + 0x2C, &static_count) &&
      static_count <= kMaxStaticMarkers &&
      Read(static_cast<uint8_t*>(map_dtp) + 0x30, &static_records) &&
      (!static_count ||
       (static_records &&
        Readable(static_records, static_cast<size_t>(static_count) * 0x14)))) {
    for (uint32_t index = 0;
         index < static_count && out->marker_count < 256; ++index) {
      uint8_t* record = static_cast<uint8_t*>(static_records) + index * 0x14;
      uint32_t object_ref = 0;
      int16_t name_id = -1;
      const char* icon_path = nullptr;
      if (!Read(record + 4, &object_ref) || !object_ref ||
          !Read(record + 8, &name_id) ||
          !Read(record + 0x0C, &icon_path)) {
        continue;
      }
      void* object = resolve_object(object_ref);
      float position[3] = {};
      if (!object || !Readable(object, 0x2E8) || !ReadVisible(object) ||
          !Read(static_cast<uint8_t*>(object) + 0x20, &position[0]) ||
          !Read(static_cast<uint8_t*>(object) + 0x24, &position[1]) ||
          !Read(static_cast<uint8_t*>(object) + 0x28, &position[2]) ||
          !std::isfinite(position[0]) || !std::isfinite(position[1]) ||
          !std::isfinite(position[2])) {
        continue;
      }
      NativeMapMarker& marker = out->markers[out->marker_count++];
      marker.kind = NativeMapMarkerKind::kStatic;
      marker.level = ObjectLevel(object);
      marker.world_x = position[0];
      marker.world_y = position[1];
      marker.world_z = position[2];
      marker.normalized_x = VanillaNormalize(position[0], a[0], width);
      marker.normalized_y = VanillaNormalize(position[1], a[1], height);
      if (local_string && name_id >= 0) {
        CopyString(local_string(name_id), marker.name);
      }
      if (SafeCStr(icon_path, 512)) {
        CopyString(Basename(icon_path), marker.icon);
      }
    }
  }

  RadarSnapshot radar;
  AcquireSRWLockShared(&g_radar_lock);
  radar = g_radar;
  ReleaseSRWLockShared(&g_radar_lock);
  const uint64_t snapshot_now = GetTickCount64();
  const bool radar_fresh = radar.live && radar.captured_ms != 0 &&
      snapshot_now >= radar.captured_ms &&
      snapshot_now - radar.captured_ms <= kRadarFreshnessMs;
  out->radar_live = radar_fresh;
  if (!radar_fresh) radar.count = 0;
  for (uint32_t index = 0;
       index < radar.count && out->marker_count < 256; ++index) {
    const RadarContact& contact = radar.contacts[index];
    const float normalized_x = VanillaNormalize(contact.x, a[0], width);
    const float normalized_y = VanillaNormalize(contact.y, a[1], height);
    // Radar can retain a fading marker for an actor from the previous streamed
    // cell. Do not paint it on an unrelated map once it is clearly out of
    // bounds; keep a small tolerance for authored boundary contacts.
    if (normalized_x > 1.05f || normalized_y > 1.05f) continue;
    NativeMapMarker& marker = out->markers[out->marker_count++];
    marker.kind = KindForRadarState(contact.state);
    marker.radar_type = contact.type;
    marker.radar_state = contact.state;
    marker.entity_key = contact.key;
    marker.level = contact.level;
    marker.world_x = contact.x;
    marker.world_y = contact.y;
    marker.world_z = contact.z;
    marker.normalized_x = normalized_x;
    marker.normalized_y = normalized_y;
  }

  out->valid = true;
  ReportMapStage(nullptr, true, out->map_resource_id, out->level);
  return true;
}

bool RefreshNativePlayerMapPose(NativeMapSnapshot* snapshot) {
  if (!snapshot || !snapshot->valid || !MapCodeReady() ||
      snapshot->width <= 0.0001f || snapshot->height <= 0.0001f) {
    return false;
  }
  void* player = CurrentPlayer();
  if (!player) return false;

  // Clear only transient pose validity. Map identity, raster metadata, radar
  // contacts and location text remain the coherent slower map snapshot.
  snapshot->player_valid = false;
  snapshot->player_heading_valid = false;
  return CapturePlayerPose(player, snapshot->bound_a_x, snapshot->bound_a_y,
                           snapshot->bound_b_x, snapshot->bound_b_y,
                           snapshot->width, snapshot->height, snapshot);
}

void DisableNativeMapCapture() {
  g_radar_capture_enabled.store(false, std::memory_order_release);
  g_last_radar_capture_ms.store(0, std::memory_order_release);
  g_radar_requested_until_ms.store(0, std::memory_order_release);
  AcquireSRWLockExclusive(&g_radar_lock);
  g_radar = RadarSnapshot{};
  ReleaseSRWLockExclusive(&g_radar_lock);
}

void ResetNativeMapCapture() {
  // The caller has already restored the vtable, so no new callback can begin.
  // A callback that fetched the old slot immediately before restoration may
  // still be returning from vanilla; give it a bounded chance to finish before
  // clearing the original pointer used by the thunk.
  DisableNativeMapCapture();
  for (unsigned spin = 0;
       spin < 1000 &&
       g_radar_callbacks.load(std::memory_order_acquire) != 0;
       ++spin) {
    Sleep(0);
  }
  if (g_radar_callbacks.load(std::memory_order_acquire) != 0) {
    DXS_WARN("[map] radar callback still active during shutdown");
    return;
  }
  g_original_radar_update.store(nullptr, std::memory_order_release);
}

}  // namespace dxs
