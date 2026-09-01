# Dead Island: Definitive Edition Dual-Screen Mod

This WEMU mod keeps gameplay on the top display and draws a touch interface on
the AYN Thor's 1240x1080 bottom display. Install `dxgi.dll` and
`DeadIslandSecondScreen.ini` beside `DeadIslandGame.exe`. Remove `dxgi.dll` to
uninstall it.

## Supported build

The current binary was developed against:

- `DeadIslandGame.exe`: 1,072,640 bytes, dated 2016-05-31
- `engine_x64_rwdi.dll`: 11,481,024 bytes
- `gamedll_x64_rwdi.dll`: 21,069,760 bytes

The engine APIs are resolved by exported name instead of fixed code offsets.
An incompatible game build therefore fails closed and names missing symbols in
`DeadIslandSecondScreen.log` rather than calling a guessed address.

## Current interface

The bottom UI has four large touch pages designed specifically for 1240x1080:

- **Stats** — live level/XP, money, available skill points, health, stamina,
  Fury, equipped-item name/icon, ammo, quantity, durability, item stats, and a
  nine-slot touch hotbar read directly from the player's real quick slots.
- **Inventory** — a tall, scrollable backpack list with nine large visible
  rows, live item names, icons, amounts, capacity and selected-item details.
  A tap selects a row; holding it opens the item-action sheet. Equip/use and
  holster are delegated to the exact PlayerDI function used by the game.
  Quick slots stay on Stats instead of being duplicated here.
- **Skills** — the complete Fury, Combat, and Survival definitions for Sam B,
  Xian, Logan, Purna, and Ryder, reconstructed from the XML shipped in
  `Data0.pak`. The page retains fan-out branches, parallel choices, milestone
  convergence, original icons, live ranks, prerequisites, purchase state, and
  available points. Purchases call the game's own LogicalPlayer upgrade path.
- **Map** — the current area's original map tiles read from the shipped game
  assets, with correct aspect ratio, exact player position and facing arrow,
  automatic player following, touch zoom and center/reset controls. Areas that
  do not expose a supported tile atlas fall back to the honest live schematic.

Inventory and Skills do not open, hide, or keep a copy of either vanilla menu.
Those XUI controllers contain no item/skill rows until the player manually
opens them, which made the old adapter empty and could interfere with save
loading. The current bridge instead follows the exact PlayerDI inventory,
quick-slot and held-item arrays and the LogicalPlayer skill registry used by
the supported executable. Every callable address is enabled only after the
gamedll PE timestamp, image size, and machine-code signatures all match.

The bridge does not create a second inventory or skill system. Equip/use,
holster, prerequisite checks, point deduction, ability activation, and save
persistence remain game-owned. Data is copied into bounded C++ snapshots at
10 Hz; no gameplay pointer is retained across a player or level replacement.

## Safety and gameplay behavior

- The panel does not synthesize keyboard, mouse, or controller input.
- Touches enqueue semantic UI actions; game callbacks never run re-entrantly
  inside the panel window procedure.
- Inventory membership and the live player are reacquired immediately before
  every action; all cached identities are discarded on player/level changes.
- Level teardown and render reads are serialized, while the driver `Present`
  and the engine's teardown call run outside that lock to avoid deadlocks.
- UI virtual calls are guarded by MSVC RTTI hierarchy checks and fail closed on
  an unexpected object type.
- The title and loading screens remain untouched. Once a live player is bound,
  only the native health, stamina, radar/map and held-item HUD groups are
  hidden; the crosshair, XP/Fury, objectives, prompts and notifications remain.
- Map and player queries stop during loading and resume only after the new main
  level is proven live.
- Repainting and the 5.4 MB panel upload occur only when visible state changes.

## How player discovery works

Dead Island does not export a local-player accessor. The mod first uses the
engine's typed nearby-actor query, converts each returned interface pointer to
its proven `CRTTIObject` base, and identifies the player by capabilities such
as health plus player-only progression or Fury fields. A bounded object-id scan
remains as a compatibility fallback.

Values are then read by reflected field name. Missing fields are displayed as
unavailable; zero is never substituted for a failed read. Set `dump_player = 1`
temporarily in the INI to log every reflected field on a newly encountered game
build.

## Files and logs

- Configuration: `DeadIslandSecondScreen.ini`
- Current log: `DeadIslandSecondScreen.log`
- Previous-run log: `DeadIslandSecondScreen.log.prev`

The most useful lifecycle messages are `selected main ILevel`, `player bound`,
`level deactivated`, `[gameplay] verified direct ...`, and the per-page
`[native]` snapshot lines.

## Building

Use the 64-bit MinGW-w64 toolchain at `C:\msys64\mingw64\bin`:

```powershell
.\build.ps1
.\build.ps1 -ValidateOnly
.\build.ps1 -Game "C:\path\to\Dead Island DE" -Install
```

The build script validates the result as a PE32+ x64 DLL, checks all nine DXGI
proxy exports, verifies ASLR/NX flags, and rejects dynamic MinGW runtime
dependencies.

The standalone preview tool writes all four pages as 1240x1080 bitmaps without
starting the game. It is for layout review only; native gameplay data still
requires an actual loaded save.

## Test checklist

After installing a new build:

1. Confirm the title screen remains visible and the lower UI does not claim a
   player before a save is loaded.
2. Load a save and confirm Status changes from `Locating the player` to live
   values without a crash or long frame stall.
3. Open Inventory without first opening the vanilla inventory. Verify the list
   is already populated, there is no duplicate quick-slot strip, selection is
   immediate, and Equip/Use targets the held row.
4. Change areas, die/respawn, and reload a save; stale values must disappear
   during each transition.
5. Leave Map selected during a transition and verify it waits for the new
   player instead of querying the retiring level.
6. Open Skills without first opening the vanilla upgrade menu. Verify all three
   branching trees and original icons appear. Buy an available rank and verify
   the point count, prerequisites, and rank update and persist.
7. Tap every tab and verify no touch causes an attack or jump on the top screen.
8. Check the log for missing exports, failed type validation, or repeated error
   spam.
