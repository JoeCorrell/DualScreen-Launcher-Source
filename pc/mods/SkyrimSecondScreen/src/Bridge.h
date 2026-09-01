#pragma once

#include <cstdint>

namespace dis {

// Bump whenever Snapshot's binary layout changes.  The proxy and SKSE plugin
// deliberately reject different versions instead of interpreting stale data.
constexpr uint32_t kBridgeVersion = 0x00010014;
// A normal modded save can easily exceed the old 96-record limit.  Inventory
// changes are not category ordered, so that limit could discard every weapon
// before the UI ever received one. The live Thor save contains 274 distinct
// records, proving the old 256 ceiling was still too small. 384 covers that
// complete inventory with headroom while keeping the fixed hand-off and the
// CPU preview cache bounded.
constexpr uint32_t kMaxItems = 384;
// AddSpellData walks as many as 512 runtime spell entries.  Keeping only the
// first 128 made the live sampler report the ceiling exactly and prevented
// every later spell (and its Skyrim MDOB preview FormID) from ever crossing
// the bridge.  Match the engine-side scan so the fixed ABI no longer truncates
// an ordinary or modded spellbook before the renderer sees it.
constexpr uint32_t kMaxMagic = 512;
constexpr uint32_t kMaxQuests = 48;
// Journal Menu shows the received objectives for one quest instance, not the
// TESQuest template's full editor list.  Thirty-two is deliberately generous
// for vanilla and modded journal entries while keeping the fixed snapshot
// hand-off bounded.
constexpr uint32_t kMaxQuestObjectives = 32;
constexpr uint32_t kMaxFavorites = 8;
// Marker discovery is refreshed only on map-page entry and remains
// deliberately bounded. Projected markers travel atomically with the native
// terrain frame instead of being copied through every telemetry snapshot.
constexpr uint32_t kMaxMapMarkers = 128;
constexpr uint32_t kNativeItemPreviewSize = 192;

// Only explicit SKSE lifecycle transitions may put an already-live panel back
// on the waiting screen.  Sampling hiccups (including ordinary exterior-cell
// changes while walking) retain the last complete snapshot instead.
enum class LoadingReason : uint32_t {
  kNone,
  kStartup,
  kPreLoadGame,
  kPostLoadFailed,
  kNewGame,
};

enum class Page : uint32_t { kStatus, kInventory, kMap, kMagic, kQuests };

enum class CommandType : uint32_t {
  kNone,
  kActivateItem,
  kActivateMagic,
  kSelectQuest,
  kActivateFavorite,
  // Requests Skyrim's own Inventory3DManager model for a form.  The request
  // is asynchronous and never blocks the lower-screen UI thread.
  kRequestItemPreview,
  // Assigns a live inventory or known-magic FormID to one of WEMU's eight
  // lower-screen hotbar slots.  Merely sampling or repainting the UI never
  // emits this command; it is produced only by the explicit assignment flow.
  kAssignHotbar,
  // Keeps the private lower-screen world-map renderer alive while the Map
  // page is visible. This is a render-only request: it never opens MapMenu,
  // pauses the game, captures input, or mutates player/gameplay state.
  kRequestMapPreview,
};

enum CommandFlags : uint32_t {
  // Visible/selected UI requests may promote an already queued thumbnail
  // without duplicating the underlying Skyrim model transaction.
  kCommandPreviewPriority = 1u << 0,
  // Set only on map-page entry. The plugin uses it to refresh Skyrim's
  // runtime marker cache on the game thread; ordinary redraws and camera
  // gestures never rescan the player's marker handles.
  kCommandRefreshMapMarkers = 1u << 1,
  // Reinitialize only the private MapCamera world state around the player.
  // This never writes PlayerCamera or the live gameplay camera root.
  kCommandResetMapCamera = 1u << 2,
};

enum ItemFlags : uint32_t {
  kItemEquipped = 1u << 0,
  kItemFavorite = 1u << 1,
  kItemRightHand = 1u << 2,
  kItemLeftHand = 1u << 3,
};

enum class ItemIconKind : uint16_t {
  kMisc,
  kSword,
  kDagger,
  kAxe,
  kMace,
  kBow,
  kStaff,
  kArrow,
  kShield,
  kHead,
  kBody,
  kHands,
  kFeet,
  kJewelry,
  kPotion,
  kIngredient,
  kBook,
};

enum class EquipmentSlot : uint32_t {
  kNone,
  kRightHand,
  kLeftHand,
  kHead,
  kBody,
  kHands,
  kFeet,
  kShield,
  kRing,
  kAmulet,
  kOther,
};

enum MapMarkerFlags : uint32_t {
  kMapMarkerVisible = 1u << 0,
  kMapMarkerFastTravel = 1u << 1,
};

enum MagicFlags : uint32_t {
  kMagicEquipped = 1u << 0,
  kMagicFavorite = 1u << 1,
  kMagicCostKnown = 1u << 2,
  kMagicLeftHand = 1u << 3,
  kMagicRightHand = 1u << 4,
  kMagicVoice = 1u << 5,
};

enum QuestFlags : uint32_t {
  kQuestActive = 1u << 0,
  kQuestCompleted = 1u << 1,
  kQuestFailed = 1u << 2,
};

struct ItemState {
  uint32_t form_id = 0;
  uint32_t form_type = 0;
  int32_t count = 0;
  uint32_t flags = 0;
  EquipmentSlot equip_slot = EquipmentSlot::kNone;
  ItemIconKind icon_kind = ItemIconKind::kMisc;
  uint16_t reserved_icon = 0;
  float weight = 0.0f;
  int32_t value = 0;
  float damage = 0.0f;
  float armor = 0.0f;
  char name[96] = {};
  char description[256] = {};
};

struct MapMarkerState {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  // Exact map.swf IconTypes index: 1-59 location, 62 quest, 63 quest door,
  // 64 multiple quest, 65 player-set destination, 66 YouAreHere.
  uint16_t type = 0;
  uint16_t reserved = 0;
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  char name[80] = {};
};

// Marker coordinates must be projected through the exact private NiCamera
// that rendered the accompanying terrain frame. Keeping this compact payload
// beside the frame prevents markers from drifting when a newer snapshot or
// gameplay camera arrives while GPU readback is still pending.
struct ProjectedMapMarker {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  uint16_t type = 0;
  uint16_t reserved = 0;
  float screen_x = 0.0f;
  float screen_y = 0.0f;
  float depth = 0.0f;
  float rotation_radians = 0.0f;
};

struct MagicState {
  uint32_t form_id = 0;
  // Skyrim's own MDOB/casting-art TESBoundObject for the native UI3D path.
  // This is deliberately a live game FormID, never a bundled replacement.
  uint32_t preview_form_id = 0;
  uint32_t form_type = 0;
  uint32_t flags = 0;
  uint32_t spell_type = 0;
  int32_t school = -1;
  float cost = 0.0f;
  char name[96] = {};
  char description[256] = {};
};

// Values mirror QUEST_OBJECTIVE_STATE in Skyrim 1.5.97.  Keeping the original
// engine value lets the lower screen distinguish current, completed, and
// failed objectives without writing any state back to the quest system.
struct QuestObjectiveState {
  uint16_t index = 0;
  uint8_t state = 0;
  uint8_t reserved = 0;
  char text[192] = {};
};

struct QuestState {
  uint32_t form_id = 0;
  uint32_t flags = 0;
  uint16_t stage = 0;
  // Number of received objectives for this live quest instance.  The stored
  // count can be smaller only when an unusually large mod exceeds the bounded
  // bridge capacity.
  uint16_t objective_count = 0;
  uint16_t objective_stored = 0;
  uint16_t reserved = 0;
  char name[112] = {};
  // Retained for the Character page's single Current Objective summary.
  char objective[192] = {};
  // Skyrim's localized journal stage entry (CNAM), with the same alias/global
  // substitutions used by the objective strings.
  char lore[768] = {};
  QuestObjectiveState objectives[kMaxQuestObjectives] = {};
};

struct Snapshot {
  uint32_t version = kBridgeVersion;
  uint32_t size = sizeof(Snapshot);
  uint64_t sequence = 0;
  uint32_t in_game = 0;
  uint32_t paused = 0;
  uint64_t world_epoch = 0;
  LoadingReason loading_reason = LoadingReason::kStartup;
  uint32_t reserved_lifecycle = 0;

