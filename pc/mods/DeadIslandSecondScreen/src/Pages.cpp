// The pages.
//
// Split from Ui.cpp because the shell and the content answer different
// questions and change for different reasons.
//
// Vitals and progression come from the live player object. The terrain page
// uses exported world transforms. Inventory, gear, missions and skills copy
// owned display data from a native controller only while that controller is
// genuinely visible. They never walk a hidden XUI tree: Dead Island tears down
// the controller's backing objects during Hide even though child visuals can
// retain local visibility flags. Treating those visuals as live caused stale
// callbacks and load-time crashes, so an unavailable page is intentional until
// a safe direct gameplay-model bridge is present.
//
// The DATA page is the exception and the useful one meanwhile: it shows the
// discovery state itself -- what class the player turned out to be, how far
// the object scan got, which field names bound and to what. That is real
// information about the running game, and it is what the next step is read
// from.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <vector>

#include "FieldCatalog.h"
#include "Game.h"
#include "GameState.h"
#include "MapAtlas.h"
#include "NativeMap.h"
#include "NativeUi.h"
#include "Rtti.h"
#include "SkillAtlas.h"
#include "Ui.h"

namespace dis {
namespace {

std::string Fmt(const char* fmt, ...) {
  char buffer[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  return std::string(buffer);
}

std::string Group(double v) {
  char raw[64];
  std::snprintf(raw, sizeof(raw), "%.0f", v < 0 ? 0 : v);
  std::string digits(raw), out;
  int count = 0;
  for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
    out.insert(out.begin(), digits[i]);
    if (++count % 3 == 0 && i > 0) out.insert(out.begin(), ',');
  }
  return out;
}

std::string CapacityLabel(const Value& current, const Value& maximum,
                          const char* noun) {
  if (!current.valid) return {};
  if (maximum.valid && maximum.v > 0.0) {
    return Fmt("%d / %d %s", current.i(), maximum.i(), noun);
  }
  return Fmt("%d %s", current.i(), noun);
}

bool ParseHudFraction(const std::string& value, double* current,
                      double* maximum) {
  if (!current || !maximum) return false;
  const std::size_t slash = value.find('/');
  if (slash == std::string::npos) return false;

  auto parse_side = [&](std::size_t begin, std::size_t end,
                        bool take_last, double* out) {
    std::string digits;
    std::string candidate;
    for (std::size_t i = begin; i < end; ++i) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      if (std::isdigit(c)) {
        candidate.push_back(static_cast<char>(c));
      } else if (c == ',' && !candidate.empty()) {
        continue;
      } else if (!candidate.empty()) {
        digits = candidate;
        candidate.clear();
        if (!take_last) break;
      }
    }
    if (!candidate.empty()) digits = candidate;
    if (digits.empty()) return false;
    double number = 0.0;
    for (char digit : digits) number = number * 10.0 + (digit - '0');
    *out = number;
    return true;
  };

  return parse_side(0, slash, true, current) &&
         parse_side(slash + 1, value.size(), false, maximum) &&
         *maximum > 0.0;
}

std::string Lower(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char c) {
                   return static_cast<char>(std::tolower(c));
                 });
  return value;
}

bool HasAny(const std::string& value,
            std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (needle && value.find(needle) != std::string::npos) return true;
  }
  return false;
}

const char* InventoryCategoryLabel(int category) {
  switch (category) {
    case 0: return "MELEE WEAPON";
    case 1: return "FIREARM";
    case 2: return "THROWABLE";
    case 3: return "CONSUMABLE";
    case 4: return "AMMUNITION";
    case 5: return "CASH";
    case 6: return "CHAINSAW";
    case 7: return "COLLECTABLE";
    case 8: return "MEDKIT";
    case 9: return "CRAFT PART";
    case 10: return "ITEM";
    case 11: return "QUEST ITEM";
    default: return nullptr;
  }
}

const char* ItemKindLabel(const NativeUiRow& row) {
  if (const char* exact = InventoryCategoryLabel(row.item_category))
    return exact;
  const std::string name = row.text.empty() ? std::string() : row.text.front();
  const std::string key = Lower(row.icon_texture + " " + name);
  if (HasAny(key, {"pistol", "rifle", "shotgun", "firearm", "gun",
                   "revolver", "ammo"})) return "FIREARM / AMMO";
  if (HasAny(key, {"knife", "blade", "sharp", "machete", "sword",
                   "katana", "axe", "cleaver", "hammer", "wrench",
                   "blunt", "bat", "club", "crowbar", "pipe"})) {
    return "MELEE WEAPON";
  }
  if (HasAny(key, {"grenade", "bomb", "molotov", "explosive"}))
    return "THROWABLE";
  if (HasAny(key, {"med", "health", "bandage", "firstaid", "bottle",
                   "drink", "alcohol", "food", "snack", "energy"})) {
    return "CONSUMABLE";
  }
  return "INVENTORY ITEM";
}

bool IsWeaponInventoryItem(const NativeUiRow& row) {
  // Definitive Edition's exact EItemCategory values. Throwables and chainsaws
  // behave as weapons in the vanilla inventory, so they stay with the weapon
  // section instead of being mixed into crafting parts and consumables.
  if (row.item_category >= 0) {
    return row.item_category == 0 || row.item_category == 1 ||
           row.item_category == 2 || row.item_category == 6;
  }
  const std::string key = Lower(
      row.icon_texture + " " +
      (row.text.empty() ? std::string() : row.text.front()) + " " +
      (row.text.size() > 1 ? row.text[1] : std::string()));
  return HasAny(key, {"weapon", "melee", "firearm", "throwable",
                      "chainsaw", "pistol", "rifle", "shotgun", "gun",
                      "knife", "machete", "sword", "axe", "hammer",
                      "wrench", "bat", "club", "crowbar", "pipe",
                      "grenade", "molotov"});
}

std::string ItemQuantityLabel(const NativeUiRow& row) {
  if (row.text.size() < 2) return {};
  const std::string& detail = row.text[1];
  std::size_t digit = detail.size();
  while (digit > 0 && std::isdigit(
             static_cast<unsigned char>(detail[digit - 1]))) {
    --digit;
  }
  if (digit == detail.size() || digit == 0) return {};
  std::size_t marker = digit;
  while (marker > 0 && std::isspace(
             static_cast<unsigned char>(detail[marker - 1]))) {
    --marker;
  }
  if (marker == 0 ||
      (detail[marker - 1] != 'x' && detail[marker - 1] != 'X')) {
    return {};
  }
  return "x" + detail.substr(digit);
}

struct InventoryDisplayEntry {
  int item_index = -1;
  const char* section = nullptr;
  bool is_section() const { return section != nullptr; }
};

std::vector<InventoryDisplayEntry> InventoryDisplayEntries(
    const InventorySnapshot& inventory) {
  std::vector<int> weapons;
  std::vector<int> other;
  weapons.reserve(inventory.items.size());
  other.reserve(inventory.items.size());
  for (int index = 0; index < static_cast<int>(inventory.items.size()); ++index) {
    (IsWeaponInventoryItem(inventory.items[index]) ? weapons : other)
        .push_back(index);
  }

  std::vector<InventoryDisplayEntry> entries;
  entries.reserve(inventory.items.size() + 2);
  auto append = [&](const char* label, const std::vector<int>& indices) {
    if (indices.empty()) return;
    entries.push_back(InventoryDisplayEntry{-1, label});
    for (int index : indices)
      entries.push_back(InventoryDisplayEntry{index, nullptr});
  };
  append("WEAPONS", weapons);
  append("PARTS & ITEMS", other);
  return entries;
}

// The original hud.dds and its authored inventory rectangles are embedded in
// the one-file proxy. Prefer the exact native texture key from the controller;
// where the game only exposes a presenter, ItemAtlas resolves the translated
// display name to the same weapon-family art used by the vanilla inventory.
void DrawItemGlyph(Canvas& canvas, const Box& bounds,
                   const std::string& texture, const std::string& name,
                   Color ink = theme::kText) {
  if (bounds.w <= 4 || bounds.h <= 4) return;
  if (TheItemAtlas().Draw(canvas, bounds, texture, name, ink)) return;
  const std::string key = Lower(texture + " " + name);
  const int cx = bounds.x + bounds.w / 2;
  const int cy = bounds.y + bounds.h / 2;
  const int scale = std::max(1, std::min(bounds.w, bounds.h) / 22);

  if (HasAny(key, {"med", "health", "bandage", "firstaid", "first_aid"})) {
    canvas.FillRect(Box(cx - 3 * scale, cy - 8 * scale,
                        6 * scale, 16 * scale), ink);
    canvas.FillRect(Box(cx - 8 * scale, cy - 3 * scale,
                        16 * scale, 6 * scale), ink);
  } else if (HasAny(key, {"pistol", "rifle", "shotgun", "firearm", "gun",
                          "revolver", "ammo"})) {
    canvas.FillRoundRect(Box(bounds.x + 2 * scale, cy - 5 * scale,
                             bounds.w - 5 * scale, 8 * scale), 2 * scale, ink);
    canvas.FillRoundRect(Box(cx - scale, cy + scale, 5 * scale, 7 * scale),
                         scale, ink);
    canvas.FillRect(Box(bounds.right() - 7 * scale, cy - 2 * scale,
                        5 * scale, 2 * scale), theme::kAccent);
  } else if (HasAny(key, {"knife", "blade", "sharp", "machete", "sword",
                          "katana", "axe", "cleaver"})) {
    canvas.Line(bounds.x + 4 * scale, bounds.bottom() - 4 * scale,
                bounds.right() - 4 * scale, bounds.y + 4 * scale,
                3 * scale, ink);
    canvas.Line(bounds.x + 4 * scale, bounds.bottom() - 7 * scale,
                bounds.x + 10 * scale, bounds.bottom() - scale,
                3 * scale, theme::kAccent);
  } else if (HasAny(key, {"hammer", "wrench", "blunt", "bat", "club",
                          "stick", "crowbar", "pipe"})) {
    canvas.Line(cx - 5 * scale, bounds.bottom() - 3 * scale,
                cx + 4 * scale, bounds.y + 6 * scale, 4 * scale, ink);
    canvas.FillRoundRect(Box(cx - 4 * scale, bounds.y + 2 * scale,
                             12 * scale, 6 * scale), 2 * scale, ink);
    canvas.FillRect(Box(cx - 7 * scale, bounds.y + 4 * scale,
                        4 * scale, 2 * scale), theme::kAccent);
  } else if (HasAny(key, {"grenade", "bomb", "molotov", "explosive"})) {
    canvas.FillCircle(cx, cy + 2 * scale, 7 * scale, ink);
    canvas.FillRect(Box(cx - 3 * scale, cy - 8 * scale,
                        6 * scale, 5 * scale), ink);
    canvas.Line(cx + 2 * scale, cy - 8 * scale, cx + 8 * scale,
                cy - 5 * scale, 2 * scale, theme::kAccent);
  } else if (HasAny(key, {"bottle", "drink", "alcohol", "food", "snack",
                          "energy", "consumable"})) {
    canvas.FillRoundRect(Box(cx - 5 * scale, cy - 4 * scale,
                             10 * scale, 12 * scale), 2 * scale, ink);
    canvas.FillRect(Box(cx - 3 * scale, cy - 9 * scale,
                        6 * scale, 6 * scale), ink);
    canvas.FillRect(Box(cx - 4 * scale, cy + scale,
                        8 * scale, 3 * scale), theme::kAccent);
  } else {
    canvas.StrokeRect(Box(cx - 8 * scale, cy - 7 * scale,
                          16 * scale, 15 * scale), 2 * scale, ink);
    canvas.Line(cx - 5 * scale, cy - 7 * scale, cx - 2 * scale,
                cy - 10 * scale, 2 * scale, ink);
    canvas.Line(cx - 2 * scale, cy - 10 * scale, cx + 5 * scale,
                cy - 7 * scale, 2 * scale, ink);
    canvas.FillRect(Box(cx - 2 * scale, cy - 2 * scale,
                        4 * scale, 4 * scale), theme::kAccent);
  }
}

void DrawSkillGlyph(Canvas& canvas, const Box& bounds,
                    const NativeUiRow& row, Color ink) {
  // SkillButtonV binds its real icon through data association 2.  NativeUi
  // preserves that atlas key in icon_texture; the remaining candidates keep
  // this compatible with localized/modded controllers that expose the skill
  // id through another association.  The embedded atlas is copied directly
  // from the game's own skills resource, so a successful match is the exact
  // authored icon rather than a generic replacement.
  SkillAtlas& atlas = TheSkillAtlas();
  if (atlas.Draw(canvas, bounds, row.icon_texture, ink) ||
      atlas.Draw(canvas, bounds, row.id, ink)) {
    return;
  }
  for (const std::string& value : row.text) {
    if (atlas.Draw(canvas, bounds, value, ink)) return;
  }

  // Fail visibly but safely if a total-conversion/modded skill does not exist
  // in the shipped atlas.  This fallback also makes a missing runtime key easy
  // to diagnose without making the skill untappable.
  const int cx = bounds.x + bounds.w / 2;
  const int cy = bounds.y + bounds.h / 2;
  const int radius = std::max(8, std::min(bounds.w, bounds.h) / 2 - 4);
  canvas.StrokeCircle(cx, cy, radius, 2, ink);
  const std::string key = Lower(row.icon_texture + " " +
      (row.text.empty() ? std::string() : row.text.front()));
  if (row.group == 0 || HasAny(key, {"fury", "rage"})) {
    canvas.Line(cx, cy - radius + 5, cx - 6, cy + 2, 4, ink);
    canvas.Line(cx - 6, cy + 2, cx + 2, cy - 1, 4, ink);
    canvas.Line(cx + 2, cy - 1, cx - 1, cy + radius - 5, 4, ink);
    canvas.Line(cx - 1, cy + radius - 5, cx + 8, cy + 2, 4, ink);
  } else if (row.group == 1 || HasAny(key, {"combat", "weapon"})) {
    canvas.FillRoundRect(Box(cx - 8, cy - 2, 16, 10), 3, ink);
    for (int i = 0; i < 4; ++i)
      canvas.FillRoundRect(Box(cx - 9 + i * 5, cy - 9, 4, 9), 1, ink);
  } else {
    canvas.Line(cx - 10, cy - 6, cx, cy + 10, 4, ink);
    canvas.Line(cx, cy + 10, cx + 10, cy - 6, 4, ink);
    canvas.Line(cx - 10, cy - 6, cx, cy - 11, 4, ink);
    canvas.Line(cx, cy - 11, cx + 10, cy - 6, 4, ink);
  }
}

enum class ImpactGlyph { kLevel, kMoney, kSkill };

