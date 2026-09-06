# SSHD local map rendering investigation

Status: the native GPU map and complete map UI have rendered on ADB
`100.83.63.70:46665`. Gameplay updates, cold full-UI initialization and gestures
are being validated; see the implementation and acceptance notes below.
Offsets below are relative to the 1.0.1 main NSO text base, Build ID
`5441AF3C89F97E1D25A8CCF14C742BF82336D8A8`. Wii decompilation provides semantic names only;
HD offsets and rendering behavior come from the project's extracted executable.

## Observed failures

1. With the native map never opened, the mailbox reports `state=3 result=7`, with no texture
   metadata. This is `InvalidCaptureWidth`: the injected bridge rejects capture+0xA0 == 0
   before setting dStage's render request. JNI maps it to -5, which the view describes as an
   unreadable texture. That message does not identify the actual failed precondition.
2. Opening the native map makes the geometry preflight pass, but the mailbox remains Pending.
   The bridge checks completion at the beginning of the stage callback, while the native UI
   can request another render before that check observes the previous request being cleared.
3. Closing the native map permits Ready results. At 15:38 on September 4, the capture reported
   size 812 x 456, GX format 5, floor 0. The displayed result was a thin strip of colored pixels,
   not the Lanayru Desert map. Do not treat Ready, readable guest memory, or the UI's
   "synchronized" label as rendering validation.

Saved local evidence is under `.work`: `sshd-native-map-open-logcat.txt`,
`sshd-after-native-map-close-logcat.txt`, and `sshd-map-diagnostic-bottom.png`. The latter is
test evidence only, not an input to the map renderer. No screenshot or exported map is used
as an implementation resource.

## Geometry and controller lifetime

- `main+0x183C440` points to dLytMapGlobal. MapMain = global - 0x10D38;
  MapCapture = global - 0xF358 = MapMain + 0x19E0.
- MapCapture's picture is at +0x78; world-space capture extents at +0xA0/+0xA8;
  requested output dimensions at +0xAC/+0xB0; request/busy at +0xB4/+0xB5.
- Constructor zeroes the extents. Native initialization is `0xC448E0`, registered at
  `0xB270..0xB278` as RenderingWait state's initializer (`0x183B8F8+0x18`).
- `0xC46060..0xC46260` obtains stage/floor bounds, reads native map output dimensions through
  `main+0x2BFEA88`, fits the floor bounds to that aspect ratio, and writes MapMain+0x1A80/0x1A88.
  `0xC462B0` onward initializes scales, scroll and rotation. The larger initializer also
  handles area classification and map UI state; calling an arbitrary suffix is unsafe.
- `0xC66164` (MapMain execute) exits immediately while Invisible. Merely reading the closed
  pause map's marker array will not continuously update its full native marker state.
- Capture state execute at `0xC69C34` applies the native transform to dStage and requests
  rendering. Stage+0x560 is current floor; +0x561 is requested render floor; +0x562 is request.
  The floor table has stride 0x1C, indices floor+3, valid floors -3 through 4, valid byte +0x368.

## Actual HD texture ownership

- Stage renderer owns the CpuTexture wrapper at stage+0x550. Its dimensions are +8/+0xA,
  legacy format +0xD, legacy backing/key +0x18. These fields alone do not describe the GPU image.
- `0xEDCF4C` (legacy copy operation) calls `0x1044680` with that key. `0x1044680` resolves
  `0x10544B0` and passes the resulting NVN texture to `0x1043E10`, which records GPU rendering.
  The HD implementation is not a copy of GX-tiled map pixels into the legacy address.
- `0x10448E0` binds a target using the same cache. `0x10540C0` resolves a GXTexObj:
  key = word0 << 32 | word6; initial slot = (key >> 7) & 0x3FFF; linear probing checks
  tag `(key >> 7) | (1ULL << 63)` at main+0x17730F0 and exact key at main+0x17930F0.
  Index at main+0x17B30F0 selects main+0x17C30F8 + index*0x50. Index -1 is not ready.
- Entry+0 holds the NVN texture; +0x30 is ready, +0x38/+0x3C actual width/height. The renderer
  temporarily replaces entries and restores owned textures from +0x40, clearing +0x40/+0x48.
  Resource identity and lifecycle must be checked at the correct render phase.
- `0x1056DA0` allocates the real texture and its memory pool. NVN texture object +0x1C0 stores
  this wrapper's allocated backing pointer, +0x1C8 its size, and +0xC0 the memory pool.
  These are wrapper fields, not a portable NVN object ABI. Live correspondence remains to verify.
