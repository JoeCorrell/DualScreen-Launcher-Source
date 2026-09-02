# Xenoblade Chronicles X: Definitive Edition dual-screen research

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
  stride `0x14`. Native `main+0x174D14` selects the handle at `+0x0C`, or
  `+0x10` when byte `+0x16` is nonzero. Byte `+0x15` is the active/player-
  controlled flag.
- `main+0x01D55130` points to the runtime handle pool. Resolving the controlled
  actor handle yields the player handle object. Field `+0x28C` is the
  character ID, not a level; `+0x28E` is the party slot. The native `GetLv`
  path is a virtual call through the actor component and has not been replaced
  with an unsafe off-thread call.
- `main+0x34C9B0` resolves the authoritative four-record roster at game state
  `+0x1B5DC+(slot*8)`. Record `+0` is roster state, `+1` is character ID, and
  `+4` contains roster flags.
- The player handle object points to its entity at `+0x1F0`. The validated
  transform chain is entity `+0x1AC0` -> manager `+0xC8` -> state `+0x50` ->
  root transform. Root position is `x/y/z` at `+0x00/+0x04/+0x08` and its
  quaternion is at `+0x10..+0x1C`.
- `main+0x01D557A8` points to the battle registry. The game's
  `GetEnemyNumInBattle` implementation at `main+0x003D3750` walks 32 buckets
  starting at registry `+0x70`, follows the bounded linked lists, resolves
  actor handles, and tests the active battle flag. The companion reproduces
  only this read-only collection/counting path.

Every pointer chain must be range-checked before dereference. Plausibility
checks must reject transient loading/uninitialized values rather than exposing
them as valid UI state.

`resetCameraForMapJump` is explicit evidence that the native map-jump path can
touch the gameplay camera. Presentation pan/zoom never calls it or reuses the
top-screen camera. An explicit fast-travel request instead enters the complete
native map-jump path at `main+0x131358`, which owns party formation, placement,
events, and camera/state transitions.

### Proven native actions

Controlled-character switching follows the game's complete path:

- `changeParty` (`main+0x3EC520`) resolves the selected actor and character ID.
- `main+0x2FB338(manager, characterId)` stores the request at manager `+0x88`
  and sets bit `0x100`.
- Party-manager update `main+0x2FB350` consumes the bit and calls
  `main+0x40DB98`.
- `main+0x40DB98 -> main+0x934E14 -> main+0x522898` dispatches native event
  `0x3C`, leaving actor, camera, animation, and party synchronization to the
  game.

The exact-build companion hook replaces the first instruction at
`main+0x2FB350`, replays it, queues through `main+0x2FB338`, restores all
volatile integer/vector registers, and resumes the original update. It never
writes character IDs, active flags, or actor pointers directly.

Fast travel uses `main+0x131358(formationId, 0)`, the implementation reached by
`reqMapJumpAndRideDoll`. Formation IDs come from the live destination's
`FLD_Location -> FLD_SkipTravel -> FLD_FormationList` join rather than from a
presentation-only coordinate teleport.

Both actions are rejected on the game thread when `main+0xCC394` reports an
event scene, the pause/player manager at `main+0x1D54EC0` has `+0xB0` flags,
or native battle collection `main+0x1F5750(..., 0x20)` reports an active battle.
Character switching additionally rejects actor type `2` (the live Skell actor)
through the same selected-actor root used by the native `changeParty` path.

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
