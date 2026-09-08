# Map calibration

The user accepted approximate cartographic placement on 2026-09-07. Position
is sampled from the real pawn; approximation concerns the original schematic
drawing, not simulated player data. The interface displays **Approximate
position** whenever this projection is active.

## Kingsparrow Island: native sheets 5, 11 and 12

These three original 800×500 drawings share geometry and differ in their
printed board marker. Native acquired-board state and the exact original
texture path must pass validation before any position is published.

| Serialized board world X | World Y | Printed marker X | Printed marker Y |
| --- | --- | --- | --- |
| -4459.7685546875 | 5685.30078125 | 689.5 | 297.5 |
| -4571.708984375 | -5402.01904296875 | 431.5 | 295.5 |
| 377 | -3475 | 477.5 | 195.5 |

An affine cartographic calibration fits those three correspondences. Separate
checks matched exported terrain, walls and docks against 2,467 native vertex
triples and the lighthouse vista against 360 native vertices before applying
the serialized component transforms. The drawing remains schematic; these
checks establish area correspondence, not centimeter accuracy.

Evidence in `.work/dishonored/`: `isle-calibration-candidate.json`,
`isle-landmark-verification.json`, `isle-lighthouse-vista-verification.json`,
and the three `map-inspection/isle-*.png` review figures. Their research-only
wording predates the user's acceptance of approximate calibrated placement.

Native `GetAxes` 0040a810 calls 00402c30. That implementation uses Rotator's
second int32 as yaw, shifts by two, masks to 0x3fff, and adds 0x4000 for
cosine. The heading reader matches the native 65,536-unit turn and lookup
quantization. Projection transforms the direction into artwork pixel axes
before computing the displayed angle. Pawn `Rotation` is resolved through
the actual StructProperty schema, not a hard-coded pawn offset.

Nonfinite and out-of-artwork positions return unknown rather than a clamped
edge marker. Unknown facing still permits a position dot. Each acquired sheet
has its own pose, so selecting another drawing cannot inherit foreign markers.
No collectible, enemy, or secret locations are inferred from static geometry.

## Overseer interior: native sheets 7 and 8

The two printed board centers align with native board actors at (-291,
-28537,1553) and (-4261,-28545,1551). Separate affine regions handle the
lower floor and the upper corridor. Membership uses 731 triangles from ten
floor mesh exports, whose vertex triples were checked against the original
package bytes before applying 4,498 validated component transforms. The
upper corridor endpoint and lower floor extents supply the additional
schematic correspondences. Review: `completion/overseer-regions.png` and
matching JSON/script. Ambiguous vertical gaps and points outside these floor
surfaces return unknown.

## Golden Cat interior: native sheet 4

The drawing rearranges three floors into separate columns. Three affine
regions use the steam-room floor, atrium/bedroom correspondences and the
native board at (36389,8847,4550), matching printed center (397,356).
The middle-floor Silver Room and upper-floor Smoking Room occupy the same
world region at different elevations; they must never share a map column.

The package property decoder read 4,972 geometry objects without errors and
validated 4,747 serialized component matrices. All vertices of 41 exported
floor instances were checked against their original StaticMesh export bytes.
The runtime membership table retains 434 floor triangles; decorative atrium
geometry and exterior terrain are excluded. Membership requires a surface
within 240 world units below the observed position. Unknown heights,
unmapped stair landings, pool gaps and exterior positions produce no marker.
This deliberately leaves gaps rather than moving Corvo into an incorrect
floor column. Calibration is approximate, including room shape differences.

Evidence: `completion/brothel-geometry-properties.json`,
`brothel-component-transforms.json`, `brothel-floor-check.{py,json}`,
`brothel-regions.py` and `brothel-regions-candidate.{png,json}`. The candidate
figure was visually reviewed after reducing the table to the retained floor
surfaces. Native vertex verification precedes projection; the figure is a
cartographic alignment check, not another live world renderer.

## Dunwall Tower interior: native sheets 10 and 13

Three schematic regions separate the entrance/kitchen, main floor and upper
floor. The entrance board (-12256,6627,4280) maps to printed (150,410).
Room/corridor extents calibrate the other columns; both drawings share the
same floor layout. The original environment package yielded 5,840 decoded
geometry objects with no errors and 5,558 validated component matrices.
All exported vertices of 65 floor instances were checked against the original
serialized mesh exports. The runtime retains 2,585 floor triangles and limits
membership to 200 units above an included surface. Intermediate stair levels,
basement regions and out-of-chart geometry remain unprojected.

The floor overlay was visually reviewed against the original artwork, with
minor schematic distortions retained under the approximate label. Evidence:
`completion/tower-geometry-properties.json`, `tower-component-transforms.json`,
`tower-floor-check.{py,json}`, `tower-regions.py` and
`tower-regions-candidate.{png,json}`. Native map 13 is enabled only if its
actual acquired board and exact artwork path are observed at runtime.

## Distillery District: native sheet 9

