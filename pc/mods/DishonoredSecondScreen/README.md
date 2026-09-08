# Dishonored Second Screen

Native dual-screen implementation in development for the user's installed
Dishonored PC build on AYN Thor. Normal gameplay occupies the upper screen;
the requested independent 1240 x 1080 touchscreen contains Map, Powers,
Equipment, Objectives, Journal, Bone Charms, and Statistics.

The first full development payload was built and installed into WEMU on
September 6, 2026 for the user's requested two-display test. The seven pages and
bounded live-state/native-action paths are compiled; runtime acceptance, map
positioning and remaining capabilities are still unfinished. Investigation
uses the installed executable and cooked packages, not assumed UE3 offsets.

## Stacked desktop correction (dev3)

The user reported texture flashing and black flickering in both Launcher and
WEMU. Logs show the 1274x716 game client at (323,482), overlapping the lower
panel at (0,720,1240,1080). Host fullscreen fitting did not separate these Wine
windows. This is a confirmed geometry defect; it does **not** establish that
all reported texture corruption has the same cause.

Payload `2026.09.06-viewport-dev3` adapts WFC's owner-thread borderless placement
to Dishonored. Every two seconds it can correct the game to (0,0,1280,720), only
on the exact 1280x1800 stacked desktop. Window identity uses the game's native
registered WNDPROC (009ca440), verified at registration site 009ca746. It rejects
other processes/threads, panels, dialogs, ambiguous candidates and minimized or
hidden windows. Resizing yields the rest of that sampling tick so the game can
handle its own WM_SIZE and swap-chain recreation. No focus, input, graphics
driver or registry changes are made. `Panel.MaintainUpperViewport=0` disables
this correction if needed for diagnosis.

`upper-viewport` logs use revision/status 0 for before, 1 for verified placement,
2 for failure, and 3 for no unique native candidate on the current game thread.
Coordinates and client dimensions occupy a/b/c/d; stable diagnostics are limited
to ten-second intervals. Native boundary tests cover overlap, throttling, HWND
recreation, 13 safety exclusions and failed placement. All 235 original-binary
and schema evidence checks pass. These checks do not replace gameplay testing.

The dev3 DLL was installed on September 6 with its separate Launcher cache;
120 package files were verified. DLL SHA-256:
`6a833c8b38d08622672ccd13c0c186bdc93a3a69d0770671d93045bbd85da2cf`.
Receipt: `.work/dishonored/recovery-launch/viewport-installation.json`.
The old DLL is backed up on the device. No game was launched or input sent.
Visual flicker acceptance and the remaining map capabilities are still pending.

## DualScreen Launcher integration

**Current installed release:** `1.6.17.16-all-changes` (code 45), built from the
current workspace including BOTW host/runtime changes, installed September 6,
2026. APK SHA-256:
`0565aa92f4970f1fc75e1e2f197c4ff515f9ecb82c912d3b48c7226e87691045`.
The release is signed with the existing certificate; 48 targeted tests pass.
Its loaded Eden ELF sections match the current native library, all 123 WFC
assets and 154 SMO assets were verified, and BOTW artwork follows the current
separate signed-module architecture. Dishonored dev3 remains a separate payload.
The verified APK and `all-changes-installation.json` are in
`.work/dishonored/recovery-launch/`. Installation retained app data and did not
launch a game. Production builds disable run-as, so private settings were
captured before this update; they were not read back after installation.

The following describes the earlier Launcher43 integration and diagnosis.

The user confirmed that the full mod launches with the verified windowed
arguments. The lower 1240x1080 panel appeared and reported real health/mana;
the top game window was framed and centered too low in Wine's 1280x1800 desktop.
The user requested continuing in **DualScreen Launcher**, with device input and
launching left under their control.

Launcher version `1.6.17.14-dishonored-runtime-fix` (code 43) applies Dishonored's
1280x720 launch arguments to both new and existing shortcuts, keeps FEXCore 2608,
selects the verified imported **Turnip A8XX Draft** driver for this shortcut,
and enables the existing renderer's client fitting for this session. Fitting
maps the game image onto the upper screen without changing its working windowed
D3D9 device; the native journal keeps its independently claimed lower surface.
The shared WFC preset and its fit/driver settings are not changed by this policy.

