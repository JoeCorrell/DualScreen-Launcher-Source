# Exact-v1.0.4 GML bridge and patch protocol

## Status

This is the live-state bridge for the inspected Fields of Mistria v1.0.4 Maybe-runtime build. The current 20-topic source generation and its narrowly gated vanilla-path actions passed all policy, exact-archive, ZIP64, and full-CRC checks and was transactionally installed on the inspected device. A post-install read-only audit verified the exact current archive as a no-op. In-game GML compilation and live-state/action behavior still require runtime proof.

The bridge contains no game assets and does not read or serialize saves. It reads the live GML models documented in `GML_STATE_RECON.md` and publishes a bounded custom protocol.

## Source layout

- Custom bridge: `gml/DualScreenBridge.gml`
- Exact archive patcher: `tools/patch_assets.py`
- PowerShell entry point: `tools/Build-GmlBridgePatch.ps1`
- Tests: `tests/test_patch_assets.py` and `tests/Run-GmlBridgeTests.ps1`

The patcher adds the bridge as `assets/gml/scripts/DualScreenBridge.gml` and replaces only the central-directory-visible `assets/gml/objects/Game.gml` entry. Every other original local ZIP record, compressed payload, and central record is copied byte-for-byte.

## Runtime lifecycle

The exact Game source is patched at four verified anchors:

1. After the successful load/new switch: construct `self.dualscreen_bridge` using `Game.unique_identifier`.
2. At `Game.step_end`: call the bounded bridge update on the GML thread.
3. At the end of `Game.room_start`: flag a full/location refresh after `GRID` assignment and `obj_ari` creation.
4. At the beginning of `Game.cleanup`: publish `session.in_game=false` and stop processing commands.

The service is owned by the persistent `Game` instance. It does not retain `obj_ari` or `CLOCK` references across room/new-day transitions.

## Transport

All paths are beneath `CONFIG_DIRECTORY + "/dualscreen"`:

| File | Direction | Purpose |
|---|---|---|
| `state.json` | game to panel | Latest full snapshot or coalesced topic delta |
| `state.new.json` | internal | Temporary state write |
| `state.old.json` | internal/fallback | Previous valid state during atomic replacement |
| `command.json` | panel to game | One command envelope, written atomically by the panel |
| `command.claimed.json` | internal | Command claimed by the GML thread |
| `ack.json` | game to panel | Latest command acknowledgement |
| `ack.new.json` | internal | Temporary acknowledgement write |
| `ack.old.json` | internal/fallback | Previous valid acknowledgement |

The writer follows the current game's settings-file pattern: remove stale temporary/old files, rename current to old, write new, and rename new to current. Sequence and epoch validation remain mandatory even if the container exposes rename atomically.

The panel must discover the host-visible location corresponding to `game_save_id`/`CONFIG_DIRECTORY` at runtime. That path is container-specific and is deliberately not guessed in this implementation.

## State envelope

The first state in an epoch, every post-room-start state, and a requested resynchronization are full snapshots. Ordinary writes are coalesced topic deltas. The bridge never rebuilds or serializes inventory merely because HP, stamina, or clock state changed.

Top-level fields:

| Field | Meaning |
|---|---|
| `protocol` | `fields-of-mistria-dualscreen` |
| `schema` | Integer schema; currently `1` |
| `epoch` | Changes for every new `Game` bridge instance |
| `sequence` | Monotonic state sequence within the epoch |
| `full` | `true` for an independent snapshot; `false` for a delta |
| `base_sequence` | `0` for a full snapshot; for a delta, the exact panel sequence to which it applies |
| `generated_tick` | Game `TICK` at publication |
| `base_revisions` | Empty for a full snapshot; for a delta, exact per-topic revisions before the write |
| `revisions` | Per-topic monotonic revisions |
| `topics` | All topics for a full snapshot; only changed, complete topic values for a delta |

The panel accepts a delta only when epoch matches, its cached sequence equals `base_sequence`, and all cached topic revisions equal `base_revisions`. It then replaces each member in `topics` and adopts `sequence`/`revisions`. A mismatch means the state file was overwritten before the panel consumed an intermediate delta; the panel must send the non-gameplay `resync` command and wait for `full:true`. A full snapshot always replaces the whole cache. This makes missed-file recovery explicit instead of silently combining incompatible revisions.

Topics:

