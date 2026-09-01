#pragma once

#include <cstdint>

#include "Snapshot.h"

namespace gdds {

inline void MixPanelKey(uint64_t* key, uint64_t value) {
  *key ^= value;
  *key *= 1099511628211ull;
}

inline uint64_t PanelContentKey(const Snapshot* state, bool available,
                                int page) {
  uint64_t key = 1469598103934665603ull;
  MixPanelKey(&key, available ? 1u : 0u);
  MixPanelKey(&key, static_cast<uint32_t>(page));
  if (!available || !state) return key;

  MixPanelKey(&key, static_cast<uint32_t>(state->lifecycle));
  MixPanelKey(&key, state->world_epoch);
  MixPanelKey(&key, state->capabilities);
  if (state->lifecycle != Lifecycle::kLive) return key;

  switch (page) {
    case 0:
      MixPanelKey(&key, state->stats_revision);
      MixPanelKey(&key, state->inventory_revision);
      MixPanelKey(&key, state->equipment_revision);
      MixPanelKey(&key, state->skills_revision);
      break;
    case 1:
      MixPanelKey(&key, state->inventory_revision);
      MixPanelKey(&key, state->equipment_revision);
      break;
    case 2:
    case 3:
      MixPanelKey(&key, state->skills_revision);
      break;
    case 4:
      MixPanelKey(&key, state->map_revision);
      break;
    case 5:
      MixPanelKey(&key, state->quest_revision);
      break;
    default:
      break;
  }
  return key;
}

}  // namespace gdds
