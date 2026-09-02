#include <cassert>
#include <cstring>

#include "Snapshot.h"
#include "StateStore.h"

int main() {
  using namespace wfcds;
  ResetState(Lifecycle::kWaitingForWorld, "test", true);
  Snapshot snapshot;
  assert(CopyLatestSnapshot(&snapshot));
  assert(snapshot.lifecycle == Lifecycle::kWaitingForWorld);
  assert(snapshot.supported_build == 1);
  assert(std::strcmp(snapshot.build_label, "test") == 0);

  snapshot.sequence++;
  snapshot.lifecycle = Lifecycle::kLive;
  snapshot.size = sizeof(Snapshot);
  snapshot.health = 10.0f;
  snapshot.max_health = 20.0f;
  snapshot.resource = 5.0f;
  snapshot.max_resource = 10.0f;
  assert(PublishSnapshot(snapshot));
  assert(!PublishSnapshot(snapshot));

  Command command;
  command.type = CommandType::kReload;
  command.world_epoch = snapshot.world_epoch;
  command.queued_at_millis = 100;
  command.expected_controller_object_index = 11;
  command.expected_player_pawn_object_index = 12;
  command.expected_target_object_index = 13;
  uint64_t request = 0;
  assert(QueueCommand(command, &request));
  assert(request != 0);
  assert(!QueueCommand(command));
  assert(PendingCommandCount() == 1);
  Command found;
  assert(PollCommand(&found));
  assert(found.request_id == request);
  assert(found.type == CommandType::kReload);

  command = {};
  command.type = CommandType::kSwitchSquadMember;
  command.world_epoch = snapshot.world_epoch;
  command.queued_at_millis = 200;
  command.expected_controller_object_index = 11;
  command.expected_player_pawn_object_index = 12;
  command.expected_target_object_index = 42;
  command.squad_slot = 1;
  assert(!QueueCommand(command));
  command.expected_pawn_object_index = 42;
  command.expected_identity_hash = 7;
  assert(QueueCommand(command));
  ResetState(Lifecycle::kLoading, "test", true);
  assert(PendingCommandCount() == 0);
  return 0;
}
