# Shipping WFC native-script incompatibility (2026-09-04)

## Status

The shared lookup failure now has an executable-backed explanation. The mod
assumed cooked UnrealScript functions/bytecode remain available and that hooks
on UFunction executors intercept normal gameplay. That is not true for this
shipping executable. **Character swapping and the complete custom class flow
are still incomplete. Do not publish these changes as a working swap feature.**

Source/native-DLL work only. No agents, APK, payload publication, device writes,
game launch or save access. Other mods and the reverted UI were left alone.

## Evidence

Exact executable: retained original TWFC.exe, SHA256
`1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016`.
Addresses below are preferred-image VAs (base `10900000`), not ASLR addresses.

The retained 19:19:35 and 19:19:39 logs show squad taps accepted, followed by
a 50-contract audit with 49 unresolved contracts and journal rejection **before
possession**. Shooter.Deactivate was exported and did not fail that audit.
The same session has no `StartLevel` UFunction in the lobby's runtime map.
These are genuine engine null results, not validation discarding a valid match.

| Native code | What it proves |
| --- | --- |
| `11725D90` function binding/postload body | Resolves a native-script wrapper through `11702D90`, installs it at UFunction+68 and sets flag `0x20`. |
| `1172604F..11726247` | When no wrapper is exported, clears the function's metadata, unlinks it from owner.Children and removes it from the class/state name map. The map is not a complete method inventory. |
| `1172650C..1172652F` | Frees Script data and zeros pointer/count at +38/+3C. +40 is a separate property-array pointer, **not Script capacity**. |
| `116E6A59..116E6A86` | For flag `0x20`, executor ECX is **UObject+10**, not UObject. Null executor results in no call. |
| `10A9BC50`, especially `10A9BC8E..95` | Compiled `TnGameLobbyGame.FinalCountdown.Tick`: sets EAX=this and directly calls `10A9B300`. No FindFunction/UFunction executor dispatch. |
| `10A9B300..10A9B43A` | Matches cooked `TnGameLobbyGame.StartLevel` export 20991: records Start/statistics, obtains settings, builds/modifies MapURL, and travels. ABI: script-this in EAX, no stack parameters. |
| `10BC8AA6..10BC8ABE`, `10BC8C47..10BC8CC5` | Shooter activation/reset copies/clears **four** delegate words. Reset preserves and calls the cached native context/function pair. The runtime delegate is 16 bytes, not 8. |
| `10BC8860..10BC897E` | Shooter state changes select native C++ vtables (e.g. Stopped `128B0FB0`, Firing `128B1020`). A base-class UFunction lookup cannot reproduce state virtual dispatch. |
| `10BC8A20`, `10BC8AA0`, `10BC8C30`, `10BC9CD0` | Shooter getter: EAX receiver and result use script bases; Activate: delegate in EAX, compiled weapon class in EDX, script receiver on stack, RET 4; Reset: EAX receiver; exported Deactivate: ECX receiver, ignores its two stack parameters, RET 8. |
| `10BB95F0..10BB9649` | Native controller SuspendPlanRunner/ResumePlanRunner preserve an authority guard, obtain PlanRunner at UObject+304 and tail-dispatch its current script vtable slots +54/+58. |
| `1137D590..1137D613`, `113790A0`, `10FDD5B0`, `11378E20` | Five GOAP runner vtables. Base Suspend requests Stopping; base Resume only clears SuspendPending. Suspended.Resume transitions to Waiting. Calling base Resume while Suspended would not restart AI. |
| `10C11610..10C11619`, `1139B3D0`, `1139BA00` | Compiled ClearSolution dispatches EnqueueSolution(None), current vtable slot +60, ECX script-this and compiled None=10. Base/Running enqueue updates PendingSolution then enters Stopping/Waiting; Waiting additionally pumps the queue. Slot +5C is TryBetterPlan, not EnqueueSolution. |
| `10FDD5D0`, `10FDD5F0`, `113736D0` | Stopping/Suspended.EnqueueSolution are RET 4 no-ops. Suspended.Begin only changes suspension flags: the halted CurrentSolution is retained. The old post-suspension ClearSolution + null-current assertion was incompatible even with working lookup. |
| `11379010..1137909A` | Waiting.PumpQueue uses EDX script-this. With PendingSolution=None it clears Current/PendingSolution, resets the goal cursor and returns without state transition, observers, or starting an action. The non-null branch starts Running and must never be taken by stopped-plan cleanup. |
| `113791F0..11379245`, `11386BA0..11386C00` | Private immediate goal/action setters: ESI script-this, one compiled object stack argument, RET 4. Preserve native Unbind/Bind, GC barrier and goal cursor reset. Public ReplaceGoalSet/ReplaceActionSet at 11379160/11379110 only queue replacement and abort; they are not interchangeable. |
| `10BBA850..10BBA892`, `10BC4280..10BC42D7` | Controller.RequestReplan: ECX script-this, priority on stack, RET 4. Sets runner +58 bit 1 then calls the static scheduler entry, which resolves Engine.GetGame and Game._AiPlanningScheduler. The global world need not be a stale controller's WorldInfo: verify equality before calling. |
| `10BC43F0..10BC4454`, `10BEE7B0..10BEE7EE`, `10BC4790..10BC480C` | SubmitRequest uses ESI script-this / RET 8. Inactive scheduler or suspended runner returns without accepting work; high requests deduplicate, and a new high request promotes/removes an existing low entry. Native queue arrays are scheduler UObject +28/+34, activation bit +50. Find/Add/Remove remain engine-owned. |
| `10A27C90`, `10BB45C0..10BB4800`, `10BBA8A0`, `10BBA9E0` | Eight controller state tables; CancelMove virtual slot +29C. Default is RET at 109D4530, but MovingToPosition/MovingToActor call PopState with ESI=UObject and false on stack. Calling the base no-op never cancels these moves. |
| `11700480..1170059B`, `10BBA920..10BBA981`, `10BBAAD0..10BBAB31` | PopState dispatches current script PoppedState at +44 before restoring the previous state. Movement PoppedState clears PathFocus and both complete 16-byte delegates. Native state frame is UObject +28; current state +38, stacked entries array +48 (12-byte entries). |

The decompiled packages remain useful for intent and contracts, but matching
their bytecode offsets is not evidence that a bytecode patch executes in-game.
Restoring names or retaining bytecode would not redirect existing compiled
direct calls or undo compiled TnPlayerPawn/TnAiPawn type checks.

## Implemented corrections

- `NativeCampaignTravel` targets the real compiled StartLevel body, with exact
  body/callsite signatures and EAX/script-this handling. It is installed only on
  the outer game tick. Campaign admission/resume uses the existing selection
  state machine; it no longer waits for a nonexistent StartLevel UFunction.
  Until **all remaining menu/loadout hooks** are ready it forwards stock travel,
  rather than trapping the player in an incomplete selection flow.
- Remaining exported compiled-script executor detours normalize incoming
  UObject+10 to UObject for existing callbacks and reverse that adjustment when
  forwarding the original. RX tail thunks are cached; no per-call allocation.
  Slot restoration uses the actual engine pointer, not the normalized callback.
- A Defined function with a null executor cannot be reported as an executed
  success. Undefined empty declarations retain their native no-op behavior.
- Bytecode access uses the correct eight-byte Script header and rejects
  compiled-script bytecode patches, even if cooked bytes happen to be retained.
- Deferred checkpoint callbacks preserve the complete 16-byte native delegate,
  validate its compiled target and marshal the complete int/delegate signature.
  Normal unmapped checkpoint saves retain their existing pass-through path.
- `NativeWeaponShooter` replaces all seven shooter call contracts and every
  shooter call in swap capture, handoff, verification, rollback, role-proxy
  combat and role-proxy shutdown. Exact signatures and forty entries across
  ten state tables gate the binding. Every call re-reads the current table;
  unknown/modified tables fail closed. Reset and Deactivate call native bodies,
  preserving callback cleanup and state transitions. Activate is deliberately
  restricted to the owner's actual equipped weapon, not the inventory-search
  branch. Null idle weapons, 16-byte delegates and compiled +10 null/object
  conversions are handled explicitly. StartFire rejects a null active weapon.
- `NativeAiPlanning` replaces controller SuspendPlanRunner/ResumePlanRunner
  call contracts and execution in the transaction and rollback. It preserves
  the complete compiled authority guard and dynamic GOAP state dispatch, checks
  runner ownership and standalone world, and verifies pending/suspended state.
  Suspension remains asynchronous: the existing transaction must still wait
  for native quiescence. Rejected calls and failed postconditions are distinct;
  a call that executed retains rollback ownership even when its result is bad.
- GOAP handoff now cancels the queued solution **before** requesting native
  suspension. Stopping/unknown states, unrelated pending replacements and stale
  plan objects are rejected rather than treating ignored calls as successful.
  After the existing later-tick suspension barrier, it drains the halted plan
  using the verified None-only branch of native PumpQueue, without Resume or
  transient AI execution. Both native and role-proxy handoffs drain before any
  controller/shooter ownership moves. Rollback requests a fresh plan after
  restoring/resuming the original AI, including failures before detachment.
- A second asynchronous barrier now protects **rollback after guard release**.
  Reversing ReleaseTransitionGuards cancels newly queued work and requests
  suspension, then the transaction waits before reversing any committed graph
  mutation. WFC validates the still-intact new reciprocal graph, world/object
  identities, held input guards and actual suspended runner, then drains its
  stopped cache. A rejected gate, five-second default stop timeout, or rollback
  clock regression leaves remaining ownership changes unapplied and commands
  closed. It no longer continues detaching pawns while suspension is pending.
  Ordinary pre-release rollback keeps its existing reverse-order path.
- The journal's three stripped ClearSolution/action/goal-setter dependencies
  now use compiled bindings. Setters require a suspended, drained runner, no
  pending set replacement, valid native-layout arrays and a reciprocal physical
  TnAiPawn/controller pair; they cannot bind TnAiPawn-only actions to the sibling
  TnPlayerPawn allocation. The narrow ESI/RET4 wrapper preserves the host ESI.
  Four complete method bodies (including ASLR-adjusted operands) and the five
  current-state enqueue slots are validated. Callback re-entry and failed
  postconditions keep the same fail-closed/rollback semantics.
- Native RequestReplan now replaces the stripped reflected call in both final
  guard release and rollback recovery. Resume must succeed first. It checks the
  physical AI ownership pair, current runner state, world/global-world identity,
  scheduler activation and reflected/native queue layout before calling the
  compiled controller-to-scheduler chain. Success requires exactly one accepted
  queue entry at the right priority and the evaluate-current-goal flag. Unknown
  priorities, stopping/suspended runners, stale worlds and corrupt/duplicate
  queues cannot mutate state. Failed postconditions retain rollback ownership.
  The mod does **not** call TryBetterPlan synchronously; the engine scheduler
  retains its per-frame planning time budget. The bounded queue verification is
  only part of the control-transfer transaction, not a new polling loop.
- `NativeAiMovement` replaces CancelMove throughout handoff and role-proxy
  cancellation, and replaces the stripped MoveDelegatesCleared query. Eight
  current-state tables (sixteen entries), four full compiled method bodies,
  base RET and the PopState entry signature gate binding. Moving-state calls
  require a readable, valid native state stack. PopState owns cleanup: no
  delegate clearing, state-table swapping or PathFocus writes are performed by
  the mod. Postconditions require leaving movement and clearing all eight
  delegate words, including the otherwise easily missed cached compiled
  callbacks. A missing/failed pop or renewed callback fails closed. The separate
  StopMovement port is documented below; neither makes proxy AI equivalent
  to a physical TnAiPawn.
- These services only execute on the established game thread and reject
  callback re-entry. Binding is read-only and performed during preflight, not
  a new per-frame scan, hook, allocation or polling loop. No state/actor copies,
  class-ID substitutions or base-state fallbacks were introduced.

### Player services and reference lifetime correction

`NativePlayerServices` now replaces three further stripped calls in both the
forward service-rebind path and its rollback path:

- `TnFineAimManager.Initialize`: complete native body `10A93E00..10A93E72`.
  EAX is compiled Pawn, EDX is compiled manager; targeting and camera are the
  two stack arguments, RET 8. The parameter order is not declaration order.
- `TnPickupManager.Initialize`: complete native body `10B11C30..10B11C76`.
  EAX is compiled owner Pawn, EDX is compiled manager; contextual command
  handler is the stack argument, RET 4. Its actual type is
  `TnContextualCommandHandler`, not a class named ContextualCommander.
- `HmPlayerTargeting.set_TargetingOwner`: cooked export 7939 contains exactly
  `_TargetingOwner = Value`. This is implemented using the shared expected-value,
  GC-aware reference writer. The compiled targeting update reads this inherited
  UObject+48 field at `10BA79B0`, `10BA7A72` and `10BA7AA3`; no guessed native
  setter address or stripped executor is called.

Native call sites `10B21F55` and `10B21F79` independently confirm both initializer
ABIs. The full bodies and all absolute GC operands are signature checked at the
actual image base. Compiled object parameters use UObject+10, including the
optional None commander (compiled sentinel 10). Admission verifies native field
offsets, reciprocal local player/pawn ownership, same standalone world, and the
controller's actual service objects. Rebinding does not change any pawn runtime
fields. Post-call failure remains distinguishable from rejection before mutation.

The existing `UnrealWriteObjectPropertyExpected` previously performed only an
atomic pointer write. Shipping compiled assignments also execute the collector
reference barrier at `116FC820..116FC851`. Missing that notification is a real
GC lifetime defect; it is **not evidence by itself that GC caused the previously
reported crash**. The shared writer now calls the signature-gated native barrier
after each successful assignment, including clearing to None, and rejects wrong
threads, unaligned/array fields and non-writable pages before mutation. If the
barrier cannot be bound, reference writes and swap admission fail closed while
normal read-only telemetry remains available. Initialization may run on the
loader worker; reference-write thread identity is established by the game pulse.

These initializers are intentionally NOT replacements for `OnSetPawn` or the
role adapter. The pickup initializer merely stores its typed pawn; subsequent
pickup/player consumers still require a compatible physical-pawn adaptation.
Passing these three contracts does not make cross-role possession safe.

At the player-services tracing stage, AI StopMovement and camera owner/form
initialization were still unbound. Their subsequent implementations are recorded
in the camera section and the movement-stop section below. The typed pawn getter
at `10BB49D0` remains unsafe for an arbitrary cross-role call; these ports do not
globally bypass it or alter an object's physical class.

### Input guards and the spawn/restart incompatibility

`NativePlayerInput` replaces both stripped IgnoreMoveInput/IgnoreLookInput calls
in admission, acquisition, release and rollback. Their complete native leaves
are `1177F960..1177F9A2` and `1177F8E0..1177F922`: ECX is compiled controller,
one 32-bit boolean stack parameter, RET 4. These are **byte counters**, not bool
fields (UObject+305/+306). Incrementing 255 wraps to zero in the original game.
The adapter rejects overflow, duplicate/copy acquisition, wrong thread, stale
controller/world identity, non-standalone/authority context, invalid property
layout and non-writable counters. Only its own increment may be released;
unrelated existing counter baselines are preserved. Executed calls with failed
postconditions retain rollback responsibility instead of reporting no mutation.

The transaction now retains two owned leases instead of trusting two booleans.
Both actual leases are checked at quiescence, before each forward mutation,
after player binding, and at the asynchronous rollback-graph barrier. Restored
graph verification also requires that both leases were successfully released.

Counter comparison alone is insufficient: a reset followed by another system's
increment can produce the same count. On the first admitted input acquisition,
a signature-gated observer is installed on the real ResetPlayerMovementInput
body `117934E0..11793534`, outside original Tick. It invalidates matching held
leases before forwarding the reset **unchanged**. The ESI entry shim preserves
all GP registers, flags and x87/MMX/SSE state; it never suppresses the reset,
injects focus/input, resets counters itself, logs per reset, or polls per frame.
DLL pinning retains the trampoline; teardown makes observation inert while
continuing the original reset. Preflight binding remains read-only.

This observer does NOT repair the live-adoption architecture. Further native
and cooked tracing establishes why simply porting full Possess is unsafe:

- TnPlayerController.Possess `10B22100..10B22188` calls engine possession, then
  typed OnSetPawn (`10B21ED0`), a pickup timer, spectate actor and damage history.
