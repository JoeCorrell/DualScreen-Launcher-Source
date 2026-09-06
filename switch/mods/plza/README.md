# Pokémon Legends: Z-A external companion module

This module is separate from the launcher APK. Its Android UI DEX, ARM64 state/action
reader, guest ARM64 injection hooks, archive lookup code and assets travel together
in one publisher-signed `.dsmod` package. No standalone game executable or keys are
included. Shared emulator memory access, texture decoding and controller routing
remain in the host.

## Compatibility

- Host native/UI ABI **1**, Android ARM64, minimum Android 8.
- Title `0100F43008C44000`.
- Supported game build `B1F12FD919EAE86AB8A978317677E64BCE443D1F` (update v393216).
- NSO segment sizes and all existing native hook signatures are checked before injection.
- The one-time launcher loader update is required. Future module releases using ABI 1
  do **not** require building or installing another launcher APK.

## Build independently

From the workspace root with the existing Android SDK/NDK and publisher key:

```powershell
python switch/mods/plza/build_package.py --version 1.0.1 --revision 2
python switch/mods/plza/tests/test_package.py .work/plza-package/plza-1.0.1.dsmod
```

The build uses one native worker and one Gradle worker (1536 MB heap), builds only
the small module and its UI library, then DEX-compiles and signs the package using
`keystore.properties`. Signing passwords are not passed on the command line.
Each update needs a strictly increasing revision and matching native/manifest version.
Version 1.0.1 links its C++ runtime privately and verifies that ELF dependencies are
Android system libraries only. No C++ objects or exceptions cross the C-layout host
ABI. `--skip-ui-build` is for a native-only update with a verified unchanged UI AAR.

## Install / update

Close the game. Put the `.dsmod` file in a folder on the device. In the launcher's
Z-A game page choose **Install mods**, select that folder, select the `.dsmod` file,
and install it. Ordinary exefs/romfs mods continue using their existing importer.
Start Z-A normally; its menu never needs to be opened to initialize this module.

The loader verifies every entry against the installed launcher's signing certificate,
checks manifest/file hashes, title, ARM64 architecture and ABI, and stages an immutable
private version before atomically changing the active pointer. Invalid, unsigned,
truncated, path-traversing or older packages cannot replace the installed version.
Previous directories are retained; no active native code is unloaded mid-session.
Native code, UI and asset roots are pinned to the same package hash for gameplay.
The host loads a hash-verified read-only UI copy from private code cache. Android
ignores DexClassLoader's optimized-directory argument on API 26+, so loading directly
from the package can create OAT/VDEX sidecars and break its strict inventory check.
Host 1.6.17.7 isolates that cache and preserves only recognized legacy ART sidecars
outside the signed tree; other unexpected files or altered hashes remain errors.

For the existing trusted payload updater, publish an independent ZIP pack with root
`plza` containing `package.dsmod`, then list it in the payload catalog. The Switch
manifest uses delivery `external-module` and package `plza/package.dsmod`.
The signed file is still verified after the catalog ZIP's hash verification.
This change does not itself publish a release or alter the remote catalog.

## Source boundaries

- `native/module.cpp`: exact build probe and stable C-layout entry point.
- `native/hooks.cpp`: unchanged Z-A guest hook installer extracted from NSO loader.
- `native/assets.cpp`: original Trinity archive lookup, over generic host RomFS APIs.
- `native/host_shim.h`: local adapter; no Eden C++ objects cross the shared-library ABI.
- `ui/`: complete external inventory/map/party/quest/Pokédex view.
- Existing native reader: `../../eden-upstream/src/android/app/src/main/jni/plza_companion.cpp`
  compiled **only into this module**, not libyuzu.
- Existing guest payload generator/tests: `../../eden-upstream/tools/dualscreen/plza_*`.
- Assets: existing extracted `src/android/app/src/dualscreen/assets/plza`, excluded from APK.

Packaging is not a claim that all item classes are implemented. Preserve the item-action
status and remaining gameplay validation in `docs/plza-item-actions.md`.
