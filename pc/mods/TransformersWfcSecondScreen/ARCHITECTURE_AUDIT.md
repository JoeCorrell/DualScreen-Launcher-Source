# WFC custom squad/control/loadout audit — 2026-09-04

## Status and scope

September 6 continuation: the current checkpoint and remaining work are recorded
at the top of `NATIVE_SCRIPT_MIGRATION.md`. Downed/death, firing/melee, controller
motion, fine-aim, targeting, camera, HUD and owner consumers now have scoped native adapters and
executable-backed local tests. This removes the corresponding ineffective cooked
patches, not the remaining normal-AI, weapon-bootstrap, class-menu and persistence
requirements. The older batch counts below are historical. No gameplay completion,
publication or device test is implied.

The 2026-09-05 form-state follow-up now exits the old controller state before
detachment, records even an early failed detach as requiring form restoration,
and keeps guards held on unsafe exit/restore. Camera setup is staged so native
controller BeginState owns its single StartForm activation, rather than having
the mod activate twice. New exact-body exit tests and expanded staged-camera
fixtures pass, along with all fifteen suites and 77 cooked behavior contracts.
The trace also confirms an unguarded physical TnPlayerPawn cast in vehicle
form-name selection and a separately inlined cast in vehicle BeginState. These
entry/role consumers remain to be ported; switching and the class system are
not finished or enabled by the exit/camera changes. See the detailed section in
NATIVE_SCRIPT_MIGRATION.md. No APK, deployment or device testing occurred.

Update: `NATIVE_SCRIPT_MIGRATION.md` supersedes the unresolved lookup hypothesis
below. The shipping executable compiles and strips these methods; cooked
bytecode and UFunction executor hooks are not the live control path for many
of them. It records the actual native chapter-entry correction, additional ABI
fixes and the still-required native role/loadout port.

The player-service source batch replaces pickup/fine-aim initialization with verified
compiled bodies, implements targeting's plain setter with the game's GC barrier,
and corrects GC notification in the shared object-reference writer. See the
player-service section in NATIVE_SCRIPT_MIGRATION.md for exact evidence, tests
and remaining restrictions. This does not complete possession or class menus.

The subsequent input batch replaces stripped IgnoreMove/LookInput calls with
native owned counter leases and observes native resets without changing their
behavior. It also establishes that full PlayerController.Possess enters a
spawn/restart path which clears input locks, selects StartingForm, resets pawn
motion and fires spawn/touch events. The unfinished live handoff must not simply
call that full path once lookup is repaired. See the input/spawn section in
NATIVE_SCRIPT_MIGRATION.md and its local native-body tests; no device test or
working swap/class-flow claim has been made.

The controller-detachment batch now calls the verified native player/AI
UnPossess bodies, including dynamic pawn cleanup, saved-move/camera handling
and complete enemy-observer removal. It adds typed 16-byte delegate-array
validation and stops rollback ownership mutations immediately on a failed
restore. It also proves base Controller.Possess still dispatches Restart, so
that is not a safe alternative to player possession. See the detachment section
in NATIVE_SCRIPT_MIGRATION.md. Adoption, native cross-role services and the
class/loadout flow remain incomplete; these local tests do not enable them.

The pawn-ownership follow-up traces the native equipped-weapon selection path
and binds PossessedBy without the controller's spawn/restart lifecycle. It admits
the same-equipped non-Inactive weapon branch and preserves actual pawn/inventory
identity. The next player-adoption batch now adds reciprocal Controller.Pawn
publication and native AcknowledgedPawn/eye-height handling, replacing full
player Possess in both bind and rollback while keeping input leases held.
The AI-adoption follow-up also replaces full AI Possess in bind/rollback with
existing-pawn ownership, icon/aimer/shooter rebinding and complete native enemy
observer registration. It avoids the three inlined AI-pawn casts which reject
the physical player actor. Its additional 3,000 same-actor round-trip tests
exercise native helper bodies without Restart. Rollback now stops weapon
callbacks before ownership restoration, with strict native planner/shooter
quiescence checks. Native cross-role services, camera/form/HUD and class/menu/save
integration remain unfinished. See NATIVE_SCRIPT_MIGRATION.md for the corrected
eight-entry RangeSet trace and exact evidence; local tests do not prove gameplay control.
The class-menu `_bIconicMode` writer also now enforces game-thread, scalar mask
and current write-permission checks; this fixes an unsafe write, not the missing
menu/control integration or a log-proven gameplay crash.

