# Z-A inventory action investigation (2026-09-05)

Status: diagnostic instrumentation, **not a completed item-use/equip fix**.
Supported binary: title `0100F43008C44000`, update `v393216`, main build ID
`B1F12FD919EAE86AB8A978317677E64BCE443D1F` (zero-padded to 32 bytes).
Do not transplant these RVAs to another build.

## Reproduced failure

Device test at 17:17:39: bottom-screen Great Ball (ID 3), quantity 6, pocket 2,
operation 1, target -1 reaches JNI and fails with `PresenterUnavailable`.
The assumed owner+0x130 object has owner+0x138 control == 0. Its supposed
scheduler/context fields include small integers and invalid-looking pointers.
This is NOT evidence of a usable presenter. The assumed owner/layout must be
checked against the actual vanilla dispatch argument. Do not remove guards.

The user reports Great Ball selection leads to throwing, with B to cancel,
not the generic party-item Use action. Ball field selection needs its own trace.

### Diagnostic comparison 1

`eden-comparison-1.log`: both Great Ball (3, quantity 6) and Antidote (18,
quantity 15, party slot 0) reach JNI, then reject with presenter-unavailable.
No native-dispatch events or item/party deltas are recorded for these attempts.
At this first comparison the observer installation was confirmed but no successful
vanilla use had yet been captured. Do not infer that opening
the bag fixes the lookup, or that no dispatch means the observer is validated live.

**Confirmed wrong object interpretation:** C14DE0 first resolves a presenter
using C14E94, then separately calls C14E7C with GOT[3EC6D48] to retrieve a command
string. C14E7C reads owner+138; if zero, it passes owner+130 to 38B90. 38B90
constructs a hash/data/length/backing-string object (including the FNV empty hash
CBF29CE484222645). If +138 is nonzero it calls 8CB498, another string getter.
Thus owner+130 is NOT a presenter and owner+138 is NOT its shared-pointer control.
The current companion's rejection is based on misidentified fields, not proof
that the true bag presenter hasn't initialized.

C14E94 reads a callback's reference, passes it to 5ED4D0 via GOT[3EC21C8],
locks a reference control block at +1A, then obtains its object from control+10
minus 28. This lifetime-managed path must not be replaced by an unretained raw
pointer. C14DE0 passes that object to C14F48 alongside the separately retrieved
command hash. Diagnostic 1.6.17.1 reported the OLD presumed chain for comparison.
The next diagnostic removes that incorrect lookup and its unsafe guest writer.

### First successful vanilla Potion capture

`eden-potion-native-1.log`, guest session timestamp 618.792461:

- observer sequence 1, no coalesced events, request 0 (vanilla);
- caller `1A2A20A538`, corresponding to return from BL at main+2B37534;
- actual presenter `2B1C613F00`, item 17 (Potion), native list index 0;
- contexts +108/+110/+118/+120:
  `2B0E377240`, `2B2A15D000`, `2B2EFA9000`, `2B314422C0`;
- scheduler `2B066FFC10`, no previous task.

Top-screen images before/after independently confirm Potion 44 -> 43 and
Feraligatr HP 63/283 -> 83/283. This validates the observer on a real successful
vanilla path, not the companion action. These addresses are diagnostic only:
never hardcode or reuse the menu-owned object after its lifetime ends.

The existing decoded save-block telemetry had not reported these deltas while
the Satchel remained open. Check again after returning to gameplay; don't
assume cached save-block data is the authoritative live working inventory.

Further bounded native trace: A5B14C gets the selected party index from
task+100 -> +68 via 8BAA60, then calls A5B188. A5B188 resolves the game-owned
party service rooted at main+610B810 (ready flag main+610B8C0), checks type and
reference lifetime, bounds-checks with A5B378, and retrieves the party entry
through 4DE870. The context argument is not a license to fabricate a bag UI.
99E34C resolves an item ID from the active tab's 12-byte-entry native vector;
C22FCC/C22F08 handle list/selection refresh after the recovery task, and are
not independently verified consume APIs.

## Verified static path

Locally extracted update main: `C:/Projects/Eden DS/.work/pokemon-za/update-v393216-exefs/`.
Bounded disassembly/decompilation output is private under `.work/plza-item-trace/`.
Raw image is already mapped at RVA zero, not an NSO header plus code.
Ghidra's unanalysed fatal-assert/tail-call output can merge unrelated functions;
cross-check with instruction boundaries. Do not treat speculative inferred C
signatures or merged error paths as authoritative.

