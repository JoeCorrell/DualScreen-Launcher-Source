# Xenoblade Chronicles X: Definitive Edition dual-screen research

## September 7 resumed ADB session and equipment inventory

The user subsequently instructed source-only development: **no more builds,
no real-time inspection, and no installation**. User will perform in-game
testing after implementation. Preserve this constraint in continuations.

Before that instruction, ADB reconnected to the existing Thor transport at
`100.83.63.70:41335` (43929 refused). Native map and secondary-display captures
are under `.work/xcx-integration/live-map-sept7.png` and `live-bottom-sept7.png`.
The native map was open in Administrative District, zone 1401. Logs reported
changing actor world coordinates, but the secondary display stayed at
checkpoint 0. Installed package was version 57 / 1.6.17.28-crysis-update,
updated 18:51:45. A mismatched packaged JNI/UI snapshot is the leading cause;
do not call this an in-game verification of the current source. The source
now reports an incompatible frame instead of silently waiting forever.
The Kotlin check completed before the user's no-build instruction.

Probe picker affordability now uses live credits and pending action state;
the native action still rechecks all requirements. The prior XCX Kotlin run
passed 23 tests. Basic probes remain free; unavailable stock or balance blocks
paid changes instead of assuming availability.

Equipment compatibility is native `0x405F5C(record, UI_slot, item_ID,
check_level, out_reason)`, with a forwarding wrapper at `0x4061C4`.
It checks armor sex restrictions, character 22 restrictions, current class
weapon types, avatar mastered-class weapon exceptions (`0x4061C8`), and
`EquLv` against record byte `+0x7A` when requested. Reasons include 1 level,
2 class, 3/4 sex, 8 character restriction. Inventory-menu callers at
`0x631804/0x631828` calculate both level-checked and unchecked results.

New `ReadEquipmentInventory(category)` / JNI `readXcxEquipmentInventory`
copies the native category vector, filters empty/other-category entries,
and double-checks the copied bytes and vector header. Category vector indices
at `main+0x17EFAF8` are `3,3,3,3,3,1,2`. Root is inline `main+0x1F919A0`
(GOT `0x1D55B58` points to `0x1F91990`, then native adds 16).
Gear entries are 64 bytes. `0x13C490` constructs key
`(record_u32_at_8 << 6) | category`; `0x13BF80` resolves that exact instance.
The private XXEI v1 packet contains header magic/version/category/count and
16-byte item records: static ID, full key, packed flags, raw identity word.
Kotlin parser validates bounds and unique instance keys. Source now wires the
12 slot buttons to a paginated owned-inventory picker, touch selection/Equip,
D-pad selection, A confirmation, L2/R2 pages, and B/Back navigation. Polling
invalidates the view when inventory/equipment changes independently of the
main snapshot. This new implementation has not been built or exercised.

Action 6 uses the full 32-bit instance key as `argument`, with character and
slot bytes in mailbox `reserved`. JNI preserves unsigned keys, validates the
character/slot/category, and publishes context before the release store.
The game-thread bridge checks bounds, live character identity, all 23
characters' 12 occupied slots for an existing owner, resolves `0x13BF80`, then
calls `0x405F5C` with level checking for real equipment and without it for
fashion. It queues `0x132CB8` or `0x132EE8` with live actor refresh enabled
and normal-menu inventory transfer disabled. Already-equipped instances are
rejected; automatic transfer from another character and unequip remain work.
Native function signatures are checked before hook installation. Snapshot
version is now **9**, still 308 bytes, to reject older libraries that lack the
inventory JNI and action. The emitter fixture includes 17 additional equipment
cases and Kotlin includes two inventory cases, all **unrun** under the user's
no-build instruction. No subsequent build, ADB inspection, or install occurred.

Ownership caution: standard menu `0x7E1E10` queues equipment through
`0x132CB8`/`0x132EE8` with refresh/transfer flags zero. Setting transfer true
re-adds old equipment and clears the selected inventory entry via `0x13E244`;
do not assume that is correct for the normal inventory menu. Exact native
ownership handling must be followed to preserve instances and upgrades.

Additional native resources extracted before source-only instruction:
`04010_map_cont04/05/06.wilay`, `03150_map.wilay` in the ignored research
archive-selected directory. cont05 defines MapMain/Layout/Icon/Player/Area/
Balloon layer rectangles; cont04 and cont06 define map and quest icons.

Map-cell JNI packets are now **48 bytes**: existing 44-byte content plus a
visibility word at 44. Bit 0 marks a known probe-visibility predicate; bit 1
hides an early probe; bit 2 shows the related-tip badge. `0x5641B4` hides kind-1
icons until story progression 2008 unless the cell's u16 `+0x12` is 1.
`0xC97A8` reads progression through `[main+0x1F94DF8] -> +8 -> +0 -> +0x5802`.
`0x562DE0` shows related tips only after `0x54AD9C`, with nonzero related ID
at `+0x1C`, a satisfied optional one-bit related flag at `+0x1E`, and discovery
for probes. Exact BDAT condition `C7A542CD` is FLD_GameCondition row 8395,
scenario condition 32 (`S_FLG_MIN=0`, `S_FLG_MAX=2007`); `0x54AD9C` negates
that condition. Renderer now uses native icon-sheet cell 13 for visible tips.
Probe hits and pickers follow visibility, and unavailable sites close pickers.
These changes and additional map parser fixtures remain unbuilt/unrun.

