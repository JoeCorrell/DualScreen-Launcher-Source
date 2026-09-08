# Weapon and inventory research, 2026-09-07

This is a **partial read contract**, not a deployed weapon adapter or permission
to call equipment actions. Exact loaded code and rdata come from
`.work/crysis3/live-190400/capture`, with the provenance/hashes recorded in
NATIVE-RUNTIME.md. New read-only observations come from Windows process 244 in
`.work/crysis3/live-200610`. Heap addresses below are session evidence only.
The captures are bounded double reads, not atomic snapshots or lifetime proofs.

## Current item and ownership

1. The already-traced framework client-actor getter obtains the actor. Actor
   virtual `+0xe4`, target `0x3803e6b0`, returns `[actor+0x40]`: its inventory.
2. Inventory virtual `+0xcc`, target `0x37279e30`, returns `[inventory+0x8c]`:
   current item EntityId. The Lua `GetCurrentItemId` registration at
   `0x37287e5f` names the callback `0x37287890`, which uses this same virtual.
3. Actor virtual `+0xdc`, target `0x380419b0`, takes one DWORD argument and
   returns with `ret 4`. With argument zero, it obtains the inventory's current
   EntityId and calls item-system virtual `+0x54`. Nonzero enables a separate
   linked-vehicle branch; this must not be silently substituted for zero.
4. Framework virtual `+0x1cc`, target `0x3749cb70`, returns
   `[framework+0x4b4]+4` when nonzero. The root-global lookup used by the actor
   reads the same interface from `0x3b9f9c68`.
5. That interface has vtable `0x3988cf40`; virtual `+0x54` is `0x37283230`.
   It performs unsigned-key lower_bound in a tree rooted at
   `[interface+0x190]`, with inline end sentinel `interface+0x18c`. Node left
   child is `+8`, right child `+0xc`, EntityId key `+0x10`, item pointer `+0x14`.
   A missing exact key returns null. Preserve those semantics; do not choose
   the nearest node or interpret EntityId as an array index.

Fresh evidence:

| Read | Observed relationship |
|---|---|
| 5 | Actor `0x32b3a960`, vtable `0x399b5940`, EntityId `0x7777`, inventory `0x011b3008` |
| 7 | Inventory vtable `0x3988c288`, owner EntityId `+8 = 0x7777`, current item `+0x8c = 0xaa5` |
| 14 | Inventory vtable's getter pointers agree with captured code/rdata |
| 19/20 | Framework's raw item system `0x01f314d8`, global interface `0x01f314dc`, exactly raw+4 |
| 23 | Raw item-system primary vtable `0x3988d000`, interface vtable `0x3988cf40` |
| 27 | Interface tree root `0x06f673d8`; item lookup still needs bounded traversal |

Inventory item count virtual `+0x90` (`0x3727a190`) uses the DWORD vector
`[inventory+0x10, inventory+0x14)`. Index virtual `+0xa4` (`0x3727a1a0`)
checks nonnegative/in-range indices before returning its EntityId.
Do not label these entries primary/secondary/weapon/grenade until their real
item classes and game slot semantics are established.

## Inventory ammunition and class names

Lua `GetAmmoCount` registration identifies callback `0x37287a10`; this resolves
an entity class by name and calls inventory virtual `+0x100`. `GetAmmoCapacity`
callback `0x37287960` calls virtual `+0x108` with the same class identity.

The live vtable resolves those to `0x3727aba0` and `0x3727abe0` respectively.
Both use unsigned-pointer lower_bound over root `[inventory+0x2c]`, end
sentinel `inventory+0x28`, left/right `node+8/+0xc`, class-pointer key `+0x10`.
Count is signed DWORD `node+0x14`; capacity is signed DWORD `node+0x1c`.
The original getters return zero when the key is absent. Preserve an additional
presence flag for the bottom UI, to distinguish absent from actual zero.
Negative values are not automatically an infinite-ammunition mode.

Read 24 sampled root `0x06844040`: class `0x022c78f0`, count 3, capacity 3.
Read 33 subsequently confirmed its internal class name **ElectricArrow**.
Read 29 sampled its left child: class `0x022c6890`, count 132, capacity 200.
Read 44 confirmed the name **kvolt_bullet**.
Read 30 sampled its right child: class `0x022c84b0`, count 0, capacity 0.
Read 45 confirmed the name **HPistolExplosiveBullet**. These are actual internal
class names, not guessed mappings from the quantities.

