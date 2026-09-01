#include <winsock2.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "PipboyProtocol.h"

namespace {

template <typename T>
void Put(std::vector<uint8_t>* out, T value) {
  const size_t old = out->size();
  out->resize(old + sizeof(value));
  std::memcpy(out->data() + old, &value, sizeof(value));
}

void PutString(std::vector<uint8_t>* out, const std::string& value) {
  out->insert(out->end(), value.begin(), value.end());
  out->push_back(0);
}

void PrimitiveBoolean(std::vector<uint8_t>* out, uint32_t id, bool value) {
  Put<uint8_t>(out, 0);
  Put<uint32_t>(out, id);
  Put<uint8_t>(out, value ? 1 : 0);
}

void PrimitiveInt8(std::vector<uint8_t>* out, uint32_t id, int8_t value) {
  Put<uint8_t>(out, 1);
  Put<uint32_t>(out, id);
  Put<int8_t>(out, value);
}

void PrimitiveUInt8(std::vector<uint8_t>* out, uint32_t id, uint8_t value) {
  Put<uint8_t>(out, 2);
  Put<uint32_t>(out, id);
  Put<uint8_t>(out, value);
}

void PrimitiveInt(std::vector<uint8_t>* out, uint32_t id, int32_t value) {
  Put<uint8_t>(out, 3);
  Put<uint32_t>(out, id);
  Put<int32_t>(out, value);
}

void PrimitiveUInt(std::vector<uint8_t>* out, uint32_t id, uint32_t value) {
  Put<uint8_t>(out, 4);
  Put<uint32_t>(out, id);
  Put<uint32_t>(out, value);
}

void PrimitiveFloat(std::vector<uint8_t>* out, uint32_t id, float value) {
  Put<uint8_t>(out, 5);
  Put<uint32_t>(out, id);
  Put<float>(out, value);
}

void PrimitiveString(std::vector<uint8_t>* out, uint32_t id,
                     const std::string& value) {
  Put<uint8_t>(out, 6);
  Put<uint32_t>(out, id);
  PutString(out, value);
}

void Array(std::vector<uint8_t>* out, uint32_t id,
           const std::vector<uint32_t>& children) {
  Put<uint8_t>(out, 7);
  Put<uint32_t>(out, id);
  Put<uint16_t>(out, static_cast<uint16_t>(children.size()));
  for (uint32_t child : children) Put<uint32_t>(out, child);
}

void Object(std::vector<uint8_t>* out, uint32_t id,
            const std::vector<std::pair<std::string, uint32_t>>& fields,
            const std::vector<uint32_t>& removed = {}) {
  Put<uint8_t>(out, 8);
  Put<uint32_t>(out, id);
  Put<uint16_t>(out, static_cast<uint16_t>(fields.size()));
  for (const auto& field : fields) {
    Put<uint32_t>(out, field.second);
    PutString(out, field.first);
  }
  Put<uint16_t>(out, static_cast<uint16_t>(removed.size()));
  for (uint32_t child : removed) Put<uint32_t>(out, child);
}

std::vector<uint8_t> LocalMap(
    uint32_t width, uint32_t height, const std::array<float, 6>& bounds,
    const std::vector<uint8_t>& pixels) {
  std::vector<uint8_t> out;
  Put<uint32_t>(&out, width);
  Put<uint32_t>(&out, height);
  for (float bound : bounds) Put<float>(&out, bound);
  out.insert(out.end(), pixels.begin(), pixels.end());
  return out;
}

void AssertOnlyDomainChanged(const std::array<uint64_t, 5>& before,
                             const std::array<uint64_t, 5>& after,
                             size_t expected) {
  for (size_t i = 0; i < before.size(); ++i)
    assert(after[i] == before[i] + (i == expected ? 1u : 0u));
}

void AssertRejected(fo4ds::PipboyDatabase* database,
                    const std::vector<uint8_t>& update) {
  const auto before = database->Capture();
  const size_t node_count = database->NodeCount();
  std::string error;
  assert(!database->ApplyDataUpdate(update.data(), update.size(), &error));
  assert(!error.empty());
  const auto after = database->Capture();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(after.player_name == before.player_name);
  assert(std::fabs(after.hp - before.hp) < 0.001f);
  assert(database->NodeCount() == node_count);
}

void BuildAllDomains(fo4ds::PipboyDatabase* database) {
  std::vector<uint8_t> update;
  PrimitiveString(&update, 1, "Sole Survivor");
  PrimitiveFloat(&update, 2, 75.0f);
  PrimitiveFloat(&update, 3, 100.0f);
  PrimitiveUInt(&update, 4, 42);
  Object(&update, 5,
         {{"PlayerName", 1}, {"CurrHP", 2}, {"MaxHP", 3}, {"Caps", 4}});
  Array(&update, 6, {});
  Object(&update, 7, {{"ActiveEffects", 6}});
  Array(&update, 8, {});
  Array(&update, 9, {});
  PrimitiveUInt(&update, 10, 7);
  Object(&update, 11, {{"Version", 10}});
  Array(&update, 12, {});
  Object(&update, 13, {});
  Array(&update, 20, {});
  PrimitiveString(&update, 30, "Commonwealth");
  Object(&update, 31, {{"CurrCell", 30}});
  PrimitiveString(&update, 40, "Diamond City Radio");
  PrimitiveBoolean(&update, 41, true);
  PrimitiveFloat(&update, 42, 98.0f);
  Object(&update, 43, {{"text", 40}, {"active", 41}, {"frequency", 42}});
  Array(&update, 44, {43});
  Object(&update, 0,
         {{"PlayerInfo", 5}, {"Stats", 7}, {"Special", 8},
          {"Perks", 9}, {"Inventory", 11}, {"Log", 12},
          {"Status", 13}, {"Quests", 20}, {"Map", 31},
          {"Radio", 44}});
  std::string error;
  assert(database->ApplyDataUpdate(update.data(), update.size(), &error));
}

void TestSnapshotsAndDomainCaching() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, R"({"version":"1.11.240.0"})",
                         "test connection");
  BuildAllDomains(&database);
  auto snapshot = database.Capture();
  assert(snapshot.connected && snapshot.root_ready && snapshot.ActionsSafe());
  assert(snapshot.stats_ready && snapshot.effects_ready &&
         snapshot.perks_ready && snapshot.general_stats_ready);
  assert(snapshot.inventory_ready && snapshot.data_ready &&
         snapshot.map_ready && snapshot.radio_ready &&
         snapshot.status_ready);
  assert(snapshot.player_name == "Sole Survivor");
  assert(std::fabs(snapshot.hp - 75.0f) < 0.001f);
  assert(std::fabs(snapshot.max_hp - 100.0f) < 0.001f);
  assert(snapshot.caps == 42 && snapshot.inventory_version == 7);
  assert(snapshot.quests.empty());
  assert(snapshot.current_cell == "Commonwealth");
  assert(snapshot.radio.size() == 1);
  assert(snapshot.radio[0].name == "Diamond City Radio");

  auto before = snapshot.domain_revisions;
  std::vector<uint8_t> stats;
  PrimitiveFloat(&stats, 2, 48.5f);
  std::string error;
  assert(database.ApplyDataUpdate(stats.data(), stats.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 0);
  assert(std::fabs(snapshot.hp - 48.5f) < 0.001f);

  before = snapshot.domain_revisions;
  std::vector<uint8_t> inventory;
  PrimitiveUInt(&inventory, 10, 8);
  assert(database.ApplyDataUpdate(inventory.data(), inventory.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.inventory_version == 8);

  before = snapshot.domain_revisions;
  std::vector<uint8_t> data;
  Array(&data, 20, {});
  assert(database.ApplyDataUpdate(data.data(), data.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 2);

  before = snapshot.domain_revisions;
  std::vector<uint8_t> map;
  PrimitiveString(&map, 30, "Sanctuary Hills");
  assert(database.ApplyDataUpdate(map.data(), map.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 3);
  assert(snapshot.current_cell == "Sanctuary Hills");

  before = snapshot.domain_revisions;
  std::vector<uint8_t> radio;
  PrimitiveFloat(&radio, 42, 99.0f);
  assert(database.ApplyDataUpdate(radio.data(), radio.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 4);
  assert(std::fabs(snapshot.radio[0].frequency - 99.0f) < 0.001f);

  auto cached = snapshot;
  assert(!database.CaptureIfChanged(cached.revision, &cached));
  assert(!database.CaptureIfChanged(cached.revision, nullptr));
  const auto cached_domains = cached.domain_revisions;
  database.SetConnection(true, true, {}, "busy");
  assert(database.CaptureIfChanged(cached.revision, &cached));
  assert(cached.busy && !cached.ActionsSafe());
  assert(cached.player_name == "Sole Survivor");
  assert(cached.domain_revisions == cached_domains);
  const uint64_t unchanged_revision = cached.revision;
  database.SetConnection(true, true, {}, "busy");
  assert(!database.CaptureIfChanged(unchanged_revision, &cached));
}

void TestFallout4StatsSchemaAndReadiness() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "stats-schema", "connected");
  std::vector<uint8_t> update;

  PrimitiveString(&update, 100, "Sole Survivor");
  PrimitiveFloat(&update, 101, 88.0f);
  PrimitiveFloat(&update, 102, 120.0f);
  PrimitiveUInt(&update, 103, 3);
  PrimitiveInt(&update, 110, 1);
  PrimitiveFloat(&update, 111, 24.5f);
  Object(&update, 112, {{"type", 110}, {"Value", 111}});
  PrimitiveInt(&update, 113, 4);
  PrimitiveFloat(&update, 114, 17.0f);
  Object(&update, 115, {{"type", 113}, {"Value", 114}});
  Array(&update, 116, {112, 115});
  PrimitiveFloat(&update, 117, 35.0f);
  Object(&update, 118, {{"type", 110}, {"Value", 117}});
  PrimitiveInt(&update, 120, 6);
  PrimitiveFloat(&update, 121, 50.0f);
  Object(&update, 122, {{"type", 120}, {"Value", 121}});
  Array(&update, 123, {118, 122});
  Object(&update, 124,
         {{"PlayerName", 100}, {"CurrHP", 101}, {"MaxHP", 102},
          {"PerkPoints", 103}, {"TotalDamages", 116},
          {"TotalResists", 123}});

  PrimitiveString(&update, 131, "Buffout");
  PrimitiveInt(&update, 132, 2);
  PrimitiveString(&update, 133, "Strength");
  PrimitiveFloat(&update, 134, -2.0f);
  PrimitiveFloat(&update, 135, 119.5f);
  PrimitiveBoolean(&update, 136, true);
  PrimitiveBoolean(&update, 137, false);
  Object(&update, 138,
         {{"Name", 133}, {"Value", 134}, {"duration", 135},
          {"showAsPercent", 136}, {"CustomDesc", 137}});
  Array(&update, 139, {138});
  Object(&update, 140,
         {{"Source", 131}, {"type", 132}, {"Effects", 139}});
  Array(&update, 141, {140});
  PrimitiveUInt(&update, 142, 9);
  PrimitiveUInt(&update, 143, 4);
  PrimitiveUInt(&update, 144, 0x1234);
  PrimitiveUInt(&update, 145, 0x5678);
  PrimitiveFloat(&update, 146, 75.0f);
  Object(&update, 147,
         {{"ActiveEffects", 141}, {"StimpakCount", 142},
          {"RadawayCount", 143}, {"BodyFlags", 144},
          {"HeadFlags", 145}, {"HeadCondition", 146},
          {"TorsoCondition", 146}, {"LArmCondition", 146},
          {"RArmCondition", 146}, {"LLegCondition", 146},
          {"RLegCondition", 146}});

  PrimitiveString(&update, 150, "Strength");
  PrimitiveInt(&update, 151, 4);
  PrimitiveInt(&update, 152, 1);
  PrimitiveString(&update, 155, "Raw physical power");
  Object(&update, 153,
         {{"text", 150}, {"value", 151}, {"modifier", 152},
          {"description", 155}});
  Array(&update, 154, {153});

  PrimitiveString(&update, 160, "Bloody Mess");
  PrimitiveUInt(&update, 161, 2);
  PrimitiveUInt(&update, 162, 4);
  PrimitiveBoolean(&update, 163, true);
  PrimitiveUInt(&update, 174, 0x4D8A7);
  PrimitiveString(&update, 175,
                  "Components/VaultBoys/Perks/PerkClip_4d8a7.swf");
  PrimitiveString(&update, 164, "Rank one");
  PrimitiveString(&update, 165, "Rank two");
  Object(&update, 166, {{"Description", 164}});
  Object(&update, 167, {{"Description", 165}});
  Array(&update, 168, {164, 165});
  Object(&update, 169,
         {{"text", 160}, {"rank", 161}, {"maxRank", 162},
          {"ListVisible", 163}, {"descriptions", 168}, {"Clip", 174},
          {"SWFFile", 175}});
  PrimitiveString(&update, 171, "Unacquired Perk");
  PrimitiveUInt(&update, 172, 0);
  Object(&update, 173,
         {{"Name", 171}, {"Rank", 172}, {"MaxRank", 162},
          {"ListVisible", 163}, {"Perks", 168}});
  Array(&update, 170, {169, 173});

  PrimitiveString(&update, 180, "$Combat");
  PrimitiveString(&update, 181, "Critical Strikes");
  PrimitiveUInt(&update, 182, 12);
  PrimitiveBoolean(&update, 183, true);
  Object(&update, 184,
         {{"text", 181}, {"Value", 182}, {"showIfZero", 183}});
  PrimitiveString(&update, 185, "Hidden zero");
  PrimitiveUInt(&update, 186, 0);
  PrimitiveBoolean(&update, 187, false);
  Object(&update, 188,
         {{"text", 185}, {"Value", 186}, {"showIfZero", 187}});
  Array(&update, 189, {184, 188});
  Object(&update, 190, {{"text", 180}, {"statArray", 189}});
  Array(&update, 191, {190});
  Object(&update, 192, {});
  Object(&update, 0,
         {{"PlayerInfo", 124}, {"Stats", 147}, {"Special", 154},
          {"Perks", 170}, {"Log", 191}, {"Status", 192}});

  std::string error;
  assert(database.ApplyDataUpdate(update.data(), update.size(), &error));
  const auto snapshot = database.Capture();
  assert(snapshot.root_ready && snapshot.stats_ready &&
         snapshot.effects_ready && snapshot.perks_ready &&
         snapshot.general_stats_ready && snapshot.status_ready);
  assert(!snapshot.inventory_ready && !snapshot.data_ready &&
         !snapshot.map_ready && !snapshot.radio_ready);
  assert(snapshot.ActionsSafe());
  assert(snapshot.perk_points == 3 && snapshot.stimpaks == 9 &&
         snapshot.radaway == 4);
  assert(snapshot.body_flags == 0x1234 && snapshot.head_flags == 0x5678);
  assert(snapshot.special[0] == 4 && snapshot.special_modifiers[0] == 1);
  assert(snapshot.special_names[0] == "Strength");
  assert(snapshot.special_descriptions[0] == "Raw physical power");
  assert(std::fabs(snapshot.total_damage[0] - 24.5f) < 0.001f);
  assert(std::fabs(snapshot.total_damage[3] - 17.0f) < 0.001f);
  assert(std::fabs(snapshot.total_resistance[0] - 35.0f) < 0.001f);
  assert(std::fabs(snapshot.total_resistance[5] - 50.0f) < 0.001f);
  assert(snapshot.effects.size() == 1);
  assert(snapshot.effects[0].source == "Buffout");
  assert(snapshot.effects[0].name == "Strength");
  assert(snapshot.effects[0].show_as_percent &&
         snapshot.effects[0].detrimental);
  assert(std::fabs(snapshot.effects[0].magnitude + 2.0f) < 0.001f);
  assert(std::fabs(snapshot.effects[0].duration - 119.5f) < 0.001f);
  assert(snapshot.perks.size() == 1);
  assert(snapshot.perks[0].name == "Bloody Mess" &&
         snapshot.perks[0].rank == 2 && snapshot.perks[0].max_rank == 4);
  assert(snapshot.perks[0].description == "Rank two");
  assert(snapshot.perks[0].clip_name == 0x4D8A7);
  assert(snapshot.perks[0].swf_file ==
         "Components/VaultBoys/Perks/PerkClip_4d8a7.swf");
  assert(snapshot.general_stats.size() == 2);
  assert(snapshot.general_stats[0].category == "$Combat");
  assert(snapshot.general_stats[0].name == "Critical Strikes");
  assert(std::fabs(snapshot.general_stats[0].value - 12.0f) < 0.001f);
  assert(!snapshot.general_stats[1].show_if_zero);
}

void TestQuestAndMapFieldCoverage() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "data-coverage", "connected");
  std::vector<uint8_t> update;

  PrimitiveUInt(&update, 101, 0x1234);
  PrimitiveInt(&update, 102, 7);
  PrimitiveInt(&update, 103, 2);
  PrimitiveBoolean(&update, 104, true);
  PrimitiveBoolean(&update, 105, true);
  PrimitiveString(&update, 106, "Main Quest");
  PrimitiveUInt(&update, 111, 0x5678);
  PrimitiveInt(&update, 112, 9);
  PrimitiveBoolean(&update, 114, true);
  PrimitiveString(&update, 115, "Reach the marker");
  Object(&update, 116,
         {{"formID", 111}, {"instance", 112}, {"enabled", 114},
          {"text", 115}});
  Array(&update, 117, {116});
  Object(&update, 118,
         {{"formID", 101}, {"instance", 102}, {"type", 103},
          {"active", 104}, {"enabled", 105}, {"text", 106},
          {"objectives", 117}});
  Array(&update, 119, {118});

  PrimitiveBoolean(&update, 201, true);
  PrimitiveBoolean(&update, 202, true);
  PrimitiveBoolean(&update, 203, true);
  PrimitiveInt(&update, 204, 17);
  PrimitiveFloat(&update, 205, 100.0f);
  PrimitiveFloat(&update, 206, 200.0f);
  PrimitiveFloat(&update, 207, 30.0f);
  PrimitiveString(&update, 208, "Sanctuary");
  Object(&update, 209,
         {{"Visible", 201}, {"Discovered", 202}, {"ClearedStatus", 203},
          {"type", 204}, {"X", 205}, {"Y", 206}, {"Height", 207},
          {"Name", 208}});
  Array(&update, 210, {209});
  PrimitiveBoolean(&update, 211, true);
  PrimitiveBoolean(&update, 212, true);
  PrimitiveFloat(&update, 213, 400.0f);
  PrimitiveFloat(&update, 214, 500.0f);
  PrimitiveString(&update, 215, "Quest Target");
  Object(&update, 216,
         {{"OnDoor", 212}, {"X", 213}, {"Y", 214}, {"Name", 215}});
  Array(&update, 217, {216});
  Object(&update, 218, {{"X", 205}, {"Y", 206}});
  Object(&update, 219,
         {{"Locations", 210}, {"Quests", 217}, {"Custom", 218}});
  Object(&update, 220,
         {{"X", 213}, {"Y", 214}, {"Name", 215}});
  Array(&update, 221, {220});
  Object(&update, 222, {{"Doors", 221}});
  PrimitiveString(&update, 223, "Commonwealth");
  Object(&update, 224,
         {{"CurrWorldspace", 223}, {"World", 219}, {"Local", 222}});
  Object(&update, 0, {{"Quests", 119}, {"Map", 224}});

  std::string error;
  assert(database.ApplyDataUpdate(update.data(), update.size(), &error));
  const auto snapshot = database.Capture();
  assert(snapshot.quests.size() == 1);
  assert(snapshot.quests[0].objectives.size() == 1);
  const auto& objective = snapshot.quests[0].objectives[0];
  assert(objective.node_id == 116 && objective.form_id == 0x5678);
  assert(objective.instance == 9 && objective.active && objective.enabled);
  assert(snapshot.markers.size() == 4);
  assert(snapshot.markers[0].kind == fo4ds::MapMarkerState::Kind::kLocation);
  assert(snapshot.markers[0].cleared && snapshot.markers[0].type == 17);
  assert(std::fabs(snapshot.markers[0].height - 30.0f) < 0.001f);
  assert(snapshot.markers[1].kind == fo4ds::MapMarkerState::Kind::kQuest);
  assert(!snapshot.markers[1].local && snapshot.markers[1].on_door);
  assert(snapshot.markers[1].visible && !snapshot.markers[1].discovered);
  assert(snapshot.markers[2].kind == fo4ds::MapMarkerState::Kind::kCustom);
  assert(snapshot.markers[2].visible && !snapshot.markers[2].discovered);
  assert(snapshot.markers[3].kind == fo4ds::MapMarkerState::Kind::kDoor);
  assert(snapshot.markers[3].local && snapshot.markers[3].visible);
  assert(!snapshot.markers[3].discovered);
}

