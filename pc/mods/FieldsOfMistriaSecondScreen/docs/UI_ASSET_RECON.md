# Fields of Mistria v1.0.4 UI and asset reconnaissance

## Scope and evidence

This document records a read-only audit of the user's installed Fields of Mistria v1.0.4 asset archive and the matching extracted GML/configuration sources. It is an interoperability map for a custom bottom-screen UI, not permission to distribute the game's art or fonts.

Build gate used for every finding:

| Component | Installed value |
|---|---|
| Game | Fields of Mistria 1.0.4 |
| `FieldsOfMistria.exe` SHA-256 | `aed5b1f5d5cd75afee86f0cf347655a7e62be361d775d6ffc0671b388660ee2e` |
| `assets.zip` size | 625,619,136 bytes |
| `assets.zip` SHA-256 | `e55ef6bcaa7430008a6fcae7980ef06ca6912bb10431c177807b366a0fc5a450` |
| Archive inventory | 122,403 entries; 593,827,810 uncompressed bytes |

The primary evidence is the installed `assets.zip`, paired `*.meta.toml` files, atlas metadata, and these current scripts/configuration files:

- `assets/gml/scripts/GameplaySystems/Camera/Display.gml`
- `assets/gml/scripts/UI/Anchor/Anchor.gml`
- `assets/gml/scripts/UI/Anchor/Menus/{JournalMenu,PlayerMenu,VitalsMenu,InfoHudMenu,MapMenu,RelationshipsMenu,AlmanacMenu,SpellcastingMenu}.gml`
- `assets/fiddle/ui/{text_styles,journal_pages}.toml`
- `assets/fiddle/ui/menus/{standard_menus,sub_menus}.toml`
- `assets/fiddle/{skills,spells}.toml`

All paths below are relative to the root of the user's installed `assets.zip`. No proprietary PNG, font, atlas, or other game asset has been added to this mod. The temporary `.work/mistria-v104/extracted` tree is audit material only.

Confidence is **high** where an archive entry, its paired metadata, its atlas placement, and current GML use agree. It is **medium** where the asset is installed and named but the current UI does not select it directly.

## Shipping rule: load the user's installed assets, never copy them

The legally conservative implementation is a read-only runtime asset provider:

1. Resolve the game directory from the loaded mod DLL's absolute module path. Do not use the current working directory or a hard-coded Downloads path.
2. Open adjacent `assets.zip` read-only and require the exact build/profile hash above before enabling the v1.0.4 manifest.
3. Index the ZIP central directory once. Normalize to forward slashes and require case-exact archive names. Reject absolute names, `..`, alternate separators, duplicate normalized names, invalid CRCs, and entries over configured compressed/decompressed limits.
4. For a requested sprite, read its individual `<path>.png` and paired `<path>.meta.toml` directly from the archive into memory. Prefer this over loading a 4096x4096 compiled atlas.
5. Validate `asset_kind`, `id`, `frame_size`, `frame_len`, and `atlas` against the versioned manifest. Split sheet frames according to metadata and use nearest-neighbor sampling.
6. Keep decoded data in a bounded, lazy texture cache. Preload only the diagnostic subset below; retain immutable handles while a UI snapshot is rendering and evict only unreferenced entries.
7. Read the selected installed TTF into memory for the UI lifetime and rasterize at integer sizes. Do not extract it to a persistent cache or bundle it with the mod.
8. Accept live sprite identity from the in-game bridge for items, portraits, animals, spells, and other variant-driven content. Resolve that identity through the archive index. Do not reconstruct seasonal/outfit/variant paths in the renderer.
9. On an unknown archive hash, missing pair, ID mismatch, corrupt image, or size-limit failure, fail closed to non-proprietary diagnostic primitives and log the exact mismatch. Never silently show a similarly named asset.

This design reads content already licensed and installed by the user and ships only loader code plus factual lookup metadata. Decoded pixels and font bytes remain process-local. The runtime asset provider must never modify `assets.zip`, write extracted assets into its install directory, or redistribute them. Any separately required, exact-build GML archive patch remains an installer-owned, verified, reversible transaction; it is not part of asset loading.

Recommended asset-provider key:

```text
AssetRef {
  logical_name,          // bridge-selected symbol, for diagnostics
  asset_id,              // metadata id, authoritative inside a gated build
  archive_png_path,
  archive_meta_path,
  frame_size,
  frame_count,
  atlas_label            // validation/fallback only
}
```

