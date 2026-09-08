# Map and objectives: exact-build research contract

This document records static traces and bounded live observations, **not a
release-ready native adapter**. No game calls or hooks were installed by this
work. `NativeMissionRead.h` only decodes copied diagnostic bytes and reproduces
projection math; it cannot mark `Snapshot.adapterVerified` or authorize actions.

## Evidence

Native addresses are VAs from the exact image at `0x37000000`, captured under
`.work/crysis3/live-190400/capture`. Full image/source fingerprints and capture
limitations remain in `NATIVE-RUNTIME.md`. The independent analysis project is
`%TEMP%/Crysis3MapAgentResearch/MapAgent`, not the shared Live190400 project.
Reports are `map-agent-contract.txt`, `map-agent-contract2.txt`, and
`map-agent-objective-system.txt` in the capture's parent. Their decompiler
argument recovery is unreliable in several optimized functions. The offsets,
argument order and formulas below were checked against the captured x86 bytes.

## Native objective collection

The main map transaction at `0x383b4e10` loads `CGame` through `0x3b9fde30`,
then reads the objective-system pointer at root `+0x104`. Lua
`SetObjectiveStatus` also follows this root member (`0x382ce510`).

The objective vector's begin/end fields are at system `+0x1c/+0x20`.
The transaction iterates **40-byte (`0x28`) records**, not pointers.

| Record offset | Traced meaning | Evidence |
|---|---|---|
| `+0x04` | Objective name/title localization-key pointer | Fresh live key plus objective-definition XML; passed as movie `_description` |
| `+0x08` | Objective description localization-key pointer | Fresh live key plus objective-definition XML; passed as movie `_message` |
| `+0x0c` | Objective identity string pointer | Lookup `0x382c3a30` compares it with the requested ID |
| `+0x10` | Tracked entity-name string pointer | Name lookup if cached entity ID is absent |
| `+0x14` | Cached tracked entity ID | Resolved through the entity system, not a pointer |
| `+0x1c` | Native objective status | Setter `0x382c3fa0`, Lua constant registration and map conversion |
| `+0x20` | Status-change time candidate | Setter reads engine timer and stores a converted float; units not adopted |
| `+0x24` | Silent flag | `SetObjectiveStatus` copies its silent argument here |
| `+0x25` | Secondary flag | Passed as addObjective secondary argument |

The movie argument names are misleading: it displays the objective description
as `_message` and uses the objective name as `_description`. The initial static
trace's provisional field labels were corrected after fresh live keys and the
definition XML established actual name/description ownership. Do not infer UI
field semantics from the movie's parameter names alone.

Unknown members are deliberately not named. String pointers are not stable UI
keys and may not cross thread boundaries. Native title/description values are
localization keys; the movie producer calls `0x384a3120` to obtain UTF-16 text.
That routine's ownership/ABI is not verified for external calls.

Lua constant registration around `0x382ce210` explicitly defines:

| Native value | Name | Map movie value |
|---|---|---|
| 0 | MO_DEACTIVATED | Omitted |
| 1 | MO_COMPLETED | 1 |
| 2 | MO_FAILED | 2 |
| 3 | MO_ACTIVATED | 0 |

Do not copy native enum integers directly into the movie's status field.
The status setter rejects a completed-to-failed transition. It writes the new
status before publishing native HUD event **154 (`0x9a`)**. The pooled payload
contains exactly two entries: the objective-record pointer, type byte **4**;
then a boolean, type byte **3**. The second boolean depends on the silent flag
and another setter condition; it is retained as `rawFlag`, not interpreted as
completion or visibility. Only the low byte of type and boolean are initialized
by the producer. Upper bytes must not be compared to zero.

The event is synchronously dispatched through `0x382c2100`, then its pool slot
is released. A future listener must copy the record and bounded strings before
returning. It still needs same-thread ownership, generation invalidation,
collection initial state, save/load/reset verification, and event subscription
safety. A copied pointer alone is never usable telemetry.

### Fresh live collection observation

Main-agent read-only inspector evidence `.work/crysis3/live-200610`:

- Read 11: current root `0x3b9f99b0` has objective system `0x026e8c78`.
- Reads 15/17: 64-byte system header matches before/after the vector copy;
  observed vtable `0x399f32c0`, begin `0x4feb51d8`, end `0x4feb6150`.
- Read 16: 3,960-byte collection, **99 records**. There are 93 deactivated,
  five completed, and one active record. No failed records were observed.
