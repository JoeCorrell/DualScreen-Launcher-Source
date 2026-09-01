# WEMU integration audit for Fallout 3 Second Screen

Date: 2026-08-29
Scope: local source and retained metadata audit only. No WEMU source, APK,
container, registry, device file, or game file was changed.

## Outcome

Fallout 3 can be integrated without a global WEMU compatibility change.

The safe bootstrap policy is:

1. Install an **x86 PE32** `dinput8.dll` owned by this mod beside the supported
   root `Fallout3.exe`.
2. Set Wine's **per-executable** override at
   `HKCU\Software\Wine\AppDefaults\Fallout3.exe\DllOverrides`, value
   `dinput8=native,builtin`.
3. Leave the prefix-wide `Software\Wine\DllOverrides`, container environment,
   input protocol, DXVK files, and WEMU APK unchanged.
4. Create the custom panel as a non-activating popup whose top-left is inside
   the non-primary RandR monitor. WEMU's existing position-based routing then
   gives that window to the physical lower panel.
5. Receive bottom-panel touch through the existing ordered IPv4 loopback UDP
   bridge on `127.0.0.1:7788`; do not synthesize X mouse input.

This is reversible, reaches Fallout whether it starts from a WEMU shortcut or
from the file manager inside the already-running `Games` container, and does
not alter DLL selection for another executable in the same prefix.

## Source evidence and configuration ownership

### Container metadata

WEMU stores each container as JSON at:

`<container root>/.container`

For the observed `Games` container that is:

`/data/user/0/com.wemu/files/imagefs/home/xuser-2/.container`

`Container.toJson()` persists the complete configuration, including `envVars`,
graphics/DX wrapper selections, input flags, and an `extraData` object.
`Container.saveData()` publishes it through `FileUtils.writeStringAtomic`.
Relevant source:

- `engine/src/main/java/com/wemu/engine/container/Container.java:568-615`
- `engine/src/main/java/com/wemu/engine/container/Container.java:446-447`

The second-display key is exactly:

`extraData.displaySecondScreen=1`

It is declared by
`ContainerPerformanceOptions.SECOND_SCREEN` and is absent/off by default.
`wemuOwnsDesktop=1` is also required for a genuine two-monitor Wine layout;
inside Wine's single virtual-desktop window, an application sees only one
monitor even when WEMU's X root has two.

The last successful device probe confirmed `displaySecondScreen=1` for
container 2. `wemuOwnsDesktop` must be re-read when ADB is available; the
existing second-screen games require it, but it was not retained as a separate
evidence field in the device bundle.

### Environment assembly and precedence

WEMU constructs one session environment in this order:

1. Runtime/translator and Android guest defaults.
2. Container `envVars`.
3. Shortcut `[Extra Data] envVars`, which overwrite duplicate names.
4. Session-owned corrections such as the canonical `WINEPREFIX`.
5. Compatibility additions such as `winemenubuilder.exe=d` appended to an
   existing `WINEDLLOVERRIDES` value.
6. `GuestProgramLauncherComponent` merges the resulting session environment
   over FEX/Box64/runtime defaults immediately before process creation.

Evidence:

- `engine/src/main/java/com/wemu/runtime/internal/WemuSession.java:614-647`
- `engine/src/main/java/com/wemu/runtime/internal/WemuSession.java:966-972`
- `engine/src/main/java/com/wemu/engine/xenvironment/components/GuestProgramLauncherComponent.java:705-843`
- `engine/src/main/java/com/wemu/engine/core/EnvVars.java`

`EnvVars` splits persisted variables on spaces and stores them by name in a
map. Commas and semicolons are safe inside a value, but a shortcut's
`WINEDLLOVERRIDES=...` replaces the container's complete value; WEMU does not
merge individual DLL clauses at that point.

WEMU shortcuts are `.desktop` files. `Shortcut` parses arbitrary values under
`[Extra Data]`, and `envVars`, `execArgs`, `screenSize`, `inputType`, and
`exclusiveXInput` can therefore be game-specific. The shortcut launch command
uses `winhandler.exe /dir <executable directory> <filename>`, preserving the
game directory as the working directory:

