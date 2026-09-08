### Delivery checkpoint - dev.10 staged, live verification pending

tools/stage_device_payload.py verified device cache and selected dev.10; receipt
.work/ac2/dev10-device-stage-receipt.json. Prior dev.9 cache preserved. Game DLL,
APK and gameplay input untouched. Game remains closed, installed registry/DLL
still dev.8. Next normal user launch applies dev.10. Never overwrite this
immutable payload version; next version is dev.11.
ZIP1,199,340 bytes SHA4d61099a1481c0d7a7baf264699a723b9725ca38f4ef6d8d02a79e3d783f0bd5;
DLL SHA5599d1a471d5dec333dee0727708d4a03c1444a48f8882d5e188a2d43f6f1058;
SOURCE SHA2897468bf66c7f7bca0580c66530dfe47f7baf253eca810ac6bdf7b4e8057d8a.
84 source files match; PE32/import/export audit saved.dev10-payload-audit.json
under .work/ac2. Package reproducibility unittest passed. New read-only probe
uploaded /sdcard/Download/ac2-inventory-probe.exe but not launched while game
closed. Async request asks user to reopen latest save; no answer yet.

Additional static action lead (NOT enabled): WeaponSelectorMenuComponent's
SelectWeapon handler011076c0 vicinity ->011063e0. This resolves player, cached
AFS component0054ee70, verifies native type01598f10, and dispatches through the
component+10 script object. Input selector IDs differ from inventory menu IDs;
do not directly pass inventory menu IDs. 010e9830 maps selector0->0,1/2->1,4->2,
5->4, others6 sentinel. Ordinary selection invokes010d7350 query then010d75c0;
special branches010d69d0,010d7830 use generated script-event dispatchers.
010d7830 dispatches global223f730 (hash5e452dd0), aliased224c080/224c0a0,
callback015f21b0 sets subscriber+1c. Its precise gameplay meaning is unverified;
do NOT label selector8 medicine or expose a Use action based on this alone.
010d69d0 uses223f718 (hash9dfd95c5), callback015f31e0 ->015f2650.
Evidence .work/ac2/dev10-next-weapon-actions.txt and weapon-selector-events.txt.
No game functions were called during this research.

### Weapon ratings and preview research - dev.10 source

NativeWeaponStats.h follows original012d9f70 ->010dd220 ->00f81840. Ownership
is re-read in the same bounded ReadTransaction; requested menu/definition handle/
resource ID must match the carried unlocked catalog entry. Native manager223c590
type1e3c920 +178 is an eight-byte pair array (count+17e); last matching resolved
definition wins, as in00f81840. The resident entity handle must reverse-map through
object+4 encoded pool index: (word & c3ffffff)-1, block index>>14, slot&3fff;
global2214e38+8c is the12-byte pool table, +92 count, each block+0 base/+4 capacity.
This is009d00a0/009d01a0, registered by009d1520. Nonresident or mismatched handles
remain unavailable; the allocating/locking009d18d0 is never invoked.
Entity+74 components must contain exactly one exact WeaponComponent1e5b2d0;
constructor010faf90 and popup012d9f70 prove damage+22, speed+20, deflection+21.
Each is an unsigned byte on the native0..5 scale. Exact native ID branches
0bcd04a6 ->1/5/1 and26740bd7 ->3/5/1 are applied only to proven owned definitions.
No constructor defaults or missing-resource fallback values are displayed.
Private disassembly evidence: .work/ac2/dev10-weapon-native-evidence.txt.

NativeWeaponStatsTests covers all interrupted reads, loss of ownership, forged ID,
invalid manager/type/pool/count, duplicate components, native last-match behavior,
resource unload/replacement, relevant-byte coherence and harmless retain changes.
New inventory rating bars and independent description scrolling are exercised by
RenderHarness; weapon-ratings.png is explicitly synthetic layout evidence, not a
live capture. All25 test executables, including the render harness, passed in
.work/ac2/dev10-build.txt. Inventory label spacing was then visually corrected;
the render harness and DLL were rebuilt for that UI-only change.

Live status remains closed, installed game dev.8; dev.9 was staged previously.
No live weapon-stat verification has happened yet. Fresh windowless probe built
with the shared reference fix and new weapon reader, plus bounded renderer fields.
Never interpret previous dev9-render-live.txt as fresh: it is the old report.

Preview static evidence: 00564770 reads descriptor2073e20 via2073b74, named
SkeletonComponent, sizea60. Constructor56d640 sets vtable1cb3568. Mesh binding
affee0 stores this component atMeshInstanceData+2c and builds a bone mapping at+30
from mesh+24 entries (24-byte records) via56e0c0, requiring skeletoncomponent+1f0.
0054ee20 is therefore the entity's cached SkeletonComponent getter. b8a870 reads
descriptor20eaa50 via20ea988, named MaterialOverrider, size14, vtable1db9eb8 from
b8b850. afec10 binds that component's resource reference atMeshInstanceData+38.
Evidence .work/ac2/dev10-preview-native-evidence.txt. These identities do not yet
establish a safe isolated preview render pass; no renderer functions were called.

### Delivery checkpoint - dev.9 staged for next user launch

tools/stage_device_payload.py verified all device cache files and selected dev.9.
Receipt .work/ac2/dev9-device-stage-receipt.json; prior dev.8 cache preserved.
ZIP1,192,977 bytes, SHA6842f7f393f0b27e655bc166f5f17607edb8bec52927939bce491d5180068d74;
DLL SHA359e6ae367616f8dcf45e833b7a70b85343c9a821b93cd89b93c3d40abbff103;
SOURCE SHA0eb36a11fe5b3c092e6dab4ee3967e79a504b433e89594fe4c0854d2b203c774.
82 source files matched at packaging; this subsequent checkpoint is not in that
immutable ZIP. Next payload version is dev.10, not a rebuilt dev.9. Package
reproducibility test passed. No APK or game DLL was overwritten by staging.

Last process check: no AC II/Wine process; game registry and actual game DLL still
dev.8, as expected before next launch. Latest Android exit-info has no new entry
for this guest shutdown; bounded logcat did not establish a crash. User's answer
to the async exit question remains pending. Private run_probe.py now checks an
actual running AC II process before sending its windowless probe launch packet
and explicitly selects the device; no stale report counts as new evidence.

Static renderer follow-up .work/ac2/ezio-render-source-native.txt:
MeshInstanceData constructor00b00430 sets vtable1db4e88, parent constructorae5670.
At00b004f0 the owner+4 -> +8 entity reaches cached component getter0054ee20
(entity cache+d1/+d2, epoch+f4) and skeleton binding00affee0;00afec10 binds a
further entity component reference at mesh instance+38. None of these functions
were invoked. Player components were NOT captured because the process closed.
Do not substitute a database portrait for the required live 3D preview.

Weapon-stat research: original012d9f70 ->010dd220 ->00f81840 uses persistent
inventory manager+178 array of8-byte definition/entity-reference pairs, then
WeaponComponent1e5b2d0 at entity+74. Native popup order is damage(+22), speed(+20),
deflect(+21), each out of5, with two exact-resource special cases in012d9f70.
This path is traced but not integrated or tested live; no fallback values shown.

### Native health and complete viewpoint/treasure totals - dev.9 source

User reopened AC II. Normal launcher installation of dev.8 is confirmed by
registry, on-disk DLL hash and native initialization. Fresh read-only reports
.work/ac2/dev8-live-20260906.txt and dev9-readers-live.txt (mtime1788729535,
Windows PID232/Android20528) validate world0d97f840/player212dfd90, health20/20,
unavailable health0, synchronization1000/14200, viewpoints1/73 and treasure2/330.
No original map/menu was opened by the agent. Capes remain absent in this save.

NativeCollectibles.h follows native aggregate traversal through catalog+24/+2c,
CollectibleGroupInfo1e9efd0 and CollectibleSliceInfo1e9ef50. The live viewpoint
districts are RHPDistrictInfo1e692d8; their MissionStepScene1ccf940 points count
states3/5, with raw point count as denominator. Treasure districts reuse class
1e99c78, referencing collection1e71ef8 at+18. Collection+e0 holds native inventory
definition handles (class1e699bc/category9). Native15642f0 ->126bc40 ->126bac0 ->
f7faa0 ->f7dea0 counts resident handles present in manager223c590+30, using exact
handle equality; manager class1e3c920 is live-verified. Max1564310 ->126ba20 reads
the byte count at+e6. Bounds reject districts exceeding255 instead of wrapping.
All traversals are bounded/coherent and expose aggregate totals only. No locked
district names, map markers or treasure locations are revealed.

Health and both collections are now sampled once per second in the bridge;
health appears in the persistent header, collections on Progress. A local layout
render with captured values was inspected at1240x1080 (dev9-progress-layout.png).
Counters are not cached across player/world generations. Failure clears the
affected optional data; no dummy totals are substituted.

Reference coherence correction: Memory.Reference used to copy/track the native
handle's +4 retain count despite only consuming its +0 pointer and +8 residency
flags. Native lock-xadd retain/release operations can change +4 without changing
any game state. It now reads only +0/+8. Tests prove reference-count changes are
accepted while identity/residency changes still invalidate the transaction.
This removes an avoidable invalidation source; live marker flicker reduction
still needs observation in the new payload.

All24 native test executables and strict DLL build passed after this shared
reader change (.work/ac2/dev9-final-build.txt). Local package/visual checks are
separate from live native rendering. The isolated Ezio preview remains unfinished.
The next read-only probe includes bounded player component evidence for tracing
that renderer. AC II exited before it ran; its attempted output dev9-render-live.txt
is an OLD report with mtime1788729535, not new render evidence. An async question
asks whether the user closed the game; do not infer a crash or restart it.

### Device delivery checkpoint - dev.8 staged; subsequent source work

Payload dev.8 passed manifest/PE/import/source checks and was staged through the
normal launcher cache. Receipt .work/ac2/dev8-device-stage-receipt.json confirms
every cached file, preserving dev.7; no APK/game files/input were changed.
ZIP1,182,501 bytes, SHA48adc0498f148140b098e93f5227b8c06db1e81bb7bdbaeb0e6f48dfd1db77f5;
DLL SHA50ae400960e89f1040a2a1e7592f09cbdf48945f404740f2602ba83f7b090c4c;
SOURCE SHA03149d9d9bf18075f8f4bf338ab5222d16ce4f0b206f96d6dfaa9707f8cfdc13.
78 source files matched the worktree when packaged. The source now has further
research below; do not regenerate the immutable dev.8 ZIP. Next package is dev.9.
Installed game remains dev.7 until the user's next launch. No running AC II,
Wine server or FEX process was present at the delivery checkpoint.

After packaging, NativeHealth.h and NativeHealthTests.cpp add a read-only health
reader to the private probe, not the live UI. It requires the three native getter
subscribers015fde10/015fde30/015fde70 to share one CSrvPlayerHealth1ea1658 owner
with the player's context. It reads current+58, maximum+5c, unavailable+60 and
validates the transaction. Focused tests pass for malformed/duplicate/missing
subscribers, stale actor/owner, interrupted reads, zero current health and reduced
restorable capacity. No medicine action or health write is enabled. The expanded
windowless probe compiles strictly and is uploaded to /sdcard/Download; it has
not been run because the game is closed. Its live layout still needs validation.

### Native action and progress continuation - payload dev.8

NativeCapeAction.h reproduces the normal cape menu path, not its low-level jobs:
012da200 / 012d9510 query event dispatcher010b3750, then mutation010b3c30.
They use entity-in-ECX, three stack arguments and callee cleanup12. The explicit
fastcall adapter supplies unused EDX to implement this x86 layout; 1,000 local
ABI calls verify the wrapper and stack behavior. Both dispatchers require return0;
the permission out-byte must additionally be1. Mutation's final argument is0,
matching the normal menu's queued update. Native subscribers015fbc00/015fcc30
lead to cape permission015fba20 and mutation015fc610; the game retains handles,
queues inventory jobs and updates visible equipment itself.

Every request resolves fresh player inventory and validates the event registry:
global event IDs223f664/223f66c, exactly one kind5 subscriber each, exact function
addresses, shared subscriber class1ea141c, owner1ea1448 and matching context.
Whole-executable SHA and both dispatcher prologues gate action activation.
Reject UI+fe blocker, no existing cape, forced Carnival cape, Altair override,
locked/unowned/current targets, duplicate identities and unstable transactions.
After the permission callback, roots, ownership, blocker, subscriber and handle
are re-resolved before dispatch. One queued request is drained at the existing
verified game-thread pump boundary; afterward equipment is re-read and a changed
world publishes a loading snapshot. UI holds inspect only; taps may request an
action. No agent-generated equip command or game input was sent.

All 22 native tests and strict DLL build passed (.work/ac2/dev8-build.txt).
This proves local contracts, not positive on-device equipment mutation or every
cutscene/death transition. The most recent observed save has young Ezio and no
cape. Game is closed at this checkpoint; do not launch it or take control.

Synchronization now exposes completed/total entitled story memories and sequences
(all eligible memories at100%), plus the five crowd-group completed/total counts.
AdditionalGroupTitle01597020 supplies native group+c text. This adds no engine
calls and does not expose undiscovered titles or marker locations.

Further research, not enabled features: Additional group catalog+24 (RHP) and+2c
(treasure) map through1596de0 to CollectibleGroupInfo1e9efd0, group+18 slices
1e9ef50, slice+3c districts. Group current/max15ccd90/15ccdd0 sum slice virtual
+44/+48; slice15cc070/15cc0c0 sum district virtual+44/+48. RHPDistrictInfo1e692d8
counts resident point+1c states3 or5 (11f04f0), raw point-array count at+18
(11f0340). Point classes and treasure district subclasses still need validation.
The read-only probe now inspects this bounded graph without calling the engine.

CSrvPlayerHealth original class is1ea1658, constructor16048e0. Native health-ratio
subscriber15fdc90 reads owner+58/+5c; +60 additionally affects heal eligibility
(15fdd20). These are research findings, not authorization to write health or
consume an item. The probe now records bounded health subscriber/owner evidence.
The native gameplay heal permission/consumption path remains to be traced.

### Touch and original shop icons - 2026-09-06 dev.7 installed

