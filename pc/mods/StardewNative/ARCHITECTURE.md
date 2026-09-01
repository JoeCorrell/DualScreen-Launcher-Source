# Stardew Valley native dual-screen architecture

## Compatibility boundary

The patch adds the internal `StardewSecondScreen.NativeBootstrap` type and two
private manifest resources to `Stardew Valley.dll`. It does not change the
assembly name, version, MVID, entry point, or any existing type/member
signature. The verifier compares the complete original API surface with the
patched assembly. Normal `Stardew Valley.exe` launch is supported, and SMAPI
and Harmony still see the original game API when `StardewModdingAPI.exe`
creates the patched `Game1`.

`NativeBootstrap` loads the embedded `StardewDualScreen.Native` assembly and
portable symbols from memory. There is no loose runtime DLL, SMAPI reference,
Android UI module, WebSocket client, or separate launcher.

## Lifecycle hooks

| Patched method | Callback | Responsibility |
| --- | --- | --- |
| `Game1.Initialize()` before return | `NativeBootstrap.Initialize()` | Load the embedded runtime, create the private lower-panel window, and initialize state tracking. |
| `Game1.Update(GameTime)` before return | `NativeBootstrap.AfterUpdate()` | Detect live state changes, drain validated UI actions on the game thread, and publish only invalidated topics. |
| `Game1.Draw(GameTime)` before return | `NativeBootstrap.AfterDraw()` | Render the lower panel with the game's MonoGame device and capture dynamic farmer art after the gameplay frame. |
| `Program.Main(string[])` before return | `NativeBootstrap.Shutdown()` | Release the panel, graphics resources, input socket, and restored HUD state. `ProcessExit` is a fail-safe. |

Every boundary is guarded so a panel failure is logged and disabled without
terminating vanilla or SMAPI-hosted Stardew Valley.

## State and game-system mapping

All pages read live save objects. No parallel world simulation is maintained.

| UI data | Live game source |
| --- | --- |
| Date, time, season, weather, gold, health, energy, luck, earnings | `Game1` date/time/weather and `Farmer`/`FarmerTeam` fields. |
| Inventory, tools, hotbar, quality, watering can, weapon cooldown | `Farmer.Items`, `CurrentToolIndex`, `MaxItems`, `Item`, `WateringCan`, and `MeleeWeapon`. |
| Skills and buffs | `Farmer` skill level/experience fields; the level properties already include active buff modifiers. |
| Relationships, birthdays, gifts, villager positions | `friendshipData`, live `NPC` objects, `NPCGiftTastes`, portraits, and `WorldMapManager`. |
| Journal and special orders | `questLog`, `Quest`, `FarmerTeam.specialOrders`, live objectives, rewards, and deadlines. |
| Community Center | `netWorldState.BundleData`, `CommunityCenter.bundles`, and `ItemRegistry`. |
| Crafting and cooking | Known recipe dictionaries and `CraftingRecipe`; crafting uses the game's ingredient, consumption, inventory, quest, achievement, debris, and sound paths. |
| Machines | Objects in every loaded location, `heldObject`, `MinutesUntilReady`, and `readyForHarvest`. |
| Animals and pets | `Farm.getAllFarmAnimals`, `FarmAnimal`, `Pet`, `lastPetDay`, and `PetBowl` buildings. |
| Crops and fruit trees | `terrainFeatures`, `HoeDirt`, `Crop`, and `FruitTree`; special forage crops use the same IDs as the game's harvest methods. Invalid empty item IDs are never passed to `ItemRegistry`. |
| Map and tracking | `WorldMapManager`, `MapAreaPositionWithContext`, live player/NPC tile points, and Stardew's world-map texture. |
| Calendar | NPC birthday fields, `Utility.isFestivalDay`, festival data assets, and merchant schedule state. |
| Shipping, mines, museum, collections | Farm shipping bin, sell prices, mine depth, museum pieces, donation eligibility, and collection dictionaries. |
| Visual assets | `Game1.menuTexture`, cursor sheets, `ItemRegistry`, portraits, animal/pet textures, seasonal tilesheets, and the live `FarmerRenderer`. |

The inventory subscribes to `OnSlotChanged` and `OnInventoryReplaced` and
coalesces changes until update completion. A one-second signature catches
nested mutations such as stack count and watering-can charge which do not
replace a slot. Gold/map changes and weapon cooldown buckets are similarly
small invalidation checks, not polling-based reconstruction.

## Identical lower-panel UI

`NativePanelRenderer` preserves the reference UI's 1240x1080 contract:

- Stardew parchment, button, slot, selection, heart, coin, season, clock,
  arrow, checkbox, sort, map, item, portrait, farmer, pet, and ground assets;
- the same three-visible-page header with previous/next arrows;
- Backpack, Crafting, Progress, Map, Journal, Relationships, Gift Guide,
  Fields, Machines, Bundles, Calendar, and Settings pages;
- the farmer sidebar, three twelve-slot backpack rows, selected-item details,
  skill rows, progress bars, scrollable lists, map markers, calendar grid, and
  game-side settings;
- touch activation/holding/dragging/scrolling and Select-modified controller
  navigation, plus L/R hotbar-row rotation while Stardew owns the panel.

The renderer consumes the same topic and sprite IDs as the reference mod. That
keeps game-state mapping and artwork behavior stable while moving ownership of
the pixels into the game process.

## Rendering and WEMU routing

After `Game1.Draw`, the renderer saves the current render targets and viewport,
draws into a 1240x1080 `RenderTarget2D` with `PointClamp`, reads BGRA pixels,
then restores the exact graphics-device state in `finally`. WEMU recognizes the
private Win32 window by the exact class `StardewValleySecondScreenPanel` or
title `Stardew Valley Dual Screen` and scans only that window to the lower
display. The gameplay window remains on the upper display, avoiding the former
combined-desktop resolution/cropping error.

The panel starts during `Game1.Initialize`, before a save is loaded. Startup
chrome is rendered at the title screen; save-dependent pages are populated on
the world-ready transition and invalidated on save/day/time/location/inventory
events. Returning to title clears save state without closing the panel.

## Input and synchronization

Touch/controller messages use an ordered IPv4 loopback UDP input channel on
port 7788. This channel carries input only; it never carries UI state or pixels
and is not a WebSocket. The window also accepts ordinary Win32 mouse/key input
for desktop use. Commands are parsed into a bounded queue and applied only from
`Game1.Update` using the game's real APIs.

Supported mutations are `select_slot`, `move_item`, `shift_toolbar`,
`eat_slot`, `sort_bag`, `craft_recipe`, `cancel_quest`, `set_option`, and
`refresh`. Every action rechecks the current live save before mutation.

## Diagnostics

`StardewDualScreen.Native.bootstrap.log` records embedded-runtime load and
callback failures. `StardewDualScreen.Native.log` rotates at 4 MiB and records
initialization, panel-window/input initialization, save transitions, publisher
failures, missing/invalid game assets, render/readback failures, HUD state, and
shutdown. Unknown crop identities are rate-limited by identity. Logging is
fail-soft, and an optional-art failure disables only that image for the session.
