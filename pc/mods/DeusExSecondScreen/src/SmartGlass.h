// Read-only bridge to DXHR Director's Cut's shipped SmartGlass/SGUI layer.
//
// The game already knows how to serialize inventory, augmentations, the map,
// objectives and media for a second display.  The original implementation
// only sends those messages to the (now defunct) SmartGlass transport.  This
// bridge asks the existing controllers for their snapshots on the game thread
// and captures the resulting messages locally.  It never opens a Scaleform
// menu and never injects keyboard, mouse or controller input.

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace dxs {

class HookSet;

struct SmartGlassRawMessage {
  uint64_t sequence = 0;
  std::wstring channel;
  std::wstring payload;
};

// A flattened view of one object in a SmartGlass message.  These records are
// intentionally retained in addition to the typed views below: the shipped
// serializers use a few different schemas for DLC and localized builds, and
// the custom renderer can still consume a new field without another engine
// hook.
struct SmartGlassRecord {
  std::map<std::wstring, double> numbers;
  std::map<std::wstring, bool> flags;
  std::map<std::wstring, std::wstring> text;
};

struct SmartGlassInventoryItem {
  int item_index = -1;
  int pickup_id = -1;
  int amount = 0;
  int stack_count = 0;
  int x = -1;
  int y = -1;
  int width = 1;
  int height = 1;
  int beauty_shot_id = -1;
  bool flipped = false;
  bool equipped = false;
  bool can_use = false;
  bool can_discard = false;
  bool can_combine = false;
  bool is_weapon = false;
  std::wstring name;
  std::wstring subtitle;
  std::wstring description;
  std::wstring beauty_shot_image;
  std::vector<int> combine_ids;
  std::map<std::wstring, double> stats;
};

struct SmartGlassQuickSlot {
  int slot = -1;
  int pickup_id = -1;
  int item_index = -1;
  int amount = 0;
  std::wstring type;
  std::wstring name;
};

struct SmartGlassStatus {
  double health = -1.0;
  double max_health = -1.0;
  double energy = -1.0;
  double max_energy = -1.0;
  int total_xp = -1;
  int next_praxis = -1;
  int credits = -1;
  int praxis_points = -1;
};

struct SmartGlassAugmentation {
  int aug_id = -1;
  int node_index = -1;
  int node_count = 0;
  int learned_count = 0;
  int praxis_cost = 0;
  bool installed = false;
  bool learned = false;
  bool buyable = false;
  std::wstring name;
  std::wstring description;
  std::wstring image_name;
  std::wstring type;
};

struct SmartGlassObjective {
  int mission_id = -1;
  int objective_id = -1;
  int state = -1;
  bool primary = false;
  bool tracked = false;
  std::wstring title;
  std::wstring short_description;
  std::wstring long_description;
};

struct SmartGlassMediaItem {
  int media_id = -1;
  int item_id = -1;
  int media_type = -1;
  std::wstring title;
  std::wstring from;
  std::wstring to;
  std::wstring content;
};

struct SmartGlassMapMarker {
  double x = 0.0;
  double y = 0.0;
  double orientation = 0.0;
  int mission_index = -1;
  bool main_mission = false;
  std::wstring name;
  std::wstring icon;
  std::wstring mission_title;
  std::wstring mission_description;
};

struct SmartGlassMap {
  // Basename emitted by the game's SmartGlass serializer (for example a
  // packed Scaleform asset name), not a filesystem path, URL, base64 payload,
  // numeric RenderResource ID, or directly usable D3D texture handle.
  std::wstring image;
  std::wstring location;
  std::wstring sublocation;
  int width = 0;
  int height = 0;
  int level = 0;
  std::vector<SmartGlassMapMarker> markers;
};

struct SmartGlassSnapshot {
  bool valid = false;
  uint64_t revision = 0;
  uint32_t captured_frame = 0;
  int selected_page = 3;

  SmartGlassStatus status;
  std::vector<SmartGlassInventoryItem> inventory_items;
  std::vector<SmartGlassInventoryItem> weapons;
  std::vector<SmartGlassQuickSlot> quick_slots;
  std::vector<SmartGlassAugmentation> augmentations;
  std::vector<SmartGlassObjective> objectives;
  std::vector<SmartGlassMediaItem> media;
  SmartGlassMap map;

  // Case-insensitive, punctuation-free keys (for example NumCredits becomes
  // "numcredits"). These are useful for HUD fields emitted by controller- or
  // DLC-specific messages which do not warrant a dedicated structure.
  std::map<std::wstring, double> numbers;
  std::map<std::wstring, bool> flags;
  std::map<std::wstring, std::wstring> text;
  std::vector<SmartGlassRecord> records;
  std::vector<SmartGlassRawMessage> raw_messages;
};

class SmartGlassBridge {
 public:
  // Installs one exact-signature hook in DXHRDC.exe 2.0.66.0. Unsupported
  // builds are rejected without modifying the process.
  bool Install(HookSet* hooks);
  void Shutdown();

  // Must be called from the primary game Present thread.  Refreshes are
  // throttled internally and are a no-op until a save is loaded.
  void Tick(uint32_t frame, bool game_loaded);

  // Custom panel page numbers: Map=1, Objectives=2, Inventory=3,
  // Augmentations=4, Data=5. This selects what is serialized; it does not open
  // or switch a vanilla menu.
  void SetPage(int page);
  int page() const;
  void ForceRefresh();
  void Clear();

  // Queue native SmartGlass actions for the primary game thread.  These use
  // the game's own validation and purchase/tracking handlers; no input is
  // synthesized and no vanilla menu is opened.  A negative node index buys
  // the base augmentation, while a non-negative index buys that upgrade node.
  // Requests are rejected unless the ID is present in the latest snapshot.
  bool QueuePurchaseAugmentation(int aug_id, int node_index = -1);
  bool QueueToggleObjectiveTracking(int objective_id);

  SmartGlassSnapshot Snapshot() const;
  bool installed() const { return installed_.load(std::memory_order_acquire); }

  // Entry point used only by the signature-checked transport shim.
  void Capture(const wchar_t* channel, const wchar_t* payload);

 private:
  struct PendingAction {
    enum class Kind : uint8_t { kPurchaseAugmentation, kToggleObjective };
    Kind kind = Kind::kPurchaseAugmentation;
    int id = -1;
    int secondary = -1;
  };

  bool Refresh(uint32_t frame, bool full, bool refresh_selected);
  bool ExecutePendingActions(uint32_t frame);
  void RequeueActions(std::vector<PendingAction>* actions);
  void MergeCaptured(uint32_t frame, bool full, bool refreshed_selected, int page,
                     std::vector<SmartGlassRawMessage>* messages);

  mutable std::mutex lock_;
  SmartGlassSnapshot snapshot_;
  std::mutex action_lock_;
  std::vector<PendingAction> pending_actions_;
  HookSet* hooks_ = nullptr;
  std::atomic<bool> installed_{false};
  std::atomic<bool> force_refresh_{true};
  std::atomic<int> selected_page_{3};
  uint32_t game_thread_id_ = 0;
  uint32_t last_update_frame_ = 0;
  uint32_t last_full_frame_ = 0;
  bool was_loaded_ = false;
  bool warned_thread_ = false;
  // The shipped controller serializers are part of the Xbox SmartGlass
  // service lifecycle.  Calling their Enter/Update methods while that service
  // is dormant mutates otherwise uninitialised state machines and produces no
  // payloads.  Remember the real service state so polling can stay completely
  // idle until the game activates it, then perform one clean refresh.
  bool native_service_active_ = false;
  bool polling_suspended_logged_ = false;
  // Augmentation Enter owns a persistent JSON builder. Seed the inventory
  // status shortcuts once per loaded save, never on the periodic timer.
  bool augmentation_seeded_ = false;
  // InventoryEnter emits catalog records first and the owned type-2 slot
  // batch later. While true, retain the previous complete list and replace it
  // exactly once when that live-slot batch is observed.
  bool inventory_replace_pending_ = false;
};

SmartGlassBridge& TheSmartGlass();

}  // namespace dxs
