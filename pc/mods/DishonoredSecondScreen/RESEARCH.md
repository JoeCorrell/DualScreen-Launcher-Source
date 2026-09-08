# Dishonored integration evidence

The dated investigation milestones below retain their original scope. Their
source-only/probe deployment status is superseded by the final WEMU development
installation entry and the current README; gameplay acceptance remains pending.

## Device and executable

The user confirmed wireless ADB after the initial USB probe found no USB device.
The working endpoint was `100.83.63.70:46665`. A read-only recursive inventory
is retained at `.research/dishonored/device-inventory.txt` with byte sizes,
timestamps and full paths. The game includes base content and DLC trees.

The executable fingerprint is recorded in README.md. `.work/dishonored/pe.json`
contains its sections, imports and image metadata. The executable imports only
DirectInput8Create from DINPUT8.dll. Native-script binding names survive in the
image and can be traced to registration records and native wrappers. Package
decompilation alone does not establish the ABI or prove a wrapper executes.

## Initial package evidence

DishonoredGame.upk has 24,432 names, 30,049 exports and 652 imports. Its cooked
version is 801/30, engine 9411, cooker 133. Its 16 LZO chunks expand to 17,320,971
bytes. Core, Engine and GFxUI package metadata are also available locally.

The package identifies these real native functions:

| Owner | Function | Serialized parameters |
| --- | --- | --- |
| DishonoredPlayerController | Dis_EquipItemByType | `_Usage` (EDisEquipUsage byte), `_pItemClass` (Class<DishonoredInventoryItem>) |
| DishonoredPlayerController | Dis_SelectPower | `_iPower` (int) |
| DisGFxMoviePlayerJournal | EquipBoneCharm / RemoveBoneCharm | `_ItemIdx` (int) |
| DisGFxMoviePlayerJournal | Req_ObjectivesList | none |
| DisGFxMoviePlayerJournal | Req_Logs_All / Req_Logs_Audio / Req_Logs_Notes / Req_Logs_Books | none |
| DisGFxMoviePlayerJournal | Req_CharmsTabContent | none |

These are tracing entry points, not permission to manufacture a journal object
or invoke a menu-owned callback while its dependencies do not exist. The native
semantic operations underneath them must be traced and validated independently.

DishonoredPlayerPawn exposes `m_Mana`, `m_ManaMax`, `m_pPowersComponent`,
`m_WhaleBoneCharms`, `m_Upgrades`, `m_StatValues`, `m_MissionStatValues`, and
`m_pKeyRing`. Array element types include DisCollectedWhaleBoneCharm and
DisMissionStatValue. Serialized property names do not establish runtime offsets.

The first lazy UELib dump contained incomplete child initialization (`@NULL`
types and missing parameter declarations) and errors while interpreting default
objects. The separate function-metadata pass correctly recovered Parm flags.
Do not treat an incomplete textual decompilation as an exact native structure.

## Original presentation resources

The installation contains DisFonts_SF, UI_Journal_SF, UI_HUD_SF,
UI_PowerWheel_SF, UI_MissionStats_SF, numerous UI_Map_*_SF packages, localized
notes/objectives/tweaks, and original UI sound banks. Their presence proves
asset availability only: it does not prove player ownership, discovery, world
coordinate calibration or that an off-screen map renderer exists.

## Required acceptance

- Exact-build bootstrap and transparent original DLL forwarding.
- Validated game-thread hook, runtime object layouts and world/player lifetime.
- Live HP/mana, owned inventory/ammunition/upgrades, powers/runes and selection.
- Acquired objectives/journal/charms and native equip/remove operations.
- Game-owned mission statistics with their actual definitions and availability.
- Owned map imagery, verified world-to-map transform and discovery predicates.
- Native-looking seven-page touch UI, isolated multitouch and controller focus.
- Save/load, death, area/mission transitions, cutscenes, shutdown and stale actions.
- Baseline-versus-mod frame time, resource lifetime, bounded logs and crash checks.
- Independently downloadable payload and transactional launcher installation.

No live integration or gameplay acceptance has passed yet. The original game
executable is unchanged; the development proxy and its INI were added alongside it.
The development probe compiles as PE32/i386, with only DirectInput8Create
exported and bcrypt/KERNEL32/msvcrt imports. It is not the release payload.
The user prioritized adding Dishonored to the launcher, supplied cover artwork,
then explicitly authorized installing the updated app. Launcher version
1.6.17.10-dishonored (code 39) was installed over wireless ADB and the new Beta
library card was visually verified on the Thor. Code 40 subsequently enabled
device testing with the same signing key. The existing Games/Dishonored directory
was registered in place, with independently cached development payload 0.0.2-probe.
Its diagnostic hook is disabled for baseline testing. The initial Games/container 1
launch raised a rendering-thread exception (00548165, 0054836f, 0041683a).
The user directed Dishonored to share Transformers WFC's actual container 3.
Its registration and shortcut now point into that prefix, and preset selection
shares WFC's key so future installs/launches retain the same runtime contract.
Other game registrations, the WFC shortcut, and container settings were verified
unchanged during reassignment. Device receipts/backups are under .work/dishonored.

Validation: 12 catalog, 4 payload-package, 5 Windows-path and 11 native-panel
identity tests passed. The release APK's optimized resource `res/im.jpg` matches
the supplied artwork byte-for-byte. All 518 checked BOTW/SMO/WFC assets match
the prior code-38 release. Dishonored native payload files are absent from the
APK. APK SHA-256: 53cee133487ff273971b0ec1e4179c609944c1bc3b633d85cb79921d04a213ab.

## Native findings from the installed executable

Addresses in this section are preferred-base VAs, not universal offsets.
The runtime header uses RVAs and rejects a different executable SHA-256.

| Evidence | Address / layout | Meaning |
| --- | --- | --- |
| Controller ctor 00abcdc0 | vtable 0110db58 | Concrete Corvo controller |
| Controller override 00ab6ab0 | vtable +19c; ECX receiver; float and int stack arguments; RET 8 | Calls base 00655820, ticks input components and updates equipment; original result preserved by the diagnostic hook |
| Controller init 00aa2660 | global 01452dd4 | Current controller, not sufficient alone to establish lifetime |
| Pawn ctor 00abf3c0 / destroy 00ab5100 | global 01452de8 | Current pawn set/clear |
| Pawn class getter / typed cast 00b4ef40 | class global 01452de0; object class +30; superclass +44 | Runtime type validation |
| Select/equip native methods | controller Pawn +248 | Reciprocal validation against current pawn global |
| OnSetPlayerHealth 00aa4530 | pawn +344 / +348 | Current / maximum health |
| Mana setter 00ab17c0 / OnSetPlayerMana 00aa4580 | pawn +a60 / +a64 | Current / maximum mana |
| Dis_SelectPower 00ab4460 | pawn TArray +a40 | UI selection index maps through usable active-power indices; selecting current power can toggle it |
| Bone-charm equip 00ab8f60 | pawn TArray +b38; records 8 bytes | Collected charm level byte +0 and info index +4 |
| Bone-charm slot handling | pawn +b10; slot count via 00aa8b50 | Native modifiers and slot updates, not independent UI state |
| FName conversion 00449950 | name pointer array 01429664, count 01429668 | Entry +10 contains ANSI/wide string, bit 0 at +8 selects wide encoding |
| Instance-variable opcode 00405110 | UProperty runtime offset +5c | Native interpreter computes receiver plus reflected offset; serialized package ElementSize is not that offset |

