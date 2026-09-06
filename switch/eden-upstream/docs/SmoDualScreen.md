# Super Mario Odyssey dual-screen investigation

## Status (2026-09-05)

### 1.6.16.6 installed / gameplay verification pending

**Anywhere wardrobe correction (installed, not runtime-verified):** the
Odyssey-only prototype has been replaced. No location-name allowlist remains.
The command uses the native wear setter/random-mode reset and ChangeStageInfo
constructor (0x248cc0), GameDataHolder::changeNextStage (0x52f6f4), and Scene::kill.
It requests the same stage with scenario -1 and a unique one-use start ID,
`DualScreenOutfitReload`, not `Closet`. This means a brief normal scene reload,
not an in-place hot swap of model pointers. Native same-stage transient-state,
camera, save/reload and moving-platform behavior still need gameplay testing.

Pose transfer is at the native StageScene::init BL 0x4c9b30. That lookup consumes
PlayerStartInfo (+0xb0 position, +0xbc quaternion) into PlayerInitInfo (+0x28 and
+0x34) before PlayerActorHakoniwa::initPlayer. rs::initPlayerActorInfo 0x56f1c4
uses this initial pose to build the player's base transform. The bridge calls
the real PlacementInfo and PlayerStartInfo constructors (0x9a3450 / 0x2e2c48)
using captured al::getTrans / getQuat (0x8ee66c / 0x8ef5bc). Actual ActorInitInfo
still comes from the stage's real PlayerStart placement. No per-frame position
forcing, old actor reference reuse, or persistent custom pose/save fields.
Unrelated lookups call the original native function. Native save serialization
and recovery of the temporary start ID must be verified before public release.

Mailbox version 2 is 616 bytes. Pending is claimed once; Reloading remains busy;
SpawnProvided is acknowledged only after the replacement scene enters exePlay,
with appearance, save/stage/scenario, position and rotation verification. The
old scene cannot acknowledge completion. Host cancellation only cancels Pending,
never an accepted native reload. Signature gates now cover 19 exact-base sites.
Native Wait/isEnableDemo, health, ownership and transition checks remain; no
wardrobe needs to be open, instantiated or previously initialized.

The actual helper passes host contract tests covering five arbitrary stage names,
outfit/hat changes, preserved native pose handoff, replacement player pointers,
exactly-once handoff, unrelated spawn lookup passthrough, invalid pose, ownership,
duplicate/stale/unsafe requests, and completion mismatch. This does not prove the
real native reload is safe: do not label this feature complete before device
verification. The larger still/open-eye portrait and removed globe stamps are
also in this installed test build, but still need visual verification.

Build checkpoint: guest helper (2772 bytes), native loader and JNI compile/link
with Ninja -j1. The first APK attempt hit Windows commit limit error 1455; work
was stopped without terminating other apps. After the user freed memory, commit
usage was 67%. Retried with one worker, in-process Kotlin, a 1536 MB Java heap
and 384 MB metaspace. Release build completed in 5m31s; all 145 launcher tests,
nine portrait tests and the native wardrobe host contract tests passed.

APK packaging/signature verification passed: nine self-contained Mario model
parts, the exact rebuilt native library, and all 1194 checked published assets
unchanged (including the last published WFC payload). APK SHA256:
`c56aab1e207317078646982237f7777f4aead215fe53517e4349bdc0354f0f51`.
User explicitly requested installation. ADB install -r succeeded on
100.83.63.70:46665; package manager confirms 1.6.16.6, code 28, update time
2026-09-05 03:44:45. No app data, saves or caches were cleared. Native outfit
reload outside the Odyssey, pose preservation, save recovery and portrait
appearance still require live gameplay verification before claiming completion.

User confirmed the 1.6.16.5 portrait renders. Device readback reports a 320x413
view with 582 nontransparent samples; screenshot confirms the assembled model.
The 1.6.16.6 changes move the portrait camera from z=3 to z=2.5 and
target y=0.75, apply native Wait at time zero once, and stop rendering after
the completed readback. Appearance changes still invalidate/rebuild the portrait.
The top-screen player's animation is untouched. Decorative globe stamps have
been removed from the left page header and both locations on the character card.