The camera follow-up now replaces the stripped owner setter with a guarded
owner/form/StartForm sequence. It removes the earlier unjournaled camera change
inside ordinary service rebinding and avoids activating the old vehicle strategy
on a newly selected pawn. Both forward and rollback stop on a failed camera
step. Local fixtures execute the shipping form/state/strategy-selection primitives
for repeated/cross-vehicle transfers, but model deep camera/engine callbacks.
Controller form-state, view-target, physical role consumers and class/checkpoint
integration still need work. See NATIVE_SCRIPT_MIGRATION.md; no gameplay or
completed class-system claim is made by this camera-only layer.

These are custom campaign features, not dormant native WFC features. This audit
uses the existing mod, the decompressed TransGame/Engine/HM_Engine packages, and
the exact supported TWFC.exe. No agents, device operations, APK build, installer,
or payload publication were used. Existing unrelated project changes are retained.

**Neither live character switching nor the complete class system is verified.**
Local tests exercise adapter logic and synthetic reflected objects, not a running
campaign. Previous architecture notes describing the feature as implemented are
not evidence of completed gameplay support. See SWAP_INVESTIGATION.md for the
previous live failure and unpublished focus/resolver changes.

Executable SHA256:
`1db04b09b36094571fab861cd01ca175d44d610b5417f950f37523cd97eeb016`.
Decompiler: `.research/transformers-wfc/tools/WfcScriptDump`.
Packages: `.research/transformers-wfc/decompressed`.

## Control transfer: observed chain, not a hypothetical native switch

1. `Panel.cpp::QueueAction` captures slot, pawn object index, character hash,
   player/controller identity and world epoch. This panel is native in-process;
   this part of the path is **not an Android IPC possession command**.
2. `StateStore.cpp::QueueCommand` owns the synchronized queue and duplicate
   suppression. `GameBridge.cpp::ProcessOneQueuedCommand/ExecuteCommand` consumes
   it on the game thread and validates snapshot age/identity/action cadence.
3. `SwitchSquadMember` starts `CharacterSwapTransaction`, closes command admission
   and discards other commands. Stable preflight validates the native squad record,
   reciprocal controller/pawn links, live identities, form, health, team, UI and
   scripted restrictions. Combat itself is not an unconditional rejection.
4. `CaptureJournal/ValidateSwapCallContracts` verifies services and callable
   signatures before any possession. **The retained .12 live test stopped here:**
   50 call lookups returned null, then the transaction rejected the request. The
   observed freeze was metadata work before possession, not proof that a possession
   had occurred and failed. Raw lookup diagnostics and bounded owner discovery are
   prepared, but the live null-lookup cause has not been conclusively established.
5. Commit detaches both existing controllers, calls audited possession functions,
   updates the squad record and current PRI identity, rebinds services, camera,
   collision/HUD/weapon roles and form state, then verifies the graph before UI
   publication and reopening input. No actor spawn/appearance-copy substitutes for
   this transfer. Rollback is serialized and restores the journal in reverse.

### What possession actually does

- `TnPlayerController.Possess` (identified by exact owner/name rather than an
  inferred SDK offset) calls `PlayerController.Possess`, then `OnSetPawn`, pickup
  timer, spectate/damage-history setup and resets controller flags, including
  bGodMode. This is a controller lifecycle function, **not a squad-swap API**.
- `TnPlayerController.OnSetPawn` casts the pawn to **TnPlayerPawn**, initializes
  pickup and fine-aim managers, resets HUD/point-of-interest state and touches
  killstreak state. The cast adapters are needed because an existing squad actor
  is physically a TnAiPawn. Widening a cast does not supply missing subclass fields.
