# F4SE bridge investigation and implementation

Last verified: 2026-08-29

## Result

`build/f4se/Fallout4SecondScreenF4SE.dll` is a minimal x64 F4SE plugin for the
installed Steam runtime `1.11.240.0` and F4SE `0.7.9`. It currently exports a
process-local, versioned C ABI containing authoritative F4SE lifecycle state.
It deliberately advertises only `FO4DS_CAP_LIFECYCLE`; direct game-tree reads
and mutations remain disabled until their 1.11.240 call/layout contracts pass
the runtime gates below.

The lower-screen `dxgi.dll` can discover the plugin without IPC or blocking:

1. `GetModuleHandleW(L"Fallout4SecondScreenF4SE.dll")`.
2. `GetProcAddress(module, "FO4DS_GetBridgeV1")`.
3. Validate both `struct_size` and `abi_version`.
4. Call `copy_snapshot` on its existing low-rate update tick.

The returned structure contains no dereferenceable engine object. This keeps
the panel/network threads isolated from Fallout's object lifetime and makes a
future immutable game-thread snapshot an ABI extension rather than a reason to
couple the renderer to CommonLibF4.

The DXGI side now implements this contract in `src/F4SEBridgeClient.*`. It
never force-loads the plugin, bounds discovery retries, validates export/module
ownership and all ABI/snapshot fields, copies only POD state, and clears stale
capabilities on malformed/unloaded providers. The lower panel samples it at the
configured diagnostic rate. A verified lifecycle generation change cancels
queued companion actions, invalidates the cached tree while preserving the TCP
connection, and pauses mutation until a valid root and safe lifecycle return.

## Source evidence

| Finding | Evidence | Confidence |
|---|---|---|
| Current F4SE is 0.7.9 for 1.11.240 | Official `ianpatt/f4se` commit `4692e9bba0f87d8b8d1a0b79110bf212f2b2ada7`; `cmake/versioning.cmake`, `f4se_readme.txt`, and `f4se_common/f4se_version.h` | Confirmed |
| Packed runtime value | F4SE `MAKE_EXE_VERSION(1,11,240)` = `0x010B0F00` | Confirmed |
| Required plugin exports | F4SE `PluginAPI.h`: `F4SEPlugin_Version` and `F4SEPlugin_Load` | Confirmed |
| Declarative version record | F4SE `F4SEPluginVersionData` size `0x45C`, compatibility list of 16 packed runtimes | Confirmed by source and local host test |
| Lifecycle messages | `F4SEMessagingInterface`: PostLoad, PostPostLoad, Pre/PostLoadGame, NewGame, GameLoaded, GameDataReady, InputLoaded | Confirmed |
| Pip-Boy classes | DCCStudios CommonLibF4 commit `12beba2a89fe117a14f1707b88c99ecb1b12f8c0` | Supported reverse-engineered declarations; not yet proven on 1.11.240 |
| Address Library support | The 1.11.240 Address Library package is published separately; this project does not copy or redistribute it | Dependency available, not installed/validated on the device yet |

The local source clones are investigation inputs under `.codex-temp`; they are
not project dependencies and are not packaged.

## Exact bridge declaration

- Plugin name: `Fallout4SecondScreenF4SE`
- Plugin version: `0.1.0` (`0x00010000`)
- Compatible runtime list: `{ 0x010B0F00, 0 }`
- Minimum F4SE: `0.7.9` (`0x00070090`)
- Address independence: `0`
- Structure independence: `NoStructs`

Address independence is intentionally zero even though the current lifecycle
code has no absolute addresses. This causes F4SE 0.7.9 to enforce the explicit
runtime list instead of interpreting the plugin as version-independent. The
plugin also checks the runtime and F4SE values again in `F4SEPlugin_Load` and
fails closed.

## PipboyDataManager findings

DCC CommonLibF4 exposes `RE::PipboyDataManager::GetSingleton()` through an
Address Library `VariantID`. For the 1.11.137+ (AE) family, the declared ID is
`4796372`. The manager declaration is size `0xD48` with this layout:

| Offset | Member |
|---:|---|
| `0x008` | `PipboyObject rootObject` |
| `0x0A0` | `PipboyStatsData` |
| `0x1D8` | `PipboySpecialData` |
| `0x280` | `PipboyPerksData` |
| `0x358` | `PipboyInventoryData` |
| `0x4F0` | `PipboyQuestData` |
| `0x5D8` | `PipboyWorkshopData` |
| `0x718` | `PipboyLogData` |
| `0x7F0` | `PipboyMapData` |
| `0xA28` | `PipboyRadioData` |
| `0xAD8` | `PipboyPlayerInfoData` |
| `0xBE0` | `PipboyStatusData` |
| `0xCC0` | `PipboyDataGroup* dataGroups[11]` |
| `0xD18` | `PipboyThrottleManager` |

