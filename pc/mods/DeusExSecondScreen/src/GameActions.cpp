#include "GameActions.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>

#include "Log.h"
#include "Hook.h"
#include "Mem.h"

namespace dxs {
namespace {

#if defined(__GNUC__)
#define DXS_GAME_THISCALL __attribute__((thiscall))
#define DXS_GAME_CDECL __attribute__((cdecl))
#define DXS_GAME_STDCALL __attribute__((stdcall))
#define DXS_GAME_FASTCALL __attribute__((fastcall))
#else
#define DXS_GAME_THISCALL __thiscall
#define DXS_GAME_CDECL __cdecl
#define DXS_GAME_STDCALL __stdcall
#define DXS_GAME_FASTCALL __fastcall
#endif

// Preferred addresses from DXHRDC.exe 2.0.66.0 (PE timestamp 0x52655004,
// SizeOfImage 0x01c54000). ReportBuild performs the broad build gate; each
// entry point below is guarded again at its call site so a partially patched
// or mismatched executable fails closed instead of corrupting a save.
constexpr uintptr_t kPlayerListGlobalVA = 0x01A07B1C;
constexpr uintptr_t kCurrentPlayerWrapperVA = 0x00602F60;
constexpr uintptr_t kActorInventoryVA = 0x0066AA50;
// InventoryPlayer's primary vtable starts immediately after its MSVC RTTI
// complete-object-locator at 0x00AA6400.  0x00AA63EC is the nearby
// "Found" type-name string, not a vtable; comparing against it made every
// otherwise-valid live inventory fail closed.
constexpr uintptr_t kInventoryPlayerVtableVA = 0x00AA6404;
constexpr uintptr_t kCanMoveVA = 0x0066AC00;
constexpr uintptr_t kMoveVA = 0x0066AC10;
constexpr uintptr_t kCanCombineVA = 0x0066B190;
constexpr uintptr_t kCombineVA = 0x0066C080;
constexpr uintptr_t kSetQuickVA = 0x0066B7C0;
constexpr uintptr_t kClearQuickVA = 0x0066AB50;
constexpr uintptr_t kActivateQuickVA = 0x0066ADE0;
constexpr uintptr_t kCanDiscardVA = 0x0066DF60;
constexpr uintptr_t kDiscardVA = 0x00671450;
constexpr uintptr_t kCanUseVA = 0x0066AD80;
// InventoryPlayer vtable slot 7. This is the complete native activation path
// used by InventoryPlayer::ActivateQuickSlot: consumables are used, weapons
// are equipped, and activating the currently equipped weapon holsters it.
// Calling the former 0x006B575F interior entry skipped its owning function's
// prologue/exception frame and could corrupt the process after a touch action.
constexpr uintptr_t kUseInventoryItemVA = 0x0066AF30;
// Controller callbacks registered for the retail weaponholsterdrawtoggle and
// weaponreload actions. Calling these is preset-independent and avoids turning
// a lower-screen gesture into a synthetic XInput press.
constexpr uintptr_t kHolsterWeaponVA = 0x007BB520;
constexpr uintptr_t kReloadWeaponVA = 0x007BB620;
constexpr uintptr_t kResolvePickupVA = 0x005A4E60;
constexpr uintptr_t kLocalStringVA = 0x004CC930;
// This is the pickup `name` helper used by Inventory Enter at 0x006B1E90.
// Unlike 0x00776EC0 (the SmartGlass `subtitle` helper), it resolves the
// localized string index at dtp::Pickup+0x02.
constexpr uintptr_t kPickupNameVA = 0x00777000;
constexpr uintptr_t kActorFromHandleVA = 0x0077F710;
constexpr uintptr_t kCurrentHealthVA = 0x00665790;
constexpr uintptr_t kMaxHealthVA = 0x00665770;
// The SmartGlass status/augmentation serializers use these exact native
// getters. Keeping their paths here avoids relying on the serializer, whose
// output is absent on some Wine/Android configurations.
constexpr uintptr_t kExperienceForActorVA = 0x0065B5D0;
constexpr uintptr_t kTotalExperienceVA = 0x0051B630;
constexpr uintptr_t kNextPraxisVA = 0x0065BC50;
constexpr uintptr_t kPraxisPointsVA = 0x00673C30;
// NsEnergyBarMovieController::Update resolves the energy system through this
// path and derives maximum energy from the configured cell count and size.
constexpr uintptr_t kEnergyPlayerFromHandleVA = 0x00787CF0;
constexpr uintptr_t kEnergySystemVA = 0x00787310;
constexpr uintptr_t kEnergyPointsPerCellVA = 0x0065E220;
constexpr uintptr_t kEnergyCellCountVA = 0x0065E240;
// EnergySystem::Recharge is the single retail path that applies the normal
// recharge delay/rate. It deliberately stops at the first cell boundary.
// The hook below lets that same recharge continue toward the real maximum.
constexpr uintptr_t kEnergyRechargeVA = 0x0065E7A0;
constexpr uintptr_t kEnergyRechargeDelayVA = 0x0065E1F0;
constexpr uintptr_t kEnergyRechargeRateVA = 0x0065E260;
constexpr uintptr_t kEnergyMaximumVA = 0x0065E3E0;
constexpr uintptr_t kEnergyAddVA = 0x0065E500;
// The retail SmartGlass augmentation serializer enumerates the first array in
// this GlobalPlayerInfo object. It is the authority for the tree DTP ids.
constexpr uintptr_t kGlobalPlayerInfoVA = 0x01CADE1C;
constexpr uintptr_t kAugmentationTreeNameVA = 0x0065B8C0;
constexpr uintptr_t kAugmentationTreeNodeCountVA = 0x0065B7C0;
constexpr uintptr_t kAugmentationTreeTypeVA = 0x0065B7D0;
// 0x65BC20 returns the record's non-zero node marker. The marker is not the
// resolved upgrade DTP and must never be dereferenced. Retail resolves the
// record's +0x10 DTP id through this helper before reading cost/text data.
constexpr uintptr_t kAugmentationTreeNodeVA = 0x0065BC20;
constexpr uintptr_t kAugmentationNodeDataVA = 0x0065B6A0;
constexpr uintptr_t kAugmentationNodeNameVA = 0x0065BB90;
constexpr uintptr_t kAugmentationNodeDescriptionVA = 0x0065BBB0;
constexpr uintptr_t kAugmentationNodeHelpVA = 0x0065BBD0;
constexpr uintptr_t kAugmentationNodeTextureVA = 0x0065BC10;
constexpr uintptr_t kAugmentationNodeCostVA = 0x006F3F20;
constexpr uintptr_t kAugmentationTreeInstalledVA = 0x0065C290;
constexpr uintptr_t kAugmentationNodeInstalledVA = 0x0065C2D0;
constexpr uintptr_t kAugmentationPrerequisitesVA = 0x0065C320;
constexpr uintptr_t kAugmentationAvailableVA = 0x0065B6C0;
constexpr uintptr_t kAugmentationBaseCostVA = 0x0065B780;
constexpr uintptr_t kSubtractPraxisVA = 0x0065BD80;
constexpr uintptr_t kInstallAugmentationNodeVA = 0x0065D960;
constexpr uintptr_t kInstallAugmentationTreeVA = 0x0065DAA0;
// NsSmartGlass/native augmentation action validators. These are the retail
// menu's final authority for ownership, prerequisites, data-build overrides,
// and current Praxis affordability before its purchase handler mutates state.
constexpr uintptr_t kValidateAugmentationNodePurchaseVA = 0x006AF820;
constexpr uintptr_t kValidateAugmentationTreePurchaseVA = 0x006AF8E0;
constexpr uintptr_t kRefreshInventoryVA = 0x0066A950;
// Retail input-controller callbacks. Calling these preserves the game's own
// unlock, energy, cooldown, animation and mutually-exclusive toggle logic.
constexpr uintptr_t kActivateTyphoonVA = 0x007B91C0;
constexpr uintptr_t kToggleCloakVA = 0x007B9310;
constexpr uintptr_t kToggleRunSilentlyVA = 0x007B93A0;
constexpr uintptr_t kToggleSmartVisionVA = 0x007B9400;
constexpr uintptr_t kCloakActiveVA = 0x006901A0;
constexpr uintptr_t kSmartVisionActiveVA = 0x0069D0F0;
constexpr uintptr_t kGameParametersGlobalVA = 0x01CADE18;

constexpr int kMaxInventorySlots = 256;
constexpr int kQuickSlotCount = 10;
constexpr size_t kInventoryContainerOffset = 0x10;
constexpr size_t kInventoryEquippedWeaponOffset = 0x14;
constexpr size_t kEquippedWeaponItemOffset = 0x70;
constexpr size_t kMoneyOffset = 0x60;
constexpr size_t kQuickSlotsOffset = 0xA0;
constexpr size_t kInventoryRevisionOffset = 0xC8;
constexpr size_t kContainerGridMapOffset = 0x04;
constexpr size_t kContainerSlotsOffset = 0x08;
constexpr size_t kContainerSlotStride = 0x28;
constexpr size_t kContainerGridWidthOffset = 0x2808;
constexpr size_t kContainerGridHeightOffset = 0x280C;

constexpr uint32_t kEmptyPickup = 0xFFFFFFFFu;
constexpr uint32_t kMaxPickupId = 0x18000u;

struct NativeSlot {
  uint32_t pickup_id = kEmptyPickup;  // +00
  uint8_t occupied = 0;              // +04
  uint8_t rotated = 0;               // +05
  uint16_t count = 0;                // +06
  uint32_t x = 0;                    // +08
  uint32_t y = 0;                    // +0c
  uint8_t item_index = 0;            // +10
  uint8_t reserved_11[3] = {};        // +11
  int16_t magazine_ammo = 0;         // +14
  uint8_t magazine_initialized = 0;  // +16
  uint8_t damage_upgrade_level = 0;  // +17
  uint8_t ammo_upgrade_level = 0;    // +18
  uint8_t rate_upgrade_level = 0;    // +19
  uint8_t reload_upgrade_level = 0;  // +1a
  uint8_t laser_installed = 0;       // +1b
  uint8_t silencer_installed = 0;    // +1c
  uint8_t reserved_1d[3] = {};        // +1d
  uint32_t runtime_reference = 0;     // +20
  uint8_t runtime_flags = 0;          // +24
  uint8_t reserved_25[3] = {};        // +25
};

static_assert(offsetof(NativeSlot, item_index) == 0x10,
              "DXHR inventory slot layout changed");
static_assert(offsetof(NativeSlot, magazine_ammo) == 0x14,
              "DXHR inventory magazine layout changed");
static_assert(offsetof(NativeSlot, ammo_upgrade_level) == 0x18,
              "DXHR inventory upgrade layout changed");
static_assert(sizeof(NativeSlot) == kContainerSlotStride,
              "DXHR inventory slot stride changed");

int EffectiveMagazineCapacity(int base_capacity, uint8_t upgrade_level) {
  if (base_capacity <= 0 || base_capacity > 10000) return -1;
  if (upgrade_level == 0) return base_capacity;

  // Director's Cut applies a fixed round increment per Ammo Capacity mod.
  // These values are the retail 2.0.66.0 weapon table, keyed by its native
  // base magazine size so localization cannot affect the result. Weapons
  // which share a base size also share the same increment. Unknown records
  // fail closed at the base value rather than inventing a larger magazine.
  int rounds_per_upgrade = 0;
  switch (base_capacity) {
    case 3:   // Sniper rifle
    case 5:   // Revolver / silenced sniper rifle
      rounds_per_upgrade = 1;
      break;
    case 6:   // Shotgun
      rounds_per_upgrade = 2;
      break;
    case 10:  // 10mm pistol
      rounds_per_upgrade = 3;
      break;
    case 20:  // Combat rifle / plasma rifle
    case 30:  // Machine pistol
      rounds_per_upgrade = 10;
      break;
    case 100: // Heavy rifle
      rounds_per_upgrade = 25;
      break;
    case 500: // Laser rifle
      rounds_per_upgrade = 100;
      break;
    default:
      return base_capacity;
  }
  const int capacity =
      base_capacity + rounds_per_upgrade * static_cast<int>(upgrade_level);
  return capacity > 0 && capacity <= 10000 ? capacity : -1;
}

struct QueuedAction {
  GameActionKind kind = GameActionKind::kTouch;
  uint32_t token = 0;
  int a = 0;
  int b = 0;
  int c = 0;
  int d = 0;
  InventoryActionIdentity item_a;
  InventoryActionIdentity item_b;
  GuestTouch touch;
};

constexpr size_t kQueueCapacity = 256;
SRWLOCK g_queue_lock = SRWLOCK_INIT;
QueuedAction g_queue[kQueueCapacity];
size_t g_queue_head = 0;
size_t g_queue_count = 0;

SRWLOCK g_callback_lock = SRWLOCK_INIT;
GuestTouchDispatcher g_touch_dispatcher = nullptr;
void* g_touch_context = nullptr;
PageSelectionHandler g_page_handler = nullptr;
void* g_page_context = nullptr;
GameActionResultHandler g_result_handler = nullptr;
void* g_result_context = nullptr;

std::atomic<DWORD> g_game_thread{0};
std::atomic<uint64_t> g_game_thread_last_seen_ms{0};
std::atomic<uint64_t> g_wrong_game_thread_warning_ms{0};
std::atomic<int> g_selected_page{3};
std::atomic<bool> g_reported_queue_full{false};
std::atomic<bool> g_reported_signature_failure{false};
std::atomic<bool> g_inventory_snapshot_ready{false};
std::atomic<uint64_t> g_typhoon_active_until_ms{0};

using EnergyRechargeFn = void(DXS_GAME_THISCALL*)(void*, float);
EnergyRechargeFn g_original_energy_recharge = nullptr;

struct PickupCacheEntry {
  bool valid = false;
  uint32_t pickup_id = kEmptyPickup;
  PickupMetadata metadata;
};
constexpr size_t kPickupCacheSize = 512;
PickupCacheEntry g_pickup_cache[kPickupCacheSize];

template <size_t N>
bool HasCode(uintptr_t preferred_va, const uint8_t (&signature)[N]) {
  const void* address = reinterpret_cast<const void*>(Rebase(preferred_va));
  return PreferredCodeMatches(address, signature, N);
}

void ReportSignatureFailure(const char* operation) {
  bool expected = false;
  if (g_reported_signature_failure.compare_exchange_strong(expected, true)) {
    DXS_ERR("[action] %s rejected: DXHRDC 2.0.66.0 native signature mismatch",
            operation);
  }
}

bool OnGameThread() {
  const DWORD owner = g_game_thread.load(std::memory_order_acquire);
  return owner != 0 && owner == GetCurrentThreadId();
}

bool PushAction(const QueuedAction& action) {
  bool pushed = false;
  const bool terminal_touch =
      action.kind == GameActionKind::kTouch &&
      (action.touch.gesture == GuestGesture::kUp ||
       action.touch.gesture == GuestGesture::kCancel);
  AcquireSRWLockExclusive(&g_queue_lock);
  // A stalled Present (loading, device reset, or a severe frame spike) can
  // otherwise accumulate hundreds of redundant pointer moves and leave no
  // room for the terminal Up/Cancel that retires the gesture. Panel computes
  // pan and drag thresholds from absolute coordinates, so retaining the newest
  // consecutive move is lossless and keeps the queue bounded in practice.
  if (action.kind == GameActionKind::kTouch &&
      action.touch.gesture == GuestGesture::kMove && g_queue_count != 0) {
    const size_t last =
        (g_queue_head + g_queue_count - 1) % kQueueCapacity;
    if (g_queue[last].kind == GameActionKind::kTouch &&
        g_queue[last].touch.gesture == GuestGesture::kMove &&
        g_queue[last].token == action.token) {
      g_queue[last] = action;
      pushed = true;
    }
  }
  // Keep one entry in reserve for Up/Cancel. Losing a terminal event leaves
  // Panel's capture attached to the old gesture and can turn the next touch
  // into an unintended drag or activation.
  const size_t queue_limit =
      terminal_touch ? kQueueCapacity : kQueueCapacity - 1;
  if (!pushed && g_queue_count < queue_limit) {
    const size_t tail = (g_queue_head + g_queue_count) % kQueueCapacity;
    g_queue[tail] = action;
    ++g_queue_count;
    pushed = true;
  }
  // Defensive recovery for a queue already filled before this policy was
  // applied: retiring capture is safer than preserving its oldest event.
  if (!pushed && terminal_touch && g_queue_count == kQueueCapacity) {
    g_queue_head = (g_queue_head + 1) % kQueueCapacity;
    --g_queue_count;
    const size_t tail = (g_queue_head + g_queue_count) % kQueueCapacity;
    g_queue[tail] = action;
    ++g_queue_count;
    pushed = true;
  }
  ReleaseSRWLockExclusive(&g_queue_lock);
  if (!pushed && !g_reported_queue_full.exchange(true)) {
    DXS_WARN("[action] lower-screen action queue full; dropping input until next frame");
  }
  return pushed;
}

bool PopAction(QueuedAction* out) {
  if (!out) return false;
  bool popped = false;
  AcquireSRWLockExclusive(&g_queue_lock);
  if (g_queue_count != 0) {
    *out = g_queue[g_queue_head];
    g_queue_head = (g_queue_head + 1) % kQueueCapacity;
    --g_queue_count;
    popped = true;
  }
  ReleaseSRWLockExclusive(&g_queue_lock);
  if (popped) g_reported_queue_full.store(false, std::memory_order_release);
  return popped;
}

bool ValidateInventory(void* inventory) {
  if (!inventory || !Readable(inventory, kInventoryRevisionOffset + sizeof(uint32_t))) {
    return false;
  }
  void* vtable = nullptr;
  if (!Read(inventory, &vtable) ||
      vtable != reinterpret_cast<void*>(Rebase(kInventoryPlayerVtableVA))) {
    return false;
  }
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  if (!container || !Readable(container, sizeof(void*))) return false;
  void* container_vtable = nullptr;
  void* get_slot = nullptr;
  if (!Read(container, &container_vtable) || !container_vtable ||
      !Readable(static_cast<uint8_t*>(container_vtable) + 0x2C, sizeof(void*)) ||
      !Read(static_cast<uint8_t*>(container_vtable) + 0x2C, &get_slot) ||
      !Executable(get_slot, 1)) {
    return false;
  }
  uint32_t width = 0;
  uint32_t height = 0;
  if (!Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset, &width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset, &height) ||
      width == 0 || height == 0 || width > 64 || height > 64) {
    return false;
  }
  void* grid_map = PtrAt(container, kContainerGridMapOffset);
  const size_t cells = static_cast<size_t>(width) * height;
  return grid_map && Readable(grid_map, cells) &&
         Readable(static_cast<uint8_t*>(container) + kContainerSlotsOffset,
                  kContainerSlotStride * kMaxInventorySlots);
}

bool ReadNativeSlotFromContainer(void* container, int storage_index,
                                 NativeSlot* out) {
  if (!container || !out || storage_index < 0 ||
      storage_index >= kMaxInventorySlots) return false;
  const uint8_t* p = static_cast<const uint8_t*>(container) +
                     kContainerSlotsOffset +
                     static_cast<size_t>(storage_index) * kContainerSlotStride;
  // Validate and copy the complete 0x28-byte slot once. The supplemental half
  // contains live magazine and installed weapon-upgrade state; truncating the
  // copy at 0x14 made those values unavailable to the lower-screen UI. The
  // previous field-by-field
  // Read calls performed seven VirtualQuery walks per slot and were a major
  // source of the observed 1 FPS fallback behavior.
  if (!Readable(p, sizeof(NativeSlot))) return false;
  std::memcpy(out, p, sizeof(NativeSlot));
  return true;
}

// The 256 slot records at container+0x08 are a reusable backing allocation.
// Removed/moved items remain in unused records and therefore cannot be
// enumerated linearly. The game's own grid lookup (DXHRDC 0x00775420) uses the
// byte map at container+0x04; each occupied cell contains the live backing
// index and 0xff means empty. Walking that map gives the same active set as the
// game without calling its allocating vector enumerator.
bool CollectActiveStorageIndices(void* container, int* indices, int* count) {
  if (count) *count = 0;
  if (!container || !indices || !count) return false;
  uint32_t width = 0;
  uint32_t height = 0;
  if (!Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset, &width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset, &height) ||
      width == 0 || height == 0 || width > 64 || height > 64) {
    return false;
  }
  const size_t cell_count = static_cast<size_t>(width) * height;
  void* grid_map = PtrAt(container, kContainerGridMapOffset);
  if (!grid_map || !Readable(grid_map, cell_count)) return false;

