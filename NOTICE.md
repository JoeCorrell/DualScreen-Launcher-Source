# Provenance

What is in this repository and where it came from. Kept because the
[LICENSE](LICENSE) depends on it being true, and because a fork that cannot say
which half is its own has lost track of something worth knowing.

## The engine

`engine/` and the guest environment are derived work, not original.

| Source | Licence | What came from it |
|---|---|---|
| [Winlator](https://github.com/brunodev85/winlator) — brunodev85 | GPL-3.0 | The foundation of every fork below: the X server, the Wine container model, the Box64 integration. Also serves the `input_controls` profiles. |
| [Winlator Ludashi](https://github.com/StevenMXZ/Winlator-Ludashi) — StevenMXZ | GPL-3.0 | The direct upstream of this fork. 218 files and roughly 35,500 lines were migrated from `com.winlator.cmod` into `com.wemu.engine.*`. |
| [Bannerlator](https://github.com/The412Banner/Bannerlator) — The412Banner | GPL-3.0 | The Steam client protocol layer under `com.wemu.steam.{client,download,emulator,saves}`. Also hosts the content catalogues this app reads. |
| [Eden DS](https://github.com/JoeCorrell/Eden-DS) and [Eden Emulator](https://git.eden-emu.dev/eden-emu/eden) | GPL-3.0-or-later | The embedded Nintendo Switch emulator core, Android/JNI runtime, Vulkan renderer, content manager, saves, settings, input, mod support, and dual-display companions under `switch/`. |

Renaming packages and rewriting comments does not make derived code original.
The migration to `com.wemu.engine.*` was a rename, and the code underneath is
still theirs.

## Fetched at run time, not bundled

None of these ship in the APK. They are downloaded on demand, and this project
does not redistribute them.

| Component | Licence | Notes |
|---|---|---|
| Wine / Proton | LGPL-2.1-or-later | The guest Windows environment. |
| Box64, FEXCore | MIT | x86 to ARM translation. |
| DXVK, VKD3D-Proton | zlib, LGPL-2.1 | Direct3D over Vulkan. |
| Mesa / Turnip, Adreno drivers | MIT and vendor terms | Guest graphics. |
| [gbe_fork](https://github.com/Detanup01/gbe_fork) (Goldberg) | LGPL-3.0 | The Steam API shim, fetched directly from a checksum-pinned upstream release. |

## Vendored in the native tree

| Component | Licence |
|---|---|
| PRoot | GPL-2.0 |
| adrenotools | BSD-3-Clause (git submodule) |
| PulseAudio | LGPL-2.1 |

## Dependencies

Declared in `gradle/libs.versions.toml`. The ones worth naming because the
project would not work without them, or because they are unusual:

| Dependency | Licence | Why it is here |
|---|---|---|
| [JavaSteam](https://github.com/Longi94/JavaSteam) (`in.dragonbra`) | MIT | Speaks the Steam client protocol: CM login, licences, depot downloads. |
| [Legendary](https://github.com/derrod/legendary) | GPL-3.0 | Public interoperability reference for Epic's cloud-save manifest, chunk and upload protocol. No Legendary source is bundled. |
| [Heroic Games Launcher](https://github.com/Heroic-Games-Launcher/HeroicGamesLauncher) and [heroic-gogdl](https://github.com/Heroic-Games-Launcher/heroic-gogdl) | GPL-3.0 | Public interoperability references for GOG's game-scoped authentication, cloud remote-config schema, save-storage paths and gzip/MD5 wire protocol. No source from either project is bundled. |
| Bouncy Castle (`org.bouncycastle`) | MIT | Supplies JavaSteam's SHA-1/AES/PKCS7 algorithms through Wemu's `SC` provider adapter while leaving Android's TLS-critical `BC` provider intact. |
| zstd-jni (`com.github.luben`) | BSD-2-Clause | Unpacks every `.tzst` payload. |
| XZ for Java (`org.tukaani`) | Public domain | Archive support. |
| [7-Zip-JBinding-4Android](https://github.com/omicronapps/7-Zip-JBinding-4Android/tree/875f38aac441f41e6eb693177e020e97971dca97) | LGPL-2.1-or-later; bundled RAR decoders additionally carry the unRAR restriction described in the upstream [licence](https://github.com/omicronapps/7-Zip-JBinding-4Android/blob/875f38aac441f41e6eb693177e020e97971dca97/License.txt) | Native arm64 decoding for upstream 7-Zip archives that use BCJ2 multi-stream filters. The unRAR-derived source may not be used to recreate the proprietary RAR compression algorithm. |
| junixsocket (`com.kohlschutter.junixsocket`) | Apache-2.0 | Unix domain sockets for the guest bridge. |
| ZXing (`com.google.zxing`) | Apache-2.0 | The Steam sign-in QR code. |
| Coil (`io.coil-kt`) | Apache-2.0 | Cover art. |
| AndroidX, Compose, Kotlin, Material | Apache-2.0 | The interface. |

## Original to this project

Not a claim that these owe nothing to what they replaced — most of them exist
because something in the upstream did the same job differently — but they were
written here:

- the Compose interface, the design system port from Loki, and the theme engine
- the dual-screen session console
- `session/desktop/` — the container desktop, file explorer, start menu, and the
  hosted replacements for the guest's built-in programs
- `WineVisualStyle` and `WineIconTheme` — a Windows visual style and icon set
  generated on the device from the launcher's own palette
- the preset system and its server under `server/`
- the Vulkan compositor's frame path, upscalers and post-processing

## Combined-work licence

Dualscreen Launcher is distributed as a GPL-3.0-or-later combined work. See
[LICENSE](LICENSE). Individual third-party components retain the copyright and
licence notices recorded here and in their source trees.