- `TnAiController.Possess` initializes icon, tactical observer, weapon aimer,
  shooter and range services. Owning a different pawn requires rebinding these
  services; changing Controller/Pawn pointers alone is insufficient.
- `TnPickupManager.Initialize` stores a typed TnPlayerPawn owner. Reviewed pickup
  consumers use inventory, touching actors, robot animator services and location;
  further typed consumers still require auditing, not blanket cast bypasses.
- Shooter firing behavior is state-dependent: `Stopped.StartFire` enters Firing;
  firing substates call Owner.StartFire/StopFire; `Firing.IsFiring` returns true.
  A correct signature on the base function alone does not prove active-state
  dispatch. The base resolver at 116C92E0 walks class maps; derived receiver
  virtual dispatch and state overrides must be established with the raw lookup
  evidence. Do not force base functions merely to turn a failed preflight green.

### Remaining control compatibility limits

- The physical former TnPlayerPawn uses the existing **custom role proxy**, with
  native GOAP/vehicle AI suspended to avoid TnAiPawn-only storage accesses. It
  follows/fires through shared services and drives vehicle input setters. This
  is not equivalent to all normal squad AI: authored interactions, revive tasks,
  navigation/vehicle edge cases, goal overrides and mission orders need explicit
  compatibility coverage.
- Twenty bytecode cast locations match the cooked code. That verifies locations,
  **not every downstream read, animation service, camera path or mission**.
- Checkpoints currently restore the physical player-pawn controller graph before
  saving. They do not implement a durable three-character runtime-state save or
  guarantee returning control to the previously selected buddy afterwards.
- Controller flags, killstreak state and mission-specific pawn references require
  further lifecycle coverage. No tested repeated three-way switch exists yet.

## Class/loadout architecture and decompiled evidence

| Native path | Existing custom integration / finding |
| --- | --- |
| TnGameLobbyGame.StartLevel | Detoured to pause campaign travel while native top-screen CharacterSelect/CustomTransformers movies run; not a bottom-screen class menu. |
| TnCharacterScriptBinding.SelectCharacter | Two inputs: CharacterName and **Ignored (int)**. Calls PC.SelectCharacter and LobbyGame.NotifyCharacterSelected. |
| TnPlayerController.SelectCharacter | Replicates selected CharacterData into PRI and clears checkpoint overrides. Must not run before the whole custom squad validates. |
| GetCharacterWeaponTypes (16351) | Inserts melee at display index 2; display CSV is not the actual two-gun inventory array. |
| CommitCharacter (16433) | Edits weapon indices 0/1 **without truncating the rest**. Replaces abilities/skills using Split; Empty clears those arrays. Custom mod intercepts persistence so campaign edits do not overwrite multiplayer customization. |
| ReplicateCharacterData (8732) | Maps IDs through native options. Unknown IDs can be logged and omitted while the call still returns. Empty ability slots produce None; empty skills are skipped. |
| VerifyCharacterData (8756) | Checks chassis/melee and presence of guns, vehicle weapons and grenade bag; it is logging, not a Boolean validation API. |
| TnAiSquad.SpawnAiCharacter | Resolves the mission definition, spawns an existing native pawn/controller pair using ChassisType, initializes AI and runs CheckForSpecialSpawn. Custom selection substitutes definition data before this path. |
| TnAiController.InitializeFromTemplate | Clones definition, applies character, then **destroys inventory again** and initializes planning/vehicle/sensors/buffs/dialogue. A resolved selected loadout is not proof of final buddy inventory. |
| TnCharacterApplier.ApplyWeapons (8685) | Checkpoint overrides replace selected robot/vehicle weapon classes and reserve ammo. Do not reapply initial defaults after a checkpoint or every swap. |
| TnAiSquad.RestartGame (10581) | Clears member controller links and marks members for checkpoint spawning; cached actor identities cannot survive this. |

### Source corrections in this audit

1. Removed loadout truncation that discarded grenade/extra inventory entries.
   Gun and vehicle edits now preserve native tail entries. Melee remains separate.
