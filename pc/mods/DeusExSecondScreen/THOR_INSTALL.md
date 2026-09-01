# AYN Thor installation

This package targets the AYN Thor's 1280×720 upper display and 1240×1080 lower
display. It supports the 32-bit DirectX 11 executable from Deus Ex: Human
Revolution Director's Cut 2.0.66.0.

## Container requirements

- Enable **WEMU owns desktop**.
- Enable **Second screen** for the Deus Ex container.
- Use a Proton build that exposes separate RandR monitors; Proton 11 is the
  current recommended WEMU configuration for this package.
- Select the game's **DirectX 11** renderer. DirectX 9 is not supported by this
  DXGI mod.
- Do not add a second copy of the mod or another `dxgi.dll` proxy beside the
  executable.

The normal WEMU layout exposes `THOR-TOP` at the origin and `THOR-BOTTOM` as a
separate monitor. Do not add manual `panel.x`, `panel.y`, `panel.width`, or
`panel.height` values unless the log proves that monitor detection failed.

## Install

1. Install `WEMU-Thor-DeusEx.apk`. It contains the lower-screen touch bridge
   used by this build.
2. Find `DXHRDC.exe` inside the Deus Ex container's game directory.
3. Back up and remove any older copy of this mod or another local `dxgi.dll`
   proxy; only one DXGI proxy can own this loader position.
4. Copy this package's `dxgi.dll` and `DeusExSecondScreen.ini` into that same
   directory.
5. If you generated the optional local floor-map cache below, copy its
   `DeusExSecondScreen\MapCache` directory beside the executable too.
6. Start the game normally and load a save.

The floor-map cache contains artwork derived from your own game installation
and therefore is not redistributed in the source repository. From a source
checkout, generate it on the PC that holds the game before step 5:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\BuildMapCache.ps1 `
  -Game "C:\path\to\Deus Ex Human Revolution Director's Cut"
```

Then copy `build\map-cache\MapCache` as
`DeusExSecondScreen\MapCache`. If the cache is omitted, the UI remains usable
and safely falls back to its schematic map instead of showing the vanilla
building/floor artwork.

`WEMU-DualScreen-Mods.apk` is the separately packaged dual-screen mod manager.
It is included when both WEMU applications are built from the same source
state, but it does not replace the game-local `dxgi.dll` installation.

## What to expect

The lower surface is intentionally blank during logos, the title menu, save
selection, and loading. Once a playable save has initialized, it opens on the
custom Inventory page with a black-and-gold five-page navigation strip:

- Inventory
- Augmentations
- Map
- Objectives
- Data

The UI uses live game data and original item, augmentation, and floor-map
textures decoded from the installed game, plus a custom padlock overlay. It does not open the vanilla pause
pages or send keyboard/controller bindings. Touch stays on the lower panel
while the controller remains assigned to normal gameplay.

Inventory supports selection, hold-to-use/equip, drag/move/combine, tap-to-
assign quick slots, quick-slot activation, tap-again weapon holstering, hold-to-
clear, and hold/drop-to-discard. Consumable quick slots use the item immediately.
Typhoon, Cloak, Smart Vision, and Run Silently show their vanilla icons and
true native lock state and can be activated by touch. Augmentations can be unlocked or
upgraded and objectives can be tracked through their native game handlers. The
map supports drag, pinch zoom, zoom buttons, centering, vanilla floor artwork,
and live friendly/hostile radar contacts. Longer Inventory, Augmentation,
Objective, and Data lists provide paging controls.

The retail gameplay HUD and real pause, hacking, dialogue, vendor, and other
modal interfaces remain visible on the upper display. The custom lower panel
does not intercept or replace them.

## Troubleshooting

`DeusExSecondScreen.log` is written beside `DXHRDC.exe`.

### The lower screen still shows the WEMU console

- Verify **Second screen** is enabled for this exact container.
- Verify the guest reports distinct `THOR-TOP` and `THOR-BOTTOM` monitors.
- Load a save; blank-before-gameplay behavior is intentional.
- Look for `[panel] ready` and `gameplay HUD detected` in the log.

### The lower UI appears but touch does not work

- Confirm the matching `WEMU-Thor-DeusEx.apk` is installed.
- Look for `[touch] listening for Thor panel gestures on 127.0.0.1:7788`.
- Restart the container if another process already owns UDP port 7788.

### The game never reaches the menu or the log reports no Present

- Confirm DirectX 11 is selected. A log mentioning `d3d9.dll` means the wrong
  renderer is active.
- Confirm there is only one `dxgi.dll` beside `DXHRDC.exe`.
- Verify the executable is Director's Cut version 2.0.66.0.

### The upper display is bordered, square, or stretched

Keep these values in `DeusExSecondScreen.ini`:

```ini
[display]
borderless_16_9 = 1
width = 1280
height = 720
```

## Uninstall

Remove `dxgi.dll`, `DeusExSecondScreen.ini`, and
`DeusExSecondScreen\MapCache` from the directory containing `DXHRDC.exe`. The
APK can remain installed if it is used by other WEMU dual-screen packages.
