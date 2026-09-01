# Fallout 3 device reconnaissance

Date: 2026-08-29
Device: AYN Thor (`kalama`)
ADB endpoint supplied for this investigation: `100.83.63.70:34023`
Scope: read-only discovery. No process was launched or stopped and no device,
container, game, registry, or configuration file was changed.

## Evidence status

The earlier probe directly enumerated the game tree and WEMU application-private
container data. The endpoint was temporarily offline during the first follow-up,
then became reachable again. The latest pass directly inspected the newest
Fallout launch, its WEMU diagnostic, logcat chronology, registry override,
deployed proxy identity, and post-session process state.

No proprietary Fallout executable or asset is retained in this repository.
Temporary executable copies used for local PE metadata inspection during the
earlier probe were outside the workspace and were not found during follow-up.

## Installation discovery

The successful probe searched Android shared Downloads storage, WEMU's managed
game area, and the accessible WEMU container prefixes. It found one actual
Fallout 3 game installation:

| Role | Android path | Result | Confidence |
|---|---|---|---|
| Installed game root | `/storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3` | Present; contains the game executable and `Data` tree | High, direct ADB enumeration |
| WEMU managed games | WEMU-managed `steam_games` area | No second Fallout 3 game tree found | High at probe time |
| WEMU Wine prefix | `/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine` | User/configuration prefix, not another copy of the game | High, direct `run-as com.wemu` inspection |

The game is reached inside Wine through:

`F:\Download\Fallout-3-GOTY-SteamRIP.com\Fallout 3\Fallout3.exe`

The Android and Wine paths resolve to the same shared-storage installation;
they must not be counted as two installations. Container drive mapping
evidence identifies `F:` with the storage root and `D:` with Downloads.

No device-side game or executable was copied into the project. The distribution
name is recorded only because it is part of the exact on-device path and is
needed to make deployment/path checks deterministic; it is not endorsed and
must never be bundled.

## Executable identity and launch target

Two different `Fallout3.exe` files exist in the installation. They are not
interchangeable:

| Property | Configured root executable | Bundled compatibility copy |
|---|---|---|
| Relative path | `Fallout3.exe` | `_Downgrader to v1.7.0.3/Fallout3.exe` |
| Size | 16,855,040 bytes | 15,044,024 bytes |
| SHA-256 | `c3f97c2255fa041a851c17cf372d69aaadd8694e2dc4230ba556001bbfbd2f3e` | `ae058c497069b8334711945cf646968b7fe8da483cce94022fc924fa0187e809` |
| File/product version | `1.7.0.4` | `1.7.0.3` |
| PE type | PE32, i386 | PE32, i386 |
| Preferred image base | `0x00400000` | `0x00400000` |
| Link timestamp | 2021-05-21 19:47:34 UTC | 2009-06-23 15:15:23 UTC |
| PE DLL-characteristics | `0x8140` (includes ASLR/NX) | `0x0000` |
| Signature result at probe | Not signed | Valid Authenticode signature |
| D3D import evidence | `d3d9.dll!D3DPERF_SetOptions` | `d3d9.dll!D3DPERF_SetOptions` |
| D3DX import | `d3dx9_42.dll` | `d3dx9_38.dll` |
| GFWL import | No `xlive.dll` import | Imports `xlive.dll` |

WEMU's historical launch diagnostic explicitly names the root `1.7.0.4`
executable at the full `F:` path. It also records one attempt involving
`FalloutLauncherSteam.exe`, but the launcher is not the configured gameplay
binary. The `1.7.0.3` file is an analysis and compatibility reference only.
The mod must not select it, replace the root executable, perform a downgrade,
or change any DRM behavior.

Confidence that the configured target is the root `1.7.0.4` file is **high**
for the observed launch attempts. Confidence that it is the executable in a
currently successful long gameplay session remains **medium** until a fresh
launch reaches a loaded save and the process command line/module hash are
captured together.

## WEMU package, prefix, and container

