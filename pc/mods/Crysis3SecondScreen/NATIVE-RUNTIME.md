# Native runtime research: 2026-09-07

## Current status update: UI 62 installed, new gameplay test pending

After the owner confirmed Crysis closed, versionCode62 / versionName
`1.6.17.33-crysis-live-ui` installed successfully and was read back from Android
with lastUpdateTime `2026-09-07 21:14:05`. APK SHA256
`072f5c2fe8254a83943ec533a5697bf59c4fbbf231e09a460c9ec94fac0c62df`;
DLL SHA256 `6cac8c5f9afea2585d4c9356b7ed116b8807db061e67b4b23d6ca7051e275789`.
All four packaged files and the compatibility patch match their manifest,
1,587 other assets match APK61, and the existing release signing certificate
matches. Thirty Python tests and 34 targeted Android tests passed; native
callback, reader, capture, UI and completed-frame presentation suites passed.
The build log is `.work/crysis3/live-ui-apk-build.log`; the signed artifact is
`.work/crysis3/DualScreenLauncher-1.6.17.33-crysis-live-ui.apk`.
Play deploys the updated payload; APK installation alone does not prove these
new readers or the black-flash fix ran successfully in actual gameplay.

The historical trial descriptions below are not the current deployment status.
APK `1.6.17.32-crysis-readonly-ui` (61), initial UI variant, was installed at
device time 20:46:21 after the owner saved/closed. APK SHA256
`76f42d92d8211fe9883d85aade474c44fa26b9e349b6ec7beea3ef741129ef7d`.
The owner confirmed live health/energy and working swipes. Top gameplay stayed
visible in the saved capture. They reported random black flashes and unavailable
optional pages; no claim of complete UI or performance validation follows.

The newly installed revision uses one completed-frame copy in
WM_PAINT, eliminating the panel's visible black-clear intermediate. It also
removes the large branding/developer header, expands content and presents the
static Nanosuit bitmap wider/larger. Native/scaled presentation regression tests
pass, including retaining the previous frame on failed offscreen scaling.
Actual-device black-flash resolution still needs testing after installation.

Optional read-only producers now join the same accepted callback/actor/phase
scope: owned weapon names (33+27 fingerprints), suit modules and authoritative
upgrade points (16 fingerprints), and native objectives/level metadata (15
fingerprints). All optional fields are discarded on a failed final identity or
epoch check. The UI requests its visible page through an atomic enum; unrelated
optional readers do not poll off-page, and copies are refreshed at most 2.5 Hz.
No old optional values are re-timestamped. A page can wait for the next sample.
Objective scratch lives outside the callback stack and is cleared after use.
`ui-publication` flags record accepted paths and capture elapsed milliseconds.

Fresh independent C3R205200 captures replay Fields / WELCOME TO THE JUNGLE,
99 objective records (7 noninactive), active Reach the North Railyard, installed
Endurance/Deflection, and four authoritative upgrade points. The suit menu's
cached point value is zero, so unlock eligibility remains explicitly unknown.
These separately timed copies test decoding, not atomic runtime observations.
Native Bow uses clip_size=0; its zero magazine entries must not become an empty
arrow count. Generic names identify NanoBow without asserting ammo support.

Map art/markers, complete ammo/attachments, module progression, tracked combat
statistics, all game-changing touch actions, and multi-mission/checkpoint/input/
performance tests remain unfinished. The read-only safety gate stays enabled.
All helper agents were stopped at the owner's request; subsequent work is solo.

Research evidence, **not a release hook contract**. APK 60 now packages an
enabled, exact-profile native callback research trial for the next Crysis launch.
Its two vtable substitutions are diagnostic instrumentation, not verified
production telemetry. No native equipment actions or bottom-screen UI are enabled.
All addresses below are observed VAs in one process at image base `0x37000000`.
Heap addresses must never become fixed release offsets. The current launcher
remains diagnostic-only; the development UI is disconnected.

## Callback trial deployment

Version `1.6.17.31-crysis-native-trial` (60) was installed and read back through
ADB on 2026-09-07 at device time 20:26:08, while Crysis and Wine were closed.
APK SHA-256: `ce8427917ee42389ede470520b1ca0174a4e3302aced960be09db415c21beea7`.
All 22 targeted Android tests passed; 1,587 non-Crysis payload/component asset
entries match APK 59. Signing certificate and every Crysis manifest payload hash
were checked. The DLL and INI are deployed by Play; installation alone does not
prove the DLL loaded or either callback ran.