`main+C150E4` reads selected item at presenter+0x140 and list index at +0x142;
retains contexts +0x108/+0x110/+0x118/+0x120; classifies the item through the
database GOT at +0x3EC3320; constructs/enqueues category tasks on scheduler+0x68;
stores task handle at presenter+0x128. Return does NOT prove an effect occurred.

Task descriptor names from the binary's own string/hash/length entries:

| Descriptor | Name | Dispatch task wrapper |
| --- | --- | --- |
| 3DACED0 | ItemProcessRecovery | C15420 |
| 3DACEE8 | ItemProcessExp | 2B21980 |
| 3DACF00 | ItemProcessWazaMachine | 2B21AB8 |
| 3DACF18 | ItemProcessEvolve | 2B21D90 |
| 3DACF30 | ItemProcessFormChange | 2B21EC8 |
| 3DACF48 | ItemProcessSeikakuHosei | 2B2201C |
| 3DACF60 | ItemProcessEffort | 2B22154 |

There is a separate `kItemProcessMegaStoneHave` descriptor at 3DACF78 and an
`ItemProcessHave` string. None of these names establishes a safe callable API.
Direct B/BL references to C150E4: C14F9C, 2B37534, 2B38834.

Recovery constructor C15590 copies list index into task+0xD8, resolves item ID
through 99E34C into task+0xDA, retains game contexts in +0xE8/+0xF0/+0xF8/+0x100,
and creates another object in +0x108. Derived vtable at 3DAE7E0 references
C31ACC (task entry), C31D64 (eligibility), C22F80 (context-state update),
DE66C0 and C32044 (callbacks). C31D64 obtains a party object via A5B14C and
checks native HP/status/context restrictions. Trace these dependencies before
attempting a menu-independent request.

The original companion stub only wrote item/list/tab and called C150E4. It did not
consume mailbox operation or party target. It requires menu contexts, and is
not a generic native equip/use API. It has been removed, not presented as fixed.
No manual bag/save/HP mutations are added.

## Deeper native path and second diagnostic

Recovery task entry C31ACC queues A59700 -> A5972C -> A598A4. The task owns UI
contexts, selection, scheduler and coroutine helpers: never call it with a
fabricated task/presenter. A598A4 orchestrates native quantity mutation, native
medicine application, event counters, callbacks and menu animation/refresh.

- A59C80 -> A5A070: native bag decrement, tracked-item bookkeeping and removal
  from the category vector when the last item is consumed. Its service is rooted
  at GOT[3EC32B0], ready main+610D940; the item database ready flag is 610DEC0.
- 2BC8EC / A5A268 -> 2BCAC8: resolve the **active** typed bag component from
  application vector main+610A710/+718, readiness main+610A980. Entries are
  component pointers. RTTI getters 291AF34/291B018/291B024 return type 3CFCB18.
  Component+40 holds 3000 16-byte records, indexed by actual item ID.
  This is separate from the encrypted save-block mirror used by the original UI.
- A5BC54 -> A5BD0C -> 439B0/689F10: resolve the target through gameplay services;
  2D2C78 obtains the target actor interface. A5C51C retains it, creates a medicine
  adapter and calls A5C988 with the actual Pokemon and item ID.
- A5C988 reads native item database fields (A5B588/A5B5C8/A5B548/941B80/A5CB18)
  for fixed/percentage HP recovery, revival and status recovery. It invokes
  adapter virtuals +28/+30/+38; do not reimplement these calculations.
- Adapter+28 -> C2BE3C builds native **RecoverHp** with **Value** and
  **CauseCode**, and C2BF5C enqueues it through the actor's command interface.
  It is not a raw HP assignment. The status adapters CA9F2C/CAA010 resolve
  Pokemon through gameplay services and call CA9FCC/CAA0B0.
- C31D64 contains the original eligibility checks. C31F84 checks the real
  `FLAG_CANT_USE_ITEM_IN_BATTLE`; C31F10 checks a live target condition.
  Full HP, fainted state, status applicability and target restrictions must
  remain intact. Dispatch/queue return alone does not prove application.