Controller Dis_SelectPower wrapper 009eec10 calls vtable +560 -> 00ab4460.
Dis_EquipItemByType wrapper 009ee630 calls +524 -> 00ab4850, with **class
first, usage byte second**. Follow NextField for UnrealScript parameter order.
Both paths use native input eligibility 00aa0710. Calls are still disabled in
the probe until ownership, lifetime and behavior are validated on the device.

Journal vtable 01158fa0 has equip/remove callbacks 00bbc260 / 00bbc290. They
reverse the visible UI index and call pawn operations 00ab8f60 / 00ab5ce0.
The underlying operations are preferable to creating a hidden journal movie.
Charm info lookup 00aa8bb0 selects ordinary versus DLC definitions at index
10000000; entries are 0x14 bytes. Native slot count includes a tweak base and
an attribute modifier. Do not replace that calculation with a fixed slot count.

The diagnostic probe only samples scalar values and bounded array counts at
10 Hz after the original tick, validates exact pawn class/vtable and current
controller/pawn agreement, and hands values to a background logger using a
nonblocking lock. No logging I/O occurs in the tick hook. This is an initial
ABI/state proof, not complete lifecycle, reflection, renderer or action support.

## Mockup adaptation

The user supplied an all-pages visual reference. `UI-DIRECTION.md` records its
composition, original-resource styling and 1240 x 1080 touch requirements.
`tools/prepare_assets.py` converts selected original TGA exports losslessly to
PNG and stages the two original fonts with provenance hashes. Its 18 staged
resources remain ignored build outputs. Nothing here establishes game-owned
map coordinates or unlock state merely from an asset's presence.

## Source-only continuation after host recovery (2026-09-06)

All addresses below are for the exact executable identified above. Evidence
comes from saved decompilation and bounded Capstone instruction windows, not a
live memory capture. No new app/payload build, package or install was performed.
The user holds builds until the mod is ready and prohibits agent delegation.
Source archives under `.work/dishonored/source-recovery-*` are recovery copies,
not installable payloads. Source review is not device validation.

`tools/verify_reader_evidence.py` passed 41 static instruction, vtable and cooked
field checks against the exact copied executable/package metadata. Its receipt
is `.work/dishonored/reader-evidence-20260906.json`. It performs no compilation
or game execution and does not establish C++ correctness or gameplay behavior.

At this earlier checkpoint the adapter was not connected. Subsequent source
work connected the full-payload bootstrap and native actions, as documented
below; the diagnostic probe remains separate.
All reader domains remain Incomplete or Unresolved where appropriate.

| Source contract | Installed native evidence |
| --- | --- |
| Pawn tweak reference +748 | Pawn ctor 00abf3c0 installs interface vtable 0110f718 at +4bc; slot10 is 00b48ba0, returning receiver+28c |
| Currency definitions and quantities | Reflected DisTweaks_PlayerPawn.m_pMoneyItem/m_pRuneItem; 00c0b4d0 searches inventory+c8, stride12, first matching definition pointer, quantity+4; absence is zero only after a successful complete search |
| Owned charm definitions | 00aa8bb0 selects tweak+714, then normal array+8c or scripted array+98 at index10000000, stride14; only pawn+b38 owned records are exposed |
| Localized charm name/effect | 00bd2460/00bd2539 uses info+8 Levels array, stride1c; FStrings+0 and+c; effect float+18; journal order reverses owned acquisition order |
| Active charm slot count | 00aa8b50 adds tweak+210 to truncation of attribute identified by FName global01451824; pawn slot storage +b10 has cooked ArrayDim10 |
| Attribute object | Pawn+4a8 interface vtable0110f784 slot8 ->00b49270, returning receiver+2d4 (pawn+77c), agreeing with reflected m_pAttributes |
| Attribute lookup | 00c74240 searches DisAttributes map+38 using FName index hash, map+38 bucket count, +34 secondary/+30 inline bucket, 96-byte sparse entries and +58 chain links |
| Attribute cache safety | 00c85e90 reads entry+18 only when entry+14 bit1 is clear; otherwise 00c85db0 recalculates and writes the cache. The first reader declined dirty values; the current source reproduces bounded read-only evaluation, described below |
| Sparse allocation bits | 00425190/005fc790 choose bit-array secondary storage +10 or four inline words; bit count +14. For the attribute map these are +1c/+c/+20 |
| Base readable getter | DisAbstractItem class registration01368100 -> constructor00c4e7a0 -> vtable010eb5d0, slot124 ->00a4dc00; copies FString+50 |
| Grouped journal contents | 00bd1020 obtains the main getter result, then owned child definitions whose parent+70 equals that item, in forward inventory order; 00bc9560 appends separate FString pages |

The journal reader validates ownership and parent links once, groups children
with a fixed bounded lookup table, and joins the native pages with paragraph
breaks for the continuous touch reader. It accepts only the verified readable
getter, without invoking it. Unsupported overrides and arena exhaustion never
produce a falsely complete document. At this checkpoint keyring, map imagery and audiograph playback were absent;
keyring and acquired map imagery were added in the later source work below. Collected charm equipped state similarly remains
unknown until the exact active-slot count and each slot index validate.

Additional source now includes active and passive powers using the independent
native component arrays; original RPG.int strings and rune costs; mission-stat
records from pawn+b7c (12-byte ID/float/maximum entries); and visible active
objectives. The stat adapter deliberately avoids OnGetPlayerStat, whose event
flag can reset a stored value. The lower UI shows coins/runes held separately
from mission totals and base mana separately from modifier-aware consumption.

The UI uses the original fonts and staged artwork, seven persistent touch tabs,
scrollable cards/readers, map gesture handling and generation/revision checks.
Actual maps/markers remain unresolved. The panel is secondary-monitor-only and
no-activate, with bounded loopback touch on port7788. Source review corrected
batched-click layout refresh and window-creation failure reporting. Visual
matching, controller isolation, performance and transition resilience still
need a completed runtime and device acceptance.

### External tool references

- Unreal-Library: https://github.com/EliotVU/Unreal-Library
- UE Explorer: https://github.com/UE-Explorer/UE-Explorer

External examples are research leads only. The installed binary/package remains
the authority for this mod's field layouts, behavior and compatibility gates.
# Continued source investigation, September 6

Additional bounded native traces are retained under `.work/dishonored`:
`native-object-membership`, `native-equipment-actions`, `native-weapon-options`,
`native-selected-options`, `native-localization`, `native-objective-tasks`,
`native-chapter-task-model`, `native-mapinfo`, and `native-world-task-status`.

- GObjObjects is the TArray at `01417620`; registration `00464990` writes
  the same index to the object and its registry slot. Sampling now verifies
  membership and the native collection/serialization guards.
- Native input eligibility `00aa0710` applies controller flags and all eleven
  masks at `+66c`. The exact DishonoredEngine `+188` override returns zero.
- Source semantic actions use controller `00ab4460`/`00ab4850` and pawn
  `00ab8f60`/`00ab5ce0`. They revalidate ownership, avoid native toggle behavior
  for an already selected item, and defer success until a later snapshot.
- Read-only dirty-attribute evaluation follows `00c85db0`: ascending allocated
  modifier indices, ordered add/percentage/override operations, native float
  rounding points and optional bounds. It does not write cached game values.
- Weapon labels follow item tweak references and the native player-specific
  object at `+104`, with localized name/description at `+14c/+158`. Ammo records
  are inventory `+bc`, elixirs `+d4/+d8`; purchased upgrades are pawn `+b50`.
- Objective history follows `00bd7810`, including chapter membership, hidden
  objectives, completed/failed task states and task hidden flags. Base task
  status `0092dc90` returns empty; custom status was subsequently traced to
  `00acbe10`, which copies the already evaluated FString at task `+7c`.