The trial uses Crysis3.exe-scoped `dinput8=native,builtin`, leaving the shared
prefix's other applications unchanged. Native DLL SHA-256:
`08fd1e5da39760c4a02b85551e47e363d11c36835309ad852b6209d50d5e3864`.
The forwarder passed a separate same-prefix identity/DirectInput-create/release
smoke test before the journal-only revision. Its non-game process was correctly
rejected by source hashing. Gameplay validation of APK 60 subsequently ran in
session C3R202900: exact-profile callbacks installed and sampled on Windows
thread252 with no observed update-thread mismatches. System callbacks also ran
on thread336; raw system events are not all lifecycle changes. The user confirmed
gameplay, pause/resume, firing/reload and walking/turning. Corresponding bounded
reads and two saved native logs are under `.work/crysis3/live-202900`. This is not
multi-mission or controller/touch coexistence validation of a UI payload.

Review limitations: cached-actor `consistent=1` is not authoritative identity or
phase proof. Nested/concurrent system events currently log the completion-time
epoch, not a unique entry epoch. The update hook has no reentrancy guard and
latches the first callback thread; thread migration drops observations. These
records must not authorize a Snapshot, action, or production lifecycle model.

## UI-enabled development build (not yet runtime validated)

The next payload uses the authoritative double-read actor/phase readers with all
21 contract fingerprints, plus 33 weapon code/vtable fingerprints. Callback
guards reject nested sampling, record immutable event-entry observation epochs,
and disarm on thread conflict. Shared actor/context and phase identities are
rechecked across weapon reads; inventory/current-item/system identities are
rechecked after ammunition sampling. These safeguards still do not establish
complete checkpoint/cutscene lifetime safety or a production adapter.

The user explicitly requested enabling the UI before completion. The new
`readOnlyTrial` Snapshot flag permits only fresh, phase-qualified display copies;
it never sets `adapterVerified`, and `CanAct` unconditionally rejects all trial
actions. The panel independently strips capabilities and discards commands.
Known actor/suit fields and supported regular-Single weapon/ammo fields feed
four pages. Map, objectives, upgrades and other unknown fields remain unavailable.
No observed heap address is a compiled runtime constant.

The native panel owns a nonactivating, nonoverlapping secondary window, starts
on Player Status, uses the existing loopback touch protocol, and caps its pump
at 10 Hz. Android recognizes only its exact private class/title and suppresses
X11 input mirroring even before claiming. The game remains on the primary
monitor; there is no single-monitor overlay fallback. Actual device rendering,
swipes, focus, frame time and transitions still require a fresh launch test.

## Capture provenance

Owner-authorized live Jailbreak gameplay, Windows PID 248 / Linux PID 21812.
Full evidence: `.work/crysis3/live-190400/capture`; auditor: sibling `audit.json`.
Capture finished in 8,110 ms using read-only process access and matching page
reads, without suspending the game. It is not an atomic snapshot.

| Evidence | SHA-256 |
|---|---|
| Patched source EXE | `fffd52e0c483b55f04e0048dff0c45dd445734cfcf6470aa3d84fbf84040e055` |
| Loaded headers, 1,024 bytes | `fb530657a66ea51a6a7495c36085e0c856d6daccffbacc1db8c037d48c55e25d` |
| Loaded text, 42,457,088 bytes | `e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f` |
| Loaded rdata, 2,885,632 bytes | `376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe` |
| Observed crash-address page | `6d977c4e29755c57cbad9a123e071d0bf8ef5ecac6cde397db1875f2c7215846` |

The loader reconstructed five PE sections and SizeOfImage `0x52a1000`; the disk
image/module extent is `0x52a3000` with six sections. The exact captured header
fingerprint is the only alternate layout accepted. The five surviving virtual
section ranges still match disk. This is not a blanket layout-check bypass.
42,291,452 text bytes differ from disk; captured text entropy is 6.74143 versus
approximately 8 on disk. Coherent native functions were subsequently inspected,
but these metrics alone do not establish decryption or safe hooking.

