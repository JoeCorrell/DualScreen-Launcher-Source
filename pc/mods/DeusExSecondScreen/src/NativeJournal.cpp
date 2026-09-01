#include "NativeJournal.h"

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "Log.h"
#include "Mem.h"
#include "SmartGlass.h"

namespace dxs {
namespace {

#if defined(__GNUC__)
#define DXS_GAME_THISCALL __attribute__((thiscall))
#define DXS_GAME_CDECL __attribute__((cdecl))
#else
#define DXS_GAME_THISCALL __thiscall
#define DXS_GAME_CDECL __cdecl
#endif

// DXHRDC.exe 2.0.66.0, PE timestamp 0x52655004. These are the exact accessors
// used by SmartGlassMission::Enter (0x006B78D0), its mission serializer
// (0x006B7740), and SmartGlassMedia::Enter (0x006B7290).
constexpr uintptr_t kPlayerListGlobalVA = 0x01A07B1C;
constexpr uintptr_t kCurrentPlayerWrapperVA = 0x00602F60;
constexpr uintptr_t kMissionContainerVA = 0x009E1E60;
constexpr uintptr_t kMissionInactiveVA = 0x006D5740;
constexpr uintptr_t kMissionShortTextVA = 0x006D5690;
constexpr uintptr_t kMissionLongTextVA = 0x006D56C0;
constexpr uintptr_t kMissionPrimaryVA = 0x006D5770;
constexpr uintptr_t kObjectiveInactiveVA = 0x006D7290;
constexpr uintptr_t kTrackMissionVA = 0x006D6190;
constexpr uintptr_t kUntrackMissionVA = 0x006D6210;
constexpr uintptr_t kResolveDtpVA = 0x005A4E60;
constexpr uintptr_t kLocalStringVA = 0x004CC930;

constexpr uint32_t kMaxDtpId = 0x18000u;
constexpr int kMaxMissions = 256;
constexpr int kMaxMedia = 512;

template <size_t N>
bool HasCode(uintptr_t preferred_va, const uint8_t (&signature)[N]) {
  return PreferredCodeMatches(reinterpret_cast<const void*>(Rebase(preferred_va)),
                              signature, N);
}

bool JournalCodeReady() {
  static const uint8_t current_signature[] = {
      0x8B, 0x41, 0x08, 0x8B, 0x04, 0x81, 0xC3};
  static const uint8_t container_signature[] = {
      0x8D, 0x41, 0x04, 0xC3};
  static const uint8_t inactive_signature[] = {
      0x8B, 0x49, 0x10, 0x33, 0xC0, 0x3B, 0x4C, 0x24, 0x04};
  static const uint8_t short_signature[] = {
      0x8B, 0x01, 0x85, 0xC0, 0x74, 0x11, 0x0F, 0xBF, 0x00};
  static const uint8_t long_signature[] = {
      0x8B, 0x01, 0x85, 0xC0, 0x74, 0x12, 0x0F, 0xBF, 0x40, 0x02};
  static const uint8_t primary_signature[] = {
      0x8B, 0x01, 0x85, 0xC0, 0x74, 0x0A, 0x0F, 0xB6, 0x40, 0x04};
  static const uint8_t objective_inactive_signature[] = {
      0x8B, 0x49, 0x08, 0x33, 0xC0, 0x3B, 0x4C, 0x24, 0x04};
  static const uint8_t resolve_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x80, 0x01,
      0x00, 0x73, 0x15, 0x8B, 0x04, 0x85, 0xE8, 0x06,
      0x98, 0x01};
  static const uint8_t local_string_signature[] = {
      0x8B, 0x15, 0xF8, 0x43, 0xB2, 0x00, 0x85, 0xD2,
      0x74, 0x1B, 0x8B, 0x0D, 0xF4, 0x43, 0xB2, 0x00,
      0x85, 0xC9, 0x74, 0x11, 0x8B, 0x44, 0x24, 0x04};
  return HasCode(kCurrentPlayerWrapperVA, current_signature) &&
      HasCode(kMissionContainerVA, container_signature) &&
      HasCode(kMissionInactiveVA, inactive_signature) &&
      HasCode(kMissionShortTextVA, short_signature) &&
      HasCode(kMissionLongTextVA, long_signature) &&
      HasCode(kMissionPrimaryVA, primary_signature) &&
      HasCode(kObjectiveInactiveVA, objective_inactive_signature) &&
      HasCode(kResolveDtpVA, resolve_signature) &&
      HasCode(kLocalStringVA, local_string_signature);
}

void* CurrentPlayerWrapper() {
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
      !expected || !Readable(expected, 0x3C)) {
    return nullptr;
  }
  using Fn = void*(DXS_GAME_THISCALL*)(void*);
  void* wrapper = reinterpret_cast<Fn>(Rebase(kCurrentPlayerWrapperVA))(
      player_list);
  return wrapper == expected && Readable(wrapper, 0x3C) ? wrapper : nullptr;
}