The base affine calibration uses the native board (-1451.0654296875,-5031,
2728.1455078125), Overseer-gate PlayerStart (3343.483154296875,9669.6484375,
3533.255126953125) and Distillery-entry PlayerStart (-5624.34912109375,
-1008.516357421875,2449.0185546875). Their cartographic reference points are
(548,245), (178,375) and (347,235) pixels respectively. The latter is the
entrance at street level, not its roof-mounted legend symbol.

A continuous piecewise correction compresses the schematic bridge and
expands the rear street. Those corrections are cartographic choices checked
visually against the original drawing and native road outlines; they are
not hidden game-provided transforms. Heading uses the local derivative of
the same corrected projection. Tests check continuity at the bridge join
and compare displayed facing with projected movement in three regions.

Both Streets1 and Streets2 block packages were decoded. All 16 exported
floor meshes were independently matched to native vertex bytes in both
packages, and their component matrices agree within 0.00001. The retained
footprint has 796 triangles from 15 instances; the distant arrival docks
are outside this drawing and remain unmapped. Outdoor positions can be up
to 2,000 world units above a verified surface to include known roof heights;
positions below all included surfaces or outside the footprint remain unknown.
The original drawing is schematic and does not accurately depict interiors.

Evidence: `completion/streets*-geometry-properties.json`,
`streets*-component-transforms.json`, `streets*-floor-check.{py,json}`,
`streets-playerstarts.json`, `streets-regions.py` and
`streets-regions-candidate.png`. The earlier single-affine candidate remains
rejected; the reviewed corrected projection is the implementation.

## Live objective markers

Only current native HUD objective-target observations supply coordinates.
The existing ownership, actor registry, hidden-state and localized-label
validation runs before the snapshot includes XYZ. The UI projects those
pointer-free observations onto the selected acquired drawing, using the same
floor membership as Corvo. Optional and primary objectives use distinct
symbols; tapping a marker displays its real localized label. No world scan,
static target list or undiscovered collectible inference is added.

Targets expire after the existing 750 ms freshness interval, including while
paused. Expiry and renewed observations invalidate the visible image; merely
refreshing a timestamp does not trigger a repaint. Projected markers are cached for the same snapshot, selected map and valid
observation interval. Panning/zooming does not repeat floor geometry searches;
new snapshots invalidate copied text references. Local map browsing remains
usable while paused and never sends a gameplay command.

## Remaining drawings and validation

All 13 catalogued drawings now have a calibration. Supported interior sheets
still have explicitly unmapped regions as described above. Native acquisition
and loaded-level validation remain required; calibration does not unlock a map.

Offline tests cover all board correspondences, three Golden Cat floor
columns, Overseer floor membership, heading transformation, invalid inputs,
objective expiry/reappearance, acquisition checks and generation invalidation.
These tests do not replace device gameplay acceptance, which remains pending.


## Final four exterior drawings (dev15)

Sheets 1 (Boyle), 2 (Kaldwin's Bridge), 3 (Golden Cat exterior), and 6
(Overseer exterior) use independently decoded native mesh vertices and component
matrices. Every exported vertex was compared with original serialized bytes.
The retained, deduplicated footprints contain 5,302 / 12,885 / 260 / 1,606
triangles respectively, indexed into 246 spatial cells. Runtime lookup uses a
binary cell search, with no package parsing or world scan on the rendering thread.

The transforms are cartographic approximations fitted to original artwork, not
engine-provided coordinates. Boyle's canal ends and the Overseer's western
street have continuous piecewise corrections. Facing uses the same derivative
as position. Outside-chart, missing-height and unmapped footprint positions
remain unknown. Exterior height tolerance is 2,000 world units (3,000 on Bridge).

Bridge's moving central span uses an explicitly cartographic corridor between
native tower inner edges (X 17106..21216, Y -199..1442, Z 380..5800). This is
map eligibility, not a fabricated deck/collision state. Corvo and objective
coordinates still come solely from live game objects. The Overseer drawing is
restricted to X -11500..3200, Y -29500..-16000 because adjacent native terrain
extends beyond the drawing. Background vistas and inaccessible Boyle gardens
are excluded, as is the Golden Cat's distant third ground mesh.

Evidence and reproducible geometry generation:
`.work/dishonored/completion/final-four-audit.py`,
`final-four/*-geometry.json`, `final-four/project.py`,
`final-four/calibrations.json`, `final-four/generate-regions.py`, and the four
reviewed overlays. Native board anchors are Bridge (334,278), Golden Cat
exterior (572,227), and Overseer exterior (643,343) in 800x500 artwork pixels.
Boyle uses two native canal crossings and the courtyard as correspondences.
No acquired Boyle map-board actor was found in the inspected exterior packages;
this implementation deliberately does not synthesize ownership. A real acquired
board must still be observed before that drawing can appear.

Regression tests exercise over 17,000 geometry/heading samples, all three new
board anchors, central-span coverage, invalid input, footprint exclusions, and
1240x1080 renders for all four drawings. These are offline integration checks;
on-device positional accuracy and acquisition acceptance remain pending.
