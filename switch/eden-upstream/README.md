# Eden DS

Eden DS is an Android-focused fork of [Eden](https://git.eden-emu.dev/eden-emu/eden) that adds a dedicated, always-on companion interface for dual-screen devices. Gameplay remains on the primary display while a touch-first UI runs on Android's presentation display.

The companion is integrated into the emulator and reads live guest state through build-checked bridges. It does not mirror the primary display, move an existing pause menu, or run a second copy of the game renderer.

## Supported games

| Game | Title ID | Supported main Build ID |
| --- | --- | --- |
| The Legend of Zelda: Breath of the Wild | `01007EF00011E000` | `CD57B23FA4BBAD65803D9788C01821EE` |
| Mario Kart 8 Deluxe | `0100152000022000` | `FE941ED5BA14BE5D505698DA1BBF4FE7` |

Compatibility is intentionally strict. The live-memory bridge is enabled only when both the title ID and main-module Build ID match. Unsupported builds fail closed instead of reading unknown memory layouts.

## Breath of the Wild companion

- Always-on Inventory, Map, and Quests pages designed for touch
- Live health, stamina, rupees, attack, defense, equipment, effects, runes, and Champion abilities
- Scrollable inventory categories and item grid with real in-game equip actions
- Live Link preview with equipped armor and back-mounted shield
- Terrain map with player position, locations, markers, shrines, pan, and pinch-to-zoom
- Completed-shrine interaction and fast-travel requests
- Live quest names, descriptions, objectives, and completion state
- Loading gate that keeps the interface inactive until a save is ready
- Cached assets, bounded bitmap memory, throttled full snapshots, and change-driven redraws

## Mario Kart 8 Deluxe companion

- Wii U GamePad-inspired layout scaled for a 1240 x 1080 lower display
- Live 12-racer standings with character or Mii portraits, positions, and lap progress
- Course-specific track maps with real-time racer projection and local-player highlighting
- Two live item slots with item-roulette presentation and touch activation
- Live coin count and race progress
- Persistent dark mode toggled by a long press anywhere on the companion UI
- Race-aware activation so the companion waits until race data is available

## Requirements

- Android 13 or newer (API 33+)
- An arm64 Android device exposing a secondary display with `Display.FLAG_PRESENTATION`
- Legally dumped games, firmware, keys, and any required updates
- The exact supported game builds listed above

The dual-screen flavor is intended for hardware such as the AYN Thor and similar Android devices whose lower panel is exposed as a presentation display before emulation starts.

## Build

The Android project uses Java 17, Android SDK 36, and NDK `28.2.13676358`.

```powershell
cd src/android
.\gradlew.bat :app:assembleDualscreenRelWithDebInfo --no-daemon --max-workers=1 --no-parallel
```

The optimized APK is written to:

```text
src/android/app/build/outputs/apk/dualscreen/relWithDebInfo/app-dualscreen-relWithDebInfo.apk
```

The optimized package ID is `dev.eden.eden_emulator.dualscreen.debug`. It installs separately from standard Eden builds and upgrades earlier Eden DS calibration builds in place.

## Install

Connect the Android device, then install the optimized package:

```powershell
adb connect DEVICE_IP:ADB_PORT
adb -s DEVICE_IP:ADB_PORT install -r src/android/app/build/outputs/apk/dualscreen/relWithDebInfo/app-dualscreen-relWithDebInfo.apk
```

Make sure the lower display is active before starting emulation. Eden DS will show a waiting screen until a supported title and valid gameplay state are detected.

## Architecture

The Android `Presentation` owns the second-screen surface. Kotlin views render the companion UI, while small JNI bridges validate the active title/build and expose compact snapshots of game state. Guest-memory reads are guarded, cached, and scheduled independently from the primary renderer.

BOTW uses inexpensive live snapshots for frequently changing values and less frequent full pouch/equipment traversal. Mario Kart updates race state without rebuilding the UI hierarchy or decoding textures every frame. Both companions redraw only when relevant state changes.

Technical details and BOTW asset tooling are documented in [AndroidDualScreen.md](docs/AndroidDualScreen.md) and [tools/botw-companion](tools/botw-companion/README.md).

## Game assets and releases

Eden DS does not grant redistribution rights for Nintendo game assets. Game data, keys, firmware, ROMs, and APKs containing assets extracted from a user's game copy must not be uploaded to public releases. Generate and use those packages only from games you legally own and dump yourself.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the upstream contribution guidelines. Changes to a live bridge should preserve exact build validation and fail safely when guest state is unavailable.

## Credits and license

Eden DS is based on the open-source [Eden Emulator](https://git.eden-emu.dev/eden-emu/eden) project. Nintendo, The Legend of Zelda, Breath of the Wild, Mario Kart, and related names and assets are trademarks or copyrights of their respective owners. This project is not affiliated with or endorsed by Nintendo.

The project is free software under GPLv3. Third-party components retain their respective licenses; see [LICENSES](LICENSES) and the repository's REUSE metadata.
