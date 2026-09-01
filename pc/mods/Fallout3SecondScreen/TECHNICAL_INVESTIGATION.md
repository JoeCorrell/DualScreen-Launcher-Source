# Fallout 3 Dual-Screen Technical Investigation

This is the living engineering record for the Fallout 3 lower-screen mod. A
finding is not promoted to **confirmed** until it has direct source, binary, or
runtime evidence. Addresses and layouts discovered from one executable remain
version-scoped.

Last updated: 2026-08-31

## 2026-08-31 - native map-coordinate and Stats artwork correction

- The exact 1.7.0.4 `MapMenu` player-marker path at RVA `0x00545F80`
  proves that an interior world-map cursor starts from
  `PlayerCharacter+0x588` and reads the reference position through virtual
  slot `+0x1F4`. The bridge now follows that path, retains the last validated
  exterior anchor while Fallout is still publishing transition state, and
  never exports raw interior actor coordinates as world-map coordinates.
- Local-map raster cropping, door markers, and the player cursor now consume
  one immutable `MapProjection`. Pan input is converted from full-panel
  normalized coordinates into the exact raster viewport before it changes the
  projection. Unit tests prove pan preserves player-to-marker alignment and
  zoom scales all overlays around the same map origin.
- The local-map builder now snapshots eligible door references from every
  cell participating in its 5x5 native-cell mosaic. It applies MapMenu's exact
  door type and visibility predicates, obtains effective reference positions
  through the native virtual accessor, and transforms interior references into
  the same map coordinate space as the raster. The panel draws Fallout's own
  `icon_local_door.dds` and `glow_cursor.dds`; no custom player arrow remains.
- MapMenu's objective-marker producer at VA `0x009463A0` proves that the live
  target source is `PlayerCharacter::GetQuestTargetList` at VA `0x008B9170`
  and that each entry is resolved by `BGSQuestTarget::GetRef(true)` at VA
  `0x00776440`. The isolated map now samples that exact engine-owned list once
  per bounded local-map build, keeps only references belonging to the current
  local cell, and projects them through the same transform as the raster,
  doors, and player. Objective pins use the exact
  `glow_hud_compass_objective_marker.dds` rectangle recorded by Fallout's
  `InterfaceShared.tai`, sampled from `InterfaceShared0.dds`.
- Re-evaluating the installed `stats_menu.xml` showed that the condition body
  must be treated as one inherited 145% coordinate tree. Head, face, torso,
  arms, and legs now pass through one canonical floating-point transform and
  are rounded only at final raster placement. The full composite is fitted
  once into a larger art lane, so resizing cannot separate the face from its
  head or distort individual limbs.
- SPECIAL and Skills now use a fixed 256x231 family content frame derived from
  the installed 256x256 DDS canvases. Each selection is centered by its alpha
  bounds at the same scale, eliminating size/position jumps while increasing
  the visible artwork area.

## Phase status

| Phase | State | Exit condition |
|---|---|---|
| 1. Reconnaissance | Complete | Exact executable, prefix, renderer, input path, and relevant engine/UI systems mapped |
| 2. State extraction | In progress | Live player, inventory, equipment, quests, map, and radio proven during gameplay |
| 3. Second-screen foundation | In progress | Independent diagnostic panel displays live state without affecting gameplay |
| 4. Pip-Boy UI | In progress | Custom STAT/ITEMS/DATA/RADIO experience is complete and usable |
| 5. Interaction | Not started | Supported actions mutate real state safely on the game thread |
| 6. Compatibility/stability | Not started | Transition, save, combat, DLC, and long-session matrix passes |
| 7. Optimization/cleanup | Not started | Performance budgets pass and failures degrade safely |

## Confirmed target environment

### Android device

| Property | Value | Evidence | Confidence |
|---|---|---|---|
| Device | AYN Thor, product `kalama` | ADB device properties | High |
| Android | 13 / API 33 | ADB package/device properties | High |
| SoC / ABI | QCS8550 / arm64-v8a | Android build and package properties | High |
| ADB endpoint | `100.83.63.70:34023` | `adb devices -l` | High |
| WEMU package | `com.wemu`, version `0.1.0` (debug) | `dumpsys package` | High |
| WEMU app-data access | `run-as com.wemu` works | Direct read-only ADB probe | High |
| Second-screen container flag | Enabled in container 2 | `.container` extra data: `displaySecondScreen=1` | High |

Physical Android displays at reconnaissance time:

| Android display | Physical mode | Active logical orientation | Refresh | Input role |
|---|---|---|---|---|
| 0, Built-in Screen | 1080x1920 | 1920x1080 landscape | 60 Hz active; 120 Hz supported | Primary |
| 4, `Screen-2` | 1080x1240 | 1240x1080 landscape | 60 Hz active; 120 Hz supported | Presentation, external touch |

Android display pixels and guest RandR coordinates are separate coordinate
spaces. The `1280x1800` guest metadata strongly suggests a 1280x720 upper
monitor stacked over a 1280x1080 lower monitor, but this must be captured from
the live Fallout X session before the panel uses hard bounds.

### WEMU container and runtime

The Fallout prefix is:

`/data/user/0/com.wemu/files/imagefs/home/xuser-2`

Evidence:

- Container 2 is named `Games`.
- It contains
  `.wine/drive_c/users/xuser/Documents/My Games/Fallout3/FALLOUT.INI`.
- Android audit records from 2026-08-29 20:28-20:30 identify processes named
  `Fallout3.exe`, the Fallout Downloads directory, and container 2 paths.
- Those records show WEMU loading the Proton 11 ARM64EC runtime and Vulkan/DXVK
  components during the launch attempts.

Historical launch logs explicitly identify the root executable as:

`F:\Download\Fallout-3-GOTY-SteamRIP.com\Fallout 3\Fallout3.exe`

They also record one launcher attempt with `FalloutLauncherSteam.exe`. The game
is not currently running, and the most recent guest diagnostic has since been
replaced by a Skyrim session.