Launcher installed dev.7 normally; registered version and actual game DLL SHA
ed10d6c0b3fe2d83bbc7e94885b26fdcee767560573a31c88c965491ab5c503c match.
ZIP SHA5fe260e9cc2ec0ae5e401d6e40623c61848dd23c18195e3b3b906c596cd53ca4;
SOURCE SHA95423fbad898e087082d9d1e68c33b848a5e52ddb61e8b5922d01b28347e85fe.
All six page changes were recorded in native logs; 250 packets accepted, zero
rejected. The user confirmed everything works so far. Progress screenshot
.work/ac2/dev7-bottom-live.png shows native7.0% and collection counters.
No APK rebuild, game launch/input or direct overwrite of a loaded DLL occurred.

User reports all page taps do nothing. Targeted disassembly of the installed
startup APK confirms SecondScreen.GUEST_INPUT_PORT=7788, matching the payload.
CompanionGuestPanelMotionRouter.dispatch sends `tap x y heldSeconds` (and hold
for >=0.25 seconds). The payload rejected fourth fields on taps and ignored
holds. ParseTouch now accepts both three-field and duration-bearing taps; held
selections navigate/inspect but do not invoke gameplay actions, avoiding duplicate
actions from the alternate route's hold-followed-by-tap delivery. Tests parse the
actual wire encoding and visit all six tabs. Input diagnostics are bounded to
first delivery, page changes, and one counter report per five seconds of input.
Installed APK evidence: .work/ac2/installed-{secondscreen,touch-router}.smali.

Original Firenze world bundle5b29460a icons5e5d55c8/be/d2/b4/558c use format0,
16x16 or32x32 with a four-byte-per-pixel first mip. Native converter00b12800
indexes table020dddb8: format0 maps to D3DFMT_A8R8G8B8(21). DecodeTexture now
accepts that exact BGRA format, preserving alpha and all prior bounds. No assets
are packaged. Focused decoder tests cover alpha, channels, lengths and rejection.
Private originals: .work/ac2/icon-*.feu; evidence texture-format-native.txt.

Local GDI+ pixel tests render a visible player arrow at the fresh probe's
coordinates/heading and a position dot when direction is unavailable. The dev.6
device screenshot lacked this indicator despite rendering the map; this remains
unresolved, with generation-scoped panel coordinate diagnostics added for dev.7.

### Device update - 2026-09-06 dev.6 installed

User explicitly authorized updating the payload ("go ahead and do it").
tools/stage_device_payload.py verified the local ZIP and every device cache
file, then selected the hash-specific app-private development package. Receipt:
.work/ac2/dev6-device-stage-receipt.json. Prior dev.2 cache is preserved. No
APK, game input, saves, launcher preferences or other game files were changed
by this staging tool. No public release was published.

The launcher itself then applied the payload: logcat at16:20:05.728 reports
Updated assassins-creed-ii payload to2026.09.06-dev.6, and16:20:07.544 verified
it. installed_games.xml now records dev.6 and the actual imported game DLL is
SHA256 d4599518a465be874f1cfaa6f15f803634505cd53b95b068ad8f6405b6ca1bed.
Native log confirms artwork worker, native IAT hook, panel buffer/input and
game-pump initialization. A proposed direct-copy helper aborted at its first
running-process guard and made NO changes; normal launcher deployment completed
the update. Do not rerun .work/ac2/deploy_dev6.py against a running game.

Before deployment, a fresh windowless probe succeeded in the user's early
Florence free-roam save. .work/ac2/dev6-live-20260906.txt, report mtime1788725561,
Windows PID236/Android3917, world0de6b550/player21e5fe50/progress05e38140:
- Native map, marker and facing readers valid;19 visible markers; heading
  -1.658148 radians; native Florence artwork52a019fa, position(.557750,.581096),
  256x256 discovery mask with953 discovered pixels. Screenshot confirms young
  Ezio in normal gameplay, money1817, no robes/cape/armor, outside a mission.
- Synchronization1000/14200=7.042254%; glyph puzzles0/20; Villa valid but locked;
  cape and carried-equipment readers valid withzero exposed gear. Inventory17
  native containers;11 DNA entries;23 unlocked database entries. These match
  the captured native state; positive cape/Villa unlock transitions and visual
  comparison with original synchronization UI still need testing.
- Current mission valid but absent and native objective record==-1, correctly
  reporting free roam. An active-objective change still needs a live test.

Updated game initialized at tick360366135, then exited at16:20:30 before a
loaded player was observed. Android shows GameSessionActivity finishing BEFORE
guest launcher exit137; there is no observed native exception or OOM proof.
The user confirmed they closed it themselves. Their next launch successfully
loaded young Ezio: tick360535973, world0534f840/player0fd198d0/progress05e38140.
Native log verifies inventory17 containers, DNA11, database23, synchronization
1000/14200 and19 map markers. The bottom screenshot dev6-bottom-live.png shows
the real discovery-masked Florence map and1817 florins. No crash established.
User touch failed (see dev.7 correction above). Do not restart the game, take
control, or replace a loaded DLL.

### Local build checkpoint - 2026-09-06 dev.6

Local development package build/payload/assassins-creed-ii-2026.09.06-dev.6.zip,
1,170,858 bytes, ZIP SHA256
2c4f0ecf97dcaea3bb945346f720eec52d005fd64af21ec36f56a8529ab132e2;
DLL SHA256 d4599518a465be874f1cfaa6f15f803634505cd53b95b068ad8f6405b6ca1bed.
Strict native DLL/probe compilation and NativeMissionTests pass. Audit in
.work/ac2/dev6-payload-audit.json checks manifest digests, PE32/system imports,
DirectInput8Create, zero timestamp and 74 source files matching current worktree.
The packaging reproducibility test passed during this continuation. No APK build,
release publication or game input during that build. Subsequent installation is
documented above.

New NativeMission.h reader follows original 01220990 -> 0129dda0 -> 0129da10:
first resident main memory with state+18==5, then the first active crowd memory
in five catalog groups. Only exact previously verified definition/state classes
are accepted. Native pause title uses shared definition+c; description+54 is
only read for main memories. It does not infer the active mission from a selected
DNA row, highest completion value, or array tail. Side activities get a separate
owned snapshot identity, with no replay action.

Current objective source is original PauseCurrentMissionObjective at01220f5e:
UI global223f820 class1e60898 -> +110 record -> first uint32 localization ID;
-1 means no objective. Native callback0121f650 writes event-kind6's +2c text ID;
0121f6a0 resets it via0128ecd0. Save restore0108ad57 calls01156f50 to restore
the same record. ReadCurrentMission requires an actual active memory before
reading that text, so stale UI data alone cannot label free roam as a mission.
Owned current-mission/title/objective snapshots update once per second, clear
on read failures and generation changes, and feed Map/Missions. Native text
formatter004bd870 remains unimplemented for dynamic control substitutions.

NativeMissionTests verify native priority across main/side activities, clearing
on free roam and the -1 sentinel, nonresident states, unexpected class/phase,
invalid synchronization, every underlying read failure and used-field mutation.
The windowless probe now logs this reader and the raw objective record, and its
latest executable was pushed ONLY to /sdcard/Download/ac2-inventory-probe.exe.
It was NOT launched: repeated ADB ps checks show no AC II/Wine/FEX process.
Live current-objective verification (including without opening the pause menu),
the other dev.5 readers, markers, and installed-panel tests remain pending.
The pending question already asks the user to reopen the latest save. Do not
launch/restart AC II, send input, or take focus; wait for their gameplay session.

Private evidence: current-objective-native.txt, current-mission-owner.txt,
objective-ui-accessors.txt, objective-event-registration.txt,
objective-component-lifetime.txt. Current-mission selector0129da10 was also
inspected in full. Cape action callbacks are in cape-action-callbacks.txt:
CanEquip event8231 subscriber015fbc00 delegates category13 to015fba20;
the mutation event8233 subscriber015fcc30 still needs full execution-phase and
equipment/render refresh verification. No game mutation or render hook enabled.

Remaining scope is still substantial: live isolated Ezio preview, native equip/
use actions and safe phase guards, complete owned gear/dyes, full conditional/
optional objectives and DLC, complete progress statistics, live transitions and
touch validation. This is not a completed mod or release-ready build.

### Previous checkpoint - 2026-09-06 dev.5 (not installed)

This section supersedes older status/remaining-work lists below. Local package
build/payload/assassins-creed-ii-2026.09.06-dev.5.zip is 1,164,070 bytes;
ZIP SHA256 d7b6cf750d028d5eb663faf394d4e4a4c32e96a4ed88acd6bd851b06982910a1;
DLL SHA256 2ef45f29caf7d2e606c5cba07c7090003762a938d1ff9b8b738f3a471db6a5e8.
The strict PE32 native DLL and windowless probe compile. Packaging test passes;
.work/ac2/dev5-payload-audit.json checks all manifest digests, system imports,
DirectInput8Create export, zero PE timestamp, and 72 source files matching the
worktree. No APK build, installation, publication, or live DLL replacement.
Installed payload remains dev.2. ADB is connected but no Wine/FEX/AC II process
was running at the latest check. The user's reopen/latest-save question remains
pending. Do not launch the game, send input, or treat old probe reports as fresh.

Added native readers, each with failure/coherence and semantic fixture tests:
- NativeSynchronization.h: original StatsOverall 010f85c0 weighted story/side
  formula. Main contributions 0126ea80, crowd 0159f970, fixed maximum100;
  entitlement 0114c970 accepts ANY enabled required bit. Five side groups use
  verified classes1e9d0c0/1e9d058, state1cb8f58. Missing resident definitions
  skip; missing state contributes zero. No invented completion weighting.
- NativeVilla.h: native inventory group4/menu18 definition+30 unlock gate,
  StatsVilla value/proportion/population from progress+c/+10. Population uses
  original x87 order value/max*3500. Restoration clamps nonzero values to1..100.
  Locked Villa values are hidden. Renovations/shops/collections remain pending.
- NativeCapes.h: original menu ownership means unlocked, not necessarily carried.
  Native current-cape priority33,26,23,25,22 requires exactly one matching carried
  subtype in category13. Standard/Medici/Venetian/Auditore exposed by catalog;
  Carnival only if currently worn. Altair39 is a separate model override and
  remains omitted. Actions remain disabled pending safe native execution.
- NativeGlyphs.h: manager22553b0+28 handle array, puzzle class1ebb42c,+44bit2
  is solved (017fa990); total017fcf2d. A nonresident puzzle withholds the count.
- NativeMap.h: fixed-map heading uses native player transform second row+20/+24,
  confirmed in HudMiniMap013241a0. Projected anisotropic map scales are applied.
  Invalid/degenerate facing keeps the real position dot; parented transforms
  still defer. NativeDna.h now accepts entitled DLC sequences via normal visible
  branch; missing-DLC placeholder and polymorphic conditions remain deferred.

Bridge samples progress/collections at1Hz using bounded read transactions,
clears caches on world/player/progress generation changes, and logs only changes.
New probe captures all DNA memories, crowd groups, capes, glyph puzzles, guarded
reader results and map heading. Latest probe is compiled locally, not yet run on
the device. All new readers above and NativeMapMarkers still need live evidence;
passing synthetic fault-injection tests is not live verification. Original
inventory/database/Florence position/fog and six city assets retain prior proof.

Private evidence: overall-synchronization-reader.txt, progress-weighted-counters.txt,
progress-memory-contribution.txt, progress-counter-layouts.txt,
dna-catalog-serialization.txt, side-memory-serialization.txt,
synchronization-leaf-readers.txt, progress-overall-villa.txt,
villa-collection-readers.txt, progress-collection-accessors.txt,
minimap-heading.txt, minimap-movie-binding.txt, cape-menu-native-path.txt,
cape-ownership-preconditions.txt, cape-current-and-ownership.txt,
cape-category-query.txt, cape-service-query.txt, cape-query-subscriber.txt,
cape-native-filter.txt, glyph-command-count.txt, glyph-completion-reader.txt,
dna-sequence-visibility.txt, dna-condition-dispatch.txt.

Remaining: live Ezio isolated render path, equip/use actions and phase guards,
owned non-carried weapons/dyes, live reader and transition/touch validation,
full objectives/conditions/DLC, complete completion statistics. This is a
development checkpoint, not a completed or release-ready mod.

### Previous checkpoint - 2026-09-06 dev.4 (not installed)

Packaged current native source as build/payload/assassins-creed-ii-2026.09.06-dev.4.zip,
1,140,226 bytes; ZIP SHA256 cbaadaa7b78fb9cf3bb9ac117c7f350255f27e523afe8d11b9fcb41d17ab64e9;
DLL SHA256 518f1fe56648099fe7daf794f0a8d9d44eb07630e6ab36599fc6c430484b1d79.
Audit .work/ac2/dev4-payload-audit.json: PE32/i386, timestamp0, only system
imports, DirectInput8Create export, every manifest digest verified, 62 source
files exactly match worktree. Package includes native build source and GPL/LZO
license, no original game assets. Descriptor stays development. No publication,
APK build/install, live payload replacement or gameplay input was performed.
Installed device payload is still dev.2. Full native suite and six-page render
passed, then targeted NativeMap tests passed after stricter no-fog handling.
Packaging reproducibility/source/manifest test passed. Original six city textures
passed C++ decoding, plus original Florence discovery mask/UI render inspected
at .work/ac2/map-native-panel.png. This is a captured-state render, NOT an
installed-device screenshot or proof of complete in-game operation.

The game closed after fresh map-markers-live-20260906.txt (timestamp1788676790).
Next marker reader probe failed to start: Winhandler UDP sender timed out,
no Wine/FEX/game process in subsequent ADB ps, remote report unchanged. Do not
trust a pulled old report as the new test. Current pending async question asks
user to reopen latest save and report Ezio in gameplay. Do not send input,
launch/restart game or take focus. Continue from this checkpoint on their reply.

Map improvements after the earlier checkpoint below:
- SparseBundle validates FILEDATA/block/resource extents and reads only needed
  LZO blocks. Actual C++ private test decoded Florence map with462187 bytes read
  instead of29308542 packed bytes. No whole-city expansion in runtime anymore.
