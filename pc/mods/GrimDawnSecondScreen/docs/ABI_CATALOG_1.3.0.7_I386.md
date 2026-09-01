# ABI catalog — Grim Dawn 1.3.0.7 I386

This catalog is scoped to the installed binary hashes in the parent technical
investigation. RVAs are module-relative and are evidence/debug aids, not the
runtime lookup mechanism. Production resolves the exact decorated export name
with `GetProcAddress` after the three-file build gate.

None of the entries below is marked device-live merely because it exports a
name. Methods returning or accepting game-owned C++ containers remain disabled
until their exact ABI and lifecycle are exercised on the device.

## Roots and lifecycle

| Module RVA | Decorated export | ABI |
|---:|---|---|
| Game `0x8090A4` | `?gGameEngine@GAME@@3PAVGameEngine@1@A` | `GameEngine**` exported data |
| Engine `0x368534` | `?gEngine@GAME@@3PAVEngine@1@A` | `Engine**` exported data |
| Game `0x25A620` | `?Update@GameEngine@GAME@@QAEXH@Z` | `void __thiscall(int)` |
| Game `0x035D20` | `?IsGameEngineOnline@GameEngine@GAME@@QBE_NXZ` | `bool __thiscall() const` |
| Game `0x24A390` | `?IsGameLoading@GameEngine@GAME@@QBE_NXZ` | `bool __thiscall() const` |
| Engine `0x066A50` | `?IsRunningGameSimulation@Engine@GAME@@QBE_NXZ` | `bool __thiscall() const` |
| Engine `0x066A90` | `?IsInTeleportMode@Engine@GAME@@QBE_NXZ` | `bool __thiscall() const` |
| Game `0x269E90` | `?GetMainPlayer@GameEngine@GAME@@QBEPAVPlayer@2@XZ` | `Player* __thiscall() const` |

The main executable imports `GameEngine::Update` at IAT RVA `0x267104`.
The hook verifies that slot still equals the same `Game.dll` export, performs
one aligned compare-exchange, calls the original first, and reacquires every
root on every accepted sample. Boolean wrappers zero-extend `AL`; float/double
wrappers preserve x87 returns; aggregate wrappers explicitly implement the
MSVC I386 hidden-result convention.

## Character primitives

| Game RVA | Export | Result |
|---:|---|---|
| `0x056FF0` | `Character::GetCurrentLife` | `double` |
| `0x057110` | `Character::GetLifeLimit` | `float` |
| `0x056B40` | `Character::GetCurrentMana` | `float` |
| `0x056F50` | `Character::GetManaLimit` | `float` |
| `0x04B3D0` | `Character::GetCharLevel` | `uint32` |
| `0x04B490` | `Character::GetExperiencePoints` | `uint32` |
| `0x04CB00` | `Character::GetCurrentMoney` | `uint32` |
| `0x3205E0` | `Player::GetPlayerName` | `unsigned short const*` |
| `0x054FA0` | `Character::GetTotalCharAttribute` | `float(enum)` |
| `0x25CD00` | `GameEngine::GetGameDifficulty` | enum |

Exact `CharAttributeType` values recovered from named attribute construction
paths are Physique 1, Cunning 2, Spirit 3, Offensive Ability 21, and Defensive
Ability 23. Difficulty values are Normal 0, Elite 1, and Ultimate 2. The bridge
still range-checks the returned values before setting capability bits.

Armor and resistances should use the real `CombatAttributeAccumulator` path:
construct the accumulator, call `Character::GetAllDefenseAttributes`, query
`GetTotalDefenseType` / `GetTotalDefenseModifierType` / `GetTotalReduction`, and
always destroy it. It is not yet enabled because the I386 object size and every
sheet-presentation rule still require live comparison.

## Inventory, equipment, skills, Devotion

| Game RVA | Export | Result |
|---:|---|---|
| `0x04E100` | `Character::GetInventoryItems` | `mem::vector<uint32> const&` |
| `0x04B6A0` | `Character::GetSkillPoints` | `uint32` |
| `0x058500` | `Character::GetSkillList` | `mem::vector<Skill*> const&` |
| `0x04B510` | `Character::GetDevotionPoints` | `uint32` |
| `0x04B780` | `Character::GetTotalDevotionPoints` | `uint32` |
| `0x04B790` | `Character::GetMaxDevotionPoints` | `uint32` |
| `0x0DF000` | `Skill::GetResourceName` | `Name const&` |
| `0x3BFE50` | `Skill::GetSkillLevel` | `uint32` |
| `0x3C0C20` | `Skill::GetCooldownRemaining` | `int` |
| `0x3C0C40` | `Skill::GetCooldownTotal` | `int` |
| Engine `0x005490` | `Name::GetDigest` | `uint32` |

