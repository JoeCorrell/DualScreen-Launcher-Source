# Fields of Mistria v1.0.4 GML state reconnaissance

## Scope and provenance

This report is based only on the installation currently present on the device at ADB endpoint `100.83.63.70:5555`:

- Install directory: `/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4`
- Executable: `FieldsOfMistria.exe`
- PE file/product version: `1.0.4`
- Executable SHA-256: `aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e`
- Source/assets archive: `assets.zip`
- Archive SHA-256: `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450`
- Runtime descriptor: `Maybe.toml` names the executable `FieldsOfMistria`; `assets/gml/scripts/Debug/Tattletale.gml` identifies the runtime as `maybe`.
- Current archive contains 567 `.gml` files (5,158,227 bytes) under `assets/gml`.

The device installation was read only. The archive was pulled and inspected locally. No proprietary source or game asset is reproduced here; paths and identifiers are recorded only to establish interoperability seams.

## Result

The current build exposes all data needed for an initial live second-screen implementation directly in GML. Save-file polling is unnecessary and should not be used.

The stable world owner is the persistent `Game` object. Its Create event constructs or loads all model globals, and its End Step event is a safe, deterministic place to collect state and process queued actions on the GML thread. `Game` persists through room transitions and new-day processing. A new `Game` instance is created after each new-game/load flow, which naturally reinitializes a bridge for a different save.

Recommended initial architecture:

1. Create a small GML bridge service after `Game.create` has successfully finished `new_game(...)` or `load_game(...)`.
2. Call its bounded `update()` from `Game.step_end`.
3. Give it independent `InventorySubscriber` instances for `ARI.inventory` and `ARI.armor`.
4. Mark other topics dirty at the functions listed below, with inexpensive scalar/fingerprint comparisons as a fallback for direct field assignments.
5. Publish a compact JSON state file only when dirty, using the current build's proven buffer/file helpers and atomic rename pattern.
6. Read commands from a separate tiny command file, validate a monotonic sequence number, and execute them only from `Game.step_end`.

## Lifecycle and bootstrap

### Exact lifecycle

`assets/gml/objects/Setup.gml` creates the world object only after `TAXI.enter_world(...)` reaches a world room. It creates `Game`, manually invokes its Room Start event for the initial room, then completes `LOAD_SEQUENCE` (approximately lines 366-405).

`assets/gml/objects/Game.gml` defines these relevant events:

- `create`: constructs `ARI`, `QUEST_LOG`, `CALENDAR`, `WEATHER`, and the other world models, then calls `load_game(...)` or `new_game(...)`. The load/new operation is complete before the Create event finishes.
- `step_begin`: increments `TICK`, begins input, updates `CLOCK`, `T2R`, chains, and the Anchor UI.
- `step`: normal world systems.
- `step_end`: camera/cursor/focus/audio finalization. This is the least disruptive bridge update point because all normal Step mutations have occurred and it is still the GML thread.
- `room_start`: selects/replaces `GRID`, creates the room's `obj_ari`, runs room setup, and refreshes room systems.
- `room_end`: stores loose items and clears room-local systems.
- `cleanup`: clears world UI/weather state when leaving the game.

`object_persists_on_room_change(game_obj)` is called at the end of `Game.gml`, confirming that the `Game` instance survives normal room changes.

New-day behavior is important: `assets/gml/scripts/GameplaySystems/Cycle/NewDay.gml` calls `CALENDAR.increment_day()` and replaces `CLOCK` with `new Clock()`. Therefore the bridge must read the `CLOCK` macro/global every update and must not cache a Clock object reference. `ARI`, `QUEST_LOG`, `CALENDAR`, `WEATHER`, and the persistent `Game` remain valid.

### Minimal injection point

The minimal reliable bootstrap is at the end of the successful `Game.create` path, after the load/new switch and before the event returns. Store the service on the `Game` instance rather than only in an unscoped global. Invoke it from `Game.step_end`.

That pair of calls survives:

- normal Step/End Step cycles;
- room changes, because `Game` is persistent;
- new days, while correctly seeing the replacement `CLOCK`;
- loading a save or starting a new game, because a fresh `Game` instance and fresh bridge are constructed;
- returning to the title, where `Game.cleanup`/destruction can shut the bridge down.

For a file-only bridge with no persistent native handles, a separate Room Start hook is optional: comparing `(CURRENT_LOCATION_ID, CURRENT_DYN_INDEX)` in End Step is sufficient. If immediate room notifications are wanted, call `bridge.on_room_start()` only at the end of `Game.room_start`, after `GRID` has been assigned and `obj_ari` created.

Do not bootstrap from `Setup`: it is destroyed before `Game` creation and does not survive the world. Do not attach the bridge to `obj_ari`: that instance is recreated on every room transition.

## Live state owners

### Player/status and currencies

Source: `assets/gml/scripts/GameplaySystems/Player/Ari.gml`

- Macro/owner: `ARI` -> `global.__ari`
- Constructor: `Ari()`
- Identity: `ARI.name`, `ARI.farm_name`, `ARI.pronouns`, `ARI.birthday`
- Health: `ARI.health_current`; read through `get_health()` and `get_max_health()`
- Stamina: `ARI.stamina_current`; read through `get_stamina()` and `get_max_stamina()`
- Mana: `ARI.mana_current`, `ARI.mana_max`; `get_mana()` exists
- Tesserae: `ARI.gold`; read through `get_gold()`
- Essence: `ARI.essence`, `ARI.essence_overflow`; read through `get_essence()` and `get_essence_overflow()`
- Renown: `ARI.renown`
- Skills/perks: `ARI.skill_xp`, `ARI.perks`, `ARI.perks_active`; use `ARI.level(skill)`, `skill_to_string(...)`, and `perk_to_string(...)` for external values. `SkillProgressUi` resolves the exact skill icon from `string_to_asset(fiddle_get(format("skills/{Skill}/sprite", skill)))`, which the bridge mirrors as a logical asset ID.
- Equipment: `ARI.armor` is a separate `Inventory(ArmorSlot.LEN)` with head/chest/legs/boots/accessory tag restrictions
- Status effects: `ARI.status_effects`