- `engine/src/main/java/com/wemu/engine/container/Shortcut.java:36-125`
- `engine/src/main/java/com/wemu/runtime/internal/WemuSession.java:1217-1262`

### Why shortcut `WINEDLLOVERRIDES` is not the primary solution

A shortcut-local value such as:

`WINEDLLOVERRIDES=dinput8=n,b`

would be scoped to a session started from that shortcut, and WEMU would retain
it as `dinput8=n,b;winemenubuilder.exe=d`. However, it has two defects here:

1. It replaces any existing container/shortcut override string unless the
   installer reads and correctly composes every existing clause.
2. It is not applied when the user opens the generic `Games` container and
   starts Fallout from Wine's file manager. `winhandler.exe` launches that child
   with the already-established session environment.

The second case is a normal WEMU workflow and is precisely why executable-
scoped Wine registry policy exists in this codebase. Shortcut environment is
acceptable as a temporary diagnostic only, not as the installed mod's source
of truth.

## Exact native `dinput8` policy

### Existing prefix-wide policy

When WEMU prepares Wine system parameters it writes these input DLLs to the
prefix-wide key `Software\Wine\DllOverrides` as `builtin,native`:

- `dinput`
- `dinput8`
- the supported `xinput*` DLL names

Evidence:
`engine/src/main/java/com/wemu/engine/core/WineUtils.java:81-90`.

Therefore an app-local `dinput8.dll` must not be assumed to load just because it
is beside the executable. The prefix currently tells Wine to try its builtin
implementation first.

### Per-executable override

The required entry in container 2's `.wine/user.reg` is logically:

```text
[Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides]
"dinput8"="native,builtin"
```

This value overrides only `Fallout3.exe`. Do not apply it to
`FalloutLauncherSteam.exe`, `Fallout3Launcher.exe`, or the whole prefix unless
runtime evidence proves one of those processes needs to host the mod.

There is a direct repository precedent. `SkyrimAudioCompatibility` writes
`native,builtin` under
`Software\Wine\AppDefaults\<executable>\DllOverrides`, explicitly so launching
from a generic container still works without changing any other game's DLL
policy:

- `engine/src/main/java/com/wemu/engine/session/SkyrimAudioCompatibility.java:55-74`
- `engine/src/main/java/com/wemu/engine/session/ContainerBootstrap.java:892-909`

`WineRegistryEditor` provides `getStringValue`, `setStringValue`,
`removeValue`, and copy-then-rename publication. An installer running inside
WEMU/Companion code should use that editor rather than performing a textual
search/replace on `user.reg`:

- `engine/src/main/java/com/wemu/engine/core/WineRegistryEditor.java:21-76`
- `engine/src/main/java/com/wemu/engine/core/WineRegistryEditor.java:140-150`
- `engine/src/main/java/com/wemu/engine/core/WineRegistryEditor.java:248-255`

### Proton ARM64EC assessment

The configured runtime is `Proton-11.0-5-arm64ec-6`, but the supported
`Fallout3.exe` is PE32/i386. WEMU's ARM64EC bootstrap explicitly retains both:

- `aarch64-windows` runtime files for `system32`.
- `i386-windows` runtime files for `syswow64`.

Evidence:
`engine/src/main/java/com/wemu/engine/session/ContainerBootstrap.java:912-933`.

The AppDefaults mechanism is applied by Wine before the x86 guest DLL is
loaded, and WEMU already relies on it in ARM64EC-capable prefixes. The expected
behavior is therefore:

`Fallout3.exe (PE32)` -> per-app `dinput8=native,builtin` -> app-local PE32
proxy -> Wine's PE32 builtin/system `dinput8` forward target.

This is **high confidence from code architecture**, not yet a live proof for
this exact Fallout executable/runtime pair. ADB was unavailable during this
audit. The first deployment must prove all of the following before any engine
hook is enabled:

- `GetModuleFileNameW` for the loaded proxy resolves to the game directory.
- The host process is the recognized `1.7.0.4` PE32 executable.
- The proxy successfully resolves and forwards `DirectInput8Create` to a
  different module handle.
- Controller, keyboard, mouse, and normal gameplay input remain functional.
- A rate-limited bootstrap log appears once and no recursive DLL load occurs.