// One visual language for all three progression currencies.  The irregular
// impact seal is recognisably Dead Island at a glance, while the glyph inside
// tells the player whether the card is level, cash or an unspent skill point.
// Sharing the shell also keeps the formerly plain level/money icons from
// looking like placeholders beside the skill-point emblem.
void DrawImpactEmblem(Canvas& canvas, const Box& bounds, ImpactGlyph glyph,
                      bool active = true) {
  if (bounds.w < 20 || bounds.h < 20) return;
  const int cx = bounds.x + bounds.w / 2;
  const int cy = bounds.y + bounds.h / 2;
  const int radius = std::max(10, std::min(bounds.w, bounds.h) / 2 - 3);
  const Color badge = active ? theme::kAccent : theme::kEdgeBright;
  const Color ink = Color(8, 9, 10);

  // Twelve short spokes produce the irregular stamped/starburst silhouette
  // used by the game's progression UI without introducing a bitmap asset.
  constexpr int kTeeth = 12;
  for (int i = 0; i < kTeeth; ++i) {
    const float a = static_cast<float>(i) * 6.28318530718f / kTeeth;
    const int x0 = cx + static_cast<int>(std::cos(a) * (radius - 9));
    const int y0 = cy + static_cast<int>(std::sin(a) * (radius - 9));
    const int x1 = cx + static_cast<int>(std::cos(a) * radius);
    const int y1 = cy + static_cast<int>(std::sin(a) * radius);
    canvas.Line(x0, y0, x1, y1, std::max(4, radius / 6), badge);
  }
  canvas.FillCircle(cx, cy, radius - 7, badge);
  canvas.StrokeCircle(cx, cy, radius - 6, 2,
                      badge.Mix(theme::kText, 0.18f));

  if (glyph == ImpactGlyph::kLevel) {
    const int head = std::max(8, radius * 7 / 20);
    canvas.FillCircle(cx, cy - radius / 8, head, ink);
    canvas.FillRoundRect(Box(cx - head * 3 / 4, cy + head / 4,
                             head * 3 / 2, head),
                         std::max(2, head / 5), ink);
    canvas.FillCircle(cx - head / 3, cy - radius / 7,
                      std::max(2, head / 6), badge);
    canvas.FillCircle(cx + head / 3, cy - radius / 7,
                      std::max(2, head / 6), badge);
    canvas.Line(cx, cy, cx - head / 5, cy + head / 4,
                std::max(1, head / 8), badge);
    for (int i = -1; i <= 1; ++i) {
      canvas.Line(cx + i * head / 3, cy + head / 2,
                  cx + i * head / 3, cy + head,
                  std::max(1, head / 9), badge);
    }
    return;
  }

  if (glyph == ImpactGlyph::kMoney) {
    const int inner = radius - 13;
    canvas.StrokeCircle(cx, cy, std::max(5, inner), 3, ink);
    const FontSpec symbol{std::max(20, radius), FW_BOLD};
    canvas.Text(Box(cx - radius, cy - radius, radius * 2, radius * 2),
                "$", symbol, ink, Align::kCenter);
    return;
  }

  const int unit = std::max(1, radius / 15);
  const int fist_w = 20 * unit;
  const int left = cx - fist_w / 2;
  const int knuckle_y = cy - 7 * unit;
  for (int i = 0; i < 4; ++i) {
    canvas.FillRoundRect(Box(left + i * 5 * unit, knuckle_y,
                             4 * unit, 8 * unit),
                         2 * unit, ink);
  }
  canvas.FillRoundRect(Box(left, cy - unit, fist_w, 12 * unit),
                       3 * unit, ink);
  canvas.Line(left + unit, cy + 2 * unit,
              left - 5 * unit, cy - 3 * unit, 5 * unit, ink);
  canvas.FillRect(Box(cx - 7 * unit, cy + 9 * unit,
                      14 * unit, 6 * unit), ink);
  // Two shallow seams keep the fist from reading as a featureless block.
  canvas.Line(left + 5 * unit, cy + unit, left + 5 * unit,
              cy + 5 * unit, unit, badge.Mix(ink, 0.65f));
  canvas.Line(left + 10 * unit, cy + unit, left + 10 * unit,
              cy + 5 * unit, unit, badge.Mix(ink, 0.65f));
}

void DrawSkillPointEmblem(Canvas& canvas, const Box& bounds,
                          bool available = true) {
  DrawImpactEmblem(canvas, bounds, ImpactGlyph::kSkill, available);
}

bool SkillRank(const NativeUiRow& row, int* current, int* maximum) {
  if (!current || !maximum) return false;
  const std::string rank = row.text.size() > 1 ? row.text[1] : std::string();
  int learned = 0;
  int total = 0;
  if (std::sscanf(rank.c_str(), " %d / %d", &learned, &total) != 2 &&
      std::sscanf(rank.c_str(), " %d/%d", &learned, &total) != 2) {
    return false;
  }
  if (learned < 0 || total <= 0 || learned > total) return false;
  *current = learned;
  *maximum = total;
  return true;
}

// ------------------------------------------------------------------- STATUS
void ReferencePanel(Canvas& canvas, const Box& bounds) {
  canvas.VGradient(bounds,
                   theme::kSurfaceHigh.Mix(theme::kAccent, 0.10f),
                   theme::kSurface);
  canvas.StrokeRect(bounds, 2, theme::kEdge);
  if (bounds.w > 8)
    canvas.HGradient(Box(bounds.x + 2, bounds.y + 2, bounds.w - 4, 4),
                     theme::kAccent, theme::kAccentDeep);
  if (bounds.w > 12 && bounds.h > 12) {
    const Box inner = bounds.Inset(5);
    canvas.StrokeRect(inner, 1, Color(31, 33, 34));
    // Small bright corner cuts add depth without boxing the contents in with
    // another heavy frame.
    constexpr int kCut = 18;
    canvas.Line(bounds.x + 3, bounds.y + 3, bounds.x + 3 + kCut,
                bounds.y + 3, 1, theme::kEdgeBright);
    canvas.Line(bounds.x + 3, bounds.y + 3, bounds.x + 3,
                bounds.y + 3 + kCut, 1, theme::kEdgeBright);
    canvas.Line(bounds.right() - 4 - kCut, bounds.bottom() - 4,
                bounds.right() - 4, bounds.bottom() - 4, 1,
                theme::kEdgeBright);
    canvas.Line(bounds.right() - 4, bounds.bottom() - 4 - kCut,
                bounds.right() - 4, bounds.bottom() - 4, 1,
                theme::kEdgeBright);
  }
}

void DrawQuickSlotStrip(Canvas& canvas, Interface& ui, const Box& panel,
                        GameState& state, const std::string& caption) {
  ReferencePanel(canvas, panel);
  const bool has_caption = !caption.empty();
  if (has_caption) {
    canvas.Text(Box(panel.x + 18, panel.y + 9, panel.w - 36, 26), caption,
                theme::kFontLabel, theme::kTextDim, Align::kLeft);
    canvas.FillRect(Box(panel.x + 18, panel.y + 39, 52, 3), theme::kAccent);
  }

  // The Definitive Edition controller normally authors B_Slot0..B_Slot7.
  // Keep a ninth position in the custom footer, but only make it interactive
  // when a controller/mod actually exposes and enables B_Slot8. This avoids a
  // decorative dead button mutating the wrong native slot.
  constexpr int kSlots = 9;
  constexpr int kGap = 8;
  constexpr int kSidePadding = 14;
  const int header = has_caption ? 47 : 8;
  const int bottom_padding = has_caption ? 11 : 8;
  const int available_h = std::max(1, panel.h - header - bottom_padding);
  const int available_w = std::max(
      1, (panel.w - 2 * kSidePadding - (kSlots - 1) * kGap) / kSlots);
  const int side = std::max(1, std::min(available_h, available_w));
  const int strip_w = kSlots * side + (kSlots - 1) * kGap;
  const int start_x = panel.x + (panel.w - strip_w) / 2;
  const int y = panel.y + header + (available_h - side) / 2;
  const FontSpec slot_font{13, FW_SEMIBOLD};

  const GearSnapshot& gear = TheNativeUi().gear();
  const InventoryCapacity& capacity = state.inventory_capacity();
  const int unlocked = capacity.quick_slots.valid
      ? std::clamp(capacity.quick_slots.i(), 0, kSlots)
      : kSlots;
  for (int index = 0; index < kSlots; ++index) {
    const Box slot(start_x + index * (side + kGap), y, side, side);
    const NativeUiRow* row = index < static_cast<int>(gear.quick_slots.size())
        ? &gear.quick_slots[index] : nullptr;
    const bool controller_has_slot = row && !row->id.empty();
    const bool locked = index >= unlocked || !controller_has_slot;
    const bool selected = row && row->selected;
    const bool pressed = ui.TargetPressed(UiAction::kGearSlot, index);
    const Color fill = selected || pressed
        ? theme::kSurfaceHigh.Mix(theme::kAccent,
                                  selected ? 0.34f : 0.22f)
        : Color(8, 7, 8);
    canvas.FillRect(slot, fill);
    canvas.StrokeRect(slot, selected ? 3 : 2,
                      selected ? theme::kAccent
                               : (locked ? theme::kEdge
                                         : theme::kEdgeBright));
    if (selected)
      canvas.FillRect(Box(slot.x + 3, slot.y + 3, 5, slot.h - 6),
                      theme::kAccent);
    canvas.Text(Box(slot.x + 8, slot.y + 4, slot.w - 16, 22),
                Fmt("%d", index + 1), theme::kFontLabel,
                locked ? theme::kTextFaint : theme::kAccent,
                Align::kLeft);

    if (locked) {
      const int cx = slot.x + slot.w / 2;
      const int cy = slot.y + slot.h / 2;
      canvas.StrokeRoundRect(Box(cx - 15, cy - 2, 30, 25), 3, 3,
                             theme::kTextFaint);
      canvas.StrokeCircle(cx, cy - 3, 11, 3, theme::kTextFaint);
      canvas.Text(Box(slot.x + 5, slot.bottom() - 27, slot.w - 10, 20),
                  "LOCKED", theme::kFontLabel, theme::kTextFaint,
                  Align::kCenter);
    } else if (row && row->enabled) {
      const std::string name =
          !row->text.empty() && !row->text.front().empty()
              ? row->text.front() : "ITEM";
      DrawItemGlyph(canvas,
                    Box(slot.x + 23, slot.y + 27, slot.w - 46,
                        std::max(30, slot.h - 58)),
                    row->icon_texture, name, theme::kTextDim);
      canvas.TextWrap(Box(slot.x + 5, slot.bottom() - 31,
                          slot.w - 10, 28),
                      name, slot_font, theme::kText,
                      Align::kCenter);
      if (row->text.size() > 1 && !row->text[1].empty()) {
        canvas.Text(Box(slot.right() - 70, slot.y + 4, 62, 22),
                    row->text[1], theme::kFontLabel, theme::kText,
                    Align::kRight);
      }
    } else {
      canvas.Text(Box(slot.x + 5, slot.y + 28, slot.w - 10,
                      slot.h - 36),
                  "EMPTY", theme::kFontLabel, theme::kTextFaint,
                  Align::kCenter);
    }
    // Keep the fixed native slot ordinal as the touch identity on every page.
    // The native adapter safely ignores an empty slot after reacquiring it.
    ui.AddTarget(slot, UiAction::kGearSlot, index, !locked);
  }
}

void SegmentedBar(Canvas& canvas, const Box& bounds, const Value& value,
                  const Value& maximum, const Color& fill,
                  int segments = 15) {
  canvas.FillRect(bounds, Color(3, 4, 5));
  canvas.StrokeRect(bounds, 2, Color(0, 0, 0));
  const int gap = 3;
  const int inner_x = bounds.x + 3;
  const int inner_y = bounds.y + 3;
  const int inner_w = bounds.w - 6;
  const int inner_h = bounds.h - 6;
  const int cell_w = std::max(2, (inner_w - (segments - 1) * gap) / segments);
  const float ratio = value.valid && maximum.valid && maximum.v > 0.0
      ? std::clamp(static_cast<float>(value.v / maximum.v), 0.0f, 1.0f) : -1.0f;
  for (int i = 0; i < segments; ++i) {
    const Box cell(inner_x + i * (cell_w + gap), inner_y, cell_w, inner_h);
    Color color = Color(44, 46, 47);
    if (ratio >= 0.0f && (i + 0.5f) / segments <= ratio) color = fill;
    canvas.VGradient(cell, color.Mix(theme::kText, 0.08f), color);
  }
}

void DrawVitalIcon(Canvas& canvas, const Box& box, int kind) {
  canvas.FillRect(box, theme::kText);
  const Color ink = theme::kSurface;
  const int cx = box.x + box.w / 2;
  const int cy = box.y + box.h / 2;
  if (kind == 0) {
    canvas.FillRect(Box(cx - 7, cy - 23, 14, 46), ink);
    canvas.FillRect(Box(cx - 23, cy - 7, 46, 14), ink);
  } else if (kind == 1) {
    canvas.FillCircle(cx + 9, cy - 21, 7, ink);
    canvas.Line(cx + 4, cy - 12, cx - 7, cy + 6, 8, ink);
    canvas.Line(cx - 6, cy + 5, cx + 18, cy + 12, 7, ink);
    canvas.Line(cx - 5, cy + 3, cx - 23, cy + 21, 7, ink);
    canvas.Line(cx + 17, cy + 12, cx + 8, cy + 28, 7, ink);
    canvas.Line(cx, cy - 6, cx + 23, cy - 7, 6, ink);
  } else {
    canvas.FillCircle(cx, cy - 4, 23, ink);
    canvas.FillRect(Box(cx - 16, cy + 12, 32, 17), ink);
    canvas.FillCircle(cx - 9, cy - 7, 5, theme::kText);
    canvas.FillCircle(cx + 9, cy - 7, 5, theme::kText);
    canvas.Line(cx, cy + 1, cx - 5, cy + 8, 3, theme::kText);
    for (int i = 0; i < 4; ++i)
      canvas.FillRect(Box(cx - 13 + i * 8, cy + 16, 5, 10), theme::kText);
  }
}