Closed-eye root cause: PlayerAnimationEye.bfres BoneVisibilityAnims/Wait has
four constant false values for EyelidCloseLeft, EyelidCloseRight, EyelidLeft,
and EyelidRight, with no curves. The skeletal-only exporter left those meshes
visible. Inspect-SmoBfres.ps1 now exports native constant Wait visibility;
prepare_smo_preview.py removes those mesh instances while retaining the joint
hierarchy and both pupil meshes. Missing/unmatched eye visibility fails the
export. Nine preview tests pass; modified Eye/Face GLBs have zero validation
errors (existing generated-tangent warnings remain). Framing and eyes still
require on-device verification in the installed 1.6.16.6 build.

Native wardrobe trace, exact base executable:

- GameDataFunction::wearCostume 0x52b4ec / wearCap 0x52b530 update the active
  file and telemetry; file methods 0x523b9c / 0x523bb4 only update SaveStrings.
  These calls alone do not rebuild gameplay Mario.
- StageSceneStateCloset::exeSelect 0x4d6a54 resets random modes, calls those
  native functions, marks +0x19c, then transitions through exeWear 0x4d788c.
- StageScene::exeCloset 0x4d0980 sees that completion flag and calls
  changeNextStageWithCloset 0x527bf4, then Scene::kill. The change-stage function
  uses the current stage and the native start ID "Closet", not Mario's current
  arbitrary world position.
- Closet::init 0x24e468 registers its placement/linked exit via
  rs::registerPlayerStartInfoToHolderForCloset 0x2e38dc; SceneObjHolder index
  0x2a is PlayerStartInfoHolder. Blindly reusing this reload in a stage without
  a registered closet is unsafe. No companion equip command is installed yet.

Read-only before/after captures, both automatically resumed, confirmed the user
changing Mario/Mario to MarioTailCoat/MarioCaptain in HomeShipInsideStage,
world 3, scenario 5. Scene/player addresses were reused, so pointer equality
alone must not be treated as proof that a costume reload did not occur.
Black Tuxedo body/head archives have now been converted privately and added to
the next portrait bundle (nine GLBs, two bodies, three hats). The native body
initializer at 0x444028 explicitly pairs ObjectData/<costume> with
ObjectData/PlayerAnimation even when a body has no InitModel.byml. Its subactor
list provides face/eyes/hands; 0x444538 creates the head separately. The exporter
now handles that path using each body's own attachment metadata. Nine preview
tests pass and all nine GLBs validate with zero errors. These assets are not in
the installed APK yet. Full wardrobe asset coverage and native command
integration remain incomplete.

### 1.6.16.5 installed test build

Built and installed in place on 100.83.63.70:46665 with user approval after
saving. Package manager confirms 1.6.16.5, code 28. Saves/caches not cleared.
141 launcher tests, host native reader tests, and five animation-transform
tests pass. Signature verified; all 1194 checked published companion assets
unchanged and packaged native library matches the rebuilt source output.
APK SHA256: `4d477dadaba3b05bfd7ee9778ed91c3f7abc9f59fcd5656216387346cb20e2e8`.
Portrait pixels verified after launch; all collection categories still require
individual gameplay verification.

User reported the blank portrait in installed 1.6.16.4. Device logs confirm
Filament initialization and `Preview ready: Mario/Mario (6 native parts)` with
no setup exception. Screenshot confirms an empty postcard. The Odyssey wrapper
changed its child's 1x1 layout parameters from onSizeChanged (after measurement),
unlike the working BOTW wrapper. It now explicitly measures and lays out the
portrait. First completed readback logs actual view size and nontransparent
sample count; device verification is required before calling the render fixed.
Animation clock now uses nanoTime, matching Choreographer instead of mixing it
with elapsedRealtime's suspend-inclusive clock.

Collections and Outfits now have reference-derived 3-column scrollable grids,
progress bars, owned checkmarks, locked question marks, detail panels and
equipped highlights. Separate Outfit/Hat and Souvenir/Capture/Music/Sticker
subpages query the real save. Their scope is the whole collection, not a falsely
kingdom-specific denominator. Original icons load from TextureClothes,
TextureSouvenir, TextureSticker and HackListTexture; names come from native MSBT.
No equip command is exposed yet: native wardrobe transition integration remains
pending, as do additional preview costumes. Do not claim the full mod complete.

Collection ABI: 34848 bytes, magic 0x434f4d53, version 1. Header is
magic/version/size/count, generation u64, world i32, kind u32. 256 entries of
136 bytes: ordinal u32, owned u32, name[64], variant[64]. Hidden names/variants
are zeroed before JNI publication. One active category is double-sampled at
2 Hz on the reader worker, then owner and scenario are revalidated. Invalid
packets clear the UI rather than retaining stale ownership. No guest writes.