- Engine.PlayerController.Possess `117960C0..117961C1` does real ownership work,
  but also calls Restart, player-only touch events and player-spawn events. Its
  local-player/no-camera error path **destroys both pawn and controller**.
- PlayerController.Restart calls EnterStartState and ClientRestart. The latter
  unconditionally resets input guards, clears saved moves, acknowledges the
  pawn and calls Pawn.ClientRestart. The native input reset copies class
  defaults, irrespective of the swap journal's recorded locks.
- TnPlayerController.EnterStartState chooses `CD.StartingForm`, NOT the selected
  pawn's live form. Pawn.ClientRestart zeroes velocity/acceleration. These are
  spawn/restart behaviors, not state-preserving live control transfer.
- ClearInputs is state-dependent: walking ends block/melee/fine aim; vehicle
  input stops boost; tank/plane/car variants reset their respective input
  structs. Base ClearInputs (`10B19B50..10B19B79`, native slot +444 in the base
  controller table `127F9B90`) is not a valid replacement for these overrides.
  The base deferred-fire helper is `10B1A8B0`; vehicle ClearInputs at `10B28040`
  additionally dispatches StopBoost at +4D4. These are traced, not ported.

The later player-adoption batch below replaces full player Possess in the
guarded journal path; the AI Possess dependency and native cross-role services
remain unfinished. Merely resolving their executors or re-applying input locks
after Restart would not preserve mission events, form, movement or all pawn
services. No preflight/cast gate was bypassed and no gameplay completion is claimed.

### Native controller detachment and rollback correction