  uint8_t cells[64 * 64] = {};
  std::memcpy(cells, grid_map, cell_count);
  bool seen[kMaxInventorySlots] = {};
  for (size_t cell = 0; cell < cell_count; ++cell) {
    const uint8_t storage = cells[cell];
    if (storage == 0xff || seen[storage]) continue;
    seen[storage] = true;
    indices[(*count)++] = storage;
  }
  return true;
}

bool FindNativeSlot(void* inventory, int item_index, NativeSlot* out,
                    int* storage_index = nullptr) {
  if (item_index < 0 || item_index >= kMaxInventorySlots ||
      !ValidateInventory(inventory)) return false;
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  int active[kMaxInventorySlots] = {};
  int active_count = 0;
  if (!CollectActiveStorageIndices(container, active, &active_count)) return false;
  for (int n = 0; n < active_count; ++n) {
    const int i = active[n];
    NativeSlot slot;
    if (!ReadNativeSlotFromContainer(container, i, &slot)) return false;
    if (slot.pickup_id != kEmptyPickup && slot.item_index == item_index) {
      if (out) *out = slot;
      if (storage_index) *storage_index = i;
      return true;
    }
  }
  return false;
}

bool MatchInventoryActionIdentity(void* inventory,
                                  const InventoryActionIdentity& identity,
                                  NativeSlot* slot, const char* operation) {
  uint32_t live_revision = 0;
  NativeSlot live_slot;
  const bool matched = inventory && identity.inventory != 0 &&
      identity.inventory == reinterpret_cast<uintptr_t>(inventory) &&
      identity.item_index >= 0 && identity.item_index < kMaxInventorySlots &&
      identity.pickup_id != kEmptyPickup && identity.pickup_id < kMaxPickupId &&
      Read(static_cast<uint8_t*>(inventory) + kInventoryRevisionOffset,
           &live_revision) && live_revision == identity.revision &&
      FindNativeSlot(inventory, identity.item_index, &live_slot) &&
      live_slot.pickup_id == identity.pickup_id;
  if (matched) {
    if (slot) *slot = live_slot;
    return true;
  }

  // A mismatch is expected occasionally when gameplay changes the inventory
  // between the last 250 ms snapshot and a touch. Rate-limit the diagnostic so
  // a held gesture cannot flood the log while still making the rejection clear.
  static uint64_t last_report_ms = 0;
  const uint64_t now = GetTickCount64();
  if (last_report_ms == 0 || now - last_report_ms >= 1000) {
    last_report_ms = now;
    DXS_WARN("[action] rejected stale inventory identity for %s "
             "(item=%d pickup=%u revision=%u)",
             operation ? operation : "action", identity.item_index,
             static_cast<unsigned>(identity.pickup_id),
             static_cast<unsigned>(identity.revision));
  }
  return false;
}

uintptr_t CurrentPlayerActorHandle() {
  static const uint8_t current_sig[] = {0x8B, 0x41, 0x08, 0x8B,
                                        0x04, 0x81, 0xC3};
  if (!OnGameThread() || !HasCode(kCurrentPlayerWrapperVA, current_sig)) return 0;
  void* player_list = nullptr;
  if (!Read(reinterpret_cast<void*>(Rebase(kPlayerListGlobalVA)), &player_list) ||
      !player_list || !Readable(player_list, 12)) {
    return 0;
  }
  uint32_t current_index = 0;
  void* expected_wrapper = nullptr;
  if (!Read(static_cast<uint8_t*>(player_list) + 8, &current_index) ||
      current_index > 1 ||
      !Read(static_cast<uint8_t*>(player_list) + current_index * sizeof(void*),
            &expected_wrapper) ||
      !expected_wrapper || !Readable(expected_wrapper, 8)) {
    return 0;
  }
  using CurrentFn = void*(DXS_GAME_THISCALL*)(void*);
  void* wrapper = reinterpret_cast<CurrentFn>(Rebase(kCurrentPlayerWrapperVA))(
      player_list);
  uintptr_t actor = 0;
  return wrapper == expected_wrapper && Readable(wrapper, 8) &&
                 Read(static_cast<uint8_t*>(wrapper) + 4, &actor)
             ? actor
             : 0;
}

void* CurrentPlayerController() {
  static const uint8_t current_sig[] = {0x8B, 0x41, 0x08, 0x8B,
                                        0x04, 0x81, 0xC3};
  if (!OnGameThread() || !HasCode(kCurrentPlayerWrapperVA, current_sig)) {
    return nullptr;
  }
  void* player_list = nullptr;
  if (!Read(reinterpret_cast<void*>(Rebase(kPlayerListGlobalVA)), &player_list) ||
      !player_list || !Readable(player_list, 12)) {
    return nullptr;
  }
  uint32_t current_index = 0;
  void* expected_wrapper = nullptr;
  if (!Read(static_cast<uint8_t*>(player_list) + 8, &current_index) ||
      current_index > 1 ||
      !Read(static_cast<uint8_t*>(player_list) + current_index * sizeof(void*),
            &expected_wrapper) ||
      !expected_wrapper || !Readable(expected_wrapper, 0x48)) {
    return nullptr;
  }
  using CurrentFn = void*(DXS_GAME_THISCALL*)(void*);
  void* wrapper = reinterpret_cast<CurrentFn>(Rebase(kCurrentPlayerWrapperVA))(
      player_list);
  if (wrapper != expected_wrapper || !Readable(wrapper, 0x48)) return nullptr;

  uintptr_t actor = 0;
  void* actor_state = nullptr;
  void* player = nullptr;
  void* controller_component = nullptr;
  if (!Read(static_cast<uint8_t*>(wrapper) + 4, &actor) || !actor ||
      !Readable(reinterpret_cast<void*>(actor), 0x5C) ||
      !Read(reinterpret_cast<uint8_t*>(actor) + 0x58, &actor_state) ||
      !actor_state || !Readable(actor_state, 0x18) ||
      !Read(static_cast<uint8_t*>(actor_state) + 0x14, &player) || !player ||
      !Read(static_cast<uint8_t*>(wrapper) + 0x44, &controller_component) ||
      !controller_component ||
      reinterpret_cast<uintptr_t>(controller_component) < 0x388u) {
    return nullptr;
  }

  void* controller = static_cast<uint8_t*>(controller_component) - 0x388;
  uintptr_t controller_actor = 0;
  void* controller_player = nullptr;
  if (!Readable(controller, 0x524) ||
      !Read(static_cast<uint8_t*>(controller) + 0x8C, &controller_actor) ||
      !Read(static_cast<uint8_t*>(controller) + 0x520, &controller_player) ||
      controller_actor != actor || controller_player != player) {
    return nullptr;
  }
  return controller;
}

void CaptureHealth(InventorySnapshot* out) {
  if (!out) return;
  static const uint8_t actor_signature[] = {0x56, 0x8B, 0x74, 0x24, 0x08,
                                            0x85, 0xF6, 0x74, 0x3E};
  static const uint8_t current_signature[] = {0x51, 0xD9, 0xEE, 0xD8,
                                              0x59, 0x60, 0xDF, 0xE0};
  static const uint8_t max_signature[] = {0x8B, 0x41, 0x0C, 0xD9,
                                          0x40, 0x08, 0xC3};
  if (!HasCode(kActorFromHandleVA, actor_signature) ||
      !HasCode(kCurrentHealthVA, current_signature) ||
      !HasCode(kMaxHealthVA, max_signature)) {
    ReportSignatureFailure("health snapshot");
    return;
  }
  const uintptr_t handle = CurrentPlayerActorHandle();
  if (!handle || !Readable(reinterpret_cast<void*>(handle), 0x5C)) return;
  using ActorFn = void*(DXS_GAME_CDECL*)(uintptr_t);
  void* actor = reinterpret_cast<ActorFn>(Rebase(kActorFromHandleVA))(handle);
  void* health_system = nullptr;
  void* health_definition = nullptr;
  if (!actor || !Readable(actor, 0x68) ||
      !Read(static_cast<uint8_t*>(actor) + 0x64, &health_system) ||
      !health_system || !Readable(health_system, 0x64) ||
      !Read(static_cast<uint8_t*>(health_system) + 0x0C, &health_definition) ||
      !health_definition || !Readable(health_definition, 0x18)) {
    return;
  }
  using HealthFn = float(DXS_GAME_THISCALL*)(void*);
  const float current =
      reinterpret_cast<HealthFn>(Rebase(kCurrentHealthVA))(health_system);
  const float maximum =
      reinterpret_cast<HealthFn>(Rebase(kMaxHealthVA))(health_system);
  if (std::isfinite(current) && std::isfinite(maximum) && current >= 0.0f &&
      maximum > 0.0f && current <= 10000.0f && maximum <= 10000.0f) {
    out->health = current;
    out->max_health = maximum;
  }
}