Relevant container 2 configuration at reconnaissance time:

| Setting | Value |
|---|---|
| Runtime | `Proton-11.0-5-arm64ec-6` |
| CPU translation | Box64 `0.4.2`; FEXCore package also configured |
| Guest desktop | `1280x1800` theme/layout metadata |
| Game viewport setting | `1280x720` |
| Display renderer | `surfaceflinger` |
| DirectX wrapper | `DXVK-3.0-1-gplasync-0` |
| Vulkan driver | Turnip `26.2.0` |
| Lower display | Enabled |
| Mapped drives | `F:` storage root; `D:` Downloads |

The precise runtime selected for a successful long game session still needs a
fresh launch proof. Recent records prove initialization attempts, not a
completed load into gameplay. Three profiles were tried:

| Profile/prefix | Runtime and translation | Renderer | Evidence assessment |
|---|---|---|---|
| `Games` / xuser-2 | Proton `11.0-5-arm64ec-6`, FEXCore 2608 PERFORMANCE, Box64 0.4.2 configured | SurfaceFlinger, Turnip 26.2.0, DXVK 3.0 GPL async | Most launch attempts; intended baseline is medium-confidence |
| `Steam · Proton 11` / xuser-1 | Proton `11.0-1-x86_64-1`, Box64 0.4.4 observed | Vulkan, FIFO, DXVK frame cap 60 | One observed Fallout attempt |
| `Deus Ez` / xuser-3 | Proton 9 ARM64EC, FEXCore INTERMEDIATE | Vulkan, older DXVK configuration | One observed Fallout attempt |

### Installed game tree and executable identity

Device game root:

`/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3`

The distribution source is not part of this repository. The mod neither uses
nor redistributes its proprietary game data. Reconnaissance copies of the two
executables were kept outside the workspace in the local OS temporary
directory solely for version and interoperability analysis.

| Candidate | Root/current executable | Bundled compatibility copy |
|---|---:|---:|
| File | `Fallout3.exe` | `_Downgrader to v1.7.0.3/Fallout3.exe` |
| Size | 16,855,040 bytes | 15,044,024 bytes |
| SHA-256 | `c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e` | `ae058c497069b8334711945cf646968b7fe8da483cce94022fc924fa0187e809` |
| File/product version | `1.7.0.4` | `1.7.0.3` |
| PE architecture | i386 / PE32 | i386 / PE32 |
| Link timestamp | 2021-05-21 19:47:34 UTC | 2009-06-23 15:15:23 UTC |
| Preferred image base | `0x00400000` | `0x00400000` |
| ASLR/NX flags | Present (`0x8140`) | Absent (`0x0000`) |
| Authenticode | Not signed | Valid signature |
| D3D import | `d3d9.dll!D3DPERF_SetOptions` | `d3d9.dll!D3DPERF_SetOptions` |
| D3DX | `d3dx9_42.dll` | `d3dx9_38.dll` |
| GFWL import | None | `xlive.dll` |

The top-level `1.7.0.4` executable is the current target because WEMU's launch
log explicitly names that path. This must be re-confirmed in a fresh
instrumented launch before any milestone is marked complete. The bundled
`1.7.0.3` executable is an analysis/compatibility reference only; the installer
must never replace the user's executable or perform a downgrade.

The root game tree contains all six official master files observed during the
probe: Fallout 3, Anchorage, The Pitt, Broken Steel, Point Lookout, and Mothership
Zeta. `Data/DLCList.txt` names exactly the five DLC ESMs. No FOSE files or
third-party ESP/DLL content were found in the top-level scan. Plugin enablement
and actual save dependencies remain unverified. Several DLC archives have odd
`.bsa.x5` siblings; their purpose must be understood before standard archive
packaging is assumed.

## Confirmed vanilla configuration clues

The prefix currently contains only `FALLOUT.INI`; a generated
`FalloutPrefs.ini`, save, or FOSE log was not present at the time of inspection.
The file matches the game/default timestamp and therefore does not prove a
successful trip through the main menu.

Pip-Boy-related settings exposed by that INI include:

- Pip-Boy RGB: `(26, 255, 128)`.
- `fScanlineScalePipboy=50`.
- Pip-Boy flicker and scanlines enabled.
- Pip-Boy brighten factor `1.3`, blur radius `3.5`, blur intensity `0.25`.
- Interface fonts include Glow Monofonto and Monofonto variants.
- Radio state is updated by a configured 250 ms engine interval.

These values are useful as style/configuration inputs. They are not substitutes
for live player state and they do not authorize copying proprietary font or
texture assets into the mod.

## Engine and UI evidence

Static strings and imports in both exact executables confirm the relevant
systems are compiled into the runtime:

- Pip-Boy on/off and manager reset paths.
- Inventory, stats, data, quest, note, radio, world-marker, local-marker, and
  active-quest labels and commands.
- Quest stage/state mutation and Pip-Boy radio control commands.
- `Interface\\Radio Wave\\%s.dds` and the `\\pipboy3000\\` model path.
- The 2021 binary retains a PDB path naming the original `Fallout` game
  project; the 2009 binary names `Fallout3.pdb`. No PDB was present.

The executable imports Direct3D 9 and DirectInput 8. The game uses a Gamebryo
XML menu system, but the lower-panel design will not depend on keeping a vanilla
menu open. Vanilla menu classes and data builders remain valuable behavioral
oracles while tracing state and safe actions.

## Provisional implementation architecture

This architecture is a working decision subject to runtime proof:

1. A 32-bit native bootstrap is loaded next to `Fallout3.exe` through a normal
   game DLL dependency. The leading candidate is a `d3d9.dll` proxy because the
   exact binary imports it and WEMU already supplies DXVK for D3D9.
2. The proxy forwards the complete required D3D9 API to the real runtime. It
   performs no file I/O, thread waits, or library loading from `DllMain`.
