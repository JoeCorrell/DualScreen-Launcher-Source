# Fallout 3 Semantic Reference Audit

Audit date: 2026-08-29

## Clean-room scope

This document records behavioral facts and interoperability hypotheses learned
from the following read-only reference material:

- `PipBoy3000Companion`, commit
  `2e750c425dc699be9c6f0c9ef05aa7c7349fcf5c`, by Aidan's Lab.
- Its Fallout 3 plugin under `GamePlugin/src-fo3`.
- The bundled FOSE 1.3 beta 2-era SDK under `GamePlugin/FOSE`.

No reference implementation code was copied into Fallout3SecondScreen. Names,
protocol details, algorithms, data layouts, and addresses in this audit are
evidence for independently implementing and validating an interface against the
user's executable. They are not approved implementation constants.

The reference plugin explicitly compiles only for Fallout `1.7.0.3`
(`src-fo3/fo3_engine.h:44-46`, `GamePlugin/CMakeLists.txt:187-192`) and rejects
all other runtimes (`src-fo3/main.cpp:2247-2266`). The installed target under
investigation is `1.7.0.4`. Therefore, every executable address, RTTI address,
singleton location, vtable slot, structure size, field offset, calling
convention, and event hook below must be rediscovered or revalidated on the
exact `1.7.0.4` executable before use.

Confidence labels in this document mean:

- **High semantic confidence**: the concept is directly exercised by the
  reference or clearly represented by multiple SDK types.
- **Medium semantic confidence**: the concept is plausible and partially
  represented, but the reference omits edge cases or runtime proof.
- **Low semantic confidence**: the SDK entry is opaque, incomplete, or unused by
  the reference.
- **1.7.0.4 confidence: none**: no address or layout was validated against the
  installed `1.7.0.4` binary during this audit.

## What the reference actually implements

The reference is a FOSE plugin and external companion synchronization bridge,
not an independent second-screen renderer. It uses a named pipe, builds JSON
snapshots, and accepts text commands (`src-fo3/main.cpp:1997-2003`). It has no
Fallout 3 quest, note, map, radio, active-effect, or custom lower-display
implementation.

Its live snapshot includes:

- load order;
- player name, level, HP/AP, carry capacity/current carried weight, karma, XP;
- SPECIAL and thirteen Fallout 3 skills;
- six limb-condition values;
- Pip-Boy light state;
- equipped weapon, equipped apparel, conditions, and weapon ammo form;
- inventory aggregated by form and condition;
- perk FormIDs and ranks.

Evidence: `src-fo3/main.cpp:772-1144`.

It does not provide the following required permanent-Pip-Boy domains:

- radiation level/stage;
- damage resistance/armor total and other derived status values;
- caps as a dedicated value;
- active effects, durations, magnitudes, addictions, diseases, or effect text;
- complete item names, descriptions, values, weights, armor values, icons, or
  ammo-in-clip state;
- quest catalog, objective state/completion, tracked objectives, notes;
- world map, local map, discovered marker data, or player map transform;
- radio stations, signal/availability, current station, or tune/off action;
- a lower-screen UI, lower renderer, touch, controller navigation, or game/input
  isolation.

Its supported mutations are equip/use, unequip, drop, and Pip-Boy-light toggle
(`src-fo3/main.cpp:1411-1418,1690-1876`). The user did not request a freeze or
vanilla overlay, but the reference's initial synchronization can disable player
controls, slow world time almost to zero, and inject XML into the vanilla UI
(`src-fo3/main.cpp:1158-1409`). That behavior is incompatible with uninterrupted
top-screen gameplay and must not be adopted.

## Semantic state model

### Actor values

The reference accesses values through the player's `ActorValueOwner`: one
virtual operation for base value, one for current/effective value, and one for
level (`src-fo3/main.cpp:115-125`). The following numeric identifiers are
explicit in the reference (`src-fo3/main.cpp:72-113`):

| Domain | Actor-value identifiers observed | Reference interpretation | Confidence |
|---|---|---|---|
| SPECIAL | Strength 5, Perception 6, Endurance 7, Charisma 8, Intelligence 9, Agility 10, Luck 11 | Current/effective value includes modifiers | High semantic; none on 1.7.0.4 |
| Vitals | AP 12, Health 16 | Base treated as maximum; current treated as current | High for access; medium for “maximum” semantics |
| Carry | Carry Weight 13, Inventory Weight 46 | Base carry capacity; current inventory weight | High semantic |
| Derived | Melee Damage 17, Karma 23, XP 24 | Current values | Medium; HUD matching still required |
| Limbs | 25 through 30 | Perception, Endurance, left/right attack, left/right mobility condition | High semantic; brain condition is not represented |
| Skills | Barter 32, Big Guns 33, Energy Weapons 34, Explosives 35, Lockpick 36, Medicine 37, Melee Weapons 38, Repair 39, Science 40, Small Guns 41, Sneak 42, Speech 43, Unarmed 45 | Current/effective values | High semantic |