void CaptureProgressAndEnergy(InventorySnapshot* out) {
  if (!out) return;
  static const uint8_t experience_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8, 0x16, 0xC7, 0x12, 0x00};
  static const uint8_t total_xp_signature[] = {0x8B, 0x41, 0x0C, 0xC3};
  static const uint8_t next_praxis_signature[] = {
      0xA1, 0x18, 0xDE, 0xCA, 0x01, 0x8B, 0x80, 0x20, 0x06, 0x00, 0x00};
  static const uint8_t praxis_points_signature[] = {0x8B, 0x41, 0x10, 0xC3};
  static const uint8_t energy_player_signature[] = {
      0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6, 0x74, 0x36};
  static const uint8_t energy_system_signature[] = {
      0x8D, 0x81, 0x78, 0x02, 0x00, 0x00, 0xC3};
  static const uint8_t points_per_cell_signature[] = {
      0x8B, 0x09, 0x6A, 0x01, 0xE8, 0xD7, 0x90, 0x12, 0x00, 0x8B, 0xC8};
  static const uint8_t cell_count_signature[] = {
      0x8B, 0x09, 0x6A, 0x02, 0xE8, 0xB7, 0x90, 0x12, 0x00, 0x8B, 0xC8};

  const bool player_layout_ok =
      HasCode(kExperienceForActorVA, experience_signature) &&
      HasCode(kEnergyPlayerFromHandleVA, energy_player_signature) &&
      HasCode(kEnergySystemVA, energy_system_signature);
  const bool progress_code_ok = player_layout_ok &&
      HasCode(kExperienceForActorVA, experience_signature) &&
      HasCode(kTotalExperienceVA, total_xp_signature) &&
      HasCode(kNextPraxisVA, next_praxis_signature) &&
      HasCode(kPraxisPointsVA, praxis_points_signature);
  const bool energy_code_ok = player_layout_ok &&
      HasCode(kEnergyPointsPerCellVA, points_per_cell_signature) &&
      HasCode(kEnergyCellCountVA, cell_count_signature);
  if (!progress_code_ok && !energy_code_ok) {
    ReportSignatureFailure("progress/energy snapshot");
    return;
  }

  const uintptr_t handle = CurrentPlayerActorHandle();
  if (!handle || !Readable(reinterpret_cast<void*>(handle), 0x5C)) return;

  // 0x787CF0 normally resolves this chain, but it also enters type/feature
  // services which dereference game globals that are temporarily null while a
  // save is being attached. Resolve only its final, immutable pointer chain
  // here and validate every link. This keeps the Present-time snapshot out of
  // partially initialized gameplay services.
  void* actor_state = nullptr;
  void* player = nullptr;
  if (!Read(reinterpret_cast<uint8_t*>(handle) + 0x58, &actor_state) ||
      !actor_state || !Readable(actor_state, 0x18) ||
      !Read(static_cast<uint8_t*>(actor_state) + 0x14, &player) || !player ||
      !Readable(player, 0x284)) {
    return;
  }
  void* experience = static_cast<uint8_t*>(player) + 0x130;
  void* energy_system = static_cast<uint8_t*>(player) + 0x278;

  if (progress_code_ok) {
    int total = -1;
    int points = -1;
    if (Read(static_cast<uint8_t*>(experience) + 0x0C, &total) &&
        Read(static_cast<uint8_t*>(experience) + 0x10, &points)) {
      if (total >= 0 && total <= 1000000000) {
        out->total_xp = total;

        // 0x65BC50 computes threshold - (total % threshold), but begins by
        // unconditionally dereferencing this global. Mirror that exact math
        // only after the global and its field are readable; during save load,
        // leaving next_praxis unknown is preferable to entering a null global.
        void* parameters = nullptr;
        int threshold = 0;
        if (Read(reinterpret_cast<void*>(Rebase(kGameParametersGlobalVA)),
                 &parameters) &&
            parameters && Readable(parameters, 0x624) &&
            Read(static_cast<uint8_t*>(parameters) + 0x620, &threshold)) {
          if (threshold == 0) threshold = 1;
          if (threshold > 0 && threshold <= 10000000) {
            out->next_praxis =
                threshold - static_cast<int>(static_cast<uint32_t>(total) %
                                             static_cast<uint32_t>(threshold));
          }
        }
      }
      if (points >= 0 && points <= 100000) out->praxis_points = points;
    }
  }

  if (energy_code_ok) {
    using FloatGetterFn = float(DXS_GAME_THISCALL*)(void*);
    using IntGetterFn = int(DXS_GAME_THISCALL*)(void*);
    void* owner = nullptr;
    float current = -1.0f;
    void* points_object = nullptr;
    void* cells_object = nullptr;
    if (!Readable(energy_system, 0x0C) ||
        !Read(energy_system, &owner) || owner != player ||
        !Read(static_cast<uint8_t*>(energy_system) + 0x04, &current) ||
        !Read(static_cast<uint8_t*>(experience) + 0x44, &points_object) ||
        !Read(static_cast<uint8_t*>(experience) + 0x48, &cells_object) ||
        !points_object || !cells_object ||
        !Readable(points_object, sizeof(void*)) ||
        !Readable(cells_object, sizeof(void*))) {
      return;
    }

    void* points_vtable = nullptr;
    void* cells_vtable = nullptr;
    void* points_getter = nullptr;
    void* cells_getter = nullptr;
    if (!Read(points_object, &points_vtable) || !points_vtable ||
        !Readable(points_vtable, 3 * sizeof(void*)) ||
        !Read(static_cast<uint8_t*>(points_vtable) + 2 * sizeof(void*),
              &points_getter) ||
        !points_getter || !Executable(points_getter, 1) ||
        !Read(cells_object, &cells_vtable) || !cells_vtable ||
        !Readable(cells_vtable, sizeof(void*)) ||
        !Read(cells_vtable, &cells_getter) || !cells_getter ||
        !Executable(cells_getter, 1)) {
      return;
    }

    // These are the two virtual property calls made by 0x65BE50/0x65BE10.
    // Calling them only after the owning energy system and both parameter
    // objects are coherent removes the save-transition crash window.
    const float points_per_cell =
        reinterpret_cast<FloatGetterFn>(points_getter)(points_object);
    const int cell_count =
        reinterpret_cast<IntGetterFn>(cells_getter)(cells_object);
    const float maximum = points_per_cell * static_cast<float>(cell_count);
    if (std::isfinite(current) && std::isfinite(points_per_cell) &&
        std::isfinite(maximum) && current >= 0.0f && points_per_cell > 0.0f &&
        points_per_cell <= 100000.0f && cell_count > 0 && cell_count <= 100 &&
        maximum > 0.0f && maximum <= 10000000.0f &&
        current <= maximum + 0.01f) {
      out->energy = current;
      out->max_energy = maximum;
    }
  }
}

void CaptureActiveAugmentations(InventorySnapshot* out) {
  if (!out) return;
  static const uint8_t cloak_active_signature[] = {
      0x8B, 0x09, 0x6A, 0x66, 0xE8, 0x67, 0x00, 0x0F, 0x00, 0xC3};
  static const uint8_t vision_active_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8, 0x96, 0x2D,
      0x08, 0x00, 0x83, 0xC4, 0x04, 0xC2, 0x04, 0x00};
  if (!HasCode(kCloakActiveVA, cloak_active_signature) ||
      !HasCode(kSmartVisionActiveVA, vision_active_signature)) {
    ReportSignatureFailure("active augmentation snapshot");
    return;
  }
  void* controller = CurrentPlayerController();
  if (!controller || !Readable(controller, 0x524)) return;
  using CloakActiveFn = bool(DXS_GAME_THISCALL*)(void*);
  using VisionActiveFn = bool(DXS_GAME_THISCALL*)(void*, bool);
  // CloakActive first follows the embedded +0x208 owner pointer and then
  // reads owner+0x44. During save transitions that owner can briefly be null
  // even while the outer controller allocation remains readable.
  void* cloak_owner = nullptr;
  if (!Read(static_cast<uint8_t*>(controller) + 0x208, &cloak_owner) ||
      !cloak_owner || !Readable(cloak_owner, 0x48)) {
    return;
  }
  uint8_t run_silently = 0;
  if (!Read(static_cast<uint8_t*>(controller) + 0x204, &run_silently)) return;
  out->augmentation_active[0] =
      GetTickCount64() <= g_typhoon_active_until_ms.load(
          std::memory_order_acquire);
  out->augmentation_active[1] =
      reinterpret_cast<CloakActiveFn>(Rebase(kCloakActiveVA))(
          static_cast<uint8_t*>(controller) + 0x208);
  out->augmentation_active[2] =
      reinterpret_cast<VisionActiveFn>(Rebase(kSmartVisionActiveVA))(
          static_cast<uint8_t*>(controller) + 0x198, false);
  out->augmentation_active[3] = run_silently != 0;
  out->augmentation_active_valid = true;
}

template <size_t N>
void CopyGameString(const char* source, char (&destination)[N]) {
  destination[0] = '\0';
  const char* safe = SafeCStr(source, 2048);
  if (!safe) return;
  size_t length = 0;
  while (length + 1 < N && safe[length] != '\0') ++length;
  std::memcpy(destination, safe, length);
  destination[length] = '\0';
}

bool ContainsAsciiInsensitive(const char* text, const char* needle) {
  if (!text || !needle || !*needle) return false;
  for (const char* start = text; *start; ++start) {
    const char* a = start;
    const char* b = needle;
    while (*a && *b &&
           std::tolower(static_cast<unsigned char>(*a)) ==
               std::tolower(static_cast<unsigned char>(*b))) {
      ++a;
      ++b;
    }
    if (!*b) return true;
  }
  return false;
}

void ResetInventorySnapshot(InventorySnapshot* snapshot) {
  if (!snapshot) return;
  // Construct directly into the caller-owned Panel scratch buffer. Assignment
  // from `InventorySnapshot{}` can materialize a very large temporary on the
  // game's Present-thread stack once the augmentation model is populated.
  new (snapshot) InventorySnapshot();
}

struct CachedAugmentationNode {
  int node_index = -1;
  uint32_t texture_id = 0;
  int praxis_cost = -1;
  int tier = 0;
  int order_in_tier = 0;
  int prerequisite_count = 0;
  int prerequisite_indices[kMaximumAugmentationPrerequisites] = {};
  char name[96] = {};
  char description[384] = {};
  char help[384] = {};
};

struct CachedAugmentationTree {
  int tree_id = -1;
  int augmentation_type = -1;
  void* tree = nullptr;
  uint32_t texture_id = 0;
  int base_praxis_cost = -1;
  int record_count = 0;
  char name[96] = {};
  char description[384] = {};
  CachedAugmentationNode nodes[kMaximumAugmentationNodesPerTree] = {};
};

CachedAugmentationTree g_augmentation_catalog[kMaximumAugmentationTrees];
CachedAugmentationTree g_augmentation_catalog_build[kMaximumAugmentationTrees];
int g_augmentation_catalog_count = 0;
bool g_augmentation_catalog_ready = false;
std::atomic<bool> g_reported_augmentation_catalog_failure{false};
std::atomic<uint32_t> g_reported_missing_core_types{0};

struct CachedAugmentationPresentation {
  uintptr_t actor_handle = 0;
  uint64_t last_attempt_ms = 0;
  bool state_valid = false;
  bool unlocked[kCoreAugmentationCount] = {};
  bool active_valid = false;
  bool active[kCoreAugmentationCount] = {};
  bool catalog_valid = false;
  int count = 0;
  int praxis_points = -1;
  AugmentationTreeSnapshot trees[kMaximumAugmentationTrees] = {};
};

CachedAugmentationPresentation g_augmentation_snapshot_cache;

void ResetAugmentationCatalogBuildBuffer() {
  for (CachedAugmentationTree& tree : g_augmentation_catalog_build) {
    new (&tree) CachedAugmentationTree();
  }
}

struct AugmentationCatalogBuildGuard {
  bool committed = false;
  ~AugmentationCatalogBuildGuard() {
    if (!committed) ResetAugmentationCatalogBuildBuffer();
  }
};

// GlobalPlayerInfo does not store the family art in tree-ID order.  These are
// the exact type-5 RenderResource targets referenced by DTP 87229..87249's DRM
// resolver records.  In particular, energy (87230/87237) and hacking
// (87238/87239) would receive the wrong vanilla icons if this were treated as
// a contiguous 9427-based range.
constexpr uint32_t kAugmentationFamilyTextureIds[] = {
    9427u, 9435u, 9429u, 9430u, 9431u, 9432u, 9433u,
    9434u, 9428u, 9437u, 9436u, 9438u, 9439u, 9440u,
    9441u, 9442u, 9443u, 9444u, 9445u, 9446u, 9447u,
};
static_assert(sizeof(kAugmentationFamilyTextureIds) /
                  sizeof(kAugmentationFamilyTextureIds[0]) == 21u,
              "DXHR augmentation family catalog changed");
constexpr uint32_t kKnownFirstAugmentationTreeDtp = 87229u;
constexpr uint32_t kKnownAugmentationTreeCount =
    sizeof(kAugmentationFamilyTextureIds) /
    sizeof(kAugmentationFamilyTextureIds[0]);

void ReportAugmentationCatalogFailure(const char* stage, uint32_t value = 0,
                                      uint32_t detail = 0) {
  bool expected = false;
  if (g_reported_augmentation_catalog_failure.compare_exchange_strong(
          expected, true)) {
    DXS_WARN("[model] native augmentation catalog unavailable at %s "
             "(value=%u detail=%u)",
             stage ? stage : "unknown", value, detail);
  }
}

uint32_t AugmentationFamilyTexture(uint32_t tree_id) {
  // The icon is part of the family's identity. Never substitute an icon by
  // list position for an unknown data build: a plausible but wrong native
  // icon is more dangerous than failing this guarded catalog closed.
  if (tree_id >= kKnownFirstAugmentationTreeDtp &&
      tree_id < kKnownFirstAugmentationTreeDtp +
                    kKnownAugmentationTreeCount) {
    return kAugmentationFamilyTextureIds[
        tree_id - kKnownFirstAugmentationTreeDtp];
  }
  return 0u;
}

enum class AugmentationRecordResolution : uint8_t {
  kInvalid,
  kMetadata,
  kNode,
};

// dtp::AugmentationTree owns a counted array of 0x5c-byte records at +0x04.
// Every purchasable record has a non-zero marker at +0x08 and a DTP id for
// the actual augmentation node at +0x10. The first record is family/root
// metadata and deliberately has a zero marker; native upgrade indices begin
// at one. Read that layout directly: the tiny
// retail accessors at 0x65bc20/0x65b6a0 are useful signatures for validating
// the layout, but calling them through Wine's x86 thunk returned zero for
// every record on the Thor even though the relocated records were present.
AugmentationRecordResolution ResolveAugmentationRecord(
    const void* tree, uint32_t index, void** resolved_node) {
  if (resolved_node) *resolved_node = nullptr;
  if (!tree || !resolved_node || !Readable(tree, 8)) {
    return AugmentationRecordResolution::kInvalid;
  }

  uint32_t count = 0;
  uint8_t* records = nullptr;
  if (!Read(tree, &count) || count == 0 || count > 64 || index >= count ||
      !Read(static_cast<const uint8_t*>(tree) + sizeof(uint32_t), &records) ||
      !records || !Readable(records, static_cast<size_t>(count) * 0x5cu)) {
    return AugmentationRecordResolution::kInvalid;
  }

  const uint8_t* record = records + static_cast<size_t>(index) * 0x5cu;
  uint32_t marker = 0;
  if (!Read(record + 0x08, &marker)) {
    return AugmentationRecordResolution::kInvalid;
  }
  if (marker == 0) return AugmentationRecordResolution::kMetadata;

  uint32_t node_dtp_id = 0;
  if (!Read(record + 0x10, &node_dtp_id) || node_dtp_id == 0 ||
      node_dtp_id >= kMaxPickupId) {
    return AugmentationRecordResolution::kInvalid;
  }
  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  void* node = reinterpret_cast<ResolveFn>(Rebase(kResolvePickupVA))(
      node_dtp_id);
  if (!node || !Readable(node, 0x18)) {
    return AugmentationRecordResolution::kInvalid;
  }
  *resolved_node = node;
  return AugmentationRecordResolution::kNode;
}