`NativeControllerDetach` replaces both stripped UnPossess journal dependencies
in forward detachment, bind reversal and original-graph restoration. The
non-short-circuit call audit now includes these two formerly late dependencies
(52 checks on the same inventory instead of the retained log's 50). No control
links are changed during binding/admission.

| Full compiled method | Verified responsibility |
| --- | --- |
| PlayerController.UnPossess `1179E670..1179E751` | ECX=compiled controller. Moves the controller to Pawn.Location, sets pawn RemoteRole, dispatches the current pawn's UnPossessed callback, clears saved moves, conditionally changes view target to self, clears Controller.Pawn with the GC barrier. |
| TnAiController.UnPossess `10BB5180..10BB5207` | Constructs the complete OnNewEnemy delegate (FName global `12B31C68`, compiled callback `10BB84E0`), removes it from TacticalAnalyzer, then calls base Controller.UnPossess. |
| Controller.UnPossess `117BA910..117BA95D` | Calls current Pawn.UnPossessed at script vtable +224, then clears Controller.Pawn with dirty/GC notification. |
| Pawn.UnPossessed `11794770..117947EA` | Clears PRI and owner through native SetOwner (`11506E50`, ESI=UObject, one stack argument), then clears Pawn.Controller with native dirty/GC notification. |
| TnPlayerPawn.UnPossessed `10BA2090..10BA20BC` | Calls ClearInteractState (`10BA1DE0..10BA1E4F`) before base pawn cleanup. |
| TnAiPawn.UnPossessed `10BC0B90..10BC0BF6` | Retains the AI initialization check before base pawn cleanup. It does NOT clear `_BoundController`; the later PossessedBy callback owns that reassignment. |
| CleanOutSavedMoves `11781530..1178156E` | EDX=compiled player controller; clears SavedMoves/PendingMove at UObject +348/+350. Does not reset the input counters. |
| Delegate Find `10B75A00..10B75A59`, Remove `10B82360..10B823C9` | Find compares UObject/FName and advances by 16 bytes. Remove retains native array compaction/shrink behavior. The cached compiled words are not compared by Find, so admission validates them separately. |

Eleven entire bodies (including shared GC barrier), ASLR operands and both
floating constants gate binding. The native pawn callbacks are selected by the
game's actual current script vtable, not by a base no-op. Admission verifies
their addresses against the physical pawn type; unknown overrides fail closed.
The TnPlayerPawn SetInteractState slot +344 must also match its audited dispatch.
Standalone reciprocal ownership, object/world identity, writable native fields,
idle interaction, a live same-world player camera, and a valid analyzer observer
array are required. DrivenVehicle occupancy is rejected (not the transformer's
own robot/vehicle form). No actor construction, appearance copy, transform reset,
spawn event, controller replacement, field-only detachment or polling was added.

The new typed `UnrealReadDelegateArrayProperty` validates ArrayProperty/inner
DelegateProperty metadata and the complete 16-byte entries. Generic array reads
only establish pointer-sized readability and are insufficient here. Detachment
captures at most 256 observers, rejects duplicate/incorrect native OnNewEnemy
bindings, and verifies exactly that observer was removed without changing any
other delegate word/order. A legitimately absent observer is left absent.

Rejected-before-call and executed-but-incoherent results remain distinct.
Rollback ownership is retained even when the native callback executed but its
postcondition failed. Original-graph restoration now stops immediately after
the first failed detach/rebind or bad reciprocal pair, rather than continuing
to mutate the second controller with a possibly broken first pair. It records
the failing restoration stage and keeps transition guards active.

Further tracing ruled out another unsafe shortcut: **base Controller.Possess**
at `117DEB70..117DED28` also virtually calls Restart (+21C at `117DED01`).
Using it instead of PlayerController.Possess does not avoid restart/reset.
Native Pawn.PossessedBy starts at `1179C600`; the AI override starts at
`10BC09B0` and updates `_BoundController` at UObject+87C.
Pawn possession owns SetOwner, PRI and rotation, but its player branch also
calls Weapon.ClientWeaponSet (`117A4110`). The follow-up trace and narrow
pawn-side binding below address that branch; they are NOT a completed
controller-adoption implementation.

This batch completed detachment bindings, not the role adapter or character
swap. The subsequent player-adoption work below replaces player Possess; the
AI bind/restore dependency still requires its own no-restart implementation.
Class/loadout native-menu and durable-state work is also still incomplete.

## Pawn-side adoption and equipped-weapon selection (2026-09-05)

`NativePawnOwnership` binds the current physical pawn's actual PossessedBy
virtual at script slot +220. It runs native Controller/Owner/PRI bookkeeping
without calling Controller.Possess, Restart, ClientRestart, spawning an actor,
replacing inventory, or temporarily hiding the equipped weapon. The AI override
updates `_BoundController` to the AI controller or None for a player controller.
The compiled ABI is ECX=pawn+10, two stack arguments (controller+10, false), RET 8.

| Complete native body | Verified effect / ABI |
| --- | --- |
| Pawn.PossessedBy `1179C600..1179C742` | Sets pawn Controller, network update fields, PRI, owner and EyeHeight; aligns controller rotation when not a vehicle transition. Player branch calls Weapon.ClientWeaponSet(false). |
| TnAiPawn.PossessedBy `10BC09B0..10BC0A41` | Calls base ownership setup then refreshes the typed `_BoundController` reference using native GC notification. |
| Weapon.ClientWeaponSet `117A4110..117A4189` | EDI=compiled weapon, stack bool, RET 4. Standalone path updates ClientWeaponSetTime and calls TryToSetWeapon. |
| Weapon.TryToSetWeapon `117A40A0..117A410C` | EAX=compiled weapon, stack bool, RET 4. Valid Instigator/InvManager dispatches manager slot +228; missing ownership can enter PendingClientWeaponSet. |
| TnInventoryManager.ClientWeaponSet `10ACC910..10ACCA37` | ECX=compiled manager, compiled weapon and bool on stack, RET 8. Advances selection timestamp; same currently equipped, non-Inactive ordinary weapon returns BEFORE SetCurrentWeapon/state transition. Bomb/flag weapons, absent/deleting/Inactive old weapons take different paths. |
| PRI assignment helper `10AF4840..10AF4875` | EAX=destination field, ECX=source field; writes the UObject reference and invokes the native collector barrier. |

The seven complete bodies, including shared GC notification, their ASLR
operands and the inventory virtual slot gate admission. Native SetOwner takes
ESI=pawn UObject and an unadjusted owner UObject on the stack; SetRotation takes
EAX=controller UObject and a pointer to pawn rotation on the stack. The defaults
helper at `116EF210` is not assumed to be a simple UClass.DefaultObject read:
its native path can choose an outer/default based on flags and requested size.

The equipped-weapon branch requires the actual native TnInventoryManager,
matching Instigator/InvManager references, PendingWeapon=None, finite native
timestamps and the same current world. Bomb/flag weapons and Inactive state or
Inactive ancestors are rejected. An absent state frame/current state is allowed:
native IsInState returns false, preserving the same-equipped early return.
Current state ancestry is checked with bounded cycle detection, not just a
display state name. No current weapon is also allowed. No polling/hook loop was
added; this work happens only in handoff preparation/transaction calls.

Binding requires both pawn and controller detached, same standalone world,
authoritative/live objects, native property offsets, writable native fields,
no driven vehicle occupancy and no active UseableInteract. Postconditions retain
the same objects, inventory, equipped weapon, state/frame and pawn pose/motion;
Controller/Owner/PRI and AI `_BoundController` must agree. Selection timestamps,
native network fields, EyeHeight and controller rotation may change as required
by the native callback. Reentry remains blocked through post-call validation.
An executed-but-incoherent callback is reported separately from rejection.

**Scope:** this is the pawn half only. It deliberately does NOT publish
Controller.Pawn, acknowledge possession, adapt physical player/AI roles, restore
camera/input services or complete controller adoption. It originally added two
read-only audit checks (54 for the same inventory as the retained 50-check log).
The player/AI adoption layers below now call it inside guarded bind and rollback.
Compatible cross-role services remain required before enabling the full transfer.

## Custom reciprocal player adoption and acknowledgment (2026-09-05)

`NativePlayerAdoption` replaces the late stripped player Possess lookup and both
forward/rollback full-player-Possess calls. It performs the pawn ownership callback,
replicated Controller.Pawn publication, and native local possession acknowledgment
without Restart, ClientRestart, movement-input reset, spawn/touch events, new actors,
weapon replacement, or copying another character's state. This is the ownership
part of custom player adoption, NOT a native squad-switch feature and NOT a
completed player/AI service/camera handoff.

| Native evidence | Responsibility / supported call |
| --- | --- |
| Engine.ClientRestart `11797B80..11797D3A` | Calls ResetPlayerMovementInput at `11797BDA`, then saved-move cleanup, Pawn publication, AcknowledgePossession at `11797C6F`, and zeroes pawn velocity/acceleration before the eye-height virtual. Confirms why this entire function must not be called for a live handoff. |
| PlayerController.AcknowledgePossession `11789030..117890BA` | EDI=compiled controller, compiled pawn on stack, RET4. Casts Player at UObject+2D0 to LocalPlayer; assigns AcknowledgedPawn at +300 with GC notification; invokes pawn eye-height virtual +204; copies BaseEyeHeight to EyeHeight; calls ServerAcknowledgePossession. |
| ServerAcknowledgePossession `11788FA0..11789027` | EAX=compiled controller, compiled pawn on stack, RET4. Standalone branch updates AcknowledgedPawn. The preceding local acknowledgment already made it equal to Pawn, so the conditional ResetTimeMargin branch does not execute. |
| Pawn.SetBaseEyeheight `11783230..11783296` | Current script virtual +204 on both physical pawn types. Crouched path uses Min(0.8*CrouchHeight, CrouchHeight-10); otherwise dispatches current +200 GetOriginalBaseEyeheight. |
| TnPawn.GetOriginalBaseEyeheight `10B09240..10B0926A` | Blueprint reference UObject+578; reads TnTransformer.BaseEyeHeight at +B4. Missing-blueprint default fallback is not admitted for custom live adoption. |
| Min wrapper `1097F750..1097F76A`, math body `1094C0B0..1094C0DF` | Native x87/SSE minimum implementation, retained exactly. The multiplier constant is double(float(0.8)), bits `3FE99999A0000000`, not double(0.8). |
| PlayerController.Possess `1179617A..1179618C` | Source for custom Controller.Pawn publication: dirty bit at UObject+30, actual UObject pointer at +1C8, then GC notification. No +10 pointer is stored in the field. |

The new EDI/RET4 wrapper is in ThisCall.S so the compiler sees an ordinary C
call boundary and the host EDI is preserved. Seven entire acknowledgment,
eye-height/math/GC bodies, all absolute operands, both floating constants and
current pawn eye-height virtual targets gate preparation. A typed scalar float
offset resolver verifies native Blueprint/BaseEyeHeight/EyeHeight/CrouchHeight
fields; a generic property read is not treated as an offset proof.

Adoption requires the exact owned move/look input leases to remain held, live
same-world standalone authority, LocalPlayer, a non-spectator PRI, a live camera,
fully configured blueprint and detached controller/pawn. Stasis, tear-off,
pending weapon changes, unsupported callbacks, wrong thread and reentry reject
before mutation. Native ownership callbacks are followed by another identity,
pose and write-permission check BEFORE reciprocal publication. Final validation
checks Pawn/AcknowledgedPawn/Controller/Owner/PRI, stable camera/player/world/
blueprint identities, retained pawn pose and input leases, and native eye height.
Rejected requests remain distinct from calls which changed state and failed a
postcondition; the existing journal retains rollback/closed-command responsibility.

GameBridge initially audited this formerly late dependency (55 checks on the same
retained inventory), logs native player-adoption results for forward and rollback
calls, and no longer keeps or invokes `player_game_possess`. It still rejects on
the unresolved role/service contracts. The next batch below also replaces full
TnAiController.Possess, whose base Controller.Possess calls Restart. The existing native
pickup timer is not stopped by player UnPossess; this work does not rerun spawn
initialization or clear controller killstreak/god-mode/damage-history state.
Per-character controller-service semantics still need completion and gameplay
verification alongside camera/form/HUD/mission integration.

## Custom reciprocal AI adoption and owner services (2026-09-05)

The current TnAiController script table `127F96D8`, Possess slot +208, targets
`10BB4F90..10BB5174`. Its call at `10BB4FC5` enters base Controller.Possess
(`117DEB70`), including the unwanted Restart. Then three separate compiled
TnAiPawn casts feed icon, aimer and shooter ownership. A physical former
TnPlayerPawn fails these casts and supplies None, even though the leaf service
initializers accept shared Pawn/TnPawn storage. Hooking a stripped getter's
executor cannot change these inlined casts.

The icon setter is inlined at `10BB4FFF..10BB5010`: controller `_IconDrawer`
at UObject+3C4, drawer `_OwnerPawn` at +28, native GC barrier. The four-word
OnNewEnemy observer is built at `10BB5011..10BB5048`, Find `10B75A00`, insertion
`10B7AF50` only when absent. WeaponAimer initialization calls `10BC8390` at
`10BB5103` with EAX=compiled pawn, EDI=compiled aimer (controller field +2FC).
Shooter initialization at `10BB513D..10BB514E` writes actual pawn UObject to
Owner+2C with native GC notification (controller WeaponShooter+2F8).
Cooked HmPawnIconDrawer.Initialize and TnAiWeaponShooter.Initialize are plain
Pawn-reference setters; TnAiWeaponAimer.Initialize takes TnPawn and additionally
derives its TnAiController owner. The complete aimer body is `10BC8390..10BC8416`:
EDI=compiled aimer, EAX=compiled pawn, ordinary RET. Its two references are actual
UObject pointers, with the shared GC barrier and an inline GC notification for
the controller. `WfcCallEdiEax` preserves host EDI at the ordinary C call boundary.

**Correction to the preceding trace:** Range initialization at
`10BB514F..10BB516B` checks a TArray count of eight, not a type tag. Controller
RangeSet is the array header at UObject+360; detector Range is at +28, with eight
8-byte FloatRange elements. Copy `10BEE830..10BEE88C` resizes/copies that array.
For an existing same-world controller, the custom handoff validates both typed
arrays and identical finite contents, then preserves the existing detector/data.
It does not reallocate configuration that is already correct; mismatches reject.

`NativeAiAdoption` now replaces the late stripped AI Possess lookup and both
forward/rollback calls. It performs the native pawn ownership callback, replicated
Controller.Pawn publication, GC-aware icon/shooter owner assignment, verified
native aimer initialization, and native AddUnique-equivalent enemy-observer
registration. It preserves controller-owned analyzer, plan runner, range detector
and debug-manager identities. Preparation adds one structural audit (56 checks
for the same inventory as the retained log); it does not pretend that an active
plan must already be suspended during the initial read-only preflight.

OnNewEnemy uses all four delegate words: controller UObject, packed FName from
`12B31C68`, compiled context controller+10, and native callback `10BB84E0`.
Find `10B75A00` takes array in EDX and delegate on stack (RET4). The admitted
append-at-Length branch of `10B7AF50` calls `10B7F870..10B7F8C6` with array in
EAX and delegate on stack (RET4). Capacity growth remains engine-owned through
`10B83FA0..10B8406D` (EAX new capacity, EDI header, old count on stack, RET4).
Complete Find/Append/Resize/aimer/GC bodies and relocations are signature-gated.
Duplicate own observers, partial/mismatched compiled bindings, excessive arrays,
unrelated service owners and stale actor/world/service identities reject.
Other observers retain their order and every word; an existing correct own
observer is not duplicated. No range data or another character's state is copied.

Execution requires the exact held move/look leases, standalone authority, a live
same-world camera with stable object identity, a genuinely drained Suspended GOAP runner, and an exactly
Stopped weapon shooter with no active weapon or remaining notification words.
`NativeAiPlanHandoffReady` checks actual native state plus all pending/current
solutions and replacement sets. `NativeWeaponShooterHandoffReady` checks the
current Stopped table, null weapon and both complete cleared notifications;
`!IsFiring` alone is insufficient. Both reject nested lifecycle mutations.
The other actor must already be reciprocally owned and acknowledged by the
player controller. Lost player ownership/acknowledgment or camera replacement
during a native callback fails the handoff; the AI layer cannot silently proceed
with an unowned player actor or accept a changed camera as equivalent.
Native callbacks are followed by identity/ownership/range/observer revalidation.
Post-mutation failures retain rollback responsibility and log their failing stage.

The service audit also exposed rollback ordering: deferred service restoration
used to reset the newly activated shooter AFTER rebuilding the original graph.
Rollback now drains/stops its weapon callbacks BEFORE detaching the cross-role
AI pair. It revalidates held leases and the drained planner after reset. A failed
callback/ownership cleanup marks graph restoration failed, stops subsequent
ownership changes and retains transition guards. Deferred cleanup remains only
a defensive path; it is no longer the primary callback-disconnection point.

This completes the narrow ownership/owner-service layer, **not** actual squad
gameplay. The former player allocation is still not compatible with native
TnAiPawn-only GOAP, navigation or vehicle fields. Player camera/form/HUD and
compiled cross-role consumers remain unported; those admission checks stay closed.

## Packed class-menu flag write safety (2026-09-05)

The custom class/loadout flow temporarily changes GRI `_bIconicMode` to open
the native multiplayer editor, then restores it. Its shared Boolean CAS writer
previously checked readability only; even its initial compare/exchange is a
write instruction and could fault on a read-only field. It also lacked the
game-thread and scalar layout checks already used by ownership writes.

The writer now rejects other threads before reflection-cache access, requires
an aligned scalar DWORD with one mask bit outside the UObject header, queries
current write permission before either CAS, and preserves every unrelated flag.
Boolean values require no UObject GC barrier. This is a source-level safety
defect fixed and regression-tested, NOT a demonstrated cause of retained gameplay
crashes or a fix for the still-missing native menu hook integration.

## Camera owner/form handoff (2026-09-05)

The old source called `set_CameraOwner` while rebinding ordinary services, then
called it again in the later journaled camera step. Apart from being a stripped
lookup, that first call changed the camera before its rollback flag was armed.
The native setter `10BDABE0..10BDAC0E` assigns the pawn, initializes form caches,
and immediately reactivates the PREVIOUS `_CameraStrategy` on the new owner.
That is not a safe ordering for an independently selected character/form. These
are source incompatibilities, **not a newly observed gameplay crash trace**.

`NativeCameraHandoff` now performs one guarded camera step: GC-aware owner
assignment, shipping `InitializeForms`, then shipping `StartForm`. No camera
template is cloned and no pawn, inventory, position or form is copied/reset.
StartForm goes through None before entering the selected robot/car/tank/plane
state, so its BeginState runs even when the old and new form states match.
Ordinary admitted camera states have no cooked EndState override. The native
state callbacks select the correct strategy; the old strategy's deactivation
continues to belong to HmCamera.SetActiveCameraStrategy, not a pointer shortcut.
Boosting car/plane states subsequently follow their normal OnUpdate transitions.

Verified primitives and storage:

- InitializeForms `10BDAC90..10BDAD5C`, jump table `10BDAD60`: EAX is compiled
  manager, plain RET. Clears all three caches before choosing a vehicle type.
- StartForm `10BDAFB0..10BDB075`, jump table `10BDB078`: ECX is compiled manager,
  one form byte in a DWORD stack slot, RET4. It calls native GotoState
  `116FCAF0..116FCB8D` using ESI=UObject, EDI=false, and three stack words.
- Manager UObject fields: strategy set +2C, camera +30, pawn +34, tank/car/plane
  +38/+3C/+40, strategy class +44. Camera Actor.Owner is +2C, NOT +9C;
  ActiveCameraStrategy is +3AC. The older ownership test fixture's +9C Owner
  model was corrected; production ownership already used reflected Owner.
- Vehicle type comes from the current script virtual +CC on the live vehicle
  form, not character identity. Only complete pure constant-return bodies 0..3
  are admitted, with corresponding real car/truck, plane or tank type checks.
- GetStrategy `113770A0..113770DF` searches 24-byte entries at strategy set +60,
  returning the first matching Type's existing Strategy. Admission validates
  reflected and native row offsets and rejects missing/duplicate strategies.
- Camera state selector `10BDA8A0` identifies the normal robot/car/tank/plane
  tables; death, turret, scripted camera and unknown tables are not admitted.
  None is accepted only with the same journal pair's camera ownership, allowing
  a partially applied transition to be restored while input remains held.

Both character directions and available form strategies are preflighted without
camera calls. Execution requires acknowledged reciprocal player ownership,
standalone authority/current world, live object indices, actual current form,
no ongoing transformation, writable manager storage and both owned input leases.
Callbacks are followed by world/identity/ownership/guard revalidation. Missing
postconditions report dirty failure for guarded rollback, never success.

GameBridge no longer changes the camera in RebindPlayerServices. Camera forward
and deferred rollback paths use the new native step, and stop before further
view-target/controller-form calls if it fails. The old reflected owner setter is
removed from execution and admission. The audit count remains 56 for the retained
inventory because this replaces one existing contract, not a new journal step.
No new frame/timer polling loop or object-wide search was added.

This ports owner/form strategy selection, NOT the remaining controller form-state,
view-target, physical AI/player camera-behavior casts, mission or menu systems.
The full role/admission gates remain in place. This has not been tested in-game.

### Controller form exit and single camera activation (2026-09-05)

The old handoff reached `GotoNewFormState` only after publishing the new pawn
and rebinding services. That is not sufficient even if its lookup is ported:
the shipping GotoState helper skips EndState/BeginState when the requested name
already equals the current state. A car-to-car switch can therefore keep the
old `CarForm` alias. On a different-state switch, EndState instead runs too late,
against the new controller/pawn pair and old cached vehicle inputs.

| Shipping routine | Verified behavior |
| --- | --- |
| GotoState `116FCAF0..116FCB8D` | ESI=UObject, EDI=force, three stack words, RET12. With force=false, equal state names skip the native state transition. |
| get_TransformerPawn `10B18130..10B18169` | Casts the physical pawn to TnPlayerPawn; failed cast returns compiled None (`10`), not an AI-capable logical player. |
| GetNewFormStateName `10B19180..10B1921C` | Vehicle branch calls the getter at `10B191B7`, immediately dereferences `[EAX+570]` at `10B191BC`, then selects car/plane/tank from the real vehicle type. A failed pawn cast is dereferenced before any form null check. |
| Tank.BeginState `10B280F0..10B281C1` | Independently inlines the same physical player-pawn cast, then dereferences `[ESI+570]` at `10B2814C`. Fixing only the getter call is insufficient. Car/Plane BeginState have their own matching form-cache assignments. |
| Walking.EndState `10B24900..10B24988` | Stops fine aim through PC+620, clears pressed-hover, follows Pawn -> InvManager -> Instigator -> Weapon and dispatches StopFire(0). Those references must still describe the old reciprocal pair. |
| Tank/Plane/Car.EndState `10B281D0..10B2824E`, `10B287C0..10B2883E`, `10B29410..10B2948E` | Use controller caches +668/+670/+66C and the form input state +104/+108/+108, call current ClearInputs and SetRemoteInputs, then clear the tutorial timer and saved moves. |

`NativePlayerFormState` now signature-gates the complete GotoState helper and
all four compiled EndState bodies, and enters None on the old reciprocal,
acknowledged pair **before** any controller detachment or role-map change.
Admission checks standalone/current world, authority, object indices, input
leases, form/transition state, current state name and exact EndState target,
cached vehicle identity, fine-aim/inventory/weapon owners, writable frame and
an empty state stack. Inherited campaign controller tables are allowed when
their actual EndState is the verified method or exact campaign forwarding
thunk described below; a base-class table address is not imposed on subclasses.
The wrapper preserves ESI/EDI and uses the actual
ESI/three-stack-word native ABI. No pawn, inventory, form or camera is cloned.

Rollback is armed before exit, including when the following detach rejects.
After a later failure, the new form exits while the new graph/services remain
intact; the original form re-enters only after restoring the original graph,
role map and services. Failed exit or deferred restoration retains transition
guards instead of resuming AI/input on uncertain state. Commit and rollback
verification now require the state/cache to match the controlled pawn, not
merely reciprocal Controller/Pawn fields. This adds one read-only admission
contract (57 for the retained inventory), without replacing ClearInputs.

The same trace exposes a second ordering error: robot and vehicle controller
BeginState call camera.StartForm themselves. GameBridge now uses
`NativeStageCameraForPlayerForm` to update the existing manager owner and
InitializeForms **without** activating. Controller entry owns the single
StartForm activation; both forward/rollback verify its actual camera owner,
strategy and state immediately afterward. The standalone NativeHandoffCamera
primitive remains available and tested but is not stacked ahead of controller
BeginState. Staging and entry remain in the existing atomic game-thread commit;
there is no intervening frame, timer or new update loop.

The subsequent controller-entry port below replaces the missing reflected entry
contract. Ongoing state-specific input/physical-pawn consumers and the complete
native role gate remain unfinished. No existing-pawn switch or class UI is
claimed to work on a device from these corrections alone.

### Custom controller form entry and campaign inheritance (2026-09-05)

`NativePlayerFormEntry.inc` is now integrated in GameBridge's forward handoff
and deferred original-form restoration. This is a custom, transaction-scoped
entry sequence, not a claim that stock WFC supports character switching.

The campaign controller's normal state vtables do **not** directly point to
the base BeginState/EndState methods. Their thirteen-byte wrappers forward one
stack argument to the exact base body and return four bytes. The previous
direct-pointer check rejected this real inheritance path. The matcher now
accepts only the complete `mov eax,[esp+4]; push eax; call base; ret 4` body with
the expected destination, not arbitrary forwarding or overridden mission logic.

| State | Campaign Begin / End | Custom entry uses |
| --- | --- | --- |
| Walking | `10B2BEC0` / `10B2BED0` | Existing pawn physics, camera.StartForm(0), robot profile, statistics.SetForm(0) |
| Car/truck | `10B2E850` / `10B2E860` | Existing CarForm alias, saved moves, car profile, statistics.SetForm(1), camera.StartForm(1) |
| Plane | `10B2E010` / `10B2E020` | Existing PlaneForm alias and plane profile, same vehicle order |
| Tank | `10B2DD50` / `10B2DD60` | Existing TankForm alias and tank profile, same vehicle order |

Four selective BeginState inline hooks install only after complete role-adapter
admission. Outside the matching game-thread/TLS transaction they call their
original trampolines unchanged. Partial installation remains pinned pass-through.
Complete code ranges, relocation operands, patch prefixes, trampoline bytes and
return destinations are verified. No worker teardown or new update loop exists.

The real engine GotoState helper selects the controller's real state and invokes
its BeginState through the campaign thunk. The custom sequence avoids both the
physical-player-only GetNewFormStateName and the independently inlined casts in
vehicle BeginState. It reads the selected pawn's actual current form and the
shared verified constant vehicle-type getter; it does not infer form from a
character name, replace state tables or manufacture a new pawn/form.

Vehicle aliases use the shared expected-value/GC-aware reference writer. Native
saved-move cleanup executes unchanged. Walking writes only DoubleClickDir at
UObject+304 (not the neighboring move/look counters); existing physics 2/4/10
is retained, walking physics 1 uses native SetPhysics with its real default
floor normal (0,0,1), and unsupported robot physics is rejected before entry.
Held boost is also rejected: entry cannot restart boost against unported state
consumers or discard a held action. These are explicit unsupported conditions,
not proof that transformed/in-combat switching is complete.

The real profile getters `10BAA290/10BAA2D0/10BAA350/10BAA310` read IDs
41/42/44/43 through native GetProfileSettingValueBool `11559900`. Native mapping
and value array shapes are checked; only bInvertMouse's packed bit is changed,
using the shared expected-value writer. Other input flags are preserved.

Native `TnClassFormStatHelper.SetForm` (`10A7B030..10A7B213`, ESI script-this,
stack form/RET4) flushes the previous form's time/distance statistics and resets
its tracking state. The complete sixteen-byte PRI.IncrementGameStat delegate
is retained and verified, not reconstructed as an eight-byte script delegate
or bypassed by writing CurrentForm. This is the controller's statistics tracker,
not a copy/reset of either character's health, inventory or ability cooldowns.

Each primitive is followed by input-lease, world, object-index, reciprocal
ownership, actual state/form, service-owner and unchanged-pose checks. Camera
staging remains separate and entry owns exactly one StartForm activation.
Reentry, duplicate entry and dirty callback failures return rejection or
rollback-required status; forward and rollback verify controller and camera
postconditions before proceeding. The full role and remaining call gates stay
in place: normal future transformation/input/AI consumers still need native
cross-role integration before this can be enabled for gameplay.

### Service failure propagation and buff HUD handoff (2026-09-05)

The forward runtime-service phase and `RebindPlayerServices` previously evaluated
later mutators using `next_call() && ok`: a failed targeting, pickup, sensor or
weapon step still allowed subsequent AI/HUD/collision mutations. They now stop
at the first failure and log the exact stage. Partial execution reports
rollback-required, retaining the existing transaction journal. The vehicle-AI
rebuild flag is armed before the call, not only when it returns success, so a
partially changed strategy remains owned by rollback.

The fine-aim/pickup/targeting native wrappers now require acknowledged reciprocal
player ownership, standalone authority, a live world, and the real Pawn.Controller
offset +1D4. Their postconditions capture and recheck all involved UObject indices,
world identity and controller-owned service pointers. A setter leaving correct
fields is not enough if a callback changed controllers, world, acknowledgment,
authority or reused an object address. Direct native setters also reject read-only
manager storage before writing. These are mutation-time checks, not new tick work.

`NativeBuffHud.inc` replaces both stripped BuffManager.SetHud call contracts and
all forward/rollback buff-HUD calls. Cooked export 8914 consists only of
`OwnerHud = NewHud`; the shared typed, expected-value, GC-aware reference writer
implements this exact leaf. No manager initialization, buff-list expansion,
expiration, ability changes or runtime copying occurs. Preparation is read-only.
Attaching requires the current acknowledged player pawn and that controller's
own live TnHUD. Clearing also admits the former player's existing manager after
its pawn returns to reciprocal squad-AI ownership. Both paths verify manager/pawn,
HUD/controller, world and object identities; wrong-thread calls reject before
metadata reads. The overall native role gate remains in place.

The ability HUD is **not** the same leaf; its complete manager handoff remains unresolved. Decompiled
TnAbilityManager.SetHud assigns OwnerHud, updates slot-zero resource UI or marks
it ready, then unconditionally marks slots one and two ready. It does not restore
their remaining cooldown display. FinishAbilityCooldown only notifies the HUD;
it does not erase the actual Cooldowns array. A live-transfer adapter must use
actual Cooldowns, ability cooldown durations, CooldownMultiplier, Resource and
resource requirements with the existing HUD animation calls; it must not replay
the stock initialization-ready notifications as a completed live-state restore.
StartAbilityCooldown includes normalized progress and paused/max-float handling.
These initialization semantics are pinned by the cooked behavioral checks.

### Native ability-HUD transport and restoration policy (2026-09-05)

`AbilityHudPlan.h` now constructs a read-only three-slot restoration batch from
effective native values. Active timers use remaining time divided by the current
multiplier and progress `1 - remaining / effective total`. A zero multiplier
pauses progress; FLT_MAX denotes cooldown not yet permitted to start. Slot zero
uses native resource/cost semantics when applicable, including the separate ready
notification after the resource threshold is met. Non-finite/overflowed inputs
fail rather than reaching Scaleform. No cooldown, spam-prevention timer, resource,
buff, ability instance or pawn state is reset or copied.

This policy follows StartAbilityCooldown (export 986), SetCooldownMultiplier
(969), Tick (975), and UpdateResourceUI (988). Effective totals cannot be taken
from a hardcoded Cooldown[1]: GetCooldownTime (508) chooses current skill data
with fallback and applies the cooldown-reduction buff. GetResourceRequired (517)
uses skill data and ResourceAmountModifier.ModifyFloat. Those live effective-value
calls still need a verified native reader before the planner can join the swap.

`NativeAbilityHud.cpp` dispatches the exact compiled TnHUD.SetAbilityResource
(`10ABF5B0..10ABF6FD`) and SetAbilityResourceReady (`10ABF700..10ABF84E`) bodies.
Both receive HUD+10 in EAX; stack arguments are slot/time/progress or slot,
respectively, with callee cleanup. Their complete relocated bodies and the -1
ready constant are verified. Each call constructs three **40-byte ASValue**
arguments; the underlying GFxMovie.Invoke uses EDI for its argument array.
HudMovie is GFxMovie, not GFxMoviePlayer; the native movie pointer is +3C and the
HUD callback FString is +4A0. The global Scaleform service and native movie must
exist because Invoke otherwise silently returns.

Transport requires game-thread execution, standalone reciprocal acknowledged
player ownership, that controller's HUD, valid world/object identities, and both
held input leases. Callback string contents/header, movie and ownership are
recaptured after each call. Nested dispatch is rejected; each batch is copied
before callbacks so caller-storage changes cannot alter remaining commands.
Dirty callbacks stop the batch. `kDispatched` means only that native calls ran:
the stock methods discard the GFx return and do not establish rendered success.
There are no new ticks, hooks, polls or per-frame scans in this transport.

**No GameBridge callsite uses this transport yet.** Remaining work is the live
effective-value reader, non-null AbilityManager.OwnerHud publication/restoration,
and forward/rollback transaction integration. The old ability SetHud contract
and the overall role/class safety gates stay unresolved; compiling this primitive
does not enable switching, fix the user-visible ability HUD, or complete classes.

### Existing ability-state capture and null HUD cleanup

`NativeAbilityState.inc` now reads the existing player's or reciprocal squad-AI
pawn's manager, individual ability instances/indices, remaining cooldowns,
anti-spam timers, multiplier and resource. It is used in GameBridge's preflight;
it does not create a manager or an ability, compact slots, modify resources, or
copy runtime state between characters. Three arrays must remain synchronized,
with at most the three campaign slots supported by the HUD planner. Manager and
ability owners, roles, acknowledged player/world, HUD owner and identities are
validated on the game thread. Reads do not require reference-write readiness.

Two native details constrain this reader. AddAbility (931) can append None while
still appending both timer entries; empty slots must retain their original indices.
Tick (975) clamps ordinary cooldown subtraction but does **not** clamp anti-spam
subtraction. Finite negative anti-spam values are normal and are preserved. These
cases are now covered by decompiled checks and regression fixtures.

`NativeClearAbilityHud` replaces the two null-only reflected SetHud calls in the
forward and rollback service paths. It uses the shared expected-value/GC-aware
writer, is idempotent when already null, and never invokes the stock SetHud's
resource getter or ready notifications. This is important even for null NewHud:
the compiled stock body still calls GetResourceRequired before checking OwnerHud
again. Only the HUD reference may change. Post-write validation checks original
object identities, the old HUD's validity, array storage/headers, slot instances,
timers and resources; dirty results stay owned by the already-armed journal.

The initial AI manager must have no HUD; preflight records this assumption and
rejects a foreign/stale non-null AI HUD instead of treating it as a rollback
destination. Both non-null SetHud contracts remain unresolved, since forward
attachment and restoration of the original player's animation state still need
the effective buff/skill readers. No readiness gate was removed.

NativePlayerServicesTests adds 6,000 ability-HUD clears and their idempotent repeats
on existing player/AI manager fixtures. It verifies unchanged manager/pawn bytes
except OwnerHud, unchanged timers and ability instances, empty slots/empty lists,
waiting cooldowns, negative anti-spam values, invalid parallel arrays/owners,
non-finite data, reentry, wrong-thread reads, and 17 dirty post-write failures.
Metadata and the shared reference writer are fixtures here (the actual writer is
tested separately). This is not a live ability/HUD or full-swap test.

Next native-value dependency: GetCooldownTime (`10BFA1E0`, compiled ability stack
argument, RET4/x87 float) calls the pawn's service interface at UObject+538 and
GetCurrentSkillDataIndex (`10A986E0`). GetResourceRequired (`10BFA350`, ESI compiled
ability, RET/x87 float) calls ModifyFloatInternal (`10B3A440`) through the actual
ResourceAmountModifier. That modifier queries GetBuff, ShouldBuffEffect and
GetFloatModifier and dispatches its aggregation/apply virtuals. A configured
number or blanket multiplication would omit these semantics. These dependencies
were traced, not declared ported or safe to call based on those addresses alone.

### Current-form ability buff service (read-only port)

`NativeAbilityBuffService.inc` now resolves the service actually used by the
ability getters, and GameBridge audits this path for both journal pawns. It does
not assume that TnPawn._BuffManager is the current ability buff service. Cooked
TnPawn.GetService (6421) asks the shared `_Services` container first, then the
current form. TnTransformerForm.GetService (14481) asks its own container, then
OnGetService. RobotForm.OnGetService (34432) supplies that form's `_BuffManager`;
VehicleForm.OnGetService (14832) has no TnBuffService case.

The port follows those reads without publishing lazy service-cache entries,
constructing objects, changing forms, or calling a stripped reflected method.
The service-container search uses the first matching Type even when its Service
is None: a later duplicate must not override native precedence. None falls
through to the next native stage, and a supported vehicle path with no service
returns successful-null, not the pawn-wide buff manager. Lookup failure remains
distinct from a genuine missing service. This distinction will be necessary
when applying GetCooldownTime's no-buff multiplier versus rejecting bad data.

`UnrealStructArrayNameEquals` compares full packed four-byte FNames via the
existing non-mutating FNAME_Find cache. It validates a scalar NameProperty with
four-byte elements and array/member bounds. Displayed/base-name equality would
ignore the lower instance-number bits and could select a different service.

The resolver admits the verified physical player/AI pawn classes, normal robot,
car/truck/plane/tank forms and base service container. Unknown overrides fail
instead of inheriting guessed behavior. Ownership, reciprocal controllers,
standalone authority, world/identity, registered current form and transformation
state are checked on the game thread. Returned managers must belong to that pawn.
No new per-frame work, renderer hook or gameplay input change was introduced.

Service tests exercise 7,000 precedence/fallback resolutions, all four vehicle
types with and without cached buffs, the AI-owned path, unknown classes, bad
owners/registrations/names, missing containers, transformation, unavailable write
readiness, and existing thread/world/reentry guards. Lazy caches and manager bytes
remain unchanged. Real reflection tests cover packed-name suffix differences,
wrong member dimensions/sizes/offsets and invalid array arguments. Service graph
metadata is a fixture in the resolver tests; the production reflection helper is
executed separately. Six additional cooked behavior checks pin the lookup chain,
including an explicit absence check for a vehicle buff fallback.

This is NOT a complete effective-value reader or HUD attachment. Skill-data
dispatch, exact active-buff queries/conditions and resource aggregation still need
their native-compatible implementations before non-null HUD handoff can replace
the remaining SetHud contracts. The stock swap/class readiness gates remain.

### Effective cooldown values (supported native skill-selector path)

`NativeAbilityValues.inc` now computes actual GetCooldownTime results for the
existing three slots, and GameBridge audits/logs those results for both pawns.
It reads the configured Cooldown array at the native/reflected +44 offset, uses
the current game's skill index with the native first-entry/empty-array fallback,
and applies +50 CooldownReductionModifier only when the resolved current buff
service contains the **exact** TnBuffAbilityCooldown class. Native GetBuff uses
class equality, not inheritance, remaining buff time or ShouldBuffEffect. Those
extra filters would change GetCooldownTime's behavior. No ability instance,
resource, cooldown or anti-spam timer is changed by the read.

The skill index is not supplied by the UI or fixed at 1. The reader validates
the current engine/world chain, the live standalone WorldInfo.Game, its actual
UClass/default object, and that default object's native selector slot +354.
Complete relocated GetCurrentSkillDataIndex (`10A986E0..10A98743`) and
WorldInfo.GetGameClass (`117A3E30..117A3E82`) bodies pin that lookup layout.
Only complete side-effect-free literal selector bodies (MOV EAX, immediate; RET,
or XOR EAX,EAX; RET) with nonnegative bounded results are supported. They are
read as native literal returns, not invoked through an assumed ABI. Unknown or
dynamic overrides reject rather than using a guessed campaign index. A live
Game is required; this port does not use the GRI/no-Game fallback during travel.

`UnrealResolveClassByPath` resolves the exact registered cooldown-buff class using
the existing bounded class/metadata lookup, not a scan of actors or an absent
UFunction. Effective reads preserve empty slots, distinguish no service/no buff
from failed lookup, reject malformed/unknown data and non-finite products, and
recapture manager/slot state after calculation. This is handoff-only work, not a
new per-frame polling/rendering loop.

The expanded service suite runs 6,000 effective cooldown reads across distinct
skill indices, buff/no-buff, first-entry fallback, empty configuration and
vehicle/no-service cases. It also covers null slots, an AI-owned pawn, exact
class mismatch, missing/dead game/defaults, stale engine world, unsupported or
negative selectors, missing buff classes, invalid buffs, NaN/overflow, and every
corrupted byte in both pinned native bodies. Both ranges match retained TWFC.exe.
Native selector leaves and object metadata are fixtures; this is not execution
of the entire native GetCooldownTime function or a live HUD/gameplay test.
The actual class-path resolver runs in the separate reflection suite.

At the end of that cooldown-only batch, resource modifiers and transactional
non-null HUD restoration remained. The next section records their supported
implementation; this does not make the full swap or class flow operational.

### Effective resource cost and transactional ability-HUD restoration

`NativeReadAbilityResourceRequired` now follows manager slot zero (not a sum of
all abilities). An empty first slot returns zero; a verified skill index other
than 1 also returns zero. Index 1 reads the existing ability's ResourceRequired
at native/reflected +54 and ResourceAmountModifier at +90. Complete relocated
`GetResourceRequired` (`10BFA350..10BFA3AF`) and `ModifyFloatInternal`
(`10B3A440..10B3A571`) bodies are checked against the retained executable. The
modifier's native/reflected BuffsWhichModifyProperty array at +28 is verified.

The supported modifier is the exact registered TnValueTimesProductBuffModifier
class, not an arbitrary derived virtual implementation. It queries the same
current-form buff service as the cooldown reader, preserves requested-class
order/duplicates and first exact-class GetBuff matches, and multiplies each
applicable factor with explicit single-precision storage between factors. A
genuine missing service returns the base value. Unknown classes, malformed
arrays, invalid objects and non-finite/negative values reject instead of being
reported as a valid zero cost or skipped modifier.

Supported active resource-buff classes are TnBuffFloatModifier and
TnBuffDifficultyAbilityResourceModifier, which use the inherited float getter
and base condition loop in the retained package. Their SP array falls back to
the normal FloatModifier array only when empty. A positive BuffLevel beyond the
array uses the LAST configured value (not cooldown's first-entry fallback).
Empty effective arrays and negative levels reject. ResourceAmountModifier's
OptionalInfo is None: behind-target and target-AI/player/sentry conditions are
therefore false, regardless of the owner's type. The downed predicate reads
the actual owner's _IsDowned flag. Condition order and short-circuiting are
preserved. Melee-service conditions, custom buff subclasses/overrides and other
unknown predicates remain unsupported and explicitly reject. No buff is
created, applied, expired, consumed, or lazily cached by these reads.

`NativeAbilityRestore.inc` joins the existing manager-state capture, effective
cooldown/resource readers, HUD planner, GC-aware expected-reference writer and
verified native Scaleform transport. GameBridge's two stripped non-null
SetHud contracts/calls are replaced by this supported restoration path. Stock
SetHud's unconditional ready notifications are not replayed. Instead the
selected pawn's existing resource fraction, cooldown progress, paused multiplier
and pending-cooldown state are sent to the HUD. Only OwnerHud is rebound; the
manager, abilities, resource, cooldown and spam-prevention arrays are retained.
Null cleanup still uses NativeClearAbilityHud, including rollback.

Read-only preflight can inspect the existing reciprocal AI-owned target and
prepare the current player's movie without mutation. Attachment itself requires
that exact target to be the reciprocal acknowledged player, standalone world
and valid HUD, with both original input leases held. Leases cannot be copied:
NativePlayerInputHeld verifies their original addresses. Restoration keeps
pointers to the journal's original leases. Re-entry is rejected. The native HUD
dispatcher now supports an owner-state guard before dispatch and after EACH
callback, stopping subsequent updates on changed identity/world/ownership,
manager/slot state or lost input. Dirty writes/dispatch report state mismatch
to the already-armed swap journal; they are not presented as successful swaps.

Local tests add 11,000 resource-cost reads and 3,000 clear/restore cycles plus
same-HUD rehydration and existing-AI/player ownership fixture round trips. They
check SP/fallback/difficulty levels, duplicates, no-buff/no-service/zero cases,
all supported condition classes, short-circuiting, unknown/malformed data,
overflow, every changed byte in both native resource signatures, no runtime
state resets, original-versus-copied input leases and post-write/per-callback
state changes. Sixteen injected mutation kinds are tested before dispatch and
at each of three callback boundaries. The native HUD suite separately executes
the complete two shipping HUD bodies and checks the new validation boundaries.
The service test's Scaleform transport, metadata, reference writer and input
leases are fixtures; their native/production pieces have separate suites. These
are NOT full GameBridge execution, rendered HUD or gameplay tests. Thirteen
additional cooked behavior checks pin the modifier/getter/condition semantics.

The complete role adapter, remaining native swap methods and native class/menu
integration are still required. Their failing admission gates remain intact.
No device, APK, payload manifest, publication or deployment changes were made.

## State-specific input cleanup integration

The journal's quiesce stage now calls `NativeClearPlayerFormInputs` instead of
the stripped reflected `ClearInputs`. Read-only admission verifies thirteen
complete shipping bodies on every preparation, current native state-method
slots, reciprocal acknowledged ownership, current world, weapon ownership,
form caches and vehicle-input ownership. The mutation requires the original
move/look leases and rejects re-entry. A post-call identity/state/lease mismatch
returns a dirty failure to the existing rollback journal, not success.

Walking executes native EndBlock, AbortMelee, StopFineAim and global ClearInputs
in their original order. Car/plane/tank execute their actual input Reset and
StopBoost paths before deferred-fire cleanup. Controller flags preserve every
unrelated bit; native vehicle resets preserve unrelated input flags. No pawn,
inventory, form, camera or actor is recreated and no controller-state transition
is introduced by this operation.

The native walking EndBlock (`10B1C980`) and AbortMelee (`10B1BAC0`) service
paths still cast to a physical TnPlayerPawn. A physical TnAiPawn in walking state
is deliberately rejected until those consumers are adapted under item 2 below;
silently skipping shield/melee cleanup is not a valid cross-role implementation.
This does not make repeated swapping operational or complete item 1.

The form fixture now executes 4,000 additional clears through all four complete
native state bodies. It verifies packed flags, vehicle axes/rotation reset,
unchanged fixture pawn/weapon bytes, exact callback counts, stale owners and
types, malformed/forwarding methods, missing boost setters, wrong thread, lost
or copied leases, nested requests and dirty callbacks. All thirteen signatures
match TWFC.exe; each byte is tested for corruption rejection. Deep block, melee,
weapon, fine-aim and boost services are fixtures here, not live gameplay.
Nine additional decompiled behavior checks pin the base/vehicle clear sequence,
deferred-fire flags and the shield/melee/boost service calls.

## AI aiming integration

Three stripped reflected calls (`ShouldUpdateAiming`, `get_OwnerIsAiming`,
`AimAtLocation`) are replaced by `NativeAiAiming`. Read-only preflight is wired
into the existing journal audit. The custom role proxy uses the new binding;
its unchanged 250-ms schedule and complete role-admission gate still apply.
This is not a replacement for native squad GOAP or an enabled swap feature.

The previous proxy checked alignment before publishing the new enemy location.
It could therefore reject the update based on an old target, never giving the
aimer the new location. The updated path verifies Controller.Enemy and its live
LOS state, executes native AimAtLocation, then executes native IsAiming. The
proxy also refreshes IsFiring after StopFire/weapon rebind in the same slice;
the prior cached boolean must not suppress starting fire for the new target.

Verified shipping paths:

- AimAtLocation `10BC86A0..10BC86B5`: EAX=compiled aimer, ECX=Vector pointer;
  changes only mode +2C to 1 and target location +34..3F on the UObject.
- IsAiming `10BC8260..10BC838B`: EAX=compiled aimer; uses weapon location and
  rotation when `_CheckAimFromWeaponRotation` and Weapon are present, otherwise
  pawn pose. Uses native adjusted aim, rotator conversion and dot threshold.
- CalculateAdjustedAimFor `10BC86E0..10BC8786` and target-location getter
  `10BC7ED0..10BC7FD9`: location mode avoids stale target-actor service pointers.
- The inlined LOS consumer `10BB74E0..10BB75A8` verifies the exact data path:
  Controller.Enemy +2A4, AiWorldState +300, collection array +28, 24-byte entries,
  class CDO HashIndex and matching entry.Type, Value.BoolVal bit 0 at entry+10.
  This consumer is signature evidence only; its unrelated side effects are
  never invoked. The mod reads the actual CDO index, not a hardcoded slot 13.

Eight complete bodies and three exact floating-point constants are checked
against the retained EXE. Threshold bytes are pinned directly: a C++ float-to-
double cast under x87 excess precision did not reproduce the shipping bits in
the optimized fixture, so no decimal-literal approximation is used. The LOS
class is resolved during preflight, not repeatedly scanned in the proxy tick.

Each update validates reciprocal ownership, object identities, current local
world, exact aimer class/layout, finite actor/weapon/target positions, input
mode and live LOS-array shape. Unknown derived aimers reject. Missing LOS or
temporary transformation/downed/stasis pauses aiming with a successful no-fire
result, allowing normal retry without declaring the role mapping broken. No
weapons, ammo, health, services, actors or transformation state are replaced.

The new optimized suite executes 6,000 native target-before-alignment updates,
covering all prior aim modes, changed/stale targets, both physical pawn types,
weapon/pawn origin and rotation selection, unarmed fallback, angle thresholds,
CDO index changes, absent/mismatched LOS, paused/resumed states, invalid worlds,
ownership/type/array/layout failures, re-entry, wrong thread and dirty callbacks.
Every signature byte is corruption-tested. Native setter, IsAiming, adjusted
aim, location getter and vector/rotator wrapper bodies run; metadata, quaternion
initialization and deep matrix/atan2/sqrt primitives are fixtures. This is NOT
full TickCharacterRoleProxy, live firing/AI, combined swap or gameplay testing.
Seven more cooked behavior contracts pin the corresponding source semantics.

### Native movement stop and the physical-AI no-op

`NativeAiStopMovement` replaces the stripped StopMovement audit and transaction
call, plus all four role-proxy reflected StopMovement calls. The complete stock
controller body `10BBA090..10BBA18C` immediately rejects a physical player pawn
through `TnAiPawn(Pawn)`. Local execution of that body reproduces the silent
no-op. The robot branch itself only zeros Acceleration at UObject+FC and copies
Rotation (+E4) into DesiredRotation (+174). The adapter applies exactly those
shared fields to either physical pawn type, after live reciprocal ownership,
standalone/current-world, authority, form identity, reflected offset, writable
range and game-thread checks. It does not change Velocity, actual pose, health,
inventory, physics, state stack or movement delegates. CancelMove remains a
separate state-dispatched operation; this does not replace its cleanup.

For physical AI vehicles, the actual strategy StopMovement body at
`109DF150..109DF16D` executes with ESI=compiled strategy. It invokes the existing
form boost setter with false and sets strategy +68 bit 1. The current form's
script slot +D0 must equal the complete car (`10BDD840`), plane (`10B14030`) or
tank (`10B69110`) leaf; those preserve unrelated packed input bits. Trucks
inherit the car path. Strategy controller/pawn links, actual input-state type
and owner, reflected flags and post-call identities are checked. Retained
vehicle table references independently confirm +70 points to the pure mode
getter `109D62F0` and +D0 to these setters. All five complete bodies are pinned
with ASLR relocation; no signature scan was added to the update loop.

Mapped physical-player vehicles deliberately continue through the existing
role-proxy input quiesce path, not the stock AI strategy. The transaction already
separated this lifecycle: preflight now records native versus role-proxy path
instead of looking up an unused stripped StopMovement function. This is NOT a
port of vehicle proxy input setters, native strategy initialization or GOAP
compatibility. Those remain in the same existing integration areas below.
Failed movement quiescence logs cancellation/stop results, proxy status and
delegate verification before retaining rollback responsibility.

The new suite executes 6,000 stock-controller robot/car/plane/tank/truck stops
and compares the production adapter byte-for-byte against them, then repeats
6,000 physical-player robot stops that the stock call skips. It verifies only
the intended fields change, idempotent repeated calls, ownership/world/form/
layout/type failures, wrong thread, re-entry, seven dirty post-call failures,
writable bounds and every signature-byte corruption. Full bodies match the
retained EXE. Native stop/boost bodies execute; metadata, class lookup and typed
pawn getter are fixtures. This is not an integrated gameplay/AI handoff test.
Six additional cooked behavior contracts cover the corresponding game logic.

### Native player/camera view-target transfer

`NativeViewTarget.inc`, sharing the existing camera admission and input leases,
replaces the remaining SetViewTarget lookup, forward call and rollback call.
It does not replace camera strategy selection or controller BeginState and does
not write Controller.ViewTarget directly. Preparation is read-only and runs
after the camera binding. Original preflight requires the current pawn as the
view target so rollback can restore that exact admitted target.

| Complete shipping body | Responsibility |
| --- | --- |
| `11688530..11688662` | Controller SetViewTarget helper: EAX=12-byte transition pointer, ECX=new raw actor, stack=raw controller, RET4. Delegates to the existing PlayerCamera. The controller and target are **not** interchangeable arguments. |
| `11686910..116869FC` | Camera SetViewTarget: validates existing/current/pending targets; same-target calls do not assign again; immediate changes call native assignment and clear the pending target. |
| `11686680..11686909` | CheckViewTarget: validates actor/PRI ownership, can retarget through PRI.Owner.Pawn, and synchronizes controller ViewTarget/RealViewTarget. |
| `11651220..116512CD` | AssignViewTarget: updates camera target/FOV/aspect, preserves the normal event-execution gate and BecomeViewTarget virtual, and checks the local-player path before client notification. |
| `10C97F20..10C97F38` | Native transition defaults: zero blend time, Cubic=1, exponent=2. The adapter supplies these values explicitly. |
| `1168B590..1168B5D3` | Native local-player predicate. Its `12B11274` class cache resolves Engine.LocalPlayer (`11465790` constructor metadata), preventing entry into the nonlocal RPC branch. |
| `114CF6D0..114CF73D`, `1098B3A0..1098B3BB` | Native event-execution and pending-kill checks, retained unchanged. |

All eight bodies, their PE relocation operands and two scalar constants are
verified against the retained executable. Runtime virtuals must retain the
known event/local-player/pending-kill targets, the complete pointer/zero native
controller/pawn getter leaves and the empty RET4 BecomeViewTarget callback.
Cooked Actor callbacks are empty and TransGame has no BecomeViewTarget override;
unknown overrides are rejected rather than executing unreviewed mission code.
The existing detachment fixture used self as both target and receiver, masking
argument-label ambiguity; it now labels the ABI correctly. This is a test
coverage correction, not evidence that the old reflected call reversed them.

Admission requires the acknowledged reciprocal player pawn, a shared standalone
world, local player, owned PRI and existing camera, matching native/reflected
camera layouts, no pending blend, and a current target from this transaction's
two pawns/controller. PRI links may be None or the owned local PRI, never an
unrelated actor graph. It will not steal a cinematic/spectator camera. Failed
postconditions retain the existing guarded rollback: both controller and camera
targets must be the new pawn, RealViewTarget must be cleared, and identities,
pose, movement, held leases and camera strategy/owner/state must remain valid.
There is no viewport/window/focus change, new polling, scan per frame, camera
clone, state restart or direct position write in this port.

The expanded camera suite executes 6,000 transfers plus same-target repeats
through the shipping controller/camera/check/assignment/default/local-player/
event-gate bodies. It covers both directions with existing actors, all four
vehicle types and both robot/vehicle modes, distinct controller/target pointers,
FOV/aspect/PRI changes, unchanged non-view fields, full signature corruption,
invalid worlds/ownership/metadata/callbacks, pending blends, re-entry, wrong
thread and eight dirty-result cases. Class lookup and metadata are fixtures;
the empty actor callback leaf is a fixture matching the verified body. This is
native ABI/local logic evidence, **not** a live camera or integrated swap test.
Five additional cooked contracts pin callbacks, transition defaults and client
forwarding. Role/class/checkpoint integration remains in the same areas below.

### Dialog-owner initialization and proxy rollback

`NativeDialogOwner` replaces both InitializeWithPlayerPawn/InitializeWithAiPawn
call contracts and forward/restoration calls. Cooked exports 7208, 19584 and
7286 establish exact semantics: assign OwnerPawn, then the private typed owner
alias. There is no dialogue-cache reset, sound playback, timer, allocation or
delegate initialization in these functions. The adapter uses the existing
expected-value UObject reference writer and its native GC barrier after each
assignment, rather than adding a guessed compiled-call address. Both fields'
live reflected offsets, distinct scalar layout, raw pointer agreement and
writable destinations are verified. Managers must be the exact stock player/AI
classes owned by the relevant controller; unknown initializer overrides reject.

The player alias is only assigned by the player-manager code (confirmed by a
decompiled function scan); its notification implementation uses inherited shared
pawn data. The AI manager is different: DialogNotifyFromTeammate calls
OwnerAiPawn.HasSameKillTarget with a physical TnAiPawn teammate. The adapter
therefore does NOT initialize that AI manager against the physical-player proxy
or claim those consumers are adapted. The existing proxy branch continues to
retain its original AI dialog owners.

There is an explicit restoration path for that retained owner. After multiple
three-member swaps it need not be either actor in the latest swap pair. A
rollback can restore the saved, live, same-world physical AI owner only when the
AI controller again owns the expected physical-player proxy. Ordinary AI
initialization still requires the reciprocal physical-AI pawn/controller pair.
This preserves the journal's existing proxy-owner contract instead of failing
rollback on a valid third-member reference or assigning an incompatible player
allocation to AI-only consumers.

Wrong thread, invalid object/world/authority/acknowledgment, lost manager links,
unknown classes and re-entry reject before mutation. After the first reference
write, any failed second write or context/identity check reports dirty state and
keeps rollback ownership. Restoration repairs a partially updated base/alias
pair. The new suite exercises 12,000 initializer calls, unchanged pawn/manager
data, both directions, physical-player rejection for native AI, retained third
member restoration, each failed write, seven dirty-result cases and admission
failures. Metadata and the reference writer are fixtures; the actual writer/GC
barrier already has its separate native suite. This is local implementation
verification, not a gameplay dialogue or role-adapter completion claim.
Four cooked contracts pin the three initializers and physical-AI consumer.

### Point-of-interest reset integration

`NativePointOfInterest` replaces the stripped manager Reset audit and the shared
forward/rollback service call. It invokes the complete compiled Reset at
`10BA9140..10BA9163` (EAX=manager UObject+10, plain RET), not a raw reference
clear. `TnPlayerController.OnSetPawn` also inlines this exact sequence at
`10B21F96..10B21FB6`. The existing service journal remains armed before entry.

Cooked exports 31428/31427/31433 and 13029 establish the sequence:

- Remove the manager's current POI, including the None/None case.
- Remove the focusing camera-state class and clear the FOV override.
- Clear the manager's focusing bit.
- Remove pending ADD notifications matching the command class name/hierarchy.
- Find the first registered command of that class/subclass, enqueue its REMOVE
  notification and remove that registered entry, retaining unrelated commands
  and existing REMOVE notifications in order.
- Clear ActivePointOfInterest, then Command.PointOfInterest, with the game's
  reference barriers.

Ordinary Remove does **not** clear Command.PointOfInterest. The admission checks
therefore accept its stale-but-valid retained target after ActivePOI becomes
None. Requiring those two references to match would introduce a silent rejection
in a normal state. Reset does not change the pawn, window, focus at the OS level,
controller input, camera target/strategy owner, or current weapon state.

Fifteen complete code bodies are pinned against the retained executable with
PE HIGHLOW relocations: Reset/Remove, camera StopFocus, command unregister/find/
notification insertion, class-cache lookup, and the camera/command/notification
array removal, insertion and heap-resize helpers. Native arrays stay on WFC's
heap, including capacity growth, shrinking and zero-length frees. There are no
new per-frame scans, polling or update loops.

Admission requires the game thread, acknowledged reciprocal local player pair,
same live standalone world, exact POI manager/command classes, owned camera and
services, writable verified property layouts, the already-resolved native focus
class cache, initialized engine allocator, bounded native arrays and terminated
command UClass chains consistent with the compiled object-type table. After
execution, exact list contents/order, reference identities and unrelated packed
flag bits are checked. A dirty result returns to the existing journal rather
than reporting success. The enclosing physical-role and mission gates remain.

`NativePointOfInterestTests` executes all fifteen shipping bodies for 4,000
resets plus repeated calls, with notification growth, three empty-array free
paths, subclass matching, stale command targets and removal of duplicate focus
states. Every pinned byte is corruption-tested. Wrong-thread/reentry, malformed
arrays, class cycles, owner/acknowledgment/world failures, missing native services
and seven dirty-state injections are covered. Metadata, imported CRT, the engine
allocator and GC barrier are fixtures; this does **not** exercise live PhysX,
mission scripts, a rendered HUD, or a complete character swap. Ten cooked
contracts independently pin the matching source semantics.

### Collision-role integration

`NativeCollisionRoles` ports the complete private
`TnPawnCollisionController.SetRBCollidesWithChannel` wrapper (export 8551) to
its actual low-level engine calls. It invokes the native
`Engine.PrimitiveComponent.SetRBCollidesWithChannel` executor on the owned robot
mesh and then the optional vehicle mesh. The component parameter is named
`bNewCollides`; `Value` belongs only to the stripped TransGame wrapper. Channel
15 is `RBCC_PlayerOnlyBlockingVolume`, as confirmed by the cooked enum and
collision-controller Initialize. Native PhysX filtering updates are retained;
there is no raw collision-flag write, physics reinitialization, collision-mode
transition, component replacement or team-channel reset.

The bridge adds only this unindexed native function to its existing allowance,
with exact flags/name/class checks and an executable-address check. A represented
conflicting package is rejected; an absent packed package outer is allowed for
the exact native declaring class, matching the shipping metadata limitation.
No general native-call or stripped-script bypass was introduced.

Both original pawn-owned collision services remain attached to their original
actors. Forward and rollback now call the same guarded wrapper, using the
current reciprocal controller role rather than the pawn's physical class.
Checks cover authority, world, acknowledged player pair, AI ownership, service
and mesh owners, exact component call contracts, and every one of the 21
collision channels on each mesh. After the first native call, a changed owner,
identity or unrelated channel stops the second call. A failed/dirty invocation
is reported as potentially mutated so the already-armed journal restores it.
Even an already-matching bit still uses the engine setter to synchronize shapes.

The new suite runs 6,000 role round trips, same-role repeats, robot-only/vehicle
cases, partial failures and rollback, eight dirty first-call injections and
off-thread/reentrant/invalid ownership cases. The production reflection suite
adds 3,000 Byte/Bool calls through the real marshaller/assembly adapter, verifies
the native parameter name and packed Boolean mask, and rejects conflicting
native owners, flags, tokens, names and null/non-executable executors. PhysX,
the engine FFrame executor and metadata for the wrapper suite are fixtures;
these checks are not in-game collision verification. Two critical signatures
and three cooked behavior contracts pin the source side of this integration.

The weapon trace now reaches Engine.Weapon.SetData (27086) and
TnWeapon.UpdateData (16737), not just the role selector (16733). UpdateData resets
spread/fire-interval state, resets heat properties and may initialize owner
animation, ammo skill and pickup meshes. ResetHeatProperties (16609) rebuilds
modifiers through SetHeatProperties (16614) and enters Cool. SyncCurrentHeat
(16620) clamps and calls the current state's CheckForStateChange; it can restore
the overheat state but does not restore cloned modifiers' prior runtime timers.
Those side effects must be preserved/adapted in item 1 below. This trace is not
a completed weapon binding and the existing weapon gates have not been bypassed.

The retained native heat path used by the heat-sync binding is:

- `109EF4E0..109EF51F`: SyncCurrentHeat; ESI=heat UObject+10, float on stack,
  RET4. It clamps with `10985AA0`, stores CurrentHeat and calls script vtable
  slot +48 with HeatMax. This is not a direct scalar setter.
- `109EF550..109EF58C`: the separate ClampHeatAndNotify body, same clamp and
  current-state dispatch; ESI receiver, plain RET.
- Heat raw layout follows those bodies: HeatProperties.HeatMax +2C, modifier
  array +30, CurrentHeat +3C, OwnerPawn +40, overheated observer array +44,
  cooled observer array +50. Validate against live reflected offsets when bound.
- Script tables `127FA198` (base), `127FA1E8` (Cool), `127FA238` (Overheated)
  are switched by `109EF210..109EF250`. Slot +48 is the empty RET4 at
  `109D1D30`, Cool check `109EF6D0..109EF6FA`, or overheated check
  `109EF700..109EF72A`. State FNames use caches `12B302CC` (Cool) and
  `12B31E80` (Overheated); class cache is `12B70218`.
- Cool checks heat >= max; Overheated checks heat < max. Both enter the native
  state path at `116FCAF0`. BeginState thunks `109D1D40`/`109D1D50` run cooled/
  overheated observer loops `109EF690..109EF6C8`/`109EF5F0..109EF628`.
  Each callback preserves all four words of the 16-byte compiled delegate.
- TnWeapon.WeaponOverheated enters the weapon's own Overheated state. That
  state's BeginState calls OwnerAnimator.OnBeginOverheat, plays event 13,
  sends heat event 5 and updates AI world state; EndState sends heat event 6
  and clears the AI world-state flag. Binding the heat routine must account for
  these real callbacks, not suppress them or treat them as empty notifications.

Nine cooked contracts pin the role-selector/SetData/update/heat semantics.

### Native heat synchronization and inventory-journal correction

`NativeWeaponHeat` replaces the stripped SyncCurrentHeat call in both forward
and rollback inventory paths. It executes the complete shipping Sync, clamp,
Cool/Overheated checks, state selector, BeginState and observer loops; it does
not write a heat scalar in place of the game callbacks or reset HeatProperties.
Fourteen complete body signatures and six script-table entries are checked
against the retained executable. The existing UObject state dispatcher remains
responsible for transitions; no state frame or observer binding is patched.

Admission requires the owned weapon/heat service, an acknowledged player or
reciprocal AI controller in the same authoritative solo world, no firing, a
valid current Cool/Overheated state, an existing empty-stack state frame and
the expected typed property offsets. Both destination and current script tables
are checked before entry. Live modifier identities and the complete four-word
observer bindings are captured. Only this weapon's own named callbacks are
admitted. Delegate FNames are **packed** (`name index << 14`), whereas an exact
function handle exposes an unpacked name index. The read-only unnumbered-name
reader handles equivalent cooked name-table entries without requiring a runtime
UFunction or inserting new FNames; numbered/incorrect names and duplicate
callbacks reject. Holstered weapons without an instigator/observers
are supported; absence of WeaponHeat is still handled by the inventory caller.

Nonfinite/negative values and values exceeding the current heat maximum reject
before mutation instead of silently clamping away the journaled value. A callback
that changes ownership, identity, the modifier array, observer bindings, maximum
or control/world state returns dirty failure for the already-armed rollback.
This binding does not reconstruct modifier timers erased by the **preceding**
normal SetData/UpdateData path. That custom role-update integration remains
unfinished; its preflight gate has not been bypassed.

The inventory graph check previously compared CurrentHeat to the earlier UI
capture before reaching the existing mutation-time refresh. Normal cooling
during AI quiescence therefore rejected an unchanged inventory. The extracted
`WeaponRoleJournal` comparison now checks the same weapon/data/heat identities,
not a time-varying scalar. The live reader still validates finite nonnegative
heat and non-firing state, and ApplyInventoryWeaponRoles still captures the
latest value immediately before conversion. No old heat value is replayed just
because it appeared in the first UI snapshot.

The new suite exercises 3,000 actual Sync/Clamp/check/selector/notification
cycles, exact maximum/zero boundaries, same-state calls, optional observers,
all signature bytes, all six table entries, packed names, wrong threads,
reentry, invalid ownership/state and seven dirty callbacks. It checks unchanged
weapon/pawn bytes and a modifier timer sentinel, and tests every identity field
in the journal comparison. UObject state dispatch, reflection and the downstream
weapon animation/heat-event callbacks are fixtures, **not live game execution**.
Six further cooked contracts pin observer dispatch and GivenTo's physical-player
role selection. This does not establish completed weapon-state preservation or
successful swapping in gameplay.

The next role-update path is now located without invoking it:

- `109E64A0..109E64FB`: GetWeaponDataByType plus its four-entry switch table.
- `109E6500..109E6514`: SetWeaponDataType; ECX=weapon+10, EAX=role, then tail
  call to `117A4430..117A4474` (Engine.Weapon.SetData). SetData assigns the
  data reference with the GC notification and dispatches script slot +228.
- `109E6520..109E6701`: base TnWeapon.UpdateData; spread and fire-interval
  initialization, heat-property clone/reset/Cool transition, owner initialization
  and pickup-mesh cloning. The heat service at weapon raw +324 is used without
  a null guard in this retained base body. A future binding must account for
  concrete UpdateData overrides/absent heat services, not assume calling this
  base implementation on every inventory item is valid. This is a source-level
  hazard, not proof that it caused a previous device crash.

### Fire-rate ramp/spread preservation

`NativeWeaponProgress` is connected to the same inventory journal's capture,
per-item forward boundary and rollback. Cooked HmValueLerper.Initialize sets
CurTime to zero; SetRampTime preserves CurTime/RampTime only **after** Initialize
has already erased it. The new source port keeps the old normalized ramp fraction
with the new tuning's interval bounds/duration and restores CurrentSpread. Zero
duration has no ramp fraction. Rollback restores all four original lerper floats
and the original spread exactly. Neither operation constructs or replaces a
lerper, weapon, pawn, inventory, modifier or pickup mesh.

The retained InitializeFireInterval body at `109E72C0..109E736C` proves weapon
FireIntervalLerper raw +2EC and lerper StartValue +28, EndValue +2C, RampTime +30,
CurTime +34. UpdateData's store at `109E6554` proves CurrentSpread raw +300. Six
instruction checks pin those offsets in local tests, and runtime admission also
requires matching reflected field names/types/offsets and writable float storage.
Invalid values, replaced lerpers/data/actors, nonlocal worlds, firing, wrong
threads and unsafe ownership reject. Identity checks permit the intended
controller-role change across graph rollback, but require the controller and
Instigator to remain unchanged during the progress restoration itself.

Preflight snapshots compare persistent identity rather than evolving timing.
Each weapon's progress and heat are captured again immediately before its own
role setter, not merely once before converting the entire inventory. A callback
from an earlier item therefore cannot silently make a later entry stale; changed
identity/type/heat ownership rejects with a boundary log. The existing journal
is armed before the setter, including potentially dirty failures.

The new suite covers 3,000 forward/rollback cycles, changed/zero durations,
ascending progress through the full fraction range, exact rollback, repeated
calls, holstered weapons, replaced identities, protected float storage,
off-thread/reentrant requests and seven dirty-state injections. Reflection and
the role-data update are fixtures: these tests run the preservation source port,
not the complete game update or a live character swap. Eight additional cooked
contracts verify the lerper rules and the three weapon-specific UpdateData
overrides (BeamData, GrenadeData, RepairData). Those derived caches, heat modifier
initialization, owner animation and pickup-mesh side effects still must be
handled by the pending custom role-update binding. The stripped SetWeaponDataType
preflight gate remains in place; this batch does not make swapping/class menus
operational or add a fifth integration area.

The heat-modifier source trace for that same pending role update now identifies
the retained state explicitly: Charge has IsCharging/IsOverheated; Hold has
IsHoldingFire/IsOverheated; Lose has those hold/overheat flags and
TimeSinceLastShot; Overheat has TimeLeftOnOverheat; PerShot applies AddPerShot on
its configured event. Initialize in the common base only sets WeaponOwner.
Weapon overheat entry/exit sends events 5/6, which reset the Overheat timer and
change those flags. SetHeatProperties only clones/runs modifiers for local
control. Therefore simply suppressing all initialization is not sufficient for
first control of an AI weapon with an empty modifier list, and replaying
Cool/Overheated after reinitialization is not exact timer restoration. This
remains part of the existing weapon-role update requirement, not a completed
modifier migration or evidence of a particular past device crash.

### Same-object heat modifier runtime journal

`NativeWeaponModifiers` now captures and restores the five concrete modifier
types listed above. The inventory reader captures their identities; each
individual weapon boundary refreshes the live flags/timers after quiescence.
Forward and reverse role journals restore these scalars **after** native heat
synchronization, whose overheat callbacks can reset them. No Clone, Initialize,
SetTimer, EndFire, state transition, object-array copy, or GC ownership change is
performed by the new helper. No per-frame work was added.

The cooked Overheat handler subtracts DeltaTime without clamping its timer;
negative terminal `TimeLeftOnOverheat` values are therefore retained exactly.
`TimeSinceLastShot` remains nonnegative. All captured floats must be finite.
The event enum includes `WHET_LoadShot = 8`, not only events 0..7; PerShot
modifiers configured for event 8 are supported (9 is the MAX sentinel).

Admission reuses the verified live weapon/control/world reader and requires
the same weapon-owned TnWeaponHeat, bounded typed modifier array, exact concrete
classes, stable object indices, and each modifier's packed UObject Outer equal
to that heat service. Its WeaponOwner must be the same pawn or null for an
uninitialized/holstered owner. Duplicate entries, foreign/template modifiers,
reordered/reallocated arrays, changed tuning/event values, unknown subclasses,
stale ownership, read-only timer storage and invalid control reject. The new
read-only `UnrealObjectOuter` uses the existing packed index decoder and validates
both GObjects identities; it does not treat Actor.Owner as UObject.Outer.

HeatMax must also remain unchanged: replaying old IsOverheated flags after a
capacity change could contradict the new threshold. A zero maximum is valid for
an ammo-fed weapon's scalar snapshot (TnWeapon.IsHeatBased tests Data.HeatMax > 0);
this does not relax NativeSyncWeaponHeat's separate positive-maximum admission.
Tick's heat events are conditioned on that data predicate, not directly on the
controller. Retaining modifiers while changing AI/player data consequently still
needs explicit lifecycle policy; merely retaining the array is not that policy.

The journal no longer adopts a replacement heat service by overwriting its saved
pointer/index. It checks both service identity and modifier topology before
native synchronization, then reports clean rejection or dirty restoration failure
as appropriate. Boolean restoration uses the existing game-thread packed-bit CAS
writer, preserving unrelated flags. Timer restoration changes only its reflected
float field. Postconditions recheck ownership, tuning, topology, all runtime
scalars and the controller/data/Instigator identities across the write boundary.

This deliberately **does not** enable the destructive normal role setter:
SetHeatProperties clears/clones the list, so that path cannot pass same-object
preservation. First player control of an AI weapon with no modifiers still needs
the custom initialization/retention lifecycle in the pending weapon-role binding.
An empty-list capture is not proof that such a weapon is ready for player use.
Tuning/derived data caches/animator/pickup handling remain in that same binding.

NativeWeaponModifierTests runs 3,000 repeated restorations with 5/64-entry
graphs, all five classes, exact negative/positive timers, unrelated-byte
preservation, idempotence, null holstered owners, load-shot events, role/data
changes between stages, eight dirty postconditions, partial Boolean failures,
stale/replaced/reordered/foreign objects, invalid floats/arrays, protected pages,
wrong thread and reentry. Metadata/control are fixtures; no actual weapon
callbacks or gameplay are claimed. The real resolver suite separately exercises
packed outers, invalid/self/absent outers and recycled outer slots. Nine new
cooked contracts pin Initialize, all five event handlers, the complete relevant
event numbering, weapon Tick and EndFire behavior.

## Remaining native port (required before gameplay claims)

### Derived tuning references and grenade handoff

`NativeWeaponDataCache` is now part of inventory capture, per-weapon boundary
refresh, forward tuning and rollback. It ports the reference assignments for
BeamData, GrenadeData, GrenadeBagData and RepairData through the existing
GC-aware expected-value writer. Data must already equal the journal's expected
forward/rollback data; the cache may be the prior reference or already updated
to that target. Any third-party/stale reference rejects. Only those references
are written; no base UpdateData, GivenTo, Equip, weapon creation, ammo refill,
heat reset, mesh cloning or animation initialization is replayed.

The earlier `--scan TnWeapon UpdateData` missed **TnGrenadeBag.UpdateData**.
Scanning UpdateData itself and the retained executable's direct calls to base
`109E6520` found these additional reference assignments:

- Beam `109EAE70..109EAEE9`, store `109EAEB2`: script +384 / raw +394.
- Grenade launcher `109EE750..109EE7C7`, store `109EE797`: script +394 / raw +3A4.
- Grenade bag `10AB6F10..10AB6F87`, store `10AB6F57`: script +388 / raw +398.
- A fourth retained caller, `1093C560..1093C5D9`, refers to
  **TnWeaponDataPlasmaCannon**, not TnWeaponDataRepair. Do not mislabel this as
  the Repair native body. Repair's source assignment is verified in the cooked
  package; its field offset is obtained from typed live metadata, not a guessed
  reuse of PlasmaCannon's raw +3C8 offset. The custom base role binding still
  must verify the concrete native UpdateData dispatch before activation.

The grenade bag has a second consumer: Equip initializes the existing
GrenadeThrower with the pawn and GrenadeBagData. Merely changing the bag's cache
leaves GrenadeThrower.GrenadeData pointing at the old role's projectile/toss
data. The new source port updates both caches without re-equipping. It checks
the thrower's exact journal identity, weapon Outer, same ThrowerPawn and typed
GrenadeData field. It revalidates control and ownership before the second write,
and supports rollback after only the first reference was changed.

ServerToss can schedule `SpawnGrenade` on ThrowerPawn with TimerObj=self after
queuing the toss animation. SpawnGrenade later reads GrenadeData for the
projectile and velocity. Retuning in between would change an already-started
throw. The cache boundary reads the pawn's bounded, typed Actor.Timers array
and rejects any timer targeting that thrower (including paused/unknown-name
entries). It does not cancel timers or discard a grenade; unrelated timers
remain untouched. Actor.TimerData's TimerObj/FuncName/Rate/Count/bLoop fields
are independently checked against Engine.xxx.

The new suite covers 4,000 base/four-derived forward/reverse cycles, exact
cache-only byte changes, already-updated idempotence, changed controller roles
between stages, stale/wrong typed data, offsets, wrong thread, protected memory,
seven dirty postconditions and failures after a GC-aware publication. Grenade
cases additionally test foreign owners/outers, pending versus unrelated timers,
malformed timer arrays, partial two-cache rollback and control/identity/timer
changes between the two writes. Three native store instructions are pinned
against TWFC.exe. Reflection/control/GC are fixtures; the actual shared writer
has a separate suite. This is not gameplay proof or a complete role update.

### Initial heat setup trace (same pending weapon-role binding)

The already-retained Core.xxx was decompressed locally with the existing
`.codex-temp/tools/decompress_ue3_lzo.py` (`--compression-offset 0x61`) into
`.research/transformers-wfc/decompressed/Core.xxx` (280,390 bytes). No device
download was needed. Core.Object.Clone is an actual **native function**, flags
00020400/native token 0, returning Object and accepting optional InOuter/Object
and InName/Name. Its cooked signature now joins the verification suite.

Retained SetHeatProperties `109EF310..109EF41C` is stdcall with four arguments:
heat service +10, properties pointer, pawn +10 (None=10), and RunModifiers. It
sets owner/max, clears the array, calls `116ED490` Clone for every template,
appends through `10A6F890`, and assigns each clone's WeaponOwner at raw +28.
Unlike ResetHeatProperties, this body does **not** enter Cool. Clone receives
raw source/outer objects and an optional packed name, returning a raw UObject;
its complete wrapper is `116ED490..116ED50E` (RET8), with native duplication at
`116ED390` and normal heap cleanup. Neither routine was enabled or called by
the new cache port. Initialization of an empty AI modifier list and durable
retention across AI/player tuning were still required at the end of that cache
batch; arbitrary clones or raw object-array copies are not a substitute.

### Guarded first-time modifier initialization (same item 1)

The subsequent source batch adds `NativeBeginWeaponModifierInitialization` and
`NativeFinishWeaponModifierInitialization`, backed by an engine-owned clone-array
transaction in `UnrealObjectClone.inc`. This completes the isolated initializer,
**not its connection to the still-unported SetWeaponDataType replacement**.
The existing reflected role gate remains in place; no new game-thread polling,
input handling, pawn spawning, UI changes or runtime hook activation was added.

- The shipping Clone wrapper `116ED490..116ED50E` receives raw UObject in ECX,
  raw Outer and four-byte FName(None) on the stack, and returns raw UObject with
  RET8. It is not a pawn/possession operation. The fastcall declaration reserves
  unused EDX to match that exact ABI without a script-base adjustment.
- Clone and allocation `1091F100..1091F1DB` have complete-body signature gates.
  The test independently checks every absolute operand against the PE base
  relocation directory (length `243ABC`, not the padded section length).
  The actual object duplication and heap backend are not recreated by the mod.
- Begin accepts only an empty, typed, writable, live ObjectProperty array.
  It allocates a separate bounded engine block and retains the previous empty
  backing block for rollback. Clones are attached with the heat service as
  Outer; each pointer/count is published and the native GC barrier runs before
  another clone or initializer call. Only a complete batch can commit.
- Partial failure releases only the transaction's exact backing allocation.
  Removed modifier UObjects are left to WFC's garbage collector, never manually
  deleted. Changed headers, outers or object indices reject cleanup instead of
  freeing another transition's array. A failed cleanup remains pending in the
  caller-owned journal. Pending journals must not be copied or cross a pulse.
- Weapon initialization reads that weapon's actual PlayerData. Only the five
  known concrete heat-modifier schemas are accepted; templates must have no
  live WeaponOwner. Their tuning, transient defaults, object identity and array
  remain checked across every allocating/initializing call. Read-only templates
  are supported; writes target the newly rooted clones only.
- Initialize reproduces the cooked leaf and compiled store at `109EF400`:
  assign the pawn to each clone's WeaponOwner with the shared GC-aware writer.
  Already-initialized compatible modifiers retain their exact objects, timers
  and event flags. Mismatched/unknown configurations reject without replacement.
- The original initializer did not change HeatMax. The later capacity/clone
  composition below now handles the empty, cold AI bootstrap. It still does
  not assign Data/role, change CurrentHeat/state, or call ResetHeatProperties/
  UpdateData. The complete role update must retain the initialized array on
  later AI/player transitions; arbitrary capacity changes remain rejected.

The lookup suite adds 2,000 clone-array commit/rollback batches, capacity bounds,
allocation failure, null/wrong-class/foreign-outer/duplicate clone returns,
modified array headers/indices, wrong-thread/reentrant entry, invalid metadata,
and reuse of an existing empty backing allocation. The modifier suite adds
2,000 initialization/rollback/commit cycles (5 and 64 entries), idempotent live
modifier preservation, six injected failures, template mutation, changed
controller/ownership, read-only templates and fail-closed recovery. These run
production transaction code with metadata/control/clone/heap fixtures; they do
not run an actual game duplication graph, live GC, weapon firing or possession.

### Direct fire-rate tuning in the existing role journal (same item 1)

`NativeRestoreWeaponProgress` no longer assumes that UpdateData first rebuilt
the lerper. Forward application resolves the actual target Data's nested
FireIntervalModifier/IntervalRange and reads Min, Max and RampTime. The retained
InitializeFireInterval instructions at `109E7301`, `109E7307`, `109E7322` prove
the raw Data offsets `11C/120/124`; typed live metadata, exact structure sizes
and matching raw values must agree before any write. Invalid/non-finite/negative
configuration rejects without mutation; decreasing interval ranges are valid.
Templates remain read-only and the existing lerper remains owned by the weapon.

The forward operation writes the new interval endpoints/duration and the saved
elapsed fraction together, retaining spread. Rollback writes all four original
lerper values and original spread exactly. The original UObject, controller,
Instigator, world and target Data identities are rechecked afterward, along with
the unchanged tuning source. No game function calls, allocation, inventory rebuilding
or weapon/actor cloning is needed to apply these scalar values.

GameBridge now applies this tuning before heat callbacks and retains its final
post-callback restoration in both forward and reverse paths. Cooked
WeaponOverheated enters Overheated; BeginState calls OwnerAnimator, weapon events
and the heat modifiers. WeaponCooled can run an animator completion delegate.
Those callbacks should see correct tuning on entry without being allowed to
erase the preserved final progress afterward. The actual SetWeaponDataType
reflection gate is deliberately still present until the full custom role setter
is ready; the first-time modifier initializer is not yet attached to that setter.

The 3,000-cycle progress suite now alternates with/without a preceding mock
UpdateData reset, tests post-callback progress restoration, checks the three
additional executable instructions and rejects changed tuning during a write.
It includes malformed nested layouts, NaN/infinity/negative inputs, read-only
templates, zero-duration ramps and eight dirty postconditions. The shared
float-field reader separately exercises real typed metadata and size/offset/
alignment/dimension checks. All are local fixture tests, not gameplay evidence.

Further existing UpdateData intent traced in this pass (not additional features):
InitializeOwnerAnimator immediately returns when the weapon already has one;
otherwise it selects robot/vehicle animator services and clones OwnerAnimations.
SetPickupMeshes clones from WeaponMeshTemplate rather than rebuilding inventory.
InitializeFromInstigator can also recompute AmmoIncrease: the retained cooked
skill implementations return zero (base), one (ExtraGrenade), or twice the
weapon's maximum clip count (IncreaseAmmoFromClips). The remaining role setter
must explicitly account for those paths instead of blindly replaying the
construction/reset routine or assuming every weapon has identical capacities.

### Native role/data assignment leaf and cold-AI preparation (same item 1)

`NativeWeaponRoleData` now reproduces the assignment portion of the shipping
role setter without invoking its destructive UpdateData tail. The complete
`109E6500..109E6514` and `117A4430..117A4474` bodies gate the layout. The latter's
two absolute GC operands are relocated at byte offsets 24 and 47; every byte in
both signatures is tested for corruption rejection against the retained EXE.

The custom leaf preserves the setter's ordering: OR bit 3 at raw weapon +30,
assign WeaponDataType at raw +329, then publish the Data reference at raw +240
with the existing checked native GC writer. The role byte uses bounded packed
CAS to preserve its neighboring fields. The native dirty bit remains set on
rollback, as another stock setter call would leave it. It never calls the
virtual UpdateData slot at script-table +228, initializes inventory, clones a
pawn, changes control, or resets ammo/heat/animation state.

Read-only capture is integrated into the existing inventory journal. It checks
the actual PlayerData/AiData roots, both tuning identities, exact field metadata,
the existing weapon/lerper/world and valid standalone control graph. The journal
comparison includes this capture, so stale desired-data roots cannot silently
survive between capture and the handoff boundary. **The mutation leaf is not yet
called by GameBridge**: its SetWeaponDataType gate remains until the complete
replacement integrates heat initialization/retention and owner animation.
The skill-capacity path is now connected below; this is not an authorization
to bypass the remaining work.

The leaf rejects stale or unrelated role/data values. If an interrupted write
leaves one field old and the other new, rollback accepts only the two values
owned by its journal and restores the prior pair. Identity/control/source or
runtime-progress changes during publication report a dirty failure. Repeated
successful calls are idempotent. No hooks or per-frame loops were added.

The progress suite adds 3,000 role/data round trips, neighboring-bit and
non-target byte preservation, native store ordering, five injected failures,
both mixed-field recovery paths, stale roots, protected memory, wrong-thread
and recursive requests. The actual assignment code runs with metadata and the
shared reference writer mocked; that real writer is covered by the resolver
suite. These are not complete UpdateData or actual gameplay tests.

A separate existing preflight mismatch was corrected: PrepareNativeWeaponHeat
formerly required positive HeatMax even for AI-owned services awaiting first
player setup. It now admits only an AI-controlled, empty-modifier, Cool service
with HeatMax=CurrentHeat=0 for **read-only binding preparation**. Sync still
rejects zero capacity before calling native clamp/state code. Nonzero heat,
existing modifiers, Overheated state or player ownership do not pass that new
exception. Tests confirm no service bytes or callbacks change during preparation.

### Skill-derived ammo capacity (same item 1)

`NativeWeaponAmmo` now connects the custom capacity update to inventory capture,
per-weapon mutation, post-callback verification and reverse-order rollback.
The actual base getter was found in **HM_Engine.HmWeapon**, not Engine.Weapon:
GetMaxAmmoClipCount reads HmWeaponData(Data).MaxAmmoClipCount, and GetMaxAmmoCount
reads MaxAmmoCount. TnWeapon adds AmmoIncrease to the latter. No additional
device data was needed.

Capture reads the real IncreaseAmmoSkill class, IsEffectedByIncreaseAmmoSkill,
_CurrentCharacter.Skills class array, target data capacities and live reserve/
magazine/bonus values. HasSkill is exact array membership, not a physical pawn
type check or a skill-instance lookup. The three verified static implementations
produce zero, one, or twice the authored maximum magazine size. Only exact
registered TransGame class identities are accepted; unknown active overrides
fail closed. A missing skill, disabled effect or null holstered Instigator
preserves the existing bonus, matching the original conditional assignment.

Only AmmoIncrease is written. Existing reserve and magazine counts are never
refilled, clamped to the new maximum or reassigned, including reserve already
above the new capacity. The original bonus is restored on rollback. Capture is
refreshed at each weapon's mutation boundary so ammo changes during AI drain
are not rewound. A changed magazine/reserve during the mutation, skill identity/
array changes, target tuning changes, integer overflow, stale control/world or
recursive/off-thread requests are rejected or reported as dirty failures.

The new shared integer CAS validates a scalar aligned IntProperty, live game
thread and current writable page protection (not cached Readable permissions).
It has resolver tests for expected-value mismatch, wrong type/size/dimension/
offset, wrong thread and protected pages, including read-cache priming followed
by a read-only protection change. Existing generic integer writers are unchanged.

The progress suite executes 3,000 actual custom capacity/rollback cycles across
all three supported skill implementations, equipped/holstered cases, repeated
calls, and eight injected failures. Metadata/control and integer publication
are fixtures there; the actual shared CAS runs in the resolver suite. Seven
additional cooked contracts pin HasSkill, initialization and both HmWeapon
getters plus the three skill bodies. These checks are not gameplay evidence.

The SetWeaponDataType reflected gate and old mutation calls are still present:
the complete custom role update must connect heat bootstrap/retention and owner
animation before replacing those calls. This capacity integration alone cannot
make swapping or the native class menu operational.

### Empty AI heat capacity and clone rollback composition (same item 1)

The existing modifier initializer now composes authored heat capacity setup
with the rooted modifier-array transaction. The retained SetHeatProperties
assigns HeatMax and clears/clones modifiers, while ResetHeatProperties also
forces Cool. Neither reset body is called. TnHeatModifier.Initialize assigns
WeaponOwner only; the existing GC-aware initializer preserves that behavior.

`NativeSeedWeaponHeatCapacity` validates the complete existing heat bindings,
real PlayerData and its positive finite HeatMax, exact native/reflected heat
layout, reciprocal standalone AI control, this pawn's heat ownership, Cool
state, zero CurrentHeat and an empty modifier array. It permits zero capacity
or the already-matching authored capacity, never a different live maximum.
An atomic scalar assignment changes only HeatMax at raw +2C. Postchecks cover
the owner/control/world/object identities, state frame, arrays/delegates and
unchanged source tuning. A second full snapshot prevents a late metadata read
from hiding an ownership/current-heat change inside a mixed old/new view.
No state transitions, observer dispatch, actor or
modifier resets occur. This does not initialize a null/foreign heat owner or
relax the guards for a hot, player-controlled or modifier-bearing service.

Begin records capacity rollback ownership before entering the potentially
dirty assignment. It then uses the existing clone allocator and immediately
roots/initializes each modifier. Cold state and zero current heat are checked
across clone/GC boundaries too, allowing the newly rooted array but refusing
to reset a heat event just to make setup or rollback pass.
Failure restores the original array first,
then reverses only a journal-owned zero-to-authored capacity seed. A failed
capacity undo keeps its journal even if clone-array rollback already succeeded;
another Begin cannot erase it. This also covers capacity-only setup with no
modifier templates. Live compatible modifiers/capacity are retained, while
mismatched capacities and zero-capacity modifier templates reject.

Finish now validates the original actor/control/Data boundary on commit and
rollback, including a no-allocation initialization. Initialization remains a
single-pulse operation **before control transfer**, not an asynchronous owner
of copied clone journals. The full GameBridge role update still does not call
this initializer; its lifecycle/retention connection and owner-animation work
remain necessary before replacing SetWeaponDataType. No preflight gate was
bypassed and no feature was enabled for gameplay.

The heat suite adds 2,000 production capacity seed/undo cycles, exact unrelated
byte preservation and zero callback/transition assertions, seven injected dirty
postconditions, signature corruption and state/ownership/source/thread guards.
The existing 2,000 modifier initialization batches now alternate zero/already-
positive capacity, commit/rollback and 5/64 clones. Additional fixtures cover
failure after seeding, rollback failure after array restoration, retry without
losing the remaining seed journal, seed-only setup and stale no-op commit.
The heat leaf runs against real signature/state-layout validation with metadata
fixtures; clone/capacity composition uses allocator/capacity fixtures. These are
local tests, not proof of live control transfer or checkpoint compatibility.

The same four integration areas remain below; this is progress inside item 1,
not an additional feature or a claim that the class flow is ready to test.

1. Finish the remaining swap call contracts with signature-gated native method
   bindings. Shooter, planner, movement cancellation/stopping/delegates, player
   services, input guards, camera owner/form handoff and native view-target
   transfer, dialog-owner initializers, point-of-interest reset and collision
   role changes are ported, but inventory role methods still
   need bindings. Three later journal dependencies (ClearSolution and immediate
   action/goal setters) plus the required stopped-plan drain are also ported.
   Both controller-detachment dependencies and custom reciprocal player/AI
   ownership, acknowledgment and basic AI owner services are now integrated.
   Custom controller form entry/exit and single camera activation are integrated
   in the transaction, but do not adapt ongoing player/AI role consumers.
   Supported ability HUD restoration is now integrated; unknown buff/condition
   overrides still reject and need explicit native verification to expand support.
   State-specific input cleanup is integrated for verified native-compatible
   pairs; its physical-AI walking service adaptation remains in item 2. The
   AI aiming is now integrated for the current native aimer. The
   immediate preflight now has one unported reflected function type left:
   SetWeaponDataType. SyncCurrentHeat has a native binding, but the preceding
   role-update path still needs preservation-safe integration. Fire-rate ramp and
   spread preservation are integrated in that journal, not a completed role setter.
   Skill-derived capacity and exact ammo/magazine preservation checks are now
   integrated in the same journal; the owner-animation/bootstrap paths remain.
   Same-object modifier timer/flag preservation is also integrated; modifier
   initialization/retention across AI/player tuning remains in this same item.
   Derived tuning caches (including the grenade bag/thrower pair) are now
   integrated in the journal; the destructive base update remains unported.
   The native component collision calls
   are separately audited by their guarded wrapper. This count
   excludes later journal dependencies and the role/menu/persistence work; it
   is not a total remaining-work estimate.
   Sensor, vehicle-AI strategy and identity journal
   dependencies remain. Full native cross-role controller/service handoff is NOT
   complete. Do not bypass
   failed preflight checks.
2. Replace the twenty cooked-bytecode cast patches and compiled-direct role
   hooks with verified native control/AI/camera adaptations. The current role
   proxy cannot be declared equivalent to native squad AI.
3. Replace non-exported class/loadout/AI-spawn methods with native bindings and
   hook actual compiled call paths, not merely reflected executor slots.
   The real chapter entry is now connected, but this does **not** make the
   entire selector operational.
4. Finish durable configuration/GC ownership/checkpoint integration described
   in ARCHITECTURE_AUDIT.md, then test actual existing-actor transfers and native
   top-screen menus in gameplay when device testing is authorized.

These are integration requirements, not permission to replace character IDs,
spawn copies, alter native class identities or suppress safety validation.

## Local verification

`build.ps1 -Configuration Release -SkipLauncherPayloadUpdate` runs twenty-three native
suites and builds only dinput8.dll. Extended resolver tests execute the actual
x86 context thunks 3,000 times, preserve the four-word checkpoint delegate through
frame capture/replay, reject stripped executors, and validate Script boundaries.
NativeCampaignTravelTests executes a real EAX-this fixture through the production
hook/trampoline, with 1,000 deferred requests, re-entry/incorrect-thread guards,
native null sentinel, pass-through and orderly stopping cases.
CharacterSwapTests also covers delayed rollback after both dirty guard-release
failure and final graph-check failure: fifty waiting ticks without repeated
guard acquisition or ownership mutation, rapid-tap serialization, resumed
reverse-order cleanup, rejected readiness, timeout, clock regression, and no
recovery/retry from an incomplete unsafe rollback. Existing mutation-by-mutation
failure and repeated A/B/C transaction tests still pass.

NativeWeaponShooterTests also exercises NativeAiPlanning: 3,000 firing/stop/
activation cycles, 3,000 executions of the exact shipping Deactivate body,
3,000 planner suspension/resume cycles, all ten shooter and five planner table
bindings, complete delegate argument transfer, callback re-entry, wrong-thread
calls, null/stale actors, unknown/modified tables, owner mismatch, pending
suspension and authority-guard postcondition failure. It executes the exact
controller Suspend/Resume bodies against synthetic runner states. Signatures
and fifty-five table entries are independently compared to the retained TWFC.exe.
The lifecycle tests additionally execute 3,000 cycles through the complete
shipping ClearSolution, EDX PumpQueue and both ESI setter bodies, with synthetic
nested Bind/Unbind, GC barrier and state callbacks. They reproduce ignored
suspended clears and verify asynchronous stopping, no pending-plan activation,
compiled None and owner arguments, null detachment, goal-index reset, callback
re-entry, pending replacement rejection, incompatible physical pawn rejection,
stale plans/invalid arrays, modified vtables/ASLR operands and mutation-aware
failure when a callback unexpectedly queues work. These do not execute native
GOAP actions or establish a functioning player-control transfer.
Synthetic state fixtures do not execute the full game or establish gameplay
compatibility. The mod DLL was compiled locally with launcher payload updates
skipped; no APK or deployed/published payload was changed.

The replanning tests execute 15,000 calls through the shipping controller,
static scheduler entry, SubmitRequest and Find/Add/Remove bodies, with fixture
class lookup/cast and nested heap/memmove helpers. They check actual UObject
queue entries (not +10 pointers), duplicate suppression, low-to-high promotion,
no demotion, initial queue allocation, inactive/suspended rejection, corrupt
headers, stale world/engine pointers, wrong thread, callback re-entry, preserved
authority guard, and acceptance failure after a native call. All six new
signature bodies/ASLR operands match the retained executable.

NativeAiMovementTests executes 6,000 moving-state cancellations through both
shipping CancelMove and PoppedState bodies with a synthetic PopState/GC layer,
checks all eight controller tables against the executable, verifies repeated
cancellation, every individual delegate word, invalid/missing state stacks,
modified vtables, no-pop and stale-callback failures, network guard and re-entry.
It does not execute the engine's real state-stack machinery. The real resolver
fixture also verifies the full object delegate read and bounded larger scheduler
array reads. The cooked-package check now covers nineteen call signatures,
twenty cast sites and forty-four behavioral contracts, including the actual
possession/restart/input-reset/form-selection chain, vehicle input cleanup and
six pawn/weapon/forced-selection contracts and four acknowledgment/eye-height
contracts from the latest trace.

NativePlayerServicesTests executes 12,000 calls through the complete shipping
pickup/fine-aim bodies and collector barrier, plus 6,000 targeting-owner writes
using a writer test double. It checks all instructions/ASLR operands and both
OnSetPawn call sites against TWFC.exe, standalone owner/world/service identity,
optional compiled None, all collector phase/reference-mark branches, unchanged
pawn bytes, wrong thread, re-entry and mutation-aware postcondition failures.
The separate actual-resolver suite tests GC notification ordering, failed
compare-and-swap, clearing/reassignment, malformed property layouts, protected
pages, missing barrier and wrong-thread rejection. These are local fixtures,
not game-session tests or proof of completed character control/class menus.

NativePlayerInputTests executes 26,400 counter calls and 3,000 hooked resets
through complete shipping bodies, using only a fixture class-default resolver
inside the reset. It checks every legal counter baseline, both release orders,
duplicates, copied leases, native overflow, resets (including same-value reset),
foreign increments, stale controller/world indices, travel, reentry, wrong
thread, rejected-versus-executed failures, normal reset pass-through and teardown.
The production observer/trampoline executes in the fixture, with FXSAVE snapshots
verifying preserved floating-point/SIMD state. Every byte of all three signatures
is checked against the executable and tested for corruption rejection. The real
reflection suite separately validates packed ByteProperty offset/type/size/dim.

NativeControllerDetachTests executes 12,000 detaches through the shipping player,
AI and pawn bodies, ClearInteractState, saved-move cleanup, observer Find/Remove
and GC barrier. Fixtures replace world movement, SetOwner bookkeeping, class
lookup/cast, interaction setters, camera and nested array allocation. Both physical
pawn/controller-role combinations, callback dispatch, absent/first/middle/last
observers, full delegate preservation, duplicate/corrupt callbacks, inaccessible
or read-only arrays, stale identity/worlds, invalid camera/interaction, re-entry,
wrong thread, repeat requests and dirty postcondition failures are covered.
Pose/form-range sentinel bytes and input counters remain unchanged. All eleven
complete signatures and three pawn vtable slots match the retained executable.
The actual reflection suite verifies typed delegate metadata and an eight-byte
readable/page-boundary fixture which the old pointer-array reader would accept.
These tests do not execute the complete native world/AI/camera or establish a
successful in-game swap.

The expanded NativeControllerDetachTests also executes 6,000 pawn ownership
bindings plus 12,000 native detach/cleanup calls across both physical pawn types
and both controller roles. Seven ownership/weapon/GC bodies and the inventory
slot match TWFC.exe; every signature byte is tested for corruption rejection.
Fixtures preserve ammo/form/pose sentinels, reject ownership mismatches,
Inactive/ancestor states, bomb/flag weapons, pending selection, bad clocks/world,
modified callbacks, reentry, wrong thread and dirty postconditions. Native
SetOwner/SetRotation, defaults, classes, state queries and world callbacks are
fixture substitutes. Only the fixture publishes reciprocal Controller.Pawn to
exercise cleanup; production does not claim that a pointer assignment completes
the player/AI transfer. No controller Restart executes in these tests.

The actual resolver tests now additionally check packed class-menu flag writes
for all 32 mask bits against three unrelated-bit patterns, expected-value
mismatches, no-op writes, invalid masks/layouts, missing game-thread identity,
wrong thread, null/missing properties and protected pages (including cached read
permission after protection is downgraded). They run without a GC barrier and
verify unchanged adjacent flags on both successful and rejected operations.

The expanded ownership fixture now executes another 3,000 custom reciprocal
player adoptions and 9,000 native detach calls using the complete shipping local/
server acknowledgment and both eye-height/math branches. It checks both physical
pawn layouts, no equipped weapon, crouching, pose/form/ammo sentinels, duplicate
requests, native dirty publication and held input guards. It rejects invalid
camera/local player/PRI/blueprint/authority, spectator/stasis/tear-off flags, bad
floats/offsets/virtuals, wrong thread and reentry. Failed pointer publication,
failed native LocalPlayer cast, input invalidation during acknowledgment and
identity mutation report dirty failure instead of success. ResetTimeMargin,
ResetPlayerMovementInput, ClientRestart and PlayerController.Possess are UD2
traps in this fixture, proving they are not entered. Seven complete bodies and
four virtual entries are independently compared to the retained executable;
each signature byte and both native constants are corruption-tested. Input
leases and the shared reference writer are test doubles here, independently
covered by their actual implementation suites. These are not gameplay tests.

The AI-adoption fixture adds 3,000 same-actor round trips, totaling 6,000 custom
AI and 6,000 custom player adoptions. It executes shipping pawn callbacks,
aimer initialization, complete observer Find/Append/Remove and GC notification.
It checks both physical pawn directions, optional AI PRI, unchanged pose/form/
ammo sentinels, range preservation, native capacity growth, first/middle/last/
absent observers, no duplicate insertion, wrong layouts/types/owners/worlds,
invalidated input, pending plan/shooter work, reentry, wrong thread and dirty
failures. Partial icon/aimer/shooter ownership publication can restore using only
the same two journal actors. All five complete new helper signatures match the
retained executable; every byte is corruption-tested. Full AI Possess and base
Controller.Possess join the UD2 forbidden-call traps. Engine array allocation,
type lookup, input leases and planner/shooter readiness are fixtures here; actual
readiness methods have separate tests including all ten shooter states, all 32
notification bytes, native planner stopping/draining, pending replacements and
nested mutation. This is not a live AI/player/camera or checkpoint test.

The cooked verification script now passes 64 behavioral contracts, including
the full AI setup, AI-only getter cast, owner initializers, eight-entry range
array, observer AddUnique and shooter callback-reset semantics.

NativeCameraHandoffTests executes 6,000 repeated native owner/form/state handoffs
plus every cross-vehicle/mode combination, same-owner re-entry and partial-None
recovery. Twelve complete code ranges (including both switch tables) and seven
StartForm table slots match the retained EXE; each signature byte/slot is tested
for corruption rejection. Actual InitializeForms, StartForm, GotoState dispatch,
typed cast wrappers/assignments, GetStrategy/set_CameraStrategy and GC run.
Class lookup, state callbacks, deep camera activation and the shared reference
writer/input leases are fixtures. Pawn bytes remain unchanged in these fixtures.
Missing/ambiguous strategies, unsupported virtuals/states/forms, stale worlds,
invalid identity/ownership/acknowledgment, transformation, input loss, wrong
thread, re-entry and dirty failures are covered. Old owner-setter and template
cloning entry points are UD2 traps. No claim of rendered camera continuity,
actual possession, checkpoint compatibility or in-game stability follows.

NativePlayerFormStateTests executes 4,000 guarded exits through the shipping
GotoState helper and all four EndState bodies. It checks old-pair callback
ordering, unchanged pawn bytes, same-neutral idempotence, both fixture pawn
types, stale form aliases/ownership/worlds, state-stack restrictions, modified
signatures/virtuals, protected state frames, wrong thread, nested requests and
dirty identity/input failures. UObject state dispatch, deep input/weapon/timer
callbacks and input leases are fixtures. Complete body bytes and every-byte
signature corruption rejection are checked against the retained executable;
two additional build checks pin the unsafe native cast/dereference sites.
That exit batch added controller form entry/exit contracts and the physical
player-only getter. The current cooked check passes 93 behavior contracts,
including input profiles, native form statistics/delegate ownership and the
ClientGotoState same-state/label path (19 call signatures and 20 cast sites).

Camera fixtures additionally exercise all 64 cross-vehicle/mode combinations
through staging followed by a controller-camera-call fixture. Staging performs
zero activations; entry performs exactly one, including same-form re-entry and
recovery after a failed None-state transition. This is not execution of the
complete controller BeginState and not an in-game camera/physics/AI test.

The expanded form suite executes another 4,000 custom entries and 4,000 exits
through all eight actual campaign thunks and the production inline hooks. Real
native profile getters/boolean lookup, saved moves and SetForm bodies execute;
engine state dispatch, physics/camera callbacks, class/string helpers and input/
reference services are fixtures. It checks all complete code ranges against
TWFC.exe, each corrupted byte, installed patch/trampoline corruption, exact
delegate layout, stale aliases, packed input bits, repeated admission/entry,
wrong thread, held boost, unsupported physics, invalid profiles/statistics,
callback input loss and normal no-transaction pass-through. Separate camera
tests cover the shared read-only form classifier for all four vehicle types,
both modes, robot-only pawns, transformation and wrong-thread rejection.
These are source/native-ABI regressions, not a live combined swap or class test.

The player-service suite now adds 12,000 buff-HUD attach/clear operations across
3,000 existing-manager round trips. All manager bytes except OwnerHud remain
unchanged, with the original managers/HUD restored on return. Its metadata and
shared writer are fixtures (the real writer/GC implementation has a separate
suite); this is not a live buff rendering test. Forty injected post-call failures
cover changed control, acknowledgment, world, service ownership, pawn/controller/
world/manager identity, deletion and authority across four service operations.
Additional cases reject foreign HUDs, wrong manager owners, AI HUD attachment,
read-only native-setter destinations and off-thread metadata access. Existing
12,000 exact-body pickup/fine-aim and 6,000 targeting tests still run. Transaction
tests separately cover rollback after every dirty/clean mutation-stage failure;
they do not execute the full GameBridge or the game's callbacks.

NativeAbilityHudTests executes 3,000 restoration batches through the two complete
shipping HUD bodies, plus waiting/paused/empty-slot cases. It verifies exact float
argument order/type, callee stack cleanup, unchanged pawn/HUD bytes and balanced
temporary allocation/destruction. Each signature byte is corruption-tested and
both complete ranges match the retained EXE. Wrong thread, malformed commands,
missing Scaleform/movie/callback, invalid roles/worlds/acknowledgment, lost input
leases, recursive dispatch, six dirty callbacks and caller batch mutation are
covered. Array helpers, Scaleform Invoke and metadata are fixtures: this does not
test a rendered native HUD, live effective-value reader or integrated swap.

Latest local-only DLL SHA256:
`d807ccbe79f3a05f4f2648d2899d209623128c9db5a70bc1c07e14093095dad9`.
All 25 local test suites, 216 cooked behavior contracts (plus 22 call signatures
and 20 role cast sites), and the DLL-only build passed with
`-Configuration Release -SkipLauncherPayloadUpdate`; no APK build ran.
The heat and modifier suites were also rebuilt/rerun against the final
clone-boundary cold-state guard after the full build's earlier test compilation.
The launcher manifest still identifies payload DLL `9360fb79a295...`; it was
not updated, published, installed or presented as containing this batch.

Build checks also compare chapter-entry/callsite, context adjustment and delegate
copy instructions against the retained executable. These checks establish ABI
and local logic, **not** a successful swap, completed class flow or in-game
stability/performance result. No new test was requested from the user.
