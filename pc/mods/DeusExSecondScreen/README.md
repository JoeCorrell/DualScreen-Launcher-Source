# Deus Ex: Human Revolution Director's Cut — Dual-Screen UI

This mod adds a purpose-built, black-and-gold Deus Ex interface to the AYN
Thor's 1240×1080 lower display. The upper display remains the gameplay window;
the lower display provides five touch pages:

- Inventory
- Augmentations
- Map
- Objectives
- Data

The lower interface is not a screenshot and does not open the game's pause
menus. It is rendered by the mod from live game state, the game's shipped
SmartGlass serializers, and original artwork decoded directly from the user's
installed game archives. The supported configuration leaves the retail
Scaleform HUD on the upper display: intercepting its anonymous root while a
save constructs movies proved unsafe under Wine/WEMU. The independent lower
UI does not depend on that interception. Experimental whole-HUD suppression
remains opt-in and is deliberately disabled by default.

The panel window uses `WS_EX_NOACTIVATE`, and Thor touch packets are accepted
only from `127.0.0.1:7788`. Touching the lower display therefore does not take
Windows focus, inject a keyboard/controller binding, fire a weapon, or divert
normal controller input from gameplay.

## Compatibility

This is a binary mod for one executable:

| Requirement | Supported value |
| --- | --- |
| Game | Deus Ex: Human Revolution **Director's Cut** |
| Executable | `DXHRDC.exe` **2.0.66.0** |
| Verified executable SHA-256 | `7A3EBF1397ECDB9F6BD3B624CB5346D6A3A24107E516126D0A1EDE1C62A8E7FA` |
| Architecture | 32-bit x86 (`PE32`, machine `0x14c`) |
| Renderer | DirectX 11 |
| Lower display | Designed for 1240×1080; layout scales proportionally |

The native action and SmartGlass bridges validate exact machine-code
signatures before installing their hooks. A different executable revision is
not assumed to be compatible: a failed signature is logged and the affected
feature stands down instead of calling an unknown address.

DirectX 9 is not supported. If `d3d9.dll` is selected, the game never creates
the DXGI swapchain this proxy needs, and the watchdog explains that in
`DeusExSecondScreen.log`.

## Pages and touch controls

### Inventory

The Inventory page shows live weapons, a square paged item grid, health,
energy, XP, credits, Praxis points, four supported augmentation shortcuts, and
all ten quick slots. Item and augmentation art is decoded from the game's own
DTP and RenderResource archives and aspect-fitted without replacing the
original assets. Locked Typhoon, Cloak, Smart Vision, and Run Silently slots
show a crisp custom padlock overlay instead of borrowing an unrelated texture.
Every firearm card also shows its live loaded rounds and effective magazine
capacity, including installed Ammo Capacity mods; a small rail and low/empty
color cue make reload state readable without opening the vanilla HUD.
The equipped weapon also gets a narrow gold loadout rail and its matching quick
slot is outlined, while the storage grid remains neutral for drag-and-drop.

- Tap an inventory item to select it.
- Hold an item to use, consume, or equip it when the game permits that action.
- Tap a weapon card to equip it; tap the equipped weapon again to holster it.
- Drag one item onto another to combine them when valid, or move/swap it.
- Select an item, then tap a quick slot to assign it. Dragging an item to a
  quick slot also works.
- Tap a quick slot to equip or use it. Consumables are consumed immediately;
  tapping an already-equipped weapon quick slot holsters it.
- Hold a quick slot to clear it.
- Hold the Discard target, or drag an item onto it, to discard safely. A normal
  tap is intentionally non-destructive.
- Six item cards are available by default. The two native Cybernetic Arm
  inventory-capacity upgrades expand the global lower-screen allowance to
  twelve and then eighteen cards. Capacity never repeats on later pages; a
  locked next page remains visible so its upgrade path is clear. This follows
  the saved augmentation state and does not create a second mod progression
  system.
- Use the enlarged page arrows to browse additional items. Both arrows wrap
  around, so the previous button on page one opens the final page.

These operations call the game's native inventory, combine, move, use, and
quick-slot methods. They are serviced on the game/Present thread rather than
being translated into mouse clicks or network commands.

### Augmentations

Shows every native augmentation family as a square button with its original
game icon. Selecting one opens its authored DTP dependency graph in the side
panel, including the real tiers, branches, localized name/description/help,
Praxis cost, and separate locked, prerequisite-blocked, available, purchased,
and complete states. Select an available node (or the family itself) and use
the purchase control to run the retail validator and the game's own Praxis and
install path. The page fails closed if native data or validation is unavailable;
it never substitutes a fabricated tree or retries a rejected purchase over the
SmartGlass transport. Purchases and changes made in the original menu refresh
the lower screen immediately.
The four active augmentation cards on Inventory call the retail Typhoon,
Cloak, Smart Vision, and Run Silently callbacks directly when installed; a
locked card opens its matching augmentation family instead. A live gold
overlay identifies Cloak, Smart Vision, and Run Silently while they are active;
Typhoon receives the same feedback as a short pulse when it fires.

