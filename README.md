# DualScreen Launcher

DualScreen Launcher is a standalone Android launcher for games with tested
dual-screen mods. It is intentionally separate from WEMU and has a different
application ID (`com.dualscreen.launcher`), data directory, build, and UI.
Installing or removing it does not replace WEMU or alter WEMU's containers.

This project was built from WEMU's production runtime/renderer modules. It does
not include or depend on the old prototype launcher. There are no Steam, Epic,
GOG, community, store, container-editor, or arbitrary-shortcut screens.

## Supported library

| Game | Required build/runtime | Lower display |
| --- | --- | --- |
| Skyrim Special Edition | `SkyrimSE.exe` 1.5.97.0, SHA-256 pinned | Native live game panel |
| Deus Ex: Human Revolution Director's Cut | `DXHRDC.exe` 2.0.66.0, SHA-256 pinned | Native live game panel |
| Dead Island Definitive Edition | The three verified 2016 executable/DLL sizes | Native live game panel |
| Valheim | Windows x64 game files; pinned BepInEx 5.4.23.5 is installed automatically | WEMU Valheim panel |
| Stardew Valley | Windows 1.6 game files; pinned SMAPI 4.5.2 is installed automatically | WEMU Stardew panel |

The player supplies their own game files as a ZIP. No game executable, asset,
mesh, texture, music, or other copyrighted game data is bundled.

## Exact predefined container

The single immutable preset is a direct capture of WEMU container `Games`
(`xuser-2`) on the AYN Thor on 2026-08-28. Among its pinned settings are:

- Proton `11.0-5-arm64ec-6`;
- FEXCore `2608-stable-0` and Box64 `0.4.2`;
- DXVK `3.0-1-gplasync-0` and VKD3D `2.9-0`;
- Turnip `26.2.0`, SurfaceFlinger presentation, mailbox mode;
- the original environment, CPU affinity, audio, DPI, FSR, direct scanout,
  adaptive refresh, and second-screen values.

The exact four installed compatibility component trees were captured with the
preset and are checked against `launcher/src/main/assets/components/manifest.json`
before installation. Box64 and the graphics driver are verified through the
production runtime payload path. A similar-looking public component is never
silently substituted.

## Installation flow

1. The selected ZIP is extracted into a private staging directory with entry,
   expansion, free-space, and path-traversal limits.
2. The launcher finds exactly one supported game root and validates its required
   files and build gates.
3. Any required framework is downloaded from its pinned official GitHub release,
   checked by exact size and SHA-256, and overlaid in staging.
4. The matching dual-screen mod payload is copied from the APK and every file is
   checked against its manifest hash.
5. Game, framework, launch executable, and mod files are verified again.
6. The old installation is retained as a rollback while the verified staging
   tree is atomically published. Only then is the direct launch entry saved.

The persistent lower-screen console exposes the active stage, overall progress,
and a bounded live log while the upper display remains on Games. A failed
install never replaces a working copy.

The Games page is always the initial page. It uses a compact horizontal shelf
with each complete 2:3 portrait cover isolated above its title, status, and
action area. Settings is a separate page exposing launcher
behavior and read-only preset/storage status; it never unlocks container
editing.

## Runtime behavior

Play starts only the installed game's shortcut. A full-window loading surface
covers the Wine environment until a mapped window with that game's verified
identity appears, so Explorer and the container desktop are never presented as
part of the launcher. The second display is selected with WEMU's production
Thor display-topology rule; virtual/private displays are rejected.

Native dual-screen games use the zero-copy secondary SurfaceFlinger output.
Stardew and Valheim use the production local companion protocol and UI. The
session owns one runtime, one top renderer, and one lower presentation, and
releases the presentation, output surface, listeners, handlers, and runtime on
exit.

The lower display is active for the entire product experience: it is a dedicated
console in the launcher, showing readiness, ZIP validation/extraction progress,
the bounded live install log, and clear failures. It then hands off immediately
to a game-specific waiting panel before Wine starts. It never waits for the
Windows desktop or game window before claiming the lower screen.

## Build and verify

Use JDK 17 and an Android SDK configured in `local.properties`:

```powershell
.\gradlew.bat :launcher:testDebugUnitTest :launcher:assembleDebug
```

The APK is written to:

```text
launcher/build/outputs/apk/debug/launcher-debug.apk
```

The integrity tests hash every captured compatibility archive and every bundled
mod payload, assert the captured preset's critical values, and audit the closed
game catalog and pinned prerequisite metadata.

SMAPI is sourced from the official [Pathoschild/SMAPI releases](https://github.com/Pathoschild/SMAPI/releases).
BepInEx is sourced from the official [BepInEx releases](https://github.com/BepInEx/BepInEx/releases).
See [NOTICE.md](NOTICE.md) for inherited notices.
