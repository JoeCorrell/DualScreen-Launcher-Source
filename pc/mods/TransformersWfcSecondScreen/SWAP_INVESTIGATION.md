# Live character swap investigation

## Device evidence, September 4, 2026

Payload `2026.09.04.9`, at 14:42:02 and 14:42:57, received squad taps for
slots 1 and 2, resolved the expected pawn identities, and accepted both
requests on the game thread. Both requests stopped before any possession
mutation at `PriIdentityMatchesPawn(player_pri, player_pawn)`.

The previous current-weapon and grenade-inventory capture failures did not
recur in this session. The window log separately verified a 1280 x 720 client
and game focus. These observations do not prove that later swap stages work.

## Decompiled identity contracts

- `TnPawn.ApplyCharacter` stores its resolved character in `_CurrentCharacter`.
- `TnCharacterApplier.ApplyCharacter` passes that character to
  `TnPlayerReplicationInfo.set_CurrentCharacter` when a PRI exists.
- That setter assigns `_CurrentCharacter`, derives `CurrentCharacterString`
  from its specialty, and calls `RefreshProvider`.
- `SelectCharacter` writes `ReplicatedCharacterData.CD` and starts character
  loading. `ConfirmSelectedCharacter` writes `_SelectedCharacter`.
- `FinalizeCharacter` confirms the replicated selection, updates
  `PlayerIconicCharacterName`, and refreshes claimed characters.
- `GetCharacterName` reads the replicated selection, not `_CurrentCharacter`.

The old preflight conflated these two contracts by requiring every current,
selected, replicated, and iconic identity to match the live pawn. The
decompiled code does not establish that invariant. The old mid-mission
identity mutation also rewrote chapter-selection data using temporarily
modified `SelectCharacter` bytecode.

## Correction

Validate the applied character and its derived current-specialty string.
During a handoff, call the exact current-character setter with the destination
pawn's resolved character. Apply the same operation to an optional AI PRI;
reject aliased player/AI PRI records. Do not manufacture an AI PRI. Preserve
selection, loader, and claimed-character state throughout the live swap.

Arm rollback before invoking each setter, restore from the original pawns
after controller ownership is restored, and validate the restored current
identities. Retain the existing transaction, role adapters, reciprocal graph
checks, AI quiescence, runtime-service rebinds, and input debounce.

Identity failures now include the actual current names/specialties. Mutation
entry and failure logs identify the next failed handoff stage. Diagnostics
run on swap attempts, not in the ordinary telemetry polling loop.

## Validation status

The optimized 32-bit DLL compiled successfully with warnings treated as
errors. The five native suites (state store, panel input, IAT hooks, character
swap transactions, checkpoint gate) passed. Executable signatures and DLL
exports passed the existing build checks.

Actual player/AI control transfer and repeated gameplay switching still need
verification with this payload. The separate campaign-class `StartLevel`
resolution error remains outside this identity correction.

## Follow-up gameplay, payload 2026.09.04.10

The device DLL hash matched the published payload. At 14:55:47 and 14:55:50,
both identity checks passed; preflight reached the later shooter/aimer check.
That check still rejected the handoff before any mutation.

Targeted decompilation of `TnAiWeaponShooter` showed another invalid invariant:
`Initialize` only assigns `Owner`; `Activate` assigns `_Weapon`, and `Reset`
clears `_Weapon` while leaving `Pawn.Weapon` equipped. A dormant shooter's null
binding is valid. Only a non-null binding must match the equipped weapon.
The journal now permits and preserves the dormant binding, retains the firing
and owner checks, and logs each individual failed requirement.

The rollback audit also found that failed adapter preparation could report
`kFailedNoMutation` after resetting the shooter, omitting callback restoration.
It now reports `kFailedNeedsRollback`; callback restoration failures also
propagate instead of being discarded by the final return value.

## Full-path audit following payload 2026.09.04.11

At 15:02:48.390 the device accepted slot 1 / pawn 97072 on the game thread.
At 15:02:48.435 capture failed at `UnrealCallBool(shooter, "IsFiring")`,
before any possession mutation. The shooter/aimer owner checks and current
character identities passed. This is a bridge-call failure, not a logged
`IsFiring == true` result. No successful handoff has been observed yet.

The following downstream defects were found without requesting another tap:

1. `ValidScriptFunction` was incorrectly also the general invocation policy.
   `TnAiWeaponShooter.IsFiring`, idle `CancelMove`, and idle `StopFire` are valid
   empty declarations (`Defined` absent). The exact executable's dispatcher
   at `116e690b..116e691e` returns without modifying the zero-initialized result
   for these functions. General calls now preserve that behavior; missing
   functions still fail. Exact executor-hook eligibility remains strict.
2. `Engine.PlayerController.SetViewTarget` is an unindexed native function,
   flags `02020400`, not a script function. The old generic bridge always
   rejected camera commit and rollback. Only this exact native owner/name/
   flags combination is admitted; indexed, latent, iterator, and the engine's
   rejected executor categories remain blocked.
