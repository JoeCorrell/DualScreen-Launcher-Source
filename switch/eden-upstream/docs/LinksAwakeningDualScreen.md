# Link's Awakening dual-screen investigation

Status: reverse engineering and a development-only launcher import/launch profile.
No working companion or native action hook yet; initial read-only gameplay
captures are available, not a completed/native-action validation.
Existing companions have not been changed by this work.

## Active target: base game 1.0.0 (no update required)

The user selected base-only development to match their existing FPS patch.
Verified base ExeFS was extracted from the complete 6,274,834,880-byte NSP now
in `/sdcard/Switch/`, using range reads and `complete-base-cache` (about 28 MB,
not a full NSP copy). Private outputs: `.work/la-audit/base-exefs/` and
`.work/la-audit/base-main.bin`.

- Full base main build:
  `ae16f71e002af8cb059a9a74c4d90f34ba984892000000000000000000000000`.
- Main NSO SHA-256:
  `20965546519a8e08cc01e6f138982390546d56520703684967425b09230e9b6d`.
- Text RVA 0, size `0x1433830`; rodata RVA `0x1434000`, size `0x71b3b8`;
  data RVA `0x1b50000`, size `0x146e40`; BSS size `0x431c0`.
- The user's `Stable-60fps-v2/exefs/1.0.0.pchtxt` declares the same build.
  This is a build compatibility check, not proof of performance/stability.
- Base Ghidra project is `LA-Base`, separate from the historical `LA` project.

Catalog recommendation is now 1.0.0. The user subsequently removed the installed
update and relaunched; base signatures now match the running executable.
No saves or game packages were changed by the diagnostic. Do not claim a
completed companion or install 1.1.0 hooks into 1.0.0.
The native code evidence below is **historical 1.1.0 evidence** unless explicitly
identified as separately checked in the base executable.

## Launch-test checkpoint

### Eden fixes and debugger checkpoint

Private release-signed launcher **1.6.16.1**, code 28, is now installed.
APK SHA-256: `1035042ad1f7ebec0f5606d7150ba9f3eab78d0cb615c223bf02dffaa62631e8`.
Includes driver selection, native setting controls, and mod-folder discovery/import
fixes; 129 launcher tests passed. No LA companion hooks are shipped.
The user reported getting past the previous wake-up freeze with these Eden fixes.

Debugger enabled on port 6543 after explicit restart approval. Current config
backup is `.work/la-audit/config-before-debugger-base.ini`; only debugger enable
and port-default fields changed. ADB forwards localhost 6543 to the device.
Handshake and read-only `monitor info` succeeded; `continue` was sent in `finally`
and subsequent game initialization activity appeared in the log.

The first debugger launch was still 1.1.0, despite selecting base-only development:
`base-debugger-launch.log` reports main build `909E904AF78AC1B8DEEFE97AB2CCDB51968F0EC7`.
The installed update must be disabled before base-game testing; absence of an
update NSP in the chosen source folder does not remove an installed NAND update.
The user subsequently removed the update; see the base captures below.

### Base 1.0.0 open/closed-menu captures

`probe_la_state.py` now targets only the verified base build and validates the
entire local decompressed image SHA-256 before connecting. Its three unique
32-byte instruction windows at `0x7e13e8`, `0xdfa948`, `0xdf8358` differ from
1.1.0 and matched live at main base `0x1a28306000`. No guest writes/breakpoints
were used; both bounded captures sent `continue` in `finally`.

Private captures: `.work/la-audit/base-inventory-map-open.json` and
`base-inventory-map-closed.json`; screenshot `la-map-inspection.png` shows the
native Mabe Village map, three full hearts and FPS 59.9 after resume.
The pulled log file for this session was empty; conclusions here come from
live debugger reads, the verified executable and the screenshot, not that log.

Independently checked base disassembly/decompilation confirms manager
`main+0x1cc1120`, inventory cell `+0x248`, flag pointer/count `+0x268/+0x270`,
inventory equipment bytes `+0xa2/+0xa3`, ownership mask `+0xa8`, and the native
flag bit-index lookup. Private base decompilation is in `base-decompiled/`.

Both menu-open and user-confirmed menu-closed captures have:

- Inventory pointer `0x1a29fc66e0`, flag pointer `0x1a29fc6fe0`, 256 flag bytes.
- Health 12 quarter-hearts, rupees 0, equipment bytes `ff ff`, ownership mask 2
  (shield), current dungeon sentinel `ff`.
- Set map flags `UI_FieldMapTraverse_MabeVillage` (index 1035) and
  `UI_FieldMapOpen_MabeVillage` (index 1075). This matches the visible native map
  and the native UiFieldMapMask row 11. Other named field-map flags were unset.

The pointers, inventory quantities/equipment, and full flag bytes persisted
unchanged after closing the menu. Some scene/locator/position-looking fields
changed: they are NOT yet proven to be live player coordinates (may be save or
respawn data). Do not publish those fields as live position without tracing.
This comparison alone establishes post-menu lifetime; the separate fresh-load
capture below checks initialization without opening either menu.
Five synthetic probe tests pass (build mismatch before object reads, wrong title,
uninitialized pointers, flag-count bounds, and field decoding). These tests do
not constitute gameplay validation of the future mod.

