# Fields of Mistria dual-screen architecture reuse audit

Date: 2026-08-30
Scope: read-only audit of the existing workspace. This document does **not** establish anything about the currently installed Fields of Mistria executable, NPC Studio's current `maybe` engine, or the current Mistria SDK. Those facts must come from device and SDK reconnaissance before an integration mechanism is selected.

## Recommendation

Build Fields of Mistria as a current-SDK-first game adapter feeding a native, no-activate lower-panel window. Reuse the WEMU panel contract and the mature generic plumbing from the newer native mods, but keep all game knowledge behind an `IMistriaBridge` boundary.

The recommended data path is:

```text
current Mistria SDK / verified engine seam
                |
                | game-thread events and commands
                v
       MistriaBridge + DirtyTopics
                |
                | validated immutable snapshots
                v
        StateStore (revision/epoch)
                |
                | copy only when revision changes
                v
 no-activate Win32 panel on WEMU monitor 2
                ^
                |
 Android Presentation -> ordered loopback UDP :7788
        touch / swipe / pinch / controller-nav
```

This preserves three important boundaries:

1. The game adapter owns reads and mutations and runs them on the game thread.
2. The panel owns only immutable presentation state and never dereferences live game objects.
3. WEMU owns physical touch/controller capture; the game receives only deliberate commands, never synthetic gameplay clicks.

Do **not** choose DLL proxying, assembly rewriting, a particular graphics API, or absolute-address hooks until reconnaissance proves the current executable architecture and the SDK's loading model. Stardew's managed patcher is a useful lifecycle pattern, not evidence that the current game is patchable .NET.

## Reuse matrix

