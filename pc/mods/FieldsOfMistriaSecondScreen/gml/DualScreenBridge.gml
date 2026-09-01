#macro DUALSCREEN_SCHEMA_VERSION 1
#macro DUALSCREEN_PROTOCOL_NAME "fields-of-mistria-dualscreen"
#macro DUALSCREEN_DIRECTORY (CONFIG_DIRECTORY + "/dualscreen")
#macro DUALSCREEN_STATE_PATH (DUALSCREEN_DIRECTORY + "/state.json")
#macro DUALSCREEN_STATE_NEW_PATH (DUALSCREEN_DIRECTORY + "/state.new.json")
#macro DUALSCREEN_STATE_OLD_PATH (DUALSCREEN_DIRECTORY + "/state.old.json")
#macro DUALSCREEN_COMMAND_PATH (DUALSCREEN_DIRECTORY + "/command.json")
#macro DUALSCREEN_COMMAND_CLAIMED_PATH (DUALSCREEN_DIRECTORY + "/command.claimed.json")
#macro DUALSCREEN_ACK_PATH (DUALSCREEN_DIRECTORY + "/ack.json")
#macro DUALSCREEN_ACK_NEW_PATH (DUALSCREEN_DIRECTORY + "/ack.new.json")
#macro DUALSCREEN_ACK_OLD_PATH (DUALSCREEN_DIRECTORY + "/ack.old.json")

// Small independent state channel for the dual-screen process. It never reads or
// writes a game save and it executes all game-state access on the GML thread.

function dualscreen_atomic_json_write(path, new_path, old_path, value) {
    if file_exists(old_path) {
        file_delete(old_path);
    }
    if file_exists(new_path) {
        file_delete(new_path);
    }
    if file_exists(path) {
        file_rename(path, old_path);
    }

    save_text_file(new_path, json_stringify(value));
    file_rename(new_path, path);
}

function dualscreen_item_use_name(use) {
    if use == undefined {
        return undefined;
    }
    switch use {
        case ItemUse.Attack: return "attack";
        case ItemUse.UseTool: return "use_tool";
        case ItemUse.Blueprint: return "blueprint";
        case ItemUse.PlantSeed: return "plant_seed";
        case ItemUse.PlantSapling: return "plant_sapling";
        case ItemUse.PlantGrass: return "plant_grass";
        case ItemUse.PlaceObject: return "place_object";
        case ItemUse.Wallpaper: return "wallpaper";
        case ItemUse.Flooring: return "flooring";
        case ItemUse.Consume: return "consume";
        case ItemUse.ApplyOil: return "apply_oil";
        case ItemUse.PlaceTile: return "place_tile";
        case ItemUse.UnlockCosmetic: return "unlock_cosmetic";
        case ItemUse.LearnRecipe: return "learn_recipe";
        case ItemUse.IdentifyItem: return "identify_item";
        case ItemUse.OpenChest: return "open_chest";
        case ItemUse.GainGold: return "gain_gold";
        case ItemUse.GainGoldInstant: return "gain_gold_instant";
        case ItemUse.ExpandInventory: return "expand_inventory";
        case ItemUse.UnlockAnimalCosmetic: return "unlock_animal_cosmetic";
        case ItemUse.CrackEgg: return "crack_egg";
        case ItemUse.UnlockPetCosmetic: return "unlock_pet_cosmetic";
        case ItemUse.CrackEssenceStone: return "crack_essence_stone";
        case ItemUse.Bomb: return "bomb";
        case ItemUse.UnlockDate: return "unlock_date";
        case ItemUse.UnpackBundle: return "unpack_bundle";
        case ItemUse.UnlockPetSkin: return "unlock_pet_skin";
        case ItemUse.Bait: return "bait";
        case ItemUse.UnlockSong: return "unlock_song";
        default: return undefined;
    }
}

function dualscreen_item_ui_category(item) {
    var tags = item.prototype.tags;
    if tags.contains("head")
        || tags.contains("chest")
        || tags.contains("legs")
        || tags.contains("boots")
        || tags.contains("accessory")
    {
        return "equipment";
    }
    switch item.prototype.use {
        case ItemUse.Attack:
        case ItemUse.UseTool: return "tool";
        case ItemUse.Consume: return "consumable";
        case ItemUse.PlantSeed:
        case ItemUse.PlantSapling:
        case ItemUse.PlantGrass: return "seed";
        case ItemUse.Blueprint:
        case ItemUse.PlaceObject:
        case ItemUse.Wallpaper:
        case ItemUse.Flooring:
        case ItemUse.PlaceTile: return "placeable";
        case ItemUse.UnlockCosmetic:
        case ItemUse.LearnRecipe:
        case ItemUse.IdentifyItem:
        case ItemUse.OpenChest:
        case ItemUse.GainGold:
        case ItemUse.GainGoldInstant:
        case ItemUse.ExpandInventory:
        case ItemUse.UnlockAnimalCosmetic:
        case ItemUse.CrackEgg:
        case ItemUse.UnlockPetCosmetic:
        case ItemUse.CrackEssenceStone:
        case ItemUse.UnlockDate:
        case ItemUse.UnpackBundle:
        case ItemUse.UnlockPetSkin:
        case ItemUse.UnlockSong: return "unlockable";
        default: return "item";
    }
}

function dualscreen_optional_asset_name(asset) {
    return asset == undefined ? undefined : asset_to_string(asset);
}

// Not every gameplay location has its own localization key. Interior and
// prologue locations such as Adeline's office intentionally inherit their map
// location's identity, so never pass an optional `name` field to local_get().
function dualscreen_location_display_name(id) {
    if id == LocationId.Farm {
        return ARI.farm_name;
    }

    var location = LOCATIONS[id];
    if location == undefined {
        return location_id_to_string(id);
    }
    if location.name != undefined {
        return local_get(location.name);
    }

    var map_id = location.map_location;
    if map_id != undefined && map_id != id {
        if map_id == LocationId.Farm {
            return ARI.farm_name;
        }
        var map_location = LOCATIONS[map_id];
        if map_location != undefined && map_location.name != undefined {
            return local_get(map_location.name);
        }
    }

    return location_id_to_string(id);
}

function dualscreen_flatten_slot(slot, equipped=false) {
    if slot.item == undefined || slot.count <= 0 {
        return {
            index: slot.index,
            empty: true,
            count: 0,
            equipped: equipped,
            required_tags: slot.required_tags.to_array(),
        };
    }

    var item = slot.item;
    var prototype = item.prototype;
    var lut = item.get_ui_lut();
    return {
        index: slot.index,
        empty: false,
        count: slot.count,
        equipped: equipped,
        required_tags: slot.required_tags.to_array(),
        item_id: item_id_to_string(item.item_id),
        display_name: item.get_display_name(),
        description: item.get_display_description(),
        // v1.0.4 LiveItem.serialize() returns only bounded instance metadata:
        // IDs, infusion/cosmetic variants, gold payload, auto-use, and date photo.
        instance_metadata: item.serialize(),
        instance_fingerprint: json_stringify(item.serialize()),
        use: prototype.use,
        use_name: dualscreen_item_use_name(prototype.use),
        can_consume_from_bridge: prototype.use == ItemUse.Consume && !item.auto_use,
        ui_category: dualscreen_item_ui_category(item),
        tags: prototype.tags.to_array(),
        icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
        icon_frame: 0,
        sub_icon_asset: dualscreen_optional_asset_name(item.get_ui_sub_icon()),
        icon_lut: lut == undefined ? undefined : {
            asset: asset_to_string(lut.lut),
            index: lut.index,
        },
        museum_eligible: MUSEUM_DATA.museum_items[item.item_id],
        museum_donated: MUSEUM_DATA.museum_items[item.item_id]
            ? MUSEUM_PROGRESS[item.item_id]
            : false,
        max_stack: prototype.max_stack,
        damage: prototype.damage,
        defense: prototype.defense,
        stamina_modifier: prototype.stamina_modifier,
        max_stamina_modifier: prototype.max_stamina_modifier,
        health_modifier: prototype.health_modifier,
        max_health_modifier: prototype.max_health_modifier,
        mana_modifier: prototype.mana_modifier,
        quality: prototype.quality,
        stars: prototype.stars,
        value_bin: prototype.value.bin,
        value_store: prototype.value.store,
    };
}

function dualscreen_flatten_inventory(inventory, equipped=false) {
    var output = array_create(inventory.size(), undefined);
    for (var i = 0; i < inventory.size(); i++) {
        output[i] = dualscreen_flatten_slot(inventory.slot(i), equipped);
    }
    return output;
}

function dualscreen_status_detail_fingerprint() {
    return json_stringify({
        skill_xp: ARI.skill_xp,
        perks: ARI.perks,
        perks_active: ARI.perks_active,
        effects: ARI.status_effects.serialize(),
    });
}

function dualscreen_build_status() {
    return {
        name: ARI.name,
        farm_name: ARI.farm_name,
        health: ARI.get_health(),
        health_max: ARI.get_max_health(),
        stamina: ARI.get_stamina(),
        stamina_max: ARI.get_max_stamina(),
        mana: ARI.get_mana(),
        mana_max: ARI.mana_max,
        tesserae: ARI.get_gold(),
        essence: ARI.get_essence(),
        renown: ARI.renown,
    };
}

function dualscreen_build_progression() {
    var skills = array_create(Skill.LEN, undefined);
    for (var i = 0; i < Skill.LEN; i++) {
        skills[i] = {
            id: skill_to_string(i),
            xp: ARI.skill_xp[i],
            level: ARI.level(i),
            // SkillProgressUi resolves this same source-owned Fiddle field.
            icon_asset: asset_to_string(string_to_asset(
                fiddle_get(format("skills/{Skill}/sprite", i)),
            )),
        };
    }

    var perks = [];
    for (var i = 0; i < Perk.LEN; i++) {
        if ARI.perks[i] {
            array_push(perks, {
                id: perk_to_string(i),
                active: ARI.perks_active[i],
            });
        }
    }

    return {
        skills: skills,
        perks: perks,
        status_effects: ARI.status_effects.serialize(),
    };
}

function dualscreen_build_location() {
    var id = CURRENT_LOCATION_ID;

    return {
        id: location_id_to_string(id),
        dynamic_index: CURRENT_DYN_INDEX,
        room: asset_to_string(room()),
        diagnostic_name: display_location(id, CURRENT_DYN_INDEX, room()),
        display_name: dualscreen_location_display_name(id),
        outdoor: LOCATIONS[id].outdoor,
    };
}

function dualscreen_flatten_location_position(location_position) {
    if location_position == undefined {
        return undefined;
    }
    return {
        id: location_id_to_string(location_position.location_id),
        dynamic_index: location_position.dyn_index,
        x: location_position.pos.x,
        y: location_position.pos.y,
    };
}

function dualscreen_player_is_in_mines() {
    return DUNGEON_RUNNER != undefined && is_dungeon_room(room());
}

function dualscreen_build_player() {
    var in_mines = dualscreen_player_is_in_mines();
    var mine = {
        active: false,
        floor_index: undefined,
        display_floor: undefined,
        biome_id: undefined,
        biome_name: undefined,
    };
    if in_mines {
        mine = {
            active: true,
            floor_index: DUNGEON_FLOOR,
            // MinesMenu.gml displays DUNGEON_FLOOR + 1.
            display_floor: DUNGEON_FLOOR + 1,
            biome_id: DUNGEON_BIOME,
            biome_name: local_get(DUNGEON.biomes[DUNGEON_BIOME].name),
        };
    }

    return {
        x: obj_ari.x,
        y: obj_ari.y,
        facing_degrees: obj_ari.dir,
        facing: cardinal_to_string(obj_ari.cardinal),
        context: in_mines ? "mines" : "world",
        mine: mine,
    };
}

