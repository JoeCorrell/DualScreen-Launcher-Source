# Fallout 4 dual-screen technical investigation

Last updated: 2026-08-30

## Scope and rules

- Interoperability work only; no DRM bypass and no redistribution of Bethesda
  binaries or assets.
- Every binary-dependent path must fail closed on an unknown runtime.
- The lower renderer must own an independent window and lifecycle. It must not
  change Fallout 4's camera, framebuffer, menu state, focus, or time state.
- Engine mutations run only on Fallout's game thread through the exact-runtime
  native action pump. UI and snapshot threads never dereference or mutate game
  objects.
- Findings below are labelled `Verified`, `Supported`, or `Unknown`.

## Device and installation

| Finding | Evidence | Status |
|---|---|---|
| Device | ADB serial `100.83.63.70:34023`; model `AYN Thor`; ABI `arm64-v8a` | Verified |
| Game root | `/storage/emulated/0/Download/Fallout-4-SteamRIP.com/Fallout 4` | Verified |
| Preferences | `<game root>/Fallout4/Fallout4Prefs.ini` | Verified |
| Runtime | `Fallout4.exe`, PE32+ x64, file/product version `1.11.240.0`, 55,343,016 bytes | Verified |
| Active EXE hash | SHA-256 `061C1AB3738612DBC46D748F02C27795A1003501C90C15D1E649D8A07FCBBB4E` | Verified |
| Signed backup | `Fallout4.rne`, same version/size, SHA-256 `FDCEF37AC1230AF6D0B0050EB2142B139EF3A867B37B9211FB6EDFCC646072F8`; Authenticode valid | Verified |
| Active signature | Bethesda certificate is present but Authenticode reports `HashMismatch` | Verified |
| Content | Base ESM, all six official DLC ESMs/archives, Creation Club manifest/content | Verified |
| Native extender | No `f4se*` files under the game root at reconnaissance time | Verified |
| Display 0 | Android internal 1080x1920, landscape override 1920x1080, 60/120 Hz | Verified |
| Display 4 | `Screen-2`, 1080x1240, landscape override 1240x1080, 60/120 Hz, presentation/touch external | Verified |

WEMU uses container 2 (`Games`) with prefix
`/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine`. Shared storage is
mapped as `F:`, making the game root
`F:\Download\Fallout-4-SteamRIP.com\Fallout 4`. The runtime uses the WEMU
Proton ARM64EC/FEX path and DXVK 3.0 gplasync. The Wine configuration already
has the global `dxgi=native,builtin` override required by a game-local proxy;
no Fallout-specific override or proxy was present during reconnaissance.

The verified display contract is a 1280x720 primary gameplay surface and a
1240x1080 lower surface. At the last preflight, the shared prefix was occupied
by `Fallout3.exe` and `wineserver`; deployment correctly failed closed and made
no device changes.

A read-only capture of the target prefix's system `dxgi.dll` is 5,115,904
bytes with SHA-256
`F15ADA76D7848D896550350AFB826B9F6936E76310B5FEF09B9BD7E8F8344E7F`.
Its named exports are `CreateDXGIFactory` (ordinal 9),
`CreateDXGIFactory1` (10), `CreateDXGIFactory2` (11),
`DXGIDeclareAdapterRemovalSupport` (16), and `DXGIGetDebugInterface1` (17).
The installed Fallout executable imports only `CreateDXGIFactory` by name.
The proxy now matches all five target names and ordinals; additional modern
DXGI compatibility exports use non-conflicting ordinals.

The package also contains replacement/emulation loader components. This mod
will not read, modify, package, depend on, or document how to use those
components. Binary investigation uses the valid Bethesda-signed 1.11.240
layout where code identity matters.

## Relevant installed configuration

The game-directory source `Fallout4Prefs.ini` selects a borderless 1280x720
game window and originally contains:

```ini
[General]
bPipboyCompanionEnabled=0

[Display]
uPipboyTargetWidth=876
uPipboyTargetHeight=700

[Pipboy]
fPipboyEffectColorR=0.0800
fPipboyEffectColorG=1.0000
fPipboyEffectColorB=0.0900
```