void DrawMapMarker(Canvas& canvas, int px, int py, float heading_degrees,
                   int radius) {
  const float angle = heading_degrees * 0.017453292519943295f;
  const float dx = std::sin(angle);
  const float dy = -std::cos(angle);
  const int tip_x = px + static_cast<int>(dx * (radius + 8));
  const int tip_y = py + static_cast<int>(dy * (radius + 8));
  const int wing = std::max(6, radius / 2);
  const int left_x = px + static_cast<int>((-dx - dy) * wing);
  const int left_y = py + static_cast<int>((-dy + dx) * wing);
  const int right_x = px + static_cast<int>((-dx + dy) * wing);
  const int right_y = py + static_cast<int>((-dy - dx) * wing);
  const int shadow_width = std::max(7, radius / 2 + 3);
  const int line_width = std::max(4, shadow_width - 4);
  canvas.FillCircle(px, py, radius, Color(0, 0, 0, 210));
  canvas.StrokeCircle(px, py, radius, 2, theme::kText);
  canvas.Line(px, py, tip_x, tip_y, shadow_width, Color(0, 0, 0, 220));
  canvas.Line(tip_x, tip_y, left_x, left_y, shadow_width,
              Color(0, 0, 0, 220));
  canvas.Line(tip_x, tip_y, right_x, right_y, shadow_width,
              Color(0, 0, 0, 220));
  canvas.Line(px, py, tip_x, tip_y, line_width, theme::kAccent);
  canvas.Line(tip_x, tip_y, left_x, left_y, line_width, theme::kAccent);
  canvas.Line(tip_x, tip_y, right_x, right_y, line_width, theme::kAccent);
  canvas.FillCircle(px, py, std::max(4, radius / 3), theme::kText);
}

void MapViewportPlayerPixel(const Box& view, float player_u, float player_v,
                            float zoom, int* px, int* py) {
  if (!px || !py) return;
  player_u = std::clamp(player_u, 0.0f, 1.0f);
  player_v = std::clamp(player_v, 0.0f, 1.0f);
  const float view_aspect = static_cast<float>(view.w) /
                            std::max(1, view.h);
  float source_w = static_cast<float>(MapAtlas::kWidth);
  float source_h = static_cast<float>(MapAtlas::kHeight);
  if (view_aspect >= 1.0f) source_h = source_w / view_aspect;
  else source_w = source_h * view_aspect;
  source_w /= std::max(1.0f, zoom);
  source_h /= std::max(1.0f, zoom);
  const int crop_w = std::max(1, std::min(MapAtlas::kWidth,
      static_cast<int>(std::lround(source_w))));
  const int crop_h = std::max(1, std::min(MapAtlas::kHeight,
      static_cast<int>(std::lround(source_h))));
  const int crop_x = std::max(0, std::min(MapAtlas::kWidth - crop_w,
      static_cast<int>(std::lround(
          player_u * MapAtlas::kWidth - crop_w * 0.5f))));
  const int crop_y = std::max(0, std::min(MapAtlas::kHeight - crop_h,
      static_cast<int>(std::lround(
          player_v * MapAtlas::kHeight - crop_h * 0.5f))));
  *px = view.x + static_cast<int>(
      ((player_u * MapAtlas::kWidth - crop_x) / crop_w) * view.w);
  *py = view.y + static_cast<int>(
      ((player_v * MapAtlas::kHeight - crop_y) / crop_h) * view.h);
  *px = std::clamp(*px, view.x, view.right() - 1);
  *py = std::clamp(*py, view.y, view.bottom() - 1);
}

Box HudRadarBounds(const Box& view, const HudRadarVisual& visual,
                   float player_center_x, float player_center_y,
                   int destination_player_x, int destination_player_y) {
  constexpr float kAuthoredRadarSize = 195.0f;
  const float scale = static_cast<float>(view.w) / kAuthoredRadarSize;
  const float source_center_x = visual.x + visual.width * 0.5f;
  const float source_center_y = visual.y + visual.height * 0.5f;
  const int center_x = destination_player_x + static_cast<int>(std::lround(
      (source_center_x - player_center_x) * scale));
  const int center_y = destination_player_y + static_cast<int>(std::lround(
      (source_center_y - player_center_y) * scale));
  const int width = std::clamp(static_cast<int>(std::lround(
      visual.width * visual.scale_x * scale)), 3, view.w);
  const int height = std::clamp(static_cast<int>(std::lround(
      visual.height * visual.scale_y * scale)), 3, view.h);
  return Box(center_x - width / 2, center_y - height / 2, width, height);
}

bool EmbeddedRadarTexture(const std::string& key) {
  static constexpr const char* kTextures[] = {
      "radar_bg", "radar_default", "radar_dot", "radar_icon_boat",
      "radar_icon_car", "radar_icon_coopplayer",
      "radar_icon_destination", "radar_icon_dropitem",
      "radar_icon_enemy", "radar_icon_enemy_red",
      "radar_icon_enemy_red_small", "radar_icon_enemy_small",
      "radar_icon_escortsurvivor", "radar_icon_fasttravel",
      "radar_icon_ladder", "radar_icon_player",
      "radar_icon_questgiver", "radar_icon_questhub",
      "radar_icon_reward", "radar_icon_shop", "radar_icon_star",
      "radar_icon_star_small", "radar_icon_storage",
      "radar_icon_takeitem", "radar_icon_townportal",
      "radar_icon_waypoint", "radar_icon_workbench",
  };
  for (const char* texture : kTextures) {
    if (key == texture) return true;
  }
  return false;
}

bool DrawLiveHudRadar(Canvas& canvas, const Box& view,
                      const HudRadarSnapshot& radar) {
  if (!radar.available || radar.player.width <= 0.0f ||
      radar.player.height <= 0.0f) {
    return false;
  }
  // The native radar background is a circular mask authored for the small
  // top-screen HUD. On the second screen it left rounded dead space inside a
  // rectangular card. Use the same native world atlas as the Map page as a
  // full-bleed square background, while retaining the HUD controller's exact
  // route and objective overlays below.
  const NativeMapSnapshot map = TheNativeMap().Snapshot();
  bool map_drawn = false;
  int destination_player_x = view.x + view.w / 2;
  int destination_player_y = view.y + view.h / 2;
  if (map.available()) {
    constexpr float kMiniMapZoom = 3.35f;
    const float player_u = std::clamp(map.schematic_u, 0.0f, 1.0f);
    const float player_v = std::clamp(1.0f - map.schematic_v, 0.0f, 1.0f);
    // Load the current area's native atlas from the minimap itself. The old
    // IfReady path used a grid until the full Map page happened to populate
    // the cache, then changed backgrounds mid-session. Besides looking like a
    // broken page switch, that transition exposed the old approximate origin.
    map_drawn = TheMapAtlas().SupportsLevel(map.level_name) &&
        TheMapAtlas().DrawViewport(canvas, view, map.level_name,
                                   player_u, player_v, kMiniMapZoom);
    if (map_drawn) {
      MapViewportPlayerPixel(view, player_u, player_v, kMiniMapZoom,
                             &destination_player_x,
                             &destination_player_y);
    }
  }
  if (!map_drawn) {
    canvas.FillRect(view, Color(10, 13, 12));
    for (int i = 1; i < 6; ++i) {
      const int x = view.x + view.w * i / 6;
      const int y = view.y + view.h * i / 6;
      canvas.Line(x, view.y, x, view.bottom(), 1,
                  theme::kEdge.Alpha(150));
      canvas.Line(view.x, y, view.right(), y, 1,
                  theme::kEdge.Alpha(150));
    }
  }

  const float player_center_x = radar.player.x + radar.player.width * 0.5f;
  const float player_center_y = radar.player.y + radar.player.height * 0.5f;
  bool have_previous_path = false;
  int previous_x = 0;
  int previous_y = 0;
  for (const HudRadarVisual& path : radar.path) {
    const Box destination = HudRadarBounds(
        view, path, player_center_x, player_center_y,
        destination_player_x, destination_player_y);
    const int cx = destination.x + destination.w / 2;
    const int cy = destination.y + destination.h / 2;
    if (!view.Inset(3).Contains(cx, cy)) {
      // The vanilla radar masks off-map route nodes. The custom CPU canvas
      // has no material mask, so break the segment at the edge instead of
      // letting it bleed into the neighboring vitals panel.
      have_previous_path = false;
      continue;
    }
    const int thickness = std::clamp(std::min(destination.w, destination.h),
                                     3, 8);
    if (have_previous_path)
      canvas.Line(previous_x, previous_y, cx, cy, thickness,
                  Color(254, 1, 1));
    canvas.FillCircle(cx, cy, std::max(2, thickness / 2),
                      Color(255, 74, 74));
    if (Lower(path.texture).find("dn_arrow") != std::string::npos) {
      canvas.Line(cx - 6, cy - 4, cx, cy + 3, 3, Color(255, 74, 74));
      canvas.Line(cx, cy + 3, cx + 6, cy - 4, 3, Color(255, 74, 74));
    }
    previous_x = cx;
    previous_y = cy;
    have_previous_path = true;
  }

  for (const HudRadarVisual& marker : radar.markers) {
    Box destination = HudRadarBounds(
        view, marker, player_center_x, player_center_y,
        destination_player_x, destination_player_y);
    if (destination.right() <= view.x || destination.x >= view.right() ||
        destination.bottom() <= view.y || destination.y >= view.bottom()) {
      continue;
    }
    destination.w = std::min(destination.w, view.w);
    destination.h = std::min(destination.h, view.h);
    destination.x = std::clamp(destination.x, view.x,
                               view.right() - destination.w);
    destination.y = std::clamp(destination.y, view.y,
                               view.bottom() - destination.h);
    const std::string key = Lower(marker.texture);
    const bool objective = HasAny(
        key, {"destination", "waypoint", "objective", "target"});
    const bool hostile = HasAny(key, {"enemy", "infected", "zombie"});
    const bool friendly = HasAny(key, {"coop", "friend", "player"});
    const Color tint = (objective || hostile)
        ? Color(254, 1, 1)
        : (friendly ? theme::kGood : Color(255, 255, 255));
    if (!EmbeddedRadarTexture(key) ||
        !TheItemAtlas().Draw(canvas, destination, key,
                             marker.label, tint)) {
      canvas.FillCircle(destination.x + destination.w / 2,
                        destination.y + destination.h / 2,
                        std::max(4, std::min(destination.w,
                                             destination.h) / 3), tint);
    }
    if (marker.up) {
      const int cx = destination.x + destination.w / 2;
      const int y = std::clamp(destination.y - 7, view.y + 1,
                               std::max(view.y + 1, view.bottom() - 8));
      canvas.Line(std::max(view.x, cx - 6), y + 6, cx, y, 3, tint);
      canvas.Line(cx, y, std::min(view.right() - 1, cx + 6), y + 6,
                  3, tint);
    }
    if (!marker.label.empty()) {
      const int label_w = std::min(view.w, destination.w + 48);
      const int label_x = std::clamp(destination.x - 24, view.x,
                                     view.right() - label_w);
      int label_y = destination.bottom() + 1;
      if (label_y + 19 > view.bottom()) label_y = destination.y - 20;
      label_y = std::clamp(label_y, view.y, view.bottom() - 19);
      canvas.Text(Box(label_x, label_y, label_w, 19),
                  marker.label, FontSpec{12, FW_BOLD}, tint,
                  Align::kCenter);
    }
  }

  HudRadarVisual player = radar.player;
  // The custom square is player-centred just like the vanilla HUD radar. Its
  // live route/marker offsets remain controller-authored around that origin.
  Box player_bounds = HudRadarBounds(
      view, player, player_center_x, player_center_y,
      destination_player_x, destination_player_y);
  player_bounds.w = std::clamp(player_bounds.w, 30, 62);
  player_bounds.h = std::clamp(player_bounds.h, 30, 62);
  player_bounds.x = destination_player_x - player_bounds.w / 2;
  player_bounds.y = destination_player_y - player_bounds.h / 2;
  player_bounds.x = std::clamp(player_bounds.x, view.x,
                               view.right() - player_bounds.w);
  player_bounds.y = std::clamp(player_bounds.y, view.y,
                               view.bottom() - player_bounds.h);
  if (map.available()) {
    // The vanilla HUD icon's rotation is a controller property rather than a
    // separate texture. Draw our directional marker from the same live world
    // forward vector so both position and facing remain explicit on the
    // square minimap.
    DrawMapMarker(canvas, destination_player_x, destination_player_y,
                  map.has_heading ? map.heading_degrees : 0.0f, 11);
  } else {
    std::string player_texture = Lower(player.texture);
    if (!EmbeddedRadarTexture(player_texture))
      player_texture = "radar_icon_player";
    if (!TheItemAtlas().Draw(canvas, player_bounds, player_texture,
                             "PLAYER", Color(255, 255, 255))) {
      canvas.FillCircle(destination_player_x, destination_player_y, 12,
                        theme::kAccent);
    }
  }
  return true;
}

void DrawStatusMinimap(Canvas& canvas, const Box& panel) {
  ReferencePanel(canvas, panel);
  // Keep only the panel's crisp keyline; the map itself now occupies the
  // entire inner rectangle instead of sitting inside a rounded HUD-radar
  // texture with a wide inset.
  const Box view = panel.Inset(3);
  canvas.FillRect(view, Color(10, 13, 12));

  // HudRadar is the authoritative objective-navigation feed. The game itself
  // has already computed the live route and marker positions; rendering its
  // owned snapshot here avoids projecting objective data through the full-map
  // terrain schematic, whose private tile origin is different.
  const HudRadarSnapshot& radar = TheNativeUi().hud().radar;
  if (DrawLiveHudRadar(canvas, view, radar)) {
    const Box live(view.right() - 69, view.y + 8, 58, 27);
    canvas.FillRect(live, Color(3, 4, 4, 205));
    canvas.FillCircle(live.x + 10, live.y + live.h / 2, 4, theme::kGood);
    canvas.Text(Box(live.x + 18, live.y, live.w - 20, live.h), "LIVE",
                FontSpec{12, FW_BOLD}, theme::kText, Align::kLeft);
    canvas.StrokeRect(view, 2, theme::kEdgeBright);
    return;
  }

  const NativeMapSnapshot map = TheNativeMap().Snapshot();
  if (!map.available()) {
    canvas.Text(Box(view.x + 18, view.y + view.h / 2 - 34,
                    view.w - 36, 34),
                "MINIMAP", theme::kFontTitle, theme::kTextDim,
                Align::kCenter);
    canvas.TextWrap(Box(view.x + 28, view.y + view.h / 2 + 8,
                        view.w - 56, 70),
                    "Synchronizing with the live player position",
                    theme::kFontSmall, theme::kTextFaint, Align::kCenter);
    canvas.StrokeRect(view, 2, theme::kEdgeBright);
    return;
  }

  const float player_u = std::clamp(map.schematic_u, 0.0f, 1.0f);
  const float player_v = std::clamp(1.0f - map.schematic_v, 0.0f, 1.0f);
  constexpr float kMiniMapZoom = 3.35f;
  const bool native_map_drawn = TheMapAtlas().SupportsLevel(map.level_name) &&
      TheMapAtlas().DrawViewport(canvas, view, map.level_name,
                                 player_u, player_v, kMiniMapZoom);

  if (!native_map_drawn) {
    for (int i = 1; i < 6; ++i) {
      const int x = view.x + view.w * i / 6;
      const int y = view.y + view.h * i / 6;
      canvas.Line(x, view.y, x, view.bottom(), 1, theme::kEdge.Alpha(150));
      canvas.Line(view.x, y, view.right(), y, 1, theme::kEdge.Alpha(150));
    }
  }

  int px = view.x + static_cast<int>(player_u * std::max(0, view.w - 1));
  int py = view.y + static_cast<int>(player_v * std::max(0, view.h - 1));
  if (native_map_drawn) {
    MapViewportPlayerPixel(view, player_u, player_v, kMiniMapZoom,
                           &px, &py);
  }

  canvas.FillRect(Box(view.x, view.y, view.w, 38), Color(3, 4, 4, 205));
  canvas.Text(Box(view.x + 13, view.y, view.w - 92, 38), "MINIMAP",
              theme::kFontLabel, theme::kText, Align::kLeft);
  canvas.FillCircle(view.right() - 63, view.y + 19, 4, theme::kGood);
  canvas.Text(Box(view.right() - 54, view.y, 42, 38), "LIVE",
              theme::kFontLabel, theme::kTextDim, Align::kLeft);
  DrawMapMarker(canvas, px, py,
                map.has_heading ? map.heading_degrees : 0.0f, 15);
  canvas.StrokeRect(view, 2, theme::kEdgeBright);
}