function dualscreen_current_base_map_id() {
    var location_id = TAXI.current_location_id();
    if location_id == undefined {
        location_id = CURRENT_LOCATION_ID;
    }
    return LOCATIONS[location_id].map_location;
}

function dualscreen_find_map_hub_index(hubs, location_position, map_id) {
    if location_position == undefined {
        return undefined;
    }
    var winner = undefined;
    var winning_priority = -infinity;
    for (var i = 0; i < array_length(hubs); i++) {
        var hub = hubs[i];
        var priority = undefined;
        switch hub.type {
            case MapHub.Position:
                if location_position.location_id == map_id {
                    priority = -point_distance(
                        hub.x,
                        hub.y,
                        location_position.pos.x,
                        location_position.pos.y,
                    );
                }
                break;
            case MapHub.Locations:
                if array_contains(hub.locations, location_position.location_id) {
                    priority = 0;
                }
                break;
        }
        if priority != undefined && (winner == undefined || priority > winning_priority) {
            winner = i;
            winning_priority = priority;
        }
    }
    return winner;
}

function dualscreen_map_marker(kind, icon_asset, hub_index, npc_id=undefined) {
    return {
        kind: kind,
        npc_id: npc_id,
        icon_asset: asset_to_string(icon_asset),
        hub_index: hub_index,
        offset_x: 0,
        offset_y: 0,
    };
}

function dualscreen_build_map_page(map_id) {
    var locked = map_id == LocationId.DeepWoods
        && !world_mod_enabled(WorldMod.ThornyStairs);
    var map_asset = locked
        ? spr_ui_map_locked
        : string_to_asset(fmt("spr_ui_map_{LocationId}", map_id));
    if map_id == LocationId.Farm && FARM_EXPANDED {
        map_asset = spr_ui_map_farm_expansion;
    }

    var source_hubs = MAP_HUBS[map_id];
    var hubs = array_create(array_length(source_hubs), undefined);
    for (var i = 0; i < array_length(source_hubs); i++) {
        var source_hub = source_hubs[i];
        var locations = array_create(array_length(source_hub.locations), undefined);
        for (var j = 0; j < array_length(source_hub.locations); j++) {
            locations[j] = location_id_to_string(source_hub.locations[j]);
        }
        hubs[i] = {
            id: format("{}:{}", location_id_to_string(map_id), i),
            index: i,
            type: source_hub.type == MapHub.Position ? "position" : "locations",
            world_x: source_hub.x,
            world_y: source_hub.y,
            map_x: source_hub.asset_x,
            map_y: source_hub.asset_y,
            locations: locations,
            markers: [],
        };
    }

    var player_hub_index = undefined;
    // The vanilla locked Deep Woods page renders no useful geography. Do not
    // disclose its authored hubs or residents until its source gate passes.
    if !locked {
        var player_position = new LocationPosition(
            CURRENT_LOCATION_ID,
            Vec2(obj_ari.x, obj_ari.y),
            CURRENT_DYN_INDEX,
        );
        player_hub_index = dualscreen_find_map_hub_index(source_hubs, player_position, map_id);

        // MapMenu exposes unlocked-but-unmet residents with an anonymous icon.
        // Preserve that visibility while withholding their identity.
        for (var i = 0; i < NpcId.LEN; i++) {
            if !npc_is_unlocked(i) {
                continue;
            }
            var npc = NPCS[i];
            var hub_index = dualscreen_find_map_hub_index(source_hubs, npc.location_position, map_id);
            if hub_index == undefined {
                continue;
            }
            var met = npc.has_met();
            var icon = met
                ? get_small_npc_icon(i)
                : (matches(i, NpcId.Dozy, NpcId.Henrietta)
                    ? spr_ui_generic_animal_name_icon
                    : spr_ui_generic_icon_npc_small_unknown);
            array_push(
                hubs[hub_index].markers,
                dualscreen_map_marker(
                    met ? "npc" : "unknown_npc",
                    icon,
                    hub_index,
                    met ? npc_id_to_string(i) : undefined,
                ),
            );
        }
        if player_hub_index != undefined {
            array_push(
                hubs[player_hub_index].markers,
                dualscreen_map_marker(
                    "player",
                    spr_ui_generic_icon_npc_small_player,
                    player_hub_index,
                ),
            );
        }

        // Match MapMenu's 11x11, square-packed resident layout exactly.
        for (var i = 0; i < array_length(hubs); i++) {
            var markers = hubs[i].markers;
            var count = array_length(markers);
            if count == 0 {
                continue;
            }
            var box_size = ceil(sqrt(count));
            var xx = -((11 / 2) * box_size);
            var yy = -((11 / 2) * ceil(count / box_size));
            var iter = 0;
            for (var row = 0; row < box_size && iter < count; row++) {
                for (var column = 0; column < box_size && iter < count; column++) {
                    markers[iter].offset_x = xx;
                    markers[iter].offset_y = yy;
                    xx += 11;
                    iter += 1;
                }
                xx = -((11 / 2) * min(box_size, count - iter));
                yy += 11;
            }
        }
    } else {
        hubs = [];
    }

    return {
        id: location_id_to_string(map_id),
        display_name: dualscreen_location_display_name(map_id),
        locked: locked,
        map_asset: asset_to_string(map_asset),
        tab_icon_asset: asset_to_string(string_to_asset(fmt("spr_ui_map_tab_icon_{LocationId}", map_id))),
        hubs: hubs,
        player_hub_id: player_hub_index == undefined
            ? undefined
            : format("{}:{}", location_id_to_string(map_id), player_hub_index),
    };
}

function dualscreen_build_map() {
    var tab_order = fiddle_get("ui/menus/sub_menus/map").tab_order;
    var pages = [];
    for (var i = 0; i < array_length(tab_order); i++) {
        var map_id = string_to_location_id(tab_order[i]);
        if map_id == LocationId.DragonswornGlade
            && !world_mod_enabled(WorldMod.DragonswornGlade)
        {
            continue;
        }
        array_push(pages, dualscreen_build_map_page(map_id));
    }
    var base_map_id = dualscreen_current_base_map_id();
    return {
        selected_map_id: location_id_to_string(base_map_id),
        base_map_id: location_id_to_string(base_map_id),
        pages: pages,
        authored_coordinates: true,
        continuous_world_projection_supported: false,
    };
}

function dualscreen_player_map_key() {
    var map_id = dualscreen_current_base_map_id();
    var hub_index = undefined;
    if !(map_id == LocationId.DeepWoods && !world_mod_enabled(WorldMod.ThornyStairs)) {
        hub_index = dualscreen_find_map_hub_index(
            MAP_HUBS[map_id],
            new LocationPosition(
                CURRENT_LOCATION_ID,
                Vec2(obj_ari.x, obj_ari.y),
                CURRENT_DYN_INDEX,
            ),
            map_id,
        );
    }
    return format(
        "{}:{}:{}:{}:{}",
        map_id,
        hub_index,
        FARM_EXPANDED,
        world_mod_enabled(WorldMod.ThornyStairs),
        world_mod_enabled(WorldMod.DragonswornGlade),
    );
}

function dualscreen_map_fingerprint() {
    var output = local_language() + "|" + dualscreen_player_map_key();
    for (var i = 0; i < NpcId.LEN; i++) {
        if !npc_is_unlocked(i) {
            continue;
        }
        var npc = NPCS[i];
        output += format("|{}:{}", i, npc.has_met());
        if npc.location_position == undefined {
            output += ":none";
        } else {
            var loc = npc.location_position;
            output += format(
                ":{}:{}:{}:{}",
                loc.location_id,
                loc.dyn_index,
                loc.pos.x,
                loc.pos.y,
            );
        }
        if npc.has_met() {
            output += ":" + asset_to_string(get_small_npc_icon(i));
        }
    }
    return output;
}

function dualscreen_relationship_status(npc) {
    var status = T2R.read(format("{NpcId}_status", npc.id));
    if matches(status, "spouse", "fiance", "dating", "best_friend") {
        return status;
    }
    return "single";
}

function dualscreen_npc_live_location(npc) {
    var obj = npc_id_to_gm_obj_id(npc.id);
    if instance_exists(obj) {
        return {
            id: location_id_to_string(CURRENT_LOCATION_ID),
            dynamic_index: CURRENT_DYN_INDEX,
            x: obj.x,
            y: obj.y,
        };
    }
    return dualscreen_flatten_location_position(npc.location_position);
}

function dualscreen_villager_is_visible(npc_id) {
    return npc_is_unlocked(npc_id) && NPCS[npc_id].has_met();
}

function dualscreen_villager_portrait_asset(npc_id) {
    var npc = NPCS[npc_id];
    var portrait = npc.wardrobe.outfit_current.portraits.get("neutral")
        ?? npc.wardrobe.outfits.get(npc.prototype.fallback_outfit).portraits.get("neutral");
    return dualscreen_optional_asset_name(portrait);
}

function dualscreen_villagers_fingerprint() {
    // The language is part of the fingerprint because names are localized only
    // when this bounded topic is rebuilt.
    var output = local_language();
    for (var i = 0; i < NpcId.LEN; i++) {
        if !dualscreen_villager_is_visible(i) {
            continue;
        }
        var npc = NPCS[i];
        var portrait_asset = dualscreen_villager_portrait_asset(i);
        output += format(
            "|{}:{}:{}:{}:{}:{}:{}",
            i,
            npc.heart_points,
            npc.times_spoken_today,
            npc.gift_flag,
            dualscreen_relationship_status(npc),
            asset_to_string(get_small_npc_icon(i)),
            portrait_asset,
        );
        var obj = npc_id_to_gm_obj_id(i);
        if instance_exists(obj) {
            output += format(
                ":live:{}:{}:{}:{}",
                CURRENT_LOCATION_ID,
                CURRENT_DYN_INDEX,
                obj.x,
                obj.y,
            );
        } else if npc.location_position == undefined {
            output += ":none";
        } else {
            var loc = npc.location_position;
            output += format(
                ":at:{}:{}:{}:{}",
                loc.location_id,
                loc.dyn_index,
                loc.pos.x,
                loc.pos.y,
            );
        }
    }
    return output;
}

function dualscreen_build_villagers() {
    var villagers = [];
    for (var i = 0; i < NpcId.LEN; i++) {
        if !dualscreen_villager_is_visible(i) {
            continue;
        }
        var npc = NPCS[i];
        var proto = npc.prototype;
        array_push(villagers, {
            id: npc_id_to_string(i),
            display_name: local_get(proto.name),
            // RelationshipsMenu uses the small icon and the current neutral
            // wardrobe portrait, falling back to the prototype outfit.
            icon_asset: asset_to_string(get_small_npc_icon(i)),
            portrait_asset: dualscreen_villager_portrait_asset(i),
            portrait_offset: proto.journal_portrait_offset,
            portrait_background_rgb: proto.journal_background_color,
            heart_points: npc.heart_points,
            heart_level: npc.heart_level(),
            relationship: dualscreen_relationship_status(npc),
            talked_today: npc.times_spoken_today > 0,
            gifted_today: !npc.gift_flag,
            location: dualscreen_npc_live_location(npc),
        });
    }
    return {
        villagers: villagers,
        count: array_length(villagers),
        discovery_policy: "unlocked_and_met_only",
        gift_preferences_exposed: false,
    };
}

