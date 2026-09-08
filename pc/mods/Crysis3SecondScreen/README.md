# Crysis 3 Second Screen — work in progress

This is **not the finished functional mod**. APK 62's read-only native UI is
installed; the owner confirmed working swipes and live health/energy on APK61.
The new revision adds guarded objective/module/weapon-name readers, removes the
large developer/title banner, expands the Nanosuit schematic, and fixes the
panel's clear-then-copy painting path. Its new data paths and rendering fix still
need gameplay validation. See [NATIVE-RUNTIME.md](NATIVE-RUNTIME.md).

Historical deployment context: APK 60 packages
an enabled, exact-profile native callback trial for the next launch; no gameplay
hook is yet verified for production use. The research DLL defaults disabled when
its opt-in INI is absent; it is not the completed mod. The Android launcher has a diagnostic-only Crysis 3
entry sharing Skyrim's Games prefix; see DEP-INVESTIGATION.md. The installed APK
contains the hash-locked, one-bit execute-permission compatibility trial described
there. It enables exception logging and the lower diagnostic console, not this
native UI harness. Rebuilt launcher `1.6.17.28-crysis-update` (versionCode 57) was
installed in place on the Thor through ADB port 41335 on 2026-09-07. The installed
version and exact patched game-executable hash were verified. The owner subsequently
reported the crash fixed during a monitored Jailbreak run; guest logging continued
beyond startup and the successful-session evidence was saved. Cross-mission and
checkpoint stability testing remain pending.

Live gameplay subsequently permitted a read-only native-code capture and
bounded actor/suit probes. The observed energy value decreased during the owner's
cloak test and returned to 100 after recharge. Native energy publication, mode
labels, actor/health getters and HUD event consumption have been traced; see
[NATIVE-RUNTIME.md](NATIVE-RUNTIME.md) for evidence and limitations. These are
not production-verified hooks. Heavy stuttering was reported; performance, input coexistence,
pointer lifetimes, actions and cross-mission stability remain unverified.

Performance update `1.6.17.29-crysis-performance` (versionCode 58) was subsequently
installed after the owner saved and closed the game. It adds Crysis-specific
FEXCore Performance selection and a verified-before-spawn affinity scope, not
native UI hooks. See [PERFORMANCE.md](PERFORMANCE.md); effective gameplay masks
and a performance improvement still require a fresh run.

The first launch of that update exposed an over-strict affinity check when
Android narrowed the requested mask. Correction `1.6.17.30-crysis-affinity`
(versionCode 59) was installed with the owner's supplied Crysis cover artwork.
It derives Wine's launch topology from the valid effective CPU subset; details
and regression evidence are in [PERFORMANCE.md](PERFORMANCE.md).

Native callback trial `1.6.17.31-crysis-native-trial` (versionCode 60) was installed
after the owner closed the game. The six-page UI is not enabled by this trial.
See [NATIVE-RUNTIME.md](NATIVE-RUNTIME.md) for package evidence, review limitations,
and the outstanding live verification gates.

## Current interface

The user requested a UI-enabled development build after live callback research.
The current source now includes a secondary-monitor-only native panel and an
explicit read-only trial data path. Health, suit state/energy and the verified
regular Single weapon/ammunition path can supply real values. Other fields
remain unavailable; no gameplay actions are enabled. This is not a completed
mod or proof of complete device UI/input/performance validation. APK62 is the
last confirmed installed build; new reader/rendering validation remains pending.

Six 1240×1080 pages: tactical map, weapons/loadout, Nanosuit/upgrades,
objectives/mission intel, inventory/resources, and status/statistics.
The latest requested layout has **no page buttons or sidebar**. Content uses
1192 pixels of width inside 24-pixel outer margins, versus 990 previously.

Swipe left/right to cycle through pages, including wraparound. Vertical swipes
scroll lists. When a calibrated map is available, dragging its body is reserved
for panning; header/footer swipes still change pages. Pinch math preserves the
anchor and clamps zoom to 1–8×. The six footer ticks are passive indicators,
not touch targets. No input is injected into the game.

Press/release interaction now retains the initial capability owner across fresh
frames. A weapon/module appearing under a held finger cannot receive the old
press. Dragging away and returning does not become a tap, and canceled/malformed
gestures cannot fire actions. Stable ownership may survive routine telemetry
updates; generation, phase, page and list-position changes still invalidate it.

Unknown data uses explicit unavailable states and hatched meters. No mockup
weapon quantities, mission names, enemy markers, suit modes, resources, or
statistics are supplied as game state. No action is optimistically applied.
The map renderer now supports bounded RGBA artwork, aspect-preserving pan/zoom,
player facing and visibility-filtered markers. It remains unavailable in the
disconnected preview: there is no verified live map/coordinate producer yet.
The suit page uses an original game schematic, explicitly labeled as static
artwork rather than a live 3D render. Weapons can display all 12 bounded
attachment slots across scrollable panels, six attachments per panel.

