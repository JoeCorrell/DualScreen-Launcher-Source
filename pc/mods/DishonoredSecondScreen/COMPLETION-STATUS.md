# Completion audit — 2026-09-07

Graphics: the user confirms Dishonored is fixed with the selected DXVK 2.6.2
and VKD3D 2.9. Preserve those per-game versions and all other container settings.

Current integration work is not a release acceptance claim.

Latest UI update: `2026.09.07-ui-dev9`, following the seven-page ADB
review. Flat frames replace the unwanted shadow/glow; inventory replaces the
loadout strip with tap-to-equip and real Equipped labels; mission/location
headers are removed; statistic fractions share one line; paused snapshots
remain browsable with fresh-state native command validation. All UI/native
regressions pass. Installed and all 120 files verified after the user confirmed
the game was saved and requested installation. Launcher APK retained; no game
launched. Device acceptance of this UI version remains pending.
Screenshots, detailed findings and renders: `.work/dishonored/ui-review/`.

| Area | Evidence and remaining work |
| --- | --- |
| Two displays | Live independent bottom panel observed on the launcher. Top gameplay remains user-controlled. |
| Touch/layout | Seven-page offline renders, gestures, stale input, footer bounds, details and clipping tests pass. Current Journal screenshot confirms bottom-screen fit. |
| Sampling | dev7 installed and all 120 payload files verified. Loaded-save generation 3 samples now finish all nine phases in approximately 1,085–1,238 reads, below the unchanged 8,192-call limit. |
| Name lookup | Replacement bounded heap cache passes a 1,200-name recovery test in eight bounded passes; warm pass uses zero name reads. Device logs confirm map lookup no longer starves Journal in the observed save. No higher polling/read limit. |
| Responsiveness | User confirms the delay is resolved after dev7. Unchanged/off-page snapshots skip repaint, static background is cached, release precedes repaint, and transitions advance at presentation time. Original pixel/layout/gesture regressions pass. |
| Powers/equipment/charms | Real readers and semantic actions implemented; full acquisition/selection/equip/unequip acceptance remains outstanding. |
| Objectives | Base and Custom task status getters match the executable and exhaust the task classes declared in the inspected base package (239 evidence checks). Failed chapter descriptions/target reads now propagate incomplete status independently of valid chapter identity. Unknown overrides and unverified DLC classes remain unsupported. |
| Journal | dev7 publishes four real entries in the observed save. Native document request 1 completed with Applied at sample 248818309. dev8 source adds the separately stored native key ring to Journal Keys without another game read. Audiograph playback and broader acquisition acceptance remain outstanding. |
| Maps | Acquired drawings/legends and paused touch browsing implemented. Current source calibrates all 13 catalogued sheets using native coordinates and explicitly approximate cartography, plus validated live HUD objective markers. Some interior regions remain explicitly unmapped; device acceptance pending. |
| Statistics | Current mission and base-game saved result readers implemented. dev8 expands the bounded snapshot/filter capacity to 307: 19 live records plus nine saved missions with up to 16 values and 16 narrative rows each. Regression reaches all 288 saved rows. Live completeness and DLC/campaign scope remain outstanding. |
| Delivery | Separate development payload installed through verified private cache. No production publication until runtime acceptance. |

Evidence: `.work/dishonored/completion/` holds logs, test renders, build receipts,
the 239 native/schema verification checks and the live bottom-panel image.
Older README sections describe historical versions and are not current claims.

The next development payload also distinguishes a successfully read empty
Journal/Bone Charms collection from a failed read. Availability is conditional
on complete document/key-ring or charm/slot/eligibility reads; unsupported map
projection and other incomplete domains remain explicitly incomplete.

## Integration dev10

`2026.09.07-integration-dev10` retains the dev9 UI and adds the objective
coverage corrections above. AbstractInventory now returns its actual read
completion result to Equipment, so a partial abstract-item traversal cannot
be mistaken for a complete read. Objective availability changes and saved
statistic maxima/known flags participate in revision tracking. No graphics
settings or native action entry points changed.

The payload builds with warnings treated as errors. All seven offline test
suites pass, including 26 touch protocol cases, seven-page renders, full
statistics scrolling, paused browsing, confirmed selection and lifecycle
invalidation. These tests do not substitute for device gameplay acceptance.
Build/tests/package/installer evidence: `.work/dishonored/completion/coverage-*`.
Installed after the user confirmed the session was closed: all 120 files
verified, launcher APK retained, no game launched or input sent. Receipt:
`.work/dishonored/completion/coverage-installer/installation.json`.
DLL SHA-256: `6aab01341b29654ea805476ef58fff12106b8a68996e39d28c6ee8c980ac5757`.