## September 7 continuation: party ordering and equipment trace

The Party page now supports exchanging occupied companion slots through native
`0x1367B4`, preserving the leader and roster membership. Touch MOVE UP/DOWN and
controller X/Y submit action 5. Its argument contains four bytes: source slot,
destination slot, expected source character ID, expected destination character
ID. Both JNI and the game-thread bridge reject changed occupants, invalid or
empty slots, and missing state. Existing combat/event/pause/Skell guards apply.
The bridge builds a complete roster copy on its own stack, swaps whole
state/character pairs, and queues the native transition. It never writes the
live roster directly. Success reports that the transition started; UI values
continue to come from the game snapshot.

Validation: native Android library build and XCX Kotlin unit tests pass; the
ARM64 emitter harness now passes 55 cases, including 19 ordering cases.
These stubbed native-call tests establish bridge correctness, not completion
of an actual game transition. No APK was installed or packaged. The packaged
runtime JNI library has not been replaced with this build. ADB at
`100.83.63.70:43929` actively refused the connection during this continuation.

Reserve replacement is still separate work. Native reserve-row rendering at
`0x6A70B4` calls `0x56BAD0` (`icon_cond`), `0x56B884` (`in_cond`),
`0x56B984` (`test_cond`), and `0x56B990` (15 `one_cond` entries). These resolve
the native table index `0x16C` by `pc_id` and evaluate native game conditions.
The menu also checks character-pair exclusions for IDs 6/10 and mission IDs
1475/1479. Do not replace these conditions with an unlocked-character guess.

Equipment investigation confirmed the following. The companion now exposes
read-only equipment inspection; item selection and equipment changes still
require native compatibility validation:

- `0x34AC50(state, character, out, reason)` copies an 840-byte character record.
  State 1 uses the avatar; state 2 accepts character IDs 0..22, taking the
  record from `[main+0x1DC1408] + character*0x7F8 + 0xF8`.
- `0x405B1C(record, UI_slot)` resolves 12 equipment slots at offsets
  `80,B4,11C,E8,184,150,1B8,220,1EC,288,254,2BC` (hex).
  `0x405D28` maps those slots to inventory categories
  `6,7,1,2,4,3,5,1,2,4,3,5`.
- `0x13BF80(inventory, &key)` resolves an existing inventory item by category
  `key&63` and instance identity `key>>6`, comparing the latter to item `+8`
  masked with `0x03FFFFFF`. Static item IDs alone are not instance identities.
- `0x132CB8` queues weapon equipment, copying the character record and a
  64-byte item record into its own task. Task `0x12863C` calls `0x10AB20`, which
  reloads current character state, saves via `0x34AA20`, updates matching live
  party actors via `0x343AF4`, and handles weapon-type changes.
- `0x132EE8` similarly queues armor; task `0x128684` calls `0x10B290`, which
  also handles armor occupying multiple body slots and fashion equipment.
- The caller at `0x8DA114` is a purchase/equip path, not an existing-inventory
  selector: `0x13D190` checks inventory capacity and `0x13E8BC` can add an item.
  Never call those as supposed ownership/compatibility checks. Native class,
  level, gender, ownership and slot validation remain to be traced before
  exposing equipment mutations.

Remaining acceptance work includes reserve-member replacement, equipment selection and changes,
FrontierNav production/link controls, conditional map-marker and hex-edge
presentation, world-map visual parity, and live verification of all actions.

### Live equipment inspection

The Party page EQUIPMENT button (controller R2) opens a 12-slot inspection
view. Back returns to the party. It binds the selected character ID when
opened, so party reordering cannot switch the displayed character accidentally.
The poller refreshes only while the view is open, every 500 ms.

`ReadEquipment` validates the exact build, character range 0..22, record
state/identity/level, readable memory and item-ID bounds. It copies the entire
native character record twice and rechecks the root, rejecting inconsistent
reads. It exports a private 208-byte XXEQ v1 packet: four u32 header fields
(magic, version, character, count=12), then 12 records of four u32 fields
(UI slot, native category, static item ID, instance key). Empty slots export
zero instance keys; nonempty slots preserve all 32 instance-key bits.

Runtime XEQN v1 metadata joins WPN_PcList/AMR_PcList Name fields to their
localized message tables. MNU_EQUIPPART texID labels are blank in portions of
the inspected US message file, so the view uses verified English slot labels
there: category 6 ranged weapon, 7 melee weapon, 1 head, 2 torso, 4 left arm,
3 right arm, 5 legs, followed by the five fashion slots. DEF_PcList
DefWpnFar/Near and DefAmr1..5 confirm these meanings (for example Elma's
DefAmr3 is category 3, Armwear R). The original slot ordering is retained.