bool AugmentationCatalogCodeReady() {
  static const uint8_t resolve_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x80, 0x01,
      0x00, 0x73, 0x15, 0x8B, 0x04, 0x85, 0xE8, 0x06,
      0x98, 0x01};
  static const uint8_t tree_name_signature[] = {
      0x56, 0x8B, 0x74, 0x24, 0x08, 0x33, 0xC0, 0x85,
      0xF6, 0x74, 0x21};
  static const uint8_t count_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x8B, 0x00, 0xC3};
  static const uint8_t tree_type_signature[] = {
      0x57, 0x8B, 0x7C, 0x24, 0x08, 0x33, 0xC0, 0x33,
      0xC9, 0x85, 0xFF, 0x74, 0x28};
  static const uint8_t node_signature[] = {
      0x8B, 0x44, 0x24, 0x08, 0x8B, 0x4C, 0x24, 0x04,
      0x6B, 0xC0, 0x5C};
  static const uint8_t node_data_signature[] = {
      0x8B, 0x44, 0x24, 0x08, 0x8B, 0x4C, 0x24, 0x04,
      0x6B, 0xC0, 0x5C, 0x8B, 0x51, 0x04};
  static const uint8_t node_name_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x0F, 0xBF, 0x48, 0x08};
  static const uint8_t node_description_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x0F, 0xBF, 0x48, 0x0A};
  static const uint8_t node_help_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x0F, 0xBF, 0x48, 0x0C};
  static const uint8_t node_texture_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x14, 0xC3};
  static const uint8_t node_cost_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x8B, 0x40, 0x04, 0xC3};
  static const uint8_t tree_installed_signature[] = {
      0x8B, 0x41, 0x20, 0x8B, 0x51, 0x28, 0x8D, 0x14,
      0x82, 0x8B, 0x41, 0x28};
  static const uint8_t node_installed_signature[] = {
      0x8B, 0x44, 0x24, 0x08, 0x8B, 0x54, 0x24, 0x04,
      0x6B, 0xC0, 0x5C};
  static const uint8_t prerequisites_signature[] = {
      0x8B, 0x54, 0x24, 0x04, 0x53, 0x55, 0x8B, 0x6A, 0x08};
  static const uint8_t available_signature[] = {
      0x53, 0x55, 0x56, 0x8B, 0x74, 0x24, 0x10, 0x57};
  static const uint8_t base_cost_signature[] = {
      0x56, 0x8B, 0x74, 0x24, 0x08, 0x33, 0xC0, 0x85, 0xF6};
  return HasCode(kResolvePickupVA, resolve_signature) &&
      HasCode(kAugmentationTreeNameVA, tree_name_signature) &&
      HasCode(kAugmentationTreeNodeCountVA, count_signature) &&
      HasCode(kAugmentationTreeTypeVA, tree_type_signature) &&
      HasCode(kAugmentationTreeNodeVA, node_signature) &&
      HasCode(kAugmentationNodeDataVA, node_data_signature) &&
      HasCode(kAugmentationNodeNameVA, node_name_signature) &&
      HasCode(kAugmentationNodeDescriptionVA, node_description_signature) &&
      HasCode(kAugmentationNodeHelpVA, node_help_signature) &&
      HasCode(kAugmentationNodeTextureVA, node_texture_signature) &&
      HasCode(kAugmentationNodeCostVA, node_cost_signature) &&
      HasCode(kAugmentationTreeInstalledVA, tree_installed_signature) &&
      HasCode(kAugmentationNodeInstalledVA, node_installed_signature) &&
      HasCode(kAugmentationPrerequisitesVA, prerequisites_signature) &&
      HasCode(kAugmentationAvailableVA, available_signature) &&
      HasCode(kAugmentationBaseCostVA, base_cost_signature);
}

bool BuildAugmentationCatalog() {
  if (g_augmentation_catalog_ready) return true;
  AugmentationCatalogBuildGuard build_guard;
  if (!AugmentationCatalogCodeReady()) {
    ReportSignatureFailure("augmentation catalog");
    return false;
  }

  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  using TreeNameFn = const char*(DXS_GAME_CDECL*)(const void*);
  using NodeCountFn = uint32_t(DXS_GAME_CDECL*)(const void*);
  using TreeTypeFn = int(DXS_GAME_CDECL*)(const void*);
  using NodeStringFn = const char*(DXS_GAME_CDECL*)(const void*);
  using NodeIntFn = int(DXS_GAME_CDECL*)(const void*);
  using TreeIntFn = int(DXS_GAME_CDECL*)(const void*);
  auto resolve = reinterpret_cast<ResolveFn>(Rebase(kResolvePickupVA));
  auto tree_name = reinterpret_cast<TreeNameFn>(
      Rebase(kAugmentationTreeNameVA));
  auto node_count = reinterpret_cast<NodeCountFn>(
      Rebase(kAugmentationTreeNodeCountVA));
  auto tree_type = reinterpret_cast<TreeTypeFn>(
      Rebase(kAugmentationTreeTypeVA));
  auto node_name = reinterpret_cast<NodeStringFn>(
      Rebase(kAugmentationNodeNameVA));
  auto node_description = reinterpret_cast<NodeStringFn>(
      Rebase(kAugmentationNodeDescriptionVA));
  auto node_help = reinterpret_cast<NodeStringFn>(
      Rebase(kAugmentationNodeHelpVA));
  auto node_texture = reinterpret_cast<NodeIntFn>(
      Rebase(kAugmentationNodeTextureVA));
  auto node_cost = reinterpret_cast<NodeIntFn>(
      Rebase(kAugmentationNodeCostVA));
  auto base_cost = reinterpret_cast<TreeIntFn>(
      Rebase(kAugmentationBaseCostVA));

  // 0x0065C750 is the exact helper called by the retail SmartGlass serializer
  // at 0x006B448D. It copies {count, ids} from the first two fields of
  // **g_GlobalPlayerInfo. Read that immutable array directly rather than
  // constructing a game-owned vector with its allocator.
  void* player_info_holder = nullptr;
  void* player_info = nullptr;
  uint32_t tree_count = 0;
  uint32_t* tree_ids = nullptr;
  if (!Read(reinterpret_cast<void*>(Rebase(kGlobalPlayerInfoVA)),
            &player_info_holder) || !player_info_holder ||
      !Readable(player_info_holder, sizeof(void*)) ||
      !Read(player_info_holder, &player_info) || !player_info ||
      !Readable(player_info, 8) ||
      !Read(player_info, &tree_count) || tree_count == 0 ||
      tree_count > kMaximumAugmentationTrees ||
      !Read(static_cast<uint8_t*>(player_info) + 4, &tree_ids) || !tree_ids ||
      !Readable(tree_ids, static_cast<size_t>(tree_count) * sizeof(uint32_t))) {
    ReportAugmentationCatalogFailure("GlobalPlayerInfo list", tree_count);
    return false;
  }

  ResetAugmentationCatalogBuildBuffer();
  for (uint32_t index = 0; index < tree_count; ++index) {
    uint32_t tree_id = 0;
    if (!Read(tree_ids + index, &tree_id) || tree_id >= kMaxPickupId) {
      ReportAugmentationCatalogFailure("tree id", index, tree_id);
      return false;
    }
    void* tree = resolve(tree_id);
    if (!tree || !Readable(tree, 2 * sizeof(void*))) {
      ReportAugmentationCatalogFailure("tree resolve", index, tree_id);
      return false;
    }
    const uint32_t records = node_count(tree);
    void* record_data = nullptr;
    if (records == 0 || records > 64 ||
        !Read(static_cast<uint8_t*>(tree) + sizeof(uint32_t), &record_data) ||
        !record_data || !Readable(record_data,
                                  static_cast<size_t>(records) * 0x5Cu)) {
      ReportAugmentationCatalogFailure("tree records", tree_id, records);
      return false;
    }

    CachedAugmentationTree& entry = g_augmentation_catalog_build[index];
    entry.tree_id = static_cast<int>(tree_id);
    entry.augmentation_type = tree_type(tree);
    entry.tree = tree;
    entry.texture_id = AugmentationFamilyTexture(tree_id);
    if (!entry.texture_id) {
      ReportAugmentationCatalogFailure("family texture", tree_id, index);
      return false;
    }
    entry.base_praxis_cost = base_cost(tree);
    if (entry.base_praxis_cost < 0 || entry.base_praxis_cost > 100) {
      ReportAugmentationCatalogFailure("base cost", tree_id,
                                       entry.base_praxis_cost);
      return false;
    }
    CopyGameString(tree_name(tree), entry.name);
    if (entry.name[0] == '\0') {
      ReportAugmentationCatalogFailure("tree name", index, tree_id);
      return false;
    }

    // Native index zero is the family's metadata/root record. It anchors
    // top-level dependency edges but is not itself a purchasable node. Keep
    // every later record's original index so state calls and purchases use the
    // same identifiers as the retail augmentation menu.
    for (uint32_t node_index = 0; node_index < records; ++node_index) {
      void* node = nullptr;
      const AugmentationRecordResolution resolution =
          ResolveAugmentationRecord(tree, node_index, &node);
      if (resolution == AugmentationRecordResolution::kMetadata) continue;
      if (resolution != AugmentationRecordResolution::kNode) {
        ReportAugmentationCatalogFailure("node record", tree_id,
                                         node_index);
        return false;
      }
      if (entry.record_count >= kMaximumAugmentationNodesPerTree) {
        ReportAugmentationCatalogFailure("node capacity", tree_id,
                                         entry.record_count + 1);
        return false;
      }
      CachedAugmentationNode& cached = entry.nodes[entry.record_count];
      cached.node_index = static_cast<int>(node_index);
      cached.texture_id = static_cast<uint32_t>(node_texture(node));
      cached.praxis_cost = node_cost(node);
      if (!cached.texture_id || cached.texture_id == 0xffffffffu ||
          cached.praxis_cost < 0 || cached.praxis_cost > 100) {
        ReportAugmentationCatalogFailure("node presentation", tree_id,
                                         node_index);
        return false;
      }
      CopyGameString(node_name(node), cached.name);
      CopyGameString(node_description(node), cached.description);
      CopyGameString(node_help(node), cached.help);
      if (cached.name[0] == '\0') {
        ReportAugmentationCatalogFailure("node name", tree_id, node_index);
        return false;
      }
      // A family has no separate body text. Vanilla uses the first upgrade's
      // help/description beneath the family heading, so retain that exact text
      // for the base-unlock selection as well.
      if (entry.description[0] == '\0') {
        const char* detail = cached.help[0] ? cached.help : cached.description;
        CopyGameString(detail, entry.description);
      }
      ++entry.record_count;
    }

    // dtp::AugmentationTree+0x08/+0x0c is the authoritative relation table.
    // Each pair is {prerequisite node, dependent node}; 0x0065C320 walks this
    // exact table when the native menu decides whether an upgrade is allowed.
    // Preserve those links verbatim instead of inventing a linear skill list.
    uint32_t relation_count = 0;
    uint32_t* relations = nullptr;
    if (!Read(static_cast<uint8_t*>(tree) + 0x08, &relation_count) ||
        relation_count > 256 ||
        !Read(static_cast<uint8_t*>(tree) + 0x0c, &relations) ||
        (relation_count &&
         (!relations || !Readable(relations,
             static_cast<size_t>(relation_count) * 2u * sizeof(uint32_t))))) {
      ReportAugmentationCatalogFailure("relation table", tree_id,
                                       relation_count);
      return false;
    }
    for (uint32_t relation = 0; relation < relation_count; ++relation) {
      uint32_t prerequisite = 0;
      uint32_t dependent = 0;
      if (!Read(relations + relation * 2u, &prerequisite) ||
          !Read(relations + relation * 2u + 1u, &dependent) ||
          dependent >= records || prerequisite >= records) {
        ReportAugmentationCatalogFailure("relation bounds", tree_id,
                                         relation);
        return false;
      }
      int child_index = -1;
      int parent_index = -1;
      for (int cached_index = 0; cached_index < entry.record_count;
           ++cached_index) {
        if (entry.nodes[cached_index].node_index ==
            static_cast<int>(dependent)) {
          child_index = cached_index;
        }
        if (entry.nodes[cached_index].node_index ==
            static_cast<int>(prerequisite)) {
          parent_index = cached_index;
        }
      }
      // A dependent metadata record is not a UI node. A missing parent is the
      // native root record (index zero), which makes the child a top-level
      // upgrade without adding a fake prerequisite.
      if (child_index < 0 || parent_index < 0) continue;
      CachedAugmentationNode& child = entry.nodes[child_index];
      bool duplicate = false;
      for (int parent = 0; parent < child.prerequisite_count; ++parent) {
        duplicate = duplicate ||
            child.prerequisite_indices[parent] ==
                static_cast<int>(prerequisite);
      }
      if (duplicate) continue;
      if (child.prerequisite_count >= kMaximumAugmentationPrerequisites) {
        ReportAugmentationCatalogFailure("prerequisite capacity", tree_id,
                                         dependent);
        return false;
      }
      child.prerequisite_indices[child.prerequisite_count++] =
          static_cast<int>(prerequisite);
    }

    // Derive visual tiers only from the native relation DAG. A final-pass
    // change means corrupt/cyclic data, in which case failing closed is safer
    // than presenting a purchase hierarchy different from the game's.
    bool changed = false;
    for (int pass = 0; pass < entry.record_count; ++pass) {
      changed = false;
      for (int node_index = 0; node_index < entry.record_count; ++node_index) {
        CachedAugmentationNode& node = entry.nodes[node_index];
        int tier = 0;
        for (int parent = 0; parent < node.prerequisite_count; ++parent) {
          int parent_index = -1;
          for (int candidate = 0; candidate < entry.record_count;
               ++candidate) {
            if (entry.nodes[candidate].node_index ==
                node.prerequisite_indices[parent]) {
              parent_index = candidate;
              break;
            }
          }
          if (parent_index < 0) {
            ReportAugmentationCatalogFailure("prerequisite index", tree_id,
                                             node.prerequisite_indices[parent]);
            return false;
          }
          tier = (std::max)(tier, entry.nodes[parent_index].tier + 1);
        }
        if (tier != node.tier) {
          node.tier = tier;
          changed = true;
        }
      }
      if (!changed) break;
    }
    if (changed) {
      ReportAugmentationCatalogFailure("cyclic relation graph", tree_id);
      return false;
    }
    int tier_counts[kMaximumAugmentationNodesPerTree] = {};
    for (int node_index = 0; node_index < entry.record_count; ++node_index) {
      CachedAugmentationNode& node = entry.nodes[node_index];
      if (node.tier < 0 || node.tier >= kMaximumAugmentationNodesPerTree) {
        ReportAugmentationCatalogFailure("tier bounds", tree_id, node.tier);
        return false;
      }
      node.order_in_tier = tier_counts[node.tier]++;
    }
    DXS_INFO("[model] augmentation family %u: dtp=%u type=%d nodes=%d %s",
             index, tree_id, entry.augmentation_type, entry.record_count,
             entry.name);
  }

  std::memcpy(g_augmentation_catalog, g_augmentation_catalog_build,
              tree_count * sizeof(CachedAugmentationTree));
  ResetAugmentationCatalogBuildBuffer();
  g_augmentation_catalog_count = static_cast<int>(tree_count);
  g_augmentation_catalog_ready = true;
  build_guard.committed = true;
  DXS_INFO("[model] native augmentation catalog ready (%u trees)",
           tree_count);
  return true;
}

