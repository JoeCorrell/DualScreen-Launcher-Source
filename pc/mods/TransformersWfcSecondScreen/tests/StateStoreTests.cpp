#include <cassert>
#include <cstring>

#include "Snapshot.h"
#include "StateStore.h"

int main() {
  using namespace wfcds;
  assert(PendingCommandCount() == 0);
  Command no_command;
  assert(!PollCommand(&no_command));
  assert(PendingCommandCount() == 0);
  assert(LatestSequence() == 0);
  assert(RequestedPanelPage() == PanelPage::kCombat);
  SetRequestedPanelPage(PanelPage::kSquad);
  assert(RequestedPanelPage() == PanelPage::kSquad);
  SetRequestedPanelPage(PanelPage::kObjectives);
  assert(RequestedPanelPage() == PanelPage::kObjectives);
  SetRequestedPanelPage(static_cast<PanelPage>(0xFF));
  assert(RequestedPanelPage() == PanelPage::kCombat);
  SetRequestedPanelPage(PanelPage::kCombat);
  ResetState(Lifecycle::kWaitingForWorld, "test", true);
  assert(LatestSequence() == 1);
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
  assert(LatestSequence() == snapshot.sequence);
  assert(!PublishSnapshot(snapshot));
  assert(LatestSequence() == snapshot.sequence);
  Snapshot same_content = snapshot;
  same_content.sequence++;
  assert(SnapshotContentEqual(snapshot, same_content));
  same_content.health++;
  assert(!SnapshotContentEqual(snapshot, same_content));

  Snapshot panel_change = snapshot;
  panel_change.sequence++;
  panel_change.squad[0].health = 3.0f;
  assert(SnapshotPanelContentEqual(snapshot, panel_change,
                                   PanelPage::kCombat));
  assert(SnapshotPanelContentEqual(snapshot, panel_change,
                                   PanelPage::kSquad));
  panel_change.abilities[0].cooldown_remaining = 1.5f;
  assert(!SnapshotPanelContentEqual(snapshot, panel_change,
                                    PanelPage::kCombat));
  assert(SnapshotCombatNonAbilityContentEqual(snapshot, panel_change));
  assert(SnapshotPanelContentEqual(snapshot, panel_change,
                                   PanelPage::kObjectives));
  panel_change.objective.distance_meters = 10.2f;
  Snapshot nearby_objective = panel_change;
  nearby_objective.objective.distance_meters = 10.4f;
  assert(SnapshotPanelContentEqual(panel_change, nearby_objective,
                                   PanelPage::kObjectives));
  nearby_objective.objective.distance_meters = 10.6f;
  assert(!SnapshotPanelContentEqual(panel_change, nearby_objective,
                                    PanelPage::kObjectives));
  assert(!SnapshotPanelContentEqual(panel_change, nearby_objective,
                                    PanelPage::kCombat));
  Snapshot squad_change = snapshot;
  squad_change.squad_count = 1;
  squad_change.squad[0].occupied = 1;
  Snapshot hurt_squad_member = squad_change;
  hurt_squad_member.squad[0].health = 3.0f;
  assert(!SnapshotPanelContentEqual(squad_change, hurt_squad_member,
                                    PanelPage::kSquad));
  assert(SnapshotPanelContentEqual(squad_change, hurt_squad_member,
                                   PanelPage::kCombat));
  SetSnapshotNotificationTarget(nullptr, 0);

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
  Command mixed = command;
  mixed.type = CommandType::kTransform;
  mixed.expected_target_object_index = mixed.expected_player_pawn_object_index;
  assert(!QueueCommand(mixed));
  assert(PendingCommandCount() == 1);
  Command found;
  assert(PollCommand(&found));
  assert(found.request_id == request);
  assert(found.type == CommandType::kReload);
  assert(PendingCommandCount() == 0);

  assert(QueueCommand(command));
  assert(PendingCommandCount() == 1);
  SetCommandAcceptance(false);
  assert(!QueueCommand(command));
  assert(PendingCommandCount() == 0);
  SetCommandAcceptance(true);
  assert(QueueCommand(command));
  assert(PendingCommandCount() == 1);
  DiscardAllCommands();
  assert(PendingCommandCount() == 0);

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
  assert(PendingCommandCount() == 1);
  RejectStaleCommands(snapshot.world_epoch);
  assert(PendingCommandCount() == 1);
  ResetState(Lifecycle::kLoading, "test", true);
  assert(PendingCommandCount() == 0);
  return 0;
}