function dualscreen_animals_fingerprint(animals) {
    var output = local_language();
    for (var i = 0; i < animals.count(); i++) {
        var animal = animals.get(i);
        var in_daycare = animal.in_daycare();
        output += format(
            "|{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}:{}",
            animal.idx,
            string_length(animal.name),
            animal.name,
            animal.kind,
            animal.variant,
            animal.sex,
            animal.days_old,
            animal.heart_points,
            animal.has_eaten,
            animal.has_been_pat,
            animal.has_been_outside,
            animal.is_incubating,
            in_daycare,
            animal.is_home(),
        );
        // Daycare retains the prior location but does not represent presence.
        if !in_daycare
            && animal.instance != undefined
            && instance_exists(animal.instance)
        {
            output += format(
                ":live:{}:{}:{}:{}",
                CURRENT_LOCATION_ID,
                CURRENT_DYN_INDEX,
                animal.instance.x,
                animal.instance.y,
            );
        } else if in_daycare || animal.location_position == undefined {
            output += ":none";
        } else {
            var loc = animal.location_position;
            output += format(
                ":at:{}:{}:{}:{}",
                loc.location_id,
                loc.dyn_index,
                loc.pos.x,
                loc.pos.y,
            );
        }
    }

    if PET.unlocked() {
        output += format(
            "|pet:{}:{}:{}:{}:{}:{}:{}:{}:{}",
            string_length(PET.name),
            PET.name,
            PET.variant,
            PET.pet_kind(),
            PET.sex,
            PET.heart_points,
            PET.has_eaten,
            PET.has_been_pat,
            PET.location_id,
        );
        if instance_exists(obj_pet) {
            output += format(":at:{}:{}", obj_pet.x, obj_pet.y);
        }
    }
    return output;
}

function dualscreen_player_animal_live_location(animal, in_daycare) {
    if in_daycare {
        return undefined;
    }
    if animal.instance != undefined && instance_exists(animal.instance) {
        return {
            id: location_id_to_string(CURRENT_LOCATION_ID),
            dynamic_index: CURRENT_DYN_INDEX,
            x: animal.instance.x,
            y: animal.instance.y,
        };
    }
    return dualscreen_flatten_location_position(animal.location_position);
}

function dualscreen_flatten_player_animal(animal) {
    var in_daycare = animal.in_daycare();
    var care_active = !in_daycare;
    // AnimalMenu resolves livestock visuals through this exact helper so the
    // variant, sex, and baby/adult selection all remain source-owned.
    var icon_data = get_animal_icon_data(
        animal.kind,
        animal.variant,
        animal.sex,
        !animal.is_baby(),
    );
    return {
        id: animal.idx,
        category: "livestock",
        name: animal.name,
        kind: animal_kind_to_string(animal.kind),
        kind_name: local_get(animal.prototype.core.name),
        variant: animal.variant,
        icon_asset: dualscreen_optional_asset_name(icon_data.sprite),
        icon_lut_asset: dualscreen_optional_asset_name(icon_data.lut),
        icon_lut_index: icon_data.lut_index,
        sex: sex_to_string(animal.sex),
        baby: animal.is_baby(),
        days_old: animal.days_old,
        heart_points: animal.heart_points,
        heart_level: points_to_animal_heart_level(animal.heart_points),
        in_daycare: in_daycare,
        at_home: animal.is_home(),
        outside_today: animal.has_been_outside,
        incubating: animal.is_incubating,
        location: dualscreen_player_animal_live_location(animal, in_daycare),
        attention: {
            active: care_active,
            fed_today: care_active ? animal.has_eaten : undefined,
            petted_today: care_active ? animal.has_been_pat : undefined,
            needs_food: care_active && !animal.has_eaten,
            needs_pet: care_active && !animal.has_been_pat,
            needed: care_active && (!animal.has_eaten || !animal.has_been_pat),
        },
    };
}

function dualscreen_build_pet() {
    if !PET.unlocked() {
        return undefined;
    }
    var location = {
        id: location_id_to_string(PET.location_id),
        dynamic_index: undefined,
        x: undefined,
        y: undefined,
    };
    if instance_exists(obj_pet) {
        location.dynamic_index = CURRENT_DYN_INDEX;
        location.x = obj_pet.x;
        location.y = obj_pet.y;
    }
    var variant = PET_PROTOTYPE.variants.get(PET.variant);
    return {
        id: PET_ID,
        category: "pet",
        name: PET.name,
        variant: PET.variant,
        // AnimalMenu and MapMenu consume these exact variant-owned assets.
        icon_asset: dualscreen_optional_asset_name(variant.ui_icon),
        map_icon_asset: dualscreen_optional_asset_name(variant.map_icon),
        icon_lut_asset: dualscreen_optional_asset_name(variant.lut),
        icon_lut_index: variant.lut == undefined ? undefined : variant.lut_index,
        kind_id: PET.pet_kind(),
        sex: sex_to_string(PET.sex),
        heart_points: PET.heart_points,
        heart_level: points_to_animal_heart_level(PET.heart_points),
        location: location,
        attention: {
            active: true,
            fed_today: PET.has_eaten,
            petted_today: PET.has_been_pat,
            needs_food: !PET.has_eaten,
            needs_pet: !PET.has_been_pat,
            needed: !PET.has_eaten || !PET.has_been_pat,
        },
    };
}

function dualscreen_build_animals(animals) {
    var livestock = array_create(animals.count(), undefined);
    for (var i = 0; i < animals.count(); i++) {
        livestock[i] = dualscreen_flatten_player_animal(animals.get(i));
    }
    return {
        livestock: livestock,
        livestock_count: array_length(livestock),
        pet: dualscreen_build_pet(),
    };
}

function dualscreen_magic_static_fingerprint() {
    var output = local_language();
    for (var i = 0; i < Spell.LEN; i++) {
        if ARI.spells_learned[i] {
            output += "|" + string(i);
        }
    }
    return output;
}

function dualscreen_build_magic_static() {
    var spells = [];
    for (var i = 0; i < Spell.LEN; i++) {
        // SpellcastingMenu applies this exact discovery gate.
        if !ARI.spells_learned[i] {
            continue;
        }
        var spell = SPELLS[i];
        var lower_icon_sprites = load_button_sprites(spell.icon_key);
        array_push(spells, {
            index: i,
            id: spell_to_string(i),
            display_name: local_get(spell.name),
            type: local_get(spell.type),
            description: local_get(spell.description),
            // SpellcastingMenu uses these exact model fields for its card.
            upper_icon_asset: dualscreen_optional_asset_name(spell.upper_icon),
            lower_icon_key: spell.icon_key,
            lower_icon_asset: dualscreen_optional_asset_name(lower_icon_sprites.enabled_sprite),
            ribbon_asset: dualscreen_optional_asset_name(spell.ribbon),
            mana_cost: spell.cost,
            display_cost_orbs: spell.cost div 4,
        });
    }
    return spells;
}

function dualscreen_build_magic(spell_cache) {
    var pinned_spell = undefined;
    var pinned_spell_index = undefined;
    if ARI.pinned_spell != undefined && ARI.spells_learned[ARI.pinned_spell] {
        pinned_spell_index = ARI.pinned_spell;
        pinned_spell = spell_to_string(ARI.pinned_spell);
    }

    var cooldowns = [];
    // can_cast_spell() has only one explicit countdown-backed spell gate in
    // v1.0.4. Durations for rain/light are not mislabeled as cooldowns.
    if ARI.spells_learned[Spell.FireBreath] {
        array_push(cooldowns, {
            index: Spell.FireBreath,
            id: spell_to_string(Spell.FireBreath),
            remaining_frames: max(0, ARI.fire_breath_time),
            ready: ARI.fire_breath_time <= 0,
        });
    }

    return {
        spells: spell_cache,
        unlocked_count: array_length(spell_cache),
        pinned_spell: pinned_spell,
        pinned_spell_index: pinned_spell_index,
        cooldowns: cooldowns,
        actions_supported: true,
        supported_actions: ["pin_spell", "unpin_spell", "cast_pinned_spell"],
    };
}

function dualscreen_build_almanac_static() {
    var categories = fiddle_get("ui/menus/sub_menus/almanac/categories");
    var output = array_create(array_length(categories), undefined);
    for (var i = 0; i < array_length(categories); i++) {
        var data = categories[i];
        var seen_keys = HashSet();
        var item_ids = List();
        var tags = ListFromArray(data.tags);
        for (var j = 0; j < ItemId.LEN; j++) {
            var prototype = ITEM_PROTOTYPES[j];
            if !seen_keys.contains(prototype.recipe_key)
                && prototype.tags.contains_any_value_from(tags)
            {
                seen_keys.insert(prototype.recipe_key);
                item_ids.push(j);
            }
        }
        item_ids.sort_with(function(first, second) {
            return string_alphanumeric_comparison(
                local_get(ITEM_PROTOTYPES[first].name_key),
                local_get(ITEM_PROTOTYPES[second].name_key),
            );
        });
        var local_key = data[$ "local_key"] ?? "misc_local/" + data.key;
        output[i] = {
            key: data.key,
            display_name: local_get(local_key),
            item_ids: item_ids.to_array(),
        };
    }
    return output;
}

function dualscreen_flatten_discovered_collection_item(item_id, donated=false) {
    var item = new LiveItem(item_id);
    return {
        id: item_id_to_string(item_id),
        display_name: item.get_display_name(),
        description: item.get_display_description(),
        icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
        icon_frame: 0,
        donated: donated,
    };
}

function dualscreen_build_almanac(category_cache) {
    var categories = array_create(array_length(category_cache), undefined);
    var total_discovered = 0;
    var total_entries = 0;
    for (var i = 0; i < array_length(category_cache); i++) {
        var category = category_cache[i];
        var discovered = [];
        total_entries += array_length(category.item_ids);
        for (var j = 0; j < array_length(category.item_ids); j++) {
            var item_id = category.item_ids[j];
            if ARI.items_acquired[item_id] {
                array_push(discovered, dualscreen_flatten_discovered_collection_item(item_id));
            }
        }
        total_discovered += array_length(discovered);
        categories[i] = {
            key: category.key,
            display_name: category.display_name,
            discovered_count: array_length(discovered),
            total_count: array_length(category.item_ids),
            discovered_items: discovered,
        };
    }
    return {
        categories: categories,
        discovered_count: total_discovered,
        total_count: total_entries,
        unknown_entries_exposed: false,
    };
}

function dualscreen_build_museum_static() {
    var wings = array_create(MuseumWing.LEN, undefined);
    for (var i = 0; i < MuseumWing.LEN; i++) {
        var wing = MUSEUM_DATA.data[i];
        var order = fiddle_get(format(
            "ui/menus/standard_menus/museum/{MuseumWing}_order",
            i,
        ));
        var sets = array_create(array_length(order), undefined);
        for (var j = 0; j < array_length(order); j++) {
            var set_key = order[j];
            var set = wing.sets.get(set_key);
            var item_ids = ListFromArray(set.items);
            item_ids.sort_with(function(first, second) {
                return string_alphanumeric_comparison(
                    local_get(ITEM_PROTOTYPES[first].name_key),
                    local_get(ITEM_PROTOTYPES[second].name_key),
                );
            });
            sets[j] = {
                key: set_key,
                display_name: local_get(set.display_name),
                description: local_get(set.description),
                item_ids: item_ids.to_array(),
            };
        }
        wings[i] = {
            id: museum_wing_to_string(i),
            display_name: local_get(wing.name),
            sets: sets,
        };
    }
    return wings;
}

