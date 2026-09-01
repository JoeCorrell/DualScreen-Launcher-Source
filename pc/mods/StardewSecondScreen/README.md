# Stardew Valley Dual Screen Mod

The Stardew Valley Dual Screen Mod turns the AYN Thor's lower display into a
live, touch-friendly Stardew Valley companion. The SMAPI mod reads the current
save and sends game state and game artwork to WEMU's shared dual-screen UI. The
main display stays focused on gameplay while the lower display handles the
backpack, crafting, map, journal, relationships, farm status, crop fields,
progress, gift planning, bundles, calendar, and companion settings.

The interface is designed for the Thor's 1240×1080 lower display. It uses
Stardew Valley's own item, character, portrait, menu, slot, and button art so it
looks like part of the game rather than a separate desktop utility.

## Requirements

- Stardew Valley 1.6
- SMAPI 4.x (`manifest.json` declares 4.0.0 as the minimum)
- A current WEMU build and the matching DualScreen Mods companion app
- The Windows version of Stardew Valley launched through SMAPI

The mod and companion UI are two halves of the same feature. Update them
together; an old APK paired with a new DLL can be missing fields, actions, or
pages even when both sides still connect.

## Pages

The top navigation rail is horizontally scrollable. Fixed left and right arrow
buttons step through pages and wrap between the first and last page, making
every destination reachable without shrinking its label.

| Page | Live features |
| --- | --- |
| **Backpack** | Farmer and farm summary, day of week, date, season, time, gold, location, daily luck, character sprite, health, stamina, backpack level, one to three 12-slot rows, locked-row upgrade guidance, selected-item details, watering-can capacity, weapon cooldowns, and skills |
| **Crafting** | Scrollable game-art categories and recipe list, large result preview, description, crafted count, ingredient icons, exact owned/required quantities, readiness, and direct crafting |
| **Progress** | Mine and Skull Cavern depth, next elevator, Museum completion, carried donations, current shipping-bin value, wallet/lifetime totals, and a seven-day shipping/net-wallet history |
| **Map** | Full Valley map, bounded drag and pinch zoom, `+`/`-` zoom buttons, live farmer marker, horizontally scrollable known villagers, tap-to-focus villager locations, farm funds, lifetime earnings, player/pet details, date, and time |
| **Journal** | Scrollable active/completed quest list, parchment-style quest details, objective text, rewards, deadlines, completed filter, special-order details, and cancellation when Stardew permits it |
| **Relationships** | Scrollable villagers, live portraits, friendship hearts, birthdays, weekly gift count, and talked-today status |
| **Gift Guide** | Scrollable known-villager selector, loved-item guide, backpack matches, friendship hearts, birthdays, weekly gift count, and positive spoken-today cues |
| **Fields** | Crop ledger grouped by location and live state, with real item art, counts, days remaining, harvest readiness, and watering needs |
| **Machines** | One vertically scrollable dashboard with large rows for machines, fruit trees, and animals; output art, quantities, locations, remaining time, produce, petting state, and friendship are shown without category buttons or crop clutter |
| **Bundles** | Community Center room summaries, progress bars, active bundle progress, vertically scrollable rooms, horizontally scrollable remaining requirements, item quality and quantity, and highlights for requirements already available in the backpack |
| **Calendar** | A fixed four-week season calendar with today's date, NPC portrait and birthday text, festivals, festival time/place, and travelling-merchant days |
| **Settings** | Scrollable lower-screen appearance, page visibility, connection information, and live mod feature switches |

Only the Settings page is intended to scroll as one whole page. Other pages
keep their main layout fixed and scroll only the list or rail that can grow.

## Backpack and hotbar controls

The Backpack page keeps the active hotbar and every unlocked vanilla backpack
row together. It deliberately stops at the vanilla maximum of three rows.

- Tap an active-hotbar item to select it.
- Tap an item in another unlocked row to move it into the selected hotbar slot.
- Hold a supported item to eat or use it.
- Tap Sort to organize the bag through Stardew's inventory API.
- Press physical **L/R** to exchange the hotbar with the previous or next
  unlocked backpack row. Repeated presses cycle through all available rows.
- Locked Row 2 and Row 3 indicators explain which backpack upgrade unlocks
  each additional hotbar row.
- A blue gauge under a watering can shows its remaining water.
- Weapons expose both their remaining cooldown text and a recovery gauge when
  the game provides the cooldown range.
- Hotbar shortcuts match Stardew's actual `1-9`, `0`, `-`, `=` keyboard labels.

All inventory mutations are queued back onto Stardew's game thread. Disabling
`AllowInventoryEdits` makes the companion read-only without hiding inventory
information.

## Map controls

- Drag with one finger to pan within the map bounds.
- Pinch with two fingers to zoom from 1× to 4×.
- Use the game-styled `+` and `-` buttons for stepped zoom.
- Tap a villager portrait to select them, focus their current mapped position,
  and show their location.
