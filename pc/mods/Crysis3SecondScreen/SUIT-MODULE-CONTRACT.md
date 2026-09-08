# Native single-player suit modules: bounded read-only contract

Status: statically traced exact native image plus separately captured live values;
offline replay passes. This is not gameplay validation of a module change, purchase,
checkpoint, or save load. No commands are authorized by these findings.

## Evidence and ownership

Uses the exact source/header/text/rdata identity in `ACTOR-CONTRACT.md`.
Loaded text SHA-256 `e2cb3e908f599a2bb90c338a04ef5bec2a89b8a563e9f5f985374b153544989f`;
rdata `376e0b44ad449a52aa98a5ac8fde81cef9819306be0cf41ef3b6074e3f3d17fe`.
New Ghidra reports under `.work/crysis3/live-190400/`:
`suit-module-entry.txt`, `suit-module-core.txt`, `suit-module-producer.txt`,
`suit-module-layout.txt`. Ghidra inferred signatures are not callable contracts;
assembly was checked for the offsets, argument order and ownership below.

CGame initialization at `3818bcaa` allocates 0xedc bytes, calls constructor
`38594300` at `3818bcbd`, stores the resulting manager at root+1f8 at
`3818bcc6`. Constructor writes primary vtable `39a45a1c`, secondary
`39a454d0`, initializes count+8, initializes points+ed8 to -1, and publishes
global `3ba12080` at `3859436f`. Destructor `385941a0` clears that global at
`385941da`, destroys inline records, then resets the count. Retaining this
pointer across callbacks is unsupported.

Stats owner root+d4 is allocated at `3818bc88`, constructed by `3841d8d0`
at `3818bc9b` and stored at `3818bca4`. Its primary vtable is `39a1a6b4`
(constructor write `3841d926`). Getter `3840cfb0` is exactly
`return *(uint32_t*)(this+0x22c+index*4)` with `ret4`. Native module points
refresh `38592830` passes index 0x2f, so authoritative points are stats+2e8.

## Module record and native UI mapping

Manager+8 is count; records are inline manager+0xc, stride0x98, capacity16
(parser `38593c70` bounds and construction `38594041`). The suit movie native
publisher is `383b6c00`; it invokes `clearPerks`, `addPerk` with ten arguments,
`addPerkStat`, then `updatePerks`. The extracted ActionScript
`.work/crysis3/ui-decompiled/Suit/scripts/frame_1/DoAction.as` independently
names those arguments in the same order.

| Record offset | Meaning and provenance |
|---|---|
| +0 | Inline container vtable 399984a0, observed in every record |
| +4, +8/+c | Underlying perk count (1..2), and perk indices |
| +18 | Module ID string pointer; publisher argument 0 |
| +44 | Slot/category string pointer; argument 1 |
| +60 | Module class string pointer; argument 6 |
| +90 | Unsigned purchase cost; argument 9 |
| +94 | Slot index, assigned by category parser385931a0 |
| +95 bit0 | Purchased; argument3 |
| +96 bit0 | Maximum upgrade achieved; argument7 |
| +97 | Native eligibility cache flag; contributes to argument4 |

Argument2 is a localized name constructed from `@ui_module_%s` (rdata39a0dba8)
with native ID;
argument8 is native localized description composition. Raw IDs are supported
by the DTO; localization is a separate asset lookup, never an assumed display
name. `class` strings describe grouping, not separate selectable Power/Speed
suit modes.

The extracted English `text_ui_messages.xml` SHA-256 is
`608a7037a7f6e11feb76b29970365b2e425bdd93bbed0aeee9678063a651d96e`.
Its actual labels include BulletProof=Deflection, StrengthModule=Super Strength,
EODPackage=Threat Detector, MedicalMonitor=First Aid, WeaponDrill=Weapon Handling,
and SensorPackage=Sensor Upgrade. Raw IDs must not be assumed to be English
display labels.

Native installed query `38592b60` resolves current player via the already
documented root/framework client-actor route. It tests the first underlying
perk in actor+13c0/+13c4 as a 64-bit mask. Shift helper `3863ee00` produces
`1ull << index` for indices0..63. The native module-index mask manager+ed4 is
a different domain, updated by toggle `385926d0`. Never substitute one for
the other or require installed=>purchased.