- `session`: in-game state, exact runtime version, game identifier, and capability flags.
- `status`: identity, HP, stamina, mana, tesserae, essence, and renown.
- `progression`: skills with the exact `skills/{Skill}/sprite` logical icon resolved by vanilla `SkillProgressUi`, acquired/active perks, and status effects.
- `inventory`: current size and flattened slots, including use/category tags, source-owned logical icon names, instance fingerprints, and museum eligibility/donation state.
- `armor`: current size and flattened equipment slots, including each slot's exact required tags and `equipped=true` marker.
- `hotbar`: selected inventory index.
- `location`: location ID, dynamic index, room name, diagnostic/display names, and outdoor flag.
- `player`: sampled live `obj_ari` x/y coordinates, quantized facing degrees/cardinal, and a source-gated mine context. Mine context carries zero-based `floor_index`, vanilla-style one-based `display_floor`, biome ID/name, and is populated only while `DUNGEON_RUNNER` exists and `is_dungeon_room(room())` is true.
- `map`: the current `LOCATIONS[...].map_location` base map and the vanilla map-tab pages. Each unlocked page carries its resolved base-map/tab asset names, `MAP_HUBS` indexes, authored world anchors and `asset_x`/`asset_y` display coordinates, and the exact 11x11 hub-relative marker layout. Player and unlocked NPCs are assigned with the same position/location hub rules as `MapMenu.find_hub_for`. An unmet NPC may appear only as an anonymous `unknown_npc`; its ID is omitted. Dragonsworn Glade is absent until its world mod exists, and locked Deep Woods emits only `spr_ui_map_locked`, with no authored hubs or residents. No continuous world-to-image projection is claimed.
- `villagers`: only currently unlocked NPCs the player has met. Each entry carries stable/localized identity, the exact `get_small_npc_icon(...)` logical asset, the current neutral wardrobe portrait (with the same fallback outfit, offset, and background color as `npc_polaroid_ui`), heart points/level, normalized relationship status, today's talk/gift checks, and live location. When the NPC instance exists its exact transform is used; otherwise the schedule-owned `Npc.location_position` is used. Unmet/locked identities and every gift-preference collection are excluded.
- `animals`: all player livestock returned by `get_all_animals(true)`, including daycare residents, plus the unlocked `PET`. Livestock entries contain owned identity/type/variant/sex/age/hearts, the exact `get_animal_icon_data(kind, variant, sex, adult)` sprite/LUT fields used by `AnimalMenu`, current location when meaningful, and daily food/petting attention. An instantiated animal uses its exact transform and otherwise its model `location_position`. Daycare entries deliberately report inactive attention and no location because v1.0.4 retains their prior location and `daycare_new_day()` performs no daily care cycle. Pet entries use the unlocked current `PET_PROTOTYPE.variants[PET.variant]` UI/map icon and optional LUT; attention and the source-owned location ID are included, while x/y is present only when `obj_pet` exists.
- `magic`: only spells for which `ARI.spells_learned[id]` is true. Each learned entry carries its source enum `index` (never inferred from the filtered array position), stable string `id`, localized type/description, raw mana cost, the vanilla menu's `cost div 4` orb count, and the source-owned visual references used by `SpellcastingMenu`: `SPELLS[id].upper_icon`, the exact `icon_key`, that key's `load_button_sprites(...).enabled_sprite`, and `ribbon`. `pinned_spell_index` and its stable string `pinned_spell` are emitted only when the current pin is still learned. The cooldown list contains only Fire Breath's source-confirmed countdown-backed gate and carries both identities. Pin/unpin and exact pinned-spell FSM casting are supported.
- `almanac`: the exact vanilla category membership/counts, but item identity/name/description and its resolved logical icon asset appear only after `ARI.items_acquired[item_id]`. Unknown members contribute to totals without exposing IDs or metadata.
- `museum`: public wing/set labels and progress totals from `MUSEUM_DATA`/`MUSEUM_PROGRESS`. An item identity is emitted only after it has been acquired or donated; no unknown item identity and no museum reward table is published.
- `renown`: current total, pending total, projected level, current level progress/threshold, current and next public rank label, plus only already-earned claimable items present in `RENOWN_REWARD_INVENTORY`. The future `RENOWN.rewards` table is never read or published, and reward claiming is unsupported.
- `calendar_events`: a rolling 28-day list of implemented festivals, Friday night at the inn, progression-gated Saturday market, the player's wedding/anniversary, a pending child's exact due date, existing children/player birthdays, and birthdays of only met/unlocked villagers. Private date availability, legendary-fish hints, unreleased festival definitions, and unmatched NPC identities are omitted.
- `calendar`: raw calendar time plus zero-based and display year/day values.
- `clock`: raw clock, hour, minute, formatted time, and day/night flags.
- `weather`: current/tomorrow IDs, inclement flag, and spell override state.
- `quests`: active identity/text/current objective/readiness, source-confirmed objective quantities, current-stage NPC/location query targets, and the same public final reward kinds rendered by the vanilla journal. `tracked_supported` remains false because v1.0.4 has no persistent tracked-quest model.