- KnownCityMaps.h binds all SIX primary regions via actual world->MapWorldInfo
  ->FEU->MapDesc->texture chains, each fingerprint checked. Venice and mountains
  are2048x1024; others1024x1024. Native fog textures: Venice512x256, others256x256;
  mountains have no configured fog. ReadMap supports bounded512x512 masks in
  <=64KB reads. No fog only for explicit native bypasses or verified Mountains
  FEU d8508c6e with zero mask UID/object; a temporarily empty city handle does NOT
  expose the map. UI uses a native-position dot; heading remains unverified.
- NativeMapMarkers.h implements 4 traced native marker classes, native discovery/
  notoriety/region/city unlock rules, group-member flags, resident entity/linked/
  aggregate positions. Unknown classes/parented transforms stay hidden. OneHz
  owned marker snapshots, original localized marker labels, original v2 icons.
  NativeReadTransaction coalesces observed fields into bounded4KB pages and
  rechecks only used bytes. NativeMapMarkersTests pass, but live validation of
  this NEW reader is still pending because the game closed before it ran.
- GameArchives resolves original marker icons from the Firenze world resource
  index with sparse block reads. Owned images only; no resource engine calls.

Private archive tool .work/ac2/city_sparse.py reads only bounded ranges through
ADB, with64KB local caching; no writes to game files. Current world MapManager
serialized component tag c773234a references MapWorldInfo UID (+6 following
01,00 markers); FEU reference at MapWorldInfo body+11. All6 chains checked:
Firenze world5b29460a info06631e79 art52a019fa desc220049ee tex220049f1;
Venezia world48b659dc info9af5a45a arta1d3c38c desc5e5d5665 tex5e5d5668;
Wetlands world701bc87d infoa9ff302d arta9ff302c desca1d3c38e texa1d3c391;
Mountains world6ad1d250 info74d312d8 artd8508c6e descca8ef908 texca8ef90b;
Toscana world7c82d813 info159f9ae9 art2f78336d desc2d8bdf45 tex2d8bdf48;
Villa world83ac7022 infoa2e385fc arta2e385f2 desca268487b texa268487e.
FEU embeds UEF v8 movie with native external-bitmap tag254, ASCII resource UID.
Each map MapDesc has206-byte body and final single texture reference.
Fog reference is MapWorldInfo's final8 bytes UID then float blur, with reference
kind at -10 (01,00 configured; Mountains03 and zeroUID). Native map UI at
012a2394->0129fbc0 enables fog only with resident mask/art and !data+81 and
!(manager+24&2). NativeMap guards unloaded nonzero mask UID instead of revealing.

Remaining work still substantial: live Ezio preview/render isolation; equipment/
use actions and complete owned-versus-carried gear/capes/dyes; facing direction;
full conditional/DLC DNA/objectives; overall synchronization, Villa and other
completion stats; installed gameplay/transition/touch validation. Do not report
this mod finished and do not enable guessed actions or publish a ready release.

New static leads for remaining progress/objectives, not implemented or live-tested:
- .work/ac2/mission-objective-service.txt:12adfd0 resolves memory+3c state,
  copies raw pointer array state+34 (7e49c0), takes indexed objective+30 text
  descriptor via12adfa0. Need objective type/state/active semantics before showing.
- StatsOverall UTF16 string1e64aec handler118c0c0 ->118afd0 ->1524040 returns
  global224a2e4; Stats data base=object+2c. Overall10f85c0 combines weighted main
  and additional memory contribution sums. 10f7130 iterates DNA catalog+8 sequence
  handles, sequence+24 memory handles via10f7050->10f66e0; per-memory gate114c970
  on definition+8, virtual+44 contribution, fixed100 maximum. 10f7180 aggregates
  5 catalog handles+10,+14,+18,+1c,+20 through10f6ad0 (group+18 handle array),
  10f6740 uses virtual+44 contribution and virtual+48 maximum. Still trace those
  polymorphic getters and entitlement gate. This ratio is the original Stats
  overall, not an invented weighted sum of our four collectible counters.
- StatsVilla118bce0 first gate: inventory catalog group4, menu18 via1158dc0,
  definition unlocked byte+30==0. Then stats object+2c getters10f4bf0,10f4b80,
  10f4b90,10f6940,10f69c0,10f4c50,10f4d60. Further layout/text-label tracing needed.
- Static evidence files progress-native-dispatch.txt,progress-overall-villa.txt,
  overall-synchronization-reader.txt,progress-weighted-counters.txt,
  progress-counter-layouts.txt,progress-memory-contribution.txt.


### Recovery checkpoint ? 2026-09-06 live map integration

Latest source full sequential build passed after NativeEquipment, NativeMap,
MapCache, and original artwork integration. This has NOT been packaged or
installed; device still runs dev.2, old local ZIP dev.3 does not contain this work.
No APK edits, game input, restarts, or loaded DLL replacement performed.

Fresh private reports map-mask-guarded-live-20260906.txt and
map-markers-live-20260906.txt: guest PID232, world052a6750, player1ee9e5a0,
map manager0fe8eb00, mapdata0e584b70. ReadMap verified player normalized
0.557750/0.581096; mask256x256,953 fully discovered texels. Mask circle does
not currently surround player; likely viewpoint coverage, must continue tracing
native stamp triggers. Do not claim alignment/transition playtests completed.

NativeMap.h: world+60 raw component array -> vt1e7682c, owner+4==world,
handle+8 -> mapdata vt1e7579c. Mapdata+c FEU art handle ID52a019fa;
+34/+38 dimensions1024; +3c/+40 scales, +44/+48 offsets (129ef60).
+6c fog texture handle -> vt1db7ed0, width+8,height+c,depth+10=1,
format+14=7/A8, CPUshadow+54 (b5f330). 129fbf0 writes255 discovered,
row origin inverted; no native allocator/lock called. NativeMap copies mask,
transform, handles, roots twice; parented player+50 rejected, no invented heading.
NativeMapTests cover each failed read and owner/mask/player/root changes.
MapCache owns one background job/city/visible result, stale generations rejected;
no archive decoding or full image composition on game/UI thread.
Currently LoadCityMap supports only verified Firenze binding; others unavailable.
No map markers are published yet. Position dot works without a guessed heading.

Original Firenze world FILEDATA UID5b29460a in DataPC_Firenze.forge790888448B,
packed29308542B, decoded streams24866B and63846697B. Private partial remote
cache .work/ac2/remote-*.bin, index firenze-index.json (2399),
world files firenze-world-{0,1}.bin, firenze-resources.json (2486).
Map_Firenze FEU ID52a019fa -> external MapDesc220049ee -> texture220049f1
MiniMap_Firenze_Map1024x1024DXT5, original artwork for map. WorldAtlasTexture
86ea1183 is DIFFERENT and not the actual FEU map texture. MapCache verifies
three resource fingerprints before loading. No game bytes shipped.
ForgeAssets now has WorldBundle and BundleResource; current decoder expands
whole world once on background worker. NEXT improve selected-block resource
reads: stream17-byteheader/block count + metadata pairs expanded/compressed u16,
then checksum4+compressed payload per block. Can seek/decode only blocks covering
selected resource, using first resource-index stream. This permits all city maps
without pulling/expanding entire archives or adding huge caches.

Native marker evidence .work/ac2/map-filter-context.txt,
map-marker-overrides.txt,map-position-base.txt,map-positions-context.txt.
Global map manager223f820 vt1e60898. Six18-byte city rows+60 hold world UID,
handle,text title,flags+14. Native city IDs: Venezia48b659dc,Firenze5b29460a,
Wetlands701bc87d,Mountains6ad1d250,Toscana7c82d813,Villa83ac7022.
MapManager marker visitor12aa930 enumerates global+18 singleton, MapData+10
raw array, manager+c dynamic array, global+20 wrapper array (wrapper+0 marker).
1538 static Firenze markers: vt1e765c0=636,1e598a0=3,1e7f7f0=827,1e9a510=72.
City map visitor12a1d80 requires marker+3b bit0; mapmanager+24bit0 bypasses
virtual visibility, +24bit1 bypasses discovery test. IMPORTANT corrected argument
order: virtual+34 arg1=notoriety, arg2=player world bit (15abcd0), arg3=discovery.
Notoriety component vt1e575c8 +c float (>0) OR +64 byte; override223f648 uses
float211c058. Player+6c world -> world+610 cached script component -> component+8
entity+40 bitset; bitdescriptor*21b1904+42 u16. Read cached data only.
Base visibility12a6af0: hidden+3a bit1, type0..60 then rule table1e76380;
global114f910 searches8-byte rows global+138 count+13e for type override+4.
Rules1always,2flag+3abit0,3!arg1,4!arg3||flagbit2,5flagbit0&&rule4,
6arg2,7arg2&&rule4. vt1e598a0/10efe60 gate+ b0 worldID via114e850 city
rows+60 fixed6 and+f0 dynamic rows18B, flag+14 (unknowncity returns1).
vt1e7f7f0/1341630 requires !hidden, nonempty+28 handles and any zero byte
in +b0 bytearray countword+b6. vt1e9a510/156d650 requiresstate+b0 2or3,
flag+3a bit0 and!bit1. Its position special types27..29,state3 usescached+c0.
Position base12a77a0: average resident+28 handles passing virtual+54 via12a7610;
then+30entity handle position4161f0; then+84 linked marker (unlesstype26);
then ownfloat4+10. Parented entity transform still unverified, skip safely.
Original UI source .research/ac2/ui-scripts/scripts/__Packages/Classes/Animus/Map/
MapIcon.as gives exact native type->localization ID/icon identifiers.

NativeEquipment.h is now implemented and tested; carried equipment currently0
on early save, do not equate unlocked catalog or Villa ownership with carried.
Actions, equipment render/capes/dyes, other cities, markers, complete objectives/
DNA conditions, full completion counters remain unfinished. Next steps continue
native integration, do NOT describe this as the finished mod.

# AC II native integration checkpoint

Status: development. The player-owned inventory, native localization and a
bounded base-story DNA reader have now been checked against the running save.
They are connected in the local payload source, not yet installed. Native
actions, live maps, database, full equipment and Ezio preview remain incomplete.
This is not a finished live companion.

## Current checkpoint: 2026-09-06, resumed after PC crash

The user confirms **controller input is fixed**. Audio works but crackles and
performance stutters; the user explicitly defers both until the mod is finished.
They first identified the running character as Desmond, then reported controlling
Ezio. Do not send input, launch/restart the game or take focus. Read-only ADB
and windowless process-memory probes are the established test workflow.

The installed launcher is `DualScreen-AC2-Startup-Fix.apk`, versionCode 41,
SHA-256 `0b929d1a9bc6d691b50d6a4e66ab2adc6197a62c5832649353383163d37ecaed`.
Its executable-scoped DLL policy validator accepts `builtin,native` as well as
`native,builtin`. The prior controller library and Pokemon/Dishonored changes
are retained. No APK rebuild or installation is needed for this native work.
Use small sequential C++ builds: the PC has repeatedly crashed and concurrent
IDE Gradle/Kotlin processes must not be killed or disturbed.

### Live native inventory and localization

The imported game actually running is
`/sdcard/Dualscreen/Games/Assassins Creed II/Game/AssassinsCreedIIGame.exe`.
Container 1 remains the Games container shared with Skyrim. Original source
files under `/sdcard/Games/Assassins Creed II` are preserved.

`NativeInventory.h` now resolves the already-cached player script component
(`0054eec0`, `00948dc0`) and follows component+10 -> entity+20 -> context+58
data array -> first record+c -> InventoryDataItem. Exact vtables are checked:
component `01e9cd48`, data item `01e797c8`, interface `01e797c0`, embedded
LogicalInventory `01e57e40`. Cache epoch/index, owner back-reference and all
array roots are rechecked. Unresolved caches are rejected without refreshing.
The count subscriber `015f75c0` -> `010baa60` -> `010ba720` confirmed this same
owner; generic event row+0 is a priority float, not an object pointer.

`NativeInventoryMenu.h` follows global `0223f81c`, catalog `01e610c8`, group 3
to reload entries `01e61020`. Menu ID is +14, title text ID +18, description
text ID +24, definition handle +8. Definition `01e699bc` has category +c and
**nonzero byte +30 means locked**, traced through `012d96c0`. Menu/native IDs:
smoke 10/5, knives 11/8, bullets 12/10, poison 13/11, medicine 14/7.
Currency category 1 is proven by the `GetInventoryMoney` dispatcher branch.
Zero-quantity unlocked pouches remain inspectable; no ownership or action is
inferred from capacity. Locked consumables are not exposed on the panel.

Live Desmond had 17 containers, no consumable capacity and all five locks set.
The reader survived the transition to world `0c76cfe0`, player `20022490`:
Ezio had 756 florins, medicine unlocked, quantity 0/capacity 5, other consumables
locked. These are observed readings, not fixed values in the payload.

`Localization.h` implements the exact `00612ab0..00613110` big-endian compressed
dictionary format, including variable-length symbols and reversed pair expansion.
Private English archive decoding and live language decoding agree on all five
consumable titles and full descriptions. `NativeLocalization.h` selects loaded
resources through `02212914`: manager+c 40-byte records, handles +10/+14,
resource type `01cacbcc`, compressed blob +1c/size +20. Owned byte copies and
decoded strings are cached; unchanged samples only check bounded descriptors.
Unknown mutable/uncompressed overrides are refused. No original text blobs,
icons, models, fonts or other Ubisoft content are redistributed.

### Live base-story DNA

`01596f00` -> `0108bbb0` selects catalog `01e75650`, whose +8 reference array
holds sequences `01e76a60`. This is loaded during gameplay without opening DNA.
Sequence title is +c; state handle +18 resolves `01cb8d38` with state at +20;
memory reference array is +24. Native `012ae520` excludes sequence state 1.
Memory definition `01e76a00` has title+c, description+54 and state handle+3c.
State `01cb8f58` contains lifecycle+18, synchronization+1c and located byte+94.
Vtable getters +40/+44 resolve to `0126ea10` / `0126ea80`.

