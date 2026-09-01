# Fields of Mistria device reconnaissance

Last verified: 2026-08-30
Scope: read-only inspection of the installed Windows build and its current WEMU host environment. No game file was changed, no complete proprietary binary or asset was copied off the device, and the game was not launched. Only metadata, small PE/resource ranges, and narrowly selected script lines needed to identify interoperability surfaces were streamed for analysis.

## Executive result

The complete runnable installation is **not** in the Downloads folder named by the user. That Downloads folder is an incomplete 47 MB redistributable shell. The complete game is installed at:

```text
/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4
```

The exact executable is:

```text
/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4/FieldsOfMistria.exe
```

This is Fields of Mistria **1.0.4**, an x86-64 Rust application using NPC Studio's current `Maybe` engine, its own GML VM, SDL 3/OpenGL rendering, and FMOD Studio audio. It is not the historical GameMaker runner layout: there is no `data.win`, `game.unx`, YoYo runner, or equivalent monolithic GameMaker data file in the installed build.

## Device and ADB identity

All successful observations were made through `100.83.63.70:5555`.

| Item | Verified value |
| --- | --- |
| Android serial | `bd88415a` |
| Model | `AYN Thor` |
| Device/product | `kalama` |
| Build fingerprint | `qti/kalama/kalama:13/TKQ1.231222.001/eng.Thor.20260206.163241:user/release-keys` |
| Working ADB endpoints during audit | `100.83.63.70:5555`, `100.83.63.70:33485` |
| Refused endpoint during audit | `100.83.63.70:36127` |

Both working endpoints reported the same serial, model, fingerprint, display size, and density, so they are two transports to the same physical device.

### Physical displays

`dumpsys display` reported two enabled internal panels:

| Display | Native modes | Current logical landscape override | Touch |
| --- | --- | --- | --- |
| Built-in Screen, display 0 | 1080x1920 at 60/120 Hz | real 1920x1080; app 1920x1025 | internal |
| Screen-2, display 4 | 1080x1240 at 60/120 Hz | real 1240x1080; app 1240x1025 | external; presentation-capable |

Both report density 369. Display 4 has `FLAG_PRESENTATION`; it is the correct Android-side target for the private lower-panel UI.

## Downloads discrepancy

The path found under Downloads is:

```text
/storage/emulated/0/Download/Fields.of.Mistria.v1.0.4/Fields.of.Mistria.v1.0.4
```

It contains only two small instruction/link files and `_Redist` installers (DirectX, VC runtimes, .NET, OpenAL, and XNA). Its total size is approximately 47 MB. It has no `FieldsOfMistria.exe` and no game data. It must not be selected as an install or deployment target.

The complete 628 MB installation was found by a read-only search outside Downloads at `/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4`.

## Exact installed files

All SHA-256 values were calculated on-device. Device mtimes are shown in the device's `-04:00` timezone.

| Relative path | Bytes | Device mtime | SHA-256 |
| --- | ---: | --- | --- |
| `FieldsOfMistria.exe` | 13,657,696 | 2026-08-19 20:39:00 | `aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e` |
| `Maybe.toml` | 514 | 2026-08-19 20:37:32 | `96cd2cb00de88a9657a38776425adbb53d0fce2fc56d1d86764bef69a22e4005` |
| `SDL3.dll` | 2,811,488 | 2026-08-19 20:38:57 | `532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157` |
| `assets.zip` | 625,619,136 | 2026-08-19 20:38:57 | `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450` |
| `fmod.dll` | 1,888,864 | 2026-08-19 20:38:58 | `76dee791122272d50cc92899490da44994e4eb62ee36bd32e21f2fd9a7251d77` |
| `fmodstudio.dll` | 1,588,832 | 2026-08-19 20:38:57 | `8f18508f2135c1525b4f5958cb54c7cfcc924ff00002c012c4249f5fd8dd956c` |
| `open-source-licenses.html` | 398,814 | 2026-08-19 20:38:54 | `eabec654aca84f70dc0d2e297306c6435c7e6dbc268cfbd36065fac258611c34` |
| `steam_api64.dll` | 11,429,288 | 2026-07-21 02:37:08 | `8b1bd0bea955aaeccd6de92d2a8c6208757dde038fd3c074113f0c2609e04de1` |
| `steam_settings/steam_appid.txt` | 7 | 2026-08-20 19:33:52 | `cecd7b6cd1fb8b99387269a40d097cece1ff1d967d35ac286279e5b7ea628279` |
| `steam_settings/steam_interfaces.txt` | 763 | 2026-08-20 19:33:52 | `9ab8d94af0c1403709acc3ebb88eab8bc2d7b264758388dab778afffd0017289` |