2. Replaced in-place, shrink-only writes with an all-fields staged transaction on
   **owned native return values**. Ability/skill arrays can grow from zero and
   retain empty slots. Invalid edits cannot leave a partially modified loadout.
   Arrays and strings allocate/free using WFC, not the DLL CRT heap.
3. Verified the array allocator at 1091F100 directly against TWFC.exe. It is called
   by FString assignment at 1090116A, takes old/new counts and element byte size,
   and returns with ret 0x0C. Its prologue and return are signature-gated; ordinary
   bridge functionality remains available if this optional capability is absent.
4. Preparation now verifies resolved chassis/melee, asset validity, array counts,
   empty ability slots and skill-level alignment. A native function returning
   successfully with omitted assets no longer makes the configuration ready.
5. All three configurations are resolved **before** the native initial selection
   mutates PRI/checkpoint fields. An exact two-input call re-enters the selector
   with a one-use token; no FFrame is retained across ticks. Native assignment is
   verified before travel is released.
6. Cached resolved assets are revalidated before temporary AI definition writes.
   This reduces stale-pointer exposure; it does not make the cache GC-rooted.
7. Ordinary unmapped checkpoint saves now pass through before custom frame parsing.
   A reflection failure can no longer suppress a stock save when no handoff or
   deferred save exists. Pending/replay/failed-closed requests retain their guards.

### Unresolved class/state requirements (do not label complete)

- `CampaignClassConfiguration` remains process-local. There is no durable
  chapter/profile/checkpoint-keyed configuration store. Restarting the process or
  returning to non-campaign clears custom mapping; name-only mission substitutions
  do not solve that. A compatible sidecar/native save extension is still needed.
- Resolved cached UObject references are not registered as GC roots, and persistent
  native return buffers lack general reflected destruction. Revalidation prevents
  some invalid uses; native ownership/cleanup across travel still needs completion.
- AI template cloning contains an opcode the current dumper cannot decode reliably.
  Do not treat its corrupted output as evidence of deep-copy/instancing semantics.
  Shared template substitution, GC and final AI inventory need further tracing.
- Preserving CharacterName as a mission identity does not preserve every class,
  archetype, cinematic actor binding, dialogue role or scripted ability assumption.
- Chapter-scope identification, checkpoint restore, direct next-chapter travel,
  scripted respawns and cold-save restore need integration tests and state handling.
- Native menu hooks still depend on resolving the common function metadata; their
  existence in source does not prove the top-screen flow appears in-game.

## Verification and required next evidence

`build.ps1 -Configuration Release -SkipLauncherPayloadUpdate` compiles only the
native mod and runs the suites listed in `NATIVE_SCRIPT_MIGRATION.md`. The actual UnrealBridge fixture covers array
growth, preserved grenade tail, sparse/empty slots, late allocation failure,
invalid metadata, duplicate fields, allocation cleanup and exact selector input
marshalling through the production x86 Invoke wrapper.

`tests/Verify-DecompiledSwapContracts.ps1` checks nineteen critical cooked signatures,
twenty cast sites and the native behavioral contracts against the packages
(the current count is recorded in `NATIVE_SCRIPT_MIGRATION.md`).
These tests are useful regressions, not proof of possession, mission progression,
rendering performance or save integrity on the device.

The shipping native-script stripping/direct-call cause is now established;
see `NATIVE_SCRIPT_MIGRATION.md` for completed native ports and remaining gates.
Controller form exit/entry now handles the actual campaign forwarding thunks,
live robot/vehicle caches, profile input and native statistics, with one camera
activation and guarded rollback. The remaining role consumers and class hooks
must still be ported; this is not evidence of a complete transfer.

Next runtime evidence, after source integration and when authorized: one
serialized handoff with complete controller, AI service, camera and inventory
postconditions. Only afterwards expand to
three-way/transformed/combat swaps, cutscenes, checkpoints, respawns and cold-save
restore. Do not publish the current changes as a completed swap/class feature.