- Read 18: root identity remained unchanged after the copy.
- Active record index 91 at `0x4feb6010`: status 3, secondary 0, silent 0,
  tracked entity `0xad7`; title key pointer `0x071e901c`, description key
  `0x071ee3cc`, identity pointer `0x071e9044`.
- Reads 25/26 copy these strings: name `@fields_objective_03_name`, description
  `@fields_objective_03_description`, identity `Fields.Fields_Objective_02c`.
  The original objective-definition XML in GameData/Levels/Fields agrees with
  all three, independently of key suffixes. Saved English localization yields
  **Rendezvous with Psycho** / **Find a way up to Psycho's position**. This
  identifies an active Fields objective, not the original capture's Jailbreak
  mission. It does not by itself prove the current level path or mission title.

These heap addresses are observations, never release constants. Equal metadata
around the copy does not prove atomicity or transition safety. Native collection
capacity exceeds the UI's 64-objective capacity: copy a bounded native collection
and filter by traced state before publication; never silently claim a truncated
collection is complete. Changes in mission objective status have not yet been
captured in an installed callback.

## Map producer and movie ABI evidence

| Native target | Role |
|---|---|
| `0x383b4dc0` | Menu object construction: size `0x2cc`, vtable `0x39a0d434`, common map component at `+0x10` |
| `0x383b1650` | Binds sub-screen movie, then calls setMapBackground when map enabled |
| `0x383b4e10` | clearAll, player/objective/unit/map-icon collection, updateObjectives |
| `0x383b1ca0` | Projects and adds one icon, returns projected coordinates through caller storage |
| `0x383b24c0` | Projects objective and localizes its two strings before addObjective |
| `0x383b5480` | fscommand receiver: literal `update` invokes the collection transaction |
| `0x383b17c0` | Menu tick adjusts zoom/help; it does not call the collection transaction |

The native movie pointer is menu owner `+0x2b8`. Calls use its virtual `+0x8c`,
with method string, typed-argument array, count and result pointer. This is a
native Flash invocation lead, **not** permission to detour a generic vtable or
call inferred signatures. Observed argument records have stride 16: type at +0,
value at +8. addIcon sends five arguments, addObjective eight. Float type is 6,
UTF-8 string 7, UTF-16 string 8, integer 3 and boolean 2 in these calls. This
is distinct from native HUD event type bytes.

`setMapBackground` receives owner `+0x204` (path) and owner `+0x2a4` multiplied
by captured radians-to-degrees constant 57.2957763671875. The code only sends
the background when owner `+0x2b4` is enabled.

**Synchronization caveat:** the movie transaction is driven by its `update`
command, including navigation; merely intercepting addIcon/addObjective does
not establish continuous gameplay updates with the menu closed. Do not refresh
old menu captures as current state. Objective events/collection and real actor
sources must be connected independently.

## Projection and visibility

Common map component `0x382cd630` computes:

`u = (worldX - startX) / (endX - startX)`

`v = 1 - (worldY - startY) / (endY - startY)`

Bounds are component `+0x27c/+0x280/+0x284/+0x288`; component enabled flag is
`+0x2a4`. Disabled projection returns (0,0), which must not be displayed as a
real player position. XML attribute loads at `0x382cdd5e` onward confirm startX,
startY, endX and endY; the loader validates positive extents and a positive scale
before setting the enabled flag. Component `+0x28c` is the scale returned by
`0x382cd530`; `+0x298` is loaded from menuMapZoom. For this menu object, component
offsets are relative to owner **+0x10**, not to the owner itself.

The menu then rotates (u,v) around (0.5,0.5) using `0x383b1730`, with owner's
`+0x2a4` rotation in radians. `0x383b1ca0` and `0x383b24c0` multiply rotated
coordinates by the captured **565.0** double at `0x39a0d3f8` for movie-local
coordinates. Neither movie coordinates nor world coordinates may be blindly
treated as the renderer's normalized unrotated map coordinates.

Icon producer sends rotation in degrees, incorporating the map rotation. Its
direction comes from atan2-style math over the supplied forward vector; further
view-direction and cutscene tests are required. The player source uses the
framework's active view path (`0x383b1370`/`0x383b13e0`), not a demonstrated
always-player entity. This matters in cutscenes/spectator views.

Enemy/friend icons are not all live entities: the transaction requests category
2 from `0x381bb230`, skips absent entities, checks entity flags and actor
availability, and combines a per-entry flag with a framework relation check
before emitting `enemy` or `friend`. Category 10 entries use script property
`sMapIcon` and additional eligibility. The category enums and flag semantics
are not fully named or runtime-verified; bypassing them would expose hidden
enemies. No enemy discovery is inferred from an icon label.