Ball selection is independent. 2BB90C receives a gameplay action context and
an ItemId. It constructs **SelectBall** / **ItemId**, or updates the existing
SelectBall task through its managed reference. 984xxx handles associated
HoldingBallId/IsChangeBall task parameters. Native callers, eligibility and
field-context lifetime still require live validation; do not invoke based on
the string/function name alone.

The direct native caller is 2BB00C -> BL 2BB0BC. It gets its retained game
context via 2BB680 (root 610B810, readiness 610B8C0, RTTI 3D2AD60, native
42910 cast/retain), reads the selected item ID from its field list and calls
2BB90C. The referenced SelectBall parameter descriptor 3D2C818 is **ItemId**.
This provides a menu-independent candidate resolver, but live correlation and
the normal gameplay eligibility checks are still required before enabling it.

Second diagnostic adds read-only observers at A5A070, A5C51C, A5C988 and
2BB90C alongside C150E4. All five sites check four original instructions before
patching. The field pre-update hook is now an ABI-transparent passthrough;
the invalid string-based action writer is gone. Companion actions explicitly
return NativeHandlerNotReady (15), not success and not a misleading demand to
open the normal bag. Implementing the actual gameplay adapter remains required.

Mailbox trace slots: dispatch 80, bag 100, medicine 180, target 200, ball 280.
Each uses a single-attempt try-lock, sequence and immutable copy under that lock.
Lower-handler records include x0/x1/x2/LR and references/vtables where the native
function itself requires them. No game-owned reference is kept for later use.
Host logs `[PLZA-NATIVE-HANDLER]` with change sequence/coalescing information.

`plza_inventory_reader.h` adds a **diagnostic-only** active-bag comparison, sampled
at 750 ms. It bounds the component vector, checks exact RTTI getters/instructions,
reference identity/strong-count/generation and membership before/after two agreeing
copies. It publishes no partial sample, never writes inventory, and does not yet
replace the UI's save-block reader before on-device comparison.
`[PLZA-LIVE-BAG]` logs native quantities alongside the old snapshot quantities.
The invalid string-object sampler (which generated noise every 200 ms) is removed.

Validation: production loader emitter passes 36 relocated unlocked/locked/contended ABI
tests covering all five observers plus field passthrough, including unchanged
GPR/SIMD/NZCV, displaced prologues, stack stores and game memory; the nullable
medicine-target reference is also tested. Modified code
signatures/layout and repeated installation are rejected. The standalone bag
reader tests pass for two relocated bases, last-item removal, invalid references,
readiness, vector bounds, instruction mismatch, bad records and mid-copy generation
changes. These are offline tests, **not proof of on-device item action success**.

## Diagnostic build

Device config backed up privately before changing only log filter and flush:
`*:Info Frontend:Debug Loader:Debug`, `flush_line=true`.
`use_gdbstub=false` remains unchanged. No startup pause, debugger, or controller
automation is needed. Restore the prior logging settings after investigation.

Logs:

- `PLZA-ITEM-INPUT`: touch down/up/cancel, selection versus activation, target
  confirmation, and duplicate suppression. Existing select-then-activate behavior
  is retained for the comparison.
- `PLZA-ITEM-ACTION`: validation, queued request, dispatch return versus effect.
- `PLZA-NATIVE-DISPATCH`: actual C150E4 argument and caller from vanilla AND
  companion, selected item/list, retained contexts, scheduler and old task.
- `PLZA-NATIVE-SELECTION`: change-only samples of the existing presumed owner
  chain (not validated as the native object until compared with dispatch).
- `PLZA-ITEM-STATE`: quantity/pouch/flags deltas for all 3000 bag records using
  the existing decrypted snapshot read, including the final item disappearing.
- `PLZA-PARTY-ITEM-STATE`: slot, species, held item, HP/max HP and status deltas
  from the existing validated party snapshots. No names/save bytes are logged.

The observer resides at bridge+0x800 and captures into mailbox+0x80..0xDF.
It preserves registers/flags, executes the displaced `sub sp,sp,#0x80` once,
then resumes C150E8. A one-attempt try-lock avoids waiting or calling a host
logger on the game thread. Latest-event capture can coalesce rapid calls (the
log reports that count); a contending capture is skipped, not retried. Observe
individual test actions with a pause between them. Snapshot samples are not a
complete event history and are not proof of save persistence.

