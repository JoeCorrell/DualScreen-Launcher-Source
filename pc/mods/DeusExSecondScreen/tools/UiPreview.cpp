// Offline visual/layout smoke test for the lower-screen UI. This never loads
// DXHR or opens a window. Optional width/height arguments let the same geometry
// be checked at the Thor's 1240x1080 and the 1280x720 desktop-test surface.

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "../src/CustomUi.h"

namespace {

bool SaveBitmap(const wchar_t* path, const void* pixels, int width, int height) {
  BITMAPFILEHEADER file = {};
  BITMAPINFOHEADER info = {};
  info.biSize = sizeof(info);
  info.biWidth = width;
  info.biHeight = -height;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biSizeImage = static_cast<DWORD>(width * height * 4);
  file.bfType = 0x4D42;
  file.bfOffBits = sizeof(file) + sizeof(info);
  file.bfSize = file.bfOffBits + info.biSizeImage;
  FILE* output = _wfopen(path, L"wb");
  if (!output) return false;
  const bool ok = std::fwrite(&file, sizeof(file), 1, output) == 1 &&
                  std::fwrite(&info, sizeof(info), 1, output) == 1 &&
                  std::fwrite(pixels, info.biSizeImage, 1, output) == 1;
  std::fclose(output);
  return ok;
}

bool ValidateFrame(const wchar_t* label, const dxs::CustomUiFrame& frame,
                   int width, int height) {
  bool valid = frame.hits.size() == frame.hit_rects.size();
  if (!valid) {
    std::fwprintf(stderr, L"%ls: %zu hits but %zu hit rectangles\n", label,
                  frame.hits.size(), frame.hit_rects.size());
  }
  for (size_t i = 0; i < frame.hit_rects.size(); ++i) {
    const RECT& rect = frame.hit_rects[i];
    if (rect.left < 0 || rect.top < 0 || rect.right > width ||
        rect.bottom > height || rect.right <= rect.left ||
        rect.bottom <= rect.top) {
      std::fwprintf(stderr,
                    L"%ls: invalid hit %zu at %ld,%ld -> %ld,%ld\n",
                    label, i, rect.left, rect.top, rect.right, rect.bottom);
      valid = false;
    }
  }
  for (size_t i = 0; i < frame.icons.size(); ++i) {
    const dxs::NativeUiRect& rect = frame.icons[i].rect;
    if (rect.left < 0.0f || rect.top < 0.0f || rect.right > width ||
        rect.bottom > height || rect.right <= rect.left ||
        rect.bottom <= rect.top) {
      std::fwprintf(stderr,
                    L"%ls: invalid icon %zu at %.1f,%.1f -> %.1f,%.1f\n",
                    label, i, rect.left, rect.top, rect.right, rect.bottom);
      valid = false;
    }
  }
  return valid;
}

void Populate(dxs::InventorySnapshot* inventory,
              dxs::SmartGlassSnapshot* smartglass,
              dxs::NativeMapSnapshot* native_map) {
  inventory->valid = true;
  inventory->revision = 7;
  inventory->grid_width = 8;
  inventory->grid_height = 8;
  inventory->credits = 10543;
  inventory->health = 83.0f;
  inventory->max_health = 100.0f;
  inventory->energy = 72.0f;
  inventory->max_energy = 100.0f;
  inventory->total_xp = 12345;
  inventory->next_praxis = 550;
  inventory->praxis_points = 2;
  const char* names[] = {"COMBAT RIFLE", "MACHINE PISTOL", "STUN GUN",
                         "SHOTGUN", "10MM AMMO", "BUCKSHOT", "TRANQ DART",
                         "FRAG GRENADE", "EMP GRENADE", "MEDKIT", "BIO CELL",
                         "NUKE VIRUS", "BARREL", "HP AMMO", "CREDIT CHIP",
                         "ENERGY BAR", "STOP WORM", "PAINKILLERS"};
  inventory->item_count = static_cast<int>(sizeof(names) / sizeof(names[0]));
  for (int i = 0; i < inventory->item_count; ++i) {
    auto& item = inventory->items[i];
    item.item_index = i;
    item.storage_index = i;
    item.x = i % 8;
    item.y = i / 8;
    item.count = static_cast<uint16_t>((i % 5) + 1);
    item.equipped = i == 0;
    item.pickup.pickup_id = 100 + i;
    item.pickup.category = i < 4 ? 3 : 1;
    if (i < 4) {
      static const int loaded[] = {18, 4, 0, 72};
      static const int capacity[] = {20, 10, 6, 100};
      item.magazine_ammo = loaded[i];
      item.magazine_capacity = capacity[i];
      item.magazine_state_valid = true;
      item.pickup.magazine_capacity = capacity[i];
    }
    std::snprintf(item.pickup.name, sizeof(item.pickup.name), "%s", names[i]);
  }
  for (int i = 0; i < 10; ++i) {
    inventory->quick_slots[i].slot = i;
    inventory->quick_slots[i].pickup_id = inventory->items[i].pickup.pickup_id;
    inventory->quick_slots[i].item_index = i;
  }

  // Exercise the same compact, native-DTP augmentation model used at runtime.
  // These literals only stand in for localized game strings in this offline
  // geometry test; the injected DLL fills every field from GlobalPlayerInfo.
  static const char* family_names[] = {
      "SOCIAL ENHANCER", "RADAR SYSTEM", "STEALTH ENHANCER",
      "HACKING: CAPTURE", "HACKING: STEALTH", "ENERGY CONVERTER",
      "SARIF SERIES 8", "CYBERNETIC ARM", "AIM STABILIZER",
      "REFLEX BOOSTER", "TYPHOON EXPLOSIVE SYSTEM", "SMART VISION",
      "GLASS-SHIELD CLOAKING", "CASIE", "ICARUS LANDING SYSTEM",
      "DERMAL ARMOR", "REBREATHER", "CYBERNETIC LEG",
      "RUN SILENTLY", "HACKING: ANALYZE", "HACKING: FORTIFY"};
  static const uint32_t family_textures[] = {
      9427u, 9435u, 9429u, 9430u, 9431u, 9432u, 9433u,
      9434u, 9428u, 9437u, 9436u, 9438u, 9439u, 9440u,
      9441u, 9442u, 9443u, 9444u, 9445u, 9446u, 9447u};
  static const char* node_names[] = {
      "BASE IMPLANT", "ENHANCED MODULE", "SPECIALIZED BRANCH",
      "ADVANCED BRANCH"};
  static const char* node_descriptions[] = {
      "Installs the augmentation's primary operating module.",
      "Improves the augmentation through its authored upgrade path.",
      "Unlocks one branch of the native augmentation tree.",
      "Unlocks the alternate branch of the native augmentation tree."};
  static const char* node_help[] = {
      "Select an available node to purchase it with Praxis.",
      "Prerequisites and cost are validated by the original game.",
      "This node depends on the preceding native DTP relation.",
      "Purchases immediately synchronize with the loaded save."};
  inventory->augmentation_catalog_valid = true;
  inventory->augmentation_state_valid = true;
  inventory->augmentation_count = 21;
  inventory->augmentation_unlocked[0] = true;
  inventory->augmentation_unlocked[1] = true;
  inventory->augmentation_unlocked[2] = true;
  inventory->augmentation_unlocked[3] = false;
  inventory->augmentation_active_valid = true;
  inventory->augmentation_active[1] = true;
  inventory->augmentation_active[2] = true;
  static const int family_ids[] = {
      87232, 87233, 87246, 87239, 87241, 87237, 87242,
      87234, 87244, 87243, 87231, 87248, 87229, 87247,
      87230, 87236, 87245, 87235, 87249, 87238, 87240};
  for (int tree_index = 0; tree_index < inventory->augmentation_count;
       ++tree_index) {
    dxs::AugmentationTreeSnapshot& tree =
        inventory->augmentations[tree_index];
    tree.tree_id = family_ids[tree_index];
    tree.augmentation_type = tree_index;
    tree.texture_id = family_textures[tree_index];
    tree.base_praxis_cost = 2;
    tree.node_count = 4;
    tree.tree_present = tree_index < 12;
    tree.installed = tree.tree_present;
    tree.installed_nodes = tree_index < 2 ? 4 : (tree.tree_present ? 1 : 0);
    tree.next_node_index = tree_index < 2 ? -2 :
                           (tree.tree_present ? 1 : -1);
    tree.next_praxis_cost = tree_index < 2 ? -1 :
                            (tree.tree_present ? 1 : 2);
    tree.purchase_available = tree_index >= 2;
    tree.affordable = tree.purchase_available;
    tree.state = tree_index < 2 ? dxs::AugmentationTreeState::kComplete :
                 (tree.tree_present
                      ? dxs::AugmentationTreeState::kPurchased
                      : dxs::AugmentationTreeState::kAvailable);
    std::snprintf(tree.name, sizeof(tree.name), "%s",
                  family_names[tree_index]);
    std::snprintf(tree.description, sizeof(tree.description), "%s",
                  "Native augmentation family and its complete upgrade graph.");
    for (int node_index = 0; node_index < tree.node_count; ++node_index) {
      dxs::AugmentationNodeSnapshot& node = tree.nodes[node_index];
      node.node_index = node_index;
      node.texture_id = 9500u + static_cast<uint32_t>(
          tree_index * 4 + node_index);
      node.praxis_cost = node_index == 0 ? 2 : 1;
      node.tier = node_index < 2 ? node_index : 2;
      node.order_in_tier = node_index < 2 ? 0 : node_index - 2;
      node.graph_available = tree.tree_present;
      node.affordable = true;
      node.name = node_names[node_index];
      node.description = node_descriptions[node_index];
      node.help = node_help[node_index];
      if (node_index == 1) {
        node.prerequisite_count = 1;
        node.prerequisite_indices[0] = 0;
      } else if (node_index >= 2) {
        node.prerequisite_count = 1;
        node.prerequisite_indices[0] = 1;
      }
      if (!tree.tree_present) {
        node.state = dxs::AugmentationNodeState::kLocked;
      } else if (tree_index < 2 || node_index == 0) {
        node.state = dxs::AugmentationNodeState::kPurchased;
        node.prerequisites_met = true;
      } else if (node_index == 1) {
        node.state = dxs::AugmentationNodeState::kAvailable;
        node.prerequisites_met = true;
      } else {
        node.state = dxs::AugmentationNodeState::kPrerequisiteBlocked;
      }
      if (tree.tree_id == 87234 && node_index == 0) {
        node.name = "INVENTORY CAPACITY";
        node.description = "Expands the player's native inventory grid.";
      }
      if (tree.tree_id == 87235 && node_index == 1) {
        node.name = "JUMP ENHANCEMENT";
        node.description = "Enhances leg servos for improved jumping.";
        node.state = dxs::AugmentationNodeState::kPurchased;
        node.prerequisites_met = true;
      }
    }
  }

  smartglass->valid = true;
  smartglass->revision = 12;
  smartglass->status.health = 83;
  smartglass->status.max_health = 100;
  smartglass->status.energy = 72;
  smartglass->status.max_energy = 100;
  smartglass->status.total_xp = 12345;
  smartglass->status.next_praxis = 550;
  smartglass->status.credits = 10543;
  smartglass->status.praxis_points = 2;
  for (int i = 0; i < 27; ++i) {
    dxs::SmartGlassAugmentation aug;
    aug.aug_id = i;
    aug.name = L"SARIF AUGMENTATION " + std::to_wstring(i + 1);
    aug.description = L"A live augmentation description supplied by the game's native second-screen serializer.";
    aug.learned = i < 5;
    aug.installed = i < 5;
    aug.buyable = i >= 5 && i < 9;
    aug.praxis_cost = 1 + i % 3;
    smartglass->augmentations.push_back(aug);
  }
  for (int i = 0; i < 20; ++i) {
    dxs::SmartGlassObjective objective;
    objective.mission_id = i;
    objective.objective_id = 1000 + i;
    objective.primary = i < 3;
    objective.tracked = i == 0;
    objective.title = L"MISSION OBJECTIVE " + std::to_wstring(i + 1);
    objective.short_description = L"Reach the marked location.";
    objective.long_description = L"Investigate the location, secure the evidence, and return without compromising the operation.";
    smartglass->objectives.push_back(objective);
  }
  for (int i = 0; i < 25; ++i) {
    dxs::SmartGlassMediaItem media;
    media.media_id = i;
    media.title = L"SECURE MESSAGE " + std::to_wstring(i + 1);
    media.from = L"Pritchard";
    media.to = L"A. Jensen";
    media.content = L"This is a populated reader preview. Runtime content comes from the game's own e-mail, pocket-secretary, and media serializers.";
    smartglass->media.push_back(media);
  }
  smartglass->map.location = L"DETROIT";
  smartglass->map.sublocation = L"SARIF INDUSTRIES";
  smartglass->map.width = 1000;
  smartglass->map.height = 1000;
  for (int i = 0; i < 8; ++i) {
    dxs::SmartGlassMapMarker marker;
    marker.x = 100.0 + i * 97.0;
    marker.y = 180.0 + (i % 4) * 163.0;
    marker.main_mission = i < 3;
    marker.mission_title = L"OBJECTIVE " + std::to_wstring(i + 1);
    smartglass->map.markers.push_back(marker);
  }

  native_map->valid = true;
  native_map->player_valid = true;
  native_map->radar_live = true;
  native_map->map_resource_id = 1;
  native_map->level = 0;
  native_map->display_level = 0;
  native_map->floor_count = 3;
  native_map->floors[0] = -1;
  native_map->floors[1] = 0;
  native_map->floors[2] = 1;
  native_map->width = 1000.0f;
  native_map->height = 1000.0f;
  native_map->raster_width = 2207;
  native_map->raster_height = 3071;
  native_map->raster_floor = 0;
  native_map->raster_exact_floor = true;
  native_map->player_x = 0.48f;
  native_map->player_y = 0.57f;
  native_map->player_heading_valid = true;
  native_map->player_heading_degrees = 42.0f;
  std::snprintf(native_map->location, sizeof(native_map->location),
                "DETROIT");
  std::snprintf(native_map->sublocation, sizeof(native_map->sublocation),
                "SARIF INDUSTRIES");
  native_map->marker_count = 6;
  for (int i = 0; i < native_map->marker_count; ++i) {
    dxs::NativeMapMarker& marker = native_map->markers[i];
    marker.kind = i < 2 ? dxs::NativeMapMarkerKind::kFriend
                        : dxs::NativeMapMarkerKind::kHostile;
    marker.normalized_x = 0.18f + static_cast<float>(i) * 0.12f;
    marker.normalized_y = 0.24f + static_cast<float>(i % 3) * 0.20f;
  }
  native_map->objective_marker_count = 2;
  for (int i = 0; i < native_map->objective_marker_count; ++i) {
    dxs::NativeObjectiveMarker& marker = native_map->objective_markers[i];
    marker.mission_id = 100 + i;
    marker.level = 0;
    marker.primary = i == 0;
    marker.tracked = i == 0;
    marker.normalized_x = 0.37f + i * 0.21f;
    marker.normalized_y = 0.31f + i * 0.24f;
    std::snprintf(marker.title, sizeof(marker.title), "OBJECTIVE %d", i + 1);
  }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
  const int requested_width = argc > 2 ? _wtoi(argv[2]) : 1240;
  const int requested_height = argc > 3 ? _wtoi(argv[3]) : 1080;
  const int width = (std::max)(640, requested_width);
  const int height = (std::max)(480, requested_height);
  const int header = (std::max)(76, static_cast<int>(height * 0.105f));
  dxs::InventorySnapshot inventory;
  dxs::SmartGlassSnapshot smartglass;
  dxs::NativeMapSnapshot native_map;
  Populate(&inventory, &smartglass, &native_map);

  BITMAPINFO bitmap_info = {};
  bitmap_info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bitmap_info.bmiHeader.biWidth = width;
  bitmap_info.bmiHeader.biHeight = -height;
  bitmap_info.bmiHeader.biPlanes = 1;
  bitmap_info.bmiHeader.biBitCount = 32;
  bitmap_info.bmiHeader.biCompression = BI_RGB;
  HDC dc = CreateCompatibleDC(nullptr);
  void* pixels = nullptr;
  HBITMAP bitmap = CreateDIBSection(dc, &bitmap_info, DIB_RGB_COLORS,
                                    &pixels, nullptr, 0);
  HGDIOBJ old = SelectObject(dc, bitmap);
  const wchar_t* labels[] = {L"map", L"objectives", L"inventory", L"augs", L"data"};
  const int pages[] = {1, 2, 3, 4, 5};
  bool ok = true;
  for (int i = 0; i < 5; ++i) {
    PatBlt(dc, 0, 0, width, height, BLACKNESS);
    dxs::CustomUiState state;
    if (pages[i] == 4) {
      state.selected_aug = 5;
      state.selected_aug_node = 1;
    }
    dxs::CustomUiFrame frame;
    dxs::DrawCustomUi(dc, width, height, header, pages[i], inventory,
                      smartglass, native_map, state, &frame);
    ok = ValidateFrame(labels[i], frame, width, height) && ok;
    if (pages[i] == 3) {
      RECT sort = {};
      RECT previous = {};
      RECT next = {};
      bool found_sort = false;
      bool found_previous = false;
      bool found_next = false;
      for (size_t hit_index = 0; hit_index < frame.hits.size(); ++hit_index) {
        const dxs::CustomHitKind kind = frame.hits[hit_index].kind;
        if (kind == dxs::CustomHitKind::kInventorySort) {
          sort = frame.hit_rects[hit_index];
          found_sort = true;
        } else if (kind == dxs::CustomHitKind::kInventoryPrevious) {
          previous = frame.hit_rects[hit_index];
          found_previous = true;
        } else if (kind == dxs::CustomHitKind::kInventoryNext) {
          next = frame.hit_rects[hit_index];
          found_next = true;
        }
      }
      if (!found_sort || !found_previous || !found_next ||
          sort.left != previous.left || sort.right != next.right ||
          sort.top != frame.inventory_grid.bottom + 6 ||
          sort.bottom != previous.top - 6) {
        std::fwprintf(stderr,
                      L"inventory: Sort does not fill the footer utility strip\n");
        ok = false;
      }
    }
    // Native game art is unavailable outside the game process. Mark its exact
    // destination rectangles so clipping/aspect mistakes remain visible.
    for (const dxs::CustomUiIcon& icon : frame.icons) {
      RECT r{static_cast<LONG>(icon.rect.left), static_cast<LONG>(icon.rect.top),
             static_cast<LONG>(icon.rect.right), static_cast<LONG>(icon.rect.bottom)};
      HBRUSH brush = CreateSolidBrush(RGB(60, 49, 21));
      FrameRect(dc, &r, brush);
      DeleteObject(brush);
    }
    GdiFlush();
    std::wstring path = argc > 1 ? argv[1] : L".";
    if (!path.empty() && path.back() != L'\\' && path.back() != L'/') path += L'\\';
    path += L"ui-preview-";
    path += labels[i];
    path += L".bmp";
    ok = SaveBitmap(path.c_str(), pixels, width, height) && ok;
  }

  // Capacity is global, not per page. The preview owns one native capacity
  // upgrade (12 total slots), so page two must contain no usable item cards;
  // it must still expose both pager arrows as the locked next capacity page.
  dxs::CustomUiState locked_inventory_page;
  locked_inventory_page.inventory_page = 1;
  dxs::CustomUiFrame locked_inventory_frame;
  dxs::DrawCustomUi(dc, width, height, header, 3, inventory, smartglass,
                    native_map, locked_inventory_page,
                    &locked_inventory_frame);
  int page_two_items = 0;
  bool page_two_previous = false;
  bool page_two_next = false;
  int inventory_page_count = 0;
  for (const dxs::CustomHit& hit : locked_inventory_frame.hits) {
    if (hit.kind == dxs::CustomHitKind::kInventoryItem) ++page_two_items;
    if (hit.kind == dxs::CustomHitKind::kInventoryPrevious) {
      page_two_previous = true;
    }
    if (hit.kind == dxs::CustomHitKind::kInventoryNext) {
      page_two_next = true;
      inventory_page_count = hit.value;
    }
  }
  if (page_two_items != 0 || !page_two_previous || !page_two_next ||
      inventory_page_count != 2) {
    std::fwprintf(stderr,
                  L"inventory: global capacity repeated or pager vanished "
                  L"(items=%d previous=%d next=%d pages=%d)\n",
                  page_two_items, page_two_previous ? 1 : 0,
                  page_two_next ? 1 : 0, inventory_page_count);
    ok = false;
  }

  // Regression check the gesture safety path separately from the clean
  // screenshots above. An extreme drag at maximum zoom must be clamped, and
  // every D3D raster/marker/arrow quad must remain wholly inside the map
  // canvas even though those overlays do not inherit the GDI clip region.
  dxs::CustomUiState extreme_map;
  extreme_map.map_zoom = 3.0f;
  extreme_map.map_pan_x = 1000000.0f;
  extreme_map.map_pan_y = -1000000.0f;
  dxs::ClampCustomMapView(width, height, header, native_map,
                          &extreme_map.map_zoom, &extreme_map.map_pan_x,
                          &extreme_map.map_pan_y);
  if (!std::isfinite(extreme_map.map_pan_x) ||
      !std::isfinite(extreme_map.map_pan_y) ||
      std::fabs(extreme_map.map_pan_x) >= 1000000.0f ||
      std::fabs(extreme_map.map_pan_y) >= 1000000.0f) {
    std::fwprintf(stderr, L"map: extreme pan was not clamped\n");
    ok = false;
  }
  dxs::CustomUiFrame extreme_frame;
  dxs::DrawCustomUi(dc, width, height, header, 1, inventory, smartglass,
                    native_map, extreme_map, &extreme_frame);
  ok = ValidateFrame(L"map-extreme", extreme_frame, width, height) && ok;
  const RECT map_canvas{32, header + 70,
                        static_cast<LONG>(width * 0.73f) - 14,
                        height - 32};
  for (size_t index = 0; index < extreme_frame.icons.size(); ++index) {
    const dxs::NativeUiRect& rect = extreme_frame.icons[index].rect;
    if (rect.left < map_canvas.left || rect.top < map_canvas.top ||
        rect.right > map_canvas.right || rect.bottom > map_canvas.bottom) {
      std::fwprintf(stderr,
                    L"map-extreme: overlay %zu escaped the map canvas\n",
                    index);
      ok = false;
    }
  }

  if (inventory.augmentation_count > 0) {
    // The compact family selector is a 4x6 grid at every supported panel
    // resolution. All 21 retail families fit on its first page; future data
    // builds still page in groups of 24.
    dxs::CustomUiState aug_grid;
    dxs::CustomUiFrame aug_grid_frame;
    dxs::DrawCustomUi(dc, width, height, header, 4, inventory, smartglass,
                      native_map, aug_grid, &aug_grid_frame);
    bool found_last_family = false;
    for (const dxs::CustomHit& hit : aug_grid_frame.hits) {
      if (hit.kind == dxs::CustomHitKind::kAugmentation &&
          hit.value == inventory.augmentation_count - 1) {
        found_last_family = true;
        break;
      }
    }
    if (!found_last_family) {
      std::fwprintf(stderr,
                    L"augs: 4x6 grid did not expose the last retail family\n");
      ok = false;
    }
  }

  // Partially streamed/corrupt live counts must fail closed at the renderer
  // boundary. These values deliberately exceed every fixed snapshot array,
  // and the malformed graph contains invalid tiers, ordering and prerequisite
  // counts. The page should remain bounded and interactive instead of reading
  // outside the native snapshot or emitting off-screen touch rectangles.
  dxs::InventorySnapshot adversarial_inventory = inventory;
  adversarial_inventory.item_count = 10000;
  adversarial_inventory.augmentation_count = 10000;
  adversarial_inventory.augmentations[0].node_count = 10000;
  adversarial_inventory.augmentations[0].nodes[0].tier = -100;
  adversarial_inventory.augmentations[0].nodes[0].order_in_tier = -100;
  adversarial_inventory.augmentations[0].nodes[1].tier = 10000;
  adversarial_inventory.augmentations[0].nodes[1].order_in_tier = 10000;
  adversarial_inventory.augmentations[0].nodes[1].prerequisite_count = 10000;
  dxs::NativeMapSnapshot adversarial_map = native_map;
  adversarial_map.marker_count = 10000;
  for (int page : pages) {
    dxs::CustomUiState adversarial_state;
    adversarial_state.selected_aug = 10000;
    adversarial_state.selected_aug_node = 10000;
    adversarial_state.selected_objective = 10000;
    adversarial_state.selected_media = 10000;
    adversarial_state.inventory_page = 10000;
    adversarial_state.augmentation_page = 10000;
    adversarial_state.objective_page = 10000;
    adversarial_state.media_page = 10000;
    adversarial_state.map_follow_player = false;
    dxs::CustomUiFrame adversarial_frame;
    dxs::DrawCustomUi(dc, width, height, header, page,
                      adversarial_inventory, smartglass, adversarial_map,
                      adversarial_state, &adversarial_frame);
    ok = ValidateFrame(L"adversarial-live-data", adversarial_frame,
                       width, height) && ok;
  }

  // An empty native/SmartGlass handoff is a normal load state, not an error.
  // Validate its centered placeholders and ensure no stale content target is
  // synthesized while the replacement save has not attached yet.
  dxs::InventorySnapshot empty_inventory;
  dxs::SmartGlassSnapshot empty_smartglass;
  dxs::NativeMapSnapshot empty_map;
  for (int page : pages) {
    dxs::CustomUiState empty_state;
    dxs::CustomUiFrame empty_frame;
    dxs::DrawCustomUi(dc, width, height, header, page, empty_inventory,
                      empty_smartglass, empty_map, empty_state, &empty_frame);
    ok = ValidateFrame(L"empty-live-data", empty_frame, width, height) && ok;
  }
  SelectObject(dc, old);
  DeleteObject(bitmap);
  DeleteDC(dc);
  return ok ? 0 : 1;
}
