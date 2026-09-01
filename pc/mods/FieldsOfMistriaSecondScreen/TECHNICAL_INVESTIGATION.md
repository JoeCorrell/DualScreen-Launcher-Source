# Fields of Mistria dual-screen technical investigation

Last updated: 2026-08-30

This is the living implementation record for the current, post-migration game.
Historical `data.win`, UndertaleModTool, and GameMaker runner layouts are not
valid evidence for this project.

## Verified target

- Device: AYN Thor, Android serial `bd88415a`.
- The Downloads directory contains only an incomplete/redist shell at
  `/storage/emulated/0/Download/Fields.of.Mistria.v1.0.4`.
- The runnable installation is
  `/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4`.
- `FieldsOfMistria.exe`: PE32+ x86-64, file/product version `1.0.4`,
  13,657,696 bytes, SHA-256
  `aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e`.
- PE link timestamp: 2026-08-19 11:08:07 (as decoded by `objdump`).
- Embedded application revision candidate: `e0c5fee` (static evidence beside
  the current build-SHA implementation; the executable hash remains the gate).
- Steam app id from the installed settings: `4317790`.

Required installed component identities:

| File | Version/size | SHA-256 |
| --- | --- | --- |
| `assets.zip` | 625,619,136 bytes | `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450` |
| `SDL3.dll` | 3.4.8, 2,811,488 bytes | `532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157` |
| `Maybe.toml` | 514 bytes | `96cd2cb00de88a9657a38776425adbb53d0fce2fc56d1d86764bef69a22e4005` |
| `fmod.dll` | 1,888,864 bytes | `76dee791122272d50cc92899490da44994e4eb62ee36bd32e21f2fd9a7251d77` |
| `fmodstudio.dll` | 1,588,832 bytes | `8f18508f2135c1525b4f5958cb54c7cfcc924ff00002c012c4249f5fd8dd956c` |

All invasive adapters and actions must fail closed unless these exact build
identities match. A later supported build gets a separate profile.

## Current engine architecture

The executable is the new NPC Studio `maybe` engine, not the old GameMaker
runner. Binary evidence identifies a Rust build and internal crates/modules
including `mwe`, `mwa`, and `mwr`; build paths include the `maybe` checkout
revision prefix `8a1edbd`. Rendering is OpenGL through SDL 3.4.8. Audio is
FMOD Studio. The executable imports the SDL window, OpenGL context, event,
gamepad, cursor, clipboard, and display APIs directly.

The current engine deliberately executes source-form GML through its own VM.
The installed `assets.zip` contains approximately 122,000 entries, including:

- 567 current `.gml` scripts/objects;
- 317 FIDDLE entries/directories;
- 4,648 T2 dialogue/cutscene entries;
- 406 Mist-script entries;
- current Tiled TMX/TSX room data;
- current animation PNG/meta assets, fonts, UI, localization, audio, and data.

This makes the current GML a valid interoperability surface while old
GameMaker binary layouts remain invalid.

## Official SDK boundary

NPC Studio's public `mistria-sdk` currently documents v0.16.4 while this game
is v1.0.4. It provides asset/meta/FIDDLE documentation, `maybe`, `vaultc`, CLI
diagnostics, and BUGGER documentation. It does **not** provide a runtime plugin
loader, ABI, event bus, live-state queries, action calls, second-window API, or
input extension API. Every schema lead must therefore be re-proved against the
installed archive. See `docs/SDK_ENGINE_RECON.md`.

BUGGER and `--debug-tools` are validation aids for disposable saves only and
will not be production dependencies. `vaultc` may be used only on copied saves
to compare persisted schemas; live synchronization will never poll saves.

## Confirmed current state owners

These identifiers come from the exact installed v1.0.4 archive:

- `assets/gml/objects/Game.gml` creates the persistent `Game` object and owns
  the stable `create`, `step_begin`, `step_end`, `room_start`, and `room_end`
  lifecycle callbacks. It constructs `ARI`, `QUEST_LOG`, `CALENDAR`, `WEATHER`,
  `NPCS`, museum progress, and other world systems. `step_begin` updates input,
  `CLOCK`, and T2. This is the current additive bridge-bootstrap candidate.
- `GameplaySystems/Player/Ari.gml` defines `ARI` as `global.__ari`. Current
  methods include `get/set/modify_health`, `get/set/modify_stamina`, and
  `get/set/modify_mana`. Its current fields include inventory, held-item index,
  health, stamina, mana, almanac state, quest artifacts, skills/progression
  leads, and renown leads.
- `GameplaySystems/Inventory/Inventory.gml` provides `Inventory`,
  `InventorySlot`, and `InventorySubscriber`. Every slot carries an update
  counter; `InventorySubscriber.pull()` is an existing event-like dirty seam
  and is preferable to rebuilding inventory each frame.