The reference intentionally rounds HP up and AP down to resemble the HUD
(`src-fo3/main.cpp:822-836`). That is presentation behavior, not proof of the
underlying maximum-value contract. Fallout 3's radiation value, brain
condition, damage resistance, poison/radiation resistance, and other status
actor values must be traced independently; they must not be guessed from the
New Vegas numbering family.

### Forms and identity

The FOSE SDK models every record/reference through `TESForm`. In its 1.7.0.3
layout, `TESForm` is `0x18` bytes with type at `+0x04`, flags at `+0x08`, FormID
at `+0x0C`, and mod-reference list at `+0x10`
(`GameForms.h:292-395`). The reference emits both runtime FormIDs and the
loaded-plugin index/name table (`src-fo3/main.cpp:785-806`).

Clean-room contract:

- A snapshot may use a runtime FormID within one loaded game generation.
- A UI cache must also retain the owning plugin identity and local FormID, or a
  resolver token, so save/load and load-order changes cannot silently retarget
  an action.
- An action must re-resolve its token on the game thread immediately before use.
- Never retain raw `TESForm`, `TESObjectREFR`, container-entry, or extra-data
  pointers across a load, cell transition, inventory mutation, or frame.

### Inventory and per-instance state

The central concept is the player's `ExtraContainerChanges` extra-data record.
It points to an entry list. Each entry carries a base form, a signed count delta,
and an optional list of per-instance `ExtraDataList` nodes
(`GameExtraData.h:449-489`). Relevant extra-data kinds include container
changes, worn, worn-left, count, health, leveled-item, script, and map-marker
tags (`GameExtraData.h:161-184`).

Per-instance condition is conceptually:

`ExtraHealth current health / TESHealthForm maximum health`

with missing `ExtraHealth` meaning pristine condition. The reference searches
the worn extra-data node rather than assuming the first node is equipped
(`src-fo3/main.cpp:299-361`). This distinction is important when several copies
of one form have different condition.

Inventory stacks must be reconstructed as follows:

1. Merge base actor-container counts with signed container-change deltas.
2. Partition the resulting count by per-instance extra-data nodes.
3. Use `ExtraCount` when present; an extra-data node without it represents one
   item.
4. Treat the unaccounted remainder as the default/pristine stack.
5. Preserve worn/worn-left, health, ownership, script, quest-item, hotkey, and
   any other action-relevant extra data in an internal instance key.
6. Aggregate only for presentation; do not destroy the identity needed for a
   later action.

The reference snapshot performs steps 2-4 and aggregates by `(FormID,
condition)` (`src-fo3/main.cpp:1008-1110`), but it reads only positive
`countDelta` entries. It does not merge the player's base-container list for
display. Its separate action helper does merge the base list and delta and
special-cases leveled items (`src-fo3/main.cpp:1540-1580`). The display
algorithm is therefore incomplete as a general inventory contract.

### Equipped state

The reference finds equipped weapons and apparel by traversing inventory
entries and checking each instance for worn/worn-left extra data
(`src-fo3/main.cpp:364-390,947-1004`). This is more reliable than a selected
inventory row and preserves condition-specific equipped state.

Conceptual rules:

- A weapon is equipped when a weapon entry has the appropriate worn marker.
- Apparel can have multiple simultaneously worn forms; record each exact worn
  instance and condition.
- A thrown weapon (reference types 10-12) represents the readied whole stack;
  ordinary weapons represent one equipped instance
  (`src-fo3/main.cpp:1448-1456`).
- `WornLeft` must be accepted when determining worn apparel. Its exact meaning
  for every Fallout 3 equipment class needs runtime validation.
- The bottom UI must update from a confirmed equipment-state change, never by
  writing state during a refresh.

### Item metadata and derived statistics

The SDK exposes useful 1.7.0.3 record concepts:

- weapons contain name, icon, value, weight, maximum health, attack damage,
  ammo form, clip rounds, equip type, weapon type, weapon skill, projectile,
  AP cost, critical data, and flags (`GameForms.h:2121-2365`);
- armor contains name, value, weight, maximum health, biped/equipment data and
  armor rating (`GameForms.h:2001-2026`);
- notes contain name and icon, but note contents are not mapped in this SDK
  (`GameForms.h:2478-2495`).