- Verified function dispatch: main+0x2C8EAC0 = nvnTextureGetWidth;
  +0x2C8EAC8 = nvnTextureGetHeight; +0x2C8EB08 = nvnTextureGetTextureAddress.
- The extracted SDK Build ID starts `B39ADD63421BE525`. Its name table at 0xABE798
  (string base 0xABAFD5, names without the `nvn` prefix) resolves those functions:
  SDK+0x476870 reads width at NVN+0x28, +0x476880 reads height at +0x2C,
  +0x4768B0 reads format at +0x38, +0x476920 reads GPU address at +0x80.
  Storage size is +0x78, memory pool +0x10 and pool offset +0x18.
- `0xE280B4` clears stage+0x562 after map commands and temporary-resource restoration.
  This is command-recording completion, not a GPU fence. A completion hook here must preserve
  the actual overwritten instruction and registers; the existing prologue bridge cannot simply
  be reused because it replays `add x29,sp,#0x60`.

## Second live check

Read-only GDB inspection of F300 on the same ADB device confirmed the SDK accessor instructions
and the different texture identities. Before opening the native map, extents were all zero;
the legacy wrapper reported 812 x 456, while its cache entry held a 1920 x 1080 NVN texture.
After opening, extents were approximately 92596.492 x 52000, scale fields 0.008769231 and
0.918859661, and the same legacy key resolved a different 1280 x 720 NVN texture (format 0x25).
The sampled legacy bytes at offsets 0, 0x100, 0x1000 and 0x20000 were unchanged. This also
demonstrates why retaining either the legacy dimensions or the first NVN texture pointer is wrong.
The texture cache entry must be resolved again after its owner replaces the target.

Evidence: `.work/sshd-runtime-before-map.txt`, `.work/sshd-runtime-native-map-open.txt`,
and `.work/sshd-native-map-open-second-logcat.txt`. GDB inspection resumed the guest in a
finally block; it did not write game memory or request a texture download.

## Implemented GPU integration

JNI resolves and validates the current NVN cache entry and publishes only metadata.
`RasterizerVulkan::AccelerateDisplay` resolves its resident image on the GPU thread.
The auxiliary Vulkan presentation uses the same device and scheduler and a separate
transparent Android surface. It never falls back to decoding CPU bytes or downloading
GPU pixels. Frames are acquired without waiting when the auxiliary surface is busy.

The stage bridge calls the complete native initializer `0xC448E0` when capture extents
are zero. Completion is observed at `0xE280B4`, after native render commands and temporary
cache ownership are restored. This denotes command recording completion; the emulator
GPU scheduler provides actual image ordering.

`tools/dualscreen/sshd_map_ui.cpp` is a freestanding guest helper. Its checked-in code is
regenerated with `build_sshd_map_ui.py`. It allocates one native pooled RGBA render target
and calls `MapMain::draw` (`0xC642F4`) into it. Native panes draw the parchment, compass,
labels, entrances, exits, discovered data and icons. Kotlin's synthetic markers were removed.
The UI render target requires FlipV when presented; its pixels have the opposite vertical
orientation from the Android display. Its native dimensions supply the aspect ratio.

The first full-UI experiment hooked MapMain draw itself. This rendered the complete Lanayru
map, but device logs showed its frame counter stopped when the original map closed.
The current hook is `0x12CC8`, the end of native GUI projection/state setup. It skips nested
offscreen targets and renders at most once per stage draw, using a guest frame counter.
Native Link position (`player+0x144`) and rotation (`player+0x13C`) feed the original global
and marker input fields. Native marker and pin controllers rebuild their own draw commands.

The no-camera entrance animation is evaluated at its resource-defined final frame without
running the interactive map state machine. Main-map visibility, pane visibility and alpha
are restored after the companion pass. Touch commands use a mailbox seqlock and apply the
inverse native projection to the map scroll; zoom changes native scale and requests the
original stage-map renderer. They do not scale an Android screenshot.

`test_sshd_map_bridge.py` runs the production emitter/helper in Unicorn, validates original
instruction signatures and checks the handshake, all integer/SIMD registers, stack, flags,
native call arguments, offscreen-target exclusion, recursion protection and hidden-map pose
updates. Native calls are ABI-clobbering stubs in this test; runtime validation remains required.

BOTW startup was separately verified in gameplay. Its stall was caused by the global
`Debugging.use_gdbstub=true` startup wait, not a BOTW map or asset regression. Another
debugging session subsequently re-enabled that setting; it must be disabled after testing.

Remaining acceptance tests: cold entry without opening the pause map; native map open/closed;
continuous movement and rotation; discovered rooms and entrance/exit icons; Lanayru Desert and
additional accessible areas; map/floor changes; reload and teardown; frame-time behavior. None
of these should be marked passed from a successful build or a mailbox Ready value alone.