function dualscreen_build_museum(wing_cache) {
    var wings = array_create(array_length(wing_cache), undefined);
    var museum_discovered = 0;
    var museum_donated = 0;
    var museum_total = 0;
    for (var i = 0; i < array_length(wing_cache); i++) {
        var cached_wing = wing_cache[i];
        var sets = array_create(array_length(cached_wing.sets), undefined);
        var wing_discovered = 0;
        var wing_donated = 0;
        var wing_total = 0;
        for (var j = 0; j < array_length(cached_wing.sets); j++) {
            var cached_set = cached_wing.sets[j];
            var known_items = [];
            var donated_count = 0;
            for (var k = 0; k < array_length(cached_set.item_ids); k++) {
                var item_id = cached_set.item_ids[k];
                var donated = MUSEUM_PROGRESS[item_id];
                var discovered = ARI.items_acquired[item_id] || donated;
                donated_count += donated;
                if discovered {
                    array_push(
                        known_items,
                        dualscreen_flatten_discovered_collection_item(item_id, donated),
                    );
                }
            }
            var set_size = array_length(cached_set.item_ids);
            wing_discovered += array_length(known_items);
            wing_donated += donated_count;
            wing_total += set_size;
            sets[j] = {
                key: cached_set.key,
                display_name: cached_set.display_name,
                description: cached_set.description,
                discovered_count: array_length(known_items),
                donated_count: donated_count,
                total_count: set_size,
                complete: set_size > 0 && donated_count == set_size,
                known_items: known_items,
            };
        }
        museum_discovered += wing_discovered;
        museum_donated += wing_donated;
        museum_total += wing_total;
        wings[i] = {
            id: cached_wing.id,
            display_name: cached_wing.display_name,
            discovered_count: wing_discovered,
            donated_count: wing_donated,
            total_count: wing_total,
            sets: sets,
        };
    }
    return {
        wings: wings,
        discovered_count: museum_discovered,
        donated_count: museum_donated,
        total_count: museum_total,
        unknown_entries_exposed: false,
        future_rewards_exposed: false,
    };
}

function dualscreen_collection_fingerprints() {
    var almanac = "";
    var museum = "";
    for (var i = 0; i < ItemId.LEN; i++) {
        if ARI.items_acquired[i] {
            almanac += "|" + string(i);
            if MUSEUM_DATA.museum_items[i] {
                museum += "|a" + string(i);
            }
        }
        if MUSEUM_PROGRESS[i] {
            museum += "|d" + string(i);
        }
    }
    return {
        language: local_language(),
        almanac: almanac,
        museum: museum,
    };
}

function dualscreen_pending_renown() {
    return ARI.pending_renown_entries.sum_with(renown_entry_value);
}

function dualscreen_build_renown(reward_cache, pending_renown) {
    var level = renown_to_level(ARI.renown);
    var at_max = level >= MAX_RENOWN_LEVEL;
    var level_start = renown_level_total_cost(level);
    var next_level_total = at_max
        ? renown_level_total_cost(MAX_RENOWN_LEVEL)
        : renown_level_total_cost(level + 1);
    var progress = ARI.renown - level_start;
    var required = at_max ? 0 : next_level_total - level_start;
    var rank_index = min(level div RENOWN.levels_per_rank, RENOWN.ranks.count() - 1);
    var rank = renown_level_to_rank(level);
    var next_rank = undefined;
    if rank_index + 1 < RENOWN.ranks.count() {
        var next_rank_data = RENOWN.ranks.get(rank_index + 1);
        next_rank = {
            rank_index: rank_index + 1,
            level: (rank_index + 1) * RENOWN.levels_per_rank,
            display_name: local_get(next_rank_data.name),
        };
    }

    return {
        total: ARI.renown,
        pending: pending_renown,
        projected_total: ARI.renown + pending_renown,
        projected_level: renown_to_level(ARI.renown + pending_renown),
        level: level,
        max_level: MAX_RENOWN_LEVEL,
        at_max: at_max,
        level_start_total: level_start,
        next_level_total: next_level_total,
        progress: progress,
        progress_required: required,
        progress_ratio: at_max ? 1 : progress / required,
        rank: {
            rank_index: rank_index,
            display_name: local_get(rank.name),
        },
        next_rank: next_rank,
        claimable_rewards: {
            size: RENOWN_REWARD_INVENTORY.size(),
            slots: reward_cache,
        },
        future_rewards_exposed: false,
        actions_supported: false,
    };
}

function dualscreen_calendar_events_fingerprint() {
    var output = format(
        "{}|{}|{}|{}|{}|{}",
        local_language(),
        CALENDAR.time,
        ARI.name,
        ARI.birthday,
        ARI.wedding_date,
        requirements_pass(Requirement.SaturdayMarketUnlocked),
    );
    if ARI.pending_child != undefined {
        output += "|due:" + string(ARI.pending_child.due_date);
    }
    for (var i = 0; i < array_length(ARI.children); i++) {
        var child = ARI.children[i];
        output += format("|child:{}:{}:{}", i, string_length(child.name), child.name);
        output += ":" + string(child.birthday);
    }
    for (var i = 0; i < NpcId.LEN; i++) {
        if dualscreen_villager_is_visible(i) {
            output += "|npc:" + string(i);
        }
    }
    return output;
}

function dualscreen_calendar_event(target_date, days_from_now, event_type, id, display_name) {
    return {
        type: event_type,
        id: id,
        display_name: display_name,
        days_from_now: days_from_now,
        raw_date: target_date,
        year: get_years(target_date),
        display_year: get_years(target_date) + 1,
        season: season_to_string(get_seasons(target_date)),
        season_index: get_seasons(target_date),
        day: get_days(target_date),
        display_day: get_days(target_date) + 1,
        weekday: get_days(target_date) % Day.LEN,
    };
}

function dualscreen_build_calendar_events() {
    static HORIZON_DAYS = 28;
    var events = [];
    var saturday_market_unlocked = requirements_pass(Requirement.SaturdayMarketUnlocked);
    for (var offset = 0; offset < HORIZON_DAYS; offset++) {
        var target_date = CALENDAR.time + days(offset);
        var target_season = get_seasons(target_date);
        var target_day = get_days(target_date);
        var target_year = get_years(target_date);

        for (var i = 0; i < FestivalId.LEN; i++) {
            var festival = FESTIVALS[i];
            if festival.prototype.implemented
                && festival.prototype.date.season == target_season
                && festival.prototype.date.day - 1 == target_day
            {
                array_push(events, dualscreen_calendar_event(
                    target_date,
                    offset,
                    "festival",
                    festival_id_to_string(i),
                    local_get(festival.prototype.name),
                ));
            }
        }

        var weekday = target_day % Day.LEN;
        if weekday == Day.Friday {
            array_push(events, dualscreen_calendar_event(
                target_date,
                offset,
                "public_event",
                "friday_night_at_the_inn",
                local_get("misc_local/friday_night_at_the_inn"),
            ));
        }
        if weekday == Day.Saturday && saturday_market_unlocked {
            array_push(events, dualscreen_calendar_event(
                target_date,
                offset,
                "public_event",
                "saturday_market",
                local_get("misc_local/saturday_market"),
            ));
        }

        if ARI.wedding_date != undefined
            && target_season == get_seasons(ARI.wedding_date)
            && target_day == get_days(ARI.wedding_date)
        {
            var is_anniversary = target_year != get_years(ARI.wedding_date);
            array_push(events, dualscreen_calendar_event(
                target_date,
                offset,
                is_anniversary ? "anniversary" : "wedding",
                "player_wedding",
                local_get(is_anniversary ? "misc_local/your_anniversary" : "misc_local/your_wedding"),
            ));
        }

        if ARI.pending_child != undefined
            && target_year == get_years(ARI.pending_child.due_date)
            && target_season == get_seasons(ARI.pending_child.due_date)
            && target_day == get_days(ARI.pending_child.due_date)
        {
            array_push(events, dualscreen_calendar_event(
                target_date,
                offset,
                "due_date",
                "pending_child",
                local_get("misc_local/due_date"),
            ));
        }

        for (var i = 0; i < array_length(ARI.children); i++) {
            var child = ARI.children[i];
            if target_year > get_years(child.birthday)
                && target_season == get_seasons(child.birthday)
                && target_day == get_days(child.birthday)
            {
                array_push(events, dualscreen_calendar_event(
                    target_date,
                    offset,
                    "birthday",
                    "child_" + string(i),
                    format(local_get("misc_local/birthday_template"), child.name),
                ));
            }
        }

        for (var i = 0; i < NpcId.LEN; i++) {
            if !dualscreen_villager_is_visible(i) {
                continue;
            }
            var npc = NPCS[i];
            if npc.prototype.birthday.season == target_season
                && npc.prototype.birthday.day - 1 == target_day
            {
                array_push(events, dualscreen_calendar_event(
                    target_date,
                    offset,
                    "birthday",
                    npc_id_to_string(i),
                    format(
                        local_get("misc_local/birthday_template"),
                        local_get(npc.prototype.name),
                    ),
                ));
            }
        }

        if target_season == get_seasons(ARI.birthday)
            && target_day == get_days(ARI.birthday)
        {
            array_push(events, dualscreen_calendar_event(
                target_date,
                offset,
                "birthday",
                "player",
                format(local_get("misc_local/birthday_template"), ARI.name),
            ));
        }
    }
    return {
        horizon_days: HORIZON_DAYS,
        events: events,
        count: array_length(events),
        private_date_availability_exposed: false,
        unreleased_festivals_exposed: false,
    };
}

function dualscreen_build_calendar() {
    return {
        raw: CALENDAR.time,
        year: CALENDAR.year(),
        display_year: CALENDAR.year() + 1,
        season: season_to_string(CALENDAR.season()),
        season_index: CALENDAR.season(),
        day: CALENDAR.day(),
        display_day: CALENDAR.day() + 1,
        weekday: CALENDAR.day_type(),
        total_days: total_days(),
    };
}

function dualscreen_build_clock() {
    return {
        raw: CLOCK.time,
        hour: CLOCK.hour(),
        minute: CLOCK.minute(),
        formatted: clock_time_to_string(CLOCK.time),
        is_day: CLOCK.is_day(),
        is_night: CLOCK.is_night(),
    };
}

function dualscreen_build_weather() {
    var tomorrow = weather_tomorrow();
    return {
        current: weather_to_string(WEATHER.weather),
        current_id: WEATHER.weather,
        tomorrow: weather_to_string(tomorrow),
        tomorrow_id: tomorrow,
        inclement: WEATHER.is_inclement(),
        under_spell: WEATHER.under_spell,
    };
}

function dualscreen_quest_item_requirement(item_id, required, current, source, source_location=undefined) {
    var item = new LiveItem(item_id);
    return {
        type: "item",
        source: source,
        source_location: source_location,
        item_id: item_id_to_string(item_id),
        display_name: item.get_display_name(),
        icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
        required: required,
        current: current,
        complete: current >= required,
    };
}

