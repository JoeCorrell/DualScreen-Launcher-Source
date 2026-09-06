# Skyward Sword HD dual-screen companion

This document records the traced implementation for the `dualscreen` Android flavor. The primary
display remains Eden's normal game surface. The presentation display hosts an independent Android
view. The live native map uses a dedicated GPU target and presentation surface on
Eden's existing Vulkan device and scheduler, without creating another game camera.

## Supported game build

The bridge fails closed unless both identifiers match:

| Field | Verified value |
| --- | --- |
| Base title ID | `01002DA013484000` |
| Update title ID | `01002DA013484800` |
| Update version | `v65536` / game version 1.0.1 |
| Main Build ID | `5441AF3C89F97E1D25A8CCF14C742BF82336D8A8` |

The executable Build ID and offsets were checked against the extracted 1.0.1 `main` NSO. Save
layout and flag IDs were cross-checked against the public
[`sshd-rando`](https://github.com/mint-choc-chip-skyblade/sshd-rando) project, while names and
system structure were compared with [`zeldaret/ss`](https://github.com/zeldaret/ss).

## Game-thread telemetry payload

The dual-screen loader recognizes the complete 1.0.1 NSO fingerprint: Build ID, all three segment
locations and sizes, and BSS size. It refuses to patch any other executable. For the verified
build it reserves a private 4 KiB read/write mailbox and a separate 4 KiB executable bridge page.
A 64-byte island in the executable's original zero-filled `.text`/`.rodata` alignment gap
at `main+0x105C950` reaches that bridge without changing the original segment layout.

The payload is called from the verified `dStage` render callback prologue at `main+0xE2391C`.
`dStage` initialization registers the callback at `main+0xE23900`; the hook is after its caller
link register has been saved and before any conditional return. The overwritten `add x29, sp,
#0x60` instruction is replayed, all payload scratch registers and vector registers are preserved,
and the game continues through the original callback.

The payload owns the live-map request/completion handshake and a native GUI callback.
It initializes the required map resources and picture binding without entering the
interactive map state; a separate gate excludes the initializer's camera/input side effects.
It initializes the versioned map
mailbox on every stage-render callback, applies a host request to the active `dStage` renderer, and
publishes completed texture metadata under a seqlock. The compact UI snapshot is read directly from
the verified game globals while Eden's system-access lock is held.

## Live snapshot

`sshd_companion.cpp` produces a packed, versioned 2070-byte Android snapshot. Reads execute while
holding Eden's system-access lock and validate the title, Build ID, mapped ranges, manager pointers,
health bounds, and flag-table shapes before publishing data.

The snapshot includes:

- current and maximum health, live stamina, rupees, seeds, arrows, and bombs;
- area, stage, room, layer, entrance, day/night state, and trial ID;
- all eight active pouch entries and all 60 item-check entries;
- equipped B-wheel item, selected pouch slot, shield slot, and dowsing slot;
- potion timers and 216 direct item-flag values;
- gratitude crystals, heart pieces, key pieces, pouch expansions, and extra wallets;
- Fi summary/objective message IDs selected from the game's `navi_table.dat` ordering and live
  story flags.

Save A fields use the game's real save structure (the payload's `FILE_MGR + 0x10`), including pouch `+0x7C0`,
item check `+0x7E0`, health capacity `+0x5302`, current health `+0x5306`, and live equipment fields
at `+0x53AE` through `+0x53B2`.

The Android view polls this compact snapshot every 250 ms on a background-priority thread. It
hashes the live fields and schedules a redraw only when data changes. Original RomFS assets are
loaded once on a separate background worker and cached by the native asset bridge. Loading
checkpoint 4 specifically means the verified save/player globals are not readable yet, so startup
and stage-transition loading remain distinguishable from an initialized save.

## Bottom-screen design

The companion follows the supplied reference's information architecture without using it as a
static image: a carved horizontal tab bar, a large inventory grid, a dedicated vitals/equipment
column, a full-height Link portrait, and an anchored control strip. The colors are Skyward Sword's
own aged parchment, saturated menu blue, warm gold, and carved brown rather than the reference's
Breath of the Wild palette.

The layout provides:

- horizontal Inventory, Map, Combat, and Progress tabs;
- a live vitals panel for hearts, stamina, rupees, seeds, arrows, and bombs;
- a reference-style inventory grid, item detail pane, equipment slots, and Link portrait;
- touch tabs, horizontal swipe page changes, category selection, and item inspection;
- a short eased page transition and clear selected, empty, and unavailable states;
- optional controller navigation, enabled only with the controller MODE button so gameplay input
  passes through by default.

The parchment shell is code-drawn and deterministic. It deliberately does not stretch the fringed
`tr_kami_05` pane across the display because that source texture expects the original BRLYT pane
geometry; stretching it caused the dark and transparent bands seen in the first build.

## Current-region atlas

**Runtime status (2026-09-04): the user confirms the reported map issues are fixed in Lanayru Desert.**
The game supplies terrain, parchment, compass, labels, markers and Link's icon. The tested
release cold-loads the native map and corrects transparency blending and presentation during
an unfinished map pass. Cross-area acceptance remains outstanding. See
[the rendering investigation](SshdMapRenderingInvestigation.md) for evidence and limitations.

The atlas has no manual map carousel or stage-to-image lookup. The reverse-engineered flow matches
the game's `dLytMapCapture` implementation: STIF supplies the area/room classification and localized
map title, `dLytMapGlobal` supplies the active floor and map transform, and the active `dStage`
`MapRelated` renderer draws the visible rooms and discovery state into its existing `CpuTexture`.
The companion requests that render on the game thread and resolves its NVN cache identity.
The complete native map UI draws into a separate native render target. Eden presents the
resident GPU image directly to the bottom-screen surface using its existing Vulkan device
and scheduler; no CPU map bitmap, frame capture or GPU download is involved.

For the supported 1.0.1 executable, the live `dLytMapGlobal` and `dLytMapCapture` objects are
`0xF358` bytes apart in their shared owner allocation. The capture bridge derives the latter from
the former on every request, then consumes the native aspect-corrected pane dimensions at capture
offsets `0xA0` and `0xA8`. This avoids retaining an actor pointer across UI lifetimes.

The image header carries the capture's stage name, floor, dimensions, texture format, and request
sequence. The Android view accepts it only when the stage and lifecycle still match the current
snapshot, clears stale images during transitions, and immediately requests the next native frame.
These checks reject stale metadata, but do not prove correct texture ownership, initialization,
GPU completion, native marker rendering, or map switching.

## Link player render

The inventory equipment column hosts the same rendering architecture as the BOTW player portrait:

- a transparent Filament OpenGL engine renders to an independent offscreen target;
- the GPU result is read back into a cached Android bitmap, avoiding a second presentation surface;
- camera fitting uses the model bounds, and horizontal touch drag rotates the complete model;
- the renderer is active only while the inventory page is visible and retires its GPU resources
  when hidden;
- Link's head and default hand meshes attach to the real `head`, `handL`, and `handR` bones in the
  exported body skeleton.

Nintendo model data is not embedded in the APK. The checked-in index points to the app external
cache `dualscreen/sshd/player/`, exactly like the BOTW player cache. The tested converter in
`tools/sshd-companion/` extracts the real `pl_body`, `pl_head`, and default `pl_hands` meshes from
`Alink`'s BRRES, decodes the matching ASTC textures from `model.xtx`, and produces four GLB files.
If those files are absent or invalid, the equipment column uses the original
`tr_sabLinkCollect_04.tpl` pause-menu artwork instead of a placeholder.

## Original game assets

The asset loader reads and decompresses the game's U8 archives directly from the mounted RomFS.
It decodes the needed TPL textures in memory and parses MSBT messages without requiring the game's
pause menu to be opened.

Currently used assets are:

- inventory cell and selected-cell art from `Layout/MenuPause.arc`;
- heart, Fi, rupee-purse, and Link portrait art from `Layout/MenuPause.arc`;
- original icons for the verified practice sword, True Master Sword, Sailcloth, Goddess Harp,
  Digging/Mogma Mitts, Water Dragon's Scale, Heart Piece, Triforce pieces, wallets, Fireshield
  Earrings, Cawlin's letter, insect cage, rattle, tablets, and Life Tree items;
- item names/descriptions and Fi text from `US/Object/en_US/0-Common.arc`.

Icon mappings are keyed by the game's `ITEMFLAGS` values. Items without a verified texture-to-ID
match use a generated text monogram; they are not assigned guessed artwork.

## Integrity boundaries

Inventory snapshots are read-only. Validated actions select owned B-wheel items, pouch slots and
dowsing targets; shields use the native pending model-update request. Tap a grid item to inspect
it, then tap it again or its action button to equip/use it. The four portrait-side slots act on
the first tap: shield and B-item slots equip the selected compatible item or cycle carried/owned
choices, the selected-item slot activates the grid selection, and dowsing cycles native targets.

Potion use selects its real pouch slot and sends the normal R input to ready the bottle. A bounded
state check waits for native BINHOLD (animation 441, held item 12) before sending A. Held item 13
is the empty catch bottle recognized by `isUsingBottle`, not a filled potion. Native potion
animations 435–437 and shield recovery 442 confirm use. The game owns effects, quantities and
animations; the companion never writes health or decrements a bottle. Stage/session changes,
leaving Inventory, hiding/detaching the view, or a timeout cancel queued input and release buttons.
Other bottles can be readied; passive pouch items remain selections. These new actions require
runtime validation in the release under test.

The map invokes
verified native game-thread initialization/render functions and updates the native map transform
for gestures. Its player marker is generated by the game's own controller and projection.

Each native map initialization also invalidates the companion's layout owner, even when the game
reuses the same MapMain and stage addresses. This ensures area/dungeon transitions rebind the
native map picture and finish its noninteractive appearance animation. The bridge regression
covers this reused-owner case; runtime transition coverage is recorded in the investigation.

## Local build

```powershell
cd src/android
.\gradlew.bat :app:assembleDualscreenRelWithDebInfo --no-daemon --max-workers=1 --no-parallel
```

The APK is written to
`src/android/app/build/outputs/apk/dualscreen/relWithDebInfo/app-dualscreen-relWithDebInfo.apk`.
The integrated launcher release is built from the workspace root with
`:launcher:assembleRelease`, after rebuilding and stripping the native library into
`switch/eden-runtime/src/main/jniLibs/arm64-v8a/`. The September 4 release was installed
and tested through ADB on `100.83.63.70:46665`; see the investigation for its artifact and scope.