### Fresh-load capture with both menus never opened

Following the requested restart/load sequence, the user confirmed they were in
the world without opening inventory or map. Read-only capture
`.work/la-audit/base-fresh-load-no-menu.json` succeeded in about two seconds and
sent `continue` in `finally`. All three base-build instruction samples matched.

The new main base is `0x1a27df8000` (different from the previous process), with
inventory `0x1a29ab86e0` and flag buffer `0x1a29ab8fe0`, length 256. Addresses were
resolved again, not reused from the preceding session. Health 12 quarter-hearts,
rupees 0, equipment `ff ff`, shield ownership mask 2 and all 256 flag bytes match
the previous closed-menu capture. Mabe Village traverse/open flags are present.

This provides runtime evidence that the tested native inventory and discovery
buffers exist after fresh gameplay initialization without the vanilla menus.
It does NOT validate an equipment mutation API, live coordinates, every map
visibility condition, dungeon lifecycle, or any as-yet-unimplemented companion UI.

Private signed APK built at `.work/la-audit/Link-Awakening-launch-test.apk`:
SHA-256 `82153ac728768162eaa77cd28eeee2435f05901ad5079349986793f0cecce0f6`.
Adds the development launcher catalog entry only; 111 launcher tests passed.
The native emulator library is byte-identical to public v1.6.16. Eight synthetic
MSBT tests passed, and all 13 extracted English MSBTs (2,081 labels) parsed.
Installed privately; the user reached the intro, then reported a wake-up freeze.
Standalone Eden's working per-game settings were subsequently copied to the
launcher without changing global settings, saves or caches. No companion hooks
were present in that original APK. Debugger was disabled at that checkpoint;
the later explicitly authorized debugger session above is now enabled.
`probe_la_state.py` is a bounded read-only diagnostic. It validates title and
executable samples and requests resume in
`finally`. It must never be mistaken for an equipment action implementation.

## Inputs verified on 2026-09-04/05

- Device: `100.83.63.70:46665`, AYN Thor.
- Base title: `01006BB00C6F0000`; update package version: `131072`.
- Update main NSO build ID (full):
  `909e904af78ac1b8deefe97ab2ccdb51968f0ec7000000000000000000000000`.
- Display version **1.1.0**, read from the update control NCA's `control.nacp`
  at `+0x3060`; it must not be inferred from the package version.
- Update ExeFS successfully extracted by byte ranges using `extract_nsp_exefs.py`.
  Modules: main, main.npdm, rtld, sdk, subsdk0, subsdk1.
- Main decompressed text: RVA 0, size `0x1433900`; rodata:
  `0x1434000`, size `0x71b400`; data: `0x1b50000`, size `0x146e40`;
  BSS size `0x431c0`.
- Private extracted binaries and decompilation: `.work/la-audit/` (not release assets).
- Ghidra project: `C:/Projects/LA-Ghidra/LA`. Ghidra 12 rejects dotted project
  path components, including `.work`. Use `LaTrace.java`, no whole-image
  autoanalysis, one CPU, 1536 MB maximum Java heap for bounded investigation.

### Base package integrity problem

Device file:
`/sdcard/Switch/The Legend of Zelda Links Awakening [01006BB00C6F0000][v0] (5.84 GB).nsp`

Two independent metadata layers exceed the file's actual end:

- Actual length from device stat: **3,544,207,248 bytes**.
- PFS0 entry extents require **6,274,802,512 bytes**.
- Decrypted, valid `NCA3` program header independently declares
  **6,273,122,304 bytes** for its program NCA, beginning at package `0xf90`.
- Ticket declared at package `0x17601d990`, size 704: byte-range read returns zero.
- Program payload at NCA+`0x4000` does **not** begin with `NCZSECTN`.
  No evidence of a normally compressed NCZ container explains the mismatch.

The source may itself be incomplete even if its transfer was complete. Do not
delete, repair headers, or overwrite it. A correct base or another valid local
source is needed for asset extraction and reliable gameplay validation. Update
executable tracing can continue independently. Keys/tickets must remain private.

### Complete replacement verified

The user supplied `/sdcard/Download/The Legend of Zelda Link’s Awakening
[01006BB00C6F0000][v0].nsp`: **6,274,834,880 bytes**, exactly matching its
PFS0 entry extents. The program NCA is the same named base content, with a
different package data offset. Use `.work/la-audit/complete-base-cache`, not
the failed/short earlier cache. Ticket and RomFS metadata read successfully;
3,836 files were enumerated. No original package was changed or deleted.

Extracted selected datasheets, US English messages, `Game.arc`, and `DgnTex.arc`
by ranges (about 82 MB). `export_la_metadata.py` exports 83 item definitions,
856 global flag definitions, 23 field-map mask rows, 27 icons, 361 dungeon-area
rows, 10 warp points and native conditions/hints with source SHA-256 provenance.
Metadata remains private in `.work/la-audit/metadata.json`. Base resource contents
still need comparison with the running title's patched RomFS.