void CaptureAugmentationCatalog(InventorySnapshot* out) {
  if (!out || !BuildAugmentationCatalog()) return;
  const uintptr_t handle = CurrentPlayerActorHandle();
  if (!handle || !Readable(reinterpret_cast<void*>(handle), 0x5C)) return;
  void* actor_state = nullptr;
  void* player = nullptr;
  if (!Read(reinterpret_cast<uint8_t*>(handle) + 0x58, &actor_state) ||
      !actor_state || !Readable(actor_state, 0x18) ||
      !Read(static_cast<uint8_t*>(actor_state) + 0x14, &player) || !player ||
      !Readable(player, 0x284)) {
    return;
  }
  void* augmentation_system = static_cast<uint8_t*>(player) + 0x130;
  uint32_t installed_tree_count = 0;
  uint32_t installed_node_count = 0;
  uint32_t* installed_trees = nullptr;
  uint32_t* installed_nodes = nullptr;
  if (!Readable(augmentation_system, 0x38) ||
      !Read(static_cast<uint8_t*>(augmentation_system) + 0x20,
            &installed_tree_count) || installed_tree_count > 128 ||
      !Read(static_cast<uint8_t*>(augmentation_system) + 0x28,
            &installed_trees) ||
      !Read(static_cast<uint8_t*>(augmentation_system) + 0x2C,
            &installed_node_count) || installed_node_count > 1024 ||
      !Read(static_cast<uint8_t*>(augmentation_system) + 0x34,
            &installed_nodes) ||
      (installed_tree_count &&
       (!installed_trees || !Readable(installed_trees,
           static_cast<size_t>(installed_tree_count) * sizeof(uint32_t)))) ||
      (installed_node_count &&
       (!installed_nodes || !Readable(installed_nodes,
           static_cast<size_t>(installed_node_count) * sizeof(uint32_t))))) {
    return;
  }

  using TreeInstalledFn = bool(DXS_GAME_THISCALL*)(void*, int);
  using NodeInstalledFn = bool(DXS_GAME_THISCALL*)(void*, const void*, int);
  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  using NodeAvailableFn = bool(DXS_GAME_THISCALL*)(void*, const void*, int);
  auto resolve = reinterpret_cast<ResolveFn>(Rebase(kResolvePickupVA));
  auto tree_installed = reinterpret_cast<TreeInstalledFn>(
      Rebase(kAugmentationTreeInstalledVA));
  auto node_installed = reinterpret_cast<NodeInstalledFn>(
      Rebase(kAugmentationNodeInstalledVA));
  auto prerequisites = reinterpret_cast<NodeAvailableFn>(
      Rebase(kAugmentationPrerequisitesVA));
  auto available = reinterpret_cast<NodeAvailableFn>(
      Rebase(kAugmentationAvailableVA));

  out->augmentation_count = g_augmentation_catalog_count;
  for (int index = 0; index < g_augmentation_catalog_count; ++index) {
    const CachedAugmentationTree& source = g_augmentation_catalog[index];
    // DTP resources normally live for the process, but save/level streaming
    // can replace the resolved object. Resolve by stable DTP id for every
    // sweep so no state method ever receives the catalog's cached pointer.
    void* live_tree = resolve(static_cast<uint32_t>(source.tree_id));
    if (!live_tree || !Readable(live_tree, 0x10)) {
      ReportAugmentationCatalogFailure("live tree resolve", source.tree_id,
                                       index);
      out->augmentation_count = 0;
      return;
    }
    AugmentationTreeSnapshot& destination = out->augmentations[index];
    destination.tree_id = source.tree_id;
    destination.augmentation_type = source.augmentation_type;
    destination.texture_id = source.texture_id;
    destination.base_praxis_cost = source.base_praxis_cost;
    destination.tree_present = tree_installed(augmentation_system,
                                              source.tree_id);
    std::memcpy(destination.name, source.name, sizeof(destination.name));
    std::memcpy(destination.description, source.description,
                sizeof(destination.description));
    destination.next_praxis_cost = -1;
    destination.next_node_index = -2;
    destination.node_count = source.record_count;
    for (int node_index = 0; node_index < source.record_count; ++node_index) {
      const CachedAugmentationNode& cached = source.nodes[node_index];
      AugmentationNodeSnapshot& node = destination.nodes[node_index];
      node.node_index = cached.node_index;
      node.texture_id = cached.texture_id;
      node.praxis_cost = cached.praxis_cost;
      node.tier = cached.tier;
      node.order_in_tier = cached.order_in_tier;
      node.prerequisite_count = cached.prerequisite_count;
      std::memcpy(node.prerequisite_indices,
                  cached.prerequisite_indices,
                  sizeof(node.prerequisite_indices));
      node.name = cached.name;
      node.description = cached.description;
      node.help = cached.help;
      void* live_node = nullptr;
      if (ResolveAugmentationRecord(
              live_tree, static_cast<uint32_t>(cached.node_index),
              &live_node) != AugmentationRecordResolution::kNode) {
        node.state = AugmentationNodeState::kUnavailable;
        continue;
      }
      if (node_installed(augmentation_system, live_tree,
                         cached.node_index)) {
        node.state = AugmentationNodeState::kPurchased;
        ++destination.installed_nodes;
        continue;
      }
      node.prerequisites_met = prerequisites(
          augmentation_system, live_tree, cached.node_index);
      node.graph_available = available(
          augmentation_system, live_tree, cached.node_index);
      node.affordable = out->praxis_points >= 0 &&
          out->praxis_points >= cached.praxis_cost;
      if (!destination.tree_present) {
        node.state = AugmentationNodeState::kLocked;
      } else if (!node.graph_available) {
        node.state = AugmentationNodeState::kUnavailable;
      } else if (!node.prerequisites_met) {
        node.state = AugmentationNodeState::kPrerequisiteBlocked;
      } else {
        node.state = AugmentationNodeState::kAvailable;
        if (destination.next_praxis_cost < 0) {
          destination.next_praxis_cost = cached.praxis_cost;
          destination.next_node_index = cached.node_index;
        }
      }
    }
    // This exactly matches retail SmartGlass `isInstalled` at 0x006B452B.
    // Family ownership is the tree id in Experience+0x20; upgrade nodes live
    // in a separate list, so an owned base ability can have zero upgrades.
    destination.installed = destination.tree_present;
    if (!destination.tree_present) {
      const int cost = source.base_praxis_cost;
      if (cost >= 0 && cost <= 100) {
        destination.next_praxis_cost = cost;
        destination.next_node_index = -1;
      }
    }
    destination.purchase_available = destination.next_node_index >= -1;
    destination.affordable = destination.purchase_available &&
        out->praxis_points >= 0 &&
        out->praxis_points >= destination.next_praxis_cost;
    if (!destination.tree_present) {
      destination.state = destination.base_praxis_cost >= 0
          ? AugmentationTreeState::kAvailable
          : AugmentationTreeState::kLocked;
    } else if (destination.node_count > 0 &&
               destination.installed_nodes >= destination.node_count) {
      destination.state = AugmentationTreeState::kComplete;
    } else {
      destination.state = AugmentationTreeState::kPurchased;
    }
  }
  out->augmentation_catalog_valid = out->augmentation_count > 0;
  if (out->augmentation_catalog_valid) {
    // The property getters report active state, not ownership. The catalog's
    // type is only the body region (cranium/eye/skin/etc.), so matching it to
    // the controller action id made every core ability appear absent. These
    // DTP family ids are the stable authority in GlobalPlayerInfo.
    constexpr int kRetailCoreTreeIds[] = {87231, 87229, 87248, 87235};
    for (int slot = 0; slot < 4; ++slot) {
      bool found = false;
      for (int tree_index = 0; tree_index < out->augmentation_count;
           ++tree_index) {
        const AugmentationTreeSnapshot& tree =
            out->augmentations[tree_index];
        if (tree.tree_id != kRetailCoreTreeIds[slot]) continue;
        if (slot != 3) {
          out->augmentation_unlocked[slot] = tree.tree_present;
        } else {
          // Run Silently is an upgrade inside the leg family rather than the
          // family's base unlock. Resolve it by its native localized node
          // metadata, and accept an active retail property as definitive
          // ownership if the text table is temporarily still streaming.
          bool silent_node_owned = false;
          for (int node_index = 0; node_index < tree.node_count; ++node_index) {
            const AugmentationNodeSnapshot& node = tree.nodes[node_index];
            if (node.state != AugmentationNodeState::kPurchased) continue;
            if (ContainsAsciiInsensitive(node.name, "silent") ||
                ContainsAsciiInsensitive(node.description, "silent") ||
                ContainsAsciiInsensitive(node.help, "silent")) {
              silent_node_owned = true;
              break;
            }
          }
          out->augmentation_unlocked[slot] = silent_node_owned ||
              (out->augmentation_active_valid &&
               out->augmentation_active[slot]);
        }
        found = true;
        break;
      }
      if (!found) {
        const uint32_t bit = 1u << slot;
        if ((g_reported_missing_core_types.fetch_or(bit) & bit) == 0) {
          DXS_WARN("[model] core augmentation tree %d missing from catalog",
                   kRetailCoreTreeIds[slot]);
        }
      }
    }
    out->augmentation_state_valid = true;
  }
}

void ResetAugmentationSnapshotCache() {
  // Avoid a large aggregate temporary on the game/Present thread during a
  // purchase or save transition. This cache owns only trivial values and
  // pointers into the immutable process-lifetime catalog.
  new (&g_augmentation_snapshot_cache) CachedAugmentationPresentation();
}

void ApplyCachedAugmentations(InventorySnapshot* out) {
  if (!out) return;
  const CachedAugmentationPresentation& cached =
      g_augmentation_snapshot_cache;
  out->augmentation_state_valid = cached.state_valid;
  std::memcpy(out->augmentation_unlocked, cached.unlocked,
              sizeof(out->augmentation_unlocked));
  out->augmentation_active_valid = cached.active_valid;
  std::memcpy(out->augmentation_active, cached.active,
              sizeof(out->augmentation_active));
  out->augmentation_catalog_valid = cached.catalog_valid;
  out->augmentation_count = cached.catalog_valid ? cached.count : 0;
  if (out->augmentation_count > 0) {
    std::memcpy(out->augmentations, cached.trees,
                static_cast<size_t>(out->augmentation_count) *
                    sizeof(AugmentationTreeSnapshot));
  }
}

void CaptureAugmentations(InventorySnapshot* out) {
  if (!out) return;
  const uintptr_t actor_handle = CurrentPlayerActorHandle();
  if (!actor_handle) {
    ResetAugmentationSnapshotCache();
    return;
  }
  if (g_augmentation_snapshot_cache.actor_handle != actor_handle) {
    ResetAugmentationSnapshotCache();
    g_augmentation_snapshot_cache.actor_handle = actor_handle;
  }

  const uint64_t now = GetTickCount64();
  // Praxis purchases invalidate this cache synchronously below. Also refresh
  // within half a second so story grants, debug changes, and purchases made in
  // the original menu become visible without reopening the lower-screen page.
  // A changed Praxis total bypasses the timer entirely.
  if (g_augmentation_snapshot_cache.last_attempt_ms != 0 &&
      g_augmentation_snapshot_cache.praxis_points == out->praxis_points &&
      now - g_augmentation_snapshot_cache.last_attempt_ms < 500) {
    ApplyCachedAugmentations(out);
    return;
  }
  g_augmentation_snapshot_cache.last_attempt_ms = now;

  CaptureActiveAugmentations(out);
  CaptureAugmentationCatalog(out);
  g_augmentation_snapshot_cache.state_valid =
      out->augmentation_state_valid;
  if (out->augmentation_state_valid) {
    std::memcpy(g_augmentation_snapshot_cache.unlocked,
                out->augmentation_unlocked,
                sizeof(g_augmentation_snapshot_cache.unlocked));
  }
  g_augmentation_snapshot_cache.active_valid =
      out->augmentation_active_valid;
  if (out->augmentation_active_valid) {
    std::memcpy(g_augmentation_snapshot_cache.active,
                out->augmentation_active,
                sizeof(g_augmentation_snapshot_cache.active));
  }
  g_augmentation_snapshot_cache.catalog_valid =
      out->augmentation_catalog_valid && out->augmentation_count > 0 &&
      out->augmentation_count <= kMaximumAugmentationTrees;
  g_augmentation_snapshot_cache.count =
      g_augmentation_snapshot_cache.catalog_valid
          ? out->augmentation_count : 0;
  if (g_augmentation_snapshot_cache.catalog_valid) {
    std::memcpy(g_augmentation_snapshot_cache.trees, out->augmentations,
                static_cast<size_t>(out->augmentation_count) *
                    sizeof(AugmentationTreeSnapshot));
  }
  g_augmentation_snapshot_cache.praxis_points = out->praxis_points;
  ApplyCachedAugmentations(out);
}

void DXS_GAME_FASTCALL FullEnergyRechargeHook(void* energy_system,
                                               void* /*edx*/, float dt) {
  EnergyRechargeFn original = g_original_energy_recharge;
  if (!original) return;

  float before = -1.0f;
  if (energy_system && Readable(energy_system, 0x0C)) {
    Read(static_cast<uint8_t*>(energy_system) + 0x04, &before);
  }
  original(energy_system, dt);

  // Retail has already handled the normal recharge delay, modifiers, and the
  // first-cell refill. Only intervene when it made no progress at a cell
  // boundary, which is the intentional vanilla limit the user wants lifted.
  if (!energy_system || !std::isfinite(dt) || dt <= 0.0f || dt > 1.0f ||
      !std::isfinite(before) || before < 0.0f ||
      !Readable(energy_system, 0x0C)) {
    return;
  }
  float current = -1.0f;
  float timer = -1.0f;
  if (!Read(static_cast<uint8_t*>(energy_system) + 0x04, &current) ||
      !Read(static_cast<uint8_t*>(energy_system) + 0x08, &timer) ||
      !std::isfinite(current) || !std::isfinite(timer) || current < 0.0f ||
      timer < 0.0f || current > before + 0.0001f) {
    return;
  }

  using FloatFn = float(DXS_GAME_THISCALL*)(void*);
  using AddFn = void(DXS_GAME_THISCALL*)(void*, float);
  const float delay = reinterpret_cast<FloatFn>(
      Rebase(kEnergyRechargeDelayVA))(energy_system);
  const float maximum = reinterpret_cast<FloatFn>(
      Rebase(kEnergyMaximumVA))(energy_system);
  const float rate = reinterpret_cast<FloatFn>(
      Rebase(kEnergyRechargeRateVA))(energy_system);
  if (!std::isfinite(delay) || !std::isfinite(maximum) ||
      !std::isfinite(rate) || delay < 0.0f || maximum <= 0.0f ||
      maximum > 10000000.0f || rate <= 0.0f || rate > 1000000.0f ||
      current >= maximum - 0.0001f || timer < delay) {
    return;
  }

  // Match EnergySystem::Recharge's partial first frame after the delay. On
  // later frames this is simply dt * the native recharge rate. AddEnergy is
  // the retail clamped mutation path, so it can never overfill the reserve.
  const float recharge_time = (std::min)(dt, (std::max)(0.0f, timer - delay));
  const float amount = recharge_time * rate;
  if (std::isfinite(amount) && amount > 0.00001f) {
    reinterpret_cast<AddFn>(Rebase(kEnergyAddVA))(energy_system, amount);
  }
}

void SendResult(const GameActionResult& result) {
  GameActionResultHandler callback = nullptr;
  void* context = nullptr;
  AcquireSRWLockShared(&g_callback_lock);
  callback = g_result_handler;
  context = g_result_context;
  ReleaseSRWLockShared(&g_callback_lock);
  if (callback) callback(result, context);
}

bool DispatchTouch(const GuestTouch& touch) {
  GuestTouchDispatcher callback = nullptr;
  void* context = nullptr;
  AcquireSRWLockShared(&g_callback_lock);
  callback = g_touch_dispatcher;
  context = g_touch_context;
  ReleaseSRWLockShared(&g_callback_lock);
  if (callback) return callback(touch, context);

  // Fail-safe header hit test while the custom renderer is being attached.
  // Content is never translated into gameplay mouse input.
  if (touch.gesture == GuestGesture::kTap &&
      touch.y >= 0.0f && touch.y <= 0.15f &&
      touch.x >= 0.0f && touch.x <= 1.0f) {
    constexpr int kVisiblePages[] = {3, 4, 1, 2, 5};
    const int slot = (std::min)(4, static_cast<int>(touch.x * 5.0f));
    return SelectCustomPage(kVisiblePages[slot]);
  }
  return false;
}

