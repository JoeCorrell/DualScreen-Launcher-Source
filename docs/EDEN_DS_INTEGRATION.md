# Eden DS integration architecture

## Boundary

Dualscreen Launcher owns the application, navigation, library, file pickers, progress UI, and
backend selection. PC games continue through `GameSessionActivity` and the immutable Wine/Proton
presets. Switch games use `EdenSwitchBackend` and start Eden's `EmulationActivity` inside the same
package and APK.

`switch/eden-upstream` is the preserved Eden DS source snapshot. `switch/eden-runtime` is the
Android library adapter. It compiles Eden's Kotlin/resources and packages the current arm64 native
core, Vulkan runtime hooks, input, content management, saves, shader caches, settings, and the
existing game-specific dual-screen presentations. Eden's standalone `MainActivity` is never
declared by the combined application.

Eden initialization is lazy. PC-only launcher use does not load `libyuzu-android.so`, scan Switch
content, initialize Vulkan, or start Eden services. `YuzuApplication.initialize()` accepts the
Launcher-owned `Application` and is called only by the Switch backend.

## Supported title gates

| Game | Base title ID | Dual-screen implementation |
| --- | --- | --- |
| Mario Kart 8 Deluxe | `0100152000022000` | `Mk8dCompanionView` / `mk8d_companion.cpp` |
| Breath of the Wild | `01007EF00011E000` | `BotwCompanionView` / `botw_companion.cpp` |
| Pokémon Legends: Z-A | `0100F43008C44000` | `PlzaCompanionView` / `plza_companion.cpp` |
| Skyward Sword HD | `01002DA013484000` | `SshdCompanionView` / `sshd_companion.cpp` |

Base packages must be bootable NSPs whose decrypted program ID exactly matches the selected game.
Update and DLC packages are inspected before installation. JNI returns each NSP's CNMT title type,
raw title ID, and Eden's canonical base title ID. The Launcher rejects the package before calling
`InstallNSP` if either its type or owning title is wrong. The canonical mask also fixes the prior
DLC mismatch caused by clearing only the low 12 bits instead of using `FileSys::GetBaseTitleID`.

## Data and migration

The integrated runtime stores its protected state below the Launcher's external-files directory at
`Switch/Eden`. Eden's original directory layout remains unchanged beneath that root, including
`config`, `keys`, `nand`, `sdmc`, `load`, `shader`, and `user/save`. This retains native compatibility
with Eden's content provider, PatchManager, save resolver, and shader cache.

On the first Switch initialization, the backend looks for the prior mainline, dualscreen, and debug
Eden package directories. If a valid prior config exists and the new runtime directory is empty, it
copies the complete tree before native initialization. This preserves settings, firmware, keys,
installed updates/DLC, mods, saves, shader caches, and profiles when Android storage policy allows
cross-package access. Base-game SAF grants cannot be transferred between Android package IDs, so a
base NSP may need to be selected once in the Launcher; no game data is copied or redistributed.

## Content and settings lifecycle

- Base NSP URIs retain a persistable read grant and remain user-owned files.
- Updates and DLC use Eden's NAND installer and are refreshed through `getPatchesForFile`.
- Mods retain Eden's `load/<title id>/<mod>` layout and `disabled_addons` configuration.
- Per-game settings are opened with `initializePerGameConfig`, written through Eden's native
  setting objects, saved, and always unloaded in `finally` to prevent configuration leakage between
  titles.
- Save import uses a traversal-safe staging directory and preserves the existing save until the new
  tree has copied successfully. Save export streams the real Eden save directory to a user-selected
  ZIP.

## Build and update notes

The runtime currently packages an arm64 native snapshot built from the adjacent vendored source.
When native Eden files change, rebuild the dualscreen native target, strip it, and update the six
libraries under `switch/eden-runtime/src/main/jniLibs/arm64-v8a`. Kotlin and resource code compiles
directly from `switch/eden-upstream`, so frontend/runtime changes do not require a second app.

The combined application is GPL-3.0-or-later. Eden and inherited source SPDX headers and licence
files remain under `switch/eden-upstream`.