3. Bootstrap identifies the main PE by version, image layout, and SHA-256 or a
   read-only equivalent. Unsupported binaries log and remain a transparent
   proxy.
4. A validated main/update synchronization point owns engine reads and action
   execution. UI and socket threads never dereference engine objects.
5. The game thread emits immutable, revisioned snapshots into a bounded cache.
   High-rate vitals, inventory, quests, map, and radio have separate dirty
   clocks so expensive collections are not rebuilt every frame.
6. A dedicated borderless Win32 window is placed wholly inside WEMU's lower
   RandR monitor. Its lifecycle is independent of the game's D3D device and
   menu/camera state.
7. The lower window is claimed by WEMU's existing `SecondScreenOutput` path.
   The renderer will begin with a deterministic diagnostic surface, then move
   to the custom Pip-Boy renderer after live state is proven.
8. WEMU's loopback gesture protocol on UDP 7788 is the primary touch route.
   Fallout must be added to the same duplicate-X-input protection used by the
   existing native lower-panel integrations.
9. Commands from touch/controller input enter a bounded queue with generation
   IDs. The game thread rejects stale commands after load, death/reload, or
   player replacement.

### Why the vanilla Pip-Boy will not be reused as the panel

Opening it changes menu mode, control state, animation/camera behavior, and the
main renderer. Moving it to another render target would couple lower-screen
lifetime to the upper camera and device. The vanilla menu will instead be used
as a behavioral oracle for data selection, formatting, and action preconditions.

## State extraction worklist

Every row requires both a stable read path and a mutation/event invalidation
strategy.

| Domain | Read target | Required proof | Current state |
|---|---|---|---|
| Lifecycle | player singleton, load/menu/death state, game-thread tick | New game, load, death/reload, cell transition | Unknown |
| Vitals | HP, max HP, AP, max AP, radiation, XP, level | Damage/heal, AP use/recovery, radiation, XP/level | Unknown |
| Attributes | SPECIAL, skills, derived stats | Equipment/effect changes and save load | Unknown |
| Effects | active effects, durations/magnitudes, limb condition | Add/expire/cure/cripple/heal | Unknown |
| Inventory | container changes, stacks, extra data, category, name | Pick up, drop, buy, consume, save swap | Unknown |
| Equipment | equipped weapon/apparel, ammo, condition | Equip/unequip, weapon swap, break/repair | Unknown |
| Quests | started/completed, stages, objectives, active quest | Start/update/track/complete/load | Unknown |
| Notes | known notes and content metadata | Acquire and load another save | Unknown |
| World map | worldspace, player transform, discovered markers | Move/discover/fast travel/save swap | Unknown |
| Local map | cell geometry/fog/player transform | Interior/exterior transition and movement | Unknown |
| Radio | available stations, signal state, tuned station | Discover/tune/off/signal loss | Unknown |
| Currency | caps | Loot, buy, sell, load | Unknown |

## Hook and signature policy

- A signature is limited to the executable `.text` section and must have a
  unique match for the recognized build.
- Relative targets are decoded and checked to fall in the expected module
  section.
- Global pointers are checked for alignment, readable memory, plausible vtable
  location, and stable identity over multiple game ticks.
- Structure offsets require at least two independent observations or an
  authoritative SDK/source plus runtime validation.
- Mutation functions additionally require calling convention, thread,
  precondition, and postcondition proof.
- A failed validation disables only the dependent feature. It must not crash or
  fall back to a guessed offset.

## Diagnostics and performance budgets

Planned logs are category-based and rate-limited: bootstrap/version, proxy,
hooks, lifecycle, player, inventory, equipment, quests, map, radio, UI,
input/actions, rendering, performance, and error.

No per-frame state dumps are allowed. Repeated errors use suppression counters
and emit a summary when the condition changes or the interval expires.

Initial budgets to validate on device:

- No full inventory, quest, marker, or form scan per rendered frame.
- No synchronous file I/O on the game or render thread after initialization.
- Bounded queues and buffers; no unbounded logs, telemetry, or retained
  snapshots.
- No blocking JNI/native transition in the per-frame path.
- Lower-screen work should stay below 1 ms average CPU time per game frame,
  with collection rebuilds amortized and profiled separately.
- No sustained memory growth during a 60-minute transition/action soak.

## Rejected or prohibited approaches

- Opening or hiding the vanilla Pip-Boy as a data source.
- Capturing, mirroring, OCRing, or periodically screenshotting the upper screen.
- Static screenshots or hardcoded values represented as live state.
- Switching the gameplay camera or its render target for the lower screen.
- Scanning all process memory or rebuilding all collections every frame.
- Executing actions from the UI/socket thread.
- Blind offsets, unvalidated detours, or silent support for an unknown binary.
- Replacing/downgrading the user's executable or altering DRM behavior.
- Committing or packaging proprietary Bethesda assets.

## Open questions

1. Does the top-level `1.7.0.4` build reach gameplay under the current Proton
   11 ARM64EC container, or does it fail before preferences/save creation?
2. Which D3D9 resolution path does DXVK use when an application-local proxy is
   present, and what Wine override (if any) is required?
3. Which 1.7.0.3 FOSE layouts and functions can be transferred to 1.7.0.4 by
   semantic signatures, and which were materially rewritten in 2021?
4. What are the exact lower RandR monitor bounds exposed during the Fallout
   session?
5. Can a D3D9 lower window use WEMU's direct AHardwareBuffer path, or should the
   first reliable renderer use the existing CPU triple-buffer fallback?
6. What input-mode contract gives controller navigation to the lower UI while
   suppressing the same navigation actions in gameplay?
7. Which map representation is safest: engine-owned map render data, a custom
   raster/vector reconstruction from loaded world/cell data, or a hybrid cache?

## Evidence log

### 2026-08-29 — initial ADB and binary reconnaissance

- Connected to the AYN Thor over ADB.
- Enumerated all five WEMU containers without modifying them.
- Found the Fallout game tree in Downloads and no copy in WEMU's managed
  `steam_games` directory.
