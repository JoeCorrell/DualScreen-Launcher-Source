# Android BOTW dual-screen companion

The `dualscreen` Android flavor keeps gameplay on the primary panel and presents an always-on,
touch-oriented BOTW companion on the Android presentation display (for example, display 4 on the
AYN Thor). The lower panel is an independent Android UI; it does not create a second Vulkan
swapchain and does not move BOTW's pause-menu framebuffer away from the main display.

## Supported game build

The live bridge is guarded by both program ID and main-module Build ID:

- Program ID: `01007EF00011E000`
- BOTW 1.9.0 Build ID prefix: `CD57B23FA4BBAD65`

All guest-memory layouts fail closed when either identifier differs. Other games show an inactive
companion screen and are otherwise unaffected.

## Live data

The inventory page reads player health/stamina, rupees, pouch items, equipped slots, weapon
durability/modifiers, attack power, bow power, shield guard, total armor defense, and rune
availability/upgrades. Item base stats come from the user's unmodified
`Actor/ActorInfo.product.sbyml`; names and descriptions come from `Bootup_USen.pack`. The generated
lookup tables are read-only and sorted, so lookups do not parse BYML or MSBT during gameplay.

The lower UI reads compact health, stamina, and rupee data once per second on a background-priority
worker. Full pouch/equipment traversal is limited to once every five seconds (or the next poll
after a companion equip action), and parsing/redrawing occurs only when data changes. Decoded
bitmap memory remains bounded to 12 MiB. It never renders the game twice.

## Player preview equipment

The preview index covers all 186 base-game pouch actors for swords, large swords, spears, bows,
and shields. Each actor is mapped to a GLB generated from the user's own RomFS and uses the
Nintendo-authored `PlayerHoldTransOffset` and `PlayerHoldRotOffset` from its actor pack. Only the
currently equipped models are read, and the headless Filament renderer is destroyed after its
cached readback, so the expanded asset library adds no steady-state rendering work.
Shields share Link's `Pod_A` back anchor with the bow and receive a small portrait-only depth
adjustment so they remain behind the torso instead of following the left-hand bone.
The 3D weapon is intentionally omitted from the portrait. Camera framing uses Link's invariant
base-body bounds, so loading a save or changing armor/back gear cannot zoom the model out.

`generate_botw_equipment_manifest.py` resolves and deduplicates the actor packs,
`export_botw_bfres_models.ps1` performs the sequential BFRES conversion, and
`pack_gltf_glb.py` normalizes Assimp's joint data into valid self-contained glTF 2.0 files.
`audit_botw_player_assets.py` verifies index coverage, attachment bones, references, and GLB
headers before packaging.

## Original UI assets

`tools/dualscreen/extract_botw_romfs_files.py` selectively reads RomFS ranges from the user's own
base NSP over ADB. `decode_botw_bfres_textures.py` converts the selected stock-item and common UI
textures to Android PNG assets. The extractor validates the decrypted RomFS header, downloads only
the requested ranges, and never prints key values.

The generated APK contains Nintendo assets recovered from the user's local game copy and is for
that user's personal installation. Do not publish or redistribute that APK or the extracted asset
directory.

## Build and install

The flavor requires Android 13 (API 33) or newer. Use the optimized package for gameplay:

```powershell
cd src/android
.\gradlew.bat :app:assembleDualscreenRelWithDebInfo
adb install -r app/build/outputs/apk/dualscreen/relWithDebInfo/app-dualscreen-relWithDebInfo.apk
```

The optimized package ID is `dev.eden.eden_emulator.dualscreen.debug`, matching the calibration
build so it upgrades in place while remaining separate from normal Eden. The second panel must be
connected and expose `Display.FLAG_PRESENTATION` before emulation starts.