Native collection contracts traced from the exact local base executable:

- rs::isHaveCloth/Cap/Gift/Sticker (0x54c498..0x54cc6c): active file arrays at
  +0x6f8/+0x6f0/+0x708/+0x700. isBuyItem 0x5235d4 uses 64 records, stride 0x58,
  SafeString pointer +8, terminates at first empty string. Holder catalogs are
  count +0x110/120/130/140, pointer arrays +0x118/128/138/148; item name inline +4.
- Capture discovery: isExistInHackDictionary 0x523cb4, file+0x7c8, 160 records
  stride 0x98, SafeString pointer +8. HackListLayout ctor 0x1ebd60 reads the
  native HackObjList BYML in order excluding IsDebug. Its 52-entry exact-base
  static catalog is reproduced in smo_collection_reader.h; this is asset
  metadata, never a custom ownership list. updateHackPartsData 0x1ec648 checks
  the same dictionary and hides names for unknown rows.
- Music: updateList 0x4d84e4 uses cCollectBgmListSize (GOT +0x1e4ecf8) and
  getCollectBgmByIndex 0x526494 (GOT +0x1e4ed00, stride 0x28). isCollectedBgm
  0x526434 matches (name, optional situation) in file+0x830 PtrArray; capacity
  +0x834, pointers +0x838, record name/situation +0/+8, collected byte +0x10.
  rs::getCollectBgmMessage 0x5788e8 forms name@situation when situation exists.
  Music cards are informational and do not change the game's soundtrack.

Host tests cover ownership changes, hidden-name removal, bad catalog counts,
unsafe identifiers, captures, and distinct music situations. Android unit tests
cover packet rejection and shared draw/hit-test geometry (including gaps).

### Previous installed checkpoint

Latest checkpoint: 1.6.16.3 was installed and live-validated in base 1.0.0.
SandWorldHomeStage map decoded in 417 ms, coins 175, health 3/3, total Moons 5,
and Sand's discovery-filtered Moon list 0/69. User tested pages and scrolling.
The game was resumed on request and subsequently reported 59.9 FPS.
These observations supersede the older pending-validation notes below.

1.6.16.4 test APK built, release-signature verified, and installed in place:
SHA256 `2fe6888de9c538627966a70d841636066ce73d98bfc958d7473d3898319af563`.
139 launcher tests passed; all 1,194 checked published companion assets are
unchanged. The packaged native library exactly matches the rebuilt runtime.
ADB install -r succeeded on 100.83.63.70:46665 after the user returned to the
launcher. Package manager confirms versionName 1.6.16.4, code 28. Saves and
caches were not cleared. Gameplay validation of this build is still pending.

1.6.16.4 work in progress (not yet device-validated): four reference-matched
brochure tabs; native-discovery map markers; separate Filament Mario portrait.
139 launcher tests and five animation-transform tests pass. Seven BFRES-derived
animated GLBs pass Khronos glTF validation with zero errors (runtime-generated
tangent-space warnings remain). Current preview assets cover Mario body with
Mario or MarioCaptain head; other outfits must not silently use the wrong model.
Ownership grids, full wardrobe commands, native objectives, remaining marker
categories and full costume coverage remain unfinished. This is not a release.

Preview provenance: native Mario InitSubActorPauseMenu / InitPartsFixInfo BYML
defines face, eye, hands and head attachments. PlayerAnimation{,Face,Eye,HandL,
HandR,Head} supplies Wait clips, sampled at native 60-fps time and packaged as
glTF TRS animation (quaternion interpolation, not linear matrix blending).
TextureArc dependencies are embedded. Parts are parented to the body skeleton,
not world-space offsets. BOTW/SSHD renderers are not modified for this integration.

Marker contract: MapLayout::appearParts 0x1f342c; isGotCheckpointInWorld 0x521300
filters 17 records of stride 0x148 by scenario mask +0x130, then got flag +0x140.
Names/positions are not read for undiscovered records. Native ordinal differs
from physical slot. Current main-stage-only projection omits interior records.
Registered shop count/coordinates: 0x524bc8/0x5248e8 (four 0x68 records at save
+0xa58). Minigames: 0x52483c/0x524868/0x524878 (+0xa68). Home visibility:
0x52c2c0, 0x51c71c, progress+0x34 activation, unlock array +0x20 and first-visit
array +0x48; home position save+0xa7c. Race/balloon suppress ordinary icons.
Marker packets revalidate owners/scenario before JNI publication and poll only
on the Map page. Travel actions are deliberately not implied by marker visibility.