September 4 lifecycle correction (device verification pending)
-----------------------------------------------------------
The persistent native RGBA target previously had no clear before MapMain::draw.
Translucent parchment and icons therefore accumulated, including after native
pan/zoom changed. The helper now issues the game's NVN CommandBufferClearColor
through main+0x2C8F100 with channel-zero command buffer main+0x4CC0960, target 0,
four zero floats, and write mask 15. This is the same GPU command/ABI used by the
stage renderer at main+0xE241AC. It runs after binding the companion target and
before native layout drawing, without a CPU texture readback.

The Android metadata poll previously invalidated only the Canvas. TextureView
visibility was published on inventory snapshot or page changes, so first texture
readiness could leave its surface invisible. Readiness now publishes map aspect
and visibility directly. A bounded seqlock retry distinguishes a busy mailbox
writer from an invalid resource; busy metadata no longer clears a resident texture.
Actual invalid headers and stage/epoch transitions retain their invalidation paths.

The native initializer call count is logged as `initializations` to distinguish
cold companion initialization from the user's first original-map opening. Unicorn
checks its cold-only increment and the GPU clear's ABI, zero color, mask, and order
between target binding and native draw. All eleven bridge scenarios pass.


Test6 result and camera/alpha follow-up
------------------------------------
Test6 installed successfully. ADB logged automatic native initialization exactly
once, first resident UI image publication, surface visibility, and continuous UI
frame advancement. This did NOT pass visual acceptance: the user reported no
cold map/brightness improvement and a detached camera. QA capture showed a nearly
white UI target with faint native title/compass/icons. Readiness counters alone were
insufficient evidence of correct rendering.

Decompiled C448E0 includes UI-entry side effects at C46C20..C46C48: input mode
16425C0/2 and camera (main+2BF9958)->31C bit 1. The companion now marks its initializer
call with ui_guard=2; an exact-signature, flag-preserving gate bypasses only that block
for that call. Original-map entry replays the original ADRP and follows the unchanged
path. The following virtual call C65DC4 initializes native pin/controller data and is
retained. The bridge test executes both camera-gate branches and checks all registers,
SP and NZCV. It never opens/closes the original map state machine.

Native GUI setup main+13300 disables alpha writes (4CC03AA=0). The auxiliary target
is displayed on a transparent, premultiplied surface: zero alpha with nonzero RGB is
incorrect there. The companion now sets native ColorState color-write mask to RGBA
through 2C8ED70 and binds it through 2C8EFD0 before clear/draw, restoring both native
RGB/alpha-write flags and bound color state afterward. Tests check exact arguments
and restoration. Test7 build/device acceptance remains pending.


Cold picture binding traced to main+C48150
----------------------------------------
The next original RenderingWait update copies stage+550's GX texture identity,
dimensions and format into MapMain+1A60, sets P_map_00 size via +1A8C/+1A90,
gets its material through pane virtual C8, and replaces that material's first
texture map (material+50). This operation is C48174..C48244. C48248 onward enters
the interactive In state. The companion now performs only the verified picture
binding when its owner or source texture key changes, retaining the original game
resource and sampler data. It does not invoke that interactive transition.

The helper is compiled with -fno-strict-aliasing because it accesses the game's
packed overlapping fields directly. The picture-size regression test caught a
real aliasing optimization: a 64-bit size copy could otherwise observe the old
height despite the preceding float store. All thirteen bridge scenarios pass,
including native material-key/dimension/format binding and preservation of its
unrelated format flags. Test8 combines this with the camera gate and alpha writes;
the user confirmed cold loading now works, with the remaining brightness and flashing
issues below. This is not yet full map acceptance.

Test9 GPU trace and composition correction
-----------------------------------------
The trace is saved at `.work/sshd-map-gpu-state9-log.txt`. The native target is
1280x720 RGBA8 UNORM. Each normal pass has one full transparent-black clear and
152 draws. Frame 2520 reaches presentation with only 130 draws and the native
map target (0x5642e0000) still bound; normal completed passes have primary target
0x507330000 bound. Thus presentation can run between the map clear and final draw.
The Vulkan resolver now refuses to sample the active map render target and leaves
the last completed Android surface displayed. The native adapter's final primary
target bind marks the end of the pass. Accepted image sampling stays on the same
scheduler/queue as the native draws, without a texture download or CPU wait.