void ProcessAction(const QueuedAction& action) {
  GameActionResult result;
  result.kind = action.kind;
  result.token = action.token;

  switch (action.kind) {
    case GameActionKind::kTouch:
      result.success = DispatchTouch(action.touch);
      break;
    case GameActionKind::kSelectPage:
      result.success = SelectCustomPage(action.a);
      break;
    case GameActionKind::kInventoryCanMove:
      result.success = CanMoveInventoryItem(action.a, action.b, action.c,
                                            action.d != 0, &result.value);
      break;
    case GameActionKind::kInventoryMove:
      result.success = MoveInventoryItem(action.a, action.b, action.c,
                                         action.d != 0);
      break;
    case GameActionKind::kInventoryCombine:
      result.success =
          TryCombineInventoryItems(action.item_a, action.item_b, &result.value);
      break;
    case GameActionKind::kSetQuickSlot:
      result.success = SetInventoryQuickSlot(action.a, action.item_a);
      break;
    case GameActionKind::kClearQuickSlot:
      result.success = ClearInventoryQuickSlot(action.a, action.item_a);
      break;
    case GameActionKind::kActivateQuickSlot:
      result.success = ActivateInventoryQuickSlot(action.a, action.item_a);
      break;
    case GameActionKind::kDiscardInventoryItem:
      result.success = DiscardInventoryItem(action.item_a);
      break;
    case GameActionKind::kUseInventoryItem:
      result.success = UseInventoryItem(action.item_a);
      break;
  }
  SendResult(result);
}

}  // namespace

bool InstallGameplayEnhancements(HookSet* hooks) {
  if (!hooks || g_original_energy_recharge) return false;
  static const uint8_t recharge_signature[] = {
      0x83, 0xEC, 0x10, 0xD9, 0x44, 0x24, 0x14};
  static const uint8_t delay_signature[] = {
      0x83, 0xEC, 0x08, 0x8B, 0x09, 0x6A, 0x00};
  static const uint8_t rate_signature[] = {
      0x83, 0xEC, 0x08, 0x8B, 0x09, 0x6A, 0x03};
  static const uint8_t maximum_signature[] = {
      0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x0E};
  static const uint8_t add_signature[] = {
      0x51, 0xD9, 0x44, 0x24, 0x08, 0x56, 0x8B, 0xF1};
  if (!HasCode(kEnergyRechargeVA, recharge_signature) ||
      !HasCode(kEnergyRechargeDelayVA, delay_signature) ||
      !HasCode(kEnergyRechargeRateVA, rate_signature) ||
      !HasCode(kEnergyMaximumVA, maximum_signature) ||
      !HasCode(kEnergyAddVA, add_signature)) {
    DXS_WARN("[gameplay] full-energy recharge signatures do not match; feature disabled");
    return false;
  }
  if (!hooks->AddInline(reinterpret_cast<void*>(Rebase(kEnergyRechargeVA)),
                        recharge_signature, sizeof(recharge_signature),
                        reinterpret_cast<void*>(&FullEnergyRechargeHook),
                        reinterpret_cast<void**>(&g_original_energy_recharge))) {
    return false;
  }
  DXS_INFO("[gameplay] native energy recharge extended across the full reserve");
  return true;
}

void SetGuestTouchDispatcher(GuestTouchDispatcher dispatcher, void* context) {
  AcquireSRWLockExclusive(&g_callback_lock);
  g_touch_dispatcher = dispatcher;
  g_touch_context = context;
  ReleaseSRWLockExclusive(&g_callback_lock);
}

void SetPageSelectionHandler(PageSelectionHandler handler, void* context) {
  AcquireSRWLockExclusive(&g_callback_lock);
  g_page_handler = handler;
  g_page_context = context;
  ReleaseSRWLockExclusive(&g_callback_lock);
}

void SetGameActionResultHandler(GameActionResultHandler handler, void* context) {
  AcquireSRWLockExclusive(&g_callback_lock);
  g_result_handler = handler;
  g_result_context = context;
  ReleaseSRWLockExclusive(&g_callback_lock);
}

bool QueueGuestTouch(GuestGesture gesture, float x, float y, float value,
                     uint32_t token) {
  if (!std::isfinite(value)) {
    return false;
  }
  if (gesture == GuestGesture::kCancel) {
    // Cancellation has no hit target. Some Android gesture recognizers send
    // sentinel coordinates after a second finger is removed, so normalize it
    // before queuing instead of leaving the prior Down attached forever.
    x = 0.0f;
    y = 0.0f;
  } else {
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    const bool outside = x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f;
    if (outside && gesture != GuestGesture::kMove &&
        gesture != GuestGesture::kUp) {
      return false;
    }
    // Move/Up outside the physical panel must reach Panel so it can retire a
    // captured drag. Panel handles these before converting to pixel
    // coordinates, so even a large finite sentinel cannot become an edge hit.
  }
  QueuedAction action;
  action.kind = GameActionKind::kTouch;
  action.token = token;
  action.touch = {gesture, x, y, value};
  return PushAction(action);
}

bool QueueSelectPage(int page, uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kSelectPage;
  action.token = token;
  action.a = page;
  return PushAction(action);
}

bool QueueInventoryCanMove(int item_index, int x, int y, bool rotated,
                           uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kInventoryCanMove;
  action.token = token;
  action.a = item_index;
  action.b = x;
  action.c = y;
  action.d = rotated ? 1 : 0;
  return PushAction(action);
}

bool QueueInventoryMove(int item_index, int x, int y, bool rotated,
                        uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kInventoryMove;
  action.token = token;
  action.a = item_index;
  action.b = x;
  action.c = y;
  action.d = rotated ? 1 : 0;
  return PushAction(action);
}

bool QueueInventoryCombine(const InventoryActionIdentity& item_a,
                           const InventoryActionIdentity& item_b,
                           uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kInventoryCombine;
  action.token = token;
  action.item_a = item_a;
  action.item_b = item_b;
  return PushAction(action);
}

bool QueueSetQuickSlot(int slot, const InventoryActionIdentity& item,
                       uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kSetQuickSlot;
  action.token = token;
  action.a = slot;
  action.item_a = item;
  return PushAction(action);
}

bool QueueClearQuickSlot(int slot, const InventoryActionIdentity& item,
                         uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kClearQuickSlot;
  action.token = token;
  action.a = slot;
  action.item_a = item;
  return PushAction(action);
}

bool QueueActivateQuickSlot(int slot, const InventoryActionIdentity& item,
                            uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kActivateQuickSlot;
  action.token = token;
  action.a = slot;
  action.item_a = item;
  return PushAction(action);
}

bool QueueDiscardInventoryItem(const InventoryActionIdentity& item,
                               uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kDiscardInventoryItem;
  action.token = token;
  action.item_a = item;
  return PushAction(action);
}

bool QueueUseInventoryItem(const InventoryActionIdentity& item,
                           uint32_t token) {
  QueuedAction action;
  action.kind = GameActionKind::kUseInventoryItem;
  action.token = token;
  action.item_a = item;
  return PushAction(action);
}

void DrainGameActions() {
  const DWORD current = GetCurrentThreadId();
  const uint64_t now = GetTickCount64();
  DWORD expected = 0;
  if (!g_game_thread.compare_exchange_strong(expected, current,
                                              std::memory_order_acq_rel) &&
      expected != current) {
    // DXVK can migrate Present after a reset. Transfer ownership only after
    // the former owner has gone quiet, never while Present paths overlap.
    const uint64_t last_seen =
        g_game_thread_last_seen_ms.load(std::memory_order_acquire);
    DWORD owner = expected;
    if (last_seen == 0 || now - last_seen >= 500) {
      if (!g_game_thread.compare_exchange_strong(owner, current,
                                                  std::memory_order_acq_rel)) {
        return;
      }
      DXS_INFO("[action] native action owner migrated from thread %lu to %lu",
               static_cast<unsigned long>(expected),
               static_cast<unsigned long>(current));
    } else {
      uint64_t last_warning =
          g_wrong_game_thread_warning_ms.load(std::memory_order_relaxed);
      if (now - last_warning >= 1000 &&
          g_wrong_game_thread_warning_ms.compare_exchange_strong(
              last_warning, now, std::memory_order_relaxed)) {
        DXS_WARN("[action] refusing native actions from concurrent Present thread");
      }
      return;
    }
  }
  g_game_thread_last_seen_ms.store(now, std::memory_order_release);

  // Bounded even if a callback queues more work. A touch storm cannot hold the
  // game thread indefinitely; any tail is processed on the next frame.
  QueuedAction action;
  for (size_t i = 0; i < kQueueCapacity && PopAction(&action); ++i) {
    ProcessAction(action);
  }
}

void ClearQueuedGameActions() {
  AcquireSRWLockExclusive(&g_queue_lock);
  g_queue_head = 0;
  g_queue_count = 0;
  ReleaseSRWLockExclusive(&g_queue_lock);
  g_reported_queue_full.store(false, std::memory_order_release);
  g_inventory_snapshot_ready.store(false, std::memory_order_release);
}

void* CurrentInventoryPlayer() {
  if (!OnGameThread()) return nullptr;
  static const uint8_t current_sig[] = {0x8B, 0x41, 0x08, 0x8B,
                                        0x04, 0x81, 0xC3};
  static const uint8_t inventory_sig[] = {
      0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8, 0x96, 0xD2, 0x11,
      0x00, 0x83, 0xC4, 0x04, 0x85, 0xC0, 0x74, 0x09};
  if (!HasCode(kCurrentPlayerWrapperVA, current_sig) ||
      !HasCode(kActorInventoryVA, inventory_sig)) {
    ReportSignatureFailure("inventory lookup");
    return nullptr;
  }

  const uintptr_t actor = CurrentPlayerActorHandle();
  if (actor == 0) return nullptr;
  using InventoryFn = void*(DXS_GAME_CDECL*)(uintptr_t);
  auto inventory_for_actor =
      reinterpret_cast<InventoryFn>(Rebase(kActorInventoryVA));
  void* inventory = inventory_for_actor(actor);
  return ValidateInventory(inventory) ? inventory : nullptr;
}

bool CanMoveInventoryItem(int item_index, int x, int y, bool rotated,
                          int* swap_index) {
  if (swap_index) *swap_index = -1;
  if (!OnGameThread() || !swap_index) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot slot;
  if (!inventory || !FindNativeSlot(inventory, item_index, &slot)) return false;
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  uint32_t width = 0;
  uint32_t height = 0;
  if (!Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset, &width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset, &height) ||
      x < 0 || y < 0 || static_cast<uint32_t>(x) >= width ||
      static_cast<uint32_t>(y) >= height) {
    return false;
  }
  static const uint8_t signature[] = {0x8B, 0x49, 0x10, 0xE9,
                                      0x08, 0xAC, 0x10, 0x00};
  if (!HasCode(kCanMoveVA, signature)) {
    ReportSignatureFailure("inventory CanMove");
    return false;
  }
  using Fn = bool(DXS_GAME_THISCALL*)(void*, int, int, int, bool, int*);
  return reinterpret_cast<Fn>(Rebase(kCanMoveVA))(
      inventory, item_index, x, y, rotated, swap_index);
}

bool MoveInventoryItem(int item_index, int x, int y, bool rotated) {
  int swap = -1;
  if (!CanMoveInventoryItem(item_index, x, y, rotated, &swap)) return false;
  // Container::Move removes an overlapping slot from the grid; despite the
  // CanMove out-parameter, it does not place that slot at the source origin.
  // Refuse every occupied destination here so no custom-UI path can detach an
  // inventory item accidentally.
  if (swap >= 0 && swap != item_index) {
    DXS_WARN("[action] refused destructive inventory move item=%d target=%d",
             item_index, swap);
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  static const uint8_t signature[] = {0x8B, 0x49, 0x10, 0xE9,
                                      0xB8, 0xAC, 0x10, 0x00};
  if (!inventory || !HasCode(kMoveVA, signature)) {
    ReportSignatureFailure("inventory Move");
    return false;
  }
  using Fn = void(DXS_GAME_THISCALL*)(void*, int, int, int, bool);
  reinterpret_cast<Fn>(Rebase(kMoveVA))(inventory, item_index, x, y, rotated);
  DXS_INFO("[action] moved inventory item=%d to %d,%d rotated=%d",
           item_index, x, y, rotated ? 1 : 0);
  return true;
}

bool SwapInventoryItems(int item_a, int item_b) {
  if (!OnGameThread() || item_a < 0 || item_b < 0 || item_a == item_b) {
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  NativeSlot source;
  NativeSlot target;
  if (!inventory || !FindNativeSlot(inventory, item_a, &source) ||
      !FindNativeSlot(inventory, item_b, &target)) {
    return false;
  }

  int source_overlap = -1;
  int target_overlap = -1;
  if (!CanMoveInventoryItem(item_a, static_cast<int>(target.x),
                            static_cast<int>(target.y), source.rotated != 0,
                            &source_overlap) ||
      source_overlap != item_b ||
      !CanMoveInventoryItem(item_b, static_cast<int>(source.x),
                            static_cast<int>(source.y), target.rotated != 0,
                            &target_overlap) ||
      target_overlap != item_a) {
    DXS_WARN("[action] inventory items do not fit each other's slots a=%d b=%d",
             item_a, item_b);
    return false;
  }

  void* container = PtrAt(inventory, kInventoryContainerOffset);
  uint32_t grid_width = 0;
  uint32_t grid_height = 0;
  PickupMetadata target_metadata;
  if (!container ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset,
            &grid_width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset,
            &grid_height) ||
      grid_width == 0 || grid_height == 0 ||
      grid_width > 32 || grid_height > 32 ||
      !ResolvePickupMetadata(target.pickup_id, &target_metadata)) {
    return false;
  }

  const int target_width = target.rotated
      ? target_metadata.height : target_metadata.width;
  const int target_height = target.rotated
      ? target_metadata.width : target_metadata.height;
  auto overlaps_target_origin = [&](int x, int y) {
    const int old_left = static_cast<int>(target.x);
    const int old_top = static_cast<int>(target.y);
    const int old_right = old_left + target_width;
    const int old_bottom = old_top + target_height;
    return x < old_right && x + target_width > old_left &&
           y < old_bottom && y + target_height > old_top;
  };

  int temporary_x = -1;
  int temporary_y = -1;
  // Search from the far edge so an interrupted swap cannot resemble a
  // successful user-requested placement.
  for (int cell = static_cast<int>(grid_width * grid_height) - 1;
       cell >= 0 && temporary_x < 0; --cell) {
    const int x = cell % static_cast<int>(grid_width);
    const int y = cell / static_cast<int>(grid_width);
    if ((x == static_cast<int>(target.x) &&
         y == static_cast<int>(target.y)) ||
        overlaps_target_origin(x, y)) {
      continue;
    }
    int overlap = -1;
    if (CanMoveInventoryItem(item_b, x, y, target.rotated != 0, &overlap) &&
        (overlap < 0 || overlap == item_b)) {
      temporary_x = x;
      temporary_y = y;
    }
  }
  if (temporary_x < 0) {
    DXS_WARN("[action] no safe temporary grid space for swap a=%d b=%d",
             item_a, item_b);
    return false;
  }

  if (!MoveInventoryItem(item_b, temporary_x, temporary_y,
                         target.rotated != 0)) {
    return false;
  }
  if (!MoveInventoryItem(item_a, static_cast<int>(target.x),
                         static_cast<int>(target.y), source.rotated != 0)) {
    MoveInventoryItem(item_b, static_cast<int>(target.x),
                      static_cast<int>(target.y), target.rotated != 0);
    return false;
  }
  if (!MoveInventoryItem(item_b, static_cast<int>(source.x),
                         static_cast<int>(source.y), target.rotated != 0)) {
    // Both original locations are empty at this point. Roll back in source,
    // then target order; each call is another non-destructive move.
    const bool source_restored = MoveInventoryItem(
        item_a, static_cast<int>(source.x), static_cast<int>(source.y),
        source.rotated != 0);
    const bool target_restored = MoveInventoryItem(
        item_b, static_cast<int>(target.x), static_cast<int>(target.y),
        target.rotated != 0);
    DXS_WARN("[action] inventory swap rolled back a=%d b=%d restored=%d/%d",
             item_a, item_b, source_restored ? 1 : 0,
             target_restored ? 1 : 0);
    return false;
  }

  DXS_INFO("[action] swapped inventory items a=%d b=%d via %d,%d",
           item_a, item_b, temporary_x, temporary_y);
  return true;
}

bool TryCombineInventoryItems(const InventoryActionIdentity& item_a,
                              const InventoryActionIdentity& item_b,
                              int* result_index) {
  if (result_index) *result_index = -1;
  if (!OnGameThread() || !result_index ||
      item_a.item_index == item_b.item_index ||
      item_a.inventory != item_b.inventory ||
      item_a.revision != item_b.revision) {
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  NativeSlot a;
  NativeSlot b;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item_a, &a, "Combine source") ||
      !MatchInventoryActionIdentity(inventory, item_b, &b, "Combine target")) {
    return false;
  }
  static const uint8_t can_signature[] = {0x83, 0xEC, 0x08, 0x53, 0x55,
                                          0x8B, 0x6C, 0x24, 0x14};
  static const uint8_t combine_signature[] = {
      0x83, 0xEC, 0x38, 0x53, 0x55, 0x8B, 0x6C, 0x24,
      0x44, 0x8B, 0x55, 0x00, 0x56, 0x8B, 0xF1};
  if (!HasCode(kCanCombineVA, can_signature) ||
      !HasCode(kCombineVA, combine_signature)) {
    ReportSignatureFailure("inventory Combine");
    return false;
  }
  using CanFn = bool(DXS_GAME_THISCALL*)(void*, int, int);
  if (!reinterpret_cast<CanFn>(Rebase(kCanCombineVA))(
          inventory, item_a.item_index, item_b.item_index)) {
    return false;
  }
  int first = item_a.item_index;
  int second = item_b.item_index;
  using CombineFn = int(DXS_GAME_THISCALL*)(void*, int*, int*);
  *result_index =
      reinterpret_cast<CombineFn>(Rebase(kCombineVA))(inventory, &first, &second);
  // A -1 result is not a reliable failure indication.  The native routine has
  // paths which consume/rebuild the source and only then return -1 (notably
  // weapon/quick-slot combinations).  CanCombine was true and the mutating
  // routine ran, so callers must refresh and must not attempt a Move fallback.
  return true;
}

