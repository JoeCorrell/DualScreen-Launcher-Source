#include <windows.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "LiveState.h"

namespace {

std::wstring Join(const std::wstring& a, const wchar_t* b) {
  std::wstring value = a;
  if (!value.empty() && value.back() != L'\\') value += L'\\';
  value += b;
  return value;
}

bool Write(const std::wstring& path, const std::string& text) {
  HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (file == INVALID_HANDLE_VALUE) return false;
  DWORD written = 0;
  const bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()),
                            &written, nullptr) &&
                  written == text.size() && FlushFileBuffers(file);
  CloseHandle(file);
  return ok;
}

std::string Revisions(unsigned status = 1, unsigned player = 1) {
  char text[512] = {};
  std::snprintf(text, sizeof(text),
    "{\"session\":1,\"status\":%u,\"progression\":1,\"inventory\":1,"
    "\"armor\":1,\"hotbar\":1,\"location\":1,\"player\":%u,"
    "\"map\":1,\"villagers\":1,\"animals\":1,\"magic\":1,"
    "\"almanac\":1,\"museum\":1,\"renown\":1,\"calendar_events\":1,"
    "\"calendar\":1,\"clock\":1,\"weather\":1,\"quests\":1,"
    "\"future_revision\":99}", status, player);
  return text;
}

