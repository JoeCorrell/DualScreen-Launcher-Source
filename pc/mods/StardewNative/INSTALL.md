# Stardew Valley native dual-screen patch

This build supports Stardew Valley `1.6.15.24356` with one of these original
`Stardew Valley.dll` SHA-256 values:

- Steam: `7F1E5B8E58D2758B78570BA771BBEB03D33522F62188BF6C32EDF0CF626DEAEE`
- GOG: `078C04DCF34FDD8DF3EC1DDCE062D3DFD4A34D94614D806466ADC78754A7693C`

Back up the installed `Stardew Valley.dll`, then replace it with the matching
patched file. The native runtime, UI, and symbols needed at runtime are embedded
in that one game assembly. No `secondscreen` app/module, WebSocket, loose mod
DLL, SMAPI mod folder, or alternate launcher is required.

Both launch modes remain supported:

- `Stardew Valley.exe` starts the embedded panel automatically.
- `StardewModdingAPI.exe` starts the same embedded panel while other SMAPI mods
  continue loading normally.

Do not additionally install the old `StardewSecondScreen` SMAPI mod. It is only
the reference implementation and would duplicate the feature.

WEMU routes the game's private `StardewValleySecondScreenPanel` window to the
lower display. Touch/controller input uses local UDP port 7788; UI pixels and
game state never travel over a network connection.

Diagnostics are written beside the game:

- `StardewDualScreen.Native.bootstrap.log` for embedded load/callback failures;
- `StardewDualScreen.Native.log` for lifecycle, state, asset, input, and render
  diagnostics.

The patcher rejects unrecognized game DLLs. Restore the backup before a game
update and rebuild for the new version rather than forcing this patch.