- `Runtime.cpp` and the full-payload branch of `Probe.cpp` are connected in
  source. Tick and teardown installation validates exact vtable targets;
  shutdown withdraws the adapter under its lifetime lock before hook removal.
  Diagnostic records cross a bounded queue; file logging stays on the worker.
- App localization `00488d90` defaults to the active wide language buffer at
  `0125e660`. Runtime loads original RPG definitions using that language.

The read-only verifier now passes **82 native/schema checks** against the exact
copied executable and cooked metadata. This does not compile C++, execute native
actions, verify UI appearance or establish device stability. The build hold
continues; no new binaries, packages or installations were produced.

## Continued native contracts and original artwork

- `00c47ff0` computes darkness level from `DisDarknessManager.m_DarknessScore`
  (`+38`) and the player's `m_DarknessLevels` (`+2c4`), walking thresholds in
  reverse. `00a01eb0` and `00a07b40` compare the resulting index against
  `GameInfo.m_MissionChaosThreshold[CurrentMissionNum]` (`+15dc`, index `+448`).
  The source chaos indicator reproduces that inclusive comparison, independently
  of the mission-stat array's raw chaos score. Metadata confirms 21 mission thresholds.
- Base power consumption virtual `+178 -> 00bf3020` reads tweak mana cost `+8c`
  and calls `00aa90a0` on component owner `+80`. The random free-cast branch is
  excluded for the exact base GameInfo class by `00bbf2b0`. Confirmed costs are
  published only for that game class, verified virtual and reciprocal owners.
- Ammunition selection dispatches the native parameter object through `00c190d0`,
  following the stack object and virtual table used by `00c30900`. Selecting a
  different weapon waits for the native equip result before selecting its ammo.
  Controller `00aaea90` consumes elixirs through native ownership, restoration,
  inventory, sound and event handling; the adapter never writes the counters.
- Map-note interaction `00a36050` sets `m_bAlreadyUsed` at `+3c0`, bit 0. Cooked
  metadata marks that flag transient, but custom save `00a223b0` and restore
  `00a31810` explicitly serialize it. The source uses the live restored flag. `00bcca10` resolves
  native title/image/legend records; the 13 base map images have no live player
  coordinates. Their printed position symbol belongs to the map board.
- Read-only device pulls and UModel extraction produced all 68 textures in
  `UI_ItemIcons_Large` (4,456,448 pixels). Contact sheets were visually inspected.
  `00c462a0` constructs the native image URL from a journal icon name plus the
  `_Large` suffix. `ItemAssets.inc` is an exact allowlist shared by UI loading
  and future resource preparation. Unknown icon names resolve to no asset.
- Source-only presentation changes add equipped items, equipped charm slots,
  Journal filters and key entries, and actual layout rectangles for touch
  scrolling. No screenshot of the compiled UI or on-device acceptance exists.

Newest evidence: `native-chaos-mana-getters`, `native-chaos-thresholds`,
`native-chaos-consumers`, `native-item-artwork`, and
`reader-evidence-20260906-chaos.json` under the ignored research work tree.

## Restore, keyring, profile and diagnostics continuation

- Corvo vtable `0110f7d0 +118 -> 00ab8790` restores player state in place.
  ECX is the pawn, stack arguments are archive and flags; `00ab8bde ret8`
  confirms the ABI. Source suppresses nested sampling during this call and
  advances the world epoch so pre-load touch commands fail generation checks.
- Map-note vtable `010d7b48 +114/+118` selects `00a223b0`/`00a31810`.
  These write/read one byte from actor `+3c0` bit0. `00a31850` applies the
  native already-used post-load behavior. `00a316c0` is event handling, not
  the save writer. Metadata alone was insufficient to determine persistence.
- Native keyring list `00bd1920` reads pawn `+a90`, then array `+38`, stride
  `1c`. Records contain localized name `+10` and optional key tweak `+c`.
  Native grouping compares localized names; descriptions/icons are tweak
  `+dc/+e8`. Source mirrors grouping and retains unknown optional artwork.
- Controller profile getter `00aa6bf0` follows Player `+49c`, data `+a4`,
  profile `+58`. ArkProfileSettings array `+44` has 24-byte records, ID `+4`.
  Setting `88` is a type6 blob, with data at record `+10`, length `+c`.
  Native `00938210`/`00938340` store/read 68-byte mission records: valid bit
  and mission number in the first word, summary bits in the second, then
  16 floats. Source uses base mission indices 0..8 and matches actual UI
  definitions by setup index and mission number before exposing values.
- GameInfo `+14e0` is the localized `MissionNames[21]` array, not statistic
  storage. Saved floats already include native aggregation. Checkbox results
  follow `00bcbdc0`; the adapter never re-adds secondary statistic values.
- Objective targets use live portrait/state records at GameInfo `+454`.
  Target names require matching chapter definition and live portrait path;
  concealed targets do not inherit undiscovered names from definitions.
- Exception observer source writes at most 32 fixed 80-byte records per run
  to a bounded diagnostic file, then continues normal exception dispatch. It
  does not recover game exceptions or perform I/O on stack-overflow faults.
  Normal read-budget/failure diagnostics cross the game-to-worker queue.
- Source now waits for the native language buffer to initialize on the worker.
  Equipped-slot occupancy crosses the bridge independently of charm labels,
  preventing failed/truncated charm descriptions from appearing as empty slots.

Read-only verification now passes **117 native/schema checks**; receipt:
`.work/dishonored/reader-evidence-20260906-restore.json`. No C++ compilation,
new package, install, UI screenshot or gameplay acceptance was performed.
The source still lacks verified map calibration/markers, some journal/item
paths, complete resource staging and completed runtime/device validation.


## Native documents, legends and touch continuation

- Audio-log journal getter `00a51500` returns an FString through a caller-owned
  header (ECX=item, one stack argument, `ret4`). `00aaccf0` reaches the native
  conversation text/condition walker `00c9ee20`. Playback uses a different
  speaker operation; displaying a transcript must not start playback.
- The audio actor at pawn `+9dc` has primary vtable `010ee498`, interface at
  `+248` with vtable `010ee420`, and conversation component at `+250`.
  `00c8b5e0` requires component `+4c` bit0 and returns hook parameters `+124`.
  Native FString destructor `00430ee0` releases getter-allocated text.
- Read requests now reject changed inventory lengths as well as changed owned
  records/parents. Keyring entries cannot accidentally index the separate
  grouped-document workspace. A cache is tied to generation, ownership and
  group signature; no transcript getter runs every sample.
- Map legend records are 16 bytes: symbol byte `+0`, localized FString `+4`.
  `00bd4b6e`/`00bd4b9b` verify the native array at map content `+18`.
- Saved narrative summaries use 36-byte definitions and the desired bit at
  `+20`. `00bd42fd` excludes unmatched summary bits. Only matched descriptions
  are exposed; unrelated narrative outcomes remain concealed.
- Elixir count clamps `00c04a60`/`00c04b10` use player tweaks `+1fc` (health)
  and `+204` (mana). Potency attributes affect restoration, not these capacities.
- Flash sprite 140 identifies ten exact power icons. Possession is `Journal_I8A`;
  `Journal_I7E` is Vitality. All ten mappings are checked against the original
  XML symbol/frame/shape/bitmap chain by `verify_power_artwork.py`.
- Source touch handling retains semantic target identity across live revisions.
  Inventory reordering cannot redirect release to another row, and ordinary
  state updates no longer cancel dragging. World-generation changes still cancel.

Latest read-only evidence receipt: `reader-evidence-20260906-documents.json`
(135 contracts); `power-artwork-verified.json` (ten mappings). Full payload build
and package recipes have only been parsed, not executed or compiled.