std::string Full(unsigned sequence = 1) {
  return std::string("{") +
    "\"protocol\":\"fields-of-mistria-dualscreen\",\"schema\":1,"
    "\"epoch\":\"save-epoch\",\"sequence\":" + std::to_string(sequence) +
    ",\"full\":true,\"base_sequence\":0,\"generated_tick\":100,"
    "\"base_revisions\":{},\"revisions\":" + Revisions() + ",\"topics\":{"
    "\"session\":{\"in_game\":true,\"game_version\":\"1.0.4\","
      "\"game_identifier\":\"unit\",\"capabilities\":{\"select_hotbar\":true,"
      "\"swap_slots\":true,\"consume_item\":true,\"equip_armor\":true,"
      "\"unequip_armor\":true,\"pin_spell\":true,\"unpin_spell\":true,"
      "\"cast_pinned_spell\":true,\"generic_item_use\":false,\"drop_item\":false,"
      "\"quest_mutation\":false,"
      "\"quest_tracking\":false}},"
    "\"status\":{\"name\":\"Mistria \\u2605\",\"farm_name\":\"Test\","
      "\"health\":80,\"health_max\":100,\"stamina\":90,\"stamina_max\":110,"
      "\"mana\":12,\"mana_max\":20,\"tesserae\":345,\"essence\":7,\"renown\":4},"
    "\"progression\":{\"skills\":[{\"id\":\"farming\",\"xp\":42,\"level\":3,"
      "\"icon_asset\":\"spr_skill_farming\"}],"
      "\"perks\":[{\"id\":\"green_thumb\",\"active\":true}],"
      "\"status_effects\":[null,{\"type\":\"speedy\",\"amount\":1.5,"
      "\"start\":10,\"finish\":20,\"stacks\":2,\"last_update\":11}]},"
    "\"inventory\":{\"size\":2,\"slots\":["
      "{\"index\":0,\"empty\":true,\"count\":0,\"equipped\":false,"
      "\"required_tags\":[]},"
      "{\"index\":1,\"empty\":false,\"count\":3,\"item_id\":\"turnip\","
      "\"equipped\":false,\"required_tags\":[],"
      "\"display_name\":\"Turnip\",\"description\":\"A root vegetable\","
      "\"instance_metadata\":{\"item_id\":\"turnip\",\"inner_item\":null,"
      "\"gold_to_gain\":null,\"cosmetic\":null,\"animal_cosmetic\":null,"
      "\"infusion\":null,\"auto_use\":false,\"pet_cosmetic_set_name\":null,"
      "\"date_photo\":null},\"instance_fingerprint\":\"{\\\"item_id\\\":\\\"turnip\\\"}\","
      "\"use\":0,\"use_name\":\"consume\",\"can_consume_from_bridge\":true,"
      "\"ui_category\":\"food\",\"tags\":[\"crop\",\"food\"],"
      "\"icon_asset\":\"spr_item_turnip\",\"icon_frame\":0,"
      "\"sub_icon_asset\":null,\"icon_lut\":{\"asset\":\"lut_silver\",\"index\":2},"
      "\"museum_eligible\":true,\"museum_donated\":false,"
      "\"max_stack\":99,\"damage\":null,"
      "\"defense\":null,\"stamina_modifier\":2,\"max_stamina_modifier\":0,"
      "\"health_modifier\":0,\"max_health_modifier\":0,\"mana_modifier\":0,"
      "\"quality\":1,\"stars\":0,\"value_bin\":30,\"value_store\":50}]},"
    "\"armor\":{\"size\":1,\"slots\":[{\"index\":0,\"empty\":true,"
      "\"count\":0,\"equipped\":true,\"required_tags\":[\"head\"]}]},"
      "\"hotbar\":{\"selected_index\":1},"
    "\"location\":{\"id\":\"farm\",\"dynamic_index\":0,\"room\":\"rm_farm\","
      "\"diagnostic_name\":\"Farm/0\",\"display_name\":\"Test Farm\",\"outdoor\":true},"
    "\"player\":{\"x\":123.5,\"y\":456.25,\"facing_degrees\":90,"
      "\"facing\":\"east\",\"context\":\"mines\",\"mine\":{\"active\":true,"
      "\"floor_index\":4,\"display_floor\":5,\"biome_id\":1,"
      "\"biome_name\":\"Upper Mines\"}},"
    "\"map\":{\"selected_map_id\":\"town\",\"base_map_id\":\"town\","
      "\"pages\":[{\"id\":\"town\",\"display_name\":\"Mistria\","
      "\"locked\":false,\"map_asset\":\"spr_ui_map_town\","
      "\"tab_icon_asset\":\"spr_ui_map_tab_icon_town\",\"hubs\":[{"
      "\"id\":\"town:0\",\"index\":0,\"type\":\"position\","
      "\"world_x\":100,\"world_y\":200,\"map_x\":12,\"map_y\":34,"
      "\"locations\":[\"town\"],\"markers\":[{\"kind\":\"npc\","
      "\"npc_id\":\"adina\",\"icon_asset\":\"spr_adina\",\"hub_index\":0,"
      "\"offset_x\":-5.5,\"offset_y\":-5.5},{\"kind\":\"unknown_npc\","
      "\"npc_id\":null,\"icon_asset\":\"spr_unknown\",\"hub_index\":0,"
      "\"offset_x\":5.5,\"offset_y\":-5.5},{\"kind\":\"player\","
      "\"npc_id\":null,\"icon_asset\":\"spr_player\",\"hub_index\":0,"
      "\"offset_x\":-5.5,\"offset_y\":5.5}]}],\"player_hub_id\":\"town:0\"}],"
      "\"authored_coordinates\":true,\"continuous_world_projection_supported\":false},"
    "\"villagers\":{\"villagers\":[{\"id\":\"adina\",\"display_name\":\"Adeline\","
      "\"icon_asset\":\"spr_adina_small\",\"portrait_asset\":\"spr_adina_neutral\","
      "\"portrait_offset\":[-3,5],\"portrait_background_rgb\":[20,40,60],"
      "\"heart_points\":275,\"heart_level\":2,\"relationship\":\"single\","
      "\"talked_today\":true,\"gifted_today\":false,\"location\":{\"id\":\"town\","
      "\"dynamic_index\":0,\"x\":321,\"y\":222}}],\"count\":1,"
      "\"discovery_policy\":\"unlocked_and_met_only\","
      "\"gift_preferences_exposed\":false},"
    "\"animals\":{\"livestock\":[{\"id\":77,\"category\":\"livestock\","
      "\"name\":\"Moo\",\"kind\":\"cow\",\"kind_name\":\"Cow\","
      "\"variant\":\"red\",\"icon_asset\":\"spr_cow\","
      "\"icon_lut_asset\":\"lut_red\",\"icon_lut_index\":1,"
      "\"sex\":\"female\",\"baby\":false,\"days_old\":40,"
      "\"heart_points\":320,\"heart_level\":3,\"in_daycare\":true,\"at_home\":false,"
      "\"outside_today\":false,\"incubating\":false,\"location\":null,"
      "\"attention\":{\"active\":false,\"fed_today\":null,\"petted_today\":null,"
      "\"needs_food\":false,\"needs_pet\":false,\"needed\":false}}],"
      "\"livestock_count\":1,\"pet\":{\"id\":-1,\"category\":\"pet\","
      "\"name\":\"Miso\",\"variant\":\"cat_tabby\",\"kind_id\":0,"
      "\"icon_asset\":\"spr_cat\",\"map_icon_asset\":\"spr_cat_map\","
      "\"icon_lut_asset\":null,\"icon_lut_index\":null,"
      "\"sex\":\"female\",\"heart_points\":120,\"heart_level\":1,"
      "\"location\":{\"id\":\"farm\",\"dynamic_index\":null,\"x\":null,\"y\":null},"
      "\"attention\":{\"active\":true,\"fed_today\":true,\"petted_today\":false,"
      "\"needs_food\":false,\"needs_pet\":true,\"needed\":true}}},"
    "\"calendar\":{\"raw\":5,\"year\":0,\"display_year\":1,\"season\":\"spring\","
      "\"season_index\":0,\"day\":4,\"display_day\":5,\"weekday\":4,\"total_days\":5},"
    "\"clock\":{\"raw\":600,\"hour\":10,\"minute\":0,\"formatted\":\"10:00 AM\","
      "\"is_day\":true,\"is_night\":false},"
    "\"weather\":{\"current\":\"sunny\",\"current_id\":0,\"tomorrow\":\"rain\","
      "\"tomorrow_id\":1,\"inclement\":false,\"under_spell\":false},"
    "\"quests\":{\"active\":[{\"id\":\"q1\",\"name\":\"A Quest\","
      "\"description\":\"Do it\",\"category\":1,\"current_stage\":0,"
      "\"stage_count\":2,\"objective\":\"Go home\",\"requirements\":[{"
      "\"type\":\"item\",\"source\":\"inventory\",\"source_location\":null,"
      "\"item_id\":\"turnip\",\"display_name\":\"Turnip\","
      "\"icon_asset\":\"spr_item_turnip\",\"required\":5,\"current\":3,"
      "\"complete\":false},{\"type\":\"met_npcs\",\"required\":2,"
      "\"current\":1,\"complete\":false,\"identities_exposed\":false}],"
      "\"targets\":[{\"type\":\"npc\",\"npc_id\":\"adina\",\"location\":null},"
      "{\"type\":\"location\",\"location_id\":\"farm\"},{\"type\":\"manual\"}],"
      "\"ready\":false,\"npc_for_icon\":{\"id\":\"adina\","
      "\"icon_asset\":\"spr_adina_portrait\"},\"rewards\":[{\"type\":\"gold\","
      "\"amount\":100},{\"type\":\"item\",\"item_id\":\"turnip\","
      "\"display_name\":\"Turnip\",\"icon_asset\":\"spr_item_turnip\","
      "\"amount\":1,\"instance_metadata\":{\"item_id\":\"turnip\","
      "\"inner_item\":null,\"gold_to_gain\":null,\"cosmetic\":null,"
      "\"animal_cosmetic\":null,\"infusion\":null,\"auto_use\":false,"
      "\"pet_cosmetic_set_name\":null,\"date_photo\":null}}]}],"
      "\"active_count\":1,\"completed_count\":2,\"tracked_supported\":false},"
    "\"magic\":{\"spells\":[{\"index\":7,\"id\":\"fire_breath\",\"display_name\":\"Dragon Breath\","
      "\"type\":\"offensive\",\"description\":\"Breathe fire\","
      "\"upper_icon_asset\":\"spr_fire_upper\",\"lower_icon_key\":\"fire_lower\","
      "\"lower_icon_asset\":\"spr_fire_lower_enabled\",\"ribbon_asset\":\"spr_fire_ribbon\",\"mana_cost\":8,"
      "\"display_cost_orbs\":2}],\"unlocked_count\":1,\"pinned_spell\":\"fire_breath\","
      "\"pinned_spell_index\":7,\"cooldowns\":[{\"index\":7,\"id\":\"fire_breath\","
      "\"remaining_frames\":30,\"ready\":false}],"
      "\"actions_supported\":true,\"supported_actions\":[\"pin_spell\","
      "\"unpin_spell\",\"cast_pinned_spell\"]},"
    "\"almanac\":{\"categories\":[{\"key\":\"crops\",\"display_name\":\"Crops\","
      "\"discovered_count\":1,\"total_count\":10,\"discovered_items\":[{\"id\":\"turnip\","
      "\"display_name\":\"Turnip\",\"description\":\"A root vegetable\","
      "\"icon_asset\":\"spr_item_turnip\",\"icon_frame\":0,\"donated\":false}]}],"
      "\"discovered_count\":1,\"total_count\":10,\"unknown_entries_exposed\":false},"
    "\"museum\":{\"wings\":[{\"id\":\"flora\",\"display_name\":\"Flora\","
      "\"discovered_count\":1,\"donated_count\":1,\"total_count\":1,\"sets\":[{"
      "\"key\":\"spring\",\"display_name\":\"Spring\",\"description\":\"Spring flora\","
      "\"discovered_count\":1,\"donated_count\":1,\"total_count\":1,\"complete\":true,"
      "\"known_items\":[{\"id\":\"turnip\",\"display_name\":\"Turnip\","
      "\"description\":\"A root vegetable\",\"icon_asset\":\"spr_item_turnip\","
      "\"icon_frame\":0,\"donated\":true}]}]}],"
      "\"discovered_count\":1,\"donated_count\":1,\"total_count\":1,"
      "\"unknown_entries_exposed\":false,\"future_rewards_exposed\":false},"
    "\"renown\":{\"total\":125,\"pending\":5,\"projected_total\":130,"
      "\"projected_level\":3,\"level\":2,\"max_level\":50,\"at_max\":false,"
      "\"level_start_total\":100,\"next_level_total\":150,\"progress\":25,"
      "\"progress_required\":50,\"progress_ratio\":0.5,\"rank\":{\"rank_index\":0,"
      "\"display_name\":\"Newcomer\"},\"next_rank\":{\"rank_index\":1,\"level\":5,"
      "\"display_name\":\"Helper\"},\"claimable_rewards\":{\"size\":0,\"slots\":[]},"
      "\"future_rewards_exposed\":false,\"actions_supported\":false},"
    "\"calendar_events\":{\"horizon_days\":28,\"events\":[{\"type\":\"festival\","
      "\"id\":\"spring_festival\",\"display_name\":\"Spring Festival\","
      "\"days_from_now\":3,\"raw_date\":8,\"year\":0,\"display_year\":1,"
      "\"season\":\"spring\",\"season_index\":0,\"day\":7,\"display_day\":8,"
      "\"weekday\":0}],\"count\":1,\"private_date_availability_exposed\":false,"
      "\"unreleased_festivals_exposed\":false},"
    "\"future_topic\":{\"nested\":[1,true,null,\"x\"]}}}";
}