`Ari.serialize_stats()` already produces a bounded struct containing gold, essence, renown, mana, status effects, health, and stamina. It is useful as an inventory-independent reference, but a second-screen protocol should build its own explicit schema so future game fields do not silently alter the wire format.

Status effects are owned by `StatusEffectManager.effects` in `assets/gml/scripts/Player/StatusEffectManager.gml`. It is a `Map` keyed by `StatusEffectId`; values contain `type`, `amount`, `start`, `finish`, `stacks`, and `last_update`. `register(...)`, `cancel(...)`, and expiry in `update()` are the dirty seams. `serialize()` converts effect IDs to strings and is suitable as a model for the bridge payload.

The shipped `assets/gml/scripts/Debug/Tattletale.gml` independently confirms these live reads: its diagnostic context reads current weather, room, calendar/clock, player coordinates, `ARI.health_current`, `ARI.stamina_current`, inventory slots, `ARI.held_item_index`, and current player FSM state directly from the running world. This is concrete evidence that no save or vanilla menu is required.

### Inventory, item metadata, armor, and hotbar

Sources:

- `assets/gml/scripts/GameplaySystems/Inventory/Inventory.gml`
- `assets/gml/scripts/GameplaySystems/Items/LiveItem.gml`
- `assets/gml/scripts/GameplaySystems/Items/Items.gml`
- `assets/gml/scripts/UI/Anchor/Menus/ToolbarMenu.gml`

Owners and fields:

- Main inventory: `ARI.inventory`
- Armor: `ARI.armor`
- Hotbar selection: `ARI.held_item_index`
- Selected item helper: `ARI.held_item()`
- `__Inventory.slots`: a `List` of `InventorySlot`
- `InventorySlot`: `index`, `inventory`, `item`, `count`, `required_tags`, `allow_soulbound`, `updates`, `one_slot`
- `LiveItem`: `item_id`, `prototype`, `inner_item`, `gold_to_gain`, `cosmetic`, `pet_cosmetic_set_name`, `animal_cosmetic`, `auto_use`, `icon_override`, `date_photo`, `infusion`
- Static prototype array: `ITEM_PROTOTYPES`

Useful live display functions:

- `item_id_to_string(item.item_id)` for a stable external ID
- `item.get_display_name()` and `item.get_display_description()` for current localization and special-item naming
- `item.get_ui_icon()` is the authoritative resolver: cosmetic variants use the player asset's merged/normal UI icon, animal and pet cosmetics use their prototype icon, Mist items honor `icon_override`, and normal items return `ITEM_PROTOTYPES[item_id].icon_sprite`. The bridge applies `asset_to_string(item.get_ui_icon())` and emits that stable logical animation name, never the numeric runtime handle.
- `item.get_ui_sub_icon()` and `item.get_ui_lut()` are the equivalent source-owned optional sub-icon/LUT resolvers.
- Prototype fields include `name_key`, `description_key`, `tags`, `value`, `max_stack`, `use`, `damage`, `defense`, restoration/max-stat modifiers, `mana_modifier`, `tool_type`, `quality`, and `stars`

`InventorySlot.add(...)`, `remove(...)`, `swap(...)`, and inventory `sort()` increment each changed slot's `updates` counter. `InventorySubscriber(inventory)` holds its own timestamps, and `pull()` returns exactly the slots whose counters changed. Subscribers do not consume a global event, so the mod can safely own separate subscribers without interfering with `ARI.update()`, `InventoryMenu`, or `ToolbarMenu`.

Important edge cases:

- `Inventory.deserialize(...)` assigns slot fields directly without incrementing `updates`. This occurs before the proposed bridge is created, so always send a full initial snapshot.
- `Inventory.resize(...)` creates new slots. `InventorySubscriber.pull()` uses a default prior timestamp of `-1`, so new slots are naturally reported.
- Do not serialize `LiveItem`/prototype objects wholesale: they contain methods, lists, runtime asset handles, and other non-wire values. Flatten only the required scalar fields.
- The same exact item ID can carry different infusion/cosmetic/inner-item state. Item-targeted mutations must compare the published deterministic JSON fingerprint of `LiveItem.serialize()` immediately before execution, in addition to slot/count/type gates.
- `MUSEUM_DATA.museum_items[item_id]` is the eligibility bit and `MUSEUM_PROGRESS[item_id]` is the donation bit. Inventory/armor item metadata can expose both without scanning museum sets. Rebuild those cached slots only when the museum fingerprint changes.

The GML runtime does not expose the paired archive metadata path or its 16-hex metadata ID. A source-wide exact-v1.0.4 audit found only handle/name facilities (`asset_get_ids(...)`, `asset_to_string(...)`) and no path lookup; loaded prototypes retain handles. Do not fabricate `metadata_path`. The trusted native provider must map the emitted logical identifier through a typed, class-root-constrained index of the already hash-gated archive, then obtain the authoritative metadata ID/path by parsing that exact central-directory entry.

### Location and player position

Sources:

- `assets/gml/scripts/GameplaySystems/RoomService/RoomServiceUtils.gml`
- `assets/gml/scripts/GameplaySystems/Locations/Location.gml`
- `assets/gml/objects/Game.gml`
- `assets/gml/objects/characters/obj_ari.gml`

Owners:

- `GRID` -> `global.__grid`
- `CURRENT_LOCATION_ID` -> `GRID.location_id`
- `CURRENT_DYN_INDEX` -> `GRID.dyn_index`
- Static metadata: `LOCATIONS[CURRENT_LOCATION_ID]`
- Player position while present: `obj_ari.x`, `obj_ari.y`
- Player facing: `obj_ari.dir` and `obj_ari.cardinal`
- GameMaker room: `room()`; use `asset_to_string(room())` for a wire-safe name

`Game.room_start` is the authoritative location-change seam: it chooses a static, dynamic, or dungeon grid and then creates `obj_ari`. For names, `LOCATIONS[id].name` is the localized key, while `display_location(id, dyn_index, room())` supplies a stable diagnostic identity for dungeon/dynamic rooms. The map UI special-cases the farm name as `ARI.farm_name` and otherwise localizes `LOCATIONS[id].name`.