Publisher's `unlocked` means raw eligibility nonzero AND
`max(cachedPoints,0) >= unsigned cost`. It is not a callable capability.
Native points refresh `38592830` copies stats index2f into manager+ed8 only
when changed or forced, then calls `385925c0`: unpurchased/eligible modules
become ineligible when points are zero, and ineligible modules become eligible
when points are nonzero. A closed suit menu can have stale cached points and
flags. Reader publishes authoritative stats points, and leaves `unlockedKnown`
false whenever live points differ from cached points. It does not pretend to
have run that refresh or use the stale cache as a resources count.

## Fresh observations, live-205200

All files in `.work/crysis3/live-205200`; metadata explicitly says
`atomicSnapshot:false`. They corroborate fields individually, not simultaneous
mutation safety. Heap addresses below identify this evidence only.

- Root read2+1f8 and global read37 both give manager33399f48.
- Read39 has primary39a45a1c, secondary39a454d0, count16.
- Actor32cfb470 read38 at+13c0 has mask `0000000010004000` (bits14,28).
- Read40 at manager+ed4 has module-index mask0x12 and cached points0.
- Read46 includes all2432 record bytes, with all ID/category/class pointers
  targeting strings inside their own record. SHA-256
  `8e46668115d9e138a94a833775c8559ead1f2f5b491c20b4c6591e6b6b1d5583`.
- Root read2+d4 gives stats33365070; read57 matches constructor vtable,
  SHA-256 `3cc27872c4569744664d205571156a8583063078eec3c70c152344b112b9a5e2`.
- Read53 at stats+2e8 has authoritative points4, not cached0. SHA-256
  `fb5e512425fc9449316ec95969ebe71e2d576dbab833d61e2a5b9330fd70ee02`.

| Runtime index | Native ID | Perk indices | Cost | Installed |
|---|---|---|---|---|
|0|SuperStealth|10,8|1|false|
|1|Endurance|28|2|true|
|2|BruteForce|0,1|3|false|
|3|SensorPackage|30|1|false|
|4|BulletProof|14|1|true|
|5|ReflexBoost|4|2|false|
|6|EnergyUpgrade|29|2|false|
|7|EODPackage|11|1|false|
|8|ExtendedCloak|5|3|false|
|9|WeaponDrill|6|1|false|
|10|StrengthModule|25|3|false|
|11|MedicalMonitor|18|1|false|
|12|HeavyArmor|23|3|false|
|13|Verticality|3|2|false|
|14|LightArmor|22|1|false|
|15|StealthKill|27|1|false|

All purchased bits and maximum bits are zero in read46, including installed
Endurance/BulletProof. All raw+97 flags are1. This is an observed configuration,
not a reason to fabricate purchased states. Runtime order here matches the
extracted `Scripts/HUD/SPPerksDef.xml`; reader still follows native IDs.

## Implementation and remaining gates

`NativeSuitRead.h` copies at most16 records, bounds every address/string,
requires root/global manager ownership, stats/actor identity, two equal copied
reads and an unchanged attested callback epoch. It has no allocations, no game
calls, no escaped game pointers and no mutation. Caller must additionally join
the current-player identity and phase against `NativeActorRead`, attest all
`NativeSuitProfile.h` fingerprints, and invalidate on transitions. The sixteen
fingerprints are split into <=4096-byte ranges for the existing read gate.

`NativeSuitReadTests.cpp` replays saved captures and tests altered ownership,
layout, invalid strings/perk bits, absent profile, stale menu cache, refresh and
epoch change. Offline replay gives16 modules, Endurance/BulletProof installed,
four authoritative points, unknown cached eligibility. This does not test
actual gameplay scheduling, menu refresh, save loading or module changes.

Progress remains unavailable: native maximum evaluation `38592df0` iterates
manager+994 entries of stride54 and only refreshes active, not-yet-maximum
modules. `38592c10` writes an averaged, clamped fraction to entry+0 from up to
two native stats requirements, but this is a cache with freshness implications,
not a globally live percentage. Maximum flags are supported separately.

Actions remain unavailable. Native AS emits `toggle_perk` and `buy_perk`;
receiver383b4cc0's purchase branch calls38592620(name,false), then38592830(0)
and38592dd0(name). Purchase38592620 checks eligibility==1 and !purchased but
does NOT check affordability itself: it sets+95=3 and debits cost through
stats3840c570(index2f,-cost). Toggle385926d0 calls player380db570 for each
underlying perk and changes manager+ed4. Blindly calling these bypasses UI
constraints and is unsafe. Native ABI, thread, transaction and interaction
validation remain required before any touchscreen module action.