int CombineInventoryItems(const InventoryActionIdentity& item_a,
                          const InventoryActionIdentity& item_b) {
  int result = -1;
  return TryCombineInventoryItems(item_a, item_b, &result) ? result : -1;
}

bool SetInventoryQuickSlot(int slot, const InventoryActionIdentity& item) {
  if (!OnGameThread() || slot < 0 || slot >= kQuickSlotCount) {
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  NativeSlot live_item;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &live_item,
                                    "quick-slot assignment")) {
    return false;
  }
  static const uint8_t signature[] = {0x53, 0x56, 0x57, 0x8B, 0x7C,
                                      0x24, 0x14, 0x8B, 0xF1};
  if (!HasCode(kSetQuickVA, signature)) {
    ReportSignatureFailure("quick-slot assignment");
    return false;
  }
  using Fn = bool(DXS_GAME_THISCALL*)(void*, int, uint32_t);
  return reinterpret_cast<Fn>(Rebase(kSetQuickVA))(
      inventory, slot, live_item.pickup_id);
}

bool ClearInventoryQuickSlot(int slot, const InventoryActionIdentity& item) {
  if (!OnGameThread() || slot < 0 || slot >= kQuickSlotCount) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot live_item;
  uint32_t quick_pickup = kEmptyPickup;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &live_item,
                                    "quick-slot clear") ||
      !Read(static_cast<uint8_t*>(inventory) + kQuickSlotsOffset +
                static_cast<size_t>(slot) * sizeof(uint32_t),
            &quick_pickup) || quick_pickup != live_item.pickup_id) {
    return false;
  }
  static const uint8_t signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0xC7, 0x84, 0x81, 0xA0,
      0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
  if (!HasCode(kClearQuickVA, signature)) {
    ReportSignatureFailure("quick-slot clear");
    return false;
  }
  using Fn = void(DXS_GAME_THISCALL*)(void*, int);
  reinterpret_cast<Fn>(Rebase(kClearQuickVA))(inventory, slot);
  return true;
}

bool ActivateInventoryQuickSlot(int slot,
                                const InventoryActionIdentity& item) {
  if (!OnGameThread() || slot < 0 || slot >= kQuickSlotCount) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot live_item;
  uint32_t pickup = kEmptyPickup;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &live_item,
                                    "quick-slot activation") ||
      !Read(static_cast<uint8_t*>(inventory) + kQuickSlotsOffset +
                static_cast<size_t>(slot) * sizeof(uint32_t),
            &pickup) ||
      pickup == kEmptyPickup || pickup != live_item.pickup_id) {
    return false;
  }
  static const uint8_t signature[] = {0x53, 0x8B, 0x5C, 0x24, 0x08,
                                      0x56, 0x8B, 0xF1};
  if (!HasCode(kActivateQuickVA, signature)) {
    ReportSignatureFailure("quick-slot activation");
    return false;
  }
  using Fn = void(DXS_GAME_THISCALL*)(void*, int);
  reinterpret_cast<Fn>(Rebase(kActivateQuickVA))(inventory, slot);
  return true;
}

bool DiscardInventoryItem(const InventoryActionIdentity& item) {
  if (!OnGameThread()) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot slot;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &slot, "Discard")) {
    return false;
  }
  static const uint8_t can_signature[] = {0x8B, 0x54, 0x24, 0x04, 0x53,
                                          0x55, 0x56, 0x8B, 0xF1};
  static const uint8_t discard_signature[] = {0x83, 0xEC, 0x2C, 0x53,
                                              0x55, 0x56, 0x8B, 0xE9};
  if (!HasCode(kCanDiscardVA, can_signature) ||
      !HasCode(kDiscardVA, discard_signature)) {
    ReportSignatureFailure("inventory Discard");
    return false;
  }
  using CanFn = bool(DXS_GAME_THISCALL*)(void*, int);
  if (!reinterpret_cast<CanFn>(Rebase(kCanDiscardVA))(
          inventory, item.item_index)) {
    return false;
  }
  using Fn = void(DXS_GAME_THISCALL*)(void*, int, bool);
  reinterpret_cast<Fn>(Rebase(kDiscardVA))(
      inventory, item.item_index, true);
  return true;
}

bool UseInventoryItem(const InventoryActionIdentity& item) {
  if (!OnGameThread()) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot slot;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &slot, "Use")) {
    return false;
  }
  static const uint8_t can_signature[] = {0x8B, 0x54, 0x24, 0x04, 0x56,
                                          0x8B, 0xF1, 0x8B, 0x4E, 0x10};
  static const uint8_t use_signature[] = {
      0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55, 0x56, 0x8B,
      0xF1, 0x8B, 0x4E, 0x10, 0x8B, 0x01, 0x8B, 0x50};
  if (!HasCode(kCanUseVA, can_signature) ||
      !HasCode(kUseInventoryItemVA, use_signature)) {
    ReportSignatureFailure("inventory Use");
    return false;
  }
  using CanFn = bool(DXS_GAME_THISCALL*)(void*, int);
  if (!reinterpret_cast<CanFn>(Rebase(kCanUseVA))(
          inventory, item.item_index)) {
    return false;
  }
  using Fn = bool(DXS_GAME_THISCALL*)(void*, int);
  // The native return value is not an action-success flag. Category 3 follows
  // the game's exact equip/holster toggle (including the same-slot identity
  // comparison), but both successful branches return AL=0. Other persistent
  // quick-slot categories do the same; AL=1 is reserved for paths that consume
  // and remove an inventory item. Once CanUse accepts the live slot, reaching
  // this call means the action was safely dispatched.
  (void)reinterpret_cast<Fn>(Rebase(kUseInventoryItemVA))(
      inventory, item.item_index);
  return true;
}

bool DispatchWeaponControllerAction(uintptr_t preferred_va,
                                    const char* diagnostic,
                                    const InventoryActionIdentity& item) {
  if (!OnGameThread()) return false;
  void* inventory = CurrentInventoryPlayer();
  NativeSlot live_item;
  int storage_index = -1;
  if (!inventory ||
      !MatchInventoryActionIdentity(inventory, item, &live_item, diagnostic) ||
      !FindNativeSlot(inventory, item.item_index, nullptr, &storage_index) ||
      storage_index < 0) {
    return false;
  }
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  void* equipped = PtrAt(inventory, kInventoryEquippedWeaponOffset);
  void* equipped_item = equipped &&
      Readable(equipped, kEquippedWeaponItemOffset + sizeof(void*))
      ? PtrAt(equipped, kEquippedWeaponItemOffset) : nullptr;
  void* expected_item = container
      ? static_cast<uint8_t*>(container) + kContainerSlotsOffset +
            static_cast<size_t>(storage_index) * kContainerSlotStride
      : nullptr;
  if (!equipped_item || equipped_item != expected_item) return false;
  void* controller = CurrentPlayerController();
  if (!controller) return false;
  // Both adjacent retail callbacks begin with the same controller-state
  // guard. The broad executable build check has already run; retaining this
  // local signature makes a mismatched executable fail closed.
  static const uint8_t signature[] = {
      0x83, 0xEC, 0x08, 0x56, 0x8B, 0xF1,
      0x8B, 0x86, 0x8C, 0x00, 0x00, 0x00};
  if (!HasCode(preferred_va, signature)) {
    ReportSignatureFailure(diagnostic);
    return false;
  }
  using Fn = void(DXS_GAME_THISCALL*)(void*, int);
  reinterpret_cast<Fn>(Rebase(preferred_va))(controller, 0);
  return true;
}

bool HolsterEquippedWeapon(const InventoryActionIdentity& item) {
  return DispatchWeaponControllerAction(kHolsterWeaponVA, "weapon holster",
                                        item);
}

bool ReloadEquippedWeapon(const InventoryActionIdentity& item) {
  return DispatchWeaponControllerAction(kReloadWeaponVA, "weapon reload",
                                        item);
}

bool ActivateCoreAugmentation(int slot, uintptr_t expected_inventory,
                              uint32_t expected_revision) {
  if (!OnGameThread() || slot < 0 || slot >= kCoreAugmentationCount) {
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  uint32_t live_revision = 0;
  if (!expected_inventory ||
      reinterpret_cast<uintptr_t>(inventory) != expected_inventory ||
      !Read(static_cast<uint8_t*>(inventory) + kInventoryRevisionOffset,
            &live_revision) || live_revision != expected_revision) {
    return false;
  }
  void* controller = CurrentPlayerController();
  if (!controller) return false;

  static const uint8_t typhoon_signature[] = {
      0x81, 0xC1, 0x18, 0x02, 0x00, 0x00, 0xE8,
      0x65, 0x75, 0xED, 0xFF, 0xC2, 0x04, 0x00};
  static const uint8_t cloak_signature[] = {
      0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x20, 0x05, 0x00,
      0x00, 0x6A, 0x39, 0xE8, 0xE0, 0xDF, 0xFC, 0xFF};
  static const uint8_t vision_signature[] = {
      0x57, 0x8B, 0xF9, 0x8B, 0x8F, 0x20, 0x05, 0x00,
      0x00, 0x6A, 0x33, 0xE8, 0xF0, 0xDE, 0xFC, 0xFF};
  static const uint8_t silent_signature[] = {
      0x56, 0x8B, 0xF1, 0x8B, 0x8E, 0x20, 0x05, 0x00,
      0x00, 0x6A, 0x35, 0xE8, 0x50, 0xDF, 0xFC, 0xFF};
  uintptr_t callback = 0;
  bool signature_valid = false;
  switch (slot) {
    case 0:
      callback = kActivateTyphoonVA;
      signature_valid = HasCode(callback, typhoon_signature);
      break;
    case 1:
      callback = kToggleCloakVA;
      signature_valid = HasCode(callback, cloak_signature);
      break;
    case 2:
      callback = kToggleSmartVisionVA;
      signature_valid = HasCode(callback, vision_signature);
      break;
    case 3:
      callback = kToggleRunSilentlyVA;
      signature_valid = HasCode(callback, silent_signature);
      break;
  }
  if (!signature_valid) {
    ReportSignatureFailure("augmentation activation");
    return false;
  }

  // The ignored event argument belongs to the retail input-listener ABI and
  // is removed by the callback's `ret 4`.
  using Fn = void(DXS_GAME_THISCALL*)(void*, int);
  reinterpret_cast<Fn>(Rebase(callback))(controller, 0);
  // Bypass the normal half-second presentation cache after a direct toggle so
  // the card feedback follows the native controller on the very next poll.
  g_augmentation_snapshot_cache.last_attempt_ms = 0;
  // Typhoon is an instantaneous action rather than a toggle, so the native
  // controller has no lasting "active" bit for the UI to sample.  Retain a
  // short presentation pulse after the real callback fires; the other three
  // cards continue to read their live controller state directly.
  if (slot == 0) {
    g_typhoon_active_until_ms.store(GetTickCount64() + 900,
                                     std::memory_order_release);
  }
  return true;
}

bool PurchaseNativeAugmentation(int tree_id, int node_index,
                                uintptr_t expected_inventory,
                                uint32_t expected_revision) {
  if (!OnGameThread() || tree_id < 0 ||
      static_cast<uint32_t>(tree_id) >= kMaxPickupId || node_index < -1 ||
      !BuildAugmentationCatalog()) {
    return false;
  }
  void* inventory = CurrentInventoryPlayer();
  uint32_t live_revision = 0;
  if (!expected_inventory ||
      reinterpret_cast<uintptr_t>(inventory) != expected_inventory ||
      !Read(static_cast<uint8_t*>(inventory) + kInventoryRevisionOffset,
            &live_revision) || live_revision != expected_revision) {
    return false;
  }

  const CachedAugmentationTree* catalog = nullptr;
  for (int index = 0; index < g_augmentation_catalog_count; ++index) {
    if (g_augmentation_catalog[index].tree_id == tree_id) {
      catalog = &g_augmentation_catalog[index];
      break;
    }
  }
  bool catalog_node_found = node_index < 0;
  if (catalog && node_index >= 0) {
    for (int index = 0; index < catalog->record_count; ++index) {
      if (catalog->nodes[index].node_index == node_index) {
        catalog_node_found = true;
        break;
      }
    }
  }
  if (!catalog || !catalog_node_found) {
    return false;
  }

  static const uint8_t experience_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x50, 0xE8, 0x16, 0xC7, 0x12, 0x00};
  static const uint8_t subtract_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x29, 0x41, 0x10};
  static const uint8_t install_node_signature[] = {
      0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55, 0x8B, 0x6C, 0x24, 0x10};
  static const uint8_t install_tree_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x55, 0x56, 0x57, 0x50};
  static const uint8_t validate_node_signature[] = {
      0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x1C, 0x7B,
      0xA0, 0x01, 0x57, 0xE8, 0x30, 0x37, 0xF5, 0xFF};
  static const uint8_t validate_tree_signature[] = {
      0x53, 0x56, 0x8B, 0xF1, 0x8B, 0x0D, 0x1C, 0x7B,
      0xA0, 0x01, 0x57, 0xE8, 0x70, 0x36, 0xF5, 0xFF};
  static const uint8_t resolve_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x80, 0x01,
      0x00, 0x73, 0x15, 0x8B, 0x04, 0x85, 0xE8, 0x06,
      0x98, 0x01};
  static const uint8_t refresh_inventory_signature[] = {
      0x56, 0x57, 0x8B, 0xF1, 0x8B, 0x46, 0x08, 0x6A, 0x10};
  if (!HasCode(kExperienceForActorVA, experience_signature) ||
      !HasCode(kSubtractPraxisVA, subtract_signature) ||
      !HasCode(kInstallAugmentationNodeVA, install_node_signature) ||
      !HasCode(kInstallAugmentationTreeVA, install_tree_signature) ||
      !HasCode(kValidateAugmentationNodePurchaseVA,
               validate_node_signature) ||
      !HasCode(kValidateAugmentationTreePurchaseVA,
               validate_tree_signature) ||
      !HasCode(kResolvePickupVA, resolve_signature) ||
      !HasCode(kRefreshInventoryVA, refresh_inventory_signature)) {
    ReportSignatureFailure("native augmentation purchase");
    return false;
  }

  const uintptr_t actor = CurrentPlayerActorHandle();
  if (!actor) return false;
  using ExperienceFn = void*(DXS_GAME_CDECL*)(uintptr_t);
  void* experience = reinterpret_cast<ExperienceFn>(
      Rebase(kExperienceForActorVA))(actor);
  if (!experience || !Readable(experience, 0x148)) return false;

  struct NativePurchaseContext {
    uint8_t reserved[0x10] = {};
    void* experience = nullptr;
    uint8_t tail[0x10] = {};
  } validation;
  static_assert(offsetof(NativePurchaseContext, experience) == 0x10,
                "retail augmentation validator context changed");
  bool validated = false;
  if (node_index < 0) {
    using ValidateTreeFn = bool(DXS_GAME_THISCALL*)(void*, int);
    validated = reinterpret_cast<ValidateTreeFn>(
        Rebase(kValidateAugmentationTreePurchaseVA))(&validation, tree_id);
  } else {
    using ValidateNodeFn = bool(DXS_GAME_THISCALL*)(void*, int, int);
    validated = reinterpret_cast<ValidateNodeFn>(
        Rebase(kValidateAugmentationNodePurchaseVA))(
            &validation, tree_id, node_index);
  }
  // The retail validator re-reads the current player and writes its Experience
  // object at +0x10. Reject an actor transition rather than applying a cached
  // touch to the save which has just replaced it.
  if (!validated || validation.experience != experience ||
      !Readable(validation.experience, 0x148)) {
    return false;
  }

  using ResolveFn = void*(DXS_GAME_CDECL*)(uint32_t);
  void* live_tree = reinterpret_cast<ResolveFn>(Rebase(kResolvePickupVA))(
      static_cast<uint32_t>(tree_id));
  if (!live_tree || !Readable(live_tree, 2 * sizeof(void*))) return false;

  using TreeInstalledFn = bool(DXS_GAME_THISCALL*)(void*, int);
  using NodeInstalledFn = bool(DXS_GAME_THISCALL*)(void*, const void*, int);
  using NodeAvailableFn = bool(DXS_GAME_THISCALL*)(void*, const void*, int);
  using TreeIntFn = int(DXS_GAME_CDECL*)(const void*);
  using NodeIntFn = int(DXS_GAME_CDECL*)(const void*);
  auto tree_installed = reinterpret_cast<TreeInstalledFn>(
      Rebase(kAugmentationTreeInstalledVA));
  auto node_installed = reinterpret_cast<NodeInstalledFn>(
      Rebase(kAugmentationNodeInstalledVA));
  auto prerequisites = reinterpret_cast<NodeAvailableFn>(
      Rebase(kAugmentationPrerequisitesVA));
  auto available = reinterpret_cast<NodeAvailableFn>(
      Rebase(kAugmentationAvailableVA));
  auto base_cost = reinterpret_cast<TreeIntFn>(
      Rebase(kAugmentationBaseCostVA));
  auto node_cost = reinterpret_cast<NodeIntFn>(
      Rebase(kAugmentationNodeCostVA));

  const bool tree_present = tree_installed(experience, tree_id);
  int cost = -1;
  if (node_index < 0) {
    if (tree_present) return false;
    cost = base_cost(live_tree);
  } else {
    if (!tree_present ||
        node_installed(experience, live_tree, node_index) ||
        !prerequisites(experience, live_tree, node_index) ||
        !available(experience, live_tree, node_index)) {
      return false;
    }
    void* node = nullptr;
    if (ResolveAugmentationRecord(
            live_tree, static_cast<uint32_t>(node_index), &node) !=
        AugmentationRecordResolution::kNode) {
      return false;
    }
    cost = node_cost(node);
  }

  int praxis = -1;
  if (cost < 0 || cost > 100 ||
      !Read(static_cast<uint8_t*>(experience) + 0x10, &praxis) ||
      praxis < cost) {
    return false;
  }

  using SubtractFn = void(DXS_GAME_THISCALL*)(void*, int);
  reinterpret_cast<SubtractFn>(Rebase(kSubtractPraxisVA))(experience, cost);
  if (node_index < 0) {
    using InstallTreeFn = void(DXS_GAME_THISCALL*)(void*, int, bool, bool,
                                                   bool);
    reinterpret_cast<InstallTreeFn>(Rebase(kInstallAugmentationTreeVA))(
        experience, tree_id, true, true, true);
  } else {
    using InstallNodeFn = void(DXS_GAME_THISCALL*)(void*, const void*, int,
                                                   bool);
    reinterpret_cast<InstallNodeFn>(Rebase(kInstallAugmentationNodeVA))(
        experience, live_tree, node_index, false);
    if (void* inventory = CurrentInventoryPlayer()) {
      using RefreshFn = void(DXS_GAME_THISCALL*)(void*);
      reinterpret_cast<RefreshFn>(Rebase(kRefreshInventoryVA))(inventory);
    }
  }
  ResetAugmentationSnapshotCache();
  DXS_INFO("[action] purchased native augmentation tree=%d node=%d cost=%d",
           tree_id, node_index, cost);
  return true;
}