The Flash movie has 32 marker clips. It hides existing clips before update,
assigns movie-local position/rotation/scale, and changes the icon's frame by
string. Main objectives are inserted in reverse order. Nonzero-state secondary
objectives are omitted. `_shouldShowOnRadar` is retained as data but the saved
movie code does not use it to hide the just-added marker. `_done` is undeclared
in addObjective, so its MainObjective assignment is not a trustworthy source.

## Artwork audit

Saved indexes were rechecked case-insensitively: Scripts (865), GameData (3970),
UI (510), Patch (3826), English (35), Jailbreak level (123), MenuSP (80), remote
Textures (9924), Objects (21425), Startup (1193), and Jailbreak dds0 (2007).
No indexed entry is a Minimap/*.gfx asset. UI contains
`Libs/UI/Overlays/Jailbreak.gfx`, a different virtual path; there is no traced
alias establishing it as the missing map, so it is not substituted.

MenuSP's `levels/jailbreak/jailbreak.xml` references `Minimap/Jailbreak.gfx`,
world bounds (-18.945923,105.72681)..(501.05408,625.72681), scale 4,
radarRange 10.2, menuMapZoom 4. `mission_mission0.xml` has generic editor 1024
bounds. These definitions are not interchangeable. Live map-component identity,
its actual loaded metadata, and the correct map artwork remain unresolved.
No multiplayer image or unrelated level overlay is adopted as a substitute.

The later live objective belongs to Fields. Its MenuSP metadata references
`Minimap/Fields.gfx`, bounds (-61.861328,-863.19165)..(2138.1387,1336.8083),
scale 3, radarRange 57 and menuMapZoom 3. This file is likewise absent from the
checked indexes. Fields metadata must not be activated merely because an
objective ID starts with Fields; verify current level and live map component.

## Focused tests

`tests/NativeMissionReadTests.cpp` compiles under the existing MinGW32 C++17
toolchain with `-Wall -Wextra -Wpedantic -Werror` and passes. It covers exact
record lengths, four traced native states and movie conversion, byte-sized
flags, malformed payload count/type/pointer rejection, projection Y inversion,
center rotation, non-finite inputs, degenerate bounds and unclamped off-map
points. These are synthetic tests, not live hook or localization validation.

## Authoritative current-level chain (static)

Framework getter `0x3749cb50`, vtable `0x398ba5e0` slot +0x70, returns
framework `+0x4ac`. Framework initialization at `0x374a67e3/0x374a67ee`
constructs/stores the level system here. Its constructor `0x37226ed0` installs
vtable `0x398868a0`.

That system's virtual +0x38 is `0x378a6cb0`, returning system `+0x1c`, the
current-level object. The loading function `0x37225190` allocates and stores
this object (`0x372252a3`). Object constructor `0x37221940` installs primary
vtable `0x398862c4` and embeds level-info at **object +4**, vtable
`0x398862d0`. Level virtual +8 returns this embedded info (`0x37221910`).

Info virtual +4 (`0x374c4970`) returns info +4 name pointer; the level lookup
`0x37222c30` compares that string with the requested level and also supports
path-basename comparison. Info virtual +8 (`0x378b9070`) returns info +8 path
pointer. Metadata loader `0x37224cd0` appends the literal `/LevelInfo.xml`
(`0x398866c0`) to that path. Therefore the minimum current-level header is:

| Current-level offset | Meaning |
|---|---|
| +0 | `0x398862c4` current-level vtable |
| +4 | `0x398862d0` embedded level-info vtable |
| +8 | Level name char pointer |
| +12 | Level filesystem path char pointer |

`DecodeLevelHeader` validates this copied header for the inspected exact build.
The current object is assigned **during loading before all load checks succeed**;
it does not establish a playable phase. The reader must recheck level-system and
current-level ownership, verify fresh bounded strings and obey the independently
established loading/generation gate. All old heap pointers are stale after the
owner saved and closed the game.

## In-game minimap owner and entity pose

The in-game HUD map is separate from the menu map. Constructor `0x382f2e70`
installs primary vtable `0x399f89e8`, an event-listener interface at owner +4
with vtable `0x399f89e0`, and common map component at **owner +0x148**.
The event thunk `0x382f3050` adjusts this by -4; primary handler `0x382f2b20`
handles objective event 154 and map-load event 212 (`0xd4`). Its component calls
the same projection/configuration code traced above. This provides a concrete
owner-discovery path via the actual objective-listener vector, without scanning
arbitrary heap memory or creating a hidden menu.

Common component +0x278 begins a 48-byte metadata tail; the implemented
`DecodeMapMetadata` preserves enabled, bounds, scale and rotation. On the HUD
owner this tail starts at **+0x3c0**, with bounds at +0x3c4..+0x3d0 and enabled
at +0x3ec. The path pointer is component +0x1f4, hence HUD owner +0x33c. A
disabled map must stay unavailable; zeros returned by disabled projection are
not a legitimate origin. No live HUD-map owner has yet been captured.

The objective-event table initializer creates `OnObjectiveChanged` at global
`0x3ba04418` and stores its following ID field at `0x3ba0441c` as 154. Fresh
read 46 found a name pointer and ID `0x9a`; the name string itself still needs
the bounded live cross-check before subscribing by name.

Named entity pose evidence from Lua registration is independent of active view:

| Name / registration | Native binding | Entity virtual |
|---|---|---|
| GetWorldPos, `0x37828adb` | `0x3781bf20` | +0x10c |
| GetWorldDir, `0x37828b05` | `0x3781bf90` | +0xec |
| GetWorldAngles, `0x37828b2f` | `0x3781c460` | +0x124 |

The initial observations did not resolve the actual entity vtable. **Correction
from the later fresh session:** actor/inventory +4 is the game-object/extension
owner, not the IEntity on which these virtual indices apply. The actual IEntity
is actor **+0x0c**; it must be reacquired and validated against the current entity
ID. Applying pose indices to the +4 interface would reach unrelated functions,
including cleanup code. No such functions were called. See the fresh pose
validation below; no virtual invocation is needed for the copied-value reader.
The native objective marker uses entity virtual +0x70 bounds and their center,
not necessarily the origin returned by GetWorldPos. This distinction must be
preserved for accurate marker placement.

## Exact captured function fingerprints

These spans exclude following padding and include the return instruction. They
are byte-identity gates for this image, not proof of thread/lifecycle safety.

| VA | Bytes | SHA-256 |
|---|---:|---|
| `3749cb50` framework level-system getter | 7 | `7e42d22dd5a41ef8347942880cde9b3fe6c6b133d345e859768c7341aa561f2d` |
| `378a6cb0` current-level getter | 4 | `0b78473b3a56c63ff2a64431fb600078066cbe407d3dee7ea8fd7575bd7f82d5` |
| `37221910` embedded level-info getter | 4 | `4181cb1fec090d6487f323c8ce84a17ff37429613d7843b4f5f40f5eae266b77` |
| `374c4970` name getter | 4 | `f7e0208f5e988ae39c7a45dfddbd12921d02f9c3deb5695d4c534c8ab0ccdb0f` |
| `378b9070` path getter | 4 | `916ae10aa3fd76a4d5b25b2fb66e9df0fd04957c7ab10930268d1ed7808be689` |
| `382cd630` projection | 106 | `a47ee42796ca46392d60563cecea198e57b35dc9241add38c59057af90f5df15` |
| `383b1730` normalized rotation | 139 | `a1f69101c595a966f34b9e36dd1aeeb39537ab29af2d553b18c53894de1c5381` |
| `383b4e10` menu marker transaction | 1635 | `0d9d0f0632b267e14c7947db30fc58ba5634fa1f100a72e7e7d5d8cc2e0900d0` |
| `3781bf20` Lua world-position binding | 102 | `e1f64f94489e90336589a5ebe88e66360629bfa13f0ad5c4baf16352abba7248` |
| `3781bf90` Lua world-direction binding | 88 | `d9e970b26ee29be068f38aa6664afbc2c75a67cf6ec4987628e32563d9ffeb56` |
| `3781c460` Lua world-angle binding | 142 | `0ccd084dc7a7a440f9da3c704f7b8a6fda110c4446ffa496219a41a1021d8b74` |

## Minimal next-launch read checklist

1. Reacquire current root/framework with existing exact-build checks. Read
   framework +0x4ac (4 bytes), resulting level-system header (32 bytes), its
   current-level header (16 bytes), bounded name/path strings, and reread ownership.
2. Read dispatcher table pointer `0x3ba03f44`; its objective-event vector is at
   table +154*12. Bound/validate begin/end before copying listener pointers. Read
   each small header until interface vtable `0x399f89e0` is found. Its owner is
   listener-4. Validate primary vtable before reading owner +0x3c0 (48 bytes)
   and owner +0x33c (4 bytes, then bounded path). Recheck owner/list membership.
3. Reacquire the actor's entity pointer and ID; copy entity header and vtable
   entries +0x70,+0xec,+0x10c,+0x124. Trace exact targets before adding a pose
   reader. A bounded walking/turning comparison and a cutscene check are needed.
4. Reacquire objective vector and each tracked entity under matching generation.
   Compare an active objective's native marker position against gameplay;
   preserve missing/untracked state rather than placing it at map origin.

An explicitly labeled tactical coordinate/radar view may use real, verified
positions without a background image if the original artwork remains absent.
It must not claim an extracted terrain map, must identify units/scale, and must
retain the native visibility/discovery rules. No such view is authorized by the
static traces alone.

## Private English localization catalog

`tests/objective_catalog.py` is a deterministic research build/lookup tool.
It validates the exact extracted `text_ui_objectives.xml` SHA-256
`e9e4201461bb0d28c53a4bd768e59bbf52f4c22cfb483d1e558e85791ae1c668`, the original
English archive fingerprint and extracted entry length/CRC. It uses the actual
spreadsheet KEY / TRANSLATED TEXT headers, preserves UTF-8, rejects duplicate
keys, and never falls back to ORIGINAL TEXT. Its output is private game content:

`python pc/mods/Crysis3SecondScreen/tests/objective_catalog.py .research/crysis3/extracted/English/text_ui_objectives.xml .research/crysis3/extracted/English/_extraction-evidence.json NEW_PRIVATE_OUTPUT.json`

The generated 298-entry catalog is at
`.work/crysis3/dev/objective-catalog-english.json`, file SHA-256
`a1721b30bf42a2e6725b90520b3e347c5563f41cd3bfb1f2d86131b2e85405d9`.
Canonical entries SHA-256 is pinned in the loader:
`6a0eaea3544b95dc65f25346e24f2c4d4ee00ae079ccdf0fb25f4aaf64a8ae9e`.
It rejects altered/rehashed content and duplicate JSON fields. Lookup requires
an explicit confirmed language of English and returns unavailable for unknown
language/keys. The current game's language remains unverified, and this is not
a deployed C++ localization backend. Four focused Python tests pass. Archive
signature/vendor authenticity are not claimed.

## Regression review and compact capture order

The metadata decoder now rejects enabled bounds whose individually finite
endpoints produce a non-finite span. All decoder failure paths clear their
output, so a previously enabled map/valid objective pointer cannot survive a
malformed replacement sample. Disabled metadata remains explicitly disabled.
Copied-byte decoders have no clock/owner context: **success does not renew an
old sample's age**. The adapter must invalidate on level/owner change, missing
owner, loading, rejected sample or expiry, and must not project disabled or
expired metadata just because its bounds still look plausible.

Additional tests cover unknown/negative encoded objective states, both flag
bytes, all event count/type/length failures, low and wrapping record pointers,
level-header mismatch/reset, all metadata float fields with NaN/infinities,
overflowing spans, reversed/zero bounds, negative scale and output clearing.
One arithmetic regression permits either a finite result or rejection when x87
retains an extended-precision intermediate; it never permits a non-finite result.
Six Python catalog tests now also cover sparse spreadsheet columns and exact
UTF-8 byte limits. All 298 actual catalog strings are at most 78 UTF-8 bytes,
below the current Model Text payload capacity of 191 plus terminator. This does
not authorize truncating a future source or an unbounded live string.

To minimize fresh-device work, use this sequence once per new session; do not
repeat full captures or spin the inspector while waiting for gameplay:

| Priority | Small reads, after existing exact-build/root checks | Stop condition |
|---|---|---|
| 1: level identity | framework+4ac:4; system:32; current-level:16; name/path:up to192 each; reread system+1c:4 | Any vtable, pointer, terminator, phase or owner mismatch |
| 2: entity pose contracts | current actor+0xc:4 (reuse fresh actor sample if available); entity header:16; vtable+ec:60 covers dir/position/angles slots | Unresolved live target or entity identity mismatch; inspect local code before more reads |
| 3: map metadata | dispatcher table pointer:4; event154 vector header:12; bounded pointer list; only candidate header prefixes; matched owner+3c0:48; owner+33c:4; bounded path | No matching HUD map listener, invalid vector extent, disabled map or failed owner recheck |
| 4: moving values | Only fields established by priorities1-3; one stationary sample then one walking/turning sample | Any level/actor/map owner replacement; restart identity checks, not an arbitrary retry loop |

Keep already-copied code/vtable evidence on the host. Combine adjacent fields
within the 4096-byte mailbox bound, but do not infer atomicity from a combined
read. There is no need to recapture the 42MB native text for these checks when
its existing exact-profile gates continue to match. Finish the guarded callback
stability check before adding this optional read burst.

## Fresh Fields and controlled pose validation

Fresh read-only evidence is `.work/crysis3/live-202900`; none of these heap
addresses may be carried into another session. Reads 20-23 establish the level
chain independently of objectives: level-system `0x021e9c10`, current level
`0x04856030`, expected level/info vtables, name **fields**, path **levels/fields**.
Reads 24/25 copy 99 objective records: 92 deactivated, five complete, one active,
one failed. The active record is index92, with title/description keys copied in
58/59 as `@fields_objective_04_name` and `@fields_objective_04_description`.
The pinned English catalog resolves these to **Reach the North Railyard** and
**Reach the North Railyard Through the Subway**. The failed record index97 is
secondary; it must not be mislabeled complete. Current language is a separate
verification gate. No continuous objective-event transition was intercepted.

Actor read11 is `0x32bbb6f0`. Its +4 pointer `0x0509e574` (read12) has vtable
`0x398a60c0`; applying the Lua pose slots to it reaches unrelated code. Its
+0x0c pointer `0x076d1708` (read50) has the actual IEntity vtable
**`0x398eccc0`**. Read55 verifies the relevant live vtable targets:

| Entity virtual | Target | Established behavior |
|---|---|---|
| +0x70 | `0x377f5740` | Transforms local AABB through world matrix to world AABB |
| +0x74 | `0x377f44c0` | Returns address entity+0x4c (world matrix) |
| +0x84 | `0x371d4ab0` | Returns entity ID at +0x0c |
| +0x10c | `0x377fbf90` | Copies floats +0x58,+0x68,+0x78 as world position; ret4 |
| +0xec | `0x377fbff0` | Refreshes dirty direction cache and returns entity+0x7c |
| +0x124 | `0x377f7ac0` | Derives Euler angles from world matrix; no external call adopted |

Read67's first24 code bytes match the complete world-position getter SHA-256
`ef1cd02f68542a27318623b975a93086a42f1343c4e03cd70c0a85efd9dc45c4`.
Read74's complete316-byte direction function matches
`51996ee0353efeb2396c931287b780365cdd3c1518089b060c25a0dd43bfe074`.
The captured four-byte world-matrix getter hash is
`adfdaf8d63f7d4f682e89dc37c55b912cc246f100ce02742bbd875a4a750c535`;
the ID getter's is
`2e339cdc5de837ec151dbde90057caeabba18ac3ea32e7c2cffc8f95df41a6ff`.

Entity+0x4c contains a row-major 3x4 transform; translation is its fourth column
(+0x58,+0x68,+0x78). When direction-cache dirty mask **0x02** (bit index1) in entity byte+8 is set,
the native getter obtains its second column (+0x50,+0x60,+0x70); the non-unit
scale branch additionally calls `0x371b5500`. It stores direction at
+0x7c,+0x80,+0x84 and clears mask0x02. **The native getter mutates state** and
must not be called on a worker thread. The reader deliberately does not invent
the non-unit-scale branch: it publishes a copied cached direction only when
the dirty bit is clear and values are finite/nonzero.

The owner explicitly performed the requested walk/turn between these samples:

| Read / helper elapsed | World position | Cached forward |
|---|---|---|
| 66 / 237177ms | (1300.987305,385.179413,130.919342) | (-0.932377,-0.361488,0) |
| 75 / 288993ms | (1272.424316,396.399872,131.463272) | (-0.999989,-0.004670,0) |

Both samples have entity ID0x7777, exact IEntity vtable, scale(1,1,1) and clear
direction-dirty bit. Cached direction equals the transform's second column in
both. Read77 reconfirms the actor/entity association and read78 reconfirms root.
This corroborates position and body-facing changes; it does not prove atomic
sampling, general lifetime, other scale branches or camera aim.

`DecodeEntityPose` validates an exact 136-byte value copy, actual IEntity
vtable and expected actor EntityId, then decodes only the established position
fields. Dirty/zero/non-finite forward caches remain unknown while valid position
can still be retained. Wrong +4 game-object interface, ownership mismatch,
length failure and non-finite position clear the whole result. New strict C++
regressions pass, including the actual wrong-interface vtable and dirty-cache
replacement case. This reader does not itself authorize Snapshot publication.