`test_plza_item_trace.py` compiles the production loader emitter and executes
its ARM64 output in Unicorn. Verified: two relocated bases, unlocked/busy paths,
all GPR/SIMD registers, NZCV, stack/prologue, unchanged game object memory,
mailbox publication, and rejection of modified native signatures/layout.
Native Android library compiled successfully. On-device comparison still needed.

Diagnostic APK `1.6.17.1` (code 30) built successfully; 151 launcher tests passed.
APK SHA-256: `c6023115ecb8db6c389a2095277ac8607a34c0a7211c954ec6a7ed3d8fae8c93`.
Packaged native library hash matches the freshly built/stripped library. Release
signing certificate is unchanged. Published WFC assets were preserved.

## Native ball implementation and live party reader (1.6.17.4)

The latest source adds a **separate hook at main+2BB00C**, the native field-ball
selector update. This is not C150E4 or the normal Satchel presenter. It receives
the currently alive field presenter from the native invocation; no cached UI
pointer is called later. Its model at +130 owns selected index +4C, item vector
+50/+58, and transition byte +74. A88620 refreshes the native list. The adapter
selects the requested, owned list entry and invokes **2BB7DC** to persist it,
matching the state change in 9B6EB4/A87AD0 without cycling controls or submitting
intermediate selections. The unmodified remainder of 2BB00C then executes
2BB680 -> **2BB90C (SelectBall)** during the same native update.

Completion is acknowledged on the next field update only when both the current
model and the game-owned selected-ball object (610DB60, +40) contain the requested
ID. Later controller selection is not overwritten. This confirms native selected
state, not a thrown ball or inventory consumption. Equipping does not decrement
quantity. Medicine, held items, TMs, key items and Mega Stones remain explicitly
unimplemented; do not treat this version as complete inventory item usage.

The queue checks title/build, live bag quantity and category, a fresh field tick,
root identity and registry token. The guest rechecks root/token, bag owner, native
readiness, current field mode, suspended state and competing controller input.
Missing list entries and changed contexts reject without consumption. Only one
request can be outstanding. Host cancellation uses an atomic compare/exchange
only on an unacquired queued request after three seconds, so it cannot execute
later after a long pause. A running request is never blindly retried. Completion
and selected-ball state are exposed in previously reserved snapshot bytes at
3600..3639; snapshot size/version are unchanged. UI shows **EQUIPPED** from native
state and waits for completion instead of reporting a queued action as success.

**ABI detail:** 2BCAC8 returns a retained, non-trivial game reference through
**x8**, not x0. The compiled guest uses a non-trivial return type and releases via
**99E6B4**, the same routine called by A5A268. The raw function was disassembled
and the tests execute the actual extracted resolver, release and 2BB7DC code.
Do not replace this with a pointer-return cast; that corrupts the indirect result.

A separate request/response channel at mailbox+300 samples live party state at
the existing 750-ms cadence. The native field thread obtains a retained gameplay
context with 2BB680, gets party count through **43B10(context)**, and obtains each
member through **4DE870(context, slot)** (also hidden x8 result). Member release is
556CA0; context release is 407900. For the observed Pokemon vtable **3E2A438**,
the native accessors are species 3668, form 3678, level 975A0, HP DB9C, maximum HP
18D80 and status 5D30. These accessors own the PKM lock/decryption/checksum flow;
they are never called from the host UI thread. Invalid types/backends or invalid
counts are not used. Valid recent samples overlay the old party snapshot's
HP/status and identity fields. Held item and nickname data are still from the
prior snapshot; native party action validation and medicine transactions remain
work to do. This reader is not proof that targeted medicine is implemented.

Generated guest code lives at a **page-aligned bridge+1000**; reservation is now
4000 bytes, mailbox remains 1000. Native helper and hook prologues are pinned
against the exact v393216 image. The new trampoline preserves all GPR/SIMD,
NZCV, FPCR, FPSR and stack alignment. Original field pre-update remains untouched
apart from its existing passthrough; all five diagnostic observers remain.

Validation before APK packaging: 36 existing trace execution cases and 42 new
guest execution cases pass at two relocated bases. They cover native reference
lifetimes, actual persistent selection, repeated/competing requests, missing or
depleted items, changed scenes/bags, bad vectors, restricted field input, and
requested live-party sampling/HP changes/invalid targets. Bag-reader lifecycle
and production snapshot tests also pass. Native Android library compiled.
These are offline tests; on-device ball selection and party HP confirmation are
still required. GDB remains off. No diagnostic release should be published.

