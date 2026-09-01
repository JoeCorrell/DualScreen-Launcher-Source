# Fields of Mistria current SDK / `maybe` engine reconnaissance

Audit date: **2026-08-30**
Scope: official NPC Studio material and the official `mistria-sdk` repository only. This document deliberately does not use pre-migration GameMaker tooling as implementation evidence.

## Executive conclusion

The current public NPC Studio package is useful for **asset/data reconnaissance and diagnostics**, but it is not a runtime mod SDK in the conventional sense. It provides documentation, two command-line programs, asset schemas, editable FIDDLE data, save inspection, game CLI switches, and an unstable in-game debug console. It does **not** publish a plugin ABI, library/header package, runtime event bus, callbacks for player/inventory/quest changes, a supported way to open a second rendering surface, or action APIs for manipulating live game state.

There is also an important version gap:

- NPC Studio's official site identifies **v1.0.4, released 2026-08-19**, as the newest public patch visible on the audit date.
- The `mistria-sdk` `main` branch still says it reflects **v0.16.4**. Its current `main` commit is `c7a92ffdbc6f8088c6c273ba6af2250931c3d208`, committed 2026-07-17 (`chore: updating to v0.16.4 (#3)`).
- There are no Git tags or GitHub Releases for the SDK repository as of the audit date.

Therefore, **none of the v0.16.4 schemas or tool behavior may be assumed compatible with an installed v1.0.x game without testing the exact installed build**. The current executable's own `sha` command and a cryptographic executable hash must gate any integration.

Confidence: **high**. This is directly established by the current official repo and NPC Studio's patch page.

## Primary sources and pinned baseline