- Found the Fallout user directory in container 2.
- Observed recent `Fallout3.exe` initialization records against that directory.
- Hashed and read version/PE metadata for `1.7.0.4` and `1.7.0.3` outside the
  repository.
- Confirmed both are 32-bit D3D9 executables at preferred base `0x00400000`.
- Confirmed no FOSE log, save, or generated `FalloutPrefs.ini` currently exists.
- Confirmed WEMU launch history explicitly targets the root `1.7.0.4` file.
- Confirmed three different WEMU profiles were attempted, with container 2 used
  most often but no successful-play baseline yet proven.
- Captured both Android display modes and external-touch ownership.
- Made no device or game-file changes.

### 2026-08-29 — clean-room audits and first native foundation

- Audited the pre-existing Fallout3SecondScreen directory and confirmed it was
  documentation-only: no source, build, tests, payload, or installer existed.
- Audited a public FOSE-era companion implementation for semantics only. It is
  hard-gated to `1.7.0.3`, polls/serializes broad state every 100 ms, and does
  not implement quests, notes, maps, radio, effects, lower-display rendering,
  touch, or controller behavior. No implementation source was copied.
- Recorded high-confidence semantic contracts for actor values,
  `ExtraContainerChanges`, stack-specific condition, worn flags, load-order
  identity, and game-thread-only mutations. Every address and layout remains
  untrusted for the installed `1.7.0.4` executable.
- Proved from WEMU source that its prefix-wide policy is
  `dinput8=builtin,native`. Installation therefore requires the narrow Wine
  AppDefaults value
  `Software\Wine\AppDefaults\Fallout3.exe\DllOverrides / dinput8 = native,builtin`.
  WEMU itself does not need a global source or container change.
- Implemented an original PE32 proxy which forwards all five `dinput8` exports
  by name/ordinal and loads the system forward target by absolute path after
  loader lock. `DllMain` only stores the module and disables thread callbacks.
- Implemented PE validation and an in-memory `.text` fingerprint. Timestamp
  matching identifies only a candidate; support is not enabled until mandatory
  signatures and runtime invariants all validate.
- Implemented a fixed-capacity, trivially-copyable state ABI and bounded action
  queue. Snapshots carry sequence and world-generation IDs; malformed/stale
  snapshots and commands are rejected.
- Implemented a no-activate, tool-window lower panel on the real non-primary
  monitor, with a configured fallback only if enumeration fails. It uses an
  isolated CPU/GDI backing store and never imports D3D9, manipulates a game
  camera, opens a menu, or captures the upper framebuffer.
- Implemented a custom phosphor/CRT diagnostic shell for STAT, ITEMS, DATA, and
  RADIO. It displays only values present in a verified snapshot; unknown data
  renders unavailable rather than zero or placeholder game state.
- Implemented bounded nonblocking loopback touch consumption on WEMU's existing
  port 7788, normalized-coordinate validation, duplicate activation suppression,
  and no gameplay input synthesis.
- Release build SHA-256 at this checkpoint:
  `2f35154fae6cee1a2ddb69697e73389568aabc1b1b0e025fcbf798ce1394df09`.
  It imports only Wine/Windows system DLLs and has no MinGW runtime dependency.
- Native tests pass for state ABI validation, monotonic publication, bounded
  command flow, stale-world rejection, PE32 parsing, `.text` hashing, and
  unknown-executable rejection.
- Exact-binary tests also prove the current command descriptors and unique
  player/equip, ActorValueOwner, and non-allocating container-root signatures
  against the user-owned `1.7.0.4` file. That test exposed and corrected a
  local-time/UTC error in the originally recorded PE timestamp before it could
  reach the device.
- The first read-only bridge now samples bounded actor values on a window-owned
  game-thread timer. It publishes only when primitives actually change; it does
  not clear/copy the large collection snapshot on unchanged 100 ms ticks.
  Aggregate tick, publish, panel composition, presentation, and bootstrap scan
  timings are emitted every ten seconds from the worker thread rather than
  writing logs from the game thread.
- A dry-run-first deployment transaction now validates the executable,
  container flags, inactive Wine process state, proxy conflicts, and the exact
  AppDefaults registry edit. It backs up both namespaces, uses sibling-temp
  renames, verifies hashes/registry content, and restores both game files and
  `user.reg` if a commit stage fails. Registry transformations have local
  preservation/insertion tests.
- The exact 1.7.0.4 localized inventory-entry name helper and vanilla category
  branches are signature-gated. A bounded, allocation-free fingerprint is
  evaluated at most once per second; names and rows rebuild only after a stable
  two-pass change.
- ADB preflight revalidated the exact executable hash/size, stopped Wine state,
  container dual-screen flags, proxy absence, and prior registry-value absence.
  The release was transactionally installed on 2026-08-29 and both the on-device
  DLL SHA-256 and per-executable `dinput8=native,builtin` override were verified.
  Backups are recorded in `docs/device-deployment-20260830-015336.md`. Runtime
  loaded-save proof remains pending.

### 2026-08-29 — three-mode Pip-Boy shell

- Replaced the four unrelated top-level pages with the canonical three-mode
  hierarchy: ITEMS, DATA, and STATS. Radio is now a DATA tab; Perks and General
  are now STATS tabs. Each mode owns exactly one five-tab rail at the bottom.
- Added a persistent two-layer GDI transition cache and a 230 ms smoothstep
  horizontal slide. Transition frames only perform clipped `BitBlt` operations;
  they do not rebuild live game state or allocate frame resources.
- Added a direction lock which commits a main-mode transition only when the
  horizontal displacement exceeds 10.5% of the panel and dominates vertical
  movement. Vertical drags independently scroll bounded list offsets.
- Placed the tab rail above WEMU's centered bottom-edge console gesture so the
  middle tab remains touchable on the Thor.
- Added live Perks and General views and split Local Map, World Map, Quests,
  Notes, and Radio into their requested DATA tabs. Selection and scroll state
  are invalidated or clamped when the snapshot loses an item.