- `GameplaySystems/Quests/QuestLog.gml` defines `QUEST_LOG` as
  `global.__quest_log`, with active/completed collections and real
  `start`/`complete`/`ActiveQuest.progress` paths.
- `GameplaySystems/TimeSeasons/Clock.gml` defines `CLOCK` as
  `global.__clock`, including current time/update and formatting methods.
- `Debug/Tattletale.gml` independently reads live weather, room, date/time,
  player position, HP, stamina, inventory, hotbar selection, and player FSM
  state without opening a menu or reading a save.
- Dedicated current implementations exist for Weather, Calendar, MapMenu,
  RelationshipsMenu, Ranching/Animal/Stable, Spells, Museum/Almanac,
  RenownUtils, Recipe/Crafting, NPC database/schedules, and Location.

The exact remaining field/action map is tracked in `docs/GML_STATE_RECON.md`.

## Selected architecture

The foundation is deliberately split into replaceable boundaries:

1. A version-gated native SDL3 proxy forwards the user's exact original SDL3
   ABI and owns a no-activate Win32 lower-panel window. It does not hook the
   gameplay renderer or move the game camera.
2. A narrowly patched current GML bridge publishes bounded versioned topic
   deltas from `Game` lifecycle/update callbacks and drains validated commands
   on the game thread. Static IDs are strings or 64-bit asset IDs, never magic
   enum ordinals.
3. Native state storage owns immutable copies by world epoch and topic
   revision. The UI never retains VM/game pointers.
4. WEMU owns physical touch and controller capture. Ordered loopback UDP input
   is consumed by the private panel and never mirrored into the game window.
5. UI assets are resolved from the player's installed `assets.zip` through a
   bounded read-only cache. No extracted proprietary art is shipped.

The exact UI baseline is the installed 426x240 logical canvas with a centered
410x234 Journal shell and 189x218 pages. The implemented provider validates 52
fixed animations, all 11 authored maps, six fonts, and six typed dynamic visual
classes by path, ID, frame geometry, CRC, and class root. The renderer indexes
the ZIP once, validates paired metadata, decodes requested PNGs/TTFs in memory,
uses nearest-neighbor presentation, and keeps bounded independent provider and
renderer caches. Dynamic item, portrait, NPC-map, animal, skill, and spell art
comes from source-selected bridge logical IDs, so the renderer never guesses
unlock or discovery state. Ambiguous, corrupt, oversized, or cross-class
requests fail closed to custom primitives.
See `docs/UI_ASSET_RECON.md`.

Because the current VM exposes proven buffer/file and atomic-rename APIs but no
proven socket or FFI API, the implementation uses small dirty-only
JSON state and command files under `CONFIG_DIRECTORY/dualscreen`. This is not
save polling: GML publishes bounded deltas only after state changes and drains
commands on `Game.step_end`. A later native interception/IPC transport remains
the optimization target after the exact path and VM behavior are measured.
The native reader accepts only validated 20-topic full/delta chains and the GML
writer exposes only the narrowly proven game-thread actions listed below.

## Confirmed synchronization and action rules

- `Game.step_end` is the selected game-thread update/action point. `Game` is
  persistent across room changes; a fresh instance naturally resets the bridge
  for a new/load game. `CLOCK` is replaced at day rollover and must never be
  cached by object reference.
- Inventory and armor use independent `InventorySubscriber` instances, followed
  by changed-slot patches. A full snapshot is sent on bridge construction,
  inventory identity/size changes, and world-epoch changes.
- Player position is demand-sampled at 10 Hz; visible quest/status aggregates
  use dirty hooks plus bounded 4 Hz safety fingerprints. Clock output changes
  only on displayed-minute changes.
- Safe actions are validated hotbar selection through `ARI.set_held_item_index`,
  inventory slot swaps through the normal slot primitive, armor equip/unequip
  through vanilla slot constraints, consumable requests through Ari's ordinary
  Use FSM, and learned-spell pin/unpin/cast through the exact vanilla paths.
  Item actions additionally require the exact published instance fingerprint.
  Quest selection remains custom-UI-only because v1.0.4 has no persistent
  tracked-quest model.
- `use_item_fast` is explicitly forbidden for inventory actions: it clears the
  normal `remove_item_at` state and can apply effects without consuming the
  item. Consumables require a one-shot request inserted into the ordinary Ari
  FSM Use branch so all normal preconditions, consumption, and callbacks run.

## WEMU integration

The exact private identity is:

- class: `FieldsOfMistriaSecondScreenPanel`
- title: `Fields of Mistria Dual-Screen`

WEMU now recognizes this exact identity as an early lower-only window,
activates Select-scoped controller routing only while it owns the panel,
consumes its touch instead of injecting X/gameplay pointer events, and displays
the correct native-panel waiting state for Fields of Mistria sessions. Exact
positive/negative identity tests pass in both the engine and app modules.