void DrawStatusPage(Canvas& canvas, const Box& body, Interface& ui, Game& game,
                    GameState& state) {
  const Vitals& v = state.vitals();
  const HudSnapshot& hud = TheNativeUi().hud();
  if (!v.usable()) {
    // Three different reasons for an empty page, and they want three
    // different sentences. Saying "the level is loaded" before a save has been
    // opened sends someone looking for a bug that is not there.
    const char* title = "No player state";
    std::string detail;
    if (!game.has_level()) {
      title = "Waiting for the level";
      detail = "Load a save or start a new game. The player search begins the "
               "moment the game asks the engine for its UI manager.";
    } else if (game.searching()) {
      title = "Locating the player";
      detail = Fmt("Scanning level objects for the one carrying player state. "
                   "%d ids examined so far.", game.scanned());
    } else {
      detail = "The level is loaded, but the live player state has not bound yet.";
    }
    EmptyState(canvas, body, title, detail);
    return;
  }

  const FontSpec label{27, FW_BOLD};
  const FontSpec number{64, FW_BOLD};
  const FontSpec mega{88, FW_BOLD};
  const int top_h = 275;
  const int top_w = (body.w - 2 * theme::kGap) / 3;
  const Box level(body.x, body.y, top_w, top_h);
  const Box equipped(level.right() + theme::kGap, body.y, top_w, top_h);
  const Box money(equipped.right() + theme::kGap, body.y,
                  body.right() - equipped.right() - theme::kGap, top_h);
  ReferencePanel(canvas, level);
  ReferencePanel(canvas, equipped);
  ReferencePanel(canvas, money);

  const GearSnapshot& gear = TheNativeUi().gear();
  const NativeUiRow* selected = nullptr;
  for (const NativeUiRow& row : gear.quick_slots) {
    if (row.selected) {
      selected = &row;
      break;
    }
  }
  const std::string equipped_name = !hud.equipped_name.empty()
      ? hud.equipped_name
      : (selected && !selected->text.empty() ? selected->text.front()
                                             : "NO LIVE ITEM SELECTED");
  const bool has_equipped = !hud.equipped_name.empty() || selected;
  const std::string equipped_texture = !hud.icon_texture.empty()
      ? hud.icon_texture : (selected ? selected->icon_texture : std::string());

  // Level card.
  canvas.Text(Box(level.x, level.y + 22, level.w, 38), "LEVEL", label,
              theme::kText, Align::kCenter);
  DrawImpactEmblem(canvas, Box(level.x + 46, level.y + 73, 116, 116),
                   ImpactGlyph::kLevel, v.level.valid);
  canvas.Text(Box(level.x + 177, level.y + 72, level.w - 194, 110),
              v.level.valid ? Fmt("%d", v.level.i()) : "--", mega,
              v.level.valid ? theme::kAccent : theme::kTextFaint,
              Align::kCenter);
  const Box xp(level.x + 25, level.bottom() - 73, level.w - 50, 23);
  Value xp_current = v.experience;
  Value xp_maximum = v.exp_to_next;
  double live_xp = 0.0;
  double live_xp_maximum = 0.0;
  // HudPlayer/G_XpText is an ephemeral gain animation and can retain its
  // authored placeholder while hidden. The signature-gated progression
  // source is authoritative; use HUD text only as a compatibility fallback
  // when a future build cannot expose either raw value.
  if ((!xp_current.valid || !xp_maximum.valid) &&
      ParseHudFraction(hud.xp, &live_xp, &live_xp_maximum)) {
    xp_current = {live_xp, true, "HudPlayer/T_Xp"};
    xp_maximum = {live_xp_maximum, true, "HudPlayer/T_Xp"};
  }
  SegmentedBar(canvas, xp, xp_current, xp_maximum, theme::kAccent, 12);
  canvas.Text(Box(level.x + 20, xp.bottom() + 7, level.w - 40, 30),
              xp_current.valid && xp_maximum.valid
                  ? Group(xp_current.v) + " / " + Group(xp_maximum.v) + " XP"
                  : "XP UNAVAILABLE",
              theme::kFontBody, xp_current.valid ? theme::kText
                                                   : theme::kTextFaint,
              Align::kCenter);

  // Use the same quiet card treatment as Money and Level. A solid red banner
  // made this one panel look like a warning despite representing normal live
  // equipment.
  canvas.Text(Box(equipped.x, equipped.y + 22, equipped.w, 38),
              "EQUIPPED ITEM", label, theme::kText, Align::kCenter);
  canvas.TextWrap(Box(equipped.x + 20, equipped.y + 59,
                      equipped.w - 40, 38),
                  equipped_name, theme::kFontTitle,
                  has_equipped ? theme::kText : theme::kTextFaint,
                  Align::kCenter);
  if (has_equipped) {
    DrawItemGlyph(canvas,
                  Box(equipped.x + 58, equipped.y + 99,
                      equipped.w - 116, 101),
                  equipped_texture, equipped_name, theme::kTextDim);
  }
  std::string durability_text = hud.durability;
  double durability_current = 0.0;
  double durability_maximum = 0.0;
  bool has_durability = ParseHudFraction(
      durability_text, &durability_current, &durability_maximum);
  if (!has_durability) {
    // Depending on weapon type, the HUD presenter places durability in the
    // generic item-stat rows rather than G_Value/T_Value. Accept the first
    // real current/max fraction, while leaving ammo's dedicated fields alone.
    for (const std::string& stat : hud.item_stats) {
      if (ParseHudFraction(stat, &durability_current,
                           &durability_maximum)) {
        durability_text = stat;
        has_durability = true;
        break;
      }
    }
  }

  std::string equipped_detail;
  if (!hud.clip_ammo.empty() || !hud.total_ammo.empty()) {
    equipped_detail = "AMMO  " + hud.clip_ammo;
    if (!hud.total_ammo.empty()) equipped_detail += " / " + hud.total_ammo;
  } else if (has_durability) {
    equipped_detail = "DURABILITY  " + durability_text;
  } else if (has_equipped) {
    equipped_detail = "LIVE EQUIPMENT";
  } else {
    equipped_detail = "SELECT AN ITEM FROM INVENTORY";
  }
  if (has_durability) {
    const Box bar(equipped.x + 24, equipped.bottom() - 57,
                  equipped.w - 48, 15);
    canvas.FillRect(bar, Color(3, 4, 5));
    canvas.StrokeRect(bar, 2, Color(0, 0, 0));
    const float ratio = std::clamp(static_cast<float>(
        durability_current / durability_maximum), 0.0f, 1.0f);
    const Box fill(bar.x + 3, bar.y + 3,
                   static_cast<int>((bar.w - 6) * ratio), bar.h - 6);
    if (fill.w > 0) {
      canvas.HGradient(fill, theme::kGood.Mix(theme::kText, 0.08f),
                       theme::kGood);
    }
    canvas.Text(Box(equipped.x + 20, equipped.bottom() - 37,
                    equipped.w - 40, 24),
                "DURABILITY  " + durability_text, theme::kFontLabel,
                theme::kText, Align::kCenter);
  } else {
    canvas.FillRect(Box(equipped.x + 20, equipped.bottom() - 44,
                        equipped.w - 40, 2), theme::kEdge);
    canvas.Text(Box(equipped.x + 20, equipped.bottom() - 38,
                    equipped.w - 40, 28),
                equipped_detail, theme::kFontLabel,
                has_equipped ? theme::kAccent : theme::kTextFaint,
                Align::kCenter);
  }

  // Cash replaces the old skill-points card. Skill points remain visible and
  // actionable on the Skills page where they have useful context.
  canvas.Text(Box(money.x, money.y + 22, money.w, 38), "MONEY", label,
              theme::kText, Align::kCenter);
  DrawImpactEmblem(canvas, Box(money.x + 36, money.y + 82, 112, 112),
                   ImpactGlyph::kMoney, v.money.valid);
  canvas.Text(Box(money.x + 146, money.y + 84, money.w - 160, 112),
              v.money.valid ? "$" + Group(v.money.v) : "--", number,
              v.money.valid ? theme::kText : theme::kTextFaint,
              Align::kCenter);
  canvas.Text(Box(money.x + 24, money.bottom() - 42, money.w - 48, 24),
              "CURRENT CASH", theme::kFontLabel, theme::kAccent,
              Align::kCenter);

  const int lower_y = level.bottom() + theme::kGap;
  const int quick_h = 148;
  const int lower_h = body.bottom() - lower_y - quick_h - theme::kGap;
  const int minimap_side = std::min(lower_h, body.w / 2);
  const Box minimap(body.right() - minimap_side, lower_y,
                    minimap_side, minimap_side);
  const Box vitals(body.x, lower_y,
                   minimap.x - body.x - theme::kGap, lower_h);
  ReferencePanel(canvas, vitals);

  struct VitalRow { const char* name; const Value* value; const Value* maximum;
                    Color color; int icon; };
  const VitalRow rows[] = {
      {"HEALTH", &v.health, &v.max_health, theme::kBlood, 0},
      {"STAMINA", &v.stamina, &v.max_stamina, theme::kStamina, 1},
      {"FURY", &v.fury, &v.max_fury, theme::kFury, 2},
  };
  const int row_h = lower_h / 3;
  for (int i = 0; i < 3; ++i) {
    const Box row(vitals.x, vitals.y + i * row_h, vitals.w,
                  i == 2 ? vitals.bottom() - (vitals.y + i * row_h) : row_h);
    if (i) canvas.FillRect(Box(row.x, row.y, row.w, 2), theme::kEdge);
    const Box icon(row.x + 24, row.y + (row.h - 78) / 2, 78, 78);
    DrawVitalIcon(canvas, icon, rows[i].icon);
    canvas.Text(Box(row.x + 126, row.y + 19, row.w - 152, 36), rows[i].name,
                label, theme::kText, Align::kLeft);
    const std::string reading = rows[i].value->valid
        ? Fmt("%d / %d", rows[i].value->i(),
              rows[i].maximum->valid ? rows[i].maximum->i() : 0)
        : "--";
    canvas.Text(Box(row.x + 300, row.y + 18, row.w - 326, 38), reading,
                theme::kFontTitle, theme::kText, Align::kRight);
    SegmentedBar(canvas, Box(row.x + 126, row.y + 67, row.w - 152, 39),
                 *rows[i].value, *rows[i].maximum, rows[i].color, 15);
  }
  if (v.fury_active.valid && v.fury_active.v != 0.0)
    canvas.FillRect(Box(vitals.right() - 8, vitals.y + 2 * row_h + 10,
                        5, row_h - 20), theme::kFury);

  DrawStatusMinimap(canvas, minimap);

  const Box quick(body.x, vitals.bottom() + theme::kGap, body.w, quick_h);
  DrawQuickSlotStrip(canvas, ui, quick, state, "");
}

// --------------------------------------------------------------------- DATA
// The discovery view. Everything here is read from the running game.
void DrawDataPage(Canvas& canvas, const Box& body, Game& game, GameState& state) {
  const int col_w = (body.w - theme::kGap) / 2;

  // Left: what the search found.
  Box left = Card(canvas, Box(body.x, body.y, col_w, body.h), "PLAYER OBJECT");
  int y = left.y;
  const int line = 30;

  auto row = [&](const std::string& label, const std::string& value, bool ok) {
    canvas.Text(Box(left.x, y, 170, line), label, theme::kFontSmall,
                theme::kTextDim, Align::kLeft);
    canvas.Text(Box(left.x + 178, y, left.w - 178, line), value,
                theme::kFontSmall, ok ? theme::kText : theme::kTextFaint,
                Align::kLeft);
    y += line;
  };

  row("reflection", TheRtti().ready() ? "resolved" : "UNAVAILABLE",
      TheRtti().ready());
  row("level", game.has_level() ? "captured" : "waiting", game.has_level());
  row("class", game.player_class().empty() ? "not found" : game.player_class(),
      !game.player_class().empty());
  row("ids scanned", Fmt("%d", game.scanned()), true);
  row("state", state.bound() ? "bound" : "unbound", state.bound());

  y += 28;
  canvas.Text(Box(left.x, y, left.w, 24), "BOUND FIELDS", theme::kFontLabel,
              theme::kTextDim, Align::kLeft);
  canvas.FillRect(Box(left.x, y + 26, 34, 2), theme::kAccent);
  y += 48;

  const Vitals& v = state.vitals();
  const InventoryCapacity& capacity = state.inventory_capacity();
  struct Bound { const char* label; const Value* value; };
  const Bound bound[] = {
      {"health", &v.health},       {"max health", &v.max_health},
      {"stamina", &v.stamina},     {"max stamina", &v.max_stamina},
      {"fury", &v.fury},           {"max fury", &v.max_fury},
      {"level", &v.level},         {"experience", &v.experience},
      {"to next", &v.exp_to_next}, {"money", &v.money},
      {"skill points", &v.skill_points},
      {"inventory", &capacity.slots},
      {"inventory max", &capacity.max_slots},
      {"quick slots", &capacity.quick_slots},
      {"quick max", &capacity.max_quick_slots},
  };
  for (const Bound& b : bound) {
    if (y + line > left.bottom()) break;
    const bool ok = b.value->valid;
    canvas.Text(Box(left.x, y, 170, line), b.label, theme::kFontSmall,
                theme::kTextDim, Align::kLeft);
    canvas.Text(Box(left.x + 178, y, left.w - 178, line),
                ok ? (b.value->bound_to ? b.value->bound_to : "?") : "no match",
                theme::kFontSmall, ok ? theme::kGood.Mix(theme::kText, 0.5f)
                                      : theme::kTextFaint,
                Align::kLeft);
    y += line;
  }

  // Right: every class the sweep saw. On a build where the player was not
  // found, this list is the answer -- the class is in it.
  Box right = Card(canvas, Box(body.x + col_w + theme::kGap, body.y, col_w, body.h),
                    "CLASSES SEEN");
  const auto& classes = game.seen_classes();
  if (classes.empty()) {
    canvas.Text(Box(right.x, right.y, right.w, 40),
                game.has_level() ? "scan has not run yet" : "waiting for level",
                theme::kFontSmall, theme::kTextFaint, Align::kLeft);
    return;
  }
  int ry = right.y;
  for (const auto& entry : classes) {
    if (ry + 26 > right.bottom()) {
      canvas.Text(Box(right.x, ry, right.w, 26),
                  Fmt("... and more; the full list is in the log"),
                  theme::kFontSmall, theme::kTextFaint, Align::kLeft);
      break;
    }
    canvas.Text(Box(right.x, ry, right.w - 70, 26), entry.name,
                theme::kFontSmall, theme::kText, Align::kLeft);
    canvas.Text(Box(right.right() - 70, ry, 70, 26), Fmt("x%d", entry.count),
                theme::kFontSmall, theme::kTextDim, Align::kRight);
    ry += 26;
  }
}

