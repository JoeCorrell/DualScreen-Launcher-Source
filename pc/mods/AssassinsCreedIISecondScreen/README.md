# Assassin's Creed II Second Screen

In development. This directory owns the native PC mod, research tools, tests,
and separate downloadable payload. No AC II implementation or game-derived
assets may be bundled into the launcher APK.

The requested interface is a persistent 1240 × 1080 Animus 2.0 touchscreen
with Map, Equipment, Inventory, Missions, Database, and Progress pages.
Gameplay must retain the entire upper screen and normal controller input.

The user's attached six-panel specification is the visual reference: white and
light-gray surfaces, blue-gray construction lines, fine geometric fragments,
condensed typography, restrained dark-red selection, DNA motifs, generous touch
targets, and a persistent six-tab navigation bar. Values and item names pictured
in that reference are illustrative, not evidence about AC II game systems.

## Verified device baseline

- ADB device: AYN Thor, `100.83.63.70:46665` at initial reconnaissance.
- Game directory: `/sdcard/Games/Assassins Creed II`.
- Actual game: `AssassinsCreedIIGame.exe`, 34,693,768 bytes, PE32/i386.
- SHA-256: `23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92`.
- Preferred image base `00400000`, image size `0216e000`, PE timestamp `4b8c2c75`.
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

The development payload now has a verified message-pump hook and a read-only
world/player/collectible adapter. **Loaded-save accuracy, transition behavior and
performance remain unverified. Equipment/use actions, live maps, missions,
database integration and the native Ezio preview remain incomplete.**

AC II is listed as beta. The user requested the separate development payload
installed against the existing game folder in Skyrim's Games container and will
perform the gameplay tests. No release is published. See [RESEARCH.md](RESEARCH.md)
for the device-test handoff, exact evidence and remaining work.
