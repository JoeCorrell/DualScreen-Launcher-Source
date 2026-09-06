# Odyssey wardrobe / portrait validation

## 2026-09-05: Android result-window crash

The installed 1.6.16.6 Android crash buffer records a main-thread
`IllegalArgumentException` at 03:47:22 in `ToastPresenter.addToastView`:
the bottom display's window context has type 2037 (presentation), while the
toast requests type 2005. `SmoCompanionView.finishOutfit` was creating a toast
with that presentation context. Both successful and rejected native requests
could therefore crash while reporting their result.

The fix draws transient action feedback into the existing companion Canvas.
It creates no extra Android window, replaces the previous message on a new
result, and cancels the delayed clear callback on detach. Source regression
coverage rejects reintroducing Toast here. This does not alter the native
wardrobe transaction or bypass its validation.

The retained crash log does **not** prove whether the underlying outfit change
succeeded. Subsequent Eden logs were from a restarted process. Validation must
capture a fresh request, its mailbox phases, completion result, top-screen
appearance and bottom-screen appearance. Do not equate a changed SaveString
with a rebuilt player model.

### Native transaction under test

The exact-base-build helper runs on StageScene's gameplay path. It validates
the same scene/save/player identity, native Wait state, demo eligibility,
ownership and pose before calling the native wear setter and same-stage reload.
The one-shot start lookup supplies a native PlayerStartInfo before initPlayer
constructs Mario. Completion verifies the new player's appearance, pose and
one spawn. There is no Odyssey location restriction or per-frame repositioning.

Offline contracts pass, but this is not a substitute for testing the transaction
in the running game. Keep the emulator debugger disabled for these tests.

### Portrait layout

1.6.16.7 removes ODYSSEY AIR MAIL and expands the portrait from the former
320 x 413 display rectangle to 320 x 500, starting at y=48. The render target
uses the same aspect ratio; there is no nonuniform stretch. Native Wait frame
zero and eye visibility remain unchanged, with redraw only on appearance changes.
Check wide hats/hands for clipping on the actual display.

The private packaged preview asset set currently contains the default and black
tuxedo bodies and three hats, not the entire wardrobe. Logs explicitly report
unavailable preview assets for other combinations (e.g. MarioExplorer/MarioGunman).
That limitation is separate from the Android result-window crash.

### Build checkpoint (not device acceptance)

Release APK 1.6.16.7 built and APK Signature Scheme v2 verified. SHA-256:
`dd16f0140f0ce79aa648b7455a8aa12a5de81fa5d2849fe49824e13405add99d`.
Packaging checks verified the exact existing native runtime, all nine private
portrait parts, and 1,194 unchanged published assets for other mods. The launcher
unit suite, nine preview tests and the native wardrobe host contracts pass.
The source-level regression initially matched the explanatory Toast comment;
it now rejects actual imports/calls instead. APK code was unchanged by that test fix.

Not installed yet: the device is still in EmulationActivity, awaiting the user's
save/safe-install confirmation. Runtime outfit completion and enlarged portrait
framing remain to be checked. Debugger setting was verified false on the device.

### Artwork build and installation

The user subsequently requested adding their supplied Odyssey cover and installing.
`art_switch_mario_odyssey.png` is an unchanged copy of that image (source SHA-256
`db5ff648bbe9664baffc59898d9f451fb70bab88c8ae105a0df22cbf47862f6b`). The existing
launcher artwork resolver now routes Odyssey to this drawable. A regression test
checks the asset hash and routing.

The rebuilt 1.6.16.7 APK SHA-256 is
`c669f7f4d9252aed423f6f1619455b7ade5480c9318cf1ad900179ee6ecc50cc`.
All 148 launcher tests pass; signature and the same packaging/preservation checks
pass. ADB install -r succeeded on 100.83.63.70:46665; package manager confirms
1.6.16.7. No saves/caches were cleared and the debugger remains false. Launcher
was opened after installation. Native outfit runtime acceptance is still pending.

## 1.6.16.8: emitted-code alignment and collected Moon names

Device log `outfit-invalid-pose-7.log` records five wardrobe requests rejected
with native result 5 in CityWorldHomeStage. All passed the native Wait, ownership
and player-identity gates and captured a finite in-range position. No mutation
or reload occurred (`spawns=0`).

The root cause is **not** a quaternion getter ABI error or the player moving:
the compiled helper was linked at page offset zero but copied 128 bytes after
the bridge page. Its ADRP + LDR floating-point constant loads address the start
of the page, so their targets were 128 bytes before the actual constants.
Resolving the emitted instructions gives:

| Constant | Intended | Value read at the old placement |
| --- | ---: | ---: |
| Position limit | 10000000 | 6.3473695e30 |
| Quaternion upper norm-squared | 1.01 | -2.6577863e-32 |
| Quaternion lower norm-squared | 0.99 | -1.6683706e26 |