For this PE32 build `mem::vector` is `{begin,end,capacity}` using three 32-bit
pointers. Inventory IDs and Skill pointers are four-byte elements. A count is
accepted only when `begin <= end <= capacity`, byte distance is aligned, the
active range is readable, and the domain maximum is not exceeded. Inventory
count is item-object count, not total stack units.

Equipment is reached through `Character::GetCombatManager`,
`CombatManager::GetEquipManager`, and `EquipManager::GetItemId(EquipLocations)`.
The main EXE imports those at RVAs `0x267774`, `0x267770`, and `0x269720`.
Slot enum correlation and entity-ID-to-record resolution are not complete, so
the UI does not label an ID as a weapon or equipment item yet.

The normal safe mutation seams are also exported/imported—for example
`EquipmentCtrl::CanItemBePlaced` + `PlaceItem`, `PlayerInventoryCtrl::AddItem` /
`RemoveItem`, and hotbar activation—but the diagnostic milestone compiles no
mutating command path.

## World, Riftgates, quests, factions

| Module RVA | Export | Result |
|---:|---|---|
| Game `0x048D80` | `Character::GetPathPosition` | `WorldVec3` hidden-result |
| Engine `0x068D80` | `Engine::GetAreaNameTag` | `std::string const&` |
| Game `0x320B90` | `Player::GetTeleportUIDs` | `mem::vector<UniqueId> const&` |
| Game `0x35D3B0` | `Quest2Repository::GetQuest(uint32)` | `Quest2*` |
| Game `0x03B2E0` | `Quest2::GetId` | `uint32` |
| Game `0x35C1A0` | `Quest2::IsStarted` | `bool` |
| Game `0x35C230` | `Quest2::IsComplete(bool)` | `bool` |
| Game `0x263A60` | `Quest2::IsTracked` | `bool` |
| Game `0x047DC0` | `Character::GetFactionValue` | `int(enum)` |

I386 `WorldVec3` is 16 bytes: `Region*` followed by three floats. The exported
method writes it through one hidden stack result pointer and returns with
`ret 4`; treating it as four floats shifts all coordinates and is incorrect.
The bridge validates the Region pointer and XYZ before publishing.

`UniqueId` is 16 bytes. `GetTeleportUIDs` selects the current-difficulty known
list, so the bridge can safely publish a bounded real count. Names/locations
require correlating those IDs with the offline teleport catalog.

Quest enumeration will use an offline ID catalog and primitive `GetQuest(id)`
queries rather than passing an uninitialized `mem::vector<Quest2*>` into the
repository. Faction enum values identify internal `userN` slots; display names,
tiers, and unlock metadata must come from merged ARZ records.

## Exact main-executable IAT seams for future actions

| IAT RVA | Imported method |
|---:|---|
| `0x269234` | `EquipmentCtrl::PlaceItem` |
| `0x269724` | `EquipmentCtrl::CanItemBePlaced` |
| `0x269230` | `EquipmentCtrl::RemoveItem` |
| `0x2699A0` | `PlayerInventoryCtrl::AddItem` |
| `0x26928C` | `PlayerInventoryCtrl::RemoveItem` |
| `0x269C68` | `PlayerInventoryCtrl::DepositReagents` |
| `0x269918` | `PlayerHotSlotCtrl::ActivateHotSlot` |
| `0x269948` | `PlayerHotSlotCtrl::ActivateHealthPotionSlot` |
| `0x269950` | `PlayerHotSlotCtrl::ActivateManaPotionSlot` |
| `0x269ADC` | `Quest2::SetTracked` |

Future panel commands must be bounded, tagged with the current world epoch,
validated again on the game thread, and executed through the same can/execute
pair and game rules as the native UI. Joined multiplayer and loading/teleport
states will reject writes. No save-file mutation is planned.
