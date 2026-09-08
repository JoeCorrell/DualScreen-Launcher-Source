# Assassin's Creed II Second Screen

In development. This directory owns the native PC mod, research tools, tests,
and separate downloadable payload. No AC II implementation or game-derived
assets may be bundled into the launcher APK.

The requested interface is a persistent 1240 × 1080 Animus 2.0 touchscreen
with Map, Equipment, Inventory, Missions, Database, and Progress pages.
Gameplay must retain the entire upper screen and normal controller input.

Current source adds the complete original city-map category legend with scrolling,
the original Ezio map icon, and a guarded native notoriety reader. Hiding spots
are excluded from map drawing, selection and counts. Empty legend categories
remain visible; an unavailable marker reader displays unknown counts. Page
navigation uses cached swipe transitions. Gesture frames now use accelerated map
resampling, skip a redundant full-screen copy and draw cached marker sprites.
Guard/Archer map icons are 20px; other map icons remain 36px. These changes were
installed in dev.14; device gesture timing remains unverified. New source widens
the Map sidebar, enlarges Player Status, keeps notoriety on one line and labels
the currency Florins. Health/currency appear only on Map. Equipment's side
panels share the central preview panel's edges and have equal horizontal gutters.
These layout changes are included in the subsequent development payloads.

Fresh read-only Ezio captures resolved twelve native character meshes, matching
geometry objects, skinned draw ranges/buffer identities and all eight skinned
mesh bone-palette sets. The bounded ownership/coherence readers are tested and
used by the private probe. CharacterPreviewTarget and CharacterDrawPacket pass
local D3D9 indexed drawing, retained-resource, state-restoration, readback and
generation-discard tests. They are not hooked into AC II yet. Native camera,
material/pass selection, alternate meshes and preview presentation are still
unfinished; this is not a working Ezio preview and does not enable that capability.

Native actions currently cover guarded cape equipment, medicine requests, and
selection of carried swords, daggers and hidden blades. Local native/ABI tests
are separate from live gameplay validation. The full mod is still in development:
the live 3D character preview, remaining contextual actions and progression edge
cases need further integration. Stable 60 FPS and audible audio correctness have
not been established. The user confirmed launcher 67 with AC II's Performance
preset, full x87 precision, affinity repair and 30 FPS cap fixes the observed
falling issue and maintains approximately 30 FPS for most gameplay. Preserve
that configuration while developing the companion. Dev.22 is staged in the
private launcher cache; activation and its bounded render-pass capture remain
unverified. It does not enable a working 3D preview. No public release is published.

Current source also recovers archive categories and entry selection when live
unlock data changes, keeps the selected entry and reading position during normal
updates, and clears stale inventory selection and pressed feedback on transitions.
Inspect-only items and rejected action requests no longer produce success-like
pressed feedback. These UI changes are local and are not in the staged dev.22 ZIP.

The user's attached six-panel specification is the visual reference: white and
light-gray surfaces, blue-gray construction lines, fine geometric fragments,
condensed typography, restrained dark-red selection, DNA motifs, generous touch
targets. The newer map reference supersedes the map layout: a left marker and
player-status sidebar beside the large map. Per the user's latest instructions,
page navigation uses animated horizontal swipes with no visible page buttons or
controller hints. On the map, start a page swipe at an outer edge or in the header;
interior drags and pinches retain map control. Values and item names pictured
in that reference are illustrative, not evidence about AC II game systems.

## Verified device baseline

- ADB device: AYN Thor, `100.83.63.70:46665` at initial reconnaissance.
- Game directory: `/sdcard/Games/Assassins Creed II`.
- Actual game: `AssassinsCreedIIGame.exe`, 34,693,768 bytes, PE32/i386.
- SHA-256: `23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92`.
- Preferred image base `00400000`, image size `0216e000`, PE timestamp `4b8c2c75`.

### Landing contact-search candidate (dev.18)

`tools/patch_landing_contacts.py INPUT.exe OUTPUT.exe` creates a separate, exact-hash-gated
executable from the user's original file. It changes only the two 0–45/0–70 degree
contact queries in `0107a0a0`, adding an executable/read-only code section without
moving original sections or the original overlay. The original input is never overwritten.

The stock queries return the first slope-matching record, then the caller rejects
excluded component types without examining later records. The candidate searches
past excluded records using the original query and component filter. It preserves
the height/timer gate, target veto, slope limits, remaining landing logic and the
unfiltered 0–135 degree query. It does not force state flags, move contact records,
teleport the player, change frame timing or change emulator settings. Code runs
synchronously on the existing caller, with a private array header on its stack.