std::string RowPrimary(const NativeUiRow& row, const char* fallback, int index) {
  if (!row.text.empty() && !row.text[0].empty()) return row.text[0];
  return Fmt("%s %d", fallback, index + 1);
}

std::string RowSecondary(const NativeUiRow& row) {
  if (row.text.size() > 1) return row.text[1];
  return {};
}

void ActionButton(Canvas& canvas, Interface& ui, const Box& box,
                   const std::string& label, UiAction action, int index = -1,
                   bool enabled = true, bool modal = false) {
  const bool pressed = ui.TargetPressed(action, index);
  Color fill = enabled ? theme::kSurfaceHigh : theme::kSurface;
  if (pressed) fill = fill.Mix(theme::kAccent, 0.24f);
  canvas.FillRoundRect(box, theme::kRadius, fill);
  canvas.StrokeRoundRect(box, theme::kRadius, pressed ? 2 : 1,
                         pressed ? theme::kAccent
                                 : (enabled ? theme::kEdgeBright : theme::kEdge));
  canvas.Text(box.Inset(10, 6), label, theme::kFontBody,
              enabled ? theme::kText : theme::kTextFaint, Align::kCenter);
  if (modal) {
    ui.AddModalTarget(box, action, index, enabled);
  } else {
    ui.AddTarget(box, action, index, enabled);
  }
}

void NativeRow(Canvas& canvas, Interface& ui, const Box& box,
               const NativeUiRow& row, const std::string& primary,
               const std::string& secondary, UiAction action, int index) {
  const bool pressed = ui.TargetPressed(action, index);
  const Color fill = row.selected
      ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.16f)
      : (pressed ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.10f)
                 : theme::kSurfaceHigh);
  canvas.FillRoundRect(box, theme::kRadius, fill);
  canvas.StrokeRoundRect(box, theme::kRadius, row.selected ? 2 : 1,
                         row.selected ? theme::kAccent : theme::kEdge);
  if (row.selected) canvas.FillRect(Box(box.x, box.y, 4, box.h), theme::kAccent);
  const int text_x = box.x + 16;
  if (secondary.empty()) {
    canvas.Text(Box(text_x, box.y, box.w - 32, box.h), primary,
                theme::kFontBody, row.enabled ? theme::kText : theme::kTextDim,
                Align::kLeft);
  } else {
    canvas.Text(Box(text_x, box.y + 8, box.w - 32, 30), primary,
                theme::kFontBody, row.enabled ? theme::kText : theme::kTextDim,
                Align::kLeft);
    canvas.Text(Box(text_x, box.y + 39, box.w - 32, box.h - 45), secondary,
                theme::kFontSmall, theme::kTextDim, Align::kLeft);
  }
  ui.AddTarget(box, action, index, row.enabled);
}

void ScrollButtons(Canvas& canvas, Interface& ui, const Box& area,
                   int offset, int maximum, int visible_count) {
  const int side = std::min(68, area.h);
  const Box up(area.x, area.y, side, side);
  const Box down(area.right() - side, area.y, side, side);
  ActionButton(canvas, ui, up, "<", UiAction::kScrollUp, -1, offset > 0);
  ActionButton(canvas, ui, down, ">", UiAction::kScrollDown, -1,
               offset < maximum);
  canvas.Text(Box(up.right() + 12, area.y, area.w - 2 * side - 24, side),
              maximum > 0 ? Fmt("%d - %d", offset + 1,
                                 std::min(offset + visible_count,
                                          maximum + visible_count))
                          : "ALL",
              theme::kFontSmall, theme::kTextDim, Align::kCenter);
}

// ---------------------------------------------------------------------- MAP
// Outdoor areas use the game's own 16x16 map tiles, decoded from their
// lightweight 64px mip once per area. Player position and heading remain live
// engine values; unsupported interior/DLC areas retain the honest terrain
// overview instead of showing artwork from the wrong level.
void DrawMapPage(Canvas& canvas, const Box& body, Interface& ui) {
  const NativeMapSnapshot map = TheNativeMap().Snapshot();
  if (!map.available()) {
    std::string detail = NativeMapAvailabilityText(map.availability);
    if (!map.level_name.empty()) detail += " in " + map.level_name + ".";
    if (map.availability == NativeMapAvailability::kNoPlayer) {
      detail += " The page will update automatically when the local player is bound.";
    }
    EmptyState(canvas, body, "Map not ready", detail);
    return;
  }

  const int info_w = 300;
  const Box map_card(body.x, body.y, body.w - info_w - theme::kGap, body.h);
  const Box info_card(map_card.right() + theme::kGap, body.y, info_w, body.h);
  Box field = Card(canvas, map_card,
                   map.level_name.empty() ? "AREA MAP"
                                          : "AREA MAP  /  " + map.level_name);
  Box info = Card(canvas, info_card, "LIVE POSITION");

  const bool atlas_supported = TheMapAtlas().SupportsLevel(map.level_name);
  Box view = field;
  if (!atlas_supported) {
    // Preserve the x:z ratio for the fallback schematic. Empty margins are
    // intentional and never stretch an unknown interior map.
    const float aspect = map.terrain_size.z > 0.0f
        ? map.terrain_size.x / map.terrain_size.z : 1.0f;
    int view_w = field.w;
    int view_h = static_cast<int>(view_w / std::max(0.01f, aspect));
    if (view_h > field.h) {
      view_h = field.h;
      view_w = static_cast<int>(view_h * aspect);
    }
    view = Box(field.x + (field.w - view_w) / 2,
               field.y + (field.h - view_h) / 2, view_w, view_h);
  }
  canvas.FillRect(view, Color(14, 18, 16));
  canvas.StrokeRect(view, 2, theme::kEdgeBright);

  const float player_u = std::clamp(map.schematic_u, 0.0f, 1.0f);
  // The stitched texture is top-down while world +Z is drawn north/up.
  const float player_atlas_v =
      std::clamp(1.0f - map.schematic_v, 0.0f, 1.0f);
  ui.SetMapViewport(view, player_u, player_atlas_v, map.level_name);
  const float zoom = ui.MapZoom();
  const float center_u = ui.MapCenterU();
  const float center_v = ui.MapCenterV();
  const bool native_map_drawn = atlas_supported &&
      TheMapAtlas().DrawViewport(canvas, view, map.level_name,
                                 center_u, center_v, zoom);
  canvas.StrokeRect(view, 2, theme::kEdgeBright);

  // Survey-style corner brackets and a north reference stay legible on both
  // the real native art and the schematic fallback.
  constexpr int kBracket = 28;
  const Color bracket = theme::kTextDim.Alpha(180);
  canvas.Line(view.x + 8, view.y + 8, view.x + 8 + kBracket, view.y + 8,
              2, bracket);
  canvas.Line(view.x + 8, view.y + 8, view.x + 8, view.y + 8 + kBracket,
              2, bracket);
  canvas.Line(view.right() - 9 - kBracket, view.y + 8,
              view.right() - 9, view.y + 8, 2, bracket);
  canvas.Line(view.right() - 9, view.y + 8, view.right() - 9,
              view.y + 8 + kBracket, 2, bracket);
  canvas.Text(Box(view.x + view.w / 2 - 28, view.y + 9, 56, 28), "N",
              theme::kFontBody, theme::kTextDim, Align::kCenter);
  canvas.Line(view.x + view.w / 2, view.y + 37,
              view.x + view.w / 2, view.y + 54, 2, theme::kAccent);

  if (!native_map_drawn) {
    // A restrained coordinate grid makes the live fallback useful without
    // pretending those lines are authored roads or buildings.
    for (int i = 1; i < 8; ++i) {
      const int x = view.x + (view.w * i) / 8;
      const int y = view.y + (view.h * i) / 8;
      canvas.Line(x, view.y, x, view.bottom(), 1, theme::kEdge.Alpha(150));
      canvas.Line(view.x, y, view.right(), y, 1, theme::kEdge.Alpha(150));
    }
    canvas.Line(view.x + view.w / 2, view.y, view.x + view.w / 2,
                view.bottom(), 2, theme::kEdgeBright.Alpha(170));
    canvas.Line(view.x, view.y + view.h / 2, view.right(),
                view.y + view.h / 2, 2, theme::kEdgeBright.Alpha(170));
  }

  int px = view.x + static_cast<int>(player_u * std::max(0, view.w - 1));
  int py = view.y +
      static_cast<int>(player_atlas_v * std::max(0, view.h - 1));
  if (native_map_drawn) {
    // Mirror MapAtlas::DrawViewport's crop so the marker stays exactly over
    // its real-map location, including when the crop clamps at an area edge.
    const float view_aspect = static_cast<float>(view.w) /
                              std::max(1, view.h);
    float source_w = static_cast<float>(MapAtlas::kWidth);
    float source_h = static_cast<float>(MapAtlas::kHeight);
    if (view_aspect >= 1.0f) source_h = source_w / view_aspect;
    else source_w = source_h * view_aspect;
    source_w /= zoom;
    source_h /= zoom;
    const int crop_w = std::max(1, std::min(MapAtlas::kWidth,
        static_cast<int>(std::lround(source_w))));
    const int crop_h = std::max(1, std::min(MapAtlas::kHeight,
        static_cast<int>(std::lround(source_h))));
    const int crop_x = std::max(0, std::min(MapAtlas::kWidth - crop_w,
        static_cast<int>(std::lround(
            center_u * MapAtlas::kWidth - crop_w * 0.5f))));
    const int crop_y = std::max(0, std::min(MapAtlas::kHeight - crop_h,
        static_cast<int>(std::lround(
            center_v * MapAtlas::kHeight - crop_h * 0.5f))));
    px = view.x + static_cast<int>(
        ((player_u * MapAtlas::kWidth - crop_x) / crop_w) * view.w);
    py = view.y + static_cast<int>(
        ((player_atlas_v * MapAtlas::kHeight - crop_y) / crop_h) * view.h);
    px = std::clamp(px, view.x, view.right() - 1);
    py = std::clamp(py, view.y, view.bottom() - 1);
  }
  const float angle = map.heading_degrees * 0.017453292519943295f;
  const float dx = std::sin(angle);
  const float dy = -std::cos(angle);
  const int tip_x = px + static_cast<int>(dx * 26.0f);
  const int tip_y = py + static_cast<int>(dy * 26.0f);
  const int left_x = px + static_cast<int>((-dx - dy) * 10.0f);
  const int left_y = py + static_cast<int>((-dy + dx) * 10.0f);
  const int right_x = px + static_cast<int>((-dx + dy) * 10.0f);
  const int right_y = py + static_cast<int>((-dy - dx) * 10.0f);
  // A high-contrast directional marker remains readable over pale roads,
  // dark jungle and the map's red points of interest. The center dot is the
  // exact player coordinate; the arrow rotates with the live heading.
  canvas.FillCircle(px, py, 20, Color(0, 0, 0, 210));
  canvas.StrokeCircle(px, py, 20, 3, theme::kText);
  canvas.Line(px, py, tip_x, tip_y, 10, Color(0, 0, 0, 220));
  canvas.Line(tip_x, tip_y, left_x, left_y, 10, Color(0, 0, 0, 220));
  canvas.Line(tip_x, tip_y, right_x, right_y, 10, Color(0, 0, 0, 220));
  canvas.Line(px, py, tip_x, tip_y, 6, theme::kAccent);
  canvas.Line(tip_x, tip_y, left_x, left_y, 6, theme::kAccent);
  canvas.Line(tip_x, tip_y, right_x, right_y, 6, theme::kAccent);
  canvas.FillCircle(px, py, 6, theme::kText);

  int y = info.y;
  auto info_row = [&](const char* label, const std::string& value,
                      const Color& color = theme::kText) {
    canvas.Text(Box(info.x, y, info.w, 22), label, theme::kFontLabel,
                theme::kTextDim, Align::kLeft);
    y += 26;
    canvas.Text(Box(info.x, y, info.w, 34), value, theme::kFontTitle, color,
                Align::kLeft);
    y += 54;
  };
  info_row("WORLD X", Fmt("%.1f", map.world_position.x));
  info_row("WORLD Y", Fmt("%.1f", map.world_position.y));
  info_row("WORLD Z", Fmt("%.1f", map.world_position.z));
  info_row("HEADING", map.has_heading ? Fmt("%.0f deg", map.heading_degrees)
                                       : "unavailable");
  info_row("TERRAIN", Fmt("%.0f x %.0f", map.terrain_size.x,
                            map.terrain_size.z));
  const Box tracking(info.x, info.bottom() - 350, info.w, 42);
  canvas.FillRect(tracking, Color(11, 20, 12));
  canvas.StrokeRect(tracking, 1, theme::kGood);
  canvas.FillCircle(tracking.x + 21, tracking.y + tracking.h / 2, 5,
                    theme::kGood);
  canvas.Text(Box(tracking.x + 36, tracking.y, tracking.w - 48, tracking.h),
              "LIVE TRACKING  /  10 HZ", theme::kFontLabel,
              theme::kGood.Mix(theme::kText, 0.35f), Align::kLeft);
  if (native_map_drawn) {
    canvas.Text(Box(info.x, info.bottom() - 286, info.w, 58),
                "Native area map / live player tracking",
                theme::kFontSmall, theme::kGood, Align::kLeft);
  } else if (!map.position_inside_schematic) {
    canvas.Text(Box(info.x, info.bottom() - 300, info.w, 62),
                "Position is outside the native map bounds; marker is clamped.",
                theme::kFontSmall, theme::kFury, Align::kLeft);
  } else {
    canvas.Text(Box(info.x, info.bottom() - 286, info.w, 44),
                "Live player marker and facing direction",
                theme::kFontSmall, theme::kGood, Align::kLeft);
  }

  const int button_h = 58;
  const int button_y = info.bottom() - 210;
  const int half = (info.w - 10) / 2;
  ActionButton(canvas, ui, Box(info.x, button_y, half, button_h),
               "ZOOM -", UiAction::kMapZoomOut, -1, zoom > 1.01f);
  ActionButton(canvas, ui,
               Box(info.x + half + 10, button_y, half, button_h),
               "ZOOM +", UiAction::kMapZoomIn, -1, zoom < 3.99f);
  ActionButton(canvas, ui, Box(info.x, button_y + button_h + 10,
                               info.w, button_h),
               "CENTER / RESET", UiAction::kMapCenter);
  canvas.Text(Box(info.x, button_y - 38, info.w, 28),
              Fmt("VIEW  %.0f%%", zoom * 100.0f), theme::kFontSmall,
              theme::kTextDim, Align::kCenter);
  canvas.Text(Box(view.x + 18, view.bottom() - 37, view.w - 36, 24),
              "DRAG TO PAN  /  PINCH TO ZOOM", theme::kFontLabel,
              theme::kText.Mix(Color(0, 0, 0), 0.16f), Align::kCenter);
}