All files are owned by Android's media group and reported mode `0660` on shared storage. There are ten files and two directories total. There is no installed mod loader, `mods` directory, SDK binary, PDB, or common proxy DLL such as `dxgi.dll`, `dinput8.dll`, or `winmm.dll` in the game directory.

`steam_appid.txt` contains `4317790`. The installed `steam_api64.dll` identifies itself through version resources as **GSE Client API**, not Valve's stock library. It must not be used as a mod injection dependency or altered as part of this work; doing so would create unnecessary DRM and compatibility risk.

## Exact executable identity

The executable's Windows version resource reports:

| Field | Value |
| --- | --- |
| Product name | `Fields of Mistria` |
| File description | `mistria` |
| File version | `1.0.4` |
| Product version | `1.0.4` |
| Copyright | `© 2026 NPC Studio` |

Independent evidence for 1.0.4 exists in the install directory name and an embedded application version string beside the current headless/test-suite implementation. The executable also contains the short string `e0c5fee` in `mistria/src/headless.rs` immediately beside the application version and the implementation that exposes a build Git SHA. It is therefore a strong candidate for the application build revision, but it has not been executed through the CLI and should be treated as a **probable embedded Git revision**, not a signed identity. The executable SHA-256 remains the authoritative build gate.

### PE metadata

Only the PE headers and exact version resource were read for metadata; the executable was not copied from the device.

| Field | Value |
| --- | --- |
| Format | PE32+ GUI x86-64 |
| Machine | `0x8664` |
| COFF timestamp | `2026-08-19T15:08:07Z` |
| Sections | 6 |
| Image base | `0x140000000` |
| Entry RVA | `0x9EFF0C` |
| Size of image | 13,664,256 bytes |
| Import RVA/size | `0xC81854`, 540 bytes |
| Subsystem | Windows GUI (`2`) |
| DLL characteristics | `0x8160` (high-entropy VA, dynamic base/ASLR, NX compatible, terminal-server aware) |

Sections:

| Section | RVA | Virtual bytes | File offset | Raw bytes |
| --- | ---: | ---: | ---: | ---: |
| `.text` | `0x1000` | 10,814,617 | `0x400` | 10,814,976 |
| `.rdata` | `0xA52000` | 2,303,208 | `0xA50A00` | 2,303,488 |
| `.data` | `0xC85000` | 14,600 | `0xC83000` | 12,800 |
| `.pdata` | `0xC89000` | 184,020 | `0xC86200` | 184,320 |
| `.rsrc` | `0xCB6000` | 271,152 | `0xCB3200` | 271,360 |
| `.reloc` | `0xCF9000` | 58,964 | `0xCF5600` | 59,392 |

## Runtime libraries and imports

### Shipped DLL versions

| File | Version-resource identity | COFF timestamp UTC |
| --- | --- | --- |
| `SDL3.dll` | Simple DirectMedia Layer 3.4.8.0 | 2026-05-02 00:48:42 |
| `fmod.dll` | FMOD 2.3.10, build 157581 | 2025-10-22 10:12:49 |
| `fmodstudio.dll` | FMOD Studio Runtime 2.3.10, build 157581 | 2025-10-22 10:16:59 |
| `steam_api64.dll` | GSE Client API file 08.33.09.23 | 2026-07-19 19:16:28 |