- Added a controller transport shared by WEMU and Companion. Select+LT/RT sends
  an isolated loopback page command only while the Fallout lower panel is
  active; Select without a trigger is replayed to the game, and bare triggers
  are never stolen from gameplay.

### 2026-08-30 — live inventory metadata and actions

- Added exact RTTI descriptor validation for `TESWeightForm`, `TESValueForm`,
  `TESHealthForm`, `TESAttackDamageForm`, and `TESObjectARMO`. Inventory rows now
  read their real weight, gold value, base damage, armor rating, and per-stack
  condition from the current form and `ExtraHealth` data.
- The semantic inventory fingerprint now includes `ExtraWorn`,
  `ExtraWornLeft`, and condition data. Equipping gear therefore invalidates the
  snapshot even when FormID and quantity remain unchanged.
- Caps are counted from the live `0000000F` inventory entry. Current/max carry
  weight use actor values 46/13, current/max HP use actor value 16, and DR uses
  the current actor value 18 so equipped gear and modifiers are represented.
- Exact descriptor and unique-callsite tests prove `EquipItem` (`0x10EE`) and
  `UnequipItem` (`0x10EF`) plus native RVAs `0x003F0A30` and `0x003F1180` in the
  user-owned executable. Commands are consumed at most four per game tick,
  re-resolve their FormID against the current inventory, and call the native
  actor method once. Read-only refresh paths never enqueue an action.
- Reworked the ITEMS detail panel into a vertical stat list and category-aware
  `EQUIP`, `UNEQUIP`, or `USE` action. Health and carry pairs render as
  `current/max` without ambiguous spacing.
- Release SHA-256 `3db4eee5c5eaa972e91fa03d4ebe1bad904e88c29ed6f1344621629d12b5aa9b`
  was transactionally installed on 2026-08-30. Device backup:
  `.fallout3-dualscreen-backups/20260830-041512`. Loaded-save runtime proof is
  still required before marking the interaction acceptance rows complete.

### 2026-08-30 — full safety, STATS, and combat-performance audit

- Fresh device logs from the installed `640deca...` build prove the exact-build
  combat patch was active. Its ten-second aggregates measured the mod's game
  callback at roughly `0.24–0.29 ms` average, inventory work below `0.12 ms`,
  and the five-second data scan around `2.1–2.7 ms`. The reported sustained
  enemy slowdown therefore was not caused by the state sampler's measured CPU
  time.
- The exact `Fallout3.exe` is already Large Address Aware (`Characteristics
  0x122`). Reapplying a 4 GB/LAA patch is neither needed nor a valid explanation
  for the combat regression.
- Added `game_tick_gap` aggregation alongside callback duration. Since the
  window timer executes on Fallout's main thread, this records delayed main-loop
  scheduling independently from time spent inside the mod and makes the next
  enemy reproduction evidence-bearing.
- Audited the profile Fallout actually opens under
  `Documents\My Games\Fallout3`, not the similarly named files beside the EXE.
  It retained debug audio, synchronous blood/morpher/particle work, foreground
  model/lip loading, one Havok worker, 4x MSAA and conflicting shadow state.
  The shipped third-party "FPS" preset also raises shadows, MSAA, decals and
  draw distances, so it is not a handheld performance profile.
- Added narrow, merge-only main/prefs profiles. They retain resolution,
  fullscreen, controls and gameplay settings; enable the verified two-thread
  actor/effect/background paths; disable debug audio and actor shadows/MSAA;
  and keep multithreaded audio disabled because the engine path is unsafe.
  The game-local DXVK profile now caps D3D9 at 60 Hz. GPL remains disabled for
  this Turnip preset because enabling it previously faulted during New Game.
- The profile transaction validates stopped Wine state, backs up all three
  files, stages atomically, verifies hashes and rolls back all files on error.
  WEMU device backup: `.fallout3-performance-backups/20260830-070124`.
- Extracted the user-owned `menus\main\stats_menu.xml` from `Fallout - Misc.bsa`
  and compared the condition figure expression by expression. The custom panel
  had omitted the face Y expression `((92-64)/2)+16`, placing face/hair artwork
  at the top of the head. Restoring the scaled 30-pixel offset fixes the broken
  head composition.
- Fixed every wide `swprintf` call in the panel that used `%s` for a wide
  string. This was the direct cause of header values such as `HP 200/200`,
  `Wg current/max`, DR and Caps displaying only their first character.
- Release SHA-256 `73a74ffe80acff4235b77a6965de89078f4382b6c7966e30ce6ea0b392170a61`
  was transactionally installed and verified. DLL backup:
  `.fallout3-dualscreen-backups/20260830-070112`.

### 2026-08-30 - static-analysis and lower-panel refresh audit

- GCC's path-sensitive analyzer completed across the panel, state bridge,
  combat patch, runtime, and BSA/DDS loader. It reported no actionable engine,
  panel, runtime, or patch defects. Its only diagnostics were exception-path
  false positives around Win32 GDI cleanup owned by `unique_ptr`; the handles
  are explicitly restored and released during cache shutdown.
- The lower panel was rebuilding the immutable CRT vignette and 216 scanlines
  on every dirty composition. That could not explain the main-thread combat
  collapse, but it was needless concurrent GDI work. The background is now a
  persistent DIB composed once at panel startup and copied for later frames.
- Release SHA-256 `6ff8f9a498659241365d2a67ffad35bb0ec690d3c18fb7c28c223ff14c42a38d`
  passed the native unit tests and exact user-binary signature tests, then was
  transactionally installed and verified. Device backup:
  `.fallout3-dualscreen-backups/20260830-071532`.

### 2026-08-30 - cross-page state and Pip-Boy detail overhaul

- The exact 1.7.0.4 `TESIcon` RTTI descriptor is RVA `0x00E05428`; the
  exact-binary test verifies `.?AVTESIcon@@` before the live bridge is enabled.
  ITEMS, PERKS, QUESTS, and NOTES now carry their real record-owned DDS paths
  through the immutable snapshot. No game asset is bundled with the mod.