std::string StatusDelta(unsigned sequence, unsigned base, unsigned revision,
                        double health) {
  return std::string("{") +
    "\"protocol\":\"fields-of-mistria-dualscreen\",\"schema\":1,"
    "\"epoch\":\"save-epoch\",\"sequence\":" + std::to_string(sequence) +
    ",\"full\":false,\"base_sequence\":" + std::to_string(base) +
    ",\"generated_tick\":104,\"base_revisions\":" + Revisions(revision - 1) +
    ",\"revisions\":" + Revisions(revision) + ",\"topics\":{\"status\":{"
    "\"name\":\"Mistria\",\"farm_name\":\"Test\",\"health\":" +
    std::to_string(health) +
    ",\"health_max\":100,\"stamina\":90,\"stamina_max\":110,\"mana\":12,"
    "\"mana_max\":20,\"tesserae\":345,\"essence\":7,\"renown\":4}}}";
}

std::string PlayerDelta(unsigned sequence, unsigned base,
                        unsigned player_revision) {
  return std::string("{") +
    "\"protocol\":\"fields-of-mistria-dualscreen\",\"schema\":1,"
    "\"epoch\":\"save-epoch\",\"sequence\":" + std::to_string(sequence) +
    ",\"full\":false,\"base_sequence\":" + std::to_string(base) +
    ",\"generated_tick\":108,\"base_revisions\":" +
    Revisions(2, player_revision - 1) + ",\"revisions\":" +
    Revisions(2, player_revision) + ",\"topics\":{\"player\":{"
    "\"x\":200,\"y\":300,\"facing_degrees\":180,\"facing\":\"south\","
    "\"context\":\"world\",\"mine\":{\"active\":false,"
    "\"floor_index\":null,\"display_floor\":null,\"biome_id\":null,"
    "\"biome_name\":null}}}}";
}