## FEXCore 2608 device override (September 6)

The upstream [FEX-2608 release](https://github.com/FEX-Emu/FEX/releases/tag/FEX-2608)
has no attached binaries. The launcher's existing component catalog points to
[the Windows CI package](https://github.com/The412Banner/winlator-contents/releases/download/fexcore-v1/FEXCore-2608-stable.wcp).
Downloaded package SHA-256 `baa16434da07fe824e2eb6da458aa7aef786d109cf3408c84d73ec008a9484f4`
matches GitHub's release asset digest. Its files are byte-identical to the already
installed 2608 component profile; no replacement of unrelated components was needed.

- `libarm64ecfex.dll`: `e3a789bd77cbccecb54012773994fff06887b0206bcd623967640e6240009d30`
- `libwow64fex.dll`: `2210f4cf405176571b8db74efd41bc8bc69cd14dd63b1b96508ae45bbf70261d`

Dishonored's shortcut selects `FEXCore-2608-stable-0`. Its 16:25 launch log names
this exact version; both DLLs in container 3's system32 match the hashes above.
All container setting fields and pre-existing extra settings compare equal to
the backup. Normal startup adds materialization receipts, including the effective
translator version. WFC's shortcut/default version is unchanged and can rematerialize
its own selection when launched. Evidence/download/backups are in
`.work/dishonored/fex2608`; the user's copy is `/sdcard/Download/FEXCore-2608-stable.wcp`.

Baseline still failed before a visible menu, at 16:25:49, with the diagnostic
hook disabled. Earlier 2601 launches and a backed-up windowed-INI experiment
also failed. The windowed experiment was restored exactly. The temporary shortcut
Wine debug environment was overwritten by the installed launcher's migration:
its final process environment remained `WINEDEBUG=-all`, so those logs contain
no usable SEH trace. No global Wine logging preference was changed; a planned
logging experiment was stopped when a user-owned WEMU Dishonored process appeared.


## Working WEMU comparison supplied by the user

Read-only inspection found the working `proton 9` container is WEMU ID 5,
actually running `Proton-10.0-arm64ec-0`. Its `Turnip A8XX Draft` driver differs
from launcher container 3's `turnip26.2.0`. Their D3D9 DLLs, FEX 2608 DLLs,
DXVK settings and DishonoredEngine.ini are identical. WEMU uses one 1280x720
monitor; the launcher exposes the requested second monitor and a 1280x1800 desktop.

A concrete additional mismatch is HKCU\Software\Arkane\Dishonored:
WEMU has DWORD ResX=1280, ResY=720; launcher has ResX=540, ResY=960. Both have
Fullscreen=1. The earlier INI-only experiment missed this authoritative native
store. `00580d60` opens this exact registry key and reads typed setting records
into the game's system settings; `005818f0` writes the same key. The read path
also checks the configuration-file Timestamp and can discard registry overrides
when that timestamp changes, so an INI edit is not an isolated viewport edit.

WEMU's native DirectInput preference is also different: built-in first, versus
Dishonored's executable-scoped native diagnostic proxy in the launcher. The proxy
hook was disabled in every baseline test, but DLL loading itself still differs.
No single cause has been isolated yet. No working-WEMU setting/session was changed.
Detailed comparisons, registry snapshots and driver hashes are saved under
`.work/dishonored/proton9-compare/comparison.md` and `comparison.json`.

## Latest WEMU baseline and local-only continuation

The user subsequently explicitly requested installing the existing work in WEMU.
Only the pre-existing diagnostic proxy could be configured: shared game INI
`[Probe] Enabled=1`, plus container 5's `Dishonored Probe.desktop` with
`WINEDLLOVERRIDES=dinput8=n,b`. The DLL SHA-256 remains
`5807d77b249529140e34ca04c340cef7084a7a6019454d655b666d4a4899a35d`.
Backups and the exact shortcut are in `.work/dishonored/wemu-probe-test`.
Container and registry bytes compared unchanged at setup. No second-monitor
setting was enabled and no full payload was built or installed. The user later
confirmed a successful WEMU game launch; which shortcut was used and whether
the proxy hooked successfully remain unverified. The diagnostic cannot draw UI.

Before that setup, the launcher-only game registry ResX/ResY was changed to
1280/720. Its subsequent test ended when the user switched games; this is not
independent proof of a game crash. The working A8XX driver has been copied to a
distinct launcher driver directory, but no shortcut selects it yet. Current work
is local source/analysis only: no ADB, device interaction, builds or packaging.

## Equipment attributes and streamed-map cache

Native inventory details dispatch through journal vtable `01158fa0` slot `234`
to `00be02b0`, then record array `+274`, stride `1c`, into `00bdf5f0`'s original
thumbnail/description conversion. Upgrades-list slot `230` is `00bd95a0`.
`00bd2280` traverses pawn `+b50`, restricts journal section `+d0` to 6, hides a
predecessor when another owned upgrade points to it at `+e0`, and takes title,
artwork and description at `+b8`, `+d4`, `+c4`. This confirms the existing reader;
no simulated upgrade table or new native menu call was added.

Inventory item attributes interface lives at item `+3c`, vtable `011630d4`.
Slot 8 calls `00bfea80`, returning receiver `+a8` = item `+e4`.
`00c86410` uses that interface and then calls `00c85e90`, the modified-attribute
getter already reproduced read-only. Native FName initialization establishes:

- `0145184c`: `Attribute_MeleeDamage` (`009e70c1` / `009e70d8`).
- `014518dc`: `Attribute_RangedDamage` (`009e7325` / `009e733c`).
- `014518e4`: `Attribute_RangedHeadshotMultiplier` (`009e735c`).

Melee context `00c078c0` adds a pawn term and attack-context multiplier after
reading the weapon attribute. Ranged contexts at `00c25291` / `00c252b6` and
`00c29cbd` / `00c29cdf` consume the weapon damage/headshot attributes before
further target handling. Source exposes these live weapon values, with native
FName text validation, as values before attack/target adjustments. It does not
claim final damage, derive accuracy from guesses, or call damage functions.
Evidence: `native-weapon-attributes.txt`, `native-equipment-details-conversion.txt`.

Equipment UI now filters All/Weapons/Ammunition/Gadgets/Supplies/Upgrades. Owned
ammunition carries its owning weapon key across the pointer-free snapshot so
the loadout shows the actual selected ammo/count. A loadout tap reveals its
category if necessary; an empty category cannot draw its message over the
loadout/navigation. Selecting a card reserves a larger scrollable detail area.

Map scanning compares the actual loaded-level pointer list, not just array
address/count. It immediately drops unloaded-level candidates and only prunes
unseen actors after a successful full bounded traversal. It rechecks the map
actor's exact vtable before using its acquisition flag. Read failure retains
uncertain cached candidates for retry; it does not confer map ownership.
Georeferencing/live markers and unloaded-area map coverage remain unresolved.

Memory-fault records are now capped at one per five seconds even when the
failing address changes; `d` reports suppressed failure observations. Root
validation is also logged at most once per five seconds, including on the
early-return path that previously skipped memory diagnostics. `root-validation`
id: 1 quiescence, 2 controller identity, 3 live controller, 4 pawn link,
5 pawn class, 6 live pawn, 7 pawn vtable. Lifecycle records still identify recovery.

Latest receipt: `.work/dishonored/reader-evidence-20260906-equipment.json`,
**160 exact native/schema checks passed**. Seven Python tools parsed as source.
These checks do not compile C++, execute the new mod, verify touch/rendering,
or establish readiness. The build hold and device hands-off instruction remain.

## Current area, marker visibility and touch continuation

`0078d340` resolves the current world package through `GetOutermost` (`0041fc50`,
UObject Outer `+24`) and replaces it with the first `LevelStreamingPersistent`
entry in persistent WorldInfo's streaming array (`+314`, count `+318`). That
entry's PackageName is an FName at `+38` (`0078d434`), converted by `00454a00`.
This differs from `WorldInfo::GetMapName` (`0078f540`), which can use WorldInfo's
title when its boolean argument is false. Source follows the current world
package path, with registry validation, a bounded/cycle-checked outer traversal,
and no allocating game call on the sampler.

DishonoredEngine loading-screen dispatch (`010d0bf8` slot `128` -> `00a01c50`)
uses MapConfig lookup `009fc4e0`: engine array `+7d8`, stride `38`, case-insensitive
package-name comparison. The visible area label comes from `00bb5e50` ->
`00bc86e0`'s `[MapNames]` section, not simply the config's friendly name. The
language is the inline array at `0125e660`, returned by `00415200`. The helper
searches localization roots for `<game>Game.<language>` and falls back to INT
when the section is absent. The installed vanilla localization path contains
`DishonoredGame.int`, including both base-game and DLC names. The source loads
this immutable section on the worker using the active language and INT fallback,
then looks up only the verified current package. Unknown keys remain blank;
neither destination-loading text nor raw package names become player-facing data.
Custom localization-root overrides have not been validated. Evidence files:
`native-current-map-name.txt`, `native-map-loading-name.txt`,
`native-map-config.txt`, `native-map-localization.txt`.

HUD constructor `00bb0630` sets primary vtable `01151fc0`; tick slot `1dc` is
`00bb1ff0`. Marker update calls `00baff20` -> `00bad4b0` for task targets and
`00bad8c0` for awareness. The task selector `00ad9240(...,1)` requires active,
nonhidden tracked objectives and nonhidden, nonterminal tracked tasks. Task
target records are at task `+68`, count `+6c`, stride `24`; actor pointers are
filtered for native visibility/deletion conditions. Existing HUD task-marker
records are pointers in `+524`, count `+528`, with actor at record `+8`.
Constructor `00bce670` creates `88`-byte task markers with vtable `01158588` and
localized target name at `+38`. Render `00bc5830` calls `00bbd4f0`, which stores
the last rendered visibility flags at `+30` (`00bbd575`); bit 0 represents
visibility. The marker's distance cache at `+80` is distinct from world/map
coordinates. These traces establish a candidate discovery-safe marker source,
but source does not yet publish live map markers or infer georeferencing from
screen positions. Evidence: `native-hud-lifecycle.txt`,
`native-hud-markers-tick.txt`, `native-hud-marker-arrays.txt`,
`native-task-marker.txt`, `native-task-marker-visibility.txt`,
`native-hud-marker-display.txt`.

Dishonored-specific launcher routing sends actual contact down/move/up, cancels
contacts when a second finger/pinch takes over, suppresses repeated completions
and their replayed down events, and cancels on surface destruction/dismissal.
Late scale callbacks after cancellation do not send pinch packets. Dishonored
touch never enters X11 gameplay input even while native-panel discovery is
pending. Other games retain their existing delayed-down routing. Source tests
were extended but not executed because the build hold remains in force.

Original journal frames `I1A/I1C/I1E` are complete idle variants with opaque
centers; `I21/I23/I25` are transparent selected-glow variants. All are 184 x 148.
The renderer uses the eight outer cells of a nine-slice with 32-pixel edges and
omits the center, retaining original corners and the tile's own surface color.
`I44` is an ink mask, not a parchment image; parchment details use a warm surface
with that original mask at low opacity. Equipped inventory/charm selections
reserve more room for descriptions; holds open details without equipping or
consuming. Asset/font failures report specific bounded errors; touch totals and
StretchBlt failures are rate-limited on the panel worker.

`prepare_assets.py --check` only decodes/inspects the original inputs and checks
the resource budget, without converting images or staging anything. All 117
resources passed, totaling 11,179,056 image pixels against the 33,554,432 limit.
The ten native Flash power mappings also passed. Latest binary/schema receipt:
`.work/dishonored/reader-evidence-20260906-area.json`, 171 checks. Grammar receipt:
`.work/dishonored/source-grammar-20260906.json`, 26 C++ sources compared with the
prior recovery checkpoint, two Kotlin sources, and seven Python sources parsed.
No new grammar diagnostics were introduced; macro/calling-convention limitations
remain. This is not C++ preprocessing, type checking, compilation, rendering or
runtime proof. No build, staging, packaging, installation or device action was
performed in this continuation.

## Authorized wireless ADB continuation and level geometry

The user explicitly authorized using ADB after verifying that the Thor reports
`device` at `100.83.63.70:46665`. Read-only access resumed. All 161 packages in
`.work/dishonored/map-level-copy-plan.json` were copied into the local original
CookedPCConsole tree. Receipt `.work/dishonored/level-copy-receipt.json` records
each local/device SHA-256: all match, and all lengths match the prior inventory.
The total is 800.2 MiB. No gameplay input, launch, process termination, container
change, build, payload staging or installation occurred.

The probe log inspected at this point contains five prior exact-build-accepted
messages, each with the hook disabled. The current INI says Enabled=1, but there
is no later initialization record in that file. This does not establish that
the hook executed during the user's successful WEMU launch, nor identify a crash.

`tools/read_level_properties.py` reads decompressed 801/30 packages with bounded
tables, FNames, state-frame headers, typed property tags and explicit unknown
native tails. It rejects compressed input. It handles the StaticMeshComponent
template prefix, with bounded outer-chain checks. Serialized absent fields are
not synthesized: they may inherit defaults. Two package export tables (13,515
entries) match the independent prebuilt UELib reader in class, offset and size;
UELib omits some `_0` suffixes that UModel and native FName numbering preserve.
Receipt: `.work/dishonored/level-reader-validation.json`.

All 161 packages have UModel inventories under `.work/dishonored/level-lists`.
Fifteen map-board placements were decoded without property errors in the nine
packages with explicit map-board tweak definitions. The placements include
Master/Slave geometry duplicates and the two Distillery District chapters;
they do not establish 15 distinct acquired maps. No Boyle board was found in
these inspected Boyle packages; the presence of a Boyle UI texture alone must
not grant access. Evidence: `map-board-placements.json` and per-package
`levels/<package>/placements.json`.

Kingsparrow's `L_Isl_Geom_Master` and `L_Isl_Geom_Slave` each contain the same
three board placements:

| Native map | Actor | Serialized location |
| --- | --- | --- |
| DUM_Lighthouse | DisAbstractItemPickupNote_2 | -4459.768555, 5685.300781, 1113.276001 |
| DUM_Lighthouse2 | DisAbstractItemPickupNote_41 | -4571.708984, -5402.019043, 2323.118408 |
| DUM_Lighthouse3 | DisAbstractItemPickupNote_42 | 377, -3475, 2435.011719 |

The three 800 x 500 original images differ only in their printed board-marker
regions. Their locations give three corresponding world/artwork anchors. An
initial affine candidate is recorded in `isle-calibration-candidate.json`; it is
not a native transform and is not enabled in runtime source. The geometry reader
decoded 6,320 component matrices from 64 static-mesh collections, with one
64-byte matrix per referenced component. For 24 selected original terrain,
wall and dock meshes, all 2,467 exported vertices were located byte-for-byte in
the native mesh payload after reversing ActorX's Y sign. This independently
checks the export-axis convention. The illustrative map and projected mesh
geometry do not establish a uniformly precise transform from three anchors;
additional landmark calibration and live checks remain necessary. Inspection
figure: `.work/dishonored/map-inspection/isle-calibration-check.png`. The research
plot and lossless viewing copies are not payload assets.

Native map getter `00bcca10` prefers its note tweak override, then falls back to
class global `014627b0`. `004967f0` returns UClass `+13c`, but can allocate a
missing default. Source now reads only the existing default, validating class
identity and object membership. It never calls the constructing getter. An
invalid nonnull override fails validation rather than silently falling back.
This removes an unnecessary dependency on an already-created note movie when
the native default is loaded. Evidence: `native-note-default-object.txt` and
`reader-evidence-20260906-levels.json` (176 native/schema contracts).

## Native task-marker observations and objective targets

The task marker's exact vtable `01158588`, slot `14`, points to `00bc5830`.
Disassembly confirms ECX receiver, six 32-bit stack arguments and `ret 18` at
`00bc5b72`. The fifth argument is the distance used by the native label, multiplied
by the float at `00fb7e7c` (0.01) and truncated with `cvttss2si`. The sixth is
the final render flags. Cached marker `+80` can be stale while label display is
disabled; it is deliberately not a live-state source.

Runtime source forwards the original render unchanged, then captures a bounded
observation only on the established game thread, outside restore/sampling and
within the current world epoch. It performs no allocations, file I/O, scans or
UI rendering in this callback. The observation table has 256 slots and at most
eight probes per callback. Lifecycle changes clear it. A callback on another
thread is ignored; actual device thread behavior still requires validation.

At sampling time, current `DisGlobalUIManager.m_pHUD` must resolve to the exact
Corvo HUD vtable. `m_TaskMarkers` is checked by reflection against `+524`, and
the objective-marker preference at `+1c0 & 80` is respected. Targets are joined
to active native tasks through their `m_Targets` array (`+68`, 36-byte records).
The native selector `00ad9240` requires a visible, active, tracked objective and
a visible, unfinished, tracked task. This exposed and fixed the old source's
missing parent-tracking condition in a task's `tracked` value.

Each published target must match both a fresh render observation and the current
HUD-owned marker array, retain object-registry identity, pass actor pending-kill
checks and retain the actor-specific visibility predicate from `00bad4b0`.
Marker `+30` must still be visible and its GFx clip nonnull. Labels come from
the actual localized marker FString at `+38`, populated by `00bce670` from the
task target's localization file/section and `m_TargetName`. No secret definition
enumeration or original menu opening is used. Objective details display these
targets, with separate observation timestamps and panel-side expiration after
750 ms. No screen position is misused as a map coordinate.

Controlled `objective-markers` diagnostics report observations since the prior
five-second log, current owned observations, published target count and domain
availability. Native/schema checks reached 202 before the audio additions.
This is source/static evidence only; the render callback has not run on device.

## Audiograph playback and transcript scratch isolation

`00bc1950` (native journal play) resolves an owned audio-log definition, places its
FName (`item+74`) in existing speaker hook parameters (`component+124`, name at
`+8`), then calls Corvo's speaker interface (`pawn+4cc`, vtable `0110f620`,
slot `28 -> 00aa1ff0`) with hook `76` and the same speaker receiver. That method
handles native replacement before forwarding to the audio actor's fire-hook
implementation `00ca6e00`. The dedicated actor is `pawn+9dc`, speaker interface
`actor+248`, vtable `010ee420`; component getter `+64 -> 00848560` returns
`actor+250`. Hook-parameter getters are checked against the traced implementations.

The source action revalidates the current owned journal row, class, parent,
hidden flag, generation/revision and native input eligibility before changing
the existing hook FName and invoking playback. It does not construct a journal
movie or write its requested-selection state. Native stop `00bc1a30` uses the
same actor's `+34 -> 00ca4b70` with reasons `9/D`, only for the matching playing
log. Source play/stop acknowledgements require a subsequent game-state sample.

Playing state follows `00bc1860`: compare the actual hook FName, then evaluate
the native speaker query `00c9ce30`. The component must be active (`+4c & 1`),
its running instance at `+8c` nonnull, instance `+c` nonnull, and its state byte
at `+14` nonzero. The panel provides a distinct PLAY/STOP button alongside the
readable transcript, with native eligibility and pending-request guards.

Transcript getter `00a51500` also overwrites the shared FName. The source now
saves and restores just those eight bytes around the native transcript getter;
it checks that the same current pawn, actor, component, registry indices/classes
and requested scratch value still exist before restoration. A separate bounded
read context, allocated on bootstrap, reserves cleanup capacity even when the
transcript string read exhausts its own budget. Native FString destruction still
occurs before any failed copy is returned. This behavior needs live validation,
especially reading a different transcript during playback, replacement, stop,
natural completion and save/load transitions.

Evidence: `native-audiolog-playback.txt`, `audio-controls-disassembly.txt` and
`reader-evidence-20260906-audio.json`. The static verifier passes 226 native/schema
contracts. ADB remained online; a read-only process query found no Dishonored or
WEMU process at that time. No game was launched, stopped or controlled, and no
payload or APK was built, staged, packaged or installed.

## Additional source validation and read overhead

The official prebuilt Cppcheck 2.21.0 package was downloaded and checked against
the release's SHA-256 (`a86eef1180dc18fde46b08f4e4b12f2a1ead8cfc0dd6c294aac8757748cbbb24`).
An installer-based extraction command was rejected by automatic approval review;
the package was instead read as an MSI database and decompressed as cabinet data,
without executing the installer. Its x64 analyzer and matching amd64 runtime
files are isolated under `.codex-temp/tools/cppcheck-2.21.0/cli`.

The Windows/C++17 static-analysis pass checks warnings, performance and portability.
Record/catalog fields now have explicit initial values and the touch socket is
assigned directly to its owning member, with the local Winsock handle only a
borrow. Two narrow documented suppressions remain: intentional 12-byte diagnostic
copies, and the analyzer's unconditional nonnull-handle model for a zero-count
Windows input-only wait. The latter follows Microsoft's API contract:
https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-msgwaitformultipleobjectsex.
This is static analysis, not a compiler/type-system or device acceptance result.

UObject validation now reads the complete native 0x38-byte header in one bounded
operation, retaining live registry, pending-kill and class-pointer checks. Optional
null objects still fail identity validation without recording a memory fault.
The marker callback similarly reads its native prefix and actor index in just
two OS memory reads. No lifetime-validation result is cached across samples.
These changes reduce memory-call count by construction; actual frame-time impact
has not been measured. Internal bridge version is now `00010002`, including
acquired map-sheet selection as well as objective observations and audiographs.

## Acquired map sheets and native discovery observations

The map snapshot now lists distinct acquired sheets in currently loaded levels.
Selecting a sheet is a panel view request, not a game-state write. The adapter
rechecks world/level membership, note registry identity and the restored native
acquisition flag. Selection is acknowledged only after a following snapshot
shows that sheet. A separate choice resumes nearest-acquired-board selection.
The UI uses the original localized titles, existing distressed frames, clipped
touch targets and scrolling. Unloaded history remains outside this reader.

Concrete note vtable `010d7b48`, slot `3c0`, points to interaction `00a36050`.
Assembly verifies ECX receiver, one pawn stack argument, EAX return value and
`ret 4` at `00a3640c`. The native operation sets actor `+3c0` bit 0 before
returning. The new hook forwards that operation and return unchanged, then takes
one bounded header read to queue a hint. A maximum of 32 hints is retained; the
next sample must independently verify a loaded, live, acquired map board.
Ordinary note/book interactions do not confer map access. Restores, player
changes and stale tick gaps discard these hints. Worker-side `map-discovery`
diagnostics are emitted only after successful revalidation.

Static evidence now passes 232 native/schema contracts, including six explicit
note-interaction ABI/behavior checks. The current Cppcheck and grammar receipts
are `static-analysis-20260906-current.json` and
`source-grammar-20260906-current.json`. They do not establish runtime correctness.

Nine further mesh exports were inspected for Kingsparrow alignment, overlapping
two earlier exports (31 distinct meshes examined overall). The eight-mesh
landmark pass verified 385 vertices, and `vista_lighthouse_top_01` independently
verified 360 more. The latter projects near the schematic lighthouse under the
three-board affine candidate, but its projected extent and the drawing are not
identical. Mesh names alone must not be treated as correspondence: the earlier
`lh_int_tower_base_floor_01` occurs near the central fort, not the distant tower
vista. Figures are `map-inspection/isle-landmark-overlay.png` and
`map-inspection/isle-lighthouse-vista-check.png`; receipts retain actual bounds
and vertex checks. This remains research, not an enabled live map transform.

## Applying the Skyrim screen architecture

See `SKYRIM_ARCHITECTURE_COMPARISON.md` for the source-to-source comparison.
Dishonored's initializer and panel now run independently, with ordered cleanup
and an explicit bootstrap status. The exact single-monitor 1280x1800 Thor
fallback uses `(0,720,1240,1080)`, relative to the primary origin, while a single
1280x720 monitor remains in a waiting state. Monitor changes and geometry are
logged on transitions. No generic primary-monitor fallback is used.

Package validation now checks a stable full-runtime identity in `Runtime.cpp`
rather than mutable hook-log text. The earlier wording would have rejected the
new hook set. The diagnostic translation unit does not contain this identity.
At this investigation milestone only the recipe changed. It was subsequently
executed for the explicitly requested WEMU test installation below.

## Authorized full WEMU development installation, September 6, 2026

The user requested installation into WEMU for a two-display test and confirmed
Dishonored was closed. The exact installed APK was copied read-only and inspected
with Android SDK DEX disassembly. Receipts under `.work/dishonored/install-test`
include `wemu-NativePanelIdentity-disassembly.txt`,
`wemu-SecondScreenOutput-disassembly.txt`,
`wemu-GuestPanelMotionRouter-disassembly.txt` and
`wemu-NativePanelInput-disassembly.txt`. The stock host lacks Dishonored identity
matching. An optional Skyrim private-window transport alias now enables existing
early panel claiming and suppression of duplicate X11 gameplay input, without
replacing the APK or reusing Skyrim engine functions. WEMU's duration-bearing
tap and standalone hold forms are now handled by the payload.

The first full compilation exposed compiler diagnostics: misleading indentation,
missing GDI+ PROPID declaration and pedantic thiscall free-function-pointer ABI
warnings. Control-flow formatting and the required COM include were corrected;
native thiscall aliases use fastcall with an explicit unused EDX argument, as in
the existing hook ABI. All call sites supply that argument. The complete i386 DLL
then built with `-Wall -Wextra -Wpedantic -Werror`, without runtime library DLL
dependencies. The original executable's 232 native/schema evidence checks and
26 compiled touch packet cases passed. These are not runtime acceptance tests.

`2026.09.06-wemu-dev1` contains the DLL, configuration and 117 original art/font
resources plus their asset manifest (120 installed files). All installed SHA-256
values match the local package manifest. DLL hash:
`26147e45931f3906672b7c7a287663850b0bf4c878381d3114116302a3be0939`.
The WEMU container 5 configuration remained byte-identical; FEXCore 2608 and
`displaySecondScreen=1` were retained. The `Dishonored Dual Screen` desktop
shortcut uses `WINEDLLOVERRIDES=dinput8=n,b`. The old DLL/INI are backed up at
`/sdcard/Games/Dishonored/.dual-screen-backups/20260906-200731` and locally.
The final receipt is `.work/dishonored/install-test/installation-receipt.json`.
ADB shell text output adds CRLF, so the shortcut was verified by remote SHA-256
rather than comparing terminal text to the LF source file.

No APK was built/replaced, no agents were used, and the game was neither launched
nor controlled. Both-display appearance, native actions and lifecycle behavior
still require the user's actual test. Live map positioning/facing/marker
calibration remains unfinished and is not represented as implemented.

## Failed startup and shared-payload conflict, September 6, 2026

The user reported that WEMU no longer launched the game. Read-only collection
found no full-mod startup log or native-fault file. The initial 20:18 WEMU
logcat records do not contain a definitive game exception. A later launcher
attempt has a persistent receipt at
`.work/dishonored/startup-failure/launcher-failed-launch.log`: at 20:19:48 it
verified `0.0.2-probe` before starting the shared Dishonored executable. The DLL
subsequently hashed to the old probe (`5807d77b...`), not installed `dev1`.
The launcher cache's `current` and `development-package` both pointed to
`360572c6d719efcf81c9ebf9d8b03e06d70e03f1846b21057d4d269ca573f0bc`.
This proves a deployment conflict, not the cause of the earlier WEMU failure.

The first headless WEMU loader test unintentionally tested that replaced probe;
this limitation was recognized and corrected. The subsequent test loaded the
hashed full diagnostic DLL from a separate `.dual-screen-diagnostics` directory
and called its DirectInput8Create export. The DLL loaded and returned S_OK in
both Windows and WEMU. The WEMU log confirms the native proxy and separate
built-in system DirectInput module, then the worker correctly rejects the
diagnostic executable before installing hooks. Headless-display and fake-HID
service warnings in that helper run do not represent game acceptance results.
Receipts are `wemu-full-loader-test.log` and `wemu-full-proxy-test.log` in the
startup-failure directory. No game was launched and no device input was sent.

The full proxy previously logged only after executable verification. It now
sets its module-relative log path once outside DllMain, records the first export
entry and native forwarding result, and logs worker entry before verification.
Worker creation/module-pin failures are logged and can retry on a later valid
DirectInput call. Build `2026.09.06-wemu-dev2` passes the full warnings-as-errors
compile and the isolated loader checks. Every imported function was also checked
against WEMU's installed i386 Wine exports; none were missing.

The diagnostic package was installed to the shared game folder and to the
launcher's verified on-demand development cache, with the old versions retained.
Both package pointers now select the same full build, preventing the launcher's
old pinned probe from replacing it. All 120 installed and 122 cache file hashes
verified. No APK changed. WEMU's container and game registry were preserved.
Before this investigation, the game's own saved ResX/ResY had changed from
1280x720 to 600x800; the container JSON was unchanged. Launcher diagnostics also
observed a 1280x1800 game window. These are unresolved display observations,
not proof of a crash mechanism.

The WEMU **Dishonored Diagnostic** and **Dishonored Baseline** shortcuts run the
same executable and settings, respectively with native-mod and built-in-only
DirectInput selection. They capture Wine module-loading errors and process exit
status for the user's next controlled comparison. Actual startup failure cause
remains unconfirmed until that game test; the user's hands-off instruction
precludes launching or controlling gameplay during this investigation.

### WEMU shortcut visibility and first full game-bootstrap receipt

The user could see `Dishonored Probe` in WEMU's Games list but not the two
new test entries. The files were present in the correct container desktop.
The tests initially targeted `cmd.exe /c <capture script>`; both entries were
changed to target Dishonored.exe directly, like the visible Probe entry, with
native/built-in selection in `envVars`. This avoids depending on WEMU exposing
system-command shortcuts in its game list. Visual list refresh is still user
controlled. The CMD capture scripts remain available but are no longer invoked
by these direct shortcuts; mod bootstrap logging remains active, and Wine loader
output goes to WEMU's console. `direct-shortcut-receipt.json` records the hashes.

The user's latest game launch produced the first full bootstrap receipt:
`startup-failure/user-probe-DishonoredSecondScreen.log`. It confirms native
DirectInput S_OK, exact EXE acceptance, successful original-asset initialization,
all five hooks installed and UDP 7788 ready. Its monitor query then reports only
one primary display, **600x800**, so the lower panel correctly waits rather than
covering gameplay. The captured native-fault file is empty. This establishes
that full mod initialization was reached in this launch, but does not prove the
main menu appeared or identify why gameplay reportedly failed. It does confirm
that the live Wine monitor topology differs from the configured two-display
layout. The saved 600x800 game resolution and fullscreen mode remain relevant
to the next display-initialization investigation.

### Successful baseline and verified windowed launch preparation

The user confirmed **Dishonored Baseline ran**. Read-only collection found the
live game process in WEMU container 5 with `WINEDLLOVERRIDES=dinput8=b;winemenubuilder.exe=d`,
`HODLL=libwow64fex.dll`, and `DISPLAY=:0`. WEMU's compositor imported and presented
1280x720 frames; the single-monitor RandR report also gave 1280x720. The game
registry contained ResX=1280, ResY=720, Fullscreen=1. This establishes baseline
startup, not long-term stability or mod compatibility. The temporary baseline
had disabled the second display as well as bypassing the mod, so this comparison
does not isolate hooks from display setup.

Native disassembly of the exact executable verifies supported launch overrides:
00637465-0063748d parses `ResX=`/`ResY=` into 01437298/0143729c;
00637495-006374b6 checks `WINDOWED` and clears 014372a0; the alternative
`FULLSCREEN` branch sets that flag. At 00637543-00637561 these values feed the
original viewport-creation call. Parser 00414470 accepts a dash or slash prefix;
00414c10 returns the original command-line buffer. Evidence is saved in
`.work/dishonored/recovery-launch/native-startup-arguments.txt`. No executable
patch or assumed Unreal launch syntax was used.

After the user's explicit "done" response, idle-process checks passed and the
Dual Screen shortcut gained `execArgs=-WINDOWED -ResX=1280 -ResY=720`. Only
container extraData.displaySecondScreen changed from 0 back to 1; all remaining
settings were compared structurally and preserved. Registry and save data were
not edited. DLL SHA-256 62b0b01f... and both launcher package pointers 452f68fe...
matched the full dev2 installation. The single-display baseline container,
registry and original shortcut were backed up under
`.work/dishonored/recovery-launch/before-windowed-dual/`.

This is a prepared display-compatibility trial, not a proven fix. The user
retains control of launching and gameplay; no device input was sent in this
continuation. Receipt: `windowed-dual-installation.json` in the same directory.

### Launcher provisioning failure after the successful WEMU mod launch

The user subsequently confirmed the full mod launched; captured top/bottom
images show the framed game window centered too low and the live lower panel.
The user requested DualScreen Launcher integration, keeping launch/input under
their control. Launcher code 42 retained the native windowed startup and added
session-local renderer client fitting, FEXCore 2608, and the verified imported
Turnip A8XX Draft shortcut override. Its first user launch was blocked by
"The captured preset has no Box64 version" before Wine started.

Source tracing identifies the cause in `ComponentProvisioner.ensure`: both
`prepare` and `prepareForLaunch` passed a JSON object containing only the
per-game `fexcoreVersion`. The provisioner unconditionally validated Box64 and
renderer fields after component verification. The full WFC preset already has
both fields; changing its settings would mask the request-scope error.

Code 43 introduces `CapturedRuntimeRequirements`: a full preset validates and
names its fixed assets, while a FEXCore override names only its exact manifest
component (`FEXCore-FEXCore-2608-stable-0`). The provisioner still requires the
captured package and matching installation receipt, and verifies archive size
and SHA-256 when installing. Six new regression tests cover this separation,
manifest identity, already-qualified IDs, invalid overrides, missing full-preset
fields, and unchanged real preset JSON. All 36 targeted launcher/renderer tests
and the APK build passed. The signed APK preserves access configuration, all
123 bundled WFC assets, and 400 installed Switch artwork files. Dishonored's
payload remains independent. APK SHA-256:
`a536640e3c1603a70c9de6206a2e4f2acf7482f1df0aeaa392b20f0af82b2e50`.

Installation evidence is recorded in `launcher-installation.json` under
`.work/dishonored/recovery-launch/`; earlier code-42 receipts are retained with
the `-version42` suffix. Runtime launch and corrected screen fitting still need
the user's Launcher session; these unit tests do not establish gameplay acceptance.

### User-verified DirectX versions (2026-09-07)

The user reported that WineD3D eliminated flicker but ran at approximately
3 FPS. Switching DXVK versions then removed the majority of the issues and
made the game playable. They requested DXVK 2.6.2 and VKD3D 2.9 for Dishonored.
The installed WEMU package identifies the tested DXVK as
`DXVK-2.6-2-gplasync-0`; its container still recorded VKD3D 3.0b, so 2.9 is an
explicit requested selection, not a claim about the earlier comparison.

Dishonored now has per-shortcut DirectX versions, separate from the shared
WFC container settings. New shortcut creation and launch migration preserve
all other DirectX options; provisioning includes the exact component IDs.
The local signed maintenance helper applied these overrides to the installed
launcher without rebuilding it, verified 21 component files, and confirmed
the shared `.container` stayed byte-identical. Receipt:
`.work/dishonored/dxvk262/installer/settings-result.txt`. The game was not
launched or controlled. This is a configuration fix based on the user's test,
not a verified native game-code fix or a claim that every visual issue is gone.

### Shared WEMU/Launcher flicker investigation and native placement (dev3)

The user confirmed Launcher43 opens, initially without audio and with a narrow
viewport after loading a save. A driver change improved fullscreen/performance;
the user then observed texture flashes and black flicker in both apps. The
shared native payload remains a suspect, not a proven explanation for every
artifact. No fatal records were present in its native exception log.

Compositor logs give game client (323,482,1274,716), while the GDI panel is
(0,720,1240,1080). These rectangles overlap inside Wine even when host fitting
makes the upper output look fullscreen. WFC already separates the real windows
on its game thread. Dishonored's `UpperViewport` follows that placement approach,
but validates the actual Dishonored class WNDPROC rather than copying WFC's
class name. Native 009ca746 assigns 009ca440 to WNDCLASSEX.lpfnWndProc;
009ca7a3 calls RegisterClassExW; 009c68f7 stores CreateWindowExW's HWND at the
viewport's +68. The evidence verifier now checks these three instructions.

The correction is restricted to the exact stacked monitor, same process and
owning thread, one unique visible/enabled native candidate, no modal/drag state.
It removes borders and uses non-activating SetWindowPos to (0,0,1280,720), then
verifies the client/outer rectangles. A resize attempt skips sampling on that
tick; no renderer hook, game resolution global, save file or registry is edited.
Controller sampling and all five original hook forwarding paths remain active.
Fake Win32 boundary tests verify lifecycle and rejection cases; the real game
still needs visual acceptance. Installed package SHA-256 is
6119aa99f80f4626bfdee4d81ea340877e12c0b50444a7fce489f070d9ad4a98.

Separately, a silent AAudio device check reproduced a 1920-frame Wine ring
stalling behind the host's rounded 1922-frame startup threshold. Priming the
two-frame gap advanced the real consumed cursor and played exactly 9600 guest
frames in the regression. The user subsequently reported fixing audio in the
Launcher app. No device audio settings were changed by this investigation.
The proposed registry resolution rewrite was removed before the combined
release build, following the user's report that the driver change fixed fitting.
