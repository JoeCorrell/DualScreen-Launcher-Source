# Fields of Mistria Second Screen

This project is an exact-version second-display mod for the current x64 Fields
of Mistria 1.0.4 build. Its native foundation creates
an independent no-activate panel and accepts isolated WEMU input on IPv4
loopback UDP port 7788. Its GML bridge publishes bounded live-state deltas from
the real game models and accepts only strictly validated actions on the game
thread.

It deliberately contains no fake values, screenshots, OCR, or redistributed
proprietary game artwork. Enabled mutations are real hotbar selection, slot
swaps, armor equip/unequip, ordinary-FSM consumable use, and learned-spell
pin/unpin/cast actions. Each request is session-, capability-, revision-,
index-, and where applicable exact-item-fingerprint gated. Unsupported or
unsafe actions fail closed.

The custom seven-page journal renders the real inventory/equipment, authored
map pages and markers, quests and requirements, villagers, animals, skills,
magic, almanac, museum, renown, calendar, clock, weather, and player status.
It resolves only source-selected logical art IDs through a typed, collision-
rejecting index of the user's exact installed archive. Primitive fallbacks
remain available if an installed resource cannot be validated.

The package's `SDL3.dll` is an exact PE export-forwarding proxy. Every one of
the installed SDL 3.4.8 DLL's 1,271 names and ordinals forwards to
`SDL3_FieldsOfMistriaOriginal.dll`; no SDL function prototype is re-declared.
The deploy script preserves the user's exact original DLL under that name.

Supported executable SHA-256:
`aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e`

Supported SDL3 SHA-256:
`532b231761a6d6c391069865bde37f6d069f3beedbcf3cfbd0e314bf25ca3157`

Build with `powershell -ExecutionPolicy Bypass -File .\build.ps1`. Deployment
is dry-run by default and is intentionally not part of the build.

Inspect a device without changing it:

```powershell
powershell -ExecutionPolicy Bypass -File .\deploy.ps1 `
  -Endpoint <adb-host:port>
```

The default target is the confirmed installed directory
`/storage/emulated/0/Games/Fields.of.Mistria.v1.0.4`. The deployer verifies the
exact executable, classifies only a pristine original or a fully owned prior
install, verifies every hash, and reports whether the exact game process or one
of the managed game files is mapped. Unrelated launcher, Wine, FEX, or Box64
processes are never targeted. `-Commit` is required for any push or rename. A committed fresh
install renames the user's original to `SDL3_FieldsOfMistriaOriginal.dll` and
rolls back explicitly on failure; it never ships that proprietary DLL.

If Wine exposes WEMU's spanning root as one Win32 monitor, the panel uses the
bounded `[Panel]` fallback rectangle in the INI (Thor default: `(0,720)
1240x1080`). A real enumerated secondary monitor still takes precedence.

The separately audited GML archive is installed with `deploy-assets.ps1`, also
dry-run by default and requiring `-Commit`. It retains the exact original as
`assets.zip.fomds-original-e55ef6bcaa743000`.

The current native and GML generations were transactionally installed and
subsequent read-only audits verified exact no-ops. All static, parser, action,
renderer, input, lifecycle, ABI, determinism, archive-CRC, and deployment tests
pass. Runtime GML compilation, live-save behavior, FEX/Wine rendering, actions,
and long-session performance remain validation items because the user cannot
test the game at this time; they are not represented as proven here.
