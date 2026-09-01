#include "UiAdapter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

namespace fo4ds {
namespace {

template <size_t Size>
void CopyText(char (&destination)[Size], const std::string& source) {
  static_assert(Size > 0);
  const size_t length = std::min(source.size(), Size - 1);
  if (length) std::memcpy(destination, source.data(), length);
  destination[length] = '\0';
}

std::string Normalize(std::string value) {
  value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) {
                return !std::isalnum(ch);
              }),
              value.end());
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

ItemCategory CategoryFor(uint32_t flags) {
  if (flags & 2u) return ItemCategory::kWeapon;
  if (flags & 4u) return ItemCategory::kApparel;
  if (flags & 8u) return ItemCategory::kAid;
  // Notes and holotapes are presented through DATA/NOTES as well as MISC.
  if (flags & (128u | 8192u)) return ItemCategory::kMisc;
  if (flags & 1024u) return ItemCategory::kJunk;
  if (flags & 2048u) return ItemCategory::kMod;
  if (flags & 4096u) return ItemCategory::kAmmo;
  if (flags & 512u) return ItemCategory::kMisc;
  return ItemCategory::kUnknown;
}

uint32_t UiId(uint32_t node_id, uint32_t form_id, uint32_t fallback) {
  if (node_id) return node_id;
  if (form_id) return form_id;
  return fallback ? fallback : 1u;
}

void AppendDescription(std::string* description, const ItemCardField& field) {
  if (!description || field.text_value.empty()) return;
  if (!description->empty()) description->append("\n");
  if (!field.label.empty()) {
    description->append(field.label);
    description->append(": ");
  }
  description->append(field.text_value);
}

void AppendDescription(std::string* description, const char* label,
                       const std::string& value) {
  if (!description || value.empty()) return;
  if (!description->empty()) description->append("\n");
  if (label && *label) {
    description->append(label);
    description->append(": ");
  }
  description->append(value);
}

std::string CardNumber(float value) {
  char buffer[40] = {};
  if (std::fabs(value - std::round(value)) < 0.005f) {
    std::snprintf(buffer, sizeof(buffer), "%.0f", value);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%.1f", value);
  }
  return buffer;
}

const char* DamageTypeName(int type) {
  switch (type) {
    case 1:
      return "PHYSICAL";
    case 2:
      return "POISON";
    case 3:
      return "TYPE 3";
    case 4:
      return "ENERGY";
    case 5:
      return "TYPE 5";
    case 6:
      return "RADIATION";
    default:
      return "OTHER";
  }
}

std::string CardValue(const ItemCardField& field) {
  std::string value = CardNumber(field.number_value);
  if (field.show_as_percent) value.append("%");
  if (field.duration > 0.0f) {
    value.append(" (");
    value.append(CardNumber(field.duration));
    value.append(field.scale_with_duration ? "s, over time)" : "s)");
  }
  return value;
}

std::string DisplayCardLabel(const ItemCardField& field) {
  std::string label = field.label;
  while (!label.empty() && label.front() == '$') label.erase(label.begin());
  return label;
}