Empty inventory slots are explicit. Populated slots include the stable string item ID, localized name/description, count, raw/use-name, a documented custom UI category derived from exact use/tag fields, raw prototype tags, stack/stat modifiers, quality/stars, values, museum flags, and `instance_metadata`. `instance_fingerprint` is the deterministic JSON text of that same bounded metadata and is required by item-targeted commands as a stale-view/variant guard.

`icon_asset` is never a numeric sprite handle. It is `asset_to_string(item.get_ui_icon())`, where `LiveItem.get_ui_icon()` is the source-owned resolver that handles cosmetics, animal/pet cosmetics, Mist overrides, and the normal `ITEM_PROTOTYPES[item_id].icon_sprite` fallback. `sub_icon_asset` similarly comes from `get_ui_sub_icon()`; optional LUT metadata comes from `get_ui_lut()`. Almanac/museum item icons are resolved only after their existing acquired/donated disclosure gate.

Exact v1.0.4 GML exposes the runtime asset handle and `asset_to_string(...)` logical identifier, but no source API exposes the ZIP central-directory `.meta.toml` path. A repository-wide source audit found `asset_get_ids(...)`/`asset_to_string(...)` only; there is no metadata-path function or retained path property. The bridge therefore does **not** emit or infer `metadata_path`. The trusted native archive provider must resolve the published logical ID through an index built from the already hash-gated installed archive, and return the exact indexed central-entry path together with the ID. It must fail closed on absence or ambiguity.

## Update behavior

- HP/stamina/mana/tesserae/essence/renown/hotbar: allocation-free scalar comparison each End Step; the compact topic is rebuilt only on change.
- Inventory/armor: independent `InventorySubscriber.pull()` calls update cached slots only when slot counters change; resizing forces a topic rebuild.
- Clock: only the displayed hour/minute fingerprint dirties the topic.
- Calendar: raw scalar comparison. Location: raw location ID, dynamic index, and room-asset comparison; localization and diagnostic formatting run only after identity changes or a room-start notification.
- Player coordinates/facing/mine context: scalar comparison every four End Steps (at most about 15 Hz at 60 FPS); localized mine metadata is built only after a change. A room-start notification forces the next sample before its full snapshot.
- Map: player hub identity is checked with the four-End-Step player sample; unlocked NPC location/icon state and all authored pages are fingerprinted in the existing 60-End-Step social pass. Pages rebuild only when the player changes hub/base map, a visible marker changes, localization changes, farm expansion changes, or the Deep Woods/Glade source gate changes.
- Villagers/relationships and owned animals: bounded fingerprints once per 60 End Steps (about 1 Hz at 60 FPS). Only a changed fingerprint rebuilds/localizes the complete topic. The animal list is collected once and reused for fingerprinting and rebuilding in that pass.
- Magic: learned-spell and language changes rebuild the cached bounded metadata list; pinned spell and the sole explicit Fire Breath countdown are compared in the existing 15-End-Step maintenance pass. Unchanged spell descriptions are not localized or serialized again.
- Almanac/museum: static category/wing/set membership is cached after initialization and rebuilt only when localization changes. Once per 60 End Steps, compact acquired/donated fingerprints are compared; complete discovery-safe topics are rebuilt only after a change.
- Renown: scalar/pending totals are compared in the 15-End-Step maintenance pass. Its earned reward inventory has its own `InventorySubscriber`, so reward slots are rebuilt only when their slot counters/size change. The future reward table is never fingerprinted.
- Calendar events: one compact, progression-safe fingerprint is evaluated with the 60-End-Step social pass. The rolling 28-day event list is rebuilt only after date, localization, family, Saturday-market availability, or met/unlocked-NPC membership changes.
- Skills/perks/status effects/weather/quests: bounded maintenance pass every 15 frames.
- Inventory changes also re-evaluate current quest quantities/readiness immediately. Other objective counters are compared in the bounded 15-frame maintenance pass.
- Dirty topics are coalesced for up to four End Steps (about 15 Hz at the normal 60 FPS). Forced initial/room/resync snapshots publish immediately.
- A delta serializes only its complete changed topics. The initial inventory is full; later inventory work is subscriber-driven and appears only in an inventory delta.
- Commands are checked every three frames and at most one claimed command file is processed per check.