### Energy quality of life

An experimental hook can extend automatic recharge across Jensen's entire
installed energy reserve instead of stopping after the first cell. It is
disabled by default because the underlying game-method ABI has not proved safe
across every Windows and Wine/WEMU environment. Enabling
`gameplay.full_energy_recharge` is for controlled diagnostics only; signature
checks fail closed on executables other than Director's Cut 2.0.66.0.

### Map

Shows the current location, sublocation, and the same locally extracted vanilla
building/floor artwork used by the game. Each floor uses its real raster
dimensions, preserving its aspect ratio instead of stretching it to the lower
display. Jensen is a live directional arrow driven by the exact player position,
facing transform, and authored map-axis correction used by the retail map
serializer. Mission markers and current-floor radar contacts are composited on
the same transform; friendly contacts are green, hostile contacts are red, and
other contacts use distinct neutral/caution colors. The floor selector lists
the real authored levels available for the current map and wraps in both
directions. Mission diamonds come from the game's live locator records, with
the tracked objective emphasized in gold and filtered to its matched floor.
Drag the tactical field to pan, pinch to zoom, or use the Zoom In, Zoom Out,
and Center on Jensen buttons.
Pan is bounded to the zoomed floor, and every raster/marker/arrow overlay is
clipped to the map canvas.
The center control reports `FOLLOWING JENSEN` while live auto-follow is active;
a manual pan changes it to `RESUME AUTO-FOLLOW` so the current camera behavior
is never ambiguous. Center also returns directly to Jensen's current floor.

### Objectives

Shows active primary and secondary objectives with the selected mission's full
description and current tracking state. Tap an objective to inspect it, then
use Track Objective/Stop Tracking to call the native mission locator handler.
Swipe or use the page controls to browse longer lists.

### Data

Shows discovered e-mails, pocket secretaries, and other media read directly
from the player's native archive. It remains functional when the retired Xbox
SmartGlass service is inactive. Tap an entry to read it; swipe or use the page
controls for additional records.

The left and right buttons beside the navigation strip cycle through all five
pages with wraparound. They are touch controls, not injected LB/RB input.

## Startup behavior

The panel window is created early so WEMU can claim its swapchain, but its
buffer remains blank through logos, the title screen, save selection, and
loading. The custom interface appears only after the game has transitioned
into a playable save. This is intentional and controlled by
`ui.hide_until_gameplay`.

During save changes, stale inventory and touch targets are retired immediately.
If the native player graph remains detached while a retail menu owns input, a
five-second hysteresis closes the gameplay gate and blanks the lower display.
This catches unnamed loading transitions without mistaking brief hacking,
dialogue, or streaming handoffs for a return to the title screen.

Returning to a menu does not make the lower panel steal focus. No lower-screen
page acquires the game's pause/menu input lease, so opening or changing a page
does not pause the world above it.

The retail HUD and genuine modal interfaces—pause, hacking, dialogue choices,
vendors, and controller-opened menus—remain visible on the upper display. The
independent lower panel does not redirect or replace any of them.

## How it works

`DXHRDC.exe` loads the local `dxgi.dll`, which forwards the normal DXGI exports
to the system DLL and installs the mod around the game's swapchain:

1. A no-activate panel window and independent DXGI swapchain are created on
   `THOR-BOTTOM`.
2. The game's Scaleform UI remains untouched. An unsafe whole-HUD suppression
   diagnostic exists behind a separate opt-in flag and is disabled by default.
3. Guarded native snapshots read inventory, status, augmentations, missions,
   discovered media, current map/floor state, and radar contacts directly from
   the same game containers used by the retail UI. The optional dormant
   SmartGlass layer is retained only as a compatibility fallback; nothing is
   sent over a network.
4. The inventory snapshot supplies exact slot indices, item positions, counts,
   health, credits, and quick-slot assignments.
5. The custom UI is composed into an opaque 1240×1080 panel texture. Original
   pickup, augmentation, lock, and floor-map textures are drawn over it through
   guarded D3D11 ShaderResourceViews immediately before the lower swapchain is
   presented.
6. Touch gestures received on loopback are queued and drained on the primary
   game thread. They never become Windows mouse, keyboard, or controller input.

The lower swapchain is presented with `DXGI_PRESENT_DO_NOT_WAIT`, so a delayed
SurfaceControl consumer cannot back-pressure the game's main Present.

## Installing

Place these files beside `DXHRDC.exe` inside the game/container directory:

```text
dxgi.dll
DeusExSecondScreen.ini
DeusExSecondScreen\MapCache\...  # generated locally for vanilla floor art
```

Only one proxy named `dxgi.dll` can own that location. Remove older copies or
other DXGI proxy mods before installing this build.

On an AYN Thor, install the matching `WEMU-Thor-DeusEx.apk` build and enable
WEMU's second-screen support for the container. See
`dist/DeusExSecondScreen-AYN-Thor/THOR_INSTALL.md` for the handheld steps.

To uninstall, remove this mod's `dxgi.dll`, `DeusExSecondScreen.ini`, and the
`DeusExSecondScreen\MapCache` directory from the directory containing
`DXHRDC.exe`.

## Building

The DLL must be compiled with MSYS2's 32-bit MinGW toolchain:

```powershell
# Install once from an MSYS2 shell:
pacman -S mingw-w64-i686-gcc

cd dualscreen/mods/DeusExSecondScreen
powershell -ExecutionPolicy Bypass -File .\build.ps1
powershell -ExecutionPolicy Bypass -File .\tools\BuildMapCache.ps1 `
  -Game "C:\Games\Deus Ex Human Revolution Director's Cut"
powershell -ExecutionPolicy Bypass -File .\build.ps1 `
  -Game "C:\Games\Deus Ex Human Revolution Director's Cut" -Install -ReplaceConfig
```

`BuildMapCache.ps1` reads the maps from the user's own game installation and
creates a validated local cache under `build\map-cache`. The derived artwork is
ignored by Git and is not redistributed with the source tree. The installer
publishes its completion marker last, so the runtime never trusts a partial
cache.

`build.ps1` validates that the result is a 32-bit PE DLL, retains ASLR/NX, has
all required undecorated DXGI exports, and does not depend on MinGW runtime
DLLs. Before installation it also verifies the exact supported 2.0.66.0 game
executable by SHA-256. Existing INIs are preserved by default and the current
template is written as `DeusExSecondScreen.ini.new`; `-ReplaceConfig` makes a
verified timestamped backup and installs the safe current defaults. The
`Makefile` is for a real MSYS2 shell; do not invoke it from Git Bash,
because combining two MSYS runtimes can make the compiler fail without useful
diagnostics.

## Configuration

The provided `DeusExSecondScreen.ini` documents every supported user-facing
option. The normal Thor configuration needs no coordinate overrides.

Important options:

- `panel.enabled`: disable the mod without removing the files.
- `panel.relocate` and `panel.whole_ui`: retained for compatibility with older
  INIs; neither enables native HUD interception.
- `panel.suppress_native_hud_experimental`: unsafe diagnostic that suppresses
  the native HUD root; leave disabled for normal use.
- `ui.tabs`: show the five-page navigation strip.
- `ui.hide_until_gameplay`: keep the lower surface blank until a save is live.
- `input.reserve_back_experimental`: diagnostic controller interception; leave
  disabled so Back/Select remains assigned to normal gameplay.
- `display.borderless_16_9`: keep the upper gameplay client at 1280×720 on the
  Thor instead of a bordered or remembered 4:3 Wine window.

## Troubleshooting

`DeusExSecondScreen.log` is written beside `DXHRDC.exe`. Check it before
changing coordinates or renderer settings.

### The WEMU console remains on the lower screen

- Confirm the container has **Second screen** enabled.
- Confirm WEMU exposes separate `THOR-TOP` and `THOR-BOTTOM` monitors.
- Remember that the panel is deliberately blank until a playable save loads.
- Search the log for `[panel] ready` and `gameplay HUD detected`.

### The game runs but the lower screen stays blank after loading

- Confirm the game is using DirectX 11, not DirectX 9.
- Search the log for `[sgui] native second-screen serializers attached`.
- Search for a `signature does not match DXHRDC 2.0.66.0` error. If present,
  verify the executable version rather than forcing an offset.
- Confirm only one `dxgi.dll` is beside the executable.

### The UI appears but touch does nothing

- Install the matching WEMU APK containing the second-screen touch bridge.
- Search the log for `[touch] listening ... 127.0.0.1:7788`.
- Another guest process must not already own loopback UDP port 7788.
- Do not focus or click through the upper game window as a substitute; lower
  touch is intentionally isolated from Windows mouse input.

### The upper game is a bordered, square, or stretched window

Keep `display.borderless_16_9=1`, `display.width=1280`, and
`display.height=720`. Desktop testing can instead use the `[test]` options in
the INI.

### The game does not start

- Verify that the DLL is 32-bit and the game is `DXHRDC.exe` 2.0.66.0.
- Remove duplicate/older DXGI proxies.
- Restore the original game files if another binary patch changed the
  executable.
- Read the last line of `DeusExSecondScreen.log`; hooks fail closed when their
  expected code is not present.