void ApplyCard(const InventoryItemState& source, ItemState* destination) {
  if (!destination) return;
  std::string description;
  for (const ItemCardField& field : source.card) {
    const std::string label = Normalize(field.label);
    if (!field.text_value.empty()) {
      AppendDescription(&description, field);
      continue;
    }
    // Fallout 4 encodes the weapon's ammo name in item-card `text`, its live
    // reserve count in `Value`, and identifies that row with damageType 10.
    // It is not labelled "Ammo", and must be handled before localized labels.
    if (field.damage_type == 10) {
      destination->ammo_count =
          static_cast<int32_t>(std::lround(field.number_value));
      AppendDescription(&description, "AMMO TYPE", field.label);
    } else if (label == "damage" || label == "dmg" ||
               label == "physicaldamage") {
      // `$dmg` is repeated for physical, poison, energy, radiation, etc. The
      // copied UI has one DAMAGE cell, so use Fallout's physical type (1)
      // rather than allowing the final resistance row to overwrite it.
      if (field.damage_type == 0 || field.damage_type == 1)
        destination->damage = field.number_value;
      if (field.damage_type > 1 && std::fabs(field.number_value) > 0.0005f) {
        const std::string detail =
            std::string("DAMAGE ") + DamageTypeName(field.damage_type);
        AppendDescription(&description, detail.c_str(), CardValue(field));
      }
    } else if (label == "damageresist" || label == "damageresistance" ||
               label == "dr" || label == "armor") {
      if (field.damage_type == 0 || field.damage_type == 1)
        destination->armor = field.number_value;
      if (field.damage_type > 1 && std::fabs(field.number_value) > 0.0005f) {
        const std::string detail =
            std::string("RESIST ") + DamageTypeName(field.damage_type);
        AppendDescription(&description, detail.c_str(), CardValue(field));
      }
    } else if (label == "weight" || label == "wg" || label == "wt") {
      destination->weight = field.number_value;
    } else if (label == "value" || label == "val") {
      destination->value = static_cast<int32_t>(std::lround(field.number_value));
    } else if (label == "ammo" && field.text_value.empty()) {
      destination->ammo_count =
          static_cast<int32_t>(std::lround(field.number_value));
    } else if (label == "rof" || label == "rateoffire") {
      AppendDescription(&description, "FIRE RATE",
                        CardNumber(field.number_value));
    } else if (label == "range" || label == "rng") {
      AppendDescription(&description, "RANGE", CardNumber(field.number_value));
    } else if (label == "accuracy" || label == "acc") {
      AppendDescription(&description, "ACCURACY",
                        CardNumber(field.number_value));
    } else {
      // Aid effects and many DLC/mod-added item-card values use localized
      // labels rather than one of the vanilla column keys. Keep those real
      // values visible instead of silently reducing the item to its name.
      const std::string display_label = DisplayCardLabel(field);
      if (!display_label.empty())
        AppendDescription(&description, display_label.c_str(),
                          CardValue(field));
    }
  }
  if (!source.components.empty()) {
    std::string value;
    for (const InventoryComponentState& component : source.components) {
      if (!value.empty()) value.append(", ");
      if (component.count > 1) {
        value.append(std::to_string(component.count));
        value.push_back(' ');
      }
      value.append(component.name);
      if (component.tagged_for_search) value.append(" [TAGGED]");
    }
    AppendDescription(&description, "COMPONENTS", value);
  }
  if (source.legendary) {
    if (!description.empty()) description.append("\n");
    description.append("Legendary");
  }
  if (source.power_armor) {
    if (!description.empty()) description.append("\n");
    description.append("Power Armor");
  }
  CopyText(destination->description, description);
}

const InventoryItemState* FindItem(const PipboySnapshot& source,
                                   uint32_t ui_id) {
  auto found = std::find_if(source.items.begin(), source.items.end(),
                            [&](const InventoryItemState& item) {
                              return UiId(item.node_id, item.form_id, 0) == ui_id;
                            });
  return found == source.items.end() ? nullptr : &*found;
}

}  // namespace

