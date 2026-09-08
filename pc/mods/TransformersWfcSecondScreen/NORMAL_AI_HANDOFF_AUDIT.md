# Normal AI handoff audit

## Scope and result (2026-09-07)

The previous player must resume real squad AI on the **same physical pawn**.
The current bounded proxy is not equivalent. This audit does not enable physical
`TnPlayerPawn` admission to `NativeRebindAiSensors`, nor claim gameplay validation.

Retained executable: `.research/transformers-wfc/device-original/Binaries/TWFC.exe`
(SHA256 `1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016`).
Cooked evidence: targeted exports from `decompressed/TransGame.xxx`, inspected
with the existing WfcScriptDump tool. Broad class dumps can throw UELib parsing
exceptions; a missing function match is not proof the behavior does not exist.

## Verified sensor lifecycle and safety correction

`TnTacticalAnalyzer.SetSensorSet`, native `10B61A70..10B61B0D`, stores the
sensor-set reference, then iterates `Sensors`. Each sensor receives a virtual
`Initialize` call through its compiled script table at `+40`, with compiled
controller and tactical-database pointers. The receiver is ECX, parameters are
stack arguments, and the callee removes eight bytes.

`TnAiSensor.Initialize`, native `10BC54D0..10BC55A4`, publishes controller,
physically casts `controller.Pawn` to `TnAiPawn`, publishes the cast pawn and
database, then invokes subclass `OnInitialize` through table `+44`. The raw
cached-reference offsets are `28`, `2C`, and `30`. These are validated against
live property metadata by the existing adapter, not assumed from a generic UE3
layout.

The old adapter called the complete native SetSensorSet loop and checked its
graph only after **all** subclass callbacks had returned. A callback could change
the pawn, sensor set, next sensor, or input/plan admission, yet the next callback
would still execute using stale handoff state.

The adapter now retains the already-rooted identical set and performs the same
native virtual Initialize dispatch **one sensor at a time**. After each callback
it checks controller/pawn/lease identities, plan ownership, sensor identities,
dispatch-table/initializer/callback pointers, and the initialized owner prefix.
An invalidated graph returns `kStateMismatch` without entering another callback.
This is not rollback: the caller must retain its existing dirty-handoff recovery.
No pawn spawn, clone, global IsA change, or template initialization was added.

Focused fixture coverage includes callback changes to pawn identity, input
lease, sensor identity, sensor owner, plan admission, next array entry, next
dispatch table, callback dispatch, and controller.Pawn. The first callback must
stop the loop in all nine cases. The executable base lifecycle bytes and PE
relocations remain checked. Engine metadata/subclass behavior are fixtures.

## Why rebinding the three base fields is insufficient for player-pawn AI

- `TnAiSensor._OwnerPawn` is authored as **TnAiPawn**, not TnPawn (export 6173).
  The native Initialize cast turns a physical player pawn into None. Replacing
  only controller.Pawn does not change its physical class or supply its AI tail.
- `TnAiBiggestThreatSensor.Initialize` (export 6194) overrides Initialize,
  invokes the base, then resolves `TnActionTargetClaimService` from `_OwnerPawn`.
  Calling only the base omits that service binding. Blindly retaining the old
  interface binds the previous pawn instead. The service's embedded interface
  representation must also be respected.
- `TnAiBiggestThreatSensor.OnUpdate` (export 6201) ranks current enemy pawns,
  rejects untargetable TnPawn enemies, weights player-controlled enemies by
  `PlayerDistanceScale`, and updates the controller's DesiredEnemy. It reads
  pawn location but also calls further targetability/controller consumers.
- `TnAiRangeSensor.OnUpdate` (export 10306) resolves AtActor and TargetIsDead
  world-state subjects, calls `RangeDetector.GetRange`, writes the result back,
  and updates tactical enemy range. A base-owner adapter alone does not audit
  that detector or world-state path.
- `TnAiLineOfSightSensor.OnUpdate` (export 10162) delegates to two LOS consumers;
  `TnAiMoveToRangeSensor.OnUpdate` (export 10199) delegates to UpdateWorldState.
  A no-op initializer does not imply their update consumers are player-safe.
- Vehicle strategy creation still has the separately traced physical AI cast
  followed by a raw `+570` access in native `10BB4D00`; strategy/path follower
  initialization has additional casts. Sensor initialization is not vehicle AI.

## Integration boundary

The callback-boundary correction is inside the existing `NativeAiSensors.cpp`
entry point and its existing test target; no new GameBridge hook is needed.
It remains physical-AI-only. Do not remove the class check or treat this change
as permission to replace the bounded proxy with ordinary planner execution.
Enabling physical-player AI still requires compatible concrete sensor/service,
strategy/path-follower, and vehicle consumers, followed by real squad gameplay
validation. None of this has been deployed or tested on a save by this audit.