`NativeDna.h` implements proven status branches. Lifecycle 6 is retrieved, 5
is the active story memory (`0129da10` confirms), 3 uses the located flag.
Lifecycle 1 can delegate to polymorphic mission conditions through `005f7aa0`;
only unconditional, non-special-mode cases are currently exposed. Remaining
conditional branches and DLC-specific filtering/numbering are deliberately
deferred, not guessed. Unlocated memories have no title or description exposed.
The guarded reader succeeded live with seven visible memories in Sequence 1.
The bridge refreshes DNA at 1 Hz, clears owned cached entries on generation
changes, and never enables replay. Full objectives/optional requirements and
all DLC/side mission behavior are not implemented yet.

### Validation and continuation

The build runs native root, quantity/capacity, lock-definition, localization
codec/cache, inventory projection, DNA status/discovery and UI/touch tests.
Failure-injection tests cover every read in representative transactions and
changes to selected owners, arrays, resource handles and lock/state fields.
Private live evidence is `.work/ac2/inventory-ezio-decoded-20260906.txt` and
`.work/ac2/dna-guarded-live-20260906.txt`; these are ignored, not release files.
Only the windowless probe has been pushed to the device in this continuation.
The running game still uses its existing dev.2 payload. Do not replace an active
DLL or claim these UI changes have been tested in the installed panel yet.
The local development checkpoint is
`build/payload/assassins-creed-ii-2026.09.06-dev.3.zip`; its descriptor and
`.work/ac2/dev3-payload-audit.json` record the final size and hashes. Archive
audit verifies only the manifest and native DLL, with no external C++ runtime
dependencies. All nine native test programs, including the render/touch
harness, pass. Private renders using captured item/memory metadata and decoded
original text were inspected at 1240x1080; inventory capacity is visible and
completed memories no longer have an empty current-objective heading. This
archive is built locally, **not installed or published**.

Next: finish conditional/DLC DNA status, original icons/equipment ownership and
actions, mission objectives, native database/map readers and isolated Ezio
rendering. Complete native integration before the next launcher release; keep
the implementation in its independently downloadable payload.

## Earlier 2026-09-06 checkpoint: controller blocked loaded-save validation

The user cannot reach Ezio until controller input works. Prioritize a native
controller compatibility test update; the complete mod release remains on hold.
No gameplay input or game launch is authorized by this test handoff. Preserve the
latest installed launcher (now versionCode 41, Dishonored development) and its
Pokemon changes. AC II itself was closed during this research session.

Controller test APK: `C:/Users/Shadow/Downloads/DualScreen-AC2-Controller-Fix.apk`.
It retains versionCode 41 / `1.6.17.12-dishonored-dev` because this is a native-only
update of the exact installed APK, not a new full launcher build. The standalone
NDK `fakeinput` target compiled successfully with one worker. The APK was then
aligned for 16 KiB native pages and re-signed with the existing certificate.
Audit confirms exactly one changed content entry (`lib/arm64-v8a/libfakeinput.so`),
with all 3,207 other content entries byte-identical. DEX retains the AC II opt-in,
controller preferences and Pokemon `ui.jar.cur.prof` fix. No full Gradle/R8 build
was run and no newer partial AC II payload was packaged into this update.
APK size 350834931 bytes; SHA-256
`01395c32c06d04b6a8c3aec5992d71d57038d4ea1e170a2aea3f2c0d167c0335`.
Reproducible local packaging/audit: `.work/ac2/rebuild_native_apk.py`,
`.work/ac2/controller-native-apk-audit.json`. Installation/live results must be
recorded separately; a successful build is not proof that gameplay input works.
The first versionCode 40 install was rejected because a concurrent update had
installed 41. No downgrade was forced. The replacement uses the verified local
version 41 APK, whose SHA-256 matches the currently installed package.
The version 41 controller update subsequently installed successfully with
`adb install --no-incremental -r`; the installed base APK's SHA-256 matches
`01395c32c06d04b6a8c3aec5992d71d57038d4ea1e170a2aea3f2c0d167c0335`.
The packaged native input library's SHA-256 is
`aa89b46e5bd9aed9d38e0ab106e2a77cf8d8e322f68261d874a2ae2397965f82`.
No game was launched and no input was sent. The user must test controls and reach
Ezio before the private inventory probe can validate the active-player owner.

Live diagnostic from the previous session (`.work/ac2/diagnostic-v1.txt`): the
game selected its native Xenon pad, type 2 / vtable `01da5430`, slot 4, with the
correct Xbox name and VID/PID. The renderer is already DXVK, not WineD3D.
The loaded dsound DLL was Microsoft Windows 7, motivating the staged AC II-only
`builtin,native` sound override. No successful audio or performance test is claimed.
EaglePatch is excluded at the user's request.