int Fail(const char* why) { std::fprintf(stderr, "LiveStateTests: %s\n", why); return 1; }

}  // namespace

int main() {
  wchar_t temp[MAX_PATH] = {};
  if (!GetTempPathW(MAX_PATH, temp)) return Fail("GetTempPath");
  wchar_t root[MAX_PATH] = {};
  std::swprintf(root, MAX_PATH, L"%sfomds-live-state-%lu-%llu", temp,
                static_cast<unsigned long>(GetCurrentProcessId()),
                static_cast<unsigned long long>(GetTickCount64()));
  if (!CreateDirectoryW(root, nullptr)) return Fail("CreateDirectory root");
  const std::wstring dual = Join(root, L"dualscreen");
  if (!CreateDirectoryW(dual.c_str(), nullptr)) return Fail("CreateDirectory dualscreen");
  const std::wstring state = Join(dual, L"state.json");
  const std::wstring old = Join(dual, L"state.old.json");
  const std::wstring ack_path = Join(dual, L"ack.json");
  const std::wstring ack_old = Join(dual, L"ack.old.json");

  fomds::LiveStateIngestor reader(root);
  if (reader.Poll() != fomds::LivePollResult::kNoFile)
    return Fail("missing file result");
  if (!Write(state, Full()) || reader.Poll() != fomds::LivePollResult::kPublished)
    return Fail("full publish");
  auto snapshot_storage = std::make_unique<fomds::LiveStateSnapshot>();
  fomds::LiveStateSnapshot& snapshot = *snapshot_storage;
  if (!reader.CopyLatest(&snapshot) || snapshot.sequence != 1 ||
      snapshot.present_topics != (1u << fomds::kLiveTopicCount) - 1u ||
      snapshot.status.health != 80 || snapshot.inventory.slot_count != 2 ||
      snapshot.inventory.slots[1].empty ||
      std::strcmp(snapshot.inventory.slots[1].item_id.value, "turnip") != 0 ||
      !snapshot.inventory.slots[1].max_stack.present ||
      snapshot.inventory.slots[1].max_stack.value != 99 ||
      snapshot.inventory.slots[1].required_tag_count != 0 ||
      snapshot.inventory.slots[1].tag_count != 2 ||
      !snapshot.inventory.slots[1].can_consume_from_bridge ||
      std::strcmp(snapshot.inventory.slots[1].instance_fingerprint.value,
                  "{\"item_id\":\"turnip\"}") != 0 ||
      !snapshot.inventory.slots[1].icon_lut_present ||
      snapshot.inventory.slots[1].icon_lut_index != 2 ||
      snapshot.armor.slot_count != 1 || !snapshot.armor.slots[0].equipped ||
      snapshot.armor.slots[0].required_tag_count != 1 ||
      snapshot.progression.skill_count != 1 ||
      std::strcmp(snapshot.progression.skills[0].icon_asset.value,
                  "spr_skill_farming") != 0 ||
      snapshot.progression.status_effect_count != 1 ||
      snapshot.progression.status_effects[0].source_index != 1 ||
      snapshot.player.x != 123.5 || !snapshot.player.mine.active ||
      !snapshot.player.mine.display_floor.present ||
      snapshot.player.mine.display_floor.value != 5 ||
      snapshot.map.page_count != 1 || snapshot.map.hub_count != 1 ||
      snapshot.map.hub_location_count != 1 || snapshot.map.marker_count != 3 ||
      snapshot.map.markers[1].npc_id.present ||
      !snapshot.map.pages[0].player_hub_id.present ||
      snapshot.villagers.villager_count != 1 ||
      std::strcmp(snapshot.villagers.villagers[0].display_name.value,
                  "Adeline") != 0 ||
      snapshot.villagers.villagers[0].portrait_offset[0] != -3 ||
      snapshot.villagers.villagers[0].portrait_background_rgb[2] != 60 ||
      snapshot.villagers.gift_preferences_exposed ||
      snapshot.animals.livestock_count != 1 ||
      !snapshot.animals.livestock[0].in_daycare ||
      snapshot.animals.livestock[0].location.present ||
      snapshot.animals.livestock[0].attention.fed_today.present ||
      !snapshot.animals.livestock[0].icon_lut_index.present ||
      !snapshot.animals.pet.present || snapshot.animals.pet.location.x.present ||
      snapshot.animals.pet.icon_lut_index.present ||
      !snapshot.animals.pet.attention.needs_pet ||
      snapshot.magic.spell_count != 1 || snapshot.magic.spells[0].index != 7 ||
      !snapshot.magic.pinned_spell.present || !snapshot.magic.actions_supported ||
      !snapshot.magic.pinned_spell_index.present ||
      snapshot.magic.pinned_spell_index.value != 7 ||
      snapshot.magic.cooldowns[0].index != 7 ||
      !snapshot.magic.supports_pin_spell ||
      !snapshot.magic.supports_cast_pinned_spell ||
      snapshot.quests.requirement_count != 2 ||
      snapshot.quests.target_count != 3 || snapshot.quests.reward_count != 2 ||
      snapshot.quests.requirements[1].identities_exposed.value ||
      snapshot.almanac.item_count != 1 ||
      snapshot.almanac.unknown_entries_exposed || snapshot.museum.wing_count != 1 ||
      snapshot.museum.set_count != 1 || snapshot.museum.item_count != 1 ||
      !snapshot.museum.known_items[0].donated ||
      !snapshot.renown.next_rank_present || snapshot.renown.progress_ratio != 0.5 ||
      snapshot.renown.actions_supported || snapshot.calendar_events.event_count != 1 ||
      snapshot.calendar_events.private_date_availability_exposed ||
      std::strcmp(snapshot.status.name.value, "Mistria \xe2\x98\x85") != 0)
    return Fail("typed full snapshot");

  fomds::LiveStateIngestor replacement_reader(root);
  if (replacement_reader.Poll() != fomds::LivePollResult::kPublished ||
      !Write(state, Full(2)) ||
      replacement_reader.Poll() != fomds::LivePollResult::kPublished ||
      !replacement_reader.CopyLatest(&snapshot) || snapshot.sequence != 2 ||
      snapshot.almanac.item_count != 1 || snapshot.museum.set_count != 1 ||
      snapshot.museum.item_count != 1)
    return Fail("complete collection topics replace rather than append");
  if (!Write(state, Full()) ||
      reader.Poll() != fomds::LivePollResult::kNoChange)
    return Fail("unconsumed same-sequence full is no change");

  Sleep(2);
  if (!Write(state, StatusDelta(2, 1, 2, 55)) ||
      reader.Poll() != fomds::LivePollResult::kPublished ||
      !reader.CopyLatest(&snapshot) || snapshot.sequence != 2 ||
      snapshot.status.health != 55 || snapshot.inventory.slot_count != 2)
    return Fail("delta merge");

  Sleep(2);
  if (!Write(state, PlayerDelta(3, 2, 2)) ||
      reader.Poll() != fomds::LivePollResult::kPublished ||
      !reader.CopyLatest(&snapshot) || snapshot.sequence != 3 ||
      snapshot.player.x != 200 || snapshot.player.mine.active ||
      snapshot.player.mine.floor_index.present || snapshot.status.health != 55 ||
      snapshot.revisions[static_cast<std::size_t>(fomds::LiveTopic::kPlayer)] != 2)
    return Fail("player delta merge and optional mine fields");

  if (!Write(old, PlayerDelta(3, 2, 2)) ||
      !Write(state, "{\"protocol\":" ) ||
      reader.Poll() != fomds::LivePollResult::kNoChange ||
      !reader.CopyLatest(&snapshot) || snapshot.sequence != 3)
    return Fail("stale atomic fallback is no change");

  Sleep(2);
  if (!Write(state, StatusDelta(5, 4, 3, 1)) ||
      reader.Poll() != fomds::LivePollResult::kNeedsResync || !reader.NeedsResync() ||
      !reader.CopyLatest(&snapshot) || snapshot.sequence != 3)
    return Fail("delta gap rejection");

  // A partial current replacement must not destroy the cache; the previous
  // valid atomic-write fallback is accepted by a new reader.
  fomds::LiveStateIngestor fallback_reader(root);
  if (!Write(old, Full()) || !Write(state, "{\"protocol\":" ) ||
      fallback_reader.Poll() != fomds::LivePollResult::kPublished ||
      !fallback_reader.CopyLatest(&snapshot) || snapshot.sequence != 1)
    return Fail("partial current fallback");

  wchar_t discovered[MAX_PATH] = {};
  if (!fallback_reader.CopyStatePath(discovered, MAX_PATH) ||
      std::wstring(discovered) != state)
    return Fail("copy path");

  const std::string ack =
      "{\"protocol\":\"fields-of-mistria-dualscreen\",\"schema\":1,"
      "\"epoch\":\"save-epoch\",\"state_sequence\":1,"
      "\"command_sequence\":900,\"action\":\"select_hotbar\","
      "\"ok\":true,\"code\":\"ok\",\"future\":{\"x\":[1,null]}}";
  fomds::BridgeActionAck parsed_ack;
  if (!Write(ack_path, ack) ||
      fallback_reader.PollAcknowledgement(&parsed_ack) !=
          fomds::LivePollResult::kPublished ||
      !parsed_ack.validated || parsed_ack.command_sequence != 900 ||
      parsed_ack.action != "select_hotbar" || !parsed_ack.ok ||
      GetFileAttributesW(ack_path.c_str()) == INVALID_FILE_ATTRIBUTES)
    return Fail("validated non-destructive ack");
  if (fallback_reader.PollAcknowledgement(&parsed_ack) !=
      fomds::LivePollResult::kNoChange)
    return Fail("unchanged ack metadata cache");

  // A partial ack replacement falls back to the prior complete bridge file.
  if (!Write(ack_old, ack) || !Write(ack_path, "{\"protocol\":"))
    return Fail("write ack fallback fixture");
  parsed_ack = {};
  if (fallback_reader.PollAcknowledgement(&parsed_ack) !=
          fomds::LivePollResult::kPublished ||
      !parsed_ack.validated || parsed_ack.command_sequence != 900)
    return Fail("partial ack fallback");

  // The reader caps ack input before parsing and leaves it untouched.
  DeleteFileW(ack_old.c_str());
  if (!Write(ack_path, std::string(17 * 1024, 'x')) ||
      fallback_reader.PollAcknowledgement(&parsed_ack) !=
          fomds::LivePollResult::kIoError ||
      GetFileAttributesW(ack_path.c_str()) == INVALID_FILE_ATTRIBUTES)
    return Fail("oversized ack cap");

  DeleteFileW(state.c_str());
  DeleteFileW(old.c_str());
  DeleteFileW(ack_path.c_str());
  DeleteFileW(ack_old.c_str());
  RemoveDirectoryW(dual.c_str());
  RemoveDirectoryW(root);
  std::puts("LiveStateTests passed");
  return 0;
}