On the user's subsequent **install** request, built 1.6.17.4/code 33 in 7m29s;
151 launcher tests passed with zero failures/errors. APK SHA-256:
`096af9414f05d6b53520f0cd8a75e37b67b266e699a49affc01df5fe225a04a2`.
Packaged native SHA-256 matches the freshly stripped artifact:
`09dc2846a621362cb9c38cee79ce3bbeb7162ac925624fabafca7982d88a5526`.
Merged library matches runtime source artifact
`4edf16c40f628f3c391d84e4a2f0922a9a4993af3fb0782aa6bd9e5d27c73d98`.
Signing certificate unchanged; published WFC payload preserved. Device was
verified at LauncherActivity before `adb install -r`, which returned Success.
Saves/caches were not cleared. No public release or source push was performed.
First post-install test should be bottom-screen Great Ball selection with the
normal Satchel closed, checking native confirmation and persistent top-screen
selection. No additional normal Potion consumption is needed for comparison yet.

## Previous comparisons

### Completed second live comparison

`eden-diagnostic2-baseline.log` confirms all five hooks installed (2728 bytes).
Before opening the Satchel, the typed bag reader resolved owner `2B0054B000`
and correctly read Great Ball 6, Poke Ball 3 and Potion 44. No bag widget was
required. `eden-diagnostic2-actions.log` records:

- At 101.023135 native SelectBall changed from item 4 to item 3 (Great Ball),
  same gameplay context `2B0EBBCB80`, caller main+2BB0C0.
- At 114.069629 native C150E4 dispatched Potion 17 from the normal Satchel.
- At 114.573510 native A5A070 was called with service `1A2DB5B800`, item 17,
  quantity 1, caller main+A5A050.
- Native A5C51C got actor interface `2B0FC41000`, vtable main+3BE0D28,
  item 17. A5C988 got medicine adapter `2B2E703C80` (vtable 3DA1970) and
  Pokemon `2AF5662C80` (vtable 3E2A438). These are call-time observations only.
- Live bag Potion quantity changed **44 -> 43**, while the encrypted save
  snapshot remained **44**. The old party snapshot did not emit an HP delta.
  Do not infer which party slot or post-use HP from the stale party snapshot.

The native SelectBall call occurs repeatedly during gameplay, not just on a
selection event. A standalone one-shot call is therefore insufficient to prove
persistent equip: the field model may subsequently reapply its old selected ID.
2BB00C reads model `[presenter+130]`, index +4C and ID vector +50/+58. Native
9B6EB4/A87AD0 change that index, call 2BB7DC, and set model+74 for UI updates.
2BB7DC persists the selected ID into the game-owned object rooted at 610DB60,
field +40 (readiness 610DC00). A88EB4 restores the model index from that same
object, respecting ownership and game sorting. A88620 rebuilds the list,
restores the index, then synchronizes it. Follow this actual persistent native
selection flow; do not repeatedly force SelectBall or simulate controller input.

Pokemon vtable 3E2A438 identifies native getters +78=18D80 (maximum HP),
+80=DB9C (current HP), +A8=5D30 (status). They use Pokemon+50 -> +70 and the
game's locking/encryption accessors. Do not call guest functions on a host UI
thread or use an unretained captured Pokemon pointer. Live party synchronization
and the complete medicine eligibility/transaction flow remain unfinished.

### Source changes after that capture (installed in 1.6.17.3)

`PopulateItems` and action quantity validation now use `ReadCurrentBag` / the
verified native typed bag reader. There is no fallback to stale save quantities.
Unavailable samples clear Bag availability and inventory entries; the last item
in a stack disappears correctly. A native bag-mutation event schedules the next
UI sample to refresh the bag, retaining the existing 750-ms background cadence.
The duplicate diagnostic bag read was removed, so this does not add polling.
SelectBall logging now reports context/ID changes rather than every field tick;
x2 is not treated as a meaningful argument to that two-argument native function.