The fake evdev Xbox identity had two incompatible details: GAS/BRAKE trigger
axes instead of Z/RZ, and extra TL2/TR2 digital trigger buttons that shift later
button ordinals. Both are corrected only under `FAKE_EVDEV_XBOX360=1`.
Do **not** swap face buttons: Linux xpad uses `BTN_X=BTN_NORTH=0x133` and
`BTN_Y=BTN_WEST=0x134`. Primary contracts:
[Linux xpad](https://github.com/torvalds/linux/blob/master/drivers/input/joystick/xpad.c),
[Linux event codes](https://github.com/torvalds/linux/blob/master/include/uapi/linux/input-event-codes.h),
[SDL enumeration](https://github.com/libsdl-org/SDL/blob/SDL2/src/joystick/linux/SDL_sysjoystick.c).
The pure native mapping checks pass. This still needs a controller test on device.

Inventory research now traces `LogicalInventory`'s category containers through
`010d2c70`. Standard container vtable `01e58098` counts individual entries using
`010de9c0` (word `+1e & 3fff`); rechargeable vtable `01e790d8` uses `012d80d0`
(dword `+10`). Capacity uses `010de600` -> `010db600` -> `012dc150`: global
`0223f4f4`, repository at `+4c`, category array at `+50`, category ID `+8`,
level array `+10`, level ID `+8`, capacity `+c`. `InventoryDataItem` embeds a
LogicalInventory at `+c`, established by constructor `012dd970`.

`NativeInventory.h` implements those bounded read-only quantity/capacity
contracts with consistency checks and rejects untraced subclasses. Its tests
cover both quantity models, every interrupted read, changing container/table/
settings roots, missing pouch levels and pointer overflow. The reader is **not
connected to the live UI**: ownership of the active InventoryDataItem remains
unverified. Do not infer that owner from a vtable scan or an archive asset.

The windowless `tools/InventoryProbe.cpp` is ready for a loaded-save inspection.
It reads the player's already-resolved component cache and the native inventory
event subscribers without calling game functions or refreshing any native cache.
It never takes focus, acquires input, sends keys, or launches AC II. It remains a
private research helper, outside the payload. Its output is research evidence,
not a coherent gameplay snapshot or proof of safe action calls.
Targeted private evidence: `inventory-storage.txt`, `inventory-callbacks.txt`,
`inventory-readers.txt` under `.work/ac2`; headless research used 512 MB / one CPU.

## Device test handoff (2026-09-05)

### Controller compatibility follow-up

The user successfully launched AC II but reported keyboard/mouse-only input.
They own device testing and requested no device control. This follow-up performs
local source/build work only and includes the current Pokemon Legends Z-A host
loader changes in the APK.

The exact hashed retail executable's callback at `009c9540` creates a DirectInput
device and queries `DIPROP_VIDPID` before choosing its native Xenon/XInput path.
The Microsoft match is `045e:028e`; the original fake evdev device advertises
`1234:5678`. Skyrim's XInput-only profile also hides the generic DirectInput view.
Independent static inspection confirmed these contracts after consulting
[EaglePatch's controller research](https://github.com/Sergeanur/EaglePatch/blob/master/EaglePatch/src/ac2.cpp);
no third-party patch code or binary is incorporated.

`00412ca0` reads `[Input] SelectedInput` and matches its string against enumerated
device names before calling `009c8de0`. `004129b0` constructs the preferences path
from CSIDL_APPDATA plus `Ubisoft/Assassin's Creed 2/Assassin2.ini`. The preferences
writer at `004139d0` saves that same name.

The host now opts only AC II into an Xbox 360 evdev identity/name, enables both
DirectInput discovery and XInput on its shortcut, and selects that device in its
INI. Other sections and bindings are preserved, with a one-time original INI
backup. Skyrim's shared container defaults remain unchanged. This is generic
host controller compatibility; the AC II DLL stays in its separate payload.
Controller response, Wine's resulting device name, and button/axis mappings still
need the user's live test; static analysis and a successful build are not runtime
validation. Private decompilation evidence: `.work/ac2/controller.txt` and
`.work/ac2/input-config.txt`.

Handoff build: `1.6.17.9-ac2-controller-plza` / versionCode 38, release-signed with
the same certificate as the installed launcher. All 168 launcher unit tests pass
(5 AC II configuration cases; 7 module code-cache cases). Release assembly and
APK audits pass: the supplied AC II artwork is pixel-identical in the APK, the
controller identity and preferences paths are present, the Z-A ART profile fix
is present, and AC II/Z-A game payload implementations remain external. The
existing native Z-A host library and other games' player assets are preserved.

Manual-install APK: `C:/Users/Shadow/Downloads/DualScreen-1.6.17.9-AC2-PLZA.apk`,
331572972 bytes, SHA256
`7a885f2cbf19d073ed7aea1fc3ddca5971affc1f5a6721347d9c235a884f5a17`.
No ADB, device control, installation or game launch was performed in this follow-up.
The user should install the APK over the launcher and reopen AC II for the live
controller test. No new game ZIP import is required for this host update.

The user requested Skyrim's existing **Games container**, confirmed as container
1 in `com.dualscreen.launcher`, Proton 11 / FEX. AC II's original executable was
started from its existing `/sdcard/Games/Assassins Creed II` folder. Startup and
the title screen rendered. An external read-only probe successfully copied the
native manager roots. Launching through Skyrim's configuration session caused
focus conflicts; it is not the supported AC II launch path.

The user then requested direct access from the game list and took over testing,
using the most recently played save. The catalog now includes AC II as beta,
with exact executable validation and an executable-scoped dinput8 override.
`register_device_test.py` registers those existing files against Skyrim's actual
container ID and installs the separate `2026.09.05-dev.2` package. It does not
launch the game. Unverified save paths are not migrated or imported.

The package stays `development`; a hash-specific app-private test opt-in enables
only that locally installed package. Public downloads still require `ready`.
Delete `files/game-payloads/assassins-creed-ii/development-package` to remove the
test pin when a validated release is available. No release has been uploaded.

`NativeBridge.cpp` atomically replaces the verified PeekMessageA import at
`01ca332c`. Only empty-queue returns at `00b06a5b` / `00b06a91`, inside the traced
Scimitar pump `00b06a40`, qualify. Sampling is confined to the first qualifying
thread, reentrancy guarded and throttled to 250 ms. No gameplay calls or actions
are issued. `NativeRead.h` follows the native world/player selection and reads
aggregate collectible groups using `01123210`'s completion-state rule. Failed
reads, unresolved references and changed roots discard state. The four candidate
counters are feathers, codex pages, statuettes and Assassin seals; comparison
against a loaded game's menus is still required.

Additional private decompilations: `live-roots.txt`, `inventory-counts.txt`,
`game-tick.txt`. Native lifecycle/reader/render tests pass. The launcher builds,
and all 15 selected catalog/package JUnit tests pass. The independent panel and
package checks also pass (14 tests). No gameplay, transition or FPS result is
claimed for this development adapter.

## Exact executable

The device game is `/sdcard/Games/Assassins Creed II/AssassinsCreedIIGame.exe`.
It is i386, preferred base `00400000`, 34,693,768 bytes, SHA-256
`23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92`.
All addresses below are **preferred VAs**, not process pointers or RVAs.
Do not transplant them to another executable revision.

## Native systems identified

| System | Static evidence | Remaining proof |
| --- | --- | --- |
| LogicalInventory | Factory `010d41a0`, construction wrappers `010d4100` / `010d3f30`, vtable `01e57e40` | Owning entity and game-thread lifetime |
| Inventory UI | Get dispatcher `012da3f0`; original menu command strings resolve to calls below | Calling convention including ECX, output string ownership and context lifetime |
| Weapon statistics | `InventoryWeapons` reaches `012d9f70`, resolves current equipment via `012d9190` | In-game comparison for each weapon/category |
| Consumables / possessions | `012da010` emits quantity/capacity; `012da050` emits count; both use `012d9720` | Native ID translation, capacities, progression checks across saves |
| Ownership/lock status | `InventoryIsLocked` reaches `012d96c0`; current equipment resolver `012d9190` checks inventory services | Counterintuitive lock polarity and special categories need runtime tests |
| Cape selection | Send dispatcher `012d9ac0`; `InventoryEquipCape` reaches `012d9510`, then `010b3750` / `010b3c30` | Preconditions, game-thread execution, costume/Altair special case and preview invalidation |
| DNA | `MainSequenceCount` in dispatcher `01597580`, helper `015965c0` | Sequence context construction, locked-memory filtering, replay safety |
| Map manager | Factory `012aba80`, constructor `012ab680`, vtable `01e7682c`; constructor registers 12 event callbacks through `00530f40` | World component lifetime, event thread, discovery and level-generation validation |
| Map UI | `MAP_MARKER_PLAYER_POS` in dispatcher `012a2270`; marker-count query rebuilds its list through `012a1ad0` and native events | Independent context without opening original map, fog/district masks, marker/icon semantics |
| Player / inventory root | Call chain includes `004654d0` and `00948dc0` | `004654d0` requires an ECX owner; it is **not** a context-free singleton getter |
| Character preview | Original Ezio resource streams found and decoded | Isolated native render target, matching equipment/animation, camera/device restoration |

Targeted Ghidra output is under `.work/ac2`: `targeted.txt`, `constructors.txt`,
`objects.txt`, `map-callbacks.txt`, `command-handlers.txt`, `inventory-services.txt`.
These files contain the user's decompiled game and remain ignored research data.
The full-program analysis was interrupted; the targeted outputs completed.

## Original Animus interface recovered

`DataPC_extra.forge` contains 1,205 named entries, including native inventory
menus, map/collectible icons, database/codex images and DNA menu resources.
Three entries were decoded for inspection:

- `Animus_InventoryWeapons`: UEF v8 Flash-compatible menu plus original icon textures.
- `Logo_DNASequence`: DNA menu and shared fonts, including `AC2Aaux_ProLight_Latin`.
- `ItemFeather_MapDesc`: original 256×128 feather illustration; its DXT3 export
  was visually checked. Texture data starts at a header-declared block, not the
  end of the resource (which also contains a trailer).

`extract_uef.py` writes a separate research copy with an SWF signature, allowing
JPEXS to decompile the ActionScript. The original game archives are not modified.
The recovered menu's `Classes.Animus.Communicator` names the real FSCommand2 API.
Its Get calls return strings/numbers through `fscommand2Var`; Send calls perform
actions. These are native command names, not an implemented external API.

The original `EquipmentInfo` constants describe UI categories: armor 0–3,
small/normal weapons 4–5, blade variants 6–8/27/28, fists 9, reloads 10–14,
collectible categories 15/17–21/38, capes 22–26/33/39, and shop categories 29–32.
They are not physical item IDs. Original weapon popup menus mostly inspect
equipment; their existence does not prove a safe general equip action.

`DataPC.forge` has 72 entries. Bootstrap, Game Fix and Ezio streams contain
5,098, 10,698 and 588 indexed resources respectively. English localization was
decompressed, but its internal text encoding is not decoded yet.

## Reproducible local checks

Run PowerShell scripts with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`:

1. `build.ps1`: compile and run C++ lifecycle/touch/navigation tests, render six
   1240×1080 pages, build the i386 proxy DLL and verify native dependencies.
2. `tools/test_launcher_contracts.ps1`: low-memory standalone compilation of
   the package loader and on-demand manager; JUnit package/routing tests.
3. `python -m unittest discover -s tests -p "test_*.py"`: archive/resource bounds.
4. `tools/audit_native.py <exe> --output <private-json>`: exact hash plus native
   command references and call-edge checks. This is static evidence only.
5. `tools/package_payload.py --dll build/release/dinput8.dll --output build/payload`:
   reproducible development ZIP; no original game assets included.

The focused independent checks pass (13 JUnit tests), as do the native build,
render/input tests, three archive tests and four static native anchor checks.
The full launcher Kotlin compilation also passes; its log is
`.work/ac2/launcher-compile.log`. The earlier full Gradle test run was interrupted;
it is not claimed as a successful full test-suite run.

## Delivery and runtime status

Android recognizes only the exact private AC II panel class/title, claims it
for the lower screen, and suppresses synthetic guest mouse fallback. Controller
gameplay routing is unchanged. The native panel uses loopback UDP 7788, bounded
packet parsing, a non-activating window and one offscreen 1240×1080 buffer.

`OnDemandGamePayloads` fetches `<game-id>-payload.json` from the existing trusted
payload release before install/launch. ZIP size/hash, per-file hashes, paths,
duplicates, expanded size and manifest version are checked. The store lives in
`files/game-payloads/<game-id>/<zip-sha256>`; an atomic pointer selects a version.
The installer then applies its normal verified file overlay. Prior versions are
retained. No AC II DLL/assets go in the APK.

The generated descriptor remains `development`; ordinary remote downloads accept
only `ready`. The explicit local device-test exception is documented above.
The DLL is installed separately from the APK. Publishing is premature until
live synchronization and device tests pass.

## Next required work

The user owns the next device test, using the most recently played save. Validate
the initial read-only adapter against that save and prove native action scheduling
and object creation/destruction guards. Then connect inventory/player data, discovery-filtered map contexts,
DNA/database/progress services and an isolated Ezio preview. The UI now includes
scrolling equipment categories, inventory/database details, database categories,
all provided progress rows and a replay action gated by native eligibility.
Remaining work includes native icons/fonts and accurate data/action adapters. Test
loading, city/cutscene/mission transitions, purchases, pickups and reloads on
device before setting any release-ready status. Measure FPS/frame-time impact.

## 2026-09-06 startup validator correction

Device logcat proved the launch failure was `Invalid DLL override policy`: the
AC II catalog selected `dsound=builtin,native`, while the executable-scoped
validator accepted only `native,builtin`. The validator now accepts either
ordering and still validates DLL names before any registry write. Four focused
regression tests passed against the current catalog and real registry editor,
including scope preservation, repeated application and invalid-policy rejection.

Installed `Downloads/DualScreen-AC2-Startup-Fix.apk` (versionCode 41), SHA-256
`0b929d1a9bc6d691b50d6a4e66ab2adc6197a62c5832649353383163d37ecaed`. The
source was compiled with the project's Kotlin/Compose compiler. Only the
`ExecutableDllOverrideConfiguration` class changed: all other 735 classes in
classes21.dex have identical disassembly; the other 3,207 APK content entries
are byte-identical to the installed controller-fix base. Signing certificate
and 16 KiB native alignment were verified. Pok?mon Legends, Dishonored, the
controller library and the separate AC II payload are preserved.

ADB installation succeeded and the installed APK SHA-256 matches. An isolated
app_process harness exercised the actual installed helper twice against a fresh
temporary registry on the device; both AC II overrides were verified, and a
global dsound override remained unchanged. The harness did not touch the game's
container or launch/input state. The user owns the gameplay/controller/audio
test; these remain unverified. Local packaging and device audits live under
`.work/ac2/startup-*`.
# Recovery checkpoint — 2026-09-06, database and original artwork

Current source (beyond dev.3, not installed) passes the full sequential native
build including database, archive decoder, artwork cache and six-page rendering.
Database live reader verified 23 visible entries (20 historical, 3 letters),
native discovery flags, sequential letter replacement, Codex tracker ownership
and decoded state. NativeDatabase.h documents the exact addresses. Letter art is
at +0c, not +08; the native Various category is used for letters.

Latest fresh read-only report: `.work/ac2/equipment-catalog-live-20260906.txt`,
guest PID 232, player 1ee9e5a0, world 052a6750. User is playing; do not launch,
restart, send game input or replace the loaded payload. Earlier artwork-handles
report was stale after the game closed; fresh report timestamps are required.
The Python helper `.work/ac2/run_probe.py` enforces a changed timestamp.

Original artwork IDs verified live: database Leonardo a5776e76; medicine detail
58e12f7a / thumbnail 7d3064ee. Native getters 012da080/012da0c0 and 017fc070 read
handle+0c even when the resource is not resident. No native resource loader is
called. ForgeAssets.h supports v25 FILEDATA, bounded LZO2A, DXT1/3/5; private
archive test passes for medicine 512x256 and Leonardo 256x512. Raw portrait is
upside down in CPU bitmap coordinates; UiImage reverses rows and trims only
fully transparent padding. Artwork IDs alone cross to UI; AssetCache requests
only visible artwork, one archive worker, 32MiB cache, 64 pending IDs and 4096
bounded entries. No game art is included in the payload. Vendored LZO2.10 safe
decoder is GPL2-or-later with COPYING/UPSTREAM.md; include notices and provide
corresponding source with redistributed releases.

Equipment ownership trace: 010b65a0 service global 0223f6b0 is live subscriber
015f77a0 -> 010b70c0 -> 010d34c0 -> standard container virtual+54 / 010df420.
Definition+0c selects inventory category; container+18 pointer array holds items,
each item+4 is a definition handle, compared by resolved definition identity.
This is carried-item ownership, not proof of everything owned in Villa storage.
012d9190 selects the first unlocked carried entry for menu IDs 0..5;
01159050 filters catalog entries by menuID+14 and definition byte+30==0.
Catalog group 1 is armor, group 2 weapons. EquipmentInfo.as gives menu IDs:
0 chest,1 vambraces,2 shoulders,3 greaves,4 small weapon,5 normal weapon,
6 hidden blade,7 double blade,8 pistol,9 fists/cestus,28 poison blade.
Current early-story save has no carried armor/sword; one native category6 item
whose role is not yet established. Do not invent equipped state or expose the
entire unlocked catalog as owned gear. Native equipment reader still pending.

## Performance checkpoint, 2026-09-06 (dev.11)

The current dev.10 game is Android PID 5024 / Wine PID 232. Read-only thread
queries identify Windows thread 328 at DINPUT8 base 7a130000 + RVA 7700 as our
`Run` function (verified against local symbols); its Linux thread 5065 consumes
roughly 90% of one CPU core. The renderer was repainting on every 4 Hz sample
revision and using Graphics(HDC). It competes with native game simulation and
DXVK submission; low reported GPU utilization alone was not proof of a GPU fault.

Stationary, same-area SurfaceFlinger captures: original affinity 24.05 FPS,
p95 83.94 ms, p99 117.50 ms; all five configured big cores 37.89 FPS,
p95 50.36 ms, p99 50.38 ms. `taskset -ap f8` is still active for this game
process after the original masks were restored for the control capture.
Original thread IDs/start times/masks and raw captures are in private
`.work/ac2/affinity-original-5024.txt` and `perf-still-*.json`.
The Windows process mask remains cached as 0x17, system 0x1f, five CPUs.
NativeAffinity.h repairs this and the three exact IAT callers at returns
00421eb4, 00c4f6db, 00ec573d (each calls [01ca3278]). Hook installation requires
Runtime's full executable hash, Wine, exact container topology, system mask,
six-byte call signatures, and an unmodified kernel32 import target.
It preserves original API returns and invalid-mask behavior, and enumerates
threads only at startup and once after ten seconds. No priority, controller,
audio, resolution, shader-cache, or global container settings are changed.

A separately launched windowless, below-normal-priority render-only fixture in
the same container measured old map HDC 438.312 ms, direct DIB 313.068 ms, and
cached player overlay 0.295 ms. Equipment full repaint: 208.162 vs 170.497 ms;
unchanged equipment now needs no repaint. Cached mission/progress DNA updates:
2.047 / 2.084 ms. These are isolated fixture timings, not in-game frame rates;
the fixture never reads or writes game state and is not packaged in the DLL.
`.work/ac2/dev11-render-benchmark-device.txt` retains the complete fresh report.
RenderHarness verifies direct DIB presentation, scaled output, all-page cached
pixel parity, live-marker geometry, follow viewport changes, scrolling and input.
VisualStateTests cover sampling-only revisions versus actual displayed changes.
Live integration and sustained gameplay FPS must be checked after activation.

An unrelated stale Isaac fake-state injector and its spinning nc child were
identified by exact cmdline/parentage and stopped. This did not materially
improve the AC II frame-rate baseline and is not reported as the affinity gain.

### Follow-up and dev.12

User relaunched normally: dev.11 installed hash
`fbc350e731bdfd0fcb8787f052b5d3a8aaba4df1f3da2c77610957d70b0f2b17`,
new Android game PID 16003, Wine PID 232. Native install log confirms process
mask 17 -> 1f; Linux game workers have mask f8. The delayed thread repair reported
all threads unavailable: SetThreadAffinityMask also requires QUERY rights.
Dev.12 requests both QUERY and SET, logs the first error code, and tests the
actual enumerated-handle repair against a suspended thread in a local test exe.
The game import wrapper's emitted assembly was checked for the exact return
addresses, argument forwarding and original stdcall ABI.

Dev.11 first live capture was 40.23 FPS, p99 50.36 ms. Reserving CPU7 for the
game owner thread initially regressed to 17.94 FPS, so all saved masks were
restored by matching thread start times. The stale sender parent 8716 had trapped
TERM without exiting and resumed its next send through nc PID6652. After checking
exact commands, parentage, stale packet fds and absence of Isaac, only those two
diagnostic processes were killed. No game input, app termination or file deletion.
The subsequent 20-second capture measured 51.34 FPS, p99 33.58 ms, max 50.37 ms.
A second short core-reservation trial after cleanup measured 53.19 -> 55.68 ->
45.07 FPS (control / reserved / restored). Combat scene changes make the small
gain uncertain; broad masks remain restored, no prime-core reservation is shipped.

GPU readings are stale: every sample and the idle interval between game sessions
returned exactly busy=56734,total=1018476, while clock-time counters also froze.
KGSL pwrscale reports 0. Qualcomm's public kgsl_pwrscale_update_stats returns
without reading counters when !psc->enabled:
https://android.googlesource.com/kernel/msm-modules/graphics/+/refs/heads/android-msm-sw5100-5.15-android15-qpr2/kgsl_pwrscale.c
This supports the observed invalid telemetry; it does not establish true GPU load.
Capture reports now label it unavailable and retain the historical raw number.
GPUStatus.java source returns -1 when pwrscale is explicitly zero; no APK rebuilt.

Dev.12 separates steady-state heavy refresh domains into 250ms slots while keeping
initial reads and invalidations immediate, and adds ten-second sampling mean/max
timings. Tests cover slot separation, initial refresh and backward-clock handling.
Current integration finding: during Ezio combat the player+50 transform controller
is non-null, so the conservative NativeMap guard intentionally hides the map.
Resolve native 004161f0 -> 0045f880 / 0045f770 controller semantics before allowing
that state; never expose full undiscovered map as a fallback. A private screenshot
confirms original Ezio robes and city combat; latest probe reports actual cape
ownership, no carried weapon, 9.154930% native sync. Live Ezio preview remains open.

### 2026-09-06 dev.13 map-controller correction

The 004161f0 and 00416250 position/matrix accessors resolve player+50 through
0045f880. That function returns [controller+40]+10 after conditionally updating
it when 0045f770 reports dirty. Source kind 0 reports clean. For source kind 1,
0045f770 ANDs [provider] (provider at controller+54) with a mask involving TLS+3c
and the provider+8 sign bit. Zero provider flags are therefore clean regardless
of the sampling thread's TLS. Source kind 2 calls another predicate and is still
unsupported; nonzero kind-1 flags are also rejected without invoking the engine.

Dev.13 accepts only these proven clean cases, requires the matrix owner to equal
the current player, and rechecks controller ownership, kind, provider identity
and flags after reading discovery. Existing world/player/map/texture transition
checks remain. Tests cover each interrupted read, provider and owner changes,
dirty flags, and the existing discovery mask. This corrects the blanket +50
rejection without exposing undiscovered map information. Live confirmation of
the new payload is pending a new game session. The last live report observed
kind 1 with provider 02214e24; the updated private probe dumps that provider.

Latest dev.12 world capture measured 51.92 FPS, p95/p99 33.57/33.58 ms and max
100.71 ms. Sampling averaged about 2.1-2.5 ms in steady state. Stable 60 FPS is
not achieved. The stale GPU counter remains unavailable, not evidence of 5% load.

Audio bridge investigation is an engine/APK change, independent of the payload.
The installed guest ALSA plugin's transfer at 5388 uses one shared PCM buffer,
copies to its beginning and sends WRITE without an acknowledgement. The older
protocol differs from current upstream (which adds an offset and acknowledgement).
The launcher now selects socket PCM at both ends, including omitting PREPARE's
shared-memory reply. This removes dependence on an intervening POINTER request
for sample ownership. It is a transport risk, not yet a proven explanation of
every audible crackle in this game. Android partial writes previously discarded
the tail; the new write pump retries with explicit byte offsets, bounds stalled
writes, and retries only the unwritten tail after a disconnect. Ten-second logs
report partial writes, timeouts, dropped frames and Android xruns. Native handles
are checked against zero, preserving tagged pointers with the Java sign bit set.
The private device tests verified 768000 ordered synthetic samples through the
actual socket request handler and 576000 silent frames through actual AAudio,
with zero drops or xruns. In-game listening and performance validation remain.

## Dev.14 stability, map motion, and audio cursor investigation

The last dev.13 gameplay capture (`.work/ac2/perf-dev13-live.json`, 25 seconds)
measured 55.42 FPS, median 16.79 ms, p95/p99 33.57/33.58 ms, maximum 67.14 ms.
KGSL pwrscale was now 1 with changing counters: average measured GPU utilization
82.75%, clock 680 MHz, temperature sample 84.4 C. The earlier frozen 5% reading
must not be reused. No clock or thermal-limit override was applied.

The user subsequently reported a freeze/crash. The guest process later exited;
Android's host process remained alive. Socket EPIPE messages were teardown
symptoms, not a proven root cause. Old SurfaceControl crashes predate this
incident. A concrete independent renderer hazard was found: `currentTx` was
shared across separate begin/update/apply JNI calls with only shared context
locks. Concurrent scenes could modify or delete the same transaction, and SC
retirement could invalidate a pointer between lookup and use. The engine now
submits one copied scene batch under one context lifetime guard with a local
transaction, holding windowScMutex through lookup, geometry, and apply. Java
collection and submission share the existing X locks; surface registration is
serialized with context creation/destruction without taking X locks from the
lifecycle lock. Surface-change callbacks queue scene refresh off the UI thread.
A private ImageReader test passed 3600 concurrent batches, SC retirement,
context destruction/recreation, and malformed batch arguments. This establishes
regression coverage for the race; it does not identify every guest crash cause.

The live map caused 350-455 ms full-page redraws when marker lists changed.
Dev.14 separates page chrome, the discovery-masked city raster, and moving
markers/player. Marker motion/removal no longer invalidates the page. Panning
and zooming use bounded direct-DIB bilinear resampling, with reused horizontal
rows; no GDI+ city rescaling or page text layout runs for each gesture. The
local 1024-square map benchmark averaged about 1.8 ms per raster; this is a PC
measurement, not an Android/Wine frame-time result. Input polling is 16 ms with
conditional repaint, not continuous idle redraw. Observed player positions and
short-arc headings interpolate between native samples; world/player changes,
teleports, missing capabilities, and loads clear or snap immediately. No state
is predicted past the last observed point. Tests cover numerical resampling,
clipping, transparency, zoom bounds, marker trail removal, and pose lifecycle.

Audio still crackled after the earlier socket/partial-write fix even with
zero logged drops/xruns and one active game AudioTrack. ALSAClient.pointer()
incorrectly returned frames accepted by write(), including queued audio. The
ALSA ioplug contract requires the consumed/DMA cursor. The new clock subtracts
AAudio's outstanding frames from accepted frames, clamps monotonically, and
accounts for discarded tails on stream replacement without replaying them.
AAudio counters are read under the existing stream lock. A private device test
proved a paused 1920-frame queue reports cursor 0 (the old bridge reported
1920), then consumed all 576000 silent frames monotonically with zero xruns,
and reset correctly on prepare. This verifies the cursor defect and fix, not
the absence of audible crackle in AC II.

Primary audio contracts consulted:
- https://raw.githubusercontent.com/alsa-project/alsa-lib/master/src/pcm/pcm_ioplug.c
- https://developer.android.com/ndk/reference/group/audio
- https://raw.githubusercontent.com/wine-mirror/wine/master/dlls/winealsa.drv/alsa.c
Current Wine source is explanatory; it is not proof of the exact installed
Proton/FEX binary's implementation.

Custom game-code performance patches are authorized. Exact EXE inspection
traced TaskThread initialization at A31600/A316A0, dispatch at A31740, and
work acquisition at A314B0. Idle work acquisition waits on an event through
421DC0; removing that wait is not a justified optimization. The generic
critical-section constructor at 421C40 chooses a default spin count of 4096,
but no guest-symbol-resolved profile proves this is the current bottleneck.
Do not change synchronization semantics or bypass simulation work speculatively.
The existing exact-build game affinity IAT patch remains active. New native
game-code optimizations and locked-60 validation still require measured evidence.

The APK changes are engine fixes; dev.14 remains an independent AC II payload.
Full native 3D Ezio preview, remaining semantic equipment/use actions, broader
mission/save-transition testing, gameplay audio verification, and stable 60 FPS
are still unverified or incomplete. Do not label the whole requested mod finished.


## Dev.14: remove snapshot collection from the game thread (not deployed)

User cannot run a gameplay test now. Packaging/deployment remain explicitly on
hold; the local held APK has not been installed. No gameplay was launched or
controlled. The two temporary FEX BlockJITNaming/ProfileStats app configurations
were removed after exact SHA-256 verification, with receipt in
`.work/ac2/ac2-profiling-config-removal.json`.

Prior logs place 3-5 ms mean snapshot reads, with occasional longer samples,
inside the verified AC II message-pump hook every 250 ms. This is a concrete
periodic contribution to the game thread's frame deadline, not a complete
explanation of the reported sub-30 FPS episodes.

NativeBridge now schedules at most one read-only worker sample at that boundary.
The worker uses below-normal thread priority and the existing bounded
ReadProcessMemory readers, field/ownership checks, discovery filtering and final
root validation. It invokes no engine functions. Requests that arrive while it
is busy do not accumulate. Mutable adapter caches and native actions share an
atomic single-owner gate: the game thread skips rather than waiting when the
worker is reading. Native cape permission/mutation calls remain on the verified
game thread, with immediate root, generation, ownership and permission checks.
The command handoff uses try_lock and pins the command's matching immutable
snapshot together; it cannot wait for publication or UI scrolling. Native events
invalidate equipment for the next worker read. The proxy and worker are pinned
for process lifetime; failed installation stops the idle worker and closes its
handles before returning.

ReadTransaction retains its 512-page cap and exact consumed-byte coherence
rules. Whole-page equality now takes the fast comparison path; pages whose
unconsumed bytes changed use eight-byte comparisons with exact byte masks.
Mask marking operates by word rather than one bitset operation per byte.
Relevant changes still reject the transaction; independently changing retain
counts remain ignored when the reader never consumes them.

Regression coverage includes a deliberately stalled reader with 100,000
nonblocking action/schedule attempts, concurrent cache access, stale command and
load rejection, all 255 nonempty consumed-byte masks, unaligned and cross-page
reads, unmapping, overflow, and the maximum page count. Existing native reader
interrupted-read tests and full rendering/build checks also apply. None of these
checks establish gameplay FPS or audible sound quality. The worker avoids game
thread stalls but still consumes CPU and may reject more snapshots while game
state is moving; actual movement, city transitions, commands and frame times
need in-game verification when the user can test.


The additional headless NativeBridgeWorkerTests executes the actual bridge code
in a local test process without installing the IAT hook or loading the game.
It delays worker notification across 100,000 ServicePump calls, verifies sample
logging comes from a different thread, and verifies unavailable cape requests
are processed on the caller's game-thread boundary without invoking game code.
This passed. Full native suite/build passed in `dev14-worker-build.txt`; the
additional bridge test is recorded in `dev14-bridge-worker-check.txt`.

A local x86 O2 synthetic comparison of 256 copied pages, 32 fields per page,
200 transactions measured the previous algorithm at 1.346 ms versus 0.660 ms
with unchanged pages, and 1.075 versus 0.633 ms with unrelated-byte churn.
This is a 1.70-2.04x improvement in that memory-check benchmark only, with
in-memory source copies, not Android system-call costs or overall game FPS.
Benchmark source/results: `.work/ac2/TransactionBenchmark.cpp` and
`.work/ac2/dev14-transaction-benchmark.txt`.


## Confirmed shared audio regression and source-only rollback

The user reports every game now has no audio and explicitly forbids APK builds.
The currently installed APK hash is
`a536640e3c1603a70c9de6206a2e4f2acf7482f1df0aeaa392b20f0af82b2e50`,
not the earlier recorded AC II Audio-Fix build. Its assets, manifest and several
DEX files contain newer unrelated changes; a whole-APK rollback would lose them.
No APK was built/installed and no game was launched/stopped in this repair turn.

The consumed-cursor change introduced a concrete global startup deadlock:
Wine's 1920-frame ring becomes full, but this Android AudioTrack path requires
1922 frames to start. Returning consumed frames leaves the guest cursor at zero,
so the guest cannot submit the additional frames Android needs. Device logs
show repeated startup tracks queued with 1920 frames and no playback progress.
The earlier silent flood test bypassed the guest's ring-space flow control,
which is why it missed this failure. It must not count as end-to-end coverage.

ALSAClient.pointer() now restores the accepted-write compatibility cursor;
accepted partial writes increment it once and prepare resets it. Stream locking,
partial-tail ownership, tagged native pointers and socket transport remain.
The unused consumed clock class is not called by the runtime client. Native
queue depth remains available solely for diagnostics, not guest flow control.
A future consumed cursor needs a complete startup/guest-ring protocol solution.

`AlsaGuestRingStartupCheck` uses only digital silence and never lets the guest
outstanding count exceed 1920 frames. In a private app_process with the exact
installed libwemu.so and installed classes, it reproduced submitted=1920,
cursor=0, queued=1920 with no forward progress. With only the source-compiled
ALSAClient override, the same test submitted 15360 frames, verified native
playback advanced, and checked prepare resets the cursor. The test exits and
closes its own stream. No installed files or active games were changed.
Evidence: `.work/ac2/audio-cursor-startup-check.txt`, copied AudioFlinger/routing
logs, and `.work/ac2/audio-cursor-rollback-audit.json`. This confirms the startup
regression and compatibility rollback, not the absence of AC II crackle.
Any unrelated local native startup-padding work was not included in this test;
the test deliberately loaded the native library extracted from the installed APK.


## DNA sequence navigation continuation (source/local DLL only)

Missions now groups visible native memories by the high-word sequence identity,
keeping side-activity domains separate even when localized titles match.
It selects and scrolls to the active sequence/memory on entry, provides a
Current Memory shortcut with touch feedback, and supports independent sequence,
memory-list, and description scrolling. Current objectives precede long mission
descriptions. Sequence counts explicitly describe visible memories, not unknown
full totals. Hidden entries never create groups; exposed locked placeholders
retain hidden descriptions. Removing a sequence or changing save generation
clears stale selection/scroll. Replay remains gated by native capability.

MissionNavigationTests covers localized-title collisions, side/story identities,
visibility/locked placeholders and capability loss. RenderHarness covers initial
active-row visibility, sequence switching/current-memory return, independent
scroll regions, removal/load handling, and exports dna-sequences.png for visual
inspection. Full native build/suite passed in dev14-dna-navigation-build.txt;
final UI refinements were followed by another render test and native DLL build.
No APK builds, packaging, installation or gameplay controls were used.

Additional native action research (not enabled): the existing cape mutation at
015fc610 resolves category 0xd, removes its old entry through015fc220, and adds
the selected cape through015fc190. It must not be reused for weapons. The
UTF-16 SelectWeapon command at01e5b6c4 is parsed by011076c0 and calls011063e0.
This resolves the owner/player, checks its actor type, maps the selector value
through010e9830, and dispatches separate native events. Slot8 calls010d7830(1),
whose event ID is223f730 (initialized from hash5e452dd0); its registration is
at015f2d00. Other slots invoke010d58e0/010d69d0 and the existing weapon-state
query010d7350 reads event223f728. Slot meanings, subscriber ownership, blocking
conditions and safe selection semantics still need verification. No item action
was exposed or invoked on the basis of these leads alone.

## Source checkpoint: swipe map and guarded native actions

The new map layout follows the latest reference's left legend/status sidebar and
large right map. It retains original game artwork, discovery masking, icons and
localized category names. Legend counts and filters use only visible discoveries;
map motion does not invalidate the static sidebar unless category data changes.
Visible page navigation buttons and controller hints are absent. Six pages wrap
through directional swipes; map interior gestures remain pan/zoom, with page
swipes originating at the edges or header. Transitions use a 220 ms cached slide
and release their two temporary pixel buffers after completion.

The private map-swipe-preview.png is rendered from recorded Florence game state
and its matching discovery mask, not a current device screenshot. It does not
substitute the illustrative reference's map art, marker counts or player values.
Notoriety is still pending: the native HUD calls 010acfe0 on world component
01e575c8, choosing component+c or the 223f648/211c058 override. Units, ownership
and restrictions require further proof before exposing this value.

This checkpoint supersedes the earlier unknown SelectWeapon slot notes above.
Slot8 dispatches medicine through 010d7830(1). The game-thread action now verifies
ownership, category7 quantity/capacity, service+6c availability, UI restrictions,
player health and the unique current event subscriber before requesting medicine.
The native 017be610 consumer retains responsibility for healing and consumption.
Hidden-blade menus6/7 select primary slot1 through 010d75c0(1), with current
ownership, service+61 availability and service+5c idle sentinel6 checked. Both
paths resolve twice and enforce retry cooldowns; neither writes game inventory
or health directly. Shared event subscriber resolution rejects duplicate, null,
inactive or mismatched records. Fixture and actual x86 calling-convention tests
cover both actions; neither action has been exercised in the user's game yet.

The standalone read-only probe found AC II closed at the last attempted capture
and did not launch it. No APK build/install, payload packaging, device DLL update
or gameplay input was performed. The full mod is not complete: isolated native
3D Ezio rendering, broader contextual actions, remaining progression semantics,
end-to-end live validation, audible audio verification and stable-60 evidence
remain outstanding. Native local renderer timings are not gameplay FPS evidence.

Validation for this checkpoint: the complete native build script passed, including
native action ABI/guard tests, gesture isolation and cached slide parity, map
legend/discovery tests, six-page rendering and full-versus-cached map pixel parity.
The compass overlay explicitly shares the full renderer's text-rendering hint.
Build log: .work/ac2/dev14-swipe-map-build.txt. This builds only the local native
DLL and standalone test executables; it does not build an APK or deploy anything.

## Native map status and original player marker checkpoint

The notoriety HUD path is now implemented in NativeNotoriety.h. 019e63e0 looks
up the current-world component (vtable01e575c8) through0046cb50; getter010acfe0
returns component+c unless byte223f648 is nonzero, when it returns211c058.
Setter010addb0 clamps to [0,1], with endpoint snapping at approximately0.0005
and0.9995. Constants1ca5aec(float) and1ca7728(double) are BOTH1.0, not zero.
Disassembly confirms ECX still holds the component when the getter runs: Ghidra's
boolean-looking call argument is a recovered-prototype artifact. Component+8
is a separate allocated state object and is not treated as a world-owner pointer.
Evidence: .work/ac2/notoriety-range.txt and notoriety-native-state.txt.
The read-only reader validates current roots/player, bounded world membership,
unique exact component type, finite normalized values and every consumed byte
across its transaction. It follows HUD override precedence without clamping bad
reads into plausible percentages. Worker progress sampling publishes the value;
unavailable and loading states clear it. The map renders a percentage or a dash.
The recorded preview has no captured notoriety value and correctly shows a dash.

Per the user's new request, the custom player arrow is removed. Original
MapIcon.as type0 names Minimap_Icon_ezio_MapDesc; resource18f36fc0 references
texture18f36fc3 (minimap_icon_ezio_Map) in Animus_Map_Icons2318d8a3 within
DataPC_extra.forge. The asset worker now indexes this small embedded bundle,
reusing the original texture for the map and Ezio legend row. No original game
asset is included in payload source/packages. The private preview verifies the
production GameArchives result against a separately extracted original texture.
The native pose-derived facing cone remains separate from the upright icon.

The reference-style legend includes every category represented by visible,
discovered game markers and scrolls through all rows. The render harness now
exercises24 categories, reaches/selects the final row, resets the filter and
checks that shrinking discovery state clamps scrolling without affecting map
pan or page navigation. No undiscovered locations or invented zero-count
categories were added to fill empty space. Notoriety fixtures cover override
precedence, invalid ranges, duplicate components, player unloads and failures at
every underlying read. Live notoriety and game actions remain unverified because
the game is closed. No APK, game input or deployment was performed.

Latest map correction: hiding spots (native label282904 / texture5e5d5596)
are excluded through the shared MapMarkerVisible predicate. This removes map
symbols, hit targets, legend rows and totals consistently, including recorded
snapshots without category IDs. The same Florence capture now shows27 markers
including Ezio instead of103. The updated preview is
.work/ac2/map-original-marker-preview.png. This exclusion supersedes the earlier
all-visible-marker legend wording for the hiding-spot category only.

Validation: complete native suite/DLL build passed in
.work/ac2/dev14-native-map-status-build.txt. After the hiding-spot correction,
focused legend tests, the six-page render harness and original-archive preview
checks passed. Additional notoriety tests reject changes to the consumed override
flag, override value, component value and session root during transaction checks.
No live notoriety claim is made. The private read-only probe source now reports
availability and percentage separately; it has not been sent to the device.

## Carried primary-weapon selection checkpoint

NativeWeaponSelection.h supersedes NativeHiddenBladeAction.h. The same guarded
010d75c0 event path now supports the carried sword/long-weapon slot and dagger/
small-weapon slot in addition to hidden blades. 010e9830 maps wheel selectors4/5
to primary2/4; 010f9750 maps those to equipment subtypes1/2. Original menu catalog
records confirm menu5 primary weapons and menu4 secondary weapons with category2
and matching definition+10 subtypes. Native01106bf0 and017c2e20 query/publish
per-primary-slot availability; the reader consumes the matching owner+60+slot
byte, not the hidden-blade byte for every weapon.

Only the first carried, unlocked item recognized by the game's inventory menu
as equipped is eligible for sword/dagger selection. Selecting a stored alternative
is deliberately unavailable through this slot-selection API. Definition identity,
category, subtype, current actor, shared event owner, UI restrictions, pending
idle sentinel6, health and availability are revalidated before a native request.
No direct equipment, animation or inventory writes are performed. Existing
cooldown and nonblocking game-thread dispatch apply to all primary slots.

NativeWeaponSelectionTests exercises hidden/double blades, sword/dagger selectors,
wrong subtypes, unavailable slots, another owned weapon in the same category,
stale state, native rejection, cooldown and1000 real x86 ABI calls per slot.
The actual bridge worker harness still passes after this integration. Live action
validation remains pending; no game functions were invoked in the user's game.
Evidence: .work/ac2/primary-weapon-availability.txt and the existing recorded
weapon-service-live.txt catalog. Neither this work nor prior offline tests prove
stable60FPS, audible audio correctness or the unfinished native3D preview.

## Full original city legend correction

The user's follow-up clarified that the sidebar must show the complete legend,
not only categories present in the current capture. The source now carries the
original23 nonempty InformationCityLegend category layers (excluding the user's
explicitly excluded hiding spots), plus a separate Ezio row. Names are decoded
from the original legend localization IDs. Mission destination/target/object/
ally types2,3,40,41 share their original mission-objective category, and fast
travel variants share one row. This replaces the earlier all-MapIcon-type catalog
approach: generic minimap-only symbols such as archer are not inserted into the
city legend. Additional genuinely reported map categories remain discoverable
through their original marker records.

Category presence no longer implies a discovered instance. Zero-count categories
remain available for filtering; only verified visible/discovered marker records
contribute to counts or map symbols. mapMarkersVerified distinguishes a valid
zero from a temporarily unavailable native marker reader (dash). Labels remain
single-line and ellipsize within their row rather than clipping a second line.

Evidence: original Classes/Animus/Map/InformationCityLegend.as and
Menus/MenuMap_CityTransitionIn.as (city import excludes types20,21,11,32).
Implementation: NativeMapLegend.h, State.h, MapLegend.h and NativeBridge.cpp.
Preview: .work/ac2/map-full-legend-preview.png. Recorded-state total remains27;
there are23 catalog categories plus Ezio, not23 newly discovered locations.
Earlier statements that empty categories are intentionally omitted are superseded
by this explicit user correction. Hiding spots remain excluded everywhere.

Final validation: complete native suite and DLL build passed in
.work/ac2/dev14-full-legend-build.txt; SHA256 98C36DD42E1492DDD4962FFCC75F6D392A101D73A0F365C400CC7484A8C35925.
No deployment or APK build. The full mod remains unfinished, including isolated
native3D Ezio rendering and live validation of the new reader/actions. The native
ProLight resource26bcff4f was located privately inside the original map UI bundle;
it is a game font resource without an embedded standard TTF/OTF signature, so no
unverified direct GDI font loading was added.

## DNA condition checkpoint (2026-09-07)

NativeDnaCondition.h adds a bounded read-only implementation of verified
005f7aa0/007e8ab0 branches. Exact condition vtable01cd09b0 invokes007e3070:
no attached owner returns false; an attached owner with global2214b64 zero
returns true; +18 inverts the result. A nonzero global requires00a11010's
identity resolution and remains unsupported. No native evaluator is called.
The special-mode branch accepts the current mission state through configuration
+934's resident reference; exact01cd09b0 is rejected for other states, following
005f4ea0. Null conditions are accepted even in special mode. Forced availability
bypasses the initial evaluator exactly where005f7aa0 does, but does not bypass
unverified special-mode restrictions. Unknown evaluators remain hidden.

ReadDna now runs in a bounded ReadTransaction, retaining its existing explicit
identity checks and validating every consumed condition/configuration byte
before publication. Tests include positive/negative/inverted conditions, current
mission and special mode, unknown native selection, failures at every underlying
read, and changes to owner, vtable, inversion, selection and resident references.
Existing DNA tests for title concealment, entitlement, lifecycle and active state
continue to pass. Evidence: .work/ac2/dna-condition-evaluators.txt and existing
dna-condition-dispatch.txt/dna-status-disassembly.txt. Composite condition types,
nonzero selection identity resolution and missing-DLC placeholders remain pending.
This is another verified source branch, not complete DNA coverage or a live test.

Validation outcome: focused DNA condition/lifecycle/entitlement tests and actual
bridge worker tests passed. Native DLL compiled and dependency checks passed;
SHA256 D9A0545EAC3549464CC5BE49D2F9680182D639BE966A40619C98988CBA680648.
No APK build or payload deployment. UI preview remains the last full-legend image.


## 2026-09-07: live character renderer ownership and map gesture rendering

The private windowless read-only probe was rebuilt and run while the user was
controlling Ezio. Fresh report `.work/ac2/character-source-live.txt` (device
mtime 1788754838) resolved **12** player-owned MeshInstanceData objects, all
bound to the same SkeletonComponent, with bone maps of 74, 74, 26, 22, 8, 27,
14, 24, 25, 29, 82 and 13 entries. No game function, controller input or memory
write was performed. The user subsequently confirmed closing the game manually.
No renderer/payload was installed and there is no completed 3D preview yet.

Exact-build renderer evidence (same executable hash as the baseline):

- `00ae16b0` resolves a visual component's resource into `visual+10` and binds
  `instance+4` back to that visual through `00ae4ff0`. Visual vtable is
  `01db4508`; MeshInstanceData vtable is `01db4e88`.
- `00affee0` binds `instance+2c` to SkeletonComponent `01cb3568`; the skeleton
  asset is `component+1f0`. The per-mesh uint32 bone map is `instance+30`.
- `00b004f0 -> 00ad5da0 -> 00ad3fd0 -> 00b280d0` registers with the MAIN WORLD
  renderer. `instance+8` is a **one-based ID**, not a pointer. Sources live in
  the array at `0221da5c` (count `0221da60`), corresponding renderer objects
  in `0221da68` (count `0221da6c`). `00b2dd80` lazily creates missing objects;
  the reader deliberately never calls it.
- `00be5570` selects renderer variants. Live capture includes vtables
  `01dcfbb4` (constructor `00c078b0`, 0x44 bytes) and `01db6848`
  (`00b37a50`, 0x74 bytes). These require different draw paths.
- `00c06880 -> 00c05e60 -> 00c04430` performs mesh submission/skinning.
  `00b37b00 -> 00b37600` is the alternate mesh path. Both depend on an engine
  graphics context and modify its cached graphics state. A D3D state block
  alone would not restore that engine-side cache. Do not directly invoke
  these functions as an isolated preview pass.
- `00c04430` uploads palette constants starting at vertex register 0x78,
  three registers per palette bone; further matrix/camera validation is
  required. `00c04240` can consume/free temporary mesh upload data; do not
  call it to obtain a second copy of the model.

`NativeCharacterSource.h` now captures a bounded, coherent diagnostic graph,
checking current roots, component/instance ownership, unique skeleton, bone
indices, matching source registration, and lazy renderer slots. Identities are
not retained objects and MUST NOT cross into a drawing callback as raw pointers.
It is used by the research probe only; it does not advertise Preview capability.
Tests cover read failure at every read and identity changes during capture.

Map gesture source changes: SSE2 vertical bilinear resampling (same SSE2
baseline as this executable); no redundant full-surface backdrop copy on
viewport-only changes; immutable backdrop for transparent-map compositing;
bounded pre-scaled marker sprites drawn directly into the presentation DIB.
Guard/Archer categories 276391/276392 (types20/21/60, native icon5e5d558c) now
render at20px instead of36px. Category names verified against the original
English localization resource. Touch hit areas and discovery rules unchanged.
Local resampling checks cover every interpolation weight, scalar/SIMD pixel
parity, alpha, repeated-frame ghosting, map bounds and zoom. Sprite checks cover
native enemy size, discovery/filter exclusion, image replacement and clipping.
The private device zoom timing comparison was not launched because AC II had
already been closed. Desktop timings do not establish device smoothness/60FPS.

Remaining preview work: verified camera/transform constants and material draw
state; both skinned/alternate mesh paths; character-only render target with
complete engine/device-state isolation; resource ownership through save and
level changes; efficient readback/presentation into Snapshot.preview; live
visual/equipment/performance checks. No static character art substitutes for it.

## 2026-09-07: verified live geometry ownership; draw-range reader

After the user confirmed gameplay, the private read-only helper captured
`.work/ac2/character-geometry-live-2.txt` (device mtime 1788756314): 12 matching
geometry bindings, zero pending, zero unsupported. Eight were skinned renderers
and four alternate renderers. The first capture was rejected as incoherent;
the reader was not weakened. These captures verify ownership, not a working
3D preview. No engine calls, memory writes, game input, or payload install occurred.

Exact-build evidence added (same executable SHA256 as above):

- `00af9a20` gives mesh+38's one-based geometry registration. `00b280c0`
  resolves source array `0221da1c` / count `0221da20`, and object array
  `0221da28` / count `0221da2c`. Both source identity and renderer linkage
  must match within the same read transaction.
- `00be4710` creates skinned geometry `01dcf9f4` through `00bfe760`, linked
  at renderer+28, or alternate geometry `01db67a8` through `00b335d0`,
  linked at renderer+20. The reader never invokes these constructors.
- `00bfdee0` calls D3D9 DrawIndexedPrimitive (vtable+148) with base vertex 0.
  Geometry+8 and +10 are arrays of 20-byte ranges: primitive type, minimum
  vertex index, vertex count, start index, primitive count. `00bfb3e0`
  produces triangle-list/strip types 4/5. The local d3d9.h declaration
  independently confirms argument order. Count flags are masked as elsewhere
  in the engine; the secondary array may alias the primary array.
- `00bfdc90` allocates skinned geometry's vertex/index wrappers at +24/+28;
  stride is geometry+20 low 11 bits. `00b11970` constructs the 12-byte vertex
  wrapper `01db5a6c`; `00b118d0` constructs index wrapper `01db5a78`.
  Both store the COM interface at +8. These are diagnostic identities only:
  no AddRef, Lock, COM calls, or DXVK-private-layout reads are performed.
- `00b36030` submits alternate meshes using dynamic per-instance streams and
  modifies engine caches. Their shared geometry buffers alone do not form a
  complete preview mesh. This path remains deliberately unexposed.

NativeCharacterGeometry.h now validates this graph in one ReadTransaction,
using the extracted ReadCharacterSourceFields reader. Copied skinned draw ranges
are capped at 128 per array with bounded vertex/index arithmetic; invalid or
changed identities reject the entire capture. Tests exercise both variants,
every failed read, ownership changes, array aliasing, draw bounds, swapped buffer
types, and valid lazy slots. NativeCharacterSourceTests and
NativeCharacterGeometryTests pass with warnings as errors; the windowless probe
compiles successfully. The geometry test is included in build.ps1.

AC II was no longer running when the expanded draw-range probe was ready.
The launch wrapper refused to start it; there is no fresh draw-range/buffer
capture yet. `.work/ac2/character-geometry-probe.exe` is the updated local helper.
Dev.14 remains installed. No new runtime DLL, payload package, or APK was built.
Preview capability remains disabled pending real geometry/material/pose capture,
render-thread ownership, isolated character rendering and live validation.

## 2026-09-07: layout polish and off-screen preview components

User requested health/florins only on Map, Equipment panels aligned around the
3D area, a slightly wider Map sidebar, taller Player Status, the currency's
Florins label and no wrapped notoriety percent. Source now implements those:
Map sidebar is 30px wider; its scroll viewport is 402px tall, giving Player
Status another 30px. Notoriety has an 85px no-wrap field. Equipment has three
panels with aligned y=238..962 bounds, equal 19px gutters and matching scroll/
touch coordinates. Swipe navigation and map aspect/pinch calculations remain
shared with the production raster path. No APK changes.

The full native build/checks passed (`.work/ac2/preview-target-layout-build.txt`),
including six-page rendering, marker scrolling/touch, cached map parity and the
existing native reader/action checks. DLL SHA256:
007172f5e88d9db9f7842d2c5a2f6ef1e2e0adca9db94a124672c9ef4d3d4fe2.
This DLL is local only. Installed dev.14 is unchanged. The latest preview is
`.work/ac2/map-expanded-preview.png`, rendered through the production DIB/map
path using `.work/ac2/character-palette-live.txt` and its exported discovery mask.
The capture reports 20/20 health, 100% notoriety, 41 markers plus Ezio, and the
category1 container's scalar is 0xdb5 florins. The picture is a recorded-state
preview, not a current device screenshot. Percent placement was inspected.

Live geometry/pose research:

- A complete `.work/ac2/character-draw-ranges-live-complete.txt` passed all12
  geometry bindings including skinned buffer identities and draw-range reads.
  Two initially pulled reports were incomplete, not failed geometry captures:
  the wrapper had treated a changed file mtime as completion. InventoryProbe now
  writes a final `probe-complete` sentinel on normal exit; the private launch
  wrapper waits for this sentinel before pulling (bounded45s).
- `.work/ac2/character-palette-live.txt` (mtime1788758609) passed all12 geometry
  bindings and bone-palette coherence for all8 skinned meshes. Alternate4 meshes
  retain their separate unresolved dynamic rendering path.
- `00c062f0` builds skinned palettes from mesh+24's 24-byte bind records and
  instance+30's bone remapping. `00c051b0` stores each palette in216 bytes:
  count at0, max42 bind pointers at4, skeleton-index bytes at ac. The new
  NativeCharacterPalette reader validates ownership/range and each mapped
  skeleton index, copying bind bytes without inventing pose semantics.
- `0045f0b0` supplies the identity matrix to `00c05e60`; `00bd1800` combines
  world and pass-camera matrices; `00bd1680` exposes the result. `00b35dd0`
  transposes via `00b35790`; `00b34b50` maps semantic95 to vertex register0
  (semantic131 maps to8); `00b11a20` handles the bounded four-register upload.
  Thus the skinned identity-world path can supply a same-pass camera basis.
  Material/pass-specific overrides still require verification before hooking.

New isolated components (NOT connected to the AC II runtime yet):

- CharacterPreviewTarget: render-thread ownership, small render/depth target,
  full D3D state capture plus explicit render/depth/viewport/scissor restoration,
  exception/failed-draw restoration, 15Hz pacing, one outstanding query, no
  forced-flush fence polling, bounded BGRA readback, generation/page discard,
  explicit resource invalidation before device reset. GetRenderTargetData still
  needs device profiling; it is not promised stall-free.
- CharacterDrawPacket: captures bound indexed geometry with actual retained COM
  buffers/state, verifies vertex/index bounds and expected buffer identities,
  rejects wrong-generation draws, and restores the preview viewport after state
  application. It does not dereference the research graph's raw addresses.
- CharacterPreviewCamera: independent Z-up view/projection and guarded matrix
  inversion/reframing under the verified transposed c0..3 convention. It
  preserves native bone constants. DrawNative uses this only for a verified
  same-pass camera; no gameplay camera writes occur.

Focused native tests passed for palette ownership, every read failure, changing
identities, camera framing/transposition, singular/nonfinite matrices and the
real D3D9 target. The graphics test actually draws indexed geometry, releases its
original buffer references, reads the pixels back, and exercises a compiled
vertex shader with a changed camera in c0..3; the geometry moves as expected.
The test triangle/shader are private verification fixtures, never UI substitutes
or packaged game assets. Shader compilation is a graphics-test dependency only.
The optional build.ps1 -GraphicsTests runs the actual-device graphics check.

Remaining before an Ezio preview is functional: verified main material/pass
selection; safe render-thread capture hook and device-reset lifecycle; the four
alternate meshes/dynamic streams; atomic same-frame character assembly and
Snapshot.preview publication; live equipment/transition/performance validation.
The components and readers do not enable Preview capability by themselves.
No new payload was packaged or deployed and no APK was built/installed.

## 2026-09-07: dynamic Ezio geometry and complete-frame assembly

The user confirms launcher67 (Performance + full x87 precision, affinity fix,
30FPS cap) fixes landing and mostly sustains30FPS. Preserve that configuration.
No translator, audio, affinity, executable, installed payload or launcher settings
were changed during this renderer work.

The four alternate parts now have a verified CPU-data path. Disassembly of
b36030 -> b089e0/b08920 shows renderer+4c uploaded for renderer+58 bytes into a
shared1MiB vertex ring. Geometry+10 is stream0 stride, +14 stream1 stride;
b35600 returns geometry+1c's static stream1 wrapper. Full-detail index slot0
at renderer+60 matches geometry+20. Other index selections remain unsupported.
Do not treat renderer+58 as a COM pointer: it is a byte count.

NativeCharacterDynamic.h reads bounded owned bytes, validates registry/owner,
buffer types, counts/strides and full-detail index identity, and rejects changing
consumed bytes. The read-only windowless CharacterDynamicProbe (no game/D3D calls,
no input and no process suspension) validated all four parts in live gameplay:
three205-vertex/357-triangle parts,24/12 byte strides,4920 bytes each; one510-
vertex/595-triangle part,12240 bytes. Evidence:
.work/ac2/character-dynamic-live-2.txt. Initial attempts failed final coherence;
attempt3 passed in6ms. The data must ultimately be captured on the owning render
thread; do not retry this whole remote graph in the per-frame path.

CharacterDrawPacket can now upload an owned stream0 snapshot and replay it at
its own offset while retaining native shaders/material/state and static stream1.
Dynamic stream0 without an explicit snapshot, dynamic indices, other dynamic
streams, instanced streams, invalid strides and out-of-bounds streams reject.
This prevents retaining a live ring buffer and later drawing overwritten data.
The real D3D9 graphics test uses two vertex streams and a nonzero ring offset,
then overwrites the original ring and clears the CPU snapshot. Captured geometry
still renders correctly. No test fixture artwork is packaged or shown as Ezio.

CharacterFramePlan/CharacterPreviewAssembly now collect every declared
instance/material section from one world/frame/pass. Incomplete, duplicate,
failed, wrong-frame and wrong-pass submissions cannot form a preview. Actual
D3D9 two-packet assembly/reframing/readback passes; generation/duplicate/lifetime
plan tests pass. All native build checks including -GraphicsTests completed;
focused dynamic and assembly tests were rerun after their final changes.
Evidence: .work/ac2/character-dynamic-build.txt and the new native tests.

These components are still NOT connected to runtime draw hooks or Preview
capability. Remaining: verify native main-pass selection/callback overrides,
install render-thread capture and reset lifecycle, assemble exact visible native
sections and same-pass camera, publish Snapshot.preview, profile on Thor and
validate equipment/world transitions. The full-detail dynamic path is validated;
alternate LOD/index paths are not. No new payload was packaged/deployed and no
APK was built for this renderer work. Installeddev21 remains active.

## Development render-pass observer (pending native build/deployment)
Added NativeCharacterPassTrace: exact verified vtable slots1dcfbb8/c06880 and
1db684c/b37b00, exchanged atomically after the existing executable hash gate.
It forwards the native ECX/two-stack-argument/ret8 ABI and performs no D3D calls.
A maximum200ms metadata capture selects only the current player's renderer
identities, records at most128 pass/argument/context/queued/device/thread rows,
then disables collection for the rest of the process. Logging occurs on the
panel thread after callbacks drain. No render-thread logging or allocation.
Diagnostics.CharacterPassTrace=0 disables these observer hooks at startup.
The observer default is enabled for this development payload only.

Pass capture is needed because skinned/alternate method1 may submit to either
an immediate D3D context or an engine command queue. Main-pass identity and
material camera override at pass+360 must be observed, not guessed. Preview
capability remains disabled; observer installation is not a working3D preview.
Tests verify ABI forwarding, ring bounds, inactive fast path, expected old slot
values, failed swaps and restored read-only page protection. New native full
build in progress; no runtime hook was injected into a running game.

Dev.22 built and staged successfully in the launcher's independent payload cache.
Cache/package SHA256: b49e56fff2141b13a25ea1e0c8e57a31079fb71c8e7caf2e84670c8bcf6ad409.
DLL SHA256: 9825be1038fcb8df512ae071f0e49665afafb7666ab23e2409fa8ffafd175911.
Previous dev21 cache and files retained. Staging receipt:
.work/ac2/dev22-install/stage-result.txt (staged=1, game_files_changed=0,
apk_changed=0). All full native checks and real graphics tests passed.
The running game was closed by the user before staging; no game was closed,
launched or controlled by the agent. Current game DLL remains dev21 until the
next launcher activation. User has been asked to load Ezio for automatic bounded
pass metadata capture. No3D preview image has been published yet.

### Remaining page work after dev.22 staging

Preserved the user-confirmed launcher 67 AC II configuration (Performance,
full x87 precision, affinity repair, 30 FPS cap). No APK, executable, container,
audio or device payload changes in this UI pass.

Archive navigation now chooses a real unlocked category/entry, preserves valid
selection and reading position across snapshots, and recovers if an entry is
removed or locked. Inventory removes stale selection and detail scroll when its
item disappears. Generation transitions clear pressed-action state. Inspect-only
items and rejected/stale action queues cannot display accepted-action feedback.
The empty inventory label now covers all possessions instead of only consumables.
UiNavigationTests exercises live lock/removal changes, preserved reading state,
inspect-only taps, stale command rejection and generation changes.

Still pending: actual native render-pass capture and preview publication; guarded
native contextual actions beyond cape, medicine and carried-weapon selection;
conditional/DLC mission branches and additional verified completion aggregates.
Do not populate these gaps with fixture values or claim the entire mod complete.
Validation: full build.ps1 -GraphicsTests passed, including all native readers/actions, real D3D9 tests, UiNavigationTests and six-page rendering. Log: .work/ac2/remaining-ui-build.txt. Local DLL rebuilt; staged dev.22 ZIP remains unchanged.

Dev.22 live render capture: hooks installed, 12 selected meshes, zero records in initial 200ms startup window. Read-only pass-objects-live.txt confirms all 12 current renderer vtables contain observer addresses. This does not prove the methods are invoked during gameplay. Dev.23 waits for a stable player and uses at most three 2s windows, counts unmatched calls, and retains bounded reads/no D3D calls. Retry and termination tests passed; native DLL rebuilt. No Preview capability enabled.

### Dev.23 successful live render-pass capture (2026-09-08)

Receipt: .work/ac2/dev23-render-live.log and dev23-pass-summary.json. First
stable-player window captured 128 records (truncated bounded buffer), out of
1140 observed method calls. The retained records cover nine distinct player
renderers, both renderer kinds, on five worker threads. Every record uses the
queued path. Modes/flags/callback: 2/41/0 (63), 1/1/b39840 (58),
1/4/b39840 (7). No argument pointer equals the expected mesh instance.
Do not treat the second method argument as the instance, or copy bound D3D
state in these producer hooks. Missing meshes in this truncated capture do not
prove missing gameplay geometry.

Static executable trace resolves b3e560 as opcode14 producer (8-byte header,
six DWORD payload); b3dd70 dispatches through 020dfe50 after skipping the header
uint16 inline count. Slot 020dfe88 points to b3ec10, which forwards six payload
DWORDs to device vtable+148 (DrawIndexedPrimitive), returning payload+24.
Queue allocation b3f050 initializes a 5MiB allocation. Dispatcher clears consumed
range fields and the queue is reused; a pointer alone is not a persistent draw ID.
Evidence disassembly: .work/ac2/ezio-command-dispatch.txt.

Added NativeCharacterDrawCommand.h and tests for the verified payload layout,
signed base vertices, inline-byte offsets, bounds and arithmetic overflow.
Tests pass. Not yet connected to an execution hook: next integration must carry
player/pass/frame attribution from producer ranges to execution, reject reused
ranges and incomplete frames, and capture native state only on execution thread.
No new payload installed and no game/runtime/performance settings changed.