Even a unit quaternion fails that negative upper bound. The same alignment
error also affected post-reload orientation verification. The fix leaves both
trampolines in their original locations and puts the compiled helper on the
next 4 KiB page of the existing 16 KiB reservation. Both calls and the code copy
use the generated CodeOffset contract; the loader rejects unaligned placement.
No validation threshold or native gameplay function was changed.

`test_smo_outfit_relocation.py` decodes the actual generated ARM64 ADRP/LDR
instructions, reproduces the old invalid bound and checks every float literal
load at multiple aligned runtime bases. Three tests pass. The C++ guest contract
tests alone did not detect this because they execute host-compiled code.
Completion logging now includes all quaternion components and their norm squared.

For Moon names, the device screenshot shows 81/81 collected Metro Moons with
question marks. The native `ShineListLayout::updateWorldInfo` branch at
0x2041f4 calls its collected-state helper (0x206d78 -> 0x52bce8 isGotShine),
then jumps directly to name resolution at 0x204248 if collected. Only otherwise
does it check isOpenShineName at 0x204240. The companion now mirrors this OR
predicate for its HintInfo-backed rows: collected OR name-state == 1. It does
not expose unknown uncollected names or bypass Moon Rock scenario filtering.
The state-reader regression tests check collected-without-name-unlock and
subsequent removal of both visibility conditions, including cached-name hiding.

Build 1.6.16.8 passed all 148 launcher tests, three compiled-code relocation tests,
the native state-reader tests and guest wardrobe contracts. APK signature and
1,194 preserved-asset checks passed. APK SHA-256:
`426bbc4d96a57e6476d4af075f54ba2974136a9991643643d254b48ea7f2a411`.
ADB install -r succeeded; package manager confirms 1.6.16.8 and debugger false.
Gameplay validation passed: `preview-missing-8.log` contains successful native
requests for body and hat changes (phase 6 -> 7 -> 4, result 0, exactly one spawn,
identical before/after coordinates and unit rotation). The user confirmed outfit
equipping works. `moon-names-8.png` shows the real collected Metro Moon names.

## Complete wardrobe preview coverage (in progress)

The remaining empty portrait is a separate asset failure, not an equipment
failure. The same log shows `MarioPrimitiveMan/MarioTailCoat` native success,
followed by `Preview assets unavailable ... No value for MarioPrimitiveMan`.
The old preview index shipped only two bodies and three hats.

`plan_smo_preview.py` now reads the actual base ItemCloth/ItemCap BYML export:
42 bodies and 41 hats. Its transitive input set includes native costume-specific
hands, face, eyes, clothing accessories and body hair, plus cross-costume head
variants. Invisible Cap follows the native constructor's Mario resource
substitution rather than requesting nonexistent MarioInvisibleHead.bfres.

The version-2 index resolves all 1,722 combinations. `smo_preview_catalog.py`
uses native InitSubActor and InitPartsFixInfo data, including LocalScale only
when UseLocalScale is true, head-specific overrides and Mario64/Metal/Other
resource suffixes traced in 0x444028 and 0x446dcc. Rigid parts without AnimArc
retain their native local pose and inherit the body transform; they are not
required to invent a Wait animation. GPU models are still loaded only for the
current appearance, and the neutral portrait still renders once per change.

The build refuses incomplete preview catalogs/files. `audit_smo_preview.py`
checks the catalog against the separate native ItemList export, all recipe
joints against the actual GLBs, finite transforms, self-contained textures,
and native animated/static pose declarations. Runtime wardrobe guest code,
save mutation, Moon visibility, map logic and other game mods are unchanged.

Conversion is complete: 153 models, 117,938,344 bytes including the version-2
index. The independent catalog/attachment audit passes all 1,722 combinations;
Khronos validation passes all 153 final GLBs with zero errors. Sixteen preview
regressions and the three unchanged emitted-code alignment regressions pass.
On-device validation of the expanded pack is still pending installation.

Release APK 1.6.16.9 built successfully in 5m44s with one Gradle worker and a
1536 MiB heap. All 148 launcher tests pass (zero failures/errors/skips). APK v2
signature validation passes; all 153 required preview models are packaged and
all 1,194 checked published companion assets remain byte-identical. The native
runtime is the existing working alignment-fixed binary, not rebuilt for this
preview-only change. APK SHA-256:
`ba4b9b8276d84d9cc6d86ff9bf55c7f942faba3c00624dfba5a382b93cfc8db3`.
The user subsequently requested installation. ADB install -r succeeded on
100.83.63.70:46665; package manager confirms version 1.6.16.9 and update time
2026-09-05 16:49:44. Launcher was opened, no saves/caches were cleared, and
use_gdbstub remains false. Do not mark the expanded pack device-verified until
previously missing outfits are actually viewed on the new APK.