The runtime also accepts the exact candidate SHA-256
`e0520f85b2cb3ac10ce59c8073cebd5853ab6395b3d2303f368b9e3203595abd`
(34,701,312 bytes). No other modified executable is accepted. This build recognizes
the original executable too, allowing rollback without changing the payload.

**Status:** Offline candidate. X86 instruction emulation covers contact ordering,
excluded/empty lists, slope ranges, register/stack preservation and rebased images;
PE checks cover original sections, overlay and checksum. The live captures do not
contain contact arrays, so this edge case is not proven to explain the reported
falling lock. Missing contacts, timer suppression or another earlier veto would
need a different correction. No claim of resolved live physics or stable FPS.

Use the dev.18 payload with the candidate executable; older payloads reject its
hash. Replace files only while AC II is closed, retain the original executable,
and keep the payload cache selection consistent with the installed DLL. Never
distribute an original or patched game executable in the downloadable mod payload.
- Imports include Direct3D 9, DirectInput 8 and XInput 1.3.
- Common asset archive: `DataPC.forge`, 88,997,888 bytes, Scimitar version 25.
- City/world archives include Firenze, Venezia, Villa, Toscana, Wetlands,
  Roma, Mountains, Abstergo, Hideout, WhiteRoom, and tomb-specific archives.

Original binaries and analysis databases are kept in ignored `.research/ac2`
and `.work/ac2`. They are never payload contents.

## Implementation rules

Use exact-build and instruction-signature gates, bounded immutable snapshots,
generation-scoped identities, and game-thread semantic actions. Unknown fields,
capabilities, discovery states and action preconditions remain unavailable.
Never infer ownership from an asset existing in a forge archive. Do not use
save editing, fabricated completion percentages, synthetic input, pause-menu
automation, duplicate gameplay actors, or per-frame process scans as substitutes
for native integration. Character preview must reuse a verified isolated native
render path; it must restore render/camera state and must not advance gameplay.

The build and launcher delivery integration must exclude the entire generated
AC II payload from APK assets, including DLLs, UI assets and previews. Native
payload updates must not require recompiling Android or the launcher.

## Validation status

The diagnostic native DLL builds and renders all six pages. Lifecycle/command,
touch/navigation, archive bounds and focused launcher package/routing tests pass.
Original Animus menu scripts and native inventory/map/DNA command handlers have
been traced. A separately packaged development ZIP and on-demand launcher loader
are implemented; the APK excludes AC II payload binaries/assets.

The source now reads native player inventory, carried equipment, localized DNA
memories, unlocked historical database entries and four collectible counters.
New guarded readers reproduce native overall synchronization weighting, glyph
puzzle completion, unlocked/equipped capes, and the Villa dashboard's
unlock/restoration/value/population rules. DNA accepts entitled DLC sequences
through the game's normal visibility branch. The current-mission reader follows
native story/side-activity priority and reads the game's current-objective text
record, clearing it during free roam. Focused tests pass. A live Florence probe
verified facing, synchronization, glyphs, and the early-save empty-cape/locked-Villa
branches. Active missions and positive cape/Villa unlock transitions need testing.
Original icons, portraits and database art load asynchronously from the owner's
archives. Map rendering supports the six main regions, native discovery masks,
live player position, native facing direction, marker visibility rules, dragging and pinch zoom.
Archive decoding reads only the blocks containing the requested resources.

Windowless probes verified inventory/database/carried-equipment readers and the
Florence position/discovery reader against the running game. All six city asset
chains were checked against the original archives. Marker rules have native
code evidence, local tests and a successful live probe. Development payload dev.7
is installed. Its touch fix is confirmed on the device: all six page changes
appear in the native log, with 250 accepted packets and none rejected. The user
also confirmed the interface works. Original uncompressed shop icons decode.

Dev.8 is now confirmed installed and adds story-memory, sequence and localized side-activity counts.
Cape selection now uses the native inventory permission and mutation dispatchers,
with exact executable/instruction/subscriber checks, ownership re-resolution,
and callback transition guards. All 22 native test executables pass, including
the x86 call ABI and the six-page render harness. Positive native cape changes
still need gameplay testing with a save that has robes and an unlocked cape;
the observed young-Ezio save correctly exposes no cape action.

Dev.9 source adds native health and total viewpoint/treasure counters, verified
by a fresh read-only probe in the user's game (20/20 health, 1/73 viewpoints,
2/330 treasure). The shared reference reader now ignores the independently
changing retain count while still checking pointer identity and residency.
All 24 native tests pass after that change; the new payload needs in-game visual
and transition checks after installation. The agent never launches or controls
gameplay for these tests.

