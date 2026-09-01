# Fallout 3 Second Screen

This project adds a permanent, independent Pip-Boy-style lower-screen interface
to Fallout 3 while normal gameplay continues on the upper screen.

The mod is being built for WEMU's stacked dual-monitor guest and native
second-screen output path. It will not open, mirror, or repeatedly drive the
vanilla Pip-Boy menu. Displayed values must come from live engine state, and
mutating actions must run through validated game systems on the game thread.

## Current status

Phase 1 reconnaissance is complete and the Phase 2/3-safe foundation is
installed on the device. The exact device, WEMU prefix, runtime, current root
`Fallout3.exe` candidate, and lower-display routing contract are documented.
The project now builds a PE32 `dinput8.dll` transparent proxy with exact export
forwarding, executable fingerprinting, a bounded immutable state/action bridge,
rate-limited logs, and an independent no-activate CPU/GDI lower window. The
custom UI is split into the same three top-level Pip-Boy modes: ITEMS (Weapons,
Apparel, Aid, Misc, Ammo), DATA (Local Map, World Map, Quests, Notes, Radio),
and STATS (Status, S.P.E.C.I.A.L., Skills, Perks, General). It never opens the
vanilla Pip-Boy and never touches Fallout's D3D device or gameplay camera.

Swipe horizontally anywhere outside a child interaction to cycle the three
modes with a short slide animation. Vertical drags scroll the active inventory,
quest, note, radio, or perk list and cannot trigger a mode change. On a
controller, hold Select and press LT/RT; bare triggers remain available to
gameplay.

The exact user-owned `1.7.0.4` executable has now been analyzed locally. The
player/ActorValue and inventory bridge is gated by its full SHA-256, current command
descriptors, unique byte signatures, section ownership, primary player vtable,
FormID, thread identity, and finite/range canaries. A bounded one-second
inventory fingerprint now includes worn-stack and condition state, resolves
localized names plus weight/value/condition/damage/armor metadata and each
record's real `TESIcon` DDS path, and rebuilds only after a stable semantic
change. The lower UI preserves icon aspect ratio and lazily resolves base-game
and GOTY DLC texture archives. Live HP/max HP, carry/max carry, actor DR,
and caps count are published directly from engine state. RAD now publishes
native radiation resistance/dose, and General publishes Fallout's complete
26-counter MiscStatManager table only while that tab is visible. General uses
the installed karma artwork and the native level/karma title. Notes now come
from the player's acquired-note list, independently of inventory, and resolve
typed text/image payloads. World Map resolves the active world's installed DDS,
native MNAM bounds, player transform, and live marker records without opening a
menu or touching the gameplay camera. Local Map now uses Fallout's separable
per-cell native map renderer, not the global `MapMenu`: one tile is produced at
a time on the game thread, read back immediately, and published as an immutable
mod-owned raster. The panel owns all cropping, panning, and zooming, and never
receives an engine camera, scene, render target, or D3D pointer. The player
cursor is updated independently from the cached raster, so movement and heading
do not require rerendering 25 terrain tiles; large interior movement triggers a
bounded recenter rebuild only near the raster edge. Exact-binary
tests also prove the native
EquipItem and UnequipItem paths and their six-argument actor ABI; lower-screen
actions are resolved from a fresh FormID and dispatched once on the game thread.

The current candidate build is
`fef3becce18f93df603c81c6576abf7da68abf1b8be6b6ac80af5cb2fce908e1`.
It passes the complete unit and exact-binary suite. Device installation and
fresh-save visual/action proof are pending an ADB connection; the last
transactionally installed build remains documented in the investigation log.
The isolated Local Map implementation is therefore built and statically proven,
but is not claimed runtime-complete until a connected-device run confirms the
native tile output, fog of war, orientation, pan/zoom, frame time, and renderer
state restoration in both interiors and exteriors.

See [TECHNICAL_INVESTIGATION.md](TECHNICAL_INVESTIGATION.md) for confirmed
findings, evidence, decisions, rejected approaches, and open questions. See
[PROOF_MATRIX.md](PROOF_MATRIX.md) for the runtime acceptance tests that gate
each milestone.

Additional focused evidence is in `docs/device-recon.md`,
`docs/wemu-integration.md`, `docs/existing-code-audit.md`, and
`docs/semantic-reference-audit.md`.

## Building and testing

From PowerShell:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\test.ps1
```

The package directory contains only original mod files. It contains no Fallout
binary or asset. Deployment additionally requires a Fallout3.exe-specific Wine
AppDefaults override (`dinput8=native,builtin`); the prefix-wide WEMU setting is
deliberately left untouched. The on-device installer will not commit until ADB
recon confirms the game is stopped, the executable hash matches, the prefix
configuration is current, and any pre-existing proxy has been classified.

Deployment is dry-run-first:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\deploy.ps1 -Endpoint <adb-host:port>
# Only after reviewing a successful preflight:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\deploy.ps1 -Endpoint <adb-host:port> -Commit
```

The commit backs up the prior game-local files and private `user.reg`, installs
through temporary sibling names, verifies both namespaces, and rolls both back
if a commit stage fails. It refuses an unknown existing `dinput8.dll` instead
of overwriting another loader.

## Safety and compatibility rules

- No proprietary Fallout executable, asset, archive, or game data is stored in
  this repository or redistributed by the mod.
- The loader must identify the executable before applying a hook. Unknown
  binaries fail closed with a useful log rather than attempting an offset.
- Hooks use signatures plus structural validation where an exported interface
  is unavailable. A raw address by itself is not a compatibility contract.
- The lower-screen renderer owns a separate window and lifecycle. It must not
  change the gameplay camera, game render target, time state, or menu state.
- Touch is consumed by the lower-screen input path and must not become a mouse,
  camera, attack, or menu event in the gameplay window.
- UI threads produce immutable snapshots and enqueue commands. Engine objects
  are read or mutated only at a validated game-thread synchronization point.

## Intended deliverables

- A version-gated 32-bit native bootstrap/proxy for the installed Fallout 3
  build.
- A dedicated lower-panel renderer and isolated touch/controller router.
- Cached live snapshots for STAT, ITEMS, DATA, MAP, and RADIO.
- Game-thread action dispatch for equipment, Aid, quests, map, and radio.
- Rate-limited diagnostics and performance counters.
- A non-destructive installer, validator, rollback path, and device proof log.
