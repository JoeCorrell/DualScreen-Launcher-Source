# Grim Dawn Dual-Screen

This directory owns the native Grim Dawn second-screen implementation for the
WEMU dual-display device. It targets the **installed root 32-bit Grim Dawn
1.3.0.7 executable**, not the unused `x64` copy.

The current milestone is an exact-build-gated, read-only diagnostic bridge and
an independent lower-screen renderer. It is intentionally not described as a
finished mod: inventory records, equipment names, skills, Devotion, quests,
factions, crafting, map reconstruction, and all write actions remain disabled
until each real game path is live-validated.

## Current architecture

- A PE32 `dxgi.dll` proxy forwards every exported DXGI entry it implements to
  Wine's real PE32 DXGI module.
- SHA-256, size, PE machine, and PE timestamp checks gate the exact executable,
  `Game.dll`, and `Engine.dll`. An unknown build receives no hooks or panel.
- One aligned main-executable IAT slot for exported
  `GAME::GameEngine::Update` is replaced after its live target is verified.
- The hook calls the original first, then samples exported game methods on the
  game thread. It never scans process memory per frame.
- A bounded immutable POD snapshot crosses to a separate GDI/DIB panel thread.
  The panel never dereferences game objects.
- The panel uses `GrimDawnSecondScreenPanel` / `Grim Dawn Dual-Screen`, is
  non-activating, and occupies the exact non-primary monitor rectangle.
- Touch/controller commands arrive through WEMU's loopback UDP protocol. The
  parser rejects embedded NULs, trailing tokens, non-finite numbers, out-of-
  range coordinates, and oversized packets. No input is injected into gameplay.

The first live fields are HP, Energy, level, XP, name, Physique, Cunning,
Spirit, Offensive Ability, Defensive Ability, Iron Bits, inventory entity
count, skill points/object count, Devotion point totals, known Riftgate ID count,
difficulty ID, world position, and engine area tag. Every optional field
has semantic/range validation and a capability bit; unavailable pages say so
instead of inventing content.

## Build

```powershell
.\build.ps1 -Configuration Release
```

The build runs native parser/state/build-gate tests, emits a static PE32 proxy,
checks its architecture and exports, rejects dynamic MinGW runtime dependencies,
and writes `package/manifest.json`.

`deploy.ps1` is dry-run by default. It verifies the three remote game hashes,
requires Wine to be stopped, checks that the Games prefix will honor native
DXGI, refuses an unowned existing proxy, stages and hashes every file, and keeps
owned prior files in a timestamped recoverable directory. Use `-Commit` only
after the dry run succeeds. Do not copy `dxgi.dll` by hand over another proxy.

See [TECHNICAL_INVESTIGATION.md](TECHNICAL_INVESTIGATION.md) and
[PROOF_MATRIX.md](PROOF_MATRIX.md) for evidence and remaining validation. The
exact current-build call catalog is in
[docs/ABI_CATALOG_1.3.0.7_I386.md](docs/ABI_CATALOG_1.3.0.7_I386.md).