The production bridge does not require `bPipboyCompanionEnabled` and the
deployment script no longer edits either preference copy. Companion TCP is
not started by the installed mod.

No loose `Data/Interface`, `Data/Scripts`, or `Data/F4SE` files were present.
The vanilla UI is held in `Data/Fallout4 - Interface.ba2`; scripts and other
records are archive-backed. Archive contents will only be inspected locally
for interoperability and will not be copied into this project.

## Selected architecture

### 1. Production state bridge: native Pip-Boy data groups

The exact 1.11.240 runtime constructs a native aggregate at singleton pointer
RVA `0x030E8AF8`. Its eleven `PipboyDataGroup` objects and aggregate pointer
array are:

| Branch | Group offset | 1.11.240 vtable RVA |
|---|---:|---:|
| Stats | `0x0A0` | `0x02546398` |
| Special | `0x1D8` | `0x02546208` |
| Perks | `0x280` | `0x02545AA8` |
| Inventory | `0x358` | `0x02544750` |
| Quests | `0x4F0` | `0x02545C40` |
| Workshop | `0x5D8` | `0x02546868` |
| Log | `0x718` | `0x025449A8` |
| Map | `0x7F0` | `0x02545600` |
| Radio | `0xA28` | `0x025468C8` |
| PlayerInfo | `0xAD8` | `0x02545DE0` |
| Status | `0xBE0` | `0x02546668` |

Each group owns a `CRITICAL_SECTION` at `+0x60`, throttle manager at `+0x88`,
and root `PipboyValue*` at `+0x90`. The aggregate pointer array at
`+0xCC0..+0xD10` uses the table order above. Runtime type canaries use exact
1.11.240 vtables for object/array/bool/integer/float/fixed/dynamic/localized
string values before any node is copied. The dynamic Stats, Map, and
PlayerInfo branches also contain `PipboyPrimitiveThrottledValue<float>`,
`<unsigned char>`, and `<bool>` at primary-vtable RVAs `0x02545730`,
`0x02545778`, and `0x025457C0`; treating these valid nodes as unknown was the
root cause of those three entire domains remaining unavailable.

Normal loaded-game lifecycle populates all eleven branches without the
companion server and without opening a menu. The confirmed path is
`AE7130 -> B3DAD0` (clear all) `-> B3DB10` (virtual `Populate` on all groups).
Calls at `C01222`, `2D45D0`, and `2D4661` cover normal runtime/load/session
paths. Companion serialization is a separate path through `B3DC00`; it is not
the producer of the tree. The bridge therefore observes engine population and
never invokes `Populate` from its worker thread.

Native value layout is `id +0x08`, dirty flags `+0x0C/+0x0D`, parent `+0x10`,
and primitive payload `+0x18`. Arrays store pointer/count at `+0x18/+0x28`.
Objects use the `BSTHashMap` capacity/entry array at `+0x24/+0x40`, with
24-byte key/value/next entries. (`+0x38` is allocator padding, not the entry
pointer; reading it was a confirmed traversal blocker.) All copies happen under the owning group lock,
with bounded depth, node counts, collection sizes, strings, readable-page
checks, duplicate-ID rejection, and vtable validation.

The exact group event-source registration wrappers at RVAs `0x00B3DBA0` and
`0x00B3DBD0` feed one atomic dirty flag per branch. Captures are now primarily
event-driven and coalesced by domain (100 ms vitals, 250 ms map/inventory,
500 ms quests/radio, 1 s static data). Integrity audits repair missed
notifications at 10 seconds for small dynamic groups, 60 seconds for ordinary
lists, and five minutes for the multi-thousand-node Perks, Workshop, and Log
groups. Only changed native records are encoded into the existing bounded
database. No whole-process scan is performed.

