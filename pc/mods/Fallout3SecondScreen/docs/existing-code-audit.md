# Existing Fallout 3 Second-Screen Code Audit

Audit date: 2026-08-29

Scope: the complete contents of `dualscreen/mods/Fallout3SecondScreen` at the
start of this audit. The device and game installation were not accessed or
modified. No implementation source was changed.

## Executive finding

There is currently no Fallout 3 mod implementation to build, run, or profile.
The directory contained three Markdown files and no source files, generated
code, binary artifacts, build system, tests, installer, configuration, or UI
assets. The repository is therefore a useful Phase 1 planning scaffold, but it
does not yet provide a bootstrap, a live game-state integration, a second-screen
window, input routing, a Pip-Boy UI, or game actions.

This is consistent with the declared project status: Phase 1 is in progress
and no files have been installed (`README.md:11-15`); Phases 2 through 7 are
explicitly not started (`TECHNICAL_INVESTIGATION.md:10-20`); every runtime proof
case is still marked `Not run` (`PROOF_MATRIX.md:10-43`).

The most important root issue is not an isolated bug. The implementation layer
described by the documents has not been created. No feature should be reported
as implemented, integrated, or merely needing polish at this point.

## Repository inventory and build status

Pre-audit inventory:

| File | Role | Executable implementation? |
|---|---|---:|
| `README.md` | Intent, safety rules, intended deliverables | No |
| `TECHNICAL_INVESTIGATION.md` | Reconnaissance record and proposed architecture | No |
| `PROOF_MATRIX.md` | Runtime acceptance matrix | No |

There was no `src`, `include`, `tests`, `assets`, `tools`, `installer`, or
`third_party` tree. No `CMakeLists.txt`, Visual Studio solution/project, Meson,
Premake, Makefile, package manifest, toolchain file, build script, or test
runner exists. Consequently:

- Build result: **not buildable; no build graph exists**.
- Test result: **not runnable; no automated tests exist**.
- Static-analysis result: **not applicable; there is no implementation source**.
- Runtime result: **not deployable; there is no DLL, executable, package, or
  installer output**.
- The two relative documentation links in `README.md` resolve correctly.

No build was attempted because there is no safe local build command or project
manifest to invoke.

## Current architecture: proposed versus implemented

The investigation document proposes a sensible high-level separation, but all
components below remain designs rather than code.

| Component | Documented design | Implemented evidence | Status |
|---|---|---|---|
| Bootstrap | A version-gated 32-bit `d3d9.dll` proxy (`TECHNICAL_INVESTIGATION.md:178-185`) | No exports, forwarding layer, loader, module identity code, or DLL project | Not implemented |
| Engine synchronization | Validated game-thread update point (`TECHNICAL_INVESTIGATION.md:186-187`) | No hook, dispatcher, signature, trampoline, or thread assertion | Not implemented |
| State transport | Immutable revisioned snapshots with bounded caches (`TECHNICAL_INVESTIGATION.md:188-190`) | No state types, ownership model, queue, dirty flags, or serializer | Not implemented |
| Lower display | Dedicated borderless Win32 window (`TECHNICAL_INVESTIGATION.md:191-196`) | No window class, placement code, D3D9 device, renderer, reset handler, or WEMU claim metadata | Not implemented |
| Touch | WEMU loopback gesture input over UDP 7788 (`TECHNICAL_INVESTIGATION.md:197-199`) | No socket receiver, packet parser, coordinate transform, routing, or suppression integration | Not implemented |
| Controller | Controller navigation is an acceptance requirement (`PROOF_MATRIX.md:16-17`) | No DirectInput/XInput interception, focus model, navigation graph, or gameplay-consumption policy | Not implemented |
| Action dispatch | Bounded generation-tagged queue on the game thread (`TECHNICAL_INVESTIGATION.md:200-202`) | No command schema, queue, acknowledgements, preconditions, or mutation calls | Not implemented |
| UI | Custom STAT/ITEMS/DATA/MAP/RADIO interface (`README.md:41-43`) | No widgets, layout, drawing code, typography, icons, animation, theme, or accessibility/controller states | Not implemented |
| Installer | Non-destructive installer/validator/rollback (`README.md:45`) | No manifest, payload, validator, backup, install, uninstall, or rollback implementation | Not implemented |

