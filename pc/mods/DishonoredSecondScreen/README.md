# Dishonored Second Screen

Native dual-screen implementation in development for the user's installed
Dishonored PC build on AYN Thor. Normal gameplay occupies the upper screen;
the requested independent 1240 x 1080 touchscreen contains Map, Powers,
Equipment, Objectives, Journal, Bone Charms, and Statistics.

This is not yet a working or release-ready mod. Investigation begins with the
installed executable and cooked packages, not assumed UE3 offsets. The seven
pages, live state, native actions and device acceptance remain required work.

## Exact investigation target

- Device: AYN Thor, wireless ADB (confirmed by the user).
- Game: `/sdcard/Games/Dishonored`.
- Executable: `Binaries/Win32/Dishonored.exe`, PE32/i386, 18,008,064 bytes.
- SHA-256: `67ced86b1675b8d27c742a84be41eaa7eba41c748a7f4ec1fe05d107d4f91ae5`.
- PE timestamp: `0x5212f0d7`; preferred image base `0x00400000`.
- Image size: `0x011fd000`; build metadata changelist 334700, engine 9411.
- Cooked package version/licensee: 801/30; core packages use LZO compression.
- The complete directory inventory contains 8,680 files, approximately 10.96 GiB.

User-owned binaries, extracted assets and decompiled game code stay in ignored
`.research/dishonored`, `.work/dishonored` and `artifacts/dishonored-*` trees.
They are analysis inputs, not checked-in mod implementation or APK assets.

## Architecture reference

`../SkyrimSecondScreen/src/Bridge.h` and `Mod.cpp` establish the versioned,
bounded snapshot/command boundary, independent no-activate lower window,
isolated loopback touch, cached rendering, and domain revisions.
`SkyrimPlugin.cpp` establishes game-thread state sampling and semantic actions,
explicit lifecycle invalidation, and validation before accessing game objects.
Only these architectural contracts transfer. SKSE, Bethesda layouts, rendering
entry points, FormIDs and addresses do not apply to Dishonored.

Dishonored imports DirectInput8Create from DINPUT8.dll and renders through D3D9.
A transparent DirectInput proxy is a candidate bootstrap, subject to device
validation; the lower renderer must not replace or alter the normal game view.
State must be read from the real game and actions must execute on its game thread.
Unknown capabilities remain unavailable. No save editing, input simulation,
pause-menu automation or fabricated discovery/progression state is acceptable.

## Delivery

The implementation and its UI resources belong in a separately versioned,
downloadable payload. Launcher integration must exclude `mods/dishonored/payload`
from APK assets and use the per-game payload installer. A package must remain
development-only until its native integration and device acceptance pass.

See `RESEARCH.md` for evidence and outstanding work. Research tools operate on
copies and never modify the installed game.
