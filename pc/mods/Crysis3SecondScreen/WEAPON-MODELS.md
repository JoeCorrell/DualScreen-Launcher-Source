# Actual textured weapon previews — work in progress

User explicitly rejected HUD silhouettes on 2026-09-07. Weapon panels must
use large previews from actual game geometry and textures. The silhouette
draw was removed from source after APK63 was built. APK63 is NOT installed
and must not be installed as the requested model implementation. APK62
remains the installed version. Module glyphs are a separate requirement.

## Asset evidence

- Hammer: `.research/crysis3/weapon-models-hammer-v1`, 84 extracted entries.
- SCAR and bow: `.research/crysis3/weapon-models-scar-bow-v1`, 677 entries.
- Each folder has `_remote-evidence.json` with CRC/size checks and extracted
  SHA256 values. The full remote archive was not hashed.
- Item XML and CDF resolve Hammer FP to `hammer_fp.chr` with
  `hammer_rework_fp.mtl`; SCAR FP to `scar_v2_fp.chr` with `scar_v2.mtl`.
- SCAR CDF references separate magazine geometry; do not omit that dependency
  or claim attachment synchronization before runtime attachment state is wired.

## Initial offline conversion experiment (superseded below)

Cryengine-Converter source cloned from
https://github.com/Markemp/Cryengine-Converter at
`7b951dbb254ab99f560e8ffbf64a771c70ccef74` (GPL-2.0). Used as an offline
research tool, not linked or packaged into the launcher. Source build succeeded
with warnings. Its GLB split-texture option did not reconstruct this game's
`.dds.0` inputs; first conversion logged decode errors and is not valid artwork.

`tools/combine_weapon_dds.py` accepts only exact, bounded legacy 2D DXT mip
chains and writes to a new output directory. It preserves source inputs and
records generated hashes. Eight Hammer textures passed; two unsupported
encodings were rejected. Four regression tests cover order, missing and
truncated chunks, and unknown encoding rejection.

Hammer conversion v2 includes reconstructed diffuse/specular data but still
logs a normal-map decode error and missing default-grey texture. It has NOT
been visually validated and is NOT a finished preview. No model renderer or
model assets have been installed. Next: decode the remaining verified texture
formats, inspect mesh/UV/material output, implement bounded large model previews,
and visually/performance-test before packaging.

## Large model UI preview milestone

The latest offline UI review is
`.work/crysis3/dev/model-ui-preview-v4/2.png` (1240x1080). It uses the actual
SCAR and Hammer first-person meshes with their game diffuse, BC5 normal,
and specular textures. It is a synthetic layout fixture, not a live screenshot.
No new APK or model payload has been installed. Model loading is currently
enabled only in `Preview.cpp`, not the native panel.

- `combine_weapon_dds.py` now supports verified ATI2/BC5 and non-mipmapped DDS;
  all ten Hammer mip chains pass. Common default-grey and rubber textures also
  reconstruct. Cubemaps are deliberately rejected by the 2D decoder.
- The converter loses some Crysis specular/decal semantics. The mesh exporter
  therefore reads texture slots and `%DECAL` directly from the extracted MTL,
  and uses decoded source DDS textures instead of substituting generic textures.
- `export_weapon_mesh.py` exports rest-pose geometry, normals, UVs and bounded
  512-pixel texture copies; source GLB/material and output hashes are recorded.
  Model v4 sidecars also record every source DDS hash. Re-export produced
  identical mesh bytes to v3 (SCAR `90aa99e0c8f607f1783535b34267d9a152fd5ec765fecf5279e30f4c149d722c`,
  Hammer `4ca9709082e06baf2c37acdca162aa5889e037ef3e8bd2f3248a917ab3d31c99`).
  Hammer inverse-bind/world matrices agree within 1.3e-7 of identity.
- SCAR uses `mag_default_1` from its CDF for the offline default-pose assembly.
  This is NOT evidence of the player's currently installed magazine. Attachment
  transforms and variants still need in-game comparison before live use.
- `WeaponModel.h` renders a bounded local mesh once and retains only resulting
  pixels in the preview artwork catalog. Studio lighting is approximate, not
  CryEngine shader parity. It does not execute inside native telemetry hooks.
- The weapons layout reserves a large aspect-preserving model region in each
  panel. Attachments paginate three at a time to avoid covering the geometry;
  touch-region tests were updated to match.

Verification: `model-ui-safety-build.log` completed successfully with the new
mesh loader/render/cache rejection tests and existing UI/native-reader tests.
`model-python-tests.log` reports 38 tests, OK. No device performance or live
attachment validation has been performed for this preview.

Performance finding: one 900x360 CPU render measured about 421 ms for SCAR and
525 ms for Hammer on the host PC. This is NOT suitable for per-frame rendering
or a game callback, and FEX cost is unknown. Do not move the current synchronous
preview loader into the live panel unchanged. A background/cancellable render
cache or offline-baked views must be evaluated before deployment; interactive
rotation would require additional performance work. Gameplay is untouched.

Before live deployment: add remaining carried-weapon models, verify model/CDF
poses and skin variants against gameplay, improve texture fidelity where needed,
audit source texture hashes, measure initialization cost on FEX, then integrate
the fixed-key model loader on the panel thread. Never load a path supplied by
runtime entity names directly. Renderer failures must leave gameplay untouched.

## Remaining live systems

Five-page navigation (Inventory removed), owned-weapon utility filtering and
stable category ordering, and sixteen genuine module icons exist in source.
Real map publication, statistics publication, and native purchase/equipment/
suit actions are still incomplete. Do not describe them as finished.
# Current coverage checkpoint (2026-09-08)

This checkpoint supersedes deployment statements in the historical notes below.
Combined APK 68 includes cached actual-mesh views for SCAR and Hammer. Jackal
has a newly converted textured default-pose view, visually reviewed offline,
but has not yet been staged or installed. These are fixed renders of real game
geometry, not live attachment/skin-aware 3D instances.

The complete owner-supplied ObjectsWeap.pak is now extracted locally under
`.research/crysis3/weapon-models-all-v1`: 6,093 entries, 1,046,728,372 unpacked
bytes, every entry CRC validated. Archive SHA-256:
`1c8845557db4be5cdc298e2c5b612843fa34b983947d38cd080c6ca50f6e6ca0`.
The archive signature was not validated.

`tools/audit_weapon_models.py` inventories base/patch declarations and explicit
material overrides. Its report `.work/crysis3/weapon-model-coverage-v1.json`
contains 105 definitions, including non-player/debug/variant entries; this is
not a claim that 105 playable weapons are supported. Inheritance is explicitly
unresolved. Variants such as CellHammer declare their own material and must not
be silently mapped to a standard Hammer texture. The two audit regression tests
pass. Remaining work includes conversion, variant resolution, visual review,
bounded runtime caching, and packaging the other verified model views.

Live follow-up C3R001100 completed fresh owned-loadout reads 1–77, but request
78 (new Hammer attachment records after the user's removal) timed out. Do not
reuse that request ID or claim the silencer removal was captured. Previous
C3R235900 ammo samples spanned a transition and are not a coherent paired-ammo
snapshot. Holstered ammo and native attachment actions remain unfinished.