For dynamic content, exact v1.0.4 GML can emit the source-selected logical animation name from `asset_to_string(...)`, plus frame and source-selected LUT identity/index where applicable. It cannot emit the metadata's 16-hex `id` or exact ZIP path: the audited runtime API exposes `asset_get_ids(...)` and `asset_to_string(...)`, but no metadata-path/metadata-ID lookup, and the loaded gameplay prototypes retain handles rather than archive paths. The provider must therefore use a typed request such as `{asset_class, logical_id}` and resolve it through a case-exact, class-root-constrained index built once from the already hash-gated central directory. Reading the exact resolved `.meta.toml` then supplies its authoritative `asset_id` and paired PNG path. Absence or duplicate logical names fail closed. This prevents both GML and the renderer from guessing paths or gameplay selection rules.

## Asset packaging and atlas relationship

The release archive retains raw individual PNG sheets and paired metadata even though it also contains compiled atlases. A sprite metadata file has this useful shape:

```toml
[meta_properties]
id = "<16-hex-digit id>"
asset_kind = "Animation"

[asset_properties]
frame_size = [width, height]
frame_len = <optional, defaults to 1>
atlas = "UI" # or Default/PortraitsSpring/etc.
offset = [x, y] # optional
```

Atlas metadata relates packed frames by `texture_ids = ["<id>::<frame>", ...]`. Each matching placement is:

```text
[packed_x, packed_y, trimmed_w, trimmed_h,
 logical_w, logical_h, logical_offset_x, logical_offset_y]
```

If atlas fallback is ever needed, reconstruct the transparent logical canvas from the final four values before presenting the sprite. `UIAtlas` is 4096x4096 with nearest minification/magnification, nearest mip filtering, repeat wrapping, and `srgb = false`. Seasonal portraits have their own atlases. `PortraitsSummer` metadata can pack into `PortraitsSummerAtlas_0` or `_1`; search the atlas metadata by asset ID rather than assuming one sheet.

Many small UI borders are nine-frame sheets, not a single CSS-style nine-slice image. `ANCHOR.nine_slice` consumes frames in this order:

```text
top-left, top, top-right, left, center, right, bottom-left, bottom, bottom-right
```

Stretch the center and edges to the target bounds and preserve corner sizes. A `frame_len = 9` alone does not prove nine-slice semantics: for example the 11x11 player map marker is also a nine-frame animation but is drawn as a regular sprite.

## Exact first diagnostic UI subset

This subset is sufficient to prove archive access, paired-meta parsing, frame extraction, font rendering, nine-slicing, LUT behavior, HUD state, map state, portrait loading, inventory icons, skill progress, magic, and animal state. Load only the rows needed by the current diagnostic page; this table is not a preload mandate.

`Atlas placement` records the installed compiled-atlas evidence but the loader should use the individual PNG pair first.