The reference computes displayed direct weapon damage from base damage, weapon
skill, condition, and melee/unarmed modifiers
(`src-fo3/main.cpp:398-465`). It explicitly cannot compute explosion damage
because the relevant FO3 projectile/explosion fields are not mapped. Treat this
formula as a candidate behavior to compare against the vanilla Pip-Boy, not a
complete damage API. Prefer a native game “display value” builder if it can be
identified, because perks, ammo, effects, difficulty, projectiles, and scripted
weapons can alter the value.

### Perks

The reference enumerates the global loaded perk list and invokes the vanilla
`HasPerk` condition evaluator for every perk to obtain rank
(`src-fo3/main.cpp:1112-1140`). The semantic concept is sound: perk state is a
player-specific rank over loaded perk forms. The polling implementation is not.
Build the list at load and update ranks from perk/player-change events or a
coarse dirty refresh, not ten full evaluator sweeps per second.

### Effects and status

The bundled SDK names `EffectSetting` and `ActiveEffect`, but `ActiveEffect` has
no mapped fields (`GameEffects.h:3-25`). The reference only uses `MagicTarget`
to test/remove the Pip-Boy-light effect. There is no general active-effect
enumeration.

Required independent work:

- locate the player's active-effect collection and its owner/lifecycle;
- resolve source magic item/effect setting, display name, icon, magnitude,
  duration, remaining time, detrimental/beneficial flags, addiction/disease and
  stacking behavior;
- determine the same filtering and formatting rules used by the vanilla STAT
  Effects page;
- invalidate on effect add/remove/update without scanning every frame.

Confidence: low semantic; none on 1.7.0.4.

### Quests and objectives

The SDK provides useful but incomplete concepts:

- `DataHandler` owns a quest list at 1.7.0.3 offset `+0x0D4`
  (`GameData.h:262-306`).
- `TESQuest` contains a running byte, priority, stage list, objective list,
  conditions and script event list (`GameForms.h:2643-2682`).
- `BGSQuestObjective` includes an objective ID, display text and owning quest
  pointer (`GameForms.h:1335-1351`).
- The 1.7.0.3 `PlayerCharacter` mapping includes current quest and current
  objective pointers (`GameObjects.h:183-193`).
- FOSE's own sample quest commands read those pointers and enumerate objective
  IDs (`Commands_Quest.cpp:5-68`). Its “set current quest” command directly
  assigns the player pointer (`Commands_Quest.cpp:74-82`), which is not enough
  evidence that marker/UI/event state is updated correctly.

Missing pieces include started/completed/failed state, objective displayed and
completed flags, localization/format substitution, objective targets and map
markers, ordering, DLC/mod behavior, and a safe tracked-quest mutation path.
Do not expose every loaded quest; first reproduce the vanilla active-quest data
builder and filtering rules. Do not implement tracking by direct pointer write
unless runtime tracing proves all downstream effects; prefer the native Pip-Boy
action/function that updates quest tracking and markers.

### Notes

`DataHandler` has a global note-form list at 1.7.0.3 offset `+0x12C`, and the
player has one `BGSNote*` field at `+0x570` (`GameData.h:304-307`,
`GameObjects.h:167-168`). These establish form/catalog and current-selection
concepts only. They do not identify the player's acquired-note set, read state,
note content variants, audio notes, or ordering. Confidence: low.

### World/local map and markers

The SDK maps player reference position and cell linkage conceptually:

- `TESObjectREFR` carries rotation, position, scale, parent cell, and extra-data
  list (`GameObjects.h:49-80`).
- a cell carries coordinates, land, object list, scene nodes, and worldspace;
  a null worldspace denotes an interior in the 1.7.0.3 mapping
  (`GameForms.h:2539-2581`);
- a worldspace carries map textures, cell map, climate, pointer maps and other
  only partly identified map data (`GameForms.h:2585-2635`).
- `ExtraMapMarker` exists, but its payload is completely opaque in this SDK
  (`GameExtraData.h:717-724`).

This is enough to define a player-location generation key
`(worldspace/cell, x, y, z, heading)`, but not enough to implement the map.
Discovery state, marker name/type/visibility/fast-travel flags, world-map
projection, local-map geometry/fog, quest targets, and vanilla filtering still
require binary tracing. Never derive the map by driving the gameplay camera or
capturing the upper framebuffer.

### Radio

The SDK identifies an `ExtraRadioData` extra-data type with size `0x1C`, but all
four payload fields are unknown (`GameExtraData.h:1225-1236`). The reference
contains no radio reader or command. Station catalog, broadcast/reference
association, signal availability, current station, playback state, and tune/off
functions remain unknown. Confidence: low.