bool ResolvePickupMetadata(uint32_t pickup_id, PickupMetadata* out) {
  if (out) *out = PickupMetadata{};
  if (!OnGameThread() || !out || pickup_id >= kMaxPickupId) return false;
  static const uint8_t pickup_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x3D, 0x00, 0x80, 0x01,
      0x00, 0x73, 0x15, 0x8B, 0x04, 0x85, 0xE8, 0x06,
      0x98, 0x01};
  static const uint8_t string_signature[] = {
      0x8B, 0x15, 0xF8, 0x43, 0xB2, 0x00, 0x85, 0xD2,
      0x74, 0x1B, 0x8B, 0x0D, 0xF4, 0x43, 0xB2, 0x00};
  static const uint8_t pickup_name_signature[] = {
      0x8B, 0x44, 0x24, 0x04, 0x0F, 0xBF, 0x48, 0x02,
      0x51, 0xE8, 0x22, 0x59, 0xD5, 0xFF};
  if (!HasCode(kResolvePickupVA, pickup_signature) ||
      !HasCode(kLocalStringVA, string_signature) ||
      !HasCode(kPickupNameVA, pickup_name_signature)) {
    ReportSignatureFailure("pickup metadata");
    return false;
  }
  PickupCacheEntry& cached = g_pickup_cache[pickup_id % kPickupCacheSize];
  if (cached.valid && cached.pickup_id == pickup_id) {
    *out = cached.metadata;
    return true;
  }
  using PickupFn = void*(DXS_GAME_CDECL*)(uint32_t);
  void* pickup = reinterpret_cast<PickupFn>(Rebase(kResolvePickupVA))(pickup_id);
  if (!pickup || !Readable(pickup, 0x34)) return false;

  int16_t description_index = 0;
  uint32_t pickup_type = 0;
  bool has_description_index = false;
  if (!Read(static_cast<uint8_t*>(pickup) + 0x08, &out->texture_id) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x10, &out->rotated_texture_id) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x1C, &pickup_type) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x24, &out->width) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x25, &out->height) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x28, &out->max_stack) ||
      !Read(static_cast<uint8_t*>(pickup) + 0x30, &out->category)) {
    *out = PickupMetadata{};
    return false;
  }
  // Inventory's native serializer uses PickupName (0x00777000) for its
  // player-facing `name`. The variant-dependent helper at 0x00776EC0 is the
  // separate `subtitle` field; using it here produced weapon codenames such as
  // "Buzzkill Threat Negation Device" instead of "Stun Gun".
  if (pickup_type <= 1) {
    has_description_index =
        Read(static_cast<uint8_t*>(pickup) + 0x22, &description_index);
  } else if (pickup_type == 2) {
    has_description_index =
        Read(static_cast<uint8_t*>(pickup) + 0x26, &description_index);
  }
  if (out->width == 0 || out->height == 0 || out->width > 16 || out->height > 16) {
    *out = PickupMetadata{};
    return false;
  }
  out->pickup_id = pickup_id;
  if (out->category == 3) {
    uint32_t base_capacity = 0;
    if (Read(static_cast<uint8_t*>(pickup) + 0x54, &base_capacity) &&
        base_capacity > 0 && base_capacity <= 10000) {
      out->magazine_capacity = static_cast<int>(base_capacity);
    }
  }
  using PickupNameFn = const char*(DXS_GAME_STDCALL*)(const void*);
  CopyGameString(
      reinterpret_cast<PickupNameFn>(Rebase(kPickupNameVA))(pickup), out->name);
  using StringFn = const char*(DXS_GAME_CDECL*)(int);
  auto local_string = reinterpret_cast<StringFn>(Rebase(kLocalStringVA));
  if (has_description_index) {
    CopyGameString(local_string(description_index), out->description);
  }
  // Do not cache a pre-localisation empty label; the next loaded frame gets a
  // chance to resolve it again after the game's string table comes online.
  if (out->name[0] != '\0') {
    cached.valid = true;
    cached.pickup_id = pickup_id;
    cached.metadata = *out;
  }
  return true;
}

bool CaptureInventorySnapshot(InventorySnapshot* out) {
  ResetInventorySnapshot(out);
  if (!OnGameThread() || !out) return false;
  void* inventory = CurrentInventoryPlayer();
  if (!inventory) return false;
  out->inventory_identity = reinterpret_cast<uintptr_t>(inventory);
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  uint32_t width = 0;
  uint32_t height = 0;
  if (!Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset, &width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset, &height) ||
      !Read(static_cast<uint8_t*>(inventory) + kInventoryRevisionOffset,
            &out->revision) ||
      !Read(static_cast<uint8_t*>(inventory) + kMoneyOffset, &out->credits)) {
    ResetInventorySnapshot(out);
    return false;
  }
  out->grid_width = static_cast<int>(width);
  out->grid_height = static_cast<int>(height);

  // InventoryPlayer keeps an equipped-weapon wrapper beside the item
  // container. Its +0x70 field is the address of the exact 0x28-byte slot in
  // the container backing store. Comparing addresses is allocation-free and
  // cannot confuse two copies of the same weapon pickup.
  void* equipped_item = nullptr;
  if (void* equipped = PtrAt(inventory, kInventoryEquippedWeaponOffset)) {
    if (Readable(equipped, kEquippedWeaponItemOffset + sizeof(void*))) {
      equipped_item = PtrAt(equipped, kEquippedWeaponItemOffset);
    }
  }

  int active[kMaxInventorySlots] = {};
  int active_count = 0;
  if (!CollectActiveStorageIndices(container, active, &active_count)) {
    ResetInventorySnapshot(out);
    return false;
  }
  for (int n = 0; n < active_count; ++n) {
    const int storage = active[n];
    NativeSlot native;
    if (!ReadNativeSlotFromContainer(container, storage, &native)) {
      ResetInventorySnapshot(out);
      return false;
    }
    if (native.pickup_id == kEmptyPickup || native.count == 0) continue;
    if (native.pickup_id >= kMaxPickupId || native.item_index >= kMaxInventorySlots ||
        native.x >= width || native.y >= height) {
      continue;
    }
    InventoryItemSnapshot& item = out->items[out->item_count];
    item.item_index = native.item_index;
    item.storage_index = storage;
    item.x = static_cast<int>(native.x);
    item.y = static_cast<int>(native.y);
    item.count = native.count;
    item.rotated = native.rotated != 0;
    if (!ResolvePickupMetadata(native.pickup_id, &item.pickup)) {
      item.pickup.pickup_id = native.pickup_id;
    }
    const void* native_item_address =
        static_cast<const uint8_t*>(container) + kContainerSlotsOffset +
        static_cast<size_t>(storage) * kContainerSlotStride;
    item.equipped = item.pickup.category == 3 &&
                    equipped_item == native_item_address;
    if (item.pickup.category == 3 && item.pickup.magazine_capacity > 0) {
      item.ammo_upgrade_level = native.ammo_upgrade_level;
      item.magazine_capacity = EffectiveMagazineCapacity(
          item.pickup.magazine_capacity, native.ammo_upgrade_level);
      const int loaded = native.magazine_initialized != 0
                             ? static_cast<int>(native.magazine_ammo)
                             : item.pickup.magazine_capacity;
      if (loaded >= 0 && loaded <= 10000 && item.magazine_capacity > 0) {
        // A modded or unknown weapon record can legitimately exceed the
        // retail capacity table. Never display an impossible current/max pair.
        item.magazine_capacity =
            (std::max)(item.magazine_capacity, loaded);
        item.magazine_ammo = loaded;
        item.magazine_state_valid = true;
      }
    }
    ++out->item_count;
  }

  for (int i = 0; i < kQuickSlotCount; ++i) {
    QuickSlotSnapshot& quick = out->quick_slots[i];
    quick.slot = i;
    if (!Read(static_cast<uint8_t*>(inventory) + kQuickSlotsOffset +
                  static_cast<size_t>(i) * sizeof(uint32_t),
              &quick.pickup_id)) {
      ResetInventorySnapshot(out);
      return false;
    }
    if (quick.pickup_id == kEmptyPickup) continue;
    for (int item = 0; item < out->item_count; ++item) {
      if (out->items[item].pickup.pickup_id == quick.pickup_id) {
        quick.item_index = out->items[item].item_index;
        break;
      }
    }
  }
  CaptureHealth(out);
  CaptureProgressAndEnergy(out);
  CaptureAugmentations(out);
  out->valid = true;
  g_inventory_snapshot_ready.store(true, std::memory_order_release);
  return true;
}

bool NativeInventoryAvailable() {
  if (!OnGameThread()) return false;
  void* inventory = CurrentInventoryPlayer();
  if (!inventory) return false;
  void* container = PtrAt(inventory, kInventoryContainerOffset);
  uint32_t width = 0;
  uint32_t height = 0;
  if (!container ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridWidthOffset,
            &width) ||
      !Read(static_cast<uint8_t*>(container) + kContainerGridHeightOffset,
            &height)) {
    return false;
  }
  // DXHR's player grid is small. Broad bounds tolerate upgrades/mods while
  // rejecting half-constructed or stale containers during a level handoff.
  return width > 0 && width <= 32 && height > 0 && height <= 32;
}

bool NativeInventorySnapshotReady() {
  return g_inventory_snapshot_ready.load(std::memory_order_acquire);
}

void ResetNativeInventorySnapshotReady() {
  g_inventory_snapshot_ready.store(false, std::memory_order_release);
  ResetAugmentationSnapshotCache();
}

bool SelectCustomPage(int page) {
  // Page zero was a retired HOME placeholder. Preserve compatibility with an
  // older guest sender by mapping it to the real Inventory page instead of
  // leaving the carousel with no selected tab.
  if (page == 0) page = 3;
  if (page < 1 || page > 5) return false;
  PageSelectionHandler callback = nullptr;
  void* context = nullptr;
  AcquireSRWLockShared(&g_callback_lock);
  callback = g_page_handler;
  context = g_page_context;
  ReleaseSRWLockShared(&g_callback_lock);
  if (callback && !callback(page, context)) return false;
  g_selected_page.store(page, std::memory_order_release);
  return true;
}

int SelectedCustomPage() {
  return g_selected_page.load(std::memory_order_acquire);
}

}  // namespace dxs