All are PE32+ x86-64 DLLs.

### Exact imported modules

The PE import table was parsed directly from small on-device byte ranges. The executable imports:

| Module | Imported symbols |
| --- | ---: |
| `SDL3.dll` | 61 |
| `fmodstudio.dll` | 42 |
| `steam_api64.dll` | 26 |
| `kernel32.dll` plus `KERNEL32.dll` | 36 + 65 |
| `user32.dll` | 1 |
| `shell32.dll` | 1 |
| `winmm.dll` | 3 |
| `WS2_32.dll` | 15 |
| `advapi32.dll` plus `ADVAPI32.dll` | 3 + 2 |
| `bcrypt.dll`, `bcryptprimitives.dll` | 1 each |
| `combase.dll` | 1 |
| `ntdll.dll` | 5 |
| `PSAPI.DLL` | 3 |
| `USERENV.dll` | 1 |
| `VCRUNTIME140.dll` | 8 |
| Windows CRT API sets | 55 total |

Important exact calls include:

- SDL lifecycle/window/rendering: `SDL_Init`, `SDL_CreateWindowWithProperties`, `SDL_GL_CreateContext`, `SDL_GL_GetProcAddress`, `SDL_GL_SwapWindow`, `SDL_GetWindowSizeInPixels`, `SDL_SetWindowFullscreen`, and `SDL_DestroyWindow`.
- SDL input: `SDL_PollEvent`, gamepad open/name/GUID/rumble functions, keyboard/mouse focus, cursor, clipboard, and text-input functions.
- FMOD Studio: system creation/initialization/update, bank loading, bus state/volume, event creation/start/stop/release, listener attributes, and timeline/parameter functions.
- Win32: `SHGetKnownFolderPath`, `GetSystemMetrics`, and `timeGetDevCaps`/`timeBeginPeriod`/`timeEndPeriod`.
- Steam: language, screenshots, achievements, controller text input, manual dispatch, and runtime state.

There are no static Direct3D, DXGI, or Vulkan imports. A `dxgi.dll` proxy will not load naturally for this executable. A local `winmm.dll` proxy is only a possible future bootstrap experiment because the game imports three winmm functions; native-over-builtin selection under the exact Proton/FEX configuration still requires proof. The shipped `SDL3.dll` is a strong render/input boundary but cannot simply be overwritten: any SDL proxy would need safe chain-loading of the user's exact original and complete forwarding for every used export.

## Current engine, renderer, and VM

Static binary evidence is explicit:

- Rust/Cargo build paths are embedded throughout the executable.
- The application crate is `mistria`.
- NPC Studio's `Maybe` engine checkout appears with revision prefix `8a1edbd`.
- Maybe components include `mwe` (engine/components/timing/text), `mwa` (assets/database/ZIP loader/project settings), and `mwr` (OpenGL renderer/framebuffers/shaders/textures/ring buffers).
- The embedded script VM/compiler uses a `fabricator` checkout with revision prefix `f2483a6`.
- Other current native modules include `t2` (conversations, requirements, schedules, world facts), `mistvault` (save vaults), and `hyperpath` (pathfinding).
- Renderer strings include GLSL `#version 330 core` and `#version 300 es`; the executable uses SDL to create the GL context and `glow` to resolve OpenGL functions.
- Audio is FMOD Studio, backed by the twelve installed `.bank` resources.

This architecture matters for the mod:

1. Historical GameMaker offsets and `data.win` tooling are invalid for this build.
2. Current source-form `.gml` is loaded by the Rust VM and is a real interoperability surface.
3. The main renderer is OpenGL/SDL, so a second-screen implementation must not assume DXGI present hooks.
4. The archive loader and executable contain asset/save checksum and tamper-state code. Any future additive script experiment must be hash-gated, staged, reversible, and proven against a copied archive before device mutation.

## `Maybe.toml`

The installed project configuration identifies:

- name: `Fields of Mistria`
- executable name: `FieldsOfMistria`
- clear color: opaque black
- texture filtering: `NearestNeighbor`
- release include: `open-source-licenses.html`
- macOS identifier: `com.npc-studio.fields-of-mistria`

No external asset directory is configured in this file, so the adjacent `assets.zip` is the release asset source selected by the current archive loader.

## Asset archive inventory

`assets.zip` is a valid ZIP containing **122,403 entries** and 593,827,810 uncompressed bytes. Every inspected entry is stored without compression; the installed ZIP is larger than the payload because it carries central/local metadata for over 122,000 entries. All archive timestamps are normalized to 1980-01-01 and must not be used for versioning.

### Major trees

| Tree | Entries, including directories |
| --- | ---: |
| `assets/animations` | 79,481 |
| `assets/shapes` | 35,320 |
| `assets/t2` | 4,648 |
| `assets/tiled` | 1,495 |
| `assets/gml` | 632 |
| `assets/mist_scripts` | 406 |
| `assets/fiddle` | 317 |
| `assets/audio` | 25 |
| `assets/atlases` | 23 |
| `assets/data_files` | 22 |
| `assets/localization` | 18 |
| `assets/fonts` | 13 |

### File formats

| Extension | Files |
| --- | ---: |
| `.toml` | 77,801 |
| `.png` | 38,941 |
| `.tsx` | 612 |
| `.gml` | 567 |
| `.tmx` | 222 |
| `.mist` | 196 |
| `.tx` | 125 |
| `.bank` | 12 |
| `.json` | 8 |
| `.ttf` | 6 |
| `.bin` | 1 |

### Data and system locations

Important current paths inside the archive include:

- Player/runtime: `assets/gml/scripts/GameplaySystems/Player/Ari.gml`, `Player/StatusEffect.gml`, `Player/StatusEffectManager.gml`, and `assets/gml/objects/characters/obj_ari.gml`.
- Inventory/items: `GameplaySystems/Inventory/Inventory.gml`, `GameplaySystems/Items/Items.gml`, `LiveItem.gml`, `use_item.gml`, and 98 FIDDLE item entries/directories below `assets/fiddle/items`.
- Equipment/data: `assets/fiddle/items/other/armor.toml`, `blacksmithing.toml`, `cooked_dishes.toml`, `materials.toml`, `ranching.toml`, and related category files.
- Quests: `GameplaySystems/Quests/QuestDatabase.gml`, `QuestLog.gml`, `QuestUtils.gml`, plus ten concrete quest TOMLs under `assets/fiddle/quests` (story, fetch, heart, crown, request-board, Stillwell, and Tali registries/challenges).
- Map/location: `UI/Anchor/Menus/MapMenu.gml`, `GameplaySystems/Locations/Location.gml`, `LocationPosition.gml`, `assets/fiddle/locations.toml`, and Tiled room/tile data under `assets/tiled`.
- NPCs/relationships: `GameplaySystems/NPCs/Npc.gml`, `NpcDatabase.gml`, schedules/routines, `RelationshipsMenu.gml`, 35 NPC FIDDLE entries/directories, seasonal portrait atlases, and `assets/t2/Schedules`.
- Animals/ranching: `GameplaySystems/Ranching/Animal.gml`, `AnimalPrototypes.gml`, `AnimalUtils.gml`, `AnimalFsm.gml`, `AnimalMenu.gml`, and per-species definitions under `assets/fiddle/ranching/animals`.
- Skills/perks: `assets/fiddle/skills.toml`, `perks.toml`, and `assets/fiddle/ui/skill_menu/{archaeology,blacksmithing,combat,cooking,farming,fishing,mining,mount,ranching,woodcrafting}.toml`.
- Magic: `assets/gml/scripts/Spells.gml`, `assets/fiddle/spells.toml`, and spell-learned Mist events.
- Crafting/cooking: `GameplaySystems/Recipe.gml`, crafting station objects, `assets/fiddle/ui/crafting/{blacksmithing,cooking,dragon_forging,milling,refining,woodcrafting}.toml`, plus cooked-dish item data.
- Calendar/time/weather: `GameplaySystems/TimeSeasons/Calendar.gml`, `Clock.gml`, `GameplaySystems/Weather.gml`, and corresponding FIDDLE data.
- Saves/settings: `Serialization/SaveGame.gml`, `Serialization/LoadState.gml`, `Serialization/Serde/{SaveManager,Saver,Loader}.gml`, and `Serialization/Settings.gml`.
- Localization: metadata/source caches/translations for French, Japanese, Korean, Russian, Spanish, Simplified Chinese, and Traditional Chinese; English is the source language.
- Data files: animation outlines/parts/tools/shadows, room expansion data, and starting-farm JSON/binary terrain data under `assets/data_files`.