3. `CheckpointManager.SaveActiveCheckpoint` takes **two** required inputs:
   `CheckpointIndex` and `WriteDataCompleteCallback`. The integer-offset
   resolver required a one-argument function, so adapter preparation was
   guaranteed to fail. Resolving an individual integer now validates its own
   reflected bounds. The full int/delegate signature is separately validated
   before installing the hook. Deferred replay retains the original delegate
   and checks its UObject identity; duplicate requests cannot overwrite it.
4. Weapon heat was required to equal an earlier snapshot across intervening
   native game ticks. That can reject valid cooling/recharge during quiescence
   or settle verification. Conversion now samples live heat immediately before
   `SetWeaponDataType` and verifies its exact restoration in that same tick.
   Later graph checks validate the same owned service and finite nonnegative
   heat, permitting subsequent native progression.

Additional diagnostics run only during swap preflight, not ordinary telemetry:
the call-contract audit checks all listed required service inputs and returns
without invoking them and logs all failures in one pass. Role-adapter discovery
also has a non-mutating validation mode, checks all hook/cast names, validates
all twenty opcode/operand sites, and verifies checkpoint/aim/event layouts
before input or AI is quiesced. Runtime reflected-call rejection logs now
include the receiver class, function, flags, and native token.

The existing transaction, native possession paths, actor reuse, AI role proxy,
rollback, input debounce, world restrictions, and post-swap snapshot barrier
remain in place. No game window, controller routing, APK, container, or other
mod source was changed by this audit.

### Checks and remaining verification

- Six optimized 32-bit native suites pass, including new call-policy and
  checkpoint-call-layout regressions. Existing transaction tests include 202
  simulated repeated swaps and failure injection at every mutation boundary.
- `tests/Verify-DecompiledSwapContracts.ps1` verifies eight critical exact
  cooked signatures and all twenty existing role-adapter cast sites against
  the local package dumper. The required `HM_Engine.xxx` was decompressed from
  the already-retained original; no duplicate device game download was needed.
- Exact executable signatures and DLL exports are checked by `build.ps1`.

These are source/binary-contract checks, **not actual gameplay proof**. Player
control, AI following/combat, transforms, ammo, camera, and checkpoint continuity
still require runtime observation with the bundled correction. The separate
campaign-class `StartLevel` discovery failure is not established to be fixed by
this swap audit and must not be reported as solved.

Published the bundled mod-only correction as payload `2026.09.04.12`.
DLL SHA-256: `3ec08ba854ecbee0f51015894808169847f88f613a8082ec6328a699b0d18cfd`.
Pack SHA-256: `61b16d29caa1d23e78169524c61c3b0b3c02cf27f8f77d156264ed0a881452ed`.
The uploaded asset digest and downloaded bootstrap were verified. All 15 mod
manifests / 1,622 files passed archive verification; the overlay changes only
the WFC DLL and its manifest relative to payload `.11`. Publication does not
mean the running device process has loaded the new DLL.

## Payload .12 follow-up: shared lookup failure and game-thread stall

The 16:08:01.503–16:08:04.673 log shows request 3 reaching the game thread,
then failing the 50-call audit with unresolved functions and rejecting the
capture journal before possession. At 16:08:03.650 the hang monitor recorded
2139ms blocked in `squad-switch`; the complete audit took about 3.17 seconds.
All role-adapter hooks/cast lookups failed as well. The earlier IsFiring policy
correction is a real downstream contract correction, **not proof of the cause
of this common resolution failure**. No successful live swap has been observed.

Read-only comparison confirmed the device's TransGame.xxx matches the retained
original: SHA-256 `6800d3f931487b83ef7dae42a151afd15fd920d555f6f4751b7504c442f3fe9f`.
The original executable also confirms UObject vtable +0xC0 resolves through
116c92e0, UClass's function map is at +0x58, and its sorted entries are packed
FName / pointer pairs (10ce4280). The existing thiscall wrapper's ECX/stack
convention matches that implementation. Do not change offsets speculatively.

Source changes prepared after this observation:

- Exact global hook discovery now resolves the declaring class from the
  bounded 4096-entry class table, caches its checked UObject identity, and
  walks its bounded child/state metadata. It no longer scans all GObjects,
  then scans them again using string comparisons for every absent function.
  This removes those whole-world scans from both the swap audit and the
  recurring campaign StartLevel discovery.
- Adapter discovery is skipped when the prerequisite call audit already
  failed. No native possession/AI mutation is permitted past a failed audit.
- `Readable` now validates all adjacent committed readable regions covered by
  a request. The old single-region gate rejected valid arrays crossing a
  VirtualQuery boundary; the new actual-resolver test reproduced this with
  its class table. Guard/inaccessible pages still reject. This is a verified
  memory-validation defect, **not yet established to explain every live null**.
- Name cache entries own their text, including temporary strings created
  while parsing qualified class/state paths. Invalid/reused owner slots cannot
  be returned from the new owner cache.