Never retain an `obj_ari` instance reference across rooms. Guard position/state access with `instance_exists(obj_ari)`.

`obj_ari.face_dir(...)` in the exact object source rounds `dir` to 45-degree increments and maps that value to `cardinal`; `cardinal_to_string(obj_ari.cardinal)` is therefore the source-owned wire representation rather than an inferred movement direction.

Mine context is also directly available without a menu:

- `DUNGEON_RUNNER` is `global.__dungeon_runner` and is either undefined or a live `DungeonRunner`.
- `DUNGEON_FLOOR` is `DUNGEON_RUNNER.current_floor`; `MinesMenu.gml` displays it as `DUNGEON_FLOOR + 1`.
- `DUNGEON_BIOME` is `DUNGEON_RUNNER.current_level().biome`; the localized biome name is `DUNGEON.biomes[DUNGEON_BIOME].name`.
- Read these fields only when `DUNGEON_RUNNER != undefined && is_dungeon_room(room())`. Seal rooms have related biome helpers but no source-confirmed mine floor, so the bridge does not label them as mines.

### Authored journal map

Exact source: `assets/gml/scripts/UI/Anchor/Menus/MapMenu.gml`.

`MapMenu.select_location` first resolves `LOCATIONS[location_id].map_location`, chooses `spr_ui_map_{LocationId}`, substitutes `spr_ui_map_farm_expansion` when `FARM_EXPANDED`, and substitutes `spr_ui_map_locked` for Deep Woods until `WorldMod.ThornyStairs`. Its tab order is the authored `ui/menus/sub_menus/map.tab_order`; Dragonsworn Glade is omitted until `WorldMod.DragonswornGlade` exists.

`MAP_HUBS` is populated once by `load_map_hubs()`, which calls `map_hubs_in(location_id_to_gm_room(id))`. Every hub carries authored world anchor `x/y`, authored map display `asset_x/asset_y`, and either position or child-location membership. `MapMenu.find_hub_for` chooses the nearest positional hub on the selected map, or a zero-distance location-membership hub. The bridge mirrors that decision and emits stable `<map-id>:<array-index>` hub IDs; it never estimates a continuous projection from world x/y.

The vanilla map considers only `npc_is_unlocked(id)`. A met NPC uses `asset_to_string(get_small_npc_icon(id))`; an unlocked but unmet NPC uses the anonymous unknown/animal icon. The bridge preserves the visible anonymous marker while omitting that NPC's ID. Player uses `spr_ui_generic_icon_npc_small_player`. Markers are laid out with the exact 11x11 square packing from `MapMenu`. Locked Deep Woods publishes no hubs/markers, and unavailable Glade publishes no page.

### Villagers and relationships

Exact sources:

- `assets/gml/scripts/GameplaySystems/NPCs/Npc.gml`
- `assets/gml/scripts/GameplaySystems/NPCs/NpcDatabase.gml`
- `assets/gml/scripts/UI/Anchor/Menus/RelationshipsMenu.gml`
- `assets/gml/scripts/UI/Anchor/Menus/MapMenu.gml`

`Game.create` constructs `NPCS = array_create_ext(NpcId.LEN, ...)`. Each `Npc` owns `id`, `prototype`, `location_position`, `heart_points`, `times_spoken_today`, and `gift_flag`; `heart_level()` derives the displayed heart level. Relationship state is the T2R fact `format("{NpcId}_status", id)` and the vanilla relationship page recognizes `spouse`, `fiance`, `dating`, and `best_friend`, otherwise displaying single.

Discovery gating must match the current journal: call `npc_is_unlocked(id)` and require `NPCS[id].has_met()` before emitting identity or relationship data. `npc_is_unlocked` contains current quest/requirement gates for market visitors, Caldarus, and Seridia. The vanilla journal renders unmet entries as `???`, and the bridge omits them entirely.

For a visible villager, `RelationshipsMenu.set_to_npc_id` uses `get_small_npc_icon(id)`. Its polaroid uses the current wardrobe's neutral portrait and falls back to the prototype fallback outfit's neutral portrait; `npc_polaroid_ui` also supplies `journal_portrait_offset` and `journal_background_color`. The bridge publishes those exact source-owned logical visual values only after the same met/unlocked gate.

`Npc.location_position` is the live schedule owner. `NpcDatabase` updates it as simulated paths advance, and `MapMenu` consumes it to place NPCs in map hubs. `LocationPosition` contains `location_id`, `dyn_index`, and `pos.x/y`. While an NPC's exact object from `npc_id_to_gm_obj_id(id)` exists, the bridge uses that instance's `x/y` with `CURRENT_LOCATION_ID`/`CURRENT_DYN_INDEX`; otherwise it uses the schedule-owned location. Both paths are current runtime reads, and neither predicts a future schedule.

Privacy boundary: `Npc` also owns `known_gift_preferences`, `gifts_given`, and prototype liked/loved/hated/disliked data. None is required for relationships or live location, and none is emitted. The bridge exposes only the journal's daily `times_spoken_today` and `gift_flag` checks, not what an NPC prefers.

### Owned animals and attention

Exact sources:

- `assets/gml/scripts/GameplaySystems/Ranching/Animal.gml`
- `assets/gml/scripts/GameplaySystems/Ranching/AnimalUtils.gml`
- `assets/gml/scripts/GameplaySystems/Ranching/Stable.gml`
- `assets/gml/scripts/GameplaySystems/Ranching/Daycare.gml`
- `assets/gml/scripts/Pet.gml`
- `assets/gml/scripts/AnimalMenu.gml`

`get_all_animals(true)` is the current bounded owner traversal: it collects every non-empty player stable stall and appends `DAYCARE`. Each `PlayerAnimal` owns `idx`, player name, `kind`, `variant`, `sex`, `days_old`, `heart_points`, `has_eaten`, `has_been_pat`, `has_been_outside`, `is_incubating`, `location_position`, and an optional live `instance`. The exact UI uses `points_to_animal_heart_level(...)`, and its animal entries display the same fed/petted/incubation fields. The bridge uses `animal.instance.x/y` while that instance exists and falls back to the owned `location_position` off-room.