Each base `PipboyDataGroup` is declared size `0x98`:

- `BSTEventSource<PipboyValueChangedEvent>` at `0x00`, size `0x58`;
- Pip-Boy data critical section at `0x60`;
- throttle manager at `0x88`;
- group `rootValue` at `0x90`.

Useful group roots declared by CommonLibF4 include inventory `inventoryObject`
at group offset `0xE8`, quest `questArray` at `0xB0`, map `mapObject` at `0xE8`,
radio `radioStations` at `0xA8`, special `specialsArray` at `0xA0`, stats
`playerStatsObject`/`activeEffectsArray` at `0xC8`/`0xD0`, and player info
`playerInfoObject` at `0x100` (the class size is `0x108`).

The value hierarchy declarations are:

- `PipboyValue`, size `0x18`: vtable, ID at `0x08`, dirty-to-game and
  dirty-to-companion flags at `0x0C`/`0x0D`, parent at `0x10`;
- `PipboyObject`, size `0x98`: member hash map at `0x18`, added IDs at `0x48`,
  removed IDs at `0x78`, newly-created flag at `0x90`;
- `PipboyArray`, size `0x80`: element array at `0x18`, added IDs at `0x30`,
  removed IDs at `0x60`, newly-created flag at `0x78`;
- serialized types exactly match the companion protocol values 0 through 8:
  bool, int8, uint8, int32, uint32, float, string, array, object.

Fallout4.exe 1.11.240 also instantiates primary
`PipboyPrimitiveThrottledValue` vtables for float, unsigned char, and bool at
RVAs `0x02545730`, `0x02545778`, and `0x025457C0`. They serialize as the same
primitive protocol types and keep their payload at `+0x18`; the adjacent
secondary vtables implement `IPipboyThrottledValue` and are not tree roots.

This explains why the built-in companion stream is the safer primary state
path: it is Fallout's own serialized form of this tree, includes incremental
dirty propagation, and avoids duplicating object locking/lifetime logic.

## PipboyValueChangedEvent strategy

`PipboyDataGroup` owns a `BSTEventSource<PipboyValueChangedEvent>`. A custom
sink can register with each of the eleven group sources and use the source
pointer to mark only that domain dirty. The callback must not traverse the
tree. It should only increment an atomic serial/enqueue a small domain token;
the game-thread snapshot task performs bounded copies later.

The event payload itself is only forward-declared in the inspected CommonLibF4
headers. Its size and fields therefore remain unknown. A sink may legally take
it by const reference and ignore it, but any attempt to read payload fields is
rejected until disassembly/runtime evidence supplies a definition.

## Required runtime gates before enabling `FO4DS_CAP_PIPBOY_TREE`

1. Install unmodified official F4SE 0.7.9 and the separate 1.11.240 Address
   Library, then prove the plugin loads under the device's active executable.
2. Resolve Address Library ID `4796372`; prove the target lies in a writable
   Fallout module data section and yields a canonical pointer or null.
3. After `GameDataReady(true)`, validate the manager pointer, manager vtable
   family, eleven `dataGroups` pointers, group vtables, and parent/root
   invariants without dereferencing arbitrary memory.
4. Validate every manager/group/value size and named offset against
   `Fallout4.exe` 1.11.240 disassembly and live instances. CommonLibF4's latest
   header currently names 1.11.221 as its latest constant, so shared AE layout
   is an assumption, not proof.
5. Resolve and canary the group lock/unlock IDs (`2225147`/`2225148` in the AE
   ID family) or prove that direct critical-section locking is the correct
   contract. Never copy a group unlocked.
6. Register one no-op change sink, exercise HP/inventory changes, and prove
   delivery and unregister behavior across load/new-game/death. Then expand to
   eleven sinks.
7. Copy each dirty group on the game thread into owned POD/string storage,
   enforce depth/node/text limits, and publish only immutable snapshots.
8. Prove root/value generation invalidation on PreLoadGame, NewGame,
   GameDataReady(false), and failed PostLoadGame.

Until all gates pass, manager/root addresses stay zero and the capability bit
stays off. The companion service remains authoritative for live Pip-Boy data.

## Build and verification

Run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-f4se.ps1
```

The build checks x64 PE format and all three exports, then runs an x64 host
test which validates version metadata, the C ABI, snapshot bounds checks, and
that unproven capabilities are disabled. Runtime lifecycle behavior still
requires an actual F4SE-launched game and is not claimed by this host test.