The design explicitly avoids driving the vanilla Pip-Boy and treats it only as
a behavioral oracle (`TECHNICAL_INVESTIGATION.md:204-209`). That matches the
requested independent interface, but no oracle tracing or replacement data
path exists yet.

## Live integrations and placeholders

### Implemented live integrations

None. The repository has no code that enters the process, obtains a player
pointer, reads a form, subscribes to an event, renders a pixel, receives input,
or invokes a game action.

The only confirmed material is environmental reconnaissance:

- Candidate WEMU/device/container information is recorded at
  `TECHNICAL_INVESTIGATION.md:22-94`.
- The candidate executable identities and hashes are recorded at
  `TECHNICAL_INVESTIGATION.md:96-126`.
- Static string/import observations establish that relevant engine subsystems
  exist, but do not locate their data paths (`TECHNICAL_INVESTIGATION.md:156-172`).

### Placeholder or simulated behavior

There are no fake values, screenshots, mock renderers, or placeholder UI in the
project. This is positive from an integrity standpoint, but it also means there
is no minimal diagnostic implementation. Every extraction domain is explicitly
`Unknown` (`TECHNICAL_INVESTIGATION.md:216-229`).

The proof matrix is a test specification, not evidence. All rows remain `Not
run`, including bootstrap, panel, touch, controller, lifecycle, every state
domain, interactions, stability, and performance (`PROOF_MATRIX.md:10-43`).

## UI, rendering, and input paths

### UI path

No current UI path exists. There is no model/view boundary, screen state
machine, tab/category hierarchy, selection persistence, list virtualization,
font loading, item formatting, screen-scale policy, or animation scheduler.
The requested Pip-Boy styling is represented only by discovered INI values such
as RGB, scanline, brighten, blur, and font names
(`TECHNICAL_INVESTIGATION.md:136-154`).

Important unresolved asset policy: proprietary fonts/textures cannot be bundled
(`README.md:24-25`, `TECHNICAL_INVESTIGATION.md:152-154,276`). A concrete choice
is still needed between runtime loading from the user's own game assets and
freely redistributable custom equivalents.

### Render path

No render path exists. The proposed route is:

`Fallout3.exe -> d3d9 proxy/bootstrap -> independent Win32 lower window ->
lower-window D3D renderer -> WEMU SecondScreenOutput`

Only the proposal is present (`TECHNICAL_INVESTIGATION.md:178-196`). Missing
pieces include complete D3D9 export forwarding, real-runtime resolution under
Wine/DXVK, lower-monitor discovery, device creation, swap chain ownership,
present scheduling, resize/reset/lost-device recovery, state isolation, frame
pacing, and proof that WEMU consistently selects the intended lower window.

No main-camera or framebuffer interference exists today because there is no
renderer. The documentation correctly prohibits camera/render-target switching
and upper-screen capture (`README.md:30-31`,
`TECHNICAL_INVESTIGATION.md:268-272`).

### Touch path

No touch path exists. UDP 7788 is merely the intended transport. The following
are unspecified: message version/schema, endian/length validation, source
validation, duplicate/reordered datagrams, pointer IDs, cancel semantics,
gesture arbitration, display rotation, Android-to-guest coordinate mapping,
and backpressure/flood behavior. The lower-display geometry is itself not yet
confirmed in a live Fallout X session (`TECHNICAL_INVESTIGATION.md:43-46`).

### Controller path

No controller path exists. A design must distinguish navigation activation from
ordinary gameplay input, consume only the commands owned by the bottom UI, and
avoid sending the same press to both the Pip-Boy and the game. The current
document records this as an open question (`TECHNICAL_INVESTIGATION.md:290-291`).

## Offsets, signatures, and engine knowledge

There are **zero documented addresses, offsets, byte signatures, RTTI/vtable
identities, structure layouts, calling conventions, or verified hooks**.

The hook policy is strong in principle: text-section uniqueness, relative-target
validation, pointer/vtable plausibility, multi-source structure validation, and
fail-closed feature gates are required (`TECHNICAL_INVESTIGATION.md:231-244`).
Nothing currently implements or proves those checks.