`AnimalMenu` resolves a livestock visual with `get_animal_icon_data(kind, variant, sex, adult)`, returning its sprite and optional LUT/index. The bridge calls that exact helper, including the baby/adult branch. The unlocked pet uses `PET_PROTOTYPE.variants.get(PET.variant).ui_icon`, `.map_icon`, and its optional LUT/index, matching `AnimalMenu` and `MapMenu`; it never constructs a path from the variant name.

Stable `on_new_day()` evaluates care, production, and outside state, then resets `has_eaten`, `has_been_pat`, and `has_been_outside`. `daycare_new_day()` is empty in v1.0.4, and moving an animal to daycare leaves its prior location object attached. Consequently the bridge marks daycare attention inactive and suppresses the stale location instead of presenting either as live.

The separately owned `PET` is included only after `PET.unlocked()`. Its source fields are `PET_ID`, `name`, `variant`, `pet_kind()`, `sex`, `heart_points`, `has_eaten`, `has_been_pat`, and `location_id`. `NewDay.gml` resets the pet's daily food/petting flags. Exact pet x/y is emitted only while `obj_pet` exists; no off-room position is invented.

There is no persistent "produce ready" Boolean on `PlayerAnimal`. `production_days` is evaluated during stable new-day processing and reset when drops are produced, so schema 1 does not claim a live ready-to-collect state. Breeding partners, treat internals, sale price, and mutation actions are also intentionally outside this read-only topic.

### Magic and spells

Exact sources:

- `assets/gml/scripts/Spells.gml`
- `assets/gml/scripts/GameplaySystems/Player/Ari.gml`
- `assets/gml/scripts/UI/Anchor/Menus/SpellcastingMenu.gml`
- `assets/gml/objects/characters/obj_ari.gml`

`load_spells()` builds the static `SPELLS` array. A spell's stable ID comes from `spell_to_string(id)`, and its prototype supplies localization keys `name`, `type`, and `description` plus raw `cost`. The current Spellcasting menu iterates `Spell.LEN` and creates a card only when `ARI.spells_learned[id]` is true; it renders `spell.cost div 4` mana orbs and uses the model's `upper_icon`, `icon_key`, and `ribbon` visual fields. Because `icon_key` is a button-sprite prefix rather than a loadable animation ID, the bridge additionally calls the same `load_button_sprites(icon_key)` resolver and publishes its exact enabled sprite as `lower_icon_asset`; it does not append a guessed suffix. Each entry retains its source enum `index`; because the learned array is filtered, consumers must never infer the enum from compact-array position. `ARI.pinned_spell` is the pin owner and is emitted both as `pinned_spell_index` and the stable string `pinned_spell` only while learned. The menu calls `ARI.set_pinned_spell(...)` after selecting only a learned card. The bridge permits that same pin/unpin mutation but never learns a spell.

`can_cast_spell(...)` contains contextual availability tests, not a generic cooldown model. Its only explicit countdown-backed gate is Fire Breath: `ARI.fire_breath_time == 0`. Casting sets this field to the configured cap, and `obj_ari` decrements it while not paused. The bridge therefore emits a `remaining_frames`/`ready` entry only for learned Fire Breath. Summon Rain's weather/spell duration and Sacred Light's active status effect are not mislabeled as cooldowns. `can_cast_spell` itself is not emitted as a stable ready flag because it also depends on transient mount/player/world context outside the requested spell metadata.

The normal Ari FSM checks pinned/learned context with `can_cast_spell`, writes `spell` plus `hold_to_use`, and transitions to `PlayerState.Spell`. The Spell state owns animation, calls `cast_spell` at the action boundary, and deducts mana. The bridge's cast command reproduces only those blackboard/state-transition lines after the same gates; it never applies the spell effect or mana change itself.

### Almanac and museum discovery

Exact sources:

- `assets/gml/scripts/UI/Anchor/Menus/AlmanacMenu.gml`
- `assets/gml/scripts/GameplaySystems/Player/Ari.gml`
- `assets/gml/scripts/Museum.gml`
- `assets/gml/scripts/UI/Anchor/Menus/MuseumMenu.gml`
- `assets/gml/scripts/MuseumDonationMenu.gml`

The current Almanac menu gets categories from `fiddle_get("ui/menus/sub_menus/almanac/categories")`. Within each category it scans `ItemId.LEN`, matches prototype tags, and deduplicates by `prototype.recipe_key`. The bridge reproduces that exact membership algorithm once into an internal static cache. `ARI.items_acquired[item_id]` is the vanilla identity gate: the menu uses a lock presentation until that Boolean is true, and `Ari.mark_item_as_acquired(...)` updates all IDs sharing the recipe key. The wire topic includes category totals but emits an item's stable ID, localized name, and description only after that gate passes. The internal membership cache is never serialized.

Museum definition/progress owners are `MUSEUM_DATA` and `MUSEUM_PROGRESS`. `MuseumMenu` uses the configured `"ui/menus/standard_menus/museum/{MuseumWing}_order"` order and publicly displays wing/set labels, descriptions, and progress totals. `register_item_to_museum(item_id)` sets `MUSEUM_PROGRESS[item_id]`. The bridge caches wing/set membership internally and emits item identity only when `ARI.items_acquired[item_id] || MUSEUM_PROGRESS[item_id]`; donation can therefore never become an anonymous or missed discovered entry. Unknown item IDs/names/descriptions are omitted even though vanilla can draw locked item icons. Future museum reward definitions and donation actions are excluded.

Acquired and donated Boolean arrays are reduced to compact ID fingerprints once per 60 End Steps. Localization changes rebuild static labels. The larger discovery-safe payload is reconstructed only when one of those fingerprints changes.

### Town renown

Exact sources:

- `assets/gml/scripts/GameplaySystems/Player/RenownUtils.gml`
- `assets/gml/scripts/GameplaySystems/Player/Ari.gml`
- `assets/gml/scripts/UI/Anchor/Menus/PlayerMenu.gml`
- `assets/gml/scripts/UI/Anchor/Menus/RenownSequenceMenu.gml`

`ARI.renown` is the committed total and `ARI.pending_renown_entries` contains values that current end-of-day processing has not yet drained. `renown_entry_value(...)` is the exact pending-value converter. `renown_to_level(...)`, `renown_level_total_cost(...)`, `renown_level_to_rank(...)`, `MAX_RENOWN_LEVEL`, `RENOWN.levels_per_rank`, and the public `RENOWN.ranks` labels provide the same current-level/rank/progress math used by the game UI.

Future reward data is deliberately not exposed. `Ari.set_renown(...)` consumes `RENOWN.rewards.get(level)` only after a level has actually been reached and transfers earned items into `RENOWN_REWARD_INVENTORY`. The bridge subscribes independently to that inventory and emits only those already-earned, currently claimable slots. It never reads the future reward table and does not provide reward-claim or renown-mutation commands.

Committed/pending totals and localization are compared in the 15-End-Step pass. Claimable slots use their own `InventorySubscriber`, so a slot update is event-driven and unrelated inventory changes do not rebuild this topic.

### Public upcoming calendar events

Exact sources:

- `assets/gml/scripts/UI/Anchor/Menus/CalendarMenu.gml`
- `assets/gml/scripts/UI/Anchor/Menus/EodMenu.gml`
- `assets/gml/scripts/Festivals.gml`

The Calendar menu establishes the release/discovery rules for festivals and birthdays. It shows only festival prototypes with `implemented == true`; wedding/anniversary and pending-child due dates from their `ARI` owners; existing-child birthdays after the birth year; villager birthdays only after both `Npc.has_met()` and `npc_is_unlocked(id)`; and the player's birthday. The EOD calendar path additionally establishes Friday night at the inn as public and Saturday market as public only after `requirements_pass(Requirement.SaturdayMarketUnlocked)`.

The bridge applies those gates to a bounded rolling 28-day horizon. It emits stable type/ID, localized display text, days from now, raw date, zero-based year/season/day and display year/day. It deliberately omits `npc_date_eligibility(...)` results and legendary-fish availability from EOD because they are private/situational rather than public calendar facts. Festival prototypes that are not implemented are omitted even if their static definitions have dates. It does not mutate festival, family, or calendar state.

The event fingerprint contains current date/localization, family values, Saturday-market requirement state, and only the IDs of villagers already passing the met/unlocked gate. It is evaluated in the existing 60-End-Step social pass; the event list is built only after a fingerprint change.

### Date and time

Sources:

- `assets/gml/scripts/GameplaySystems/TimeSeasons/Calendar.gml`
- `assets/gml/scripts/GameplaySystems/TimeSeasons/Clock.gml`
- `assets/gml/scripts/GameplaySystems/TimeSeasons/Time.gml`

Owners:

- `CALENDAR` -> `global.__calendar`, constructed by `Calendar()`
- `CLOCK` -> `global.__clock`, constructed by `Clock()`
- Calendar raw day base: `CALENDAR.time`
- Time of day: `CLOCK.time`

Read helpers:

- `CALENDAR.year()`, `season()`, `day()`, `day_type()`, `unified_time()`
- `total_days()`
- `CLOCK.hour()`, `minute()`, `is_day()`, `is_night()`
- `clock_time_to_string(CLOCK.time)` and enum string converters such as `season_to_string(...)`

Calendar year/day values are zero based; the shipped UI displays `CALENDAR.day() + 1` and `CALENDAR.year() + 1`. Include both raw and display values in the protocol to avoid ambiguity.

`Clock.update()` advances `CLOCK.time` every Begin Step. Publishing only when the displayed minute changes avoids per-frame traffic. `Clock.set_time(...)` and `jump(...)` are explicit discontinuity seams. `Calendar.increment_day()` is the date seam, but the bridge must also handle the replacement `CLOCK` in `new_day_non_grid()`.

### Weather

Source: `assets/gml/scripts/GameplaySystems/Weather.gml`

- Owner: `WEATHER` -> `global.__weather`
- Constructor: `WeatherManager()`
- Current weather: `WEATHER.weather`
- Current atmosphere: `WEATHER.atmosphere`
- Forecast: `WEATHER.forecast`
- Spell override: `WEATHER.under_spell`, `WEATHER.spell_timer`

Read/format helpers:

- `WEATHER.is_inclement()`
- `weather_tomorrow()`
- `weather_to_string(...)`
- `weather_to_forecast_group(...)`
- `ui_icon_for_weather(weather, season)` if the bottom UI maps game icon names to its own assets

Dirty seams are `WeatherManager.set_weather(...)`, `on_new_day()`, and restoration of forecast weather in `on_room_start()`/`on_draw()` when a rain spell ends. `new_day_non_grid()` also writes `WEATHER.weather` directly before `WEATHER.on_new_day()`, so retain a cheap value comparison as a safety net.

### Quests and objectives

Sources:

- `assets/gml/scripts/GameplaySystems/Quests/QuestLog.gml`
- `assets/gml/scripts/GameplaySystems/Quests/QuestDatabase.gml`
- `assets/gml/scripts/UI/Anchor/Menus/QuestLogMenu.gml`
- `assets/gml/scripts/Requirements.gml`

Runtime owners:

- `QUEST_LOG` -> `global.__quest_log`, constructed by `QuestLog()`
- `QUEST_LOG.active`: `Map` from quest key string to `ActiveQuest`
- `QUEST_LOG.completed`: `HashSet` of quest key strings
- `QUEST_LOG.completion_timestamps`: `Map`
- `QUEST_LOG.blackboard`: global quest-progress `Map`
- `ActiveQuest`: `quest`, `quest_name`, `blackboard`, `current_stage`

Static database:

- `QUESTS` -> `global.__quest_db`
- `Quest`: `name`, `description`, `tasks`, `reward_list`, `post_complete_tutorial`, plus parsed `category`, `npc_for_icon`, and `test_target`
- `QuestTask`: `description`, `requirements`, `reward_list`, `query_targets`, `turn_in_description`, `turn_in_button_label`

The vanilla journal builds its active list from `QUEST_LOG.active.keys()`. For each entry, read the static `Quest` from `QUESTS`, the current stage from `ActiveQuest.current_stage`, and localize the quest/task keys. `fulfills_all_requirements(task.requirements, active.blackboard)` gives the current stage's completion readiness.

For detailed requirement counts, the current journal uses `gather_listings_from_requirements(...)` and `try_requirement_to_listing(...)`. Those functions resolve live inventory, gold, shipped-item/tag counters, monster counts, NPC introductions, supplied inventories, and special gather totals. They return UI-oriented `Listing` structs; flatten their scalar values rather than serializing the structs directly.

The bridge mirrors the displayed sources: `ARI.inventory.item_id_quantity`, `ARI.get_gold`, stage-relative shipped tag/item and monster counters from the active blackboard, met-NPC count without identity disclosure, chest/seal `SuppliedItems`, and special `Custom.gather` totals. Current-stage `query_targets` provide source-confirmed NPC or cutscene-location targets. `quest.npc_for_icon` is emitted explicitly as an icon association, not mislabeled as a quest giver. Final rewards follow `gather_listings_from_quest_rewards`: public item/gold/scroll/cosmetic/renown/available lookup items only; tier definitions and quest-chain rewards remain omitted exactly as in the journal.

Quest dirty seams:

- `QuestLog.start(quest_name, target_stage)`
- `QuestLog.complete(quest_name)`
- `ActiveQuest.progress()` when `current_stage` increments
- `ActiveQuest.initialize_requirements_on_task(...)` for stage-relative baselines
- mutations to `ActiveQuest.blackboard` and `QUEST_LOG.blackboard`

The generic `Map` implementation has no generation/version counter. For version resilience, patch the three quest lifecycle methods above to mark the quest topic dirty, and compute a small fallback fingerprint from active quest keys/current stages/completed count at a low rate. Objective counters are derived from other live systems, so also recompute visible active objectives when inventory, gold, shipment, monster, or relevant quest-blackboard state changes.

There is no persistent gameplay-level "tracked quest" in this v1.0.4 source. `QuestLogMenu.active_quest` is only the currently selected row in that menu instance. Do not present it as tracked game state, and do not invent a tracking action unless the mod intentionally adds its own second-screen-only selection.

## Dirty/change seams and fallback cadence

| Topic | Primary seam | Fallback |
|---|---|---|
| HP | `ARI.set_health(...)`, `modify_health(...)` | compare current/max in End Step |
| Stamina | `ARI.set_stamina(...)`, `modify_stamina(...)` | compare current/max in End Step |
| Mana | `ARI.set_mana(...)`, `modify_mana(...)` | compare current/max in End Step; `mana_max` has direct story assignment |
| Tesserae | `ARI.set_gold(...)`, `modify_gold(...)` | compare scalar in End Step |
| Essence | `ARI.set_essence(...)`, `modify_essence(...)`, `gain_essence(...)` | compare scalar in End Step |
| Skills/perks | `gain_xp(...)`, `acquire_perk(...)` | compact array fingerprint at 4 Hz or on status-page demand |
| Status effects | manager `register(...)`, `cancel(...)`, expiry in `update()` | compare effect IDs/stacks/finish times at 4 Hz |
| Inventory/armor | independent `InventorySubscriber.pull()` | full snapshot on bridge construction or inventory identity change |
| Hotbar | `ARI.set_held_item_index(...)` | compare scalar each End Step |
| Location | `Game.room_start` after `GRID` assignment | compare location/dynamic index each End Step |
| Authored map | `LOCATIONS.map_location`, player/NPC `LocationPosition`, farm/world mods | player hub every four steps; bounded unlocked-NPC/page fingerprint every 60 steps |
| Position/facing/mines | `obj_ari.x/y`, `dir`, `cardinal`, `DUNGEON_RUNNER` | scalar sample every four End Steps (maximum nominal 15 Hz) and forced refresh after room start |
| Villagers/relationships | `Npc.set_has_met`, `add_heart_points`, schedule/location updates, daily reset | discovery-gated bounded `NpcId.LEN` fingerprint once per 60 End Steps |
| Owned animals | stable/daycare registration, daily care flags, `PET` updates | one `get_all_animals(true)` collection plus scalar fingerprint once per 60 End Steps |
| Magic | `Ari.learn_spell(...)`, `set_pinned_spell(...)`, Fire Breath timer decrement | learned/static and pin/countdown comparison every 15 End Steps; metadata cached |
| Almanac | `Ari.mark_item_as_acquired(...)` | acquired-ID fingerprint once per 60 End Steps; static category membership cached |
| Museum | `register_item_to_museum(...)` plus item acquisition | acquired/donated-ID fingerprint once per 60 End Steps; static wing/set membership cached |
| Renown | `Ari.set_renown(...)` and end-of-day pending drain | total/pending comparison every 15 End Steps; independent reward-inventory subscriber |
| Public calendar events | `CALENDAR.increment_day()`, family/NPC discovery, market unlock | discovery/progression-safe fingerprint once per 60 End Steps; fixed 28-day rebuild only on change |
| Date | `CALENDAR.increment_day()` | compare raw calendar time |
| Clock | `CLOCK.update()`, `set_time(...)`, `jump(...)` | publish on displayed-minute change |
| Weather | `WEATHER.set_weather(...)`, `on_new_day()` | compare weather/forecast-head/spell flag at 4 Hz |
| Quests | `QUEST_LOG.start/complete`, `ActiveQuest.progress` | compact quest fingerprint at 4 Hz; refresh counts on dependent topic changes |

Scalar comparisons are constant-time and safer than assuming every caller uses a setter. Full inventory or quest reconstruction every frame is not justified.

## Event-safe actions