- Icon rendering uses aspect-fit placement rather than stretching into the
  panel. Loose files are resolved first, then `Fallout - Textures.bsa`; GOTY
  DLC `*- Main.bsa` archives are opened lazily only after a cache miss. Parsed
  DDS data and tinted GDI resources retain their existing bounded cache
  lifetime and are never tied to panel recomposition.
- The previous quest-stage value was an unproven byte read from
  `TESQuest+0x60`. FOSE's layout marks that member unknown. The exact installed
  executable instead exposes `GetStage` descriptor RVA `0x00D0BC98`, execute
  RVA `0x003962F0`, and evaluator RVA `0x002F4210`; disassembly proves the
  evaluator receives the quest as its second argument. Quest details now use
  that verified engine function.
- Fallout's `ShowQuestLog` has an exact callsite at RVA `0x003A8F59` to its
  journal-text resolver at RVA `0x00375BC0`. Both the resolver prologue and
  relative call target are binary-tested. The data scan walks only completed
  stage entries that own the selected quest and have a journal date, then asks
  Fallout to resolve the correct localized `CNAM` text on the game thread.
  NOTE records resolve their text through the validated `TESDescription*` at
  `BGSNote+0x6C`. Empty quest-lore and note-transcript panes are therefore no
  longer baked into the producer.
- Added distinct NOTE and PERK capability bits. Their loading states no longer
  piggyback on quest capability, which could expose a ready page before its own
  data producer had published.
- Vertical drag routing now honors the pane where the gesture began. Quest
  objectives, active effects, and long note text have bounded independent
  scrolling, while the left list remains stable. Detail offsets reset whenever
  selection or save generation changes.
- Failed action presentation now expires even when no new snapshot is
  published. Queue rejection, game rejection, and confirmation timeout all
  produce bounded visible feedback for ITEMS, QUESTS, and RADIO instead of a
  silent tap or a permanently stuck error label.
- Reworked item, perk, quest, note, radio, and general detail layouts around
  real live fields and real artwork. Inventory statistics remain vertical,
  objective completion uses explicit markers instead of gradient-like rows,
  and `ITEM TYPES` now accurately names the general-page count.
- Release SHA-256 `1e5f41efd2fdb4095e6bde2e43cb0a72dc0a9611fd9ef68399918416c89261f3`
  passed unit and exact-binary tests, was transactionally installed and
  byte-verified, and was staged into Dualscreen Companion from the same
  canonical package. Device backup:
  `.fallout3-dualscreen-backups/20260830-161547`.

### 2026-08-30 - live hostile-health timing proof

- Disassembly of the exact installed executable proves that HUD call RVA
  `0x0052DA40` and target-health-change call RVA `0x005309DF` both invoke the
  stock timer at RVA `0x005DA660`. Its sub-10 ms branch calls `Sleep` for the
  remaining interval on the main thread and then fabricates a 10 ms delta.
- The exact-build patch now routes the two sites through distinct no-wait
  wrappers. It records rate-limited HUD and health-change execution counts,
  maximum HUD scheduling gap, paused/alternate-clock branches, and verifies
  the live patched bytes every ten seconds. No log I/O occurs in either game-
  thread hook.
- The Inventory content brackets now end above the category navigation instead
  of overlapping its lower corners. The right detail divider follows the same
  corrected boundary.
- Release SHA-256 `c42e446fc38bbadce7d629eef360a54bcc1fe14de5ef0067c5603a3685a2afd3`
  passed unit and exact-binary tests, was transactionally installed and staged
  into Dualscreen Companion. Device backup:
  `.fallout3-dualscreen-backups/20260830-163902`.

### 2026-08-30 - hostile-combat follow-up and page-lifecycle audit

- A live reproduction with the installed `c42e446f...` release proved the two
  health-timer callsites remained patched (`patch_bytes=1`). The HUD path ran
  roughly 244-635 times per ten seconds while the target-health-change path
  remained at zero calls. During the same windows the complete mod callback
  averaged about 0.3 ms, but Fallout's main-thread callback gaps reached
  397-678 ms. The original target-health sleeps are therefore fixed and are
  not the source of the remaining hostile-only collapse.
- Thread sampling during the collapse placed the `dxvk-frame` worker around
  39-42% CPU, `wine_dsound_mix` around 14-15%, and Fallout's main thread around
  9-11% while sleeping in Wine/NT synchronization. A bounded exact-build IAT
  diagnostic now aggregates main-thread Sleep/Wait callsites without logging
  in a hook. All four imports and both health call contexts are independently
  checked by the exact-executable test. Partial diagnostic installation is
  transactionally rolled back.
- The redundant DXVK D3D9 60 FPS limiter was removed from the candidate
  profile. The Thor output already runs at 60 Hz, and the live profile showed
  `dxvk-frame` active while the game thread was blocked. This remains an A/B
  test candidate, not a claimed root cause, until the next hostile reproduction.
- SPECIAL, Skills, limb condition, quest, and perk reads no longer run while
  their pages are hidden. Entering a page schedules an immediate bounded scan;
  subsequent limb reads run at 4 Hz and static attribute reads at 1 Hz. Quest
  and perk traversal is likewise dormant outside the pages that consume it.
  Dedicated `vitals_scan`, `condition_scan`, `special_scan`, and `skills_scan`
  aggregates prove the new frequency and cost.
- Re-reading the user-owned `stats_menu.xml` found two additional Status
  composition errors: the face X offset is literal screen space under the
  native 145% head zoom, and the torso hierarchy is painted after the face.
  The lower panel now uses that exact coordinate relationship and native paint
  order, preventing the face from covering the neck/torso. SPECIAL and Skills
  artwork now aspect-fits instead of stretching to a fixed rectangle.