Class `0x022c78f0` (read 28) has vtable `0x398f4638`. Its name virtual `+0x24`
is `0x378b9070`, which returns `[class+8]`; that sample points to `0x0233a72c`.
A caller must copy a bounded NUL-terminated string, not carry that pointer to
the renderer. Display names/localization may differ from internal class names.

## Equipped weapon magazine versus reserve

The Lua weapon `GetAmmoCount` callback `0x3824a840` resolves the item by
EntityId, invokes item virtual `+0x128` (weapon interface), calls that interface
virtual `+0x7c` (current fire-mode index), then `+0x74(index)` (fire mode), and
fire-mode virtual `+0x20` for ammunition. HUD reader `0x382cf760` follows the
same sequence and stores the result at HUD object `+0x102`.

HUD reader `0x382cfb30` separately uses fire-mode virtual `+0x24` for the value
at HUD `+0x100`; the renderer clamps displayed rounds to min(`+0x100`,
`+0x102`). It separately displays overflow when applicable. Fire-mode `+0xb0`
provides the ammo class used in the inventory calculations. HUD reserve handling
also has a special item-system classification path and uses weapon virtual
`+0x144` in the normal path. Therefore **inventory count is not unconditionally
the same as the HUD's reserve count**, and `+0x24` must not be called a verified
magazine capacity until its actual implementation is resolved for the live
weapon. No production adapter was authorized from these initial static leads.

### Further static fire-mode contracts (not yet observed on the current item)

The common item-to-weapon getter `0x37f6c960` checks null and returns `item+0x1bc`.
Base constructor `0x38254cf0` installs primary vtable `0x399e3d08` and weapon
interface vtable `0x399e3b70`. Derived constructors replace these; one cannot
assume the base vtable describes every actual item.

For interfaces whose actual vtable resolves these exact methods:

- `+0x7c -> 0x3824e130`: current fire-mode index is `[interface+0x120]`.
- `+0x74 -> 0x382532b0`: index must be nonnegative and below the DWORD-vector
  length `[interface+0x54, interface+0x58)`, then returns that element. A missing
  index returns null. Relative to the raw item, those fields are +0x2dc and
  +0x210/+0x214 respectively.

The literal `Single` at `0x399a8994` is passed by `0x38262c5e` to registration
`0x382665a0`, which installs factory `0x38264930` (not `0x382637a0`, which is a
pool cleanup callback). The factory sets fire-mode vtable `0x399e6b28`.

For that actual fire-mode vtable:

- `+0x20 -> 0x38278910`: gets owner item `[fireMode+4]`, configuration holder
  `[fireMode+8]`, parameters `[holder+4]`, ammo class `[parameters+0x7c]`, then
  calls the owner's weapon-interface virtual `+0x64` with that class.
- `+0xb0 -> 0x38278cc0`: directly returns the same ammo-class pointer.
- `+0x24 -> 0x38278940`: starts with signed 16-bit `[parameters+0x8a]`, but can
  return -1 based on global `0x3b8064d1`, owner virtual `+0x10c`, and parameter
  byte `+0x94`. It is not unconditionally the raw parameter field. For the
  observed actor vtable, virtual `+0x10c -> 0x3803dc60` returns actor byte `+0x21`.

Common weapon-interface `+0x64 -> 0x38250890` normally searches a vector
`[interface+0x7c, interface+0x80)` with 8-byte entries `{classPointer, count}`.
Relative to raw item this is +0x238/+0x23c. However its earlier branches include
a fixed 99 result for a particular owner predicate and another object's virtual
getter for a special state. These branches are real game mechanics/AI behavior,
not permission to hardcode 99 into the UI. Raw vector capture alone does not
prove the displayed magazine count in every mode.

Common reserve getter `+0x144 -> 0x3824e070` has linked-owner and debug/special
paths before its ordinary tail call into the owning inventory's `+0x100`.
This reinforces the need to verify real ownership and predicates before using
plain inventory counts as equipped-weapon reserve.

These static findings make the next read small: after a **fresh** actor/inventory
and exact item-system lookup, copy the item's first 0x2e0 bytes, check EntityId
and actual vtable method identities, then copy the current fire-mode's first
12 bytes, its parameter holder, parameters, and bounded class name. The old
process has since closed. Do not use any of its heap addresses for the next run.
Saved item-map traversal ends at read 43 with a left child not captured; the
current item's pointer cannot honestly be reconstructed from that earlier dump.

### Fresh Hammer / Single observation and reload comparison