### UI and art organization

The current UI is not one opaque atlas. The archive contains an explicit `UIAtlas.png` plus thousands of individual UI animation/shape resources and `.meta.toml` records. `assets/animations/UI NEW` is organized into Journal (805 entries), Generic (1,052), Crafting (508), Skills (425), Inventory and Storage (258), Calendar (178), HUD (157), Ranching (155), Store Menu (105), Dialogue (87), Pets, Cursor, Load Save, Quest Board, Main Menu, and other page-specific groups. Corresponding polygon/shape metadata lives under `assets/shapes/UI NEW`.

Fonts are installed as six TTFs with matching metadata: Cubic 11, Mistria Birdseed, Nosutaru, Pixel M Plus, Silver, and Zpix. Any released mod package must load art/fonts from the user's archive at runtime or into an owned cache; it must not redistribute them.

Portraits are split across spring, summer, autumn, winter, and miscellaneous atlases. Map art is exposed through `spr_ui_map_*` assets, while actual rooms and collision/object metadata are stored as Tiled TMX/TSX data (including the farm, town/region interiors, and dungeon biomes).

## Confirmed script-side state anchors

Targeted, minimal inspection of current v1.0.4 scripts found these concrete owners:

- Save serialization reads the live player from `ARI`: `ARI.inventory.serialize()`, `ARI.armor.serialize()`, `ARI.serialize_stats()`, `ARI.perks`, `ARI.spells_learned`, item/recipe unlock collections, `ARI.quest_artifacts`, and `ARI.save_position`.
- `Inventory.gml` defines `Inventory`, `InventorySlot`, and `InventorySubscriber`. Slots carry update counters and `InventorySubscriber` maintains per-slot timestamps, which is a current event-like dirty signal for incremental synchronization.
- `QuestLog.gml` owns `QUEST_LOG.active`, completed quest keys, completion timestamps, and real `start`, `complete`, and active quest progress paths. Quest transitions also write T2 world facts such as `quest_<name>_in_progress` and `quest_<name>_complete`.
- `MapMenu.gml` owns `MAP_HUBS`, maps a location through `LOCATIONS[location_id].map_location`, resolves `spr_ui_map_<LocationId>`, and assigns player/pet/child/NPC residents to map hubs.
- Saves serialize separate logical files named `header`, `gamedata`, `player`, `npcs`, `quests`, `game_stats`, and `date_photos` into a `RustSaver`/vault container.

These identifiers are current-build evidence. They are preferable to blind native memory scanning, but runtime behavior still must be proved in a running save before any field is declared integrated.

## Saves, settings, and logs

No Fields of Mistria save, config, or log directory currently exists in any inspected WEMU Wine prefix. No Fields of Mistria process was running during this audit, and no Mistria entry was found in WEMU's `game_library.xml` or `wemu_library.xml`. Therefore the exact WEMU prefix assigned at first launch is not yet confirmed.

Static code confirms:

- the configuration root is exposed to GML as `CONFIG_DIRECTORY`;
- settings are `settings.json`, with transactional `settings.new.json` and `settings.old.json` siblings;
- saves are under `CONFIG_DIRECTORY/saves`;
- normal containers end in `.sav`;
- transitional/backup names include `.new.sav` and `.old.sav`;
- invalid containers are renamed to `.invalid.sav`;
- the native executable contains `error_log.json` and a `last_save_path` state key.