The initial update after `Game.room_start` builds all caches and emits a full snapshot. A different save/new game creates a different epoch and a new full snapshot.

## Commands and acknowledgements

A command is one JSON object with:

- `schema`: must equal `1`.
- `epoch`: must equal the latest state epoch.
- `sequence`: integer, strictly greater than the last accepted command sequence.
- `action`: supported action name.
- Action parameters.

The game claims `command.json` by renaming it before parsing. Every parsed command produces `ack.json` with protocol/schema/epoch, current state sequence, command sequence, action, Boolean `ok`, and a stable result `code`.

The bridge accepts one transport-only command and the following narrowly gated gameplay actions. Native `ActionTransport` has a typed queue for each action, with validated capability/topic-revision/index gates, bounded escaped instance fingerprints for item mutations, and explicit source enum indexes for spell actions. Panel controls remain responsible for exposing only actions represented by the current validated snapshot.

### `resync`

No parameters. After normal schema/epoch/command-sequence checks, it queues an immediate full snapshot and acknowledges with `resync_queued`. It does not pass the gameplay action gate or mutate game state.

### `select_hotbar`

Parameter: integer `index` within the current `ARI.inventory` size.

Execution uses `ARI.set_held_item_index(index)`, preserving the game's own state checks and vanilla toolbar synchronization.

### `swap_slots`

Parameters: distinct integer `first` and `second` indexes within the current main inventory.

Execution uses the same `InventorySlot.swap(...)` primitive as vanilla toolbar drag/drop. Both slots increment their update counters, so the next bridge update publishes the result.

### `equip_armor`

Parameters: integer `inventory_index`, integer `armor_index`, string `expected_item`, and string `expected_equipped_item`. The expected fields must exactly equal the corresponding slot's published `instance_fingerprint`; an empty target uses the empty string.

The source inventory stack and any occupied equipment stack must each contain exactly one item. The bridge calls the armor slot's real `valid_for(...)`/`room_for_item(...)` checks. Empty placement uses the same `add`/`remove` primitives as `InventoryMenu`; replacement uses the same final `InventorySlot.swap(...)` only after both directions are valid. All other shapes fail closed.

### `unequip_armor`

Parameters: integer `armor_index`, integer `inventory_index`, string `expected_item`, and string `expected_destination_item`. The equipment slot must contain exactly one item and the destination must still be empty (`expected_destination_item` is `""`). The bridge validates `room_for_item`, then performs vanilla `add`/`remove`. It will not place an arbitrary inventory stack into an armor slot as an attempted atomic swap.

### `consume_item`

Parameters: integer `index` and string `expected_item`, which must match the published instance fingerprint. The slot must be the currently selected hotbar slot, non-empty, non-auto-use, and have exact `ItemUse.Consume`; the normal common gate, Fire Breath gate, and `LiveItem.can_use()` must all pass.

Execution calls `use_item(item, obj_ari.cell_select.clone())`, requires `UseItemSuccess.RemoveLater`, preserves `remove_item_at`, and sets `execute_instantly=true`. The existing `PlayerState.HoldToUse` then owns the real pop, eat animation broadcast, stat restoration, infusions/status effects, game statistics, audio, and exit. This deliberately does not call `use_item_fast`, because v1.0.4's helper removes `remove_item_at` and would apply effects without consuming the inventory item.

### `pin_spell` / `unpin_spell`

`pin_spell` takes integer `spell`; it must be in range and currently learned, then calls `ARI.set_pinned_spell(spell)`. `unpin_spell` also takes the expected current integer `spell` and fails with `stale_pinned_spell` if the pin changed before execution; it calls `ARI.set_pinned_spell(undefined)`.

### `cast_pinned_spell`

Parameter: expected integer `spell`. It must still equal `ARI.pinned_spell`, remain learned, and pass `can_cast_spell(spell)` after the common gate. The bridge writes the exact normal-state UI request blackboard values (`spell`, `hold_to_use=false`) and changes to `PlayerState.Spell`. The vanilla Spell state retains the animation, contextual effect, rumble/audio, and mana deduction. The bridge never calls `cast_spell(...)` directly.

