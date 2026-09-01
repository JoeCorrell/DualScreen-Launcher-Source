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

## Retired character preview

The actor-clone preview was removed after device testing showed that sharing
Skyrim's UI3D manager with a second live actor scene could destabilize camera,
equipment, and render-resource ownership. The Character page is now a
read-only status/objective/equipment view.

The configuration key and three bridge exports remain as no-op ABI shims so a
mixed-version install fails safely or stays dormant. No character surface,
point-cloud payload, scene traversal, clone update, camera control, GPU
readback, or panel repaint loop is scheduled. The bridge version was bumped
when the obsolete 4,096-point snapshot payload was removed.

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