The later `.work/crysis3/live-202900` session resolved the entire current-item
chain anew rather than reusing heap pointers:

| Evidence | Result |
|---|---|
| 11/14 | Actor `0x32bbb6f0`, owner EntityId `0x7777`; inventory current item `0x109ab` |
| 26-33 | Exact item-system tree resolves item `0x4b7c2eb0`, EntityId `0x109ab` |
| 33-35 | Actual primary vtable `0x399e3d08`, actual weapon vtable `0x399e3b70`, known method targets |
| 51 | Item weak-owner control block has nonzero strong count; owner pointer agrees with actor |
| 52/56 | Fire-mode index zero selects `0x4fb41ee4`, actual Single vtable `0x399e6b28`, back-reference to item |
| 54/63/69 | Item `+0xc` IEntity ID also `0x109ab`; its class resolves to internal name **Hammer** |
| 53/61 | Loaded **HPistolBullet** count changes **8 to 10** across owner's firing/reload request |
| 64/70 | Ammunition class name confirms HPistolBullet rather than an inferred weapon/ammo mapping |
| 65/71 | Other loaded-ammo entry is HPistolExplosiveBullet, zero in both samples |
| 57/60 | After reload, item, owner and current inventory EntityId still agree |
| 73 | Item's shared-data special-magazine flag is zero, selecting ordinary ammo-vector branch |
| 72/76 | Actual fire-mode parameters select HPistolBullet and signed 16-bit clip limit **9** |
| 79-81 | Exact inventory ammo-key traversal finds HPistolBullet inventory count **48**, capacity **54** |

The actual loaded value 10 does **not** establish a clip limit of 10. The traced
native HUD displays min(loaded 10, limit 9) = 9 plus overflow 1. Before reload
the observed 8 rounds yield display 8 and overflow 0. These are code-backed
interpretations of sampled values, not a frame-aligned screenshot comparison.
The inventory count remains separately labeled inventory ammunition rather than
silently claiming all HUD-reserve special branches have been validated.

Type correction: actor/item `+4` is not adopted as IEntity. Fresh actor `+4`
has vtable `0x398a60c0` whose `+0x128` takes an argument and delegates to another
object. Actual actor/item `+0xc` has vtable `0x398eccc0`; its `+0x128` resolves
to `0x378a6cb0`, exactly returning class pointer `[this+0x1c]`. Wrong-interface
virtual slots must never be used as pose or class getters.

`ReadRegularSingle` now implements **read-only, value-only** decoding of this
strict regular path. It refuses unknown/derived vtables, wrong actor/item/entity
identity, mismatched current selection, dead weak-owner controls, non-player
predicate, nonzero special-magazine flag, unsupported clip/count values,
malformed/oversized vectors, duplicate active ammo entries and invalid class
names. It copies both names, loaded rounds, nominal clip limit, display rounds
and overflow; it does not expose native pointers in its output. Initial and final
ownership/configuration/value reads must agree. No equipment actions or reserve
emulation are included. The helper does not increment native weak references.

`NativeWeaponProfile.h` supplies 33 exact `{address,size,sha256}` entries for the
read-contract code and relevant vtables, including HUD overflow presentation.
The host must check its executable/header profile first, then this full table
before passing `profileVerified=true`. That boolean is a required external gate,
not a conclusion inferred from vtable names. The host must also establish game
thread, safe phase, current actor, epoch and lifetime ownership; matching hashes
and repeated bytes alone do not establish those properties.

The expanded synthetic x86 tests pass with warnings-as-errors. They exercise
10/9 -> 9+1, 8/9, zero/missing ammo, derived/unknown table rejection, weak-owner
and selection invalidation, special flags, negative or signed-short-overflow
counts, invalid class names and late changes. The two Python profile tests also
pass, checking all 33 entries against the independently hash-verified captured
text/rdata. These are not multi-mission or installed-mod gameplay tests.

HUD callback `0x382d0eb0` receives these relevant events:

- `OnSetAmmoCount`: name table storage `0x3ba04190`, ID storage `0x3ba04194`,
  ID `0x49` / 73. It reads a weapon-interface pointer at **event wrapper +8**,
  not from the pooled payload, and refreshes real getters.
- `OnSetInventoryAmmoCount`: storage `0x3ba04198/+4`, ID `0x4a` / 74. It
  resolves the HUD's current item and resamples its weapon readers.