| Property | Observed value | Confidence/evidence |
|---|---|---|
| Android package | `com.wemu` | High, package inspection |
| WEMU version | `0.1.0` debug | High, package inspection |
| `run-as` access | `run-as com.wemu` succeeds | High, direct read-only probe |
| Container | ID 2, name `Games` | High, container metadata |
| Prefix root | `/data/user/0/com.wemu/files/imagefs/home/xuser-2` | High, direct private-tree inspection |
| Wine prefix | `/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine` | High |
| Second display | `displaySecondScreen=1` in container extra data | High |
| Guest desktop | `1280x1800`; live RandR `THOR-TOP 1280x720+0+0`, `THOR-BOTTOM 1240x1080+0+720` | High; observed in latest session |
| Game viewport | `1280x720` | High for configured value |
| Display renderer | `surfaceflinger` | High |
| Proton package | `Proton-11.0-5-arm64ec-6` | High for container configuration |
| Box64 package | `0.4.2` | High for container configuration |
| FEX configuration | FEXCore/PERFORMANCE, CPU affinity `3,4,5,6,7` | High for latest launch environment |
| DirectX wrapper | `DXVK-3.0-1-gplasync-0` | High for configured value |
| Vulkan driver | Turnip `26.2.0` | High for configured value |

Recent initialization records show WEMU loading Proton 11 ARM64EC, Vulkan,
Turnip, and DXVK components for attempts from the `Games` container. Those
records prove setup/launch attempts, not a completed load into gameplay.

Two other profiles appeared in historical attempts and must not be confused
with the intended baseline:

| Prefix/profile | Observed runtime | Assessment |
|---|---|---|
| `xuser-1`, `Steam - Proton 11` | Proton `11.0-1-x86_64-1`, Box64 `0.4.4`, Vulkan/DXVK, 60 FPS/FIFO settings | One Fallout attempt; not the configured `Games` baseline |
| `xuser-3`, `Deus Ez` | Proton 9 ARM64EC, FEXCore INTERMEDIATE, older DXVK configuration | One Fallout attempt; not the configured `Games` baseline |

Before version-specific hooks are enabled, a live proof must log all of the
following in the same session: prefix path, runtime path, translator,
`Fallout3.exe` command line, loaded main-module size/hash, D3D9 provider, DXVK
version, and successful transition into normal gameplay.

## Fallout configuration and user data

The Fallout user configuration directory found in container 2 is:

`/data/user/0/com.wemu/files/imagefs/home/xuser-2/.wine/drive_c/users/xuser/Documents/My Games/Fallout3`

Observed contents relevant to reconnaissance:

| Entry | Probe result | Meaning |
|---|---|---|
| `FALLOUT.INI` | Present | Base/generated configuration available to the prefix |
| `FalloutPrefs.ini` | Not present | Preferences had not been generated or retained at probe time |
| Saves | No save found in this user directory | Does not establish whether saves exist in a different mapped/user path |
| FOSE log | Not present | No evidence of an active FOSE bootstrap in this prefix |

The observed `FALLOUT.INI` had the game/default timestamp. It exposes useful
vanilla Pip-Boy presentation parameters, including:

- Pip-Boy RGB `(26, 255, 128)`.
- `fScanlineScalePipboy=50`.
- Pip-Boy scanline and flicker effects enabled.
- Brighten factor `1.3`.
- Blur radius `3.5` and blur intensity `0.25`.
- Interface font declarations for Glow Monofonto/Monofonto variants.
- A radio update interval of 250 ms.

These are configuration facts, not permission to copy Bethesda fonts,
textures, or other proprietary assets into the mod. The custom interface can
use compatible original artwork/code and read user-owned assets at runtime.

## Data and DLC inventory

The probe found the base master plus all five official Fallout 3 DLC master
files in the installed `Data` tree:

- `Fallout3.esm`
- `Anchorage.esm`
- `ThePitt.esm`
- `BrokenSteel.esm`
- `PointLookout.esm`
- `Zeta.esm`

`Data/DLCList.txt` lists exactly the five DLC ESMs (all entries above except
`Fallout3.esm`). This proves the files are installed; it does not yet prove the
active load order for the save that will be used in runtime testing.