void AdaptPipboySnapshot(const PipboySnapshot& source, bool actions_enabled,
                         Snapshot* destination) {
  if (!destination) return;
  std::fill_n(reinterpret_cast<unsigned char*>(destination),
              sizeof(*destination), 0);
  destination->version = kSnapshotVersion;
  destination->size = sizeof(*destination);
  destination->sequence = source.revision ? source.revision : source.generation;
  destination->world_epoch = source.generation;
  destination->vitals_revision = source.domain_revisions[0];
  destination->inventory_revision = source.domain_revisions[1];
  destination->data_revision = source.domain_revisions[2];
  destination->map_revision = source.domain_revisions[3];
  destination->radio_revision = source.domain_revisions[4];
  destination->supported_build = 1;
  // Connection readiness is a data-capability state, not a panel lifecycle.
  // The original diagnostic panel stayed navigable before a live producer was
  // ready; treating a disconnected producer as kBoot made the copied Fallout 3
  // chrome cover the entire UI with an INITIALIZING screen forever.  Keep the
  // panel available and represent unavailable domains through capability bits
  // and sentinel values instead.
  destination->lifecycle =
      !(source.connected && source.root_ready)
          ? (source.connected ? Lifecycle::kLoading : Lifecycle::kMainMenu)
          : (source.is_player_dead
                 ? Lifecycle::kDead
                 : (source.is_loading ? Lifecycle::kLoading
                                      : Lifecycle::kLive));
  const bool producer_ready = source.connected && source.root_ready;
  const bool stats_available = producer_ready && source.stats_ready;
  if (producer_ready) {
    if (source.stats_ready) destination->capabilities |= kCapStats;
    if (source.inventory_ready) destination->capabilities |= kCapInventory;
    if (source.data_ready) destination->capabilities |= kCapQuests;
    if (source.map_ready) destination->capabilities |= kCapMap;
    if (source.radio_ready) destination->capabilities |= kCapRadio;
    if (source.effects_ready) destination->capabilities |= kCapEffects;
    if (source.perks_ready) destination->capabilities |= kCapPerks;
    if (source.general_stats_ready)
      destination->capabilities |= kCapGeneralStats;
  }
  if (actions_enabled && source.ActionsSafe())
    destination->capabilities |= kCapActions;

  destination->level = stats_available ? static_cast<int32_t>(source.level) : -1;
  destination->caps = stats_available ? static_cast<int32_t>(source.caps) : -1;
  destination->perk_points =
      stats_available ? static_cast<int32_t>(source.perk_points) : -1;
  destination->stimpaks =
      stats_available ? static_cast<int32_t>(source.stimpaks) : -1;
  destination->radaway =
      stats_available ? static_cast<int32_t>(source.radaway) : -1;
  destination->xp = stats_available
                        ? static_cast<int32_t>(std::lround(
                              std::clamp(source.xp_progress, 0.0f, 1.0f) *
                              100.0f))
                        : -1;
  destination->xp_next = stats_available ? 100 : -1;
  destination->health = stats_available ? source.hp : -1.0f;
  destination->max_health = stats_available ? source.max_hp : -1.0f;
  destination->action_points = stats_available ? source.ap : -1.0f;
  destination->max_action_points =
      stats_available ? source.max_ap : -1.0f;
  destination->damage_resistance =
      stats_available ? source.total_resistance[0] : -1.0f;
  destination->radiation = -1.0f;
  destination->carry_weight = stats_available ? source.carry_weight : -1.0f;
  destination->max_carry_weight =
      stats_available ? source.max_weight : -1.0f;
  destination->karma = std::numeric_limits<float>::quiet_NaN();
  destination->player_x = source.player_x;
  destination->player_y = source.player_y;
  destination->player_heading = source.player_rotation;
  for (size_t i = 0; i < kSpecialCount; ++i) {
    destination->special[i] =
        stats_available ? static_cast<float>(source.special[i]) : -1.0f;
    destination->special_modifiers[i] =
        stats_available ? source.special_modifiers[i] : 0;
    CopyText(destination->special_names[i], source.special_names[i]);
    CopyText(destination->special_descriptions[i],
             source.special_descriptions[i]);
  }
  for (size_t i = 0; i < kDamageTypeCount; ++i) {
    destination->total_damage[i] =
        stats_available ? source.total_damage[i] : -1.0f;
    destination->total_resistance[i] =
        stats_available ? source.total_resistance[i] : -1.0f;
  }
  for (size_t i = 0; i < kLimbCount; ++i) {
    const float value = source.limb_condition[i];
    destination->limbs[i] =
        !stats_available
            ? -1.0f
            : (value >= 0.0f && value <= 1.0f ? value * 100.0f : value);
  }
  destination->body_flags = source.body_flags;
  destination->head_flags = source.head_flags;
  CopyText(destination->player_name, source.player_name);
  CopyText(destination->location,
           source.current_cell.empty() ? source.current_worldspace
                                       : source.current_cell);
  CopyText(destination->build_label,
           source.status.empty() ? "Waiting for Fallout 4 live state"
                                 : source.status);

  for (const InventoryItemState& item : source.items) {
    if (destination->item_count >= kMaxItems) break;
    if (item.count <= 0) continue;
    ItemState& value = destination->items[destination->item_count++];
    value.form_id = UiId(item.node_id, item.form_id,
                         destination->item_count);
    value.count = item.count;
    value.value = -1;
    value.weight = -1.0f;
    value.condition = -1.0f;
    value.damage = -1.0f;
    value.armor = -1.0f;
    value.ammo_count = -1;
    value.category = CategoryFor(item.filter_flag);
    if (item.equip_state) value.flags |= kItemEquipped;
    if (item.favorite >= 0 || (item.filter_flag & 1u))
      value.flags |= kItemFavorite;
    value.flags |= kItemPlayable;
    CopyText(value.name, item.name);
    ApplyCard(item, &value);

    if ((item.filter_flag & (128u | 8192u)) &&
        destination->note_count < kMaxNotes) {
      NoteState& note = destination->notes[destination->note_count++];
      note.form_id = value.form_id;
      CopyText(note.title, item.name);
      CopyText(note.text, value.description);
    }
  }

  for (const EffectState& effect : source.effects) {
    if (destination->effect_count >= kMaxEffects) break;
    auto& value = destination->effects[destination->effect_count++];
    value.form_id = effect.node_id;
    std::string label = effect.name;
    if (!effect.source.empty()) {
      label = effect.source;
      if (!effect.name.empty() && effect.name != effect.source) {
        label.append(": ");
        label.append(effect.name);
      }
    }
    CopyText(value.name, label);
    value.seconds_remaining = effect.duration;
    value.magnitude = effect.custom_description ? 0.0f : effect.magnitude;
    value.show_as_percent = effect.show_as_percent ? 1u : 0u;
    value.detrimental = effect.detrimental ? 1u : 0u;
  }

  for (const PipPerkState& perk : source.perks) {
    if (destination->perk_count >= kMaxPerks) break;
    auto& value = destination->perks[destination->perk_count++];
    value.form_id = perk.form_id ? perk.form_id
                                 : UiId(perk.node_id, perk.clip_name,
                                        destination->perk_count);
    value.clip_name = perk.clip_name;
    value.rank = perk.rank;
    value.max_rank = perk.max_rank;
    CopyText(value.name, perk.name);
    CopyText(value.description, perk.description);
    CopyText(value.swf_file, perk.swf_file);
  }

  for (const PipGeneralStatState& stat : source.general_stats) {
    if (destination->general_stat_count >= kMaxGeneralStats) break;
    if (!stat.show_if_zero && std::fabs(stat.value) < 0.0005f) continue;
    auto& value =
        destination->general_stats[destination->general_stat_count++];
    value.form_id = UiId(stat.node_id, 0,
                         destination->general_stat_count);
    value.value = stat.value;
    value.show_if_zero = stat.show_if_zero ? 1u : 0u;
    const std::string category =
        !stat.category.empty() && stat.category.front() == '$'
            ? stat.category.substr(1)
            : stat.category;
    CopyText(value.category, category);
    CopyText(value.name, stat.name);
  }

  for (const QuestState& quest : source.quests) {
    if (destination->quest_count >= kMaxQuests) break;
    auto& value = destination->quests[destination->quest_count++];
    value.form_id = quest.form_id ? quest.form_id : quest.node_id;
    // `enabled` is the companion tree's display gate, not an authoritative
    // completion flag. Disabled quests include hidden/not-yet-started records,
    // so do not mislabel all of them completed.
    value.flags = quest.enabled ? static_cast<uint32_t>(kQuestActive) : 0u;
    if (quest.active) value.flags |= kQuestTracked;
    value.objective_begin = destination->objective_count;
    CopyText(value.title, quest.name);
    CopyText(value.description, quest.description);
    for (const QuestObjectiveState& objective : quest.objectives) {
      if (destination->objective_count >= kMaxObjectives) break;
      auto& out = destination->objectives[destination->objective_count++];
      out.quest_form_id = value.form_id;
      out.objective_id = objective.node_id;
      if (objective.enabled) out.flags |= kObjectiveDisplayed;
      if (objective.completed) out.flags |= kObjectiveCompleted;
      if (objective.failed) out.flags |= kObjectiveFailed;
      if (objective.active) out.flags |= kObjectiveTracked;
      CopyText(out.text, objective.text);
      ++value.objective_count;
    }
  }

  for (const MapMarkerState& marker : source.markers) {
    if (destination->marker_count >= kMaxMarkers) break;
    auto& value = destination->markers[destination->marker_count++];
    value.form_id = UiId(marker.node_id, marker.form_id,
                         destination->marker_count);
    if (marker.visible) value.flags |= kMarkerVisible;
    if (marker.discovered) value.flags |= kMarkerDiscovered;
    if (marker.cleared) value.flags |= kMarkerCleared;
    if (marker.local) value.flags |= kMarkerLocal;
    if (marker.on_door || marker.kind == MapMarkerState::Kind::kDoor)
      value.flags |= kMarkerDoor;
    if (marker.kind == MapMarkerState::Kind::kQuest)
      value.flags |= kMarkerQuest;
    if (marker.kind == MapMarkerState::Kind::kCustom)
      value.flags |= kMarkerCustom;
    if (marker.kind == MapMarkerState::Kind::kPowerArmor)
      value.flags |= kMarkerPowerArmor;
    value.type = marker.type;
    value.x = marker.x;
    value.y = marker.y;
    value.z = marker.height;
    CopyText(value.name, marker.name);
  }

  for (const RadioState& station : source.radio) {
    if (destination->station_count >= kMaxStations) break;
    auto& value = destination->stations[destination->station_count++];
    value.form_id = UiId(station.node_id, 0, destination->station_count);
    if (station.in_range) value.flags |= kStationAvailable;
    if (station.active) value.flags |= kStationTuned;
    value.frequency = station.frequency;
    value.signal = -1.0f;
    CopyText(value.name, station.name);
  }
}