Read 31 confirms the two IDs, and reads 36/37 confirm the corresponding copied
name strings. The initializer stores the *previous* ID immediately before
constructing the next name; misreading it shifts these IDs by one. Event `0x48`
has a different pointer/count/class payload; do not label it OnSetAmmoCount.
Event callbacks and engine methods remain uninstalled and thread-unverified.

## Attachment commands and real eligibility

The native command receiver starts at **`0x383baba0`**, not `0x383bac30` or
`0x383bad29` (both latter addresses are inside comparisons). It returns with
`ret 0xc`, preserving three stack arguments even though the decompiler infers
only two meaningful arguments. `attach_primary`/`detach_primary` are compared
against exact captured strings. Both require receiver byte `+0xb2` nonzero;
zero returns handled without applying equipment. The flag's full lifecycle and
receiver ownership are unresolved.

Its presentation meaning is confirmed: at `0x383b3099` the native producer
copies receiver `+0xb2` to a GFx boolean and invokes `setEnableCustomisation`
at `0x383b30c2`. The flag defaults to one at `0x383b9805` and is recomputed by
`0x383b6b87..0x383b6bd0` with a multiplayer/loadout predicate. It should not be
renamed "menu active" or bypassed. At `0x383b369c` the producer calls
`addAttachment_new` with seven GFx arguments; catalogue byte `+0x6a` switches
default entries to another function, matching the decompiled UI's distinction.

Primary item selection `0x383b0f50` resolves current actor, checks its inventory,
then calls actor `+0xdc(0)`. The attach helper `0x383b1db0` takes receiver ECX,
item and native string-reference arguments, ending `ret 8`:

1. Reject empty native string and null item.
2. Lock the item's weak-owner through `0x3821e8b0` (`item+0x104/+0x108`)
   and obtain that owner's inventory. This is not simply the cached client actor.
3. Resolve the named attachment through the game's class registry.
4. Require inventory virtual `+0xbc(class)` ownership (`0x3727aab0`), which
   searches class pointers in `[inventory+0x1c, inventory+0x20)`.
5. Require item virtual `+0x234(class)` to return accessory parameters.
6. Invoke item virtual `+0x250(nativeStringReference)`; optionally update the
   selected fire-mode through the weapon-interface methods.

Detach helper `0x383ba9a0` resolves compatibility, calls item virtual `+0x258`,
then can reattach the default accessory in the same category using the real
accessory catalogue. A UI-only deselection would not reproduce this behavior.

**Do not pass a C string to these calls.** Native string operations dereference
the argument as a string object, and read the data length at `data-8`.
Constructing/destroying that object, exact item-vtable implementations,
receiver lifecycle, main-thread scheduling, selection generation and action
acknowledgement are still required. No mutation is implemented here.

## Bounded offline reader and tests

`src/NativeWeaponRead.h` implements copy-only inventory observations, bounded
item/attachment vectors, exact item-system lookup and ammo-tree lookup. It requires a supplied
read function; it neither dereferences native pointers nor calls game methods.
It fails closed on owner/vtable mismatch, reversed/misaligned/oversized vectors,
changed copies, cycles, more than 64 tree nodes, address overflow and read errors.
Checks before/after are consistency checks, not proof of thread safety.
The host must separately validate the image, methods and update/lifecycle phase.

`tests/NativeWeaponReadTests.cpp` was compiled as x86 with warnings-as-errors and
passed. Cases include count/capacity, true zero versus absent class, signed value
preservation, malformed/cyclic trees, changed reads, owner/vtable rejection and
vector change invalidation. These are synthetic tests, not gameplay validation.

Relevant exact function fingerprints from the captured text:

| VA | Bytes | SHA-256 |
|---|---:|---|
| `0x3803e6b0` | 4 | `68b70ba1e6ad2356c69cac8e846649e599945994fa7690a0bf4af28fa25c24a5` |
| `0x380419b0` | 93 | `acd77862b5bf5c7f116723cd65d7153805b9b6b89bb8dbb40c81974aa3bf1691` |
| `0x37279e30` | 7 | `923d1a98e397f42b18b1fe647cb0d836cdfa25ef31f2788ff06a51f12dcb3c64` |
| `0x3727aba0` | 63 | `fe431452250ab2ff424071470aa52cc798676c30d4057e8a6bf10f6c6d23abaf` |
| `0x3727abe0` | 63 | `ca493c2216abee3a79a3ad4d1d151c03303ff6e309d59a2a8e4f477014d50a08` |
| `0x37283230` | 69 | `3e04e9fe7b41f9529aeca8de6bd4ac7b81badbf3c92f2dc07134fadd264e8edf` |
| `0x3727aab0` | 47 | `35cbffc9ae643a78e7441b0d180b6735502b8b2fc14c0dd37b2ec1c22bc9036d` |
| `0x378b9070` | 4 | `916ae10aa3fd76a4d5b25b2fb66e9df0fd04957c7ab10930268d1ed7808be689` |
| `0x37f6c960` | 14 | `d712a02d1b418a7a9d9629085c013d249e7a89dd6a60f6190a76fd5dff89a791` |
| `0x3824e130` | 7 | `c4ead592093c82fcaec09324e11beabd00c48b6e26ef7f17fbd1695949037ade` |
| `0x382532b0` | 35 | `1b04fc435239a67f9b3563c933ff3de782efb9c2d5bf60ca2e43fa88b0ff8a65` |
| `0x38278910` | 33 | `a201873e07e881f4b6aae2e3325776692c65c4c59e9d22fca413e50204606146` |
| `0x38278940` | 83 | `4f65c0460a2233306ebace56796dc5f3c680145591685561d50e1fade5367e91` |
| `0x38278cc0` | 10 | `9730c1f45c057f6297a68050e8fff11a5386756dd56f60271a4dc979629febff` |
| `0x38250890` | 178 | `06ac22d6fada12219cc87fa80e133b520ca3eb06404300dcf7c3f657fe65b25e` |
| `0x3824e070` | 177 | `dd0ee2426d6d43ee569b93d497f5bfde16ecb05231cfacd5ff99675dc699f745` |
| `0x3803dc60` | 4 | `c979f6eaf0fee01e57ad97bb5e1859f15517e30c80300980833adce41c95f73d` |
| `0x3821e8b0` | 64 | `d84815733d2ac1463ea382556c7578afae5ac3924dd72a5ae9897b0b462a5812` |
| `0x378a6cb0` | 4 | `0b78473b3a56c63ff2a64431fb600078066cbe407d3dee7ea8fd7575bd7f82d5` |

Independent Ghidra project: `%TEMP%/Crysis3WeaponResearch/Live190400`.
Reports under `.work/crysis3/live-190400/weapon-*.txt`; decompiler argument
recovery is not authoritative. All critical offsets above were checked against
instructions and observed vtables, not inferred pseudocode types.

## General owned weapon names, separate from ammunition

`ReadOwnedWeaponName` now returns only an owned weapon's EntityId, internal
class name and whether its ID equals inventory `currentItem`. It does not name
inventory-order positions “primary” or “secondary”, call equipment actions, or
adopt Single fire-mode offsets for other weapons. Host authorization requires
both `NativeWeaponProfile` and the new `NativeWeaponNameProfile`, the verified
game-thread phase and the existing epoch/actor/selection publication guards.

The additional profile contains two common CItem methods and 25 exact primary
vtable fingerprints. Each accepted table has `+0x18 -> 0x3821bac0`,
`+0x64 -> 0x3821bb70`, and `+0x128 -> 0x37f6c960`. The first method refreshes
`item+0xc` IEntity and `item+8` EntityId from IGameObject, the second returns the
owner EntityId at `item+0x100`, and the third returns the non-null embedded
IWeapon interface. These facts justify common identity fields, not the layout
or mechanics of a derived fire mode. Full code and table hashes are in the
profile header; the capture regression verifies all hashes and all three
virtual targets in every accepted family.

The reader requires exactly one matching inventory ID, current item-map lookup,
matching owner ID and weak-owner actor pointer, a live nonzero weak control
count, matching IEntity ID, and the known IEntity/class tables. All data is
copied into bounded local storage; it repeats owner, item, entity, inventory
membership, selection and map lookup checks before returning value-only data.
Repeated-copy stability is not an atomic snapshot or an independent lifetime
guarantee. Host phase/lifecycle guards remain necessary.

Fresh session `live-205200/read33` captured primary table `0x399e8fc8`, embedded
weapon table `0x399e8e30`, EntityId `0x10818` and owner ID `0x7777`. The main
screen showed Predator Bow. Read41 confirms the matching IEntity ID and read42
the weak-control table/count. Read55/read62 confirm internal class `NanoBow`. These
heap pointers are session-local evidence, never profile constants. The later
read44 of an earlier magazine-vector address no longer resembles class/count
pairs; it must not be used to publish ammo or justify a layout. Refetch the
item and its current vector before any follow-up read.

