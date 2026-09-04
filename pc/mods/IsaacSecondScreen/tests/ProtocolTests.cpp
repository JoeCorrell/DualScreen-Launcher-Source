#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "Protocol.h"

namespace {

bool Apply(const std::string& packet, isaacds::LiveState* state,
           isaacds::PacketTopic* topic = nullptr) {
  return isaacds::ApplyStatePacket(packet.data(), packet.size(), state, topic);
}

}  // namespace

int main() {
  using isaacds::PacketTopic;

  std::string decoded;
  assert(isaacds::PercentDecode("Isaac%20%26%20Blue", &decoded));
  assert(decoded == "Isaac & Blue");
  assert(!isaacds::PercentDecode("bad%2", &decoded));
  assert(!isaacds::PercentDecode("bad\tfield", &decoded));

  isaacds::LiveState state;
  PacketTopic topic = PacketTopic::kNone;
  assert(Apply(
      "DSI1\tS\t1\t42\t1\t21\tIsaac%20B\t7\t2\t4\t6\t8\t4\t2\t1\t2\t1\t0\t0\t3.5\t4.2\t6.0\t1.1\t1.0\t-1.0",
      &state, &topic));
  assert(topic == PacketTopic::kStatus);
  assert(state.in_run);
  assert(state.character_id == 21);
  assert(state.character_name == "Isaac B");
  assert(state.consumables.coins == 7);
  assert(state.consumables.bombs == 2);
  assert(state.consumables.keys == 4);
  assert(state.health.black_heart_mask == 2);
  assert(std::fabs(state.stats.damage - 3.5) < 0.0001);

  // Isaac can reload Lua inside the same process; sequence one begins a fresh
  // authoritative producer stream instead of leaving the panel permanently
  // stuck on the prior stream.
  assert(Apply(
      "DSI1\tS\t1\t43\t1\t0\tIsaac\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0\t0",
      &state));
  assert(state.last_sequence == 1);
  assert(state.consumables.bombs == 0);

  // Even if the provider claims two descriptors, DisplayFlags=0 is filtered
  // from native state so the UI cannot reveal an undiscovered secret room.
  assert(Apply(
      "DSI1\tM\t2\t43\tBasement%20I\t1\t0\t0\t15\t1234\t2\t15,15,5,1,1,1,0;16,16,0,0,7,1,0",
      &state, &topic));
  assert(topic == PacketTopic::kMap);
  assert(state.map.received);
  assert(state.map.floor == "Basement I");
  assert(state.map.rooms.size() == 1);
  assert(state.map.rooms.front().grid == 15);
  assert(!Apply(
      "DSI1\tM\t2\t43\tBasement%20I\t1\t0\t0\t15\t1234\t1\t15,15,5,1,1,1,0",
      &state));

  assert(Apply(
      "DSI1\tI\t3\t44\t2\t1,1,The%20Sad%20Onion,Tears%20up,3;118,2,Brimstone,Blood%20laser%20barrage,4\t2\t1\t105,0,3,1\t2\t1\t10,0,1\t2\t2\tC,1,0,0,The%20Fool,CardFronts;P,5,12,1,Pretty%20Fly,",
      &state, &topic));
  assert(topic == PacketTopic::kInventory);
  assert(state.inventory.collectibles.size() == 2);
  assert(state.inventory.collectibles[1].count == 2);
  assert(state.inventory.collectibles[1].name == "Brimstone");
  assert(state.inventory.collectibles[1].description ==
         "Blood laser barrage");
  assert(state.inventory.collectibles[1].quality == 4);
  assert(state.inventory.actives.front().battery_charge == 1);
  assert(state.inventory.trinkets.front().golden);
  assert(state.inventory.pockets[1].name == "Pretty Fly");
  assert(state.inventory.active_capacity == 2);
  assert(state.inventory.trinket_capacity == 2);
  assert(state.inventory.pocket_capacity == 2);

  assert(Apply(
      "DSI1\tR\t4\t45\tABCD%20EFGH\tBasement%20I\t1\t0\t0\t3600\t0.25\t0.5\t0.125\t1\t3\t0,2,12",
      &state, &topic));
  assert(topic == PacketTopic::kRun);
  assert(state.run.seed == "ABCD EFGH");
  assert(state.run.timer_frames == 3600);
  assert(std::fabs(state.run.planetarium_chance - 0.125) < 0.0001);
  assert(state.run.transformations.size() == 3);

  isaacds::LiveState untouched = state;
  assert(!Apply("DSI1\tM\t5\t46\tFloor\t1\t0\t0\t15\t0\t1\tbad", &state));
  assert(state.last_sequence == untouched.last_sequence);
  assert(state.map.rooms.size() == untouched.map.rooms.size());

  const std::string oversized(65508, 'A');
  assert(!isaacds::ApplyStatePacket(oversized.data(), oversized.size(), &state));

  std::cout << "ProtocolTests passed\n";
  return 0;
}