### Strict common action gate

Every gameplay action above is rejected unless all of the following are true:

- current room is a world room;
- `obj_ari` exists;
- the game is not paused;
- the game is not returning to the title;
- no end-of-day sequence is active;
- the player is not holding an animal;
- Mist/cutscene execution is inactive;
- the player's current FSM state is normal and has no pending next state;
- the vanilla toolbar exists;
- textbox and end-of-day menus are absent.

Known result codes include `ok`, `action_started`, `resync_queued`, `schema_mismatch`, `stale_epoch`, `invalid_sequence`, `invalid_action`, `game_state_unsafe`, `invalid_slot`, `stale_item`, `selection_rejected`, `source_not_single_item`, `wrong_equipment_slot`, `equipment_slot_full`, `equipment_swap_rejected`, `equipment_slot_empty`, `inventory_destination_full`, `selection_required`, `empty_slot`, `not_consumable`, `item_use_rejected`, `unexpected_use_result`, `spell_not_learned`, `stale_pinned_spell`, `no_learned_pinned_spell`, `spell_not_castable`, `action_disabled`, `unsupported_action`, and `invalid_json`.

Generic item use, world drop, and every quest mutation remain explicitly disabled. Tools, attacks, seeds, placement, bombs, unlockables, and other use types depend on cell targeting, animation, world collision, menus, or follow-up state that a generic command cannot preserve. World throwing drains a stack (or pops one bomb), writes `items_thrown`, enters `PlayerState.ThrowItem`, and creates a world instance later; journal trash permanently deletes a non-soulbound hand item. Neither is mislabeled as a safe generic `drop_item`.

Quest tracking is not an omitted implementation: no persistent tracked-quest owner exists in exact v1.0.4. `QuestLogMenu.active_quest` is menu-local selection only. `quest_track`, `QUEST_LOG.start`, `ActiveQuest.progress`, and `QUEST_LOG.complete` remain disabled because the latter paths consume requirements, grant rewards, alter boards/world grids, and trigger context-specific dialogue/popups.

There are no villager, animal, almanac, museum, renown, or calendar-event commands in schema 1. The bridge does not gift, talk, pet, feed, move, breed, rename, learn spells, donate, claim rewards, or mutate calendar/family state.

## Exact archive gate and transactional output

Accepted input identity:

- Size: `625619136` bytes
- Entries: `122403`
- SHA-256: `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450`
- Original `Game.gml` SHA-256: `82cccb93e460246b52033b9dc56a76da5460dc27133150bcbfb40a7bdcbb43e8`

The patcher additionally requires each of the four content anchors to appear exactly once and refuses an archive that already contains the bridge entry. Both appended replacement entries use `ZIP_STORED`, matching all 122,403 entries in the inspected archive, and output verification asserts that method.

It never edits the input. It writes a same-directory partial output, flushes it, verifies preserved bytes/entries, verifies both modified payloads, optionally decompresses every entry for CRC checking (default), and then atomically replaces the requested output path. A JSON manifest is written transactionally after the archive succeeds.

The original `Game.gml` local record remains in the preserved original byte region but is no longer referenced by the rebuilt central directory. This is intentional: it allows all original compressed data to remain byte-identical. The new patched Game and bridge records are appended, followed by a rebuilt ZIP64 directory containing byte-identical central records for all untouched entries.

Before any future installation, deployment tooling must:

1. Reconfirm the installed source hash is the exact expected original.
2. Create and verify `assets.zip.fomds-original-e55ef6bcaa743000` beside the game archive.
3. Never overwrite the only verified original.
4. Install through a same-directory temporary file and atomic rename.
5. Retain a tested rollback path that restores the verified original.

The patch generator itself never deploys or mutates the device. The separate, explicitly committed transaction below consumes its verified local output.

## Separate archive deployment transaction

`deploy-assets.ps1` installs only the locally staged GML archive and deliberately does not share or modify the SDL/native `deploy.ps1` transaction. It is dry-run-only unless `-Commit` is supplied.

The asset deployer accepts exactly two remote states:

- **Fresh:** `assets.zip` is the exact original hash/size and the required backup, asset owner marker, and asset deployment manifest are all absent.
- **Owned:** live `assets.zip` exactly matches the hash/size declared by a valid asset-owner manifest, the owner marker is exact, and `assets.zip.fomds-original-e55ef6bcaa743000` remains the exact original hash/size.