void TestEmptyUpdateAndRootReadiness() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "root-regression", "connected");
  const auto before = database.Capture();
  const size_t nodes_before = database.NodeCount();
  std::string error;
  assert(database.ApplyDataUpdate(nullptr, 0, &error));
  auto after = database.Capture();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(database.NodeCount() == nodes_before);

  const std::vector<uint8_t> empty;
  assert(database.ApplyDataUpdate(empty.data(), empty.size(), &error));
  after = database.Capture();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(database.NodeCount() == nodes_before);

  std::vector<uint8_t> primitive_root;
  PrimitiveUInt(&primitive_root, 0, 1234);
  assert(database.ApplyDataUpdate(primitive_root.data(), primitive_root.size(),
                                  &error));
  after = database.Capture();
  assert(!after.root_ready);
  assert(!after.ActionsSafe());
  assert(database.NodeCount() == 1);
}

void AddInventoryItem(std::vector<uint8_t>* update, uint32_t base,
                      const std::string& name, uint32_t handle, int32_t count) {
  PrimitiveString(update, base, name);
  PrimitiveUInt(update, base + 1, handle);
  PrimitiveInt(update, base + 2, count);
  Object(update, base + 3,
         {{"text", base}, {"HandleID", base + 1}, {"count", base + 2}});
}