The prior execute-fault VA `0x39c169ce` is now in committed PAGE_EXECUTE_READ
memory in the saved region inventory. This supports the permission trial's
specific effect, not a guarantee against other crashes. The user retracted the
later crash alarm. Heavy stuttering was reported; no claim of normal performance.

Ghidra: `%TEMP%/Crysis3LiveResearch/Live190400`, raw text imported at `0x37001000`.
`tools/CrysisLiveAudit.java` checks source evidence hashes before adding blocks.
Generated pseudocode contains inferred calling conventions and occasional bad
stack argument recovery: check instructions, not just pseudocode signatures.
General `.data` contents were not imported; unresolved globals are not zero.

## Actor and health evidence

`HUD_EnergyAndSuitMode` update `0x382e0030` references global slot `0x3b9fde30`.
Root probe `.work/crysis3/live-190500/capture` observed CGame `0x3b9f99b0`,
vtable `0x399bef80`. Virtual `+0x5c` resolves to `0x3802f430`, returning `[this+0x30]`.
That framework pointer was `0x3b8516a0`, vtable `0x398ba5e0`.
Framework virtual `+0x1a0` is `0x3749e9b0`, taking context `[this+0x28]`
(`0x3350e230`) into `0x37486e90`.

That client-actor getter obtains the entity ID from the channel and validates a
cache at context `+0x8b0/+0x8b4`. An alternate branch and forced-refresh global
remain to be fully resolved; production must not blindly use the cache.
Inspector reads 2/3 and final read 20 observed entity `0x7777`, actor `0x4e8a7770`,
actor vtable `0x399b5940`. Actor `+8` agrees with the entity ID.

Lua `GetHealth` registration at `0x380bd7d7` identifies `0x380ba930`, which
dispatches actor virtual `+0x64`. The actual target `0x3804d9c0` returns zero
when `[actor+0x28]` is zero; otherwise it reinterprets
`uint32(actor+0x24) XOR uint32(actor+0x28)` as a float. Read 3 yields 1000.
Lua `GetMaxHealth` registration at `0x380bd801` identifies `0x380ba9f0`, actor
virtual **+0x70**, target `0x370d8f00`, returning float `[actor+0x2c]`: also 1000.
Virtual +0x68 is a different XOR integer getter, used by the HUD, not max health.
Health-change/damage behavior has not yet been compared against a controlled
live change; these findings do not authorize a health hook.

## Suit owner and cloak/recharge comparison

Actor virtual `+0x1d4` resolves to `0x380d3360`: return `[actor+0xfc0]+0x30`
or a static fallback if absent. Actor initialization at `0x380e31f7` allocates
`0x268` bytes and calls suit constructor `0x3816c390`. Read 4 found suit owner
`0x4e25f638`; read 10 confirmed its actor back-reference at `+0x18`.

Energy setter `0x381681a0` accesses `owner+0x88+channel*0x70`. Selector at
owner `+0x1b8` was 0. The candidate maximum at `owner+0xc8+channel*0x70` was
100 and agrees with parsed maximumEnergy configuration, but maximum handling
and alternate-channel semantics need further review. Never hardcode 100 as max.

Inspector evidence: `.work/crysis3/live-191000/read8` through `read11`.
The owner said they enabled cloak, then completed the requested off/recharge step.

| Read | Helper elapsed ms | Energy channel 0 | Candidate maximum |
|---|---:|---:|---:|
| 8 | 577345 | 100 | 100 |
| 9 | 584965 | approximately 89.2461 | 100 |
| 10 | 594195 | 100 | 100 |
| 11 | 603606 | 100 | 100 |

These sparse samples correlate depletion/recharge with the user's action; they
are not frame-aligned, an observed event capture, or proof that movement had no
effect. Sampled mode remained 0, so a live 0-to-2 transition was not captured.
Read 20 still found the same context actor/cache identity. No transition/lifetime
guarantee follows from equal pointers before and after.

## Native events

Setter `0x381681a0` calls `0x38168010` on value changes, walking the listener
array `[owner+0x1f4, owner+0x1f8)` and invoking virtual +8 with energy/channel.
Read 7 found begin/end `0x0545f900/0x0545f91c`. Reads 12-19 resolved all seven
listener objects. Listener 3 (`0x02550524`, vtable `0x399f3108`) dispatches
energy to **`0x382c2f70`**. This builds event ID **60**, float type byte 2,
then integer channel type byte 1, and calls dispatcher **`0x382c2100`**.
Assembly reads float at entry stack +4 and channel +8, ending in `ret 8`.
Its `this` is unused; do not trust the initially inferred Ghidra prototype.

