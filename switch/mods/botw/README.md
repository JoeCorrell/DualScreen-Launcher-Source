# BOTW external companion package

Migration from the working `87a35dc` checkpoint. **Payload built and authenticated;
matching launcher APK not yet built, installed, or launch-verified.**

First local artifact: `.work/botw-package/botw-1.0.0.dsmod`, revision 1,
34,937,096 bytes. SHA-256:
`2e7a52776efffcb59acf5e1e48e34cb322785b2840d639253a4a3b20a351c2ca`.
This is not published and is not compatible with the old host without the migration
runtime extensions. Do not hand off an APK-only or package-only installation as a
working migration.

The signed `.dsmod` owns the BOTW native hook emitter, live-state readers, actions,
map/stock texture routing, complete bottom-screen UI, preview renderer, and assets.
The launcher retains generic Eden memory/NSO/RomFS services, texture codecs,
Filament, package authentication, input routing, and display lifecycle support.

The supported executable remains build ID
`CD57B23FA4BBAD65803D9788C01821EE00000000000000000000000000000000`.
Both the exact NSO layout and existing instruction signatures are checked before
patching. Hold/Eat/equipment/map gameplay logic is not rewritten for this migration.

## Preservation and ABI

- Never move, delete, or replace the user's game RomFS, installed RomFS overrides,
  saves, or `dualscreen/botw/player` caches. The package reads the normal patched
  RomFS through the host. Existing renderer cache lookup order is retained.
- Package assets match all 246 BOTW assets in the last working APK byte for byte.
  The input set is 61,430,419 bytes, including all required indexed armor and held
  models. Weapon variants previously supplied only by cache retain their existing
  packaged-fallback behavior.
- BOTW uses the optional `CompanionHostV2` / `query64-rgba-v1` capability. The ABI-1
  prefixes remain unchanged for previously installed Z-A packages. Queries return
  JSON; actions retain their full 64-bit item identity. Mailbox mapping is bounded
  by the package's verified reservation (BOTW 0x4000, not Z-A's 0x1000).
- Host JNI holds the real session lock around native package callbacks. The
  module's session shim must not try to lock it again. Game objects use checked
  memory access; only the reserved mailbox permits mapped atomic access.
- DEX is loaded from verified code-cache copies, not the immutable package root.
  Known legacy ART files, including `oat/ui.jar.cur.prof`, are relocated to code
  cache; unknown package extras and checksum failures still reject the package.
- The original host renderer remains for SSHD, which shares it. Active BOTW UI
  uses its package copy and has no Eden Java implementation dependencies.

## Build and delivery

Use one build at a time on this 16-GB host. Keep the existing preflight and runtime
memory guard; do not terminate unrelated IDE, Gradle, or reverse-engineering jobs.
Full builds were deferred on September 6 because available memory/commit fell below
the guard's 6/8-GiB starting thresholds.

1. Rebuild the changed Eden native runtime with `ninja -j1 yuzu-android`; strip/copy
   that result into `switch/eden-runtime/src/main/jniLibs/arm64-v8a/`.
2. Build/test the host using `.work/plza-package/build-host-safe.ps1`, tests and
   assembly separately. Keep the last published WFC payload override and existing
   SMO model input. Do not ship experimental unrelated PC payload work.
3. Build the independently signed package with:

   ```powershell
   python switch/mods/botw/build_package.py --version 1.0.0 --revision 1 --player-assets ".work/botw-player-assets/player"
   ```

   This uses the publisher's existing local signing configuration. No secrets or
   original game assets belong in source control. Increase revision for every
   different archive after the first import; never reuse an imported revision.
   For payload-only builds, `--low-memory-ui` compiles the same production sources
   in an isolated 512-MB Kotlin compiler, without configuring launcher Gradle projects.
   A fresh classes directory prevents stale classes, and only BOTW classes enter DEX;
   host APIs/Kotlin/Filament are compile dependencies, not duplicated implementations.
   The first artifact used `.work/botw-package/build-payload-safe.ps1` with live
   memory monitoring. This does not lower the full APK build's safety thresholds.
4. Authenticate the package and inspect the release APK: no `assets/botw/` tree or
   BOTW module classes/native implementation should be embedded. Filament and
   public host interfaces must remain available to external DEX after R8.
5. With the game safely closed, install the matching host and launch it normally.
   Automatic payload setup downloads/verifies the BOTW pack and installs the signed
   module for an installed BOTW game. Do not use manual import to validate this path.
   An APK alone is not a completed deployment if the feed is unavailable.
   Repeat launch to catch ART profile/file-list regressions. Keep Eden debugger off.

## Automatic delivery

The normal delivery path is the existing public
`JoeCorrell/DualScreen-Launcher-Payloads` release `launcher-payloads-v1` and its
`payload-bootstrap.json` catalog. `tools/build-switch-module-feed.py` adds separate
`botw` / `plza` pack roots containing `package.dsmod`, while retaining every other
published pack descriptor unchanged. Upload immutable packs, download and check
their hashes, then update the catalog last. Preserve the previous catalog for rollback.
No source, original RomFS files, or saves are installed through this feed.

The launcher retains its bundled external-module descriptor when the global pack
still contains a legacy APK-only descriptor for the same title. Remote external
descriptors can still update normally. The global pack is not rewritten to force
old launchers to load native modules they do not support.

Startup waits for payload setup, then synchronizes installed Switch games. Failed
imports do not mark the catalog synchronized, so the next refresh retries. The
first startup checks for updates even immediately after device boot. Already
authenticated installed modules remain available if an update package is absent.
Unsigned, corrupt, wrong-title, unsupported-host, or reused-revision packages still
fail validation; no fake installed state or manual-import shortcut is used.

Staged feed version: `2026.09.06.1`, BOTW 1.0.0 revision 1 and Z-A 1.0.1 revision 2.
The original `components` and `mods` pack hashes remain unchanged.

## Checks completed so far

- Standalone Android arm64 native module compile/link; only libc/libm/libdl needed.
- Complete external UI type-check against actual Android/Filament/host API.
- Working hook emitter and game-state bodies match checkpoint source.
- 48 AArch64 CarryBox paths, supported-image installation, atomic signature rejection.
- 567 inventory scroll/hit tests and no whole-grid texture readiness gate.
- Native held-slot reader: one to five slots, repeated ingredients, insects,
  release, stale object/hierarchy rejection, torn poses, read-only access.
- Native map-state reader: regions, revisions, saved pins/stamps and removal.
- All previous APK BOTW assets identical; 534 required index entries resolve.
- Signed artifact accepted by the host's actual archive verifier; all 248 hashes
  verified, 119 module-only DEX classes, no bundled host/Kotlin/Filament classes.
- Wrong publisher, native/manifest tampering, unsigned extras, and path traversal
  rejected by that same verifier.

Still required: guarded host build/tests,
APK packaging audit, import, actual BOTW Play/relaunch, inventory/Hold/Eat/map/quests/
equipment/preview smoke tests, and legacy Z-A package launch regression test.
No new APK/package has been installed as part of this migration yet.

Useful offline checks:

```powershell
python switch/mods/botw/test_migration.py --typecheck --player-assets .work/botw-player-assets/player
```

The existing tests under `switch/eden-upstream/tools/dualscreen/` now route the
carry emitter, inventory scrolling, and native readers to the external sources.