The primary target appears to be PE32 Fallout `1.7.0.4`, with ASLR/NX enabled,
while the included historical compatibility candidate is `1.7.0.3` without
ASLR (`TECHNICAL_INVESTIGATION.md:107-126`). This makes direct transfer of FOSE
1.7.0.3 offsets unsafe. The investigation correctly lists semantic transfer
between those builds as an open question (`TECHNICAL_INVESTIGATION.md:284-285`).

There is also no non-proprietary reverse-engineering output checked into the
project: no function map, annotated disassembly notes, signature database,
layout headers, symbol map, or evidence captures. Those are the essential next
artifacts for reproducible implementation without redistributing the binary.

## State-domain coverage

| Domain | Reader | Change detection | Actions | Runtime proof |
|---|---:|---:|---:|---:|
| Lifecycle/player generation | None | None | N/A | None |
| HP/AP/XP/level/radiation | None | None | N/A | None |
| SPECIAL/skills/derived stats | None | None | N/A | None |
| Effects/limb condition | None | None | N/A | None |
| Inventory/stacks/condition | None | None | None | None |
| Equipped weapon/apparel/ammo | None | None | None | None |
| Quests/objectives/tracking | None | None | None | None |
| Notes | None | None | None | None |
| World/local map and markers | None | None | None | None |
| Radio availability/tuning | None | None | None | None |
| Caps/currency | None | None | N/A | None |

This matches the repository's own worklist (`TECHNICAL_INVESTIGATION.md:211-229`).
No game-state value is ready to display, and no mutation is ready to invoke.

## Logging and profiling

Logging and profiling are requirements only. Planned rate-limited categories
are listed at `TECHNICAL_INVESTIGATION.md:246-253`, and initial budgets are
listed at `TECHNICAL_INVESTIGATION.md:255-264`. There is no logger, sink,
rotation policy, timestamp/thread metadata, suppression implementation,
performance counter, histogram, working-set sampler, GPU timing path, or build
ID generator.

The proof matrix requests useful artifacts such as exact executable identity,
hook validation, revision counters, CPU distribution, and memory samples
(`PROOF_MATRIX.md:45-54`), but none have been emitted by this project.

Before feature work grows, diagnostics need stable event names, monotonic
timing, per-domain revision/cost counters, bounded file output, and an explicit
privacy rule that avoids dumping player/save contents unnecessarily.

## Prioritized root issues and risks

### P0 - no loadable or buildable foundation

There is no native project, no proxy, and no verified way for mod code to run.
Until a transparent 32-bit bootstrap builds and forwards correctly, every
higher feature is blocked. A forwarding mistake can prevent Fallout or DXVK
from starting, so transparent pass-through and unsupported-binary behavior must
be proven before installing engine hooks.

### P0 - exact successful runtime baseline is not proven

The README says the runtime candidates are identified (`README.md:13-15`), but
the detailed investigation says the precise runtime used for a successful long
session still needs fresh proof (`TECHNICAL_INVESTIGATION.md:86-94`). Historical
logs do not establish a stable gameplay baseline. Implementing against a
possibly unused prefix/runtime risks diagnosing container failures as mod bugs.

### P0 - no validated game-thread synchronization point

All live reads and writes depend on this, yet no candidate function or signature
exists. Reading engine objects from render/UI/socket threads risks use-after-free
during save loads, death/reload, cell transitions, or shutdown. Mutating from
those threads risks reentrancy and heap corruption. This blocks safe extraction
and all actions.

### P0 - no verified engine layouts or APIs for `1.7.0.4`

The target's ASLR and 2021 rebuild make 2009/FOSE raw addresses unsuitable.
Inventory stacks and extra data, quest/objective objects, map markers, and radio
state each need version-scoped semantic signatures and structure validation.
Blindly copying `1.7.0.3` layouts would violate the documented fail-closed
policy and is the largest likely crash source.

### P1 - `d3d9.dll` proxy compatibility under Wine/DXVK is unproven

The executable imports D3D9, but the resolution/override behavior is explicitly
open (`TECHNICAL_INVESTIGATION.md:282-283`). Risks include recursive loading,
incomplete forwarding, wrong-bitness runtime resolution, altered DXVK selection,
work in `DllMain`, and failure on device reset. The proxy should first be tested
as a no-hook identity/logger with exhaustive export forwarding.

### P1 - lower-window ownership and focus behavior are unproven