The data scan also saw unusual `.bsa.x5` siblings associated with some DLC
archives. Their producer and runtime role are still unknown. Archive loading
must be observed before assuming those files can be ignored, renamed, or
handled like ordinary BSA files.

No FOSE loader/runtime files, `Data/FOSE` plugin tree, third-party ESP, or known
native gameplay DLL mod was found in the inspected top-level/mod paths. No
existing Fallout 3 second-screen plugin or dedicated second-screen log was
found. These are high-confidence absence results for the paths inspected, not
a claim that every file in every archive or inaccessible Android directory was
classified.

## Display evidence relevant to the mod

At the successful probe, Android exposed:

| Display | Native mode | Active logical orientation | Refresh/role |
|---|---|---|---|
| Display 0, built-in | 1080x1920 | 1920x1080 landscape | 60 Hz active, 120 Hz supported; primary |
| Display 4, `Screen-2` | 1080x1240 | 1240x1080 landscape | 60 Hz active, 120 Hz supported; presentation/external touch |

The container's `1280x1800` metadata is consistent with a guest layout using a
1280x720 top viewport stacked above a 1280x1080 lower viewport, but this is not
yet a live RandR fact. The actual Fallout X session must be queried after launch
before hard bounds or coordinate transforms are committed.

## Launch evidence (2026-08-29 EDT)

ADB was available again at `100.83.63.70:34023`. The newest Fallout attempt was
inspected read-only from the complete Android log buffer, WEMU's current guest
diagnostic, the live process table, the prefix registry, and the game directory.
No process was launched, stopped, signalled, or modified during this inspection.

### `Games` / `xuser-2` event sequence

| Device time (EDT) | Evidence |
|---|---|
| `22:06:48.443` | Android starts WEMU `SessionActivity` for the second attempt. |
| `22:06:48.644` | `ProcessHelper` reports successful Wine desktop creation, PID `31780`. |
| `22:06:57.317` | `THOR/Session` records the exact start request: `F:\Download\Fallout-3-GOTY-SteamRIP.com\Fallout 3\Fallout3.exe` in container `Games`. |
| `22:06:58.531` | Linux/Wine process `Fallout3.exe`, PID `31893`, is executing Wine's ARM64EC `winevulkan.so`. This proves process creation, not merely a requested command. |
| `22:06:58.585` | The process loads the configured custom Turnip driver from WEMU's `turnip26.2.0` component. |
| `22:06:58.695` | Mesa initializes for the Fallout process. |
| `22:06:58.882` | WEMU's `ASurfaceRenderer` reports `Foreground program windows absent; waiting for handoff`. |
| `22:07:03.539` | Final modification time of `no_backup/diagnostics/last-guest-session.log`; its last line is `Process set as DPI aware`. |
| `22:07:13.883` | `ASurfaceRenderer` reports `Foreground program handoff window expired`; no foreground Fallout window was adopted. |
| `22:08:20.437` | WEMU resets and deletes its fake-input endpoint as session teardown begins. |
| `22:08:20.440` | WEMU explicitly sends `SIGKILL` to Wine desktop PID `31780`. |
| `22:08:20.443` | `ProcessHelper` reports PID `31780` exited with status `137` (`128 + SIGKILL`). |
| `22:08:20.959`-`22:08:20.966` | X client count drains from eight to zero. |

The game command preceded session teardown by **83.123 seconds**, and the Wine
desktop existed for **91.799 seconds**. These are **session/desktop** intervals,
not proof that `Fallout3.exe` survived for 83 seconds. The Fallout process is
directly proven through `22:06:58.695`; the two additional X clients then drop
back to the nine-client desktop baseline at `22:06:58.830`, followed by the
missing-foreground-window message at `22:06:58.882`. This strongly indicates
that Fallout itself exited within roughly 1.5 seconds of the start command while
the WFM desktop and wineserver remained alive. There is no retained direct
process-death record from which to calculate a more exact Fallout lifetime.

The later status-137 exit is not an unexplained game crash: the log explicitly
shows WEMU sending signal 9 to the desktop immediately beforehand. There is no
corresponding Android fatal exception, ANR, or native crash dump.

### Last confirmed game-rendering stage