### Pip-Boy light

The reference treats the light as a self-targeted magic effect, with the base
spell at FormID `0x00000147`. It checks effect presence on the player's magic
target and casts/removes the effect through engine methods
(`src-fo3/main.cpp:608-749`). This is a useful semantic model, but the direct
global/function addresses and caster/target layouts are 1.7.0.3-only.

## 1.7.0.3 engine layout ledger

These are factual descriptions of the bundled SDK, not approved 1.7.0.4
layouts.

| Type | Relevant 1.7.0.3 layout | Why it matters | Semantic confidence |
|---|---|---|---|
| `TESForm` | size `0x18`; type `+0x04`, flags `+0x08`, FormID `+0x0C`, mod refs `+0x10` | form validation and identity | High |
| `TESObjectREFR` | size `0x54`; base form `+0x1C`, rotation `+0x20`, position `+0x2C`, scale `+0x38`, parent cell `+0x3C`, extra data `+0x40` | player lifecycle/location/inventory | High |
| `Actor` | magic caster `+0x80`, magic target `+0x8C`, actor-value owner `+0x9C`, cached values `+0xA0` | stats and effects | Medium |
| `PlayerCharacter` | size `0x9BC`; note `+0x570`, third-person flag `+0x5A8`, disabled controls `+0x5DC`, topic `+0x604`, quest `+0x614`, objective `+0x618` | lifecycle, current quest/note, controls | Medium |
| `ExtraContainerChanges::EntryData` | extra list pointer, signed delta, base-form pointer | inventory stacks | High |
| `TESContainer::FormCount` | signed count plus form pointer | merge base inventory with deltas | High |
| `TESObjectWEAP` | size `0x20C`; metadata and weapon data described above | item stats/ammo/categories | High for concept |
| `TESObjectARMO` | size `0x180`; value/weight/health/biped/equip/armor fields | apparel stats/categories | High for concept |
| `DataHandler` | size `0x5B8`; worldspace list `+0x0C`, spell list `+0x3C`, quest list `+0xD4`, perk list `+0x11C`, note list `+0x12C`, mod list `+0x19C` | loaded-form catalogs | Medium |
| `TESQuest` | size `0x6C`; running `+0x3C`, priority `+0x3D`, stages `+0x44`, objectives `+0x4C`, conditions `+0x54`, events `+0x5C` | quest list/data | Medium |
| `BGSQuestObjective` | vtable, ID `+0x04`, display string `+0x08`, quest `+0x10`, then unknown fields | objective text/ownership | Medium-low |
| `TESObjectCELL` | size `0xE0`; coordinates `+0x44`, land `+0x48`, objects `+0xAC`, scene nodes `+0xB4/+0xB8`, worldspace `+0xC0` | local/world context | Medium |
| `TESWorldSpace` | size `0xE8`; map texture `+0x24`, cell map `+0x30`, cell `+0x34`, climate `+0x40`; many unknown fields | world map | Low-medium |
| `BGSNote` | size `0x80`; name and icon mapped, contents unknown | note catalog | Low-medium |
| `ExtraMapMarker` | size `0x10`, payload unknown | map markers | Low |
| `ExtraRadioData` | size `0x1C`, payload unknown | radio | Low |
| `ActiveEffect` | type named, fields unknown | status effects | Low |

Validation rule for each type on `1.7.0.4`: identify construction/accessor code,
validate RTTI/vtable and size, prove field semantics using at least two runtime
state changes, then gate the dependent feature independently.

## Address revalidation ledger

All values below are preferred-base virtual addresses reported for the
reference's `1.7.0.3` target. They must never be called or dereferenced in
`1.7.0.4`. Locate semantic signatures in `.text`, decode relative references,
convert to module-relative locations, and validate the target section/type.

### Directly used by the reference plugin

| 1.7.0.3 VA | Reported semantic role | Reference |
|---:|---|---|
| `0x00F52B70` / `0x00F54B50` | console command-table begin/end | `fo3_engine.h:49-55` |
| `0x00F54B78` / `0x00F5A438` | script command-table begin/end | `fo3_engine.h:49-55` |
| `0x0106C86C` | cached Pip-Boy-light spell pointer | `main.cpp:617-624` |
| `0x006B6D00` | magic-target effect-presence query | `main.cpp:647-661` |
| `0x006B76E0` | magic-target effect removal | `main.cpp:701-709` |
| `0x0061B500` | item-list refresh | `main.cpp:1146-1156` |
| `0x0086C220` | global time-multiplier setter | `main.cpp:1158-1173` |
| `0x01090BA0` | object/global passed to time-multiplier setter | `main.cpp:1165-1172` |
| `0x00BF37B0` | tile XML reader | `main.cpp:1183-1186` |
| `0x007198E0` | alternate actor equip function | `main.cpp:1459-1469` |
| `0x007133E0` | actor unequip function | `main.cpp:1459-1469` |
| `0x00729880` | player appearance/3D update | `main.cpp:1471-1475` |
| `0x00BEE190` | tile float setter selected by FOSE 1.7 build | `GameTiles.h:28-31` |