- Repeated `CreatePen`/`DeleteObject` pairs were removed from normal panel
  composition in favor of a bounded twelve-entry GDI pen cache. The DDS cache
  no longer permanently rejects all new icons after 256 paths; it now has a
  96 MiB byte budget and least-recently-used eviction, including failed-path
  eviction. Rate-limited `[UI-ASSET-PERF]` logs expose file reads, decodes,
  retained bytes, tints, and misses.
- UI actions are serialized from press through game confirmation. Selecting a
  second item, quest, or station while another command is pending can no longer
  overwrite the first request's confirmation state or enqueue a conflicting
  equip operation. Quest/radio producers remain requested until their pending
  action is confirmed even if the user immediately changes pages.
- Candidate release SHA-256
  `328e07bc43a52d7f04fb4165a5804f4ce22f3bd90e405072ed90303b9de19f1f`
  passes the complete unit, exact-binary, deployment, INI/registry, and GCC
  path-sensitive analysis suites. It is staged byte-for-byte in Dualscreen
  Companion. Device deployment and fresh Status/combat proof are pending ADB.

### 2026-08-30 - condition-figure and panel-lifecycle hardening

- The reported Status defect is in the native condition-figure composition:
  the page and figure are present, while the independently decoded head, face,
  torso, arm, and leg layers can otherwise produce an incoherent partial figure.
  The renderer now preflights the exact intact/broken variant for every limb and
  the matching face before submitting any native layer. Missing broken variants
  fall back to their intact counterpart. If a complete native set is not
  available, the page selects one coherent vector figure instead of mixing it
  with a partly rendered native body.
- Native drawing now returns the real GDI submission result. A failed layer is
  rate-limited in diagnostics and prevents the incomplete native composition
  from being treated as ready. Startup no longer marks Status artwork ready
  before the requested DDS set has actually loaded.
- Panel worker ownership is now explicit. The worker ID is atomic, duplicate
  starts are rejected while cleanup is in progress, and a timed-out stop keeps
  the worker handle and ID instead of permitting a second panel thread to race
  the first thread's GDI/window teardown.
- Release SHA-256
  `2e40303f21f22c1c1fd4cbde79e0a44d45e3346296b176ebca7037b2abf0b2a0`
  passes the native state/version, exact-executable, deploy-script, INI,
  registry-override, and installed-path tests. The Android `secondscreen` tests
  and restored `dualscreen` module compilation also pass under a constrained
  single-worker build. Fresh device visual proof remains pending ADB.

### 2026-08-31 - native-reference page and General-stat overhaul

- Nine native top-screen Pip-Boy references were captured from the running
  installed game: Local Map, World Map, Quests, Notes, Radio, Status CND/RAD/
  EFF, and General. The lower UI now follows their actual information
  hierarchy: DATA and STATS own five bottom tabs, CND/RAD/EFF remain a compact
  left selector, Quests/Notes/Radio use native-style list/detail panes, and
  General is a scrolling counter list with the live Karma card.
- The exact installed `Fallout3.exe` is still the supported 1.7.0.4 PE32 file.
  Its `GetPCMiscStat` command descriptor is RVA `0x00D0E448`, execute RVA
  `0x00395980`, and evaluator RVA `0x002F3540`. Descriptor, opcode `0x1138`,
  one-parameter ABI, evaluator address, and exact file hash are test-gated.
  Disassembly proves the evaluator consumes the second argument as the native
  misc-stat index and returns the value through its double output argument.
- The executable-owned label table contains exactly 26 valid counters, index
  0 (`Quests Completed`) through index 25 (`Mysterious Stranger Visits`). The
  bridge stages all 26 results and publishes them atomically; it never calls
  the unowned index 26. Scans are dormant outside General and aggregate under
  `[PERF] general_stats_scan`.
- RAD now uses actor value 20 for live radiation resistance and actor value 54
  for the live radiation dose, laid out on the native calibrated 0..1000 scale.
  The shared snapshot validator rejects non-finite RAD fields and any negative
  native counter once the General capability is published.
- Local/World map input now modifies panel-owned pan/zoom state only. The
  lower renderer plots the live player transform and engine-discovered marker
  records; World Map additionally resolves Fallout's own installed map DDS.
  It does not open a menu, copy a framebuffer, or write the gameplay camera.
- Release SHA-256
  `89615683881246a8871429c01cc4ae1f881f6a2facb87d032cf673bcf21a4297`
  passed unit, exact-binary, deployment, INI, registry, installed-path, and
  launcher payload tests. It was transactionally installed and byte-verified
  at `.fallout3-dualscreen-backups/20260831-183738`. Launcher payload
  `2026.08.31.3` contains the same DLL hash and was independently downloaded
  from the published release for verification.

### 2026-08-31 - acquired Notes, resolved World Map, and stack hardening

- The previous Notes producer was structurally wrong: it inferred notes from
  inventory entries. The exact executable's add/remove and Pip-Boy paths prove
  that acquired notes are the bounded list embedded at `PlayerCharacter+0x570`.
  Notes now have an independent page-driven fingerprint and revision, with
  title, read state, kind, text transcript, and image path copied while the
  engine objects are valid. Text notes resolve the native `TNAM` payload and
  image notes resolve their native image payload; no UI-thread engine pointer
  survives the snapshot.
- World Map no longer draws a synthetic grid. The bridge resolves the active
  `TESWorldSpace` MNAM data and installed texture path using exact-binary-gated
  helpers, publishes native bounds/usable dimensions, and falls back to the
  data handler's default world while the player is in an interior. The panel
  draws that installed DDS and applies one bounded world-to-map transform to
  the background crop, player arrow, and native marker records. Pan and zoom
  remain panel-owned and do not send input to Fallout.
- General now selects Fallout's five installed karma images under
  `textures\interface\icons\pipboyimages\karma icons` and displays the native
  level/karma rank instead of the player name. It does not bundle or redraw a
  replacement Vault Boy.