Read-only native companion implementation is underway: live status, native map
resources/projection, touch pan/zoom, and a discovery-filtered Moon-list bridge.
The new runtime/UI have not yet passed on-device acceptance. No wardrobe command,
travel command or live Mario preview is implemented. Link's Awakening work is
shelved; other companions must remain unchanged. This is not a completed mod.

Development APK 1.6.16.2 (code 28) is installed; 130 launcher tests passed and
1,194 checked companion asset entries match the published 1.6.16 checkpoint.
The first launch was update 1.4.1. The user subsequently switched to base 1.0.0:
its main build ID was confirmed in loader logs, with a 59.9 FPS UI sample.
Base-session logs contain GPU macro assertions and a texture-format assertion;
no crash/device-loss/OOM signature was found in that retained sample. This is
not proof of comprehensive renderer stability.

## Exact local targets

Title: `0100000000010000`. Version comes from extracted control.nacp, not filenames
or presence of particular class names.

| Version | Main NSO build ID (32 bytes) | Named dynamic symbols |
| --- | --- | --- |
| Base 1.0.0 | `3ca12dfaaf9c82da064d1698df79cda100000000000000000000000000000000` | 91,230 |
| Update 1.4.1 | `b424be150a8e7d78701cbe7a439d9ebf00000000000000000000000000000000` | 746 |

Initial integration target is the symbol-rich base. Update offsets are not
interchangeable. Full build-ID and instruction validation must precede hooks.
Private extracted executables, assets, symbol indexes and decompilation are in
`.work/smo-audit/`; do not publish those as source documentation.

## Static evidence, not yet live-validated

All addresses below are **base main-image RVAs**, not absolute guest pointers.
The local NSO's dynamic symbols provide names and sizes. Selected functions were
disassembled/decompiled using SmoTrace.java; inferred decompiler types and tail-call
boundaries still need instruction-level verification before invocation.

| System | Evidence | Integration constraint |
| --- | --- | --- |
| GameDataHolderAccessor | Constructors `0x5316ec`, `0x53171c` resolve scene object index `0x12` | Resolve through the real scene; no fabricated holder |
| Active save state | GameDataHolder `+0x20` points to GameDataFile | Revalidate across scene/save/title transitions |
| Coins | `getCoinNum` `0x529c24` reads active file `+0x5d8` | Read-only observation, not a purchase implementation |
| Current world | `getCurrentWorldId` `0x528358` | Verify in live gameplay before exposing |
| Regional coins | `getCoinCollectNum` `0x529b50`, file method `0x5200e4` | Use current-world semantics |
| Scene update candidate | `StageScene::control` `0x4cc348` | Candidate only; ABI/lifetime not yet traced |
| Costume state | `getCurrentCostumeTypeName` `0x52b48c`; `wearCostume` `0x52b4ec` calls file method `0x523b9c` | Saved outfit change alone does not prove live model refresh |
| Native wardrobe | `StageSceneStateCloset::initModel` `0x4d5560`, `appearMarioModel` `0x4d637c`, `exeWear` `0x4d788c` | Menu-owned state cannot be called with fake objects |
| Model setup | `PlayerFunction::setupClosetPlayerModel` `0x446dcc` | Trace underlying model/skeleton/render ownership |
| Checkpoint discovery | `isGotCheckpointInWorld` `0x529ccc` | Preserve scenario filtering and native index meaning |
| Checkpoint transition | `changeNextSceneByGotCheckpoint` `0x529d48` | Invalid index can yield unsafe access; not a complete travel command |
| Travel state | `StageSceneStateCheckpointWarp` constructor `0x4d39f4`, fly `0x4d3a90`, land `0x4d3ea8` | Enter the actual scene state machine |
| Map data | `MapDataHolder` constructor `0x537040` | World/scenario-owned data, not static global discovery |
| Map projection | `MapLayout::updatePlayerPosLayout` `0x1f1e50` | Native projection includes substage entrance and area overrides |
| Map texture | `MapLayout::loadTexture` `0x1f0014` | Loads `ObjectData/TextureMapLayout%s`; eight texture slots |