Code 42 exposed a Launcher provisioning bug before Wine started: the per-game
FEXCore-only request went through complete-preset validation and failed with
"The captured preset has no Box64 version". Code 43 uses explicit runtime
requirements: full presets require their Box64 archive and renderer; a FEXCore
override requires only its exact manifest component. The existing component
presence, receipt, archive-size and SHA-256 checks still apply. Both installation
and launch preparation use the corrected request. No container field is invented
or changed to bypass validation.

The APK build and 36 targeted launcher/renderer tests pass, including six
regressions covering translator requests and full-preset validation. Its signing
certificate and public sign-in configuration match the prior installed APK.
All 123 bundled WFC asset files and 400 previously installed Switch artwork
files were preserved for this update. Dishonored's native DLL/assets remain
outside the APK in the verified on-demand dev2 payload. The final APK SHA-256 is
`a536640e3c1603a70c9de6206a2e4f2acf7482f1df0aeaa392b20f0af82b2e50`.
Build/verification records are under `.work/dishonored/recovery-launch/`;
`launcher-installation.json` records completed device installation when present.
Corrected fullscreen presentation still requires the user's Launcher test.

## WEMU investigation history

**Working baseline confirmed:** on September 6 the user successfully launched
**Dishonored Baseline** in WEMU container 5. The captured process environment
confirms `dinput8=b`; compositor logs confirm 1280x720 frames. This is an
unmodded, single-display baseline, not acceptance of the mod.

After the user closed the game and container, **Dishonored Dual Screen** was
prepared with `execArgs=-WINDOWED -ResX=1280 -ResY=720` and the container's
`displaySecondScreen` restored to `1`. The game's native viewport startup code
was checked for these arguments before applying them. All remaining container
settings, including FEXCore 2608, were verified unchanged. The full DLL and
launcher-cache identities were checked again. No game was launched or input
sent during this change. The user's subsequent launch succeeded; its positioning
result is described above.
Receipt: `.work/dishonored/recovery-launch/windowed-dual-installation.json`.

**Startup failure investigation:** the user reported failed launches after the
first installation. Current diagnostic version is `2026.09.06-wemu-dev2`, DLL
SHA-256 `62b0b01f355ad75abbd5dbd520bb6433c7060966aba85230ef2d734af1c7cc0f`.
WEMU shortcuts **Dishonored Diagnostic** (full mod) and **Dishonored Baseline**
(Wine built-in DirectInput, bypassing the mod) target the executable directly;
Wine diagnostics go to WEMU's console. The original game shortcut
remains available. This build adds logging before native DirectInput forwarding
and before executable verification. The original game now has a successful
baseline; the cause of the mod/two-display launch failure remains unconfirmed.

The later DualScreen Launcher attempt at 20:19:48 verified `0.0.2-probe` and
overwrote the shared DLL/INI, invalidating subsequent full-mod testing. Its
app-private development cache is now pinned to the same verified full package
as WEMU, preserving the prior cache version. Cache SHA-256:
`452f68febf507719f40804abee933a341fe3888c6fea2cf7cebbe7d2238a43ef`.
All 120 installed and 122 cache file hashes passed. WEMU container/registry
bytes were unchanged by this diagnostic installation. An isolated loader test
of the explicitly hashed full DLL passed both on Windows and in headless WEMU;
the helper does not launch the game or validate gameplay hooks/rendering.
Receipt: `.work/dishonored/startup-failure/diagnostic-installation-receipt.json`.

The following details describe the initial `dev1` installation, superseded by
the diagnostic update above:

Launch **Dishonored Dual Screen** in WEMU's **proton 9** container (ID 5).
The shortcut points to `F:\Games\Dishonored\Binaries\Win32\Dishonored.exe`
with `WINEDLLOVERRIDES=dinput8=n,b`. The full DLL replaces the diagnostic probe.
All 120 installed files were SHA-256 verified, including 117 original art/font
resources and their manifest. FEXCore 2608 and every container setting were
preserved byte for byte; `displaySecondScreen=1` was already enabled.
No APK was rebuilt/replaced and no game was launched or controlled.