The installed 330,776,576-byte `Fallout4.esm` was parsed locally by record and
subrecord boundaries (not searched by display text alone). Its authoritative
base `AVIF` records are `Experience` FormID `0x000002C9`, `Health`
`0x000002D4`, `ActionPoints` `0x000002D5`, and `Rads` `0x000002E1`. Official
matching F4SE source identifies the player singleton at RVA `0x032DD370`, the
`ActorValueOwner` subobject at player `+0x58`, and `LookupFormByID` at RVA
`0x00311B80`. These provide a precise path for absolute XP/radiation sampling,
but calls will only be added through the verified game-thread task dispatcher;
the snapshot worker will not invoke actor-value virtuals off-thread.

### 2. Retired diagnostic option: built-in companion transport

Fallout 4 already builds a live, incremental data tree for the official
Pip-Boy companion connection. It is disabled in the current preferences and
must be enabled transactionally for runtime validation. The service uses TCP
27000 and sends framed, typed, incremental updates. This is not the user
interface: the mod will implement its own renderer, navigation, caches, and
input routing.

Verified from protocol implementations and sample data generated from the game:

- `PlayerInfo`: current/max HP and AP, carry/max weight, caps, level, XP
  progress, damage/resistance summaries, perk points, time/date, player name.
- `Stats`: limb condition and flags, Stimpak/RadAway counts, active effects.
- `Special` and `Perks`: names, values/modifiers/ranks/descriptions.
- `Inventory`: versioned category arrays, localized item names, quantities,
  form IDs, handles, stack IDs, equip/favorite state, item-card statistics,
  components, power-armor state, and sort/filter data.
- `Quests`: actual quest/objective/tracking records.
- `Map`: current cell/worldspace, world/local extents, player position and
  rotation, locations/discovery/cleared state, quests, custom marker, doors,
  and power armor. Local map raster updates are a separate message type.
- `Radio`: station name, frequency, range, and active state.
- `Workshop`, `Status`, and `Log` branches.
- Action requests include use/equip item, drop, favorite, component tagging,
  quest tracking, custom markers, fast travel, radio selection, inventory
  sorting, and local-map requests.

This transport remains useful as a schema/action research reference, but it is
not used by the installed build. The user correctly identified that requiring
the companion preference/service would be an unnecessary production
dependency once the in-process data groups were available.

No companion preference, socket, client, or local-map request is created by the
production mod. The retained parser and protocol tests are research fixtures,
not a runtime fallback or requirement.

### 3. Optional F4SE research artifact

The official F4SE site lists build 0.7.9 for Fallout runtime 1.11.240. The
installed game currently has no F4SE. A host-tested artifact remains useful for
future lifecycle research, but the current production mod does not load or
require it.

- explicit pre-load/post-load/new-game/data-loaded lifecycle comparison;
- narrowly scoped event-sink and diagnostics experiments.

It does not render, copy vanilla Scaleform, provide production state, or own an
action path. Future runtimes require new signatures and layout canaries rather
than assuming that the 1.11.240 native implementation is compatible.

### 4. Exact-runtime native game-thread actions

The official F4SE 1.11.240 source and matching executable show that RVA
`0x00C45150` is the game's `MessageQueueProcessTask` pump, not an AddTask API.
When and only when `ActionsEnabled=1`, the proxy installs a private seven-byte
detour after the executable SHA-256 and exact prologue
`48 8B C4 48 89 58 08` match. With `ActionsEnabled=0`, startup does not
inspect, allocate a trampoline for, suspend threads for, or patch the game
pump. The shipped exact-runtime configuration now enables the path, which
refuses an existing hook,
requires every native target to be executable, allocates a near W^X trampoline
and relay, suspends existing threads for a checked patch transaction, relocates
only instruction-boundary PCs, and flushes the instruction cache. Failure leaves
the renderer and state bridge read-only.

The panel submits bounded requests to a 64-entry queue. The game-thread pump
processes at most 16 per invocation and publishes a reserved completion record:

- inventory use/equip/unequip validates the live Inventory.Version, handle and
  stack, then calls the same underlying routine used by `UseItemCommand` at RVA
  `0x00AE0CF0`;
- quest track/untrack re-resolves `(formID, instance, type)` and uses the engine
  helper at RVA `0x00B707A0`;
- radio tune/off re-resolves the live station node through RVA `0x00B709F0` and
  follows the game's `BDAE00`/player-radio update path.