Diagnostic anchors mentioned by comments, but not stored as callable constants,
also require rediscovery: the Pip-Boy-light bootstrap near `0x00408A2C`, light
handler near `0x007748C0`, cast branch near `0x007749F3`, removal branch near
`0x00774975`, and vanilla Dispel path near `0x005234A0`
(`main.cpp:617-623,647-650,687-704`).

### Indirect SDK dependencies exercised by the reference

| 1.7.0.3 VA | SDK role | Source |
|---:|---|---|
| `0x00C0050A` | engine dynamic cast | `GameAPI.cpp:131-139` |
| `0x00455190` | lookup form by runtime ID | `GameAPI.cpp:131-139` |
| `0x0106CDCC` | data-handler singleton pointer | `GameAPI.cpp:141-147` |
| `0x0107A104` | player singleton pointer | `GameObjects.cpp:28-42` |
| `0x01075B24` | interface-manager singleton pointer | `GameInterface.cpp:50-55` |
| `0x0106A7BC` | tile-menu array | `GameInterface.cpp:50-55` |
| `0x011793DB` | menu-visibility array | `GameInterface.cpp:50-55` |
| `0x0061B850` | queue UI message | `GameInterface.cpp:50-55` |
| `0x00F4A748` | form-record metadata pointer | `GameData.cpp:11-18` |
| `0x00F46E68` | RTTI for base extra data | `GameRTTI_1_7_3.inl:14` |
| `0x00F46078` | RTTI for `TESForm` | `GameRTTI_1_7_3.inl:194` |
| `0x00F49F3C` | RTTI for health-form component | `GameRTTI_1_7_3.inl:195` |
| `0x00F461FC` | RTTI for scriptable-form component | `GameRTTI_1_7_3.inl:204` |

Other SDK addresses such as heap functions, TLS, save managers, all RTTI
objects, and member-function macros must be considered contaminated by version
assumptions if any bundled SDK source is linked. The clean-room implementation
should define only the minimal interfaces it validates rather than compiling
the 1.7.0.3 SDK wholesale.

## Lifecycle and event strategy

The reference combines FOSE messaging with a fallback check that considers the
game loaded when the player singleton and its parent cell are non-null
(`src-fo3/main.cpp:1885-1906`). It handles pre-load, post-load, new game,
exit-game and exit-to-main-menu messages (`src-fo3/main.cpp:2181-2236`). The
bundled FOSE messaging interface documents additional load/save events and a
successful-load flag (`PluginAPI.h:152-221`).

Useful clean-room lifecycle contract:

1. Maintain a monotonically increasing **game generation**.
2. Enter `NoGame` at process start/main menu; no engine pointer is publishable.
3. On pre-load/new-game start, invalidate the generation, cancel commands,
   clear raw-pointer caches, and publish a loading snapshot.
4. After the native load finishes successfully, wait for player, parent cell,
   data handler and the chosen update synchronization point to be simultaneously
   valid for multiple ticks before publishing `Ready`.
5. Treat cell/worldspace changes as location revisions, not new player
   generations, while still invalidating cell-owned map pointers.
6. On death/reload, failed load, main menu and shutdown, reject stale commands
   and release renderer/UI references without touching destroyed game objects.
7. Every snapshot and command includes its game generation.

FOSE messages are valuable semantic evidence, but FOSE 1.3b2 targets
`1.7.0.3`. For `1.7.0.4`, identify equivalent native save/load dispatch points
or use a separately validated compatibility interface. A Win32 `WM_TIMER` on
the game window, as used by the reference (`fo3_engine.h:296-349`), runs on the
window thread but is not proof of a safe engine-update phase. It can fire while
menu, load, or window messages are being processed. It should not be the mod's
game-state synchronization point.

## Action contracts and preconditions

All mutations must execute at the validated game-thread/update safe point. A UI
or transport thread only enqueues a bounded immutable command. Every action
requires:

- supported executable identity and validated feature signatures;
- current `Ready` game generation matching the request;
- live player and current cell, with no load/death/main-menu transition;
- re-resolution of the form or object token immediately before action;
- expected form type and playable/actionable flags;
- item ownership and exact stack/instance resolution where applicable;
- action-specific menu/dialog/container/barter/repair restrictions;
- request ID for exactly-once processing;
- postcondition observed from engine state before acknowledging success.

Action-specific contracts:

| Action | Preconditions beyond common checks | Completion proof | Reference lesson | Confidence |
|---|---|---|---|---|
| Equip weapon | WEAP, owned count > 0, exact condition instance when selected, engine permits use | exact instance gains worn state; weapon/ammo/derived stats revision changes | worn flags and per-condition stack selection are necessary | High semantic |
| Equip apparel | ARMO, owned, slot/equip restrictions validated | requested instance worn; displaced slots reflected | do not assume one apparel item or one stack | High semantic |
| Unequip | exact instance currently worn; engine permits removal | worn/worn-left removed and replacement state observed | reject UI-only toggles and stale form IDs | High semantic |
| Consume Aid | AID/Alchemy, positive count, usable now, exact instance if stateful | count decrements once and effect appears when applicable | reference routes `USE` through equip semantics, but this needs native-path proof | Medium |
| Track quest | running/player-visible quest, valid objective state | native tracked quest/objective and markers change | direct player-pointer assignment is insufficient evidence | Low-medium |
| Radio tune/off | station available/in range; radio manager ready | native current station/playback state changes | no reference implementation exists | Low |
| Pip-Boy light | validated spell/effect and actor magic components | effect presence reaches requested state | self-targeted magic-effect model is useful | Medium-high |

The reference debounces repeated equip commands for 500 ms
(`src-fo3/main.cpp:1738-1749`). Our mod should not use timing as the primary
exactly-once mechanism. A request ID plus state postcondition distinguishes a
retransmit from a legitimate rapid switch.

The reference calls low-level equip/unequip addresses and then forces a player
appearance rebuild (`src-fo3/main.cpp:1650-1687,1865-1870`). It also refreshes
the vanilla Pip-Boy list after every command, even though its own interface is
external (`src-fo3/main.cpp:1872`). For our always-on custom UI, find the
highest-level native action that performs inventory, equipment, animation,
events, audio and 3D updates correctly. Do not compensate with a broad 3D
rebuild or call a vanilla-menu refresh when that menu is closed.

## Polling and performance audit

The reference architecture is unsuitable for the user's negligible-overhead
target without redesign.

### Confirmed high-frequency work

- A window timer pumps every 50 ms (`main.cpp:64-66`).
- A full player snapshot is rebuilt every 100 ms (`main.cpp:55-62,1974-1991`).
- Every snapshot enumerates the static load order (`main.cpp:785-806`).
- Every snapshot walks equipped state and the entire inventory, allocates maps,
  sets and vectors, groups every stack, formats strings and serializes the
  complete JSON document (`main.cpp:911-1110`).
- Every snapshot enumerates every loaded perk and calls a game evaluator for
  each (`main.cpp:1112-1140`).
- The pipe thread wakes every 50 ms, locks and copies the whole snapshot string,
  compares it with the previous string and writes only if changed
  (`main.cpp:2086-2101,2137`).
- Verbose mode flushes the log after every line and can log complete snapshots
  in chunks (`main.cpp:172-229`).

The “send only when bytes differ” optimization saves transport traffic, not the
dominant game-thread collection, allocation, evaluation, and serialization
cost. The expensive snapshot has already been rebuilt.

### Correct event-driven split

Use separately revisioned domains:

| Domain | Preferred invalidation | Heartbeat ceiling if no event is found |
|---|---|---:|
| HP/AP/radiation/limbs | actor-value change hook; compact scalar comparison | 10 Hz, no allocation |
| Position/heading | fixed scalar sample only while map page visible | 10-20 Hz |
| Inventory/equipment/ammo/caps | container/equip/use/drop transaction hooks | 1 Hz audit checksum |
| Effects | effect add/remove/update hooks | 1 Hz audit |
| Quests/objectives | quest stage/objective/tracking hooks | 1 Hz audit |
| Map markers | discovery/visibility and cell/worldspace events | on transition plus 1 Hz audit |
| Radio | station availability/tune/playback events | 2-4 Hz only while Radio page visible |
| Load order/static metadata | once per game generation | no periodic poll |
| Perk catalog/ranks | load/level/perk-change/effect-change | on dirty only |

Publish immutable per-domain snapshots by revision. The bottom UI requests only
changed domains. Build strings/layouts away from the game thread using copied
plain data. Bound collection sizes and queues; never retain engine pointers in
the UI snapshot.