The earlier build hold was superseded by the user's explicit request to install
the mod into WEMU for testing, followed by confirmation that Dishonored was
closed. No agents were used. The source survived the earlier PC crash.

Version: `2026.09.06-wemu-dev1`. DLL SHA-256:
`26147e45931f3906672b7c7a287663850b0bf4c878381d3114116302a3be0939`.
Receipt: `.work/dishonored/install-test/installation-receipt.json`.
Previous DLL/INI backup on the device:
`/sdcard/Games/Dishonored/.dual-screen-backups/20260906-200731`.
Build passed `-Wall -Wextra -Wpedantic -Werror`; 232 native/schema evidence
checks and 26 compiled touch-protocol cases passed. These checks do not establish
runtime correctness, visual acceptance or crash-free gameplay.

Stock WEMU's inspected APK does not recognize Dishonored's new private class.
This install enables `[Panel] WemuSkyrimTransport=1`, adding the supported
`SkyrimSecondScreenPanel` token to the private window class while retaining the
Dishonored title. This uses the existing GDI/UDP 7788 transport and input-isolation
rules; it does not call Skyrim engine code or alter WEMU. Default packages leave
this compatibility option off. Original map sheets can be browsed, but live map
position, facing and marker calibration remain unfinished.

The investigation sections below include historical source-only and probe-test
milestones. The installation record above supersedes their deployment status.

`Bridge`, `Mailbox`, `GameMemory`, `StateAdapter`, `Definitions`, `Touch`, `Ui`
`Panel`, `StateAdapterActions`, `StateAdapterMaps`, `StateAdapterDocuments` and `Runtime` now contain the source for the immutable game/UI boundary, bounded
readers, original localized power definitions, seven-page layout, isolated
touch receiver and secondary-only window. Readers cover scalar health/mana,
coins/runes, active/passive powers, objective history and child tasks, owned
weapon slots, ammunition, elixirs, purchased upgrades, abstract inventory,
collected/equipped charms with read-only modifier evaluation, mission-stat
records, saved mission results, the native mission chaos category, localized
keyring entries and owned grouped readable documents. Unknown or failed data remains
explicitly unavailable. Source action paths validate and call native power,
weapon, ammunition and charm methods on the game thread, then wait for state confirmation.
Elixir consumption uses the native success result and restoration/consumption path.
Elixir capacities come from the native player tweak limits. Audiograph transcripts
use the verified journal text getter on an explicit read request, with native
FString destruction and fresh owned-document validation. Audiograph play/stop
source uses Corvo's native speaker hooks and waits for conversation-state confirmation.
Map sheets include their native localized legends and a touch selector for
currently acquired loaded sheets. A native note-interaction hint lets a newly
read map board be revalidated on the next sample instead of waiting for the
amortized actor scan. Saved mission
summaries respect the original condition-bit predicate.
The UI source includes equipped loadout/charm rows, Equipment and Journal category filters and
allowlists of 68 original item illustrations and 15 objective portraits resolved
from game definitions. Chapter targets and evaluated custom-task status follow
the native objective interface. Bounded exception and memory-read diagnostics
remain source-only and have not been exercised on the device. Equipped ranged
weapons show their selected ammunition and count through owned weapon keys.
Weapon details read live modified damage/headshot attributes, explicitly before
attack and target adjustments; these are not a prediction of damage per hit.

Maps can display a currently loaded map board whose native interaction flag is
set, using one of 13 verified original map exports. This is not complete map
coverage: native custom serialization saves/restores the interaction flag, but
map selection across unloaded areas and live position calibration remain unresolved.
The bounded map cache now detects in-place loaded-level replacements and prunes
unloaded or successfully rescanned missing candidates without treating read failure
as proof that an actor disappeared.