| Purpose | Exact archive metadata path | Asset ID | Frame / count | Atlas placement | Confidence |
|---|---|---:|---:|---|---|
| Journal shell | `assets/animations/UI NEW/Journal/spr_ui_journal_backplate.meta.toml` | `4702779637b29d89` | 410x234 / 1 | UIAtlas `[825,167,410,234,410,234,0,0]` | High |
| Inventory left page | `assets/animations/UI NEW/Journal/spr_ui_journal_book_page_layout_inventory_left.meta.toml` | `cbb7c2a856e09a0f` | 189x218 / 1 | UIAtlas `[3124,856,189,218,189,218,0,0]` | High |
| Generic box, nine-slice | `assets/animations/UI NEW/Generic/Box/spr_ui_generic_box_main.meta.toml` | `783a48fad2e7d292` | 4x4 / 9 | UIAtlas `[1158,3354,4,4,4,4,0,0]` | High |
| Generic button, nine-slice | `assets/animations/UI NEW/Generic/Button/spr_ui_button_main.meta.toml` | `7e8cc6f0c0b60e02` | 3x3 / 9 | UIAtlas `[615,2345,3,3,3,3,0,0]` | High |
| Common font LUT | `assets/animations/UI NEW/Fonts/spr_ui_generic_font_lut.meta.toml` | `3cb7abe98f97949a` | 21x256 / 1 | DefaultAtlas_0 `[2787,3263,21,256,21,256,0,0]` | High |
| Health backplate, nine-slice | `assets/animations/UI NEW/HUD/spr_ui_hud_health_health_bar_backplate.meta.toml` | `c7441fd5f4ef1a65` | 4x5 / 9 | UIAtlas `[1483,2320,4,5,4,5,0,0]` | High |
| Health fill states | `assets/animations/UI NEW/HUD/spr_ui_hud_health_health_bar.meta.toml` | `10280c00b1f329d1` | 1x2 / 9 | UIAtlas `[1020,4051,1,2,1,2,0,0]` | High |
| Stamina fill states | `assets/animations/UI NEW/HUD/spr_ui_hud_health_stamina_bar.meta.toml` | `506224c49c069c78` | 1x2 / 9 | UIAtlas `[1020,4071,1,2,1,2,0,0]` | High |
| Mana orb on | `assets/animations/UI NEW/HUD/spr_ui_hud_health_mana_ball_on.meta.toml` | `6e1cecd9b2b5e889` | 9x9 / 1 | UIAtlas `[2119,2277,9,9,9,9,0,0]` | High |
| Info HUD top, nine-slice | `assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_top.meta.toml` | `b58027d2a47a0589` | 8x8 / 9 | UIAtlas `[2130,3369,8,8,8,8,0,0]` | High |
| Sunny weather | `assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_weather_icon_sunny.meta.toml` | `7fe1a1e4c607da4f` | 10x10 / 1 | UIAtlas `[2354,2102,10,10,10,10,0,0]` | High |
| Spring season | `assets/animations/UI NEW/HUD/spr_ui_hud_info_backplate_season_icon_spring.meta.toml` | `b967c1510254c266` | 10x10 / 1 | UIAtlas `[2225,2102,10,10,10,10,0,0]` | High |
| Tesserae/currency | `assets/animations/UI NEW/HUD/spr_ui_hud_info_currency_icon.meta.toml` | `a7d6f7b8c91f9bf` | 8x8 / 1 | UIAtlas `[3071,2134,8,8,8,8,0,0]` | High |
| Essence | `assets/animations/UI NEW/HUD/spr_ui_hud_info_essence_icon.meta.toml` | `f699cc1a861adfbf` | 8x8 / 1 | UIAtlas `[2671,2134,8,8,8,8,0,0]` | High |
| Status-effect bar back | `assets/animations/UI/Status Effect/spr_ui_statuseffect_bar_back.meta.toml` | `141e838035dd947d` | 1x1 / 9 | UIAtlas `[1581,2988,1,1,1,1,0,0]` | High |
| Map grid | `assets/animations/UI NEW/Journal/Map/spr_ui_map_grid.meta.toml` | `dda7256a8d29cbc9` | 362x202 / 1 | UIAtlas `[1,1501,362,202,362,202,0,0]` | High |
| Farm map | `assets/animations/UI NEW/Journal/Map/spr_ui_map_farm.meta.toml` | `462b8dcb60f81d53` | 339x179 / 1 | UIAtlas `[3070,1705,339,179,339,179,0,0]` | High |
| Animated player map marker | `assets/animations/UI NEW/Generic/Icons/NPC Small/spr_ui_generic_icon_npc_small_player.meta.toml` | `ac367f7789670d79` | 11x11 / 9 | UIAtlas `[2119,3693,8,9,11,11,1,1]` | High |
| Farm map tab icon | `assets/animations/UI NEW/Journal/Map/spr_ui_map_tab_icon_farm.meta.toml` | `960f32a8bec650e7` | 18x17 / 1 | UIAtlas `[1944,2768,14,15,18,17,2,2]` | High |
| Relationship header | `assets/animations/UI NEW/Journal/Relationship/spr_ui_journal_relationship_header_icon.meta.toml` | `bb5935b153cf25d6` | 14x14 / 1 | UIAtlas `[2957,2054,13,12,14,14,0,1]` | High |
| Relationship hearts | `assets/animations/UI NEW/Journal/Relationship/spr_ui_journal_relationship_heart_level.meta.toml` | `100fb6ecd906e43d` | 39x15 / 11 | UIAtlas `[978,2022,39,15,39,15,0,0]` | High |
| Example resolved portrait | `assets/animations/NPCs/Adeline/Portraits/Spring/spr_portrait_adeline_spring_neutral.meta.toml` | `5e2770bc75d292ac` | 296x180 / 2 | PortraitsSpring `[1645,3639,102,141,296,180,98,39]` | High |
| Example real item icon | `assets/animations/Item Icons/Crops/spr_ui_item_turnip.meta.toml` | `8ef4b5af2f84f924` | 18x18 / 1 | UIAtlas `[1529,3989,11,16,18,18,4,1]` | High |
| Almanac header | `assets/animations/UI NEW/Journal/Almanac/spr_ui_journal_almanac_header_icon.meta.toml` | `56693bbcd6fa27d5` | 14x14 / 1 | UIAtlas `[3459,1953,13,14,14,14,0,0]` | High |
| Almanac crops category | `assets/animations/UI NEW/Journal/Almanac/spr_ui_journal_almanac_icon_crops.meta.toml` | `7bfb693d83356591` | 14x13 / 1 | UIAtlas `[2767,2102,10,10,14,13,2,2]` | High |
| Farming skill icon | `assets/animations/UI NEW/Generic/Icons/Skill/spr_ui_skill_icon_farming.meta.toml` | `69e8d6b509e8d967` | 8x7 / 1 | UIAtlas `[3891,2134,8,7,8,7,0,0]` | High |
| Green skill progress | `assets/animations/UI NEW/Journal/spr_ui_journal_skill_progress_bar_green.meta.toml` | `ec261c36807f251e` | 96x7 / 95 | UIAtlas `[191,3569,96,7,96,7,0,0]` | High |
| Magic header | `assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_header_icon.meta.toml` | `48efe0f92476aac5` | 14x14 / 1 | UIAtlas `[2922,2000,13,12,14,14,0,1]` | High |
| Magic card | `assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_card_backplate.meta.toml` | `5d1da79668dc2db8` | 74x100 / 1 | UIAtlas `[3997,1080,66,100,74,100,4,0]` | High |
| Restore card icon | `assets/animations/UI NEW/Journal/Magic/spr_ui_journal_magic_restore_card_icon.meta.toml` | `5e00c81ec45d8250` | 14x12 / 1 | UIAtlas `[3352,1986,14,12,14,12,0,0]` | High |
| Animal header | `assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_header_icon.meta.toml` | `6ec145c8bd549b25` | 14x14 / 1 | UIAtlas `[2024,2000,14,13,14,14,0,1]` | High |
| Animal hearts | `assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_heart_level.meta.toml` | `860055ff70f22c43` | 39x15 / 11 | UIAtlas `[978,2073,39,15,39,15,0,0]` | High |
| Animal preview nine-slice | `assets/animations/UI NEW/Journal/Animals/spr_ui_journal_animal_preview_box.meta.toml` | `2e386f0eefb7c39c` | 2x2 / 9 | UIAtlas `[616,2364,2,2,2,2,0,0]` | High |
| Example resolved animal icon | `assets/animations/UI NEW/Ranching/spr_ui_icon_animal_chicken_basic_female.meta.toml` | `f4c6d242e143867d` | 18x18 / 1 | UIAtlas `[1977,3922,13,14,18,18,3,2]` | High |
| Museum on | `assets/animations/UI NEW/Generic/Icons/spr_ui_generic_icon_museum_on.meta.toml` | `c14f40d56e1c0072` | 19x10 / 1 | UIAtlas `[3815,844,18,9,19,10,1,0]` | High |
| Renown | `assets/animations/UI NEW/Journal/Inventory/spr_ui_journal_inventory_renown_icon.meta.toml` | `348f9ca21c710578` | 10x8 / 1 | UIAtlas `[3811,2078,10,8,10,8,0,0]` | High |