### Crash, deadlock, and correctness hazards not to inherit

- The reference detaches its pipe thread and only flips an atomic flag during
  DLL detach; the thread may remain blocked in `ConnectNamedPipe` or run plugin
  code after unload (`main.cpp:2296-2299,2310-2326`).
- DLL detach takes a log mutex and performs file I/O while the loader lock is
  held, risking deadlock.
- The command queue and partial input buffer are unbounded
  (`main.cpp:2113-2132`).
- `g_gameLoaded` and several pump/window globals cross threads without complete
  synchronization.
- `catch (...)` blocks suppress errors but ordinarily do not make invalid
  pointer dereferences safe; silent failure destroys diagnostic value.
- Snapshot traversal races with game mutations unless the timer happens to land
  at a safe engine point. “Main window thread” is not sufficient proof.
- Main-thread XML temp-file creation and parsing occur while applying a sync
  lock (`main.cpp:1296-1305`), and the lock freezes normal play.
- Directly writing control flags and restoring time multiplier to hardcoded
  `1.0` can clobber another system's current state.
- A whole-snapshot mutex/string copy increases contention and allocation.
- Refreshing the vanilla Pip-Boy and rebuilding player 3D after custom actions
  are broad side effects with avoidable crash/performance risk.
- Inventory display omits base-container counts while action count includes
  them, allowing UI/action disagreement.
- Command success is logged from dispatch return, not universally from observed
  postcondition; some vanilla command handlers can report execution despite
  failed argument extraction (`fo3_engine.h:217-242`).

## Actionable clean-room interface matrix

This matrix is the handoff contract for independently authored readers and
actions. “Validation” is the minimum evidence required on the exact installed
`1.7.0.4` build.

| Interface/domain | Independent read contract | Dirty/event contract | Optional action contract | Required 1.7.0.4 validation | Confidence |
|---|---|---|---|---|---|
| Runtime identity | PE/version/text fingerprint and module sections | process startup only | none | exact installed hash, ASLR base, section bounds | High semantic / none runtime |
| Lifecycle | generation, state, player-ready, cell/worldspace | pre/post load, new game, death/reload, main menu, exit | reject/cancel stale actions | trace native lifecycle dispatcher and safe update phase | High semantic |
| Player identity | name and stable generation-local identity | new/load/player replacement | none | player singleton signature and type/vtable checks | High semantic |
| Vitals | current/max HP and AP, level, XP, radiation | actor-value changes | none | compare damage/heal/AP/rads/level against vanilla HUD | High except radiation unknown |
| SPECIAL/skills | current and optionally base values | AV/effect/equipment/load changes | none | verify IDs, vtable operations and modifier behavior | High semantic |
| Limb/status | all limbs including brain, condition, DR/derived stats | damage/heal/effect/equipment | none | reproduce STAT page values in multiple states | Medium |
| Active effects | source, name, icon, magnitude, duration, flags | effect add/remove/tick expiry | none initially | map active-effect collection and vanilla filters | Low |
| Form resolver | runtime ID to validated form/plugin/local ID | load-order generation | none | lookup signature, form layout, plugin ownership | High semantic |
| Static item metadata | category, name, description, icon, value, weight and base stats | load order/mod data changes only | none | validate form subclasses/fields and localization | Medium-high |
| Inventory | immutable stack records with exact instance tokens | add/remove/use/drop/buy/sell/repair/load | bounded generation-tagged commands | merge base counts/deltas and test all inventory scenarios | High concept |
| Equipment | exact worn weapon/apparel instances and ammo relation | equip/unequip/use/break/repair/load | native high-level equip/unequip | trace pipeline and prove animation/3D/event behavior | High concept |
| Aid use | inventory plus usable/effect metadata | consume and effect changes | consume once | identify native use path and postcondition | Medium |
| Ammo | owned counts, equipped ammo form, clip/loaded count | inventory, equip, reload, fire | none initially | trace weapon/ammo runtime state | Medium |
| Caps | owned currency count | loot/trade/reward/spend/load | none initially | resolve currency form and exact count path | Medium |
| Perks | acquired ranks over loaded perk catalog | perk/level/load changes | none | native rank path and multi-rank proof | High concept |
| Quests | player-visible active quests, states, stages, objectives | start/stage/objective/complete/fail/load | native track/untrack | trace vanilla Pip-Boy quest builder and tracking setter | Medium-low |
| Notes | acquired/read notes and content/audio metadata | acquire/read/load | select/play where safe | trace acquired-note store and content resolver | Low |
| Player map transform | worldspace/cell, x/y/z/heading | movement/cell/fast travel | none | coordinate conversion versus vanilla map | Medium |
| World markers | name/type/position/discovered/visible/fast travel | discover/state/quest/cell/load | fast travel out of initial scope unless explicitly authorized | map `ExtraMapMarker` and Pip-Boy filtering | Low |
| Local map | cell geometry/fog/player transform | cell load, movement, discovery | pan/zoom only in custom UI | trace vanilla local-map data generation without camera hijack | Low |
| Radio | station list, signal, active station, playback | availability/tune/off/cell/load | native tune/off | trace radio manager and `ExtraRadioData` | Low |
| Pip-Boy light | magic-effect presence | effect/toggle/load | cast/remove validated light effect | relocate global/functions/layouts | Medium-high concept |
| Menu safety | gameplay/menu/load restrictions only; no menu as data source | menu open/close | gate actions | relocate visibility state or use native dispatcher | Medium |