Event constructor `0x37f719d0` uses one of ten global pooled payloads. Event
`+4` points to a pool record: count at +4, entries from +8 with stride 8
(`value DWORD`, `type byte` plus three uninitialized bytes). Energy emits exactly
two entries. Event wrapper is stack-local and the pool is released immediately
after synchronous dispatch. A future callback must copy bounded values before
returning; neither wrapper nor payload pointers may cross into the UI thread.
No worker-thread engine calls or listener-list mutations during dispatch.

The same listener's mode virtual +4 is `0x382c2e70`, which publishes event 59,
placing the mode integer at event +0x0c and selecting these actual string literals:
0 = Tactical, 1 = Armor, 2 = Stealth. This is code evidence, not a completed
runtime test of all mode transitions or permission to call the mode setter.

HUD consumer primary vtable `0x399f6028`, callback `0x382e0b90`; the interface
thunk at `0x382e11f0` adjusts this by -4. The consumer handles energy 60 via
`0x382dfc20` and mode 59 via `0x382dfb00`.

**Event-table trap:** registration assembly stores the preceding record's ID
before constructing the next name. Do not associate that preceding store with
the new name. Read 6 confirms name storage +4: OnHealthChanged=28,
OnSuitModeChanged=59, OnEnergyChanged=60. Other event IDs need their own checks.

Dispatcher `0x382c2100` uses global enable `0x3b81fef8`, per-event listener
vectors at `[0x3ba03f44]+eventId*12`, and synchronous virtual +4 calls.
It changes `0x3b81fef9` around dispatch. Subscription `0x382c2280` takes a listener
and event-name pointer, resolves the name through `0x382c20b0`, and inserts only
if that listener is absent. Removal `0x382c21b0` removes that pointer from every
event vector. Both silently return while the dispatch flag is zero; a callback
cannot unregister itself and assume removal succeeded. Unknown-name registration
logs an error but continues with ID 0, so callers must reject unknown names first.

Bulk listener cleanup `0x382c2230` is called at `0x3818a6a5` from system-event
handler `0x3818a2c0`, case `0x1b`. This is a concrete invalidation path, not yet a
verified symbolic event name. Dispatcher-vector initialization `0x382c2310` is
called at `0x3818b6cd` within the framework-accepting initialization method
`0x3818b490` (root vtable slot +0x50). Correction: the nearby `0x3818a7d0`
vtable assignments belong to destruction; its deleting-destructor wrapper at
`0x3818eed0` confirms this. Padding proximity alone is not a function boundary.
Execution thread ownership and the full level/checkpoint
event sequence still need verification before any hook is installed.

`src/VitalsCapture.h` implements **offline, value-only** decoding and independent
field expiry with synthetic tests. It installs nothing and never marks a
Snapshot adapter verified. A future adapter must establish identity/lifecycle,
initial state and unchanged-state validity, phase, maximums and thread ownership.
Mode labels do not justify invented Power/Speed toggles.

## Next native leads and release gates

- Resolve dispatcher subscription/removal and main-thread/level lifecycle.
- Confirm health damage, mode transitions, energy maximums and channel meaning.
- Inventory Lua GetCurrentItemId registration `0x37287e63` points at
  `0x37287890`; ownership and virtual +0xcc dispatch require review.
- Attachment command strings reach dispatcher around `0x383bac30` and
  `0x383bad29`; eligibility/item ownership and real call ABIs are unverified.
- Suit-menu module commands reach `0x383b22a0`, `0x38592dd0` and eligibility
  routines around `0x38592b60`; these are leads, not callable contracts.
- Map projection/asset, mission objectives, real resources/statistics, bottom
  transport, actual touch actions, and multi-mission/checkpoint testing remain.

The low-priority inspector is bounded to 15 minutes, 128 requests, and 4096 bytes
per request, read-only. It neither binds a socket nor sends game input. Staging
uses the launcher's existing WinHandler EXEC command in the same Wine prefix.
Its mailbox helper is research tooling, not the intended telemetry transport.