All commands must be validated and executed from the GML thread in `Game.step_end`; the external UI must never mutate VM objects from another thread.

### Safe now

- Hotbar selection: `ARI.set_held_item_index(index)`. Validate that `instance_exists(obj_ari)`, the index is in `[0, ARI.inventory.size())`, the toolbar menu exists, and the game is in an allowed state. The function itself rejects several unsafe player FSM states and updates the vanilla toolbar cursor.
- Inventory rearrangement: `ARI.inventory.slot(a).swap(ARI.inventory.slot(b))`. This is the same slot primitive used by `ToolbarMenu` drag/drop and increments both update counters. Gate it to normal gameplay and validate both indexes.
- Armor equip/unequip: the Player journal pairs `ARI.armor` with `ARI.inventory`; `InventoryMenu` uses `valid_for`, `room_for_item`, `add/remove`, and single-item `swap`. The bridge permits only atomic final states that pass those primitives: one-item equip to an empty compatible slot, one-for-one valid replacement, or one-item unequip to an empty compatible inventory slot. It rejects stack-shaped or occupied-destination ambiguity and requires both slot instance fingerprints to match the published view.
- Consumption: only the currently selected, fingerprint-matching, non-auto-use `ItemUse.Consume` item is accepted. `use_item` must return `RemoveLater`; the bridge retains `remove_item_at` and opts into HoldToUse's existing instant UI path. HoldToUse still performs the real inventory pop, restoration, infusion/effect, stats, animation, and audio work.
- Spell pin/unpin: validate the exact learned spell index and call `ARI.set_pinned_spell`.
- Pinned spell cast: validate the expected pin/learned state and `can_cast_spell`, then reproduce AriFsm's `spell`, `hold_to_use=false`, and `PlayerState.Spell` transition. The vanilla state applies effect and mana cost.
- Read-only quest selection: keep this in the custom UI only. It is not a game-state mutation because v1.0.4 has no tracked-quest model.

### Explicitly unsupported

Generic item use stays disabled. Attack/tool/seed/bait/placement/bomb/unlock use types depend on selected cells, collision, world objects, held animations, popups, or other state-specific preconditions. `use_item_fast` is never used for inventory consumption because it intentionally removes `remove_item_at` and can apply effects without consuming the slot.

Drop stays disabled. Normal-world Throw either pops one bomb or drains the entire held stack, writes `items_thrown`, changes to `PlayerState.ThrowItem`, and creates the world item at a later animation boundary. Journal trash instead permanently drains/deletes a non-soulbound hand item. There is no context-free operation that can honestly be called generic drop.

Do not expose raw `QUEST_LOG.start`, `ActiveQuest.progress`, or `QUEST_LOG.complete` as generic UI commands. Progress can consume items/tesserae, trigger rewards/popups/cutscenes, and mutate world grids. Quest interaction should only be added through the exact vanilla acceptance/turn-in paths for each supported context.

## File/buffer bridge viability

The current build proves the required APIs are available:

- `assets/gml/scripts/Utilities/DataStructures/file_utils.gml` implements `read_text_file`, `save_text_file`, `read_json_file`, and `save_json_file` with `buffer_load`, `buffer_create`, `buffer_read`, `buffer_write`, `buffer_save`, and `buffer_delete`.
- `assets/gml/scripts/Serialization/Settings.gml` uses `file_exists`, `file_delete`, and `file_rename` in a current/old/new atomic replacement pattern.
- `CONFIG_DIRECTORY` is initialized in `assets/gml/scripts/Serialization/Serde/SerdeUtils.gml` using `fields_of_mistria_config_directory()`, which returns `game_save_id` outside tests.

Therefore a bridge can operate independently of saves:

- state: `CONFIG_DIRECTORY + "/dualscreen/state.json"`
- temporary state: `state.new.json`
- commands: `commands.json`
- acknowledgement/error: `ack.json`

Publish only flattened, bounded structs. Write the temporary state, then rename it using the proven settings-file pattern. Include `protocol`, `sequence`, `game_identifier`, `tick`, and topic revision numbers. The panel can watch the state file or poll only its metadata; GML writes it only when dirty.

For commands, call `file_exists` at a modest cadence (for example 15-20 Hz), read only when present, validate the sequence and current game identifier, delete/rename after claiming it, execute a bounded number per frame, and publish an acknowledgement. This is command-file polling, not save polling, and it never invokes the save serializer.

No current GML source uses `network_*`, sockets, or an extension-defined IPC function. Although such built-ins may exist in the runtime, availability and container behavior are unproven. File/buffer transport is the confirmed Phase 1 seam. The exact host-visible path corresponding to `game_save_id` must be probed in the running FEX/container environment before the panel is wired to it.

## Recommended initial state payload

Keep the first protocol deliberately small:

- Session: `in_game`, game version, `Game.unique_identifier`, `TICK`
- Player: name, farm name, HP/max, stamina/max, mana/max, tesserae, essence, renown
- Status effects: string ID, amount, stacks, start, finish
- Inventory: revision, size, changed/full indicator, and flattened slot entries
- Armor: same shape as inventory
- Hotbar: `held_item_index` and selected item ID
- Location: string location ID, dynamic index, room name, and localized display name
- Player: sampled x/y, exact facing angle/cardinal, and guarded mine floor/biome context
- Map: current base map, source-gated tab pages/base assets, authored hubs/display coordinates, and discovery-safe player/NPC markers
- Villagers: met-and-unlocked identity, hearts/relationship, talk/gift daily checks, and live location; no preference data
- Animals: owned livestock/daycare and unlocked pet identity, hearts, location where valid, and real daily attention flags
- Magic: learned spell ID/name/type/description/cost, current learned pin, and only source-confirmed Fire Breath countdown state
- Almanac/museum: public category/wing/set totals plus identities/details only for acquired or donated entries
- Renown: current/pending/projected progress and current/next public rank, plus only already-earned claimable reward inventory
- Public calendar events: 28-day implemented/progression-safe festivals, public recurring events, family events, and met/unlocked birthdays
- Calendar: raw time, display year/season/day, weekday
- Clock: raw time, hour, minute, formatted time
- Weather: current string ID, tomorrow string ID, inclement/spell flags
- Quests: active key/name/description/current stage/current objective/readiness, visible objective quantities/targets/public rewards, and completed count/revision