void TestObjectAndArrayMutation() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "inventory", "connected");
  std::vector<uint8_t> initial;
  AddInventoryItem(&initial, 100, "10mm Pistol", 5001, 2);
  Array(&initial, 110, {103});
  PrimitiveUInt(&initial, 111, 1);
  Object(&initial, 112, {{"Weapons", 110}, {"Version", 111}});
  Object(&initial, 0, {{"Inventory", 112}});
  std::string error;
  assert(database.ApplyDataUpdate(initial.data(), initial.size(), &error));
  auto snapshot = database.Capture();
  assert(snapshot.items.size() == 1);
  assert(snapshot.items[0].name == "10mm Pistol" && snapshot.items[0].count == 2);

  std::vector<uint8_t> replacement_name;
  PrimitiveString(&replacement_name, 120, "Hardened 10mm Pistol");
  assert(database.ApplyDataUpdate(replacement_name.data(), replacement_name.size(),
                                  &error));
  auto before = database.Capture().domain_revisions;
  std::vector<uint8_t> replace_field;
  Object(&replace_field, 103, {{"text", 120}});
  assert(database.ApplyDataUpdate(replace_field.data(), replace_field.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items[0].name == "Hardened 10mm Pistol");

  before = snapshot.domain_revisions;
  std::vector<uint8_t> remove_field;
  Object(&remove_field, 103, {}, {120});
  assert(database.ApplyDataUpdate(remove_field.data(), remove_field.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items[0].name.empty());

  std::vector<uint8_t> restore_field;
  Object(&restore_field, 103, {{"text", 100}});
  assert(database.ApplyDataUpdate(restore_field.data(), restore_field.size(), &error));
  assert(database.Capture().items[0].name == "10mm Pistol");

  std::vector<uint8_t> second_item;
  AddInventoryItem(&second_item, 130, "Laser Musket", 5002, 1);
  assert(database.ApplyDataUpdate(second_item.data(), second_item.size(), &error));
  before = database.Capture().domain_revisions;
  std::vector<uint8_t> replace_array;
  Array(&replace_array, 110, {133, 133});
  assert(database.ApplyDataUpdate(replace_array.data(), replace_array.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items.size() == 1 && snapshot.items[0].name == "Laser Musket");

  before = snapshot.domain_revisions;
  std::vector<uint8_t> clear_array;
  Array(&clear_array, 110, {});
  assert(database.ApplyDataUpdate(clear_array.data(), clear_array.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items.empty());

  std::vector<uint8_t> restore_array;
  Array(&restore_array, 110, {133});
  assert(database.ApplyDataUpdate(restore_array.data(), restore_array.size(), &error));
  assert(database.Capture().items.size() == 1);

  before = database.Capture().domain_revisions;
  std::vector<uint8_t> replace_object_with_scalar;
  PrimitiveUInt(&replace_object_with_scalar, 133, 9);
  assert(database.ApplyDataUpdate(replace_object_with_scalar.data(),
                                  replace_object_with_scalar.size(), &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items.empty());

  before = snapshot.domain_revisions;
  std::vector<uint8_t> restore_object;
  Object(&restore_object, 133,
         {{"text", 130}, {"HandleID", 131}, {"count", 132}});
  assert(database.ApplyDataUpdate(restore_object.data(), restore_object.size(),
                                  &error));
  snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.items.size() == 1 && snapshot.items[0].name == "Laser Musket");

  std::vector<uint8_t> dangling_array;
  Array(&dangling_array, 110, {999999});
  AssertRejected(&database, dangling_array);
}

void TestSameFrameInventoryAdditionDomain() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "inventory-domain", "connected");
  std::vector<uint8_t> initial;
  Array(&initial, 200, {});
  PrimitiveUInt(&initial, 201, 1);
  Object(&initial, 202, {{"Weapons", 200}, {"Version", 201}});
  Object(&initial, 0, {{"Inventory", 202}});
  std::string error;
  assert(database.ApplyDataUpdate(initial.data(), initial.size(), &error));
  const auto before = database.Capture().domain_revisions;

  std::vector<uint8_t> addition;
  AddInventoryItem(&addition, 210, "Pipe Pistol", 6001, 3);
  Array(&addition, 200, {213});
  PrimitiveUInt(&addition, 201, 2);
  assert(database.ApplyDataUpdate(addition.data(), addition.size(), &error));
  const auto snapshot = database.Capture();
  AssertOnlyDomainChanged(before, snapshot.domain_revisions, 1);
  assert(snapshot.inventory_version == 2);
  assert(snapshot.items.size() == 1);
  assert(snapshot.items[0].name == "Pipe Pistol");
  assert(snapshot.items[0].count == 3);
}

void AddInventoryItemWithCard(std::vector<uint8_t>* update, uint32_t base,
                              const std::string& name, uint32_t handle,
                              int32_t count, float damage) {
  PrimitiveString(update, base, name);
  PrimitiveUInt(update, base + 1, handle);
  PrimitiveInt(update, base + 2, count);
  PrimitiveString(update, base + 3, "Damage");
  PrimitiveFloat(update, base + 4, damage);
  PrimitiveInt(update, base + 5, 0);
  PrimitiveInt(update, base + 6, 0);
  PrimitiveFloat(update, base + 10, 15.0f);
  PrimitiveBoolean(update, base + 11, true);
  PrimitiveBoolean(update, base + 12, true);
  Object(update, base + 7,
         {{"text", base + 3}, {"Value", base + 4},
          {"ValueType", base + 5}, {"damageType", base + 6},
          {"duration", base + 10}, {"showAsPercent", base + 11},
          {"scaleWithDuration", base + 12}});
  Array(update, base + 8, {base + 7});
  Object(update, base + 9,
         {{"text", base}, {"HandleID", base + 1}, {"count", base + 2},
          {"itemCardInfoList", base + 8}});
}

const fo4ds::InventoryItemState* FindItem(const fo4ds::PipboySnapshot& snapshot,
                                          uint32_t node_id) {
  for (const auto& item : snapshot.items) {
    if (item.node_id == node_id) return &item;
  }
  return nullptr;
}

void TestInventoryIncrementalCache() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "inventory-cache", "connected");
  std::vector<uint8_t> initial;
  AddInventoryItemWithCard(&initial, 300, "10mm Pistol", 7001, 2, 18.0f);
  AddInventoryItemWithCard(&initial, 320, "Laser Musket", 7002, 1, 30.0f);
  Array(&initial, 340, {309, 329});
  PrimitiveUInt(&initial, 341, 1);
  Object(&initial, 342, {{"Weapons", 340}, {"Version", 341}});
  Object(&initial, 0, {{"Inventory", 342}});
  std::string error;
  assert(database.ApplyDataUpdate(initial.data(), initial.size(), &error));

  auto snapshot = database.Capture();
  assert(snapshot.inventory_initialized);
  assert(snapshot.items.size() == 2);
  const uint64_t initial_full_rebuilds = snapshot.inventory_full_rebuilds;
  const uint64_t initial_incremental = snapshot.inventory_incremental_refreshes;
  assert(initial_full_rebuilds == 1);
  const auto* changed_before = FindItem(snapshot, 309);
  const auto* untouched_before = FindItem(snapshot, 329);
  assert(changed_before && untouched_before);
  assert(changed_before->card.size() == 1);
  assert(std::fabs(changed_before->card[0].number_value - 18.0f) < 0.001f);
  assert(std::fabs(changed_before->card[0].duration - 15.0f) < 0.001f);
  assert(changed_before->card[0].show_as_percent);
  assert(changed_before->card[0].scale_with_duration);
  const std::string untouched_name = untouched_before->name;
  const int32_t untouched_count = untouched_before->count;
  const float untouched_damage = untouched_before->card[0].number_value;

  std::vector<uint8_t> leaf_update;
  PrimitiveString(&leaf_update, 300, "Hardened 10mm Pistol");
  PrimitiveInt(&leaf_update, 302, 5);
  PrimitiveFloat(&leaf_update, 304, 22.5f);
  assert(database.ApplyDataUpdate(leaf_update.data(), leaf_update.size(), &error));
  assert(database.CaptureIfChanged(snapshot.revision, &snapshot));
  assert(snapshot.inventory_full_rebuilds == initial_full_rebuilds);
  assert(snapshot.inventory_incremental_refreshes == initial_incremental + 1);
  const auto* changed_after = FindItem(snapshot, 309);
  const auto* untouched_after = FindItem(snapshot, 329);
  assert(changed_after && untouched_after);
  assert(changed_after->name == "Hardened 10mm Pistol");
  assert(changed_after->count == 5);
  assert(changed_after->card.size() == 1);
  assert(std::fabs(changed_after->card[0].number_value - 22.5f) < 0.001f);
  assert(untouched_after->name == untouched_name);
  assert(untouched_after->count == untouched_count);
  assert(untouched_after->card.size() == 1);
  assert(std::fabs(untouched_after->card[0].number_value - untouched_damage) <
         0.001f);

  const uint64_t full_before_metadata = snapshot.inventory_full_rebuilds;
  const uint64_t incremental_before_metadata =
      snapshot.inventory_incremental_refreshes;
  std::vector<uint8_t> metadata_update;
  PrimitiveUInt(&metadata_update, 341, 2);
  assert(database.ApplyDataUpdate(metadata_update.data(), metadata_update.size(),
                                  &error));
  assert(database.CaptureIfChanged(snapshot.revision, &snapshot));
  assert(snapshot.inventory_version == 2);
  assert(snapshot.inventory_full_rebuilds == full_before_metadata);
  assert(snapshot.inventory_incremental_refreshes ==
         incremental_before_metadata);
  assert(FindItem(snapshot, 309)->name == "Hardened 10mm Pistol");
  assert(FindItem(snapshot, 329)->name == untouched_name);

  const uint64_t full_before_structure = snapshot.inventory_full_rebuilds;
  const uint64_t incremental_before_structure =
      snapshot.inventory_incremental_refreshes;
  std::vector<uint8_t> category_replacement;
  Array(&category_replacement, 340, {329, 309});
  assert(database.ApplyDataUpdate(category_replacement.data(),
                                  category_replacement.size(), &error));
  assert(database.CaptureIfChanged(snapshot.revision, &snapshot));
  assert(snapshot.inventory_full_rebuilds == full_before_structure + 1);
  assert(snapshot.inventory_incremental_refreshes ==
         incremental_before_structure);
  assert(snapshot.items.size() == 2);
  assert(snapshot.items[0].node_id == 329);
  assert(snapshot.items[1].node_id == 309);
  assert(FindItem(snapshot, 309)->name == "Hardened 10mm Pistol");
  assert(FindItem(snapshot, 329)->name == untouched_name);
}