Three equipment parser tests cover wrong-character packets, all truncation
lengths, malformed slots/categories/items, full unsigned instance identities,
live item changes and separate weapon/armor name namespaces. Native library
and Kotlin unit builds pass. No APK installation, packaging, or runtime JNI
copy was performed. Live inspection remains unverified while ADB refuses the
last supplied endpoint.

### Segment descriptions and controller destination selection

The XSGL catalog is now version 3. After its existing field/icon/quest-parent
sections, it includes bounded content-ID/title/description records. Titles and
descriptions join SEG_NormalList `info_title` / `info_summary` to
`SEG_Segment_ms`, not to quest IDs. For example content row 1 joins title 1
(Heart-to-Heart) and summary 589 (Celica's affinity/time/prerequisite details).
The inspected description strings are at most 116 UTF-8 bytes; the parser
accepts up to 16 KiB per field with truncation and identity checks.

Discovered kind-2 cells without a mission reference now open these details
through touch or controller confirmation. Mission and probe interactions keep
their existing native paths. The detail panel scrolls with touch, direction
controls or triggers, and closes with Back/confirmation or CLOSE. Its selected
cell is refreshed by field, ID, column, row and kind; completion follows the
native two-bit flag and missing/undiscovered cells dismiss the panel. This
does not implement the remaining native conditional lock/badge presentation.

Controller fast travel also now requires the selected destination to remain
under the crosshair on the second confirmation. Previously a retained selection
could trigger travel after panning away. A different centered pin is selected
before it can be confirmed.

Native library build and all 22 XCX Kotlin tests pass, including variable-length
segment descriptions, preserved paragraphs, invalid content IDs and truncation.
No APK was packaged or installed; the runtime JNI library copy remains unchanged.

Further equipment tracing: `0x7E1E10` applies edited party equipment from menu
records at owner+`0x409900`, stride `0x698`, dirty byte `+0x694`, party slot
`+0x690`. It compares IDs/instance identities and queues the already identified
native equipment tasks. Do not call this with a fabricated menu object.
`checkGenderEquip` (`0x3F0020`) examines existing avatar armor for sex-specific
models; it is not a general candidate-item compatibility validator.

### FrontierNav continent/site navigation

FrontierNav's site metadata now follows the selected continent, rather than
always using the player's current zone while showing another continent's
survey score. Its selector order resolves to native map zones
1101,1501,1601,1701,2001,2201 (NLA, Primordia, Noctilum, Oblivia, Sylvalum,
Cauldros). The existing runtime metadata and discovery/open flags supply sites.
The NLA target is the native 1101 map sector, not a fabricated aggregate of
its separate district tables.

The former four-site-only display is now paginated. Touching a listed site
opens its real map cell and, when the cell is open, the native probe picker.
VIEW MAP opens the selected region without selecting a probe. Controller
left/right selects a continent; up/down selects a site; triggers page through
sites; A opens the selected site or the region if no site is selected.
The existing native InstallProbe action remains responsible for changes.

Map routing retains the requested region/site during asynchronous atlas/cell
loading and suppresses initial player auto-centering for explicit navigation.
Pending probe resolution uses the live cell's native field/ID and anchor/grid
coordinates. Panning, zooming, finding the player, opening segment details,
or choosing a different region cancels the pending site jump. No synthetic
probe record or site coordinates are generated.

Kotlin compilation and all 22 XCX tests pass; scoped whitespace checks pass.
No native ABI change, APK build or installation was performed in this pass.
ADB still actively refused 100.83.63.70:43929 at verification time.

This is the living interoperability record for the native Xenoblade X DE
dual-screen subsystem. Keep confirmed observations separate from hypotheses so
offsets and structures are not repeatedly rediscovered or treated as valid
without evidence.

No game executable, encryption key, ticket, database, or proprietary asset is
stored in this repository. Research extracts remain under the ignored
`.research/` directory and must never be committed or shipped in the launcher.

## Target build

The user's North American base game and update were inspected directly over
ADB. The active target is:

- Program title ID: `0100453019AA8000`
- Update title ID: `0100453019AA8800`
- Display version: `1.0.2`
- Update content version: `v131072`
- Program SDK: `19.3.2`
- Update meta SDK: `20.5.0`
- Updated `main` NSO build ID: `2A720C7CE5C84905C1C6847211F670FBB31842B6`
- Base `main` NSO build ID: `40229B9405F4DBED`

Only build `2A720C7CE5C84905C1C6847211F670FBB31842B6` is initially supported. Runtime hooks and direct
state reads must fail closed for every other build. Build-specific addresses
belong in one version descriptor, not scattered through UI or JNI code.

The 1.0.2 update changes `main`; `rtld`, `sdk`, and `subsdk0` are byte-identical
to the base package. The updated flat `main` image has these segments:

| Segment | Address | Size |
| --- | ---: | ---: |
| text | `0x00000000` | `0x01735050` |
| rodata | `0x01736000` | `0x004E7000` |
| data | `0x01C1D000` | `0x0019F848` |

## Package and asset layout

The base NSP's PFS0 file data starts immediately after its string table. It is
not implicitly aligned to 16 bytes. Assuming that padding shifted every NCA
read by nine bytes for this package and produced invalid NCA headers. The
shared selective NSP tools now follow the PFS0 format instead of adding that
padding.

The base program RomFS contains only 53 top-level files. Most game content is
inside:

- `/sts.arh`: ARH2 index, 104,818 entries, 16-byte member alignment
- `/sts.ard`: indexed data archive, about 10 GiB

ARH2 entries contain an XXH64 normalized path hash, on-disk size, and expanded
size. Member offsets are the cumulative 16-byte-aligned on-disk sizes. Members
may be wrapped in `Xbc1` and compressed with zlib or Zstandard.

`extract_xcx_archive_files.py` resolves requested paths through the local ARH2
index, translates the member range through RomFS/NCA/NSP offsets, retrieves
only that encrypted range over ADB, decrypts it with user-supplied keys, and
then expands Xbc1. It intentionally has no extract-all mode.

Verified selectively extracted resources include:

- `/bdat/common.bdat`
- `/ui/image/icon_segment.wilay`
- `/ui/image/icon_dataprobe_img.wilay`
- `/ui/image/01020_segment.wilay`
- `/ui/image/04030_frontiernet_cont01.wilay`

The definitive-edition UI resources use `wilay` containers (`LAHD`/`LAGP`
headers), rather than the Wii U `drc_*.bmn` paths. The old GamePad file list is
useful for semantic comparison, but those paths are not present under their
old hashes in the Switch archive. UI code must not silently substitute the Wii
U resources.

## Static gameplay data

`common.bdat` was parsed successfully as modern BDAT format version 2. The
definitive-edition tables retain many Xenoblade X identifiers. Confirmed tables
relevant to the companion include:

- `SEG_ProbeList`
- `FLD_Fnet`
- `FLD_ConditionList_FNStat`
- `FLD_ConditionList_Probe`
- `FLD_ConditionList_Segment`
- `FLD_Location`
- `FLD_FieldNameList`
- `FLD_SkipTravel`
- `FLD_questlist`
- `FLD_QuestTask`
- `FLD_TargetList`
- `BTL_ArtsList`
- `BTL_PcArtsInfo`
- `BTL_DlArtsList`
- `BTL_SoulVoice`
- `BTL_BuffList`
- `BTL_BufEffList`
- `CHR_DlList`
- `CHR_EnList`
- `CHR_EnParam`

For example, `SEG_ProbeList` exposes `flg_disp`, `zone`, `vein_id`, and six
segment-terminal links. This is real static topology and will be joined with
the live save/runtime state; it is not a substitute for discovery, installed
probe, inventory, or production state.

The 1.0.2 patch RomFS overlay still needs to be checked before generated static
metadata is considered final. Runtime game state remains authoritative.

## Confirmed 1.0.2 code paths

The updated executable contains a name/function registry. The following are
confirmed wrapper addresses, not guessed symbols:

| Function | `main` offset |
| --- | ---: |
| `hideMap` | `0x003EC3D0` |
| `changeParty` | `0x003EC520` |
| `setMapLodDisp` | `0x003EC670` |
| `setMapLodEnable` | `0x003EE6D0` |
| `setPartyPos` | `0x003EED00` |
| `reqMapJumpAndRideDoll` | `0x003EED90` |
| `getOffDollAndSetPartyPos` | `0x003EEE30` |
| `addMiranium` | `0x003EFA20` |
| `getSegmentScore` | `0x003EFF10` |
| `hasMyDoll` | `0x003F04F0` |
| `isCondSegmentMatch` | `0x003F0770` |
| `repairDoll` | `0x003F0BB0` |
| `resetCameraForMapJump` | `0x003F13F0` |
| `dollFuelFull` | `0x003F19E0` |
| `openPartyChange` | `0x003F2EB0` |
| `closePartyChange` | `0x003F3050` |
| `setPartyNums` | `0x003F3080` |
| `isFinishPartyChange` | `0x003F3130` |
| `openMapAllClear` | `0x003F3BD0` |
| `closeMapAllClear` | `0x003F3C80` |

The executable also registers combat/AI helpers including `BattleStart`,
`OpenArts`, `GetEnemyNumInBattle`, `GetHP`, `GetMHP`, `GetLv`, `GetDollFuel`,
`GetPartsHp`, `GetCurrentWpn`, buff/debuff queries, Soul Voice queries, and
target/appendage queries. Registry presence proves the functions exist, but it
does not by itself prove a safe calling convention for companion actions.
Action calls stay disabled until their owner object, arguments, thread, and
preconditions have been verified in the running game.

### Proven state roots and fields

Disassembly establishes these 1.0.2 relationships:

- `main+0x01DC1408` stores the main game-state pointer.
- Miranium is a 32-bit field at game state `+0x001B5D0`.
- The game's `addMiranium` path clamps the value to `99,999,999` and emits a
  state notification after changing it.
- `main+0x01D58F60` is a relocated pointer to the segment-state array root.
- Segment state entries have stride `0x2510`; `getSegmentScore(index)` reads a
  32-bit value at entry `+0x28`.
- `getZone` obtains the field manager through the relocated pointer at
  `main+0x01D55148` and reads its 32-bit zone value at manager `+0xAC`.
- `getLastClearedQuestId` reaches a 32-bit field at state offset `0x89F0`
  through the relocated root at `main+0x01D55638`.
- `main+0x01D55110` points to the four controlled-actor slots. Each slot has
  stride `0x14`. Native `main+0x174D14` selects the handle at `+0x0C` when
  byte `+0x16` is nonzero, otherwise `+0x10`. Byte `+0x15` is the active/player-
  controlled flag.
- `main+0x01D55130` points to the runtime handle pool. Resolving the controlled
  actor handle yields the player handle object. Field `+0x28C` is the
  character ID, not a level; `+0x28E` is the party slot. The native `GetLv`
  path is a virtual call through the actor component and has not been replaced
  with an unsafe off-thread call.
- `main+0x34C9B0` resolves the authoritative four-record roster at game state
  `+0x1B5DC+(slot*8)`. Record `+0` is roster state, `+1` is character ID, and
  `+4` contains roster flags.
- The player handle object's world position is `+0x50/+0x54/+0x58`, proven
  by native distance functions `0x3CA760` and `0x3D3EE0`. The entity chain
  `+0x1F0 -> +0x1AC0 -> +0xC8 -> +0x50` resolves a model transform; using
  its translation caused build 54's player marker to appear outside the map.
  Quaternion reads still use that transform and require separate heading proof.
- `main+0x01D557A8` points to the battle registry. The game's
  `GetEnemyNumInBattle` implementation at `main+0x003D3750` walks 32 buckets
  starting at registry `+0x70`, follows the bounded linked lists, resolves
  actor handles, and tests the active battle flag. The companion reproduces
  only this read-only collection/counting path.

Every pointer chain must be range-checked before dereference. Plausibility
checks must reject transient loading/uninitialized values rather than exposing
them as valid UI state.

September 7 source corrections (snapshot ABI version 5, 248 bytes):

- Character zero is the avatar. Accept it only with a resolved live actor whose
  identity also equals zero; empty zero-filled records are not sufficient.
- Character labels join `DEF_PcList.Name` to `DEF_PcList_ms.id`. Character IDs
  are not message IDs (character 11 maps to message 16, Elma).
- Object `+0x200` points to the actor component. The exact component getters
  `0x1CC92C` and `0x1CC978` read HP and maximum HP from bank 2 at `+0x250`
  and `+0x254`. The reader validates both vtable getter slots and rechecks
  component identity; it never invokes guest getters from the host thread.
- Credits are at game state `+0x1B5CC`, bounded by the native `addMoney`
  implementation at `0x34A460` to 99,999,999.
- FrontierNav save data starts at game state `+0x9A200` (`0x34D040`). The
  controller binds 144 three-byte records starting at save `+4`. Record byte
  meanings and mutation preconditions still need tracing.
- Archive entries with expanded size zero can contain real data. The local
  index has 48,992 such nonempty entries. Raw data uses its bounded disk size;
  Xbc data validates its own expanded-size header.
- A map LBIM size can include the footer. Validate required texture bytes
  against the actual payload before the footer, rather than rejecting the
  entire image because its declared size includes that footer.

The archive reader fixture tests, actor-vitals tests, mission-window unit tests,
native build, and release APK build passed. These checks do not establish that
the pages or actions work correctly in the running game; device checks remain
necessary.

`resetCameraForMapJump` is explicit evidence that the native map-jump path can
touch the gameplay camera. Presentation pan/zoom never calls it or reuses the
top-screen camera. An explicit fast-travel request instead enters the complete
native map-jump path at `main+0x131358`, which owns party formation, placement,
events, and camera/state transitions.

### Native party action and scheduling

September 7 local revision: the companion now calls `0x1367B4` with four
`(state, character)` u32 pairs at argument offset `+0x0C`. This is the native
roster task submission used by `0x41A7D0`. It copies the input and schedules
`0x121AC0`, which applies all four slots, transfers existing actor handles,
and preserves character/Skell state. The bridge swaps the selected existing
member into slot zero on its own stack, rechecks membership on the game
thread, and reads the controlled slot's Inner/Skell selector rather than the
interaction-target actor. The old `0x2FB338` call below is no longer used.
Native task completion still requires device verification; successful mailbox
submission is not evidence that gameplay/camera control has changed.

Static analysis identifies these parts of controlled-character switching:

- `changeParty` (`main+0x3EC520`) resolves the selected actor and character ID.
- `main+0x2FB338(manager, characterId)` stores the request at manager `+0x88`
  and sets bit `0x100`.
- Transition routine `main+0x2FB350` consumes the bit and calls
  `main+0x40DB98`.
- `main+0x40DB98 -> main+0x934E14 -> main+0x522898` dispatches native event
  `0x3C`, leaving actor, camera, animation, and party synchronization to the
  game. Raw disassembly confirms `0x40DBA4` tail-branches to `0x934E14`;
  decompiler inlining had previously hidden that intermediate call.

The revised exact-build companion hook replaces the first instruction at
`main+0x2FAE58`, replays it, queues through `main+0x1367B4`, restores all
volatile integer/vector registers, and resumes the original routine. It never
writes character IDs, active flags, or actor pointers directly.

**The hook executes; character-change completion is still unverified.**
September 7 version-53 device logs show capabilities `0x1FF`, including
ActionBridge, during loaded NLA gameplay. Earlier logs without ActionBridge
were from the old transition-only hook.
The only direct caller of the old hook `0x2FB350` is the `endSync` transition state at
`0x2D34CC`. The manager's actual virtual update is `0x2FAE58` (vtable
`0x1C468A8`, slot `+0x20`). Constructor call `0x2CF0A0` registers it in the
owner's `+0x348` list, which the `0x2CFDB0` update loop traverses at
`0x2CFE00..0x2CFE20`. The old pending-bit path did not start that transition
and was replaced with the roster task above. Do not report a queued
request as a verified character change.