Partial, unowned, missing, directory, and symlinked managed paths are rejected. The deployer validates the final local archive and patch-manifest hashes, sizes, entry count, Game/bridge entry hashes, stored-entry proof, full-CRC proof, and backup contract before connecting.

Before commit it requires enough free space on the game directory's filesystem for the staged archive plus a 64 MiB rollback margin. It rejects the exact Fields of Mistria process and exact managed-file mappings. Unrelated launcher, Wine, FEX, or Box64 processes are not targets. After staging and hash verification, it repeats the process/map, executable, install-state, free-space, and staged-file checks immediately before the first rename.

Fresh installation renames the original archive to the required backup in the same directory, then renames the staged patched archive into place. An owned update similarly renames the prior owned generation aside for rollback; it never moves or replaces the verified original backup. Owner and deployment manifests contain only clean version/hash metadata, not host paths. Every commit path verifies the final `Owned` state; failures explicitly restore the preceding state. An exact current owned install is a verified no-op.

Classifier self-test:

```powershell
.\deploy-assets.ps1 -SelfTest
```

Read-only device audit (default):

```powershell
.\deploy-assets.ps1 `
  -Endpoint '100.83.63.70:5555' `
  -RemoteGame '/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4' `
  -Adb 'C:\Android\Sdk\platform-tools\adb.exe'
```

Installation requires adding `-Commit` to the same explicit invocation. The script feeds programs to `adb shell sh` over stdin and does not use the argument-unsafe `adb shell sh -c` form.

## Commands for local validation

Dry-run identity and anchor validation:

```powershell
.\tools\Build-GmlBridgePatch.ps1 -InputArchive 'X:\path\to\assets.zip' -DryRun
```

Unit tests plus optional exact-archive dry run:

```powershell
.\tests\Run-GmlBridgeTests.ps1 -ExactArchive 'X:\path\to\assets.zip'
```

Generate a new archive without touching the input:

```powershell
.\tools\Build-GmlBridgePatch.ps1 `
  -InputArchive 'X:\path\to\assets.zip' `
  -OutputArchive '.\build\gml-patch\assets.zip' `
  -Manifest '.\build\gml-patch\assets.zip.manifest.json'
```

## Local proof and source-generation boundary

The exact real archive has passed:

- executable/file identity recon;
- exact archive dry-run gate;
- all bridge/patch policy unit tests;
- exact anchor replacement;
- ZIP64 open/read validation;
- all-entry CRC decompression;
- all untouched entry CRC/size/compressed-size comparison;
- byte-for-byte verification of the original 608,744,200-byte local-record region.

The following hashes describe the previously built/deployed bridge generation and are retained only as its historical audit record:

- bridge SHA-256: `f14ee23ff4487598321a9967b40d2371fc11eb5549344ab4c6fe15cb18f4fc2c`;
- patched `Game.gml` SHA-256: `9a14923fafb91a3a79cf2e0724445baaa958ee632c75c1fc7f9d51d184997293`;
- patched archive SHA-256: `9ac5ac6b15fc91e1f66f2ad2afb46483e47fc5dacb0248cd17d289ce5e1baba2`;
- patched archive size/entries: `625685904` bytes / `122404` entries;
- both appended entries use compression method `0` (`ZIP_STORED`).

The installed current generation adds magic, collection, renown,
public-calendar, detailed inventory/quest, and authored-map topics plus the
gated actions described above. Its verified identities are:

- bridge SHA-256: `1f0d300410e9f5d1358e9133a2cf0dcab56920f7adf1e989778c73bd638830b2`;
- patched `Game.gml` SHA-256: `9a14923fafb91a3a79cf2e0724445baaa958ee632c75c1fc7f9d51d184997293`;
- patch manifest SHA-256: `d86ea8a05427a3512fa785b0562c1df05f2a5df63310435546494cd1f264361f`;
- patched archive SHA-256: `8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59`;
- patched archive size/entries: `625762141` bytes / `122404` entries.

Runtime compilation was proved by reaching bridge snapshot initialization in a
real save. That first run exposed and precisely located an optional location
name passed to `local_get`; the guarded correction is installed, and a second
live save-load test remains pending.

On 2026-08-30 the device at `100.83.63.70:34893` passed the deployer's
read-only `Owned` audit, then completed the committed owned-update transaction.
A second read-only audit classified it as `Owned` and verified an exact-current
no-op: the live
archive matches the audited patched hash, the permanent backup matches the
original hash, the ownership metadata is valid, and no runtime process or
archive mapping was active.