| Concern | Reusable source/pattern | Reuse decision | Evidence | Confidence |
| --- | --- | --- | --- | --- |
| Independent lower-panel output | WEMU's `SecondScreenOutput` plus a private no-activate Win32 panel | Reuse the contract and window behavior. Create an exact class/title identity such as `FieldsOfMistriaSecondScreenPanel` / `Fields of Mistria Dual-Screen`. | `engine/src/main/java/com/wemu/engine/renderer/SecondScreenOutput.java:15-30` sends the guest window buffer directly to the physical lower panel without making the top renderer build a second scene. `dualscreen/mods/Fallout4SecondScreen/src/Panel.cpp:991-1068` discovers monitor 2, creates `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, positions it without activation, and reports initialization. | High |
| Render lifecycle | Revision-driven panel invalidation | Reuse the principle; implement bounded startup and explicit stop/join rather than copying detached-thread lifecycle verbatim. | `dualscreen/mods/GrimDawnSecondScreen/src/Panel.cpp:603-618` repaints only when the published sequence changes and blits a private backbuffer. `dualscreen/mods/Fallout4SecondScreen/src/Panel.cpp:970-1083` has bounded monitor discovery and fail-soft resource teardown. | High for the pattern; medium for direct code reuse because neither source is a complete lifecycle abstraction. |
| Physical touch isolation | WEMU Presentation-level interception and ordered UDP pump | Reuse unchanged at the protocol level. | `core/display/src/main/kotlin/com/thor/core/display/ThorPresentation.kt:79-84,124-125` intercepts touch before an embedded surface can consume it. `secondscreen/src/main/kotlin/com/wemu/session/secondscreen/SecondScreen.kt:44-73,101-135` uses one actor/socket to preserve down/move/up ordering and pins IPv4 loopback. `app/src/main/kotlin/com/wemu/session/SessionScreen.kt:569-600` states that only normalized finger gestures are sent and enables the router only while the guest owns the panel. | High |
| Gesture parsing and arbitration | Fallout 4 `PanelInput` | Extract and rename as generic `PanelInput`; keep packet limits, finite/range validation, viewport mapping, full-trajectory tap suppression, swipe/scroll thresholds, reserved WEMU handle, `page` and `nav` commands. | `dualscreen/mods/Fallout4SecondScreen/src/PanelInput.cpp:102-159` validates the protocol; `:175-229` maps away letterbox bars and preserves pointer-sequence state; `:231-327` arbitrates taps, vertical scroll, horizontal page swipe, controller navigation, cancellation, and the reserved bottom-center gesture. Regression coverage is in `dualscreen/mods/Fallout4SecondScreen/tests/PanelInputTests.cpp:24-128`. | High |
| Swipe between game UI pages | `PanelGestureKind::kPage` plus page carousel concepts | Reuse input semantics; build a seven-page Mistria carousel with animation state independent of game state. Do not copy Fallout/Grim page contents or artwork. | Page packets and horizontal swipes resolve to a direction at `dualscreen/mods/Fallout4SecondScreen/src/PanelInput.cpp:273-275,303-306`. `dualscreen/mods/GrimDawnSecondScreen/src/PageCarousel.cpp:8-16` provides generic wrap/visible-neighbor arithmetic and has dedicated tests wired by `dualscreen/mods/GrimDawnSecondScreen/build.ps1:43-51`. | High |
| Controller mode without gameplay theft | WEMU Select-modified controller routing | Extend native-panel recognition for Mistria, then reuse the existing `Select + L2/R2` page and `Select + d-pad/A` navigation contract. | `app/src/main/kotlin/com/wemu/session/SessionActivity.kt:2770-2807` scopes commands to the panel and replays an unused Select tap to the guest. `:2810-2848` routes pages/navigation only while Select is held. `:2851-2880` consumes trigger-axis packets in panel mode so stick/trigger state cannot leak into gameplay. | High |
| Switch between WEMU console and game panel | Bottom-center WEMU panel-mode gesture | Reuse unchanged; reserve its origin in the guest arbiter. This is separate from horizontal game-page swipes. | `app/src/main/kotlin/com/wemu/session/SessionScreen.kt:1002-1056` implements the small bottom-center upward-swipe toggle. `dualscreen/mods/Fallout4SecondScreen/src/PanelInput.cpp:234-256,267-269` reserves the same region from guest actions. | High |
| Immutable live state handoff | Grim Dawn `Snapshot`/`StateStore` | Extract the version/size/sequence/world-epoch/capability validation pattern, but define Mistria-specific topic snapshots rather than one enormous struct. | `dualscreen/mods/GrimDawnSecondScreen/src/StateStore.cpp:35-52` stamps lifecycle, sequence and epoch. `:54-87` rejects malformed/impossible state. `:114-141` accepts only newer snapshots and provides a locked copy plus atomic sequence. Tests cover duplicate/stale and malformed values at `dualscreen/mods/GrimDawnSecondScreen/tests/StateStoreTests.cpp:22-100`. | High |
| Event-driven synchronization | Stardew native lifecycle and dirty-state model | Reuse the model, not Stardew types. Subscribe to current SDK events, coalesce dirty topics, use slow signatures only for nested mutations lacking events, and detach subscriptions on world unload. | `dualscreen/mods/StardewNative/NativeHooks.cs:51-123` derives world/day/time/location transitions and coalesces inventory changes; `:162-194` safely reattaches/detaches inventory events. `dualscreen/mods/StardewNative/ARCHITECTURE.md:26-34,82-96` documents event invalidation, fallback polling, reconnect snapshots, and bounded transport. | High for the pattern; unknown for event availability in the current Mistria SDK. |
| Safe actions | Background input queue drained on game thread | Reuse as a hard rule. Parse/validate off-thread; enqueue typed commands; revalidate ownership, current save/epoch, menu/cutscene restrictions, and game rules on the game thread before calling SDK actions. | `dualscreen/mods/StardewSecondScreen/Commands.cs:25-63` bounds and parses a concurrent command queue. `dualscreen/mods/StardewNative/ARCHITECTURE.md:68-80` documents that socket commands are drained only during `Game1.Update` and invoke real game APIs. `dualscreen/mods/DeadIslandSecondScreen/src/TouchInput.cpp:68-105,218-264` coalesces move traffic, bounds per-frame consumption, and separates a receiver thread from panel handling. | High |
| Reconnectable topic transport | Stardew `Bridge` | Reuse only if the SDK/plugin and renderer end up in separate processes or the Android Compose UI is selected. It is unnecessary inside one native module. | `dualscreen/mods/StardewSecondScreen/Bridge.cs:26-33` keeps latest-by-topic state and bounds output; `:51-67` defaults to loopback; `:147-190` caps inbound frames at 64 KiB; `:216-225` updates reconnect state; `:263-305` bounds queues and closes cleanly. | High technically; low that it is needed for the preferred in-process native panel. |
| Logging | Grim Dawn rate-limited logger plus Stardew rotation/fail-soft behavior | Extract a game-neutral logger with categories and rate keys; add size rotation and an emergency bootstrap log. | `dualscreen/mods/GrimDawnSecondScreen/src/Log.cpp:30-49` timestamps and flushes errors; `:74-109` serializes output and rate-limits by key. `dualscreen/mods/StardewNative/NativeSupport.cs:23-69` rotates at 4 MiB and makes logging fail-soft. `dualscreen/mods/StardewNative/NativeBootstrap.cs:104-132` catches callback/log failures. | High |
| Performance telemetry | Fallout 4 aggregate timers | Reuse after making domain names Mistria-specific. Keep counters allocation-free and flush summaries, not per-frame samples. | `dualscreen/mods/Fallout4SecondScreen/src/UiPerf.cpp:12-35` defines atomic domains; `:40-68` records microseconds; `:70-88` emits count/total/average/max every ten seconds. | High |
| Build/version gate | Grim Dawn `BuildIdentity` and native build tests; Stardew hash manifest | Reuse the fail-closed identity shape. Gate SDK ABI/version first, then executable/module architecture, file version, size and SHA-256 as needed. Unknown builds may show a diagnostic panel but must not install hooks or enable actions. | `dualscreen/mods/GrimDawnSecondScreen/src/BuildIdentity.cpp:86-135` validates PE architecture, timestamp, sizes, and SHA-256 for all required modules. Its unrelated test executable is required to fail the support gate at `dualscreen/mods/GrimDawnSecondScreen/tests/StateStoreTests.cpp:88-94`. `dualscreen/mods/StardewNative/build.ps1:41-59` records input, patched, and runtime hashes plus game version/distribution. | High |
| Build/test scaffolding | Fallout 4 and Grim Dawn scripts | Copy the test-first structure, deterministic linker option where supported, PE/export audit only if a proxy is actually chosen, runtime probe, and hashed package manifest. | `dualscreen/mods/Fallout4SecondScreen/build.ps1:29-86` builds/runs isolated protocol, bridge, ABI, input, assets, adapter and client tests; `:90-157` performs a proxy runtime probe; `:103` disables linker timestamps; `:178-195` creates a hashed package. `dualscreen/mods/GrimDawnSecondScreen/build.ps1:33-82,112-137` adds input/carousel/hook/compat/state tests and PE/export/runtime checks. | High |
| Deployment safety | Fallout 4 ADB deployment transaction | Adapt, including dry run by default, exact executable gate, owner marker, non-symlink checks, staged hashes, verified backups, commit verification, and verified rollback. Never overwrite an unowned DLL/config. | `dualscreen/mods/Fallout4SecondScreen/deploy.ps1:15-23,56-70` validates owner/path/target; `:89-127` verifies existing ownership and defaults to dry run; `:132-182` verifies backup and staged/installed hashes; `:192-233` verifies rollback and reports rollback failure explicitly. | High |
| Installed-game asset loading | Stardew engine asset APIs first; Deus Ex archive loader only as a parser-hardening reference | Prefer SDK asset IDs/texture handles. If the SDK cannot expose art, write a Mistria-specific read-only installed-asset provider with strict bounds and caches. Do not reuse another game's archive parser. | Stardew reads the game's loaded content and sends each sprite once (`dualscreen/mods/StardewSecondScreen/Sprites.cs:17-32,50-72`; `dualscreen/mods/StardewNative/NativeRuntime.cs:1237-1271`). The archive fallback demonstrates bounded parsing, installed-directory discovery, and device-keyed caching at `dualscreen/mods/DeusExSecondScreen/src/GameTextureArchive.cpp:30-31,88-99,112-143,540-620,687-739`. | High for the policy; unknown for current Mistria asset APIs and formats. |