  float health = 0.0f;
  float max_health = 0.0f;
  float magicka = 0.0f;
  float max_magicka = 0.0f;
  float stamina = 0.0f;
  float max_stamina = 0.0f;
  float carry = 0.0f;
  float max_carry = 0.0f;
  float level_xp = 0.0f;
  float level_xp_max = 0.0f;
  uint32_t level = 0;
  uint32_t perk_points = 0;
  int32_t gold = 0;
  // PlayerCharacter's live card values. These are sampled from Skyrim's
  // actor/item calculators after world-entry stabilization, rather than
  // reconstructed by summing base records in the proxy.
  float armor_rating = 0.0f;
  float damage_rating = 0.0f;

  float player_x = 0.0f;
  float player_y = 0.0f;
  float player_z = 0.0f;
  float heading = 0.0f;
  uint32_t world_form_id = 0;
  // The current save's actual Player TESNPC display name. The lower panel
  // must never substitute the generic "Dragonborn" title for this identity.
  char player_name[96] = {};
  char location[96] = {};

  uint32_t item_count = 0;
  uint32_t magic_count = 0;
  uint32_t quest_count = 0;
  uint32_t favorite_count = 0;
  uint32_t map_marker_count = 0;
  uint32_t reserved = 0;
  // Large sections persist between samples.  Their revisions advance only
  // when player-visible section data changes, allowing the proxy to retain an
  // unchanged section instead of copying the entire ~700 KiB snapshot at the
  // sampler's 10 Hz cadence.
  uint64_t inventory_revision = 0;
  uint64_t magic_revision = 0;
  uint64_t quest_revision = 0;
  uint64_t map_revision = 0;
  ItemState items[kMaxItems] = {};
  MagicState magic[kMaxMagic] = {};
  QuestState quests[kMaxQuests] = {};
  uint32_t favorites[kMaxFavorites] = {};
  MapMarkerState map_markers[kMaxMapMarkers] = {};
};

struct Command {
  uint32_t version = kBridgeVersion;
  CommandType type = CommandType::kNone;
  uint32_t form_id = 0;
  int32_t index = -1;
  uint32_t flags = 0;
  // Native world-map gesture input. These are normalized deltas, not bitmap
  // crop coordinates: the plugin feeds them through Skyrim's MapCamera World
  // state and publishes a newly rendered frame. They must remain zero for all
  // other command types.
  float map_pan_x = 0.0f;
  float map_pan_y = 0.0f;
  float map_zoom_delta = 0.0f;
  // The map worldspace is a second identity boundary inside one save epoch
  // (for example Tamriel versus Solstheim). Non-map commands keep this zero.
  uint32_t map_world_id = 0;
  uint64_t control_revision = 0;
  // Commands are valid only for the save/world whose visible snapshot
  // produced them. This prevents a queued tap or asynchronous model request
  // from being replayed after a load transition reuses the same FormID.
  uint64_t world_epoch = 0;
};

// Retained only as the mixed-version ABI for the retired character renderer.
// Current proxy exports return an invisible surface and ignore submissions.
struct CharacterPreviewSurface {
  uint32_t version = kBridgeVersion;
  uint32_t size = sizeof(CharacterPreviewSurface);
  uint64_t hwnd = 0;  // Reserved for ABI compatibility; always zero.
  int32_t width = 0;
  int32_t height = 0;
  uint32_t visible = 0;
  uint32_t reserved = 0;
  float rotation_degrees = 0.0f;
  float zoom = 1.0f;
  uint64_t control_revision = 0;
};

// Retained with the no-op ABI while older character-renderer internals remain
// compiled but unreachable behind the permanent feature gate.
constexpr uint32_t kCharacterPreviewMaxWidth = 512;
constexpr uint32_t kCharacterPreviewMaxHeight = 768;
// The private map target is a bounded 16:9 surface. The lower UI preserves its
// aspect ratio and applies pan/zoom without creating another swap chain.
constexpr uint32_t kNativeMapMaxWidth = 1280;
constexpr uint32_t kNativeMapMaxHeight = 720;

using SubmitSnapshotFn = void(__stdcall*)(const Snapshot* snapshot);
using SubmitItemPreviewFn = void(__stdcall*)(uint32_t form_id,
                                             uint64_t world_epoch,
                                             uint32_t width,
                                             uint32_t height,
                                             const uint8_t* rgba,
                                             uint32_t row_pitch);
// Sized poll prevents a mixed-version proxy from writing a newer Command
// layout into an older plugin's stack. The legacy one-argument export remains
// a no-write stub; all current plugins resolve SDS_PollCommandSized.
using PollCommandFn = int(__stdcall*)(Command* command,
                                      uint32_t command_size,
                                      uint32_t bridge_version);
/** Cheap lock-free queue probe used by the per-frame player hook. */
using HasPendingCommandFn = int(__stdcall*)();
// Emitted by the SKSE side only after an explicit lower-panel command has
// reached Skyrim's real action path. The proxy turns this semantic success
// signal into Android haptic feedback; UI refreshes can never call it.
using ReportActionSuccessFn = void(__stdcall*)(CommandType type,
                                               uint32_t form_id,
                                               int32_t index);
/** Returns non-zero once the renderer hook is attached. */
using EnableRendererFn = int(__stdcall*)();
using GetCharacterPreviewSurfaceFn = int(__stdcall*)(
    CharacterPreviewSurface* surface);
using SetCharacterPreviewReadyFn = void(__stdcall*)(uint32_t ready);
using SubmitCharacterPreviewFn = void(__stdcall*)(uint32_t width,
                                                  uint32_t height,
                                                  const uint8_t* rgba,
                                                  uint32_t row_pitch,
                                                  uint64_t source_revision);
using SubmitMapPreviewFn = void(__stdcall*)(uint32_t width,
                                            uint32_t height,
                                            const uint8_t* rgba,
                                            uint32_t row_pitch,
                                            uint32_t world_id,
                                            uint64_t source_revision,
                                            const ProjectedMapMarker* markers,
                                            uint32_t marker_count);

}  // namespace dis