## License and attribution constraints

### PipBoy3000Companion

The reference repository declares its original source to be licensed under
Creative Commons Attribution-NonCommercial-ShareAlike 4.0 and names Aidan
Lee-Calamera/Aidan's Lab as copyright holder (`LICENSE:1-9`; source-file headers
at `src-fo3/main.cpp:1-16` and `src-fo3/fo3_engine.h:1-16`). Its README credits
the FOSE and xNVSE teams (`README.md:128-138`).

Implications:

- Copying or adapting its implementation would require attribution,
  noncommercial use, and ShareAlike distribution, and may be incompatible with
  the intended license/distribution model of WEMU or Dualscreen Companion.
- This audit therefore records behavior and facts only. The mod implementation
  must be written independently, with different internal structure and no
  copied code, UI XML, comments, strings, JSON protocol, or helper algorithms.
- Retain provenance in engineering documentation: repository URL, author,
  commit, inspection date and the fact that it was used only as a semantic
  reference.

### Bundled FOSE SDK

The reference README says the official FOSE SDK is obtained from
`fose.silverlock.org` and credits the FOSE team (`README.md:95-97,128-133`). The
bundled SDK tree contains no top-level FOSE license or readme in the inspected
copy. The PipBoy3000Companion CC license cannot be assumed to relicense this
third-party SDK. One FOSE utility also embeds separately copyrighted
Mersenne-Twister code with its own redistribution notice
(`FOSE/fose/fose/Utilities.cpp:271-296`).

Until FOSE's authoritative license is obtained and reviewed:

- do not copy, compile, redistribute, or package the bundled FOSE source in
  Fallout3SecondScreen;
- do not assume its headers are compatible with the mod's distribution terms;
- independently define only the minimal ABI facts proven necessary for
  interoperability against `1.7.0.4`;
- credit the FOSE team and source site for semantic/layout reference in the
  technical documentation;
- do not ship FOSE binaries, downgrade the user's executable, apply the
  Anniversary Patcher, or bypass DRM.

This is an engineering constraint summary, not legal advice. Any future reuse
of implementation text or SDK code needs license-owner clarification and
project-level legal review.

## Immediate reverse-engineering targets

Prioritized next steps for `1.7.0.4`:

1. Find the player singleton and a safe game-update synchronization point.
2. Validate `TESForm`, `TESObjectREFR`, actor-value owner and container-extra
   layouts using HP/AP and a two-item inventory change.
3. Identify native container/equipment change notifications before writing a
   general inventory serializer.
4. Trace the highest-level equip, unequip and Aid-use paths from the vanilla
   Pip-Boy; prove their preconditions and postconditions without opening it.
5. Trace the vanilla Pip-Boy data builders separately for effects, quests,
   notes, world/local markers and radio. The bundled SDK is insufficient for
   these domains.
6. Record signatures as masked semantic patterns plus structural validators,
   never as the 1.7.0.3 virtual addresses listed above.
7. Instrument domain timings and revision rates before adding the lower-screen
   renderer. Reject any design that performs a full inventory/perk/quest scan on
   a 100 ms timer.

## Bottom line

The reference gives high-confidence concepts for actor-value access,
inventory-extra-data traversal, condition-aware stacks, worn-state detection,
load-order identity and game-thread-only actions. It does not solve the user's
quest, note, map, radio, active-effect, UI, renderer or input requirements, and
its polling/threading design should not be inherited. Its `1.7.0.3` addresses
and layouts are useful search anchors only. The clean-room implementation must
revalidate every dependency on `1.7.0.4`, use event-driven domain revisions,
and invoke mutations through fully traced high-level engine paths.