Fast travel uses `main+0x131358(formationId, 0)`, the implementation reached by
`reqMapJumpAndRideDoll`. Formation IDs come from the live destination's
`FLD_Location -> FLD_SkipTravel -> FLD_FormationList` join rather than from a
presentation-only coordinate teleport.

Both actions are rejected on the game thread when `main+0xCC394` reports an
event scene, the pause/player manager at `main+0x1D54EC0` has `+0xB0` flags,
or native battle collection `main+0x1F5750(..., 0x20)` reports an active battle.
Character switching additionally rejects actor type `4` (the live Skell actor)
through the same selected-actor root used by the native `changeParty` path.
`GetDollFuel` at `0x3D7030` proves that type `2` is an Inner actor and type `4`
is a Skell. Party identity/health now reads the Inner handle at slot `+0x0C`,
while the controlled actor type still follows `0x174D14`'s selected handle.
This fixes avatar visibility and human vitals while riding a Skell. The
updated emitter has 13 passing ARM64 protocol cases, including Inner accepted
and Skell rejected; these tests stub native calls and do not prove switching.

### Native tiled map correction (in development)

`map02_1401.wilay` means tile `(14, 1)`, **not zone 1401**. The previous
renderer stretched one 256-pixel tile across the screen. `0x560C10` formats
tile paths from `MNU_MapInfo.level_name` and two coordinates, consulting
`condition2..5` and sparse extension planes for story-dependent artwork.