// --------------------------------------------------------------- INVENTORY
void DrawInventoryContextMenu(Canvas& canvas, const Box& body, Interface& ui,
                              const InventorySnapshot& inventory,
                              GameState& state) {
  if (!ui.InventoryPopupOpen()) return;
  const int index = ui.InventoryPopupIndex();
  if (index < 0 || index >= static_cast<int>(inventory.items.size())) return;
  const NativeUiRow& item = inventory.items[index];
  const std::string name = RowPrimary(item, "Item", index);

  // Darken, rather than replace, the page below. This makes the sheet's
  // capture state obvious while retaining enough list context to remember
  // which row was held.
  canvas.FillRect(body, theme::kBackground.Alpha(218));
  const int popup_w = std::min(760, body.w - 80);
  const int popup_h = std::min(670, body.h - 40);
  const Box popup(body.x + (body.w - popup_w) / 2,
                  body.y + (body.h - popup_h) / 2,
                  popup_w, popup_h);
  ui.SetInventoryPopupBounds(popup);
  canvas.FillRect(popup, Color(8, 7, 8));
  canvas.StrokeRect(popup, 3, theme::kEdgeBright);
  canvas.StrokeRect(popup.Inset(7), 1, Color(39, 31, 32));

  const Box title(popup.x + 3, popup.y + 3, popup.w - 6, 60);
  canvas.VGradient(title, theme::kAccent, theme::kAccentDeep);
  canvas.Text(Box(title.x + 22, title.y, title.w - 44, title.h),
              "ITEM ACTIONS", theme::kFontTitle, theme::kText,
              Align::kLeft);
  canvas.Text(Box(title.right() - 155, title.y, 128, title.h),
              Fmt("SLOT %02d", index + 1), theme::kFontLabel,
              theme::kText, Align::kRight);

  const Box item_row(popup.x + 22, title.bottom() + 14,
                     popup.w - 44, 96);
  canvas.VGradient(item_row, theme::kSurfaceHigh, theme::kSurface);
  canvas.StrokeRect(item_row, 1, theme::kEdgeBright);
  const Box icon(item_row.x + 12, item_row.y + 8, 80, 80);
  canvas.FillRect(icon, Color(4, 4, 5));
  canvas.StrokeRect(icon, 1, theme::kEdgeBright);
  DrawItemGlyph(canvas, icon.Inset(10), item.icon_texture, name,
                item.enabled ? theme::kText : theme::kTextFaint);
  const Box popup_name(icon.right() + 18, item_row.y + 5,
                       item_row.right() - icon.right() - 35, 48);
  canvas.TextWrap(popup_name, name, FontSpec{19, FW_BOLD},
                  theme::kText, Align::kLeft);
  const std::string amount = ItemQuantityLabel(item);
  canvas.Text(Box(icon.right() + 18, item_row.y + 61,
                  item_row.right() - icon.right() - 35, 27),
              amount.empty() ? ItemKindLabel(item)
                             : std::string(ItemKindLabel(item)) + "  /  " + amount,
              theme::kFontSmall, theme::kTextDim, Align::kLeft);

  const GearSnapshot& gear = TheNativeUi().gear();
  const InventoryCapacity& capacity = state.inventory_capacity();
  int live_slots = std::min(9, static_cast<int>(gear.quick_slots.size()));
  if (capacity.quick_slots.valid) {
    live_slots = std::min(live_slots,
        std::clamp(capacity.quick_slots.i(), 0, 9));
  }

  int y = item_row.bottom() + 10;
  canvas.Text(Box(popup.x + 24, y, popup.w - 48, 25),
              live_slots > 0 ? "ASSIGN TO QUICK SLOT"
                             : "QUICK SLOTS UNAVAILABLE",
              theme::kFontLabel,
              live_slots > 0 ? theme::kAccent : theme::kTextFaint,
              Align::kLeft);
  y += 29;
  if (live_slots > 0) {
    constexpr int kSlotGap = 8;
    const int slot_side = std::min(72,
        (popup.w - 44 - (live_slots - 1) * kSlotGap) / live_slots);
    const int slots_w = live_slots * slot_side +
                        (live_slots - 1) * kSlotGap;
    const int slots_x = popup.x + (popup.w - slots_w) / 2;
    for (int slot_index = 0; slot_index < live_slots; ++slot_index) {
      const Box slot(slots_x + slot_index * (slot_side + kSlotGap), y,
                     slot_side, slot_side);
      const bool pressed = ui.TargetPressed(
          UiAction::kInventoryAssignQuickSlot, index, slot_index);
      canvas.VGradient(slot,
                       pressed
                           ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.30f)
                           : theme::kSurfaceHigh,
                       theme::kSurface);
      canvas.StrokeRect(slot, pressed ? 3 : 1,
                        pressed ? theme::kAccent : theme::kEdgeBright);
      canvas.Text(Box(slot.x + 5, slot.y + 2, slot.w - 10, 20),
                  Fmt("%d", slot_index + 1), theme::kFontLabel,
                  theme::kAccent, Align::kLeft);
      if (slot_index < static_cast<int>(gear.quick_slots.size())) {
        const NativeUiRow& current = gear.quick_slots[slot_index];
        if (current.enabled) {
          DrawItemGlyph(canvas, Box(slot.x + 14, slot.y + 20,
                                    slot.w - 28, slot.h - 27),
                        current.icon_texture,
                        RowPrimary(current, "Item", slot_index),
                        theme::kTextDim);
        }
      }
      ui.AddModalTarget(slot, UiAction::kInventoryAssignQuickSlot,
                        index, item.enabled, slot_index);
    }
    y += slot_side + 12;
  } else {
    y += 18;
  }

  const int button_h = 54;
  const int gap = 8;
  const int x = popup.x + 22;
  const int width = popup.w - 44;
  ActionButton(canvas, ui, Box(x, y, width, button_h),
               "EQUIP / USE", UiAction::kInventoryEquip, index,
               inventory.equip_control_enabled, true);
  y += button_h + gap;
  const int half = (width - gap) / 2;
  ActionButton(canvas, ui, Box(x, y, half, button_h),
               "COMPARE", UiAction::kInventoryCompare, index,
               inventory.compare_control_enabled, true);
  ActionButton(canvas, ui, Box(x + half + gap, y, width - half - gap, button_h),
               "INSPECT", UiAction::kInventoryInspect, index,
               inventory.inspect_control_enabled, true);
  y += button_h + gap;
  ActionButton(canvas, ui, Box(x, y, width, button_h),
               "DROP ITEM", UiAction::kInventoryDrop, index,
               inventory.drop_control_enabled, true);
  y += button_h + gap;
  ActionButton(canvas, ui, Box(x, y, width, button_h),
               "CANCEL", UiAction::kInventoryPopupCancel, index, true, true);

  canvas.Text(Box(popup.x + 26, popup.bottom() - 42, popup.w - 52, 24),
              "Quick-slot changes use the same ownership and eligibility checks as the game.",
              theme::kFontSmall, theme::kTextFaint, Align::kCenter);
}

void DrawInventoryPage(Canvas& canvas, const Box& body, Interface& ui,
                        GameState& state) {
  const InventorySnapshot& inventory = TheNativeUi().inventory();
  const InventoryCapacity& capacity = state.inventory_capacity();
  const std::string capacity_text =
      CapacityLabel(capacity.slots, capacity.max_slots, "SLOTS");
  const int header_h = 72;
  const Box header(body.x, body.y, body.w, header_h);
  ReferencePanel(canvas, header);
  canvas.Text(Box(header.x + 26, header.y, 420, header.h), "INVENTORY",
              theme::kFontBig, theme::kText, Align::kLeft);
  canvas.Text(Box(header.right() - 430, header.y, 400, header.h),
              capacity_text.empty() ? "CAPACITY UNAVAILABLE" : capacity_text,
              theme::kFontTitle,
              capacity_text.empty() ? theme::kTextFaint : theme::kText,
              Align::kRight);

  // Quick slots belong to the always-visible Stats page. The inventory uses
  // the recovered vertical space for a proper backpack list; contextual
  // actions live behind a deliberate long press so an accidental combat tap
  // can never Drop or Use an item.
  const int content_y = header.bottom() + theme::kGap;
  const int detail_w = 360;
  const Box grid_panel(body.x, content_y, body.w - detail_w - theme::kGap,
                       body.bottom() - content_y);
  const Box detail_panel(grid_panel.right() + theme::kGap, content_y, detail_w,
                         body.bottom() - content_y);
  ReferencePanel(canvas, grid_panel);
  ReferencePanel(canvas, detail_panel);

  canvas.Text(Box(grid_panel.x + 20, grid_panel.y + 12,
                  300, 32),
               inventory.filter.empty() ? "BACKPACK" : inventory.filter,
               theme::kFontTitle, theme::kTextDim, Align::kLeft);
  canvas.Text(Box(grid_panel.right() - 420, grid_panel.y + 12,
                   400, 32),
              "SWIPE TO SCROLL  /  HOLD FOR ACTIONS",
              theme::kFontLabel, theme::kTextDim, Align::kRight);
  constexpr int kVisible = 9;
  const int controls_h = 64;
  const int gap = 8;
  const Box list(grid_panel.x + 18, grid_panel.y + 55, grid_panel.w - 36,
                 grid_panel.h - 55 - controls_h - 14);
  const int row_h = std::max(48,
      (list.h - (kVisible - 1) * gap) / kVisible);
  const std::vector<InventoryDisplayEntry> display =
      InventoryDisplayEntries(inventory);
  const int max_offset = std::max(0,
      static_cast<int>(display.size()) - kVisible);
  ui.ClampScroll(Page::kInventory, max_offset);
  const int offset = ui.ScrollOffset(Page::kInventory);
  ui.SetInventoryListViewport(list, row_h + gap, max_offset);
  for (int visible = 0; visible < kVisible; ++visible) {
    const int display_index = offset + visible;
    const Box item_box(list.x, list.y + visible * (row_h + gap),
                       list.w, row_h);
    if (display_index >= static_cast<int>(display.size())) continue;
    const InventoryDisplayEntry& entry = display[display_index];
    if (entry.is_section()) {
      const Box divider(item_box.x, item_box.y + (item_box.h - 38) / 2,
                        item_box.w, 38);
      canvas.HGradient(divider, theme::kAccentMuted,
                       theme::kSurfaceHigh);
      canvas.FillRect(Box(divider.x, divider.y, 6, divider.h),
                      theme::kAccent);
      canvas.Text(Box(divider.x + 20, divider.y, divider.w - 40,
                      divider.h),
                  entry.section, theme::kFontLabel, theme::kText,
                  Align::kLeft);
      canvas.FillRect(Box(divider.x + 138, divider.y + divider.h / 2,
                          divider.w - 158, 1), theme::kEdgeBright);
      continue;
    }

    const int index = entry.item_index;
    if (index < 0 || index >= static_cast<int>(inventory.items.size()))
      continue;
    const NativeUiRow& item = inventory.items[index];
    const bool pressed = ui.TargetPressed(UiAction::kInventoryItem, index);
    const bool selected = item.selected;
    canvas.VGradient(item_box,
                     selected || pressed
                         ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.28f)
                         : theme::kSurfaceHigh,
                     theme::kSurface);
    canvas.StrokeRect(item_box, selected ? 3 : 1,
                      selected ? theme::kAccent
                               : theme::kEdgeBright);
    if (selected)
      canvas.FillRect(Box(item_box.x + 3, item_box.y + 3,
                          5, item_box.h - 6),
                      theme::kAccent);
    canvas.Text(Box(item_box.x + 13, item_box.y, 47, item_box.h),
                Fmt("%02d", index + 1), theme::kFontLabel,
                theme::kAccent, Align::kCenter);
    const Box icon(item_box.x + 67, item_box.y + 7,
                   std::max(34, item_box.h - 14),
                   std::max(34, item_box.h - 14));
    canvas.FillRect(icon, Color(7, 7, 8));
    canvas.StrokeRect(icon, 1, theme::kEdgeBright);
    const std::string item_name = RowPrimary(item, "Item", index);
    DrawItemGlyph(canvas, icon.Inset(7), item.icon_texture, item_name,
                  item.enabled ? theme::kTextDim : theme::kTextFaint);
    const int text_x = icon.right() + 17;
    const std::string amount = ItemQuantityLabel(item);
    const int amount_w = amount.empty() ? 0 : 78;
    const Box name_box(text_x, item_box.y + 4,
                       item_box.right() - text_x - 16 - amount_w,
                       std::max(25, item_box.h - 29));
    canvas.TextWrap(name_box, item_name, FontSpec{17, FW_SEMIBOLD},
                    item.enabled ? theme::kText : theme::kTextDim,
                    Align::kLeft);
    canvas.Text(Box(text_x, item_box.bottom() - 22,
                    item_box.right() - text_x - 16 - amount_w,
                    18),
                ItemKindLabel(item), theme::kFontLabel,
                selected ? theme::kAccent : theme::kTextDim,
                Align::kLeft);
    if (!amount.empty()) {
      canvas.Text(Box(item_box.right() - amount_w - 12, item_box.y,
                      amount_w, item_box.h),
                  amount, theme::kFontBody,
                  selected ? theme::kAccent : theme::kText,
                  Align::kRight);
    }
    ui.AddTarget(item_box, UiAction::kInventoryItem, index, item.enabled);
  }

  if (display.empty()) {
    canvas.Text(Box(list.x + 30, list.y, list.w - 60, list.h),
                inventory.available ? "BACKPACK IS EMPTY"
                                    : "WAITING FOR LIVE INVENTORY",
                theme::kFontTitle, theme::kTextFaint, Align::kCenter);
  }

  ScrollButtons(canvas, ui,
                Box(grid_panel.x + 18, grid_panel.bottom() - controls_h,
                    grid_panel.w - 36, controls_h - 10),
                offset, max_offset, kVisible);

  canvas.VGradient(Box(detail_panel.x, detail_panel.y, detail_panel.w, 54),
                   theme::kAccent, Color(126, 7, 9));
  canvas.Text(Box(detail_panel.x + 20, detail_panel.y, detail_panel.w - 40, 54),
              "SELECTED ITEM", theme::kFontTitle, theme::kText, Align::kLeft);
  const std::string selected = inventory.selected_name.empty()
      ? "SELECT AN ITEM" : inventory.selected_name;
  const NativeUiRow* selected_row = nullptr;
  for (const NativeUiRow& row : inventory.items)
    if (row.selected) { selected_row = &row; break; }
  DrawItemGlyph(canvas,
                Box(detail_panel.x + 105, detail_panel.y + 66,
                    detail_panel.w - 210, 90),
                selected_row ? selected_row->icon_texture : std::string(),
                selected, inventory.selected_name.empty()
                    ? theme::kEdge : theme::kTextDim);
  canvas.TextWrap(Box(detail_panel.x + 24, detail_panel.y + 162,
                      detail_panel.w - 48, 68), selected, theme::kFontTitle,
                  inventory.selected_name.empty() ? theme::kTextFaint
                                                  : theme::kText,
                  Align::kCenter);
  if (!inventory.price.empty())
    canvas.Text(Box(detail_panel.x + 24, detail_panel.y + 228,
                    detail_panel.w - 48, 32), inventory.price,
                theme::kFontBody, theme::kAccent, Align::kCenter);

  std::string description = inventory.description;
  if (!inventory.available) {
    description = inventory.unavailable_reason.empty()
        ? "Inventory data is waiting for the game's controller."
        : inventory.unavailable_reason;
  } else if (description.empty()) {
    description = "Tap an item to inspect it. Hold any item to open its native action menu.";
  }
  canvas.TextWrap(Box(detail_panel.x + 24, detail_panel.y + 278,
                      detail_panel.w - 48, detail_panel.h - 438),
                  description, theme::kFontSmall, theme::kTextDim,
                  Align::kCenter);
  const Box hold_hint(detail_panel.x + 18, detail_panel.bottom() - 132,
                      detail_panel.w - 36, 108);
  canvas.VGradient(hold_hint,
                   theme::kSurfaceHigh.Mix(theme::kAccent, 0.18f),
                   theme::kSurface);
  canvas.StrokeRect(hold_hint, 1, theme::kEdgeBright);
  canvas.FillRect(Box(hold_hint.x, hold_hint.y, 5, hold_hint.h),
                  theme::kAccent);
  canvas.Text(Box(hold_hint.x + 20, hold_hint.y + 14,
                  hold_hint.w - 40, 28),
              "HOLD FOR ITEM ACTIONS", theme::kFontLabel,
              theme::kText, Align::kCenter);
  canvas.TextWrap(Box(hold_hint.x + 22, hold_hint.y + 48,
                      hold_hint.w - 44, 45),
                  "Assign a quick slot, equip, use, compare, inspect or drop through the game's live inventory.",
                  theme::kFontSmall, theme::kTextDim, Align::kCenter);

  DrawInventoryContextMenu(canvas, body, ui, inventory, state);
}