std::wstring Wide(const char* value) {
  value = SafeCStr(value, 4096);
  if (!value || !*value) return {};
  UINT code_page = CP_UTF8;
  int count = MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, value, -1,
                                  nullptr, 0);
  if (count <= 1) {
    code_page = CP_ACP;
    count = MultiByteToWideChar(code_page, 0, value, -1, nullptr, 0);
  }
  if (count <= 1 || count > 4096) return {};
  std::wstring result(static_cast<size_t>(count), L'\0');
  MultiByteToWideChar(code_page, code_page == CP_UTF8 ? MB_ERR_INVALID_CHARS : 0,
                      value, -1, &result[0], count);
  if (!result.empty() && result.back() == L'\0') result.pop_back();
  return result;
}

bool ReadObjectiveList(void* list, int mission_id, bool mission_primary,
                       bool mission_tracked,
                       std::vector<SmartGlassObjective>* output) {
  if (!list || !output || !Readable(list, sizeof(void*))) return false;
  void* sentinel = nullptr;
  void* node = nullptr;
  if (!Read(list, &sentinel)) {
    return false;
  }
  // Retail treats a null list head as a valid empty objective list. Some
  // missions only populate one of their two embedded lists, so rejecting the
  // empty sibling made otherwise valid mission data look like a capture
  // failure in diagnostics.
  if (!sentinel) return true;
  if (!Readable(sentinel, sizeof(void*)) || !Read(sentinel, &node)) {
    return false;
  }

  using InactiveFn = bool(DXS_GAME_THISCALL*)(void*, int);
  using TextFn = const char*(DXS_GAME_THISCALL*)(void*);
  auto inactive = reinterpret_cast<InactiveFn>(Rebase(kObjectiveInactiveVA));
  auto short_text = reinterpret_cast<TextFn>(Rebase(kMissionShortTextVA));
  auto long_text = reinterpret_cast<TextFn>(Rebase(kMissionLongTextVA));

  for (int visited = 0; node && node != sentinel && visited < kMaxMissions;
       ++visited) {
    if (!Readable(node, 12)) return false;
    void* next = nullptr;
    void* objective_record = nullptr;
    if (!Read(node, &next) ||
        !Read(static_cast<uint8_t*>(node) + 8, &objective_record)) {
      return false;
    }
    if (objective_record && Readable(objective_record, 0x14) &&
        !inactive(objective_record, 0)) {
      int objective_id = -1;
      int state = -1;
      if (Read(static_cast<uint8_t*>(objective_record) + 4, &objective_id) &&
          objective_id >= 0 &&
          Read(static_cast<uint8_t*>(objective_record) + 8, &state)) {
        SmartGlassObjective objective;
        objective.mission_id = mission_id;
        objective.objective_id = objective_id;
        objective.state = state;
        objective.primary = mission_primary;
        objective.tracked = mission_tracked;
        objective.title = Wide(short_text(objective_record));
        objective.short_description = objective.title;
        objective.long_description = Wide(long_text(objective_record));
        if (objective.title.empty()) {
          objective.title = objective.long_description;
        }
        if (!objective.title.empty() || !objective.long_description.empty()) {
          const auto duplicate = std::find_if(
              output->begin(), output->end(), [&](const auto& old) {
                return old.mission_id == objective.mission_id &&
                    old.objective_id == objective.objective_id;
              });
          if (duplicate == output->end()) {
            output->push_back(std::move(objective));
          }
        }
      }
    }
    if (!next || next == node) return false;
    node = next;
  }
  return true;
}