When first captured, `last-guest-session.log` was 3,188 bytes (68 lines), modified at
`2026-08-29 22:07:03.539016766 -0400`. It confirms all of the following for the
actual `Fallout3.exe` process:

- DXVK `v3.0-gplasync` selected its x86 D3D9 path.
- `winevulkan.dll` exposed `vkGetInstanceProcAddr`.
- Vulkan found `Wrapper(Turnip Adreno (TM) 740 ())`, wrapper driver `26.1.99`.
- D3D9 device/vendor translation and depth-format mapping executed.
- Fallout reached `Process set as DPI aware`.

The log ends there. This places the strongest visible/window failure after D3D9
and Vulkan initialization but before WEMU observes a stable foreground game
window. Nine X clients remain connected during the wait, so the process tree did
not simply vanish at `22:06:58.882`.

### `dinput8` proxy/bootstrap evidence

The deployed file is present and byte-identical to the current local package:

```text
path:   /storage/emulated/0/Download/Fallout-3-GOTY-SteamRIP.com/Fallout 3/dinput8.dll
size:   3,399,263 bytes
mtime:  2026-08-29 21:52:33 -0400
SHA256: 2f35154fae6cee1a2ddb69697e73389568aabc1b1b0e025fcbf798ce1394df09
```

The active `xuser-2/.wine/user.reg` also contains:

```text
[Software\\Wine\\AppDefaults\\Fallout3.exe\\DllOverrides]
"dinput8"="native,builtin"
```

`Fallout3.exe` itself contains both `DINPUT8.dll` and `DirectInput8Create`, so
the import surface is relevant. Nevertheless, there is **no positive proxy
initialization evidence**:

- no `Fallout3SecondScreen.log` or previous log exists through depth four of
  the game directory;
- no Fallout/FOSE/dinput/second-screen log exists in the Wine user's tree;
- WEMU's guest diagnostic has no `[BOOT]` or `[PROXY]` entry;
- logcat has no `FO3DS`, `Fallout3SecondScreenPanel`, bootstrap, or proxy entry;
- no lower-panel window is observed before the renderer handoff expires.

The proxy source writes `[BOOT] Fallout 3 dual-screen bootstrap initialized`
from `InitializeProxy`, which is reached on the first forwarded `dinput8`
export. Its complete absence means proxy execution is **not proved** and is the
strongest current mod failure boundary: native DLL resolution/load or entry to
the first forwarded export, before runtime/panel creation. A log-open failure is
still a narrower alternative and must be ruled out with Wine loader diagnostics
or loaded-module inspection during a future already-running session; absence of
a log alone is not treated as definitive proof that Windows never mapped the
DLL.

At the read-only process check at `22:08:23.790`, neither `Fallout3.exe` nor
`FalloutLauncherSteam.exe` was running.

### Newer `test` / `xuser-3` attempt observed during inspection

While the read-only inspection was in progress, a new user-controlled WEMU
session started in a different container. It was not initiated or controlled by
this investigation. This new session replaced `last-guest-session.log`, so the
`Games` log metadata above records the already-captured prior contents.

| Device time (EDT) | Evidence |
|---|---|
| `22:12:43.981` | Wine desktop for container `test` is created as PID `1087`; prefix is `xuser-3`. |
| `22:12:53.716` | `THOR/Session` requests the same root `Fallout3.exe` in `test`. |
| `22:12:54.915`-`22:12:54.951` | `Fallout3.exe`, PID `1328`, executes Wine Vulkan and wrapper libraries. |
| `22:12:55.168` | X client count returns from 11 to the nine-client desktop baseline. |
| `22:13:45.745` | Full process table contains the Wine desktop/wineserver but no Fallout process. |

Thus the newest observed Fallout attempt lasted about **1.452 seconds** from
start request to the X-client return, with only **0.253 seconds** between the
first proven Fallout process record and that return. This is an observational
window, not a precise process lifetime.