The first map draws show RGBA mask 0x1111 and RGB/alpha factors SRC_ALPHA,
ONE_MINUS_SRC_ALPHA, ADD. Enabling alpha writes alone is insufficient: this
produces A_out = A_src squared + A_dst*(1-A_src). Android expects premultiplied
coverage A_out = A_src + A_dst*(1-A_src), so the old target adds excessive background
behind its unchanged RGB. For only the registered native UI target and matching
program, the rasterizer temporarily changes alpha source factor to ONE while
building/executing the draw state. It restores the guest register and dirties both
static and dynamic blend state on exit, including failed/early-return draws. RGB
equations, shaders, assets, discovery data and original-map draws are unchanged.
The new target alpha is passed directly to the transparent surface compositor.

Test10 native compilation and full `:launcher:assembleRelease` passed (2m24s).
All 14 bridge scenarios passed. The release was installed on 100.83.63.70:46665
and SSHD launched. After the user loaded the Lanayru Desert save, the Map page
published its full native UI at 218.4s. The new GPU trace verifies alpha factors
ONE/ONE_MINUS_SRC_ALPHA/ADD, unchanged RGB factors, full per-pass clears, and
continuous native frames with one cold initialization. ADB visual inspection
shows the original parchment, compass, discovered terrain and native icons at
normal brightness. The user reported: "All the issues look fixed!"

This verifies the reported issues in the tested Lanayru session; additional
areas/floors and long-session stress testing were not independently completed
in this final pass. Do not claim exhaustive all-area runtime coverage.

Release artifact (same bytes installed):
`artifacts/DualScreen-Launcher-release-2026-09-04-sshd-map.apk` (259484750 bytes).
SHA256: `655CF5AA458F4218A264E2CE8F6C381CB6879639CA551067C29B82FE6A55FFAE`.
Evidence: `.work/sshd-map-composition10-log.txt`, `.work/sshd-bottom10.png`,
`.work/sshd-map-composition-native10.log`, `.work/sshd-map-composition-apk10.log`.

## September 4: transitions, inventory actions, and map hints (build18)

The transition trace reused MapMain, dStage and the native texture key while the
initializer count advanced. The old owner comparison skipped the resource-only
picture bind and appearance animation after this reinitialization. The loader now
clears `ui_owner` and `ui_frames` after the real C448E0 initializer, forcing those
steps even when addresses are reused. `cold-reused-owner` verifies that case.
Build15 ran through five native initializations and continued producing frames;
ADB inspection captured a complete Lanayru map with the original top map closed.
This alone does not establish that every dungeon/floor transition is correct.

The native Map2D.arc `blyt/map_00.brlyt` identifies hint branches `N_backZoom_00`,
`N_input_03`, and `N_nameRotate_00`. The helper resolves them once per layout
initialization through Pane::FindPaneByName (HD virtual slot 0x70), clears their
visible bits only around the companion draw, and restores the exact flags afterward.
These branches sit below the priority groups modified by native MapMain::draw, so
its normal draw ordering does not re-enable them. Compass, beacon count, area name,
terrain and map markers remain part of the original draw. All 15 bridge scenarios
pass, including assertions that hint flags are hidden during draw and restored on
return with volatile registers and camera behavior preserved.

Inventory taps now use real native equipment selections and bottle controls. The
first gameplay trace exposed an incorrect use of `isUsingBottle` (AE0138): that
predicate checks held item 13, the empty catch bottle. R readied the filled potion
as held item 12 in native BINHOLD animation 441, so the first implementation timed
out without sending A. Build18 recognizes both bottle types, waits for the filled
BINHOLD pose before A, and lets native animations 435–437/442 confirm consumption.
It never modifies potion effects, health or quantities. Runtime confirmation of the
corrected action is still pending at this entry.

UI changes include equal text-only tabs, clipped scrolling for the complete quest
history, durability bars below slot names, and direct taps on the four equipment
side slots. The release bundles current Isaac and WFC native mods/assets; WFC's
unchanged prerequisites were hash-checked against the existing remote pack.
Isaac's integrity test now expects its two added UI images and checks their targets.

Validation: 110 launcher tests + 149 engine tests passed; both PC native mod build
checks passed; full native and APK release builds passed. APK signature, packaged
Eden library hash, and every bundled Isaac/WFC manifest hash were verified.
Build18 artifact: `artifacts/DualScreen-Launcher-release-2026-09-04-all-mods-map-touch.apk`
(274125086 bytes), SHA256
`bf4e44543aadc4ae5754264375f9d2ce2f9018ac361698c32a0dd4f3fdb794cd`.
Evidence: `.work/sshd-runtime15-current.txt`, `.work/sshd-map15-bottom.png`,
`.work/sshd-map15-top.png`, `.work/sshd-items-apk15.log`,
`.work/sshd-items-hints-native18.log`, `.work/sshd-items-hints-apk18.log`.
