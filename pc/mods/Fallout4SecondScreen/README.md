# Fallout 4 Second Screen

This project adds a permanent, independent Pip-Boy-style lower-screen interface
to Fallout 4 while gameplay continues on the upper screen. It does not open,
mirror, capture, or relocate the vanilla Pip-Boy.

The target device is the AYN Thor running Fallout 4 through WEMU. The lower UI
is a non-activating native window routed to WEMU's second monitor, with touch
received only through the isolated loopback gesture channel. Live state comes
directly from Fallout 4 1.11.240's in-process `PipboyDataGroup` trees. It does
not require the Pip-Boy companion preference, TCP service, a raised Pip-Boy,
or an open Scaleform menu.

Current status: Phase 1 reconnaissance is complete for the installed
`Fallout4.exe` 1.11.240.0 build. The Phase 2/3 foundation includes exact
singleton/group/value-layout canaries, locked and bounded native-tree copies,
event-driven per-domain synchronization, immutable domain caches, isolated
loopback touch, live world/local marker data, exact-runtime detection, and an
exact-version native game-thread action pump. The action pump is hash,
prologue, executable-address, trampoline, and thread-transaction gated; any
failure leaves the UI read-only. The shipped `ActionsEnabled=1` configuration
now enables inventory, quest, and radio mutations on the exact supported
runtime. An unsupported hash, changed prologue, unsafe game state, stale
identity, or failed install still leaves the panel read-only and reports the
specific disabled/offline/busy state instead of a generic unavailable action.

The three permanent lower-screen modes now consume separate live capabilities
instead of one all-or-nothing readiness flag. ITEMS exposes Weapons, Apparel,
Aid, Misc, Junk, Mods, Ammo, and a Favorites view. DATA consumes the real
quest/objective, note/holotape, world/local-map marker, position, rotation, and
radio branches. STATS consumes PlayerInfo, Stats, Special, Perks, ActiveEffects,
Status, and Log. A missing branch disables only its own page and never traps the
whole interface behind an initialization overlay.

The lower renderer now ports the complete custom Fallout 3 dual-screen visual
system: its exact 1240x1080 logical canvas, colors, Lucida Console sizes, glow,
CRT edge falloff, scanlines, brackets, meters, row geometry, three primary
modes, five subpages per mode, and 230 ms smoothstep page slide. Fallout 4 live
state is converted through a dedicated adapter; no Fallout 3 engine hook,
binary, or proprietary asset is reused. Horizontal swipes switch ITEMS, DATA,
and STATS, while direction-locked vertical gestures scroll the active list.
Controller navigation is scoped behind Select so lower-screen navigation never
steals bare gameplay controls: Select+LT/RT changes the three modes and
Select+D-pad/A navigates the active page. WEMU recognizes both the Fallout 3
and Fallout 4 native panel identities for this isolated route.

STAT now has the Fallout 3 redesign's selectable CND/RAD/EFF, SPECIAL, PERKS,
and GENERAL two-pane layouts. Its artwork is authentic Fallout 4 art selected
by the live `BodyFlags`, `HeadFlags`, perk `clipName`, and optional `SWFFile`
fields. A local preparation tool reads the user's own
`Fallout4 - Interface.ba2`, rasterizes the vector SWFs, and installs the cache
beside the game. The proxy lazily loads at most 48 bounded images; generated
art is not linked into the DLL or included in this repository/package.
Fallout 4 has no Skills subsystem, so PERKS occupies the corresponding visual
role without inventing Fallout 3 skill values.

The active WEMU profile uses FEXCore. A
bootstrap stack overflow was fixed by moving executable hashing off the 1 MiB
worker stack; device tracing now proves DXGI forwarding, exact-runtime gating,
isolated touch binding, lower-window creation, and successful Fallout 4 launch.
The installed native-state build no longer starts the companion client. The
F4SE artifact is research-only and is neither installed nor required. Loaded-
save validation of every individual inventory, quest, and radio mutation is
still required by `PROOF_MATRIX.md`; enabling the guarded path is not recorded
as proof that an operation succeeded. Fast travel is not yet implemented.

Build and test the diagnostic proxy with:

```powershell
.\build.ps1
```

Build and host-test the exact-runtime F4SE lifecycle plugin with:

```powershell
.\build-f4se.ps1
```

Run a deployment preflight (no writes) with:

```powershell
.\deploy.ps1
```

Prepare and preflight the user-owned Fallout 4 artwork cache with:

```powershell
.\tools\Prepare-Fallout4UiAssets.ps1 `
  -InterfaceBa2 "<Fallout 4>\Data\Fallout4 - Interface.ba2" `
  -OutputDirectory ".\build\generated-assets"
.\deploy-assets.ps1
```

See `TECHNICAL_INVESTIGATION.md` for verified facts, `docs/F4SE_BRIDGE.md` for
the native integration gates, and `PROOF_MATRIX.md` for the in-game tests that
define completion. Static tests and a rendered shell are not counted as
runtime proof.

No Fallout executable, archive, script, or asset is included in this project.