`0x67E868` loads `ui/seg/<level_name>.seg`. Each plane starts with a 32-byte
header followed by row-major coverage bytes, aligned to four bytes. Header
fields are tile width/height, columns/rows, and two unsigned 16-bit edge trims.
NLA's `map02.seg` has 21×21 tiles of 256 pixels, trimmed by 152 pixels on each
axis, yielding 5224×5224. The complete base NLA atlas was extracted and joined
offline; roads/buildings match the earlier native Segments-screen capture.

`UI.bin` loads at configuration-object `+0x310`. Map constructors `0x7FCF9C`
(Mira) and `0x89277C` (NLA) copy world extents, pixel extents, and world offsets.
The native conversion is `(world - offset) * pixelExtent / worldExtent +
pixelExtent / 2`. Mira uses 20000, 4098, offsets approximately -4997.56/-3748.17;
NLA uses 20000, 37200, offsets 6540/16206. The actual BDAT location
(-1436.5601, 7315.1621) maps to native NLA pixels (3763.5983, 2063.0415).

The new `XcxMapAtlas`/`XcxMapRenderer` reads these values and plane definitions
from the game's archives, caches decoded tiles within 24 MiB, preserves aspect
ratio, and supports Mira/continent and NLA/district navigation, pan, zoom, and
Find Self. Native tile PNGs are no longer retained in an unbounded host cache.
The old single-tile fetch is removed. Three new geometry/parser tests pass.
Native hex topology, exact native overlays/icons, and live device comparison
remain unfinished; the renderer must not yet be described as visually identical.