Live map projection remains unfinished. Existing original drawings are
schematic; the Kingsparrow affine fit is research only and is not published
as player coordinates. Full native action, acquisition, save/load and mission
transition acceptance also remains outstanding. This is a development payload,
not a completed release.

Additional map research decoded 4,681 Overseer geometry objects without
property errors and validated 4,498 serialized component matrices. The two
interior board locations are known, but component origins do not establish
mesh boundaries or a verified world-to-artwork transform. Research script,
source hash, decoded matrices and inspection plot are in
`.work/dishonored/completion/overseer-*`; no calibration was enabled at runtime.

## Integration dev11 (staged, not installed)

Acquired-map selection is now a local UI operation. Each natively acquired
sheet carries its verified artwork and copied native legend in the bounded
snapshot (bridge version `0x00010004`). Browsing no longer queues SelectMap
on the game thread, and works while paused. Another sheet cannot inherit
the nearest map's player marker or objectives. Confirmed acquisition changes
and new generations clear the preference; incomplete reads preserve it but
hide unavailable artwork rather than substituting another area's map.

All seven regression suites and 239 native/schema checks pass. The new map
regression checks paused selection/legends, zero game commands, unacquired-key
rejection, cross-map marker isolation, partial-read recovery and generation
invalidation. Final DLL SHA-256:
`6dfe5cb75c20d09b7cac83d3b789b42d6f46a579773d7f5e552863c3244eb91a`.
Package and signed local helper: `.work/dishonored/completion/map-browse-*`.
Assassin's Creed II was active when staging finished; no installation attempted.

Map research now verifies ten Overseer floor mesh exports against native
serialized vertex bytes and the decoded instance matrices. The two-board
similarity candidate does not align the entire drawing: floors are arranged
separately and distances are distorted. Inspection:
`.work/dishonored/completion/overseer-floor-check.png` and matching JSON/script.
The user has been asked whether explicitly approximate calibrated placement
is acceptable; they asked for an explanation and have not accepted it yet.
No approximate live marker was enabled in dev11.

## Integration dev12 (staged, not installed)

The user accepted approximate calibrated placement after the explanation.
Kingsparrow native maps 5, 11 and 12 now use the three-board affine calibration,
independently reviewed against original terrain/dock/wall/lighthouse geometry.
Real pawn Location and reflected Rotation provide position and facing; the
UI explicitly labels the result “Approximate position.” Each acquired sheet
carries its own pose, and unknown calibration never produces a marker.
The pawn rotation contract is verified against native GetAxes/rotation-matrix
code. No collectible or secret marker was added.

Eight test suites pass, including anchor projection, invalid input, transformed
heading and per-sheet movement repaint without collection-revision changes.
Native/schema evidence now has 245 checks. DLL SHA-256:
`64660ddc7423056f80b9aca9d23816dcd72e49555c5c539c262de362c31466b5`.
Package/helper: `.work/dishonored/completion/calibration-*`.
ACII remained active; no replacement or gameplay control was attempted.

See [MAP-CALIBRATION.md](MAP-CALIBRATION.md) for exact scope and evidence.
Ten other drawings remain uncalibrated. Runtime acceptance of this marker,
native action coverage and the broader full-mod release remains outstanding.

## Integration dev13 (installed)

Adds separate Overseer, Golden Cat and Dunwall Tower floor regions and live HUD objective
map markers to dev12. Snapshot bridge version is 0x00010006. Objective targets
carry validated XYZ; projection uses acquired original artwork and floor
membership, with 750 ms expiry/reappearance repaint handling. No extra game
thread scan, graphics setting change or gameplay input is introduced.

Golden Cat geometry checks cover 41 mesh instances; the runtime table retains
434 floor triangles. Overseer retains 731 and Tower 2,585. Exact scope and unmapped regions
are documented in MAP-CALIBRATION.md. All nine regression families pass,
including the new floor and objective-map cases. Runtime acceptance and the
remaining five map calibrations are still outstanding. ACII was active on
the read-only ADB check; its session was left untouched.

The user confirmed the active session was closed. The final Tower/cache build
was installed and verified through the signed helper. Launcher APK retained;
no game launched or gameplay input sent. DLL SHA-256:
`b6b61f11b6d0ccf7b4905168dd161284a52d3304688e73ee920e2bd1beaa1841`.
Receipt: `.work/dishonored/completion/map-objectives-installer/installation.json`.
All nine regression families and 245 native/schema checks pass. Runtime
acceptance remains outstanding; the full mod is not yet complete.