// --------------------------------------------------------------------- GEAR
void DrawGearPage(Canvas& canvas, const Box& body, Interface& ui,
                  GameState& state) {
  const GearSnapshot& gear = TheNativeUi().gear();
  const InventoryCapacity& capacity = state.inventory_capacity();
  const std::string capacity_text = CapacityLabel(
      capacity.quick_slots, capacity.max_quick_slots, "UNLOCKED");
  if (!gear.available) {
    std::string detail = gear.unavailable_reason.empty()
        ? "The native quick-slot group has not initialized yet."
        : gear.unavailable_reason;
    if (!capacity_text.empty()) {
      detail += " Live quick-slot capacity: " + capacity_text + ".";
    }
    EmptyState(canvas, body, "Gear unavailable",
               detail);
    return;
  }

  std::string title = "QUICK SLOTS  /  TAP TO EQUIP OR USE";
  if (!capacity_text.empty()) title += "  /  " + capacity_text;
  Box content = Card(canvas, body, title);
  const int cols = 4;
  const int rows = 2;
  const int gap = 14;
  const int cell_w = (content.w - (cols - 1) * gap) / cols;
  const int cell_h = (content.h - (rows - 1) * gap) / rows;
  for (int i = 0; i < 8; ++i) {
    const NativeUiRow* source = i < static_cast<int>(gear.quick_slots.size())
        ? &gear.quick_slots[i] : nullptr;
    NativeUiRow empty;
    empty.group = i;
    const NativeUiRow& row = source ? *source : empty;
    const Box cell(content.x + (i % cols) * (cell_w + gap),
                   content.y + (i / cols) * (cell_h + gap), cell_w, cell_h);
    const bool pressed = ui.TargetPressed(UiAction::kGearSlot, i);
    canvas.FillRoundRect(cell, theme::kRadius,
                         pressed ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.16f)
                                 : theme::kSurfaceHigh);
    canvas.StrokeRoundRect(cell, theme::kRadius, row.selected ? 3 : 1,
                           row.selected ? theme::kAccent : theme::kEdgeBright);
    canvas.Text(Box(cell.x + 16, cell.y + 12, cell.w - 32, 28),
                Fmt("SLOT %d", i + 1), theme::kFontLabel, theme::kAccent,
                Align::kLeft);
    const std::string name = !row.text.empty() && !row.text[0].empty()
        ? row.text[0] : "EMPTY";
    canvas.TextWrap(Box(cell.x + 18, cell.y + 64, cell.w - 36, cell.h - 104),
                    name, theme::kFontTitle,
                    row.enabled ? theme::kText : theme::kTextFaint,
                    Align::kCenter);
    canvas.Text(Box(cell.x + 16, cell.bottom() - 38, cell.w - 32, 24),
                row.enabled ? "READY" : "UNASSIGNED", theme::kFontSmall,
                row.enabled ? theme::kGood : theme::kTextFaint, Align::kCenter);
    ui.AddTarget(cell, UiAction::kGearSlot, i, row.enabled);
  }
}

// ---------------------------------------------------------------- MISSIONS
void DrawMissionsPage(Canvas& canvas, const Box& body, Interface& ui) {
  const MissionsSnapshot& missions = TheNativeUi().missions();
  if (!missions.available) {
    EmptyState(canvas, body, "Missions unavailable",
               missions.unavailable_reason.empty()
                   ? "The native quest log has not initialized yet."
                   : missions.unavailable_reason);
    return;
  }

  const int left_w = 442;
  const Box left_card(body.x, body.y, left_w, body.h);
  const Box right_card(left_card.right() + theme::kGap, body.y,
                       body.w - left_w - theme::kGap, body.h);
  Box list = Card(canvas, left_card, "QUEST LOG");
  Box detail = Card(canvas, right_card, "CURRENT OBJECTIVES");

  constexpr int kVisibleRows = 8;
  const int controls_h = 70;
  const int gap = 7;
  const int row_h = std::max(48, (list.h - controls_h -
                                 (kVisibleRows - 1) * gap) / kVisibleRows);
  const int max_offset = std::max(0,
      static_cast<int>(missions.quests.size()) - kVisibleRows);
  ui.ClampScroll(Page::kMissions, max_offset);
  const int offset = ui.ScrollOffset(Page::kMissions);
  for (int visible = 0; visible < kVisibleRows; ++visible) {
    const int index = offset + visible;
    if (index >= static_cast<int>(missions.quests.size())) break;
    const NativeUiRow& row = missions.quests[index];
    const Box bounds(list.x, list.y + visible * (row_h + gap), list.w, row_h);
    NativeRow(canvas, ui, bounds, row, RowPrimary(row, "Quest", index),
              RowSecondary(row), UiAction::kMission, index);
  }
  if (missions.quests.empty()) {
    canvas.TextWrap(Box(list.x + 24, list.y + 80, list.w - 48, 100),
                    "No active quests are exposed by the native quest log.",
                    theme::kFontBody, theme::kTextFaint, Align::kCenter);
  }
  ScrollButtons(canvas, ui,
                Box(list.x, list.bottom() - controls_h + 4, list.w, controls_h - 4),
                offset, max_offset, kVisibleRows);

  int y = detail.y;
  canvas.Text(Box(detail.x, y, detail.w - 180, 42),
              missions.selected_name.empty() ? "Select a quest"
                                              : missions.selected_name,
              theme::kFontTitle, theme::kText, Align::kLeft);
  if (!missions.reward.empty()) {
    canvas.Text(Box(detail.right() - 170, y, 170, 42), missions.reward,
                theme::kFontBody, theme::kAccent, Align::kRight);
  }
  y += 56;
  for (std::size_t i = 0; i < missions.objectives.size() && i < 7; ++i) {
    const NativeUiRow& objective = missions.objectives[i];
    const Box row(detail.x, y, detail.w, 54);
    canvas.FillRoundRect(row, 4, theme::kSurfaceHigh);
    // Native row selection is focus, not objective completion. Keep the marker
    // neutral until the quest model exposes a real completed-state flag.
    canvas.FillRect(Box(row.x + 12, row.y + 19, 14, 14),
                    theme::kEdgeBright);
    canvas.Text(Box(row.x + 40, row.y, row.w - 52, row.h),
                RowPrimary(objective, "Objective", static_cast<int>(i)),
                theme::kFontSmall, theme::kText, Align::kLeft);
    y += 61;
  }
  const int button_h = 66;
  const int action_y = detail.bottom() - button_h;
  if (!missions.description.empty() && y < action_y - 20) {
    canvas.TextWrap(Box(detail.x, y + 8, detail.w, action_y - y - 22),
                    missions.description, theme::kFontSmall, theme::kTextDim);
  }
  ActionButton(canvas, ui, Box(detail.x, action_y, detail.w, button_h),
               "TRACK", UiAction::kMissionTrack, -1,
               missions.track_control_enabled);
}