### Existing external signature evidence

XCXDE Mod Loader supports Western 1.0.2 and uses a signature-checked archive
hook at `main+0x013B59D0`. Its pattern is useful evidence for the required
version-gated/pattern-validated approach. It is not the planned state hook and
will not be reused blindly.

## Companion architecture

The launcher already embeds Eden and presents bottom-screen companions using a
separate Android `Presentation`. That surface is independent from the emulated
game's Vulkan framebuffer and camera. XCX should follow the proven emulator
integration pattern:

1. Exact title/build/segment gate in the NSO loader.
2. A small game-thread action payload at a stable, signature-checked hook.
3. A versioned request/result mailbox with release/acquire publication.
4. JNI copies an immutable, validated snapshot while holding emulator system
   access.
5. Kotlin maintains a cached state model and updates only changed fields.
6. The bottom-screen `Presentation` renders Map/FrontierNav independently.
7. Actions use a separate request/result mailbox and are executed on the game
   thread only after the native target has been proven.

The current bottom-display navigation has one explicit renderer per page:
Map, FrontierNav, Missions, Combat, Party, and Skell. Touch hit testing is
page-specific and consumes every touch owned by the second-display
`Presentation`, preventing input from reaching the primary gameplay surface.
The controller bridge uses a weak target, only enters companion navigation
after an explicit mode-button opt-in, and leaves normal gameplay routing
untouched otherwise. Map pan and zoom modify only cached presentation-space
map state; they never write the guest player transform or camera.