The proxy must fail closed on an unsupported executable hash and remain a
transparent forwarder. Initialization should be lazy from the first exported
call or another post-loader-lock point. `DllMain` should only record its module,
disable thread notifications, and avoid hooks, file I/O, waits, COM, window
creation, or thread joins.

The forward target should be opened from the Wine Windows system directory
with an absolute path/system-directory search, and the returned module must be
checked against the proxy's own handle. Never call bare `LoadLibrary("dinput8")`
from the proxy because that can select the proxy again. Runtime proof is still
required because Wine's builtin/native loader differs from Windows' ordinary
filesystem search.

## Lower-screen window routing

### WEMU monitor creation

With `displaySecondScreen=1`, WEMU asks Android for a presentation display and
builds a vertically stacked X root:

```text
THOR-TOP    = (0, 0, topWidth, topHeight)
THOR-BOTTOM = (0, topHeight, bottomWidth, bottomHeight)
```

The root width is the larger panel width and its height is the sum. Source:

- `engine/src/main/java/com/wemu/runtime/internal/WemuSession.java:1122-1158`
- `engine/src/main/java/com/wemu/engine/xserver/ScreenLayout.java:84-99`

When a second screen is enabled WEMU also forces
`WINE_DISABLE_FULLSCREEN_HACK=0`, allowing rootless Wine windows and the
auxiliary panel to coexist without changing the main game's exclusive state:
`WemuSession.java:904-920`.

### Window ownership rule

`SecondScreenOutput` owns the Android lower-panel surface. A guest window is
claimed when its **top-left root coordinate** is inside the second monitor.
This is generic and does not require Fallout-specific WEMU code:

- `engine/src/main/java/com/wemu/engine/renderer/SecondScreenOutput.java:297-321`
- `engine/src/main/java/com/wemu/engine/renderer/CompositorRenderer.java:934-982`
- `engine/src/main/java/com/wemu/engine/renderer/ASurfaceRenderer.java:1206-1262`

GPU-backed windows submit an `AHardwareBuffer` directly to SurfaceFlinger.
Ordinary GDI/CPU windows use a bounded three-buffer copy path. Both renderer
paths keep a lower-only window out of the primary scene. GPU scanout is the
preferred final path if a second independent D3D9 device proves stable and
cheaper than CPU copies; a low-rate GDI diagnostic surface is suitable for
Phase 3 bring-up and must be profiled before becoming the finished renderer.

### Required Fallout panel construction

The panel should follow the already-proven native window contract:

1. Call `EnumDisplayMonitors` and select a non-primary monitor from
   `MONITORINFO`; do not hardcode `y=720` or a bottom resolution.
2. Register a stable class such as `Fallout3SecondScreenPanel` and title such
   as `Fallout 3 Dual Screen` for diagnostics.
3. Create a `WS_POPUP` with `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`.
4. Create it hidden at exactly `rcMonitor.left/top` and the monitor's full
   client width/height.
5. Initialize and clear the backing surface before mapping it, then call
   `ShowWindow(..., SW_SHOWNOACTIVATE)`.
6. Every `SetWindowPos` must include `SWP_NOACTIVATE`; do not make it topmost on
   the Thor. Handle display geometry changes by re-enumerating and rebuilding
   only size-dependent resources.
7. Destroy/release the panel outside `DllMain`; never tear down D3D/window state
   under loader lock.

The existing implementations demonstrating this contract are:

- `dualscreen/mods/SkyrimSecondScreen/src/Mod.cpp:1428-1508`
- `dualscreen/mods/DeadIslandSecondScreen/src/Panel.cpp:187-295`
- `dualscreen/mods/DeusExSecondScreen/src/Panel.cpp:730-760`

Position must be correct before the first mapped frame. WEMU has an early
identity exception for Skyrim, but no Fallout exception. Creating the Fallout
window hidden in its final lower-monitor position avoids a transient opaque
panel over gameplay and requires no WEMU source change.

## Touch routing and duplicate-input audit

The Android lower-display `Presentation` is created with `takesFocus=false`, so
it cannot take controller/keyboard focus away from the upper game. Once the
guest has presented a lower-panel frame, `GuestPanelMotionRouter` consumes the
Android `MotionEvent` and sends normalized gestures to the guest:

- `app/src/main/kotlin/com/wemu/session/SessionScreen.kt:452-576`
- `app/src/main/kotlin/com/wemu/session/SessionScreen.kt:662-832`

`SecondScreen` owns one coroutine, one IPv4 socket, and an ordered channel. It
sends native gestures to `127.0.0.1:7788`; port 7787 is separately reserved for
discovery. This preserves down/move/up ordering and keeps network work off the
Compose pointer loop:

- `secondscreen/src/main/kotlin/com/wemu/session/secondscreen/SecondScreen.kt:34-74`
- `secondscreen/src/main/kotlin/com/wemu/session/secondscreen/SecondScreen.kt:216-275`

The Fallout listener should:

- Bind only AF_INET `127.0.0.1:7788`.
- Accept only the documented bounded messages: `tap`, `hold`, `down`, `move`,
  `up`, `cancel`, `pinch`, and later the existing text/key messages if needed.
- Validate finite normalized coordinates and pinch factors.
- Coalesce adjacent move packets and use a bounded queue.
- Never dereference game objects or execute gameplay actions on the socket
  thread.
- Drain into the UI/game synchronization point with load-generation IDs and
  discard stale actions after save/load/death transitions.
- Send haptic acknowledgements only after a real UI action succeeds.

`SecondScreenOutput.injectTouch()` contains a legacy X-pointer path and bypasses
it only for Deus Ex and Skyrim identities. A source-wide call-site audit found
no current caller: both the Presentation-level router and Compose fallback call
`SecondScreen.sendGuestGesture` directly. Therefore Fallout touch is **not
currently duplicated into X**.

Do not call `injectTouch()` for Fallout. If WEMU later reconnects that legacy
method, add a narrowly scoped native-panel identity mechanism before shipping;
otherwise one finger action could become both a UDP UI event and a gameplay X
click. A robust future WEMU refinement would use a shortcut extra such as
`nativeSecondScreenPanel=fallout3`, rather than broad title substring matching,
to suppress X injection and managed WebSocket discovery only for that launch.
That change is optional and was not made in this audit.

Because Fallout is not yet recognized by `nativePanelWaitingTitle`, WEMU may
also retry its managed second-screen WebSocket while the native panel is
starting. That path does not synthesize gameplay input, but it is unnecessary
work. It can be removed later with the same explicit per-shortcut native-panel
key; it is not a reason to make global Fallout checks throughout WEMU.

## Controller routing

The `Games` container should retain its existing protocol until device evidence
says otherwise. WEMU's default is XInput (`0x04`); DirectInput is `0x08`, and
the editor can enable both. Enabling both speculatively is unsafe because some
games see one physical controller twice. WEMU already exposes
`exclusiveXInput` for that class of bug:

- `engine/src/main/java/com/wemu/engine/winhandler/WinHandler.java:49-55`
- `app/src/main/kotlin/com/wemu/library/ContainerEditorTabs.kt:1124-1163`
- `engine/src/main/java/com/wemu/engine/session/ContainerBootstrap.java:399-415`

The `dinput8` bootstrap does not require exposing the physical gamepad through
Wine DirectInput. Fallout imports DirectInput for normal PC input, while WEMU
can continue presenting the controller through the already-working container
protocol.

The bottom Android Presentation deliberately does not receive controller
focus. For Phase 5, controller navigation should therefore be implemented in
the native mod at Fallout's validated input/update boundary:

- Use an explicit mode/chord to enter lower-panel navigation.
- Read the game's already-normalized button edges once per update.
- Consume only the UI-owned actions while that mode is active so one button
  cannot both activate the Pip-Boy UI and attack/use in gameplay.
- Do not create a second DirectInput device solely for the panel.
- Do not toggle WEMU's global XInput/DirectInput mode as part of page changes.

If Android-side controller routing is later preferred, it needs a per-session
Fallout identity and an acknowledged command channel similar to WEMU's existing
Stardew shoulder-button special case (`SessionActivity.kt:2622-2641`). It must
consume both key down and key up only while the native panel explicitly owns
controller focus. No such WEMU change was made here.

## Reversible installer transaction

The installer must treat the shared game directory and private Wine registry as
one transaction and refuse to run while Fallout or the prefix wineserver is
active.