- The farmer and selected-villager markers remain readable instead of growing
  with the map image.

Stardew does not provide world-map coordinates for every interior, festival,
or modded location. A villager in an unmapped location is reported as
unavailable instead of being placed at a fabricated tile coordinate.

## What the mod publishes

- Farmer name, farm name, character and portrait artwork, level, health,
  stamina, location, luck, gold, and lifetime earnings
- Day of week, date, season, year, clock, weather, and tomorrow's weather
- The complete vanilla backpack and hotbar, item stacks, quality, categories,
  watering-can capacity, cooldowns, and item sprites
- The Valley map, farmer coordinates, known-villager coordinates, portraits,
  display names, hearts, birthdays, weekly gifts, and talked-today state
- Every learned crafting recipe, internal recipe key, result, description,
  number produced, crafted total, and exact ingredient availability
- Machine input/output state, output quantity, location, readiness, and time
  remaining
- Fruit ready on fruit trees, including the fruit's real display name and art
- Farm animals, animal art, friendship, age, petting state, and real produce
- Active Community Center bundles, progress, remaining items, required quality,
  required count, and whether enough qualifying items are currently carried
- Active quests, completed-state information available from the game, rewards,
  deadlines, special orders, and their objective text
- Skills, shipping-bin state, mine depth, museum donations,
  collection information, birthdays, festivals, and travelling-cart state
- Stardew's own UI and content sprites required to render the companion

Idle chickens and other animals with Stardew's empty `0`/`-1` produce sentinel
are treated as having no produce. They are never converted into object ID 0,
which is the weeds item.

Optional crop telemetry can still be disabled with `SendCrops`, but crops are
intentionally not shown on the Machines dashboard.

## Companion-only appearance and alerts

The scrollable Settings page stores appearance and alert preferences per farm.
It now provides 40 deterministic game-sourced background choices: eight
interior tiles and eight from each seasonal outdoor sheet. Previews repeat the
real tile instead of stretching it, the default tile scale is a compact `1x`,
and existing `3x` defaults migrate once to the smaller appearance. Tile scale,
shade, and a custom solid RGB background remain adjustable.

Machine-finished, birthday, and late-night vibrations can be enabled
independently. The late warning time is selectable from 10pm through 1am. Page
visibility includes Show All and Backpack + Map presets as well as individual
switches; Settings can never be hidden.

## Actions accepted from the companion

- Select a hotbar slot
- Move an item into the active hotbar slot
- Cycle unlocked backpack rows through the hotbar
- Eat or use a supported item
- Sort the backpack
- Craft a learned recipe when its requirements are satisfied
- Cancel a quest when the save and configuration allow it
- Apply supported mod settings and request a complete refresh

The map, page arrows, list scrolling, tab visibility, and visual preferences are
native companion interactions and do not inject mouse clicks into the game.

## Top-screen behavior

With `HideGameHud` enabled, Stardew's normal HUD is hidden only while a
companion is connected because the lower screen repeats that information. The
HUD is restored immediately when the companion disconnects or the mod exits.
The main game continues to receive ordinary gameplay input.

## Settings

SMAPI creates `config.json` beside the mod. Most settings are also available on
the companion's Settings page and are written back to the same file when
changed.

| Option | Default | Purpose |
| --- | ---: | --- |
| `HideGameHud` | `true` | Hide Stardew's HUD on the main display while the lower screen is connected |
| `AllowInventoryEdits` | `true` | Allow select, move, use, sort, row-cycle, and craft commands |
| `AllowQuestCancel` | `true` | Allow the companion to request quest cancellation |
| `FarmerMarker` | `true` | Draw the live farmer marker on the lower-screen map |
| `SendMap` | `true` | Publish the Valley map and live map positions |
| `SendCrops` | `true` | Publish crop/watering and fruit-tree telemetry; crops remain off the Machines page |
| `SendMachines` | `true` | Scan placed machines and publish output/readiness information |
| `SendAnimals` | `true` | Publish animal care and produce information |
| `SendBundles` | `true` | Publish active Community Center bundle requirements |
| `SendVillagers` | `true` | Publish known villagers, portraits, friendship, birthdays, positions, and gift information |
| `SendCrafting` | `true` | Publish learned recipes and ingredient checks |
| `AllowRemote` | `false` | Listen beyond loopback and advertise the companion service on the local network |

`AllowRemote` is intentionally file-only. Turning it on changes who can connect
to the running game, so it is not exposed as a casual lower-screen toggle.

The machine, animal, bundle, villager, map, crop, and crafting switches can
reduce background work when their pages are not used. No live save data is
collected while no companion is connected.

## Installation

### WEMU / AYN Thor

1. Install matching current builds of WEMU and the DualScreen Mods companion
   APK.