## Build and inspect

From the workspace root:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File pc/mods/Crysis3SecondScreen/build-dev.ps1
```

Outputs under `.work/crysis3/dev/`:

- `core-tests.exe`: lifecycle, freshness, capability ownership, layout, swipe,
  wraparound, pan/zoom math, mailbox, and malformed touch packet checks.
- `suit-capture-tests.exe` and `attachment-capture-tests.exe`: transactional
  clear/add/update capture, duplicate/capacity rejection, epoch/owner checks,
  expiry and preservation of the decompiled movie's default-entry semantics.
- `vitals-capture-tests.exe`: copied native-energy payload decoding, byte-sized
  tags, malformed/duplicate/epoch rejection, and separate energy/mode expiry.
  Synthetic tests only; no native event interception or adapter authorization.
- `map-capture-tests.exe`: raw map-movie transactions, background retention,
  reverse objective insertion order, secondary filtering, separate icon/objective
  freshness, ownership and bounded-input rejection. No world calibration inferred.
- `capture-selection-tests.exe`: exact process-name matching, path/case handling,
  and rejection of zero IDs or multiple matching processes.
- `ui-tests.exe`: image validation, map aspect/anchor, marker filtering, stale
  image removal, attachment continuation, hit regions and duplicate-action guards.
- `Crysis3RuntimeCapture.exe`: read-only research utility, described below.
- `Crysis3UiReview.exe --render DIRECTORY`: renders all six disconnected pages.
- `Crysis3UiReview.exe --window`: optional explicit desktop review window.
- `--window --touch`: additionally opens the existing IPv4 loopback protocol
  on port 7788. Do not run alongside another receiver using that port.

The harness reuses `DeadIslandSecondScreen/src/Canvas` and the existing
`DishonoredSecondScreen/src/Touch` parser/socket, without changing those mods.
The window is not topmost, does not request focus, and is not a deployed native
second-screen transport. No top-screen preservation claim has been runtime tested.

## Guarded native research probe

`build-probe.ps1` builds and tests `.work/crysis3/native-probe/dinput8.dll`.
It is separate from launcher packaging and has not been installed or verified
in gameplay. Forwarding is enabled by default; research hooks require an explicit
`Crysis3NativeProbe.ini` with `[Probe] Enabled=1`.

The probe requires exact executable/header/function fingerprints before replacing
two verified vtable slots (game update and system-event reception). It preserves
the observed calling conventions and copies bounded actor/suit diagnostics at
most ten times per second. File output is limited to once per second. Offline
tests check call forwarding, hash/read guards and disabled paths, not game-thread
ownership or checkpoint safety. Cached actor observations remain labeled
`adapterVerified=0`; they do not authorize UI values or gameplay actions.

## Acquired and inspected

ADB device: `100.83.63.70:41335` (AYN Thor; address may change).
Original download path: `/sdcard/Download/Crysis3/Crysis 3`.
Current launcher copy: `/sdcard/Dualscreen/Games/Crysis 3/Game`.
The original `%user%/Game.log` reports 32-bit version **1.2.1.1000**, STLport,
but is an incomplete startup log. The actual native log lives under Saved Games
in the Wine prefix; the updated diagnostics export it separately. The original
executable is 86,636,544 bytes with SHA-256:

`f332e2fb466ec8ce44336a4af06cd498bb5a787d7352548c76f2e4ec008f5e8e`

Original files are in `.research/crysis3/original`. The extractor located the
archive public key in that exact executable at file offset 75,570,360. Every
RSA-OAEP wrapped key, archive table, extracted length, and entry CRC was checked:

| Archive | Extracted entries | SHA-256 |
|---|---:|---|
| Scripts.pak | 865 | dc13eadb9778443885d481cfce60a6b00797275a44a3b679f18a39fb35b649de |
| GameData.pak | 3970 | 94006c268c2695545e39dc3c5a2740d7180a2e053e81a12a67c744b33d7a9ab2 |
| UI.pak | 510 | 1b4ac212532957b6a94626db9ff60aaf7607a39b7dd371fd9cfd96e22448f719 |
| patch1.pak | 3826 | 08e06ea600a582044ace09958bd8c46189b7335b8b7dffcc55ab1e86a555bb3c |
| English_xml.pak | 35 | d288fd8c329bc7e46f09e6a7c42e348b41070fc33e94b5e3158d8a176cbcbb9e |
| Jailbreak/level.pak | 123 | 7a6b1d2502c957202dcb3596983540f8dc44acbddfa24dcbb4f2d199509f2337 |
| Modes/menucommon_sp.pak | 80 | d1988911f32f4802ba39ec052ef2ecd2f7b3d2cda887d4bee3b938762b8218bb |

Archive signatures were **not** validated; checksums establish extraction
consistency, not vendor authenticity. Originals were not rewritten. The archive
extractor only supports this verified layout and refuses existing destinations.
89 binary XML files from patched scripts were decoded with table bounds,
parent/child, string and reachability validation into a separate research tree.

JPEXS 26.2.1 decompiled the original suit menu, patched weapon menu, SP map, and
3D HUD into `.work/crysis3/ui-decompiled/`. Original and patched definitions
remain separate; their presence does not prove which is active at runtime.

`tools/remote_pak.py` indexes archive tables and optionally pulls only explicitly
selected entries using read-only ADB ranges. Textures (600 MB), Objects (1.54 GB),
startup and Jailbreak's dds0 cache have been indexed without full downloads.
Selected extractions check length/CRC; their manifests explicitly do **not**
claim a verified full-archive hash or signature.

Jailbreak's menu metadata references `Minimap/Jailbreak.gfx` with world bounds
(-18.945923, 105.72681) to (501.05408, 625.72681). Its mission editor data instead
contains generic 1024-by-1024 bounds. Neither is adopted as a live projection.
The referenced SP artwork was not found in the checked UI, level, menu, patch,
texture, object, startup or dds0 archives; other archives remain unchecked.
Multiplayer map textures are not used as substitutes.

`tools/prepare_art.py` validates the original UI schematic against extraction
evidence and converts it into `assets/nanosuit.rgba` in the private build folder.
`tools/prepare_weapon_art.py` verifies the patched `WeaponIcons.gfx`, derives
frame numbers from named labels in its exported XML, and exports nine original
weapon/attachment/explosive silhouettes with hashes and provenance. Outputs are
under `.work/crysis3/verified-weapon-art`, not shipped or displayed as inventory.
Asset labels are not native item IDs or evidence of ownership.

## Verified static leads, not native hook contracts

| Domain | Evidence in extracted/decompiled files | Still needed |
|---|---|---|
| Health/energy/suit | `Scripts/HUD/HUDObjects_sp.xml` registers `HUD_EnergyAndSuitMode` with `OnEnergyChanged`, `OnHealthChanged`, `OnSuitModeChanged`, `OnSuitOverChargeChanged` | Native event payload ABI, thread ownership, current actor and lifecycle |
| Ammo | Same XML registers `HUD_Ammo`, `OnSetAmmoCount`, `OnSetInventoryAmmoCount`, selection/reload events; `BasicActor.lua` uses `actor:GetHealth` and `inventory:GetCurrentItemId` | Native sources, capacity/reserve semantics, weapon switching and reset |
| Attachments | Patched `Menus_WeaponCustomization.gfx`: `addAttachment_new`, `mountedAttachment_new`, `setEnableCustomisation`; outgoing `attach_primary` / `detach_primary` | Native command receiver, eligibility and owning item; do not invoke strings blindly |
| Modules | `SPPerksDef.xml` defines 16 modules, costs and MaximumMode events. Suit UI exports `addPerk`, `addLoadout`, `addPerkStat`; emits `toggle_perk`, `buy_perk`, `load_perkloadout`, `save_perkloadout` | Native receiver and progression state, ownership and purchase validation |
| Suit modes | Patched `nanosuit.xml` contains Energy, ArmorParams and movement parameters; module classes include Speed/Strength | Runtime state mapping. Module categories do not justify invented Power/Speed toggles |
| Map/objectives | `GeneralAndPC/map_sp.gfx`: `setMapBackground(path,rotation)`, `addIcon(x,y,rotation,scale,icon)`, `addObjective(...)`, `clearAll`, `updateIcons` | Native producer, level asset, coordinate units/calibration, discovery and visibility filtering |
| Resources | `PickableAmmo.lua` uses inventory ammo count/capacity; `NanosuitUpgrade.lua` uses real upgrade points and native `Used` event | Runtime inventory/counters; no generic tech/material/consumable system inferred |
| Statistics | SP module definitions reference real usage-stat names; patched `PersistantStatMapParams.xml` defines categories | Which counters are available, scope, units and live addresses |

The executable has no normal import directory, a nonstandard startup section,
and a `.text` section with approximately 8 bits/byte entropy. Its loader wrapper
was disassembled and passed through bounded Ghidra decompilation. That output
contains unresolved/overlapping instruction warnings and **is not reliable
gameplay decompilation**. No offsets or native hooks have been adopted from it.
Ghidra project: `%TEMP%/Crysis3BootstrapResearch/BootstrapAudit`; research log and
output: `.work/crysis3/ghidra.log`, `.work/crysis3/bootstrap-decompiled.txt`.

## Read-only runtime capture

Once this exact build has initialized, run the development capture executable
inside the **same Windows/Wine environment**, with a new output directory whose
parent already exists. Automatic selection requires exactly one Crysis process:

```text
Crysis3RuntimeCapture.exe --auto <NEW_OUTPUT_DIRECTORY>
```

Alternatively supply its Windows PID (not an Android/Linux PID):

```text
Crysis3RuntimeCapture.exe <WINDOWS_GAME_PID> <NEW_OUTPUT_DIRECTORY>
```

Process-name matching only selects a candidate; it never authorizes a capture.
No match exits with code 15, multiple matches with code 14, without creating an
output directory. The tool opens the process with query/read permissions, verifies the original or exact
permission-trial hash and checks loaded section layout against that disk image.
It derives `.text` and `.rdata` from the section table and reads each page twice.
Schema-2 output adds loaded PE headers, `.rdata`, the observed crash's `.data`
page, and a SHA-256-recorded VirtualQueryEx region/permission inventory. It does not inject,
suspend, request debug privileges, write
memory or modify files on the game installation. The copy loop has a 30-second
budget. Incomplete evidence is retained and marked unusable for hook contracts.
Even a complete copy is **not an atomic snapshot or proof of decrypted code**;
analysis and runtime cross-checks must follow. Compilation, recognized/unrecognized
image checks, synthetic process selection and real no-process/invalid-PID rejection
have been tested. A complete live capture was also acquired and audited during
Jailbreak on 2026-09-07. Its exact reconstructed-header fingerprint is separately
accepted, without relaxing support for arbitrary layouts; see NATIVE-RUNTIME.md.

`--probe-auto` performs the bounded CGame-root research capture. `--inspect-auto`
starts the read-only, low-priority diagnostic mailbox for at most 15 minutes,
128 requests and 4096 bytes per request. `stage_live_capture.py` invokes these
through the existing same-prefix WinHandler; `read_live_inspector.py` records
each requested address's evidence rationale and checks returned bytes/hashes.
These tools are not production telemetry and do not send game input.

After pulling a capture to the PC, validate it against the **matching** original
or permission-trial executable before inspecting its native code:

```powershell
python pc/mods/Crysis3SecondScreen/tools/audit_capture.py <CAPTURE_DIRECTORY> <MATCHING_EXE>
python -m unittest discover -s pc/mods/Crysis3SecondScreen/tests -p 'test_*.py' -v
```

The read-only auditor checks the recognized executable fingerprint, all binary
lengths/hashes, loaded PE section layout, declared capture locations, and complete
ordered memory-region coverage. It refuses partial captures, duplicate JSON keys,
altered data, build mismatches, and unsupported claims of verified hooks. Earlier
schema-2 captures without `memoryRegionsSha256` are also refused; preserve them as
diagnostic evidence and obtain a new capture rather than filling in missing fields.

Its JSON output reports disk/runtime text differences and entropy, plus the memory
protection covering the observed fault address. Differences are not proof of
decryption. Region permissions were sampled before the byte copies, not atomically
with them. Hash consistency does not authenticate a dump or establish hook safety.
The ten auditor tests use explicitly synthetic evidence; they are not game sessions.

The three UI capture classes are value-only offline components aligned with the
decompiled Flash transactions. They do not intercept calls, establish native
ABIs, authorize actions, or refresh old menu data as fresh game telemetry.

`MapCapture` preserves raw status values and movie coordinates rather than
guessing native enums or treating them as normalized world positions. It does
not load arbitrary paths, infer enemy visibility from icon names, or publish
captured menu data as live `Snapshot` state. It deliberately ignores the movie's
undeclared `_done` variable as a reliable source for the primary objective.

## Remaining implementation / release gates

1. Use the acquired live native-code/state evidence to resolve thread-safe
   update and lifecycle hooks, including mission changes and actor replacement.
2. Establish native readers and action receivers against decompiled runtime code,
   with version/byte checks and generation-scoped handles. UI strings are leads,
   never sufficient proof of native call signatures.
3. Connect the implemented map renderer, original art, attachment scrolling and
   transactional UI captures to verified native sources; resolve the actual SP
   map asset/projection, module operations and resource/statistic readers.
4. Add the actual native bottom-panel bootstrap/transport and launcher integration;
   preserve top viewport and input without unsafe fallback injection.
5. Verify every page, damage/ammo changes, pause/cutscene/loading/death, saves,
   checkpoints, multiple missions, action acknowledgement, stale frames, duplicate
   events, pointer lifetime, touch bounds, scaling and measured frame time.

Research prerequisites: existing Python 3.12 with pefile/PyCryptodome, local
MinGW32/64, JPEXS and Ghidra. `twofish_ctr.c` binds separately acquired Niels
Ferguson Twofish 0.3 code, stored under `.research/crysis3/tools/`; its source
license must accompany any redistribution. No game code/assets, archive keys,
third-party extractor implementation, or proprietary decompilation is in this
source directory.