Static BDAT, map topology, labels, and UI resources are cached. Player position,
orientation, active roster, and combat state update at presentation cadence.
Mission, location-discovery, and probe-open flags are polled in bounded batches
rather than rebuilding static structures every frame. Probe inventory,
installation/configuration, production, and revenue remain separate work and
are not represented by fake UI state.

## Remaining runtime/device proof

### September 7 continuation: native segment overlay and party event consumer

Build 54 was installed and its failure reproduced via ADB: the initial Mira
overview suppressed all cells, first Find Self showed only the selected NLA
district at overview zoom, and a second Find Self centered on empty space.
Build 55 source corrects world-position reads, initially centers on the live
player, preserves that intent across asynchronous atlas loading, uses native
105px grid zoom for region/self views, and polls all fields belonging to the
displayed atlas. Region centers now derive from actual segment positions.
Content and mission icons use the native sheet and BDAT icon IDs. Probe state
follows `0x324FFC -> 0x321BB4`: root `main+0x1FEFCE0`, array pointer `+0x10`,
entry pointer at probeId * `0x1D0`, signed byte > 0 means installed.
The private segment JNI packet now has 40-byte records: the original 36 bytes
plus a little-endian u32 installed predicate. Ten Kotlin tests pass. Build 55
has not yet been verified on device; conditional quest locks, related-info
badges, exact region overview styling, and party actions still need work.

The tiled renderer now polls `readXcxMapSegments(field)` and separate one-bit
discovery / two-bit completion banks every 500 ms for the selected region.
The JNI reader bounds field IDs to 1..19 and records to 128, validates memory,
and rechecks the manager pointer and count after copying. It is a bounded live
read, not an atomic guest snapshot. The parser rejects cross-field records,
truncation, unsupported kinds, and excessive counts. Kind 4 is skipped, matching
the iterator at `0x563920`.

Native `0x7B9250` transforms the BDAT field anchor and then adds the pixel grid:
column * 78.75 and (row + odd-column * 0.5) * 105. Using converted world spacing
instead introduces small cumulative rounding errors. The renderer now uses
this exact placement, draws the grid, and uses actual `icon_segment.wilay`
sheet cells for locked/completed kind-2 segments. Objective/probe icon frame
selection, edge-mask presentation, and exact world overview styling remain
unfinished. This is not yet a claim of visual parity or device verification.

Nine Kotlin tests pass, including new negative-column geometry, independent
flag offsets, and malformed live-record cases. The updated JNI/native library
also compiles successfully.

Further party tracing: the `0x3C` event is retained by `0x90CC7C`; consumer
`0x90C3F4` constructs `0x7FF7F0`, whose initializer `0x7FFB10` dispatches through
the visitor table at `0x1C7D760`. Entry 60 calls `0x807870`, constructing
`0x8ED86C` and copying the character argument via `0x8ED8D8`. That task proceeds
through `0x8EDB50`, constructing a second UI task (`0x8F37B4`) and supplying
the character plus a true flag via `0x8F397C`. It also reaches `0x8EE12C`, which
searches all four party slots for an empty handle. This suggests roster
selection/recruitment behavior; it does not prove direct controlled-character
switching. Do not substitute this event for Take Control without further proof.
The existing `0x2FB338` queue-only call remains unverified and can stall.

The active-slot byte is written by `0x175624(slot, boolean)`, called by actor
setup at `0xF6ED4` and reset at `0x16C140..0x16C164`. This setter alone is not a
safe character switch: actor ownership/camera/controller transitions must also
be established.

- Confirm world/continent/zone coordinate conversion against movement in at
  least NLA and one continent.
- Resolve the installed-probe and owned-probe structures.
- Compare production/revenue/link results with the in-game FrontierNav screen.
- Verify character switching and native fast travel across on-foot/Skell cases
  after an explicitly authorized APK build and device test.
- Verify probe installation and mission-track action owners/arguments before
  exposing those mutation buttons.
- Prove input routing does not reach gameplay while the bottom UI owns a touch
  or controller focus action.

## September 7 probe management and character details (local, not installed)

Installation remains canceled. These changes have not been tested in the
running game and are not a declaration that all companion pages are finished.

- Map probe cells now use the native record's probe ID (`+0x10`) and current
  `ITM_BeaconList` item ID from the live save record. The 40-byte private JNI
  segment packet carries the item ID at `+36`, replacing its old Boolean.
- `xcx/probes/data.bin` exposes the 39 native probe definitions, localized
  names/descriptions, and costs. Touch or controller selection opens a picker
  with installed type, available/installed/owned counts, paging, and install.
  Inventory reads run only while the picker is open.