bool ReadMissionList(void* manager, std::vector<SmartGlassObjective>* output,
                     void** wanted = nullptr, int wanted_id = -1) {
  if (wanted) *wanted = nullptr;
  if (!manager || !output || !Readable(manager, 8)) return false;
  using ContainerFn = void*(DXS_GAME_THISCALL*)(void*);
  void* list = reinterpret_cast<ContainerFn>(Rebase(kMissionContainerVA))(
      manager);
  void* sentinel = nullptr;
  void* node = nullptr;
  if (!list || !Readable(list, sizeof(void*)) || !Read(list, &sentinel) ||
      !sentinel || !Readable(sentinel, sizeof(void*)) ||
      !Read(sentinel, &node)) {
    return false;
  }

  using InactiveFn = bool(DXS_GAME_THISCALL*)(void*, int);
  // 0x6D5690 reads only ECX and returns with plain `ret`; the retail caller
  // pushes a legacy zero and removes it itself. Calling the actual no-argument
  // accessor avoids applying thiscall's callee-cleanup ABI to that stray slot.
  using TextFn = const char*(DXS_GAME_THISCALL*)(void*);
  using LongTextFn = const char*(DXS_GAME_THISCALL*)(void*);
  using PrimaryFn = bool(DXS_GAME_THISCALL*)(void*);
  auto inactive = reinterpret_cast<InactiveFn>(Rebase(kMissionInactiveVA));
  auto short_text = reinterpret_cast<TextFn>(Rebase(kMissionShortTextVA));
  auto long_text = reinterpret_cast<LongTextFn>(Rebase(kMissionLongTextVA));
  auto primary = reinterpret_cast<PrimaryFn>(Rebase(kMissionPrimaryVA));

  for (int visited = 0; node && node != sentinel && visited < kMaxMissions;
       ++visited) {
    if (!Readable(node, 12)) return false;
    void* next = nullptr;
    void* mission = nullptr;
    if (!Read(node, &next) || !Read(static_cast<uint8_t*>(node) + 8,
                                    &mission)) {
      return false;
    }
    if (mission && Readable(mission, 0x20) && !inactive(mission, 0)) {
      int id = -1;
      int state = -1;
      uint8_t tracked = 0;
      if (Read(static_cast<uint8_t*>(mission) + 4, &id) && id >= 0 &&
          Read(static_cast<uint8_t*>(mission) + 0x10, &state) &&
          Read(static_cast<uint8_t*>(mission) + 0x1C, &tracked)) {
        const bool is_primary = primary(mission);
        const bool is_tracked = tracked != 0;
        const size_t before = output->size();
        // SmartGlassMission serializes two objective lists embedded at +0x08
        // and +0x14. Capturing only the outer mission made the page appear
        // empty for missions whose useful text lives exclusively on their
        // objective records.
        ReadObjectiveList(static_cast<uint8_t*>(mission) + 0x08, id,
                          is_primary, is_tracked, output);
        ReadObjectiveList(static_cast<uint8_t*>(mission) + 0x14, id,
                          is_primary, is_tracked, output);
        if (output->size() == before) {
          SmartGlassObjective objective;
          objective.mission_id = id;
          objective.objective_id = id;
          objective.state = state;
          objective.primary = is_primary;
          objective.tracked = is_tracked;
          objective.title = Wide(short_text(mission));
          objective.short_description = objective.title;
          objective.long_description = Wide(long_text(mission));
          if (objective.title.empty()) {
            objective.title = objective.long_description;
          }
          if (!objective.title.empty() || !objective.long_description.empty()) {
            output->push_back(std::move(objective));
          }
        }
        if (wanted && id == wanted_id) *wanted = mission;
      }
    }
    if (next == node) break;
    node = next;
  }
  return true;
}

std::wstring Localized(int16_t id) {
  if (id < 0) return {};
  using Fn = const char*(DXS_GAME_CDECL*)(int);
  return Wide(reinterpret_cast<Fn>(Rebase(kLocalStringVA))(id));
}

void CaptureMediaVector(void* archive, size_t count_offset, size_t data_offset,
                        int media_type,
                        std::vector<SmartGlassMediaItem>* output) {
  uint32_t count = 0;
  void* records = nullptr;
  if (!Read(static_cast<uint8_t*>(archive) + count_offset, &count) ||
      count > static_cast<uint32_t>(kMaxMedia) ||
      !Read(static_cast<uint8_t*>(archive) + data_offset, &records) ||
      (count && (!records || !Readable(records, static_cast<size_t>(count) * 16)))) {
    return;
  }
  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  auto resolve = reinterpret_cast<ResolveFn>(Rebase(kResolveDtpVA));
  for (uint32_t index = 0; index < count &&
       output->size() < static_cast<size_t>(kMaxMedia); ++index) {
    const uint8_t* record = static_cast<const uint8_t*>(records) + index * 16;
    uint32_t dtp_id = 0;
    int record_id = -1;
    if (!Read(record, &dtp_id) || dtp_id == 0 || dtp_id >= kMaxDtpId ||
        !Read(record + 12, &record_id)) {
      continue;
    }
    void* dtp = resolve(dtp_id);
    if (!dtp || !Readable(dtp, 10)) continue;
    int16_t strings[5] = {-1, -1, -1, -1, -1};
    std::memcpy(strings, dtp, sizeof(strings));
    SmartGlassMediaItem item;
    item.item_id = static_cast<int>(dtp_id);
    item.media_id = record_id >= 0 ? record_id : static_cast<int>(index);
    item.media_type = media_type;
    if (media_type == 0) {
      // The e-mail DTP stores sender, recipient, subject, then body. This is
      // also the order read by the retail serializer at 0x006B6BF0.
      item.from = Localized(strings[0]);
      item.to = Localized(strings[1]);
      item.title = Localized(strings[2]);
      item.content = Localized(strings[3]);
    } else {
      // Pocket-secretary records use +0x02 for the title and +0x08 for the
      // body. +0x04 is a 32-bit volume field, not two localization IDs.
      item.title = Localized(strings[1]);
      item.content = Localized(strings[4]);
    }
    if (item.title.empty()) item.title = item.from;
    if (item.title.empty() && item.content.empty()) continue;
    const auto duplicate = std::find_if(
        output->begin(), output->end(), [&](const SmartGlassMediaItem& old) {
          return old.item_id == item.item_id && old.media_id == item.media_id;
        });
    if (duplicate == output->end()) output->push_back(std::move(item));
  }
}