bool BuildNativeActionRequest(const Command& command,
                              const PipboySnapshot& source, uint64_t token,
                              NativeActionRequest* request) {
  if (!request || !token) return false;
  NativeActionRequest next = {};
  next.token = token;
  switch (command.type) {
    case CommandType::kEquipItem:
    case CommandType::kUnequipItem:
    case CommandType::kUseItem: {
      const InventoryItemState* item = FindItem(source, command.form_id);
      if (!item) return false;
      next.kind = NativeActionKind::kUseInventoryItem;
      next.item.handle_id = item->handle_id;
      next.item.stack_id = item->stack_id;
      next.item.inventory_version = source.inventory_version;
      *request = next;
      return true;
    }
    case CommandType::kTrackQuest: {
      auto found = std::find_if(source.quests.begin(), source.quests.end(),
                                [&](const QuestState& quest) {
                                  return (quest.form_id ? quest.form_id
                                                        : quest.node_id) ==
                                         command.form_id;
                                });
      if (found == source.quests.end()) return false;
      next.kind = NativeActionKind::kToggleQuestActive;
      next.quest.form_id = found->form_id;
      next.quest.instance = found->instance;
      next.quest.type = found->type;
      *request = next;
      return true;
    }
    case CommandType::kTuneRadio: {
      auto found = std::find_if(source.radio.begin(), source.radio.end(),
                                [&](const RadioState& station) {
                                  return station.node_id == command.form_id;
                                });
      if (found == source.radio.end()) return false;
      next.kind = NativeActionKind::kToggleRadioStation;
      next.radio.node_id = found->node_id;
      *request = next;
      return true;
    }
    case CommandType::kRadioOff: {
      next.kind = NativeActionKind::kRadioOff;
      *request = next;
      return true;
    }
    default:
      return false;
  }
}

}  // namespace fo4ds