### Travel eligibility is a conjunction

`MapLayout::isEnableCheckpointWarp` (`0x1f2998`) consults invalid-map-stage state,
player open-map eligibility, Yukimaru race, meeting Cappy, Cappy removed by Jango,
and boss-battle scene status. The standalone `isEnableCheckpointWarp` at `0x529c58`
is not sufficient. Reuse the underlying gameplay checks without instantiating or
opening MapLayout. Do not teleport by writing coordinates.

### Asset/data provenance

Locally extracted Yaz0/SARC archives include WorldList, CheckpointFlagInfo,
ShineInfo, CostumeList, native Map/Closet/ShopBG/WorldSelect layouts, and US English
layout/stage/system messages. Native layouts include nested `layout.lyarc` SARCs,
BFLYT/BFLAN, combined BNTX textures and Nintendo UDSG font references.

Checkpoint tables are named by stage **and scenario**. Shine tables contain data
for undiscovered Moons too. Asset presence is never proof of discovery. Preserve
the game's visibility predicates; do not display the raw complete tables.

## Proposed 1240 x 1080 interface

Use Odyssey's red/white/black menu treatment, native icons and type, with kingdom
art and brochure treatment inside content pages. Avoid transplanting Zelda's
parchment design. Review extracted layouts/art before final styling.

- Compact persistent upper strip: live Mario preview, kingdom, native health,
  coins, regional coins and Moon total. Preview owns a separate camera/target,
  not a second gameplay update or a static portrait.
- Main content takes most of the height. Map is the default, with small recenter
  and contextual marker controls. No dense permanent dashboard over the map.
- Five large bottom tabs: MAP, MOONS, KINGDOM, COLLECTIONS, ODYSSEY.
- Collections has separate capture, wardrobe and souvenir/sticker subpages.
- Unknown objectives, names and destinations stay absent/locked according to
  verified native knowledge state; no invented live values or objectives.

## Implementation gates

1. Verify the base build in live gameplay and capture scene/player/holder lifetime
   without opening vanilla menus. Every debugger operation must resume in finally.
2. Implement a version-gated, bounded snapshot bridge using existing emulator
   synchronization abstractions. Invalidate references on generation changes.
3. Resolve native discovery/progression and resource projection contracts. Cache
   static resources; do not repeatedly scan memory or extract textures per frame.
4. Trace wardrobe refresh, UI3D rendering and travel state transitions completely
   before introducing write commands. Commands require validated ownership,
   player restrictions, request IDs and duplicate suppression.
5. Wire the custom touch renderer to real snapshots. Pan/pinch arbitration,
   atomic frame snapshots and stable resource ownership must prevent ghost layers.
6. Validate fresh load without menus, save/scene/death/title transitions, repeated
   taps, wardrobe changes, checkpoints and kingdom travel on device. Unsupported
   builds and unverified actions must not silently mutate game state.

Crazy Cap may remain informational until its complete native purchase transaction
is verified. No tests listed here have passed on Odyssey yet.

## Menu-independent root path

`GameSystemFunction::getGameSystem` (`0x5369fc`) and
`Application::getRootTask` (`0x515788`) establish:

```
main + Application::sInstance (0x1e665c8)
  -> Application +0x28 -> framework +0x38 -> task manager +0x128
  -> RootTask +0x200 -> GameSystem +0x10 -> HakoniwaSequence +0xb0
  -> StageScene
```

The scene exposes GameDataHolder at +0x2d0; cross-check against scene-object slot
0x12 through scene +0xa0 -> holder +8 -> array. PlayerHolder comes through scene
+0x90 -> scene information +0x98 (`al::getScenePlayerHolder`, `0x9cf374`).

PlayerHolder layout is array pointer +0, **capacity +8, active count +12**, with
16-byte records. The constructor (`0x9a3cb0`) proves this order. A first diagnostic
probe stopped safely because it initially reversed capacity/count; the game was
resumed in finally. A second probe reached the player but rejected the initially
assumed vtable prefix. `PlayerActorHakoniwa`'s constructor (`0x41b250`) explicitly
installs vtable **+0x18**, rather than the other observed classes' +0x10. The reader
now checks that exact address point instead of relaxing type validation.

