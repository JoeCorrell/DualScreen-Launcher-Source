# Skyward Sword HD dual-screen companion

This document records the traced implementation for the `dualscreen` Android flavor. The primary
display remains Eden's normal game surface. The presentation display hosts an independent Android
view and does not create another Vulkan renderer, camera, or game framebuffer.

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
build it reserves a private 4 KiB mailbox and installs a small payload in the executable's original
zero-filled `.text`/`.rodata` alignment gap at `main+0x105C950`.

The payload is called from the verified main-loop site at `main+0xE3E818`. The overwritten
`mov w1, w19` instruction is replayed, all scratch registers and the link register are preserved,
and the hook remains within the AArch64 direct-call range. This is the same game-thread payload
architecture used by the existing dual-screen integrations; it is compiled into the dual-screen
APK's native library rather than distributed as an external IPS file.

Every game-loop iteration publishes a versioned, seqlocked mailbox containing the actual file/save
manager, player, item-flag manager, story-flag manager, current stage, room, layer, entrance,
day/night state, and trial state. The Android bridge only accepts a complete even sequence with the
expected magic, ABI version, and size. It never consumes a partially written payload frame.

## Live snapshot

`sshd_companion.cpp` consumes the game-thread payload and produces a packed, versioned 816-byte
Android snapshot. Reads execute while holding Eden's system-access lock and validate the title,
Build ID, mailbox ABI, mapped ranges, manager pointers, health bounds, and flag-table shapes before
publishing data.

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
checkpoint 4 specifically means the matching game-thread payload has not published a valid mailbox
frame yet, so payload failures are distinguishable from an uninitialized save.

## Bottom-screen design

The companion follows the supplied reference's information architecture without using it as a
static image: a carved horizontal tab bar, a large inventory grid, a dedicated vitals/equipment
column, a full-height Link portrait, and an anchored control strip. The colors are Skyward Sword's
own aged parchment, saturated menu blue, warm gold, and carved brown rather than the reference's
Breath of the Wild palette.

The layout provides:

- horizontal Inventory, Pouch, Atlas, and Fi tabs matching the reference proportions;
- a live vitals panel for hearts, stamina, rupees, seeds, arrows, and bombs;
- a reference-style inventory grid, item detail pane, equipment slots, and Link portrait;
- Inventory, Adventure Pouch, Survey Atlas, and Fi pages;
- touch tabs, horizontal swipe page changes, category selection, and item inspection;
- a short eased page transition and clear selected, empty, and unavailable states;
- optional controller navigation, enabled only with the controller MODE button so gameplay input
  passes through by default.

The parchment shell is code-drawn and deterministic. It deliberately does not stretch the fringed
`tr_kami_05` pane across the display because that source texture expects the original BRLYT pane
geometry; stretching it caused the dark and transparent bands seen in the first build.

## Current-region atlas

The atlas has no manual map carousel. It resolves the visible original map from the live stage ID:

- Skyloft, the Sky, Thunderhead, and unknown/non-regional stages use the world chart;
- `F/D/B/S1xx` stages use Faron;
- `F/D/B/S2xx` stages use Eldin;
- `F/D/B/S3xx` stages use Lanayru;
- `F4xx` Sealed Grounds and Temple of Hylia stages use Faron.

This mapping was checked against the extracted stage tables and `sshd-rando` world data. Touch and
controller input cannot switch the atlas to an unrelated region. A player marker remains disabled
until the game's native world-to-map coordinate transform is traced.

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
- world, Faron, Eldin, and Lanayru maps from `Layout/Map2D.arc`;
- item names/descriptions and Fi text from `US/Object/en_US/0-Common.arc`.

Icon mappings are keyed by the game's `ITEMFLAGS` values. Items without a verified texture-to-ID
match use a generated text monogram; they are not assigned guessed artwork.

## Integrity boundaries

The current guest-state bridge is read-only. Touch and controller input navigate the companion but do not
mutate guest inventory or equipment. Guest-state actions will remain disabled until the relevant
game-thread mutation functions and ownership rules are traced and safely invoked. Likewise, the
atlas does not invent a player marker.

These omissions are intentional safeguards, not recreated approximations.

## Local build

```powershell
cd src/android
.\gradlew.bat :app:assembleDualscreenRelWithDebInfo --no-daemon --max-workers=1 --no-parallel
```

The APK is written to
`src/android/app/build/outputs/apk/dualscreen/relWithDebInfo/app-dualscreen-relWithDebInfo.apk`.
Device installation and runtime testing are left to the user.