LA English MSBTs use **UTF-8**; the BOTW UTF-16 reader must not be reused unchanged.
`la_msbt.py` validates header/section/text bounds and skips control tags using the
encoding's actual marker width. `test_la_msbt.py` uses synthetic fixtures only.

## Historical 1.1.0 native code evidence (main-relative RVAs, not live addresses)

These are statically traced observations, **not runtime-validated hook contracts**.
Never call event-query wrappers as equipment mutation functions.

- `Inventory` string `0x16e803b` referenced by registration code at
  `0x80db50`; `EventFlags` string `0x16c2b40` referenced at `0x80db24`
  and `0xc937f4`.
- Query dispatcher at `0x819880` selects `HasItem` function-object vtable
  `0x1bc1328`; relative relocation of its invocation slot points to
  `0x819a70`, which forwards to `0x7e2b10`.
- `0x7e2b10` reads item ownership/quantities from the object at global cell
  `main+0x1cc1368`, including fields `+0x9a..+0xad`, collectibles bitfields,
  and per-dungeon entries. It does not require a menu object in this path.
  Actual item-name/enum associations must be corroborated with game data.
- The query named `EquipItem` selects vtable `0x1bc1298`; invocation
  `0x819b70` forwards to `0x7e3310`. **This is an equipped-item predicate,
  not an equip command.** It compares a converted item enum against bytes
  `+0xa2/+0xa3` in the same state object. X/Y ordering is not established yet.
- `0x7e2b10` uses current-dungeon byte `main+0x1cc13c8` (invalid sentinel
  `0xff`) and pointer-array cell `main+0x1cc11b0`; dungeon key count is byte
  zero, with several possession flags in byte one. Array capacity, ownership,
  lifecycle and flag identities need further tracing.
- Native map data paths found in executable:
  `datasheets/UiFieldMapMask.gsheet`, `UiFieldMapIcons.gsheet`,
  `UiFieldMapLocationName.gsheet`. Mask path callers at `0x8682a8` and
  `0x8736a0` are investigation leads, not yet discovery logic.
- `GetEquipedItemKind` string referenced at `0x9c5324` is another lead.
- `0x8d27e0` converts Items.gsheet item IDs to equipment enums using jump table
  `0x16b3354`: sword 0, shield 1, bombs 2, bow 3, hookshot 4, boomerang 5,
  magic rod 6, shovel 7, powder 8, feather 9, bracelet 10, boots 11,
  ocarina 12, bottle 13. Invalid/non-equipment returns `0xff`. Conversion alone
  does NOT prove X/Y assignability; swords/shields/passive gear must be excluded
  by the actual menu selection rules. Event query enums are a separate mapping.
- `0x8d2880` fills an empty equipment slot, verifies ownership and clears
  duplicate assignments. It is an **auto-equip** helper, not an arbitrary X/Y
  assignment API. Do not use it for a requested occupied slot.
- Menu assignment `0xebb060(menu, index, slot)` contains inlined ownership and
  X/Y swap behavior, then refreshes live HUD panels and sound; it dereferences
  menu entries at `menu+index*0x280+0x598` and reads ocarina selection at `+0x4028`.
  **Do not call it with a fabricated or absent menu.** Need a menu-independent
  native entry/validated instruction path. No guest writes are implemented.
- Native flag query `0xdfa920` (called by `CheckFlag` wrapper `0x7e13c0`) uses
  actual GlobalFlags definitions and a bit array at manager+`0x268`, bounded
  by byte count manager+`0x270`. Wrapper passes manager `main+0x1cc1120`.
  Thus flag storage cells are `main+0x1cc1388/0x1cc1390`. Name lookup object at
  `main+0x1cb24f0` must not be assumed initialized before gameplay.
- Max-health getter `0xdf8260` handles a special dungeon-mode override before
  counting heart containers/pieces. Do not use a flat guessed maximum.

## Native UI resource findings

`Game.arc` is a SARC with 1,097 members; its combined BNTX has 767 textures,
including ASTC and BC formats. The native map is composed from 23 `FldChip_*`
tiles, underlays, masks and BFLYT part transforms, **not one generic map image**.
`Gm_Map.bflyt` stores `L_FldArea_00..22` under `N_Map_01` with native scale
approximately 1/3; part-local sizes/transforms must also be applied. Do not
guess tile placement or substitute a screenshot. Local UI extract is
`.work/la-audit/ui/`.

## Integration boundaries

Reuse independent `DualScreenPresentation` routing and non-focusable touch window,
JNI snapshot access protected by `EmulationSession::LockSystemAccess`, build-ID
validation and asynchronous UI updates. Add a separate LA route and bridge;
do not alter BOTW-specific readers, payloads, maps or native Hold/Eat.

Mutation must execute on the appropriate guest game thread through a validated
native function/action path, not write the save fields above from Android.
No UI should advertise assignable items until the native assignment contract,
ownership validation, lifecycle guards and completion acknowledgement are known.
Discovery masks/markers must use the game's current save state, not a mod-owned
unlock list. Extracted map/icons are private local inputs, not automatically
authorized public release assets.