function dualscreen_build_quest_requirements(requirements, blackboard) {
    var output = [];
    var requirement = requirements[Requirement.HasItem];
    if requirement != undefined {
        for (var i = 0; i < array_length(requirement); i++) {
            var data = requirement[i];
            array_push(output, dualscreen_quest_item_requirement(
                data[0],
                data[1],
                ARI.inventory.item_id_quantity(data[0]),
                "inventory",
            ));
        }
    }

    requirement = requirements[Requirement.HasGold];
    if requirement != undefined {
        array_push(output, {
            type: "gold",
            required: requirement,
            current: ARI.get_gold(),
            complete: ARI.get_gold() >= requirement,
        });
    }

    requirement = requirements[Requirement.ShippedTag];
    if requirement != undefined {
        for (var i = 0; i < array_length(requirement); i++) {
            var data = requirement[i];
            var registration = QUEST_TAG_REGISTRATIONS.get(data[0]);
            var starting_amount = blackboard.get(format("{}_needed", data[0])) - data[1];
            var current = ARI.times_item_tag_sold(data[0]) - starting_amount;
            array_push(output, {
                type: "shipped_tag",
                tag: data[0],
                display_name: local_get(registration.text),
                icon_asset: asset_to_string(ITEM_PROTOTYPES[registration.item_for_icon].icon_sprite),
                required: data[1],
                current: current,
                complete: current >= data[1],
            });
        }
    }

    requirement = requirements[Requirement.ShippedItem];
    if requirement != undefined {
        for (var i = 0; i < array_length(requirement); i++) {
            var data = requirement[i];
            var starting_amount = blackboard.get(format("{ItemId}_needed", data[0])) - data[1];
            var current = ARI.items_sold[data[0]] - starting_amount;
            var item = new LiveItem(data[0]);
            array_push(output, {
                type: "shipped_item",
                item_id: item_id_to_string(data[0]),
                display_name: item.get_display_name(),
                icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
                required: data[1],
                current: current,
                complete: current >= data[1],
            });
        }
    }

    requirement = requirements[Requirement.DefeatedMonster];
    if requirement != undefined {
        for (var i = 0; i < array_length(requirement); i++) {
            var data = requirement[i];
            var starting_amount = blackboard.get_or(format("{MonsterId}_needed", data[0]), 0) - data[1];
            var current = ARI.monsters_killed[data[0]] - starting_amount;
            var info = monster_id_to_ui_info(data[0]);
            array_push(output, {
                type: "defeated_monster",
                monster_id: data[0],
                display_name: local_get(info.label),
                icon_asset: asset_to_string(info.icon),
                required: data[1],
                current: current,
                complete: current >= data[1],
            });
        }
    }

    requirement = requirements[Requirement.MetNpc];
    if requirement != undefined {
        var current = 0;
        for (var i = 0; i < array_length(requirement); i++) {
            if NPCS[requirement[i]].has_met() {
                current += 1;
            }
        }
        array_push(output, {
            type: "met_npcs",
            required: array_length(requirement),
            current: current,
            complete: current >= array_length(requirement),
            identities_exposed: false,
        });
    }

    requirement = requirements[Requirement.SuppliedItems];
    if requirement != undefined {
        for (var i = 0; i < array_length(requirement); i++) {
            var supply = requirement[i];
            var inventory = undefined;
            var source = "seal";
            var source_location = undefined;
            if supply.type == SupplyType.Chest {
                var point = trellis_point_location_position(supply.point);
                var ni = GRIDS[point.location_id].node_index_for_cell(point.pos.x div 8, point.pos.y div 8);
                inventory = GRIDS[point.location_id].node_parent[ni].inventory;
                source = "chest";
                source_location = dualscreen_flatten_location_position(point);
            } else {
                inventory = SEAL_INVENTORIES[supply.seal];
            }
            for (var item_id = 0; item_id < ItemId.LEN; item_id++) {
                var required = supply.items[item_id];
                if required == 0 {
                    continue;
                }
                array_push(output, dualscreen_quest_item_requirement(
                    item_id,
                    required,
                    inventory.item_id_quantity(item_id),
                    source,
                    source_location,
                ));
            }
        }
    }

    requirement = requirements[Requirement.Custom];
    if requirement != undefined && requirement[$ "gather"] != undefined {
        var item_id = string_to_item_id(requirement.gather.item);
        var item = new LiveItem(item_id);
        array_push(output, {
            type: "gather_item",
            item_id: item_id_to_string(item_id),
            display_name: item.get_display_name(),
            icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
            required: undefined,
            current: total_item_count(item_id),
            complete: undefined,
        });
    }
    return output;
}

function dualscreen_quest_reward_item(item, amount, reward_type) {
    return {
        type: reward_type,
        item_id: item_id_to_string(item.item_id),
        display_name: item.get_display_name(),
        icon_asset: dualscreen_optional_asset_name(item.get_ui_icon()),
        amount: amount,
        instance_metadata: item.serialize(),
    };
}

function dualscreen_build_quest_rewards(rewards) {
    var output = [];
    for (var i = 0; i < rewards.count(); i++) {
        var reward = rewards.get(i);
        switch reward.type {
            case QuestRewardType.Item:
                array_push(output, dualscreen_quest_reward_item(
                    new LiveItem(reward.item),
                    reward.amount,
                    "item",
                ));
                break;
            case QuestRewardType.Gold:
                array_push(output, { type: "gold", amount: reward.amount });
                break;
            case QuestRewardType.RecipeScroll:
                array_push(output, dualscreen_quest_reward_item(
                    new LiveItem(ItemId.RecipeScroll, reward.item),
                    1,
                    "recipe_scroll",
                ));
                break;
            case QuestRewardType.CraftingScroll:
                array_push(output, dualscreen_quest_reward_item(
                    new LiveItem(ItemId.CraftingScroll, reward.item),
                    1,
                    "crafting_scroll",
                ));
                break;
            case QuestRewardType.AnimalCosmetic:
                var item = new LiveItem(ItemId.AnimalCosmetic);
                item.animal_cosmetic = {
                    animal: reward.animal,
                    cosmetic: reward.cosmetic,
                };
                array_push(output, dualscreen_quest_reward_item(item, 1, "animal_cosmetic"));
                break;
            case QuestRewardType.PlayerCosmetic:
                var item = new LiveItem(ItemId.Cosmetic);
                item.cosmetic = reward.cosmetic;
                array_push(output, dualscreen_quest_reward_item(item, 1, "player_cosmetic"));
                break;
            case QuestRewardType.Renown:
                array_push(output, { type: "renown", amount: reward.value });
                break;
            case QuestRewardType.LookupKey:
                var value = ARI.quest_artifacts.get(reward.key);
                if value != undefined {
                    var item = reward_to_live_item(parse_reward(value));
                    if item != undefined {
                        array_push(output, dualscreen_quest_reward_item(item, 1, "lookup_item"));
                    }
                }
                break;
            // The vanilla journal deliberately omits tier and quest-chain
            // rewards, so the bridge does too.
            default: break;
        }
    }
    return output;
}

function dualscreen_build_quest_targets(task) {
    var output = [];
    for (var i = 0; i < task.query_targets.count(); i++) {
        var target = task.query_targets.get(i);
        switch target.type {
            case QuestQueryType.Npc:
                var npc = NPCS[target.npc_name];
                array_push(output, {
                    type: "npc",
                    npc_id: npc_id_to_string(target.npc_name),
                    location: npc_is_unlocked(target.npc_name)
                        ? dualscreen_flatten_location_position(npc.location_position)
                        : undefined,
                });
                break;
            case QuestQueryType.Cutscene:
                array_push(output, {
                    type: "location",
                    location_id: location_id_to_string(target.cutscene_location),
                });
                break;
            case QuestQueryType.Manual:
                array_push(output, { type: "manual" });
                break;
        }
    }
    return output;
}

function dualscreen_quest_fingerprint() {
    var output = local_language() + "|" + string(QUEST_LOG.completed.count());
    var keys = QUEST_LOG.active.keys();
    for (var i = 0; i < array_length(keys); i++) {
        var active = QUEST_LOG.active.get(keys[i]);
        var ready = false;
        if active.current_stage < active.quest.tasks.count() {
            var task = active.quest.tasks.get(active.current_stage);
            ready = fulfills_all_requirements(task.requirements, active.blackboard);
            output += "|requirements:" + json_stringify(
                dualscreen_build_quest_requirements(task.requirements, active.blackboard)
            );
            output += "|targets:" + json_stringify(dualscreen_build_quest_targets(task));
        }
        output += format("|{}:{}:{}", keys[i], active.current_stage, ready);
    }
    return output;
}

function dualscreen_build_quests() {
    var active_output = [];
    var keys = QUEST_LOG.active.keys();
    for (var i = 0; i < array_length(keys); i++) {
        var key = keys[i];
        var active = QUEST_LOG.active.get(key);
        var quest = active.quest;
        var has_current_task = active.current_stage < quest.tasks.count();
        var objective = "";
        var ready = false;
        var requirements = [];
        var targets = [];
        if has_current_task {
            var task = quest.tasks.get(active.current_stage);
            objective = local_get(task.description);
            ready = fulfills_all_requirements(task.requirements, active.blackboard);
            requirements = dualscreen_build_quest_requirements(task.requirements, active.blackboard);
            targets = dualscreen_build_quest_targets(task);
        }

        array_push(active_output, {
            id: key,
            name: local_get(quest.name),
            description: local_get(quest.description),
            category: quest.category,
            current_stage: active.current_stage,
            stage_count: quest.tasks.count(),
            objective: objective,
            requirements: requirements,
            targets: targets,
            ready: ready,
            npc_for_icon: {
                id: npc_id_to_string(quest.npc_for_icon),
                icon_asset: asset_to_string(get_npc_icon(quest.npc_for_icon)),
            },
            rewards: dualscreen_build_quest_rewards(quest.reward_list),
        });
    }

    return {
        active: active_output,
        active_count: array_length(active_output),
        completed_count: QUEST_LOG.completed.count(),
        tracked_supported: false,
    };
}

function dualscreen_actions_are_safe() {
    if !is_world_room(room())
        || !instance_exists(obj_ari)
        || game_paused()
        || Game.go_to_main_menu
        || ARI.end_of_day_sequence
        || ARI.held_animal_id != undefined
        || MIST.running
        || !obj_ari.is_in_normal_state()
        || obj_ari.fsm.next_state != undefined
        || ANCHOR.get_menu(Menu.Toolbar) == undefined
        || ANCHOR.get_menu(Menu.Textbox) != undefined
        || ANCHOR.get_menu(Menu.Eod) != undefined
    {
        return false;
    }
    return true;
}

function dualscreen_valid_slot_index(index) {
    return is_real(index)
        && floor(index) == index
        && index >= 0
        && index < ARI.inventory.size();
}

function dualscreen_valid_armor_slot_index(index) {
    return is_real(index)
        && floor(index) == index
        && index >= 0
        && index < ARI.armor.size();
}

function dualscreen_valid_spell_index(index) {
    return is_real(index)
        && floor(index) == index
        && index >= 0
        && index < Spell.LEN;
}

function dualscreen_slot_fingerprint(slot) {
    return slot.item == undefined || slot.count <= 0
        ? ""
        : json_stringify(slot.item.serialize());
}

function dualscreen_command_matches_slot(command, field, slot) {
    var expected = command[$ field];
    return is_string(expected)
        && string_length(expected) <= 2048
        && expected == dualscreen_slot_fingerprint(slot);
}

