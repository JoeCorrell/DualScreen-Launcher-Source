# AC II native integration checkpoint

Status: development; a read-only native adapter is implemented, but loaded-save
validation is pending. Native actions, live maps, mission/database adapters and
the Ezio render path remain incomplete. This is not a finished live companion.

## Device test handoff (2026-09-05)

### Controller compatibility follow-up

The user successfully launched AC II but reported keyboard/mouse-only input.
They own device testing and requested no device control. This follow-up performs
local source/build work only and includes the current Pokemon Legends Z-A host
loader changes in the APK.

The exact hashed retail executable's callback at `009c9540` creates a DirectInput
device and queries `DIPROP_VIDPID` before choosing its native Xenon/XInput path.
The Microsoft match is `045e:028e`; the original fake evdev device advertises
`1234:5678`. Skyrim's XInput-only profile also hides the generic DirectInput view.
Independent static inspection confirmed these contracts after consulting
[EaglePatch's controller research](https://github.com/Sergeanur/EaglePatch/blob/master/EaglePatch/src/ac2.cpp);
no third-party patch code or binary is incorporated.

`00412ca0` reads `[Input] SelectedInput` and matches its string against enumerated
device names before calling `009c8de0`. `004129b0` constructs the preferences path
from CSIDL_APPDATA plus `Ubisoft/Assassin's Creed 2/Assassin2.ini`. The preferences
writer at `004139d0` saves that same name.

The host now opts only AC II into an Xbox 360 evdev identity/name, enables both
DirectInput discovery and XInput on its shortcut, and selects that device in its
INI. Other sections and bindings are preserved, with a one-time original INI
backup. Skyrim's shared container defaults remain unchanged. This is generic
host controller compatibility; the AC II DLL stays in its separate payload.
Controller response, Wine's resulting device name, and button/axis mappings still
need the user's live test; static analysis and a successful build are not runtime
validation. Private decompilation evidence: `.work/ac2/controller.txt` and
`.work/ac2/input-config.txt`.

Handoff build: `1.6.17.9-ac2-controller-plza` / versionCode 38, release-signed with
the same certificate as the installed launcher. All 168 launcher unit tests pass
(5 AC II configuration cases; 7 module code-cache cases). Release assembly and
APK audits pass: the supplied AC II artwork is pixel-identical in the APK, the
controller identity and preferences paths are present, the Z-A ART profile fix
is present, and AC II/Z-A game payload implementations remain external. The
existing native Z-A host library and other games' player assets are preserved.

Manual-install APK: `C:/Users/Shadow/Downloads/DualScreen-1.6.17.9-AC2-PLZA.apk`,
331572972 bytes, SHA256
`7a885f2cbf19d073ed7aea1fc3ddca5971affc1f5a6721347d9c235a884f5a17`.
No ADB, device control, installation or game launch was performed in this follow-up.
The user should install the APK over the launcher and reopen AC II for the live
controller test. No new game ZIP import is required for this host update.

The user requested Skyrim's existing **Games container**, confirmed as container
1 in `com.dualscreen.launcher`, Proton 11 / FEX. AC II's original executable was
started from its existing `/sdcard/Games/Assassins Creed II` folder. Startup and
the title screen rendered. An external read-only probe successfully copied the
native manager roots. Launching through Skyrim's configuration session caused
focus conflicts; it is not the supported AC II launch path.

The user then requested direct access from the game list and took over testing,
using the most recently played save. The catalog now includes AC II as beta,
with exact executable validation and an executable-scoped dinput8 override.
`register_device_test.py` registers those existing files against Skyrim's actual
container ID and installs the separate `2026.09.05-dev.2` package. It does not
launch the game. Unverified save paths are not migrated or imported.

The package stays `development`; a hash-specific app-private test opt-in enables
only that locally installed package. Public downloads still require `ready`.
Delete `files/game-payloads/assassins-creed-ii/development-package` to remove the
test pin when a validated release is available. No release has been uploaded.

`NativeBridge.cpp` atomically replaces the verified PeekMessageA import at
`01ca332c`. Only empty-queue returns at `00b06a5b` / `00b06a91`, inside the traced
Scimitar pump `00b06a40`, qualify. Sampling is confined to the first qualifying
thread, reentrancy guarded and throttled to 250 ms. No gameplay calls or actions
are issued. `NativeRead.h` follows the native world/player selection and reads
aggregate collectible groups using `01123210`'s completion-state rule. Failed
reads, unresolved references and changed roots discard state. The four candidate
counters are feathers, codex pages, statuettes and Assassin seals; comparison
against a loaded game's menus is still required.

Additional private decompilations: `live-roots.txt`, `inventory-counts.txt`,
`game-tick.txt`. Native lifecycle/reader/render tests pass. The launcher builds,
and all 15 selected catalog/package JUnit tests pass. The independent panel and
package checks also pass (14 tests). No gameplay, transition or FPS result is
claimed for this development adapter.

## Exact executable

The device game is `/sdcard/Games/Assassins Creed II/AssassinsCreedIIGame.exe`.
It is i386, preferred base `00400000`, 34,693,768 bytes, SHA-256
`23a73e5f2d035973ebe4afc056e5c56bad982d27142294fa44e7cd11732c8b92`.
All addresses below are **preferred VAs**, not process pointers or RVAs.
Do not transplant them to another executable revision.

## Native systems identified

| System | Static evidence | Remaining proof |
| --- | --- | --- |
| LogicalInventory | Factory `010d41a0`, construction wrappers `010d4100` / `010d3f30`, vtable `01e57e40` | Owning entity and game-thread lifetime |
| Inventory UI | Get dispatcher `012da3f0`; original menu command strings resolve to calls below | Calling convention including ECX, output string ownership and context lifetime |
| Weapon statistics | `InventoryWeapons` reaches `012d9f70`, resolves current equipment via `012d9190` | In-game comparison for each weapon/category |
| Consumables / possessions | `012da010` emits quantity/capacity; `012da050` emits count; both use `012d9720` | Native ID translation, capacities, progression checks across saves |
| Ownership/lock status | `InventoryIsLocked` reaches `012d96c0`; current equipment resolver `012d9190` checks inventory services | Counterintuitive lock polarity and special categories need runtime tests |
| Cape selection | Send dispatcher `012d9ac0`; `InventoryEquipCape` reaches `012d9510`, then `010b3750` / `010b3c30` | Preconditions, game-thread execution, costume/Altair special case and preview invalidation |
| DNA | `MainSequenceCount` in dispatcher `01597580`, helper `015965c0` | Sequence context construction, locked-memory filtering, replay safety |
| Map manager | Factory `012aba80`, constructor `012ab680`, vtable `01e7682c`; constructor registers 12 event callbacks through `00530f40` | World component lifetime, event thread, discovery and level-generation validation |
| Map UI | `MAP_MARKER_PLAYER_POS` in dispatcher `012a2270`; marker-count query rebuilds its list through `012a1ad0` and native events | Independent context without opening original map, fog/district masks, marker/icon semantics |
| Player / inventory root | Call chain includes `004654d0` and `00948dc0` | `004654d0` requires an ECX owner; it is **not** a context-free singleton getter |
| Character preview | Original Ezio resource streams found and decoded | Isolated native render target, matching equipment/animation, camera/device restoration |

Targeted Ghidra output is under `.work/ac2`: `targeted.txt`, `constructors.txt`,
`objects.txt`, `map-callbacks.txt`, `command-handlers.txt`, `inventory-services.txt`.
These files contain the user's decompiled game and remain ignored research data.
The full-program analysis was interrupted; the targeted outputs completed.

## Original Animus interface recovered

`DataPC_extra.forge` contains 1,205 named entries, including native inventory
menus, map/collectible icons, database/codex images and DNA menu resources.
Three entries were decoded for inspection:

- `Animus_InventoryWeapons`: UEF v8 Flash-compatible menu plus original icon textures.
- `Logo_DNASequence`: DNA menu and shared fonts, including `AC2Aaux_ProLight_Latin`.
- `ItemFeather_MapDesc`: original 256×128 feather illustration; its DXT3 export
  was visually checked. Texture data starts at a header-declared block, not the
  end of the resource (which also contains a trailer).

`extract_uef.py` writes a separate research copy with an SWF signature, allowing
JPEXS to decompile the ActionScript. The original game archives are not modified.
The recovered menu's `Classes.Animus.Communicator` names the real FSCommand2 API.
Its Get calls return strings/numbers through `fscommand2Var`; Send calls perform
actions. These are native command names, not an implemented external API.

The original `EquipmentInfo` constants describe UI categories: armor 0–3,
small/normal weapons 4–5, blade variants 6–8/27/28, fists 9, reloads 10–14,
collectible categories 15/17–21/38, capes 22–26/33/39, and shop categories 29–32.
They are not physical item IDs. Original weapon popup menus mostly inspect
equipment; their existence does not prove a safe general equip action.

`DataPC.forge` has 72 entries. Bootstrap, Game Fix and Ezio streams contain
5,098, 10,698 and 588 indexed resources respectively. English localization was
decompressed, but its internal text encoding is not decoded yet.

## Reproducible local checks

Run PowerShell scripts with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`:

1. `build.ps1`: compile and run C++ lifecycle/touch/navigation tests, render six
   1240×1080 pages, build the i386 proxy DLL and verify native dependencies.
2. `tools/test_launcher_contracts.ps1`: low-memory standalone compilation of
   the package loader and on-demand manager; JUnit package/routing tests.
3. `python -m unittest discover -s tests -p "test_*.py"`: archive/resource bounds.
4. `tools/audit_native.py <exe> --output <private-json>`: exact hash plus native
   command references and call-edge checks. This is static evidence only.
5. `tools/package_payload.py --dll build/release/dinput8.dll --output build/payload`:
   reproducible development ZIP; no original game assets included.

The focused independent checks pass (13 JUnit tests), as do the native build,
render/input tests, three archive tests and four static native anchor checks.
The full launcher Kotlin compilation also passes; its log is
`.work/ac2/launcher-compile.log`. The earlier full Gradle test run was interrupted;
it is not claimed as a successful full test-suite run.

## Delivery and runtime status

Android recognizes only the exact private AC II panel class/title, claims it
for the lower screen, and suppresses synthetic guest mouse fallback. Controller
gameplay routing is unchanged. The native panel uses loopback UDP 7788, bounded
packet parsing, a non-activating window and one offscreen 1240×1080 buffer.

`OnDemandGamePayloads` fetches `<game-id>-payload.json` from the existing trusted
payload release before install/launch. ZIP size/hash, per-file hashes, paths,
duplicates, expanded size and manifest version are checked. The store lives in
`files/game-payloads/<game-id>/<zip-sha256>`; an atomic pointer selects a version.
The installer then applies its normal verified file overlay. Prior versions are
retained. No AC II DLL/assets go in the APK.

The generated descriptor remains `development`; ordinary remote downloads accept
only `ready`. The explicit local device-test exception is documented above.
The DLL is installed separately from the APK. Publishing is premature until
live synchronization and device tests pass.

## Next required work

The user owns the next device test, using the most recently played save. Validate
the initial read-only adapter against that save and prove native action scheduling
and object creation/destruction guards. Then connect inventory/player data, discovery-filtered map contexts,
DNA/database/progress services and an isolated Ezio preview. The UI now includes
scrolling equipment categories, inventory/database details, database categories,
all provided progress rows and a replay action gated by native eligibility.
Remaining work includes native icons/fonts and accurate data/action adapters. Test
loading, city/cutscene/mission transitions, purchases, pickups and reloads on
device before setting any release-ready status. Measure FPS/frame-time impact.