Dev.10 source adds native weapon ratings (damage, speed, deflection) and five-part
bars in the inventory details panel. The reader checks current ownership and
catalog identity, follows the persistent definition/entity association, verifies
the resident entity's reverse resource-pool mapping, and reads the original
WeaponComponent fields. Unloaded or changing resources leave ratings unavailable.
Native-layout/transition tests and the layout/scroll harness pass; live weapon
ratings are not yet verified because the game is currently closed. Long inventory
card labels now have room for two full lines. No APK rebuild is needed.
**Other equipment/use actions, the live Ezio preview,
full missions/objectives/DLC and complete progress statistics
remain unfinished. This is not a release-ready mod.**

Dev.11 reduces software rendering overhead: display data comparisons suppress
unchanged frames without suppressing fresh command revisions, direct DIB drawing
avoids the GDI+ HDC path, and map player indicators / DNA animate over a cached
page. Map viewport, discovery, artwork, marker, page, input and lifecycle changes
invalidate the cache. Two fixed 1240x1080 buffers use approximately 10.2 MiB.
The normal-size presentation uses BitBlt; other monitor sizes retain scaling.
Bounded ten-second timing summaries distinguish full from cached renders.

The exact supported executable in the measured Wine `5:3,4,5,6,7` topology also
gets an affinity repair. Three verified game import call sites retain access to
all five configured CPUs, with one startup and one delayed thread repair. Other
topologies and native Windows are unaffected. A reversible stationary live trial
improved 24.05 to 37.89 FPS; this is not a 60 FPS guarantee. Device-only isolated
render benchmarks measured cached marker updates at 0.295 ms and DNA updates at
about 2.1 ms, excluding presentation, native sampling and gameplay. Dev.11 needs
activation on the user's next normal launch and an end-to-end comparison.

Dev.12 spreads recurring map/equipment, DNA/missions, database, and progress
refreshes across four sample slots. New generations and explicit invalidations
still refresh immediately. Native sampling now logs aggregate timing every ten
seconds. It also corrects the access rights needed to repair pre-existing Wine
threads; the process-wide affinity change already activated successfully in
dev.11. The enumerated-thread repair has a real Windows API regression test.

Live dev.11 captures after removing a stale synthetic-state sender reached
51.34 FPS over twenty seconds; shorter captures varied from 45 to 56 FPS.
Scene/combat variation prevents treating those as a controlled gain or a stable
60 FPS result. The device's KGSL pwrscale=0 leaves its GPU busy counters frozen,
so the reported 5% is unavailable as a live utilization measurement. The separate
launcher source now hides that invalid reading; no APK is rebuilt for this payload.

AC II is listed as beta. The user requested the separate development payload
installed against the existing game folder in Skyrim's Games container and will
perform the gameplay tests. No release is published. See [RESEARCH.md](RESEARCH.md)
for the device-test handoff, exact evidence and remaining work.

## Source and third-party notices

The native payload uses the LZO 2.10 safe LZO2A decoder by Markus F.X.J.
Oberhumer, under GPL-2.0-or-later. See COPYING and
third_party/lzo/UPSTREAM.md. Development packages include the complete native
build source and license alongside the DLL. Original game files and artwork
are read locally and are never included in a package. The Android launcher
remains separate from this source package.

### Scheduling regression diagnostic

For a controlled comparison against the game's original thread scheduling, create
`AssassinsCreedIISecondScreen.ini` alongside the game executable with:

```ini
[Diagnostics]
NativeAffinity=0
```

Restart the game. The log must report `performance affinity disabled by
Diagnostics.NativeAffinity=0`. This skips both the affinity IAT hook and the two
startup thread repairs; it does not disable UI or native state readers. Remove
that setting or set it to 1 to restore the existing behavior on the next launch.
This is a diagnostic control, not a confirmed fix for the reported landing lock.

`[Diagnostics] ModEnabled=0` disables the state bridge, asset workers, and companion UI. DirectInput forwarding
and the separately configured affinity repair stay active for the game. This is a restart-only isolation control; the lower screen
will have no companion interface during the test. Default1 retains the mod.

`[Diagnostics] CharacterPassTrace=0` disables the development renderer observer.
Dev.22 otherwise collects one bounded200ms batch of current-player render-pass
metadata (maximum128 rows) and logs it after callbacks drain. It does not call
D3D, change the gameplay renderer, or enable the unfinished3D preview. This
identifies immediate versus queued draws and native material/camera overrides
before connecting the tested off-screen character renderer.