The first in-game diagnostic should deliberately stay small:

- Render the 410x234 journal shell and one 189x218 page at integer scale.
- Render localized installed-font text for live day/time/season/weather, health/stamina/mana, currency/essence, current location, and selected inventory item.
- Exercise one generic nine-slice, the health/stamina bars, four mana orbs, currency/season/weather icons, and one status-effect row.
- Show the current base map, player/NPC hub markers supplied by the bridge, one real inventory icon, one relationship entry respecting discovery state, one skill icon/progress bar, one learned spell, and one animal icon.
- Print archive hash/profile, asset cache count/bytes, last bridge sequence, and validation errors on a developer-only overlay.

That gives coverage of every risky asset path without front-loading thousands of icons or portraits.

## Installed fonts and locale routing

Every font has an adjacent metadata file with `asset_kind = "Font"` and `allow_shading = false`.

| Font path | Asset ID | Minimum size | Expected line height | Installed bytes | Current role |
|---|---:|---:|---:|---:|---|
| `assets/fonts/fnt_mistria_birdseed.ttf` | `db8119e8fc1f60f3` | 16 | 13 | 25,848 | English, Spanish, French, Russian standard UI |
| `assets/fonts/fnt_pixel_m_plus.ttf` | `925bcca2087de7fc` | 12 | 14 | 1,274,396 | Japanese |
| `assets/fonts/fnt_cubic_11.ttf` | `1a071ee8ccda4cf2` | 12 | 14 | 2,768,240 | Traditional Chinese |
| `assets/fonts/fnt_zpix.ttf` | `83a694c47ebc1031` | 12 | 12 | 7,179,288 | Simplified Chinese |
| `assets/fonts/fnt_silver.ttf` | `bbefb270d4dd141b` | 19 | 14 | 3,740,448 | Korean |
| `assets/fonts/fnt_nosutaru.ttf` | `2041ade250fa9f07` | 12 | 14 | 2,952,228 | Installed/reserved; no current normal-UI reference found |