This attempt cannot validate the installed proxy configuration: `xuser-3` has
no `AppDefaults\\Fallout3.exe\\DllOverrides` entry and no `dinput8` value in
`user.reg`; its final environment contains only
`WINEDLLOVERRIDES=winemenubuilder.exe=d`. It used FEXCore/INTERMEDIATE, DXVK
2.3.1, and a different Turnip A8XX-draft profile, so it must not be conflated
with the `Games`/`xuser-2` baseline or used to judge the xuser-2 override.

For later checks, use the full process table rather than `pidof` alone because
Wine process naming can make `pidof` incomplete:

```text
adb -s 100.83.63.70:34023 shell "ps -A -o PID,NAME,ARGS | grep -Ei 'Fallout3.exe|FalloutLauncherSteam.exe' | grep -v grep"
```

This is a read-only verification command; it must not be followed by an
automatic launch, kill, deployment, or input injection.

## Confirmed implementation constraints from the target

1. Build for a 32-bit PE host and version-gate the `1.7.0.4` executable.
2. Do not reuse offsets from `1.7.0.3` without signatures and runtime proof;
   the binaries differ materially in age, size, mitigations, D3DX, and GFWL
   imports.
3. D3D9 interception is technically plausible because both binaries import
   `d3d9.dll`, but app-local proxy behavior under this exact Wine/DXVK setup
   must be tested before selecting it as the permanent bootstrap.
4. Never replace or downgrade the game executable and never bundle game/DLC
   assets.
5. Treat `Games`/container 2 as the target only after a successful-session
   launch proof confirms the configured runtime was actually used.
6. Keep lower-display rendering and input independent from Fallout's gameplay
   camera and main D3D9 render target.

## Required follow-up

These unknowns still block a completed Phase 1 sign-off:

1. During a future user-started session, inspect `/proc/<Fallout-pid>/maps`
   read-only while Fallout is alive to prove whether the game-local PE32
   `dinput8.dll` is actually mapped. Temporarily enabling targeted Wine loader
   diagnostics requires a separately authorized diagnostic build/config change;
   it was not done during this read-only inspection.
2. Re-enumerate the game root and hash the root executable on-device to detect
   changes since the earlier probe.
3. Capture exact top-level filenames/sizes and `Data/DLCList.txt`, then inspect
   current plugin/load-order files without modifying them.
4. Enumerate the complete Fallout user directory, including alternate Wine
   user names and save redirections, to resolve the missing preferences/saves
   question.
5. Record container 2's current `.container`, DOS-drive symlink targets,
   registry DLL overrides, Proton path, and renderer environment in one
   timestamped evidence bundle.
6. If a game session is already running, capture live RandR monitors and D3D9/
   DXVK module mappings. Do not launch the game solely for reconnaissance.
7. Pull only small text configuration/log files to
   `artifacts/fallout3-recon/device`; do not retain executable or asset content
   after metadata extraction.

## Confidence summary

| Finding | Confidence |
|---|---|
| One shared-storage Fallout 3 installation at the recorded Downloads path | High |
| Root executable identity is Fallout 3 `1.7.0.4`, SHA-256 recorded above | High as of probe |
| Historical configured launch target is that root executable | High |
| Bundled `_Downgrader` executable is `1.7.0.3` and is not the configured target | High |
| Container 2 / `xuser-2` is the intended WEMU `Games` environment | High |
| Proton/DXVK/Turnip values reflect container configuration | High |
| `Games` `Fallout3.exe` reached DXVK D3D9/Vulkan/Turnip initialization | High |
| `Games` desktop exit status 137 was caused by WEMU's explicit session-teardown SIGKILL | High |
| WEMU adopted a stable foreground Fallout window in the `Games` attempt | No; handoff expired |
| Newest `test` attempt used the intended `xuser-2` proxy override/runtime | No; it used unpatched `xuser-3` |
| Game-local proxy file and per-app override are installed | High |
| Proxy `InitializeProxy`/runtime/panel executed | Not proven; no bootstrap evidence found |
| Base game and all five official DLC master files are present | High |
| DLC active load order and current save dependencies | Unknown |
| No FOSE log or Fallout second-screen runtime log in inspected locations | High for inspected paths |
| Fallout running at the `22:13:45.745` process check | No; installers and the Wine desktop remained |