void TestDetachedInventorySubtreesAreReclaimed() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "inventory-reclaim", "connected");
  std::vector<uint8_t> initial;
  AddInventoryItem(&initial, 500, "Pipe Pistol 0", 8000, 1);
  Array(&initial, 510, {503});
  PrimitiveUInt(&initial, 511, 1);
  Object(&initial, 512, {{"Weapons", 510}, {"Version", 511}});
  Object(&initial, 0, {{"Inventory", 512}});
  std::string error;
  assert(database.ApplyDataUpdate(initial.data(), initial.size(), &error));
  const size_t stable_nodes = database.NodeCount();

  for (uint32_t iteration = 1; iteration <= 128; ++iteration) {
    const uint32_t base = 1000 + iteration * 10;
    std::vector<uint8_t> replacement;
    AddInventoryItem(&replacement, base,
                     "Pipe Pistol " + std::to_string(iteration),
                     8000 + iteration, 1);
    Array(&replacement, 510, {base + 3});
    PrimitiveUInt(&replacement, 511, iteration + 1);
    assert(database.ApplyDataUpdate(replacement.data(), replacement.size(),
                                    &error));
    database.PruneUnreachableNodes();
    const auto snapshot = database.Capture();
    assert(snapshot.items.size() == 1);
    assert(snapshot.items[0].name ==
           "Pipe Pistol " + std::to_string(iteration));
    assert(database.NodeCount() == stable_nodes);
  }
}