2. Install SMAPI into the Windows Stardew Valley game directory in the WEMU
   container.
3. Create a single mod folder under Stardew's `Mods` directory, for example:

   ```text
   Stardew Valley/
     Mods/
       StardewDualScreenMod/
         manifest.json
         StardewDualScreen.dll
         config.json        (created after first run)
   ```

4. Launch `StardewModdingAPI.exe` through the Steam container.
5. Load a save. The companion connects locally and fills its pages after the
   world is ready.

Keep exactly one `StardewDualScreen.dll` beneath the game's `Mods` directory.
An old copy in a renamed backup folder is still discovered by SMAPI and can
produce duplicate-mod or protocol problems.

### Manual desktop installation

Copy the built mod folder into the desktop game's `Mods` directory and launch
Stardew through SMAPI. The companion protocol defaults to
`ws://127.0.0.1:7786`.

## Connection and privacy

The bridge is a small WebSocket server using plain JSON, one object per text
frame. Its handshake and framing are implemented directly because Wine does
not provide the Windows components used by .NET's normal WebSocket server.

By default the server listens only on `127.0.0.1:7786`. It does not require the
internet, send save data to a hosted service, or advertise itself to other
devices. `AllowRemote=true` enables LAN binding and periodic local discovery.

## Performance design

The mod does not rescan the whole save every frame.

- Day, clock, map movement, machines, animals, and other time-sensitive state
  update on the relevant SMAPI events or ten-minute game ticks.
- Crop, quest, special-order, machine, animal, and pet state receives bounded
  event refreshes so actionable pages do not remain stale until a manual refresh.
- Inventory-derived information updates when inventory changes.
- Calendar and other day-level information update at day/load boundaries.
- Sprites are encoded once and cached instead of being resent every tick.
- Nothing is scanned or encoded until a companion is attached.

`SendMachines` and `SendCrafting` generally perform the broadest scans. Disable
unused publishers in Settings if a very large modded farm or recipe collection
needs less background work.

## Compatibility and limitations

- Gameplay state is read through public Stardew/SMAPI APIs; the project does
  not Harmony-patch Stardew methods.
- Commands are validated and applied on the game thread.
- The Backpack page intentionally displays no more than the vanilla 36 slots.
- Modded items can use their registered sprites and display names, but a modded
  location needs valid `WorldMapManager` data to appear on the Valley map.
- Only villagers already known to the player are exposed by the relationship
  and map lists.
- Stardew supplies some quest objectives as one formatted string rather than a
  per-objective completion model; the Journal displays the information the game
  exposes without inventing check states.
- Relationship data includes weekly gift counts, not a complete historical
  list of which gift items were given.
- The Bundles page represents currently active, incomplete bundles rather than
  a permanent archive of every completed bundle.

## Troubleshooting

### The companion connects but a page is empty

Load fully into a save first. Most live objects do not exist at the title/load
screens. Verify that the page's `Send...` option is enabled, then use the
companion's refresh action or reconnect it.

### Touch actions are visible but disabled

Check `AllowInventoryEdits`, `AllowQuestCancel`, and `SendCrafting`. Crafting is
also disabled when a recipe is unknown, its internal key is unavailable, or
the game reports missing requirements.

### A villager cannot be focused on the map

They are probably indoors, at a festival, or in a location without Valley map
coordinates. Their portrait can remain in the list while their marker is
unavailable.

### Stardew closes without a SMAPI exception

Update WEMU as well as the mod. Box64/CoreCLR compatibility policy and guest
crash diagnostics live in WEMU, not in this DLL. SMAPI's latest log is normally
under the guest user's Stardew Valley `ErrorLogs` directory even when it does
not contain a managed exception.

### SMAPI reports a duplicate

Search the entire Stardew `Mods` directory for `StardewDualScreen.dll` and keep
only the current copy shown in the installation layout above.

## Building

From this directory:

```powershell
dotnet build -c Release
```

The output is:

```text
bin/Release/net6.0/StardewDualScreen.dll
```

The project deliberately has automatic desktop deployment disabled. The game
normally runs inside WEMU's Android container, so copying into the PC's Steam
directory would update a different installation.

## Protocol notes for contributors

- WebSocket endpoint: `ws://127.0.0.1:7786`
- Transport: UTF-8 JSON text frames
- Sprite payloads: base64 PNG data keyed by stable game or UI IDs
- Companion commands: bounded queue, parsed with `System.Text.Json`, drained on
  Stardew's game thread
- Display name: **Stardew Valley Dual Screen Mod**
- SMAPI unique ID: `Profmags.StardewValleyDualScreenMod`
- Assembly and release DLL: `StardewDualScreen.dll`

The unique ID follows SMAPI's `Author.ModName` convention and matches the
current mod name. The release DLL remains `StardewDualScreen.dll`.
