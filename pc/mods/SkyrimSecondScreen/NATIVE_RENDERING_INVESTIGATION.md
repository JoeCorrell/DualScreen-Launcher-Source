# Native Skyrim rendering investigation (runtime 1.5.97.0)

This document records the verified safety boundary for optional native
second-screen rendering. Addresses are RVAs relative to the SkyrimSE.exe
image base and are never used on another executable version.

## Inputs and version gate

- Examined executable: SkyrimSE.exe 1.5.97.0, SHA-256
  `5666E1BDDD01BCAB31ECF11691EF1A3F22E1541AF79F2BC0E55318533CFE5D12`.
- Address source: the canonical Address Library 1.5.97 database plus focused
  disassembly of that executable.
- Structure/function source: CommonLibSSE-NG commit `b93280e`.

## Current character renderer: orientation fix (2026-09-07)

`CustomPlayerPreview.cpp` is the active private NIF/DDS renderer. The older
actor-clone path remains disabled. Two independent errors caused the portrait
to look backwards or inside-out:

1. The private view looks from -Y toward +Y, with Z up. The supplied Argonian
   bind-pose face points +Y. `Inspect-Character` resolves its eyes to Y
   7.356..8.171 while the head spans -7.052..13.841. The old -7 degree default
   yaw viewed the back. The new 173 degree yaw turns the assembled skeleton
   toward the camera, retaining the slight portrait angle and rotating normals
   and tangents consistently. No per-part flip is applied.
2. The view's Y/Z swap has determinant -1. With the D3D viewport, exterior
   NIF triangles are counterclockwise on screen, but the rasterizer selected
   clockwise fronts. It discarded outer faces and exposed inner/rear surfaces,
   including mouth/eye geometry. `FrontCounterClockwise=TRUE` corrects this
   while retaining back-face culling. Disabling culling or reversing only the
   head would not fix the coordinate contract.

Evidence: the older checkout's `.work/argonian-head-assets`, `nifly` source,
CommonLib's `NiSkinInstance`, `NiSkinData` and `NiTransform` definitions, and
existing 1.5.97 camera disassembly at RVA `0x00C66710`. Bone-world times
skin-to-bone composition is already correct; a nonidentity rotation/translation
inverse-bind regression confirms it. No game offsets or hooks were changed.

The WARP regression renders a red exterior and green rear triangle through
the production pipeline. Before the fix it sees green; afterward it sees red,
and a 180 degree turn correctly sees green. It also checks that the default
camera sees the +Y front without mirroring. The supplied Argonian eyes have
80 outward and zero inward normal/winding matches. Optional head renders use
diagnostic grey materials and geometric normals where FaceGen omits normals;
they are not a textured device test.

Review also fixed stale scene publication across world resets, out-of-order
readbacks after ring wrap, incomplete GPU initialization retries, a missing
model-space normal-map fallback, and unchecked DDS dimensions/mip counts/pixel
layouts. Renderer regressions cover these paths except forced GPU allocation
failure. The existing UI harness now verifies the intentional disabled-map
navigation contract rather than expecting an unreachable page.

Run `tests/Run-Tests.ps1 -Build` with the dependency root described in README.
Local reproduction and build receipts are in `.work/skyrim-render-fix` at the
workspace root. Gameplay/device acceptance remains a separate check.

## Retired actor-clone preview (historical)

The actor-clone preview was removed after device testing showed that sharing
Skyrim's UI3D manager with a second live actor scene could destabilize camera,
equipment, and render-resource ownership. The Character page subsequently
gained the private asset renderer described above.

The obsolete 4,096-point snapshot payload was removed in an earlier bridge
version. The configuration key and character exports now serve the private
renderer; they no longer act as no-op shims. No live actor clone is scheduled.

The rejected approaches remain explicit: never reparent the live player root,
create a second gameplay Actor, open a hidden menu, capture the main
framebuffer, or drive Skyrim's player camera from the lower UI.

## World-map safety boundary and implementation

Focused Skyrim SE 1.5.97 tracing established that constructing or hiding a
duplicate `MapMenu` is not safe: the menu pauses gameplay, takes the map input
context, and changes shared camera, weather, visibility, LOD, image-space, and
scene state. There is no isolated world-map render target. The `LOCAL_MAP`
targets belong only to the embedded local-map renderer.

The rejected experiment copied the live WorldRoot pointer into a UI3D scheme
slot and called `UI3DSceneManager::Render`. Device logs proved the exact
failure: UI3D's culling process accepted `WorldRoot Node`, rejected
`WorldRoot CameraNode` for its zero UI bound, entered `shadow scene node`, and
the process terminated before Render returned. WorldRoot is a gameplay render
graph and cannot satisfy the inventory renderer's scene contract. That path is
permanently retired.

The replacement follows Skyrim's native world-map rendering contract without
constructing a menu:

- Address Library ID 35560 resolves to the render-loop function at RVA
  `0x005B1860`; its call at `0x005B2091` targets the exact
  `Main_RenderWorld(bool)` entry at `0x012E3E70`.
- A five-byte callsite hook is installed once during SKSE plugin load. On an
  explicit lower Map-page request it inserts one native map pass before the
  original gameplay pass. With no request, it immediately calls the original
  function and does no map work.
- The pass constructs Skyrim's 0x90-byte `MapCamera` using the game's null
  callback vtable, resolves the current world space and player map position,
  and uses the exact camera-root/update routines called by `MapMenu`.
- World-map activation (`0x008E8680`) and teardown (`0x008E87B0`, false) are
  strictly paired. The live camera and mutable root bytes are saved before
  activation and restored after teardown; player visibility is restored using
  `MapCamera`'s own close path.
- Render target zero and its dimensions are redirected only for the map pass.
  The target metadata is restored before the normal gameplay pass runs in the
  same frame, so the top screen is never replaced by the map.
- The private 960x540 color surface is copied to staging and mapped only after
  a GPU event query completes in a later Present. A successful frame is cached
  by the lower UI; it is never replaced by the old LOD16 software terrain.

The native MapMenu itself is never constructed, pushed, opened, hidden, or
advanced. The old WorldRoot/UI3D producer is unreachable.
