from __future__ import annotations

import hashlib
import importlib.util
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT / "tools" / "patch_assets.py"
SPEC = importlib.util.spec_from_file_location("fom_patch_assets", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
patch_assets = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = patch_assets
SPEC.loader.exec_module(patch_assets)


def synthetic_game_source() -> bytes:
    return b"\n".join(patch.anchor for patch in patch_assets.ANCHOR_PATCHES)


class GamePatchTests(unittest.TestCase):
    def setUp(self) -> None:
        self.original_hash = patch_assets.EXPECTED_GAME_SHA256
        self.source = synthetic_game_source()
        patch_assets.EXPECTED_GAME_SHA256 = hashlib.sha256(self.source).hexdigest()

    def tearDown(self) -> None:
        patch_assets.EXPECTED_GAME_SHA256 = self.original_hash

    def test_all_exact_anchors_are_patched_once(self) -> None:
        patched = patch_assets.patch_game_source(self.source)
        self.assertEqual(patched.count(b"new DualScreenBridge(self.unique_identifier)"), 1)
        self.assertEqual(patched.count(b"self.dualscreen_bridge.update()"), 1)
        self.assertEqual(patched.count(b"self.dualscreen_bridge.on_room_start()"), 1)
        self.assertEqual(patched.count(b"self.dualscreen_bridge.shutdown()"), 1)

    def test_duplicate_anchor_is_rejected(self) -> None:
        duplicated = self.source + b"\n" + patch_assets.ANCHOR_PATCHES[0].anchor
        patch_assets.EXPECTED_GAME_SHA256 = hashlib.sha256(duplicated).hexdigest()
        with self.assertRaises(patch_assets.PatchError):
            patch_assets.patch_game_source(duplicated)

    def test_wrong_game_hash_is_rejected(self) -> None:
        patch_assets.EXPECTED_GAME_SHA256 = "00" * 32
        with self.assertRaises(patch_assets.PatchError):
            patch_assets.patch_game_source(self.source)


class RawZipPatchTests(unittest.TestCase):
    def test_untouched_local_region_and_entries_are_preserved(self) -> None:
        game = b"original game source"
        patched_game = b"patched game source"
        bridge = b"function DualScreenBridge() constructor {}\n"
        keep = (b"unchanged compressed payload\x00" * 1024) + bytes(range(256))

        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            original_path = directory / "assets.zip"
            output_path = directory / "patched.zip"
            with zipfile.ZipFile(
                original_path, "w", compression=zipfile.ZIP_DEFLATED, allowZip64=True
            ) as archive:
                archive.comment = b"preserve-this-comment"
                archive.writestr("assets/keep.bin", keep)
                archive.writestr(patch_assets.GAME_ENTRY, game)
                archive.writestr("assets/second.txt", b"also unchanged")

            with zipfile.ZipFile(original_path, "r") as archive:
                start_dir = archive.start_dir
                count = len(archive.infolist())
                comment = archive.comment

            layout = patch_assets.build_archive(
                original_path,
                output_path,
                patched_game=patched_game,
                bridge=bridge,
                original_count=count,
                original_start_dir=start_dir,
                archive_comment=comment,
            )
            verified = patch_assets.verify_output(
                original_path,
                output_path,
                patched_game=patched_game,
                bridge=bridge,
                original_start_dir=start_dir,
                original_prefix_hash=str(layout["original_local_region_sha256"]),
                full_crc=True,
            )

            with original_path.open("rb") as original, output_path.open("rb") as output:
                self.assertEqual(original.read(start_dir), output.read(start_dir))
            with zipfile.ZipFile(output_path, "r") as archive:
                self.assertEqual(archive.comment, comment)
                self.assertEqual(archive.read("assets/keep.bin"), keep)
                self.assertEqual(archive.read(patch_assets.GAME_ENTRY), patched_game)
                self.assertEqual(archive.read(patch_assets.BRIDGE_ENTRY), bridge)
                self.assertEqual(
                    archive.getinfo(patch_assets.GAME_ENTRY).compress_type,
                    zipfile.ZIP_STORED,
                )
                self.assertEqual(
                    archive.getinfo(patch_assets.BRIDGE_ENTRY).compress_type,
                    zipfile.ZIP_STORED,
                )
                self.assertEqual(len(archive.infolist()), count + 1)
            self.assertTrue(verified["full_crc_verified"])


class IdentityGateTests(unittest.TestCase):
    def test_archive_hash_gate_rejects_unrecognized_input(self) -> None:
        with tempfile.TemporaryDirectory() as directory_name:
            directory = Path(directory_name)
            archive_path = directory / "assets.zip"
            bridge_path = directory / "bridge.gml"
            bridge_path.write_bytes(b"function DualScreenBridge() constructor {}")
            with zipfile.ZipFile(archive_path, "w") as archive:
                archive.writestr(patch_assets.GAME_ENTRY, b"not the inspected source")

            with self.assertRaises(patch_assets.PatchError):
                patch_assets.validate_input(
                    archive_path,
                    bridge_path,
                    expected_archive_hash="00" * 32,
                    expected_archive_size=archive_path.stat().st_size,
                    expected_entry_count=1,
                )


class BridgePolicyTests(unittest.TestCase):
    def test_bridge_contains_required_live_state_seams(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "new InventorySubscriber(ARI.inventory)",
            "new InventorySubscriber(ARI.armor)",
            "self.dirty_topics[$ topic] = true",
            "base_revisions: base_revisions",
            'if action == "resync"',
            "dualscreen_quest_fingerprint()",
            "dualscreen_build_player()",
            "dualscreen_villagers_fingerprint()",
            "dualscreen_animals_fingerprint(animals)",
            "dualscreen_magic_static_fingerprint()",
            "dualscreen_collection_fingerprints()",
            "new InventorySubscriber(RENOWN_REWARD_INVENTORY)",
            "dualscreen_calendar_events_fingerprint()",
            "self.player_poll_counter >= 4",
            "self.social_poll_counter >= 60",
            "dualscreen_atomic_json_write(",
            "function on_room_start()",
            "function update()",
            "function shutdown()",
        ):
            self.assertIn(marker, bridge)

        self.assertIn("instance_metadata: item.serialize()", bridge)
        self.assertNotIn("live: item.serialize()", bridge)
        self.assertNotIn("full: true", bridge)

    def test_social_topics_are_discovery_safe_and_read_only(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        self.assertIn(
            "return npc_is_unlocked(npc_id) && NPCS[npc_id].has_met();",
            bridge,
        )
        self.assertIn('discovery_policy: "unlocked_and_met_only"', bridge)
        self.assertIn("location: dualscreen_npc_live_location(npc)", bridge)
        self.assertIn("location: dualscreen_player_animal_live_location", bridge)
        self.assertIn("var animals = get_all_animals(true);", bridge)
        self.assertIn("var care_active = !in_daycare;", bridge)
        self.assertIn("pet: dualscreen_build_pet()", bridge)
        for marker in (
            "icon_asset: asset_to_string(get_small_npc_icon(i))",
            "portrait_asset: dualscreen_villager_portrait_asset(i)",
            'portraits.get("neutral")',
            "var icon_data = get_animal_icon_data(",
            "icon_asset: dualscreen_optional_asset_name(icon_data.sprite)",
            "var variant = PET_PROTOTYPE.variants.get(PET.variant);",
            "map_icon_asset: dualscreen_optional_asset_name(variant.map_icon)",
        ):
            self.assertIn(marker, bridge)
        for forbidden in (
            "known_gift_preferences",
            ".loved_gifts",
            ".liked_gifts",
            ".hated_gift",
            ".disliked_gift_tags",
            'case "pet_animal":',
            'case "feed_animal":',
            'case "gift_npc":',
        ):
            self.assertNotIn(forbidden, bridge)

    def test_player_topic_uses_exact_runtime_owners(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "x: obj_ari.x",
            "y: obj_ari.y",
            "facing_degrees: obj_ari.dir",
            "facing: cardinal_to_string(obj_ari.cardinal)",
            "DUNGEON_RUNNER != undefined && is_dungeon_room(room())",
            "floor_index: DUNGEON_FLOOR",
            "display_floor: DUNGEON_FLOOR + 1",
            "biome_id: DUNGEON_BIOME",
        ):
            self.assertIn(marker, bridge)

    def test_magic_exposes_only_learned_spells_and_uses_vanilla_fsm(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "if !ARI.spells_learned[i]",
            "index: i",
            "id: spell_to_string(i)",
            "display_name: local_get(spell.name)",
            "description: local_get(spell.description)",
            "upper_icon_asset: dualscreen_optional_asset_name(spell.upper_icon)",
            "lower_icon_key: spell.icon_key",
            "var lower_icon_sprites = load_button_sprites(spell.icon_key);",
            "lower_icon_asset: dualscreen_optional_asset_name(lower_icon_sprites.enabled_sprite)",
            "ribbon_asset: dualscreen_optional_asset_name(spell.ribbon)",
            "mana_cost: spell.cost",
            "pinned_spell = spell_to_string(ARI.pinned_spell)",
            "pinned_spell_index = ARI.pinned_spell",
            "pinned_spell_index: pinned_spell_index",
            "remaining_frames: max(0, ARI.fire_breath_time)",
            "index: Spell.FireBreath",
            "self.magic_spell_cache = dualscreen_build_magic_static()",
            'case "pin_spell":',
            'case "unpin_spell":',
            'case "cast_pinned_spell":',
            "ARI.set_pinned_spell(spell);",
            'obj_ari.fsm.blackboard.set("spell", spell);',
            'obj_ari.fsm.blackboard.set("hold_to_use", false);',
            "obj_ari.fsm.change_state(PlayerState.Spell);",
            "if !can_cast_spell(spell)",
        ):
            self.assertIn(marker, bridge)
        for forbidden in (
            "\n    cast_spell(",
            ".learn_spell(",
            'case "learn_spell":',
        ):
            self.assertNotIn(forbidden, bridge)

    def test_map_uses_authored_hubs_and_vanilla_discovery_gates(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "LOCATIONS[location_id].map_location",
            "function dualscreen_location_display_name(id)",
            "if location.name != undefined",
            "var map_id = location.map_location;",
            "if map_location != undefined && map_location.name != undefined",
            "display_name: dualscreen_location_display_name(id)",
            "display_name: dualscreen_location_display_name(map_id)",
            "var source_hubs = MAP_HUBS[map_id];",
            "world_x: source_hub.x",
            "world_y: source_hub.y",
            "map_x: source_hub.asset_x",
            "map_y: source_hub.asset_y",
            "dualscreen_find_map_hub_index(",
            "if !npc_is_unlocked(i)",
            'met ? "npc" : "unknown_npc"',
            "met ? npc_id_to_string(i) : undefined",
            "world_mod_enabled(WorldMod.ThornyStairs)",
            "world_mod_enabled(WorldMod.DragonswornGlade)",
            "continuous_world_projection_supported: false",
        ):
            self.assertIn(marker, bridge)
        self.assertNotIn("local_get(LOCATIONS[id].name)", bridge)
        self.assertNotIn("local_get(LOCATIONS[map_id].name)", bridge)
        self.assertNotIn("world_to_map", bridge)

    def test_inventory_metadata_and_equipment_follow_slot_primitives(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "tags: prototype.tags.to_array()",
            "icon_asset: dualscreen_optional_asset_name(item.get_ui_icon())",
            "museum_eligible: MUSEUM_DATA.museum_items[item.item_id]",
            "museum_donated: MUSEUM_DATA.museum_items[item.item_id]",
            "required_tags: slot.required_tags.to_array()",
            "instance_fingerprint: json_stringify(item.serialize())",
            'case "equip_armor":',
            'case "unequip_armor":',
            "target.valid_for(source.item)",
            "target.room_for_item(source.item)",
            "source.swap(target);",
            'dualscreen_command_matches_slot(command, "expected_item", source)',
            'dualscreen_command_matches_slot(command, "expected_equipped_item", target)',
        ):
            self.assertIn(marker, bridge)

        # Exact v1.0.4 exposes the logical asset identifier to GML, not its
        # archive central-directory path. Never fabricate a metadata path.
        self.assertNotIn("icon_metadata_path", bridge)

    def test_skill_visual_uses_vanilla_fiddle_asset(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        self.assertIn('fiddle_get(format("skills/{Skill}/sprite", i))', bridge)

    def test_collections_never_emit_unknown_item_identity(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "if ARI.items_acquired[item_id]",
            "var discovered = ARI.items_acquired[item_id] || donated;",
            "if discovered {",
            "unknown_entries_exposed: false",
            "future_rewards_exposed: false",
            "self.social_poll_counter >= 60",
        ):
            self.assertIn(marker, bridge)
        self.assertNotIn("known_unknown_items", bridge)

    def test_renown_exposes_only_earned_rewards(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "new InventorySubscriber(RENOWN_REWARD_INVENTORY)",
            "renown_level_total_cost(level + 1)",
            "renown_level_to_rank(level)",
            "claimable_rewards:",
            "slots: reward_cache",
            "future_rewards_exposed: false",
        ):
            self.assertIn(marker, bridge)
        self.assertNotIn("RENOWN.rewards.get", bridge)
        self.assertNotIn("consume_reward(", bridge)

    def test_calendar_visibility_matches_public_progression_gates(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "static HORIZON_DAYS = 28",
            "festival.prototype.implemented",
            "requirements_pass(Requirement.SaturdayMarketUnlocked)",
            "if !dualscreen_villager_is_visible(i)",
            "ARI.pending_child.due_date",
            "ARI.wedding_date",
            "private_date_availability_exposed: false",
            "unreleased_festivals_exposed: false",
        ):
            self.assertIn(marker, bridge)
        self.assertNotIn("npc_date_eligibility(", bridge)
        self.assertNotIn("legendary_fish_caught", bridge)

    def test_only_source_confirmed_mutating_actions_are_enabled(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        self.assertEqual(bridge.count('case "select_hotbar":'), 1)
        self.assertEqual(bridge.count('case "swap_slots":'), 1)
        self.assertEqual(bridge.count('case "equip_armor":'), 1)
        self.assertEqual(bridge.count('case "unequip_armor":'), 1)
        self.assertEqual(bridge.count('case "consume_item":'), 1)
        self.assertIn("var output = use_item(item, obj_ari.cell_select.clone());", bridge)
        self.assertIn('obj_ari.fsm.blackboard.set("remove_item_at", index);', bridge)
        self.assertIn('obj_ari.fsm.blackboard.set("execute_instantly", true);', bridge)
        self.assertIn('dualscreen_command_matches_slot(command, "expected_item", slot)', bridge)
        self.assertNotIn("use_item_fast(", bridge)
        self.assertNotIn("QUEST_LOG.start(", bridge)
        self.assertNotIn(".progress();", bridge)
        self.assertNotIn("QUEST_LOG.complete(", bridge)
        self.assertIn('"drop_item",', bridge)
        self.assertIn('"quest_track",', bridge)
        self.assertIn('"action_disabled"', bridge)

    def test_quest_details_are_read_only_and_source_backed(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for marker in (
            "ARI.inventory.item_id_quantity(data[0])",
            "ARI.times_item_tag_sold(data[0])",
            "ARI.items_sold[data[0]]",
            "ARI.monsters_killed[data[0]]",
            "trellis_point_location_position(supply.point)",
            "npc_for_icon:",
            "dualscreen_build_quest_targets(task)",
            "dualscreen_build_quest_rewards(quest.reward_list)",
            "tracked_supported: false",
        ):
            self.assertIn(marker, bridge)

    def test_bridge_does_not_poll_or_serialize_game_saves(self) -> None:
        bridge = (ROOT / "gml" / "DualScreenBridge.gml").read_text(encoding="utf-8")
        for forbidden in ("load_game(", "save_game(", "exact_save_path(", ".sav"):
            self.assertNotIn(forbidden, bridge)


if __name__ == "__main__":
    unittest.main()