A second Win32 window can steal activation, redirect keyboard/controller input,
change mouse clipping, or be selected incorrectly by WEMU. Monitor bounds have
not been observed in a live Fallout session. Window creation, positioning,
no-activate behavior, surface selection, shutdown, and load/reset recovery must
be proven before attaching a complex UI.

### P1 - snapshot ownership and invalidation are only slogans

“Immutable, revisioned, bounded” is a sound goal, but the documents do not
define generations, lifetime rules, atomic publication, maximum collection
sizes, update rates, or dirty sources. Without those details, inventory/quest
polling can introduce periodic frame-time spikes, while stale Form or ExtraData
pointers can crash after load transitions.

### P1 - safe inventory actions are substantially more complex than Form IDs

Fallout 3 inventory uses stacks and per-instance extra data for condition,
ownership and equipment state. Equip, apparel replacement, Aid use, ammo, and
drop/buy/sell state cannot safely be modeled as a bare FormID command. Each
action needs a generation, stack identity or stable resolver, game-thread
preconditions, exactly-once semantics, and a postcondition acknowledgement.
None exists.

### P1 - input isolation is not designed at protocol level

The stated goal that touch never reaches gameplay is not yet enforceable. UDP
input plus X/DirectInput can produce duplicate pointer/controller events. A
malformed or high-rate packet stream can also create allocation or queue
pressure. Define and fuzz a bounded parser and prove suppression before action
commands are enabled.

### P2 - map scope has no selected data/render model

The investigation explicitly leaves engine-owned, custom, and hybrid map
representations open (`TECHNICAL_INVESTIGATION.md:292-293`). World markers,
discovery, worldspace transforms, interiors, local fog, fast travel and save
changes cannot be scheduled coherently until that decision is backed by traced
vanilla data builders. A map renderer must remain independent of the gameplay
camera and cannot use top-screen capture.

### P2 - asset and typography strategy is unresolved

The UI must match the Pip-Boy feel without redistributing Bethesda assets.
Runtime extraction/loading and redistributable custom assets have different
installer, cache, error-handling, and performance implications. This choice is
needed before the UI renderer and installer contracts stabilize.

### P2 - no automated validation or reproducible evidence pipeline

The proof matrix is comprehensive but manual. There are no host tests for
signature scanning, PE identity, packet parsing, coordinate mapping, snapshot
diffs, command generation rejection, formatter edge cases, cache bounds, or
device-log parsing. Without these, regressions will be discovered only in long
device cycles.

## Recommended implementation order

1. Add a reproducible 32-bit build system, build ID, warning-as-error policy,
   and host-test target. Keep output out of the source tree.
2. Build a transparent no-hook D3D9 forwarder. Unit-test its export table and
   fail-open forwarding, then prove the exact Wine/DXVK resolution path.
3. Add executable identification using PE metadata plus a version-scoped
   `.text` fingerprint. Unknown builds must forward without engine hooks.
4. Capture one successful, exact device baseline: executable, prefix, runtime,
   DXVK, Turnip, guest RandR monitors, save load, frame pacing, and clean exit.
5. Produce a version-scoped reverse-engineering map for the main update point,
   player singleton/lifecycle, and read-only HP/AP. Store signatures and
   validation rationale, not proprietary code or data.
6. Implement rate-limited logging, crash-safe initialization stages, timings,
   counters, and bounded snapshots before adding collection readers.
7. Prove a diagnostic lower window with synthetic content and strict touch
   isolation. Do not combine first injection, first engine hook, first lower
   surface, and first input receiver into one device experiment.
8. Prove two live scalar values through the game-thread snapshot path, including
   save-load generation invalidation. Only then add inventory/equipment, quests,
   map, and radio as separately gated domains.
9. Add actions only after the matching read model and postcondition proof are
   stable. Keep all UI requests generation-tagged and exactly-once.
10. Build the custom Pip-Boy UI over the proven snapshot/action interfaces,
    then execute every applicable proof-matrix row and retain artifacts.

## Completion assessment

Current engineering completion is **reconnaissance-only**. The environmental
and executable identity notes provide a useful start, and the safety/proof
policies are directionally strong. However, zero requested runtime features are
implemented and zero milestones have runtime evidence. The next meaningful
milestone is not the final Pip-Boy UI; it is a transparent, version-gated,
diagnostic bootstrap plus a proven game-thread read of a small live state set
on the exact device baseline.