`assets/fiddle/ui/text_styles.toml` is authoritative for locale selection. Popup-description and textbox styles add locale overrides but otherwise inherit the standard face. Preserve integer rasterization, nearest sampling, and the metadata's expected line height; fractional scaling visibly breaks the intended pixel face.

## Layout, coordinates, and visual rules

### Canvas and journal

`Display.gml` sets the normal minimum-spec height to 240 and derives width as `16 * 240 / 9` (426.666..., floored during resize). Treat **426x240 logical pixels** as the compatibility canvas unless runtime display data proves otherwise.

The installed journal backplate is 410x234 and is centered. `standard_menus.toml [journal]` defines:

| Element | Offset | Size |
|---|---:|---:|
| Left/right page | `[16,8]` from the journal side | `[189,218]` |
| Header | `[9,9]` within a page | `[176,21]` |
| Body | `[9,31]` within a page | `[176,178]` |

Journal tabs begin at x=6, y=14 and advance 23 pixels vertically. Current tab/page order from `journal_pages.toml` is player/inventory, customization, quest log, relationships, animal, spellcasting/magic, map, almanac, and settings. Header text is centered/middle-aligned; an optional 14x14 header icon sits at x=3, left/middle-aligned.

The vanilla journal menu pauses and hides the normal HUD. The custom second-screen implementation must reuse these visual assets/measurements without instantiating the vanilla menu.

### Common palette and LUT

`COMMON_LUT` is `spr_ui_generic_font_lut`, a 21x256 image: x selects a LUT and y is source intensity. Current `CommonLutIndex` order is:

```text
0 Source, 1 Standard, 2 Header, 3 Green, 4 Blue, 5 Red,
6 Dark, 7 BlackOnYellow, 8 BlackOnBeige, 9 Spring, 10 Summer,
11 Fall, 12 Winter, 13 WhitePureBlack, 14 WhiteOnRed,
15 WhiteOnGreen, 16 DarkAlt, 17 DarkPurple, 18 Gray,
19 BlackOnGold, 20 BlackOnRed
```

Observed black-to-white endpoints provide useful shader validation:

| Index | Dark endpoint | Light endpoint |
|---:|---:|---:|
| 1 Standard | `#DDCBDA` | `#000000` |
| 2 Header | `#D9A0B0` | `#000000` |
| 3 Green | `#DDCBDA` | `#34836B` |
| 4 Blue | `#DDCBDA` | `#2E5E8D` |
| 5 Red | `#DDCBDA` | `#C40954` |
| 6 Dark | `#D08397` | `#000000` |
| 9 Spring | `#DB9AA9` | `#000000` |
| 10 Summer | `#7CBB92` | `#000000` |
| 11 Fall | `#D68B6F` | `#000000` |
| 12 Winter | `#80B5C2` | `#000000` |
| 15 WhiteOnGreen | `#139C8A` | `#F8F8F8` |
| 17 DarkPurple | `#F3B9C8` | `#6A3C58` |
| 18 Gray | `#DDCBDA` | `#91798A` |

`UI_FADE_ALPHA` is 0.5. Explicit vitals colors are health secondary/delayed `rgb(111,23,49)`, health flash `rgb(255,175,209)`, and stamina secondary/delayed `rgb(20,92,60)`. Museum locked art uses `#2a2a2a`.

### Live HUD

`VitalsMenu.gml` places its root at `(3,6)`. Health begins with its current icon; the backplate is `(13,0)`, 56 pixels wide, and its inner fill is `(3,2)` with a 50x9 target. Stamina follows the same construction. Four 9x9 mana orbs are positioned at x=13, 23, 33, and 43. Status effects use a compact icon/grid and a dynamic lower progress bar; the installed legacy bar-back is a one-pixel, nine-frame sheet.

`InfoHudMenu.gml` defines:

- top backplate: right/top `(-6,6)`, 102x18; weather x=7, left/middle; season name `(-4,0)`, center/middle;
- middle backplate: right/top `(-6,23)`, 97x19;
- bottom backplate: right/top `(-6,41)`, initially 24x24; essence `(-3,-3)` right/bottom; currency `(-3,3)` right/top;
- essence is hidden and the bottom panel shrinks to 15 pixels until `Requirement.CollectEssence` is satisfied.

Weather asset selection is not a free-form filename mapping. The current function maps calm to sunny; special weather to spring petals or leaves; inclement to winter snow or rain; and heavy inclement to winter blizzard or storm. Installed season icons are autumn, spring, summer, and winter.

## Feature asset families and state gates

### Inventory and item icons

The archive contains 6,466 item-icon metadata entries under `assets/animations/Item Icons`. Major installed groups include Placeables (2,991), Wearable (955), Food (442), Animals (346), Fish (268), Archaeology (222), Bugs (187), Seeds (171), Forageables (156), Player Equipment (122), Tools (108), Misc (100), Materials (84), Crops (54), Fruit (38), and smaller groups.

Item icons are gameplay-prototype-selected assets. The bridge must emit the resolved icon asset ID/name and current item state. Never derive a path merely from display text, item category, or localized name. Metadata supplies the logical 18x18-ish canvas and atlas trim offset.

Exact v1.0.4 source resolution is `asset_to_string(live_item.get_ui_icon())`. `LiveItem.get_ui_icon()` selects cosmetic/animal/pet/Mist variants before falling back to `ITEM_PROTOTYPES[item_id].icon_sprite`; using the prototype alone would be wrong for those live variants. A read-only central-directory audit found 6,466 paired Item Icons metadata/PNG entries, 6,466 unique logical basenames, and no missing PNG pair. Across all 38,930 `assets/animations/**/*.meta.toml` entries, every logical basename is unique in this exact archive. Therefore a hash-gated provider can resolve the emitted logical name to one exact installed metadata/PNG pair without accepting a localized-name-derived or prefix-guessed path. The current provider additionally constrains item requests to the exact Item Icons root, so even future cross-class basename collisions cannot substitute a portrait or unrelated animation. Unknown almanac/museum items still emit no logical name at all.

### Player and skills

`PlayerMenu.gml` uses the inventory left/right page layouts. The left information block begins at x=42 with rows at y=2, 16, and 30 for season/day, year, and renown. Currency is right/top `(-5,21)` and essence `(-5,31)`. Equipment displays five armor slots plus defense.

The skill summary is laid out in these rows:

```text
farming, fishing, archaeology, cooking, mining
ranching, woodcrafting, blacksmithing, combat
```

`assets/fiddle/skills.toml` maps each skill to `assets/animations/UI NEW/Generic/Icons/Skill/spr_ui_skill_icon_<name>`. Progress color is green for farming/fishing/ranching, blue for mining/combat/archaeology, and pink for cooking/blacksmithing/woodcrafting. The 95-frame progress sheet uses `floor(progress * 95)`, clamped to its last frame.

`assets/animations/UI NEW/Skills` contains more than 200 perk/domain icons and state sprites. Their presence does **not** establish that a perk is unlocked. Expose only bridge-provided skill/perk state.

### Map

Canonical base maps all live under `assets/animations/UI NEW/Journal/Map/` and are 339x179:

| Logical location | Sprite | Asset ID |
|---|---|---:|
| Beach | `spr_ui_map_beach` | `e686cfe8861007c6` |
| Deep Woods | `spr_ui_map_deep_woods` | `bd493660489029fe` |
| Dragonsworn Glade | `spr_ui_map_dragonsworn_glade` | `5833518e925cdcfd` |
| Eastern Road | `spr_ui_map_eastern_road` | `b9f440f06cb4489b` |
| Farm | `spr_ui_map_farm` | `462b8dcb60f81d53` |
| Farm expansion | `spr_ui_map_farm_expansion` | `1603062b15c61868` |
| Hayden's Farm | `spr_ui_map_haydens_farm` | `16151f90fcce1218` |
| Narrows | `spr_ui_map_narrows` | `1adfa585a7a92b3a` |
| Summit | `spr_ui_map_summit` | `dc26a1e2a17a3730` |
| Town | `spr_ui_map_town` | `f2ab438aecf145b9` |
| Western Ruins | `spr_ui_map_western_ruins` | `98d36aece77f70af` |

