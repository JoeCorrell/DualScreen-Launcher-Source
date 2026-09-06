# Isaac Second Screen

Full in-process dual-screen patch for **The Binding of Isaac: Repentance+
v1.9.7.17.J460**. This is a native PC-game mod, not an Android companion UI.

The launcher installs a transparent PE32 `winmm.dll` proxy beside
`isaac-ng.exe`, matching the bootstrap style used by the other native PC-game
mods. It forwards Isaac's three imported WinMM functions to the real 32-bit
system library, then verifies the exact `isaac-ng.exe` SHA-256 before opening a
no-activate 1240x1080 guest window on the lower monitor.
WEMU captures that window directly and forwards lower-screen touch to its local
UDP input endpoint on port 7788.

The official Isaac Mod API remains authoritative for player, map, inventory,
and run state. Because J460 removes both LuaSocket and `package.loadlib`, the
exact-build patch hooks the executable's `luaL_loadfilex` import and publishes
a single bounded `IsaacSecondScreenSend` function into the sandbox before mod
scripts load. No helper process is required. The native receiver rejects
malformed, stale, and oversized packets. Map rooms are filtered by
`RoomDescriptor.DisplayFlags` in Lua and again in native code so an
undiscovered room is never exposed merely because its descriptor exists.

The DLL owns four purpose-built 1240x1080 dashboards. All four reuse
`assets/custom/background.png`; Stats and Run Info layer
`assets/custom/sticky_note.png` over the lower-left panel for the character and
place their page data to its right. Items uses `assets/custom/item_slot_dark.png`
for a selectable 6x3 collection grid and
`assets/custom/item_description_panel.png` for the live description strip.
Every live label, icon, charge bar, chance, room tile, character portrait,
metric, and collection count stays inside the parchment panels. Horizontal
swipes cycle through map, stats, run info, and items. All dashboards keep the run timer,
pickup counters, and two prepared positions for each active-item, pocket-item,
and trinket category visible at once; a secondary position receives content
only while a second item is actually held. Active items use a live segmented
room-charge bar, and the map includes Devil, Angel, and Planetarium reward
chances. Game-derived item, card, pill, trinket, pickup, map, font, and player
art remains loaded from the exact-build asset catalog.

Build and run native protocol/input tests:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File .\pc\mods\IsaacSecondScreen\build.ps1 -Configuration Release
```

The build emits `build/release/winmm.dll`, creates a standalone
`package` directory, and updates the launcher's verified Isaac payload manifest.
The generated module is PE32/i386 and exports Isaac's complete imported WinMM
surface—`timeBeginPeriod`, `timeEndPeriod`, and `timeGetDevCaps`—plus the
private Lua state bridge entry point.
