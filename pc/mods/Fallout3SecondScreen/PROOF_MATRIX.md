# Fallout 3 Second-Screen Proof Matrix

A feature is not complete because it compiled, hooked, or rendered once. Each
row must be demonstrated inside the running game with timestamped logs and,
where visual behavior matters, a device capture. Failures and regressions stay
recorded until re-tested against the exact installed executable.

## Milestone gates

| Area | Scenario | Expected evidence | Status |
|---|---|---|---|
| Bootstrap | Supported `1.7.0.4` launch | Exact identity logged; proxy forwards; no startup regression | Not run |
| Bootstrap | Unknown executable | Transparent forwarding; state hooks disabled; actionable log | Not run |
| Panel | Gameplay idle/combat | Lower window remains active; upper input/render unchanged | Not run |
| Panel | D3D reset, minimize/restore, loading | Panel recovers without stale device objects | Not run |
| Touch | Tap/drag/hold/pinch lower panel | Only lower UI reacts; no look/fire/menu action above | Not run |
| Controller | Navigate all pages | UI focus is visible; consumed actions do not reach gameplay | Not run |
| Lifecycle | New game and load save | New player generation; no stale pointers or snapshot | Not run |
| Lifecycle | Interior/exterior/cell/fast travel | State pauses/rebinds cleanly; panel survives | Not run |
| Lifecycle | Death and reload | Old commands rejected; new state acquired | Not run |
| STAT | Damage and healing | HP/max HP track actual HUD/game state | Not run |
| STAT | AP spend/recovery | AP/max AP update without full-state rebuild | Not run |
| STAT | Radiation/effects/limbs | Add, change, expire, cure, cripple, heal all synchronize | Not run |
| STAT | XP/level/SPECIAL/skills | Award, level, effect/equipment changes, save swap synchronize | Not run |
| ITEMS | Pick up and drop | Correct stack/category/quantity/detail deltas | Not run |
| ITEMS | Equip weapon/apparel | Real equipped state and derived stats update | Not run |
| ITEMS | Consume Aid/use item | Real item count and effects update once | Not run |
| ITEMS | Buy/sell/repair | Inventory, caps, condition, equipped state remain consistent | Not run |
| ITEMS | Load different save | No item IDs or selection leak from previous save | Not run |
| DATA | Quest start/update/complete/fail | Quest/objective states and ordering synchronize | Not run |
| DATA | Change tracked quest below | Vanilla tracking and markers reflect the action | Not run |
| DATA | Acquire/read note | Known-note list updates and remains stable on load | Not run |
| MAP | Exterior movement | Player position/heading and discovered markers are correct | Not run |
| MAP | Discover marker | Marker appears once with correct discovered state | Not run |
| MAP | Interior/local movement | Local representation and player transform update | Not run |
| MAP | Fast travel | World/cell/player/markers rebind after load | Not run |
| MAP | Pan/zoom/save swap | Interaction is responsive; no stale world or marker cache | Not run |
| RADIO | Availability/signal changes | Actual station list and availability update | Not run |
| RADIO | Tune/switch/off | Real radio state changes and persists through snapshots | Not run |
| Stability | Dialogue/container/merchant/scripted scenes | No focus theft, deadlock, or menu interference | Not run |
| Stability | DLC areas | Owned/active DLC state works without form-ID assumptions | Not run |
| Stability | 60-minute soak | No crash, unbounded memory/log growth, or desync | Not run |
| Performance | Combat and traversal profile | Budgeted CPU/GPU/allocation/frame-time impact | Not run |

## Local pre-device evidence

| Check | Evidence | Status |
|---|---|---|
| Clean PE32 release build | `build.ps1`; MinGW i686 target; warnings treated as errors | Pass |
| Proxy export surface | Ordinals 1-5 and all five canonical dinput8 names inspected with `objdump` | Pass |
| Runtime dependencies | Release DLL imports only Windows/Wine system DLLs; no MinGW runtime DLL required | Pass |
| Snapshot ABI validation | Version/size/count bounds and monotonic sequence tests | Pass |
| Snapshot stack safety | 1.1 MiB state resets clear persistent storage in place; no PE32 stack temporary | Pass (static/test) |
| Notes source and payload ABI | Acquired list, kind/read validation, typed TNAM/image accessors | Pass (static) |
| World Map state contract | Native MNAM bounds/dimensions/path and marker types validated; invalid map snapshots rejected | Pass (static/test) |
| Local Map isolation | Unsafe MapMenu producer rejected; exact per-cell renderer copies immutable BGRA frames; one projection drives raster/player/door/objective coordinates; native quest-target list and InterfaceShared objective sprite are signature-gated | Pass (static/test); device visual/perf proof pending |
| Local Map projection | Pure projection tests cover pan displacement, player-to-pin alignment, shared zoom scale, and exact 1112x806 content viewport | Pass |
| Interior World Map anchor | Exact `PlayerCharacter+0x588`/virtual-position path; raw interior actor coordinates are rejected and last validated exterior anchor is retained | Pass (static/signature); transition proof pending |
| Native Stats artwork transform | Installed `stats_menu.xml` 145% head/face/torso/limb tree evaluated once in floating point and fitted as one composite; SPECIAL/Skills use a fixed 256x231 family frame | Pass (static/build); device visual proof pending |
| Command safety | Bounded queue, request IDs, and world-generation rejection tests | Pass |
| Executable fingerprinting | PE32 parsing, `.text` range/hash, and unknown-host rejection test | Pass |
| Exact 1.7.0.4 signatures | Unique player, actor-value, container, and localized item-name patterns plus command descriptors | Pass (static) |
| Wine registry transaction | Existing-value preservation, narrow AppDefaults replacement/insertion | Pass (local) |
| Live 1.7.0.4 state bridge | Requires loaded-save runtime proof | Pending launch |
| Device deployment | Exact hash/flags/process/conflict preflight; transactional copy and registry verification | Pass |

## Required artifacts per device run

- Exact executable identity and mod build ID.
- WEMU container/runtime/renderer identifiers.
- Initialization and hook-validation summary.
- Feature revision counters and action acknowledgements, not per-frame dumps.
- CPU time distribution for state capture, UI build, render, input, and actions.
- Working-set/allocation samples at start, transition checkpoints, and end.
- A short description of the save/location/actions used.
- Screenshots or video only as behavioral evidence, never as a data source.