After a full initial snapshot, inventory can be sent as changed-slot patches keyed by slot index. Send a full resync when the game identifier, inventory object identity, size, or protocol generation changes.

## Confidence and remaining unknowns

High confidence, source-confirmed:

- The exact state owners and field/function names in this report.
- `Game` persistence and room/new-day/load lifecycle.
- Direct live reads without opening a menu or reading a save.
- Per-slot inventory change detection through independent subscribers.
- File/buffer JSON transport and atomic rename primitives.
- Hotbar selection and slot swap action seams.
- Armor `valid_for`/`room_for_item`/add/remove/swap seams and the conservative atomic equip/unequip shapes.
- Consume's exact `use_item -> HoldToUse -> START_ACTION` removal/effect path and the safe instant-UI blackboard flag.
- Absence of a persistent tracked-quest state in the current source.
- `use_item_fast` being inappropriate for consuming a real inventory slot.
- Spell pin and normal-state FSM cast request paths; the Spell state retains effect/mana ownership.
- Authored `MAP_HUBS` coordinates, MapMenu hub selection/layout, and Deep Woods/Glade visibility gates.
- Player facing/mine owners, the met/unlocked villager gate, live NPC location owner, and owned-animal care owners documented above.
- Absence of a persistent livestock produce-ready flag and the stale-location/daily-care limitations of daycare.
- Learned-spell/pin owners, vanilla spell metadata/cost presentation, and Fire Breath being the only explicit countdown-backed spell gate.
- Source-owned logical visual selection for skill icons, visible villager small icons/current neutral portraits, owned livestock/pet icons and LUTs, learned spell card/icon/ribbon assets, and map markers.
- The GML runtime exposes logical asset names but not archive metadata IDs/paths; those must be supplied by the trusted archive index rather than invented in the bridge.
- Almanac recipe-key/category membership, item-acquisition identity gate, museum donation owner, and the discovery-safe collection boundary.
- Renown level/rank/progress functions and the distinction between future `RENOWN.rewards` and earned `RENOWN_REWARD_INVENTORY`.
- Calendar festival implementation gate, met/unlocked birthday gate, family dates, and progression-gated Saturday market behavior.

Medium confidence until an in-game probe:

- `Game.step_end` is the optimal bridge point under every cutscene/loading state. It is structurally correct, but frame timing and command gating must be logged in a running save.
- The best fallback polling rates; they should be profiled on the device.
- Localized item/quest strings being safe to JSON-encode in every supported language.
- Whether one-second villager/animal snapshots and four-step player samples are the best on-device cadences; both are bounded and must be profiled.
- Whether the one-second collection/calendar fingerprints and quarter-second magic/renown comparisons are the best on-device cadences; they are bounded but require profiling.

Unknown and requiring runtime validation:

- The host-visible filesystem path corresponding to `CONFIG_DIRECTORY`/`game_save_id` in the device's FEX/container setup.
- Whether the Maybe runtime compiles and executes the transactionally installed
  archive generation. The archive rebuild/index/update path is statically and
  structurally verified; execution still requires a real launch.
- Whether atomic rename visibility is preserved across the container boundary; use sequence numbers and parse validation regardless.
- Live proof of the conservative consume/equip/unequip requests through animation boundaries, room changes, pause/menu races, and save reloads. Source/policy proof is not runtime proof.
- End-to-end renderer coverage for every emitted logical visual. The native provider now has a hash-gated typed logical-name-to-central-entry index and exact-archive fixtures, but live state parsing/rendering of every variant remains unproven. One exact special case is the vanilla unmet Dozy/Henrietta map marker, `spr_ui_generic_animal_name_icon`, which sits in `UI NEW/Generic/Icons` rather than the provider's normal `NPC Small` class root and must be admitted only as that audited singleton (or rendered as a non-proprietary anonymous marker).
- Live proof that the authored map topic matches every current-map/NPC transition and that its bounded refresh cadence is sufficient.
- Whether any spell besides Fire Breath gains a distinct countdown in a future game version; schema 1 exposes only the exact v1.0.4 owner.
- Whether calendar-event consumers want a horizon longer than 28 days; extending it is a protocol/UI choice, not a missing state owner.

## First runtime proof checklist

1. Log bridge construction after `load_game`/`new_game` and one full snapshot.
2. Confirm End Step continues across at least two room changes and a new day.
3. Verify HP, stamina, mana, and tesserae update immediately from gameplay changes.
4. Pick up, split/move, consume through vanilla controls, and drop items; verify subscriber slot patches and counts.
5. Change hotbar pages and selected slots; verify `held_item_index`.
6. Start, advance, and complete quests; verify active/current-stage/completed transitions and objective counts.
7. Meet a previously unknown unlocked NPC, change hearts, talk/gift, cross rooms, and verify only met/unlocked entries and live locations appear; inspect the payload to confirm preference fields never appear.
8. Feed/pet/move livestock, send one to/from daycare, unlock/use the pet, and cross a new day; verify attention and location semantics, including inactive daycare care.
9. Walk and turn in/out of the mines; verify sampled x/y, 45-degree facing, one-based display floor, and guarded biome context.
10. Learn and pin spells, cast Fire Breath, and verify only learned cards appear and the countdown reaches zero without publishing rain/light durations as cooldowns.
11. Acquire an almanac item and donate a museum item; verify counters/known entries change while a payload inspection finds no unknown item identity or future reward data.
12. Gain pending and committed renown and earn a claimable reward; verify level/rank thresholds and subscriber-driven earned slots without exposing future rewards.
13. Advance dates and unlock/meet relevant NPCs; verify the rolling public calendar adds only implemented/progression-safe events and met/unlocked birthdays.
14. Load a different save and confirm a new game identifier plus full resync, with no stale references.
15. Return to title and confirm the bridge publishes/leaves a clean not-in-game state and holds no world references.