Map calibration/markers, full weapon statistics,
remaining journal paths, broader lifecycle
coverage, visual polish and acceptance testing remain unfinished. Runtime source
hooks the verified controller tick, pawn restore, teardown, task-marker render
and note interaction, uses object-registry
membership plus collection/serialization guards, and withdraws the adapter
before restoring hooks. This is source implementation, not runtime proof.
`build-payload.ps1` and `tools/package_payload.py` now describe the full payload
as source-only recipes. Neither has been executed. `build-probe.ps1` still
describes only the earlier diagnostic proxy. Read-only verification passed 232
native/schema contracts and all ten original Flash power-icon mappings; these
checks do not compile or validate the C++ implementation.

The latest continuation adds current-area names using the native persistent
streaming-package selection and original active-language MapNames section, with
the game's INT section fallback. It never displays a raw package identifier as
an area title. Native objective-marker visibility has been traced, but live map
markers remain withheld until the map transforms are verified.

Dishonored launcher touch source now sends real down/move/up contacts, cancels
them on pinch and surface loss, suppresses replayed completions, and keeps touch
out of X11 gameplay input even before panel discovery. Late scale callbacks after
cancellation are ignored. These host changes require an eventual launcher build;
they are not active in WEMU or the installed launcher.

The original idle/selected journal frames now use their correct roles and
preserve their corners with a nine-slice border. Selected equipment/charm details
reserve more reading space. Image/font initialization failures and touch/render
counters have bounded worker-side diagnostics. Read-only asset inspection passes
117 original resources totaling 11,179,056 image pixels, below the 33,554,432-pixel
budget. All ten power-symbol mappings and 232 native/schema checks pass. Grammar
inspection finds no newly introduced C++ diagnostics and no Kotlin grammar errors;
this is not preprocessing, type checking, a build or runtime validation.

After ADB access resumed, 161 original level packages (800.2 MiB) were copied and
verified against device SHA-256 values. The package reader identifies 15 map-board
placements in the examined packages, including repeated chapter/geometry variants.
Kingsparrow's three printed board markers provide candidate calibration anchors;
24 exported geometry meshes were checked against 2,467 original vertex positions.
The drawings are schematic, so the initial affine fit is still research data and
is not enabled as a live player-position transform. The map-definition source now
also reads the existing native class default when the note movie/tweak override
is absent, following the game's fallback without opening or constructing a menu.

Objective details now include native localized target names and distances captured
from the task-marker render input. Every published target must still belong to an
active tracked task and its tracked parent, match a current HUD-owned marker,
pass actor lifetime/visibility checks and have an unexpired render observation.
The panel expires these observations independently of snapshot arrival. This does
not enable map-coordinate markers; the map calibration remains unfinished.
Audiograph controls use the existing conversation actor rather than a journal
movie. Transcript reads restore the shared audio-name scratch field using a
separate bounded cleanup reader, preserving the playing recording's identity.
These additional hooks/actions and their touch layout are in the installed
development build and require device validation.

The [direct Skyrim architecture comparison](SKYRIM_ARCHITECTURE_COMPARISON.md)
now informs independent lower-panel startup and an exact 1280x1800 tall-desktop
fallback. Game-state initialization runs separately, with joined shutdown and
explicit bootstrap-failure state. This preserves the GDI panel while game hooks
initialize or fail; it does not make a single 1280x720 WEMU desktop a dual-screen
container. These changes are included in the current WEMU development build.

## Earlier launcher configuration (before the WEMU test installation)

Dishonored uses the actual Transformers WFC container 3. At the user's request,
its shortcut now overrides only `fexcoreVersion=FEXCore-2608-stable-0`; the shared
container's other settings and the WFC shortcut are unchanged. The downloaded
2608 component package matches GitHub's published SHA-256, and both installed
translator DLL hashes were verified after the launcher selected it. Launcher
source preserves this per-game selection for new and existing shortcuts.

The user confirms that Dishonored launches successfully in WEMU's `proton 9`
container (ID 5, actually `Proton-10.0-arm64ec-0`). That is the working baseline;
it does not verify that the diagnostic hook or full mod is working. WEMU uses
the `Turnip A8XX Draft` driver and one 1280 x 720 monitor. Launcher container 3
uses `turnip26.2.0` and a 1280 x 1800 desktop. DXVK settings, D3D9 DLLs, FEX 2608
DLLs and game INI contents matched during the comparison.