`ActionsEnabled` is the first gate and also suppresses UI command submission.
The shipped build sets it to one; every remaining hash, address, prologue,
thread-transaction, lifecycle, identity, and engine-result gate remains
fail-closed. Fast travel is not implemented.

### 5. Independent lower renderer and input

Use the proven WEMU dual-monitor contract:

- enumerate monitors and create a hidden-in-final-position `WS_POPUP` on the
  non-primary monitor;
- use `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` and every move/show operation must
  preserve no-activate semantics;
- Phase 3 starts with a low-rate CPU/GDI diagnostic surface; the final path may
  use an independent D3D11 device/swap chain after profiling;
- never use Fallout's gameplay swap chain, camera, render targets, or menu;
- bind touch only to IPv4 loopback UDP `127.0.0.1:7788`, validate normalized
  gestures, and never synthesize mouse input into the gameplay HWND;
- controller navigation requires an explicit UI-capture chord/mode so normal
  movement and combat controls remain untouched.

## Data synchronization model

- Eleven native group event sinks set only atomic dirty bits.
- A worker performs bounded copies while holding the owning group lock, then
  publishes immutable per-domain snapshots for the renderer.
- Coalescing intervals are 100 ms for vitals, 250 ms for map/inventory, 500 ms
  for quests/radio, and one second for static data. Integrity audits range from
  ten seconds for dynamic groups to five minutes for large static branches.
- Inventory/quest/map/radio lists rebuild only when their native branch changes.
- Commands carry a monotonic token plus current engine identifiers and expected
  inventory version; the game-thread executor revalidates all mutable identity.
- Logs are edge-triggered/rate-limited and include state revisions, rendering
  timing, queueing, game-thread result codes, and confirmation timeouts.

## Implemented foundation (static/host verified)

- Exact runtime gate for the two inspected 1.11.240 executables; unknown
  Fallout runtimes do not start mod services.
- Transparent x64 DXGI proxy matching the target Wine DXGI name/ordinal
  contract, plus modern compatibility exports and the six-argument
  `DXGID3D10CreateDevice` ABI. Pointer-shaped layered-device arguments remain
  pointer-sized on x64; compile-time ABI assertions and an automatically run
  factory-forwarding probe guard these contracts.
- Bounded 16 MiB protocol frames, 250,000 nodes, 256 KiB strings, a 96 MiB
  retained-node budget, atomic malformed/duplicate-ID rejection, reference
  validation, two-second partial-frame socket deadlines,
  heartbeat/busy/reconnect handling, and epoch-checked stale action invalidation
  immediately before send.
- Per-domain parent graph and revision caches for STAT, inventory, DATA, map,
  and radio; unchanged lists are retained rather than rebuilt at the panel
  refresh rate.
- Local-map raster bounds/finite checks, DWORD-aligned 8-bit DIB rows, cached
  raster revisions, affine player-position projection, and rate-limited logs.
- Independent `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` lower window; touch accepts
  only normalized loopback UDP gestures and never synthesizes gameplay input.
- Exact-runtime native request builders for inventory
  `(HandleID, StackID, Inventory.Version)`, quest
  `(formID, instance, type)`, and radio node identity. Execution is fail-closed
  behind the `ActionsEnabled` gate, native-pump readiness, and authoritative Status
  flags. `ActionsEnabled=0` remains an explicit read-only fallback; the shipped
  exact-runtime config enables the fully gated pump.
- Exact-runtime F4SE lifecycle plugin plus a defensive DXGI-side consumer. Both
  plugin and consumer have separate malformed/absent/recovery host tests.
- Transactional ADB deployment with executable hash gate, active-prefix gate,
  remote hash verification, profile preference backup, and verified rollback.
  A generated owner manifest binds the deployed DLL and config hashes; upgrades
  are refused if any remote file is missing, unreadable, symlinked, or no longer
  matches that manifest.

These are implementation facts, not in-game proof. Every row in
`PROOF_MATRIX.md` remains pending until Fallout 4 is launched into a save.

## Device deployment and first startup trace (2026-08-30)