## Required WEMU integration

A new native window will render on monitor 2 without game-specific Android UI, but four small WEMU changes are required for first-class behavior:

1. Add an exact `isFieldsOfMistria(className, title)` matcher to `engine/src/main/java/com/wemu/engine/renderer/NativePanelIdentity.java` and include it in `isEarlyClaimPanel`. The current allow-list contains only Skyrim, Fallout, and Grim Dawn (`NativePanelIdentity.java:9-31`). Exact matching is important; the negative cases in `engine/src/test/java/com/wemu/engine/renderer/NativePanelIdentityTest.java:19-27` prevent an ordinary game/helper window from stealing the panel.
2. Include the Mistria identity in `SecondScreenOutput.isNativePanelControllerActive()` (`engine/src/main/java/com/wemu/engine/renderer/SecondScreenOutput.java:216-220`) so Select-modified controller commands activate only while its real panel is visible.
3. Add `isFieldsOfMistriaSessionIdentity` and a `FIELDS OF MISTRIA` waiting title to `app/src/main/kotlin/com/wemu/session/SessionScreen.kt:97-119`. This suppresses the irrelevant managed WebSocket retry and shows the native waiting surface; that distinction is made at `SessionScreen.kt:553-584`.
4. Add exact positive/negative and ended-session tests alongside `NativePanelIdentityTest` and `app/src/test/kotlin/com/wemu/session/NativePanelSessionIdentityTest.kt:6-32`.