The map grid is placed at `(8,8)` on the left page and the map at `(12,12)`. Tab order is farm, town, narrows, Hayden's Farm, beach, western ruins, summit, eastern road, deep woods, and Dragonsworn Glade. Tabs begin at `(20,-3)` and advance by `width(spr_ui_map_tab_disabled)-1`; selected tabs rise to y=-8. Unselected icon alpha is 0.5 and selected alpha is 1. Navigation arrows sit 14 pixels outside the relevant edge and animate by `sin(current_time / 150)`.

Important gating and coordinate behavior:

- Dragonsworn Glade is only inserted when `WorldMod.DragonswornGlade` is present.
- Deep Woods renders `spr_ui_map_locked` until `WorldMod.ThornyStairs` is present.
- The expanded-farm sprite is selected from the real farm-expansion state.
- The journal map does not expose arbitrary continuous world coordinates. `map_hubs_in(room)` supplies authored hub asset/world coordinates; player and residents are associated with a hub/location and laid out as 11x11 hub icons.
- Locked NPCs are omitted. Met NPCs use their small resolved NPC icon; un-met but eligible NPCs use the unknown icon. Pet presence is also unlock-gated.

The bridge should therefore emit the selected base-map asset plus already-resolved hub IDs, local display positions, marker identities, and visibility state. Do not estimate image coordinates from world x/y in the renderer.

### Relationships and portraits

The archive contains 2,255 primary NPC portrait metadata entries under `assets/animations/NPCs/<NPC>/Portraits`. Portraits are logical 296x180 sheets and are heavily trimmed in seasonal atlases.

The relationship journal does not choose an arbitrary current-season neutral portrait. It obtains `npc_polaroid_ui` from the NPC's current outfit wardrobe portrait `neutral`, falling back to the outfit's neutral portrait, then applies `NPC_PROTOTYPES[npc].journal_portrait_offset` and `journal_background_color` inside a clipped photo background. The bridge must emit that resolved sprite identity, frame, offset, and background color.

Example seasonal neutral IDs, useful only as loader tests:

| NPC/outfit | Asset ID | Atlas |
|---|---:|---|
| Adeline spring neutral | `5e2770bc75d292ac` | PortraitsSpring |
| Adeline summer neutral | `7f9f8b6dc5a50866` | PortraitsSummer |
| Adeline autumn neutral | `89bfce52cb5a6943` | PortraitsAutumn |
| Adeline winter neutral | `b008bc27b26fe4` | PortraitsWinter |
| March summer neutral | `be141b723986252d` | PortraitsSummer |

Discovery rules are part of correct rendering:

- `npc_is_unlocked == false`: omit the NPC entirely.
- Unmet vendors and unmet animal-tag NPCs: omit them.
- Other unlocked but unmet NPCs: alpha 0.5, display name `???`, black silhouette, no interaction.
- Met NPCs: show full portrait, name, and interaction.
- Loved/liked gift slots show a real item icon only when `known_gift_preferences` permits it; otherwise show a lock. A known preference not yet given is rendered at alpha 0.5.

The relationship-heart sprite has 11 frames for levels 0 through 10; relationship progress is separate state.

### Almanac and museum

Current almanac category order from `sub_menus.toml` is artifacts, blacksmithing, bugs, cooked dishes, crops, date inspiration, fish (fishable and dive tags), forageables, furniture, materials, ranching, and song crystal.

| Category | Installed sprite suffix | Asset ID |
|---|---|---:|
| Header | `header_icon` | `56693bbcd6fa27d5` |
| Artifacts | `icon_artifacts` | `c5ec62004b513106` |
| Blacksmithing | `icon_blacksmithing` | `f5dae5845f3a02d` |
| Bugs | `icon_bugs` | `a5c453ffc5fd65cb` |
| Cooked dishes | `icon_cooked_dishes` | `274d18f5d0adf749` |
| Crops | `icon_crops` | `7bfb693d83356591` |
| Date inspiration | `icon_date_inspiration` | `3437b65bd860d8df` |
| Fish | `icon_fish` | `63a8f178ff73ea59` |
| Forageables | `icon_forageables` | `95b699a03b631747` |
| Furniture | `icon_furniture` | `5c32daa55889d6a2` |
| Materials | `icon_materials` | `9d7e168af0fb276` |
| Ranching | `icon_ranching` | `b8d675572d1c7f96` |
| Song crystals | `icon_song_crystals` | `7b207cb58a8a1bb7` |

These are `assets/animations/UI NEW/Journal/Almanac/spr_ui_journal_almanac_<suffix>.meta.toml`. Category icons are 14x13; the header is 14x14. Category-list icons sit at `(21,3)` and counts at `(21,20)`; the item grid begins at `(5,9)`.