void TestWorldAndLocalPlayerExtraction() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "map-players", "connected");
  std::vector<uint8_t> initial;
  PrimitiveFloat(&initial, 400, 10.0f);
  PrimitiveFloat(&initial, 401, 20.0f);
  PrimitiveFloat(&initial, 402, 30.0f);
  Object(&initial, 403, {{"X", 400}, {"Y", 401}, {"Rotation", 402}});
  Object(&initial, 404, {{"Player", 403}});
  PrimitiveFloat(&initial, 410, 110.0f);
  PrimitiveFloat(&initial, 411, 120.0f);
  PrimitiveFloat(&initial, 412, 130.0f);
  Object(&initial, 413, {{"X", 410}, {"Y", 411}, {"Rotation", 412}});
  Object(&initial, 414, {{"Player", 413}});
  Object(&initial, 415, {{"World", 404}, {"Local", 414}});
  Object(&initial, 0, {{"Map", 415}});
  std::string error;
  assert(database.ApplyDataUpdate(initial.data(), initial.size(), &error));

  auto snapshot = database.Capture();
  assert(std::fabs(snapshot.player_x - 10.0f) < 0.001f);
  assert(std::fabs(snapshot.player_y - 20.0f) < 0.001f);
  assert(std::fabs(snapshot.player_rotation - 30.0f) < 0.001f);
  assert(snapshot.local_player_valid);
  assert(std::fabs(snapshot.local_player_x - 110.0f) < 0.001f);
  assert(std::fabs(snapshot.local_player_y - 120.0f) < 0.001f);
  assert(std::fabs(snapshot.local_player_rotation - 130.0f) < 0.001f);
  assert(snapshot.local_player_x != snapshot.player_x);
  assert(snapshot.local_player_y != snapshot.player_y);

  std::vector<uint8_t> local_only;
  PrimitiveFloat(&local_only, 410, 210.0f);
  assert(database.ApplyDataUpdate(local_only.data(), local_only.size(), &error));
  assert(database.CaptureIfChanged(snapshot.revision, &snapshot));
  assert(std::fabs(snapshot.player_x - 10.0f) < 0.001f);
  assert(std::fabs(snapshot.local_player_x - 210.0f) < 0.001f);
}