`probe_smo_state.py` is a bounded read-only diagnostic, not the production bridge.
It validates local NSO segment hashes, exact image/build, unique instruction
windows and live object types. It does not call native functions or open menus.
The corrected reader completed a live base-game capture without opening menus
on 2026-09-05. Private evidence: `.work/smo-audit/base-gameplay-no-menu.json`.
It resolved the entire typed root chain and scene-object cross-check, then read
world ID 2, scenario 1, 175 coins, 0 spendable regional coins, health 3, outfit
`Mario`, hat `MarioCaptain`, and position approximately (-3361.179, 100, -20361.523).
This validates one gameplay snapshot, not continuous synchronization, transitions,
discovery predicates, commands or the character renderer. Native string buffers
can be byte-aligned; object pointers remain subject to stricter alignment checks.

Eleven synthetic Odyssey tests now pass (eight probe/validation cases and three
metadata cases), plus six executable-index validation tests. These are offline
tests, not proof that the production companion or native actions work.

Other instruction-traced fields:

- Mario translation: LiveActor +0x50 -> pose +8 (`al::getTrans`, `0x8ee66c`).
- Active GameDataFile: holder +0x20. World ID +0x9f0; scenario +0xa24.
- Health: file +0x860 -> PlayerHitPointData +0xc (`0x51d124`, `0x53886c`).
- Outfit/hat safe-string buffers: file +0x718/+0x770. Native getters invoke the
  safe-string virtual method before returning these; the probe only reads bounded
  already-existing strings and does not assume a model change transaction.
- Spendable regional coins: world-indexed total at file +0xa00 minus spent at
  +0x5e8, as implemented by `GameDataFile::getCoinCollectNum` (`0x5200e4`).

`export_smo_metadata.py` exports 139 BYML tables from six local base archives,
preserving order, condition keys and scenario-specific filenames. Source archive
and member hashes are included. `runtimeVisibilityResolved` remains false.

## Additional wardrobe/travel evidence

Instruction-level tracing of `StageSceneStateCloset::exeSelect` (`0x4d6a54`)
shows resetCostumeRandomMode, wearCostume and wearCap calls before entering its
wear state. `StageScene::exeCloset` (`0x4d0980`) then checks closet +0x19c on
completion. When set, it calls `changeNextStageWithCloset` (`0x527bf4`) and the
scene's virtual method at +0x28. The change-stage helper constructs the native
ChangeStageInfo for the current stage and passes it to GameDataHolder::changeNextStage.
Therefore the currently traced vanilla path includes a scene transition, not
merely a live model-pointer swap. Do not omit that lifecycle or pretend an
unverified direct reload is equivalent.

`StageScene::exeWaitWarpToCheckpoint` (`0x4cf96c`) prepares Mario, camera and the
warp destination, and waits for the native animation. `exeWarpToCheckpoint`
(`0x4cfc4c`) drives the transition and selects the next scene via the checkpoint
entry obtained from the pause-map state (or the alternate Odyssey destination).
The custom action must supply a validated gameplay-owned request that does not
depend on that menu-owned selection field. Calling this state unchanged before
the menu exists is not established as safe.

Ghidra output can still misidentify external tail calls/PIC even with exact
symbol body extents. Confirm calls and field access against bounded ARM64
disassembly; do not use decompiler-inferred signatures as an ABI contract.

## Production bridge implementation checkpoint (2026-09-05, not release acceptance)

`smo_state_reader.h` and `smo_companion.cpp` now implement a read-only JNI bridge,
not just the debugger probe. The bridge checks the full base build ID and title,
validates the typed root chain, and rechecks scene/save/player identities after
reading. It rejects mixed scene frames. The emulator session lock does NOT make
all game fields an atomic guest frame; values remain observations, not mutation
authority. The reader has a bounded 8 KiB read budget and does not scan the heap.

`SmoSnapshot.kt` decodes the 416-byte schema-1 packet into immutable values.
Offsets 60 and 412 hold optional kingdom/total Moon counters (-1 unavailable).
`SmoCompanionView.kt` is routed only for Odyssey. State reads run on a background
worker at ~30 Hz; ROMFS/PNG decoding runs on a separate background worker. UI
results are coalesced, asset callbacks are epoch/key checked, and map painting
uses one opaque Canvas redraw, a clip, and a shared pan/zoom transform. No
controller input or vanilla menu automation is involved.