uint64_t JournalRevision(const SmartGlassSnapshot& snapshot) {
  uint64_t hash = 1469598103934665603ull;
  auto add = [&hash](uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ull;
  };
  add(snapshot.objectives.size());
  for (const SmartGlassObjective& objective : snapshot.objectives) {
    add(static_cast<uint64_t>(objective.objective_id + 1));
    add(static_cast<uint64_t>(objective.state + 1));
    add(objective.tracked ? 1u : 0u);
  }
  add(snapshot.media.size());
  for (const SmartGlassMediaItem& item : snapshot.media) {
    add(static_cast<uint64_t>(item.item_id + 1));
    add(static_cast<uint64_t>(item.media_id + 1));
  }
  return hash;
}

}  // namespace

bool MergeNativeJournalSnapshot(SmartGlassSnapshot* snapshot,
                                bool capture_objectives,
                                bool capture_media) {
  if (!snapshot || (!capture_objectives && !capture_media) ||
      !JournalCodeReady()) {
    return false;
  }
  void* wrapper = CurrentPlayerWrapper();
  if (!wrapper) return false;
  bool captured = false;
  if (capture_objectives) {
    void* manager = nullptr;
    std::vector<SmartGlassObjective> objectives;
    if (Read(static_cast<uint8_t*>(wrapper) + 0x30, &manager) && manager &&
        ReadMissionList(manager, &objectives)) {
      snapshot->objectives = std::move(objectives);
      captured = true;
    }
  }
  if (capture_media) {
    void* archive = nullptr;
    if (Read(static_cast<uint8_t*>(wrapper) + 0x38, &archive) && archive &&
        Readable(archive, 0x24)) {
      std::vector<SmartGlassMediaItem> media;
      CaptureMediaVector(archive, 0x0C, 0x14, 0, &media);
      CaptureMediaVector(archive, 0x18, 0x20, 1, &media);
      snapshot->media = std::move(media);
      captured = true;
    }
  }
  if (captured) {
    snapshot->valid = true;
    snapshot->revision ^= JournalRevision(*snapshot);
  }
  return captured;
}

bool ToggleNativeObjectiveTracking(int objective_id) {
  if (objective_id < 0 || !JournalCodeReady()) return false;
  static const uint8_t track_signature[] = {
      0x53, 0x56, 0x57, 0x8B, 0xF9, 0x8B, 0x77, 0x08};
  static const uint8_t untrack_signature[] = {
      0x53, 0x55, 0x8B, 0xE9, 0x8B, 0x45, 0x08};
  if (!HasCode(kTrackMissionVA, track_signature) ||
      !HasCode(kUntrackMissionVA, untrack_signature)) {
    return false;
  }
  void* wrapper = CurrentPlayerWrapper();
  void* manager = nullptr;
  if (!wrapper || !Read(static_cast<uint8_t*>(wrapper) + 0x30, &manager) ||
      !manager) {
    return false;
  }
  std::vector<SmartGlassObjective> ignored;
  void* mission = nullptr;
  if (!ReadMissionList(manager, &ignored, &mission, objective_id) || !mission ||
      !Readable(mission, 0x20)) {
    return false;
  }
  uint8_t tracked = 0;
  if (!Read(static_cast<uint8_t*>(mission) + 0x1C, &tracked)) return false;
  using Fn = void(DXS_GAME_THISCALL*)(void*, void*);
  if (tracked) {
    reinterpret_cast<Fn>(Rebase(kUntrackMissionVA))(manager, mission);
  } else {
    reinterpret_cast<Fn>(Rebase(kTrackMissionVA))(manager, mission);
  }
  return true;
}

}  // namespace dxs