The first live launch exposed one incorrect native assumption: WEMU advertised
the correct 1280x1800 root and 1240x1080 lower Android output, while Wine's
`EnumDisplayMonitors` exposed only one spanning Win32 monitor. The proxy loaded,
passed its compatibility gate, then logged `no non-primary monitor appeared`
and aborted panel creation. The fixed panel now prefers an enumerated secondary
but immediately falls back to the bounded/configurable Thor lower rectangle
`(0,720) 1240x1080`. WEMU's exact early-claim identity keeps this window off the
gameplay output even before final positioning. Generic-container window
detection now also includes exact Fields of Mistria identities, not launcher
names.

The updated Android app assembled successfully on 2026-08-30. Debug APK:
`app/build/outputs/apk/debug/app-debug.apk`, SHA-256
`3b14a0afbf78526ab915e8693d22e9d068460e76d97b2ae7eed6f177e17f7ab8`.
It was installed with `adb install -r` on the current device and package
`com.wemu` reports version `0.1.0`, code `1`, with an update timestamp of
2026-08-30 21:59:28 local device time. APK badging was checked before install;
the separate launcher package was not targeted.

## Save-load runtime incident (2026-08-30)

The first real save-load attempt produced the game's own
`AppData/Local/FieldsOfMistria/error_log.json`, not a WEMU or Wine crash. The
exact VM report was `type error, expected string, found undefined`, with the
bridge stack at `DualScreenBridge.gml:240 -> 1954 -> 2710` from
`Game.gml:585`. The failing value was `LOCATIONS[CURRENT_LOCATION_ID].name`
while the save was in `rm_adelines_office`.

This is an authored optional field: v1.0.4 `Location.gml` loads `name` without
a default, and vanilla room-title code checks it for `undefined` before
localization. The bridge now resolves a location name only when the key exists,
otherwise using the authored parent-map name and finally the stable
`LocationId` string. The same guard is used defensively for map-page titles.
A source audit found no second exact-v1.0.4 nullable localization/asset
conversion in snapshot initialization.

The corrected archive passed all 18 bridge policy tests and a full CRC scan of
all 122,404 entries. It was installed transactionally on the device as SHA-256
`8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59`;
the exact original archive remains retained and verified as
`e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450`.
An in-game reload is still required to prove the corrected runtime path.

## Deployment rules

- Dry run by default.
- Verify executable, SDL3, and asset archive hashes before mutation.
- Never overwrite an unowned proxy/original/config/asset patch.
- Stage and hash every file before publication.
- Preserve and verify the original SDL3 and `assets.zip` before replacement.
- Install only while the exact game process and managed game-file mappings are
  absent. Unrelated launcher, Wine, FEX, and Box64 processes are not targeted.
- Roll back transactionally on any verification failure.
- Never redistribute the original SDL3, game executable, archive, or extracted
  assets. The installer operates only on the user's installed copy.

## Current proof status

Confirmed:

- exact current build and module identities;
- current engine/script/data/renderer architecture;
- absence of a public runtime SDK ABI;
- concrete player/inventory/quest/time owners and lifecycle entry points;
- independent WEMU panel identity/input isolation integration;
- WEMU identity regression tests.
- current game-thread lifecycle, dirty seams, bounded diagnostic payload, and
  the safe/unsafe initial action boundaries;
- a successful full WEMU debug APK build and device installation with Mistria
  panel recognition;
- a previous native generation was installed on the exact v1.0.4 device build
  with the original SDL3 retained as
  `532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157`;
- the replacement native package that trusts the corrected archive and uses a
  separately logged hidden panel host passed the full deterministic gate:
  proxy `d751b9d49380e426635cbec0fa81604162c5b66c19c51349698fbef71bce8f19`
  and host `19a7354d8cea5e08baa00750f7cb3f5853668a7dfa515be92a8adbfdff07a53b`;
- that replacement native package was installed transactionally on the current
  ADB endpoint, the legacy visible helper was migrated to the hidden
  `.fomds-panel-host.exe`, and a subsequent exact-current `Owned` audit was a
  verified no-op. The previous owned generation remains recoverable at
  `.FieldsOfMistriaSecondScreen.backup.20260831T025344Z`;
- committed GML archive installation on the current ADB endpoint; the corrected
  save-load build is:
  patched archive
  `8faf44883c3756d317bed2ddaf1881c79c85402a67374e7dcccbc54998667b59`
  and retained original archive
  `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450`.
- all 20 topics, eight safe actions, the seven-page native journal, authored
  maps, source-selected dynamic visuals, and bounded asset caches pass their
  static/unit/integration build gates;

Not yet claimed:

- native proxy runtime under FEX/Wine after the current combined install;
- successful save load after the optional-location-name correction;
- live topic transport from the running game;
- any live value or gameplay action;
- device-side installed-art decode/presentation under FEX/Wine;
- long-session performance or lifecycle stability.

No placeholder value is considered proof. Each item moves to confirmed only
after it is observed changing in a running save and the corresponding log,
snapshot revision, and UI result agree.
