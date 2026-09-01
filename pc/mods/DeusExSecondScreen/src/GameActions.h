// Direct, game-native actions for the custom lower-screen interface.
//
// The physical Thor touch sheet lives in Android, so its UDP listener runs on
// a worker thread.  DXHR's inventory and UI objects are not thread safe.  All
// input is therefore queued here and drained by the game's Present callback.
// No function in this file synthesizes a Windows mouse, keyboard, or controller
// event; a lower-screen action can never become a shot or melee attack.

#pragma once

#include <cstdint>

namespace dxs {

class HookSet;

// Retail active augmentations exposed by the lower-screen status panel:
// Typhoon, Cloak, Smart Vision, and Run Silently.
constexpr int kCoreAugmentationCount = 4;

enum class GuestGesture : uint8_t {
  kTap,
  kDown,
  kMove,
  kUp,
  // Ends a gesture without activating or dropping its pressed target.
  kCancel,
  kHold,
  kPinch,
};

struct GuestTouch {
  GuestGesture gesture = GuestGesture::kTap;
  float x = 0.0f;
  float y = 0.0f;
  float value = 0.0f;
};

enum class GameActionKind : uint8_t {
  kTouch,
  kSelectPage,
  kInventoryCanMove,
  kInventoryMove,
  kInventoryCombine,
  kSetQuickSlot,
  kClearQuickSlot,
  kActivateQuickSlot,
  kDiscardInventoryItem,
  kUseInventoryItem,
};

// The fields exposed by DXHR's dtp::Pickup record.  Texture IDs are retained
// rather than copied pixels: the lower-screen D3D renderer can resolve the
// already-loaded game resource and therefore displays the exact vanilla icon.
struct PickupMetadata {
  uint32_t pickup_id = 0xFFFFFFFFu;
  uint32_t texture_id = 0;
  uint32_t rotated_texture_id = 0;
  uint32_t max_stack = 0;
  // Base number of rounds in a fresh magazine. A negative value means this
  // pickup is not a firearm or the build-specific DTP field was unavailable.
  int magazine_capacity = -1;
  int category = -1;
  uint8_t width = 1;
  uint8_t height = 1;
  char name[96] = {};
  char description[256] = {};
};

struct InventoryItemSnapshot {
  // DXHR actions address an item by InventorySlot::slotIndex, not by its
  // position in the UI grid.  storage_index is included only for diagnostics.
  int item_index = -1;
  int storage_index = -1;
  int x = 0;
  int y = 0;
  uint16_t count = 0;
  bool rotated = false;
  // Live firearm state copied from the native 0x28-byte InventorySlot. This
  // is intentionally distinct from `count`, which is the inventory stack
  // count and was never a valid loaded-ammo readout.
  int magazine_ammo = -1;
  int magazine_capacity = -1;
  uint8_t ammo_upgrade_level = 0;
  bool magazine_state_valid = false;
  // True only when this exact native inventory-slot record is the weapon held
  // by the player.  The custom UI uses this identity to dispatch an explicit
  // holster instead of relying on a second equip toggle being interpreted the
  // same way by every controller preset.
  bool equipped = false;
  PickupMetadata pickup;
};

// Immutable identity carried from a rendered inventory snapshot to a native
// action. DXHR reuses item_index values, so the index alone is never sufficient
// for a destructive or consumptive operation.
struct InventoryActionIdentity {
  uintptr_t inventory = 0;
  uint32_t revision = 0;
  int item_index = -1;
  uint32_t pickup_id = 0xFFFFFFFFu;
};

struct QuickSlotSnapshot {
  int slot = -1;
  uint32_t pickup_id = 0xFFFFFFFFu;
  // First matching live inventory slot, or -1 for an empty/stale assignment.
  int item_index = -1;
};

constexpr int kMaximumAugmentationTrees = 32;
constexpr int kMaximumAugmentationNodesPerTree = 16;
constexpr int kMaximumAugmentationPrerequisites = 8;

// These states deliberately separate ownership from eligibility.  The game
// uses the family/tree ID for the base augmentation and a second installed-ID
// list for its upgrade nodes; treating zero installed nodes as a locked family
// is what made the old page report misleading "0 / 0" and unlock states.
enum class AugmentationTreeState : uint8_t {
  kUnavailable,
  kLocked,
  kAvailable,
  kPurchased,
  kComplete,
};

enum class AugmentationNodeState : uint8_t {
  kUnavailable,
  // The family itself has not been purchased yet.
  kLocked,
  // The family is owned, but one or more DTP prerequisite links point to an
  // upgrade which has not been purchased.
  kPrerequisiteBlocked,
  kAvailable,
  kPurchased,
};

struct AugmentationNodeSnapshot {
  int node_index = -1;
  uint32_t texture_id = 0;
  int praxis_cost = -1;
  // Tier and prerequisite links come directly from the DTP tree edge table.
  // They are presentation metadata, never an inferred substitute for the
  // game's own prerequisite validation.
  int tier = 0;
  int order_in_tier = 0;
  int prerequisite_count = 0;
  int prerequisite_indices[kMaximumAugmentationPrerequisites] = {};
  bool prerequisites_met = false;
  bool graph_available = false;
  bool affordable = false;
  AugmentationNodeState state = AugmentationNodeState::kUnavailable;
  // Immutable strings live in the process-lifetime native catalog. Keeping
  // pointers here avoids copying roughly half a megabyte through the live
  // inventory snapshot four times per second.
  const char* name = nullptr;
  const char* description = nullptr;
  const char* help = nullptr;
};

struct AugmentationTreeSnapshot {
  int tree_id = -1;
  // Stable GlobalPlayerInfo enum used by the native status/input paths. The
  // DTP tree_id itself may differ between data builds.
  int augmentation_type = -1;
  uint32_t texture_id = 0;
  int base_praxis_cost = -1;
  int node_count = 0;
  int installed_nodes = 0;
  int next_praxis_cost = -1;
  // -1 buys the base family, >=0 buys that upgrade node, and -2 means there
  // is no currently valid purchase in the family.
  int next_node_index = -2;
  bool tree_present = false;
  bool installed = false;
  bool purchase_available = false;
  bool affordable = false;
  AugmentationTreeState state = AugmentationTreeState::kUnavailable;
  char name[96] = {};
  char description[384] = {};
  AugmentationNodeSnapshot nodes[kMaximumAugmentationNodesPerTree] = {};
};

struct InventorySnapshot {
  bool valid = false;
  uintptr_t inventory_identity = 0;
  uint32_t revision = 0;
  int grid_width = 0;
  int grid_height = 0;
  uint32_t credits = 0;
  float health = -1.0f;
  float max_health = -1.0f;
  // Native player progression/status values. A negative value means that the
  // exact 2.0.66.0 getter was unavailable, so the UI can fail closed instead
  // of presenting fabricated data.
  float energy = -1.0f;
  float max_energy = -1.0f;
  int total_xp = -1;
  int next_praxis = -1;
  int praxis_points = -1;
  // Native ownership and live toggle state for the active augmentations shown
  // in the lower-screen status panel. The order is Typhoon, Cloak, Smart
  // Vision, and Run Silently. Ownership comes from the saved augmentation
  // graph; active flags come from retail controller properties.
  bool augmentation_state_valid = false;
  bool augmentation_unlocked[kCoreAugmentationCount] = {};
  bool augmentation_active_valid = false;
  bool augmentation_active[kCoreAugmentationCount] = {};
  bool augmentation_catalog_valid = false;
  int augmentation_count = 0;
  AugmentationTreeSnapshot augmentations[kMaximumAugmentationTrees] = {};
  int item_count = 0;
  InventoryItemSnapshot items[256] = {};
  QuickSlotSnapshot quick_slots[10] = {};
};

struct GameActionResult {
  GameActionKind kind = GameActionKind::kTouch;
  uint32_t token = 0;
  bool success = false;
  // CanMove: swap item index (-1 when empty). Combine: resulting item index.
  // Other actions leave this at -1.
  int value = -1;
};

// Called only by DrainGameActions, on the game/Present thread. Returning true
// means the custom UI consumed the touch. The fallback handler only hit-tests
// the persistent page header; content is deliberately inert until its page has
// registered exact geometry.
using GuestTouchDispatcher = bool (*)(const GuestTouch& touch, void* context);
using PageSelectionHandler = bool (*)(int page, void* context);
using GameActionResultHandler = void (*)(const GameActionResult& result,
                                         void* context);

void SetGuestTouchDispatcher(GuestTouchDispatcher dispatcher, void* context);
void SetPageSelectionHandler(PageSelectionHandler handler, void* context);
void SetGameActionResultHandler(GameActionResultHandler handler, void* context);

// Thread-safe producers. These never dereference a game object.
bool QueueGuestTouch(GuestGesture gesture, float x, float y,
                     float value = 0.0f, uint32_t token = 0);
bool QueueSelectPage(int page, uint32_t token = 0);
bool QueueInventoryCanMove(int item_index, int x, int y, bool rotated,
                           uint32_t token = 0);
bool QueueInventoryMove(int item_index, int x, int y, bool rotated,
                        uint32_t token = 0);
bool QueueInventoryCombine(const InventoryActionIdentity& item_a,
                           const InventoryActionIdentity& item_b,
                           uint32_t token = 0);
bool QueueSetQuickSlot(int slot, const InventoryActionIdentity& item,
                       uint32_t token = 0);
bool QueueClearQuickSlot(int slot, const InventoryActionIdentity& item,
                         uint32_t token = 0);
bool QueueActivateQuickSlot(int slot, const InventoryActionIdentity& item,
                            uint32_t token = 0);
bool QueueDiscardInventoryItem(const InventoryActionIdentity& item,
                               uint32_t token = 0);
bool QueueUseInventoryItem(const InventoryActionIdentity& item,
                           uint32_t token = 0);

// Present-thread consumer. Probe.cpp calls this once per primary game frame.
void DrainGameActions();
void ClearQueuedGameActions();

// Direct helpers for a custom hit dispatcher or snapshot builder. They reject
// calls made outside the Present thread and validate the supported 2.0.66.0
// code signatures before entering any build-specific game method.
void* CurrentInventoryPlayer();
bool CanMoveInventoryItem(int item_index, int x, int y, bool rotated,
                          int* swap_index);
bool MoveInventoryItem(int item_index, int x, int y, bool rotated);
// Exchanges two live items through verified empty native grid space. DXHR's
// Move routine detaches an occupied destination instead of swapping it, so
// callers must use this guarded operation for item-on-item rearrangement.
bool SwapInventoryItems(int item_a, int item_b);
// Returns true when the pair was natively combinable and the Combine call was
// made.  `result_index` is the game's result and may legitimately be -1 when
// the operation consumed/replaced an item.  Callers must use this boolean,
// rather than `result_index >= 0`, to decide whether to fall back to Move.
bool TryCombineInventoryItems(const InventoryActionIdentity& item_a,
                              const InventoryActionIdentity& item_b,
                              int* result_index);
int CombineInventoryItems(const InventoryActionIdentity& item_a,
                          const InventoryActionIdentity& item_b);
bool SetInventoryQuickSlot(int slot, const InventoryActionIdentity& item);
bool ClearInventoryQuickSlot(int slot, const InventoryActionIdentity& item);
bool ActivateInventoryQuickSlot(int slot, const InventoryActionIdentity& item);
bool DiscardInventoryItem(const InventoryActionIdentity& item);
bool UseInventoryItem(const InventoryActionIdentity& item);
// Exact retail controller callbacks. These are intentionally separate from
// inventory activation so a held weapon can always holster and a long press
// can request a reload without synthesizing a controller button.
bool HolsterEquippedWeapon(const InventoryActionIdentity& item);
bool ReloadEquippedWeapon(const InventoryActionIdentity& item);
// Activates/toggles Typhoon, Cloak, Smart Vision, or Run Silently (0..3) by
// calling the corresponding retail controller callback.
bool ActivateCoreAugmentation(int slot, uintptr_t expected_inventory,
                              uint32_t expected_revision);
// Purchases a native augmentation family (-1) or upgrade node (>=0), using
// the same validation and mutation calls as the retail SmartGlass handler.
bool PurchaseNativeAugmentation(int tree_id, int node_index,
                                uintptr_t expected_inventory,
                                uint32_t expected_revision);

// Installs the guarded gameplay-side improvements owned by this mod. The
// current pass extends the retail energy recharge routine beyond its first
// cell while retaining the game's own delay and recharge-rate parameters.
bool InstallGameplayEnhancements(HookSet* hooks);

// A coherent, allocation-free view of the live player inventory. Both calls
// are Present-thread-only and leave the destination zeroed on failure.
bool ResolvePickupMetadata(uint32_t pickup_id, PickupMetadata* out);
bool CaptureInventorySnapshot(InventorySnapshot* out);
// Lightweight gameplay-readiness probe. This validates the live inventory
// object and its grid without invoking pickup localization, HUD getters, or
// SmartGlass serialization. Panel uses consecutive successful probes to avoid
// entering native UI code while a save is still constructing the player.
bool NativeInventoryAvailable();
// Becomes true only after a complete guarded inventory snapshot has succeeded
// for the current loaded save. Legacy serializers must not enter the game's
// transient inventory interface before this gate opens.
bool NativeInventorySnapshotReady();
void ResetNativeInventorySnapshotReady();

// Custom-page state only. This never opens a vanilla menu and never injects a
// key. A registered page handler owns the real UI; the current Panel selection
// is a compatibility fallback while that renderer is being migrated.
bool SelectCustomPage(int page);
int SelectedCustomPage();

}  // namespace dxs