No Mistria-specific touch overlay is needed. `GuestPanelMotionRouter` already owns the full guest panel, reserves WEMU's bottom-center handle, normalizes coordinates, classifies drag/pinch, and cancels incomplete gestures (`app/src/main/kotlin/com/wemu/session/SessionScreen.kt:667-841,843-969`).

## Concrete initial project structure

```text
dualscreen/mods/FieldsOfMistriaSecondScreen/
|-- README.md
|-- TECHNICAL_INVESTIGATION.md
|-- PROOF_MATRIX.md
|-- FieldsOfMistriaSecondScreen.ini
|-- FieldsOfMistriaSecondScreen.owner
|-- build.ps1
|-- deploy.ps1
|-- docs/
|   |-- ARCHITECTURE_REUSE.md
|   |-- SDK_SURFACE.md
|   `-- DEVICE_RECON.md
|-- src/
|   |-- Entry.cpp/.h                 # selected SDK/proxy loader only
|   |-- Compatibility.cpp/.h         # SDK/build/architecture gate
|   |-- BuildIdentity.cpp/.h         # hashes and module identity
|   |-- Config.cpp/.h
|   |-- Log.cpp/.h
|   |-- Perf.cpp/.h
|   |-- panel/
|   |   |-- Panel.cpp/.h             # monitor-2 no-activate window lifecycle
|   |   |-- PanelRenderer.cpp/.h     # custom pixel-art renderer
|   |   |-- PanelInput.cpp/.h        # UDP packet mapping/arbitration
|   |   |-- PageCarousel.cpp/.h
|   |   `-- FocusModel.cpp/.h
|   |-- state/
|   |   |-- SnapshotTypes.h           # POD/UI-owned data only
|   |   |-- StateStore.cpp/.h         # version, epoch, revision, validation
|   |   `-- DirtyTopics.cpp/.h        # status/inv/map/quest/etc. invalidation
|   |-- bridge/
|   |   |-- IMistriaBridge.h          # engine-independent contract
|   |   |-- SdkBridge.cpp/.h          # preferred current SDK implementation
|   |   |-- CommandQueue.cpp/.h       # typed, bounded, epoch-tagged actions
|   |   `-- ActionPolicy.cpp/.h       # world/menu/cutscene/rule revalidation
|   |-- assets/
|   |   |-- IAssetSource.h
|   |   |-- SdkAssetSource.cpp/.h     # preferred installed-game assets
|   |   `-- TextureCache.cpp/.h       # bounded decoded/GPU cache
|   `-- ui/
|       |-- StatusStrip.cpp/.h
|       |-- InventoryPage.cpp/.h
|       |-- MapPage.cpp/.h
|       |-- QuestsPage.cpp/.h
|       |-- VillagersPage.cpp/.h
|       |-- AnimalsPage.cpp/.h
|       |-- SkillsMagicPage.cpp/.h
|       `-- AlmanacPage.cpp/.h
`-- tests/
    |-- CompatibilityTests.cpp
    |-- StateStoreTests.cpp
    |-- PanelInputTests.cpp
    |-- PageCarouselTests.cpp
    |-- CommandQueueTests.cpp
    |-- ActionPolicyTests.cpp
    |-- AssetPathTests.cpp
    |-- ProtocolFixtureTests.cpp
    `-- RuntimeProbe.cpp             # only when the loader mechanism is known