- [NPC Studio `mistria-sdk` repository](https://github.com/NPC-Studio/mistria-sdk)
- [README pinned at audited commit](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/README.md)
- [SDK changelog pinned at audited commit](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/CHANGELOG.md)
- [NPC Studio v1.0.4 patch page](https://www.fieldsofmistria.com/post/patch-notes-v1-0-4)
- [NPC Studio new-engine v0.16.2 announcement](https://www.fieldsofmistria.com/post/new-engine-v0-16-2-patch-notes)

Local audit clone: `.work/mistria-sdk-official` (read-only research copy; do not ship it).

The official repository has no license file or GitHub license metadata. Do not copy its binaries into the mod package without a separate permission determination. Invoking a user-supplied/downloaded official tool during development is distinct from redistributing it.

## Version facts

| Subject | Confirmed value | Evidence | Confidence |
|---|---:|---|---|
| New-engine migration | v0.16.2, 2026-07-07 | NPC Studio says the game moved to an internally developed engine and that old-engine mods are incompatible | High |
| Latest public game patch visible on audit date | v1.0.4, 2026-08-19 | Official Fields of Mistria patch page | High |
| SDK documented target | v0.16.4 | SDK README | High |
| SDK `main` commit | `c7a92ffdbc6f8088c6c273ba6af2250931c3d208` | Local clone of official `main` | High |
| `maybe.exe` version | `maybe 0.8.0` | Executed official repository binary with `--version` | High |
| `vaultc.exe` version | `vaultc 0.16.4` | Executed official repository binary with `--version` | High |
| SDK release/tag status | No GitHub Releases; no Git tags | GitHub API and `git ls-remote --tags` on audit date | High |

Audited Windows tool hashes:

- `maybe.exe`: SHA-256 `4F8B53A92F1E21128116783B76A3907360F76E8EDD441A39CF6D3E617260354E`, 2,827,264 bytes.
- `vaultc.exe`: SHA-256 `438B48EE050D8E0BF0D421AED89EFAD0893B10FB678760EC44C3379F1F3D1280`, 735,744 bytes.

The SDK changelog labels v0.16.4 as `2024-09-27`, while the repository commit and surrounding entries are from July 2026. This is almost certainly a changelog date typo, but that is an inference; do not use that date for compatibility logic.

## What `maybe` is

NPC Studio uses **`maybe`** as the name of Fields of Mistria's internally developed engine. NPC Studio's migration announcement is explicit that pre-v0.16.2 engine mods are incompatible. The official SDK also calls its asset utility `maybe`; the command-line tool is not itself a runtime extension host.

The installed game remains script/data driven in several observable ways:

- Official documentation points to GML source under `assets/gml/scripts`, including `assets/gml/scripts/Debug/BuggerInitialize.gml`.
- Gameplay definitions are held in TOML under `assets/fiddle`.
- Assets use `.meta.toml` descriptors and stable 64-bit asset IDs.
- The asset runtime includes an `AssetDatabase` dependency loader.
- Animation assets eventually become `mwe::Sprite` values.
- Audio uses FMOD banks.

This does **not** mean the new game is still a GameMaker runner or that old `data.win` techniques apply. It means NPC Studio's new engine consumes GML-like scripts and familiar project data as part of its own runtime. Old GameMaker binary layouts, object indices, runner APIs, `data.win`, UndertaleModTool patches, and old injection offsets are historical and must be treated as incompatible until independently proved against the current executable.

Evidence:

- [`maybe.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/maybe.md)
- [`bugger.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/bugger.md)
- [`meta-files.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/meta-files.md)
- [`animations.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/animations.md)

Confidence: **high** for the listed files/names and new-engine incompatibility; **unknown** for how GML is compiled or interpreted internally because the public SDK does not document that lifecycle.

## Public SDK contents

The SDK README accurately describes the repository as a **grouping of files describing methods of editing and modifying the game**. It is not a source SDK. The audited tree contains:

- Markdown documentation.
- `tools/windows/maybe.exe` and `tools/linux/maybe`.
- `tools/windows/vaultc.exe` and `tools/linux/vaultc`.
- Asset property documentation for animations, texture atlases, shapes, fonts, audio banks, audio strings, and magic enums.

It contains no:

- C/C++/Rust headers or libraries for linking a mod.
- Plugin interface, registration function, manifest format, or mod loader.
- Public engine source.
- Runtime reflection or IPC package.
- Render/window/input extension API.
- Documented live state events or action calls.
- Build system for compiling GML or assembling a complete mod.
- Package installer, conflict resolver, asset overlay order, or rollback system.

This absence matters: a complete dual-screen implementation cannot be built solely from a documented supported runtime interface today.

Confidence: **high**, based on the complete audited repository tree.

## Supported data and asset seams

### FIDDLE gameplay definitions

The current engine can consume native FIDDLE edits. Officially documented locations include:

- `assets/fiddle/object_prototypes/*` for grid objects such as furniture, crops, trees, coops, barns, and grass.
- `assets/fiddle/items/other/*.toml` for mod-added items.
- `assets/fiddle/locations.toml` as the documented `LocationId` generation example.
- `assets/etc/magic_enums.meta.toml` for the current magic-enum list.

Object and item headers use `snake_case` names that participate in serialization. Object category is inferred from the containing prototype file. Items do not have a single explicit category; behavior is inferred into an `item_use` value from item properties at startup. Missing object IDs are ignored; missing item IDs become `unknown_item`, and a later save can permanently replace the old item ID. This makes removal/rollback correctness critical.

Useful static item fields shown in the official examples include:

- `icon_sprite`, `mini_sprite`, `sow_sprite`
- `name`, `description`, `tags`
- `value` (`bin`, `store` examples)
- `restore`
- `object`, `crop_object`
- `recipe`, `crafting_level_requirement`

This seam is suitable for building a **static catalog** of item/object names, icons, descriptions, values, recipes, tags, and location definitions from the user's installed copy. It is not a live inventory or action interface.

Evidence: [`create-objects-items.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/create-objects-items.md).

Confidence: **high for v0.16.4**, **unverified for installed v1.0.x field schemas**.

### Asset identity and dependency model

All documented assets use `.meta.toml` files with:

- `[meta_properties]`
- required 64-bit hexadecimal `id`
- required `asset_kind`
- optional `required_assets`
- `[asset_properties]`, whose fields depend on the asset kind

The `id`, not the path, is asset identity. Assets can move or be renamed without breaking ID-based links. `AssetDatabase` requires dependencies to load first; failing to declare a referenced asset in `required_assets` can crash because ordinary load order is random.

Documented `asset_kind` strings:

- `Animation`
- `TextureAtlas`
- `AudioBank`
- `AudioStrings`
- `Font`
- `Shape`
- `MagicEnums`

The docs explicitly say other specialized/singleton kinds exist but are undocumented. Unknown kinds fail to load and can cause startup crashes.

Evidence: [`meta-files.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/meta-files.md).

Confidence: **high for the documented model**, **low for undocumented kinds**.

### Visual assets useful to the bottom UI

`Animation` is the engine's sprite-style asset and ultimately feeds an `mwe::Sprite`. A same-stem PNG is mandatory. Important documented properties are:

- `frame_size` (required)
- `frame_len`, `dimensions`
- `filter_kind` (`Nearest` / `NearestNeighbor` or `Linear`)
- `mipmap_filter_kind`
- `wrap` (`Clamp`, `Repeat`, `Mirror`)
- `srgb`
- scalar or per-frame `duration`
- horizontal/vertical `offset`
- `atlas`, `tags`

`TextureAtlas` is optional but improves startup and batching. It uses a same-stem PNG and supports four- or eight-value `placement` arrays in the v0.16.4 schema; `maybe atlas create` should generate it rather than hand-authoring it.

`Font` uses a same-stem TTF or OTF and supports `minimum_expected_size`, `renders_at_multiples_only`, and `allow_shading`. This is a viable way to reproduce the installed game's pixel-font behavior without bundling the proprietary font, provided the second-screen renderer reads only the user's installed asset.

`Shape` supports `box`, `circle`, and `empty`; the docs state shapes are used for UI mouse clicking and centering as well as physics and other gameplay systems. This confirms the engine has shape-based UI hit regions, but publishes no input event API.

Evidence:

- [`animations.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/animations.md)
- [`texture-atlases.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/texture-atlases.md)
- [`fonts.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/fonts.md)
- [`shapes.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/shapes.md)

Confidence: **high for static rendering data**, **none yet for a second-window renderer seam**.

### Audio

The engine documents `AudioBank` and `AudioStrings` as FMOD Studio bank assets with same-stem `.bank` / `.strings.bank` files and a `load_sync` option. This is not relevant to core state extraction, but it identifies FMOD as the current audio path and cautions against treating an FMOD station/event as an engine-level state API.

Evidence: [`audio-bank.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/audio-bank.md) and [`audio-strings.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/audio-strings.md).

## `maybe` utility: exact capabilities and limits

Official binary: `maybe 0.8.0`.

Commands:

- `maybe new <PATH>` (aliases `add`, `a`, `n`) creates metadata for supported simple binary assets; directories are recursive and `--force` controls overwriting.
- `maybe atlas create <ATLAS_TO_MAKE>` builds atlas metadata/images from explicit animation paths or a tag; relevant options include `--opaque-deduplicator`, `--padding`, `--animation-paths`, and `--tag`.
- `maybe atlas list` enumerates atlas tagging with `--filter`, `--untagged`, `--paths`, and `--machine`.
- `maybe atlas set` changes animation atlas tags using `--tag` or `--untag`, `--paths`, and `--verbose`.
- `maybe asset-id` emits a new random `AssetId`.

It does not expose commands for launching or attaching to the game, inspecting live objects, subscribing to events, calling GML functions, rendering a window, routing input, packaging mods, or compiling scripts.

Evidence: [`maybe.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/maybe.md) plus direct execution of the pinned official binary.

Confidence: **high**.

## Magic enums and stable identifiers

The `MagicEnums` asset provides two useful mechanisms:

1. Enum/string conversion functions generated by the runtime:
   - `try_<enum>_to_string` -> `Option<String>`
   - `<enum>_to_string` -> string or throw
   - `try_string_to_<enum>` -> `Option<Enum>`
   - `string_to_<enum>` -> enum or throw
2. Enum generation from TOML table names under `assets/fiddle`.

Concrete documented examples:

- `try_string_to_object_id("worn_table")`
- `string_to_object_id("worn_table")`
- `format("This is a {ObjectId}", ObjectId.WornTable)`
- generated `LocationId` entries from `assets/fiddle/locations.toml`

The generator sorts table headers alphabetically, omits a `[default]` table, and adds `LEN`. This means numerical enum values can shift when definitions are added or removed. **The bridge protocol must use snake-case string IDs or stable 64-bit asset IDs, never persist generated numeric enum ordinals across versions.**

Evidence: [`magic-enums.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/asset-properties/magic-enums.md).

Confidence: **high**.

## Game CLI, logging, build detection, and tests

The official v0.16.4 CLI documentation lists:

- `FieldsOfMistria --help`
- `FieldsOfMistria sha`
- `FieldsOfMistria test [TESTS_TO_RUN]...`
- `--debug-tools`
- `--debug-assertions`
- `--auto-start=off|continue|new`
- `--fps <FPS>`
- `--all-unlocks`
- `--story-events`

The docs call terminal execution the ideal way to test mods because stdout includes stack traces, debug messages, and warnings beyond the crash dialog. On Windows, the documented PowerShell form is:

```powershell
./FieldsOfMistria --help | Write-Host
```

The `sha` subcommand is the best official build-identity seam: it returns the source Git SHA known when the game was built or returns an error if unavailable. The current-device reconnaissance should record, independently:

1. stdout/stderr and exit code of `FieldsOfMistria sha`;
2. stdout/stderr of the installed executable's `--help` (do not assume the v0.16.4 option list);
3. SHA-256 and size of the exact executable;
4. PE product/file version if present;
5. platform/architecture and imported renderer/window libraries;
6. the installed `assets` tree manifest/hash.

The official built-in test groups include `standard`, `all`, `general_cutscenes`, `shooting_star`, `main_story`, `dragon_story`, `festivals`, and `dateable_progression`. `dateable_progression` accepts `--npc-test-target` and `test` accepts `--seasons`. These are diagnostic tools for an isolated test copy/save, not acceptance tests for the dual-screen bridge. Some tests require `--all-unlocks`, which changes new-game behavior and must not be enabled for normal play.

Evidence: [`cli.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/cli.md).

Confidence: **high for v0.16.4**, **must be re-read from the installed v1.0.x binary**.

## BUGGER: useful reconnaissance, unsuitable production API

`BUGGER` is the engine's in-game terminal. It requires `--debug-tools=true` and is toggled with `F2`. The SDK explicitly warns that its commands are unstable, can destabilize the game, receive no stability promise, and receive zero testing beyond developer use.

Documented command examples:

- `goto <LOCATION_ID | NPC_ID>`
- `noclip`
- `add_item <ITEM_ID> [COUNT]`
- `new_day [NUMBER_OF_DAYS]`
- `manana [NUMBER_OF_DAYS]`

The authoritative command registration for the installed build is identified as `assets/gml/scripts/Debug/BuggerInitialize.gml`, and `help` lists current commands at runtime.

Correct use for this project:

- Use BUGGER only on disposable validation saves to drive repeatable state changes.
- Inspect `BuggerInitialize.gml` to find current, real game action functions and their preconditions.
- Trace those functions back to ordinary gameplay callers before invoking them from a bridge.
- Never ship a production dependency on BUGGER, `--debug-tools`, `add_item`, teleportation, or day-skipping.

Evidence: [`bugger.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/bugger.md).

Confidence: **high**.

## `vaultc`: save reconnaissance, not live synchronization

Official binary: `vaultc 0.16.4`. It handles the `.sav` container used since v0.11.5:

- `vaultc unpack <SAV_FILEPATH> <OUTPUT_FILEPATH>` -> JSON plus farm buffer directory.
- `vaultc pack <INPUT_FILEPATH> <OUTPUT_FILEPATH>` -> `.sav`.
- `vaultc view <SAV_FILEPATH> <KEY>` -> a value addressed by pseudo-file/path.
- `vaultc edit <SAV_FILEPATH> <KEY> <JSON_VALUE>` -> mutation.

The official key examples include `player`, `player/spells_learned`, and `player/misc/foo`. A key path can address nested object fields but cannot address a single array element. `edit` may create a missing nested field but normally cannot create a missing top-level pseudo-file.

Correct use:

- Unpack copies of saves before and after controlled gameplay changes to discover persisted state names and serialization semantics.
- Use `view` for targeted, read-only confirmation.
- Never poll `.sav` files as the live bridge; saves are stale between writes, disk-heavy, and do not cover transient state.
- Do not run `edit` on a player's active save. It is not an action API and bypasses gameplay validation/events.
- The v0.16.4 tool must first be tested against a copied v1.0.x save, with byte-for-byte preservation checks, before relying on it even for reconnaissance.

Evidence: [`vaultc.md`](https://github.com/NPC-Studio/mistria-sdk/blob/c7a92ffdbc6f8088c6c273ba6af2250931c3d208/vaultc.md).

Confidence: **high for documented behavior**, **compatibility with v1.0.x unverified**.

## Live state and action coverage

The public SDK exposes no documented live query or mutation interface for the requested systems.

| Domain | Static/persisted lead | Published live event/query/action API | Required next proof |
|---|---|---|---|
| Player HP/stamina/mana/name/position | Save schema may contain some values; no names documented | None | Trace current GML player object/state owner and ordinary HUD reads |
| Date/time/season/weather | FIDDLE/script data likely exists; no schema in SDK | None | Trace current HUD/calendar/weather scripts and day-transition callbacks |
| Tesserae / Town Renown | Persisted state likely; no SDK fields | None | Trace display code and award/spend/update call paths |
| Inventory/hotbar/equipment | Static item definitions and runtime-derived `item_use` | None | Trace actual inventory container, stack mutation, selection, equip/use/drop calls and change notifications |
| Quests/objectives | No documented schema | None | Trace Journal render source plus accept/progress/complete/track state transitions |
| Relationships/NPC positions | `goto <NPC_ID>` proves an NPC ID resolver exists in debug code | None | Trace relationship UI and live NPC/location registry; preserve discovery rules |
| Animals | Object/FIDDLE definitions only | None | Trace ranching UI and animal state owner/events |
| Skills/perks | No documented runtime schema | None | Trace Skills/Dragonshrine UI reads and purchase precondition/action path |
| Magic | `player/spells_learned` is a documented save-key example | None | Trace mana/spell selection/casting path and cooldown/preconditions |
| Map | `LocationId`, `assets/fiddle/locations.toml` example | None | Trace current map UI transform, current location, actor positions, unlock/visibility state |
| Almanac/museum | Static definitions may identify entries | None | Trace discovery/donation state and UI filtering; never infer unlocked state from database presence |
| Crafting/cooking | Static `recipe` and requirement examples | None | Trace live material aggregation (including eligible chests) and ordinary craft transaction |
| Rendering/second window | Animation/font/atlas schemas | None | Determine renderer/window API from installed binary and establish an independent owned surface |
| Input routing | Shape assets are used for UI hit areas | None | Trace current input dispatcher and prove bottom events are consumed before gameplay |

Confidence: **high** that these APIs are absent from the public SDK. The locations of the eventual state owners/functions are **unknown until installed-build inspection**.

## Recommended integration boundary

The evidence supports a three-part architecture, but only the first part is currently documented:

1. **Installed-asset catalog (supported/static):** parse the user's installed FIDDLE/meta data to build names, icons, recipes, asset references, and location definitions. Cache by installed-build fingerprint. Never ship extracted proprietary assets.
2. **In-process live bridge (not yet supported by a public API):** obtain live state and perform actions on the game thread using the least invasive current-build seam. Prefer an additive current-engine GML initialization mechanism if the installed game proves one exists. Otherwise use narrowly scoped, signature-gated native interop. The bridge emits small versioned deltas and receives validated commands.
3. **Independent second-screen client:** owns bottom rendering and touch/controller focus, consumes deltas, and sends explicit commands. The public SDK provides no second-window or IPC implementation.

Do not decide between a GML bridge and a native hook until the installed v1.0.x file tree proves:

- whether loose/additive `.gml` scripts are discovered automatically;
- whether there is an official mod folder or manifest/load order;
- whether initialization hooks exist without replacing a base script;
- whether current GML exposes file/socket/FFI functions suitable for IPC;
- whether renderer/window/input services can create an independent surface;
- whether action calls can be marshalled safely to the game thread.

A base-game asset/script replacement is not a maintainable default. If unavoidable, the installer must verify exact original hashes, back up only files it owns, install transactionally, and refuse unknown builds.

## Immediate installed-build reconnaissance checklist

These are read-only tasks for the parent investigation; this SDK audit made no device changes.

1. Locate the exact `FieldsOfMistria` executable and record `sha`, SHA-256, size, PE version, and architecture.
2. Capture installed `--help`; compare every flag/subcommand with the v0.16.4 SDK document.
3. Inventory the top-level runtime and `assets` tree. Specifically confirm:
   - `assets/gml/scripts/Debug/BuggerInitialize.gml`
   - `assets/fiddle`
   - `assets/fiddle/items`
   - `assets/fiddle/object_prototypes`
   - `assets/fiddle/locations.toml`
   - `assets/etc/magic_enums.meta.toml`
   - all `.meta.toml`, atlas, font, localization, map, and UI assets
4. Search GML for ordinary UI readers and mutation call paths for every state domain. Record exact function/object/event names and callers; do not assume old GameMaker names.
5. Determine script compilation/loading and whether an additive mod initialization seam exists.
6. Identify renderer and window imports/runtime libraries, then prove whether the engine supports multiple windows/surfaces.
7. Identify input ownership and the earliest safe consumption point for second-display touch.
8. Copy—not edit—a save and test current `vaultc unpack`; compare repacked output in an isolated temporary directory only.
9. Use current logs and `--debug-assertions=true` on a disposable test profile when runtime experiments begin.

## Version-resilience rules established by this audit

- Gate by executable SHA-256 and, when available, `FieldsOfMistria sha`; human-readable version alone is insufficient.
- Treat v0.16.4 SDK docs as a schema lead, not a v1.0.4 compatibility contract.
- Use string IDs / 64-bit asset IDs over generated numeric enum values.
- Keep current-build adapters separate from UI/domain models.
- Never invoke an address/signature on an unknown build.
- Validate action preconditions on the game thread and degrade to read-only when an action adapter is unavailable.
- Cache static assets/catalog data by build fingerprint; send event/diff updates for dynamic state.
- Keep save parsing out of the live synchronization loop.
- Never make BUGGER or debug flags a production dependency.
- Keep old GameMaker/`data.win`/UndertaleModTool techniques explicitly quarantined as pre-v0.16.2 historical material.

## Confirmed unknowns

The official material does not answer the following, so any current assertion would be speculation:

- Current v1.0.4 script loader and compilation model.
- Official mod directory, manifest, dependency resolution, or load order.
- Runtime object/class layouts, native vtables, or stable exported interfaces.
- GML event/callback names for inventory, quests, relationships, animals, skills, magic, map, or day transitions.
- Live player/inventory/quest/relationship/animal/map data structure names.
- Thread-affinity rules for state reads and gameplay actions.
- Renderer backend and multiple-window/surface support.
- Input dispatcher and controller focus APIs.
- IPC/socket/FFI facilities callable from current GML.
- Current v1.0.x save-schema compatibility with `vaultc 0.16.4`.
- Current v1.0.x compatibility of `maybe 0.8.0` output.

Those questions require inspection and controlled tests against the user's exact installed game. They should not be filled with names or behavior borrowed from the pre-migration GameMaker build.