- Expanding `Snapshot` for complete note transcripts increased the PE32 object
  to `0x113808` bytes. Assigning `{}` to the persistent engine, store, panel,
  and test snapshots caused the compiler to materialize a full temporary on
  the 32-bit thread stack and produced `0xC00000FD` stack-overflow exits.
  Reset paths now zero their existing static storage in place with
  `SecureZeroMemory`; large state objects are never constructed on Fallout's
  stack.
- Exact-binary analysis proved the full native Local Map producer at RVA
  `0x54C820` is not an isolated rendering API. It requires the global `MapMenu`,
  its XML tile/progress graph, and shared renderer state. That unsafe producer
  remains unused. The per-cell render path beneath it is separable: exterior
  and interior cell preparation/texture getters create Fallout's dedicated
  local-map camera and offscreen targets without a MapMenu instance. The mod
  invokes one exact-binary-gated tile per game-thread tick, holds the native
  renderer lock only around production, performs same-thread D3D9 readback,
  releases the native Ni reference, and publishes only an immutable BGRA copy.
  A 17x17 native fog-of-war sample is composited per tile. The panel performs
  aspect-correct crop, pan, zoom, and the player arrow from that copied frame;
  it never receives engine/D3D pointers and never writes gameplay camera state.
  A separate small immutable cursor record tracks the current transformed
  player position and heading, avoiding full raster rebuilds during ordinary
  movement. A rebuild is requested only after the cursor reaches the inner
  one-cell margin, which also covers large single-cell interiors.
  Runtime proof in exterior/interior saves remains required before declaring
  the Local Map acceptance row complete.
- Candidate DLL
  `fef3becce18f93df603c81c6576abf7da68abf1b8be6b6ac80af5cb2fce908e1`
  passes state/version, exact-executable, deploy-script, INI, Wine-registry, and
  installed-path tests. The exact-binary suite additionally verifies every
  Local Map transform, preparation, texture, fog-of-war, cell-lookup, and RTTI
  oracle entry point. Immutable-frame tests reject stale revisions and invalid
  bounds. Deployment is pending because no ADB device is currently connected.

### 2026-09-01 - native marker, radio, and CND coordinate correction

- A live 1240x1080 CND capture proved that asset availability was not the
  failure: all seven condition layers decoded, but independently stretching
  their power-of-two DDS canvases separated the face, shoulders, torso and
  limbs. Direct inspection of the user-owned `Fallout - Textures.bsa` found
  128x128 head/torso canvases, 64x64 face, 128x64 arms and 128x128 legs. Their
  visible-pixel extents match the authored `stats_menu.xml` body coordinates.
  The renderer now applies the XML hierarchy once and draws every canvas at one
  shared source-pixel scale. Left/right actor values 27/28 and 29/30 are also
  paired with their matching XML limb instead of the opposite-side meter.
- Local Map pins are now canonicalized before an immutable frame is published.
  Fallout can surface the same logical door through adjacent participating
  cell lists; references of the same marker kind that land within one native
  raster pixel are merged, while distinct objective/door semantics remain.
  The publish log reports the exact `coalesced` count. Unit coverage proves
  same-FormID promotion and same-position alias behavior.
- Local and World map view centers are panel-owned for the lifetime of a
  visible map page. The raster, live player arrow and every pin use the same
  projection, so game-state refreshes cannot recenter a user pan or create
  half-second marker jumps. Re-entering a map page, changing local cells/world
  spaces, or changing save generation establishes a new native player anchor.
- World location capture now uses the same virtual reference-position accessor
  as `MapMenu`, publishes only native visible+discovered markers, and hashes
  discovery flags so a newly discovered location appears automatically.
  Icon-aware projection rejects pins whose original 50-pixel artwork would
  cross the map frame; labels are clamped inside the same frame.
- Radio candidates are synchronized in the background every five seconds and
  at one-second cadence while the Radio page or an action is active. Newly
  entering native candidates emit `[RADIO] signal received`, external tuning
  updates the selected row, and tune/off actions continue to use the verified
  Fallout native calls.
- Quest details no longer repeat the selected title. Stage, description and a
  five-row objective viewport occupy the detail area, while Track Quest is
  anchored at the bottom with matching touch/controller hit testing. Apparel
  rows now draw their record-resolved Pip-Boy icon, and long inventory detail
  names select the largest cached font that fits instead of clipping.
- Release SHA-256
  `29a86f6d10a32433dfe1d4ed74f23b33815c851fa120a029368b4a1e6e2a3f77`
  passes immutable-frame/projection unit tests, exact 1.7.0.4 executable
  signature tests, deployment-script/INI/registry tests, and the warning-clean
  release build. Dualscreen Launcher stages the identical hash under payload
  version `launcher-2026.09.01-fo3-native-sync-1`.

### 2026-09-01 - CND sizing, native apparel art, and radio animation

- The corrected `stats_menu.xml` condition composition is now fitted at exactly
  80% of its previous size around the same center. All six health meters inherit
  that 0.80 scale and remain anchored to their transformed head, torso, arm, or
  leg box; no independent screen-space compensation was introduced.
- FOSE's supported record layout confirms that ARMO and CLOT Pip-Boy artwork is
  not the direct `TESIcon` component used by ordinary inventory forms. It lives
  at `TESBipedModelForm::icon[2]` (`+0x8C` within the biped component). Inventory
  capture now reads that native nested path from ARMO `+0x70` or CLOT `+0x68`,
  with the ordinary component retained only as a mod-compatibility fallback.
- The duplicate selected item and radio station names were removed from their
  right detail panels. The freed inventory area is assigned to the original
  record artwork. The Radio detail panel now draws an animated, strength-scaled
  carrier at 20 FPS only while that page is visible; animation is panel-thread
  work and adds no per-frame/game-thread callback.
- Release SHA-256
  `ea5289e7cd0c1cc3c7361974742ee9eddc58005ae8c5efab6342d3539ad6a304`
  passes the native unit, exact-executable signature, deployment-script, INI,
  registry, warning-clean compiler, PE architecture, and export checks.