- Probe action 3 encodes `site | (item << 16)`. The bridge bounds site 1..143
  and item 1..39, requires an already deployed site (`0x324FFC`), and invokes
  `0x5650E4(site, item, outReason)`. Its validation `0x564FA8` checks owned
  inventory, installed usage, current type, and credits. It charges the BDAT
  cost through `0x34A460`, then calls `0x3251EC` for native saving and network
  recalculation. Nonnegative return values indicate success, including zero.
  First-time site deployment still requires visiting the site in the game.
- Owned probe counts follow `0x321DA8 -> 0x13DC50(category 28)`. Inventory is
  inline at `main+0x1F919A0`; category vector index 8 has start `+0x148`, end
  `+0x150`, and u16 stride `+0x16A`. Packed item ID is bits 0..12, category
  bits 13..18, and quantity bits 19..26. Installed usage comes from probe
  data `+0x130D4+item*4`; this is **not** owned inventory. Reads validate
  bounds and reread container identity, but are not atomic guest snapshots.
- Snapshot ABI is now version 7, 304 bytes. Skell fuel mask at 248, four
  current floats at 252, and maxima at 268 follow the deployed Skell handles
  even while on foot. Exact component getter leaves `0x1CFA2C/0x1CFA34` read
  `+0x3CF0/+0x3CF4` on the supported Skell component.
- Level mask at 284 and four u32 values at 288 follow `GetLv 0x3D5DD0 ->
  0x1D778C -> 0x1CBBD4 -> 0x33E7C8`: component `+0x98`, stat object `+8`,
  then the level byte `+0x7A`. Constructor `0x33FDA0` establishes stat vtable
  `0x1C49248`; actor creation `0xF6D38..0xF6D40` binds this object. Reader
  tests reject wrong getters, wrong tables, and changing actor ownership.
- Transform validity now has separate position (bit 0) and heading (bit 1)
  flags. Missing animation data no longer suppresses a valid world position;
  the map draws a dot when heading cannot be read.
- The native party bridge uses `0x1367B4`, described above. ARM64 protocol
  tests cover the roster pairs passed to that task, avatar and companion
  swaps, stale membership, missing state, and existing action guards. Native
  task completion, camera ownership, and persistence still need live checks.

Equipment changes, full roster recruitment/replacement,
FrontierNav production/link presentation, conditional map markers, and exact
world-map visual parity remain unfinished. Mission state setter `0xD4020`
changes quest progress and must not be used as a navigation-target setter.

### Mission tracking and map-to-mission details (next local revision)

- The native confirmation at `0x6D8F34` displays `menu_program_ms` row 467
  (track this mission), with rows 468/469 for Track/Cancel. Its accepted path
  calls `0x56E58C` at `0x6D91AC`. That function resolves `prt_quest_id`, then
  calls `0x41B9EC` on `[main+0x1DF5978]+0x8150`. It clears/rebuilds navigation
  details, writes the target at `+0x54`, and notifies the native UI through
  `0x930CA0`. Passing zero clears tracking.
- Action 4 now invokes this path on the game thread. It bounds quest IDs to
  0..2558, checks both navigation and quest managers, and requires native
  `0xD009C(quest)==1` for nonzero requests. This getter reads quest phase;
  it does not mutate progress. Completed, unaccepted, and out-of-range
  requests are rejected. Zero skips the active-phase check for clearing.
- Snapshot ABI is now **version 8, 308 bytes**, with the target u32 at 304
  and NavigationState capability bit 9. Kotlin highlights the game-selected
  mission and offers Track/Stop Tracking through touch and controller A.
  Clearing remains available if the active mission list becomes empty.
- Native `0x563B60` joins a kind-2 map cell's ID against the field's live
  content array at `+0x1940`, with count `+0x2430` and stride `0x1C` (capacity
  100). Content source type `+0x15 == 2` identifies a quest reference at
  `+0x16`, as used by `0x563F5C -> 0x56CE54`.
- The private map packet is now **44 bytes per cell**: original 36 bytes,
  installed probe item u32 at 36, quest reference u32 at 40. Static XSGL
  catalog version 2 includes native quest-parent mappings. Mission hexes
  open their real mission title/summary, including inactive missions, and
  return to the preserved map view using touch or controller Back. Tracking
  is only offered for active missions or to clear the current target.
- Native compilation and Kotlin tests passed. ARM64 bridge tests passed
  36 cases, including the mission guards and clear path. Segment parser tests
  cover child-to-parent resolution and malformed/reference-type packets.
  This tests the bridge ABI and readers, not live mission-navigation behavior.
  ADB `100.83.63.70:43929` refused the latest read-only connection attempt.
  No installation was attempted.

## Failed or rejected approaches

- Implicitly aligning PFS0 data: disproven by the package header; it shifted
  base NCA reads by nine bytes.
- Copying the complete base NSP or `sts.ard`: unnecessary and intentionally
  avoided.
- Treating Wii U `drc_*.bmn` paths as Switch assets: hashes are absent from the
  definitive-edition archive.
- Calling a name-registry wrapper without its scripting context: unsafe; the
  wrappers marshal arguments/results for the game's script VM.
- Reusing map-jump camera helpers for the companion map: rejected because they
  are coupled to gameplay camera state.
- Rendering a mock Map/FrontierNav before live state is proven: rejected by
  design.
