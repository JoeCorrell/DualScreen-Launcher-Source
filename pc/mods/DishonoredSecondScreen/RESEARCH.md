# Dishonored integration evidence

## Device and executable

The user confirmed wireless ADB after the initial USB probe found no USB device.
The working endpoint was `100.83.63.70:46665`. A read-only recursive inventory
is retained at `.research/dishonored/device-inventory.txt` with byte sizes,
timestamps and full paths. The game includes base content and DLC trees.

The executable fingerprint is recorded in README.md. `.work/dishonored/pe.json`
contains its sections, imports and image metadata. The executable imports only
DirectInput8Create from DINPUT8.dll. Native-script binding names survive in the
image and can be traced to registration records and native wrappers. Package
decompilation alone does not establish the ABI or prove a wrapper executes.

## Initial package evidence

DishonoredGame.upk has 24,432 names, 30,049 exports and 652 imports. Its cooked
version is 801/30, engine 9411, cooker 133. Its 16 LZO chunks expand to 17,320,971
bytes. Core, Engine and GFxUI package metadata are also available locally.

The package identifies these real native functions:

| Owner | Function | Serialized parameters |
| --- | --- | --- |
| DishonoredPlayerController | Dis_EquipItemByType | `_Usage` (EDisEquipUsage byte), `_pItemClass` (Class<DishonoredInventoryItem>) |
| DishonoredPlayerController | Dis_SelectPower | `_iPower` (int) |
| DisGFxMoviePlayerJournal | EquipBoneCharm / RemoveBoneCharm | `_ItemIdx` (int) |
| DisGFxMoviePlayerJournal | Req_ObjectivesList | none |
| DisGFxMoviePlayerJournal | Req_Logs_All / Req_Logs_Audio / Req_Logs_Notes / Req_Logs_Books | none |
| DisGFxMoviePlayerJournal | Req_CharmsTabContent | none |

These are tracing entry points, not permission to manufacture a journal object
or invoke a menu-owned callback while its dependencies do not exist. The native
semantic operations underneath them must be traced and validated independently.

DishonoredPlayerPawn exposes `m_Mana`, `m_ManaMax`, `m_pPowersComponent`,
`m_WhaleBoneCharms`, `m_Upgrades`, `m_StatValues`, `m_MissionStatValues`, and
`m_pKeyRing`. Array element types include DisCollectedWhaleBoneCharm and
DisMissionStatValue. Serialized property names do not establish runtime offsets.

The first lazy UELib dump contained incomplete child initialization (`@NULL`
types and missing parameter declarations) and errors while interpreting default
objects. The separate function-metadata pass correctly recovered Parm flags.
Do not treat an incomplete textual decompilation as an exact native structure.

## Original presentation resources

The installation contains DisFonts_SF, UI_Journal_SF, UI_HUD_SF,
UI_PowerWheel_SF, UI_MissionStats_SF, numerous UI_Map_*_SF packages, localized
notes/objectives/tweaks, and original UI sound banks. Their presence proves
asset availability only: it does not prove player ownership, discovery, world
coordinate calibration or that an off-screen map renderer exists.

## Required acceptance

- Exact-build bootstrap and transparent original DLL forwarding.
- Validated game-thread hook, runtime object layouts and world/player lifetime.
- Live HP/mana, owned inventory/ammunition/upgrades, powers/runes and selection.
- Acquired objectives/journal/charms and native equip/remove operations.
- Game-owned mission statistics with their actual definitions and availability.
- Owned map imagery, verified world-to-map transform and discovery predicates.
- Native-looking seven-page touch UI, isolated multitouch and controller focus.
- Save/load, death, area/mission transitions, cutscenes, shutdown and stale actions.
- Baseline-versus-mod frame time, resource lifetime, bounded logs and crash checks.
- Independently downloadable payload and transactional launcher installation.

No live integration or gameplay acceptance has passed yet. No game files,
configuration, saves, or executable code have been modified on the device.

## External tool references

- Unreal-Library: https://github.com/EliotVU/Unreal-Library
- UE Explorer: https://github.com/UE-Explorer/UE-Explorer

External examples are research leads only. The installed binary/package remains
the authority for this mod's field layouts, behavior and compatibility gates.