The launcher also had game registry resolution 540 x 960, versus WEMU's
1280 x 720. Only the launcher's game registry resolution was changed to
1280 x 720 for the subsequent test. The working driver was copied into a
separate launcher driver directory but has not been selected. The test did
not reach a visible menu before the user switched games; its termination must
not be treated as proof of an independent crash. No single cause is isolated.

Under the later explicit WEMU installation request, the existing diagnostic
proxy was enabled in the shared game INI and a `Dishonored Probe.desktop`
shortcut was added with `WINEDLLOVERRIDES=dinput8=n,b`. The WEMU container and
registry remained unchanged. The proxy DLL was not rebuilt or replaced. The
subsequent authorized log inspection found only five earlier hook-disabled
initializations, despite the currently enabled INI; it did not establish hook execution.
This diagnostic artifact cannot draw the bottom-screen interface. WEMU's
console on the bottom screen is not evidence that the full mod is installed.
No full-mod build or installation has been performed.

## Exact investigation target

- Device: AYN Thor, wireless ADB (confirmed by the user).
- Game: `/sdcard/Games/Dishonored`.
- Executable: `Binaries/Win32/Dishonored.exe`, PE32/i386, 18,008,064 bytes.
- SHA-256: `67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5`.
- PE timestamp: `0x5212f0d7`; preferred image base `0x00400000`.
- Image size: `0x011fd000`; build metadata changelist 334700, engine 9411.
- Cooked package version/licensee: 801/30; core packages use LZO compression.
- The complete directory inventory contains 8,680 files, approximately 10.96 GiB.

User-owned binaries, extracted assets and decompiled game code stay in ignored
`.research/dishonored`, `.work/dishonored` and `artifacts/dishonored-*` trees.
They are analysis inputs, not checked-in mod implementation or APK assets.

## Architecture reference

`../SkyrimSecondScreen/src/Bridge.h` and `Mod.cpp` establish the versioned,
bounded snapshot/command boundary, independent no-activate lower window,
isolated loopback touch, cached rendering, and domain revisions.
`SkyrimPlugin.cpp` establishes game-thread state sampling and semantic actions,
explicit lifecycle invalidation, and validation before accessing game objects.
Only these architectural contracts transfer. SKSE, Bethesda layouts, rendering
entry points, FormIDs and addresses do not apply to Dishonored.

Dishonored imports DirectInput8Create from DINPUT8.dll and renders through D3D9.
A transparent DirectInput proxy is a candidate bootstrap, subject to device
validation; the lower renderer must not replace or alter the normal game view.
State must be read from the real game and actions must execute on its game thread.
Unknown capabilities remain unavailable. No save editing, input simulation,
pause-menu automation or fabricated discovery/progression state is acceptable.

## Delivery

The implementation and its UI resources belong in a separately versioned,
downloadable payload. Launcher integration must exclude `mods/dishonored/payload`
from APK assets and use the per-game payload installer. A package must remain
development-only until its native integration and device acceptance pass.

See `RESEARCH.md` for evidence and outstanding work. Research tools operate on
copies and never modify the installed game.

## Current launcher and development artifacts

Dishonored is registered as a Beta library entry with the user's supplied
cover artwork, exact executable verification, executable-scoped dinput8
override and an independently managed payload ID (`dishonored`). The APK asset
filter excludes its native payload. Private lower-panel identity is
`DishonoredSecondScreenPanel` / `Dishonored Dual-Screen Journal`; it must never
match the ordinary Dishonored game window.

`build-probe.ps1` builds a **diagnostic-only** 32-bit dinput8 proxy. It samples
verified scalar state and bounded counts after the native controller tick;
it does not implement the requested interactive pages or enable game actions.
`tools/package_probe.py` creates a separate package marked `development`,
which the production download path rejects. Device testing uses the launcher's
existing explicit app-private development opt-in. These artifacts are not a
release claim.

The user's all-pages mockup is the presentation reference in `UI-DIRECTION.md`.
`tools/prepare_assets.py` prepares original extracted artwork and fonts in a
local, ignored staging directory with source hashes.