- Clean installation completed at
  `/storage/emulated/0/Download/Fallout-4-SteamRIP.com/Fallout 4` only after the
  shared Games prefix became idle. Published hashes and the active preference
  edit were independently re-read from the device.
- The first launcher handoff created the expected
  `F:\\Download\\Fallout-4-SteamRIP.com\\Fallout 4\\Fallout4.exe` process.
  It exited within seconds and returned to the WEMU desktop. No
  `Fallout4SecondScreen.log` was created, proving the failure occurred before
  the original proxy reached its first successful DXGI forward/bootstrap log.
- No Fallout 4 crash dump or game-specific error log was created in the active
  prefix during that attempt. Android logcat showed WEMU client churn but no
  actionable native crash record.
- A controlled diagnostic build proved the system DXGI loaded at a distinct
  base from the proxy, `CreateDXGIFactory` was entered, and the bootstrap
  thread started. The real factory call never returned and the process exited;
  no runtime identity or factory-exit line followed. This rules out proxy
  self-aliasing and places the failure inside the WEMU/DXVK factory chainload,
  before companion, panel, or input initialization.
- Under the Box64 configuration, the proxy (device SHA-256
  `044b365942d23a34e6cd2a069edf4bddafd01314f26028be9eda7b6230a05bc3`)
  was initially moved, not deleted, to the timestamped deployment backup after
  the prefix became idle.
- The user established that Fallout 4 itself runs when the Games container uses
  FEXCore/`PERFORMANCE` instead of Box64. With the prefix idle, the exact owned
  proxy was restored from that backup and its hash re-verified. A subsequent
  deployment dry-run passed the executable, ownership-manifest, DLL, config,
  profile, and active-prefix gates. The game root therefore contains the active
  proxy again, now targeted at the confirmed FEXCore configuration.
- The diagnostic service bootstrap is now also linked into the exact-runtime
  F4SE plugin. This avoids interposing on Fallout's graphics loader entirely;
  the plugin retains exact 1.11.240/0.7.9 gates and still leaves mutation
  actions disabled. Official F4SE installation and in-game proof are pending.
- `build/tools/ProxyRuntimeProbe.exe` is a source-built, non-packaged diagnostic
  that can separately test Wine DLL loading, export lookup, and the real
  `CreateDXGIFactory` call if the early trace still cannot be reached.

### FEXCore startup crash root cause and resolution (2026-08-30)

- The reordered proxy proved that real DXGI factory creation completed under
  FEXCore, but the process still stopped immediately after
  `[INIT] bootstrap thread entered`. Disassembly then identified a deterministic
  stack overflow in `Runtime.cpp::Sha256`: its local read buffer produced a
  `0x100080`-byte stack frame, while `Fallout4.exe` reserves exactly
  `0x100000` bytes for a default worker thread.
- The SHA-256 read buffer now uses a bounded 64 KiB heap allocation. Both build
  scripts reject future frames larger than 64 KiB, and the proxy regression
  executable is linked with Fallout 4's exact 1 MiB stack reserve.
- `DisableThreadLibraryCalls` was removed from both DLL entry points because the
  statically linked C++ runtime and threading support require normal TLS thread
  notifications. Bootstrap and panel thread entries also have exception
  boundaries so ordinary C++ failures degrade to logged initialization errors.
- The exact-stack proxy probe completed system-DXGI loading, factory creation,
  executable hashing, and transparent non-Fallout return. The installed DLL
  SHA-256 is
  `621591f5b2d74dbf7b6e6f3385ff23b93a818a4af0ec7d93c09c650ce9ba6436`.
- Historical device proof at 01:08:42 showed factory success, runtime identity
  `1.11.240.0` with the expected executable SHA-256, configuration load,
  the then-present companion client startup, isolated touch binding, and
  lower-panel creation at
  `0,720 1240x1080`. Fallout4.exe remained running at the title screen. The
  immediate launch crash is therefore resolved.
- That historical build logged companion socket error `10061`. The production
  client and dependency have since been removed; loaded-save native-state proof
  remains the current gate.

## Fallout 3 UI parity port (2026-08-30)

