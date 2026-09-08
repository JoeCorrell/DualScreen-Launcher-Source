# Skyrim Dual Screen Mod

Native second-screen interface for Skyrim Special Edition 1.5.97 on the AYN
Thor. It combines a CPU/GDI lower-screen window with an SKSE 2.0.20 plugin that
reads and changes Skyrim state on the game thread.

## Pages and controls

- Status: Health, Magicka, Stamina, gold, carry weight, level/XP, equipped gear,
  favorites, the current tracked quest objective, and a private character portrait.
- Inventory: category filters, item list, equipped/favorite state, real item
  statistics and effects, an optional native item model, and touch-to-use/equip.
- Magic: known spells and powers with a full-size native asset card, vertical
  details, description, hotbar assignment, and touch equip.
- Quests: Skyrim's player-journal instances, resolved radiant objectives,
  state, and tracking controls.

Only three page buttons are visible at once; the left and right arrows wrap
through all pages. Touch input is handled by the lower-screen process and does
not intentionally move focus away from Skyrim.

The Map implementation is retained but currently disabled in the UI and both
native hosts. It is not part of the navigable pages.

## Architecture

`dxgi.dll` is a proxy used to bootstrap the mod and forward Skyrim's normal DXGI
factory calls. The lower display itself is an isolated CPU/GDI window; it does
not create a second DXGI swap chain. It appears at startup with `WAITING FOR
WORLD TO LOAD`, then switches to the live UI after the SKSE plugin publishes a
validated in-world snapshot.

The SKSE plugin accepts only SkyrimSE.exe 1.5.97.0. It samples the player after
the real `PlayerCharacter::Update`, validates the expected runtime structures,
and sends bounded snapshots to the lower-screen window. Explicit SKSE lifecycle
events put the UI into its waiting state. Ordinary movement, cell changes, menu
use, or a transient failed sample retain the last valid live snapshot instead
of resetting the lower screen.

Optional native visuals use Skyrim's installed assets at runtime:

- Item previews ask Skyrim's `Inventory3DManager` to render the selected model
  into a private off-screen target. No Bethesda item models are bundled.
- The character portrait assembles installed NIF/DDS assets on a background
  worker from a copied appearance manifest. A private neutral skeleton skins
  the armor-only model; a deferred D3D11 context renders a 400x640 target and
  asynchronous readback supplies the GDI panel. It does not clone an actor or
  share Skyrim's UI3D scene. Weapons and shields are intentionally omitted.
- The currently disabled Map path requests one real Skyrim world-map pass. The plugin uses the
  native `MapCamera`, world-map activation/teardown pair, models, textures,
  LOD, weather, and `Main_RenderWorld`, but redirects target zero to a private
  960x540 texture. It restores the gameplay camera and target metadata before
  immediately running the normal gameplay pass. It never constructs or opens
  `MapMenu`, pauses the game, captures input, or passes WorldRoot to UI3D.

Preview buffers are retained and reused. They do not create additional actors,
AI, collision, Havok simulation, or save-game records.

## Configuration

`SkyrimSecondScreen.ini` and `SkyrimGamepad.ini` are installed beside
`SkyrimSE.exe`. Launchers should merge the latter's `bGamepadEnable=1` into the
Wine user's `Documents/My Games/Skyrim Special Edition/SkyrimPrefs.ini` before
starting Skyrim; replacing the full preferences file would discard the
player's graphics settings.

```ini
[panel]
enabled=1
x=0
y=720
width=1240
height=1080

[ui]
touch_port=7788
safe_mode=0
enable_native_item_models=1
enable_character_preview=1
enable_native_map=0
```

`enable_character_preview` controls the private NIF/DDS renderer. The unstable
actor-clone renderer remains retired. The Map feature gate remains disabled
even if an older INI requests it.

`safe_mode=1` disables item, character, and map rendering paths while keeping
the panel, game telemetry, software UI, and touch navigation enabled. It takes
precedence over the individual native options and is the first troubleshooting
step for a renderer-specific crash or black preview. Each native path can also
be disabled independently with its corresponding setting. Changes take effect
on the next game launch.

The fallback panel rectangle is used when Wine reports the Thor as one tall
display instead of separate monitors. `touch_port` is a loopback UDP port and
must match the WEMU integration.

## Stability and performance

- Snapshot counts, strings, numeric values, lifecycle state, and preview sizes
  are validated before they reach UI layout.
- Commands from an old world are discarded during explicit save transitions.
- Identical telemetry does not redraw the full 1240x1080 panel.
- Inventory, magic, quest, marker, and preview work is bounded and
  cached; expensive native visuals are optional.
- The lower-screen panel is isolated from Skyrim's primary swap chain. A panel
  creation or presentation failure is logged and handled without intentionally
  changing the top-screen render target.

Native visuals still depend on the active Wine/DXVK combination. Character
frames are cached and redrawn on appearance/control changes rather than on
every gameplay frame.

## Diagnostic logs

Two logs are written beside `SkyrimSE.exe`; the previous run is retained with a
`.prev` suffix:

- `SkyrimSecondScreen.log`: panel selection and dimensions, configuration,
  touch/keyboard startup, page changes, bridge validation, snapshot transitions,
  preview cache activity, waiting-state reasons, and presentation failures.
- `SkyrimDualScreenPlugin.log`: SKSE lifecycle, hook installation, sampling
  readiness, retained snapshots, inventory/magic/quest/marker refreshes, command
  execution, native item/character/map rendering, and renderer diagnostics.

High-frequency systems log state transitions and bounded summaries rather than
writing every frame.

## Build and install

The build requires MinGW-w64, official SKSE 2.0.20 files, ADB for device installs,
and the extracted interface assets referenced by `build.ps1`.

```powershell
.\build.ps1
.\build.ps1 -Install -Device IP:PORT
```

When dependencies are in another checkout, pass `-DependencyRoot` pointing to
its `artifacts` directory. Build and run the renderer/UI regressions with:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/Run-Tests.ps1 -Build -DependencyRoot 'C:\Projects\New folder (10)\artifacts'
```

The renderer tests use D3D11 WARP and an invisible window. They exercise the
production camera, shaders, winding, bind transforms, readback ordering, world
reset and DDS validation without launching Skyrim. `-HeadNifs` optionally
accepts locally supplied head-part NIFs for a diagnostic geometry render.

Do not install while Skyrim is running. The package places one `dxgi.dll`, the
configuration file, official SKSE runtime/scripts, and
`Data/SKSE/Plugins/SkyrimDualScreen.dll` in the selected game directory.

To remove the dual-screen component, remove `dxgi.dll`,
`SkyrimSecondScreen.ini`, and
`Data/SKSE/Plugins/SkyrimDualScreen.dll`. Keep the SKSE runtime and scripts if
another installed mod uses them.