## After dev13 installation: local diagnostics and remaining-map research

Source adds one `map-projection` event on the existing five-second diagnostic
interval. Event id is the current map key, revision is snapshot sequence;
a flags position=1/facing=2/approximate=4, b/c are normalized XY times 100000,
d is acquired-sheet count. It adds no reads or game-thread file I/O. This
addition is not in the installed dev13 binary.

Distillery District research verified 16 original ground/road mesh instances,
2,913 serialized component matrices and ten native PlayerStart entries. A
board/two-transition-door affine candidate aligns the central boulevard but
misplaces the upper street and stretches the bridge. It remains research
only and must not be enabled as a whole-area calibration. Evidence:
`completion/streets-layout-candidate.{py,json,png}` and
`streets-{geometry-properties,component-transforms,floor-check,playerstarts}.json`.
The runtime still supports eight acquired drawings; five remain uncalibrated.

The post-install telemetry source builds with warnings treated as errors.
Local DLL SHA-256: `7dc15ec93471f2adc7632c79a184eef5762776a59aca2e214546dd1e4b4a31ab`.
Build: `.work/dishonored/completion/map-telemetry-build/`; not installed.

## Integration dev14 (installed)

Adds the reviewed Distillery District piecewise projection with a verified
796-triangle footprint, matching geometry in both mission versions. The
bridge correction is continuous; facing follows the local projection
derivative. Board/transition anchors, unmapped dock rejection, invalid height
and three heading comparisons pass. The earlier single-affine candidate was
not enabled. Runtime map support is now nine drawings; four remain uncalibrated.

Includes the five-second map-projection telemetry and a startup source SHA-256.
The build recipe fingerprints every payload source/header/geometry table,
shared Canvas source/header and itself, emits a manifest, and verifies that
the compiled DLL contains the fingerprint. This avoids treating older generic
startup logs as evidence of a new build. No graphics/container setting changes.

All nine regression families pass. The refreshed device log was identical to
the older 662,464-byte log; it provides no new dev13 gameplay acceptance.

Installed dev14 and verified all 120 payload files. Backup:
`/sdcard/Games/Dishonored/.dual-screen-backups/20260907-032333`.
DLL SHA-256: `406c4f96805787a62ab1ea662354ab8aaa3b9207784a64920ab769a6e8200904`.
Embedded source SHA-256: `9d53134a639521403f68c2b28ab5b4d4f08703fba6c4d2c1296a1da87b4d2e4f`.
Package/installer/build/test evidence: `.work/dishonored/completion/streets-*`.
Launcher APK and graphics settings retained; no game launched or input sent.

Paused Journal audit: normal documents already copy the native +0x50 text and
owned child pages into each bounded snapshot through JournalPart. This supports
paused browsing without native getters. Audiograph transcripts remain explicit
native requests; do not prefetch their condition-dependent getters or invoke
them from the panel thread merely to force paused availability.

Further map6 research exported nine Overseer exterior ground/road meshes and
checked their native vertex triples. The board/statue/courtyard-front candidate
compresses the drawing excessively and does not establish full-area alignment.
`completion/overseer-exterior-candidate.{py,json,png}` remains research only.
The third correspondence is a provisional courtyard-front construction, not
an independently verified entrance marker; do not publish it as native data.
No map6 calibration was enabled. Current source and installed dev14 binary
remain aligned; later changes are documentation/research only.


### Final four calibration delivery ? dev15

Installed `2026.09.07-integration-dev15`; all 120 files verified. Only
`dinput8.dll` changed in the game folder. Launcher APK and container settings
retained; no game launched and no device input sent. DLL SHA-256:
`75c8969de01ecfe559bc8f62ee8e4c0ddc4ea7959a846123572004a280152db3`.
Source fingerprint: `21a7085021a3c62f05f381dd518b2ccb90f30bbfcf98538a02011721cb83c8e0`.

All four remaining exterior calibrations are implemented. Nine test families
pass, with additional four-map 1240x1080 renders visually reviewed. Evidence and
installation receipt: `.work/dishonored/completion/final-four-*`.
Runtime acquisition/position acceptance remains pending, including Boyle's
native acquired-board availability. This closes the four calibration
implementations, not the outstanding whole-mod gameplay acceptance.