// ------------------------------------------------------------------- SKILLS
void DrawSkillsPage(Canvas& canvas, const Box& body, Interface& ui,
                    GameState& state) {
  const SkillsSnapshot& skills = TheNativeUi().skills();
  if (!skills.available) {
    const Box header(body.x, body.y, body.w, 56);
    ReferencePanel(canvas, header);
    canvas.Text(Box(header.x + 24, header.y, 420, header.h), "SKILL TREES",
                theme::kFontTitle, theme::kText, Align::kLeft);
    const Value& points = state.vitals().skill_points;
    canvas.Text(Box(header.right() - 420, header.y, 390, header.h),
                points.valid ? Fmt("AVAILABLE POINTS  %d", points.i())
                             : "POINTS UNAVAILABLE",
                theme::kFontTitle, points.valid ? theme::kAccent
                                                : theme::kTextFaint,
                Align::kRight);
    const int y = header.bottom() + 8;
    const int w = (body.w - 2 * theme::kGap) / 3;
    const char* names[] = {"FURY", "COMBAT", "SURVIVAL"};
    for (int tree = 0; tree < 3; ++tree) {
      const Box panel(body.x + tree * (w + theme::kGap), y,
                      tree == 2 ? body.right() - (body.x + tree * (w + theme::kGap))
                                : w,
                      body.bottom() - y);
      ReferencePanel(canvas, panel);
      canvas.FillRect(Box(panel.x, panel.y, panel.w, 50),
                      tree == 0 ? theme::kAccent : theme::kSurfaceHigh);
      canvas.Text(Box(panel.x, panel.y, panel.w, 50), names[tree],
                  theme::kFontTitle, theme::kText, Align::kCenter);
      for (int tier = 0; tier < 7; ++tier) {
        const int cy = panel.y + 84 + tier * 110;
        canvas.StrokeCircle(panel.x + panel.w / 2, cy, 33, 3, theme::kEdge);
        canvas.Line(panel.x + panel.w / 2, cy + 34,
                    panel.x + panel.w / 2, cy + 79, 3, theme::kEdge);
      }
      canvas.TextWrap(Box(panel.x + 24, panel.bottom() - 150, panel.w - 48, 120),
                      tree == 1
                          ? (skills.unavailable_reason.empty()
                                 ? "Waiting for the game's skill controller."
                                 : skills.unavailable_reason)
                          : "",
                      theme::kFontSmall, theme::kTextFaint, Align::kCenter);
    }
    return;
  }

  const int detail_w = 350;
  const Box grid_card(body.x, body.y, body.w - detail_w - theme::kGap, body.h);
  const Box detail_card(grid_card.right() + theme::kGap, body.y, detail_w, body.h);
  ReferencePanel(canvas, grid_card);
  ReferencePanel(canvas, detail_card);
  constexpr int kCompactHeader = 38;
  canvas.Text(Box(grid_card.x + 14, grid_card.y + 5,
                  grid_card.w - 28, 28),
              "SKILL TREES", theme::kFontLabel, theme::kTextDim,
              Align::kLeft);
  canvas.FillRect(Box(grid_card.x + 14, grid_card.y + 33, 42, 2),
                  theme::kAccent);
  canvas.Text(Box(detail_card.x + 14, detail_card.y + 5,
                  detail_card.w - 28, 28),
              "SKILL DETAILS", theme::kFontLabel, theme::kTextDim,
              Align::kLeft);
  canvas.FillRect(Box(detail_card.x + 14, detail_card.y + 33, 42, 2),
                  theme::kAccent);
  Box grid(grid_card.x + 10, grid_card.y + kCompactHeader,
           grid_card.w - 20, grid_card.h - kCompactHeader - 10);
  std::string points = skills.available_points;
  if (points.empty() && state.vitals().skill_points.valid)
    points = Fmt("%d", state.vitals().skill_points.i());
  Box detail(detail_card.x + 10, detail_card.y + kCompactHeader,
             detail_card.w - 20, detail_card.h - kCompactHeader - 10);

  constexpr int kTrees = 3;
  constexpr int kTiers = 7;
  const char* tree_names[kTrees] = {"FURY", "COMBAT", "SURVIVAL"};
  const int tree_gap = 8;
  const int tree_w = (grid.w - (kTrees - 1) * tree_gap) / kTrees;
  const int title_h = 42;
  struct SkillNodeLayout {
    int index = -1;
    int tier = -1;
    Box bounds;
  };
  for (int tree = 0; tree < kTrees; ++tree) {
    const int tree_x = grid.x + tree * (tree_w + tree_gap);
    const int width = tree == kTrees - 1 ? grid.right() - tree_x : tree_w;
    canvas.VGradient(Box(tree_x, grid.y, width, title_h),
                     tree == 0 ? theme::kAccent : theme::kAccentDeep,
                     theme::kAccentMuted);
    canvas.StrokeRect(Box(tree_x, grid.y, width, grid.h), 1, theme::kEdge);
    canvas.Text(Box(tree_x, grid.y, width, title_h), tree_names[tree],
                theme::kFontLabel, theme::kText, Align::kCenter);

    std::vector<std::vector<SkillNodeLayout>> layout(kTiers);
    std::vector<int> tree_indices;
    for (int i = 0; i < static_cast<int>(skills.skills.size()); ++i) {
      if (skills.skills[i].group == tree && skills.skills[i].tier >= 0 &&
          skills.skills[i].tier < kTiers) {
        tree_indices.push_back(i);
      }
    }

    float native_min_x = 0.0f;
    float native_max_x = 0.0f;
    float native_min_y = 0.0f;
    float native_max_y = 0.0f;
    int positioned = 0;
    float native_tier_sum = 0.0f;
    float native_y_sum = 0.0f;
    float native_tier_y_sum = 0.0f;
    for (int index : tree_indices) {
      const NativeUiRow& row = skills.skills[index];
      if (!row.has_native_position || !std::isfinite(row.native_x) ||
          !std::isfinite(row.native_y)) continue;
      if (positioned++ == 0) {
        native_min_x = native_max_x = row.native_x;
        native_min_y = native_max_y = row.native_y;
      } else {
        native_min_x = std::min(native_min_x, row.native_x);
        native_max_x = std::max(native_max_x, row.native_x);
        native_min_y = std::min(native_min_y, row.native_y);
        native_max_y = std::max(native_max_y, row.native_y);
      }
      native_tier_sum += static_cast<float>(row.tier);
      native_y_sum += row.native_y;
      native_tier_y_sum += static_cast<float>(row.tier) * row.native_y;
    }
    const bool use_native_x = positioned > 1 &&
        native_max_x - native_min_x > 0.01f;
    // Runtime controls may report either tree-space coordinates or positions
    // relative to their tier container. Only a substantial range proves the
    // former. When it is proven, preserve its direction as well as its span.
    const bool use_native_y = positioned > 1 &&
        native_max_y - native_min_y > 60.0f;
    const float tier_y_covariance = positioned * native_tier_y_sum -
        native_tier_sum * native_y_sum;
    const bool native_y_ascending = tier_y_covariance >= 0.0f;
    const int node_size = 62;
    const int lane_left = tree_x + 22;
    const int lane_right = tree_x + width - 18;
    const int lane_span = std::max(0, lane_right - lane_left - node_size);
    const int nodes_top = grid.y + title_h + 12;
    const int nodes_bottom = grid.bottom() - 12;
    const int vertical_span =
        std::max(0, nodes_bottom - nodes_top - node_size);

    for (int tier = 0; tier < kTiers; ++tier) {
      std::vector<int> indices;
      for (int index : tree_indices)
        if (skills.skills[index].tier == tier) indices.push_back(index);
      // Runtime skill controls are inserted into the tier group in controller
      // order, which is not guaranteed to be their visual order. The authored
      // X coordinate is the source of truth for left/centre/right branches.
      std::stable_sort(indices.begin(), indices.end(), [&](int a, int b) {
        const NativeUiRow& left = skills.skills[a];
        const NativeUiRow& right = skills.skills[b];
        if (left.has_native_position != right.has_native_position)
          return left.has_native_position;
        if (left.has_native_position && right.has_native_position &&
            std::fabs(left.native_x - right.native_x) > 0.01f) {
          return left.native_x < right.native_x;
        }
        return a < b;
      });
      if (indices.empty()) continue;
      const int count = static_cast<int>(indices.size());
      std::vector<int> centers;
      centers.reserve(indices.size());
      for (int item = 0; item < count; ++item) {
        const int index = indices[item];
        const NativeUiRow& row = skills.skills[index];
        float x_fraction = 0.5f;
        if (use_native_x && row.has_native_position) {
          x_fraction = (row.native_x - native_min_x) /
                       (native_max_x - native_min_x);
        } else if (count > 1) {
          x_fraction = static_cast<float>(item) /
                       static_cast<float>(count - 1);
        }
        x_fraction = std::clamp(x_fraction, 0.0f, 1.0f);
        centers.push_back(lane_left + node_size / 2 +
            static_cast<int>(std::lround(x_fraction * lane_span)));
      }
      // Authored controls occasionally share a nearly identical X while
      // belonging to separate branches. Keep their ordering but fan them out
      // enough for independent square touch targets.
      bool overlaps = false;
      for (int item = 1; item < count; ++item) {
        if (centers[item] - centers[item - 1] < node_size + 4) overlaps = true;
      }
      if (overlaps && count > 1) {
        for (int item = 0; item < count; ++item) {
          centers[item] = lane_left + node_size / 2 +
              item * lane_span / (count - 1);
        }
      }
      for (int item = 0; item < count; ++item) {
        const int index = indices[item];
        const NativeUiRow& row = skills.skills[index];
        float y_fraction = static_cast<float>(tier) /
                           static_cast<float>(kTiers - 1);
        if (use_native_y && row.has_native_position) {
          y_fraction = (row.native_y - native_min_y) /
                       (native_max_y - native_min_y);
          if (!native_y_ascending) y_fraction = 1.0f - y_fraction;
        }
        y_fraction = std::clamp(y_fraction, 0.0f, 1.0f);
        const int node_y = nodes_top +
            static_cast<int>(std::lround(y_fraction * vertical_span));
        layout[tier].push_back({index, tier,
            Box(centers[item] - node_size / 2, node_y,
                node_size, node_size)});
      }
    }

    for (int tier = 0; tier < kTiers; ++tier) {
      if (layout[tier].empty()) continue;
      int average_y = 0;
      for (const SkillNodeLayout& node : layout[tier]) average_y += node.bounds.y;
      average_y /= static_cast<int>(layout[tier].size());
      canvas.Text(Box(tree_x + 4, average_y + 17, 17, 20),
                  Fmt("%d", tier + 1), theme::kFontLabel,
                  theme::kTextFaint, Align::kCenter);
    }

    // Dead Island gates a tier by the previous tier's purchased-point total,
    // rather than by one fabricated parent skill. Draw the same topology: a
    // singleton milestone fans out, parallel choices continue in lanes, and
    // the lanes converge into the next milestone.
    int previous_tier = -1;
    for (int tier = 0; tier < kTiers; ++tier) {
      if (layout[tier].empty()) continue;
      if (previous_tier >= 0) {
        const auto& parents = layout[previous_tier];
        const auto& children = layout[tier];
        auto connect = [&](const SkillNodeLayout& parent,
                           const SkillNodeLayout& child) {
          const NativeUiRow& child_row = skills.skills[child.index];
          int child_rank = 0;
          int child_maximum = 0;
          const bool child_owned =
              SkillRank(child_row, &child_rank, &child_maximum) &&
              child_rank > 0;
          const Color line = child_row.purchasable || child_owned
              ? theme::kEdgeBright : theme::kEdge;
          const int x1 = parent.bounds.x + parent.bounds.w / 2;
          const int y1 = parent.bounds.bottom();
          const int x2 = child.bounds.x + child.bounds.w / 2;
          const int y2 = child.bounds.y;
          const int mid_y = y1 + std::max(4, (y2 - y1) / 2);
          canvas.Line(x1, y1, x1, mid_y, 2, line);
          canvas.Line(x1, mid_y, x2, mid_y, 2, line);
          canvas.Line(x2, mid_y, x2, y2, 2, line);
        };
        if (parents.size() == 1 || children.size() == 1) {
          for (const SkillNodeLayout& parent : parents)
            for (const SkillNodeLayout& child : children)
              connect(parent, child);
        } else if (parents.size() == children.size()) {
          for (std::size_t i = 0; i < parents.size(); ++i)
            connect(parents[i], children[i]);
        } else {
          for (const SkillNodeLayout& child : children) {
            const SkillNodeLayout* nearest = &parents.front();
            int distance = std::abs(
                (child.bounds.x + child.bounds.w / 2) -
                (nearest->bounds.x + nearest->bounds.w / 2));
            for (const SkillNodeLayout& parent : parents) {
              const int candidate = std::abs(
                  (child.bounds.x + child.bounds.w / 2) -
                  (parent.bounds.x + parent.bounds.w / 2));
              if (candidate < distance) {
                nearest = &parent;
                distance = candidate;
              }
            }
            connect(*nearest, child);
          }
        }
      }
      previous_tier = tier;
    }

    for (int tier = 0; tier < kTiers; ++tier) {
      for (const SkillNodeLayout& placed : layout[tier]) {
        const int index = placed.index;
        const NativeUiRow& row = skills.skills[index];
        const Box& node = placed.bounds;
        const bool pressed = ui.TargetPressed(UiAction::kSkill, index);
        int learned_rank = 0;
        int maximum_rank = 0;
        const bool has_rank = SkillRank(row, &learned_rank, &maximum_rank);
        const bool learned = has_rank && learned_rank > 0;
        const bool complete = has_rank && learned_rank == maximum_rank;
        const Color fill = row.selected || pressed
            ? theme::kSurfaceHigh.Mix(theme::kAccent, 0.34f)
            : (learned ? theme::kSurfaceHigh.Mix(theme::kGood, 0.10f)
                       : theme::kSurfaceHigh);
        canvas.FillRect(node, fill);
        canvas.StrokeRect(node, row.selected ? 3 : 2,
                          row.selected ? theme::kAccent
                                       : (complete ? theme::kGood
                                                   : (row.purchasable || learned
                                                          ? theme::kEdgeBright
                                                          : theme::kEdge)));
        DrawSkillGlyph(canvas, node.Inset(8), row,
                       row.purchasable || learned ? theme::kText
                                                 : theme::kTextFaint);
        const std::string rank = RowSecondary(row);
        if (!rank.empty()) {
          canvas.FillRect(Box(node.right() - 37, node.bottom() - 20, 35, 18),
                          theme::kSurface);
          canvas.Text(Box(node.right() - 37, node.bottom() - 20, 35, 18),
                      rank, theme::kFontLabel,
                      complete ? theme::kGood
                               : (row.purchasable || learned
                                      ? theme::kAccent
                                      : theme::kTextFaint),
                      Align::kCenter);
        }
        ui.AddTarget(node, UiAction::kSkill, index, row.enabled);
      }
    }
  }

  int y = detail.y;
  const Box point_chip(detail.x, y, detail.w, 58);
  canvas.VGradient(point_chip, Color(26, 18, 18), theme::kSurfaceHigh);
  canvas.StrokeRect(point_chip, 1, theme::kEdgeBright);
  DrawSkillPointEmblem(canvas,
                       Box(point_chip.x + 8, point_chip.y + 5, 48, 48),
                       true);
  canvas.Text(Box(point_chip.x + 67, point_chip.y + 5,
                  point_chip.w - 145, 22), "AVAILABLE POINTS",
              theme::kFontLabel, theme::kTextDim, Align::kLeft);
  canvas.Text(Box(point_chip.right() - 70, point_chip.y,
                  54, point_chip.h), points.empty() ? "--" : points,
              theme::kFontBig,
              !points.empty() && points != "0" ? theme::kAccent
                                                : theme::kTextFaint,
              Align::kRight);
  canvas.Text(Box(point_chip.x + 67, point_chip.y + 27,
                  point_chip.w - 145, 22),
              !points.empty() && points != "0" ? "READY TO SPEND"
                                                : "LEVEL UP TO EARN MORE",
              theme::kFontLabel,
              !points.empty() && points != "0" ? theme::kText
                                                : theme::kTextFaint,
              Align::kLeft);
  y += 72;
  const NativeUiRow* selected_row = nullptr;
  for (const NativeUiRow& row : skills.skills)
    if (row.selected) { selected_row = &row; break; }
  if (selected_row) {
    DrawSkillGlyph(canvas, Box(detail.x + detail.w / 2 - 38, y, 76, 76),
                   *selected_row, theme::kText);
    y += 84;
  }
  canvas.TextWrap(Box(detail.x, y, detail.w, 66),
                  skills.selected_name.empty() ? "Select a skill"
                                               : skills.selected_name,
                  theme::kFontTitle, theme::kText, Align::kCenter);
  y += 76;
  if (!skills.selected_level.empty()) {
    canvas.Text(Box(detail.x, y, detail.w, 30), skills.selected_level,
                theme::kFontBody, theme::kAccent, Align::kLeft);
    y += 42;
  }
  if (selected_row) {
    int learned_rank = 0;
    int maximum_rank = 0;
    if (SkillRank(*selected_row, &learned_rank, &maximum_rank)) {
      const std::string state_text = learned_rank >= maximum_rank
          ? "MAXIMUM RANK"
          : (learned_rank > 0 ? "UPGRADE OWNED" : "NOT YET LEARNED");
      const Color state_color = learned_rank >= maximum_rank
          ? theme::kGood : (learned_rank > 0 ? theme::kText
                                             : theme::kTextFaint);
      canvas.Text(Box(detail.x, y, detail.w, 25), state_text,
                  theme::kFontLabel, state_color, Align::kLeft);
      y += 33;
    }
  }
  const int button_h = 66;
  const int action_top = detail.bottom() -
      (skills.confirmation_visible ? (2 * button_h + 10) : button_h);
  canvas.TextWrap(Box(detail.x, y, detail.w, std::max(40, action_top - y - 14)),
                  skills.description.empty()
                      ? "Purchases use the native skill controller, including point costs and prerequisites."
                      : skills.description,
                  theme::kFontSmall, theme::kTextDim);

  if (skills.confirmation_visible) {
    ActionButton(canvas, ui, Box(detail.x, action_top, detail.w, button_h),
                 "CONFIRM PURCHASE", UiAction::kSkillConfirmYes);
    ActionButton(canvas, ui,
                 Box(detail.x, action_top + button_h + 10, detail.w, button_h),
                 "CANCEL", UiAction::kSkillConfirmNo);
  } else {
    ActionButton(canvas, ui, Box(detail.x, action_top, detail.w, button_h),
                 "LEARN / UPGRADE", UiAction::kSkillLearn, -1,
                 skills.learn_control_enabled);
  }
}

}  // namespace

void Interface::DrawPage(Canvas& canvas, const Box& body, Game& game,
                         GameState& state) {
  switch (page_) {
    case Page::kStatus:
      DrawStatusPage(canvas, body, *this, game, state);
      break;
    case Page::kData:
      DrawDataPage(canvas, body, game, state);
      break;
    case Page::kInventory:
      DrawInventoryPage(canvas, body, *this, state);
      break;
    case Page::kGear:
      DrawGearPage(canvas, body, *this, state);
      break;
    case Page::kMap:
      DrawMapPage(canvas, body, *this);
      break;
    case Page::kMissions:
      DrawMissionsPage(canvas, body, *this);
      break;
    case Page::kSkills:
      DrawSkillsPage(canvas, body, *this, state);
      break;
    default:
      break;
  }
}

}  // namespace dis