### Preflight

1. Resolve the canonical game root and require the supported root executable
   size/hash/version from `device-recon.md`.
2. Resolve container 2 by ID/root, not only by its mutable display name.
3. Require `displaySecondScreen=1`, `wemuOwnsDesktop=1`, and the expected Proton
   runtime, but do not rewrite unrelated settings automatically.
4. Read and record the existing AppDefaults `dinput8` value.
5. Hash any existing game-local `dinput8.dll`.
6. If that DLL is not an allowlisted prior build of this mod, stop with a clear
   conflict. Do not overwrite or silently chain an unknown mod loader.
7. Prove `Fallout3.exe`, its launcher, and the container's Wine process tree are
   absent using the full process table, not `pidof` alone.

### Commit

1. Create a timestamped backup/manifest outside the live game filenames. Store
   prior file hashes, prior registry presence/value, target executable hash,
   container ID, runtime ID, and installer revision.
2. Copy each new file to a temporary sibling, hash it, and rename it into place.
   The payload should contain only original mod files; no Bethesda executable,
   DLL, font, texture, BSA, ESM, or other proprietary content.
3. Use `WineRegistryEditor` against
   `xuser-2/.wine/user.reg` to set only:
   `Software\Wine\AppDefaults\Fallout3.exe\DllOverrides / dinput8 = native,builtin`.
4. Re-read the registry value and installed file hash before reporting success.
5. Do not modify prefix-wide `Software\Wine\DllOverrides`, the container
   `envVars`, `inputType`, DXVK files, Proton files, or `.container` unless a
   separately proven prerequisite is missing and the user authorizes it.

### Rollback/uninstall

1. Again prove the game/prefix is inactive.
2. Remove only files whose hashes match the installed manifest.
3. Restore the prior `dinput8.dll` byte-for-byte if one belonged to an earlier
   allowlisted build.
4. Restore the exact prior AppDefaults value, or remove only the `dinput8` value
   if it did not previously exist. Do not delete the whole AppDefaults key when
   it contains other values.
5. Verify restored hashes and registry state, then retain the manifest/log for
   diagnosis.

### Launcher-specific note

Dualscreen Companion can also set a shortcut-local override because it always
owns the one-click launch descriptor. It should still prefer the same
AppDefaults transaction for consistency with WEMU and to survive a launcher
path change. If it uses `WINEDLLOVERRIDES`, it must merge the existing clauses
instead of replacing them and produce an effective value such as:

`dinput8=n,b;<existing clauses>;winemenubuilder.exe=d`

The registry remains the authoritative and less fragile plan.

## Acceptance checks before Phase 1 integration is considered proven

- The device is reachable and the current `.container`, `user.reg` value, and
  shortcut `[Extra Data]` are captured read-only.
- A dry-run installer reports exactly one game file addition and one
  per-executable registry value change.
- A test launch log proves the PE32 proxy loaded from the game directory under
  Proton 11 ARM64EC and forwarded DirectInput successfully.
- The top game window retains focus before and after the lower window maps.
- WEMU logs the lower monitor, then accepts the panel by its root coordinates.
- The panel never appears in the primary compositor, including its first frame.
- A lower-panel tap produces one UDP event, zero X button events, and zero
  gameplay actions.
- Drag/pinch packet order remains correct and the listener queue stays bounded.
- Closing/reloading Fallout releases the lower panel back to WEMU without a
  frozen final frame or resource leak.
- Uninstall restores the exact prior AppDefaults and file state.

## Remaining unknowns

1. The exact current `wemuOwnsDesktop`, container input flags, prefix-wide and
   AppDefaults registry values must be refreshed when ADB returns.
2. The Proton ARM64EC loader behavior for this exact app-local PE32 proxy needs
   live proof; source architecture strongly supports it but is not runtime
   evidence.
3. The cheapest stable final panel renderer (independent D3D9 GPU surface versus
   throttled GDI CPU surface) needs on-device profiling.
4. The exact Fallout engine input/update boundary for controller UI mode belongs
   to the binary/game-state investigation and is not established by WEMU code.
5. If WEMU's currently unused `injectTouch()` is connected in a future build,
   Fallout needs explicit native-panel identity suppression before deployment.