Native Android library compiled successfully. `test_plza_bag_snapshot.py`
compiles the production mapping function and passes native quantity changes,
last-stack removal, unavailable data, output-capacity and action-reader checks.
The underlying reader lifecycle/relocation tests also pass. On user request,
1.6.17.3/code 32 was built and installed with `adb install -r` (package-manager
lastUpdateTime 2026-09-05 18:46:40). The build passed 151 launcher tests, zero
failures/errors, and completed in 5m44s. Signing certificate is unchanged and
published WFC assets were preserved. APK SHA-256:
`26a2cba76798667d163a3467900143de58a63a7b92abfe310dc291d68d63af97`.
The merged library matches the new native source artifact, and the packaged
library matches the stripped output:
`0e393b30b8e4626a3379c6beccee4fe6d5897b64998bf979f0b4152acffbd985`.
GDB verified off after installation; diagnostic logging remains enabled. No app
data or caches were deleted. On-device UI synchronization validation after this
installation is pending. Native bottom-screen use/equip remains explicitly
incomplete; no new item-use test is requested yet.

Diagnostic `1.6.17.2` (code 31) built successfully in 5m43s; 151 launcher tests
passed, zero failures/errors. APK SHA-256:
`c7452bbf4fc79d6bd77001f7886f03f7292bf87c47924680c2ef4fecc7a5d12d`.
The merged native library matches the freshly built source library; the APK
entry matches AGP's stripped output (`b78745739dcabf68350ab1f692bddc8e6283ff4431885d41bbd02935c130fbb5`).
Signing certificate is unchanged. Published WFC assets were preserved. Installed
with user approval using `adb install -r`; package manager verified version
1.6.17.2/code 31, lastUpdateTime 2026-09-05 18:16:54. No app-data clearing or
cache deletion was performed. Verified GDB remains off; debug logging and flush
remain enabled.

After installing, start gameplay without opening the Satchel first. Capture the
typed bag baseline before asking for item actions. Then record one normal field
ball selection/change and cancel without throwing, and one normal Potion on an
already injured member. Compare lower native handlers, reference types, native
bag quantity and old save snapshot after closing the menus. Do not spend more
items until these logs are reviewed. Bottom-screen use/equip intentionally
reports the unresolved native handler rather than attempting the invalid writer.

Do not initialize/open the normal bag as an implementation workaround or mark
dispatch as success. Next implementation still needs native context validation,
normal eligibility checks, safe game-thread invocation, target/effect confirmation
and live UI synchronization before the action can be called complete.
# Native recovery implementation (1.6.17.5 installed, live validation pending)

Installed via `adb install -r` on 100.83.63.70:46665; package manager verified
code 34, lastUpdateTime **2026-09-05 20:44:22**. No saves/caches were cleared;
GDB remains off, logging/flush on. No public release/commit/push. APK SHA256
`3c54e37473ffac22ac73dab9c3be308cc891fa5bca6ed3e66e31dafc32f66949`.
Packaged native SHA256
`c64875a8df0825cc24ca2282d17910aabb041b617a4e74e32f8ccde162463f61`.
Runtime matches merged native library; APK matches stripped native library.
Unchanged signing certificate. 151 launcher tests passed (zero failures/errors);
60 medicine/command/ABI cases across two bases passed. Published WFC asset
override retained. Guest code 6784 bytes, 59 signature checks. User has been
asked for one bottom-screen Potion on an injured member, normal bag unopened;
do not request repeat consumption until its logs have been checked.

The compiled field-update guest now accepts UseOnParty for native Recovery items.
It resolves fresh party identities and tokens, never a saved menu pointer. Host
target validation now uses the recent native party channel, not the stale PKM
snapshot. Ball selection retains its existing native path.

`plza_medicine_guest.h` traces C31D64 eligibility with native item predicates,
status compatibility, battle/item locks, HP/maximum-HP and revive checks. Native
A5B608 handles percentage eligibility rounding; A5C988 owns actual effect math.
The actor path is A5BD0C -> 2D2C78. A5C51C's GUID lookup chain independently
checks actor-to-Pokemon identity before any mutation. Note 52D688 **moves** its
source reference and clears it. All x8-returned references are released using
their exact native ownership functions.

C2BF5C's DB220 -> 72AF0 command binding is retained during the transaction;
6A740's disabled byte at command manager +A1 is checked. Status medicines also
preflight CA9F2C/CAA010's 52D5DC -> D88B8 status target. The field model's +8
allocator is used by native A89188; A5C670 uses it to construct the real medicine
adapter (3DA1970), not a synthetic vtable/object. Once targets and dependencies
are retained, A5A070 consumes one item, A5D060 and 986638 perform native usage
bookkeeping, and A5C988 applies the native recovery operations. Native adapter
destruction is 120500; actor and command binding refs use their real virtual
releases. No HP, status, PKM encryption or save data is manually written.