- Bounded one-time diagnostics preserve the raw engine function pointer,
  requested packed name/roundtrip text, receiver type, validation status and
  a small sample of the class's function map. Previously every rejected raw
  result was logged simply as null. These diagnostics run automatically on a
  failed ordinary lookup; another squad tap is not needed to collect them.

### Focus recovery (source prepared, not installed)

The device logged successful Win32 activation at 16:06:33, then remapped guest
windows at 16:07:29 without any further focus verification. `g_display_focus_applied`
was a permanent success latch. It now rechecks foreground/active/focus ownership
on the existing two-second display slice, and only repairs a missing/desktop/
lower-panel destination. It respects real dialogs, menus, another foreground
application, disabled/minimized clients, and selects only the observed
`LaunchUnrealUWindowsClient` class for sizing. Existing 1280x720 client sizing
is retained; no controller replacement or synthetic click is introduced.

The Android source has a complementary WFC-only check when its real GPU client
is being presented. Outside the render lock, and only while the host activity
is focused, it walks anonymous drawable ancestors to the existing application
window and repairs root/stale/lower-panel X focus using DesktopHelper's normal
click-to-focus/WinHandler foreground path. Live other-app/dialog focus and
pointer grabs are preserved. Calls are throttled and an already focused target
does not receive repeated foreground requests. This engine-side change needs
a future explicitly authorized APK build; it is not deliverable in the DLL.

### Verification and remaining blocker

Seven optimized native suites pass, including the new test of the actual
UnrealBridge implementation plus x86 assembly wrapper. It covers scoped class
and state lookup, incorrect-owner rejection, temporary-name lifetime, stale
identities, and readable/guard/inaccessible region boundaries. The eight cooked
signature and twenty cast-site checks still pass. The three Android focus-policy
tests pass; the changed production Java classes compile against the existing
engine/Android dependencies using javac (no Gradle/APK build).

The user declined a title-screen runtime check and requested source preparation
only. Device operations stopped at that instruction. No APK was built or
installed, no payload was published, and no game/save was launched for this
turn. The new DLL was compiled locally with `-SkipLauncherPayloadUpdate`.
Bootstrap/launcher payload assets therefore remain the previously published
version. The root of the live raw function-lookup failure and actual swapping
remain unverified; the new raw/map diagnostics are the next evidence needed.
Do not report character swapping, class-menu activation, or in-game controller
recovery as tested or complete on the basis of these local checks.

## Shipping-native root cause established

See `NATIVE_SCRIPT_MIGRATION.md`. The fresh map/raw diagnostics plus executable
binding code prove that WFC strips non-exported compiled-script UFunctions and
their bytecode. Normal compiled calls bypass the attempted UFunction hooks.
The source now targets the real native StartLevel entry and corrects compiled
executor context, Script header and 16-byte delegate ABI errors. Swapping and
the remaining class/loadout integration still require native bindings; no
successful gameplay test or payload deployment is claimed.

## Subsequent source-only architecture audit

See `ARCHITECTURE_AUDIT.md` for the direct comparison against the cooked game,
the preserved UI/queue/transaction path, and the explicit remaining limitations.
The audit corrected loadout array truncation (including lost grenade entries),
shrink-only/partially applied edits, acceptance of dropped native asset IDs,
initial PRI selection occurring before three-character validation, and ordinary
checkpoint saves unnecessarily entering swap frame parsing. All changes remain
local; no APK, device operation or payload publication was performed.

Seven native suites pass. The package audit now verifies nine critical call
signatures, twenty cast sites and eight behavior contracts. The last local DLL
SHA256 is `5325f3fb1cc6e70c2977e6a5740f677b408c67e2ead8ac8f32982e0cab1b2ce5`.
This does not establish live control transfer, complete native AI compatibility,
durable custom squad saves, or successful class-menu activation.

## Latest source-only continuation: native AI handoff services

The historical suite count/hash above are superseded by
`NATIVE_SCRIPT_MIGRATION.md`. The swap/rollback path now calls the compiled
RequestReplan chain and verifies actual scheduler acceptance after Resume.
CancelMove now dispatches through the current controller state and verifies
the complete movement callback cleanup. The base method was a no-op; moving
states require native PopState/PoppedState. Ten local suites pass, including
15,000 compiled request/dedup/promotion calls and 6,000 compiled movement
cancellations against synthetic engine dependencies.

These remove three more stripped-function dependencies; they do not complete
the remaining possession, logical-role, camera/aiming or native class-menu port.
No new runtime logs were collected, device accessed, APK built, payload updated
or gameplay verification claimed. Existing UI changes remain untouched.
Latest local-only DLL SHA256:
`d86f2b50d1e8411691ffa483f3055095c1e57f9ec662ce8baf54bcf291fdab53`.
The launcher manifest still references the previously published `9360fb79a295...`
DLL; this continuation deliberately did not deploy or publish the local build.