The executable imports `SHGetKnownFolderPath`. In the currently active WEMU prefix, Wine maps Windows Local AppData to:

```text
/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine/drive_c/users/xuser/AppData/Local
```

The likely first-launch root is a FieldsOfMistria-named child of Local AppData, consistent with the native known-folder call and current config naming. This final path is an inference, not observed fact. It must be confirmed after the user deliberately launches the game and creates/loads a save. The mod must never poll save vaults for live state; copied saves are useful only for offline schema validation.

## WEMU runtime context

WEMU reports version `0.1.0` (version code 1), updated 2026-08-30 15:48:04. Installed compatibility components include:

- FEXCore `FEXCore-2608-stable-0`
- Proton `10.0-arm64ec-0`, `11.0-1-x86_64-1`, and `11.0-5-arm64ec-6`
- Wine `9.21-x86_64-0` and `9.22-X86_64-1`
- DXVK `2.3.1-1-gplasync-0`, `3.0-1-gplasync-0`, and `3.0-1-gplasync-arm64ec-0`
- VKD3D `2.9-0`
- Turnip/adrenotools driver sets

The desktop session active during reconnaissance used:

- `WINEPREFIX=/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine`
- Proton `11.0-5-arm64ec-6`
- `HODLL=libwow64fex.dll` and FEX-related environment controls
- Zink/OpenGL over the Turnip 26.2.0 driver

This proves the current WEMU desktop environment, not the future Fields of Mistria launch profile. The target's exact prefix, Proton/FEX choice, Wine DLL overrides, and launch command remain runtime unknowns until the game is registered/launched.

## Deployment and interoperability consequences

1. Gate every native or script build profile on the exact executable, `SDL3.dll`, and `assets.zip` SHA-256 values above. The version string alone is insufficient.
2. Never target the incomplete Downloads shell. Resolve and verify the complete `/storage/emulated/0/Games/...` directory before any eventual dry run.
3. Do not use the non-stock Steam API DLL as an injection mechanism and do not alter it.
4. Do not assume DXGI/D3D. The confirmed graphics boundary is SDL3 plus OpenGL.
5. Prefer current VM state owners and dirty signals over broad native scans or save-file polling.
6. Treat `assets.zip` as proprietary user-owned content. If artwork is used, read it locally into a bounded cache and distribute only code/metadata necessary to locate it.
7. Any asset/GML patch must preserve the exact original in a verified backup, use a staging file, publish atomically, and account for the current checksum/tamper paths.
8. Install only with the game and its Wine server stopped. This audit intentionally made no install or runtime change.

## Confidence and remaining unknowns

### High confidence

- physical device identity and both displays;
- complete install path versus incomplete Downloads shell;
- exact executable version, timestamp, size, hash, PE layout, and imports;
- shipped DLL identities, sizes, versions, and hashes;
- Rust/Maybe/Fabricator/SDL3/OpenGL/FMOD architecture;
- archive entry counts, formats, and exact current data/script/UI paths;
- absence of an installed runtime mod loader/SDK;
- current save container names and live script-side owners listed above.

### Medium confidence

- embedded application Git revision `e0c5fee` (strong static placement, not CLI-executed);
- likely Local AppData FieldsOfMistria config root (known-folder and naming evidence, not created on this device yet).

### Still unknown and requiring a deliberate runtime session

- the exact WEMU launch profile/prefix selected for this title;
- the created save/config/log absolute paths;
- ASLR module bases and stable signatures in the running process;
- whether a native `winmm.dll` proxy is selected under this exact Proton/FEX setup;
- whether an additive GML/archive patch passes current archive integrity checks;
- the VM lifecycle point safest for bridge bootstrap;
- real-time values, event timing, commands, renderer stability, and lower-display input behavior.

Nothing in this document claims live integration. Those items require an instrumented run and observable state changes in a real save.
