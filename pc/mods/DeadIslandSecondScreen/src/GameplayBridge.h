// Version-gated access to Dead Island's live gameplay-owned inventory and
// skill models.
//
// The shipped Inventory and Upgrade XUI controllers do not own these models:
// they build transient rows only after the vanilla menu opens. Reading those
// hidden rows therefore cannot populate a second-screen UI during gameplay.
// This bridge follows the exact PlayerDI/LogicalPlayer access paths used by
// the verified Definitive Edition executable and delegates mutations to the
// game's own equip, quick-slot and skill-purchase functions.

#pragma once

#include <cstddef>

#include "GameState.h"
#include "NativeUi.h"

namespace dis {
namespace gameplay {

// True only for the exact gamedll build whose PE identity and instruction
// signatures match every callable entry point used below.
bool Supported();

void Reset();

// Samples owned values into the existing UI snapshot types. The hud pointer is
// optional; when supplied, the live held item supplements the native HUD text
// feed. These functions return true when this bridge owns the result,
// including the valid "player not ready yet" unavailable state.
bool SampleInventory(InventorySnapshot* inventory, GearSnapshot* gear,
                     HudSnapshot* hud = nullptr);
bool SampleSkills(const Vitals& vitals, SkillsSnapshot* skills);

bool SelectInventoryItem(std::size_t index);
// Assigns the exact owned inventory row to one of the game's live quick
// slots through PlayerDI's vanilla assignment primitive. The function
// performs the same container moves, displaced-item handling and eligibility
// checks as MenuInventory; it never writes the quick array directly.
bool AssignInventoryItemToQuickSlot(std::size_t inventory_index,
                                    std::size_t quick_slot);
bool ActivateQuickSlot(std::size_t index);
bool SelectGearSlot(std::size_t index);
bool InventoryAction(InventoryNativeAction action);

bool SelectSkill(std::size_t index);
bool SkillAction(SkillNativeAction action);

}  // namespace gameplay
}  // namespace dis