`ARI.items_acquired[item_id]` gates an almanac cell. Acquired items use the prototype's real icon; undiscovered items use the generic lock. Do not leak undiscovered names or raw art. Likewise museum state comes from `MUSEUM_PROGRESS[item_id]`; an incomplete item is rendered with the locked `#2a2a2a` treatment rather than being inferred from inventory ownership.

### Magic

`assets/fiddle/spells.toml` defines the current five journal spells:

| Spell | Cost | Installed visual family |
|---|---:|---|
| Full Restore | 4 | restore |
| Summon Rain | 4 | rain |
| Growth | 8 | growth |
| Dragon's Breath | 8 | fire/fire_breath |
| Sacred Light | 4 | sacred_light |

Visuals live under `assets/animations/UI NEW/Journal/Magic/` and include the card, header, rounded nine-slice boxes, spell/ribbon/card icons, pin icon, and locked variants. A left-list row is 40 pixels high, with the icon at x=9, label at x=4 (maximum width 101), and pin checkbox at right x=-9. The detail card begins at `(3,7)` in the full right body and is 74x100. Mana cost is displayed as `cost div 4` mana-ball icons.

Current discovery behavior is stricter than the presence of locked art suggests: only `ARI.spells_learned[i] == true` entries are listed. If none are learned, the page says none. Pin state is `ARI.pinned_spell`. Do not expose unlearned spell names/icons merely because locked sprite variants exist in the archive.

### Animals and pets

Journal animal chrome is under `assets/animations/UI NEW/Journal/Animals/`; resolved ranch-animal icons are under `assets/animations/UI NEW/Ranching/`, and pet icons under `assets/animations/UI NEW/Pets/`. The right preview box begins at `(6,10)` and is 88x88. Its frame is a 2x2 nine-slice. The 11-frame heart sprite represents the current heart level; fed, petted, pregnancy/stork, cosmetic, and festival states are separate live gates.

Animal icon selection is data-driven. `get_animal_icon_data(kind, variant, sex, adult)` resolves `variant_data.ui_icon` together with LUT identity/index. Pets use the selected pet prototype variant's `ui_icon`. The bridge must emit the resolved asset and LUT information. Do not form a filename from species/sex/age on the UI side.

The vanilla large preview is rendered from live animal animation state by `render_animal_in_ui`. The first diagnostic can safely use only the resolved static UI icon. A later animated preview must receive a bridge-selected animation asset, frame, LUT, and offset rather than sharing a world renderer blindly.

## Implementation acceptance checks

Static asset integration is complete; runtime acceptance still requires all of
these to pass in the running installed build:

1. The archive and executable hashes match before proprietary content is requested.
2. Every diagnostic asset validates against its paired metadata and renders with logical size/trim restored.
3. Nine-slice corners remain pixel-exact at multiple bottom-display sizes; text and icons use nearest filtering and integer layout.
4. English plus at least one CJK locale selects the installed face from `text_styles.toml`, preserves expected line height, and has no persistent extracted-font file.
5. Weather, season, health, stamina, mana, status effects, currency, and essence change from live bridge events without reopening a vanilla menu.
6. Picking up/removing an item changes its bridge-selected icon/quantity and does not require a full archive scan.
7. Deep Woods, Dragonsworn Glade, expanded farm, NPC markers, portraits, almanac cells, museum icons, spells, and animal icons obey the exact discovery/unlock gates above.
8. Save switching invalidates dynamic UI state while retaining immutable decoded assets safely; unknown IDs degrade to diagnostics rather than stale art.
9. Cache size, first-load latency, decode failure, and eviction are logged once per meaningful event, never every frame.
10. The installed archive remains byte-for-byte unchanged and the distributable contains no proprietary assets.

## Remaining UI-specific unknowns

- The bridge now publishes source-selected logical IDs/names for current outfit
  portraits, live item icons, animal variants/LUTs, map markers, skills, and
  learned spells. Runtime coverage of every emitted variant remains pending;
  the renderer must continue to fail closed rather than solve gameplay choices.
- Touch/controller hit targets, page swipes, list selection, map capture,
  panning, and zooming are implemented against normalized lower-panel input.
  Final accessibility/scale tuning requires observation on the device surface.
- Full perk-tree and animated animal-preview rendering require additional live-state proof. Installed icons alone are not evidence of unlock state.
- If a future patch removes individual PNG sheets, implement atlas fallback using ID placement and transparent logical-canvas reconstruction; do not enable that path for unknown hashes without a new audited manifest.
