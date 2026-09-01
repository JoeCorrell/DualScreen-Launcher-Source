# Grim Dawn 1.3.0.7 technical investigation

## Device and launch identity

- AYN Thor, Android 13, WEMU `com.wemu` 0.1.0.
- Install: `/storage/emulated/0/Games/Grim Dawn`; Wine drive path
  `F:\Games\Grim Dawn`.
- Captured process command: `F:\Games\Grim Dawn\Grim Dawn.exe`.
- The running executable is the root I386 image. `x64/Grim Dawn.exe` was not
  the launched image.
- Runtime: Proton 11.0-5-arm64ec with `libwow64fex.dll` and esync.
- Displays: top 1280x720, bottom 1240x1080, vertically stacked.
- Renderer log: D3D11, feature level 11.1, Turnip Adreno 740 wrapper, DISPLAY1
  and DISPLAY2 enumerated.

The executable resource says 0.3.0.0 and is stale. Grim Dawn's own `log.xml` and
crash metadata both say 1.3.0.7; the implementation additionally gates immutable
binary identities:

| File | Size | SHA-256 |
|---|---:|---|
| `Grim Dawn.exe` | 3,812,864 | `aacf685f9a11f2a3ca41162473eca83c1b01540143a536453eee18dac97b80e8` |
| `Game.dll` | 8,871,936 | `fa2e444b9d89496156f8e403d264c34651303b63250147de6ff2b489c048017f` |
| `Engine.dll` | 3,668,480 | `687adb5626df2ca6ad3a3048cd29b8ef73fda495ab3d0b622c10da9b1420d580` |

The executable COFF timestamp is `0x6a7f6a16`. Version-dependent data is kept in
the build gate and export-resolution layer.

## Installed content and data authority

The live process opened base, Ashes of Malmouth (`gdx1`), Forgotten Gods
(`gdx2`), Fangs of Asterkarn (`gdx3`), and survival/Crucible resources. Static
definitions live in ARZ databases and ARC resources. The intended cache merges
base then expansion records by load precedence and resolves record IDs, strings,
icons, skills, constellations, recipes, quests, factions, and map definitions
once. Dynamic state remains authoritative in the live game.

Active saves are under
`C:\users\Public\Documents\Steam\RUNE\219990\remote\save`; the observed
character is `_joe/player.gdc`. Saves are cold/offline evidence only, never the
live source and never a write target for actions.

## Module ownership

- `Engine.dll`: database/archive/resources, Lua, display/widget dispatch, world,
  regions, input, and renderer abstraction.
- `Game.dll`: player/character, inventory/equipment, skills/Devotion, Quest2,
  factions, crafting, teleport, hotbar, and game commands.
- `Direct3D11.dll`: engine D3D11 renderer. It exports primary and secondary
  surfaces, but the mod does not swap or duplicate the gameplay framebuffer.
- `DirectInput.dll`: game input backend and event enumeration.
- `lua51.dll`: LuaJIT-compatible scripting API. Content scripting is not a
  sufficiently complete native plugin ABI for the requested interface.

`Game.dll` retains thousands of decorated C++ exports. The exact main executable
imports the same functions, giving a much stronger seam than scanning arbitrary
memory. High-value data exports include `gGameEngine`, `GetMainPlayer`, Character
vitals/level/XP/money/name, `GetTotalCharAttribute`, inventory vectors,
skill ranks/cooldowns/status skills, Devotion points, Quest2 repository methods,
faction values, detail-map data, player location, and teleport information.

## Runtime call path

```text
main EXE IAT -> Game.dll!GameEngine::Update
                         |
                         +-- original update first
                         +-- exact-build sampler on owning game thread
                               |
                               +-- gGameEngine -> GetMainPlayer
                               +-- exported getters / validated POD copies
                               +-- immutable Snapshot + domain revisions
                                             |
                                      SRW-protected copy
                                             |
                              independent lower panel thread
```

The hook changes one verified, aligned IAT pointer. It does not patch function
bytes, enumerate memory regions every frame, call game objects off-thread, or
cache player/world pointers across loading and character transitions.

The first sampler runs at 10 Hz and publishes only changed state plus a one-second
heartbeat. Every pointer, vector boundary, aggregate scalar, wide string, and
MSVC x86 string is checked before publication. Slow samples over 5 ms and panel
renders over 8 ms are rate-limited performance warnings.

## Rendering and input

The lower UI is an opaque 32-bit DIB rendered by GDI in its own no-activate popup
window. Its exact identity is `GrimDawnSecondScreenPanel` / `Grim Dawn
Dual-Screen`. It is placed at the non-primary monitor's real origin before being
shown and does not touch the D3D device, gameplay camera, primary framebuffer,
or normal menu stack.

WEMU now claims that exact identity as lower-only before initial Wine positioning
finishes, retains ownership through surface recreation, avoids legacy X touch
injection, and routes only Select-modified controller commands. The panel's UDP
parser is strict and the center bottom system-gesture strip is ignored.

## Remaining exact work

Static export evidence is not accepted as live proof. Physique, Cunning, Spirit,
OA, and DA use the primitive total-attribute getter with exact enum values
1/2/3/21/23, not the earlier ambiguous `CharacterBio` array indices. The next device pass must
validate calling conventions and synchronization while damaging/healing,
spending Energy, gaining XP, equipping and looting, transitioning areas, and
loading/death. Only then can entity/record resolution and domain-specific event
hooks be promoted.

The full implementation still requires ARZ/ARC cache tooling; inventory bags and
equipment slots; item definitions and icons; mastery/skill/Devotion graphs;
Quest2 objectives/tracking; factions; map tiles/discovery/Riftgates; recipes and
materials; bounded epoch-tagged game-thread actions; controller focus; mutation
rule enforcement; profiling; multiplayer restrictions; and long-session soak
tests. Those gaps are visible in the proof matrix and are not hidden behind
placeholder values.
