# BOTW bottom-screen preview and map discovery

Status: source implementation and isolated tests, **not an installed gameplay acceptance test**.
No APK was built or installed for this change. Native gameplay Hold/Eat hooks are unchanged.

## Held preview

The old preview displayed one flat ingredient icon at a fixed screen position and
stopped at the last sampled Hold animation frame. It had no collection of native
held actors and no live skeleton pose.

`botw_held_preview.inc` reads the supported executable's Player, CarryBox links,
1–5 held actors, ModelNW skeleton, and model matrices. A live two-material capture
verified 121 bones, the `Weapon_R` attachment, and each ingredient's native target.
The reader checks actor identities, collection membership and pose consistency;
it never writes game memory or uses pause-menu objects.

`BotwHeldPreview.kt` converts native world matrices to bone-local transforms and
hand-relative item transforms. `BotwPlayerRenderView` parents up to five independently loaded ingredient models
to `Armature_Weapon_R`. Release and replacement invalidate pending model loads.
Only the visible held preview requests animation-rate samples. Existing Eat and
inventory state mutation remain on their existing paths.

### Preview startup, pose and scale correction

- A cached portrait could return with no renderer. Hold only polled a reader that
  immediately returned without an engine; Eat happened to restart it. Hold now
  explicitly wakes the renderer, and reopening a held portrait bypasses the idle
  cache shortcut. The production methods are exercised by the JVM regression test.
- The portrait no longer applies the entire gameplay locomotion skeleton. It uses
  native `Lift_Lift_Pouch_Few` / `Lift_Lift_Pouch_Many` resources with the 0..5
  blend range from `PauseMenuPlayer_Lift.bas`. The live Weapon_R-relative actor
  matrices still drive the real ingredient models; no icon/static position is used.
- `F7849C..F784D0` supplies ModelUnit with Model+58 and the component-wise product
  of Model+88 and Model+94. The reader previously omitted that scale. It now applies
  this product to the three basis columns once, leaving the native center-corrected
  translation untouched, and verifies the scale did not change during sampling.
- Bone interpolation now blends translation/scale and shortest-arc quaternion
  rotation instead of shrinking/shearing rotating matrices. Existing Hold and Eat
  samples were regenerated identically; only HoldMany was added to the asset index.
- Missing Eat clips no longer leave `heldItemEating` latched. Held collection
  replacement/release is processed even while the Eat preview is active.

### Stationary portrait attachments follow-up

The release was built and installed, and the user confirmed working rendering but
reported that gameplay movement still shifted the ingredients inside the stationary
portrait. `stationaryAttachments` now retains the native hand-local placement for
the ordered held collection. Player/actor identity, item type, count/order, release
and renderer lifetime invalidate that layout; native model-scale settling remains
live. The items remain skeleton children and follow the portrait's own animation.
Unchanged attachments no longer trigger a render on every native poll. JVM tests
cover 73 movement/rotation samples with duplicate items, scale changes and all
invalidation paths. This follow-up has not yet been packaged or installed.

Ingredient assets are reproducible through `generate_botw_equipment_manifest.py
--materials-only`, followed by conversion and `install_botw_equipment_assets.py
--model-subdirectory held`. The release validation checks indexed model files.

## Inventory scrolling

The entire grid previously switched to a loading panel on an uncached visible
icon. Scrolling rounded to whole rows, and LRU eviction recycled bitmaps still
potentially used by RenderThread. The grid now stays present while individual
icons load, uses fractional scroll positions and momentum, prefetches adjacent
rows, and avoids explicit recycling on cache eviction. Async completion guards
reject stale generations before touching current pending requests.

## Existing world-map discovery

The old map drew the full terrain texture and static tower/town markers without
checking this save's discovery state.

`generate_botw_map_discovery.py` extracts LocationMarker/SaveFlag metadata from
the game's Bootup resource and resolves each key's type from its GameData schema.
`botw_map_discovery.inc` reads those typed values from the
active GameData buffer, not a mod-owned discovered-location list. Terrain masking
uses the live Ecosystem MapTower BECO runs and native `MapTower_XX` flags.

Supported-update evidence:

- `1185744..1185758`: Ecosystem map-region initialization, including `+1F0`.
- `11858A8`: native region lookup and clamp/round rules.
- `109D228..109D278`: region index + 1 to `MapTower_%02d`.
- `D86BDC..D86C34`: saved MapApp_MapIconNo/MapApp_MapIconPos entries.
- `D89B58`: selectable stamps 27–35 and pins 37–41; enum 36 is unused.

The map polls while visible, reuses immutable geometry, and invalidates discovery
on loading/unavailable state. Locked terrain stays covered even if its texture
was previously cached. Cached shrine state cannot bypass unavailable discovery.
Saved stamps/pins use native asset textures; no default quest-target array is
displayed. Such arrays contain future positions and need separate native gates.

The missing town/tower marker regression was a type mismatch: Location_* keys are
saved s32 discoveries, whereas MapTower_* keys are bool terrain unlocks. The old
reader searched both in the bool buffer. The corrected reader uses s32 count/data
at +18/+20 and the existing read-only hash/value getter decoder for locations.
Zero stays undiscovered; positive discovery values are visible. Regression fixtures
include Hateno=74, Kakariko=11 and Location_MapTower07=6 from a read-only save audit,
plus an undiscovered location. Even values must not be interpreted as bool bit 0.
Terrain reveal continues to use only the separate native bool tower flags.

## Checks performed

- Native reader harness compiled with Android NDK and ran on the connected device:
  1–5 slots, duplicate ingredients, insects, release, stale links, malformed
  hierarchy, torn pose, executable/lifecycle checks and read-only memory.
- The same harness checks region flags/revision reuse and saved/removed/invalid
  pin and stamp entries using the production getter decoders.
- Production Kotlin matrix/parser test: 219 rotation/scale/translation cases and
  reconstruction of all 121 bones from the native capture.
- Production scrolling test: 567 hit tests, fractional refresh stability, limits
  and redraw coalescing.
- Production region/fog test: 7,500 samples against the native BECO, save revision
  invalidation, marker changes/removal and unavailable-state rejection.
- Kotlin sources type-checked against cached Android and Filament APIs. This is
  not a full native app link or an APK build.

## Still requiring verification / remaining scope

- Install an authorized build before claiming visual Hold, armor, animation,
  scrolling or performance acceptance. Existing installed code is not this patch.
- Visually check the corrected model scaling and ingredient-specific orientation, transitions and
  single/multi-item release in the installed preview.
- Compare native flag/proxy instances and all map marker behavior in gameplay.
  The latest device debugger connection was unavailable; config was already back
  to disabled, so it was not changed and the game was not restarted.
- The existing companion renders the Hyrule world map only. Local-map rendering,
  complete quest-objective visibility logic and DLC-specific marker metadata are
  not implemented by this world-map gating change. Unsupported information is
  not exposed as a substitute for tracing its actual unlock conditions.