void TestMalformedAndTruncatedUpdates() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "malformed", "connected");
  std::vector<uint8_t> seed;
  PrimitiveString(&seed, 1, "Sole Survivor");
  PrimitiveFloat(&seed, 2, 75.0f);
  Object(&seed, 3, {{"PlayerName", 1}, {"CurrHP", 2}});
  Object(&seed, 0, {{"PlayerInfo", 3}});
  std::string error;
  assert(database.ApplyDataUpdate(seed.data(), seed.size(), &error));

  std::vector<std::vector<uint8_t>> canonical;
  canonical.emplace_back();
  PrimitiveBoolean(&canonical.back(), 50, true);
  canonical.emplace_back();
  PrimitiveInt8(&canonical.back(), 51, -4);
  canonical.emplace_back();
  PrimitiveUInt8(&canonical.back(), 52, 250);
  canonical.emplace_back();
  PrimitiveInt(&canonical.back(), 53, -4000);
  canonical.emplace_back();
  PrimitiveUInt(&canonical.back(), 54, 4000);
  canonical.emplace_back();
  PrimitiveFloat(&canonical.back(), 55, 1.25f);
  canonical.emplace_back();
  PrimitiveString(&canonical.back(), 56, "text");
  canonical.emplace_back();
  Array(&canonical.back(), 57, {1, 2});
  canonical.emplace_back();
  Object(&canonical.back(), 58, {{"Name", 1}}, {2});

  for (const auto& record : canonical) {
    for (size_t length = 1; length < record.size(); ++length) {
      std::vector<uint8_t> prefix(record.begin(), record.begin() + length);
      AssertRejected(&database, prefix);
    }
  }
  for (uint16_t raw = 9; raw <= 255; ++raw) {
    std::vector<uint8_t> unknown;
    Put<uint8_t>(&unknown, static_cast<uint8_t>(raw));
    Put<uint32_t>(&unknown, 60);
    AssertRejected(&database, unknown);
  }

  std::vector<uint8_t> nan;
  PrimitiveFloat(&nan, 61, std::numeric_limits<float>::quiet_NaN());
  AssertRejected(&database, nan);
  std::vector<uint8_t> infinity;
  PrimitiveFloat(&infinity, 61, std::numeric_limits<float>::infinity());
  AssertRejected(&database, infinity);

  std::vector<uint8_t> unterminated;
  Put<uint8_t>(&unterminated, 6);
  Put<uint32_t>(&unterminated, 62);
  unterminated.insert(unterminated.end(), {'n', 'o', 'n', 'u', 'l'});
  AssertRejected(&database, unterminated);

  std::vector<uint8_t> short_array;
  Put<uint8_t>(&short_array, 7);
  Put<uint32_t>(&short_array, 63);
  Put<uint16_t>(&short_array, 2);
  Put<uint32_t>(&short_array, 1);
  AssertRejected(&database, short_array);
  std::vector<uint8_t> empty_key;
  Object(&empty_key, 64, {{"", 1}});
  AssertRejected(&database, empty_key);
  std::vector<uint8_t> duplicate_keys;
  Object(&duplicate_keys, 65, {{"duplicate", 1}, {"duplicate", 2}});
  AssertRejected(&database, duplicate_keys);
  std::vector<uint8_t> dangling_object;
  Object(&dangling_object, 66, {{"Missing", 999999}});
  AssertRejected(&database, dangling_object);

  std::vector<uint8_t> atomic_batch;
  PrimitiveFloat(&atomic_batch, 2, 12.0f);
  PrimitiveString(&atomic_batch, 67, "missing terminator");
  atomic_batch.pop_back();
  AssertRejected(&database, atomic_batch);
  assert(std::fabs(database.Capture().hp - 75.0f) < 0.001f);

  std::vector<uint8_t> duplicate_records;
  PrimitiveFloat(&duplicate_records, 2, 12.0f);
  PrimitiveFloat(&duplicate_records, 2, 13.0f);
  AssertRejected(&database, duplicate_records);
  assert(std::fabs(database.Capture().hp - 75.0f) < 0.001f);

  uint8_t byte = 0;
  error.clear();
  assert(!database.ApplyDataUpdate(nullptr, 1, &error) && !error.empty());
  error.clear();
  assert(!database.ApplyDataUpdate(&byte, 16u * 1024u * 1024u + 1u, &error));
  assert(!error.empty());
}

void TestDataFrameSizeBoundary() {
  constexpr size_t kFrameLimit = 16u * 1024u * 1024u;
  constexpr size_t kStringLimit = 256u * 1024u;
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "frame-boundary", "connected");
  std::vector<uint8_t> exact;
  const std::string maximum_string(kStringLimit, 'm');
  for (uint32_t i = 0; i < 63; ++i)
    PrimitiveString(&exact, 10000 + i, maximum_string);
  assert(exact.size() < kFrameLimit);
  const size_t final_record_bytes = kFrameLimit - exact.size();
  assert(final_record_bytes >= 6 && final_record_bytes <= kStringLimit + 6);
  PrimitiveString(&exact, 10063,
                  std::string(final_record_bytes - 6, 'z'));
  assert(exact.size() == kFrameLimit);

  std::string error;
  assert(database.ApplyDataUpdate(exact.data(), exact.size(), &error));
  const auto before = database.Capture();
  const size_t nodes_before = database.NodeCount();
  const size_t retained_before = database.RetainedBytes();
  assert(nodes_before == 64);

  exact.push_back(0);
  error.clear();
  assert(!database.ApplyDataUpdate(exact.data(), exact.size(), &error));
  assert(!error.empty());
  const auto after = database.Capture();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(database.NodeCount() == nodes_before);
  assert(database.RetainedBytes() == retained_before);
}

void TestRetainedNodeBudgetAtomicity() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "memory-budget", "connected");
  std::vector<uint8_t> seed;
  PrimitiveUInt(&seed, 1, 1);
  std::string error;
  assert(database.ApplyDataUpdate(seed.data(), seed.size(), &error));

  const std::vector<uint32_t> repeated_reference(65535, 1);
  std::vector<uint8_t> retained;
  for (uint32_t i = 0; i < 39; ++i)
    Array(&retained, 20000 + i, repeated_reference);
  assert(retained.size() < 16u * 1024u * 1024u);
  assert(database.ApplyDataUpdate(retained.data(), retained.size(), &error));
  const auto before = database.Capture();
  const size_t nodes_before = database.NodeCount();
  const size_t retained_before = database.RetainedBytes();
  assert(nodes_before == 40);
  assert(retained_before < 96u * 1024u * 1024u);

  std::vector<uint8_t> over_budget;
  for (uint32_t i = 0; i < 4; ++i)
    Array(&over_budget, 21000 + i, repeated_reference);
  error.clear();
  assert(!database.ApplyDataUpdate(over_budget.data(), over_budget.size(),
                                   &error));
  assert(error.find("retained node memory budget") != std::string::npos);
  const auto after = database.Capture();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(database.NodeCount() == nodes_before);
  assert(database.RetainedBytes() == retained_before);
}

void AssertLocalMapRejected(fo4ds::PipboyDatabase* database,
                            const std::vector<uint8_t>& payload) {
  const auto before = database->Capture();
  const auto map_before = database->CaptureLocalMap();
  std::string error;
  assert(!database->ApplyLocalMap(payload.data(), payload.size(), &error));
  assert(!error.empty());
  const auto after = database->Capture();
  const auto map_after = database->CaptureLocalMap();
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(map_after.revision == map_before.revision);
  assert(map_after.width == map_before.width && map_after.height == map_before.height);
  assert(map_after.pixels == map_before.pixels);
}