- The source of truth is the custom GDI renderer in the sibling
  `Fallout3SecondScreen/src/Panel.cpp`. That project contains no external UI,
  image, font, or game-asset files; copying the interface does not copy
  Bethesda content.
- The Fallout 4 renderer preserves the source's logical 1240x1080 canvas,
  five exact phosphor colors, four Lucida Console sizes, glow offset, 18 CRT
  edge strokes, five-pixel scanline cadence, brackets, meters, every page/row
  coordinate, three primary modes, mode-specific subpages, and 230 ms
  smoothstep transition layers.
- A bounded `UiSnapshot` adapter translates Fallout 4's native group model
  without importing Fallout 3 hooks, offsets, proxy code, or x86 ABI. Stable
  node IDs select records; action dispatch resolves them back to current
  handles, stack IDs, quest instances, radio nodes, and inventory versions.
- Missing fields are never invented. Canonical radiation and absolute XP remain
  unavailable; XP progress is a real 0-100 value. Damage/resistance arrays,
  SPECIAL modifiers, perks/ranks/descriptions, effects, notes, magazines, and
  holotapes now come from authoritative native branches.
- Native local/world marker records and player transform drive both map pages.
  The old companion-only local raster request is not issued; raster generation
  remains an explicit later investigation rather than a hidden dependency.
- The native gesture path accepts exact bounded packets only, rejects embedded
  NULs/non-finite/out-of-range/trailing input and non-loopback senders, performs
  horizontal/vertical direction locking only on terminal UP, remembers whether
  a drag ever left tap slop so a loop back to its origin cannot activate a
  control, and reserves the center-bottom WEMU console-switch strip. Packet
  coordinates are mapped into the actual aspect-fitted content rectangle before
  classification; a sequence beginning in a letterbox bar is discarded through
  release. Windows mouse messages remain eaten by `MA_NOACTIVATEANDEAT`; no
  mouse fallback or game input injection was copied.
- Haptic replies retain the persistent Android touch socket as their peer.
  Select-modified controller packets arrive from one-shot UDP sockets and no
  longer overwrite that peer with an endpoint which closes immediately after
  sending; malformed loopback traffic is likewise never accepted as a feedback
  destination.
- The F4SE build is now bridge-only. It no longer links the service host, so an
  eventual F4SE installation cannot compete with the DXGI host for companion
  port 27000, touch port 7788, or the lower window.

## Fallout 4 Pip-Boy artwork pipeline (2026-08-30)

- The installed `Fallout4 - Interface.ba2` is BTDX v8/GNRL with 1,101 records.
  The relevant SWFs are stored without BA2-level compression and remain CWS
  vector movies internally.
- SPECIAL selection 0..6 maps exactly to
  `Interface\Components\VaultBoys\SPECIAL\{Strength,Perception,Endurance,
  Charisma,Intelligence,Agility,Luck}.swf`.
- Each live SPECIAL entry supplies lowercase `text`, `value`, `modifier`, and
  `description`. Vanilla displays `value` directly and uses `modifier` only for
  its `(+)`/`(-)` annotation; adding the two would double-apply the modifier.
  The adapter now preserves the displayed value, sign, localized name, and
  localized description separately.
- Vanilla `Pipboy_StatsPage.swf` proves that condition art loads
  `Condition_Body_<BodyFlags>.swf`; the separately loaded head uses frame
  `HeadFlags + 1` from `Condition_Head.swf`. The child movies intentionally
  draw outside their declared stages, so the preparation tool widens only its
  temporary copies before rasterization. Body and transformed head layers stay
  separate, allowing live flag changes without rebuilding assets.
- The vanilla Perks tab reads `selectedEntry.clipName:uint` and the optional
  `selectedEntry.SWFFile:String`. If `SWFFile` is absent it constructs
  `Components/VaultBoys/Perks/PerkClip_<clipName hex>.swf`. Those exact fields
  are now retained through `PipboyProtocol`, `UiAdapter`, and `UiSnapshot`;
  node IDs and localized display-name guesses are not used to choose art.
