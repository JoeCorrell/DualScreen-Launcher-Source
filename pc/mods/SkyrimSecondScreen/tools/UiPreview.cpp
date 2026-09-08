#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "../src/Ui.h"

namespace dis {
namespace {
Snapshot g_preview;
Command g_last_command;
int g_command_count = 0;
}
bool PushUiCommand(const Command& command) {
  g_last_command = command;
  ++g_command_count;
  return true;
}
bool CopyLatestSnapshot(Snapshot* out) { if (!out) return false; *out = g_preview; return true; }
}

#pragma pack(push, 1)
struct BmpFile { uint16_t type = 0x4D42; uint32_t size = 0; uint16_t a = 0, b = 0; uint32_t offset = 54; };
struct BmpInfo {
  uint32_t size = 40; int32_t width = 0, height = 0; uint16_t planes = 1, bits = 32;
  uint32_t compression = 0, image = 0; int32_t xppm = 2835, yppm = 2835;
  uint32_t used = 0, important = 0;
};
#pragma pack(pop)

int main(int argc, char** argv) {
  using namespace dis;
  g_preview.in_game = 1; g_preview.sequence = 1;
  g_preview.health = 325; g_preview.max_health = 325;
  g_preview.magicka = 150; g_preview.max_magicka = 150;
  g_preview.stamina = 270; g_preview.max_stamina = 270;
  g_preview.gold = 5423; g_preview.carry = 185; g_preview.max_carry = 300;
  g_preview.armor_rating = 31.0f; g_preview.damage_rating = 15.0f;
  g_preview.level = 23; g_preview.level_xp = 62; g_preview.level_xp_max = 100;
  g_preview.player_x = 12122; g_preview.player_y = -8034; g_preview.player_z = 714;
  g_preview.world_form_id = 0x0000003C;
  g_preview.heading = 1.1f; std::strcpy(g_preview.location, "Whiterun Hold");
  const char* names[] = {"Steel Sword", "Iron War Axe", "Hunting Bow", "Steel Dagger",
                         "Orcish Mace", "Woodcutter's Axe", "Steel Arrow", "Iron Arrow",
                         "Potion of Healing", "Leather Armor", "The Lusty Argonian Maid",
                         "Blue Mountain Flower", "Whiterun Guard Note", "Lockpick"};
  const uint8_t types[] = {0x29,0x29,0x29,0x29,0x29,0x29,0x2A,0x2A,
                           0x2E,0x1A,0x1B,0x1E,0x30,0x20};
  const ItemIconKind icons[] = {
      ItemIconKind::kSword, ItemIconKind::kAxe, ItemIconKind::kBow,
      ItemIconKind::kDagger, ItemIconKind::kMace, ItemIconKind::kAxe,
      ItemIconKind::kArrow, ItemIconKind::kArrow, ItemIconKind::kPotion,
      ItemIconKind::kBody, ItemIconKind::kBook, ItemIconKind::kIngredient,
      ItemIconKind::kBook, ItemIconKind::kMisc};
  for (int i = 0; i < 14; ++i) {
    auto& item = g_preview.items[g_preview.item_count++]; item.form_id = 0x100 + i;
    item.form_type = types[i]; item.count = i == 6 ? 24 : i == 7 ? 45 : 1;
    item.icon_kind = icons[i];
    item.weight = 2.5f + i; item.value = 15 + i * 12;
    if (types[i] == 0x29) item.damage = 15 + i;
    if (types[i] == 0x1A) item.armor = 31;
    if (i == 0) {
      item.flags = kItemEquipped | kItemFavorite | kItemRightHand;
      item.equip_slot = EquipmentSlot::kRightHand;
    } else if (i == 9) {
      item.flags = kItemEquipped;
      item.equip_slot = EquipmentSlot::kBody;
    }
    std::strcpy(item.name, names[i]);
    std::strcpy(item.description,
                i == 0 ? "A well-balanced steel blade, reliable in close combat."
                       : "An item carried by the Dragonborn.");
  }
  g_preview.favorite_count = 5;
  for (int i = 0; i < 4; ++i) g_preview.favorites[i] = g_preview.items[i].form_id;
  const char* spells[] = {"Flames", "Healing", "Oakflesh", "Clairvoyance", "Fury"};
  const int32_t schools[] = {20, 22, 18, 21, 21};
  for (int i = 0; i < 5; ++i) {
    auto& magic = g_preview.magic[g_preview.magic_count++]; magic.form_id = 0x500 + i;
    magic.preview_form_id = 0x600 + i;
    magic.form_type = 0x16; magic.spell_type = 0; magic.school = schools[i];
    magic.cost = 12 + i * 7;
    magic.flags = kMagicCostKnown;
    if (i == 0)
      magic.flags |= kMagicEquipped | kMagicFavorite | kMagicRightHand;
    if (i == 1) magic.flags |= kMagicEquipped | kMagicLeftHand;
    std::strcpy(magic.name, spells[i]);
    std::strcpy(magic.description,
                i == 0 ? "A gout of fire that deals continuous damage."
                       : "A learned spell powered by Magicka.");
  }
  // Keep an equipped spell on the preview hotbar so regressions in magic
  // sizing and equipped-state highlighting are visible in the golden image.
  g_preview.favorites[4] = g_preview.magic[0].form_id;
  const char* quests[] = {"Dragon Rising", "The Golden Claw", "Bleak Falls Barrow"};
  for (int i = 0; i < 3; ++i) {
    auto& quest = g_preview.quests[g_preview.quest_count++]; quest.form_id = 0x800 + i;
    quest.flags = 1; quest.stage = 20 + i * 10; std::strcpy(quest.name, quests[i]);
    std::strcpy(quest.lore,
                "The people of Whiterun have asked for aid. The journal records "
                "what the Dragonborn learned in the world, in Skyrim's own prose.");
    const char* objectives[] = {
        "Speak to the Jarl of Whiterun.",
        "Travel to the western watchtower.",
        "Help the guards defeat the dragon.",
        "Return to Jarl Balgruuf."};
    const uint8_t states[] = {3, 3, 1, 1};
    quest.objective_count = quest.objective_stored = 4;
    for (uint16_t objective = 0; objective < 4; ++objective) {
      quest.objectives[objective].index = objective * 10;
      quest.objectives[objective].state = states[objective];
      std::strcpy(quest.objectives[objective].text, objectives[objective]);
    }
    std::strcpy(quest.objective, quest.objectives[2].text);
  }

  struct MarkerSeed {
    float x, y;
    uint16_t type;
    uint32_t flags;
    const char* name;
  };
  const MarkerSeed markers[] = {
      {12122, -8034, 40, kMapMarkerVisible | kMapMarkerFastTravel, "Whiterun"},
      {-9200, 23000, 4, kMapMarkerVisible, "Embershard Mine"},
      {27500, 18500, 7, kMapMarkerVisible, "Bleak Falls Barrow"},
      {18800, -2200, 62, kMapMarkerVisible, "Dragon Rising"},
      {6200, -16500, 64, kMapMarkerVisible, "Custom Destination"},
  };
  for (const MarkerSeed& seed : markers) {
    MapMarkerState& marker = g_preview.map_markers[g_preview.map_marker_count++];
    marker.form_id = 0xA00 + g_preview.map_marker_count;
    marker.flags = seed.flags;
    marker.type = seed.type;
    marker.x = seed.x;
    marker.y = seed.y;
    marker.z = 700.0f;
    std::strcpy(marker.name, seed.name);
  }

  Ui& ui = TheUi();
  if (!ui.Create(1240, 1080)) return 2;
  const char* mode = argc > 1 ? argv[1] : "status";
  const bool loading = std::strcmp(mode, "loading") == 0;
  if (!loading) {
    // Exercise wraparound and the three-visible-page carousel before choosing
    // the requested page. Arrow centers come from BuildNavigationLayout's
    // fixed 1240x1080 regression geometry.
    if (!ui.Tap(47.0f / 1240.0f, 0.05f) ||
        ui.page() != Page::kQuests) return 8;
    if (!ui.Tap(1193.0f / 1240.0f, 0.05f) ||
        ui.page() != Page::kStatus) return 9;

    Page expected = Page::kStatus;
    if (std::strcmp(mode, "map-disabled") == 0) {
      // Map is intentionally gated off in both producer and UI. Exercise the
      // complete carousel twice; an old harness expected this retired page.
      g_command_count = 0;
      for (int step = 0; step < 8; ++step) {
        if (!ui.Tap(1193.0f / 1240.0f, 0.05f) || ui.page() == Page::kMap)
          return 70;
      }
      if (ui.page() != Page::kStatus || g_command_count != 0) return 71;
      std::puts("disabled map stays out of navigation: PASS");
    } else if (std::strcmp(mode, "inventory") == 0) {
      expected = Page::kInventory;
    } else if (std::strcmp(mode, "magic") == 0) {
      expected = Page::kMagic;
    } else if (std::strcmp(mode, "quests") == 0) {
      expected = Page::kQuests;
    } else if (std::strcmp(mode, "status") != 0) {
      return 10;
    }
    for (int step = 0; ui.page() != expected && step < 5; ++step)
      if (!ui.Tap(1193.0f / 1240.0f, 0.05f)) return 11;
    if (ui.page() != expected) return 11;
    g_command_count = 0;
    if (!ui.Render(g_preview)) return 2;

    if (expected == Page::kInventory) {
      // Every visible category must resolve at least one real Skyrim form
      // type. Select and activate its first row to exercise filtering and hit
      // testing together; this catches the empty-category regression.
      for (int category = 1; category <= 6; ++category) {
        const int category_width = (1224 - 24 - 6 * 6) / 7;
        const int category_x = 8 + 12 + category * (category_width + 6) +
                               category_width / 2;
        if (!ui.Tap(category_x / 1240.0f, 154.0f / 1080.0f)) return 20 + category;
        g_command_count = 0;
        if (!ui.Tap(100.0f / 1240.0f, 300.0f / 1080.0f))
          return 30 + category;
        // Rows select only; the large, explicit detail-panel action prevents
        // a list tap or scroll release from equipping/consuming accidentally.
        if (!ui.Tap(958.0f / 1240.0f, 943.0f / 1080.0f))
          return 40 + category;
        if (g_command_count != 1 ||
            g_last_command.type != CommandType::kActivateItem) return 50 + category;
      }
      // Restore the preview to the complete inventory after the checks.
      const int category_width = (1224 - 24 - 6 * 6) / 7;
      const int all_x = 8 + 12 + category_width / 2;
      if (!ui.Tap(all_x / 1240.0f, 154.0f / 1080.0f)) return 60;
      // Add-to-hotbar must switch to Character assignment mode and emit only
      // an assignment when a slot is explicitly chosen. It must never equip
      // the selected inventory item as a side effect of refreshing pages.
      if (!ui.Tap(100.0f / 1240.0f, 300.0f / 1080.0f)) return 63;
      g_command_count = 0;
      if (!ui.Tap(958.0f / 1240.0f, 1013.0f / 1080.0f) ||
          ui.page() != Page::kStatus) return 64;
      if (!ui.Tap(102.0f / 1240.0f, 961.0f / 1080.0f)) return 65;
      if (g_command_count != 1 ||
          g_last_command.type != CommandType::kAssignHotbar ||
          g_last_command.form_id != g_preview.items[0].form_id ||
          g_last_command.index != 0) return 66;
      if (!ui.Tap(1193.0f / 1240.0f, 0.05f) ||
          ui.page() != Page::kInventory) return 67;
      // Clear the intentional dirty state from the interactive checks before
      // the unchanged-frame suppression assertions below.
      if (!ui.Render(g_preview)) return 61;
    }

    if (expected == Page::kMagic) {
      // Both halves of the toggle must reach Skyrim. The old UI disabled the
      // action as soon as a spell was marked equipped, so bottom-screen magic
      // could equip but could never unequip.
      g_command_count = 0;
      if (!ui.Tap(100.0f / 1240.0f, 300.0f / 1080.0f) ||
          !ui.Tap(958.0f / 1240.0f, 943.0f / 1080.0f)) return 72;
      if (g_command_count != 1 ||
          g_last_command.type != CommandType::kActivateMagic ||
          g_last_command.form_id != g_preview.magic[0].form_id) return 73;
      g_command_count = 0;
      if (!ui.Tap(100.0f / 1240.0f, 424.0f / 1080.0f) ||
          !ui.Tap(958.0f / 1240.0f, 943.0f / 1080.0f)) return 74;
      if (g_command_count != 1 ||
          g_last_command.type != CommandType::kActivateMagic ||
          g_last_command.form_id != g_preview.magic[2].form_id) return 75;
      if (!ui.Render(g_preview)) return 76;
    }

    // A game Present can call Render many times for one snapshot, while the SKSE
    // sampler advances its sequence even when the pixels would be identical.
    // Keep both cases as executable regression checks for the upload gate.
    if (ui.Render(g_preview)) return 4;
    ++g_preview.sequence;
    if (ui.Render(g_preview)) return 5;
    g_preview.health += 1.0f;
    ++g_preview.sequence;
    const bool health_is_visible = expected == Page::kStatus;
    if (ui.Render(g_preview) != health_is_visible) return 6;
    // Non-status pages intentionally ignore health telemetry. Prove that a
    // value actually visible on each page still invalidates its pixels.
    if (!health_is_visible) {
      switch (expected) {
        case Page::kInventory: ++g_preview.items[0].value; break;
        case Page::kMap: g_preview.location[0] ^= 1; break;
        case Page::kMagic: g_preview.magic[0].cost += 1.0f; break;
        case Page::kQuests: ++g_preview.quests[0].stage; break;
        default: break;
      }
      ++g_preview.sequence;
      if (!ui.Render(g_preview)) return 62;
    }
    g_preview.health -= 1.0f;
    if (!health_is_visible) {
      switch (expected) {
        case Page::kInventory: --g_preview.items[0].value; break;
        case Page::kMap: g_preview.location[0] ^= 1; break;
        case Page::kMagic: g_preview.magic[0].cost -= 1.0f; break;
        case Page::kQuests: --g_preview.quests[0].stage; break;
        default: break;
      }
    }
    ++g_preview.sequence;
    if (!ui.Render(g_preview)) return 7;
    if (expected == Page::kStatus) {
      // Equipment cards are status-only. A normal tap used to queue
      // ActivateItem and silently oscillate the real actor's equipment.
      g_command_count = 0;
      if (!ui.Tap(950.0f / 1240.0f, 365.0f / 1080.0f) ||
          g_command_count != 0) return 68;
      // The center panel is now a read-only live summary. Dragging or pinching
      // it must not resurrect actor-camera controls or enqueue game writes.
      if (!ui.Pointer(WM_LBUTTONDOWN, 0.50f, 0.50f) ||
          !ui.Pointer(WM_MOUSEMOVE, 0.56f, 0.50f) ||
          !ui.Pointer(WM_LBUTTONUP, 0.56f, 0.50f) ||
          ui.Pinch(0.50f, 0.50f, 1.1f) || g_command_count != 0) return 69;
      if (!ui.Tap(0.082f, 0.89f)) return 15;
      if (g_command_count != 1 ||
          g_last_command.type != CommandType::kActivateFavorite ||
          g_last_command.form_id != g_preview.favorites[0]) return 16;
    }
  } else if (!ui.RenderLoading()) {
    return 12;
  } else if (ui.RenderLoading()) {
    // An unchanged loading screen must not trigger another 5.4 MB upload.
    return 13;
  }
  std::vector<uint8_t> rgba(1240u * 1080u * 4u), bgra(rgba.size());
  ui.canvas().CopyTo(rgba.data(), 1240u * 4u);
  // Keep the captured pixels as the loading screen, but prove that the first
  // live snapshot is never suppressed by loading-screen idempotence.
  if (loading && !ui.Render(g_preview)) return 14;
  for (int y = 0; y < 1080; ++y) for (int x = 0; x < 1240; ++x) {
    const size_t src = (static_cast<size_t>(y) * 1240 + x) * 4;
    const size_t dst = (static_cast<size_t>(1079 - y) * 1240 + x) * 4;
    bgra[dst] = rgba[src + 2]; bgra[dst + 1] = rgba[src + 1];
    bgra[dst + 2] = rgba[src]; bgra[dst + 3] = 255;
  }
  BmpFile file; BmpInfo info; info.width = 1240; info.height = 1080;
  info.image = static_cast<uint32_t>(bgra.size()); file.size = file.offset + info.image;
  char output_path[128] = {};
  std::snprintf(output_path, sizeof(output_path), "build/ui-preview-%s.bmp", mode);
  FILE* output = std::fopen(output_path, "wb");
  if (!output) return 3;
  std::fwrite(&file, sizeof(file), 1, output); std::fwrite(&info, sizeof(info), 1, output);
  std::fwrite(bgra.data(), bgra.size(), 1, output); std::fclose(output);
  return 0;
}