void TestLocalMapBoundsAndAtomicity() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "map", "connected");
  BuildAllDomains(&database);
  const std::array<float, 6> bounds = {-10.0f, 20.0f, 30.0f,
                                       20.0f, -10.0f, -40.0f};
  auto valid = LocalMap(3, 2, bounds, {0, 32, 64, 128, 192, 255});
  auto before = database.Capture().domain_revisions;
  const uint64_t global_revision_before = database.Capture().revision;
  std::string error;
  assert(database.ApplyLocalMap(valid.data(), valid.size(), &error));
  const auto map = database.CaptureLocalMap();
  const auto snapshot = database.Capture();
  assert(snapshot.revision == global_revision_before + 1);
  assert(snapshot.domain_revisions == before);
  assert(map.revision == 1 && map.width == 3 && map.height == 2);
  assert(map.northwest_x == bounds[0] && map.southwest_y == bounds[5]);
  assert(map.pixels == std::vector<uint8_t>({0, 32, 64, 128, 192, 255}));

  for (size_t length = 0; length < 32; ++length) {
    std::vector<uint8_t> prefix(valid.begin(), valid.begin() + length);
    AssertLocalMapRejected(&database, prefix);
  }
  AssertLocalMapRejected(&database, LocalMap(0, 2, bounds, {}));
  AssertLocalMapRejected(&database, LocalMap(2, 0, bounds, {}));
  AssertLocalMapRejected(&database,
                         LocalMap(16u * 1024u * 1024u + 1u, 1, bounds, {}));
  AssertLocalMapRejected(&database,
                         LocalMap(std::numeric_limits<uint32_t>::max(),
                                  std::numeric_limits<uint32_t>::max(), bounds,
                                  {}));
  AssertLocalMapRejected(&database, LocalMap(2, 2, bounds, {1, 2, 3}));
  AssertLocalMapRejected(&database, LocalMap(2, 2, bounds, {1, 2, 3, 4, 5}));

  const auto snapshot_before_null = database.Capture();
  const auto map_before_null = database.CaptureLocalMap();
  error.clear();
  assert(!database.ApplyLocalMap(nullptr, 32, &error));
  assert(!error.empty());
  const auto snapshot_after_null = database.Capture();
  const auto map_after_null = database.CaptureLocalMap();
  assert(snapshot_after_null.revision == snapshot_before_null.revision);
  assert(snapshot_after_null.domain_revisions ==
         snapshot_before_null.domain_revisions);
  assert(map_after_null.revision == map_before_null.revision);
  assert(map_after_null.pixels == map_before_null.pixels);

  for (size_t coordinate = 0; coordinate < bounds.size(); ++coordinate) {
    for (float invalid : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity(),
                          -std::numeric_limits<float>::infinity()}) {
      auto invalid_bounds = bounds;
      invalid_bounds[coordinate] = invalid;
      AssertLocalMapRejected(&database, LocalMap(1, 1, invalid_bounds, {1}));
    }
  }

  AssertLocalMapRejected(
      &database,
      LocalMap(32769, 1, bounds, std::vector<uint8_t>(32769, 0x7f)));
  AssertLocalMapRejected(
      &database,
      LocalMap(1, 32769, bounds, std::vector<uint8_t>(32769, 0x7f)));

  uint8_t byte = 0;
  const auto snapshot_before_oversize = database.Capture();
  const auto map_before_oversize = database.CaptureLocalMap();
  error.clear();
  assert(!database.ApplyLocalMap(&byte, 16u * 1024u * 1024u + 1u, &error));
  assert(!error.empty());
  const auto snapshot_after_oversize = database.Capture();
  const auto map_after_oversize = database.CaptureLocalMap();
  assert(snapshot_after_oversize.revision == snapshot_before_oversize.revision);
  assert(snapshot_after_oversize.domain_revisions ==
         snapshot_before_oversize.domain_revisions);
  assert(map_after_oversize.revision == map_before_oversize.revision);
  assert(map_after_oversize.pixels == map_before_oversize.pixels);
}

void TestReconnectGenerations() {
  fo4ds::PipboyDatabase database;
  auto initial = database.Capture();
  assert(initial.generation == 0 && !initial.connected);
  database.SetConnection(true, false, "server-a", "connected");
  BuildAllDomains(&database);
  auto cached = database.Capture();
  const uint64_t first_generation = cached.generation;
  assert(first_generation > initial.generation && cached.root_ready);

  database.SetConnection(false, false, {}, "connection lost");
  assert(database.CaptureIfChanged(cached.revision, &cached));
  assert(!cached.connected && cached.root_ready);
  assert(cached.player_name == "Sole Survivor");
  assert(cached.generation == first_generation);

  database.SetConnection(true, false, "server-b", "reconnected");
  assert(database.CaptureIfChanged(cached.revision, &cached));
  assert(cached.connected && !cached.root_ready && cached.player_name.empty());
  assert(cached.items.empty() && cached.quests.empty() && cached.markers.empty());
  assert(cached.radio.empty() && cached.generation > first_generation);
  assert(database.NodeCount() == 0 && database.CaptureLocalMap().pixels.empty());

  const uint64_t reconnect_generation = cached.generation;
  database.Reset("save changed");
  assert(database.CaptureIfChanged(cached.revision, &cached));
  assert(!cached.connected && cached.generation > reconnect_generation);
  assert(cached.status == "save changed");

  const uint64_t reset_generation = cached.generation;
  database.SetConnection(true, false, {}, "connected again");
  assert(database.CaptureIfChanged(cached.revision, &cached));
  assert(cached.generation > reset_generation && cached.connected);
  assert(!cached.root_ready);
}

void TestInvalidateTreeLifecycle() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "persistent-server", "connected");
  BuildAllDomains(&database);
  const std::array<float, 6> bounds = {-1.0f, 1.0f, 1.0f,
                                       1.0f, -1.0f, -1.0f};
  const auto local_map = LocalMap(2, 2, bounds, {1, 2, 3, 4});
  std::string error;
  assert(database.ApplyLocalMap(local_map.data(), local_map.size(), &error));
  const auto before = database.Capture();
  const auto map_before = database.CaptureLocalMap();
  assert(before.ActionsSafe());
  assert(database.NodeCount() > 0);
  assert(map_before.revision > 0 && !map_before.pixels.empty());

  database.InvalidateTree("save-load boundary");
  auto after = database.Capture();
  const auto map_after = database.CaptureLocalMap();
  assert(after.generation == before.generation + 1);
  assert(after.revision == before.revision + 1);
  for (size_t i = 0; i < before.domain_revisions.size(); ++i)
    assert(after.domain_revisions[i] == before.domain_revisions[i] + 1);
  assert(after.connected == before.connected);
  assert(after.busy == before.busy);
  assert(after.server_identity == before.server_identity);
  assert(after.status == "save-load boundary");
  assert(!after.root_ready && !after.ActionsSafe());
  assert(after.player_name.empty() && after.items.empty());
  assert(after.quests.empty() && after.markers.empty() && after.radio.empty());
  assert(database.NodeCount() == 0);
  assert(map_after.revision == 0 && map_after.width == 0 && map_after.height == 0);
  assert(map_after.pixels.empty());

  std::vector<uint8_t> player_name;
  PrimitiveString(&player_name, 500, "Returned Survivor");
  assert(database.ApplyDataUpdate(player_name.data(), player_name.size(), &error));
  after = database.Capture();
  assert(!after.root_ready && !after.ActionsSafe());

  std::vector<uint8_t> player;
  Object(&player, 501, {{"PlayerName", 500}});
  assert(database.ApplyDataUpdate(player.data(), player.size(), &error));
  after = database.Capture();
  assert(!after.root_ready && !after.ActionsSafe());

  std::vector<uint8_t> root;
  Object(&root, 0, {{"PlayerInfo", 501}});
  assert(database.ApplyDataUpdate(root.data(), root.size(), &error));
  after = database.Capture();
  assert(after.connected && after.root_ready && !after.ActionsSafe());
  assert(after.player_name == "Returned Survivor");

  std::vector<uint8_t> status;
  Object(&status, 502, {});
  Object(&status, 0, {{"Status", 502}});
  assert(database.ApplyDataUpdate(status.data(), status.size(), &error));
  after = database.Capture();
  assert(after.status_ready && after.ActionsSafe());
}

void TestEmptyCommandDiscard() {
  fo4ds::PipboyDatabase database;
  database.SetConnection(true, false, "discard-server", "connected");
  const auto before = database.Capture();
  fo4ds::PipboyClient client(&database, 1);
  client.DiscardCommands(nullptr);
  client.DiscardCommands("");
  client.DiscardCommands("tree invalidated");
  const auto after = database.Capture();
  assert(after.generation == before.generation);
  assert(after.revision == before.revision);
  assert(after.domain_revisions == before.domain_revisions);
  assert(after.connected == before.connected);
  assert(after.busy == before.busy);
  assert(after.server_identity == before.server_identity);
  assert(after.status == before.status);
}

bool ReceiveExact(SOCKET socket, void* output, size_t size) {
  auto* bytes = static_cast<uint8_t*>(output);
  size_t received = 0;
  while (received < size) {
    const int chunk = recv(socket, reinterpret_cast<char*>(bytes + received),
                           static_cast<int>(size - received), 0);
    if (chunk <= 0) return false;
    received += static_cast<size_t>(chunk);
  }
  return true;
}

