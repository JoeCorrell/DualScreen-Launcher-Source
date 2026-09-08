# Skyrim reference: screen and payload architecture

Read directly from the existing Skyrim implementation on September 6, 2026.
This records source behavior and the subsequently authorized WEMU installation.
Runtime acceptance remains pending the user's launch test.

| System | Skyrim implementation | Dishonored adaptation |
| --- | --- | --- |
| Bootstrap | `SkyrimSecondScreen/src/Proxy.cpp` loads a separate CPU/GDI panel; `SkyrimPlugin.cpp` supplies SKSE game state. | D3D9 Dishonored uses its `dinput8` bootstrap. `Runtime.cpp` now starts game-state initialization separately from the panel, keeping the panel available during language/player initialization and hook failure. No Skyrim engine calls are reused. |
| Upper screen | The lower window does not create another DXGI swap chain. Native optional previews are separate from ordinary gameplay. | `Panel.cpp` creates a GDI window with `WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`; mouse activation returns `MA_NOACTIVATE`. No additional D3D9 device or gameplay swap chain is created. |
| Lower geometry | `Mod.cpp::CreatePanel` prefers a secondary monitor, then reads the `(0,720,1240,1080)` configured fallback for Wine's tall desktop. | Real secondary monitors remain preferred. The same lower rectangle is supported only for the exact single-monitor 1280x1800 Thor layout. A single 1280x720 desktop is not accepted as a lower display. `[Panel] TallDesktopFallback=0` disables this fallback. |
| Android output | `CompositorRenderer` and `ASurfaceRenderer` classify the private panel window and route changed CPU pixels through `SecondScreenOutput.submitPixels` and its bounded hardware-buffer ring. | Dishonored's exact private class/title already participate in `NativePanelIdentity` and both common rendering paths in launcher source. This classification also excludes the panel from upper-screen composition and gameplay-window focus selection. |
| Touch | Normalized loopback UDP 7788 feeds semantic panel events. Skyrim bypasses duplicate X11 pointer injection. | Same transport and panel coordinate fitting, with Dishonored-specific continuous down/move/up contacts, cancellation, pinch handling and completion suppression. The launcher source bypasses X11 gameplay input before and after panel discovery. |
| State and actions | SKSE samples after the real player update and uses explicit lifecycle events and bounded commands. | Exact executable validation, verified UE3 controller tick/Corvo restore/teardown hooks, a triple snapshot buffer, bounded queues, live object-registry checks and commands executed on the established game thread. Native task-marker and note-interaction observations are also restricted to that thread. |
| Delivery | Native proxy/plugin files are installed into the game; the launcher owns Android output and input routing. | The full Dishonored DLL, configuration and original-resource files remain a separate downloadable payload. A stable full-runtime identity distinguishes it from the earlier diagnostic DLL. Android routing belongs to the host and cannot be installed merely by replacing the Windows DLL. |

The newly separated initializer is joined before the adapter, definitions or
bridge are destroyed, including panel failures and exception unwinding. A failed
initializer detaches partially installed hooks and publishes only a bootstrap
failure state; it cannot manufacture a live player snapshot. Worker log rotation
and appends are serialized now that panel and initializer logging can overlap.

The earlier WEMU baseline had `displaySecondScreen=0`; the live container read
before installation already had `displaySecondScreen=1`. Its bytes were preserved
through installation, including Wine, driver, FEX 2608 and all other settings.
The full development DLL now replaces the diagnostic proxy. The separately
installed WEMU APK was inspected directly, rather than inferred from launcher
source: `NativePanelIdentity.isSkyrim` accepts a class component containing
`skyrimsecondscreenpanel`; `SecondScreenOutput.injectTouch` returns before X11
injection for that identity. `GuestPanelMotionRouter` and `NativePanelInput`
send normalized tap/hold/drag/pinch packets over UDP 7788.

The payload-only `[Panel] WemuSkyrimTransport=1` option adds that recognized
transport token to Dishonored's private class. The title, UI and native UE3
adapter remain Dishonored-specific. WEMU additionally includes duration on
standalone tap/hold packets: the parser now accepts and validates this metadata,
and a standalone hold can open an entry's details without executing an action.
Twenty-six compiled protocol cases cover these forms and malformed packets.
The option is off by default and enabled only in the WEMU package recipe via
`--wemu-transport`. No APK change is required to use this existing host path.

Skyrim's world-map rendering is engine-specific: it has a native MapCamera and
world-map rendering path. Dishonored's inspected maps are schematic note-board
artwork. Its live positions must be calibrated against its own world and assets;
copying Skyrim's map-render hook cannot solve that missing transform. Likewise,
SKSE lifecycle notifications and character layouts are not UE3 hooks.

Full source/static checks and real device acceptance remain separate. The user
subsequently requested installation and confirmed Dishonored was closed. The
payload was built and installed; all 120 file hashes were verified. No APK was
built or installed, and the game has not been launched or controlled by this
installation step. See `.work/dishonored/install-test/installation-receipt.json`.
