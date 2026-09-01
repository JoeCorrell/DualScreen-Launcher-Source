#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>

#include "UiAdapter.h"

int main() {
  using namespace fo4ds;
  PipboySnapshot source;
  source.generation = 7;
  source.revision = 12;
  source.domain_revisions = {1, 2, 3, 4, 5};
  source.connected = true;
  source.root_ready = true;
  source.stats_ready = true;
  source.inventory_ready = true;
  source.data_ready = true;
  source.map_ready = true;
  source.radio_ready = true;
  source.effects_ready = true;
  source.perks_ready = true;
  source.general_stats_ready = true;
  source.status_ready = true;
  source.inventory_version = 77;
  source.status = "Live";
  source.player_name = "Sole Survivor";
  source.current_cell = "Publick Occurrences";
  source.current_worldspace = "Commonwealth";
  source.level = 14;
  source.caps = 321;
  source.hp = 82.0f;
  source.max_hp = 100.0f;
  source.ap = 61.0f;
  source.max_ap = 90.0f;
  source.xp_progress = 0.42f;
  source.perk_points = 2;
  source.stimpaks = 9;
  source.radaway = 4;
  source.special[0] = 4;
  source.special_modifiers[0] = 1;
  source.special_names[0] = "Strength";
  source.special_descriptions[0] = "Raw physical power";
  source.total_damage = {18.0f, 0.0f, 2.0f, 7.0f, 0.0f, 0.0f};
  source.total_resistance = {12.0f, 0.0f, 0.0f, 30.0f, 0.0f, 50.0f};
  source.limb_condition = {1.0f, 0.75f, 0.5f, 0.25f, 0.1f, 0.0f};
  source.body_flags = 0x1234;
  source.head_flags = 0x5678;

  InventoryItemState removed;
  removed.node_id = 90;
  removed.count = 0;
  removed.name = "Removed";
  source.items.push_back(removed);

  InventoryItemState weapon;
  weapon.node_id = 100;
  weapon.form_id = 0x1234;
  weapon.handle_id = 500;
  weapon.count = 2;
  weapon.equip_state = 1;
  weapon.stack_id = 7;
  weapon.filter_flag = 2;
  weapon.name = "10mm Pistol";
  weapon.card.push_back({"Damage", "", 18.0f, 0, 0});
  weapon.card.push_back({"$dmg", "", 18.0f, 2, 1});
  weapon.card.push_back({"$dmg", "", 0.0f, 2, 4});
  weapon.card.push_back({"$dmg", "", 0.0f, 2, 6});
  weapon.card.push_back({"$wt", "", 3.5f, 2, 0});
  weapon.card.push_back({"$ROF", "", 46.7f, 2, 0});
  weapon.card.push_back({"$rng", "", 84.0f, 2, 0});
  weapon.card.push_back({"$acc", "", 61.0f, 2, 0});
  weapon.card.push_back({"10mm", "", 129.0f, 0, 10});
  source.items.push_back(weapon);

  InventoryItemState armor;
  armor.node_id = 102;
  armor.count = 1;
  armor.filter_flag = 4;
  armor.name = "Combat Armor";
  armor.card.push_back({"$dr", "", 12.0f, 2, 1});
  armor.card.push_back({"$dr", "", 30.0f, 2, 4});
  armor.card.push_back({"$dr", "", 99.0f, 2, 6});
  source.items.push_back(armor);

  InventoryItemState note;
  note.node_id = 101;
  note.count = 1;
  note.filter_flag = 128;
  note.name = "Overseer's Log";
  note.card.push_back({"Text", "Vault entry", 0.0f, 0, 0});
  source.items.push_back(note);

  InventoryItemState junk;
  junk.node_id = 103;
  junk.count = 3;
  junk.filter_flag = 1024;
  junk.name = "Desk Fan";
  junk.components.push_back({"Steel", 2, true});
  source.items.push_back(junk);

  InventoryItemState mod;
  mod.node_id = 104;
  mod.count = 1;
  mod.filter_flag = 2048;
  mod.name = "Hardened Receiver";
  source.items.push_back(mod);

  InventoryItemState favorite;
  favorite.node_id = 105;
  favorite.count = 1;
  favorite.filter_flag = 1 | 2;
  favorite.name = "Favorite Rifle";
  source.items.push_back(favorite);

  EffectState effect;
  effect.node_id = 200;
  effect.source = "Buffout";
  effect.name = "Strength";
  effect.magnitude = -2.0f;
  effect.duration = 120.0f;
  effect.show_as_percent = true;
  effect.detrimental = true;
  source.effects.push_back(effect);

  PipPerkState perk;
  perk.node_id = 201;
  perk.form_id = 0x4D8A7;
  perk.clip_name = 0x4D8A7;
  perk.rank = 2;
  perk.max_rank = 4;
  perk.name = "Bloody Mess";
  perk.description = "Rank two description";
  perk.swf_file = "Components/VaultBoys/Perks/PerkClip_4d8a7.swf";
  source.perks.push_back(perk);

  PipGeneralStatState general;
  general.node_id = 202;
  general.value = 12.0f;
  general.show_if_zero = true;
  general.category = "$Combat";
  general.name = "Critical Strikes";
  source.general_stats.push_back(general);
  PipGeneralStatState hidden_zero;
  hidden_zero.node_id = 203;
  hidden_zero.show_if_zero = false;
  hidden_zero.category = "$Crime";
  hidden_zero.name = "Murders";
  source.general_stats.push_back(hidden_zero);

  QuestState quest;
  quest.form_id = 0x2000;
  quest.instance = 9;
  quest.type = 6;
  quest.enabled = true;
  quest.active = true;
  quest.name = "When Freedom Calls";
  QuestObjectiveState objective;
  objective.node_id = 0x2100;
  objective.active = true;
  objective.text = "Enter the museum";
  objective.enabled = true;
  quest.objectives.push_back(objective);
  source.quests.push_back(quest);

  MapMarkerState marker;
  marker.node_id = 300;
  marker.visible = true;
  marker.discovered = true;
  marker.cleared = true;
  marker.local = true;
  marker.kind = MapMarkerState::Kind::kQuest;
  marker.on_door = true;
  marker.type = 17;
  marker.height = 42.0f;
  marker.name = "Sanctuary";
  source.markers.push_back(marker);

  RadioState radio;
  radio.node_id = 400;
  radio.active = true;
  radio.in_range = true;
  radio.frequency = 98.5f;
  radio.name = "Diamond City Radio";
  source.radio.push_back(radio);

  auto ui = std::make_unique<Snapshot>();
  AdaptPipboySnapshot(source, true, ui.get());
  assert(ui->version == kSnapshotVersion && ui->size == sizeof(Snapshot));
  assert(ui->lifecycle == Lifecycle::kLive);
  assert(ui->sequence == 12 && ui->world_epoch == 7);
  assert((ui->capabilities & kCapActions) != 0);
  assert((ui->capabilities & kCapStats) != 0);
  assert((ui->capabilities & kCapInventory) != 0);
  assert((ui->capabilities & kCapQuests) != 0);
  assert((ui->capabilities & kCapMap) != 0);
  assert((ui->capabilities & kCapRadio) != 0);
  assert((ui->capabilities & kCapEffects) != 0);
  assert((ui->capabilities & kCapPerks) != 0);
  assert((ui->capabilities & kCapGeneralStats) != 0);
  assert(ui->level == 14 && ui->caps == 321);
  assert(std::string(ui->location) == "Publick Occurrences");
  assert(ui->perk_points == 2 && ui->stimpaks == 9 && ui->radaway == 4);
  assert(ui->xp == 42 && ui->xp_next == 100);
  assert(ui->radiation < 0.0f);
  assert(std::fabs(ui->damage_resistance - 12.0f) < 0.01f);
  assert(std::fabs(ui->special[0] - 4.0f) < 0.01f);
  assert(ui->special_modifiers[0] == 1);
  assert(std::string(ui->special_names[0]) == "Strength");
  assert(std::string(ui->special_descriptions[0]) == "Raw physical power");
  assert(std::fabs(ui->total_damage[3] - 7.0f) < 0.01f);
  assert(std::fabs(ui->total_resistance[5] - 50.0f) < 0.01f);
  assert(std::fabs(ui->limbs[1] - 75.0f) < 0.01f);
  assert(ui->body_flags == 0x1234 && ui->head_flags == 0x5678);
  assert(ui->item_count == 6);
  assert(ui->items[0].form_id == 100);
  assert(ui->items[0].category == ItemCategory::kWeapon);
  assert((ui->items[0].flags & kItemEquipped) != 0);
  assert(std::fabs(ui->items[0].damage - 18.0f) < 0.01f);
  assert(std::fabs(ui->items[0].weight - 3.5f) < 0.01f);
  assert(ui->items[0].ammo_count == 129);
  assert(std::string(ui->items[0].description).find("10mm") !=
         std::string::npos);
  assert(std::string(ui->items[0].description).find("FIRE RATE") !=
         std::string::npos);
  assert(std::fabs(ui->items[1].armor - 12.0f) < 0.01f);
  assert(ui->items[3].category == ItemCategory::kJunk);
  assert(std::string(ui->items[3].description).find("2 Steel [TAGGED]") !=
         std::string::npos);
  assert(ui->items[4].category == ItemCategory::kMod);
  assert(ui->items[5].category == ItemCategory::kWeapon);
  assert((ui->items[5].flags & kItemFavorite) != 0);
  assert(ui->note_count == 1);
  assert(ui->effect_count == 1 && ui->effects[0].form_id == 200);
  assert(std::string(ui->effects[0].name) == "Buffout: Strength");
  assert(std::fabs(ui->effects[0].magnitude + 2.0f) < 0.01f);
  assert(ui->effects[0].show_as_percent && ui->effects[0].detrimental);
  assert(ui->perk_count == 1 && ui->perks[0].rank == 2);
  assert(ui->perks[0].form_id == 0x4D8A7 &&
         ui->perks[0].clip_name == 0x4D8A7);
  assert(std::string(ui->perks[0].swf_file) ==
         "Components/VaultBoys/Perks/PerkClip_4d8a7.swf");
  assert(std::string(ui->perks[0].description) == "Rank two description");
  assert(ui->general_stat_count == 1);
  assert(std::string(ui->general_stats[0].category) == "Combat");
  assert(std::string(ui->general_stats[0].name) == "Critical Strikes");
  assert(ui->quest_count == 1 && ui->objective_count == 1);
  assert((ui->quests[0].flags & kQuestTracked) != 0);
  assert(ui->objectives[0].objective_id == 0x2100);
  assert((ui->objectives[0].flags & kObjectiveTracked) != 0);
  assert(ui->marker_count == 1);
  assert((ui->markers[0].flags & kMarkerDiscovered) != 0);
  assert((ui->markers[0].flags & kMarkerCanTravel) == 0);
  assert((ui->markers[0].flags & kMarkerCleared) != 0);
  assert((ui->markers[0].flags & kMarkerLocal) != 0);
  assert((ui->markers[0].flags & kMarkerQuest) != 0);
  assert((ui->markers[0].flags & kMarkerDoor) != 0);
  assert(ui->markers[0].type == 17);
  assert(std::fabs(ui->markers[0].z - 42.0f) < 0.01f);
  assert(ui->station_count == 1);
  assert(std::fabs(ui->stations[0].frequency - 98.5f) < 0.01f);
  assert(ui->stations[0].signal < 0.0f);

  Command item_command = {};
  item_command.type = CommandType::kEquipItem;
  item_command.form_id = 100;
  NativeActionRequest action = {};
  assert(BuildNativeActionRequest(item_command, source, 41, &action));
  assert(action.token == 41);
  assert(action.kind == NativeActionKind::kUseInventoryItem);
  assert(action.item.handle_id == 500 && action.item.stack_id == 7);
  assert(action.item.inventory_version == 77);

  Command quest_command = {};
  quest_command.type = CommandType::kTrackQuest;
  quest_command.form_id = 0x2000;
  assert(BuildNativeActionRequest(quest_command, source, 42, &action));
  assert(action.token == 42);
  assert(action.kind == NativeActionKind::kToggleQuestActive);
  assert(action.quest.form_id == 0x2000 && action.quest.instance == 9 &&
         action.quest.type == 6);

  Command radio_command = {};
  radio_command.type = CommandType::kTuneRadio;
  radio_command.form_id = 400;
  assert(BuildNativeActionRequest(radio_command, source, 43, &action));
  assert(action.token == 43);
  assert(action.kind == NativeActionKind::kToggleRadioStation);
  assert(action.radio.node_id == 400);

  radio_command.type = CommandType::kRadioOff;
  radio_command.form_id = 0;
  assert(BuildNativeActionRequest(radio_command, source, 44, &action));
  assert(action.token == 44);
  assert(action.kind == NativeActionKind::kRadioOff);

  Command travel_command = {};
  travel_command.type = CommandType::kFastTravel;
  travel_command.form_id = 300;
  assert(!BuildNativeActionRequest(travel_command, source, 45, &action));
  assert(!BuildNativeActionRequest(item_command, source, 0, &action));
  assert(!BuildNativeActionRequest(item_command, source, 46, nullptr));
  item_command.form_id = 0xDEADBEEF;
  assert(!BuildNativeActionRequest(item_command, source, 47, &action));
  quest_command.form_id = 0xDEADBEEF;
  assert(!BuildNativeActionRequest(quest_command, source, 48, &action));
  radio_command.type = CommandType::kTuneRadio;
  radio_command.form_id = 0xDEADBEEF;
  assert(!BuildNativeActionRequest(radio_command, source, 49, &action));

  source.connected = false;
  source.root_ready = false;
  AdaptPipboySnapshot(source, true, ui.get());
  assert(ui->lifecycle == Lifecycle::kMainMenu);
  assert((ui->capabilities & kCapActions) == 0);

  source.connected = true;
  AdaptPipboySnapshot(source, true, ui.get());
  assert(ui->lifecycle == Lifecycle::kLoading);
  assert((ui->capabilities & kCapActions) == 0);

  source.root_ready = true;
  source.stats_ready = false;
  source.inventory_ready = false;
  source.data_ready = false;
  source.map_ready = false;
  source.radio_ready = false;
  source.effects_ready = false;
  source.perks_ready = false;
  source.general_stats_ready = false;
  source.status_ready = false;
  source.inventory_ready = true;
  AdaptPipboySnapshot(source, true, ui.get());
  assert(ui->capabilities == kCapInventory);
  assert((ui->capabilities & kCapActions) == 0);

  source.inventory_ready = false;
  AdaptPipboySnapshot(source, true, ui.get());
  assert(ui->capabilities == 0);
  assert(ui->lifecycle == Lifecycle::kLive);
  assert(ui->level < 0 && ui->xp < 0 && ui->xp_next < 0);
  assert(ui->health < 0.0f && ui->radiation < 0.0f);
  assert(ui->total_resistance[5] < 0.0f);

  std::cout << "UiAdapterTests passed\n";
  return 0;
}