- `Prepare-Fallout4UiAssets.ps1` selects the nonblank frame with greatest
  alpha coverage (midpoint tie-break), crops thumbnails, writes a bounded RGBA
  cache, and records the source Interface BA2 SHA-256. The cache covers both
  `VaultBoys\Perks` and the 17 `Magazine perks` SWFs referenced by live
  `SWFFile` values; restricting it to the former left acquired-magazine art
  unavailable. On this installation it produces 7 SPECIAL, 17 body, 32 head,
  and 120 perk assets.
- `Fallout4UiAssets.cpp` validates every header, dimension, payload length,
  and constructed relative path; applies Pip-Boy tint into premultiplied DIBs;
  and keeps a 48-entry lazy cache. Missing or malformed art falls back to the
  custom vector renderer instead of failing the panel or game.
- Fallout 4 removed Fallout 3's Skills page. The matching two-pane navigation
  and detail treatment is applied to PERKS and GENERAL while all displayed
  values remain native Fallout 4 data.

## Confirmed protocol contract

- Frame: little-endian `uint32 payloadSize`, `uint8 messageType`, then payload.
- Type 0 heartbeat is echoed exactly.
- Type 1 supplies connection JSON including game version/language.
- Type 2 means another client owns the single companion connection.
- Type 3 carries typed incremental records; record ID zero is the root object.
- Type 4 carries local-map dimensions, extents, and one-byte pixels.
- Type 5 is a JSON action request; type 6 is an action response.
- Primitive, array, and object update types reference earlier IDs; malformed or
  dangling references must reject the frame rather than corrupt the cache.
- Verified inventory `filterFlag` values are Favorites 1, Weapons 2, Apparel 4,
  Aid 8, Notes/magazines 128, Misc 512, Junk 1024, Mods 2048, Ammo 4096, and
  Holotapes 8192. Flags can be combined, so specific content categories are
  evaluated before Misc/Favorites.
- Retained real-schema Status flags include `IsDataUnavailable`,
  `IsInAnimation`, `IsInVats`, `IsInVatsPlayback`, `IsLoading`, `IsMenuOpen`,
  `IsPipboyNotEquipped`, `IsPlayerDead`, `IsPlayerInDialogue`,
  `IsPlayerMovementLocked`, and `IsPlayerPipboyLocked`. The mutation gate uses
  these authoritative flags rather than inferred menu timing.
- Independent protocol references disagree on whether command 13 `[]` toggles
  continuous local-map updates or requests a snapshot. The diagnostic sends it
  only after an explicit guarded tap, consumes any returned type-4 rasters, and
  does not guess at an undocumented second/off command until packet cadence is
  measured in the installed runtime.

## Version resilience classification

| Surface | Classification | Policy |
|---|---|---|
| Native Pip-Boy group layouts | Version-dependent | Exact executable hash, group/value vtable canaries, bounded traversal |
| Native action calls | Version-dependent | Exact hash/prologue/RVA gates and live identity revalidation |
| Companion protocol fixtures | Research-only stable service | Parser tests only; no production connection |
| F4SE messaging/plugin API | Optional research artifact | Exact compatibility declaration; not installed or required |
| Renderer/input window | Windows/WEMU integration | Runtime monitor and socket discovery; no hardcoded geometry |

## Rejected approaches

- Opening, hiding, moving, or repeatedly driving the vanilla Pip-Boy.
- Capturing a Pip-Boy/game frame, OCR, screenshots, or fake state.
- Switching Fallout's render targets or camera to service the lower screen.
- Whole-process memory scans or full inventory/map reconstruction every frame.
- Direct input injection into Fallout for touch actions.
- Absolute offsets without an exact runtime/signature/structure contract.

## Open questions and next evidence

1. Loaded-save proof that all 11 group sinks register, populate, update, recover
   across load boundaries, and do not regress startup under FEXCore.
2. Exact absolute XP and current-radiation game-thread sampling semantics.
3. In-game proof for each enabled native inventory, quest, and radio action;
   configuration is not evidence of a successful game-state mutation.
4. Native local-map raster production without opening or driving the Pip-Boy.
5. Device proof for Select-modified controller routing and long-session CPU,
   frame-time, allocation, and memory stability.