function DualScreenBridge(game_identifier) constructor {
    self.game_identifier = string(game_identifier);
    self.epoch = format("{}-{}", self.game_identifier, get_timer());
    self.sequence = 0;
    self.last_command_sequence = -1;
    self.command_poll_counter = 0;
    self.maintenance_counter = 0;
    self.player_poll_counter = 0;
    self.social_poll_counter = 0;
    self.publish_counter = 0;
    self.shutting_down = false;
    self.initialized = false;
    self.dirty = true;
    self.force_full = true;

    self.inventory_subscriber = new InventorySubscriber(ARI.inventory);
    self.armor_subscriber = new InventorySubscriber(ARI.armor);
    self.renown_reward_subscriber = new InventorySubscriber(RENOWN_REWARD_INVENTORY);
    self.inventory_cache = [];
    self.armor_cache = [];
    self.renown_reward_cache = [];
    self.magic_spell_cache = [];
    self.almanac_category_cache = [];
    self.museum_wing_cache = [];

    self.cache = {
        session: undefined,
        status: undefined,
        progression: undefined,
        inventory: undefined,
        armor: undefined,
        hotbar: undefined,
        location: undefined,
        player: undefined,
        map: undefined,
        villagers: undefined,
        animals: undefined,
        magic: undefined,
        almanac: undefined,
        museum: undefined,
        renown: undefined,
        calendar_events: undefined,
        calendar: undefined,
        clock: undefined,
        weather: undefined,
        quests: undefined,
    };
    self.revisions = {
        session: 0,
        status: 0,
        progression: 0,
        inventory: 0,
        armor: 0,
        hotbar: 0,
        location: 0,
        player: 0,
        map: 0,
        villagers: 0,
        animals: 0,
        magic: 0,
        almanac: 0,
        museum: 0,
        renown: 0,
        calendar_events: 0,
        calendar: 0,
        clock: 0,
        weather: 0,
        quests: 0,
    };
    self.published_revisions = {
        session: 0,
        status: 0,
        progression: 0,
        inventory: 0,
        armor: 0,
        hotbar: 0,
        location: 0,
        player: 0,
        map: 0,
        villagers: 0,
        animals: 0,
        magic: 0,
        almanac: 0,
        museum: 0,
        renown: 0,
        calendar_events: 0,
        calendar: 0,
        clock: 0,
        weather: 0,
        quests: 0,
    };
    self.dirty_topics = {
        session: false,
        status: false,
        progression: false,
        inventory: false,
        armor: false,
        hotbar: false,
        location: false,
        player: false,
        map: false,
        villagers: false,
        animals: false,
        magic: false,
        almanac: false,
        museum: false,
        renown: false,
        calendar_events: false,
        calendar: false,
        clock: false,
        weather: false,
        quests: false,
    };

    self.last_status_detail_fingerprint = "";
    self.last_health = undefined;
    self.last_health_max = undefined;
    self.last_stamina = undefined;
    self.last_stamina_max = undefined;
    self.last_mana = undefined;
    self.last_mana_max = undefined;
    self.last_tesserae = undefined;
    self.last_essence = undefined;
    self.last_renown = undefined;
    self.last_location_id = undefined;
    self.last_location_dynamic_index = undefined;
    self.last_location_room = undefined;
    self.last_player_x = undefined;
    self.last_player_y = undefined;
    self.last_player_dir = undefined;
    self.last_player_cardinal = undefined;
    self.last_player_in_mines = undefined;
    self.last_player_mine_floor = undefined;
    self.last_player_mine_biome = undefined;
    self.last_player_map_key = undefined;
    self.last_map_fingerprint = undefined;
    self.last_villager_fingerprint = undefined;
    self.last_animal_fingerprint = undefined;
    self.last_magic_static_fingerprint = undefined;
    self.last_magic_pinned_spell = undefined;
    self.last_magic_fire_breath_time = undefined;
    self.last_collection_language = undefined;
    self.last_almanac_fingerprint = undefined;
    self.last_museum_fingerprint = undefined;
    self.last_renown_fingerprint = undefined;
    self.last_calendar_events_fingerprint = undefined;
    self.last_calendar_time = undefined;
    self.last_clock_hour = undefined;
    self.last_clock_minute = undefined;
    self.last_weather_current = undefined;
    self.last_weather_tomorrow = undefined;
    self.last_weather_inclement = undefined;
    self.last_weather_under_spell = undefined;
    self.last_quest_fingerprint = "";

    function bump(topic) {
        self.revisions[$ topic] += 1;
        self.dirty_topics[$ topic] = true;
        self.dirty = true;
    }

    function ensure_directory() {
        if !directory_exists(DUALSCREEN_DIRECTORY) {
            directory_create(DUALSCREEN_DIRECTORY);
        }
    }

    function initialize_snapshot() {
        self.ensure_directory();
        self.inventory_cache = dualscreen_flatten_inventory(ARI.inventory);
        self.armor_cache = dualscreen_flatten_inventory(ARI.armor, true);
        self.renown_reward_cache = dualscreen_flatten_inventory(RENOWN_REWARD_INVENTORY);
        self.magic_spell_cache = dualscreen_build_magic_static();
        self.almanac_category_cache = dualscreen_build_almanac_static();
        self.museum_wing_cache = dualscreen_build_museum_static();
        self.cache.session = {
            in_game: true,
            game_version: version_to_string(GAME_VERSION),
            game_identifier: self.game_identifier,
            capabilities: {
                select_hotbar: true,
                swap_slots: true,
                consume_item: true,
                equip_armor: true,
                unequip_armor: true,
                pin_spell: true,
                unpin_spell: true,
                cast_pinned_spell: true,
                generic_item_use: false,
                drop_item: false,
                quest_mutation: false,
                quest_tracking: false,
            },
        };
        self.cache.status = dualscreen_build_status();
        self.cache.progression = dualscreen_build_progression();
        self.cache.inventory = {
            size: ARI.inventory.size(),
            slots: self.inventory_cache,
        };
        self.cache.armor = {
            size: ARI.armor.size(),
            slots: self.armor_cache,
        };
        self.cache.hotbar = {
            selected_index: ARI.held_item_index,
        };
        self.cache.location = dualscreen_build_location();
        self.cache.player = dualscreen_build_player();
        self.cache.map = dualscreen_build_map();
        self.cache.villagers = dualscreen_build_villagers();
        var animals = get_all_animals(true);
        self.cache.animals = dualscreen_build_animals(animals);
        self.cache.magic = dualscreen_build_magic(self.magic_spell_cache);
        self.cache.almanac = dualscreen_build_almanac(self.almanac_category_cache);
        self.cache.museum = dualscreen_build_museum(self.museum_wing_cache);
        var pending_renown = dualscreen_pending_renown();
        self.cache.renown = dualscreen_build_renown(self.renown_reward_cache, pending_renown);
        self.cache.calendar_events = dualscreen_build_calendar_events();
        self.cache.calendar = dualscreen_build_calendar();
        self.cache.clock = dualscreen_build_clock();
        self.cache.weather = dualscreen_build_weather();
        self.cache.quests = dualscreen_build_quests();

        var topics = struct_get_names(self.revisions);
        for (var i = 0; i < array_length(topics); i++) {
            self.revisions[$ topics[i]] += 1;
            self.dirty_topics[$ topics[i]] = true;
        }

        self.last_status_detail_fingerprint = dualscreen_status_detail_fingerprint();
        self.last_health = ARI.get_health();
        self.last_health_max = ARI.get_max_health();
        self.last_stamina = ARI.get_stamina();
        self.last_stamina_max = ARI.get_max_stamina();
        self.last_mana = ARI.get_mana();
        self.last_mana_max = ARI.mana_max;
        self.last_tesserae = ARI.get_gold();
        self.last_essence = ARI.get_essence();
        self.last_renown = ARI.renown;
        self.last_location_id = CURRENT_LOCATION_ID;
        self.last_location_dynamic_index = CURRENT_DYN_INDEX;
        self.last_location_room = room();
        self.last_player_x = obj_ari.x;
        self.last_player_y = obj_ari.y;
        self.last_player_dir = obj_ari.dir;
        self.last_player_cardinal = obj_ari.cardinal;
        self.last_player_in_mines = dualscreen_player_is_in_mines();
        self.last_player_mine_floor = self.last_player_in_mines ? DUNGEON_FLOOR : undefined;
        self.last_player_mine_biome = self.last_player_in_mines ? DUNGEON_BIOME : undefined;
        self.last_player_map_key = dualscreen_player_map_key();
        self.last_map_fingerprint = dualscreen_map_fingerprint();
        self.last_villager_fingerprint = dualscreen_villagers_fingerprint();
        self.last_animal_fingerprint = dualscreen_animals_fingerprint(animals);
        self.last_magic_static_fingerprint = dualscreen_magic_static_fingerprint();
        self.last_magic_pinned_spell = ARI.pinned_spell;
        self.last_magic_fire_breath_time = ARI.spells_learned[Spell.FireBreath]
            ? ARI.fire_breath_time
            : undefined;
        var collection_fingerprints = dualscreen_collection_fingerprints();
        self.last_collection_language = collection_fingerprints.language;
        self.last_almanac_fingerprint = collection_fingerprints.almanac;
        self.last_museum_fingerprint = collection_fingerprints.museum;
        self.last_renown_fingerprint = format("{}|{}|{}", local_language(), ARI.renown, pending_renown);
        self.last_calendar_events_fingerprint = dualscreen_calendar_events_fingerprint();
        self.last_calendar_time = CALENDAR.time;
        self.last_clock_hour = CLOCK.hour();
        self.last_clock_minute = CLOCK.minute();
        self.last_weather_current = WEATHER.weather;
        self.last_weather_tomorrow = weather_tomorrow();
        self.last_weather_inclement = WEATHER.is_inclement();
        self.last_weather_under_spell = WEATHER.under_spell;
        self.last_quest_fingerprint = dualscreen_quest_fingerprint();
        self.initialized = true;
        self.dirty = true;
        self.force_full = true;
        trace("[DualScreen] live-state bridge initialized (schema {}, epoch {})", DUALSCREEN_SCHEMA_VERSION, self.epoch);
    }

    function refresh_inventory() {
        var updates = self.inventory_subscriber.pull();
        var resized = array_length(self.inventory_cache) != ARI.inventory.size();
        if resized {
            self.inventory_cache = dualscreen_flatten_inventory(ARI.inventory);
            self.cache.inventory = {
                size: ARI.inventory.size(),
                slots: self.inventory_cache,
            };
            self.bump("inventory");
            return true;
        }

        if updates.is_empty() {
            return false;
        }
        for (var i = 0; i < updates.count(); i++) {
            var slot = updates.get(i);
            self.inventory_cache[slot.index] = dualscreen_flatten_slot(slot);
        }
        self.cache.inventory.slots = self.inventory_cache;
        self.bump("inventory");
        return true;
    }

    function refresh_armor() {
        var updates = self.armor_subscriber.pull();
        var resized = array_length(self.armor_cache) != ARI.armor.size();
        if resized {
            self.armor_cache = dualscreen_flatten_inventory(ARI.armor, true);
            self.cache.armor = {
                size: ARI.armor.size(),
                slots: self.armor_cache,
            };
            self.bump("armor");
            return;
        }

        if updates.is_empty() {
            return;
        }
        for (var i = 0; i < updates.count(); i++) {
            var slot = updates.get(i);
            self.armor_cache[slot.index] = dualscreen_flatten_slot(slot, true);
        }
        self.cache.armor.slots = self.armor_cache;
        self.bump("armor");
    }

    function refresh_renown_rewards() {
        var updates = self.renown_reward_subscriber.pull();
        var resized = array_length(self.renown_reward_cache) != RENOWN_REWARD_INVENTORY.size();
        if resized {
            self.renown_reward_cache = dualscreen_flatten_inventory(RENOWN_REWARD_INVENTORY);
            var resized_pending_renown = dualscreen_pending_renown();
            self.last_renown_fingerprint = format(
                "{}|{}|{}",
                local_language(),
                ARI.renown,
                resized_pending_renown,
            );
            self.cache.renown = dualscreen_build_renown(
                self.renown_reward_cache,
                resized_pending_renown,
            );
            self.bump("renown");
            return;
        }

        if updates.is_empty() {
            return;
        }
        for (var i = 0; i < updates.count(); i++) {
            var slot = updates.get(i);
            self.renown_reward_cache[slot.index] = dualscreen_flatten_slot(slot);
        }
        var pending_renown = dualscreen_pending_renown();
        self.last_renown_fingerprint = format(
            "{}|{}|{}",
            local_language(),
            ARI.renown,
            pending_renown,
        );
        self.cache.renown = dualscreen_build_renown(
            self.renown_reward_cache,
            pending_renown,
        );
        self.bump("renown");
    }

    function refresh_scalars() {
        var health = ARI.get_health();
        var health_max = ARI.get_max_health();
        var stamina = ARI.get_stamina();
        var stamina_max = ARI.get_max_stamina();
        var mana = ARI.get_mana();
        var mana_max = ARI.mana_max;
        var tesserae = ARI.get_gold();
        var essence = ARI.get_essence();
        var renown = ARI.renown;
        if health != self.last_health
            || health_max != self.last_health_max
            || stamina != self.last_stamina
            || stamina_max != self.last_stamina_max
            || mana != self.last_mana
            || mana_max != self.last_mana_max
            || tesserae != self.last_tesserae
            || essence != self.last_essence
            || renown != self.last_renown
        {
            self.last_health = health;
            self.last_health_max = health_max;
            self.last_stamina = stamina;
            self.last_stamina_max = stamina_max;
            self.last_mana = mana;
            self.last_mana_max = mana_max;
            self.last_tesserae = tesserae;
            self.last_essence = essence;
            self.last_renown = renown;
            self.cache.status = dualscreen_build_status();
            self.bump("status");
        }

        if self.cache.hotbar.selected_index != ARI.held_item_index {
            self.cache.hotbar = { selected_index: ARI.held_item_index };
            self.bump("hotbar");
        }

        var location_id = CURRENT_LOCATION_ID;
        var location_dynamic_index = CURRENT_DYN_INDEX;
        var location_room = room();
        if location_id != self.last_location_id
            || location_dynamic_index != self.last_location_dynamic_index
            || location_room != self.last_location_room
        {
            self.last_location_id = location_id;
            self.last_location_dynamic_index = location_dynamic_index;
            self.last_location_room = location_room;
            self.cache.location = dualscreen_build_location();
            self.bump("location");
        }

        if CALENDAR.time != self.last_calendar_time {
            self.last_calendar_time = CALENDAR.time;
            self.cache.calendar = dualscreen_build_calendar();
            self.bump("calendar");
        }

        var clock_hour = CLOCK.hour();
        var clock_minute = CLOCK.minute();
        if clock_hour != self.last_clock_hour || clock_minute != self.last_clock_minute {
            self.last_clock_hour = clock_hour;
            self.last_clock_minute = clock_minute;
            self.cache.clock = dualscreen_build_clock();
            self.bump("clock");
        }
    }

    function refresh_player() {
        var player_x = obj_ari.x;
        var player_y = obj_ari.y;
        var player_dir = obj_ari.dir;
        var player_cardinal = obj_ari.cardinal;
        var in_mines = dualscreen_player_is_in_mines();
        var mine_floor = in_mines ? DUNGEON_FLOOR : undefined;
        var mine_biome = in_mines ? DUNGEON_BIOME : undefined;
        if player_x != self.last_player_x
            || player_y != self.last_player_y
            || player_dir != self.last_player_dir
            || player_cardinal != self.last_player_cardinal
            || in_mines != self.last_player_in_mines
            || mine_floor != self.last_player_mine_floor
            || mine_biome != self.last_player_mine_biome
        {
            self.last_player_x = player_x;
            self.last_player_y = player_y;
            self.last_player_dir = player_dir;
            self.last_player_cardinal = player_cardinal;
            self.last_player_in_mines = in_mines;
            self.last_player_mine_floor = mine_floor;
            self.last_player_mine_biome = mine_biome;
            self.cache.player = dualscreen_build_player();
            self.bump("player");
        }

        var player_map_key = dualscreen_player_map_key();
        if player_map_key != self.last_player_map_key {
            self.last_player_map_key = player_map_key;
            self.last_map_fingerprint = dualscreen_map_fingerprint();
            self.cache.map = dualscreen_build_map();
            self.bump("map");
        }
    }

    function refresh_social() {
        var villager_fingerprint = dualscreen_villagers_fingerprint();
        if villager_fingerprint != self.last_villager_fingerprint {
            self.last_villager_fingerprint = villager_fingerprint;
            self.cache.villagers = dualscreen_build_villagers();
            self.bump("villagers");
        }

        var animals = get_all_animals(true);
        var animal_fingerprint = dualscreen_animals_fingerprint(animals);
        if animal_fingerprint != self.last_animal_fingerprint {
            self.last_animal_fingerprint = animal_fingerprint;
            self.cache.animals = dualscreen_build_animals(animals);
            self.bump("animals");
        }

        var map_fingerprint = dualscreen_map_fingerprint();
        if map_fingerprint != self.last_map_fingerprint {
            self.last_map_fingerprint = map_fingerprint;
            self.last_player_map_key = dualscreen_player_map_key();
            self.cache.map = dualscreen_build_map();
            self.bump("map");
        }


        var collection_fingerprints = dualscreen_collection_fingerprints();
        var language_changed = collection_fingerprints.language != self.last_collection_language;
        if language_changed {
            self.last_collection_language = collection_fingerprints.language;
            self.almanac_category_cache = dualscreen_build_almanac_static();
            self.museum_wing_cache = dualscreen_build_museum_static();
        }
        if language_changed || collection_fingerprints.almanac != self.last_almanac_fingerprint {
            self.last_almanac_fingerprint = collection_fingerprints.almanac;
            self.cache.almanac = dualscreen_build_almanac(self.almanac_category_cache);
            self.bump("almanac");
        }
        if language_changed || collection_fingerprints.museum != self.last_museum_fingerprint {
            self.last_museum_fingerprint = collection_fingerprints.museum;
            self.cache.museum = dualscreen_build_museum(self.museum_wing_cache);
            self.bump("museum");
            // Donation state is also item metadata. Rebuild only on this
            // low-rate, bounded museum fingerprint seam.
            self.inventory_cache = dualscreen_flatten_inventory(ARI.inventory);
            self.armor_cache = dualscreen_flatten_inventory(ARI.armor, true);
            self.cache.inventory.slots = self.inventory_cache;
            self.cache.armor.slots = self.armor_cache;
            self.bump("inventory");
            self.bump("armor");
        }

        var calendar_events_fingerprint = dualscreen_calendar_events_fingerprint();
        if calendar_events_fingerprint != self.last_calendar_events_fingerprint {
            self.last_calendar_events_fingerprint = calendar_events_fingerprint;
            self.cache.calendar_events = dualscreen_build_calendar_events();
            self.bump("calendar_events");
        }
    }

    function refresh_maintenance() {
        var magic_static_fingerprint = dualscreen_magic_static_fingerprint();
        var magic_static_changed = magic_static_fingerprint != self.last_magic_static_fingerprint;
        if magic_static_changed {
            self.last_magic_static_fingerprint = magic_static_fingerprint;
            self.magic_spell_cache = dualscreen_build_magic_static();
        }
        var magic_fire_breath_time = ARI.spells_learned[Spell.FireBreath]
            ? ARI.fire_breath_time
            : undefined;
        if magic_static_changed
            || ARI.pinned_spell != self.last_magic_pinned_spell
            || magic_fire_breath_time != self.last_magic_fire_breath_time
        {
            self.last_magic_pinned_spell = ARI.pinned_spell;
            self.last_magic_fire_breath_time = magic_fire_breath_time;
            self.cache.magic = dualscreen_build_magic(self.magic_spell_cache);
            self.bump("magic");
        }

        var pending_renown = dualscreen_pending_renown();
        var renown_fingerprint = format("{}|{}|{}", local_language(), ARI.renown, pending_renown);
        if renown_fingerprint != self.last_renown_fingerprint {
            self.last_renown_fingerprint = renown_fingerprint;
            self.cache.renown = dualscreen_build_renown(self.renown_reward_cache, pending_renown);
            self.bump("renown");
        }

        var status_detail_fingerprint = dualscreen_status_detail_fingerprint();
        if status_detail_fingerprint != self.last_status_detail_fingerprint {
            self.last_status_detail_fingerprint = status_detail_fingerprint;
            self.cache.progression = dualscreen_build_progression();
            self.bump("progression");
        }

        var weather_current = WEATHER.weather;
        var weather_next = weather_tomorrow();
        var weather_inclement = WEATHER.is_inclement();
        var weather_under_spell = WEATHER.under_spell;
        if weather_current != self.last_weather_current
            || weather_next != self.last_weather_tomorrow
            || weather_inclement != self.last_weather_inclement
            || weather_under_spell != self.last_weather_under_spell
        {
            self.last_weather_current = weather_current;
            self.last_weather_tomorrow = weather_next;
            self.last_weather_inclement = weather_inclement;
            self.last_weather_under_spell = weather_under_spell;
            self.cache.weather = dualscreen_build_weather();
            self.bump("weather");
        }

        var quest_fingerprint = dualscreen_quest_fingerprint();
        if quest_fingerprint != self.last_quest_fingerprint {
            self.last_quest_fingerprint = quest_fingerprint;
            self.cache.quests = dualscreen_build_quests();
            self.bump("quests");
        }
    }

    function publish() {
        var full = self.force_full;
        var base_sequence = full ? 0 : self.sequence;
        var base_revisions = full ? {} : self.published_revisions;
        var changed_topics = {};
        var topic_names = struct_get_names(self.cache);
        for (var i = 0; i < array_length(topic_names); i++) {
            var topic = topic_names[i];
            if full || self.dirty_topics[$ topic] {
                changed_topics[$ topic] = self.cache[$ topic];
            }
        }

        self.sequence += 1;
        var payload = {
            protocol: DUALSCREEN_PROTOCOL_NAME,
            schema: DUALSCREEN_SCHEMA_VERSION,
            epoch: self.epoch,
            sequence: self.sequence,
            full: full,
            base_sequence: base_sequence,
            generated_tick: TICK,
            base_revisions: base_revisions,
            revisions: self.revisions,
            topics: changed_topics,
        };
        dualscreen_atomic_json_write(
            DUALSCREEN_STATE_PATH,
            DUALSCREEN_STATE_NEW_PATH,
            DUALSCREEN_STATE_OLD_PATH,
            payload,
        );
        for (var i = 0; i < array_length(topic_names); i++) {
            var topic = topic_names[i];
            self.published_revisions[$ topic] = self.revisions[$ topic];
            self.dirty_topics[$ topic] = false;
        }
        self.publish_counter = 0;
        self.dirty = false;
        self.force_full = false;
    }

    function acknowledge(command_sequence, action, ok, code) {
        var ack = {
            protocol: DUALSCREEN_PROTOCOL_NAME,
            schema: DUALSCREEN_SCHEMA_VERSION,
            epoch: self.epoch,
            state_sequence: self.sequence,
            command_sequence: command_sequence,
            action: action,
            ok: ok,
            code: code,
        };
        dualscreen_atomic_json_write(
            DUALSCREEN_ACK_PATH,
            DUALSCREEN_ACK_NEW_PATH,
            DUALSCREEN_ACK_OLD_PATH,
            ack,
        );
    }

    function execute_command(command) {
        var command_sequence = command[$ "sequence"];
        var action = command[$ "action"];
        if command[$ "schema"] != DUALSCREEN_SCHEMA_VERSION {
            self.acknowledge(command_sequence ?? -1, action ?? "", false, "schema_mismatch");
            return;
        }
        if command[$ "epoch"] != self.epoch {
            self.acknowledge(command_sequence ?? -1, action ?? "", false, "stale_epoch");
            return;
        }
        if !is_real(command_sequence)
            || floor(command_sequence) != command_sequence
            || command_sequence <= self.last_command_sequence
        {
            self.acknowledge(command_sequence ?? -1, action ?? "", false, "invalid_sequence");
            return;
        }
        self.last_command_sequence = command_sequence;

        if !is_string(action) {
            self.acknowledge(command_sequence, "", false, "invalid_action");
            return;
        }

        // A panel requests this after an epoch, sequence, or revision-base gap.
        // It changes bridge transport state only; it does not mutate gameplay.
        if action == "resync" {
            self.force_full = true;
            self.dirty = true;
            self.acknowledge(command_sequence, action, true, "resync_queued");
            return;
        }

        // These operations require world/menu-specific vanilla contexts which
        // this independent panel cannot truthfully recreate.
        if matches(
            action,
            "use_item",
            "drop_item",
            "quest_start",
            "quest_progress",
            "quest_complete",
            "quest_track",
        ) {
            self.acknowledge(command_sequence, action, false, "action_disabled");
            return;
        }

        if !dualscreen_actions_are_safe() {
            self.acknowledge(command_sequence, action, false, "game_state_unsafe");
            return;
        }

        switch action {
            case "select_hotbar":
                var index = command[$ "index"];
                if !dualscreen_valid_slot_index(index) {
                    self.acknowledge(command_sequence, action, false, "invalid_slot");
                    return;
                }
                if !ARI.set_held_item_index(index) && ARI.held_item_index != index {
                    self.acknowledge(command_sequence, action, false, "selection_rejected");
                    return;
                }
                self.cache.hotbar = { selected_index: ARI.held_item_index };
                self.bump("hotbar");
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "swap_slots":
                var first = command[$ "first"];
                var second = command[$ "second"];
                if !dualscreen_valid_slot_index(first)
                    || !dualscreen_valid_slot_index(second)
                    || first == second
                {
                    self.acknowledge(command_sequence, action, false, "invalid_slot");
                    return;
                }
                ARI.inventory.slot(first).swap(ARI.inventory.slot(second));
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "equip_armor":
                var inventory_index = command[$ "inventory_index"];
                var armor_index = command[$ "armor_index"];
                if !dualscreen_valid_slot_index(inventory_index)
                    || !dualscreen_valid_armor_slot_index(armor_index)
                {
                    self.acknowledge(command_sequence, action, false, "invalid_slot");
                    return;
                }
                var source = ARI.inventory.slot(inventory_index);
                var target = ARI.armor.slot(armor_index);
                if !dualscreen_command_matches_slot(command, "expected_item", source)
                    || !dualscreen_command_matches_slot(command, "expected_equipped_item", target)
                {
                    self.acknowledge(command_sequence, action, false, "stale_item");
                    return;
                }
                if source.item == undefined || source.count != 1 {
                    self.acknowledge(command_sequence, action, false, "source_not_single_item");
                    return;
                }
                if !target.valid_for(source.item) {
                    self.acknowledge(command_sequence, action, false, "wrong_equipment_slot");
                    return;
                }
                if target.item == undefined {
                    if target.room_for_item(source.item) < 1 {
                        self.acknowledge(command_sequence, action, false, "equipment_slot_full");
                        return;
                    }
                    target.add(source.item, 1);
                    source.remove(1);
                } else {
                    if target.count != 1
                        || target.item.partial_eq(source.item)
                        || !source.valid_for(target.item)
                    {
                        self.acknowledge(command_sequence, action, false, "equipment_swap_rejected");
                        return;
                    }
                    // InventoryMenu's hand path ends in this same slot swap
                    // when one single equipment item replaces another.
                    source.swap(target);
                }
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "unequip_armor":
                var armor_index = command[$ "armor_index"];
                var inventory_index = command[$ "inventory_index"];
                if !dualscreen_valid_armor_slot_index(armor_index)
                    || !dualscreen_valid_slot_index(inventory_index)
                {
                    self.acknowledge(command_sequence, action, false, "invalid_slot");
                    return;
                }
                var source = ARI.armor.slot(armor_index);
                var target = ARI.inventory.slot(inventory_index);
                if !dualscreen_command_matches_slot(command, "expected_item", source)
                    || !dualscreen_command_matches_slot(command, "expected_destination_item", target)
                {
                    self.acknowledge(command_sequence, action, false, "stale_item");
                    return;
                }
                if source.item == undefined || source.count != 1 {
                    self.acknowledge(command_sequence, action, false, "equipment_slot_empty");
                    return;
                }
                // An occupied different inventory slot would leave the prior
                // stack in an invalid armor slot. Require the same final state
                // the vanilla hand transaction can complete safely.
                if target.item != undefined || target.room_for_item(source.item) < 1 {
                    self.acknowledge(command_sequence, action, false, "inventory_destination_full");
                    return;
                }
                target.add(source.item, 1);
                source.remove(1);
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "consume_item":
                var index = command[$ "index"];
                if !dualscreen_valid_slot_index(index) {
                    self.acknowledge(command_sequence, action, false, "invalid_slot");
                    return;
                }
                if ARI.held_item_index != index {
                    self.acknowledge(command_sequence, action, false, "selection_required");
                    return;
                }
                var slot = ARI.inventory.slot(index);
                var item = slot.item;
                if !dualscreen_command_matches_slot(command, "expected_item", slot) {
                    self.acknowledge(command_sequence, action, false, "stale_item");
                    return;
                }
                if item == undefined || slot.count <= 0 {
                    self.acknowledge(command_sequence, action, false, "empty_slot");
                    return;
                }
                if item.prototype.use != ItemUse.Consume || item.auto_use {
                    self.acknowledge(command_sequence, action, false, "not_consumable");
                    return;
                }
                if ARI.fire_breath_time > 0 || !item.can_use() {
                    self.acknowledge(command_sequence, action, false, "item_use_rejected");
                    return;
                }
                var output = use_item(item, obj_ari.cell_select.clone());
                if output != UseItemSuccess.RemoveLater {
                    self.acknowledge(command_sequence, action, false, "unexpected_use_result");
                    return;
                }
                // use_item_fast removes this key and is therefore not used.
                // Setting execute_instantly retains HoldToUse's real removal,
                // infusion/stat/effect, sound, and animation event path while
                // avoiding a synthetic Interact hold.
                obj_ari.fsm.blackboard.set("remove_item_at", index);
                obj_ari.fsm.blackboard.set("execute_instantly", true);
                self.acknowledge(command_sequence, action, true, "action_started");
                return;

            case "pin_spell":
                var spell = command[$ "spell"];
                if !dualscreen_valid_spell_index(spell) || !ARI.spells_learned[spell] {
                    self.acknowledge(command_sequence, action, false, "spell_not_learned");
                    return;
                }
                ARI.set_pinned_spell(spell);
                self.last_magic_pinned_spell = ARI.pinned_spell;
                self.cache.magic = dualscreen_build_magic(self.magic_spell_cache);
                self.bump("magic");
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "unpin_spell":
                var spell = command[$ "spell"];
                if !dualscreen_valid_spell_index(spell) || ARI.pinned_spell != spell {
                    self.acknowledge(command_sequence, action, false, "stale_pinned_spell");
                    return;
                }
                ARI.set_pinned_spell(undefined);
                self.last_magic_pinned_spell = ARI.pinned_spell;
                self.cache.magic = dualscreen_build_magic(self.magic_spell_cache);
                self.bump("magic");
                self.acknowledge(command_sequence, action, true, "ok");
                return;

            case "cast_pinned_spell":
                var spell = command[$ "spell"];
                if !dualscreen_valid_spell_index(spell)
                    || ARI.pinned_spell != spell
                    || !ARI.spells_learned[spell]
                {
                    self.acknowledge(command_sequence, action, false, "no_learned_pinned_spell");
                    return;
                }
                if !can_cast_spell(spell) {
                    self.acknowledge(command_sequence, action, false, "spell_not_castable");
                    return;
                }
                // This is the exact ui_spell_request branch in AriFsm's
                // normal state: same blackboard keys and Spell state. The
                // state owns animation, effect application, and mana cost.
                obj_ari.fsm.blackboard.set("spell", spell);
                obj_ari.fsm.blackboard.set("hold_to_use", false);
                obj_ari.fsm.change_state(PlayerState.Spell);
                self.acknowledge(command_sequence, action, true, "action_started");
                return;

            default:
                self.acknowledge(command_sequence, action, false, "unsupported_action");
                return;
        }
    }

    function poll_command() {
        if !file_exists(DUALSCREEN_COMMAND_PATH) {
            return;
        }
        if file_exists(DUALSCREEN_COMMAND_CLAIMED_PATH) {
            file_delete(DUALSCREEN_COMMAND_CLAIMED_PATH);
        }
        file_rename(DUALSCREEN_COMMAND_PATH, DUALSCREEN_COMMAND_CLAIMED_PATH);
        if !file_exists(DUALSCREEN_COMMAND_CLAIMED_PATH) {
            return;
        }

        try {
            var command = read_json_file(DUALSCREEN_COMMAND_CLAIMED_PATH);
            self.execute_command(command);
        } catch (e) {
            self.acknowledge(-1, "", false, "invalid_json");
            error("[DualScreen] failed to parse command: {}", e);
        }
        file_delete(DUALSCREEN_COMMAND_CLAIMED_PATH);
    }

    function on_room_start() {
        // Called only after Game.room_start has assigned GRID and created obj_ari.
        self.force_full = true;
        self.dirty = true;
        self.last_location_id = undefined;
        self.last_location_dynamic_index = undefined;
        self.last_location_room = undefined;
        self.last_player_x = undefined;
        self.last_player_y = undefined;
        self.last_player_dir = undefined;
        self.last_player_cardinal = undefined;
        self.last_player_in_mines = undefined;
        self.last_player_mine_floor = undefined;
        self.last_player_mine_biome = undefined;
        self.last_player_map_key = undefined;
        self.last_map_fingerprint = undefined;
        self.last_villager_fingerprint = undefined;
        self.last_animal_fingerprint = undefined;
        self.player_poll_counter = 3;
        self.social_poll_counter = 59;
        self.maintenance_counter = 14;
    }

    function update() {
        if self.shutting_down {
            return;
        }
        if !self.initialized {
            self.initialize_snapshot();
        }

        var inventory_changed = self.refresh_inventory();
        self.refresh_armor();
        self.refresh_renown_rewards();
        self.refresh_scalars();

        // Position/facing is sampled at most once per four End Steps. The
        // published delta cadence is bounded by the same four-step interval.
        self.player_poll_counter += 1;
        if self.player_poll_counter >= 4 {
            self.player_poll_counter = 0;
            self.refresh_player();
        }

        self.maintenance_counter += 1;
        if self.maintenance_counter >= 15 {
            self.maintenance_counter = 0;
            self.refresh_maintenance();
        } else if inventory_changed {
            // Inventory-backed quest readiness should not wait for maintenance.
            var quest_fingerprint = dualscreen_quest_fingerprint();
            if quest_fingerprint != self.last_quest_fingerprint {
                self.last_quest_fingerprint = quest_fingerprint;
                self.cache.quests = dualscreen_build_quests();
                self.bump("quests");
            }
        }

        // NPC schedules and owned-animal care/location state are bounded lists,
        // but they are intentionally scanned only once per 60 End Steps.
        self.social_poll_counter += 1;
        if self.social_poll_counter >= 60 {
            self.social_poll_counter = 0;
            self.refresh_social();
        }

        self.command_poll_counter += 1;
        if self.command_poll_counter >= 3 {
            self.command_poll_counter = 0;
            self.poll_command();
        }

        self.publish_counter += 1;
        if self.force_full || (self.dirty && self.publish_counter >= 4) {
            self.publish();
        }
    }

    function shutdown() {
        if self.shutting_down {
            return;
        }
        self.shutting_down = true;
        if self.initialized {
            self.cache.session.in_game = false;
            self.bump("session");
            self.force_full = false;
            self.publish();
        }
        trace("[DualScreen] live-state bridge stopped (epoch {})", self.epoch);
    }
}
