# UI development pass — 2026-09-06

## Stability continuation: ui-stability-dev5

The user confirmed that texture flashing and black flicker persisted across
three WEMU containers. Container settings are not changed by this continuation.
The previous payload log reports zero eligible owner-thread viewport windows
(upper-viewport status 3) and repeated 8192-call sampling-budget exhaustion.
Those observations do not prove the cause of all texture corruption.

The panel now follows Skyrim's single-BitBlt native-resolution presentation.
Scaled frames, including letterboxing, are composed offscreen before one copy
to the window. A failed scale leaves the prior window frame untouched. A new
offscreen GDI regression test verifies these behaviors and invalid bounds.

Viewport lookup examines native-procedure windows across the game process.
An eligible borderless viewport owned by another thread receives an asynchronous
SetWindowPos request without any cross-thread style mutation. Status 4 means
queued, not verified; status 1 requires observed correct geometry on a later
check. A decorated foreign-thread viewport remains untouched (status 6).
No title-based fallback, D3D hook, focus change or driver change is introduced.

Saved results now use an already loaded MainMenu tweak CDO when the menu or its
override is absent, following native 00bdacf0 -> global 01462794 -> 004967f0.
Invalid nonnull overrides fail validation. No menu is opened and no default is
constructed. The evidence suite now has 238 checks.

`tools/build_local_installer.py` creates an independent, locally signed Android
instrumentation helper. Android requires the same signing certificate as the
production launcher. It validates every archive/manifest hash, requires the
existing private development opt-in, atomically updates the verified cache pin,
and preserves the previous pin. `tools/install_local_payload.py` refuses active
game sessions, installs the cache and changed game files with backups, verifies
all 120 files and the unchanged launcher APK, then removes the helper. This is
local development tooling, not an embedded launcher payload or public release.

Live texture/flicker acceptance, map georeferencing/discovery markers and
remaining campaign statistics are still outstanding. A successful build,
installation or offline render does not resolve those acceptance items.

The `2026.09.06-ui-dev4` local development payload contains swipe navigation,
the 1240×1080 layout cleanup and bounded read-only sample caching. It has not
been installed or accepted in live gameplay. The installed launcher was running
a Switch EmulationActivity during the deployment check, so it was not disturbed.

## Changes

- Seven pages share a 32-pixel content margin and a persistent health/mana footer.
  Labels sit above the bars; values align with their right edges. Both groups
  have 48-pixel outer margins and stay outside the scrolling content.
- Page buttons are replaced by directional swipes and a 240 ms slide transition.
  Noninteractive dots indicate the page. The map body retains pan/pinch;
  swiping its header changes pages. Vertical scrolling never switches pages.
- Powers occupy the full content height until a description is selected. Closing
  the description restores that height. Icons preserve aspect ratio and remain
  centered in their cards. Equipment and charm details use the same dock.
- Documents, equipment, charms, statistics and map lists have bounded drawing
  and touch regions. Scroll indicators show overflow. Map rows with less than
  64 visible pixels cannot accept an accidental selection.
- Nonfinite or nonpositive pinch factors are rejected. Stale snapshots suppress
  actions, and a new generation clears obsolete selections.
- The game-memory cache lasts for one read-only sampling pass. Exact successful
  reads of at most 64 bytes can be reused. Native actions and post-sample root
  validation remain uncached. Its live performance effect is not yet measured.

## Validation

Run `tests/run-tests.ps1` with the prepared original asset directory and an
ignored output directory. The UI harness renders offline fixtures, not live game
data. It is not linked into the payload.

Checks cover all seven pages with full grids, selected details and partial
scrolling; pixel-identical health/mana regions across these states; full-height
powers and description closure; directional page gestures and transitions;
vertical scrolling; map gestures; invalid pinch factors; zero gameplay commands
from navigation; ten charm slots; empty powers; stale state; and generation
recovery. Separate suites cover 26 touch packets, viewport placement/lifecycle
boundaries, and sampling cache invalidation/budgets.

The native payload builds with warnings treated as errors. The reader evidence
tool passes 235 native/schema checks. These checks do not establish live game
compatibility or completion of the entire mod.

## Outstanding acceptance

Install the development payload into both the launcher's pinned cache and the
game directory when no launcher emulation session is active. Do not replace
only the shared DLL: the launcher would restore its currently pinned package.
The installed production APK disables `run-as`; a supported signed development
installation path is still needed to update its private cache.

Live game actions, transition stability and performance still require gameplay
acceptance. Verified Corvo world-to-map coordinates, discovery-aware dynamic
map markers and remaining campaign statistics are unfinished. Fixture renders
must not be presented as evidence for those integrations.