The implemented pages currently provide a development map/status view, native
Moon counters, kingdom/region labels from local game messages, current outfit/hat
identifiers and a scrollable native Moon list with known-name details. Discovery
markers, travel, wardrobe actions, objectives and Mario preview are NOT implemented.
Their tabs explicitly say so. Mushroom Kingdom's additional achievement entries
are not yet joined into the HintInfo-backed list.
Do not describe this checkpoint as the fully functional mod or a public release.

### Exact map resource and matrix layout

`ObjectData/Texture2dMap.szs` contains `Texture2dMap.bfres`, 22 named 2048x2048 map
textures, and matching BYML files. `TextureMapLayout<World>.szs` is brochure
photography, **not the actual map atlas**.

GameDataHolder +0x1e0 owns MapDataHolder; +0x190 owns WorldList. MapDataHolder's
world array is count +0, array +8, owner +0x10. `findMapData` (0x537d4c) chooses
the world's base entry or its matching scenario child. Scenario selection uses
GameDataFile +0x7d8[world], not the placement scenario at +0xa24.

`loadMapData` (0x537338) establishes this layout:

| Offset | Field |
| --- | --- |
| 0x00 | **ViewProjMatrix**, combined world-to-map transform |
| 0x40 | ViewMatrix (3x4) |
| 0x70 | ProjMatrix (4x4), NOT combined world projection |
| 0xb0 | Texture reference |
| 0xb8 / 0xbc | Width / height |
| 0xc0 / 0xc8 | Additional texture count / array |
| 0xd0 | Scenario variant (-1 base) |
| 0xd8 / 0xe0 | Child count / array |

The initial +0x70 combined-matrix assumption was caught before deployment by
checking loadMapData's string references: 0x1850dc5 = ViewProjMatrix,
0x1850dd4 = ViewMatrix, 0x1850ddf = ProjMatrix. Native
MapLayout::updatePlayerPosLayout (0x1f1e50) applies rows 0 and 1, then multiplies
by 1024 (constant at 0x18aa100). Canvas UV is ((x+1)/2, (1-y)/2). Tests deliberately
give the two matrices different values to catch a regression.

Current map support is restricted to the current main stage. Substage entrance
substitution (`getStageMapPlayerPos`, 0x52c458), InvalidateStageMapArea/PlayerPoint
links, Deep Woods special handling and additional textures still need integration.
Markers are not shown until their native discovery checks are implemented.
The map-enabled/progression predicates still need complete runtime verification.

An attempted additional `probe_smo_state.py --map` capture disconnected at
qSupported; the finally handler sent continue and disconnected. It produced no
valid map capture and must not be counted as validation. Device activity inspection
subsequently showed LauncherActivity, not EmulationActivity. Earlier successful
core-state capture remains the only menu-independent live evidence.

### Moon visibility contracts traced next

GameDataFile +0x858 points to the twenty counters summed by getTotalShineNum
(0x51f0c0); getShineNum (0x51f094) indexes the current world. These are Power Moon
units, not a count of unique rows in the collection list.

GameDataFile +0x9a0 is a native array of 1024 HintInfo records, stride 0x238.
`calcShineIndexTableNameAvailable` (0x522844) iterates this exact bounded array:
world at +0x1cc, Moon Rock flag +0x1d0, name state +0x1e0 == 1. Moon Rock entries
are included only when the world's native scenario equals WorldList entry +0x1c.
Collected flag is +0x1d1 (`isGotShine`, 0x51ead8 / 0x523e10). The name gate is
distinct from collected state; do not expose a name merely because a metadata
record exists. `getWorldTotalShineNum` (0x52410c) also has Mushroom achievement
handling; do not label the twenty-counter total as the collection denominator.

`tryFindShineMessage` (0x52bdf4) resolves HintInfo's stage string (+8) and placement
string (+0xa0) through rs::makeMessageLabel and the native stage message archive.
`smo_moon_reader.h` now implements an independently polled 36,896-byte Moon packet.
It indexes the known 1024-record array once per save/table owner, then reads only
current-world flags at 4 Hz while the Moons page is selected. Hidden names are
zeroed before JNI publication, including names cached while previously unlocked.
The packet rechecks root identity and scenario before publishing. Android resolves
`ScenarioName_<placement>` from that record's actual stage-message file and shows
unknown names as ???. Literal-only MSBT decoding deliberately excludes unresolved
control/substitution text. The list and details are not runtime-validated yet.