Read58 refreshes that item's changed vector pointers. Read59/read60 then capture
fire mode `0x399e54c8` with the correct item owner. Its `+0x20`, `+0x24`, and
`+0xb0` targets are the same count/clip/class methods as Single. The actual bow
IWeapon table `0x399e8e30` likewise shares `+0x64`, `+0x74`, and `+0x7c` targets.
This is method-level evidence for common getters, not permission to present
bow ammunition as a rifle magazine. Decoded patch `NanoBow.xml` defines
`clip_size=0`, `type=BowCharge`, and SP arrow `amount=0`; its stock resides in
inventory resources. The refreshed read61 contains four valid class/count
pairs, all zero. The old Single helper intentionally rejects clip zero; do not
remove that check and interpret zero magazine rounds as zero remaining arrows.

Synthetic name-reader regressions cover all 25 accepted families without
accessing fire-mode/ammo state, equipped versus owned-only status, unknown
tables, wrong owner/entity IDs, absent/duplicate inventory membership, dead
weak controls, and late actor/inventory/map/entity/selection mutations.
These tests establish bounded fail-closed behavior, not live gameplay coverage.

## Additional attachment record trace (builds/installations held)

New report `live-190400/attachment-records.txt` decompiles targets selected from
the captured Hammer primary table399e3d08. Assembly independently confirms:

- Virtual+234 ->382259e0 searches shared data at item+14, vector[shared+8,
  shared+c), **stride0x74**, comparing each record's first word with the requested
  attachment class. It returns the matching record, not a boolean or an item.
- Virtual+230 ->38225a30 searches the same vector and checks the matching bit
  in item+1b4 (16-bit mask). A zero bit yields true. This is a separate predicate
  from class membership; its full native UI interpretation still needs tracing.
  Do not label every compatible class owned or currently equip-able.
- Virtual+240 ->38226380 walks item+70 count and inline entries at+74,
  **stride8**, consisting of class and entity ID. It resolves the accessory
  entity through the item system and joins the class with+234 parameters.
  This establishes a mounted-accessory collection separate from inventory's
  owned attachment classes at+1c/+20. Capacity, mutators and live records still
  require verification before publishing an installed attachment list.

These offsets describe the observed CItem methods, not permission to adopt
them for every derived item. Live per-item method/profile identity, weak owner,
class identity, bounded vector/count checks and post-read revalidation remain
mandatory. No attachment commands or read producer were enabled by this trace.
# Attachment publication checkpoint — 2026-09-08

`NativeAttachmentRead.h` now reads compatible class records (0x74 stride),
inventory-owned attachment classes, the per-record native enable mask, and
mounted class/entity pairs. The reader is connected to the first two loadout
rows in NativeProbe's Weapons-page transaction. It repeats owned-item lookup,
inventory vectors, item bytes, shared-record bytes and identities before
publishing pointer-free names and states. The existing outer actor/phase/epoch
guard still invalidates the entire sample on transitions. Profiles include the
accessory lookup/mask/mounted functions and the extended item vtable.

Scope is deliberately limited to the captured 0x399e3d08 item family, at most
12 compatible records and at most four mounted records (an observation budget,
not a native capacity assertion). Other layouts and larger records fail closed.
Mounted labels reflect inline records; no default mount is inferred from an
empty group. Inventory absence is displayed as NOT OWNED, not an invented
unlock requirement. Exact class names are used pending localized presentation.

Synthetic tests cover mounted/removal/holstered reads, unknown profile, wrong
owner, malformed vectors, oversized lists, unresolved mounted classes, missing
entity IDs, mutations, unterminated names, and publication without action grants.
The captured-code fingerprint/vtable test passes. This source is not installed
or live-validated yet. The final probe DLL rebuild (including presentation
fields) passed, SHA-256
`740c36be2f2d90e2f983cfb5850f9061cf75b2a450f58b9d18d4f2e6b63ee34a`.
The complete development harness and all 48 Python tests passed. No payload
manifest, APK, or installed device files were changed in this checkpoint.

Equip/remove commands remain disabled. Assembly confirms 383b1db0 receives the
UI receiver in ECX and two stack arguments (item and CryString reference), RET 8;
the decompiler's unaff_ESI is not a trustworthy ABI declaration. Receiver
lifetime/customization state, CryString ownership, native detach/default restore,
and one-shot game-thread command dispatch still require validation. Merely
finding owned/compatible classes does not authorize calls to this function.