The request remains Running after the void handler returns. Subsequent gameplay
updates re-resolve the target and confirm a real HP/status/condition change plus
exact quantity reduction. No repeated consumption or automatic refund occurs.
Uncertain post-consumption results explicitly say not to retry; pre-consumption
no-effect cases retain the item. Target/scene changes are rejected safely.
Medicine mailbox diagnostic +400 logs stages, consumed state, before/after
quantity, HP, status and other condition. Existing five observers are unchanged.

Static item categories now come from C150E4's native database predicate order,
cached per game root, at most 24 owned-item classifications per 750-ms sample.
Mailbox +440..FF7 stores these kinds under the party response publication.
Snapshot reserve +40 stores 512 four-bit kinds without changing item/save flags
or snapshot size. UI uses these to route HP Up and similar Other-pocket
consumables to Use rather than incorrectly offering Give. This does not yet
implement Exp/Effort/Nature/Evolution/TM/Have/Mega/key-item handlers.

Tests: production guest/trampoline at two relocated bases; real game A5C988,
C2BE3C and C2BF5C instructions execute for medicine dispatch/RecoverHp command
construction. Other game services are ABI-checking doubles; this is not a
substitute for on-device validation. Test cases cover healing, percentage
recovery, revives, status/condition cures, full HP, wrong status, invalid targets,
missing actor/binding, disabled native command, locks, last stack, bag changes,
delayed/unconfirmed effects, balanced ownership and one-shot dispatch. Existing
ball and live party tests and production inventory mapper tests pass.

Device log `eden-install4-current.log` verifies 1.6.17.4's native ball selection
for Great Ball, Poke Ball and Net Ball and all six native party entries. Potion
17, Paralyze Heal 22 and HP Up 45 were still rejected by that installed version;
do not confuse its logs with testing the new recovery implementation.

## Additional native paths traced, not yet implemented

- **Effort/vitamins:** factory 2B22154 -> ctor 2B2F330, vtable 3DAEEB8.
  2ACDAB0(database,item) determines stat, 2ACDB9C(database,item,stat) signed
  change. Task 2B2F3A0 determines available quantity using Pokemon v+F0 current
  EV, v+348 total EV, and for negative berries v+160 friendship; native caps
  observed there are 252/510. 2B2F5A0 resolves 42910 -> 689F10(slot), calls
  2B23EC4 -> **28EEAF4(wrapper,item)** (native friendship/item usage), then
  **28EEB38(wrapper,stat,positiveAmount)** or **28F1A90(wrapper,stat,negativeAmount)**.
  These resolve 116690 -> Pokemon and call v+350=61ED50 / v+358=2D6FC50.
  61ED50 owns EV caps, recalculation, HP adjustment, encrypted PKM/checksums;
  do not reproduce this by editing stats. Its restrictions must be preflighted.
  Later coroutine 2B2F77C consumes via A59C80 and does UI/callback cleanup.
- **Have/held items:** factory 2B22518 -> ctor 2B26E00, vtable 3DADF78.
  2B27964 resolves gameplay wrapper then **28F1980(wrapper,item)** ->116690
  -> Pokemon v+178=2D7201C (held-item setter). **68A208(wrapper)** processes
  item-dependent form changes (do not omit). It then consumes via A59C80 and
  returns the previous held item through 2B17888 -> native **48963C(service,
  oldItem,1,0,0)** gated by 61B29C(service). UI sorting/selection follows; those
  are menu-specific and must not be invoked on a fabricated task. Need trace
  exact hold eligibility, old-stack capacity checks, Mega-specific overrides,
  transaction confirmation and native actor reference identity before wiring.
  Pokemon v+170=DF5978 is native held-item getter (not yet added to sampler).
- **Friendship item processing:** 28EEAF4 ->116690 -> Pokemon v+588=2D6FDC8.
  116690 returns retained Pokemon via x8; the wrapper uses release **66F200**.
  Do not confuse wrapper/reference types with raw Pokemon or interface pointers.

Ghidra batches through `ghidra-console32.log` are saved in the existing project.
No live writes or extra consumptions were used for these additional traces.
