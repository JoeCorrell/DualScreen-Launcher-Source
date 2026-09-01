#include <windows.h>

#include <cstdio>
#include <cstring>

#include "Snapshot.h"
#include "StateStore.h"
#include "Version.h"

namespace {

fo3ds::Snapshot g_snapshot = {};
fo3ds::Snapshot g_copy = {};
int g_failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::fprintf(stderr, "FAIL: %s\n", message);
  ++g_failures;
}

}  // namespace

int main() {
  using namespace fo3ds;
  ResetState(Lifecycle::kBoot, "unit-test", false);
  Check(LatestSequence() == 1, "reset publishes first sequence");
  Check(CopyLatestSnapshot(&g_copy), "reset state is copyable");
  Check(g_copy.size == sizeof(Snapshot), "reset stamps ABI size");
  Check(g_copy.lifecycle == Lifecycle::kBoot, "reset lifecycle preserved");
  Check(std::strcmp(g_copy.build_label, "unit-test") == 0,
        "reset build label preserved");

  SecureZeroMemory(&g_snapshot, sizeof(g_snapshot));
  g_snapshot.version = kSnapshotVersion;
  g_snapshot.size = sizeof(Snapshot);
  g_snapshot.sequence = 2;
  g_snapshot.world_epoch = g_copy.world_epoch;
  g_snapshot.lifecycle = Lifecycle::kLive;
  g_snapshot.supported_build = 1;
  g_snapshot.capabilities = kCapStats;
  g_snapshot.health = 90.0f;
  g_snapshot.max_health = 100.0f;
  Check(PublishSnapshot(g_snapshot), "newer valid snapshot accepted");
  Check(LatestSequence() == 2, "published sequence visible atomically");
  Check(CopyLatestSnapshot(&g_copy) && g_copy.health == 90.0f,
        "published data copied exactly");
  Check(!PublishSnapshot(g_snapshot), "duplicate sequence rejected");

  g_snapshot.sequence = 3;
  g_snapshot.capabilities |= kCapGeneralStats;
  for (uint32_t i = 0; i < kMiscStatCount; ++i)
    g_snapshot.misc_stats[i] = static_cast<int32_t>(i * 3);
  g_snapshot.radiation_resistance = 17.0f;
  Check(PublishSnapshot(g_snapshot),
        "live radiation resistance and native misc counters accepted");
  Check(CopyLatestSnapshot(&g_copy) &&
            g_copy.radiation_resistance == 17.0f &&
            g_copy.misc_stats[kMiscStatCount - 1] ==
                static_cast<int32_t>((kMiscStatCount - 1) * 3),
        "new Pip-Boy fields copied exactly");

  g_snapshot.sequence = 4;
  g_snapshot.misc_stats[7] = -1;
  Check(!PublishSnapshot(g_snapshot),
        "negative native counter rejected when capability is published");
  g_snapshot.misc_stats[7] = 21;

  g_snapshot.sequence = 5;
  g_snapshot.item_count = kMaxItems + 1;
  Check(!PublishSnapshot(g_snapshot), "oversized item collection rejected");
  g_snapshot.item_count = 0;

  g_snapshot.sequence = 8;
  g_snapshot.note_count = 1;
  g_snapshot.notes[0].form_id = 0xBEEF;
  g_snapshot.notes[0].kind = 1;
  g_snapshot.notes[0].read = 0;
  std::snprintf(g_snapshot.notes[0].title,
                sizeof(g_snapshot.notes[0].title), "A Birthday Poem");
  std::snprintf(g_snapshot.notes[0].text,
                sizeof(g_snapshot.notes[0].text), "Transcript");
  Check(PublishSnapshot(g_snapshot),
        "typed acquired-note state is accepted");
  g_snapshot.sequence = 9;
  g_snapshot.notes[0].kind = 4;
  Check(!PublishSnapshot(g_snapshot),
        "unknown note payload kind is rejected");
  g_snapshot.notes[0].kind = 1;
  g_snapshot.note_count = 0;

  g_snapshot.sequence = 10;
  g_snapshot.capabilities |= kCapMap | kCapWorldMap;
  g_snapshot.map_min_x = -16384.0f;
  g_snapshot.map_max_y = 20480.0f;
  g_snapshot.map_max_x = 24576.0f;
  g_snapshot.map_min_y = -12288.0f;
  g_snapshot.map_usable_width = 2048;
  g_snapshot.map_usable_height = 2048;
  std::snprintf(g_snapshot.map_texture_path,
                sizeof(g_snapshot.map_texture_path),
                "textures\\interface\\worldmap\\wasteland_2048_no_map.dds");
  Check(PublishSnapshot(g_snapshot),
        "resolved world-map texture, bounds, and usable dimensions accepted");

  g_snapshot.sequence = 11;
  g_snapshot.map_max_x = g_snapshot.map_min_x;
  Check(!PublishSnapshot(g_snapshot),
        "degenerate native world-map bounds are rejected");
  g_snapshot.map_max_x = 24576.0f;
  g_snapshot.capabilities &= ~(kCapWorldMap | kCapMap);
  g_snapshot.map_usable_width = 0;
  g_snapshot.map_usable_height = 0;
  g_snapshot.map_texture_path[0] = '\0';

  g_snapshot.sequence = 12;
  g_snapshot.quest_count = 1;
  g_snapshot.objective_count = 1;
  g_snapshot.quests[0].objective_begin = 1;
  g_snapshot.quests[0].objective_count = 1;
  Check(!PublishSnapshot(g_snapshot),
        "quest objective range outside snapshot is rejected");
  g_snapshot.quest_count = 0;
  g_snapshot.objective_count = 0;

  g_snapshot.sequence = 13;
  g_snapshot.item_count = 1;
  g_snapshot.items[0].category = ItemCategory::kWeapon;
  std::memset(g_snapshot.items[0].name, 'X',
              sizeof(g_snapshot.items[0].name));
  Check(!PublishSnapshot(g_snapshot),
        "unterminated active item text is rejected");
  g_snapshot.item_count = 0;

  Command command = {};
  command.type = CommandType::kEquipItem;
  command.world_epoch = g_copy.world_epoch;
  command.form_id = 0x1234;
  uint64_t assigned_request_id = 0;
  Check(QueueCommand(command, &assigned_request_id),
        "valid action enters bounded queue");
  Check(assigned_request_id != 0,
        "queue reports the exact assigned request id to the caller");
  Check(PendingCommandCount() == 1, "command depth increments");
  Command result = {};
  Check(PollCommand(&result), "command can be consumed");
  Check(result.form_id == command.form_id &&
            result.request_id == assigned_request_id,
        "command identity and generated request id preserved");
  Check(!PollCommand(&result), "empty queue remains empty");

  command = {};
  command.type = CommandType::kRadioOff;
  command.world_epoch = g_copy.world_epoch;
  Check(QueueCommand(command),
        "radio-off action does not require an unrelated station FormID");
  Check(PollCommand(&result) && result.type == CommandType::kRadioOff,
        "formless radio-off action can be consumed");

  command = {};
  command.type = CommandType::kDropItem;
  command.world_epoch = g_copy.world_epoch;
  command.form_id = 0x1234;
  Check(!QueueCommand(command), "unimplemented drop action is rejected");
  command.type = CommandType::kFastTravel;
  Check(!QueueCommand(command), "unimplemented fast-travel action is rejected");
  command.type = CommandType::kEquipItem;
  command.count = 2;
  Check(!QueueCommand(command),
        "unsupported aggregate multi-count action is rejected");
  command.count = 1;
  command.stack_condition = 50;
  Check(!QueueCommand(command),
        "unsupported condition-stack selector is rejected");

  command = {};
  command.type = CommandType::kEquipItem;
  command.form_id = 0x1234;
  command.world_epoch = g_copy.world_epoch;
  Check(QueueCommand(command), "stale-rejection setup command queued");
  RejectStaleCommands(g_copy.world_epoch + 1);
  Check(PendingCommandCount() == 0, "old-world command rejected");

  const BuildIdentity identity = IdentifyExecutable(GetModuleHandleW(nullptr));
  Check(identity.valid_pe && identity.is_i386, "PE32 test host identified");
  Check(identity.text.begin && identity.memory_text_hash,
        "readable executable text section fingerprinted");
  Check(identity.rdata.begin && identity.data.begin,
        "read-only and writable data ranges identified");
  Check(identity.file_size != 0 && identity.file_sha256[0],
        "on-disk executable receives stable SHA-256 identity");
  Check(!identity.known_candidate,
        "unrelated PE32 host is not a Fallout candidate");

  if (g_failures) {
    std::fprintf(stderr, "%d test(s) failed\n", g_failures);
    return 1;
  }
  std::puts("StateStore/Version tests passed");
  return 0;
}