```

`SnapshotTypes` should be split into bounded topic records (status, inventory/equipment, quests, villagers, animals, skills/magic, almanac/museum, map/locations). Each topic carries `worldEpoch`, `revision`, `complete`, and capability flags. Static definitions use stable asset/data IDs and are cached; dynamic records refer to those IDs. This prevents a relationship update from copying the whole inventory or rebuilding the almanac.

## Implementation gates

### Gate 0: reconnaissance before source integration

Confirm from the installed copy and current SDK:

- exact executable, PE architecture, file/product version, SHA-256, loaded runtime modules, graphics API, and launcher path;
- SDK loader/manifest format, SDK version/ABI, lifecycle and world/save events, game-thread callback, logging API, asset APIs, and UI/render extension points;
- whether the SDK supplies stable IDs and events for every target data domain and supported mutations;
- the actual asset containers and whether the SDK can resolve a texture/atlas region without extraction;
- the panel window behavior under the current Wine/FEX runtime.

The bridge interface can be defined after this inventory, but `SdkBridge` must not be stubbed with guessed types. If an SDK operation is absent, mark the corresponding capability unavailable; do not silently fall back to old GameMaker/data.win techniques.

### Gate 1: diagnostic foundation

The first executable build should contain only:

- compatibility/version gate;
- logger/performance counters;
- empty state store and lifecycle states;
- monitor-2 panel with exact WEMU identity;
- isolated touch/page/controller input with no game mutation;
- status topics proven through supported SDK calls.

Prove that the top game keeps input/focus and that panel startup, save load/unload, day transition, area transition, and game exit are clean before adding the full UI.

### Gate 2: live state and action safety

For every domain, record the authoritative SDK owner, event/callback, stable ID, snapshot cadence, invalidation reason, action API, thread requirement, and validation scenario in `TECHNICAL_INVESTIGATION.md`. Actions remain disabled by configuration until their game-thread tests pass. Unsupported builds and missing SDK capabilities render an explanatory read-only state; they never install unknown hooks.

## What is generic and safe to reuse

- Monitor discovery and a no-activate lower-only Win32 window.
- Immutable versioned snapshots, lifecycle epochs, capability bits, sequence checks, and bounded records.
- Loopback UDP packet grammar, finite/range validation, viewport mapping, gesture arbitration, WEMU-handle reservation, swipe/controller page commands, haptic acknowledgements, and move coalescing.
- Typed command queues drained on the game thread.
- Dirty-topic/event coalescing, reconnect/full-snapshot rules, and carefully bounded fallback polling.
- Rate-limited/rotating logs, aggregate performance timers, deterministic/test-first builds, compatibility gates, owner markers, staged deployment, verified backup/rollback.
- Abstract asset-source/cache interfaces and defensive parser techniques.

## What must not be copied

- `StardewNative.NativeRuntime`, `StardewState`, or Stardew command implementations: all contain Stardew-specific types, rules, IDs and world assumptions.
- Any old Fields of Mistria GameMaker, `data.win`, UndertaleModTool, save-layout, or hook work. The request explicitly targets the current migrated engine.
- `Fallout3Ui.cpp`, Fallout Pip-Boy layouts, fonts, icons, scanlines, page structure, or Fallout asset-name mappings.
- Grim Dawn, Deus Ex, Dead Island, Skyrim, Fallout, Stardew, or Valheim game-state offsets, signatures, SDK calls, menu actions, coordinate transforms, archive parsers, UI strings, or category rules.
- Bundled extracted artwork. In particular, existing project asset directories and generated caches are evidence of prior pipelines, not redistributable source material for Mistria.
- Proprietary Mistria sprites, portraits, atlases, fonts, maps, localization, or database exports in the package. Resolve them from the user's installed game at runtime or build a user-owned cache containing hashes/metadata rather than shipping the content.
- Hidden/unrevealed almanac, gift preference, quest, event, romance, museum, map, or perk information. The bridge must publish discovery flags and the UI must honor them.

The custom Mistria renderer may reproduce the game's broad visual language through layout, palette and pixel-art-friendly primitives, while installed asset IDs supply the user's legitimate icons/portraits. It should not copy another mod's game-specific screen implementation wholesale.

## Principal risks and mitigations

| Risk | Consequence | Mitigation |
| --- | --- | --- |
| SDK assumptions based on the old engine | Immediate breakage or wasted reverse engineering | Treat the current installed modules and current SDK as the only source of truth; no GameMaker path in the initial tree. |
| SDK can read but cannot mutate one or more systems | UI appears interactive but lies about game state | Capability-gate every action separately; show unavailable actions; add a lower-level adapter only after a concrete missing seam is proved. |
| SDK callbacks occur off the game thread | Races/crashes during inventory, quest or save transitions | Callbacks only set dirty flags or enqueue POD deltas. Build snapshots and drain actions at a verified game-thread lifecycle callback. |
| Save/day/area transitions invalidate object references | Stale pointers, wrong-save commands, crashes | Never store engine object pointers in UI state. Increment `worldEpoch`, clear command queues, detach events, publish loading state, then reacquire owners. Tag commands with epoch. |
| Seven rich pages cause giant snapshots/serialization | Frame spikes and memory growth | Topic revisions, fixed limits, static-definition cache, paged lists, event coalescing, and per-domain performance counters. |
| Asset APIs are unavailable or return transient GPU handles | missing art or device-loss crashes | Stable logical IDs, render-thread acquisition, bounded cache keyed by device/generation, placeholders generated by code, and fail-soft reload on device loss. |
| Window is not recognized early by WEMU | Console/wallpaper covers it or helper window steals the panel | Exact WEMU identity matcher, early-claim priority, ownership-policy tests, and device proof through window create/destroy/recreate. |
| Android touch and X/Windows pointer both reach gameplay | attacks/tool use/camera movement | Use only the ordered `127.0.0.1:7788` normalized command channel for the custom panel, consume Presentation touch, keep the panel no-activate, and device-test every gesture with the gameplay pointer captured. |
| Controller mode becomes sticky | Gameplay controls stop responding | Reuse Select-scoped routing, timeout/reset on focus/session/panel loss, and exact active-panel identity checks. |
| Proxy/loader ABI is guessed | Game fails before logging | Select the official SDK loader when possible. If proxying is required, derive exact exports/ABI from the installed binary and require a load/export/factory runtime probe before packaging. |
| Deployment overwrites another mod or an updated game | unrecoverable install damage | Dry-run default, exact game hash, owner manifest, non-symlink regular-file checks, staging, verified backup, commit hashes, and verified rollback. |
| Engine update changes SDK/data semantics | silent wrong values/actions | SDK ABI gate plus build profiles; fail closed for mutations, report capabilities and exact identity, retain a read-only diagnostic panel only where safe. |

## Initial acceptance tests

Before UI polish, require automated tests for malformed packets, black-bar rejection, drag-return-not-tap, scroll-versus-page arbitration, WEMU reserved handle, controller page/navigation commands, stale epoch/sequence rejection, bounded collection counts, command overflow/coalescing, unsupported-build fail-closed behavior, path traversal/symlink asset refusal, rate limiting, and deployment rollback.

Device proof must then show:

1. the exact Mistria panel is claimed and released without the top screen flickering;
2. lower touch never swings a tool, attacks, moves, or changes the gameplay cursor;
3. horizontal page swipe, vertical list scroll, pinch and WEMU console toggle do not conflict;
4. Select-scoped controller mode enters/exits cleanly and an unused Select tap still reaches the game;
5. repeated save loads, day changes, area transitions, cutscenes and game exit increment/clear lifecycle state without stale objects;
6. each displayed value and each enabled action is demonstrated against actual current game state; and
7. ten-second performance aggregates show no recurring full-domain rebuild and no meaningful top-screen frame-time regression.

Until current device/SDK reconnaissance is recorded, confidence is high in the reusable WEMU/render/input/state/deployment architecture and intentionally **unknown** in the Mistria gameplay bridge, asset format, loader mechanism, and action coverage.