struct Frame {
  uint8_t type = 0;
  std::string payload;
};

Frame ReceiveFrame(SOCKET socket) {
  uint32_t size = 0;
  Frame frame;
  assert(ReceiveExact(socket, &size, sizeof(size)));
  assert(ReceiveExact(socket, &frame.type, sizeof(frame.type)));
  assert(size <= 65536);
  frame.payload.resize(size);
  if (size) assert(ReceiveExact(socket, frame.payload.data(), size));
  return frame;
}

void SendFrame(SOCKET socket, uint8_t type, const std::string& payload) {
  std::vector<uint8_t> frame;
  Put<uint32_t>(&frame, static_cast<uint32_t>(payload.size()));
  Put<uint8_t>(&frame, type);
  frame.insert(frame.end(), payload.begin(), payload.end());
  size_t sent = 0;
  while (sent < frame.size()) {
    const int chunk = send(socket,
                           reinterpret_cast<const char*>(frame.data() + sent),
                           static_cast<int>(frame.size() - sent), 0);
    assert(chunk > 0);
    sent += static_cast<size_t>(chunk);
  }
}

bool ReadableWithin(SOCKET socket, long timeout_ms) {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(socket, &readable);
  timeval timeout = {};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  return select(0, &readable, nullptr, nullptr, &timeout) == 1;
}

SOCKET AcceptWithin(SOCKET listener, long timeout_ms) {
  if (!ReadableWithin(listener, timeout_ms)) return INVALID_SOCKET;
  return accept(listener, nullptr, nullptr);
}

void TestCommandEpochInvalidation() {
  WSADATA winsock = {};
  assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(listener != INVALID_SOCKET);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  int address_size = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                     &address_size) == 0);
  const uint16_t port = ntohs(address.sin_port);

  fo4ds::PipboyDatabase database;
  fo4ds::PipboyClient client(&database, port);
  assert(client.Start());
  assert(client.SendCommand(40, R"({"stale":true})"));
  client.DiscardCommands("lifecycle epoch changed");
  assert(listen(listener, 1) == 0);
  SOCKET peer = AcceptWithin(listener, 5000);
  assert(peer != INVALID_SOCKET);
  const DWORD receive_timeout_ms = 5000;
  assert(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&receive_timeout_ms),
                    sizeof(receive_timeout_ms)) == 0);

  assert(!ReadableWithin(peer, 350));
  assert(client.SendCommand(41, R"({"current":true})"));
  const Frame current = ReceiveFrame(peer);
  assert(current.type == 5);
  assert(current.payload ==
         R"({"type":41,"args":{"current":true},"id":2})");

  client.Stop();
  closesocket(peer);
  closesocket(listener);
  WSACleanup();
}

void TestPartialFrameTimeoutReconnect() {
  WSADATA winsock = {};
  assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(listener != INVALID_SOCKET);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(listener, 2) == 0);
  int address_size = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                     &address_size) == 0);
  const uint16_t port = ntohs(address.sin_port);

  fo4ds::PipboyDatabase database;
  fo4ds::PipboyClient client(&database, port);
  assert(client.Start());
  SOCKET first_peer = AcceptWithin(listener, 3000);
  assert(first_peer != INVALID_SOCKET);
  bool initially_connected = false;
  for (int i = 0; i < 100 && !initially_connected; ++i) {
    initially_connected = database.Capture().connected;
    if (!initially_connected)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(initially_connected);
  const uint64_t first_generation = database.Capture().generation;

  const uint32_t declared_size = 4;
  assert(send(first_peer, reinterpret_cast<const char*>(&declared_size), 2, 0) ==
         2);
  SOCKET second_peer = AcceptWithin(listener, 6000);
  assert(second_peer != INVALID_SOCKET);

  bool reconnected = false;
  for (int i = 0; i < 200 && !reconnected; ++i) {
    const auto snapshot = database.Capture();
    reconnected = snapshot.connected && snapshot.generation > first_generation;
    if (!reconnected)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(reconnected);
  SendFrame(second_peer, 1, R"({"version":"after-timeout"})");
  bool identity_received = false;
  for (int i = 0; i < 100 && !identity_received; ++i) {
    identity_received = database.Capture().server_identity.find("after-timeout") !=
                        std::string::npos;
    if (!identity_received)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(identity_received);

  client.Stop();
  closesocket(first_peer);
  closesocket(second_peer);
  closesocket(listener);
  WSACleanup();
}

void TestCommandQueueFraming() {
  WSADATA winsock = {};
  assert(WSAStartup(MAKEWORD(2, 2), &winsock) == 0);
  SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(listener != INVALID_SOCKET);
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
  assert(listen(listener, 1) == 0);
  int address_size = sizeof(address);
  assert(getsockname(listener, reinterpret_cast<sockaddr*>(&address),
                     &address_size) == 0);
  const uint16_t port = ntohs(address.sin_port);

  fo4ds::PipboyDatabase database;
  fo4ds::PipboyClient stopped_client(&database, port);
  assert(!stopped_client.SendCommand(13, "[]"));
  fo4ds::PipboyClient client(&database, port);
  assert(client.Start() && client.Start());
  SOCKET peer = accept(listener, nullptr, nullptr);
  assert(peer != INVALID_SOCKET);
  const DWORD receive_timeout_ms = 5000;
  assert(setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO,
                    reinterpret_cast<const char*>(&receive_timeout_ms),
                    sizeof(receive_timeout_ms)) == 0);

  assert(!client.SendCommand(1, ""));
  assert(!client.SendCommand(1, std::string(65537, 'x')));
  assert(client.SendCommand(13, "[]"));
  assert(client.SendCommand(7, R"({"x":1})"));
  const Frame first = ReceiveFrame(peer);
  const Frame second = ReceiveFrame(peer);
  assert(first.type == 5);
  assert(first.payload == R"({"type":13,"args":[],"id":1})");
  assert(second.type == 5);
  assert(second.payload == R"({"type":7,"args":{"x":1},"id":2})");

  SendFrame(peer, 0, {});
  const Frame pong = ReceiveFrame(peer);
  assert(pong.type == 0 && pong.payload.empty());
  SendFrame(peer, 1, R"({"version":"test-server"})");
  bool identity_received = false;
  for (int i = 0; i < 100 && !identity_received; ++i) {
    identity_received = database.Capture().server_identity.find("test-server") !=
                        std::string::npos;
    if (!identity_received)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(identity_received);

  client.Stop();
  closesocket(peer);
  closesocket(listener);
  WSACleanup();
}

}  // namespace

int main() {
  TestSnapshotsAndDomainCaching();
  TestFallout4StatsSchemaAndReadiness();
  TestQuestAndMapFieldCoverage();
  TestEmptyUpdateAndRootReadiness();
  TestObjectAndArrayMutation();
  TestSameFrameInventoryAdditionDomain();
  TestInventoryIncrementalCache();
  TestDetachedInventorySubtreesAreReclaimed();
  TestWorldAndLocalPlayerExtraction();
  TestMalformedAndTruncatedUpdates();
  TestDataFrameSizeBoundary();
  TestRetainedNodeBudgetAtomicity();
  TestLocalMapBoundsAndAtomicity();
  TestReconnectGenerations();
  TestInvalidateTreeLifecycle();
  TestEmptyCommandDiscard();
  TestCommandEpochInvalidation();
  TestPartialFrameTimeoutReconnect();
  TestCommandQueueFraming();
  std::cout << "ProtocolTests passed\n";
  return 0;
}
